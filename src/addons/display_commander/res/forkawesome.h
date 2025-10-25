//Header Generated with https://github.com/aiekick/ImGuiFontStudio
//Based on https://github.com/juliettef/IconFontCppHeaders

#pragma once

/*
 * FORKAWESOME UNICODE COMPATIBILITY NOTES
 * ======================================
 *
 * This ForkAwesome font only supports a limited set of Unicode characters.
 * The following characters DO NOT work and should be replaced with alternatives:
 *
 * UNSUPPORTED CHARACTERS:
 * ----------------------
 * • (U+2022) - Bullet point - Use "-" or "*" instead
 * ✁ (U+2701) - Upper blade scissors - Use ICON_FK_OK or ICON_FK_CANCEL
 * ✗ (U+2717) - Ballot X - Use ICON_FK_CANCEL instead
 * ✓ (U+2713) - Check mark - Use ICON_FK_OK instead
 * ⚠️ (U+26A0) - Warning sign - Use ICON_FK_WARNING instead
 * ⭐ (U+2B50) - Star - No direct replacement, use text or different icon
 * 🔧 (U+1F527) - Wrench - No direct replacement
 * ⚙️ (U+2699) - Gear - No direct replacement
 * 📊 (U+1F4CA) - Bar chart - No direct replacement
 * 📈 (U+1F4C8) - Trending up - No direct replacement
 * 📉 (U+1F4C9) - Trending down - No direct replacement
 * 🎮 (U+1F3AE) - Video game - No direct replacement
 * 🎯 (U+1F3AF) - Direct hit - No direct replacement
 * 🔍 (U+1F50D) - Magnifying glass - Use ICON_FK_SEARCH instead
 * 💡 (U+1F4A1) - Light bulb - No direct replacement
 * 🌟 (U+2B50) - Glowing star - No direct replacement
 *
 * SUPPORTED CHARACTERS:
 * --------------------
 * All ForkAwesome icons defined below (ICON_FK_*) work correctly.
 * These are mapped to specific Unicode ranges (0xf002-0xf1c9).
 *
 * RECOMMENDED REPLACEMENTS:
 * ------------------------
 * • → "-" or "*" (for bullet points)
 * ✁ → ICON_FK_OK (for status indicators)
 * ✗ → ICON_FK_CANCEL (for error/cancel states)
 * ✓ → ICON_FK_OK (for success states)
 * ⚠️ → ICON_FK_WARNING (for warnings)
 * 🔍 → ICON_FK_SEARCH (for search functionality)
 *
 * USAGE IN IMGUI:
 * --------------
 * Always use the ICON_FK_* constants instead of raw Unicode characters.
 * Example: ImGui::Text(ICON_FK_OK " Success") instead of ImGui::Text("✓ Success")
 *
 * FONT LIMITATIONS:
 * ----------------
 * ForkAwesome is an icon font, not a full Unicode font.
 * It only contains specific icon glyphs, not general text characters.
 * For text, use the default ImGui font or system fonts.
 */

#define FONT_ICON_BUFFER_NAME_FK FK_compressed_data_base85
#define FONT_ICON_BUFFER_SIZE_FK 0xc26

#define ICON_MIN_FK 0xf002
#define ICON_MAX_FK 0xf1c9

#define ICON_FK_CANCEL "\uf00d" //  ✖
#define ICON_FK_FILE "\uf016" //  📄
#define ICON_FK_FILE_CODE "\uf1c9" //  📝
#define ICON_FK_FILE_IMAGE "\uf1c5" //  🖼️
#define ICON_FK_FLOPPY "\uf0c7" //  💾
#define ICON_FK_FOLDER "\uf114" //  📁
#define ICON_FK_FOLDER_OPEN "\uf115" //  📂
#define ICON_FK_MINUS "\uf068" //  ➖
#define ICON_FK_OK "\uf00c" //  ✔
#define ICON_FK_PENCIL "\uf040" //  ✏️
#define ICON_FK_PLUS "\uf067" //  ➕
#define ICON_FK_REFRESH "\uf021" //  🔄
#define ICON_FK_SEARCH "\uf002" //  🔍
#define ICON_FK_UNDO "\uf0e2" //  ↶
#define ICON_FK_WARNING "\uf071" //  ⚠️

