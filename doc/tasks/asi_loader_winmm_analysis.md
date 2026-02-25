# ASI Loader via winmm.dll – How It Works

**Goal:** Document how ASI loaders (e.g. [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader)) are able to load into a game process by replacing `winmm.dll`, and how this relates to Windows DLL search order and proxy DLLs.

---

## 1. What is an ASI Loader?

An **ASI Loader** is a DLL that loads custom plugins (`.asi` files) into a game process. It does not inject from outside; instead it gets loaded **as if** it were a system DLL the game already depends on. It then:

1. Loads the **real** system DLL and forwards all API calls to it.
2. Discovers and loads `.asi` plugins (e.g. from game root, `scripts`, `plugins`, or `update` folders).
3. Calls each plugin’s `InitializeASI()` if present.

So the loader is a **proxy DLL**: same name and exports as a real DLL, but with extra logic (load real DLL + load ASI plugins) and all exports forwarded to the real DLL.

---

## 2. Why winmm.dll?

Games often depend on **winmm.dll** (Windows Multimedia API) for:

- **timeGetTime()** – millisecond timer (e.g. for frame timing, animation).
- Other multimedia APIs (wave audio, joystick, etc.).

So the executable (or one of its dependencies) has an import for `winmm.dll` or calls `LoadLibrary("winmm.dll")`. That gives the loader an entry point: if we can make the process load **our** DLL when it asks for `winmm.dll`, we run first and can load ASI files, then forward to the real `winmm.dll`.

Ultimate ASI Loader supports many proxy names (dinput8.dll, version.dll, dsound.dll, **winmm.dll**, d3d9/11/12, xinput*, etc.). If a game doesn’t load dinput8 but does load winmm, renaming the loader to `winmm.dll` and placing it in the game folder makes it the one that gets loaded.

---

## 3. How the Loader Gets Loaded – DLL Search Order

When the game (or any DLL) calls `LoadLibrary("winmm.dll")` or has an import for `winmm.dll`, the **loader does not specify a path**. So Windows resolves the name using the **DLL search order**.

Relevant reference: [Dynamic-link library search order (Microsoft Learn)](https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-search-order).

### Unpackaged apps (typical games), SafeDllSearchMode enabled (default)

Rough order:

1. Directories in **PATH**
2. **Current directory** (process working directory)
3. Windows directory
4. 16-bit system directory
5. **System directory** (`%SystemRoot%\system32`)
6. **Directory from which the application (exe) was loaded**
7. … then Known DLLs, loaded-module list, etc.

Important for ASI loaders:

- **Current directory** is often set to the game folder (by launcher, shortcut, or “Run from here”). So it is searched **before** the system directory.
- If the user places the proxy as `winmm.dll` in the **game directory** and the game is run with current directory = game folder, that `winmm.dll` is found at step 2 and loaded **instead of** the one in `system32` (step 5).
- Alternatively, the **application directory** (where the .exe lives) is step 6; for a normal game install the exe and our proxy are in the same folder, so even if current directory differed, the loader in the exe’s folder could still be found (after system32 in this list). So the **reliable** case is: run the game so that the **current directory** is the game directory; then the proxy in the game folder is found first.

So: **ASI loader gets loaded via winmm.dll by placing a proxy named `winmm.dll` in the game directory and relying on DLL search order (current directory or application directory) so that this copy is loaded before the system’s `winmm.dll`.**

---

## 4. Proxy Behavior (Ultimate ASI Loader–style)

Once the proxy `winmm.dll` (the ASI loader) is loaded:

### 4.1 DllMain / initialization

- **GetSelfName()** – gets its own DLL filename (e.g. `winmm.dll`) via `GetModuleFileNameW` on its `HMODULE`, so it knows it’s acting as `winmm`.
- **LoadOriginalLibrary()** – loads the **real** winmm:
  - Prefer `winmmHooked.dll` in the same directory (user can rename the real `winmm.dll` from system to `winmmHooked.dll`).
  - Otherwise load from system directory: `SHGetKnownFolderPath(FOLDERID_System) + L"\\winmm.dll"`.
- Store the real module handle and, for each export, resolve the real function with `GetProcAddress(real_winmm, "ExportName")`.

### 4.2 ASI loading

- Scan configured folders (e.g. game root, `scripts`, `plugins`, `update`) for `*.asi`.
- For each `.asi`: `LoadLibrary` the file, then `GetProcAddress(h, "InitializeASI")` and call it if present.
- Optional: `global.ini` next to the loader DLL for paths and options.

### 4.3 Export forwarding

- The proxy exports the same symbols as the real `winmm.dll` (e.g. `timeGetTime`, `PlaySound`, …).
- Each export is a small stub that:
  1. Ensures the real library is loaded (once).
  2. Gets the real function pointer from the real `winmm` module.
  3. Calls the real function with the same arguments and returns its return value.

So the game keeps calling “winmm” APIs as usual; those calls go to the proxy, which forwards to the real `winmm.dll`. The game behavior stays correct; the only added work is loading the real DLL and all ASI plugins during startup.

