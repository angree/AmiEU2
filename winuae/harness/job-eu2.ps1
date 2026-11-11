# Run an AmigaDOS script inside the running guest and print its output.
#   job-eu2.ps1 -Script "Stack 2000000`nWork:eu2" [-TimeoutSec 300] [-WaitFile eu2.log -WaitFor "text"]
# The script is written as next.tmp and renamed to next (jobrun only ever sees a complete file).
# LF line endings only - AmigaDOS does not run CRLF scripts.
param([Parameter(Mandatory)][string]$Script, [int]$TimeoutSec = 300)
$jobs = "C:\temp\amiga_eu2\work\jobs"
if (-not (Test-Path (Join-Path $jobs "jobrun.up"))) { Write-Output "ERROR: guest not up (start-eu2.ps1)"; exit 2 }
if (Test-Path (Join-Path $jobs "next")) { Write-Output "ERROR: a job is still queued"; exit 2 }
$lastf = Join-Path $jobs "last"
$before = if (Test-Path $lastf) { [int](Get-Content $lastf) } else { 0 }
$tmp = Join-Path $jobs "next.tmp"
[IO.File]::WriteAllText($tmp, ($Script -replace "`r", "") + "`n")
Move-Item $tmp (Join-Path $jobs "next") -Force
$n = $before + 1
$done = Join-Path $jobs "job$n.done"
$deadline = (Get-Date).AddSeconds($TimeoutSec)
while ((Get-Date) -lt $deadline) {
  Start-Sleep -Milliseconds 500
  if (Test-Path $done) {
    Write-Output ("--- job{0} {1}" -f $n, (Get-Content $done))
    $log = Join-Path $jobs "job$n.log"
    if (Test-Path $log) { Get-Content $log }
    exit 0
  }
}
Write-Output "TIMEOUT: job$n not done after $TimeoutSec s (heartbeat: $(Get-Content (Join-Path $jobs 'heartbeat') -ErrorAction SilentlyContinue))"
exit 1
