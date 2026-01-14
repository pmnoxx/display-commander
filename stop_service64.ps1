# Stop injection service
# This script stops the running injection service by calling Stop

param(
    [string]$AddonPath = "build\src\addons\display_commander\zzz_display_commander.addon64"
)

# Check if addon file exists
if (-not (Test-Path $AddonPath)) {
    Write-Error "Addon file not found at: $AddonPath"
    Write-Host "Please build the project first or specify the correct path with -AddonPath" -ForegroundColor Yellow
    exit 1
}

Write-Host "Stopping injection service..." -ForegroundColor Yellow
Write-Host "Addon path: $AddonPath" -ForegroundColor Cyan

# Stop injection
$addonFullPath = Resolve-Path $AddonPath
$sys32Path = Join-Path $env:SystemRoot "System32\rundll32.exe"
Start-Process -FilePath $sys32Path -ArgumentList "`"$addonFullPath`",Stop" -WindowStyle Hidden -Wait

Write-Host "Injection service stopped!" -ForegroundColor Green