---

## 5. Why This Works for winmm

- **Games use winmm** – Many titles link to or load `winmm.dll` for timing/audio, so the loader has a guaranteed load path when renamed to `winmm.dll`.
- **No path in load request** – The game (or its DLLs) loads by name only (`winmm.dll`), so the OS uses search order and finds the proxy in the game folder when current directory (or application directory) is that folder.
- **Same ABI** – The proxy has the same export set and calling conventions as the real `winmm.dll`, so forwarding is a thin wrapper per function.
- **Optional “Hooked” rename** – If the user renames the real system DLL to `winmmHooked.dll` and puts it next to the loader, the loader loads that instead of system32; this avoids depending on system path and keeps a single game folder.

---

## 6. Comparison to This Project (Display Commander)

This repo uses a similar **proxy DLL** idea for **opengl32.dll**:

- **scripts/gen_opengl32_proxy.py** generates `opengl32_proxy.cpp`: stubs for all `gl*` and `wgl*` exports that load the real `opengl32.dll` from the system directory and forward each call via `GetProcAddress` + call.
- The proxy is loaded when the game loads `opengl32.dll` from the application directory (same search-order idea).

The ASI loader extends that pattern:

- **Multiple possible proxy names** (one binary, many filenames: dinput8, winmm, version, …) and runtime detection via `GetSelfName()`.
- **Optional “Hooked” DLL** in the same directory to use a copied “real” DLL instead of system path.
- **Plugin loading** (`.asi` + `InitializeASI`) on top of pure forwarding.

So: **ASI loader loaded through winmm.dll = proxy DLL (same name + forwarded exports) + DLL search order (game directory) + load real winmm (system or winmmHooked.dll) + load and init ASI plugins.**

---

## 8. winmm.dll exports checklist (to add for a proxy)

**Implemented:** Display Commander includes a winmm proxy. Build the addon, then copy the built DLL (e.g. `zzz_display_commander.addon64`) to the game folder and rename it to `winmm.dll`. Optionally copy the real `winmm.dll` from system32 to the game folder as `winmmHooked.dll`. See `scripts/gen_winmm_proxy.py` and `proxy_dll/winmm_proxy.cpp`.

Exports that must be forwarded by a winmm proxy (x64; from Ultimate ASI Loader’s def). Each needs a stub that loads the real winmm and forwards the call.

