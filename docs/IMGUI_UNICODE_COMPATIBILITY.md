# ImGui Unicode Compatibility Guide

## Overview

This document outlines Unicode character compatibility issues when using ImGui with custom fonts, specifically ForkAwesome icons in the Display Commander addon.

## The Problem

ImGui uses custom fonts for rendering, and not all Unicode characters are supported by every font. ForkAwesome is an icon font that only contains specific glyphs, not a complete Unicode character set.

## Unsupported Characters

The following Unicode characters **DO NOT work** with ForkAwesome and should be avoided:

### Common Symbols
- `•` (U+2022) - Bullet point
- `✁` (U+2701) - Upper blade scissors
- `✗` (U+2717) - Ballot X
- `✓` (U+2713) - Check mark
- `⚠️` (U+26A0) - Warning sign
- `⭐` (U+2B50) - Star
- `🔧` (U+1F527) - Wrench
- `⚙️` (U+2699) - Gear

### Emoji Characters
- `📊` (U+1F4CA) - Bar chart
- `📈` (U+1F4C8) - Trending up
- `📉` (U+1F4C9) - Trending down
- `🎮` (U+1F3AE) - Video game
- `🎯` (U+1F3AF) - Direct hit
- `🔍` (U+1F50D) - Magnifying glass
- `💡` (U+1F4A1) - Light bulb
- `🌟` (U+2B50) - Glowing star

## Recommended Replacements

| Original Character | Recommended Replacement | ForkAwesome Alternative |
|-------------------|------------------------|------------------------|
| `•` | `-` or `*` | N/A (use text) |
| `✁` | `ICON_FK_OK` | `ICON_FK_CANCEL` |
| `✗` | `ICON_FK_CANCEL` | `ICON_FK_CANCEL` |
| `✓` | `ICON_FK_OK` | `ICON_FK_OK` |
| `⚠️` | `ICON_FK_WARNING` | `ICON_FK_WARNING` |
| `🔍` | `ICON_FK_SEARCH` | `ICON_FK_SEARCH` |

## Best Practices

### 1. Use ForkAwesome Constants
```cpp
// ❌ Bad - Raw Unicode characters
ImGui::Text("✓ Success");
ImGui::Text("⚠️ Warning");

// ✅ Good - ForkAwesome constants
ImGui::Text(ICON_FK_OK " Success");
ImGui::Text(ICON_FK_WARNING " Warning");
```

### 2. Use Simple Text for Bullet Points
```cpp
// ❌ Bad - Unicode bullet points
ImGui::Text("• Item 1");
ImGui::Text("• Item 2");

// ✅ Good - Simple text characters
ImGui::Text("- Item 1");
ImGui::Text("- Item 2");
```

### 3. Check Font Support
Before using any Unicode character, verify it's supported by your font:
- ForkAwesome: Only supports specific icon ranges (0xf002-0xf1c9)
- Default ImGui font: Limited Unicode support
- System fonts: Varies by platform

## Font Limitations

### ForkAwesome
- **Type**: Icon font
- **Range**: 0xf002-0xf1c9
- **Purpose**: Icons only, not general text
- **Usage**: Use `ICON_FK_*` constants

### Default ImGui Font
- **Type**: Bitmap font
- **Range**: Limited ASCII + some extended characters
- **Purpose**: General text rendering
- **Usage**: For regular text content

### System Fonts
- **Type**: Full Unicode fonts
- **Range**: Platform dependent
- **Purpose**: Complete text rendering
- **Usage**: When full Unicode support is needed

## Troubleshooting

### Character Not Displaying
1. Check if character is in ForkAwesome range
2. Verify font is loaded correctly
3. Use ForkAwesome constants instead of raw Unicode
4. Fall back to simple text characters

### Performance Issues
- Avoid mixing multiple fonts unnecessarily
- Use consistent character sets
- Prefer ForkAwesome icons over emoji

## Implementation Notes

### Display Commander Specific
- All UI elements use ForkAwesome for icons
- Bullet points replaced with `-` character
- Status indicators use `ICON_FK_OK`/`ICON_FK_CANCEL`
- Warnings use `ICON_FK_WARNING`

### Code Examples
```cpp
// Status indicators
ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), ICON_FK_OK " Active");
ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), ICON_FK_CANCEL " Disabled");

// Warning messages
ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), ICON_FK_WARNING " Warning: This feature requires setup");

// Bullet points
ImGui::Text("- First item");
ImGui::Text("- Second item");
ImGui::Text("- Third item");
```

## References

- [ForkAwesome Documentation](https://forkaweso.me/)
- [ImGui Font Documentation](https://github.com/ocornut/imgui/blob/master/docs/FONTS.md)
- [Unicode Character Database](https://unicode.org/charts/)
