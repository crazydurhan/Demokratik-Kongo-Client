# Drip Launcher - build (Release|x64).
$ErrorActionPreference = "Stop"
$Root = $PSScriptRoot
$Sln = Join-Path $Root "DripLauncher.sln"

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
    Write-Host "vswhere unavailable/empty - falling back to msbuild on PATH."
    $msbuild = "msbuild"
}

# Auto-detect the installed platform toolset: VS 18 BuildTools ships v145,
# VS 2022 ships v143. Passing the matching value avoids MSB8020 on machines
# that only have one of them (see AGENTS.md).
$toolsetArgs = @()
if ($msbuild -match "\\18\\") { $toolsetArgs = @("/p:PlatformToolset=v145") }
elseif ($msbuild -match "\\17\\") { $toolsetArgs = @("/p:PlatformToolset=v143") }

Write-Host "Building Drip Launcher (Release|x64)..."
& $msbuild $Sln /p:Configuration=Release /p:Platform=x64 @toolsetArgs /m /v:minimal
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$srcExe = Join-Path $Root "DripLauncher\build\RuntimeHost.exe"
$dstExe = Join-Path $Root "build\RuntimeHost.exe"
$releaseDir = Join-Path $Root "release"
$releaseExe = Join-Path $releaseDir "RuntimeHost.exe"

New-Item -ItemType Directory -Force -Path (Join-Path $Root "build") | Out-Null
New-Item -ItemType Directory -Force -Path $releaseDir | Out-Null

if (-not (Test-Path $srcExe)) {
    Write-Error "Launcher EXE not found: $srcExe"
}

try { Copy-Item $srcExe $releaseExe -Force } catch { Write-Warning "Could not copy launcher EXE to release\ (file locked?)." }
try { Copy-Item $srcExe $dstExe -Force } catch { Write-Warning "Could not update build\RuntimeHost.exe (file locked?). Use release\ folder." }

$exeLen = (Get-Item $releaseExe).Length
Write-Host ""
Write-Host "=== RELEASE (share this) ==="
Write-Host "  $releaseExe ($exeLen bytes)"
Write-Host ""
Write-Host "The launcher fetches the client core from the update service at"
Write-Host "inject time (license-gated) and loads it through a java.exe-hosted"
Write-Host "agent. No core DLL ships with the launcher."
Write-Host "See LISANS_VE_YUKLEME_STANDARTLARI.md."
