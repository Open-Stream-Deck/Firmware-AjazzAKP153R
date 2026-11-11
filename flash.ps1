param(
    [switch]$Flash,
    [string]$AppPath = (Join-Path $PSScriptRoot '..\..\open-stream-deck-app\src-tauri')
)

$ErrorActionPreference = 'Stop'
$BuildPath = Join-Path $PSScriptRoot 'build'
$Manifest = Get-Content -LiteralPath (Join-Path $BuildPath 'firmware.json') -Raw | ConvertFrom-Json

if ([string]::IsNullOrWhiteSpace($Manifest.file) -or [IO.Path]::GetFileName($Manifest.file) -ne $Manifest.file) {
    throw 'Invalid image filename in firmware.json.'
}
$ImagePath = Join-Path $BuildPath $Manifest.file
if ((Get-Item -LiteralPath $ImagePath).Length -ne $Manifest.bytes) {
    throw 'Image size does not match firmware.json.'
}
if ((Get-FileHash -LiteralPath $ImagePath -Algorithm SHA256).Hash.ToLowerInvariant() -ne $Manifest.sha256) {
    throw 'Image hash does not match firmware.json.'
}
if ($Manifest.application_base -ne '0x08004000' -or $Manifest.bytes -gt 96496) {
    throw 'Image does not match the application memory layout.'
}

$Plan = [ordered]@{
    image = $ImagePath
    bytes = $Manifest.bytes
    sha256 = $Manifest.sha256
    crc16 = $Manifest.crc16_arc
    firmware = $Manifest.firmware_version
    flasher = (Join-Path $AppPath 'examples\flash.rs')
}

if (-not $Flash) {
    $Plan | ConvertTo-Json
    return
}

Push-Location -LiteralPath $AppPath
try {
    & cargo run --quiet --example flash -- $ImagePath
    if ($LASTEXITCODE -ne 0) {
        throw 'Firmware update failed.'
    }
} finally {
    Pop-Location
}