- [ ] CloseDriver
- [ ] DefDriverProc
- [ ] DriverCallback
- [ ] DrvGetModuleHandle
- [ ] GetDriverModuleHandle
- [ ] NotifyCallbackData
- [ ] OpenDriver
- [ ] PlaySound
- [ ] PlaySoundA
- [ ] PlaySoundW
- [ ] SendDriverMessage
- [ ] WOW32DriverCallback
- [ ] WOW32ResolveMultiMediaHandle
- [ ] WOWAppExit
- [ ] aux32Message
- [ ] auxGetDevCapsA
- [ ] auxGetDevCapsW
- [ ] auxGetNumDevs
- [ ] auxGetVolume
- [ ] auxOutMessage
- [ ] auxSetVolume
- [ ] joy32Message
- [ ] joyConfigChanged
- [ ] joyGetDevCapsA
- [ ] joyGetDevCapsW
- [ ] joyGetNumDevs
- [ ] joyGetPos
- [ ] joyGetPosEx
- [ ] joyGetThreshold
- [ ] joyReleaseCapture
- [ ] joySetCapture
- [ ] joySetThreshold
- [ ] mci32Message
- [ ] mciDriverNotify
- [ ] mciDriverYield
- [ ] mciExecute
- [ ] mciFreeCommandResource
- [ ] mciGetCreatorTask
- [ ] mciGetDeviceIDA
- [ ] mciGetDeviceIDFromElementIDA
- [ ] mciGetDeviceIDFromElementIDW
- [ ] mciGetDeviceIDW
- [ ] mciGetDriverData
- [ ] mciGetErrorStringA
- [ ] mciGetErrorStringW
- [ ] mciGetYieldProc
- [ ] mciLoadCommandResource
- [ ] mciSendCommandA
- [ ] mciSendCommandW
- [ ] mciSendStringA
- [ ] mciSendStringW
- [ ] mciSetDriverData
- [ ] mciSetYieldProc
- [ ] mid32Message
- [ ] midiConnect
- [ ] midiDisconnect
- [ ] midiInAddBuffer
- [ ] midiInClose
- [ ] midiInGetDevCapsA
- [ ] midiInGetDevCapsW
- [ ] midiInGetErrorTextA
- [ ] midiInGetErrorTextW
- [ ] midiInGetID
- [ ] midiInGetNumDevs
- [ ] midiInMessage
- [ ] midiInOpen
- [ ] midiInPrepareHeader
- [ ] midiInReset
- [ ] midiInStart
- [ ] midiInStop
- [ ] midiInUnprepareHeader
- [ ] midiOutCacheDrumPatches
- [ ] midiOutCachePatches
- [ ] midiOutClose
- [ ] midiOutGetDevCapsA
- [ ] midiOutGetDevCapsW
- [ ] midiOutGetErrorTextA
- [ ] midiOutGetErrorTextW
- [ ] midiOutGetID
- [ ] midiOutGetNumDevs
- [ ] midiOutGetVolume
- [ ] midiOutLongMsg
- [ ] midiOutMessage
- [ ] midiOutOpen
- [ ] midiOutPrepareHeader
- [ ] midiOutReset
- [ ] midiOutSetVolume
- [ ] midiOutShortMsg
- [ ] midiOutUnprepareHeader
- [ ] midiStreamClose
- [ ] midiStreamOpen
- [ ] midiStreamOut
- [ ] midiStreamPause
- [ ] midiStreamPosition
- [ ] midiStreamProperty
- [ ] midiStreamRestart
- [ ] midiStreamStop
- [ ] mixerClose
- [ ] mixerGetControlDetailsA
- [ ] mixerGetControlDetailsW
- [ ] mixerGetDevCapsA
- [ ] mixerGetDevCapsW
- [ ] mixerGetID
- [ ] mixerGetLineControlsA
- [ ] mixerGetLineControlsW
- [ ] mixerGetLineInfoA
- [ ] mixerGetLineInfoW
- [ ] mixerGetNumDevs
- [ ] mixerMessage
- [ ] mixerOpen
- [ ] mixerSetControlDetails
- [ ] mmDrvInstall
- [ ] mmGetCurrentTask
- [ ] mmTaskBlock
- [ ] mmTaskCreate
- [ ] mmTaskSignal
- [ ] mmTaskYield
- [ ] mmioAdvance
- [ ] mmioAscend
- [ ] mmioClose
- [ ] mmioCreateChunk
- [ ] mmioDescend
- [ ] mmioFlush
- [ ] mmioGetInfo
- [ ] mmioInstallIOProcA
- [ ] mmioInstallIOProcW
- [ ] mmioOpenA
- [ ] mmioOpenW
- [ ] mmioRead
- [ ] mmioRenameA
- [ ] mmioRenameW
- [ ] mmioSeek
- [ ] mmioSendMessage
- [ ] mmioSetBuffer
- [ ] mmioSetInfo
- [ ] mmioStringToFOURCCA
- [ ] mmioStringToFOURCCW
- [ ] mmioWrite
- [ ] mmsystemGetVersion
- [ ] mod32Message
- [ ] mxd32Message
- [ ] sndPlaySoundA
- [ ] sndPlaySoundW
- [ ] tid32Message
- [ ] timeBeginPeriod
- [ ] timeEndPeriod
- [ ] timeGetDevCaps
- [ ] timeGetSystemTime
- [ ] timeGetTime
- [ ] timeKillEvent
- [ ] timeSetEvent
- [ ] waveInAddBuffer
- [ ] waveInClose
- [ ] waveInGetDevCapsA
- [ ] waveInGetDevCapsW
- [ ] waveInGetErrorTextA
- [ ] waveInGetErrorTextW
- [ ] waveInGetID
- [ ] waveInGetNumDevs
- [ ] waveInGetPosition
- [ ] waveInMessage
- [ ] waveInOpen
- [ ] waveInPrepareHeader
- [ ] waveInReset
- [ ] waveInStart
- [ ] waveInStop
- [ ] waveInUnprepareHeader
- [ ] waveOutBreakLoop
- [ ] waveOutClose
- [ ] waveOutGetDevCapsA
- [ ] waveOutGetDevCapsW
- [ ] waveOutGetErrorTextA
- [ ] waveOutGetErrorTextW
- [ ] waveOutGetID
- [ ] waveOutGetNumDevs
- [ ] waveOutGetPitch
- [ ] waveOutGetPlaybackRate
- [ ] waveOutGetPosition
- [ ] waveOutGetVolume
- [ ] waveOutMessage
- [ ] waveOutOpen
- [ ] waveOutPause
- [ ] waveOutPrepareHeader
- [ ] waveOutReset
- [ ] waveOutRestart
- [ ] waveOutSetPitch
- [ ] waveOutSetPlaybackRate
- [ ] waveOutSetVolume
- [ ] waveOutUnprepareHeader
- [ ] waveOutWrite
- [ ] wid32Message
- [ ] wod32Message

---

## 9. References

- [Ultimate ASI Loader (ThirteenAG)](https://github.com/ThirteenAG/Ultimate-ASI-Loader) – multi-DLL proxy ASI loader; supports winmm.dll (Win32 and x64).
- [Dynamic-link library search order](https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-search-order) – Microsoft Learn.
- [DLL hijacking / proxying](https://www.carlos-menezes.com/post/dll-hijacking) – high-level explanation of proxy DLLs and load order.
- Display Commander: `scripts/gen_opengl32_proxy.py`, `proxy_dll/opengl32_proxy.cpp` – same proxy pattern for opengl32.
