# Runs every probe-manifest.txt line on native Windows and writes one output per line.
# Usage: run-probes-native.ps1 -Source http://10.211.55.2:8765 -Out C:\xodus-probe\paired\native
param([string]$Source = 'http://10.211.55.2:8765', [string]$Out = 'C:\xodus-probe\paired\native', [int]$TimeoutSec = 30)
$ErrorActionPreference = 'Stop'
$bin = Join-Path (Split-Path $Out) 'bin'
New-Item -ItemType Directory -Force $Out, $bin | Out-Null
$lines = (Invoke-WebRequest -UseBasicParsing "$Source/probe-manifest.txt").Content -split "`n" |
    ForEach-Object { $_.Trim() } | Where-Object { $_ -and -not $_.StartsWith('#') }
foreach ($line in $lines) {
    $parts = $line -split '\s+'
    $exe = $parts[0]; $pargs = @($parts | Select-Object -Skip 1)
    $path = Join-Path $bin $exe
    if (-not (Test-Path $path)) { Invoke-WebRequest -UseBasicParsing "$Source/$exe" -OutFile $path }
    $name = ($exe -replace '\.exe$', '') + (($pargs | ForEach-Object { "_$_" }) -join '') + '.txt'
    $dest = Join-Path $Out $name
    $psi = New-Object System.Diagnostics.ProcessStartInfo $path
    $psi.Arguments = $pargs -join ' '
    $psi.WorkingDirectory = $bin
    $psi.UseShellExecute = $false; $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true
    $p = [System.Diagnostics.Process]::Start($psi)
    $so = $p.StandardOutput.ReadToEndAsync(); $se = $p.StandardError.ReadToEndAsync()
    if (-not $p.WaitForExit($TimeoutSec * 1000)) { $p.Kill(); $rc = 'timeout' } else { $rc = '0x{0:x8}' -f $p.ExitCode }
    $text = $so.Result + $se.Result + "EXIT=$rc`n"
    [IO.File]::WriteAllText($dest, ($text -replace "`r`n", "`n"))
    "$name $rc"
}
