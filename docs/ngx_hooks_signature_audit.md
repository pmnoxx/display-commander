# NGX Hooks vs Official SDK Signature Audit

This document compares the NGX detour signatures in `ngx_hooks.cpp` (lines ~1938–2010) with the **official NVIDIA NGX SDK** headers in `external/nvidia-dlss/include/nvsdk_ngx.h` and the [NVIDIA NGX Programming Guide](https://docs.nvidia.com/ngx/latest/programming-guide/index.html). These hooks were disabled in the past due to crashes; signature mismatches are a likely cause.

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
| **Our impl** (`ngx_hooks.cpp` ~194–198, 221–224) | `(InApplicationId, InApplicationDataPath, InDevice, **InFeatureInfo**, InSDKVersion)` → **5 parameters** |
| **SDK – App** (no NGX_SNIPPET_BUILD) | `(InApplicationId, InApplicationDataPath, InDevice, const NVSDK_NGX_FeatureCommonInfo *InFeatureInfo, NVSDK_NGX_Version InSDKVersion)` → **5 params** ✓ |
| **SDK – DLL/Core** (NGX_SNIPPET_BUILD) | `(InApplicationId, InApplicationDataPath, InDevice, **NVSDK_NGX_Version InSDKVersion**)` → **4 parameters** |

**Verdict:** We use the **App** signature (5 params with `InFeatureInfo`). The **exported** DLL symbol likely uses the **4-parameter** Core signature. Using a 5-parameter detour on a 4-parameter call causes **stack misalignment** and can crash.

---

## 2. NVSDK_NGX_D3D12_Init_Ext / NVSDK_NGX_D3D11_Init_Ext

| Source | Signature |
|--------|-----------|
| **Our impl** | `(InApplicationId, InApplicationDataPath, InDevice, **InFeatureInfo**, **void* Unknown5**)` → 5 params, 4th = FeatureCommonInfo*, 5th = void* |
| **SDK – App** | Not in App block (Init_Ext only in Snippet block). |
| **SDK – DLL/Core** (NGX_SNIPPET_BUILD) | `(InApplicationId, InApplicationDataPath, InDevice, **InSDKVersion**, **const NVSDK_NGX_Parameter* InParameters**)` → 5 params, 4th = Version, 5th = **Parameter*** |

**Verdict:** We use `(FeatureCommonInfo*, void*)` for the last two arguments. The DLL uses `(InSDKVersion, const NVSDK_NGX_Parameter*)`. **Type and order mismatch** → same stack/crash risk.

---

## 3. NVSDK_NGX_D3D12_Init_ProjectID / NVSDK_NGX_D3D11_Init_ProjectID

| Source | Signature |
|--------|-----------|
| **Our impl** | We hook **"NVSDK_NGX_D3D12_Init_ProjectID"** and use `(InProjectId, InEngineType, InEngineVersion, InApplicationDataPath, InDevice, InFeatureInfo, InSDKVersion)`. |
| **SDK – App** | Declared as **NVSDK_NGX_D3D12_Init_with_ProjectID** (name has **"with"**) with same parameter list. |
| **SDK – DLL** | Init_ProjectID is **not** in the NGX_SNIPPET_BUILD block; only Init_with_ProjectID exists in the non-SNIPPET block. |

**Verdict:** Export name may be **NVSDK_NGX_D3D12_Init_with_ProjectID**, not `Init_ProjectID`. If the DLL exports `Init_with_ProjectID`, then `GetProcAddress(..., "NVSDK_NGX_D3D12_Init_ProjectID")` can be NULL and the hook is skipped. Parameter list matches the SDK’s Init_with_ProjectID; only the **symbol name** may differ.

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
| **Our impl** | `(InCmdList/InDevCtx, InFeatureID, **NVSDK_NGX_Parameter* InParameters**, OutHandle)` — **non-const** InParameters |
| **SDK** (main export) | `(..., **const NVSDK_NGX_Parameter *InParameters**, ...)` (lines 561–562). Header also has overload with non-const (566–567). |

**Verdict:** SDK exposes both `const` and non-const. Using non-const in the detour is ABI-compatible; **match** for calling convention. Optional improvement: use `const NVSDK_NGX_Parameter*` in our typedef to match the primary declaration.

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
| **Our impl** | `(InCmdList/InDevCtx, InFeatureHandle, InParameters, PFN_NVSDK_NGX_ProgressCallback InCallback)` with `bool& OutShouldCancel` in callback |
| **SDK** | Same; callback optional (default NULL). |

**Verdict:** **Match.**

---

## 8. NVSDK_NGX_D3D12_EvaluateFeature_C / NVSDK_NGX_D3D11_EvaluateFeature_C

| Source | Signature |
|--------|-----------|
| **Our impl** | `(InCmdList/InDevCtx, InFeatureHandle, InParameters, PFN_NVSDK_NGX_ProgressCallback_C InCallback)` with `bool* OutShouldCancel` |
| **SDK** | `(..., PFN_NVSDK_NGX_ProgressCallback_C InCallback)` — no default, required. |

**Verdict:** **Match.**

---

## Summary and Recommendations

| Function | Our signature vs DLL export | Risk |
|----------|-----------------------------|------|
| **NVSDK_NGX_D3D12/11_Init** | We use 5 params (with InFeatureInfo); DLL likely 4 params | **High** – stack corruption / crash |
| **NVSDK_NGX_D3D12/11_Init_Ext** | We use (FeatureCommonInfo*, void*); DLL (InSDKVersion, Parameter*) | **High** – stack corruption / crash |
| **NVSDK_NGX_*_Init_ProjectID** | Parameter list OK; export name may be **Init_with_ProjectID** | Medium – hook may not install if name wrong |
| **Shutdown1** | Match | None |
| **CreateFeature** | Match (const vs non-const compatible) | None |
| **ReleaseFeature** | Match | None |
| **EvaluateFeature** | Match | None |
| **EvaluateFeature_C** | Match | None |

**Suggested next steps:**

1. **Confirm exported names** from the NGX DLL (e.g. `dumpbin /EXPORTS` on `_nvngx.dll` or the game’s NGX DLL) for `Init`, `Init_Ext`, and `Init_ProjectID` / `Init_with_ProjectID`.
2. **Align Init and Init_Ext detours with the DLL (Core) interface:**
   - **Init**: 4 parameters `(unsigned long long, const wchar_t*, ID3D12Device*, NVSDK_NGX_Version)`.
   - **Init_Ext**: 5 parameters `(..., NVSDK_NGX_Version, const NVSDK_NGX_Parameter*)`.
3. **Init_ProjectID**: Try hooking **NVSDK_NGX_D3D12_Init_with_ProjectID** (and D3D11) if the DLL exports that name; keep parameter list as in SDK.
4. Keep **Shutdown1**, **CreateFeature**, **ReleaseFeature**, **EvaluateFeature**, and **EvaluateFeature_C** as-is; their signatures match the SDK.

**Update (signatures fixed):** Init and Init_Ext detours were updated to match the DLL/Core interface (4-param Init, 5-param Init_Ext with `InSDKVersion` and `const NVSDK_NGX_Parameter*`). Init_ProjectID hook now tries `NVSDK_NGX_*_Init_with_ProjectID` then `NVSDK_NGX_*_Init_ProjectID`. The NGX Init/Shutdown/CreateFeature/ReleaseFeature/EvaluateFeature hooks are re-enabled in `ngx_hooks.cpp`.
