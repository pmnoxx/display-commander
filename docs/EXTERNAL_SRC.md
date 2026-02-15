# external-src (reference sources)

The `external-src/` directory holds **reference-only** copies of third-party projects. It is gitignored; nothing under `external-src/` is committed. These are **not** git submodules. Use them for reading code and comparing behavior only.

## Populating external-src

Clone repositories manually or run the fetch script (no submodules):

```powershell
# From repo root
./tools/fetch_external_src.ps1
```

Or clone individually:

| Folder             | Repository | Clone (no submodules) |
|--------------------|------------|------------------------|
| **optiscaller**    | OptiScaler | `git clone https://github.com/optiscaler/OptiScaler.git external-src/optiscaller` |
| streamline         | NVIDIA Streamline | `git clone https://github.com/NVIDIA-RTX/Streamline.git external-src/streamline` |
| SpecialK           | Special K  | `git clone https://github.com/SpecialKO/SpecialK.git external-src/SpecialK` |
| Ultimate-ASI-Loader| Ultimate ASI Loader | `git clone https://github.com/ultimate-research/Ultimate-ASI-Loader.git external-src/Ultimate-ASI-Loader` |
| renodx              | renodx     | (clone into `external-src/renodx` as needed) |

## optiscaller (OptiScaler)

- **Source**: [optiscaler/OptiScaler](https://github.com/optiscaler/OptiScaler)
- **Path**: `external-src/optiscaller`
- **Purpose**: Reference for upscaling/Frame Gen (DLSS, FSR, XeSS) and DX/Vulkan hooking. Do **not** add as a git submodule; use a plain clone into `external-src/optiscaller` as above or via `tools/fetch_external_src.ps1`.
