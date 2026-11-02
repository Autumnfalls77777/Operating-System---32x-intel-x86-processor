# =============================================================================
# tools/build_image.ps1  -  Assemble the Altitude OS 1.44 MiB floppy image
#
# Layout: boot sector (512 B) + stage 2 (8 KiB) + kernel (<= 448 KiB), padded
# with zeroes to the standard 1.44 MiB floppy size.
# =============================================================================

param(
    [string]$Root = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = 'Stop'

$build  = Join-Path $Root 'build'
$boot   = Join-Path $build 'boot.bin'
$stage2 = Join-Path $build 'stage2.bin'
$kernel = Join-Path $build 'kernel.bin'
$image  = Join-Path $build 'os_image.bin'

$imageSize  = 1474560
$kernelMax  = 896 * 512

foreach ($f in @($boot, $stage2, $kernel)) {
    if (-not (Test-Path -LiteralPath $f)) {
        Write-Host "missing build input: $f"
        exit 1
    }
}

$bootBytes   = [IO.File]::ReadAllBytes($boot)
$stageBytes  = [IO.File]::ReadAllBytes($stage2)
$kernelBytes = [IO.File]::ReadAllBytes($kernel)

if ($bootBytes.Length -ne 512) {
    Write-Host "boot sector must be exactly 512 bytes (got $($bootBytes.Length))"
    exit 1
}
if ($stageBytes.Length -ne 8192) {
    Write-Host "stage 2 must be exactly 8192 bytes (got $($stageBytes.Length))"
    exit 1
}
if ($kernelBytes.Length -gt $kernelMax) {
    Write-Host "kernel is $($kernelBytes.Length) bytes; the limit is $kernelMax"
    exit 1
}

$total = $bootBytes.Length + $stageBytes.Length + $kernelBytes.Length
if ($total -gt $imageSize) {
    Write-Host "payload is $total bytes; it does not fit in a 1.44 MiB image"
    exit 1
}

$img = New-Object byte[] $imageSize
[Array]::Copy($bootBytes,   0, $img, 0, $bootBytes.Length)
[Array]::Copy($stageBytes,  0, $img, 512, $stageBytes.Length)
[Array]::Copy($kernelBytes, 0, $img, 512 + 8192, $kernelBytes.Length)

[IO.File]::WriteAllBytes($image, $img)

Write-Host ("   image  {0:N0} bytes  ({1:N0} KiB)  boot 512 B + stage2 8 KiB + kernel {2:N0} B" -f `
    $imageSize, ($imageSize / 1024), $kernelBytes.Length)
