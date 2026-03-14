# hid.dll proxy: comparison vs official Microsoft documentation

Compare each export with the official Windows HID API (hidsdi.h / hidpi.h). References: [hidsdi.h](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/hidsdi/), [hidpi.h](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/hidpi/).

- **Official:** Microsoft Learn signature (HANDLE = pointer-sized; BOOLEAN = BOOL; NTSTATUS = LONG).
- **Ours:** Current proxy signature in `proxy_dll/hid_proxy.cpp` (from Wine hid.spec via gen_proxy_from_spec.py).
- **Match?:** Yes / No. **Notes:** ABI or type differences (especially 64-bit).

## Summary of issues (fixed in code)

1. **HidD_* device-handle parameter:** Official API uses `HANDLE HidDeviceObject` (pointer-sized). Wine spec uses `long` for that parameter; on 64-bit Windows `long` is 32-bit, so the proxy would truncate the handle. **Fix:** Use `HANDLE` (or `LPVOID`) for the first parameter of all HidD_* functions that take a device handle.
2. **HidP_* return type:** Official API returns `NTSTATUS` (e.g. HIDP_STATUS_SUCCESS). Our proxy used `BOOL`. Same 32-bit size, but callers may check NTSTATUS values. **Fix:** Use `LONG` (NTSTATUS) for all HidP_* return types; stubs return 0 (or a negative NTSTATUS).

---

## HidD_* (hidsdi.h – HID class driver interface)

| Function | Official (Microsoft Learn) | Ours | Match? | Notes |
|----------|---------------------------|------|-------|------|
| HidD_FlushQueue | `BOOLEAN HidD_FlushQueue(HANDLE HidDeviceObject);` | BOOL WINAPI HidD_FlushQueue(LPVOID p0) | Yes | LPVOID = pointer-sized, same as HANDLE. |
| HidD_FreePreparsedData | `BOOLEAN HidD_FreePreparsedData(PHIDP_PREPARSED_DATA PreparsedData);` | BOOL WINAPI HidD_FreePreparsedData(LPVOID p0) | Yes | ptr for preparsed data. |
| HidD_GetAttributes | `BOOLEAN HidD_GetAttributes(HANDLE HidDeviceObject, PHIDD_ATTRIBUTES Attributes);` | BOOL WINAPI HidD_GetAttributes(LONG p0, LPVOID p1) | **No (64-bit)** | Official first param HANDLE (ptr-sized). Wine spec (long ptr) → we had LONG; on x64 handle truncated. **Fixed:** HANDLE p0. |
| HidD_GetConfiguration | (stub – not in official API) | BOOL WINAPI HidD_GetConfiguration(void) | Yes | Stub; return FALSE. |
| HidD_GetFeature | `BOOLEAN HidD_GetFeature(HANDLE HidDeviceObject, PVOID ReportBuffer, ULONG ReportBufferLength);` | BOOL WINAPI HidD_GetFeature(LONG p0, LPVOID p1, LONG p2) | **No (64-bit)** | First param HANDLE. **Fixed:** HANDLE p0. |
| HidD_GetHidGuid | `void HidD_GetHidGuid(LPGUID HidGuid);` | void WINAPI HidD_GetHidGuid(LPVOID p0) | Yes | **Fixed:** return type void to match official API (no return value). |
| HidD_GetIndexedString | `BOOLEAN HidD_GetIndexedString(HANDLE HidDeviceObject, ULONG StringIndex, PVOID Buffer, ULONG BufferLength);` | BOOL WINAPI HidD_GetIndexedString(LPVOID p0, LONG p1, LPVOID p2, LONG p3) | Yes | First param ptr (HANDLE); ULONG/LONG same size. |
| HidD_GetInputReport | `BOOLEAN HidD_GetInputReport(HANDLE HidDeviceObject, PVOID ReportBuffer, ULONG ReportBufferLength);` | BOOL WINAPI HidD_GetInputReport(LONG p0, LPVOID p1, LONG p2) | **No (64-bit)** | First param HANDLE. **Fixed:** HANDLE p0. |
| HidD_GetManufacturerString | `BOOLEAN HidD_GetManufacturerString(HANDLE HidDeviceObject, PVOID Buffer, ULONG BufferLength);` | BOOL WINAPI HidD_GetManufacturerString(LONG p0, LPVOID p1, LONG p2) | **No (64-bit)** | First param HANDLE. **Fixed:** HANDLE p0. |
| HidD_GetMsGenreDescriptor | (stub) | BOOL WINAPI HidD_GetMsGenreDescriptor(void) | Yes | Stub. |
| HidD_GetNumInputBuffers | `BOOLEAN HidD_GetNumInputBuffers(HANDLE HidDeviceObject, PULONG NumberBuffers);` | BOOL WINAPI HidD_GetNumInputBuffers(LONG p0, LPVOID p1) | **No (64-bit)** | First param HANDLE. **Fixed:** HANDLE p0. |
| HidD_GetPhysicalDescriptor | `BOOLEAN HidD_GetPhysicalDescriptor(HANDLE HidDeviceObject, PVOID Buffer, ULONG BufferLength);` | BOOL WINAPI HidD_GetPhysicalDescriptor(LONG p0, LPVOID p1, LONG p2) | **No (64-bit)** | First param HANDLE. **Fixed:** HANDLE p0. |
| HidD_GetPreparsedData | `BOOLEAN HidD_GetPreparsedData(HANDLE HidDeviceObject, PHIDP_PREPARSED_DATA *PreparsedData);` | BOOL WINAPI HidD_GetPreparsedData(LPVOID p0, LPVOID p1) | Yes | Both params pointer-sized. |
| HidD_GetProductString | `BOOLEAN HidD_GetProductString(HANDLE HidDeviceObject, PVOID Buffer, ULONG BufferLength);` | BOOL WINAPI HidD_GetProductString(LONG p0, LPVOID p1, LONG p2) | **No (64-bit)** | First param HANDLE. **Fixed:** HANDLE p0. |
| HidD_GetSerialNumberString | `BOOLEAN HidD_GetSerialNumberString(HANDLE HidDeviceObject, PVOID Buffer, ULONG BufferLength);` | BOOL WINAPI HidD_GetSerialNumberString(LONG p0, LPVOID p1, LONG p2) | **No (64-bit)** | First param HANDLE. **Fixed:** HANDLE p0. |
| HidD_Hello | (stub) | BOOL WINAPI HidD_Hello(void) | Yes | Stub. |
| HidD_SetConfiguration | (stub) | BOOL WINAPI HidD_SetConfiguration(void) | Yes | Stub. |
| HidD_SetFeature | `BOOLEAN HidD_SetFeature(HANDLE HidDeviceObject, PVOID ReportBuffer, ULONG ReportBufferLength);` | BOOL WINAPI HidD_SetFeature(LONG p0, LPVOID p1, LONG p2) | **No (64-bit)** | First param HANDLE. **Fixed:** HANDLE p0. |
| HidD_SetNumInputBuffers | `BOOLEAN HidD_SetNumInputBuffers(HANDLE HidDeviceObject, ULONG NumberBuffers);` | BOOL WINAPI HidD_SetNumInputBuffers(LONG p0, LONG p1) | **No (64-bit)** | First param HANDLE. **Fixed:** HANDLE p0. |
| HidD_SetOutputReport | `BOOLEAN HidD_SetOutputReport(HANDLE HidDeviceObject, PVOID ReportBuffer, ULONG ReportBufferLength);` | BOOL WINAPI HidD_SetOutputReport(LONG p0, LPVOID p1, LONG p2) | **No (64-bit)** | First param HANDLE. **Fixed:** HANDLE p0. |

