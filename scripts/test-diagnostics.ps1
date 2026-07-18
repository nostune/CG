[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [switch]$SkipBuild,
    [switch]$ProbePvd,
    [switch]$ContactDebug
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

$knownLogs = @{}
if (Test-Path -LiteralPath $logDirectory) {
    Get-ChildItem -LiteralPath $logDirectory -Filter "OuterWilds-*.log" -File |
        ForEach-Object { $knownLogs[$_.FullName] = $true }
}

$previousPvd = $env:OUTERWILDS_PVD
$previousContactDebug = $env:OUTERWILDS_CONTACT_DEBUG
try {
    if ($ProbePvd) {
        $env:OUTERWILDS_PVD = "1"
    } else {
        Remove-Item Env:OUTERWILDS_PVD -ErrorAction SilentlyContinue
    }
    if ($ContactDebug) {
        $env:OUTERWILDS_CONTACT_DEBUG = "1"
    } else {
        Remove-Item Env:OUTERWILDS_CONTACT_DEBUG -ErrorAction SilentlyContinue
    }

    Push-Location $projectRoot
    try {
        # Legacy systems still write directly to inherited stdout. The structured
        # session log below is the authoritative diagnostic output for this test.
        & $executable --diagnostics-smoke *> $null
        $exitCode = $LASTEXITCODE
    }
    finally {
        Pop-Location
    }
}
finally {
    $env:OUTERWILDS_PVD = $previousPvd
    $env:OUTERWILDS_CONTACT_DEBUG = $previousContactDebug
}

if ($exitCode -ne 0) {
    throw "Diagnostics smoke executable exited with code $exitCode."
}

$sessionLog = Get-ChildItem -LiteralPath $logDirectory -Filter "OuterWilds-*.log" -File |
    Where-Object { -not $knownLogs.ContainsKey($_.FullName) } |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1
if (-not $sessionLog) {
    throw "Diagnostics smoke run did not create a new session log."
}

$logLines = Get-Content -LiteralPath $sessionLog.FullName
$requiredPatterns = @(
    "Automated smoke mode enabled",
    "Entering main game loop",
    "Debug geometry ready:",
    "Automatic diagnostic stop reached",
    "PhysX shutdown successfully",
    "Program exiting normally"
)
if ($ContactDebug) {
    $requiredPatterns += "[PhysXContact]"
}

$missingPatterns = @(
    foreach ($pattern in $requiredPatterns) {
        if (-not ($logLines | Select-String -SimpleMatch $pattern -Quiet)) {
            $pattern
        }
    }
)
if ($missingPatterns.Count -gt 0) {
    throw "Diagnostics smoke log is missing:`n  $($missingPatterns -join "`n  ")"
}

$fatalLines = @($logLines | Select-String -Pattern "\[(ERROR|CRITICAL)\]")
if ($fatalLines.Count -gt 0) {
    throw "Diagnostics smoke recorded errors:`n  $($fatalLines.Line -join "`n  ")"
}

$geometryLine = $logLines | Select-String -SimpleMatch "Debug geometry ready:" | Select-Object -First 1
$pvdLine = $logLines | Select-String -Pattern "\[(PVD)\]" | Select-Object -First 1
Write-Host "Diagnostics smoke test passed." -ForegroundColor Green
Write-Host "  Log: $($sessionLog.FullName)"
Write-Host "  $($geometryLine.Line)"
if ($ProbePvd -and $pvdLine) {
    Write-Host "  PVD probe: $($pvdLine.Line)"
}
