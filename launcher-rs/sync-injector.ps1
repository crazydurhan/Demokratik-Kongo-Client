# Regenerates launcher-rs/assets/injector.b64 from the committed
# DripLauncher/src/launcher/injectorDllData.h base64 blob.
#
# Run this after the injector agent (src/agent/injectAgent.cpp) changes:
#   .\build.ps1 -Cpp
#   powershell -File DripLauncher\resources\build-injector.ps1
#   powershell -File launcher-rs\sync-injector.ps1
#   .\build.ps1
$ErrorActionPreference = "Stop"

$Crate = $PSScriptRoot
$Header = Join-Path $Crate "..\DripLauncher\src\launcher\injectorDllData.h"
$Out = Join-Path $Crate "assets\injector.b64"

if (-not (Test-Path $Header)) {
    Write-Error "Header not found: $Header"
}

$text = [IO.File]::ReadAllText($Header)
$chunks = [regex]::Matches($text, '"([A-Za-z0-9+/=]{20,})"') | ForEach-Object { $_.Groups[1].Value }
$base64 = -join $chunks
if ($base64.Length -lt 1000) {
    Write-Error "Could not extract the base64 blob from $Header"
}

$bytes = [Convert]::FromBase64String($base64)
[IO.File]::WriteAllText($Out, $base64, (New-Object System.Text.UTF8Encoding($false)))

Write-Host "Injector DLL: $($bytes.Length) bytes"
Write-Host "Wrote: $Out"
