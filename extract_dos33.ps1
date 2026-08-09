$ErrorActionPreference = 'Stop'

$sourcePath = Join-Path $PSScriptRoot 'diskDos33.h'
$outputDirectory = Join-Path $PSScriptRoot 'sdcard\apple2'
$outputPath = Join-Path $outputDirectory 'dos33.dsk'
$expectedSize = 143360

$source = Get-Content -LiteralPath $sourcePath -Raw
$matches = [regex]::Matches($source, '0x([0-9A-Fa-f]{2})')

if ($matches.Count -ne $expectedSize) {
  throw "Expected $expectedSize bytes in diskDos33.h, found $($matches.Count)."
}

$bytes = [byte[]]::new($matches.Count)
for ($index = 0; $index -lt $matches.Count; $index++) {
  $bytes[$index] = [Convert]::ToByte($matches[$index].Groups[1].Value, 16)
}

New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
[System.IO.File]::WriteAllBytes($outputPath, $bytes)

$writtenSize = (Get-Item -LiteralPath $outputPath).Length
if ($writtenSize -ne $expectedSize) {
  throw "Output validation failed: expected $expectedSize bytes, wrote $writtenSize."
}

Write-Output "Created $outputPath"
Write-Output "Size: $writtenSize bytes"
