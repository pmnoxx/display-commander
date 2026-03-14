# Try All Proxies

Copy the Display Commander addon to every Wine/Proton proxy DLL name in a target directory. Use this to discover which libraries a game loads: place the addon in the game folder, run this script, then run the game. Whichever DLL names get loaded will use the addon (you can add logging in the proxy to see which ones are hit).

DLL list source: [Wine dlls](https://github.com/wine-mirror/wine/tree/master/dlls). Non-.dll outputs (e.g. `.tlb`, `.cpl`, `.msstyles`, `.sys`) are excluded.

## Prerequisites

- Python 3
- **64-bit**: `zzz_display_commander.addon64` in the target directory  
- **32-bit**: `zzz_display_commander.addon32` in the target directory  

The script will not create the addon; you must copy it there first.

## Usage

```bash
# 64-bit (default): use zzz_display_commander.addon64
python try_all_proxies.py [target_dir]

# 32-bit: use zzz_display_commander.addon32
python try_all_proxies.py --32 [target_dir]
```

If `target_dir` is omitted, the current directory is used.

**Examples:**

```bash
python try_all_proxies.py
python try_all_proxies.py "C:\Games\STEINS;GATE"
python try_all_proxies.py --32 ./game_folder
```

## Behavior

- **Source**: `target_dir/zzz_display_commander.addon64` or `.addon32` (must exist).
- **Destination**: For each Wine proxy DLL name, creates `target_dir/<name>.dll` by copying the addon, **only if** that file does not already exist (existing files are skipped).
- **Output**: Prints how many files were copied, how many skipped, and a short list of created/skipped names.

## Optional: external DLL list

If `wine_proxy_dll_list.py` (defining `WINE_PROXY_DLLS`) is in the same directory as this script, it will be used. Otherwise the script uses its embedded list of proxy DLL names.
