$ErrorActionPreference = 'Continue'
Add-Type @"
using System; using System.Text; using System.Collections.Generic; using System.Runtime.InteropServices;
public static class W {
  delegate bool EP(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] static extern bool EnumWindows(EP f, IntPtr l);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint p);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
  public static List<string> List(uint pid) {
    var r = new List<string>();
    EnumWindows((h, l) => { uint p; GetWindowThreadProcessId(h, out p);
      if (p == pid) { var c = new StringBuilder(256); var t = new StringBuilder(256);
        GetClassName(h, c, 256); GetWindowText(h, t, 256);
        r.Add(String.Format("  hwnd={0:x} visible={1} class={2} title={3}", h.ToInt64(), IsWindowVisible(h), c, t)); }
      return true; }, IntPtr.Zero);
    return r; }
}
"@
$pfn = 'Microsoft.GamingApp_8wekyb3d8bbwe'
$loc = (Get-AppxPackage Microsoft.GamingApp).InstallLocation
function Report($label, $wait) {
  Start-Sleep $wait
  $procs = Get-Process XboxPcApp -ErrorAction SilentlyContinue
  "[$label] after ${wait}s: XboxPcApp processes: $(@($procs).Count)"
  foreach ($p in $procs) {
    "  pid=$($p.Id) session=$($p.SessionId) started=$($p.StartTime.ToString('HH:mm:ss')) mainwnd=$($p.MainWindowHandle) responding=$($p.Responding)"
    [W]::List([uint32]$p.Id)
  }
  $afh = Get-Process ApplicationFrameHost -ErrorAction SilentlyContinue
  foreach ($a in $afh) { "  ApplicationFrameHost pid=$($a.Id) windows:"; [W]::List([uint32]$a.Id) | Select-String 'Xbox' }
}
"desktop session=" + (Get-Process -Id $PID).SessionId
Get-Process XboxPcApp -ErrorAction SilentlyContinue | % { "stopping pre-existing exact pid $($_.Id)"; Stop-Process -Id $_.Id -Force }
Start-Sleep 3
"== Control 1: plain process creation with package identity (no activation) =="
try {
  Invoke-CommandInDesktopPackage -PackageFamilyName $pfn -AppId 'Microsoft.Xbox.AppL' -Command "$loc\XboxPcApp.exe" -PreventBreakaway
  "Invoke-CommandInDesktopPackage returned"
} catch { "Invoke-CommandInDesktopPackage failed: $($_.Exception.Message)" }
Report 'plain' 25
Get-Process XboxPcApp -ErrorAction SilentlyContinue | % { "stopping control-1 exact pid $($_.Id)"; Stop-Process -Id $_.Id -Force }
Start-Sleep 3
"== Control 2: normal activation (shell:AppsFolder) =="
Start-Process "shell:AppsFolder\$pfn!Microsoft.Xbox.AppL"
Report 'activated' 25
"(control 2 left running; restores the pre-test state)"
