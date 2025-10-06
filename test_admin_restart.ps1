# Test driver restart with admin privileges
Write-Host "Testing driver restart with admin privileges..."
Write-Host ""

# Check if running as admin
$isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole] "Administrator")

if (-not $isAdmin) {
    Write-Host "Not running as administrator. Please run this script as administrator."
    Write-Host "Right-click PowerShell and select 'Run as Administrator'"
    pause
    exit 1
}

Write-Host "Running as administrator. Starting driver restart test..."
Write-Host ""

# Run the driver restart tool with verbose mode
& "D:\repos\reshade-display-commander\build\tools\driver_restart.exe" /v

Write-Host ""
Write-Host "Driver restart test completed."
Write-Host "Exit code: $LASTEXITCODE"

# Check if log file was created
if (Test-Path "driver_restart_log.txt") {
    Write-Host ""
    Write-Host "Log file contents:"
    Write-Host "=================="
    Get-Content "driver_restart_log.txt"
}

pause
