# Texture Replacement: Falcom-Style Fake Glare (D3D11)

## 1. Goal & Scope

- **Goal**: Allow users to disable the annoying fake glare / lens-dirt overlay texture in Falcom games (and similar titles) by replacing it with a **transparent** texture, matching the behavior achievable with [Special K](https://github.com/SpecialKO/SpecialK) texture injection (replacing the texture with a transparent one).
- **Primary use case**: DX11 games (Falcom engine: Trails, Ys, etc.) that draw a fullscreen or large overlay texture producing an unwanted glare effect.
- **Scope**:
  - **Phase 1**: D3D11 only. Replace matching textures with transparent content at creation and/or at update time using ReShade addon events. Optional: “Falcom glare” preset (heuristic or known dimensions/format).
  - **Phase 2 (optional)**: User-provided replacement texture from file (Special K–style injection), match by hash or dimensions; document as future work.

---

## 2. Background: How Special K Does It

- Special K hooks **CreateTexture2D** (and related) and supports **texture dumping** and **texture injection** from disk (see [Steam Community Guide](https://steamcommunity.com/sharedfiles/filedetails/?id=1491783680), [Upscale Wiki](https://upscale.wiki/wiki/Texture_injection_and_dumping)).
- For the “transparent glare” case: the user (or a preset) replaces the glare texture file with a transparent DDS; SK injects it when the game creates/loads that texture (matched by hash or path).
- Display Commander does not ship game-specific texture files; the spec therefore focuses on **procedural replacement with transparent content** (no external file required for the basic case), with optional support later for file-based replacement.

---

## 3. ReShade Addon API Constraints

- **create_resource**  
  - Signature: `bool (api::device*, api::resource_desc& desc, api::subresource_data* initial_data, api::resource_usage)`.  
  - Addon can **modify `desc` and `initial_data`** in place. It cannot change the returned resource pointer.  
  - When the game calls `CreateTexture2D` with `pInitialData`, ReShade copies it and passes a mutable pointer to addons; modifying that buffer replaces what the game’s texture is created with.

- **update_texture_region**  
  - Signature: `bool (api::device*, const api::subresource_data& data, api::resource resource, uint32_t subresource, const api::subresource_box* box)`.  
  - **Return `true`** to **prevent** the update. If we block the game’s update, we can then call `device->update_texture_region(transparent_data, resource, subresource, box)` ourselves so the texture becomes transparent.

- **create_resource_view**  
  - Only allows modifying the **view desc** (e.g. format). It does **not** allow substituting a different resource or view. So we cannot “swap in” a separate transparent texture at view-creation time; replacement must be via **resource content** (initial_data or update_texture_region).

- **push_descriptors / bind_descriptor_tables**  
  - Events are `void` and do not allow changing the bound descriptors. Substituting a different SRV at bind time would require a lower-level hook (e.g. MinHook on `ID3D11DeviceContext::PSSetShaderResources`), which is more invasive and out of scope for this addon-event–based design.

**Conclusion**: Replacement is done by (1) **create_resource**: when `initial_data` is non-null and the texture matches, overwrite `initial_data` with transparent texels; (2) **update_texture_region**: when the destination resource is a tracked “glare” texture, return true (block), then call `device->update_texture_region(transparent_data, resource, subresource, box)` so the texture content is transparent.

---

## 4. High-Level Design

### 4.1 Identification of the “Glare” Texture

- **Option A – Preset (Falcom)**  
  - Enable a “Replace Falcom glare texture” (or “Replace fake glare”) option.  
  - When enabled, use a **heuristic** to mark a texture as glare: e.g. 2D texture, specific format (e.g. R8G8B8A8_UNORM / R8G8B8A8_UNORM_SRGB), dimensions in a typical range for a fullscreen overlay (e.g. power-of-two, or matching back-buffer size), and optionally creation order (e.g. first such texture per frame/session).  
  - May require per-game tuning (e.g. known width/height for a specific title) and/or a small “learned” list (width × height × format) from community.

- **Option B – User-specified**  
  - Allow user to specify **width, height, format** (and optionally mip count) so that any texture matching that description is replaced.  
  - UI: e.g. “Replace texture: W×H, format” with override/clear.

- **Option C – Hash / file (Phase 2)**  
  - Match by content hash of initial_data (or first update). Replace with user DDS from disk. Deferred.

For Phase 1, **Option A + B** are in scope: a Falcom-oriented preset plus optional manual W×H/format rules.

### 4.2 Replacement Content

- **Transparent 2D texture**: All texels (R,G,B,A) = (0,0,0,0) for UNORM formats, or equivalent for other formats.  
- **Format handling**: Only replace when format is one we can fill correctly (e.g. R8G8B8A8_*, B8G8R8A8_*, R16G16B16A16_FLOAT). For unsupported formats, skip replacement and log.

### 4.3 Data Flow

1. **create_resource** (D3D11, texture_2d):  
   - If feature disabled, return false.  
   - If `initial_data` is non-null and texture matches (preset or user rule), **overwrite** each subresource in `initial_data` with transparent texels (same dimensions/row pitch/slice per subresource). Return true so ReShade uses the modified data.  
   - If texture matches but `initial_data` is null, we cannot register here (no resource handle yet). See init_resource.

2. **init_resource**:  
   - Called after the resource is created; we get the resource handle. If the resource’s desc (from `device->get_resource_desc(resource)`) matches our glare rules (preset or custom), **add** the resource to the “glare” set so that subsequent **update_texture_region** will replace updates with transparent content.

3. **update_texture_region**:  
   - If the **destination resource** is in our “glare” set:  
     - Return **true** (block the game’s update).  
     - Build transparent `api::subresource_data` for the same subresource/box (or full subresource).  
     - Call `device->update_texture_region(transparent_data, resource, subresource, box)`.  
   - Transparent buffer can be cached per (format, width, height) or a single 1×1 texture copied to the box; implementation choice (see §6).

4. **destroy_resource**:  
   - Remove the resource from the “glare” set when it is destroyed.

### 4.4 Threading and Lifetime

- Only the D3D11 device thread(s) that create/update/destroy resources need to see consistent state. Use a thread-safe set (or map) for “glare” resources, e.g. guarded by SRWLOCK (project rule: no std::mutex).  
- Transparent buffers or 1×1 textures can be created on first use and reused; lifetime tied to device (destroy when device is destroyed).

---

## 5. UI and Settings

- **Location**: Experimental tab (or a dedicated “Texture replacement” subsection).  
- **Controls**:
  - **Enable “Replace fake glare texture”** (master switch).  
  - **Preset**: “Falcom (D3D11)” / “Off” / “Custom”.  
  - **Custom** (when preset = Custom): optional **Width**, **Height**, **Format** (dropdown or “Any matching”).  
- **Persistence**: Store in Display Commander config (e.g. experimental or a new small section).  
- **Tooltip**: Explain that this replaces the matched texture with a transparent one to remove fake glare/lens effects; D3D11 only; Falcom preset tuned for known games.

---

## 6. Implementation Notes

### 6.1 Transparent Data for create_resource

- `initial_data` is one `api::subresource_data` per subresource (D3D11: MipLevels × ArraySize). Each has `data`, `row_pitch`, `slice_pitch`.  
- For each subresource we overwrite the existing buffer with zeros (or with (0,0,0,0) in the correct format). Row pitch and slice pitch must match the existing layout; we do not change dimensions or format of the resource, only the contents.

### 6.2 update_texture_region After Blocking

- `update_texture_region` takes `const api::subresource_data&`. We must provide our own buffer (e.g. cached transparent buffer for that format/size, or 1×1 then copy if ReShade supports partial update).  
- If the game updates a subregion (non-null `box`), we fill that region with transparent data; if full subresource, we can use a pre-filled buffer of the right size.  
- Careful with **format and row pitch**: must match the resource’s format and layout.

### 6.3 “Glare” Set When initial_data Is Null

- When we decide a texture is “glare” but `initial_data` is null, we cannot fill at creation. We store the resource handle (from **init_resource**, which is called after the resource is created with the real handle). So the flow is:  
  - **create_resource**: we cannot add to the set yet (we don’t have the handle). We can only return true/false and modify desc/initial_data.  
  - **init_resource**: (device, desc, initial_data, usage, **resource**). Here we have the handle. If we had a way to “pending mark” (e.g. by desc hash from create_resource), we could add this resource to the glare set here. So: in create_resource we don’t have the handle; in init_resource we do. Strategy: in create_resource, when texture matches and initial_data is null, store “pending” criteria (e.g. desc hash or a unique id). In init_resource, if the resource matches the pending criteria, add it to the glare set and clear pending. Matching by “last matching create” is fragile if multiple similar textures are created. Better: in init_resource, check the resource’s desc (device->get_resource_desc(resource)) against our rules and if it matches, add to glare set. So we don’t need a “pending” from create_resource; we simply in **init_resource** run the same matching logic (dimensions, format, etc.) and if it matches and feature is on, add resource to the set. Then on first **update_texture_region** for that resource we block and replace with transparent.  
- So: **create_resource**: only handle the case where initial_data != null (overwrite with transparent). **init_resource**: when desc matches “glare” rules, add resource to glare set. **update_texture_region**: when resource in set, block and call device->update_texture_region(transparent_data, ...). **destroy_resource**: remove from set.

### 6.4 D3D11 Only

- All logic is gated by `device->get_api() == api::device_api::d3d11`. No D3D12/ Vulkan/OpenGL in Phase 1.

---

## 7. Testing and Risks

- **Format mismatch**: If we fill with wrong format or pitch, the game or driver may crash. Only support a whitelist of formats and validate dimensions/pitch.  
- **Performance**: Overwriting large initial_data or calling update_texture_region on every glare update has a cost; keep transparent buffers small (e.g. 1×1) and reuse, or cache by (format, width, height).  
- **Multiple glare textures**: If a game has several similar textures, user may need “Custom” to narrow by dimensions. Falcom preset should be conservative (e.g. one known size).

---

## 8. References

- [Special K (SpecialKO)](https://github.com/SpecialKO/SpecialK) – texture injection/dumping.  
- [Steam Community: SpecialK - Dumping and Injecting Textures](https://steamcommunity.com/sharedfiles/filedetails/?id=1491783680).  
- [Upscale Wiki: Texture injection and dumping](https://upscale.wiki/wiki/Texture_injection_and_dumping).  
- ReShade addon events: `create_resource`, `init_resource`, `update_texture_region`, `destroy_resource` in `external/reshade/include/reshade_events.hpp`.  
- Display Commander: existing `OnCreateResource` / `OnCreateResourceView` in `swapchain_events.cpp`; add glare-replacement in a dedicated module (e.g. `texture_replacement` or under `experimental`) and call from the same events.

---

## 9. Summary

| Item | Choice |
|------|--------|
| API | ReShade addon events only (create_resource, init_resource, update_texture_region, destroy_resource) |
| Replacement content | Transparent (0,0,0,0) texels; no external file for Phase 1 |
| Identification | Preset “Falcom” + optional Custom (W×H, format) |
| API scope | D3D11 only (Phase 1) |
| UI | Experimental tab (or Texture replacement subsection), persisted in config |

This design provides a spec for implementing the “replace fake glare with transparent texture” feature in Display Commander, aligned with what users achieve via Special K and constrained to the ReShade addon API.
