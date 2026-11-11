# vm-qemu.ps1 - starts the comparison VM (QEMU x86_64, WHPX) headless if it is not running.
# Disk: D:\temp\amiga_eu2_vm\nocloud.qcow2 (Debian 12 nocloud, grown to 10 GB). ssh 127.0.0.1:2299, serial 127.0.0.1:4599.
$d = 'D:\temp\amiga_eu2_vm'
$pidf = Join-Path $d 'qemu.pid'
if (Test-Path $pidf) {
  $old = Get-Process -Id ([int](Get-Content $pidf)) -ErrorAction SilentlyContinue
  if ($old -and $old.ProcessName -like 'qemu*') { "already running: pid $($old.Id)"; exit 0 }
}
$a = "-machine q35 -accel whpx -m 2048 -smp 4 -cpu qemu64 -drive file=$d\nocloud.qcow2,if=virtio " +
     "-netdev user,id=n0,hostfwd=tcp:127.0.0.1:2299-:22 -device virtio-net-pci,netdev=n0 -display none " +
     "-serial tcp:127.0.0.1:4599,server=on,wait=off -pidfile $pidf"
$p = Start-Process -FilePath "$d\qemu\qemu-system-x86_64.exe" -ArgumentList $a -WindowStyle Hidden -PassThru
"started pid $($p.Id)"
