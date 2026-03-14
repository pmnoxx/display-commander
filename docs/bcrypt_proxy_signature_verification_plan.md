# BCrypt proxy signature verification plan

## Goal

Check every `PFN_BCrypt*` and `PFN_Get*Interface` in `bcrypt.hpp` and the corresponding proxy in `bcrypt_proxy.cpp` against the **official** Windows BCrypt API. Use Microsoft docs (bcrypt.h) as the source of truth; do not rely on Wine bcrypt.spec (it is wrong for some functions).

## Official references

- **bcrypt.h (API index):** https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/
- **Doc URL pattern:** `https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-<functionname>` (function name in lowercase, e.g. `bcryptdecrypt` for BCryptDecrypt).

## Files to check

- `src/addons/display_commander/proxy_dll/bcrypt.hpp` — typedefs for forwarding (PFN_*).
- `src/addons/display_commander/proxy_dll/bcrypt_proxy.cpp` — extern "C" proxy implementations and fn() call argument order.

## Verification process (per function)

1. **Open the official doc** for the function (see table below).
2. **Compare parameter count and order** — Our typedef and proxy must match the "Syntax" block exactly (parameter names may differ; types must be ABI-compatible: opaque handles as `void*`, ULONG, LPCWSTR, etc.).
3. **Compare return type** — NTSTATUS (we use LONG, which is correct for Windows ABI).
4. **Proxy forwarding** — In bcrypt_proxy.cpp the `fn(...)` call must pass arguments in the **same order** as the typedef and the official API (no swapped pbOutput/cbOutput, etc.).
5. **Mark result** — OK / Wrong (fix in bcrypt.hpp and bcrypt_proxy.cpp).

## Already fixed (do not regress)

| Function | Issue | Fix |
|----------|--------|-----|
| BCryptResolveProviders | Wine spec had no params; official has 8 (incl. dwMode, dwFlags). | Added dwMode, dwFlags to typedef and proxy. |
| BCryptExportKey | Our code had extra pParameterList; official has 7 params (no pParameterList). | Removed pParameterList from typedef and proxy. |
| BCryptHash | We had 8 params (pbInput, cbInput, pbHashObject, cbHashObject, pbOutput, cbOutput, dwFlags); official has 7: (pbSecret, cbSecret, pbInput, cbInput, pbOutput, cbOutput) and no dwFlags. | Fixed to 7 params: pbSecret, cbSecret, pbInput, cbInput, pbOutput, cbOutput. |

## Checklist: every function with doc link

Use this table to track. Verify each row: open the doc, compare signature, then mark OK or describe fix.

