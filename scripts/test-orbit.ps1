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
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "Executable not found at '$executable'."
}

$knownLogs = @{}
if (Test-Path -LiteralPath $logDirectory) {
    Get-ChildItem -LiteralPath $logDirectory -Filter "OuterWilds-*.log" -File |
        ForEach-Object { $knownLogs[$_.FullName] = $true }
}

Push-Location $projectRoot
try {
    & $executable --orbit-smoke *> $null
    $exitCode = $LASTEXITCODE
}
finally {
    Pop-Location
}

if ($exitCode -ne 0) {
    throw "Orbit smoke executable exited with code $exitCode."
}

$sessionLog = Get-ChildItem -LiteralPath $logDirectory -Filter "OuterWilds-*.log" -File |
    Where-Object { -not $knownLogs.ContainsKey($_.FullName) } |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1
if (-not $sessionLog) {
    throw "Orbit smoke run did not create a new session log."
}

$logLines = Get-Content -LiteralPath $sessionLog.FullName
$requiredPatterns = @(
    "Automated circular orbit smoke mode enabled",
    "Prepared Earth circularization assist",
    "flight.establish_orbit -> active",
    "flight.establish_orbit -> completed",
    "Program exiting normally"
)

$missingPatterns = @(
    foreach ($pattern in $requiredPatterns) {
        if (-not ($logLines | Select-String -SimpleMatch $pattern -Quiet)) {
            $pattern
        }
    }
)
if ($missingPatterns.Count -gt 0) {
    throw "Orbit smoke log is missing:`n  $($missingPatterns -join "`n  ")`nLog: $($sessionLog.FullName)"
}

$fatalLines = @($logLines | Select-String -Pattern "\[(ERROR|CRITICAL)\]")
if ($fatalLines.Count -gt 0) {
    throw "Orbit smoke recorded errors:`n  $($fatalLines.Line -join "`n  ")"
}

Write-Host "Circular orbit smoke test passed." -ForegroundColor Green
Write-Host "  Log: $($sessionLog.FullName)"
