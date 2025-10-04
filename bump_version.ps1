# Display Commander Version Bumping Script
# Usage: .\bump_version.ps1 [major|minor|patch|build] [--build] [--commit] [--tag]

param(
    [Parameter(Mandatory=$true)]
    [ValidateSet("major", "minor", "patch", "build")]
    [string]$Type,

    [switch]$Build,
    [switch]$Commit,
    [switch]$Tag,
    [string]$Message = ""
)

# Colors for output
$Red = "`e[31m"
$Green = "`e[32m"
$Yellow = "`e[33m"
$Blue = "`e[34m"
$Reset = "`e[0m"

function Write-ColorOutput {
    param([string]$Text, [string]$Color = $Reset)
    Write-Host "${Color}${Text}${Reset}"
}

function Get-CurrentVersion {
    $versionFile = "src\addons\display_commander\version.hpp"
    if (-not (Test-Path $versionFile)) {
        Write-ColorOutput "Error: version.hpp not found at $versionFile" $Red
        exit 1
    }

    $content = Get-Content $versionFile -Raw

    # Extract version numbers using regex
    $majorMatch = [regex]::Match($content, '#define DISPLAY_COMMANDER_VERSION_MAJOR (\d+)')
    $minorMatch = [regex]::Match($content, '#define DISPLAY_COMMANDER_VERSION_MINOR (\d+)')
    $patchMatch = [regex]::Match($content, '#define DISPLAY_COMMANDER_VERSION_PATCH (\d+)')
    $buildMatch = [regex]::Match($content, '#define DISPLAY_COMMANDER_VERSION_BUILD (\d+)')

    if (-not $majorMatch.Success -or -not $minorMatch.Success -or -not $patchMatch.Success -or -not $buildMatch.Success) {
        Write-ColorOutput "Error: Could not parse version numbers from version.hpp" $Red
        exit 1
    }

    return @{
        Major = [int]$majorMatch.Groups[1].Value
        Minor = [int]$minorMatch.Groups[1].Value
        Patch = [int]$patchMatch.Groups[1].Value
        Build = [int]$buildMatch.Groups[1].Value
    }
}

function Update-VersionFile {
    param([hashtable]$Version)

    $versionFile = "src\addons\display_commander\version.hpp"
    $content = Get-Content $versionFile -Raw

    # Update individual version numbers
    $content = $content -replace '#define DISPLAY_COMMANDER_VERSION_MAJOR \d+', "#define DISPLAY_COMMANDER_VERSION_MAJOR $($Version.Major)"
    $content = $content -replace '#define DISPLAY_COMMANDER_VERSION_MINOR \d+', "#define DISPLAY_COMMANDER_VERSION_MINOR $($Version.Minor)"
    $content = $content -replace '#define DISPLAY_COMMANDER_VERSION_PATCH \d+', "#define DISPLAY_COMMANDER_VERSION_PATCH $($Version.Patch)"
    $content = $content -replace '#define DISPLAY_COMMANDER_VERSION_BUILD \d+', "#define DISPLAY_COMMANDER_VERSION_BUILD $($Version.Build)"

    # Update version string
    $versionString = "$($Version.Major).$($Version.Minor).$($Version.Patch).$($Version.Build)"
    $content = $content -replace '#define DISPLAY_COMMANDER_VERSION_STRING "[^"]*"', "#define DISPLAY_COMMANDER_VERSION_STRING `"$versionString`""

    # Update build date and time
    $buildDate = Get-Date -Format "yyyy-MM-dd"
    $buildTime = Get-Date -Format "HH:mm:ss"
    $content = $content -replace '#define DISPLAY_COMMANDER_BUILD_DATE "[^"]*"', "#define DISPLAY_COMMANDER_BUILD_DATE `"$buildDate`""
    $content = $content -replace '#define DISPLAY_COMMANDER_BUILD_TIME "[^"]*"', "#define DISPLAY_COMMANDER_BUILD_TIME `"$buildTime`""

    # Write back to file
    Set-Content -Path $versionFile -Value $content -NoNewline

    Write-ColorOutput "Updated version to: $versionString" $Green
    Write-ColorOutput "Build date: $buildDate $buildTime" $Green
}

function Build-Project {
    Write-ColorOutput "`nBuilding project..." $Blue

    # Clean and build
    if (Test-Path "build") {
        Remove-Item -Recurse -Force "build"
    }

    cmake -B build -S . -G "Ninja"
    if ($LASTEXITCODE -ne 0) {
        Write-ColorOutput "Error: CMake configuration failed" $Red
        exit 1
    }

    cmake --build build --target zzz_display_commander
    if ($LASTEXITCODE -ne 0) {
        Write-ColorOutput "Error: Build failed" $Red
        exit 1
    }

    Write-ColorOutput "Build completed successfully!" $Green
}

