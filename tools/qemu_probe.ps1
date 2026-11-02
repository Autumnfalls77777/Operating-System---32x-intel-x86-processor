# =============================================================================
# tools/qemu_probe.ps1  -  Headless QEMU boot probe and screenshot helper
#
# Boots build/os_image.bin with -display none, optionally injects key chords,
# captures a PPM through the HMP monitor, then quits QEMU.
#
# Example:
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/qemu_probe.ps1
#   powershell ... -File tools/qemu_probe.ps1 -Keys meta_l-c -Shot build/shot.ppm
# =============================================================================

param(
    [string]   $Shot = 'build/screen.ppm',
    [string[]] $Keys = @(),
    [int]      $MoveX = 0,
    [int]      $MoveY = 0,
    [switch]   $Click,
    [int]      $BootWait = 15,
    [int]      $Settle = 2,
    [string]   $Qemu = 'C:/msys64_new/ucrt64/bin/qemu-system-x86_64.exe'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$shotPath = [IO.Path]::GetFullPath((Join-Path $root $Shot)).Replace('\', '/')
$serial   = 'build/serial.log'
Remove-Item -LiteralPath $shotPath, $serial -Force -ErrorAction SilentlyContinue

$qemuArgs = @(
    '-m', '256M',
    '-cpu', 'qemu32',
    '-vga', 'std',
    '-rtc', 'base=localtime',
    '-drive', 'format=raw,file=build/os_image.bin,if=floppy',
    '-boot', 'order=a',
    '-serial', "file:$serial",
    '-display', 'none',
    '-monitor', 'tcp:127.0.0.1:55555,server,nowait'
)

$proc = Start-Process -FilePath $Qemu -ArgumentList $qemuArgs -PassThru -WindowStyle Hidden
Start-Sleep -Seconds $BootWait

$client = New-Object Net.Sockets.TcpClient('127.0.0.1', 55555)
$stream = $client.GetStream()
$writer = New-Object IO.StreamWriter($stream)
$writer.AutoFlush = $true

foreach ($chord in $Keys) {
    $writer.WriteLine("sendkey $chord")
    Start-Sleep -Milliseconds 400
}

if ($MoveX -ne 0 -or $MoveY -ne 0) {
    $writer.WriteLine("mouse_move $MoveX $MoveY")
    Start-Sleep -Milliseconds 500
}
if ($Click) {
    $writer.WriteLine('mouse_button 1')
    Start-Sleep -Milliseconds 150
    $writer.WriteLine('mouse_button 0')
    Start-Sleep -Milliseconds 500
}
Start-Sleep -Seconds $Settle

$writer.WriteLine("screendump `"$shotPath`"")
Start-Sleep -Seconds 2
$writer.WriteLine('quit')
Start-Sleep -Seconds 2

$client.Close()
if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force }

Write-Host "serial log : $serial"
Write-Host "screenshot : $shotPath"
