# Download ReShade Addon installers (6.7.2, 6.7.1, 6.6.2), extract ReShade64.dll and ReShade32.dll,
# and compute SHA256 for each. Output can be used to populate reshade_sha256_database.cpp.
#
# Usage: .\download_reshade_hashes.ps1 [-OutDir <path>]
#   -OutDir: directory for downloads and extraction (default: $env:TEMP\dc_reshade_hashes).
# Run offline after copying the script to a machine with network: download once, then run again
# with -OutDir pointing to the same folder to only compute hashes from already-downloaded files.
#
# Requires: Windows 10+ (tar.exe for extraction). PowerShell 5.1+.

param(
    [string]$OutDir = (Join-Path $env:TEMP "dc_reshade_hashes")
)

$ErrorActionPreference = "Stop"
$BaseUrl = "https://reshade.me/downloads"
$Versions = @("6.7.2", "6.7.1", "6.6.2")

function Get-FileSha256Hex {
    param([string]$FilePath)
    $hash = Get-FileHash -Path $FilePath -Algorithm SHA256
    return $hash.Hash.ToLowerInvariant()
}

function Extract-ReshadeDlls {
    param([string]$ExePath, [string]$DestDir)
    if (-not (Test-Path $ExePath)) { return $false }
    $null = New-Item -ItemType Directory -Path $DestDir -Force
    # ReShade Addon installer is a self-extracting archive; Windows 10+ tar can extract it.
    # Use absolute path for archive since working directory is the extraction folder.
    $proc = Start-Process -FilePath "tar.exe" -ArgumentList "-xf", $ExePath, "ReShade64.dll", "ReShade32.dll" -WorkingDirectory $DestDir -Wait -PassThru -NoNewWindow
    if ($proc.ExitCode -ne 0) { return $false }
    return (Test-Path (Join-Path $DestDir "ReShade64.dll")) -and (Test-Path (Join-Path $DestDir "ReShade32.dll"))
}

$results = @()
$outPath = [System.IO.Path]::GetFullPath($OutDir)
Write-Host "Output directory: $outPath"
$null = New-Item -ItemType Directory -Path $outPath -Force

foreach ($ver in $Versions) {
    $exeName = "ReShade_Setup_${ver}_Addon.exe"
    $exePath = Join-Path $outPath $exeName
    $extractDir = Join-Path $outPath $ver

    if (-not (Test-Path $exePath)) {
        $url = "$BaseUrl/$exeName"
        Write-Host "Downloading $url ..."
        try {
            Invoke-WebRequest -Uri $url -OutFile $exePath -UseBasicParsing
        } catch {
            Write-Warning "Download failed for $ver : $_"
            continue
        }
    } else {
        Write-Host "Using existing $exeName"
    }

    if (-not (Extract-ReshadeDlls -ExePath $exePath -DestDir $extractDir)) {
        Write-Warning "Extract failed for $ver (ensure tar.exe is available)."
        continue
    }

    $dll64 = Join-Path $extractDir "ReShade64.dll"
    $dll32 = Join-Path $extractDir "ReShade32.dll"
    $sha64 = Get-FileSha256Hex -FilePath $dll64
    $sha32 = Get-FileSha256Hex -FilePath $dll32
    $results += [PSCustomObject]@{ Version = $ver; Sha256_64 = $sha64; Sha256_32 = $sha32 }
    Write-Host "  $ver  ReShade64.dll SHA256: $sha64"
    Write-Host "  $ver  ReShade32.dll SHA256: $sha32"
}

Write-Host ""
Write-Host "--- C++ database snippet (paste into reshade_sha256_database.cpp s_reshade_sha256_db) ---"
foreach ($r in $results) {
    Write-Host ("    {`"" + $r.Version + "`", `"" + $r.Sha256_64 + "`", `"" + $r.Sha256_32 + "`"},")
}
Write-Host "--- end ---"
