# PowerShell script to remove trailing whitespace from all source files
# Usage: .\fix_whitespace.ps1

Write-Host "Fixing trailing whitespace in source files..."

# Get all source files
$files = Get-ChildItem -Path "src" -Recurse -Include "*.cpp", "*.hpp", "*.h", "*.c"

$totalFiles = 0
$modifiedFiles = 0

foreach ($file in $files) {
    $totalFiles++
    $content = Get-Content $file.FullName -Raw
    $originalLength = $content.Length

    # Remove trailing whitespace from each line
    $lines = $content -split "`r?`n"
    $fixedLines = $lines | ForEach-Object { $_ -replace '\s+$', '' }
    $newContent = $fixedLines -join "`n"

    if ($newContent.Length -ne $originalLength) {
        Set-Content -Path $file.FullName -Value $newContent -NoNewline
        Write-Host "Fixed: $($file.FullName)"
        $modifiedFiles++
    }
}

Write-Host "Processed $totalFiles files, modified $modifiedFiles files"
