# Screenshot OUR guest window with PrintWindow (works when covered; takes control of nothing).
#   shot-eu2.ps1 -Out C:\temp\amiga_eu2\shots\x.png
param([Parameter(Mandatory)][string]$Out, [string]$Config = "eu2-ref020.uae")
Add-Type -AssemblyName System.Drawing
Add-Type -Name P -Namespace Eu2S -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint f);
[DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
public struct RECT { public int L, T, R, B; }
public delegate bool EnumProc(IntPtr h, IntPtr l);
[DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint wp);
[DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, System.Text.StringBuilder s, int n);
'@
$procs = @(Get-CimInstance Win32_Process | Where-Object { $_.Name -like 'winuae*' -and $_.CommandLine -like "*$Config*" } | ForEach-Object { [uint32]$_.ProcessId })
if ($procs.Count -eq 0) { Write-Output "not running"; exit 1 }
$script:hw = [IntPtr]::Zero
$cb = [Eu2S.P+EnumProc]{ param($h, $l)
  [uint32]$wp = 0; [Eu2S.P]::GetWindowThreadProcessId($h, [ref]$wp) | Out-Null
  if ($procs -contains $wp) { $cn = New-Object System.Text.StringBuilder 64; [Eu2S.P]::GetClassName($h, $cn, 64) | Out-Null
    if ($cn.ToString() -eq 'PCsuxRox') { $script:hw = $h } }
  return $true }
[Eu2S.P]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
if ($script:hw -eq [IntPtr]::Zero) { Write-Output "no window"; exit 1 }
$r = New-Object Eu2S.P+RECT
[Eu2S.P]::GetClientRect($script:hw, [ref]$r) | Out-Null
$bmp = New-Object System.Drawing.Bitmap ([Math]::Max(1, $r.R)), ([Math]::Max(1, $r.B))
$g = [System.Drawing.Graphics]::FromImage($bmp)
$dc = $g.GetHdc(); [Eu2S.P]::PrintWindow($script:hw, $dc, 3) | Out-Null; $g.ReleaseHdc($dc)
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
Write-Output "saved $Out ($($r.R)x$($r.B))"
