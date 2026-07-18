[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [switch]$Clean,
    [switch]$ConfigureOnly
)

$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = Join-Path $projectRoot "out\build\windows-x64"

if (-not $env:VCPKG_ROOT) {
    throw "VCPKG_ROOT is not set. Point it to a bootstrapped vcpkg checkout."
}
$toolchain = Join-Path $env:VCPKG_ROOT "scripts\buildsystems\vcpkg.cmake"
if (-not (Test-Path -LiteralPath $toolchain)) {
    throw "vcpkg toolchain not found at '$toolchain'."
}

if ($Clean -and (Test-Path -LiteralPath $buildRoot)) {
    $resolvedProject = (Resolve-Path -LiteralPath $projectRoot).Path
    $resolvedBuild = (Resolve-Path -LiteralPath $buildRoot).Path
    if (-not $resolvedBuild.StartsWith($resolvedProject, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to clean a build directory outside the project."
    }
    Remove-Item -LiteralPath $resolvedBuild -Recurse -Force
}

Push-Location $projectRoot
try {
    & cmake --preset windows-x64
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configure failed with exit code $LASTEXITCODE."
    }

    if (-not $ConfigureOnly) {
        $buildPreset = $Configuration.ToLowerInvariant()
        & cmake --build --preset $buildPreset
        if ($LASTEXITCODE -ne 0) {
            throw "Build failed with exit code $LASTEXITCODE."
        }
    }
}
finally {
    Pop-Location
}
