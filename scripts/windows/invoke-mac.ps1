[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [ValidateSet(
        "doctor",
        "bootstrap",
        "bootstrap-with-crossover",
        "fmt",
        "clippy",
        "test",
        "build",
        "edge-cdp",
        "overlay-on",
        "overlay-off",
        "overlay-all-on",
        "overlay-all-off",
        "install-gui-launcher",
        "install-hogwarts",
        "launch-hogwarts",
        "hogwarts-status",
        "build-widl",
        "launch-crossover",
        "launch-steam",
        "quit-native-steam",
        "quit-windows-steam",
        "xgameruntime-smoke",
        "xodus-service-smoke",
        "xodus-login",
        "start-xodus-service",
        "stop-xodus-service",
        "prepare-steam-control",
        "bootstrap-status"
    )]
    [string]$Action
)

$repo = '$HOME/src/xodus-macos'
$cargoPath = 'export PATH="$HOME/.cargo/bin:/opt/homebrew/bin:$PATH"'

$commands = @{
    "doctor"                   = "cd $repo && ./scripts/macos/doctor.sh"
    "bootstrap"                = "cd $repo && ./scripts/macos/bootstrap.sh"
    "bootstrap-with-crossover" = "cd $repo && ./scripts/macos/bootstrap.sh --with-crossover"
    "fmt"                      = "cd $repo && $cargoPath && cargo fmt --check --all"
    "clippy"                   = "cd $repo && $cargoPath && cargo clippy --workspace"
    "test"                     = "cd $repo && $cargoPath && cargo test"
    "build"                    = "cd $repo && $cargoPath && cargo build --release --workspace"
    "edge-cdp"                 = "cd $repo && ./scripts/macos/trigger-gui.sh edge-cdp"
    "overlay-on"               = "cd $repo && ./scripts/macos/trigger-gui.sh overlay-on"
    "overlay-off"              = "cd $repo && ./scripts/macos/trigger-gui.sh overlay-off"
    "overlay-all-on"           = "cd $repo && ./scripts/macos/set-crossover-overlay.sh on"
    "overlay-all-off"          = "cd $repo && ./scripts/macos/set-crossover-overlay.sh off"
    "install-gui-launcher"     = "cd $repo && ./scripts/macos/install-gui-launcher.sh"
    "install-hogwarts"         = "cd $repo && ./scripts/macos/trigger-gui.sh install-hogwarts"
    "launch-hogwarts"          = "cd $repo && ./scripts/macos/trigger-gui.sh launch-hogwarts"
    "hogwarts-status"          = "cd $repo && ./scripts/macos/steam-app-status.sh 990080"
    "build-widl"               = "cd $repo && ./scripts/macos/build-widl.sh"
    "launch-crossover"         = "cd $repo && ./scripts/macos/trigger-gui.sh crossover"
    "launch-steam"             = "cd $repo && ./scripts/macos/trigger-gui.sh steam-control"
    "quit-native-steam"        = "cd $repo && ./scripts/macos/trigger-gui.sh quit-native-steam"
    "quit-windows-steam"       = "cd $repo && ./scripts/macos/trigger-gui.sh quit-windows-steam"
    "xgameruntime-smoke"       = "cd $repo && ./scripts/macos/trigger-gui.sh xgameruntime-smoke"
    "xodus-service-smoke"      = "cd $repo && ./scripts/macos/trigger-gui.sh xodus-service-smoke"
    "xodus-login"              = "cd $repo && ./scripts/macos/trigger-gui.sh xodus-login"
    "start-xodus-service"      = "cd $repo && ./scripts/macos/trigger-gui.sh start-xodus-service"
    "stop-xodus-service"       = "cd $repo && ./scripts/macos/trigger-gui.sh stop-xodus-service"
    "prepare-steam-control"    = "cd $repo && ./scripts/macos/create-hogwarts-control.sh"
    "bootstrap-status"         = 'run_id=$(cat "$HOME/xodus-runs/latest-bootstrap"); run_dir="$HOME/xodus-runs/$run_id"; pid=$(cat "$run_dir/pid"); if kill -0 "$pid" 2>/dev/null; then echo "STATUS=RUNNING"; else echo "STATUS=EXITED"; fi; tail -n 80 "$run_dir/bootstrap.log"'
}

& ssh xodus-mac $commands[$Action]
exit $LASTEXITCODE
