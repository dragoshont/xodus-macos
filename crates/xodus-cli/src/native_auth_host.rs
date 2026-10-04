use std::io;
use std::os::fd::OwnedFd;
use std::process::Stdio;
use tokio::net::unix::OwnedWriteHalf;
use tokio::process::{Child, Command};
use tokio::sync::mpsc;
use tokio::task::JoinHandle;
use xodus_management::native_auth::{
    self, Command as HostCommand, CommandFrame, Disposition, HostBinding, HostResult, ResultFrame,
};

fn unavailable() -> io::Error {
    io::Error::other("Owned native authentication host is unavailable")
}

pub struct NativeHost {
    writer: OwnedWriteHalf,
    reader: JoinHandle<()>,
    results: mpsc::Receiver<io::Result<Option<ResultFrame>>>,
    child: Child,
    flow_id: String,
    sent: u64,
    received: u64,
}

impl NativeHost {
    #[cfg(test)]
    pub(crate) fn pid(&self) -> u32 {
        self.child.id().expect("owned neutral helper PID")
    }

    pub fn spawn(binding: &HostBinding, flow_id: &str) -> io::Result<Self> {
        native_auth::verify_binding(binding).map_err(|_| unavailable())?;
        let (parent, child_socket) = std::os::unix::net::UnixStream::pair()?;
        parent.set_nonblocking(true)?;
        let channel = tokio::net::UnixStream::from_std(parent)?;
        let child = Command::new(&binding.executable)
            .stdin(Stdio::from(OwnedFd::from(child_socket)))
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .env_remove("XODUS_LOG")
            .env_remove("RUST_LOG")
            .env_remove("GH_TOKEN")
            .env_remove("GITHUB_TOKEN")
            .kill_on_drop(true)
            .spawn()
            .map_err(|_| unavailable())?;
        let (mut read, writer) = channel.into_split();
        let (sender, results) = mpsc::channel(1);
        let reader = tokio::spawn(async move {
            loop {
                let frame = native_auth::read_optional::<ResultFrame, _>(&mut read).await;
                let terminal = !matches!(frame, Ok(Some(_)));
                if sender.send(frame).await.is_err() || terminal {
                    break;
                }
            }
        });
        Ok(Self {
            writer,
            reader,
            results,
            child,
            flow_id: flow_id.to_owned(),
            sent: 0,
            received: 0,
        })
    }

    pub async fn send(&mut self, message: HostCommand) -> io::Result<()> {
        self.sent = self.sent.checked_add(1).ok_or_else(unavailable)?;
        native_auth::write(
            &mut self.writer,
            &CommandFrame {
                version: native_auth::VERSION,
                flow_id: self.flow_id.clone(),
                session_id: native_auth::SESSION_ID,
                sequence: self.sent,
                message,
            },
        )
        .await
    }

    pub async fn receive(&mut self) -> io::Result<HostResult> {
        let frame = self
            .results
            .recv()
            .await
            .ok_or_else(unavailable)??
            .ok_or_else(unavailable)?;
        let expected = self.received.checked_add(1).ok_or_else(unavailable)?;
        if !native_auth::valid_identity(
            frame.version,
            &frame.flow_id,
            frame.session_id,
            frame.sequence,
        ) || frame.flow_id != self.flow_id
            || frame.sequence != expected
            || frame.reply_to != self.sent
        {
            return Err(unavailable());
        }
        self.received = expected;
        Ok(frame.message)
    }

    pub async fn completed(&mut self) -> io::Result<bool> {
        tokio::time::timeout(std::time::Duration::from_secs(5), async {
            self.send(HostCommand::Close {
                disposition: Disposition::Completed,
            })
            .await?;
            match self.receive().await? {
                HostResult::Closed {
                    disposition: Disposition::Completed,
                } => {}
                HostResult::Cancelled => return Ok(false),
                _ => return Err(unavailable()),
            }
            let exit = self.child.wait().await?;
            if !exit.success() || !matches!(self.results.recv().await, Some(Ok(None))) {
                return Err(unavailable());
            }
            Ok(true)
        })
        .await
        .map_err(|_| unavailable())?
    }

