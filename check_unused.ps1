# check_unused.ps1 - Unused Code Analysis Script for ReShade Display Commander
# This script analyzes the codebase to find unused functions, headers, and files

Write-Host "=== ReShade Display Commander - Unused Code Analysis ===" -ForegroundColor Green
Write-Host ""

# Get all source files
$sourceFiles = Get-ChildItem -Path "src/addons/display_commander" -Recurse -Filter "*.cpp"
$headerFiles = Get-ChildItem -Path "src/addons/display_commander" -Recurse -Filter "*.hpp"

Write-Host "Found $($sourceFiles.Count) source files and $($headerFiles.Count) header files" -ForegroundColor Yellow
Write-Host ""

# Function to extract function names from a file
function Get-FunctionNames {
    param([string]$filePath)
    $content = Get-Content $filePath -Raw
    $matches = [regex]::Matches($content, '^\s*(static\s+)?[a-zA-Z_][a-zA-Z0-9_]*\s+([a-zA-Z_][a-zA-Z0-9_]*)\s*\([^)]*\)\s*\{', [System.Text.RegularExpressions.RegexOptions]::Multiline)
    return $matches | ForEach-Object { $_.Groups[2].Value }
}

# Function to count function usage across all files
function Get-FunctionUsage {
    param([string]$functionName)
    $count = 0
    $sourceFiles | ForEach-Object {
        $content = Get-Content $_.FullName -Raw
        $count += ([regex]::Matches($content, "\b$functionName\s*\(")).Count
    }
    return $count
}

# Collect all function names
Write-Host "Extracting function definitions..." -ForegroundColor Cyan
$allFunctions = @()
$sourceFiles | ForEach-Object {
    $functions = Get-FunctionNames $_.FullName
    $functions | ForEach-Object { $allFunctions += $_ }
}

$uniqueFunctions = $allFunctions | Sort-Object | Get-Unique
Write-Host "Found $($uniqueFunctions.Count) unique functions" -ForegroundColor Yellow
Write-Host ""

# Analyze function usage
Write-Host "Analyzing function usage..." -ForegroundColor Cyan
$unusedFunctions = @()
$usedFunctions = @()

foreach ($func in $uniqueFunctions) {
    $usageCount = Get-FunctionUsage $func
    if ($usageCount -eq 1) {
        $unusedFunctions += $func
    } else {
        $usedFunctions += $func
    }
}

Write-Host "Function Analysis Results:" -ForegroundColor Green
Write-Host "  Used functions: $($usedFunctions.Count)" -ForegroundColor Green
Write-Host "  Unused functions: $($unusedFunctions.Count)" -ForegroundColor Red
Write-Host ""

# Categorize unused functions
Write-Host "Categorizing unused functions..." -ForegroundColor Cyan
$hookFunctions = $unusedFunctions | Where-Object { $_ -match "Are.*Hooks.*Installed|Install.*Hooks|Uninstall.*Hooks" }
$reshadeFunctions = $unusedFunctions | Where-Object { $_ -match "^On[A-Z]" }
$uiFunctions = $unusedFunctions | Where-Object { $_ -match "Draw.*|Init.*|Cleanup.*|Initialize.*" }
$utilityFunctions = $unusedFunctions | Where-Object { $_ -match "Get.*|Set.*|Is.*|Has.*" }
$threadFunctions = $unusedFunctions | Where-Object { $_ -match ".*Thread$|.*Monitor$" }
$otherFunctions = $unusedFunctions | Where-Object { $_ -notmatch "Are.*Hooks.*Installed|Install.*Hooks|Uninstall.*Hooks|^On[A-Z]|Draw.*|Init.*|Cleanup.*|Initialize.*|Get.*|Set.*|Is.*|Has.*|.*Thread$|.*Monitor$" }

Write-Host "Unused Function Categories:" -ForegroundColor Yellow
Write-Host "  Hook Management: $($hookFunctions.Count)" -ForegroundColor Red
Write-Host "  ReShade Event Handlers: $($reshadeFunctions.Count)" -ForegroundColor Red
Write-Host "  UI/Widget Functions: $($uiFunctions.Count)" -ForegroundColor Red
Write-Host "  Utility Functions: $($utilityFunctions.Count)" -ForegroundColor Red
Write-Host "  Thread Functions: $($threadFunctions.Count)" -ForegroundColor Red
Write-Host "  Other Functions: $($otherFunctions.Count)" -ForegroundColor Red
Write-Host ""

