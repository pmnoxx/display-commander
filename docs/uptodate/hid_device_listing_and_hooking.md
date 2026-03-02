# HID Device Listing and Hooking

This document describes how **listing HID devices** and **HID hooking** work, with reference to Special K and the existing Display Commander code. It supports adding an **HID subsection** in the **Controller tab** with devices grouped into: **XInput**, **DualSense**, and **Other**.

**Special K reference:** [SpecialKO/SpecialK `src/input/hid.cpp`](https://github.com/SpecialKO/SpecialK/blob/main/src/input/hid.cpp) (and related input hooks). For local comparison, clone into `external-src/SpecialK` per [EXTERNAL_SRC.md](../EXTERNAL_SRC.md).

---

## Design session: HID hooking architecture (always-on, which detours)

**Principle:** HID hooks are installed **always** (at addon init), not only when "HID suppression" is enabled. The **suppression** setting only controls whether we *block* or *zero* I/O; the **detours** themselves are always active for tracking, statistics, and future features (e.g. device list from "devices the game opened").

### Design goals

1. **Always install HID detours** — Install once during addon init (e.g. with other input hooks), independent of `hid_suppression_enabled`. No "install when user enables suppression, uninstall when they disable."
2. **Two responsibilities in each detour:**
   - **Tracking** (always): record stats, optionally maintain handle → device info for UI (path, VID, PID, usage). No blocking.
   - **Suppression** (only when enabled): block or zero I/O based on `hid_suppression_enabled` and the per-action settings (block ReadFile, block GetInputReport, etc.).
3. **Our own code** (DualSense wrapper, HID enumeration for UI) must call **Original** (or existing *Direct* wrappers that call Original) so it is never blocked or broken by our hooks.

### Which detours to use

| Detour | Module (e.g. kernel32 / hid.dll) | When to run | Purpose |
|--------|----------------------------------|-------------|--------|
| **CreateFileA** | kernel32 | Always | (1) Track: detect HID device path, update stats, optionally add handle → (path, later VID/PID) to a map. (2) Suppress: if suppression + block CreateFile, return INVALID_HANDLE_VALUE. |
| **CreateFileW** | kernel32 | Always | Same as CreateFileA for wide paths; primary path for HID on Windows. |
| **ReadFile** | kernel32 | Always | (1) Track: if handle is known HID (from CreateFile map or heuristics), update read stats. (2) Suppress: if suppression + block ReadFile and handle looks like HID read (small buffer, FILE_TYPE_UNKNOWN), return 0 bytes / failure. |
| **HidD_GetAttributes** | hid.dll | Always | (1) Track: on success, we know VID/PID for this handle; can update handle→device info if we have a map. (2) Suppress: if suppression + block GetAttributes and (optionally) device matches (e.g. DualSense or all), return FALSE or spoof attributes. |
| **HidD_GetInputReport** | hid.dll | Always | (1) Track: count calls per handle/device. (2) Suppress: if suppression + block GetInputReport, zero buffer and return FALSE. |

Optional (for richer device list or future features):

| Detour | Purpose |
|--------|--------|
| **CloseHandle** | Remove handle from the HID handle map when a HID device handle is closed (so we don’t treat stale handles as HID). Requires a way to know the handle was from our CreateFile (we only add to map when we see HID path in CreateFile). |
| **HidD_GetPreparsedData** | Track or filter device type (gamepad/mouse/keyboard) for listing; Special K uses this to classify. Can be added later if we want "usage" in the UI without opening devices ourselves. |
| **HidP_GetCaps** | Same as above; used with preparsed data. |

### Detour logic pattern (per detour)

- **At top of detour:**
  - Do **tracking** only (stats, handle map update if applicable). No branching on suppression for tracking.
- **Then:**
  - If **suppression is enabled** and the corresponding **per-action setting** is on (e.g. `hid_suppression_block_readfile`), perform the **suppression** action (block, zero, or spoof).
- **Else:**
  - Call **Original** and return (for CreateFile/ReadFile/HidD_* we always call Original unless we decided to block; when blocking we return without calling Original).

So: **hooks always run** → tracking always happens; **suppression is a conditional inside the hook**.

### Handle → device map (optional)

- On **CreateFileW** (and A): if path is HID (e.g. `\?\hid#...`), and we call Original and get a valid handle, insert `handle → { path, VID?, PID? }`. VID/PID can be filled later when **HidD_GetAttributes** is called for this handle (we can match by handle).
- On **ReadFile** / **HidD_GetInputReport**: check if handle is in map to know "this is a HID device" instead of heuristics.
- On **CloseHandle**: if handle is in map, remove it. (Requires a CloseHandle detour and a way to avoid affecting non-HID handles; we only remove if present in map.)

If we don’t add a handle map, we keep the current heuristic in ReadFile (small buffer + FILE_TYPE_UNKNOWN) and still get stats from CreateFile path parsing; listing stays via SetupAPI enumeration.

### Installation and lifecycle

- **Install:** Call `InstallHIDSuppressionHooks()` (or a renamed `InstallHIDHooks()`) **unconditionally** during addon init (e.g. in the same place other input hooks are installed), not inside `if (hid_suppression_enabled)`.
- **Uninstall:** Only on addon shutdown (DLL unload). Do not uninstall when the user turns "HID suppression" off.
- **Rename (optional):** Consider renaming to "HID hooks" in code/comments (e.g. `InstallHIDHooks`) and keep "HID suppression" as the name of the *feature* (the setting that controls blocking), so it’s clear hooks are always on and suppression is one use of them.

### Summary table: detours and behavior

| Detour | Always run? | Tracking (always) | Suppression (when enabled) |
|--------|------------|-------------------|----------------------------|
| CreateFileA | Yes | Count HID opens, path-based stats, optional handle→path in map | If block CreateFile: return INVALID_HANDLE_VALUE |
| CreateFileW | Yes | Same | Same |
| ReadFile | Yes | Count reads; optional: if handle in map, count per device | If block ReadFile + HID-like: return 0 bytes, fail |
| HidD_GetAttributes | Yes | Optional: store VID/PID for handle in map | If block GetAttributes + match device: return FALSE or spoof |
| HidD_GetInputReport | Yes | Count calls | If block GetInputReport: zero buffer, return FALSE |
| CloseHandle | Optional | If handle in map: remove | No |

This design keeps a single, simple rule: **hook once at init, never toggle by suppression**; **suppression only changes behavior inside the detours**.

---

## 1. How HID device listing works

### 1.1 Windows APIs (SetupAPI + hid.dll)

Enumeration uses the **SetupAPI** device interface list plus **hid.dll** to get attributes and optional strings:

| Step | API | Purpose |
|------|-----|--------|
| 1 | `SetupDiGetClassDevs(&GUID_DEVINTERFACE_HID, nullptr, nullptr, DIGCF_DEVICEINTERFACE)` | Get device info set for all HID interfaces (present devices). |
| 2 | `SetupDiEnumDeviceInterfaces(...)` | Iterate each device in the set. |
| 3 | `SetupDiGetDeviceInterfaceDetail(...)` | Get device path (e.g. `\?\hid#vid_054c&pid_0ce6#...`) and optional `SP_DEVINFO_DATA`. |
| 4 | `CreateFile(path, GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE, ...)` | Open device handle (required for next steps). |
| 5 | `HidD_GetAttributes(handle, &HIDD_ATTRIBUTES)` | Get **VendorID**, **ProductID**, **VersionNumber**. |
| 6 | (Optional) `HidD_GetProductString` / `HidD_GetManufacturerString` / `HidD_GetSerialNumberString` | Human-readable names. |
| 7 | (Optional) `HidD_GetPreparsedData` + `HidP_GetCaps` | Get HID usage page/usage (gamepad, joystick, mouse, keyboard, etc.). |
| 8 | `CloseHandle(handle)` | Release device. |
| 9 | `SetupDiDestroyDeviceInfoList(hDevInfo)` | Free device info set. |

**GUID:** `GUID_DEVINTERFACE_HID` = `{4d1e55b2-f16f-11cf-88cb-001111000030}` (from `initguid.h` + `hidclass.h` or defined locally as in `dualsense_hid_wrapper.cpp` / `dualsense_widget.cpp`).

**Existing code:** Display Commander already does this in `src/addons/display_commander/dualsense/dualsense_hid_wrapper.cpp` — `EnumerateHIDDevices()` uses `SetupDiGetClassDevs` → `SetupDiEnumDeviceInterfaces` → `SetupDiGetDeviceInterfaceDetail` → `CreateFile` → `HidD_GetAttributes_Direct` (to avoid suppression), then filters by Sony VID/PID. That same pipeline can be generalized to enumerate **all** HID devices and then classify them.

### 1.2 Alternative: GetRawInputDeviceList

- **GetRawInputDeviceList**(nullptr, &count, sizeof(RAWINPUTDEVICELIST)) then with a buffer returns handles and types (RIM_TYPEHID, etc.).
- **GetRawInputDeviceInfo**(hDevice, RIDI_DEVICEINFO, ...) gives VID/PID for HID devices.
- This list only includes devices that are **registered for Raw Input** by the process or system; it does not necessarily include every physical HID device. For a full “all HID devices” list, SetupAPI enumeration is the standard approach.

### 1.3 Special K: device type from HID usage

Special K uses **HidD_GetPreparsedData** + **HidP_GetCaps** to read `UsagePage` and `Usage`, then classifies:

- **Gamepad/Joystick:** `HID_USAGE_PAGE_GENERIC` + `HID_USAGE_GENERIC_GAMEPAD` / `JOYSTICK` / `MULTI_AXIS_CONTROLLER`
- **Mouse:** `HID_USAGE_GENERIC_POINTER` / `HID_USAGE_GENERIC_MOUSE`
- **Keyboard:** `HID_USAGE_GENERIC_KEYBOARD` / `KEYPAD`
- **Other:** e.g. `HID_USAGE_PAGE_GAME` / `VR` / `SPORT` / `ARCADE` → treated as gamepad

So for the “Other” category we can either show only VID/PID/version, or optionally use `HidP_GetCaps` to show usage page/usage (and optionally label as “Gamepad”, “Mouse”, “Keyboard”, “Other”).

---

## 2. Classifying devices for the UI: XInput, DualSense, Other

For the Controller tab HID subsection, group each enumerated device into one of three categories using **Vendor ID** and **Product ID** (from `HidD_GetAttributes`).

### 2.1 XInput (Microsoft Xbox)

- **Vendor ID:** `0x045E` (Microsoft).
- **Product IDs:** e.g. `0x028E` (Xbox 360), `0x0B12` (Xbox One), `0x0B00` (Elite Series 2), and other Microsoft gamepad PIDs. A practical approach is: **VID == 0x045E** and PID in a known XInput gamepad list (or treat all 0x045E HID gamepads as XInput if you don’t need to exclude other Microsoft HID devices).

### 2.2 DualSense (Sony)

- **Vendor ID:** `0x054C` (Sony).
- **Product IDs:**
  - `0x0CE6` — DualSense
  - `0x0DF2` — DualSense Edge

Already implemented in Display Commander as `IsDualSenseDevice()` in `hid_suppression_hooks.cpp` and in `dualsense_hid_wrapper.cpp` (with additional DualShock 4 PIDs for enumeration filter). For the **DualSense** group in the UI, use the same VID and the two DualSense PIDs above; optionally include DS4 PIDs in a separate sub-label or in “Other” depending on product goals.

### 2.3 Other

- Any HID device that is not classified as XInput or DualSense (e.g. other gamepads, mice, keyboards, VR, custom HID). Optionally use `HidP_GetCaps` to show usage (gamepad/mouse/keyboard/other).

---

## 3. HID hooking (what we hook and why)

Hooking allows intercepting or suppressing HID access (e.g. for “HID suppression” when the game is in background, or for DualSense-to-XInput while still hiding raw HID from the game).

### 3.1 Display Commander hooks (current vs designed)

**Current:** Hooks are installed only when `hid_suppression_enabled` is true (see `main_entry.cpp`: `if (hid_suppression_enabled) InstallHIDSuppressionHooks()`). **Designed:** Per the design session above, hooks should be installed **always**; suppression only controls whether each detour blocks/zeros I/O.

| Hook | File | Purpose |
|------|------|--------|
| **ReadFile** | hid_suppression_hooks.cpp | Track read stats (always). Suppress reads on HID-like handles (small buffers, FILE_TYPE_UNKNOWN) only when HID suppression + block ReadFile are on. |
| **HidD_GetInputReport** | hid_suppression_hooks.cpp | Track (always). Suppress or zero input reports only when suppression + block GetInputReport are on. (Currently this hook is commented out in install.) |
| **HidD_GetAttributes** | hid_suppression_hooks.cpp | Track (always). Return failure or spoof attributes only when suppression + block GetAttributes are on and device matches. (Currently this hook is commented out in install.) |
| **CreateFileA / CreateFileW** | hid_suppression_hooks.cpp | Track HID path opens and stats (always). Block opening only when suppression + block CreateFile are on. |

“Direct” variants (`ReadFile_Direct`, `HidD_GetInputReport_Direct`, `HidD_GetAttributes_Direct`) call the Original and are used by DualSense and by our own enumeration so that addon code is never blocked. See [DUALSENSE_XINPUT_FEATURE.md](../DUALSENSE_XINPUT_FEATURE.md).

### 3.2 Special K HID hooks (reference)

Special K hooks many more HID APIs to classify and filter input:

- **HidD_GetPreparsedData** — Prevents game from getting preparsed data when “disable HID” or “filter by usage” (gamepad capture) is on; can free the data and return FALSE.
- **HidD_FreePreparsedData** — Cleans up tracking of current preparsed data.
- **HidP_GetCaps** — Used in conjunction with preparsed data to decide device type (gamepad/mouse/keyboard).
- **HidD_GetAttributes** — Can spoof PID (e.g. DualShock 4 v1→v2, or hide DualSense Edge PID).
- **HidD_GetFeature** / **HidD_SetFeature** — Can zero or block feature reports when capturing gamepad.
- **HidP_GetData** / **HidP_GetUsages** — Can zero input when gamepad capture is wanted.

They also hook **CreateFileW** (and similar) to build a map of open HID device handles → `SK_HID_DeviceFile` (path, caps, VID/PID, type, product/manufacturer/serial strings). That map is used in **ReadFile** and other hooks to decide whether a handle is a known HID device and whether to allow/block/hide I/O. So **listing** in Special K is effectively “all HID devices we have seen opened” (and optionally cached via SetupAPI), not a separate “enumeration-only” path.

### 3.3 Listing vs hooking

- **Listing:** Use SetupAPI + `HidD_GetAttributes` (and optionally `HidD_GetPreparsedData`/`HidP_GetCaps`) with **direct** APIs or unhooked APIs so that enumeration is not affected by our own suppression. Display Commander already uses `HidD_GetAttributes_Direct` in `EnumerateHIDDevices()` for this reason.
- **Hooking:** Affects the **game** (or any process using our DLL). For the addon’s own UI/list we should use the same pattern: open device, call **HidD_GetAttributes_Direct**, optionally get preparsed/caps/strings, close handle — so the list is accurate even when HID suppression is enabled.

---

## 4. UI design: HID subsection in Controller tab

This section describes the **UI layout and behavior** for the HID device list in the Controller tab. It follows existing patterns: `CollapsingHeader` for sections, `res/ui_colors.hpp` for colors, and a separate draw function so the Controller tab stays maintainable.

### 4.1 Placement and structure

- **Where:** Controller tab, **after** the XInput widget and **before** the Remapping widget (same order: XInput → *new HID subsection* → Remapping).
- **Integration:** In `new_ui_tabs.cpp`, the Controller tab lambda should call a new function after `DrawXInputWidget`, e.g. `DrawHIDDevicesSubsection(wrapper)`, then `ImGui::Spacing()`, then `DrawRemappingWidget`. The HID block is **not** inside the XInput widget so that HID enumeration and UI stay independent.

**Wireframe (collapsed):**

```
  ▼ HID Devices                                    [Refresh]
  │  XInput (2)  │  DualSense (1)  │  Other (3)
  │  • Xbox Wireless Controller   VID 045E  PID 0B12
  │  • Xbox 360 Controller         VID 045E  PID 028E
```

**Wireframe (expanded, with Block checkbox and four categories):**

```
  ▼ HID Devices                                    [Refresh]
     All HID devices on this system. Check "Block" to prevent the game from opening that device.

     XInput (Microsoft) (2)                    [?]  ← tooltip: how category is defined
       [ ] Xbox Wireless Controller          VID 045E  PID 0B12
       [ ] Xbox 360 Controller for Windows    VID 045E  PID 028E

     DualSense (Sony) (1)                      [?]
       [ ] DualSense Controller               VID 054C  PID 0CE6  USB

     HID Compliant game controller (2)        [?]
       [ ] HID-compliant game controller      VID 0738  PID 2210
       [x] Generic gamepad                     VID 1234  PID 5678   ← Block checked

     Other HID (2)                            [?]
       [ ] USB Input Device                   VID 046D  PID C52B
       [ ] USB Keyboard                       VID 046D  PID C31C

     HID suppression (block game access to HID) is in Experimental tab.
```

### 4.2 Components and behavior

| Component | Description |
|-----------|-------------|
| **CollapsingHeader "HID Devices"** | Single top-level section; default **open** so users see the list without an extra click. |
| **Tooltip / help line** | One short line under the header or tooltip on "HID Devices": e.g. "All HID devices on this system. Uses direct APIs so the list is correct even when HID suppression is enabled." |
| **Refresh button** | Right-aligned (SameLine) or below the header. On click: trigger re-enumeration. While refreshing: show "Refreshing..." or disable button. |
| **Four groups** | **XInput (Microsoft)**, **DualSense (Sony)**, **HID Compliant game controller**, **Other HID**. Each group is a **TreeNode** with count. A **tooltip on the category label** (hover on the header row) explains how that category is defined (see 4.7). |
| **Per-device row** | **Checkbox** at the start of the row (no label, or label "Block" in tooltip only to keep the row compact). Then product string, then VID/PID in hex. Layout: `[ ]  Device name   VID 045E  PID 0B12`. When checked, the game cannot open that device; state persisted by device path (or stable id). Tooltip on checkbox: "Block: prevent the game from opening this HID device." |
| **Device tooltip** | On hover over device name: show full device path for debugging. |
| **Link to Experimental** | At bottom: dimmed text "HID suppression (block game access) is in Experimental tab." (global suppression options remain there). |

### 4.3 States and edge cases

| State | UI behavior |
|-------|-------------|
| **Empty (no devices)** | Show all three groups with (0); inside each, optional "No devices" in `TEXT_DIMMED` or `TEXT_SUBTLE`. |
| **Enumeration in progress** | Show "Refreshing..." and/or disable Refresh until done. If sync enumeration, run on background thread. |
| **Enumeration error** | One line in `TEXT_ERROR`: "Could not enumerate HID devices." with optional tooltip for GetLastError. |
| **First load** | Enumerate once when Controller tab is first opened, or at addon init and cache; Refresh updates cache. |

### 4.4 Visual style (res/ui_colors.hpp)

- **Section title:** `CollapsingHeader` default; optional `TEXT_LABEL` for "HID Devices".
- **Group headers:** `TreeNode` or `TextColored(TEXT_LABEL, "XInput (Microsoft)")`; count in same line.
- **Device name:** default or `TEXT_DEFAULT`.
- **VID/PID:** `TextColored(TEXT_SUBTLE)` or `TEXT_DIMMED`.
- **Empty / "No devices":** `TEXT_DIMMED` or `TEXT_SUBTLE`.
- **Help / link to Experimental:** `TEXT_SUBTLE`.
- **Error:** `TEXT_ERROR` or `ICON_ERROR`.

Use `Indent()` / `Unindent()` for hierarchy under each group.

### 4.5 Data per device (for the list)

Minimum: `path`, `vendor_id`, `product_id`. Optional: `product_string` (from HidD_GetProductString; fallback "VID_xxxx&PID_yyyy"), `usage_page` / `usage` from HidP_GetCaps for classification (HID Compliant game controller vs Other). Classification: XInput (VID 0x045E), DualSense (VID 0x054C + PID 0x0CE6/0x0DF2), HID Compliant game controller (usage from caps; see 4.7), Other.

### 4.6 Block device: UI and behavior

- **Checkbox per device:** One checkbox per device row, labeled **"Block"** (or shown as a small checkbox only; label in tooltip). Placed at the start of the row so the list stays scannable: `[ ] Device name   VID xxxx  PID xxxx`.
- **Persisted set:** Store the set of **blocked device paths** (or a stable id such as path, or VID+PID+serial if available) in settings/config. On load, restore the set; when the user checks/unchecks Block, add/remove that device’s path (or id) from the set and save.
- **Hook behavior:** In the **CreateFileW** (and CreateFileA) detour: if the requested path is in the blocked set, return **INVALID_HANDLE_VALUE** and set an appropriate error (e.g. ERROR_ACCESS_DENIED) so the game does not get a handle to that device. ReadFile / HidD_GetInputReport / HidD_GetAttributes only need to block if the *handle* belongs to a blocked device; that requires a handle→path map populated when CreateFile succeeds (for non-blocked paths). Alternatively, block only at CreateFile so the game never gets a handle; then no handle map is needed for blocking (only for optional stats). Prefer **block at CreateFile only** for simplicity: blocked path → never open, so no handle is ever created for that device.
- **Visual:** Use the same row layout as the rest of the subsection (indent under each TreeNode). Optionally show a dimmed "Blocked" badge or icon next to the device name when Block is checked, for quick scan. Tooltip on Block: e.g. "Prevent the game from opening this HID device (CreateFile will fail for this path)."

### 4.7 Categories and tooltips (four groups)

Display **four** groups in this order. Each category header has a **tooltip** (e.g. hover on the TreeNode label, or a small [?] icon) that explains how the category is defined:

| Category | Definition | Tooltip text (example) |
|----------|------------|------------------------|
| **XInput (Microsoft)** | Vendor ID is **0x045E** (Microsoft). Typically Xbox controllers. | "Devices with Microsoft vendor ID (0x045E). Usually Xbox 360, Xbox One, or Xbox Series controllers." |
| **DualSense (Sony)** | Vendor ID **0x054C** (Sony) and Product ID **0x0CE6** (DualSense) or **0x0DF2** (DualSense Edge). | "Sony DualSense or DualSense Edge controllers (VID 0x054C, PID 0x0CE6 or 0x0DF2)." |
| **HID Compliant game controller** | HID Usage Page = **Generic (0x01)** and Usage = **Game Pad (0x05)**, **Joystick (0x04)**, or **Multi-axis Controller (0x08)**. Exclude devices already classified as XInput or DualSense. | "Devices that report as HID game pad, joystick, or multi-axis controller (Usage Page 0x01, Usage 0x04/0x05/0x08). Excludes XInput and DualSense." |
| **Other HID** | All remaining HID devices (keyboards, mice, other peripherals, or unknown usage). | "All other HID devices (e.g. keyboard, mouse, or unknown usage)." |

**Implementation note:** To classify **HID Compliant game controller**, enumeration must call **HidD_GetPreparsedData** and **HidP_GetCaps** to read `UsagePage` and `Usage`. Use `HID_USAGE_PAGE_GENERIC` (0x01) and `HID_USAGE_GENERIC_GAMEPAD` (0x05), `HID_USAGE_GENERIC_JOYSTICK` (0x04), `HID_USAGE_GENERIC_MULTI_AXIS_CONTROLLER` (0x08). Apply after VID/PID rules so XInput and DualSense are not reclassified as "HID Compliant game controller".

### 4.8 Optional enhancements (later)

- Auto-refresh timer when Controller tab is visible; expand/collapse state per group; copy device path to clipboard.

---

## 5. Implementation notes for Controller tab HID subsection

- **Reuse enumeration logic:** Generalize the loop in `dualsense_hid_wrapper.cpp`’s `EnumerateHIDDevices()` (or add a shared helper in a small HID enumeration module) that:
  - Uses `SetupDiGetClassDevs` + `SetupDiEnumDeviceInterfaces` + `SetupDiGetDeviceInterfaceDetail`.
  - For each device: `CreateFile` → `HidD_GetAttributes_Direct` → optionally `HidD_GetProductString` / `HidD_GetManufacturerString` (and optionally `HidD_GetPreparsedData` + `HidP_GetCaps` for usage).
  - Pushes a small struct (path, VID, PID, product string, usage if desired) into a list.
  - Closes the handle and continues; finally destroys the device info list.
- **Classification:** For each enumerated device, classify as **XInput** (VID 0x045E + known PIDs), **DualSense** (VID 0x054C, PID 0x0CE6/0x0DF2), or **Other**. No hooks needed for this; it’s pure VID/PID (and optional usage) logic.
- **Threading:** Run enumeration on a background thread or at idle so the UI doesn’t stall; use SRWLOCK or atomics when updating a shared device list for the UI (per project rules, no `std::mutex`).
- **Controller tab:** The tab with id `"controller"` (and `show_controller_tab`) is the right place for the HID subsection. See **section 4** for the full UI design (placement, wireframes, components, states, colors).
- **Refresh:** Provide a “Refresh” button to re-enumerate so newly connected devices appear without restarting.
- **Draw function:** Implement `DrawHIDDevicesSubsection(IImGuiWrapper&)` (e.g. in a new file under `ui/new_ui/` or `widgets/`) and call it from the Controller tab lambda in `new_ui_tabs.cpp` between XInput and Remapping.

This document does not implement the UI or the shared enumeration function; it only describes how listing and hooking work and how to group devices and how the Controller tab HID subsection should look and behave (section 4).
