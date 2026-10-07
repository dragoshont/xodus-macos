# Phase A plain-launch control (native VM). Runs audit-plainlaunch.exe from an
# unpackaged context and records every XboxPcApp instance with its creation time
# relative to the probe start, so instances not started by the probe can be told apart.
# Only exact enumerated PIDs are stopped. Afterwards, normal activation is restored.
$ErrorActionPreference = 'Continue'
$pfn = 'Microsoft.GamingApp_8wekyb3d8bbwe'
$exe = (Get-AppxPackage Microsoft.GamingApp).InstallLocation + '\XboxPcApp.exe'
function Snap($label) {
  foreach ($p in Get-CimInstance Win32_Process -Filter "Name='XboxPcApp.exe'") {
    $svc = (Get-CimInstance Win32_Service -Filter "ProcessId=$($p.ParentProcessId)" | % Name) -join ','
    "[$label] pid=$($p.ProcessId) created=$($p.CreationDate.ToString('HH:mm:ss.fff')) parent=$($p.ParentProcessId) parentServices=$svc cmd=$($p.CommandLine)"
  }
}
foreach ($id in @(Get-Process XboxPcApp -ErrorAction SilentlyContinue | % Id)) { "stopping exact pid $id"; Stop-Process -Id $id -Force }
Start-Sleep 5
Snap 'before'
$t0 = Get-Date
"probe start " + $t0.ToString('HH:mm:ss.fff')
& C:\xprobe\audit-plainlaunch.exe $exe C:\xprobe\plain.txt 30
"probe exit=$LASTEXITCODE end " + (Get-Date).ToString('HH:mm:ss.fff')
Get-Content C:\xprobe\plain.txt
Snap 'after'
foreach ($id in @(Get-Process XboxPcApp -ErrorAction SilentlyContinue | % Id)) { "stopping exact pid $id"; Stop-Process -Id $id -Force }
Start-Sleep 3
Start-Process "shell:AppsFolder\$pfn!Microsoft.Xbox.AppL"
"restored normal activation"
