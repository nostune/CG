[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [ValidateSet("EarthExit", "MoonExit", "SaturnArrival", "MarsArrival")]
    [string]$Scenario = "EarthExit",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
$executable = Join-Path $projectRoot "out\build\windows-x64\$Configuration\OuterWildsECS.exe"
$logDirectory = Join-Path $projectRoot "logs"

if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot "build.ps1") -Configuration $Configuration
}
& (Join-Path $PSScriptRoot "check-assets.ps1")

if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "Executable not found at '$executable'."
}

$startedAt = Get-Date
Push-Location $projectRoot
try {
    $scenarioArgument = switch ($Scenario) {
        "MoonExit" { "--moon-transition-smoke" }
        "SaturnArrival" { "--saturn-collision-smoke" }
        "MarsArrival" { "--mars-collision-smoke" }
        default { "--sector-transition-smoke" }
    }
    & $executable $scenarioArgument *> $null
    $exitCode = $LASTEXITCODE
}
finally {
    Pop-Location
}

if ($exitCode -ne 0) {
    throw "Sector transition smoke executable exited with code $exitCode."
}

$sessionLog = Get-ChildItem -LiteralPath $logDirectory -Filter "OuterWilds-*.log" -File |
    Where-Object { $_.LastWriteTime -ge $startedAt } |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1
if (-not $sessionLog) {
    throw "Sector transition smoke run did not create a new session log."
}

$logLines = Get-Content -LiteralPath $sessionLog.FullName
$scenarioPatterns = switch ($Scenario) {
    "MoonExit" {
        @("Prepared scenario: MoonExit", "switching sector: Moon -> Earth", "Earth collision ENABLED")
    }
    "SaturnArrival" {
        @("Prepared scenario: SaturnArrival", "switching sector: Sun (Space) -> Saturn", "Saturn collision ENABLED (1 shapes)")
    }
    "MarsArrival" {
        @(
            "Prepared scenario: MarsArrival",
            "switching sector: Sun (Space) -> Mars",
            "Mars collision ENABLED (1 shapes)",
            "[PhysXContact] Spacecraft <-> Mars"
        )
    }
    default {
        @("Prepared scenario: EarthExit", "switching sector: Earth -> Sun (Space)", "Sun (Space)")
    }
}
$requiredPatterns = @(
    "Automated sector transition smoke mode enabled",
    "Velocity frame change:",
    "Automatic diagnostic stop reached",
    "Program exiting normally"
) + $scenarioPatterns

$missingPatterns = @(
    foreach ($pattern in $requiredPatterns) {
        if (-not ($logLines | Select-String -SimpleMatch $pattern -Quiet)) {
            $pattern
        }
    }
)
if ($missingPatterns.Count -gt 0) {
    throw "Sector transition smoke log is missing:`n  $($missingPatterns -join "`n  ")"
}

$fatalLines = @($logLines | Select-String -Pattern "\[(ERROR|CRITICAL)\]")
if ($fatalLines.Count -gt 0) {
    throw "Sector transition smoke recorded errors:`n  $($fatalLines.Line -join "`n  ")"
}

$transitionLine = $logLines | Select-String -SimpleMatch "switching sector:" | Select-Object -First 1
$velocityLine = $logLines | Select-String -SimpleMatch "Velocity frame change:" | Select-Object -First 1
Write-Host "Sector transition smoke test passed: $Scenario." -ForegroundColor Green
Write-Host "  Log: $($sessionLog.FullName)"
Write-Host "  $($transitionLine.Line)"
Write-Host "  $($velocityLine.Line)"
