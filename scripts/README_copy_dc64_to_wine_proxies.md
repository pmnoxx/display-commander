# copy_dc64_to_wine_proxies.py

Copy `dc64.dll` to every Wine/Proton proxy DLL name in a directory so you can see **which libraries a game loads**.

## Usage

1. Build or obtain **dc64.dll** (Display Commander 64-bit proxy) and put it in the game folder.
2. Run the script with the game directory:

   ```batch
   cd "C:\Program Files (x86)\Steam\steamapps\common\STEINS;GATE"
   python path\to\copy_dc64_to_wine_proxies.py
   ```

   Or from anywhere:

   ```batch
   python path\to\copy_dc64_to_wine_proxies.py "C:\Program Files (x86)\Steam\steamapps\common\STEINS;GATE"
   ```

   Or use the helper batch for STEINS;GATE (edit the path inside if needed):

   ```batch
   scripts\run_copy_dc64_steins_gate.bat
   ```

3. The script copies `dc64.dll` to **all** Wine proxy DLL names (winmm.dll, version.dll, d3d11.dll, dxgi.dll, xinput1_4.dll, etc.) that **don’t already exist** in that folder.
4. **Detecting which DLLs the game loads:** Create a file named **`.DLL_DETECTOR`** (or e.g. `.DLL_DETECTOR.off`) in the same game folder. Run the game. Display Commander (when loaded as any of those proxy DLLs) will copy itself into a **`dlls_loaded`** subfolder under the same DLL filename. After you exit the game, **`dlls_loaded`** contains one copy per DLL name that was actually loaded (e.g. `dlls_loaded\dxgi.dll`, `dlls_loaded\version.dll`).
5. Without `.DLL_DETECTOR`, run the game as usual; whichever DLLs the game loads will load the proxy (you can add logging in the proxy to see which names were used).

## Files

- **copy_dc64_to_wine_proxies.py** – main script (self-contained; can be copied alone to the game dir).
- **wine_proxy_dll_list.py** – optional; full list of 653 Wine dll names. If this file is next to the script (e.g. in repo `scripts/`), the script uses it; otherwise it uses the embedded list.

## Source of DLL names

DLL list is derived from [wine-mirror/wine dlls](https://github.com/wine-mirror/wine/tree/master/dlls). Excluded: `.tlb`, `.cpl`, `.msstyles`, `.sys`, `.drv16`, `.dll16` (non-DLL outputs).
