#!/bin/sh
# Amiga build of AmiEU2 (68k, one binary for AmigaOS 3.x / AROS 68k / MorphOS).
#   wsl sh /mnt/c/temp/amiga_eu2/w.sh build/build.sh            (build + deploy to C:\temp\amiga_eu2\work)
#   ... build/build.sh clean                                     (objects from scratch)
#   DATA=1 ... build/build.sh                                    (also copy the converted data -> the deploy folder data)
# Rules (CLAUDE.md / !instrukcje 10-68k-2D.md): -mcpu=68020 -msoft-float, never -m68040/-lc/-lpthread,
# NEVER strip, -fbbb without i and e, fp_*.c ahead of -lm with -fno-builtin, check libm881 in the link.
set -e
REPO=$(cd "$(dirname "$0")/.." && pwd)
WORK=$HOME/eu2-build
OBJ=$WORK/obj
DEPLOY=${DEPLOY:-/mnt/c/temp/amiga_eu2/work}
export PATH=/opt/amiga/bin:$PATH
[ "$1" = "clean" ] && rm -rf "$WORK"
mkdir -p "$OBJ" "$DEPLOY"

# sources are mirrored by CONTENT: on this SMB share an edit does not move the mtime WSL sees
rsync -rc --delete --exclude cgx-include "$REPO/native/" "$WORK/native/"
rsync -rc --delete "$REPO/native/cgx-include/" "$WORK/native/cgx-include/"
rsync -rc --delete "$REPO/src/" "$WORK/src/"

