# runq.sh <name> "<extra cfg>" "<autoinput>" : one test run in the PERSISTENT guest eu2-rtgtest.
# WinUAE is started once (start_not_captured, iconified) and never restarted: Work:run is a loop that
# starts Work:eu2 whenever Work:go appears (user 2026-11-11: restarting WinUAE kept grabbing their mouse).
# Base cfg: GFX (aga|rtg, default rtg 800x600x8), autoexit after EXIT ms (default 90000). Shot -> <name>.png.
W=/c/temp/amiga_eu2/work-rtgtest
cd $W || exit 1
if [ -f started ] && [ ! -f done ]; then for i in $(seq 1 60); do [ -f done ] && break; sleep 2; done; fi   # the last run still going
rm -f autoinput.txt eu2setup.test go done started
cp /c/temp/amiga_eu2/work/eu2 .
if [ "${GFX:-rtg}" = "aga" ]; then
  printf 'gfx=aga\ntitlebar=1\npointer=system\n' > eu2.cfg
else
  printf 'gfx=rtg\nrtgmode=800x600\nrtgdepth=8\ntitlebar=0\npointer=system\n' > eu2.cfg
fi
printf 'menu=0\ntitle_ms=0\nintro=0\nsound=0\nanim=0\nautoexit_ms=%s\n' "${EXIT:-90000}" >> eu2.cfg
printf "$2" >> eu2.cfg
[ -n "$3" ] && printf "$3" > autoinput.txt
printf 'Stack 2000000\nLab loop\nIf EXISTS Work:go\n  Delete >NIL: Work:go\n  Work:eu2\n  Echo >Work:done "done"\nEndIf\nWait 1\nSkip loop BACK\n' > run
: > eu2.log
if ! powershell -NoProfile -Command "if (Get-CimInstance Win32_Process -Filter \"Name like 'winuae%'\" | Where-Object { \$_.CommandLine -like '*eu2-rtgtest.uae*' }) { exit 0 } else { exit 1 }"; then
  powershell -NoProfile -Command "Start-Process powershell -ArgumentList '-NoProfile','-File','I:/GITHUB/Ami_EuropaUniversalis2/winuae/harness/start-eu2.ps1','-Config','eu2-rtgtest.uae' -WindowStyle Hidden"
  sleep 25
fi
: > go; : > started
for i in $(seq 1 60); do grep -aq "${WAITFOR:-map first}" eu2.log && break; grep -aq "TRAP" eu2.log && break; sleep 3; done
sleep ${SETTLE:-8}
powershell -NoProfile -File "I:/GITHUB/Ami_EuropaUniversalis2/winuae/harness/shot-eu2.ps1" -Config eu2-rtgtest.uae -Out C:/temp/amiga_eu2/$1.png
grep -a "TRAP" eu2.log | head -3
