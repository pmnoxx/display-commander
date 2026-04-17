# Installer marker (`.display_commander_installer_marker.json`)

## Purpose

When **global per-game config** is active (empty flag file `.DC_CONFIG_GLOBAL` next to the add-on DLL or under the Display Commander app data root), Display Commander resolves `RESHADE_BASE_PATH_OVERRIDE` to a folder under `%LocalAppData%\Programs\Display_Commander\Games\<game_name>\` (or an override path). Installers and launcher tools need a **stable, machine-readable** place to discover that resolution and optionally **redirect** the config directory **before** the game process starts.

This feature maintains **`.display_commander_installer_marker.json`** in the **game install root** (same notion of “install root” as used for derived `game_name`; see `GetGameInstallRootPathFromProcess` in `utils/general_utils.cpp`).

Implementation: `features/installer_marker/installer_marker.*`. Orchestration: `ChooseAndSetDcConfigPath` in `dll_boot_logging.cpp`.

## Preconditions (`.DC_CONFIG_GLOBAL`)

The marker file is **read and written only when global per-game config is active**: an empty `.DC_CONFIG_GLOBAL` exists next to the add-on DLL **or** under the Display Commander app data root (see README). The feature API takes an explicit `dc_config_global_flag_present` argument; when it is false, **read** returns no override and **write** does nothing, so the marker is not created or refreshed unless that flag file is present.

## When the file is read

During early boot, if `.DC_CONFIG_GLOBAL` is present (global config branch), and the game install root is non-empty, the add-on reads:

`<game_install_root>\.display_commander_installer_marker.json`

If the file contains a string field **`display_commander_config_directory`** that parses as a **non-empty absolute** path (UTF-8, strict validation via `MB_ERR_INVALID_CHARS`, then `lexically_normal()`), that path becomes the active config directory **instead of** the default `Games\<game_name>` folder.

## When the file is written

Only in the same **`.DC_CONFIG_GLOBAL` active** situation: after the resolved config path is chosen (default or marker), the add-on **creates** the config directory if needed, then **refreshes** the marker JSON in the game install root so it always reflects the **current** resolution.

Write uses a **temporary file** (`.display_commander_installer_marker.json.tmp`) and **`rename`** to the final name so readers rarely see a half-written file.

If the final rename fails (typical cause: insufficient permissions), a boot log line is emitted:

`[DC] installer marker: could not write .display_commander_installer_marker.json (permissions?)`

## JSON shape (`format_version` 1)

Single-line object (pretty-printing not required). Fields:

| Field | Type | Meaning |
|--------|------|---------|
| `format_version` | number | Currently `1`. |
| `display_commander_config_directory` | string | Absolute path in UTF-8 to the directory used as `RESHADE_BASE_PATH_OVERRIDE` for this run. |
| `game_display_name` | string | Derived folder name segment (same string as used for default `Games\<name>`); informational. |
| `display_commander_game_install_root` | string | Absolute UTF-8 path of the detected game install root. |
| `display_commander_games_folder_name` | string | Same as `game_display_name` in current builds; reserved for installers comparing folder naming. |
| `display_commander_config_path_source` | string | `"default"` if the path came from built-in `Games\<game_name>` logic; `"marker"` if it came from `display_commander_config_directory` in the marker file. |

## Parser notes (installer-supplied marker)

The reader implements a **minimal** JSON subset: it locates the key `"display_commander_config_directory"` and parses the following quoted string with basic escape handling (`\"`, `\\`, `/`, `n`, `r`, `t`, `b`, `f`). It does not use a full JSON library. Installers should emit **valid UTF-8**, a **single** `"display_commander_config_directory"` key, and an **absolute** path string.

## User-facing documentation

High-level behavior is also described in the repository `README.md` (`.DC_CONFIG_GLOBAL` section).
