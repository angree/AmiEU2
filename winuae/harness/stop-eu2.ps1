# Close OUR guest (matched by config name on the command line) with WM_CLOSE. Never by process name,
# never -Force: a killed WinUAE can corrupt a hardfile, and other guests on this PC belong to others.
param([string]$Config = "eu2-ref020.uae", [int]$TimeoutSec = 30)
$mine = @(Get-CimInstance Win32_Process | Where-Object { $_.Name -like 'winuae*' -and $_.CommandLine -like "*$Config*" })
if ($mine.Count -eq 0) { Write-Output "not running"; exit 0 }
foreach ($m in $mine) {
  $p = Get-Process -Id $m.ProcessId -ErrorAction SilentlyContinue
  if ($p -and $p.MainWindowHandle -ne 0) { [void]$p.CloseMainWindow(); Write-Output "WM_CLOSE -> pid $($p.Id)" }
}
$deadline = (Get-Date).AddSeconds($TimeoutSec)
while ((Get-Date) -lt $deadline) {
  Start-Sleep -Milliseconds 500
  $left = @(Get-CimInstance Win32_Process | Where-Object { $_.Name -like 'winuae*' -and $_.CommandLine -like "*$Config*" })
  if ($left.Count -eq 0) { Write-Output "stopped"; exit 0 }
}
Write-Output "STILL RUNNING after $TimeoutSec s - not forcing it; ask the user"
exit 1