function Commit-Changes {
    param([hashtable]$Version, [string]$Message)

    if ([string]::IsNullOrEmpty($Message)) {
        $Message = "Bump version to $($Version.Major).$($Version.Minor).$($Version.Patch).$($Version.Build)"
    }

    Write-ColorOutput "`nCommitting changes..." $Blue

    git add src\addons\display_commander\version.hpp
    git commit -m $Message

    if ($LASTEXITCODE -ne 0) {
        Write-ColorOutput "Error: Git commit failed" $Red
        exit 1
    }

    Write-ColorOutput "Changes committed: $Message" $Green
}

function Create-Tag {
    param([hashtable]$Version)

    $tagName = "v$($Version.Major).$($Version.Minor).$($Version.Patch).$($Version.Build)"

    Write-ColorOutput "`nCreating tag: $tagName" $Blue

    git tag -a $tagName -m "Release $tagName"

    if ($LASTEXITCODE -ne 0) {
        Write-ColorOutput "Error: Git tag creation failed" $Red
        exit 1
    }

    Write-ColorOutput "Tag created: $tagName" $Green
}

# Main execution
Write-ColorOutput "Display Commander Version Bumper" $Blue
Write-ColorOutput "================================" $Blue

# Get current version
$currentVersion = Get-CurrentVersion
Write-ColorOutput "Current version: $($currentVersion.Major).$($currentVersion.Minor).$($currentVersion.Patch).$($currentVersion.Build)" $Yellow

# Calculate new version
$newVersion = $currentVersion.Clone()
switch ($Type) {
    "major" {
        $newVersion.Major++
        $newVersion.Minor = 0
        $newVersion.Patch = 0
        $newVersion.Build = 0
    }
    "minor" {
        $newVersion.Minor++
        $newVersion.Patch = 0
        $newVersion.Build = 0
    }
    "patch" {
        $newVersion.Patch++
        $newVersion.Build = 0
    }
    "build" {
        $newVersion.Build++
    }
}

Write-ColorOutput "New version: $($newVersion.Major).$($newVersion.Minor).$($newVersion.Patch).$($newVersion.Build)" $Green

# Confirm before proceeding
$confirm = Read-Host "`nProceed with version bump? (y/N)"
if ($confirm -ne "y" -and $confirm -ne "Y") {
    Write-ColorOutput "Version bump cancelled." $Yellow
    exit 0
}

# Update version file
Update-VersionFile -Version $newVersion

# Build if requested
if ($Build) {
    Build-Project
}

# Commit if requested
if ($Commit) {
    Commit-Changes -Version $newVersion -Message $Message
}

# Create tag if requested
if ($Tag) {
    Create-Tag -Version $newVersion
}

Write-ColorOutput "`nVersion bump completed successfully!" $Green
Write-ColorOutput "New version: $($newVersion.Major).$($newVersion.Minor).$($newVersion.Patch).$($newVersion.Build)" $Green

# Show usage examples
Write-ColorOutput "`nUsage examples:" $Blue
Write-ColorOutput "  .\bump_version.ps1 patch --build --commit" $Yellow
Write-ColorOutput "  .\bump_version.ps1 minor --build --commit --tag" $Yellow
Write-ColorOutput "  .\bump_version.ps1 build --build" $Yellow
Write-ColorOutput "  .\bump_version.ps1 major --build --commit --tag --message 'Major release with new features'" $Yellow
