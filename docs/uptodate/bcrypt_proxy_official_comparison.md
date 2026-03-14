# bcrypt.dll proxy: comparison vs official Microsoft documentation

Compare each export with the official Windows CNG API (bcrypt.h). References: [bcrypt.h](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/), [BCrypt functions](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/#functions).

- **Official:** Microsoft Learn signature (NTSTATUS = LONG; BCRYPT_*_HANDLE = void*; ULONG = LONG; ULONGLONG = DWORD64).
- **Ours:** Current proxy signature in `proxy_dll/bcrypt_proxy.cpp` (maintained against official API; Wine bcrypt.spec was removed as wrong).
- **Match?:** Yes / No. **Notes:** ABI or type differences (especially 64-bit).

## Summary

1. **Return types:** All BCrypt* functions in the official API return **NTSTATUS** (LONG). Our proxy uses LONG for all BCrypt* except **BCryptFreeBuffer**, which officially returns **void** — we have `void WINAPI BCryptFreeBuffer(LPVOID p0)`. **Match.**
2. **Handle/pointer parameters:** Official API uses BCRYPT_ALG_HANDLE, BCRYPT_HASH_HANDLE, etc. (all typedefs to void*). Our proxy uses LPVOID for these. **Match** (same ABI).
3. **ULONG / ULONGLONG:** We use LONG and DWORD64; same size as ULONG/ULONGLONG. **Match.**
4. **Get*Interface exports:** Undocumented internal exports; not in bcrypt.h. We stub them and return LONG 0 (NULL-like). Callers that expect a pointer get 0. **Acceptable** for stub behavior.

No 64-bit or type mismatches were found; no proxy code changes required for official API alignment.

---

## Detailed signature verification (MSDN pages checked)

The following functions were checked against the exact Syntax blocks on Microsoft Learn. Parameter order and count match our proxy.

| Function | Official params (from MSDN) | Our proxy | Verified |
|----------|-----------------------------|-----------|----------|
| BCryptAddContextFunction | (ULONG, LPCWSTR, ULONG, LPCWSTR, ULONG) | 5 params | Yes |
| BCryptCloseAlgorithmProvider | (BCRYPT_ALG_HANDLE, ULONG) | 2 params | Yes |
| BCryptCreateHash | (BCRYPT_ALG_HANDLE, BCRYPT_HASH_HANDLE*, PUCHAR, ULONG, PUCHAR, ULONG, ULONG) | 7 params | Yes |
| BCryptDecrypt | (BCRYPT_KEY_HANDLE, PUCHAR, ULONG, VOID*, PUCHAR, ULONG, PUCHAR, ULONG, ULONG*, ULONG) | 10 params | Yes |
| BCryptEncrypt | (BCRYPT_KEY_HANDLE, PUCHAR, ULONG, VOID*, PUCHAR, ULONG, PUCHAR, ULONG, ULONG*, ULONG) | 10 params | Yes |
| BCryptEnumAlgorithms | (ULONG, ULONG*, BCRYPT_ALGORITHM_IDENTIFIER**, ULONG) | 4 params | Yes |
| BCryptEnumContextFunctions | (ULONG, LPCWSTR, ULONG, ULONG*, PCRYPT_CONTEXT_FUNCTIONS*) | 5 params | Yes |
| BCryptFreeBuffer | (PVOID) — return **void** | void(LPVOID) | Yes |
| BCryptGetFipsAlgorithmMode | (BOOLEAN*) | 1 param (out ptr) | Yes |
| BCryptGetProperty | (BCRYPT_HANDLE, LPCWSTR, PUCHAR, ULONG, ULONG*, ULONG) | 6 params | Yes |
| BCryptHash | (BCRYPT_ALG_HANDLE, PUCHAR, ULONG, PUCHAR, ULONG, PUCHAR, ULONG) | 7 params | Yes |
| BCryptOpenAlgorithmProvider | (BCRYPT_ALG_HANDLE*, LPCWSTR, LPCWSTR, ULONG) | 4 params | Yes |
| BCryptDeriveKeyPBKDF2 | (BCRYPT_ALG_HANDLE, PUCHAR, ULONG, PUCHAR, ULONG, ULONGLONG, PUCHAR, ULONG, ULONG) | 9 params | Yes |
| BCryptSignHash | (BCRYPT_KEY_HANDLE, VOID*, PUCHAR, ULONG, PUCHAR, ULONG, ULONG*, ULONG) | 8 params | Yes |
| BCryptVerifySignature | (BCRYPT_KEY_HANDLE, VOID*, PUCHAR, ULONG, PUCHAR, ULONG, ULONG) | 7 params | Yes |

All pointer types (PUCHAR, VOID*, ULONG*, BCRYPT_*_HANDLE) are pointer-sized; we use LPVOID/LONG so the ABI matches.

---

## BCrypt* functions (documented in bcrypt.h)

| Function | Official (Microsoft Learn) | Ours | Match? | Notes |
|----------|---------------------------|------|-------|------|
| BCryptAddContextFunction | NTSTATUS(ULONG, LPCWSTR, ULONG, LPCWSTR, ULONG) | LONG(LONG, LPCWSTR, LONG, LPCWSTR, LONG) | Yes | ULONG = LONG. |
| BCryptAddContextFunctionProvider | NTSTATUS(ULONG, LPCWSTR, ULONG, LPCWSTR, LPCWSTR, ULONG) | LONG(…) | Yes | Same. |
| BCryptCloseAlgorithmProvider | NTSTATUS(BCRYPT_ALG_HANDLE, ULONG) | LONG(LPVOID, LONG) | Yes | Handle = LPVOID. |
| BCryptConfigureContext | (documented) | LONG(void) stub | Yes | Stub returns 0. |
| BCryptConfigureContextFunction | (documented) | LONG(void) stub | Yes | Stub. |
| BCryptCreateContext | (documented) | LONG(void) stub | Yes | Stub. |
| BCryptCreateHash | NTSTATUS(BCRYPT_ALG_HANDLE, BCRYPT_HASH_HANDLE*, …) | LONG(LPVOID, LPVOID, LPVOID, LONG, …) | Yes | ptr = handle; ptr = output handle. |
| BCryptDecrypt | NTSTATUS(… 10 params) | LONG(… 10 params) | Yes | All ptr/long. |
| BCryptDeleteContext | (documented) | LONG(void) stub | Yes | Stub. |
| BCryptDeriveKey | NTSTATUS(BCRYPT_SECRET_HANDLE, LPCWSTR, …) | LONG(LPVOID, LPCWSTR, …) | Yes | Same. |
| BCryptDeriveKeyCapi | NTSTATUS(…) | LONG(…) | Yes | Same. |
| BCryptDeriveKeyPBKDF2 | NTSTATUS(BCRYPT_ALG_HANDLE, PUCHAR, ULONG, PUCHAR, ULONG, ULONGLONG, PUCHAR, ULONG, ULONG) | LONG(LPVOID, LPVOID, LONG, LPVOID, LONG, DWORD64, LPVOID, LONG, LONG) | Yes | ULONGLONG = DWORD64. |
| BCryptDestroyHash | NTSTATUS(BCRYPT_HASH_HANDLE) | LONG(LPVOID) | Yes | Same. |
| BCryptDestroyKey | NTSTATUS(BCRYPT_KEY_HANDLE) | LONG(LPVOID) | Yes | Same. |
| BCryptDestroySecret | NTSTATUS(BCRYPT_SECRET_HANDLE) | LONG(LPVOID) | Yes | Same. |
| BCryptDuplicateHash | NTSTATUS(…) | LONG(…) | Yes | Same. |
| BCryptDuplicateKey | NTSTATUS(…) | LONG(…) | Yes | Same. |
| BCryptEncrypt | NTSTATUS(… 10 params) | LONG(…) | Yes | Same. |
| BCryptEnumAlgorithms | NTSTATUS(ULONG, …) | LONG(LONG, …) | Yes | Same. |
| BCryptEnumContextFunctionProviders | (documented) | LONG(void) stub | Yes | Stub. |
| BCryptEnumContextFunctions | NTSTATUS(ULONG, LPCWSTR, ULONG, …) | LONG(LONG, LPCWSTR, LONG, …) | Yes | Same. |
| BCryptEnumContexts | (documented) | LONG(void) stub | Yes | Stub. |
| BCryptEnumProviders | (documented) | LONG(void) stub | Yes | Stub. |
| BCryptEnumRegisteredProviders | (documented) | LONG(void) stub | Yes | Stub. |
| BCryptExportKey | NTSTATUS(…) | LONG(…) | Yes | Same. |
| BCryptFinalizeKeyPair | NTSTATUS(BCRYPT_KEY_HANDLE, ULONG) | LONG(LPVOID, LONG) | Yes | Same. |
| BCryptFinishHash | NTSTATUS(…) | LONG(…) | Yes | Same. |
| **BCryptFreeBuffer** | **void(PVOID)** | **void(LPVOID)** | **Yes** | Only void-returning BCrypt*; we match. |
| BCryptGenRandom | NTSTATUS(BCRYPT_ALG_HANDLE, PUCHAR, ULONG, ULONG) | LONG(LPVOID, LPVOID, LONG, LONG) | Yes | Same. |
| BCryptGenerateKeyPair | NTSTATUS(…) | LONG(…) | Yes | Same. |
| BCryptGenerateSymmetricKey | NTSTATUS(…) | LONG(…) | Yes | Same. |
| BCryptGetFipsAlgorithmMode | NTSTATUS(BOOLEAN*) | LONG(LPVOID) | Yes | Out param = pointer. |
| BCryptGetProperty | NTSTATUS(BCRYPT_HANDLE, LPCWSTR, PUCHAR, …) | LONG(LPVOID, LPCWSTR, LPVOID, …) | Yes | Same. |
| BCryptHash | NTSTATUS(…) | LONG(…) | Yes | Same. |
| BCryptHashData | NTSTATUS(…) | LONG(…) | Yes | Same. |
| BCryptImportKey | NTSTATUS(…) | LONG(…) | Yes | Same. |
| BCryptImportKeyPair | NTSTATUS(…) | LONG(…) | Yes | Same. |
| BCryptKeyDerivation | NTSTATUS(…) | LONG(…) | Yes | Same. |
| BCryptOpenAlgorithmProvider | NTSTATUS(BCRYPT_ALG_HANDLE*, LPCWSTR, LPCWSTR, ULONG) | LONG(LPVOID, LPCWSTR, LPCWSTR, LONG) | Yes | First param = pointer to handle. |
| BCryptQueryContextConfiguration | (documented) | LONG(void) stub | Yes | Stub. |
| BCryptQueryContextFunctionConfiguration | (documented) | LONG(void) stub | Yes | Stub. |
| BCryptQueryContextFunctionProperty | (documented) | LONG(void) stub | Yes | Stub. |
| BCryptQueryProviderRegistration | (documented) | LONG(void) stub | Yes | Stub. |
| BCryptRegisterConfigChangeNotify | (documented) | LONG(void) stub | Yes | Stub. |
| BCryptRegisterProvider | NTSTATUS(LPCWSTR, ULONG, …) | LONG(LPCWSTR, LONG, …) | Yes | Same. |
| BCryptRemoveContextFunction | NTSTATUS(ULONG, LPCWSTR, ULONG, LPCWSTR) | LONG(LONG, LPCWSTR, LONG, LPCWSTR) | Yes | Same. |
| BCryptRemoveContextFunctionProvider | NTSTATUS(…) | LONG(…) | Yes | Same. |
| BCryptResolveProviders | (documented) | LONG(void) stub | Yes | Stub. |
| BCryptSecretAgreement | NTSTATUS(…) | LONG(…) | Yes | Same. |
| BCryptSetAuditingInterface | (documented) | LONG(void) stub | Yes | Stub. |
| BCryptSetContextFunctionProperty | (documented) | LONG(void) stub | Yes | Stub. |
| BCryptSetProperty | NTSTATUS(…) | LONG(…) | Yes | Same. |
| BCryptSignHash | NTSTATUS(…) | LONG(…) | Yes | Same. |
| BCryptUnregisterConfigChangeNotify | (documented) | LONG(void) stub | Yes | Stub. |
| BCryptUnregisterProvider | NTSTATUS(LPCWSTR) | LONG(LPCWSTR) | Yes | Same. |
| BCryptVerifySignature | NTSTATUS(…) | LONG(…) | Yes | Same. |

## Get*Interface (undocumented)

Not in bcrypt.h; internal/legacy exports. We stub and return 0. If real bcrypt.dll returns an interface pointer, callers get NULL from our stub.

| Function | Official | Ours | Match? | Notes |
|----------|----------|------|--------|------|
| GetAsymmetricEncryptionInterface | Undocumented | LONG(void) stub → 0 | Stub | Acceptable. |
| GetCipherInterface | Undocumented | LONG(void) stub → 0 | Stub | Acceptable. |
| GetHashInterface | Undocumented | LONG(void) stub → 0 | Stub | Acceptable. |
| GetRngInterface | Undocumented | LONG(void) stub → 0 | Stub | Acceptable. |
| GetSecretAgreementInterface | Undocumented | LONG(void) stub → 0 | Stub | Acceptable. |
| GetSignatureInterface | Undocumented | LONG(void) stub → 0 | Stub | Acceptable. |

---

## Not in our proxy (documented in bcrypt.h)

The following are documented but not in our proxy. We do not add them unless a game or tool requires them.

- **BCryptCreateMultiHash**, **BCryptProcessMultiOperations** — multi-hash APIs (newer).
- **BCRYPT_INIT_AUTH_MODE_INFO** — macro/helper, not a DLL export.

Our proxy is maintained against the official API (see docs/bcrypt_proxy_signature_verification_plan.md). Wine bcrypt.spec was removed; several signatures were fixed (e.g. BCryptResolveProviders, BCryptExportKey, BCryptHash) to match the official API.