## HidP_* (hidpi.h – HID parsing library)

All HidP_* routines return **NTSTATUS** (LONG) in the official API (e.g. HIDP_STATUS_SUCCESS, HIDP_STATUS_INVALID_PREPARSED_DATA). Our proxy used BOOL; same 32-bit size but wrong semantics. **Fixed:** return type LONG for all HidP_*.

| Function | Official return | Ours (before fix) | Match? | Notes |
|----------|-----------------|--------------------|--------|-------|
| HidP_GetButtonCaps | NTSTATUS | BOOL | **No** | **Fixed:** LONG. |
| HidP_GetCaps | NTSTATUS | BOOL | **No** | **Fixed:** LONG. |
| HidP_GetData | NTSTATUS | BOOL | **No** | **Fixed:** LONG. |
| HidP_GetExtendedAttributes | NTSTATUS (stub in our list) | BOOL | **No** | Stub; **Fixed:** LONG, return 0. |
| HidP_GetLinkCollectionNodes | NTSTATUS | BOOL | **No** | **Fixed:** LONG. |
| HidP_GetScaledUsageValue | NTSTATUS | BOOL | **No** | **Fixed:** LONG. |
| HidP_GetSpecificButtonCaps | NTSTATUS | BOOL | **No** | **Fixed:** LONG. |
| HidP_GetSpecificValueCaps | NTSTATUS | BOOL | **No** | **Fixed:** LONG. |
| HidP_GetUsageValue | NTSTATUS | BOOL | **No** | **Fixed:** LONG. |
| HidP_GetUsageValueArray | NTSTATUS | BOOL | **No** | **Fixed:** LONG. |
| HidP_GetUsages | NTSTATUS | BOOL | **No** | **Fixed:** LONG. |
| HidP_GetUsagesEx | NTSTATUS | BOOL | **No** | **Fixed:** LONG. |
| HidP_GetValueCaps | NTSTATUS | BOOL | **No** | **Fixed:** LONG. |
| HidP_InitializeReportForID | NTSTATUS | BOOL | **No** | **Fixed:** LONG. |
| HidP_MaxDataListLength | NTSTATUS | BOOL | **No** | **Fixed:** LONG. |
| HidP_MaxUsageListLength | NTSTATUS | BOOL | **No** | **Fixed:** LONG. |
| HidP_SetData | NTSTATUS | BOOL | **No** | **Fixed:** LONG. |
| HidP_SetScaledUsageValue | NTSTATUS | BOOL | **No** | **Fixed:** LONG. |
| HidP_SetUsageValue | NTSTATUS | BOOL | **No** | **Fixed:** LONG. |
| HidP_SetUsageValueArray | NTSTATUS | BOOL | **No** | **Fixed:** LONG. |
| HidP_SetUsages | NTSTATUS | BOOL | **No** | **Fixed:** LONG. |
| HidP_TranslateUsagesToI8042ScanCodes | NTSTATUS | BOOL | **No** | **Fixed:** LONG. |
| HidP_UnsetUsages | NTSTATUS | BOOL | **No** | **Fixed:** LONG. |
| HidP_UsageListDifference | NTSTATUS (stub) | BOOL | **No** | Stub; **Fixed:** LONG, return 0. |

---

## Wine spec vs official (for generator)

Wine `hid.spec` uses `long` for the first parameter of many HidD_* functions; on Windows x64 that is 32-bit and does not match HANDLE (64-bit). When regenerating from the spec, either:

- Manually correct the proxy for 64-bit (HANDLE for device-handle params, LONG for HidP_* return), or
- Use a spec that uses `ptr` for handle parameters (Wine may use `long` for 32-bit compatibility).

Our proxy is hand-corrected in `hid_proxy.cpp` for the above; the generator script still emits LONG/BOOL from the spec.
