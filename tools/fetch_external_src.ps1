# Fetch reference sources into external-src (no git submodules).
# Run from repo root: ./tools/fetch_external_src.ps1

$ErrorActionPreference = "Stop"
# Script lives in <repo>/tools/; repo root is parent of tools
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$ExternalSrc = Join-Path $RepoRoot "external-src"

if (-not (Test-Path $RepoRoot)) {
    Write-Error "Repo root not found: $RepoRoot"
}

$sources = @(
    @{
        Name = "optiscaller"
        Url  = "https://github.com/optiscaler/OptiScaler.git"
    }
)

if (-not (Test-Path $ExternalSrc)) {
    New-Item -ItemType Directory -Path $ExternalSrc | Out-Null
    Write-Host "Created $ExternalSrc"
}

foreach ($s in $sources) {
    $dest = Join-Path $ExternalSrc $s.Name
    if (Test-Path $dest) {
        Write-Host "Exists: $dest (skip clone)"
        continue
    }
    Write-Host "Cloning $($s.Url) -> $dest (no submodules)"
    & git clone --no-recurse-submodules $s.Url $dest
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

Write-Host "Done. external-src contents:"
Get-ChildItem $ExternalSrc -Directory | ForEach-Object { Write-Host "  $($_.Name)" }
