# DemokratikKongo - build Release|x64
$ErrorActionPreference = "Stop"
$Root = $PSScriptRoot
$Sln = Join-Path $Root "DemokratikKongo.sln"

if (-not (Test-Path $Sln)) {
    Write-Error "Solution not found: $Sln"
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
$msbuild = $null
try {
    if (Test-Path $vswhere) {
        $msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe" |
            Select-Object -First 1
    }
} catch {
    Write-Warning "vswhere invocation failed: $_"
}

if (-not $msbuild) {
    Write-Host "vswhere unavailable/empty — falling back to msbuild on PATH."
    $msbuild = "msbuild"
}

Write-Host "Building DemokratikKongo (Release|x64)..."
& $msbuild $Sln /p:Configuration=Release /p:Platform=x64 /m /v:minimal
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$srcExe = Join-Path $Root "DemokratikKongo\build\RuntimeHost.exe"
$srcDll = Join-Path $Root "build\RuntimeHostCore.dll"
$dstExe = Join-Path $Root "build\RuntimeHost.exe"
$releaseDir = Join-Path $Root "release"
$releaseExe = Join-Path $releaseDir "RuntimeHost.exe"
$releaseDll = Join-Path $releaseDir "RuntimeHostCore.dll"

New-Item -ItemType Directory -Force -Path (Join-Path $Root "build") | Out-Null
New-Item -ItemType Directory -Force -Path $releaseDir | Out-Null

if (-not (Test-Path $srcExe)) {
    Write-Error "Launcher EXE not found: $srcExe"
}
if (-not (Test-Path $srcDll)) {
    Write-Error "Core DLL not found: $srcDll"
}

try {
    Copy-Item $srcExe $releaseExe -Force
} catch {
    Write-Warning "Could not copy launcher EXE to release\ (file locked?)."
}
try {
    Copy-Item $srcDll $releaseDll -Force
} catch {
    Write-Warning "Could not copy core DLL to release\ (file locked?)."
}
try {
    Copy-Item $srcExe $dstExe -Force
} catch {
    Write-Warning "Could not update build\RuntimeHost.exe (file locked?). Use release\ folder."
}

$exeLen = (Get-Item $releaseExe).Length
$dllLen = (Get-Item $releaseDll).Length
Write-Host ""
Write-Host "=== RELEASE (share these) ==="
Write-Host "  DLL only (updates / manual inject): $releaseDll"
Write-Host "  Size: $dllLen bytes"
Write-Host ""
Write-Host "  Launcher + optional sibling DLL:"
Write-Host "    $releaseExe ($exeLen bytes, core embedded)"
Write-Host "    $releaseDll (same folder = drop-in update, skips TEMP extract)"
Write-Host ""
Write-Host "Note: DLL alone does not inject. Use launcher or a manual injector."
