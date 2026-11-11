#!/bin/sh
# vm-shot.sh NAME - screenshot of the original to C:\temp\amiga_eu2\shots\orig_NAME.png
. "$(dirname "$0")/vm.inc"
$VSSH 'DISPLAY=:77 import -window root png:-' > "/mnt/c/temp/amiga_eu2/shots/orig_${1:-x}.png" && echo "shots/orig_${1:-x}.png"
