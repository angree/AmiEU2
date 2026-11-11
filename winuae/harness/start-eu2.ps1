# Start OUR WinUAE guest for the EU2 port ONCE and leave it running; tests go in through job-eu2.ps1.
#   powershell -ExecutionPolicy Bypass -File winuae\harness\start-eu2.ps1 [-Config eu2-ref020.uae]
# Rules (CLAUDE.md): one instance per config, found by its config name on the command line; never
# kill by name; started DETACHED via WMI so an interrupted agent shell cannot take it down; the window
# is shown WITHOUT activation (a minimised WinUAE 2.8.1 pauses); never synthesises host input.
param([string]$Config = "eu2-ref020.uae", [int]$TimeoutSec = 180)
$rt   = "C:\temp\amiga_eu2"
$exe  = Join-Path $rt "uae\winuae-eu2.exe"
$src  = Join-Path (Split-Path -Parent $PSScriptRoot) $Config
$cfg  = Join-Path $rt $Config
Copy-Item $src $cfg -Force
$name = [IO.Path]::GetFileName($cfg)
$mine = @(Get-CimInstance Win32_Process | Where-Object { $_.Name -like 'winuae*' -and $_.CommandLine -like "*$name*" })
if ($mine.Count -gt 0) { Write-Output ("ALREADY RUNNING: pid {0} - use job-eu2.ps1" -f $mine[0].ProcessId); exit 3 }

# Work: comes from the config itself (eu2-aros.uae has its own folder). Hard-coding work\jobs here once
# wiped the reference guest's queue when the AROS guest started (2026-11-08).
$workdir = ((Get-Content $cfg | Where-Object { $_ -match '^filesystem2=.*WORK:Work:' } | Select-Object -First 1) -replace '^.*WORK:Work:', '' -replace ',-?[0-9]+$', '')
if (-not $workdir) { Write-Output "ERROR: no WORK:Work: mount in $cfg"; exit 5 }
$jobs = Join-Path $workdir "jobs"
New-Item -ItemType Directory -Force $jobs | Out-Null
# jobrun numbers its jobs from 1 again after every boot: old results must go
Get-ChildItem $jobs -File -ErrorAction SilentlyContinue | Remove-Item -ErrorAction SilentlyContinue

$si = New-CimInstance -ClassName Win32_ProcessStartup -ClientOnly -Property @{ ShowWindow = [uint16]7 }
$r = Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{
  CommandLine = ('"{0}" -log -f "{1}"' -f $exe, $cfg); CurrentDirectory = (Split-Path $exe); ProcessStartupInformation = $si }
if ($r.ReturnValue -ne 0) { Write-Output "Win32_Process.Create failed: $($r.ReturnValue)"; exit 1 }
Write-Output "started pid $($r.ProcessId) $cfg"
Set-Content (Join-Path $rt "winuae.pid") $r.ProcessId

Add-Type -Name W -Namespace Eu2H -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool ShowWindowAsync(IntPtr h, int c);
public delegate bool EnumProc(IntPtr h, IntPtr l);
[DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint wp);
[DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, System.Text.StringBuilder s, int n);
'@
$deadline = (Get-Date).AddSeconds($TimeoutSec)
$script:shown = $false
while ((Get-Date) -lt $deadline) {
  Start-Sleep -Milliseconds 700
  if (-not $script:shown) {
    $procs = @(Get-CimInstance Win32_Process | Where-Object { $_.Name -like 'winuae*' -and $_.CommandLine -like "*$name*" } | ForEach-Object { [uint32]$_.ProcessId })
    $cb = [Eu2H.W+EnumProc]{ param($h, $l)
      [uint32]$wp = 0; [Eu2H.W]::GetWindowThreadProcessId($h, [ref]$wp) | Out-Null
      if ($procs -contains $wp) { $cn = New-Object System.Text.StringBuilder 64; [Eu2H.W]::GetClassName($h, $cn, 64) | Out-Null
        if ($cn.ToString() -eq 'PCsuxRox') { [Eu2H.W]::ShowWindowAsync($h, 4) | Out-Null; $script:shown = $true } }
      return $true }
    [Eu2H.W]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
  }
  if (Test-Path (Join-Path $jobs "jobrun.up")) { Write-Output "guest up: jobrun is polling Work:jobs (window shown: $script:shown)"; exit 0 }
}
Write-Output "TIMEOUT: no Work:jobs/jobrun.up after $TimeoutSec s (guest left running)"
exit 1
