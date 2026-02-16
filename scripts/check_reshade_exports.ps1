# Scan local DLL files for ReShadeRegisterAddon export
# Usage: .\check_reshade_exports.ps1 [path]
#   path: directory to scan (default: current directory). Scans recursively.
# Example: .\check_reshade_exports.ps1 .
#          .\check_reshade_exports.ps1 "C:\Games\MyGame"
#
# DLLs that export ReShadeRegisterAddon are the ReShade loader (e.g. dxgi.dll, d3d11.dll,
# ReShade64.dll when used as the API proxy). Addons and Display Commander's proxy_dll do
# not export it, so they will not appear in the results.

param(
    [Parameter(Position = 0)]
    [string]$Path = "."
)

$ErrorActionPreference = "Stop"
$ExportName = "ReShadeRegisterAddon"

# Parse PE and return list of export names, or $null on error/invalid PE
function Get-PeExportNames {
    param([string]$FilePath)
    try {
        $bytes = [System.IO.File]::ReadAllBytes($FilePath)
    } catch {
        return $null
    }
    if ($bytes.Length -lt 64) { return $null }
    # DOS header: e_lfanew at offset 0x3C
    $e_lfanew = [BitConverter]::ToInt32($bytes, 0x3C)
    if ($e_lfanew -le 0 -or $e_lfanew -ge $bytes.Length - 6) { return $null }
    # PE signature
    $pe = [System.Text.Encoding]::ASCII.GetString($bytes, $e_lfanew, 4)
    if ($pe -ne "PE`0`0") { return $null }
    $coff = $e_lfanew + 4
    $machine = [BitConverter]::ToUInt16($bytes, $coff + 0)
    $numSections = [BitConverter]::ToUInt16($bytes, $coff + 2)
    $sizeOptionalHeader = [BitConverter]::ToUInt16($bytes, $coff + 16)
    $optHeader = $coff + 20
    if ($optHeader + $sizeOptionalHeader -gt $bytes.Length) { return $null }
    # Optional header: Magic at 0
    $magic = [BitConverter]::ToUInt16($bytes, $optHeader + 0)
    # Data directory: Export is first; offset 96 for PE32, 112 for PE32+
    $ddOffset = if ($magic -eq 0x20b) { 112 } else { 96 }
    $exportRva = [BitConverter]::ToUInt32($bytes, $optHeader + $ddOffset + 0)
    if ($exportRva -eq 0) { return @() }
    # Section headers
    $sectionHeader = $optHeader + $sizeOptionalHeader
    $sectionSize = 40
    $exportFileOffset = $null
    for ($i = 0; $i -lt $numSections; $i++) {
        $sec = $sectionHeader + ($i * $sectionSize)
        if ($sec + $sectionSize -gt $bytes.Length) { break }
        $va = [BitConverter]::ToUInt32($bytes, $sec + 12)
        $rawSize = [BitConverter]::ToUInt32($bytes, $sec + 16)
        $rawPtr = [BitConverter]::ToUInt32($bytes, $sec + 20)
        if ($exportRva -ge $va -and $exportRva -lt $va + $rawSize) {
            $exportFileOffset = $rawPtr + ($exportRva - $va)
            break
        }
    }
    if ($null -eq $exportFileOffset -or $exportFileOffset + 40 -gt $bytes.Length) { return $null }
    # IMAGE_EXPORT_DIRECTORY: NumberOfNames at +0x18, AddressOfNames at +0x20
    $numNames = [BitConverter]::ToUInt32($bytes, $exportFileOffset + 0x18)
    $addrNamesRva = [BitConverter]::ToUInt32($bytes, $exportFileOffset + 0x20)
    if ($numNames -eq 0 -or $addrNamesRva -eq 0) { return @() }
    # Resolve AddressOfNames RVA to file offset
    $namesFileOffset = $null
    for ($i = 0; $i -lt $numSections; $i++) {
        $sec = $sectionHeader + ($i * $sectionSize)
        $va = [BitConverter]::ToUInt32($bytes, $sec + 12)
        $rawSize = [BitConverter]::ToUInt32($bytes, $sec + 16)
        $rawPtr = [BitConverter]::ToUInt32($bytes, $sec + 20)
        if ($addrNamesRva -ge $va -and $addrNamesRva -lt $va + $rawSize) {
            $namesFileOffset = $rawPtr + ($addrNamesRva - $va)
            break
        }
    }
    if ($null -eq $namesFileOffset) { return $null }
    # Names array is array of RVA (4 bytes each for PE32/PE32+)
    $names = [System.Collections.ArrayList]::new()
    for ($j = 0; $j -lt $numNames; $j++) {
        $nameRvaOffset = $namesFileOffset + ($j * 4)
        if ($nameRvaOffset + 4 -gt $bytes.Length) { break }
        $nameRva = [BitConverter]::ToUInt32($bytes, $nameRvaOffset)
        # Resolve name RVA to file offset
        $nameFileOffset = $null
        for ($i = 0; $i -lt $numSections; $i++) {
            $sec = $sectionHeader + ($i * $sectionSize)
            $va = [BitConverter]::ToUInt32($bytes, $sec + 12)
            $rawSize = [BitConverter]::ToUInt32($bytes, $sec + 16)
            $rawPtr = [BitConverter]::ToUInt32($bytes, $sec + 20)
            if ($nameRva -ge $va -and $nameRva -lt $va + $rawSize) {
                $nameFileOffset = $rawPtr + ($nameRva - $va)
                break
            }
        }
        if ($null -eq $nameFileOffset) { continue }
        $end = $nameFileOffset
        while ($end -lt $bytes.Length -and $bytes[$end] -ne 0) { $end++ }
        $name = [System.Text.Encoding]::ASCII.GetString($bytes, $nameFileOffset, $end - $nameFileOffset)
        [void]$names.Add($name)
    }
    return $names
}

$root = (Resolve-Path -LiteralPath $Path -ErrorAction SilentlyContinue).Path
if (-not $root) {
    Write-Error "Path not found: $Path"
    exit 1
}

$dlls = Get-ChildItem -LiteralPath $root -Recurse -Include "*.dll","*.DLL" -File -ErrorAction SilentlyContinue
# Also include addon extensions (they are DLLs)
$addons = Get-ChildItem -LiteralPath $root -Recurse -Include "*.addon64","*.addon32" -File -ErrorAction SilentlyContinue
$allFiles = @($dlls) + @($addons) | Sort-Object -Property FullName -Unique

$found = [System.Collections.ArrayList]::new()
$scanned = 0
$errors = 0

foreach ($f in $allFiles) {
    $scanned++
    $exports = Get-PeExportNames -FilePath $f.FullName
    if ($null -eq $exports) {
        $errors++
        Write-Verbose "Could not read PE or no exports: $($f.FullName)"
        continue
    }
    if ($exports -contains $ExportName) {
        $rel = if ($f.FullName.StartsWith($root)) { $f.FullName.Substring($root.Length).TrimStart('\','/') } else { $f.FullName }
        [void]$found.Add([PSCustomObject]@{ FullName = $f.FullName; RelativePath = $rel })
    }
}

Write-Host "Scanned $scanned file(s) under: $root"
if ($errors -gt 0) { Write-Host "  (Skipped/unreadable: $errors)" }
Write-Host ""
if ($found.Count -eq 0) {
    Write-Host "No DLL exports '$ExportName' (no ReShade loader found in this directory)."
    exit 0
}
Write-Host "DLL(s) that export '$ExportName' (ReShade loader):"
foreach ($o in $found) {
    Write-Host "  $($o.RelativePath)"
}
exit 0