# Analyze unused headers
Write-Host "Analyzing header file usage..." -ForegroundColor Cyan
$unusedHeaders = @()
foreach ($header in $headerFiles) {
    $headerName = $header.Name
    $usageCount = 0
    $sourceFiles | ForEach-Object {
        $content = Get-Content $_.FullName -Raw
        if ($content -match "#include.*$headerName") {
            $usageCount++
        }
    }
    if ($usageCount -eq 0) {
        $unusedHeaders += $headerName
    }
}

Write-Host "Header Analysis Results:" -ForegroundColor Green
Write-Host "  Used headers: $($headerFiles.Count - $unusedHeaders.Count)" -ForegroundColor Green
Write-Host "  Unused headers: $($unusedHeaders.Count)" -ForegroundColor Red
Write-Host ""

# Check for potentially unused files (files without main functions or exports)
Write-Host "Analyzing file usage patterns..." -ForegroundColor Cyan
$potentiallyUnusedFiles = @()
$sourceFiles | Where-Object { $_.Name -notmatch "main_entry|addon" } | ForEach-Object {
    $file = $_.Name
    $content = Get-Content $_.FullName -Raw
    $hasMain = $content -match "int main\s*\("
    $hasDllMain = $content -match "BOOL.*DllMain"
    $hasExports = $content -match "extern.*C.*__declspec.*dllexport"
    if (-not $hasMain -and -not $hasDllMain -and -not $hasExports) {
        $potentiallyUnusedFiles += $file
    }
}

Write-Host "File Analysis Results:" -ForegroundColor Green
Write-Host "  Entry point files: 2 (main_entry.cpp, addon.cpp)" -ForegroundColor Green
Write-Host "  Potentially unused files: $($potentiallyUnusedFiles.Count)" -ForegroundColor Yellow
Write-Host ""

# Generate detailed reports
Write-Host "=== DETAILED UNUSED FUNCTIONS REPORT ===" -ForegroundColor Magenta
Write-Host ""

if ($hookFunctions.Count -gt 0) {
    Write-Host "Hook Management Functions ($($hookFunctions.Count)):" -ForegroundColor Red
    $hookFunctions | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
    Write-Host ""
}

if ($reshadeFunctions.Count -gt 0) {
    Write-Host "ReShade Event Handlers ($($reshadeFunctions.Count)):" -ForegroundColor Red
    $reshadeFunctions | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
    Write-Host ""
}

if ($uiFunctions.Count -gt 0) {
    Write-Host "UI/Widget Functions ($($uiFunctions.Count)):" -ForegroundColor Red
    $uiFunctions | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
    Write-Host ""
}

if ($utilityFunctions.Count -gt 0) {
    Write-Host "Utility Functions ($($utilityFunctions.Count)):" -ForegroundColor Red
    $utilityFunctions | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
    Write-Host ""
}

if ($threadFunctions.Count -gt 0) {
    Write-Host "Thread Functions ($($threadFunctions.Count)):" -ForegroundColor Red
    $threadFunctions | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
    Write-Host ""
}

if ($otherFunctions.Count -gt 0) {
    Write-Host "Other Functions ($($otherFunctions.Count)):" -ForegroundColor Red
    $otherFunctions | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
    Write-Host ""
}

if ($unusedHeaders.Count -gt 0) {
    Write-Host "=== UNUSED HEADER FILES ===" -ForegroundColor Magenta
    $unusedHeaders | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
    Write-Host ""
}

# Generate summary
Write-Host "=== SUMMARY ===" -ForegroundColor Magenta
Write-Host "Total unused functions: $($unusedFunctions.Count)" -ForegroundColor Red
Write-Host "Total unused headers: $($unusedHeaders.Count)" -ForegroundColor Red
Write-Host "Potentially unused files: $($potentiallyUnusedFiles.Count)" -ForegroundColor Yellow
Write-Host ""

Write-Host "=== RECOMMENDATIONS ===" -ForegroundColor Magenta
Write-Host "1. Review unused functions before removing - some may be called via function pointers" -ForegroundColor Yellow
Write-Host "2. Check for conditional compilation blocks that might use these functions" -ForegroundColor Yellow
Write-Host "3. Consider removing unused headers to reduce build dependencies" -ForegroundColor Yellow
Write-Host "4. Removing unused code could significantly reduce final .addon64 file size" -ForegroundColor Yellow
Write-Host "5. Focus on hook management functions first - they appear to be safe to remove" -ForegroundColor Yellow
Write-Host ""

Write-Host "Analysis complete! Check the output above for detailed results." -ForegroundColor Green
