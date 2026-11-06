# Drip Launcher — environment setup.
#
# All third-party dependencies (imgui, DirectX SDK, fonts, nlohmann/json and
# the licence helpers) are vendored in the repository, so a fresh clone
# already builds. This script only prepares the build directory and,
# optionally, runs the build.
#
# A JDK is required only to regenerate the embedded attach agent JAR:
#   DripLauncher\resources\build-agent.ps1
param(
    [switch]$Build
)

$ErrorActionPreference = "Stop"
$Root = $PSScriptRoot

New-Item -ItemType Directory -Force -Path (Join-Path $Root "build") | Out-Null
Write-Host "Setup complete."

if ($Build) {
    & (Join-Path $Root "build.ps1")
}
