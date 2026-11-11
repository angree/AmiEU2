# shared: ssh into the comparison VM (QEMU on D:\temp\amiga_eu2_vm, WHPX, port 2299)
VSSH="ssh -q -i $HOME/.ssh/eu2vm -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=10 -p 2299 root@127.0.0.1"
