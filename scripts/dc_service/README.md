# dc_service

- **download_dc.bat** — Downloads DC as `.addon32` and `.addon64` (latest_build) to this folder.
- **dc_start32.cmd** / **dc_start64.cmd** — Start injection service for 32-bit / 64-bit.
- **dc_stop32.cmd** / **dc_stop64.cmd** — Stop injection service for 32-bit / 64-bit.

## Notes

- Create a `.NORESHADE` empty file to start without ReShade.
- Create a `.NODC` empty file to load ReShade but not register Display Commander as an addon (proxy-only; no DC UI or features).
- Or download `Reshade32.dll` and `Reshade64.dll` (ReShade runtimes) if you want to run with ReShade.
