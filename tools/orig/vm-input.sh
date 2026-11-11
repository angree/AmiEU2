#!/bin/sh
# vm-input.sh ACTION... - input to the original on the VM's display :77 only
#   click X Y | rclick X Y | move X Y | key NAME | type TEXT | wait SECONDS
. "$(dirname "$0")/vm.inc"
cmd=""
while [ $# -gt 0 ]; do
	case "$1" in
	click)  cmd="$cmd xdotool mousemove $2 $3 sleep 0.3 click 1;"; shift 3 ;;
	rclick) cmd="$cmd xdotool mousemove $2 $3 sleep 0.3 click 3;"; shift 3 ;;
	move)   cmd="$cmd xdotool mousemove $2 $3;"; shift 3 ;;
	key)    cmd="$cmd xdotool key $2;"; shift 2 ;;
	type)   cmd="$cmd xdotool type '$2';"; shift 2 ;;
	wait)   cmd="$cmd sleep $2;"; shift 2 ;;
	*) echo "unknown $1"; exit 1 ;;
	esac
done
$VSSH "export DISPLAY=:77; $cmd"
