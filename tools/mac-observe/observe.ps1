# SPDX-License-Identifier: GPL-3.0-only
# Windows-side driver for the single remote-Mac observability tool (Xodus Observe).
#   .\tools\mac-observe\observe.ps1 install
#   .\tools\mac-observe\observe.ps1 capture window Xodus xodus-home -Pull C:\path\to\dir
#   .\tools\mac-observe\observe.ps1 capture display desktop -Pull C:\path\to\dir
param(
    [Parameter(Mandatory, Position = 0)][ValidateSet('install', 'capture')][string]$Command,
    [Parameter(Position = 1, ValueFromRemainingArguments)][string[]]$Rest,
    [string]$Pull
)
$ErrorActionPreference = 'Stop'
$ssh = @('-o', 'BatchMode=yes', '-o', 'ConnectTimeout=15', '-o', 'Hostname=mbp-dragos.hont.ro', '-o', 'HostKeyAlias=m5.hont.ro')
& ssh @ssh xodus-mac 'mkdir -p ~/xodus-app-tooling/observe'
& scp -q @ssh "$PSScriptRoot\XodusObserve.swift" "$PSScriptRoot\observe.py" 'xodus-mac:xodus-app-tooling/observe/'
$quoted = $Rest | ForEach-Object { "'" + ($_ -replace "'", "") + "'" }
& ssh @ssh xodus-mac ((@('python3', '~/xodus-app-tooling/observe/observe.py', $Command) + $quoted) -join ' ')
if ($Command -eq 'capture' -and $Pull) {
    New-Item -ItemType Directory -Force $Pull | Out-Null
    $name = $Rest[-1]
    # Binary-safe copy; PowerShell redirection would corrupt PNG bytes.
    $copy = "import subprocess,sys;d=subprocess.run(['ssh']+sys.argv[3:]+['xodus-mac','cat ~/xodus-app-tooling/observe/'+sys.argv[1]+'.png'],capture_output=True).stdout;open(sys.argv[2],'wb').write(d);print(len(d),'bytes ->',sys.argv[2])"
    python -c $copy $name (Join-Path $Pull "$name.png") @ssh
}
