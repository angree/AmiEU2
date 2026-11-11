cat > $HOME/eu2run.sh <<'X'
export PATH=/opt/wine-stable/bin:$PATH DISPLAY=:77 WINEDEBUG=-all WINEDLLOVERRIDES="mscoree=d;mshtml=d"
lsmod | grep -q snd_dummy || modprobe snd-dummy
pgrep -x Xvfb >/dev/null || (setsid nohup Xvfb :77 -screen 0 800x600x16 -nolisten tcp >/root/xvfb.log 2>&1 </dev/null &)
sleep 1
wineserver -k 2>/dev/null; sleep 1
timeout 300 wineboot -u >/root/wb.log 2>&1
wine reg add 'HKCU\Software\Wine\Direct3D' /v renderer /d gdi /f >/dev/null 2>&1
wine reg add 'HKCU\Software\Wine\Drivers' /v Audio /d alsa /f >/dev/null 2>&1
cd /root/eu2 && setsid nohup wine eu2.exe >/root/eu2.log 2>&1 </dev/null &
sleep 2; wine --version; echo started
X
scp -q -i $HOME/.ssh/eu2vm -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -P 2299 $HOME/eu2run.sh root@127.0.0.1:/root/eu2run.sh
ssh -q -i $HOME/.ssh/eu2vm -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -p 2299 root@127.0.0.1 'sh /root/eu2run.sh'
