# DemokratikKongo — dependency setup
#
# All third-party dependencies (imgui, minhook, JNI headers, fonts, DirectX SDK,
# nlohmann/json) are vendored under the projects' `ext/` folders and tracked in
# git, so a fresh clone already contains everything needed to build.
#
# The only machine-specific step is locating a JDK's `jvm.lib`, which is written
# to the git-ignored `local.props`. If no JDK is found the build falls back to
# the vendored `DemokratikKongoCore\ext\jni\jvm.lib`.
param(
    [switch]$Build
)

$ErrorActionPreference = "Stop"
$Root = $PSScriptRoot

# JDK jvm.lib detection (prefer 1.8, fallback any JDK).
$JvmLib = $null
$SearchRoots = @(
    "C:\Program Files\Java",
    "C:\Program Files (x86)\Java",
    "C:\Program Files\Eclipse Adoptium",
    "C:\Program Files\Microsoft",
    "C:\Program Files\Zulu"
)
$JdkPatterns = @("jdk1.8*", "jdk-8*", "jdk-17*", "jdk-*")
foreach ($base in $SearchRoots) {
    if (-not (Test-Path $base)) { continue }
    foreach ($pat in $JdkPatterns) {
        $candidates = Get-ChildItem -Path $base -Directory -Filter $pat -ErrorAction SilentlyContinue |
            Sort-Object Name -Descending
        foreach ($jdk in $candidates) {
            $candidate = Join-Path $jdk.FullName "lib\jvm.lib"
            if (Test-Path $candidate) { $JvmLib = $candidate; break }
        }
        if ($JvmLib) { break }
    }
    if ($JvmLib) { break }
}

$LocalProps = Join-Path $Root "local.props"
if ($JvmLib) {
    $xml = @"
<?xml version="1.0" encoding="utf-8"?>
<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <PropertyGroup>
    <JvmLibPath>$JvmLib</JvmLibPath>
  </PropertyGroup>
</Project>
"@
    Set-Content -Path $LocalProps -Value $xml -Encoding UTF8
    Write-Host "JDK found: $JvmLib"
}
else {
    Write-Warning "JDK jvm.lib not found; the build will use the vendored ext\jni\jvm.lib."
    Write-Warning "To use your own JDK, create local.props with a <JvmLibPath> property."
}

New-Item -ItemType Directory -Force -Path (Join-Path $Root "build") | Out-Null
Write-Host "Setup complete."

if ($Build) {
    & (Join-Path $Root "build.ps1")
}
