# Comparing with the original

The port is checked screen by screen against the original `eu2.exe` of the user's own GOG
installation, running in a separate VM: QEMU x86_64 (WHPX) on `D:\temp\amiga_eu2_vm`, Debian 12 nocloud,
Wine 8 (wine32) on an invisible Xvfb display `:77` at 800x600x16. Nothing appears on or is sent to
the Windows desktop. The game files are copied into the VM (`/root/eu2`); nothing goes into the repo,
and the screenshots stay in `C:\temp\amiga_eu2\shots` (they show the original's art).

- start the VM: `D:\temp\amiga_eu2_vm` (see PROGRESS for the QEMU command line), ssh on 127.0.0.1:2299
- `vm-start.sh` - starts eu2.exe; `vm-shot.sh NAME` - screenshot; `vm-input.sh click X Y key Return ...`
- `vmcon.py` - root shell on the serial console (127.0.0.1:4599) for setup

Run the scripts through `wsl sh /mnt/c/temp/amiga_eu2/w.sh tools/orig/<script>`.
