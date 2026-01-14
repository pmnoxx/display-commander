# Start injection service (forever mode)
# This script starts the injection service that will inject into all new processes indefinitely

param(
    [string]$AddonPath = "build\src\addons\display_commander\zzz_display_commander.addon64"
)

# Check if addon file exists
if (-not (Test-Path $AddonPath)) {
    Write-Error "Addon file not found at: $AddonPath"
    Write-Host "Please build the project first or specify the correct path with -AddonPath" -ForegroundColor Yellow
    exit 1
}

Write-Host "Starting injection service (forever mode)..." -ForegroundColor Green
Write-Host "Addon path: $AddonPath" -ForegroundColor Cyan

# Start injection in background
$addonFullPath = Resolve-Path $AddonPath
Start-Process -FilePath "rundll32.exe" -ArgumentList "`"$addonFullPath`",Start" -WindowStyle Hidden

Write-Host "Injection service started!" -ForegroundColor Green
Write-Host "The service will inject into all new processes indefinitely." -ForegroundColor Yellow
Write-Host "Use stop_service64.ps1 to stop the injection service." -ForegroundColor Cyan
