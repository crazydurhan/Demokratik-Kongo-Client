# Service endpoint configuration (single place to switch licence/update host).
#
# Rewrites the licensing service host names and the Keygen account slug in
#   - include/serviceEndpoints.h                        (launcher, source of truth)
#   - <client repo>/native/license_client.c             (DripLoader)
#   - README.md / AGENTS.md / LISANS_...md              (documentation)
# and nothing else. Rebuild after running it.
#
# Prerequisites on the server side (do these first, or licences stop validating):
#   1. Rename / create the Keygen CE account slug in the panel.
#   2. Point the new DNS names at the same host and keep the same paths
#      (/v1/accounts/<slug>/..., /up/api/*, /up/dl/*).
#   3. Keep the old names reachable during the transition, or every client
#      built before the rename fails its licence check.
#
# Usage:
#   .\tools\configure-service.ps1 -ApiHost api.example.com -UpdateHost example.com -Account myaccount
#   .\tools\configure-service.ps1 ... -ClientRepo C:\path\to\Drip-Client
#   .\tools\configure-service.ps1 ... -DryRun

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$ApiHost,
    [Parameter(Mandatory = $true)][string]$UpdateHost,
    [Parameter(Mandatory = $true)][string]$Account,
    [string]$ClientRepo = "",
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Header = Join-Path $Root "include\serviceEndpoints.h"
$ClientFile = if ($ClientRepo) { Join-Path $ClientRepo "native\license_client.c" } else { "" }
$DocFiles = @(
    (Join-Path $Root "README.md"),
    (Join-Path $Root "AGENTS.md"),
    (Join-Path $Root "LISANS_VE_YUKLEME_STANDARTLARI.md")
)

function Validate-Host([string]$hostName, [string]$label) {
    if ($hostName -match '^\d{1,3}(\.\d{1,3}){3}$') {
        throw "$label looks like a raw IP address ($hostName). Use a DNS name so the server address is not embedded in client binaries."
    }
    if ($hostName -notmatch '^[A-Za-z0-9]([A-Za-z0-9.-]*[A-Za-z0-9])?$' -or $hostName -match '\.\.') {
        throw "$label is not a valid host name: $hostName"
    }
}

function Validate-Account([string]$slug) {
    if ($slug -notmatch '^[a-z0-9][a-z0-9-]{1,62}$') {
        throw "Account slug must be lowercase alphanumeric/hyphen (2-63 chars): $slug"
    }
}

Validate-Host $ApiHost "ApiHost"
Validate-Host $UpdateHost "UpdateHost"
Validate-Account $Account
if ($ApiHost -eq $UpdateHost) { Write-Warning "ApiHost and UpdateHost are identical; that is fine only if one host serves both." }

$enc = New-Object System.Text.UTF8Encoding($false)
$current = $null
if (Test-Path $Header) {
    $existing = [IO.File]::ReadAllText($Header, $enc)
    $m = [regex]::Match($existing, 'kApiHost\[\]\s*=\s*L"([^"]+)"')
    if ($m.Success) { $current = [ordered]@{}; $current.ApiHost = $m.Groups[1].Value }
    $m = [regex]::Match($existing, 'kUpdateHost\[\]\s*=\s*L"([^"]+)"')
    if ($m.Success) { $current.UpdateHost = $m.Groups[1].Value }
    $m = [regex]::Match($existing, 'kAccount\[\]\s*=\s*L"([^"]+)"')
    if ($m.Success) { $current.Account = $m.Groups[1].Value }
}

function Rewrite-File([string]$path, [System.Collections.Specialized.OrderedDictionary]$pairs) {
    if (-not (Test-Path $path)) { Write-Warning "skip (missing): $path"; return }
    $text = [IO.File]::ReadAllText($path, $enc)
    $orig = $text
    foreach ($old in $pairs.Keys) {
        if ([string]::IsNullOrEmpty($old)) { continue }
        $text = $text.Replace($old, $pairs[$old])
    }
    if ($text -eq $orig) { Write-Host "unchanged: $path"; return }
    if ($DryRun) { Write-Host "would rewrite: $path" }
    else { [IO.File]::WriteAllText($path, $text, $enc); Write-Host "rewrote: $path" }
}

# --- launcher header: replace only the three value literals -----------------
if (-not (Test-Path $Header)) { throw "Header not found: $Header" }
$headerText = [IO.File]::ReadAllText($Header, $enc)
$headerText = [regex]::Replace($headerText, '(kApiHost\[\]\s*=\s*L")[^"]+(")', { param($m) $m.Groups[1].Value + $ApiHost + $m.Groups[2].Value })
$headerText = [regex]::Replace($headerText, '(kUpdateHost\[\]\s*=\s*L")[^"]+(")', { param($m) $m.Groups[1].Value + $UpdateHost + $m.Groups[2].Value })
$headerText = [regex]::Replace($headerText, '(kAccount\[\]\s*=\s*L")[^"]+(")', { param($m) $m.Groups[1].Value + $Account + $m.Groups[2].Value })
$headerText = [regex]::Replace($headerText, '(/v1/accounts/)[^/]+(/licenses/actions/validate-key")', { param($m) $m.Groups[1].Value + $Account + $m.Groups[2].Value })
$headerText = [regex]::Replace($headerText, '(/v1/accounts/)[^/]+(/machines")', { param($m) $m.Groups[1].Value + $Account + $m.Groups[2].Value })
if ($DryRun) { Write-Host "would rewrite: $Header" } else { [IO.File]::WriteAllText($Header, $headerText, $enc); Write-Host "rewrote: $Header" }

# --- client C file: host + account paths ------------------------------------
if ($ClientFile) {
    $text = [IO.File]::ReadAllText($ClientFile, $enc)
    $text = [regex]::Replace($text, '(kApiHost\[\]\s*=\s*L")[^"]+(")', { param($m) $m.Groups[1].Value + $ApiHost + $m.Groups[2].Value })
    $text = [regex]::Replace($text, '(/v1/accounts/)[^/]+(/licenses/actions/validate-key")', { param($m) $m.Groups[1].Value + $Account + $m.Groups[2].Value })
    $text = [regex]::Replace($text, '(/v1/accounts/)[^/]+(/machines")', { param($m) $m.Groups[1].Value + $Account + $m.Groups[2].Value })
    if ($DryRun) { Write-Host "would rewrite: $ClientFile" } else { [IO.File]::WriteAllText($ClientFile, $text, $enc); Write-Host "rewrote: $ClientFile" }
}

# --- documentation: replace the previous values everywhere else ------------
if ($current) {
    $pairs = [ordered]@{}
    if ($current.ApiHost) { $pairs[$current.ApiHost] = $ApiHost }
    if ($current.UpdateHost) { $pairs[$current.UpdateHost] = $UpdateHost }
    if ($current.Account) { $pairs["accounts/" + $current.Account + "/"] = "accounts/" + $Account + "/" }
    foreach ($doc in $DocFiles) { Rewrite-File $doc $pairs }
}

Write-Host ""
Write-Host "Configured: ApiHost=$ApiHost UpdateHost=$UpdateHost Account=$Account"
Write-Host "Next: rebuild (launcher: .\build.ps1; client: cmake/native build) and re-verify the binaries contain no old host/name strings."
