use std::process::ExitCode;
use std::sync::{
    Arc,
    atomic::{AtomicBool, Ordering},
};

use async_trait::async_trait;
use tao::event::{Event, WindowEvent};
use tao::event_loop::{ControlFlow, EventLoopBuilder};
use tao::platform::run_return::EventLoopExtRunReturn;
use tao::window::WindowBuilder;
use wry::WebViewBuilder;
use xodus::xal::{AuthPromptCallback, AuthPromptData, url::Url};
use xodus_management::adapter::{ConsentHandoff, MAX_AUTH_HANDOFF_BYTES};

fn private_channel(fd: std::os::fd::OwnedFd) -> std::io::Result<std::os::unix::net::UnixStream> {
    let channel = std::os::unix::net::UnixStream::from(fd);
    if !channel.peer_addr()?.is_unnamed() || !channel.local_addr()?.is_unnamed() {
        return Err(std::io::Error::other(
            "Expected inherited anonymous parent channel",
        ));
    }
    channel.set_write_timeout(Some(std::time::Duration::from_secs(5)))?;
    Ok(channel)
}

fn write_handoff(
    channel: &mut std::os::unix::net::UnixStream,
    outcome: &ConsentHandoff,
) -> std::io::Result<()> {
    use std::io::Write;
    let bytes = serde_json::to_vec(outcome)
        .map_err(|_| std::io::Error::other("Session serialization failed"))?;
    if bytes.len() > MAX_AUTH_HANDOFF_BYTES {
        return Err(std::io::Error::other(
            "Session exceeds private handoff bound",
        ));
    }
    channel.write_all(&(bytes.len() as u32).to_be_bytes())?;
    channel.write_all(&bytes)?;
    channel.flush()
}

struct NativeConsent {
    cancelled: Arc<AtomicBool>,
}

#[derive(Clone)]
enum ConsentEvent {
    Redirect(Url),
}

#[async_trait]
impl AuthPromptCallback for NativeConsent {
    async fn call(
        &self,
        prompt: AuthPromptData,
    ) -> Result<Option<Url>, Box<dyn std::error::Error>> {
        let initial = prompt.authentication_url();
        if !prompt.expect_url()
            || initial.scheme() != "https"
            || initial.host_str() != Some("login.live.com")
        {
            return Err(std::io::Error::other("Unsupported consent prompt").into());
        }
        let mut event_loop = EventLoopBuilder::<ConsentEvent>::with_user_event().build();
        let proxy = event_loop.create_proxy();
        let window = WindowBuilder::new()
            .with_title("Connect Xodus to Xbox")
            .with_inner_size(tao::dpi::LogicalSize::new(520.0, 720.0))
            .build(&event_loop)?;
        let webview = WebViewBuilder::new()
            .with_url(initial.as_str())
            .with_navigation_handler(move |value| {
                let Ok(url) = Url::parse(&value) else {
                    return false;
                };
                if is_callback(&url) {
                    let _ = proxy.send_event(ConsentEvent::Redirect(url));
                    false
                } else {
                    url.scheme() == "https"
                }
            })
            .build(&window)?;
        let mut redirect = None;
        event_loop.run_return(|event, _, control| {
            *control = ControlFlow::Wait;
            match event {
                Event::UserEvent(ConsentEvent::Redirect(url)) => {
                    redirect = Some(url);
                    *control = ControlFlow::Exit;
                }
                Event::WindowEvent {
                    event: WindowEvent::CloseRequested,
                    ..
                } => *control = ControlFlow::Exit,
                _ => {}
            }
        });
        drop(webview);
        drop(window);
        if redirect.is_none() {
            self.cancelled.store(true, Ordering::SeqCst);
        }
        Ok(redirect)
    }
}

fn is_callback(url: &Url) -> bool {
    url.scheme() == "https"
        && url.host_str() == Some("login.live.com")
        && url.path() == "/oauth20_desktop.srf"
        && url.port().is_none()
        && url.username().is_empty()
        && url.password().is_none()
}

pub async fn run(flow_id: String) -> ExitCode {
    std::panic::set_hook(Box::new(|_| {}));
    if uuid::Uuid::parse_str(&flow_id).is_err()
        || !xodus::secrets::management_native_keychain_enabled()
    {
        return ExitCode::FAILURE;
    }
    use std::os::fd::AsFd;
    let Ok(fd) = rustix::io::dup(std::io::stdin().as_fd()) else {
        return ExitCode::FAILURE;
    };
    let Ok(mut channel) = private_channel(fd) else {
        return ExitCode::FAILURE;
    };
    let cancelled = Arc::new(AtomicBool::new(false));
    let result = xodus::auth::start_new_session(NativeConsent {
        cancelled: cancelled.clone(),
    })
    .await;
    let (outcome, code) = match result {
        Ok(session) if xodus_management::adapter::xal_session_valid(&session) => (
            ConsentHandoff::Completed {
                session: Box::new(session),
            },
            ExitCode::SUCCESS,
        ),
        _ if cancelled.load(Ordering::SeqCst) => (ConsentHandoff::Cancelled, ExitCode::from(2)),
        _ => (ConsentHandoff::Failed, ExitCode::FAILURE),
    };
    if write_handoff(&mut channel, &outcome).is_ok() {
        code
    } else {
        ExitCode::FAILURE
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn handoff_is_anonymous_bounded_private_socket_not_public_output() {
        use std::io::Read;
        let (parent, child) = std::os::unix::net::UnixStream::pair().unwrap();
        let mut channel = private_channel(child.into()).unwrap();
        write_handoff(&mut channel, &ConsentHandoff::Cancelled).unwrap();
        drop(channel);
        let mut bytes = Vec::new();
        (&parent).read_to_end(&mut bytes).unwrap();
        let length = u32::from_be_bytes(bytes[..4].try_into().unwrap()) as usize;
        assert_eq!(length, bytes.len() - 4);
        assert!(matches!(
            serde_json::from_slice::<ConsentHandoff>(&bytes[4..]).unwrap(),
            ConsentHandoff::Cancelled
        ));
        let file = tempfile::tempfile().unwrap();
        assert!(private_channel(file.into()).is_err());
    }

    #[test]
    fn callback_is_exact_https_desktop_origin() {
        assert!(is_callback(
            &Url::parse("https://login.live.com/oauth20_desktop.srf#state=fixture").unwrap()
        ));
        for value in [
            "http://login.live.com/oauth20_desktop.srf",
            "https://login.live.com.attacker.invalid/oauth20_desktop.srf",
            "https://login.live.com/other",
            "https://user@login.live.com/oauth20_desktop.srf",
            "https://login.live.com:8443/oauth20_desktop.srf",
        ] {
            assert!(!is_callback(&Url::parse(value).unwrap()), "{value}");
        }
    }
}
