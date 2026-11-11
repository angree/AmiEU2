#!/usr/bin/env python3
"""vmcon.py CMD... - run shell commands as root on the comparison VM's serial console
(QEMU -serial tcp:127.0.0.1:4599). Logs in when it sees a login prompt. Prints the output."""
import socket, sys, time, re

def read_until(s, pats, timeout):
    buf, end = b"", time.time() + timeout
    while time.time() < end:
        try:
            d = s.recv(4096)
            if d: buf += d
        except socket.timeout:
            pass
        for p in pats:
            if p in buf: return buf, p
    return buf, None

s = socket.create_connection(("127.0.0.1", 4599), 10); s.settimeout(0.5)
s.sendall(b"\r"); buf, p = read_until(s, [b"login:", b"# "], 120)
if p == b"login:":
    s.sendall(b"root\r"); buf, p = read_until(s, [b"# "], 30)
for cmd in sys.argv[1:]:
    mark = "__END%d__" % int(time.time() * 1000 % 100000)
    s.sendall((cmd + "; echo " + mark + "\r").encode())
    out, p = read_until(s, [(mark + "\r\n").encode(), (mark + "\n").encode()], 1800)
    txt = out.decode("utf-8", "replace")
    txt = re.sub(r"\x1b\[[0-9;?]*[a-zA-Z]", "", txt)
    lines = [l for l in txt.splitlines() if mark not in l]
    print("\n".join(lines[-40:]))
