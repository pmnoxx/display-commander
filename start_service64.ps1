# Start 64-bit injection service (forever mode)
# This script starts the 64-bit injection service that will inject into all new 64-bit processes indefinitely

param(
    [string]$AddonPath = "build\src\addons\display_commander\zzz_display_commander.addon64"
)

# Check if addon file exists
if (-not (Test-Path $AddonPath)) {
    Write-Error "Addon file not found at: $AddonPath"
    Write-Host "Please build the project first or specify the correct path with -AddonPath" -ForegroundColor Yellow
    exit 1
}

Write-Host "Starting 64-bit injection service (forever mode)..." -ForegroundColor Green
Write-Host "Addon path: $AddonPath" -ForegroundColor Cyan

# Start injection in background using 64-bit rundll32
$addonFullPath = Resolve-Path $AddonPath
$sys32Path = Join-Path $env:SystemRoot "System32\rundll32.exe"
Start-Process -FilePath $sys32Path -ArgumentList "`"$addonFullPath`",Start" -WindowStyle Hidden

Write-Host "64-bit injection service started!" -ForegroundColor Green
Write-Host "The service will inject into all new 64-bit processes indefinitely." -ForegroundColor Yellow
Write-Host "Use stop_service64.ps1 to stop the injection service." -ForegroundColor Cyan