    pub async fn abort(&mut self) -> io::Result<()> {
        self.reader.abort();
        let closed = self.writer.shutdown().await;
        match tokio::time::timeout(std::time::Duration::from_secs(1), self.child.wait()).await {
            Ok(result) => {
                result?;
            }
            Err(_) => self.child.kill().await?,
        }
        match closed {
            Err(error) if error.kind() == io::ErrorKind::NotConnected => Ok(()),
            result => result,
        }
    }
}

impl Drop for NativeHost {
    fn drop(&mut self) {
        self.reader.abort();
    }
}

use tokio::io::AsyncWriteExt;

#[cfg(test)]
pub(crate) mod fixtures {
    use super::*;
    use std::os::unix::fs::PermissionsExt;

    pub fn helper(mode: &str) -> (tempfile::TempDir, HostBinding) {
        let dir = tempfile::tempdir().unwrap();
        let executable = dir.path().join("neutral-host.py");
        let script = format!(
            r#"#!/usr/bin/python3
import socket,struct,json,os,time
s=socket.socket(fileno=0)
mode={mode:?}
seq=0
def take(n):
 b=b''
 while len(b)<n:
  x=s.recv(n-len(b))
  if not x:return None
  b+=x
 return b
def recv():
 p=take(4)
 if p is None:return None
 return json.loads(take(struct.unpack('>I',p)[0]))
def send(frame,msg):
 global seq
 seq+=1
 r={{'version':1,'flowID':frame['flowID'],'sessionID':1,'sequence':seq,'replyTo':frame['sequence'],'message':msg}}
 if mode=='foreign':r['flowID']='00000000-0000-0000-0000-000000000001'
 if mode=='version':r['version']=2
 if mode=='session':r['sessionID']=2
 if mode=='sequence':r['sequence']=2
 if mode=='correlation':r['replyTo']=2
 if mode=='unknown':r['providerPayload']='fixture-forbidden'
 b=json.dumps(r,separators=(',',':')).encode()
 if mode=='duplicate':b=b.replace(b'{{"version":1',b'{{"version":1,"version":1',1)
 if mode=='oversize':s.sendall(struct.pack('>I',262145));return
 if mode=='partial':s.sendall(b'\x00\x00\x00');os._exit(1)
 s.sendall(struct.pack('>I',len(b))+b)
while True:
 f=recv()
 if f is None:break
 k=f['message']['kind']
 if k in ['open','navigate']:
  send(f,{{'kind':'ready'}})
  if mode=='crash':os._exit(23)
  if mode=='cancel':
   send(f,{{'kind':'cancelled'}});os._exit(2)
 elif k=='close':
  if mode=='cancelClose':
   send(f,{{'kind':'cancelled'}});os._exit(2)
  send(f,{{'kind':'closed','disposition':f['message']['disposition']}})
  if mode=='extra':send(f,{{'kind':'ready'}})
  os._exit(7 if mode=='nonzero' else 0)
"#
        );
        std::fs::write(&executable, script).unwrap();
        std::fs::set_permissions(&executable, std::fs::Permissions::from_mode(0o700)).unwrap();
        let executable = executable.canonicalize().unwrap();
        let digest = std::process::Command::new("/usr/bin/shasum")
            .args(["-a", "256"])
            .arg(&executable)
            .output()
            .unwrap();
        assert!(digest.status.success());
        let sha256 = String::from_utf8(digest.stdout)
            .unwrap()
            .split_whitespace()
            .next()
            .unwrap()
            .to_owned();
        (
            dir,
            HostBinding {
                version: 1,
                executable,
                sha256,
            },
        )
    }

