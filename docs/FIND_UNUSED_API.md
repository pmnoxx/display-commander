# Finding unused APIs / functions

The repo can report functions in the Display Commander addon that are never called, using **cppcheck** and the build’s `compile_commands.json`.

## Prerequisites

1. **Generate compile commands** when configuring CMake:
   ```bash
   cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
   ```
   (Or use a preset that already sets `CMAKE_EXPORT_COMPILE_COMMANDS=ON`.)

2. **Install cppcheck**, e.g.:
   - Windows: `choco install cppcheck`
   - Or download from [cppcheck.sourceforge.io](https://cppcheck.sourceforge.io/)

## How to run

**Option A – CMake target (from build dir):**
```bash
cmake --build build --target find_unused_api
```

**Option B – Run the script directly:**
```bash
python tools/find_unused_api.py [build_dir]
python tools/find_unused_api.py build --addon-only   # only addon unused functions (no external headers)
```
`build_dir` defaults to `build` (relative to repo root).

## What it does

- Reads `build/compile_commands.json`.
- Keeps only entries for sources under `addons/display_commander` (excluding e.g. `cli_ui_exe`).
- Runs `cppcheck --enable=unusedFunction` on that subset so include paths and defines match the real build.

## Notes

- **False positives**: Functions only used via function pointers, macros, or in excluded code can be reported as unused. ReShade callbacks and similar may need to be suppressed (e.g. `// cppcheck-suppress unusedFunction`).
- **MSVC**: cppcheck parses MSVC-style compile commands; some flags may be ignored. Results are still usually useful.
- Filtered compile commands are written to `build/compile_commands_addon.json` for debugging.
- A summary list of addon-only unused functions (by module) is kept in `docs/unused_functions_addon.txt` for reference after a run.
