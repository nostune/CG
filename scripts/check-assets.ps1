[CmdletBinding()]
param(
    [string]$ManifestPath
)

$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $ManifestPath) {
    $ManifestPath = Join-Path $projectRoot "config\assets.required.txt"
}

if (-not (Test-Path -LiteralPath $ManifestPath -PathType Leaf)) {
    throw "Asset manifest not found at '$ManifestPath'."
}

$requiredPaths = Get-Content -LiteralPath $ManifestPath |
    ForEach-Object { $_.Trim() } |
    Where-Object { $_ -and -not $_.StartsWith("#") }

$missingPaths = @(
    foreach ($relativePath in $requiredPaths) {
        $fullPath = Join-Path $projectRoot $relativePath
        if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
            $relativePath
        }
    }
)

if ($missingPaths.Count -gt 0) {
    Write-Host "Missing $($missingPaths.Count) required asset(s):" -ForegroundColor Red
    $missingPaths | ForEach-Object { Write-Host "  $_" }
    Write-Host "Restore local development assets with .\scripts\restore-assets.ps1"
    exit 1
}

Write-Host "Asset check passed: $($requiredPaths.Count) required files are present."
