# Phase A re-measurement (native VM): who owns the CoreWindow after a normal activation?
# Stops only the exact XboxPcApp PIDs it enumerates, activates via shell:AppsFolder,
# then records each new instance's command line, parent (and its services) and the
# CoreWindow children of ApplicationFrameWindow frames with their owner PIDs.
$ErrorActionPreference = 'Continue'
Add-Type @"
using System; using System.Text; using System.Collections.Generic; using System.Runtime.InteropServices;
public static class W {
  delegate bool EP(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] static extern bool EnumWindows(EP f, IntPtr l);
  [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr p, EP f, IntPtr l);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint p);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
  static string Cls(IntPtr h) { var c = new StringBuilder(256); GetClassName(h, c, 256); return c.ToString(); }
  public static List<string> Frames() {
    var r = new List<string>();
    EnumWindows((h, l) => {
      if (Cls(h) != "ApplicationFrameWindow") return true;
      var t = new StringBuilder(256); GetWindowText(h, t, 256);
      EnumChildWindows(h, (c, l2) => { if (Cls(c) == "Windows.UI.Core.CoreWindow") { uint p; GetWindowThreadProcessId(c, out p);
          r.Add(String.Format("frame={0:x} title={1} corewindow={2:x} ownerpid={3} visible={4}", h.ToInt64(), t, c.ToInt64(), p, IsWindowVisible(c))); }
        return true; }, IntPtr.Zero);
      return true; }, IntPtr.Zero);
    return r; }
}
"@
$pfn = 'Microsoft.GamingApp_8wekyb3d8bbwe'
$before = @(Get-Process XboxPcApp -ErrorAction SilentlyContinue | % Id)
foreach ($id in $before) { "stopping exact pid $id"; Stop-Process -Id $id -Force }
Start-Sleep 3
"remaining XboxPcApp: " + @(Get-Process XboxPcApp -ErrorAction SilentlyContinue).Count
$t0 = Get-Date
"activate at " + $t0.ToString('HH:mm:ss') + ": shell:AppsFolder\$pfn!Microsoft.Xbox.AppL"
Start-Process "shell:AppsFolder\$pfn!Microsoft.Xbox.AppL"
Start-Sleep 20
foreach ($p in Get-CimInstance Win32_Process -Filter "Name='XboxPcApp.exe'") {
  $par = Get-CimInstance Win32_Process -Filter "ProcessId=$($p.ParentProcessId)"
  $svc = (Get-CimInstance Win32_Service -Filter "ProcessId=$($p.ParentProcessId)" | % Name) -join ','
  "pid=$($p.ProcessId) created=$($p.CreationDate.ToString('HH:mm:ss')) new=$($p.CreationDate -ge $t0) cmd=$($p.CommandLine)"
  "  parent pid=$($p.ParentProcessId) name=$($par.Name) services=$svc"
}
[W]::Frames()
"(activated instance left running; restores the pre-test state)"