CPU="-mcpu=68020 -msoft-float -noixemul"
OPT="-O1 -fomit-frame-pointer -fbbb=abcfhlmnprsz0"
WARN="-Wall -Wno-unused-function -Wno-pointer-sign"
CFLAGS="$CPU $OPT $WARN -std=gnu99 -D__AMIGA__ -I$WORK/native -I$WORK/native/cgx-include -I$WORK/src"
SUM=$(printf '%s' "$CFLAGS" | md5sum | cut -d' ' -f1)
if [ "$(cat "$WORK/flags.sum" 2>/dev/null)" != "$SUM" ]; then rm -f "$OBJ"/*.o "$OBJ"/*.d; echo "$SUM" > "$WORK/flags.sum"; fi

cc() {   # cc <dir> <file> [extra flags]
	o="$OBJ/$(echo "$2" | tr / _).o"
	if [ -f "$o" ] && [ ! "$1/$2" -nt "$o" ] && [ -f "$o.d" ] && ! (sed -e 's/\\$//' -e 's/^[^:]*://' "$o.d" | tr ' ' '\n' | grep -v '^$' | while read -r d; do [ -f "$d" ] && [ "$d" -nt "$o" ] && echo x; done | grep -q x); then return 0; fi
	m68k-amigaos-gcc $CFLAGS $3 -MMD -MP -MF "$o.d" -c "$1/$2" -o "$o" 2>"$o.warn" || { cat "$o.warn"; echo "COMPILE FAILED: $2"; exit 1; }
	[ -s "$o.warn" ] && { echo "--- $2"; cat "$o.warn"; } || true
}

for f in fp_conv.c fp_single.c fp_double.c; do cc "$WORK/native" $f -fno-builtin; done
for f in amiga_gfx.c amiga_trap.c amiga_stack.c amiga_uclock.c eu2_sys.c amiga_audio.c; do cc "$WORK/native" $f; done
cc "$WORK/src" ai.c -O2                     # the AI's inner loops want its small helpers inlined
for f in main_amiga.c pak.c eui.c script.c world.c calendar.c events.c economy.c military.c peace.c diplo.c savegame.c frontend.c text.c sound.c lightmap.c mapview.c mapmode.c envoys.c ledger.c monarchs.c leaders.c revolts.c missions.c buildings.c options.c religion.c font.c sprite.c units.c ui.c tutorial.c test_script.c test_events.c test_econ.c test_war.c test_peace.c test_diplo.c test_save.c test_world.c test_lightmap.c; do cc "$WORK/src" $f; done
if [ ! -f "$OBJ/c2p_glue.o" ] || [ "$WORK/native/c2p_glue.s" -nt "$OBJ/c2p_glue.o" ]; then
	vasmm68k_mot -quiet -Fhunk -m68020 -no-opt -I/opt/amiga/m68k-amigaos/ndk-include -I"$WORK/native" -o "$OBJ/c2p_glue.o" "$WORK/native/c2p_glue.s"
fi
rm -f "$OBJ/main_amiga.c.o"   # the built: stamp must belong to THIS build
cc "$WORK/src" main_amiga.c

GAME="main_amiga pak eui script world calendar events economy military peace diplo ai savegame frontend text sound lightmap mapview mapmode envoys ledger monarchs leaders revolts missions buildings options religion font sprite units ui tutorial"
NAT="amiga_gfx amiga_trap amiga_stack amiga_uclock eu2_sys amiga_audio"
OBJS=""
for f in $GAME; do OBJS="$OBJS $OBJ/$f.c.o"; done
for f in $NAT; do OBJS="$OBJS $OBJ/$f.c.o"; done
# fp_*.o BEFORE -lm: our __mulsf3/__divsf3 win over the broken ROM IEEE entries
OBJS="$OBJS $OBJ/c2p_glue.o $OBJ/fp_conv.c.o $OBJ/fp_single.c.o $OBJ/fp_double.c.o"
m68k-amigaos-gcc $CPU -Wl,-Map,"$WORK/eu2.map" -Wl,-t -o "$WORK/eu2" $OBJS -lamiga -lm > "$WORK/link.txt" 2>&1 || { cat "$WORK/link.txt"; echo "LINK FAILED"; exit 1; }
if grep -q libm881 "$WORK/link.txt"; then echo "FPU GATE: libm881 was linked - refusing"; exit 1; fi
grep "duplicate section" "$WORK/link.txt" | head -3 || true

# host-vs-Amiga test binaries (never shipped)
m68k-amigaos-gcc $CPU -o "$WORK/test_script" $OBJ/test_script.c.o $OBJ/script.c.o $OBJ/pak.c.o $OBJ/fp_conv.c.o $OBJ/fp_single.c.o $OBJ/fp_double.c.o -lm || { echo "LINK FAILED test_script"; exit 1; }
cp "$WORK/test_script" "$DEPLOY/test_script"
m68k-amigaos-gcc $CPU -o "$WORK/test_world" $OBJ/test_world.c.o $OBJ/world.c.o $OBJ/script.c.o $OBJ/pak.c.o $OBJ/fp_conv.c.o $OBJ/fp_single.c.o $OBJ/fp_double.c.o -lm || { echo "LINK FAILED test_world"; exit 1; }
cp "$WORK/test_world" "$DEPLOY/test_world"
m68k-amigaos-gcc $CPU -o "$WORK/test_events" $OBJ/test_events.c.o $OBJ/text.c.o $OBJ/events.c.o $OBJ/calendar.c.o $OBJ/world.c.o $OBJ/script.c.o $OBJ/pak.c.o $OBJ/fp_conv.c.o $OBJ/fp_single.c.o $OBJ/fp_double.c.o -lm || { echo "LINK FAILED test_events"; exit 1; }
cp "$WORK/test_events" "$DEPLOY/test_events"
m68k-amigaos-gcc $CPU -o "$WORK/test_econ" $OBJ/test_econ.c.o $OBJ/economy.c.o $OBJ/events.c.o $OBJ/calendar.c.o $OBJ/world.c.o $OBJ/script.c.o $OBJ/pak.c.o $OBJ/fp_conv.c.o $OBJ/fp_single.c.o $OBJ/fp_double.c.o -lm || { echo "LINK FAILED test_econ"; exit 1; }
cp "$WORK/test_econ" "$DEPLOY/test_econ"
m68k-amigaos-gcc $CPU -o "$WORK/test_war" $OBJ/test_war.c.o $OBJ/military.c.o $OBJ/buildings.c.o $OBJ/calendar.c.o $OBJ/world.c.o $OBJ/script.c.o $OBJ/pak.c.o $OBJ/fp_conv.c.o $OBJ/fp_single.c.o $OBJ/fp_double.c.o -lm || { echo "LINK FAILED test_war"; exit 1; }
cp "$WORK/test_war" "$DEPLOY/test_war"
m68k-amigaos-gcc $CPU -o "$WORK/test_peace" $OBJ/test_peace.c.o $OBJ/peace.c.o $OBJ/military.c.o $OBJ/buildings.c.o $OBJ/calendar.c.o $OBJ/world.c.o $OBJ/script.c.o $OBJ/pak.c.o $OBJ/fp_conv.c.o $OBJ/fp_single.c.o $OBJ/fp_double.c.o -lm || { echo "LINK FAILED test_peace"; exit 1; }
cp "$WORK/test_peace" "$DEPLOY/test_peace"
m68k-amigaos-gcc $CPU -o "$WORK/test_diplo" $OBJ/test_diplo.c.o $OBJ/diplo.c.o $OBJ/ai.c.o $OBJ/peace.c.o $OBJ/revolts.c.o $OBJ/religion.c.o $OBJ/military.c.o $OBJ/buildings.c.o $OBJ/calendar.c.o $OBJ/world.c.o $OBJ/script.c.o $OBJ/pak.c.o $OBJ/fp_conv.c.o $OBJ/fp_single.c.o $OBJ/fp_double.c.o -lm || { echo "LINK FAILED test_diplo"; exit 1; }
cp "$WORK/test_diplo" "$DEPLOY/test_diplo"
m68k-amigaos-gcc $CPU -o "$WORK/test_save" $OBJ/test_save.c.o $OBJ/leaders.c.o $OBJ/missions.c.o $OBJ/text.c.o $OBJ/savegame.c.o $OBJ/envoys.c.o $OBJ/revolts.c.o $OBJ/religion.c.o $OBJ/diplo.c.o $OBJ/ai.c.o $OBJ/peace.c.o $OBJ/military.c.o $OBJ/buildings.c.o $OBJ/economy.c.o $OBJ/events.c.o $OBJ/calendar.c.o $OBJ/world.c.o $OBJ/script.c.o $OBJ/pak.c.o $OBJ/fp_conv.c.o $OBJ/fp_single.c.o $OBJ/fp_double.c.o -lm || { echo "LINK FAILED test_save"; exit 1; }
cp "$WORK/test_save" "$DEPLOY/test_save"
m68k-amigaos-gcc $CPU -o "$WORK/test_lightmap" $OBJ/test_lightmap.c.o $OBJ/lightmap.c.o $OBJ/pak.c.o $OBJ/fp_conv.c.o $OBJ/fp_single.c.o $OBJ/fp_double.c.o -lm || { echo "LINK FAILED test_lightmap"; exit 1; }
cp "$WORK/test_lightmap" "$DEPLOY/test_lightmap"
m68k-amigaos-gcc $CPU -O1 -I$WORK/native/cgx-include -o "$WORK/jobrun" "$WORK/native/jobrun.c" 2>"$WORK/jobrun.warn" || { cat "$WORK/jobrun.warn"; exit 1; }


# eu2setup: the settings tool (GadTools); no game code, no maths
cc "$WORK/src" setup_amiga.c
m68k-amigaos-gcc $CPU -o "$WORK/eu2setup" "$OBJ/setup_amiga.c.o" -lamiga > "$WORK/link_setup.txt" 2>&1 || { cat "$WORK/link_setup.txt"; echo "LINK FAILED eu2setup"; exit 1; }
cp "$WORK/eu2setup" "$DEPLOY/eu2setup"
# Workbench icons (our own drawing, tools/mkicon.py: WBTOOL, 2 MB stack)
python3 tools/mkicon.py "$DEPLOY/eu2.info" game > /dev/null && python3 tools/mkicon.py "$DEPLOY/eu2setup.info" setup > /dev/null || { echo "ICONS FAILED"; exit 1; }
# NEVER strip (a stripped Hunk halts the machine)
cp "$WORK/eu2" "$DEPLOY/eu2"
mkdir -p "$DEPLOY/fonts" && rm -f "$DEPLOY"/fonts/*.euf && cp "$REPO"/fonts/*.euf "$DEPLOY/fonts/"   # all faces: X.Org misc-fixed, public domain (fonts/LICENSE-misc-fixed.txt)
cmp -s "$WORK/jobrun" "$DEPLOY/jobrun" || cp "$WORK/jobrun" "$DEPLOY/jobrun"
printf 'Stack 2000000\nWork:jobrun\n' > "$DEPLOY/run.new"
cmp -s "$DEPLOY/run.new" "$DEPLOY/run" && rm "$DEPLOY/run.new" || mv "$DEPLOY/run.new" "$DEPLOY/run"
if [ "$DATA" = "1" ]; then
	mkdir -p "$DEPLOY/data"
	rsync -r --size-only /home/angree/eu2out/ "$DEPLOY/data/"
	echo "data deployed"
fi
ls -la "$WORK/eu2"
echo "built and deployed"
