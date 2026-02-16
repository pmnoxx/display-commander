# Display Commander CLI - invoke addon via rundll32 for scripting
# Usage: .\display_commander_cli.ps1 [command] [args...] [-AddonPath path]
# Example: .\display_commander_cli.ps1 version
#          .\display_commander_cli.ps1 DetectExe              (scan current directory)
#          .\display_commander_cli.ps1 DetectExe .             (same, explicit)
#          .\display_commander_cli.ps1 DetectExe "C:\Games\MyGame"
#          $v = .\display_commander_cli.ps1 version

param(
    [Parameter(Position = 0)]
    [string]$Command = "help",
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$CommandArgs = @(),
    [string]$AddonPath = ""
)

$ErrorActionPreference = "Stop"
# Check both possible addon filenames (use whichever exists)
$addonNames = @("zzz_DisplayCommander.addon64", "zzz.addon64", "zzz_display_commander.addon32", "zzz.addon32")

function Find-AddonPath {
    $localDir = Get-Location
    $searchDirs = @($localDir)
    $scriptPath = $MyInvocation.MyCommand.Path
    if (-not [string]::IsNullOrWhiteSpace($scriptPath)) {
        $scriptDir = Split-Path -Parent $scriptPath
        $searchDirs += $scriptDir
        $rootDir = Split-Path -Parent $scriptDir
        $searchDirs += (Join-Path $rootDir "build\src\addons\display_commander")
    }
    foreach ($dir in $searchDirs) {
        if ([string]::IsNullOrWhiteSpace($dir) -or -not (Test-Path -LiteralPath $dir -PathType Container)) { continue }
        foreach ($name in $addonNames) {
            $p = Join-Path $dir $name
            if (Test-Path -LiteralPath $p -PathType Leaf) {
                return $p
            }
        }
    }
    return $null
}

if ([string]::IsNullOrWhiteSpace($AddonPath)) {
    $AddonPath = Find-AddonPath
    if ([string]::IsNullOrWhiteSpace($AddonPath)) {
        throw "Could not find addon ($($addonNames -join ' or ')) in current or script directory. Set -AddonPath to the addon DLL path."
    }
}

$fullPath = (Resolve-Path -LiteralPath $AddonPath).Path
$addonDir = Split-Path -Parent $fullPath
$addonFile = Split-Path -Leaf $fullPath
$logPath = Join-Path $addonDir "CommandLine.log"

# Remember log size before run so we can read only the new output
$logOffset = 0
if (Test-Path -LiteralPath $logPath -PathType Leaf) {
    # TODO: delete old log file
    # $logOffset = (Get-Item -LiteralPath $logPath).Length
    Remove-Item -LiteralPath $logPath
}

# For DetectExe: default to current directory when no path given; resolve "." to full path
# so the addon (running in addon dir) gets the correct directory.
if ($Command -eq "DetectExe") {
    if (-not $CommandArgs -or $CommandArgs.Count -eq 0) {
        $CommandArgs = @((Get-Location).Path)
    } else {
        $resolved = @()
        foreach ($a in $CommandArgs) {
            if ($a -eq "." -or $a -eq "..") {
                $resolved += (Resolve-Path -LiteralPath $a).Path
            } else {
                $resolved += $a
            }
        }
        $CommandArgs = $resolved
    }
}

# Run from addon directory with relative path (like: rundll32.exe .\zzz.addon64,CommandLine)
# so the DLL and its dependencies are found correctly. Output is written to CommandLine.log.
Push-Location -LiteralPath $addonDir
try {
    & rundll32.exe ".\$addonFile,CommandLine" $Command $CommandArgs 2>&1 | Out-Null
} finally {
    Pop-Location
}

# Give the addon a moment to flush and close CommandLine.log
Start-Sleep -Milliseconds 150

# Read only the output from this run (content appended after $logOffset)
if (Test-Path -LiteralPath $logPath -PathType Leaf) {
    $stream = [System.IO.File]::Open($logPath, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
    try {
        if ($logOffset -lt $stream.Length) {
            $stream.Seek($logOffset, [System.IO.SeekOrigin]::Begin) | Out-Null
            $reader = New-Object System.IO.StreamReader($stream)
            $newContent = $reader.ReadToEnd()
            $reader.Dispose()
            Write-Output $newContent.TrimEnd()
        }
    } finally {
        $stream.Dispose()
    }
}