| # | Function | Official doc | Params (our typedef) | Status |
|---|----------|--------------|----------------------|--------|
| 1 | BCryptAddContextFunction | [nf-bcrypt-bcryptaddcontextfunction](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptaddcontextfunction) | 5: dwTable, pszContext, dwInterface, pszFunction, dwPosition | |
| 2 | BCryptAddContextFunctionProvider | [nf-bcrypt-bcryptaddcontextfunctionprovider](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptaddcontextfunctionprovider) | 6: + pszProvider | |
| 3 | BCryptCloseAlgorithmProvider | [nf-bcrypt-bcryptclosealgorithmprovider](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptclosealgorithmprovider) | 2: hAlgorithm, dwFlags | |
| 4 | BCryptConfigureContext | [nf-bcrypt-bcryptconfigurecontext](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptconfigurecontext) | 3: dwTable, pszContext, pConfig | |
| 5 | BCryptConfigureContextFunction | [nf-bcrypt-bcryptconfigurecontextfunction](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptconfigurecontextfunction) | 5: dwTable, pszContext, dwInterface, pszFunction, pConfig | |
| 6 | BCryptCreateContext | [nf-bcrypt-bcryptcreatecontext](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptcreatecontext) | 3: dwTable, pszContext, pConfig | |
| 7 | BCryptCreateHash | [nf-bcrypt-bcryptcreatehash](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptcreatehash) | 7: hAlgorithm, phHash, pbHashObject, cbHashObject, pbSecret, cbSecret, dwFlags | |
| 8 | BCryptDecrypt | [nf-bcrypt-bcryptdecrypt](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptdecrypt) | 10: hKey, pbInput, cbInput, pPaddingInfo, pbIV, cbIV, pbOutput, cbOutput, pcbResult, dwFlags | |
| 9 | BCryptDeleteContext | [nf-bcrypt-bcryptdeletecontext](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptdeletecontext) | 2: dwTable, pszContext | |
| 10 | BCryptDeriveKey | [nf-bcrypt-bcryptderivekey](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptderivekey) | 7: hSecret, pwszKdf, pParameterList, pbDerivedKey, cbDerivedKey, pcbResult, dwFlags | |
| 11 | BCryptDeriveKeyCapi | [nf-bcrypt-bcryptderivekeycapi](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptderivekeycapi) | 5: hHash, hAlg, pbDerivedKey, cbDerivedKey, dwFlags | |
| 12 | BCryptDeriveKeyPBKDF2 | [nf-bcrypt-bcryptderivekeypbkdf2](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptderivekeypbkdf2) | 9: hPrf, pbPassword, cbPassword, pbSalt, cbSalt, cIterations, pbDerivedKey, cbDerivedKey, dwFlags | |
| 13 | BCryptDestroyHash | [nf-bcrypt-bcryptdestroyhash](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptdestroyhash) | 1: hHash | |
| 14 | BCryptDestroyKey | [nf-bcrypt-bcryptdestroykey](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptdestroykey) | 1: hKey | |
| 15 | BCryptDestroySecret | [nf-bcrypt-bcryptdestroysecret](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptdestroysecret) | 1: hSecret | |
| 16 | BCryptDuplicateHash | [nf-bcrypt-bcryptduplicatehash](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptduplicatehash) | 5: hHash, phNewHash, pbHashObject, cbHashObject, dwFlags | |
| 17 | BCryptDuplicateKey | [nf-bcrypt-bcryptduplicatekey](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptduplicatekey) | 5: hKey, phNewKey, pbKeyObject, cbKeyObject, dwFlags | |
| 18 | BCryptEncrypt | [nf-bcrypt-bcryptencrypt](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptencrypt) | 10: hKey, pbInput, cbInput, pPaddingInfo, pbIV, cbIV, pbOutput, cbOutput, pcbResult, dwFlags | |
| 19 | BCryptEnumAlgorithms | [nf-bcrypt-bcryptenumalgorithms](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptenumalgorithms) | 4: dwAlgOperations, pAlgCount, ppAlgList, dwFlags | |
| 20 | BCryptEnumContextFunctionProviders | [nf-bcrypt-bcryptenumcontextfunctionproviders](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptenumcontextfunctionproviders) | 6: dwTable, pszContext, dwInterface, pszFunction, pcbBuffer, ppBuffer | |
| 21 | BCryptEnumContextFunctions | [nf-bcrypt-bcryptenumcontextfunctions](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptenumcontextfunctions) | 5: dwTable, pszContext, dwInterface, pcbBuffer, ppBuffer | |
| 22 | BCryptEnumContexts | [nf-bcrypt-bcryptenumcontexts](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptenumcontexts) | 3: dwTable, pcbBuffer, ppBuffer | |
| 23 | BCryptEnumProviders | [nf-bcrypt-bcryptenumproviders](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptenumproviders) | 4: dwAlgOperations, pImplCount, ppImplList, dwFlags | |
| 24 | BCryptEnumRegisteredProviders | [nf-bcrypt-bcryptenumregisteredproviders](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptenumregisteredproviders) | 2: pcbBuffer, ppBuffer | |
| 25 | BCryptExportKey | [nf-bcrypt-bcryptexportkey](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptexportkey) | 7: hKey, hExportKey, pszBlobType, pbOutput, cbOutput, pcbResult, dwFlags | **Fixed** (was 8 with pParameterList) |
| 26 | BCryptFinalizeKeyPair | [nf-bcrypt-bcryptfinalizekeypair](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptfinalizekeypair) | 2: hKey, dwFlags | |
| 27 | BCryptFinishHash | [nf-bcrypt-bcryptfinishhash](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptfinishhash) | 4: hHash, pbOutput, cbOutput, dwFlags | |
| 28 | BCryptFreeBuffer | [nf-bcrypt-bcryptfreebuffer](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptfreebuffer) | 1: pvBuffer (return void) | |
| 29 | BCryptGenRandom | [nf-bcrypt-bcryptgenrandom](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptgenrandom) | 4: hAlgorithm, pbBuffer, cbBuffer, dwFlags | |
| 30 | BCryptGenerateKeyPair | [nf-bcrypt-bcryptgeneratekeypair](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptgeneratekeypair) | 4: hAlgorithm, phKey, dwLength, dwFlags | |
| 31 | BCryptGenerateSymmetricKey | [nf-bcrypt-bcryptgeneratesymmetrickey](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptgeneratesymmetrickey) | 7: hAlgorithm, phKey, pbKeyObject, cbKeyObject, pbSecret, cbSecret, dwFlags | |
| 32 | BCryptGetFipsAlgorithmMode | [nf-bcrypt-bcryptgetfipsalgorithmmode](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptgetfipsalgorithmmode) | 1: pfEnabled (BOOLEAN*; we use void*) | |
| 33 | BCryptGetProperty | [nf-bcrypt-bcryptgetproperty](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptgetproperty) | 6: hObject, pszProperty, pbOutput, cbOutput, pcbResult, dwFlags | |
| 34 | BCryptHash | [nf-bcrypt-bcrypthash](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcrypthash) | 7: hAlgorithm, pbSecret, cbSecret, pbInput, cbInput, pbOutput, cbOutput | **Fixed** (was 8 with pbHashObject/cbHashObject/dwFlags) |
| 35 | BCryptHashData | [nf-bcrypt-bcrypthashdata](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcrypthashdata) | 4: hHash, pbInput, cbInput, dwFlags | |
| 36 | BCryptImportKey | [nf-bcrypt-bcryptimportkey](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptimportkey) | 9: hAlgorithm, hImportKey, pszBlobType, phKey, pbKeyObject, cbKeyObject, pbInput, cbInput, dwFlags | |
| 37 | BCryptImportKeyPair | [nf-bcrypt-bcryptimportkeypair](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptimportkeypair) | 7: hAlgorithm, hImportKey, pszBlobType, phKey, pbInput, cbInput, dwFlags | |
| 38 | BCryptKeyDerivation | [nf-bcrypt-bcryptkeyderivation](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptkeyderivation) | 6: hKey, pParameterList, pbDerivedKey, cbDerivedKey, pcbResult, dwFlags | |
| 39 | BCryptOpenAlgorithmProvider | [nf-bcrypt-bcryptopenalgorithmprovider](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptopenalgorithmprovider) | 4: phAlgorithm, pszAlgId, pszImplementation, dwFlags | |
| 40 | BCryptQueryContextConfiguration | [nf-bcrypt-bcryptquerycontextconfiguration](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptquerycontextconfiguration) | 4: dwTable, pszContext, pcbBuffer, ppBuffer | |
| 41 | BCryptQueryContextFunctionConfiguration | [nf-bcrypt-bcryptquerycontextfunctionconfiguration](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptquerycontextfunctionconfiguration) | 5: dwTable, pszContext, dwInterface, pcbBuffer, ppBuffer | |
| 42 | BCryptQueryContextFunctionProperty | [nf-bcrypt-bcryptquerycontextfunctionproperty](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptquerycontextfunctionproperty) | 7: dwTable, pszContext, dwInterface, pszFunction, pszProperty, pcbValue, ppValue | |
| 43 | BCryptQueryProviderRegistration | [nf-bcrypt-bcryptqueryproviderregistration](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptqueryproviderregistration) | 5: pszProvider, dwMode, dwInterface, pcbBuffer, ppBuffer | |
| 44 | BCryptRegisterConfigChangeNotify | [nf-bcrypt-bcryptregisterconfigchangenotify](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptregisterconfigchangenotify) | 1: pEvent (HANDLE*) | |
| 45 | BCryptRegisterProvider | [nf-bcrypt-bcryptregisterprovider](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptregisterprovider) | 3: pszProvider, dwMode, pRegistration | |
| 46 | BCryptRemoveContextFunction | [nf-bcrypt-bcryptremovecontextfunction](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptremovecontextfunction) | 4: dwTable, pszContext, dwInterface, pszFunction | |
| 47 | BCryptRemoveContextFunctionProvider | [nf-bcrypt-bcryptremovecontextfunctionprovider](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptremovecontextfunctionprovider) | 5: + pszProvider | |
| 48 | BCryptResolveProviders | [nf-bcrypt-bcryptresolveproviders](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptresolveproviders) | 8: pszContext, dwInterface, pszFunction, pszProvider, dwMode, dwFlags, pcbBuffer, ppBuffer | **Fixed** (was 6) |
| 49 | BCryptSecretAgreement | [nf-bcrypt-bcryptsecretagreement](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptsecretagreement) | 4: hPrivKey, hPubKey, phSecret, dwFlags | |
| 50 | BCryptSetAuditingInterface | [nf-bcrypt-bcryptsetauditinginterface](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptsetauditinginterface) | 1: pAuditingInterface | |
| 51 | BCryptSetContextFunctionProperty | [nf-bcrypt-bcryptsetcontextfunctionproperty](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptsetcontextfunctionproperty) | 7: dwTable, pszContext, dwInterface, pszFunction, pszProperty, pbValue, cbValue | |
| 52 | BCryptSetProperty | [nf-bcrypt-bcryptsetproperty](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptsetproperty) | 5: hObject, pszProperty, pbInput, cbInput, dwFlags | |
| 53 | BCryptSignHash | [nf-bcrypt-bcryptsignhash](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptsignhash) | 8: hKey, pPaddingInfo, pbInput, cbInput, pbOutput, cbOutput, pcbResult, dwFlags | |
| 54 | BCryptUnregisterConfigChangeNotify | [nf-bcrypt-bcryptunregisterconfigchangenotify](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptunregisterconfigchangenotify) | 1: hEvent (HANDLE) | |
| 55 | BCryptUnregisterProvider | [nf-bcrypt-bcryptunregisterprovider](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptunregisterprovider) | 1: pszProvider | |
| 56 | BCryptVerifySignature | [nf-bcrypt-bcryptverifysignature](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptverifysignature) | 7: hKey, pPaddingInfo, pbHash, cbHash, pbSignature, cbSignature, dwFlags | |
| 57 | GetAsymmetricEncryptionInterface | (undocumented; no nf-bcrypt page) | 0 | |
| 58 | GetCipherInterface | (undocumented) | 0 | |
| 59 | GetHashInterface | (undocumented) | 0 | |
| 60 | GetRngInterface | (undocumented) | 0 | |
| 61 | GetSecretAgreementInterface | (undocumented) | 0 | |
| 62 | GetSignatureInterface | (undocumented) | 0 | |

## High-risk functions (check first)

These have many pointer/size parameters; param order or count mistakes cause crashes:

- BCryptDecrypt, BCryptEncrypt (10 params: buffers + sizes)
- BCryptExportKey (7 params) — **fixed**
- BCryptImportKey, BCryptImportKeyPair (many buffers)
- BCryptCreateHash, BCryptHash (multiple buffers)
- BCryptResolveProviders (8 params) — **fixed**
- BCryptGetProperty, BCryptSetProperty (buffer + size)
- BCryptSignHash, BCryptVerifySignature

## Notes

- **BOOLEAN* vs void*:** BCryptGetFipsAlgorithmMode takes `BOOLEAN *pfEnabled`. Using `void*` in our typedef is ABI-safe (same pointer size); the real function receives the same pointer.
- **Get*Interface:** No official nf-bcrypt docs; these return interface pointers (void*), take no args; keep as-is unless we find a definitive source.
- After fixing any signature, run a full build and optionally a quick test (e.g. load a game that uses bcrypt.dll proxy) to confirm no regressions.
