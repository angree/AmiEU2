#!/usr/bin/env python3
"""release-gate.py - refuse a release tree that holds any Paradox file or anything made from one.

    python3 build/release-gate.py <dir> [<dir> ...]      exit 0 = clean, 1 = refused
    python3 build/release-gate.py --git                   checks every file git tracks

The port may only ship our own code and free (CC0 / public domain) material; the player converts
their own GOG copy with eu2conv. A file is refused when it
  1. has the SHA-1 of a file of the GOG v1.09 install (tools/eu2conv/knownfiles.h - the original,
     byte for byte, whatever it is called now),
  2. starts with one of our data-set magics (docs/DATA_FORMAT.md): EU2P / EUI1 / EUS1 / EUM1 - those
     are converted Paradox data,
  3. is the installer, the game executable, or has a type only the original brings (Bink video,
     .eug scenarios / saves, the map tables, .ai files, .tbl, .csv text databases).
A .txt / .bmp / .wav is not refused by its name alone (we have our own), only by rules 1 and 2;
a .bmp or .wav anywhere in a release is still reported as a warning to look at.
The model is the OpenTTD port's assert_no_original_ttd_data (Amiga_OpenTTD/build/make-release-all.sh).
"""
import hashlib, os, re, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAGICS = (b"EU2P", b"EUI1", b"EUS1", b"EUM1")
BAD_NAMES = re.compile(r"^(eu2\.exe|setup_europa_universalis2.*|eu2\.ini|.*\.(bik|eug|ai|tbl|csv|ctl|idx|lightmap|gnd))$", re.I)
WARN_EXT = re.compile(r"\.(bmp|wav|mp3|tga)$", re.I)


def known_sha1():
    p = os.path.join(ROOT, "tools", "eu2conv", "knownfiles.h")
    s = {}
    for m in re.finditer(r'\{ "([^"]+)", (\d+)UL, "([0-9a-f]{40})" \}', open(p, encoding="latin-1").read()):
        s[m.group(3)] = m.group(1)
    if len(s) < 1000:
        sys.exit("release-gate: knownfiles.h looks wrong (%d hashes) - refusing" % len(s))
    return s


def check(files, known):
    bad, warn = [], []
    for f in files:
        name = os.path.basename(f)
        try:
            with open(f, "rb") as h:
                data = h.read()
        except OSError as e:
            bad.append((f, "unreadable: %s" % e)); continue
        sha = hashlib.sha1(data).hexdigest()
        if sha in known and len(data) > 0:
            bad.append((f, "is the original %s" % known[sha]))
        elif data[:4] in MAGICS:
            bad.append((f, "converted game data (%s)" % data[:4].decode()))
        elif BAD_NAMES.match(name):
            bad.append((f, "a file type only the original brings"))
        elif WARN_EXT.search(name):
            warn.append(f)
    return bad, warn


def main(argv):
    if not argv:
        print(__doc__); return 2
    known = known_sha1()
    if argv == ["--git"]:
        out = subprocess.run(["git", "-C", ROOT, "ls-files", "-z"], capture_output=True, check=True).stdout
        files = [os.path.join(ROOT, f) for f in out.decode("utf-8", "replace").split("\0") if f]
        files = [f for f in files if os.path.isfile(f)]
    else:
        files = []
        for d in argv:
            if os.path.isfile(d): files.append(d); continue
            for top, _, names in os.walk(d):
                files += [os.path.join(top, n) for n in names]
    bad, warn = check(files, known)
    for f in warn:
        print("release-gate: look at %s (an image / sound - ours?)" % f)
    if bad:
        print("FATAL: original Europa Universalis II data found:")
        for f, why in bad:
            print("  %s - %s" % (f, why))
        print("We may not redistribute it. Remove it and build again.")
        return 1
    print("release-gate: %d files, no original EU2 data  OK" % len(files))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
