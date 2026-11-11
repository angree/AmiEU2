#!/bin/sh
# Builds the PC-side extractor/converter: Linux (build/host/eu2conv) and Windows (build/host/eu2conv.exe).
# Run inside WSL:  sh build/build-eu2conv.sh
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host
SRC="tools/eu2conv/eu2conv.c tools/eu2conv/inno.c tools/eu2conv/sha1.c tools/eu2conv/bink.c tools/eu2conv/third_party/LzmaDec.c tools/eu2conv/third_party/Lzma2Dec.c"
CF="-O2 -Wall -Wno-unused-function -Wno-misleading-indentation -std=gnu99"
gcc $CF -o build/host/eu2conv $SRC -lm
gcc $CF -o build/host/innodump tools/eu2conv/innodump.c tools/eu2conv/inno.c tools/eu2conv/sha1.c tools/eu2conv/third_party/LzmaDec.c tools/eu2conv/third_party/Lzma2Dec.c
gcc $CF -o build/host/bink_test tools/eu2conv/bink_test.c tools/eu2conv/bink.c     # developer tool
x86_64-w64-mingw32-gcc $CF -static -o build/host/eu2conv.exe $SRC -lm
ls -la build/host/eu2conv build/host/eu2conv.exe
echo "eu2conv built"
