#!/bin/bash
# Bash script to remove trailing whitespace from all source files
# Usage: ./fix_whitespace.sh

echo "Fixing trailing whitespace in source files..."

# Find all source files and process them
find src -type f \( -name "*.cpp" -o -name "*.hpp" -o -name "*.h" -o -name "*.c" \) | while read file; do
    if [ -f "$file" ]; then
        # Create a backup
        cp "$file" "$file.bak"

        # Remove trailing whitespace using sed
        sed -i 's/[[:space:]]*$//' "$file"

        # Check if file was modified
        if ! diff -q "$file" "$file.bak" > /dev/null 2>&1; then
            echo "Fixed: $file"
            rm "$file.bak"
        else
            # No changes, restore original
            mv "$file.bak" "$file"
        fi
    fi
done

echo "Done!"
