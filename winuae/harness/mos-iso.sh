S=/home/angree/mosiso; rm -rf $S; mkdir -p $S/eu2/data/music
O=/home/angree/eu2out
cp /mnt/c/temp/amiga_eu2/work/eu2 $S/eu2/
cp $O/*.pak $O/conversion.txt $O/music/index.txt $S/eu2/data/ 2>/dev/null; mv $S/eu2/data/index.txt $S/eu2/data/music/
python3 tools/paktool.py get $O/text.pak music/14.txt /home/angree/l14.txt >/dev/null 2>&1 || python3 tools/paktool.py get $O/text.pak music/14.txt > /home/angree/l14.txt
tr -d '\r' < /home/angree/l14.txt | while read b; do [ -n "$b" ] && grep " $b\$" $O/music/index.txt | cut -d' ' -f1; done | sort -u > /home/angree/l14f.txt
for f in $(cat /home/angree/l14f.txt); do cp $O/music/$f $S/eu2/data/music/; done
printf 'gfx=rtg\ntitle_ms=0\nautoexit_ms=200000\ncountry=POR\n' > $S/eu2/eu2.cfg
printf '3000 key 64\n60000 key 1\n70000 click 165 18\n75000 click 150 136\n' > $S/eu2/autoinput.txt
du -sh $S/eu2 $S/eu2/data/music; ls $S/eu2/data/music | wc -l
genisoimage -quiet -V EU2GRA -J -R -o /mnt/e/morphos_eu2/iso/eu2.iso $S && ls -la /mnt/e/morphos_eu2/iso/eu2.iso