    pub async fn open(host: &mut NativeHost, budget: u64) -> io::Result<()> {
        host.send(
            crate::webview::login_request(
                crate::commands::login::CLIENT_ID.to_owned(),
                crate::commands::login::LOGIN_MARKET.to_owned(),
                true,
            )
            .native_open(budget),
        )
        .await
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[tokio::test]
    async fn neutral_helper_completed_ack_eof_clean_exit_is_required() {
        let (_dir, binding) = fixtures::helper("normal");
        let mut host = NativeHost::spawn(&binding, &uuid::Uuid::nil().to_string()).unwrap();
        fixtures::open(&mut host, 1000).await.unwrap();
        assert!(matches!(host.receive().await.unwrap(), HostResult::Ready));
        assert!(host.completed().await.unwrap());
    }

    #[tokio::test]
    async fn cancellation_at_the_close_fence_is_reaped_without_success() {
        let (_dir, binding) = fixtures::helper("cancelClose");
        let mut host = NativeHost::spawn(&binding, &uuid::Uuid::nil().to_string()).unwrap();
        fixtures::open(&mut host, 1000).await.unwrap();
        assert!(matches!(host.receive().await.unwrap(), HostResult::Ready));
        assert!(!host.completed().await.unwrap());
        host.abort().await.unwrap();
        assert!(host.child.try_wait().unwrap().is_some());
    }

    #[tokio::test]
    async fn neutral_helper_rejects_foreign_malformed_oversized_and_uncorrelated_frames() {
        for mode in [
            "foreign",
            "version",
            "session",
            "sequence",
            "correlation",
            "unknown",
            "duplicate",
            "oversize",
            "partial",
        ] {
            let (_dir, binding) = fixtures::helper(mode);
            let mut host = NativeHost::spawn(&binding, &uuid::Uuid::nil().to_string()).unwrap();
            fixtures::open(&mut host, 1000).await.unwrap();
            assert!(host.receive().await.is_err());
            host.abort().await.unwrap();
        }
    }

    #[tokio::test]
    async fn neutral_helper_cancel_crash_and_nonzero_exit_cannot_promote_success() {
        for mode in ["cancel", "crash", "nonzero", "extra"] {
            let (_dir, binding) = fixtures::helper(mode);
            let mut host = NativeHost::spawn(&binding, &uuid::Uuid::nil().to_string()).unwrap();
            fixtures::open(&mut host, 1000).await.unwrap();
            assert!(matches!(host.receive().await.unwrap(), HostResult::Ready));
            if mode == "cancel" {
                assert!(matches!(
                    host.receive().await.unwrap(),
                    HostResult::Cancelled
                ));
            } else {
                assert!(host.completed().await.is_err());
            }
            host.abort().await.unwrap();
        }
    }

    #[tokio::test]
    async fn helper_binding_rejects_hash_version_writable_and_symlink_before_spawn() {
        use std::os::unix::fs::PermissionsExt;
        let (_dir, mut binding) = fixtures::helper("normal");
        let original = binding.sha256.clone();
        binding.sha256 = "0".repeat(64);
        assert!(NativeHost::spawn(&binding, &uuid::Uuid::nil().to_string()).is_err());
        binding.sha256 = original;
        binding.version = 2;
        assert!(NativeHost::spawn(&binding, &uuid::Uuid::nil().to_string()).is_err());
        binding.version = 1;
        std::fs::set_permissions(&binding.executable, std::fs::Permissions::from_mode(0o777))
            .unwrap();
        assert!(NativeHost::spawn(&binding, &uuid::Uuid::nil().to_string()).is_err());
        std::fs::set_permissions(&binding.executable, std::fs::Permissions::from_mode(0o700))
            .unwrap();
        let link = binding.executable.with_extension("link");
        std::os::unix::fs::symlink(&binding.executable, &link).unwrap();
        binding.executable = link;
        assert!(NativeHost::spawn(&binding, &uuid::Uuid::nil().to_string()).is_err());
    }
}
