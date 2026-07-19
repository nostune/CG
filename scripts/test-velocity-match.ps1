[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$executable = Join-Path $projectRoot "out\build\windows-x64\$Configuration\OuterWildsECS.exe"
$logDirectory = Join-Path $projectRoot "logs"

if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot "build.ps1") -Configuration $Configuration
}

$startedAt = Get-Date
Push-Location $projectRoot
try {
    & $executable --velocity-match-smoke *> $null
    $exitCode = $LASTEXITCODE
}
finally {
    Pop-Location
}
if ($exitCode -ne 0) { throw "Velocity-match smoke exited with code $exitCode." }

$sessionLog = Get-ChildItem -LiteralPath $logDirectory -Filter "OuterWilds-*.log" -File |
    Where-Object { $_.LastWriteTime -ge $startedAt } |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1
if (-not $sessionLog) { throw "Velocity-match smoke did not create a session log." }

$lines = Get-Content -LiteralPath $sessionLog.FullName
$required = @(
    "Automated velocity-match smoke mode enabled",
    "Prepared 20 m/s Earth-relative velocity offset",
    "VelocityMatch] Engaged for Earth",
    "VelocityMatch] Completed for Earth",
    "Program exiting normally"
)
foreach ($pattern in $required) {
    if (-not ($lines | Select-String -SimpleMatch $pattern -Quiet)) {
        throw "Velocity-match smoke is missing '$pattern'. Log: $($sessionLog.FullName)"
    }
}
$fatal = @($lines | Select-String -Pattern "\[(ERROR|CRITICAL)\]")
if ($fatal.Count -gt 0) {
    throw "Velocity-match smoke recorded errors:`n$($fatal.Line -join "`n")"
}

Write-Host "Velocity-match smoke test passed." -ForegroundColor Green
Write-Host "  Log: $($sessionLog.FullName)"
