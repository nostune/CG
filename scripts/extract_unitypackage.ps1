# Unity Package Extraction Script
# Usage: .\extract_unitypackage.ps1 -PackagePath "path\to\package.unitypackage" -OutputDir "path\to\output"

param(
    [Parameter(Mandatory=$true)]
    [string]$PackagePath,
    
    [Parameter(Mandatory=$false)]
    [string]$OutputDir = ".\extracted_assets"
)

# Check 7-Zip
$7zipPath = "C:\Program Files\7-Zip\7z.exe"
if (-not (Test-Path $7zipPath)) {
    $7zipPath = "C:\Program Files (x86)\7-Zip\7z.exe"
}
if (-not (Test-Path $7zipPath)) {
    Write-Host "Error: Please install 7-Zip first (https://www.7-zip.org/)" -ForegroundColor Red
    exit 1
}

# Check input file
if (-not (Test-Path $PackagePath)) {
    Write-Host "Error: File not found - $PackagePath" -ForegroundColor Red
    exit 1
}

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Unity Package Extraction Tool" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Input: $PackagePath"
Write-Host "Output: $OutputDir"
Write-Host ""

# Create temp directory
$tempDir = Join-Path $env:TEMP "unitypackage_temp_$(Get-Random)"
New-Item -ItemType Directory -Force -Path $tempDir | Out-Null

# Create output directory
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

try {
    Write-Host "Step 1/3: Extracting .unitypackage (tar.gz)..." -ForegroundColor Yellow
    
    # Copy and rename to .tar.gz
    $tarGzPath = Join-Path $tempDir "package.tar.gz"
    Copy-Item $PackagePath $tarGzPath
    
    # Extract gz (produces archtemp.tar or package.tar)
    & $7zipPath x $tarGzPath -o"$tempDir" -y | Out-Null
    
    # Find the tar file (could be archtemp.tar or package.tar)
    $tarFile = Get-ChildItem -Path $tempDir -Filter "*.tar" | Select-Object -First 1
    if (-not $tarFile) {
        throw "Failed to extract tar file from gzip"
    }
    $tarPath = $tarFile.FullName
    Write-Host "  Found tar: $($tarFile.Name)" -ForegroundColor Gray
    
    # Extract tar
    $extractedDir = Join-Path $tempDir "extracted"
    & $7zipPath x $tarPath -o"$extractedDir" -y | Out-Null
    
    Write-Host "Step 2/3: Organizing asset files..." -ForegroundColor Yellow
    
    # Iterate all GUID folders
    $assetFolders = Get-ChildItem -Path $extractedDir -Directory
    $extractedCount = 0
    $modelExtensions = @(".fbx", ".obj", ".dae", ".blend", ".3ds", ".max", ".gltf", ".glb")
    $textureExtensions = @(".png", ".jpg", ".jpeg", ".tga", ".psd", ".exr", ".hdr", ".tif", ".tiff")
    $materialExtensions = @(".mat")
    
    foreach ($folder in $assetFolders) {
        $pathnameFile = Join-Path $folder.FullName "pathname"
        $assetFile = Join-Path $folder.FullName "asset"
        
        if ((Test-Path $pathnameFile) -and (Test-Path $assetFile)) {
            # Read original path
            $originalPath = Get-Content $pathnameFile -Raw
            $originalPath = $originalPath.Trim()
            
            # Skip .meta files and scripts
            if ($originalPath -match "\.meta$" -or $originalPath -match "\.cs$") {
                continue
            }
            
            # Get file extension
            $extension = [System.IO.Path]::GetExtension($originalPath).ToLower()
            
            # Determine output subdirectory
            $subDir = "other"
            if ($modelExtensions -contains $extension) {
                $subDir = "models"
            } elseif ($textureExtensions -contains $extension) {
                $subDir = "textures"
            } elseif ($materialExtensions -contains $extension) {
                $subDir = "materials"
            }
            
            # Create output path
            $fileName = [System.IO.Path]::GetFileName($originalPath)
            $outputSubDir = Join-Path $OutputDir $subDir
            New-Item -ItemType Directory -Force -Path $outputSubDir | Out-Null
            
            $outputPath = Join-Path $outputSubDir $fileName
            
            # Handle duplicate filenames
            $counter = 1
            $baseName = [System.IO.Path]::GetFileNameWithoutExtension($fileName)
            while (Test-Path $outputPath) {
                $newName = "${baseName}_${counter}${extension}"
                $outputPath = Join-Path $outputSubDir $newName
                $counter++
            }
            
            # Copy file
            Copy-Item $assetFile $outputPath
            $extractedCount++
            
            Write-Host "  Extracted: $originalPath" -ForegroundColor Gray
        }
    }
    
    Write-Host ""
    Write-Host "Step 3/3: Cleaning up temp files..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force $tempDir
    
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Green
    Write-Host "Done! Extracted $extractedCount files" -ForegroundColor Green
    Write-Host "========================================" -ForegroundColor Green
    Write-Host ""
    Write-Host "Extracted files are in: $OutputDir"
    Write-Host "  - models/    : 3D models - fbx, obj, glb"
    Write-Host "  - textures/  : Textures - png, jpg, tga"
    Write-Host "  - materials/ : Material definitions - mat"
    Write-Host "  - other/     : Other files"
    Write-Host ""
    Write-Host "Note: Unity .mat files cannot be used directly."
    Write-Host "      Use .fbx or .obj with your Assimp loader."
    
} catch {
    Write-Host "Error: $_" -ForegroundColor Red
    if (Test-Path $tempDir) {
        Remove-Item -Recurse -Force $tempDir
    }
    exit 1
}
