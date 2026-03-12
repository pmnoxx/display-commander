# NGX Hooks vs Official SDK Signature Audit

This document compares the NGX detour signatures in `ngx_hooks.cpp` with the **official NVIDIA NGX SDK** headers in `external/nvidia-dlss/include/nvsdk_ngx.h` and the [NVIDIA NGX Programming Guide](https://docs.nvidia.com/ngx/latest/programming-guide/index.html). Hooks target the **DLL/Core** interface (NGX_SNIPPET_BUILD in the header). Last re-check: current implementation aligned with SDK and docs.

## Important: Two Interfaces in the Same Header

`nvsdk_ngx.h` states (lines 114–118):

> Functions under the same name and different function signatures exist between the NGX SDK, NGX Core (driver), and NGX Snippets.

- **NGX_SNIPPET_BUILD defined**: Interface between **NGX Core (driver/DLL)** and **Snippet**. The **DLL we hook** (`_nvngx.dll` / `nvngx_dlss.dll`) exports this side.
- **NGX_SNIPPET_BUILD NOT defined**: Interface used by **applications** (SDK side).

When we install hooks via `GetProcAddress(ngx_dll, "NVSDK_NGX_...")`, we are hooking the **DLL export**, i.e. the **Core/Snippet** interface. Our detour signatures **must** match that interface.

---

## 1. NVSDK_NGX_D3D12_Init / NVSDK_NGX_D3D11_Init

| Source | Signature |
|--------|-----------|
| **Our impl** (`ngx_hooks.cpp` ~195–198, 219–221) | `(InApplicationId, InApplicationDataPath, InDevice, NVSDK_NGX_Version InSDKVersion)` → **4 parameters** |
| **SDK – DLL/Core** (NGX_SNIPPET_BUILD, `nvsdk_ngx.h` 165–166, 167–168) | `(InApplicationId, InApplicationDataPath, InDevice, InSDKVersion)` → **4 params** |
| **SDK – App** (no NGX_SNIPPET_BUILD) | 5 params with `const NVSDK_NGX_FeatureCommonInfo *InFeatureInfo` (not used for hooks). |

**Verdict:** **Match.** Our typedefs and detours use the 4-parameter DLL/Core signature.

---

## 2. NVSDK_NGX_D3D12_Init_Ext / NVSDK_NGX_D3D11_Init_Ext

| Source | Signature |
|--------|-----------|
| **Our impl** (`ngx_hooks.cpp` ~198–202, 223–226) | `(InApplicationId, InApplicationDataPath, InDevice, NVSDK_NGX_Version InSDKVersion, const NVSDK_NGX_Parameter* InParameters)` → **5 params** |
| **SDK – DLL/Core** (NGX_SNIPPET_BUILD, `nvsdk_ngx.h` 166, 168) | `(..., InSDKVersion, const NVSDK_NGX_Parameter* InParameters)` → **5 params** |

**Verdict:** **Match.** Our typedefs and detours use the 5-parameter DLL/Core signature.

---

## 3. NVSDK_NGX_D3D12_Init_with_ProjectID / NVSDK_NGX_D3D11_Init_with_ProjectID

| Source | Signature |
|--------|-----------|
| **Our impl** | We hook **"NVSDK_NGX_D3D12_Init_with_ProjectID"** (and D3D11) and use `(InProjectId, InEngineType, InEngineVersion, InApplicationDataPath, InDevice, InFeatureInfo, InSDKVersion)`. |
| **SDK – App** | Declared as **NVSDK_NGX_D3D12_Init_with_ProjectID** with that parameter list. |

**Verdict:** We use the exact SDK export name **Init_with_ProjectID**. **Init_ProjectID** (no "with") is a different ABI; we do not hook it or use it as a fallback name.

---

## 4. NVSDK_NGX_D3D12_Shutdown1 / NVSDK_NGX_D3D11_Shutdown1

| Source | Signature |
|--------|-----------|
| **Our impl** | `(ID3D12Device* InDevice)` / `(ID3D11Device* InDevice)` |
| **SDK** | `NVSDK_NGX_D3D12_Shutdown1(ID3D12Device *InDevice)` / same for D3D11 |

**Verdict:** **Match.**

---

## 5. NVSDK_NGX_D3D12_CreateFeature / NVSDK_NGX_D3D11_CreateFeature

| Source | Signature |
|--------|-----------|
| **Our impl** | `(InCmdList/InDevCtx, InFeatureID, const NVSDK_NGX_Parameter* InParameters, OutHandle)` — matches DLL. |
| **SDK – DLL/Core** (NGX_SNIPPET_BUILD, `nvsdk_ngx.h` 561–562) | `(..., const NVSDK_NGX_Parameter *InParameters, ...)` |

**Verdict:** **Match.** Our typedefs and detours use `const NVSDK_NGX_Parameter*` to match the exported DLL; detour uses `const_cast` only where it needs to pass the pointer to helpers that modify (e.g. preset override).

---

## 6. NVSDK_NGX_D3D12_ReleaseFeature / NVSDK_NGX_D3D11_ReleaseFeature

| Source | Signature |
|--------|-----------|
| **Our impl** | `(NVSDK_NGX_Handle* InHandle)` |
| **SDK** | `NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D12_ReleaseFeature(NVSDK_NGX_Handle *InHandle)` |

**Verdict:** **Match.**

---

## 7. NVSDK_NGX_D3D12_EvaluateFeature / NVSDK_NGX_D3D11_EvaluateFeature

| Source | Signature |
|--------|-----------|
| **Our impl** | `(InCmdList/InDevCtx, const NVSDK_NGX_Handle* InFeatureHandle, const NVSDK_NGX_Parameter* InParameters, PFN_NVSDK_NGX_ProgressCallback InCallback)`; we use `bool* OutShouldCancel` in callback (C ABI). |
| **SDK** (`nvsdk_ngx.h` 700–702) | Same; C++ overload uses `bool &`; C ABI uses pointer. |

**Verdict:** **Match.**

---

## 8. NVSDK_NGX_D3D12_EvaluateFeature_C / NVSDK_NGX_D3D11_EvaluateFeature_C

| Source | Signature |
|--------|-----------|
| **Our impl** | `(InCmdList/InDevCtx, InFeatureHandle, InParameters, PFN_NVSDK_NGX_ProgressCallback_C InCallback)` with `bool* OutShouldCancel` |
| **SDK** | `(..., PFN_NVSDK_NGX_ProgressCallback_C InCallback)` — no default, required. |

**Verdict:** **Match.**

---

## 9. ABI and export resolution (index vs name)

- **We do not use export ordinals or a fixed table index.** All NGX hooks are installed by **symbol name** via `GetProcAddress(ngx_dll, entry.name)` (and optional `entry.name_alt`). The order of entries in `kNGXHooks` is irrelevant to resolution; only the export names matter. So any external list that maps indices (e.g. `[00]`, `[01]`, `[02]`) to NGX symbols is **not** our resolution path and may not match the actual _nvngx.dll export order.
- **NVSDK_NGX_*_GetFeatureRequirements** are **not hooked** by Display Commander. Official ABI (single declaration in `nvsdk_ngx.h`, no NGX_SNIPPET_BUILD split):
  - `NVSDK_NGX_D3D11_GetFeatureRequirements(IDXGIAdapter *Adapter, const NVSDK_NGX_FeatureDiscoveryInfo *FeatureDiscoveryInfo, NVSDK_NGX_FeatureRequirement *OutSupported)`
  - `NVSDK_NGX_D3D12_GetFeatureRequirements(IDXGIAdapter *Adapter, const NVSDK_NGX_FeatureDiscoveryInfo *FeatureDiscoveryInfo, NVSDK_NGX_FeatureRequirement *OutSupported)`
  - Calling convention: `NVSDK_CONV` (__cdecl). If you add hooks for these later, use this signature.
- **Init_with_ProjectID**: The SDK export name is **NVSDK_NGX_*_Init_with_ProjectID**. We hook only that symbol. **NVSDK_NGX_*_Init_ProjectID** (no "with") is a different ABI; we do not use it.

---

## Summary (vs official SDK / DLL)

| Function | Our signature vs DLL export | Status |
|----------|-----------------------------|--------|
| **NVSDK_NGX_D3D12/11_Init** | 4 params `(AppId, Path, Device, Version)` | **Match** |
| **NVSDK_NGX_D3D12/11_Init_Ext** | 5 params `(..., Version, const NVSDK_NGX_Parameter*)` | **Match** |
| **NVSDK_NGX_*_Init_with_ProjectID** | Symbol `Init_with_ProjectID` only; param list matches SDK. `Init_ProjectID` is a different ABI, not used | **Match** |
| **Shutdown1** | `(Device*)` | **Match** |
| **CreateFeature** | `(..., const NVSDK_NGX_Parameter*, OutHandle)` | **Match** |
| **ReleaseFeature** | `(Handle*)` | **Match** |
| **EvaluateFeature** | `(..., const Handle*, const Parameter*, Callback)` | **Match** |
| **EvaluateFeature_C** | `(..., const Handle*, const Parameter*, ProgressCallback_C)` | **Match** |
| **GetParameters / GetCapabilityParameters / AllocateParameters** | `(NVSDK_NGX_Parameter** OutParameters)` — single declaration in SDK, no NGX_SNIPPET_BUILD split | **Match** |
| **GetFeatureRequirements (D3D11/D3D12)** | Not hooked. Official ABI: `(IDXGIAdapter*, const NVSDK_NGX_FeatureDiscoveryInfo*, NVSDK_NGX_FeatureRequirement*)` — single declaration in SDK | **N/A (not hooked)** |

All hooked NGX functions are aligned with `external/nvidia-dlss/include/nvsdk_ngx.h` (DLL/Core interface where applicable) and the [NVIDIA NGX Programming Guide](https://docs.nvidia.com/ngx/latest/programming-guide/index.html).
