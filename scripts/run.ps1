[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [switch]$SkipBuild,
    [switch]$SkipAssetCheck,
    [switch]$Diagnostics,
    [switch]$Pvd,
    [switch]$PhysXDebug,
    [switch]$ContactDebug,
    [switch]$SkipWelcome,
    [switch]$Console
)

$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
$executable = Join-Path $projectRoot "out\build\windows-x64\$Configuration\OuterWildsECS.exe"

if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot "build.ps1") -Configuration $Configuration
}

if (-not (Test-Path -LiteralPath $executable)) {
    throw "Executable not found at '$executable'. Build the requested configuration first."
}

if (-not $SkipAssetCheck) {
    & (Join-Path $PSScriptRoot "check-assets.ps1")
    if ($LASTEXITCODE -ne 0) {
        throw "Required asset check failed with exit code $LASTEXITCODE."
    }
}

Push-Location $projectRoot
try {
    $previousDiagnostics = $env:OUTERWILDS_DIAGNOSTICS
    $previousPvd = $env:OUTERWILDS_PVD
    $previousPhysXDebug = $env:OUTERWILDS_PHYSX_DEBUG_VIEW
    $previousContactDebug = $env:OUTERWILDS_CONTACT_DEBUG
    if ($Diagnostics) { $env:OUTERWILDS_DIAGNOSTICS = "1" }
    if ($Pvd) { $env:OUTERWILDS_PVD = "1" }
    if ($PhysXDebug) { $env:OUTERWILDS_PHYSX_DEBUG_VIEW = "1" }
    if ($ContactDebug) { $env:OUTERWILDS_CONTACT_DEBUG = "1" }

    $arguments = @()
    if ($Console) { $arguments += "--console" }
    if ($SkipWelcome) { $arguments += "--skip-welcome" }
    if ($Console) {
        & $executable @arguments
    } else {
        & $executable @arguments *> $null
    }
}
finally {
    $env:OUTERWILDS_DIAGNOSTICS = $previousDiagnostics
    $env:OUTERWILDS_PVD = $previousPvd
    $env:OUTERWILDS_PHYSX_DEBUG_VIEW = $previousPhysXDebug
    $env:OUTERWILDS_CONTACT_DEBUG = $previousContactDebug
    Pop-Location
}
