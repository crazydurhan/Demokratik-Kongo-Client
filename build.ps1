# Drip Launcher - build.
#
#   .\build.ps1          # Rust launcher (default) -> release\RuntimeHost.exe
#   .\build.ps1 -Cpp     # legacy MSVC/ImGui implementation (reference/rollback)
param(
    [switch]$Cpp
)

$ErrorActionPreference = "Stop"
$Root = $PSScriptRoot
$ReleaseDir = Join-Path $Root "release"
$BuildDir = Join-Path $Root "build"
New-Item -ItemType Directory -Force -Path $ReleaseDir, $BuildDir | Out-Null

# --------------------------------------------------------------- legacy C++
if ($Cpp) {
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

    $toolsetArgs = @()
    if ($msbuild -match "\\18\\") { $toolsetArgs = @("/p:PlatformToolset=v145") }
    elseif ($msbuild -match "\\17\\") { $toolsetArgs = @("/p:PlatformToolset=v143") }

    Write-Host "Building Drip Launcher (legacy C++, Release|x64)..."
    & $msbuild $Sln /p:Configuration=Release /p:Platform=x64 @toolsetArgs /m /v:minimal
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    $srcExe = Join-Path $Root "DripLauncher\build\RuntimeHost.exe"
    if (-not (Test-Path $srcExe)) {
        Write-Error "Launcher EXE not found: $srcExe"
    }
    Copy-Item $srcExe (Join-Path $ReleaseDir "RuntimeHost.exe") -Force
    Copy-Item $srcExe (Join-Path $BuildDir "RuntimeHost.exe") -Force
    Write-Host "Output: $(Join-Path $ReleaseDir 'RuntimeHost.exe')"
    exit 0
}

# ------------------------------------------------------------------- Rust
$Crate = Join-Path $Root "launcher-rs"
if (-not (Test-Path (Join-Path $Crate "Cargo.toml"))) {
    Write-Error "Rust crate not found: $Crate"
}

Write-Host "Building Drip Launcher (Rust, release)..."
Push-Location $Crate
try {
    # cargo writes progress to stderr; keep PowerShell from treating that as
    # a terminating error so $LASTEXITCODE stays authoritative.
    $previousErrorAction = $ErrorActionPreference
    $ErrorActionPreference = "Continue"

    # Keep machine-specific absolute paths out of the shipped EXE: panic
    # locations from dependencies would otherwise embed the Cargo registry
    # path (C:\Users\<name>\.cargo\...) and the workspace path.
    $cargoHome = if ($env:CARGO_HOME) { $env:CARGO_HOME } else { Join-Path $env:USERPROFILE ".cargo" }
    $remapFlags = @(
        "--remap-path-prefix=$cargoHome=C:\cargo",
        "--remap-path-prefix=$Root=C:\build",
        "-C",
        "link-arg=/PDBALTPATH:%_PDB%"
    )
    $env:CARGO_ENCODED_RUSTFLAGS = ($remapFlags -join [char]31)

    & cargo build --release
    $cargoExit = $LASTEXITCODE
    Remove-Item Env:CARGO_ENCODED_RUSTFLAGS -ErrorAction SilentlyContinue
    $ErrorActionPreference = $previousErrorAction
} finally {
    Pop-Location
}
if ($cargoExit -ne 0) { exit $cargoExit }

$srcExe = Join-Path $Crate "target\release\RuntimeHost.exe"
if (-not (Test-Path $srcExe)) {
    Write-Error "Launcher EXE not found: $srcExe"
}

$releaseExe = Join-Path $ReleaseDir "RuntimeHost.exe"
Copy-Item $srcExe $releaseExe -Force
try { Copy-Item $srcExe (Join-Path $BuildDir "RuntimeHost.exe") -Force } catch { }

$item = Get-Item $releaseExe
$sha256 = (Get-FileHash $releaseExe -Algorithm SHA256).Hash.ToLower()
$version = $item.VersionInfo.FileVersion

Write-Host ""
Write-Host "=== RELEASE (share this) ==="
Write-Host "  $releaseExe"
Write-Host "  size   : $($item.Length) bytes"
Write-Host "  version: $version"
Write-Host "  sha256 : $sha256"
Write-Host ""
Write-Host "The launcher fetches the client core from the update service at"
Write-Host "inject time (license-gated) and loads it through a java.exe-hosted"
Write-Host "agent. No core DLL ships with the launcher."
Write-Host "See LISANS_VE_YUKLEME_STANDARTLARI.md."
