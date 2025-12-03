# Clean up all build and application processes
Write-Host "Cleaning up OuterWilds build processes..." -ForegroundColor Cyan

$processNames = @(
    "*OuterWilds*",
    "*msbuild*",
    "*cl*",
    "*link*",
    "*cmake*",
    "*conhost*"
)

$killed = 0
foreach ($pattern in $processNames) {
    Get-Process | Where-Object { $_.ProcessName -like $pattern } | ForEach-Object {
        try {
            Write-Host "  Terminating: $($_.ProcessName) (PID: $($_.Id))" -ForegroundColor Yellow
            Stop-Process -Id $_.Id -Force -ErrorAction Stop
            $killed++
        } catch {
            Write-Host "  Failed to terminate: $($_.ProcessName) (PID: $($_.Id))" -ForegroundColor Red
        }
    }
}

if ($killed -eq 0) {
    Write-Host "No processes to clean up." -ForegroundColor Green
} else {
    Write-Host "`nSuccessfully terminated $killed process(es)." -ForegroundColor Green
}

# Optional: Clean up PDB locks
Write-Host "`nCleaning up temporary build files..." -ForegroundColor Cyan
if (Test-Path ".\build\*.pdb") {
    Remove-Item ".\build\*.pdb" -Force -ErrorAction SilentlyContinue
    Write-Host "  Removed PDB files" -ForegroundColor Gray
}

Write-Host "`nCleanup complete!" -ForegroundColor Green
