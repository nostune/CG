[CmdletBinding()]
param(
    [string]$ArchivePath,
    [string]$ManifestPath,
    [switch]$Force
)

$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $ArchivePath) {
    $ArchivePath = Join-Path (Split-Path -Parent $projectRoot) "OuterWilds\OuterWIlds_release.zip"
}
if (-not $ManifestPath) {
    $ManifestPath = Join-Path $projectRoot "config\assets.required.txt"
}

if (-not (Test-Path -LiteralPath $ArchivePath -PathType Leaf)) {
    throw "Asset archive not found at '$ArchivePath'. Pass -ArchivePath explicitly."
}
if (-not (Test-Path -LiteralPath $ManifestPath -PathType Leaf)) {
    throw "Asset manifest not found at '$ManifestPath'."
}

Add-Type -AssemblyName System.IO.Compression.FileSystem

$requiredPaths = Get-Content -LiteralPath $ManifestPath |
    ForEach-Object { $_.Trim().Replace("\", "/") } |
    Where-Object { $_ -and -not $_.StartsWith("#") }

$archive = [IO.Compression.ZipFile]::OpenRead((Resolve-Path -LiteralPath $ArchivePath))
try {
    $entries = @{}
    foreach ($entry in $archive.Entries) {
        $entries[$entry.FullName.Replace("\", "/")] = $entry
    }

    $missingFromArchive = @($requiredPaths | Where-Object { -not $entries.ContainsKey($_) })
    if ($missingFromArchive.Count -gt 0) {
        throw "Archive is missing required entries:`n  $($missingFromArchive -join "`n  ")"
    }

    $restored = 0
    $skipped = 0
    foreach ($relativePath in $requiredPaths) {
        $destination = Join-Path $projectRoot $relativePath
        if ((Test-Path -LiteralPath $destination -PathType Leaf) -and -not $Force) {
            $skipped++
            continue
        }

        $destinationDirectory = Split-Path -Parent $destination
        New-Item -ItemType Directory -Path $destinationDirectory -Force | Out-Null

        $sourceStream = $entries[$relativePath].Open()
        try {
            $destinationStream = [IO.File]::Create($destination)
            try {
                $sourceStream.CopyTo($destinationStream)
            }
            finally {
                $destinationStream.Dispose()
            }
        }
        finally {
            $sourceStream.Dispose()
        }
        $restored++
    }
}
finally {
    $archive.Dispose()
}

Write-Host "Asset restore complete: $restored restored, $skipped already present."
Write-Host "The original soundtrack is intentionally not part of the required manifest."
& (Join-Path $PSScriptRoot "check-assets.ps1") -ManifestPath $ManifestPath
