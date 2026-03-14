/*
 * bcrypt.dll proxy. Signatures follow the official Windows API only.
 * Wine bcrypt.spec was removed (wrong for several functions). See docs/bcrypt_proxy_signature_verification_plan.md.
 *
 * Official API: https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/
 * Header: bcrypt.h (we must not include it here so our extern "C" proxy symbols do not conflict
 * with the SDK declarations (C2733 on x86)). We include Windows.h with WIN32_LEAN_AND_MEAN first
 * so that any later inclusion of windows.h (e.g. from timing.hpp) does not pull in bcrypt.h.
 */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "../utils/detour_call_tracker.hpp"
#include "../utils/timing.hpp"
#include "bcrypt.hpp"

// Libraries <standard C++>
#include <string>

#ifndef STATUS_NOT_IMPLEMENTED
#define STATUS_NOT_IMPLEMENTED ((NTSTATUS)0xC0000002L)
#endif

namespace {

HMODULE g_bcrypt_module = nullptr;

bool LoadRealBcrypt() {
    if (g_bcrypt_module != nullptr) return true;
    WCHAR system_path[MAX_PATH];
    if (GetSystemDirectoryW(system_path, MAX_PATH) == 0) return false;
    std::wstring path = std::wstring(system_path) + L"\\bcrypt.dll";
    g_bcrypt_module = LoadLibraryW(path.c_str());
    return g_bcrypt_module != nullptr;
}

}  // namespace

extern "C" LONG WINAPI BCryptAddContextFunction(ULONG dwTable, LPCWSTR pszContext, ULONG dwInterface,
                                                LPCWSTR pszFunction, ULONG dwPosition) {
    CALL_GUARD(utils::get_now_ns());
    if (!LoadRealBcrypt()) return (LONG)STATUS_NOT_IMPLEMENTED;
    auto fn = (PFN_BCryptAddContextFunction)GetProcAddress(g_bcrypt_module, "BCryptAddContextFunction");
    if (fn == nullptr) return (LONG)STATUS_NOT_IMPLEMENTED;
    return (LONG)fn(dwTable, pszContext, dwInterface, pszFunction, dwPosition);
}

extern "C" LONG WINAPI BCryptAddContextFunctionProvider(ULONG dwTable, LPCWSTR pszContext, ULONG dwInterface,
                                                        LPCWSTR pszFunction, LPCWSTR pszProvider, ULONG dwPosition) {
    CALL_GUARD(utils::get_now_ns());
    if (!LoadRealBcrypt()) return (LONG)STATUS_NOT_IMPLEMENTED;
    auto fn = (PFN_BCryptAddContextFunctionProvider)GetProcAddress(g_bcrypt_module, "BCryptAddContextFunctionProvider");
    if (fn == nullptr) return (LONG)STATUS_NOT_IMPLEMENTED;
    return (LONG)fn(dwTable, pszContext, dwInterface, pszFunction, pszProvider, dwPosition);
}

extern "C" LONG WINAPI BCryptCloseAlgorithmProvider(void* hAlgorithm, ULONG dwFlags) {
    CALL_GUARD(utils::get_now_ns());
    if (!LoadRealBcrypt()) return (LONG)STATUS_NOT_IMPLEMENTED;
    auto fn = (PFN_BCryptCloseAlgorithmProvider)GetProcAddress(g_bcrypt_module, "BCryptCloseAlgorithmProvider");
    if (fn == nullptr) return (LONG)STATUS_NOT_IMPLEMENTED;
    return (LONG)fn(hAlgorithm, dwFlags);
}

extern "C" LONG WINAPI BCryptConfigureContext(ULONG dwTable, LPCWSTR pszContext, void* pConfig) {
    CALL_GUARD(utils::get_now_ns());
    if (!LoadRealBcrypt()) return (LONG)STATUS_NOT_IMPLEMENTED;
    auto fn = (PFN_BCryptConfigureContext)GetProcAddress(g_bcrypt_module, "BCryptConfigureContext");
    if (fn == nullptr) return (LONG)STATUS_NOT_IMPLEMENTED;
    return (LONG)fn(dwTable, pszContext, pConfig);
}

extern "C" LONG WINAPI BCryptConfigureContextFunction(ULONG dwTable, LPCWSTR pszContext, ULONG dwInterface,
                                                      LPCWSTR pszFunction, void* pConfig) {
    CALL_GUARD(utils::get_now_ns());
    if (!LoadRealBcrypt()) return (LONG)STATUS_NOT_IMPLEMENTED;
    auto fn = (PFN_BCryptConfigureContextFunction)GetProcAddress(g_bcrypt_module, "BCryptConfigureContextFunction");
    if (fn == nullptr) return (LONG)STATUS_NOT_IMPLEMENTED;
    return (LONG)fn(dwTable, pszContext, dwInterface, pszFunction, pConfig);
}

extern "C" LONG WINAPI BCryptCreateContext(ULONG dwTable, LPCWSTR pszContext, void* pConfig) {
    CALL_GUARD(utils::get_now_ns());
    if (!LoadRealBcrypt()) return (LONG)STATUS_NOT_IMPLEMENTED;
    auto fn = (PFN_BCryptCreateContext)GetProcAddress(g_bcrypt_module, "BCryptCreateContext");
    if (fn == nullptr) return (LONG)STATUS_NOT_IMPLEMENTED;
    return (LONG)fn(dwTable, pszContext, pConfig);
}

extern "C" LONG WINAPI BCryptCreateHash(void* hAlgorithm, void* phHash, void* pbHashObject, ULONG cbHashObject,
                                        void* pbSecret, ULONG cbSecret, ULONG dwFlags) {
    CALL_GUARD(utils::get_now_ns());
    if (!LoadRealBcrypt()) return (LONG)STATUS_NOT_IMPLEMENTED;
    auto fn = (PFN_BCryptCreateHash)GetProcAddress(g_bcrypt_module, "BCryptCreateHash");
    if (fn == nullptr) return (LONG)STATUS_NOT_IMPLEMENTED;
    return (LONG)fn(hAlgorithm, phHash, pbHashObject, cbHashObject, pbSecret, cbSecret, dwFlags);
}

extern "C" LONG WINAPI BCryptDecrypt(void* hKey, void* pbInput, ULONG cbInput, void* pPaddingInfo, void* pbIV,
                                     ULONG cbIV, void* pbOutput, ULONG cbOutput, ULONG* pcbResult, ULONG dwFlags) {
    CALL_GUARD(utils::get_now_ns());
    if (!LoadRealBcrypt()) return (LONG)STATUS_NOT_IMPLEMENTED;
    auto fn = (PFN_BCryptDecrypt)GetProcAddress(g_bcrypt_module, "BCryptDecrypt");
    if (fn == nullptr) return (LONG)STATUS_NOT_IMPLEMENTED;
    return (LONG)fn(hKey, pbInput, cbInput, pPaddingInfo, pbIV, cbIV, pbOutput, cbOutput, pcbResult, dwFlags);
}

extern "C" LONG WINAPI BCryptDeleteContext(ULONG dwTable, LPCWSTR pszContext) {
    CALL_GUARD(utils::get_now_ns());
    if (!LoadRealBcrypt()) return (LONG)STATUS_NOT_IMPLEMENTED;
    auto fn = (PFN_BCryptDeleteContext)GetProcAddress(g_bcrypt_module, "BCryptDeleteContext");
    if (fn == nullptr) return (LONG)STATUS_NOT_IMPLEMENTED;
    return (LONG)fn(dwTable, pszContext);
}

extern "C" LONG WINAPI BCryptDeriveKey(void* hSecret, LPCWSTR pwszKdf, void* pParameterList, void* pbDerivedKey,
                                       ULONG cbDerivedKey, ULONG* pcbResult, ULONG dwFlags) {
    CALL_GUARD(utils::get_now_ns());
    if (!LoadRealBcrypt()) return (LONG)STATUS_NOT_IMPLEMENTED;
    auto fn = (PFN_BCryptDeriveKey)GetProcAddress(g_bcrypt_module, "BCryptDeriveKey");
    if (fn == nullptr) return (LONG)STATUS_NOT_IMPLEMENTED;
    return (LONG)fn(hSecret, pwszKdf, pParameterList, pbDerivedKey, cbDerivedKey, pcbResult, dwFlags);
}

extern "C" LONG WINAPI BCryptDeriveKeyCapi(void* hHash, void* hAlg, void* pbDerivedKey, ULONG cbDerivedKey,
                                           ULONG dwFlags) {
    CALL_GUARD(utils::get_now_ns());
    if (!LoadRealBcrypt()) return (LONG)STATUS_NOT_IMPLEMENTED;
    auto fn = (PFN_BCryptDeriveKeyCapi)GetProcAddress(g_bcrypt_module, "BCryptDeriveKeyCapi");
    if (fn == nullptr) return (LONG)STATUS_NOT_IMPLEMENTED;
    return (LONG)fn(hHash, hAlg, pbDerivedKey, cbDerivedKey, dwFlags);
}

extern "C" LONG WINAPI BCryptDeriveKeyPBKDF2(void* hPrf, void* pbPassword, ULONG cbPassword, void* pbSalt, ULONG cbSalt,
                                             ULONGLONG cIterations, void* pbDerivedKey, ULONG cbDerivedKey,
                                             ULONG dwFlags) {
    CALL_GUARD(utils::get_now_ns());
    if (!LoadRealBcrypt()) return (LONG)STATUS_NOT_IMPLEMENTED;
    auto fn = (PFN_BCryptDeriveKeyPBKDF2)GetProcAddress(g_bcrypt_module, "BCryptDeriveKeyPBKDF2");
    if (fn == nullptr) return (LONG)STATUS_NOT_IMPLEMENTED;
    return (LONG)fn(hPrf, pbPassword, cbPassword, pbSalt, cbSalt, cIterations, pbDerivedKey, cbDerivedKey, dwFlags);
}

extern "C" LONG WINAPI BCryptDestroyHash(void* hHash) {
    CALL_GUARD(utils::get_now_ns());
    if (!LoadRealBcrypt()) return (LONG)STATUS_NOT_IMPLEMENTED;
    auto fn = (PFN_BCryptDestroyHash)GetProcAddress(g_bcrypt_module, "BCryptDestroyHash");
    if (fn == nullptr) return (LONG)STATUS_NOT_IMPLEMENTED;
    return (LONG)fn(hHash);
}

extern "C" LONG WINAPI BCryptDestroyKey(void* hKey) {
    CALL_GUARD(utils::get_now_ns());
    if (!LoadRealBcrypt()) return (LONG)STATUS_NOT_IMPLEMENTED;
    auto fn = (PFN_BCryptDestroyKey)GetProcAddress(g_bcrypt_module, "BCryptDestroyKey");
    if (fn == nullptr) return (LONG)STATUS_NOT_IMPLEMENTED;
    return (LONG)fn(hKey);
}

extern "C" LONG WINAPI BCryptDestroySecret(void* hSecret) {
    CALL_GUARD(utils::get_now_ns());
    if (!LoadRealBcrypt()) return (LONG)STATUS_NOT_IMPLEMENTED;
    auto fn = (PFN_BCryptDestroySecret)GetProcAddress(g_bcrypt_module, "BCryptDestroySecret");
    if (fn == nullptr) return (LONG)STATUS_NOT_IMPLEMENTED;
    return (LONG)fn(hSecret);
}

extern "C" LONG WINAPI BCryptDuplicateHash(void* hHash, void* phNewHash, void* pbHashObject, ULONG cbHashObject,
                                           ULONG dwFlags) {
    CALL_GUARD(utils::get_now_ns());
    if (!LoadRealBcrypt()) return (LONG)STATUS_NOT_IMPLEMENTED;
    auto fn = (PFN_BCryptDuplicateHash)GetProcAddress(g_bcrypt_module, "BCryptDuplicateHash");
    if (fn == nullptr) return (LONG)STATUS_NOT_IMPLEMENTED;
    return (LONG)fn(hHash, phNewHash, pbHashObject, cbHashObject, dwFlags);
}

extern "C" LONG WINAPI BCryptDuplicateKey(void* hKey, void* phNewKey, void* pbKeyObject, ULONG cbKeyObject,
                                          ULONG dwFlags) {
    CALL_GUARD(utils::get_now_ns());
    if (!LoadRealBcrypt()) return (LONG)STATUS_NOT_IMPLEMENTED;
    auto fn = (PFN_BCryptDuplicateKey)GetProcAddress(g_bcrypt_module, "BCryptDuplicateKey");
    if (fn == nullptr) return (LONG)STATUS_NOT_IMPLEMENTED;
    return (LONG)fn(hKey, phNewKey, pbKeyObject, cbKeyObject, dwFlags);
}

extern "C" LONG WINAPI BCryptEncrypt(void* hKey, void* pbInput, ULONG cbInput, void* pPaddingInfo, void* pbIV,
                                     ULONG cbIV, void* pbOutput, ULONG cbOutput, ULONG* pcbResult, ULONG dwFlags) {
    CALL_GUARD(utils::get_now_ns());
    if (!LoadRealBcrypt()) return (LONG)STATUS_NOT_IMPLEMENTED;
    auto fn = (PFN_BCryptEncrypt)GetProcAddress(g_bcrypt_module, "BCryptEncrypt");
    if (fn == nullptr) return (LONG)STATUS_NOT_IMPLEMENTED;
    return (LONG)fn(hKey, pbInput, cbInput, pPaddingInfo, pbIV, cbIV, pbOutput, cbOutput, pcbResult, dwFlags);
}

extern "C" LONG WINAPI BCryptEnumAlgorithms(ULONG dwAlgOperations, ULONG* pAlgCount, void* ppAlgList, ULONG dwFlags) {
    CALL_GUARD(utils::get_now_ns());
    if (!LoadRealBcrypt()) return (LONG)STATUS_NOT_IMPLEMENTED;
    auto fn = (PFN_BCryptEnumAlgorithms)GetProcAddress(g_bcrypt_module, "BCryptEnumAlgorithms");
    if (fn == nullptr) return (LONG)STATUS_NOT_IMPLEMENTED;
    return (LONG)fn(dwAlgOperations, pAlgCount, ppAlgList, dwFlags);
}

extern "C" LONG WINAPI BCryptEnumContextFunctionProviders(ULONG dwTable, LPCWSTR pszContext, ULONG dwInterface,
                                                          LPCWSTR pszFunction, ULONG* pcbBuffer, void* ppBuffer) {
    CALL_GUARD(utils::get_now_ns());
    if (!LoadRealBcrypt()) return (LONG)STATUS_NOT_IMPLEMENTED;
    auto fn =
        (PFN_BCryptEnumContextFunctionProviders)GetProcAddress(g_bcrypt_module, "BCryptEnumContextFunctionProviders");
    if (fn == nullptr) return (LONG)STATUS_NOT_IMPLEMENTED;
    return (LONG)fn(dwTable, pszContext, dwInterface, pszFunction, pcbBuffer, ppBuffer);
}

extern "C" LONG WINAPI BCryptEnumContextFunctions(ULONG dwTable, LPCWSTR pszContext, ULONG dwInterface,
                                                  ULONG* pcbBuffer, void* ppBuffer) {
    CALL_GUARD(utils::get_now_ns());
    if (!LoadRealBcrypt()) return (LONG)STATUS_NOT_IMPLEMENTED;
    auto fn = (PFN_BCryptEnumContextFunctions)GetProcAddress(g_bcrypt_module, "BCryptEnumContextFunctions");
    if (fn == nullptr) return (LONG)STATUS_NOT_IMPLEMENTED;
    return (LONG)fn(dwTable, pszContext, dwInterface, pcbBuffer, ppBuffer);
}

extern "C" LONG WINAPI BCryptEnumContexts(ULONG dwTable, ULONG* pcbBuffer, void* ppBuffer) {
    CALL_GUARD(utils::get_now_ns());
    if (!LoadRealBcrypt()) return (LONG)STATUS_NOT_IMPLEMENTED;
    auto fn = (PFN_BCryptEnumContexts)GetProcAddress(g_bcrypt_module, "BCryptEnumContexts");
    if (fn == nullptr) return (LONG)STATUS_NOT_IMPLEMENTED;
    return (LONG)fn(dwTable, pcbBuffer, ppBuffer);
}

extern "C" LONG WINAPI BCryptEnumProviders(ULONG dwAlgOperations, ULONG* pImplCount, void* ppImplList, ULONG dwFlags) {
    CALL_GUARD(utils::get_now_ns());
    if (!LoadRealBcrypt()) return (LONG)STATUS_NOT_IMPLEMENTED;
    auto fn = (PFN_BCryptEnumProviders)GetProcAddress(g_bcrypt_module, "BCryptEnumProviders");
    if (fn == nullptr) return (LONG)STATUS_NOT_IMPLEMENTED;
    return (LONG)fn(dwAlgOperations, pImplCount, ppImplList, dwFlags);
}

extern "C" LONG WINAPI BCryptEnumRegisteredProviders(ULONG* pcbBuffer, void* ppBuffer) {
    CALL_GUARD(utils::get_now_ns());
    if (!LoadRealBcrypt()) return (LONG)STATUS_NOT_IMPLEMENTED;
    auto fn = (PFN_BCryptEnumRegisteredProviders)GetProcAddress(g_bcrypt_module, "BCryptEnumRegisteredProviders");
    if (fn == nullptr) return (LONG)STATUS_NOT_IMPLEMENTED;
    return (LONG)fn(pcbBuffer, ppBuffer);
}

extern "C" LONG WINAPI BCryptExportKey(void* hKey, void* hExportKey, LPCWSTR pszBlobType, void* pbOutput,
                                       ULONG cbOutput, ULONG* pcbResult, ULONG dwFlags) {
    CALL_GUARD(utils::get_now_ns());
    if (!LoadRealBcrypt()) return (LONG)STATUS_NOT_IMPLEMENTED;
    auto fn = (PFN_BCryptExportKey)GetProcAddress(g_bcrypt_module, "BCryptExportKey");
    if (fn == nullptr) return (LONG)STATUS_NOT_IMPLEMENTED;
    return (LONG)fn(hKey, hExportKey, pszBlobType, pbOutput, cbOutput, pcbResult, dwFlags);
}

extern "C" LONG WINAPI BCryptFinalizeKeyPair(void* hKey, ULONG dwFlags) {
    CALL_GUARD(utils::get_now_ns());
    if (!LoadRealBcrypt()) return (LONG)STATUS_NOT_IMPLEMENTED;
    auto fn = (PFN_BCryptFinalizeKeyPair)GetProcAddress(g_bcrypt_module, "BCryptFinalizeKeyPair");
    if (fn == nullptr) return (LONG)STATUS_NOT_IMPLEMENTED;
    return (LONG)fn(hKey, dwFlags);
}

extern "C" LONG WINAPI BCryptFinishHash(void* hHash, void* pbOutput, ULONG cbOutput, ULONG dwFlags) {
    CALL_GUARD(utils::get_now_ns());
    if (!LoadRealBcrypt()) return (LONG)STATUS_NOT_IMPLEMENTED;
    auto fn = (PFN_BCryptFinishHash)GetProcAddress(g_bcrypt_module, "BCryptFinishHash");
    if (fn == nullptr) return (LONG)STATUS_NOT_IMPLEMENTED;
    return (LONG)fn(hHash, pbOutput, cbOutput, dwFlags);
}

extern "C" void WINAPI BCryptFreeBuffer(void* pvBuffer) {
    CALL_GUARD(utils::get_now_ns());
    if (!LoadRealBcrypt()) return;
    auto fn = (PFN_BCryptFreeBuffer)GetProcAddress(g_bcrypt_module, "BCryptFreeBuffer");
    if (fn == nullptr) return;
    fn(pvBuffer);
}

extern "C" LONG WINAPI BCryptGenRandom(void* hAlgorithm, void* pbBuffer, ULONG cbBuffer, ULONG dwFlags) {
    CALL_GUARD(utils::get_now_ns());
    if (!LoadRealBcrypt()) return (LONG)STATUS_NOT_IMPLEMENTED;
    auto fn = (PFN_BCryptGenRandom)GetProcAddress(g_bcrypt_module, "BCryptGenRandom");
    if (fn == nullptr) return (LONG)STATUS_NOT_IMPLEMENTED;
    return (LONG)fn(hAlgorithm, pbBuffer, cbBuffer, dwFlags);
}

extern "C" LONG WINAPI BCryptGenerateKeyPair(void* hAlgorithm, void* phKey, ULONG dwLength, ULONG dwFlags) {
    CALL_GUARD(utils::get_now_ns());
    if (!LoadRealBcrypt()) return (LONG)STATUS_NOT_IMPLEMENTED;
    auto fn = (PFN_BCryptGenerateKeyPair)GetProcAddress(g_bcrypt_module, "BCryptGenerateKeyPair");
    if (fn == nullptr) return (LONG)STATUS_NOT_IMPLEMENTED;
    return (LONG)fn(hAlgorithm, phKey, dwLength, dwFlags);
}

extern "C" LONG WINAPI BCryptGenerateSymmetricKey(void* hAlgorithm, void* phKey, void* pbKeyObject, ULONG cbKeyObject,
                                                  void* pbSecret, ULONG cbSecret, ULONG dwFlags) {
    CALL_GUARD(utils::get_now_ns());
    if (!LoadRealBcrypt()) return (LONG)STATUS_NOT_IMPLEMENTED;
    auto fn = (PFN_BCryptGenerateSymmetricKey)GetProcAddress(g_bcrypt_module, "BCryptGenerateSymmetricKey");
    if (fn == nullptr) return (LONG)STATUS_NOT_IMPLEMENTED;
    return (LONG)fn(hAlgorithm, phKey, pbKeyObject, cbKeyObject, pbSecret, cbSecret, dwFlags);
}

extern "C" LONG WINAPI BCryptGetFipsAlgorithmMode(void* pfEnabled) {
    CALL_GUARD(utils::get_now_ns());
    if (!LoadRealBcrypt()) return (LONG)STATUS_NOT_IMPLEMENTED;
    auto fn = (PFN_BCryptGetFipsAlgorithmMode)GetProcAddress(g_bcrypt_module, "BCryptGetFipsAlgorithmMode");
    if (fn == nullptr) return (LONG)STATUS_NOT_IMPLEMENTED;
    return (LONG)fn(pfEnabled);
}

extern "C" LONG WINAPI BCryptGetProperty(void* hObject, LPCWSTR pszProperty, void* pbOutput, ULONG cbOutput,
                                         ULONG* pcbResult, ULONG dwFlags) {
    CALL_GUARD(utils::get_now_ns());
    if (!LoadRealBcrypt()) return (LONG)STATUS_NOT_IMPLEMENTED;
    auto fn = (PFN_BCryptGetProperty)GetProcAddress(g_bcrypt_module, "BCryptGetProperty");
    if (fn == nullptr) return (LONG)STATUS_NOT_IMPLEMENTED;
    return (LONG)fn(hObject, pszProperty, pbOutput, cbOutput, pcbResult, dwFlags);
}

extern "C" LONG WINAPI BCryptHash(void* hAlgorithm, void* pbSecret, ULONG cbSecret, void* pbInput, ULONG cbInput,
                                  void* pbOutput, ULONG cbOutput) {
    CALL_GUARD(utils::get_now_ns());
    if (!LoadRealBcrypt()) return (LONG)STATUS_NOT_IMPLEMENTED;
    auto fn = (PFN_BCryptHash)GetProcAddress(g_bcrypt_module, "BCryptHash");
    if (fn == nullptr) return (LONG)STATUS_NOT_IMPLEMENTED;
    return (LONG)fn(hAlgorithm, pbSecret, cbSecret, pbInput, cbInput, pbOutput, cbOutput);
}

extern "C" LONG WINAPI BCryptHashData(void* hHash, void* pbInput, ULONG cbInput, ULONG dwFlags) {
    CALL_GUARD(utils::get_now_ns());
    if (!LoadRealBcrypt()) return (LONG)STATUS_NOT_IMPLEMENTED;
    auto fn = (PFN_BCryptHashData)GetProcAddress(g_bcrypt_module, "BCryptHashData");
    if (fn == nullptr) return (LONG)STATUS_NOT_IMPLEMENTED;
    return (LONG)fn(hHash, pbInput, cbInput, dwFlags);
}

extern "C" LONG WINAPI BCryptImportKey(void* hAlgorithm, void* hImportKey, LPCWSTR pszBlobType, void* phKey,
                                       void* pbKeyObject, ULONG cbKeyObject, void* pbInput, ULONG cbInput,
                                       ULONG dwFlags) {
    CALL_GUARD(utils::get_now_ns());
    if (!LoadRealBcrypt()) return (LONG)STATUS_NOT_IMPLEMENTED;
    auto fn = (PFN_BCryptImportKey)GetProcAddress(g_bcrypt_module, "BCryptImportKey");
    if (fn == nullptr) return (LONG)STATUS_NOT_IMPLEMENTED;
    return (LONG)fn(hAlgorithm, hImportKey, pszBlobType, phKey, pbKeyObject, cbKeyObject, pbInput, cbInput, dwFlags);
}

extern "C" LONG WINAPI BCryptImportKeyPair(void* hAlgorithm, void* hImportKey, LPCWSTR pszBlobType, void* phKey,
                                           void* pbInput, ULONG cbInput, ULONG dwFlags) {
    CALL_GUARD(utils::get_now_ns());
    if (!LoadRealBcrypt()) return (LONG)STATUS_NOT_IMPLEMENTED;
    auto fn = (PFN_BCryptImportKeyPair)GetProcAddress(g_bcrypt_module, "BCryptImportKeyPair");
    if (fn == nullptr) return (LONG)STATUS_NOT_IMPLEMENTED;
    return (LONG)fn(hAlgorithm, hImportKey, pszBlobType, phKey, pbInput, cbInput, dwFlags);
}

extern "C" LONG WINAPI BCryptKeyDerivation(void* hKey, void* pParameterList, void* pbDerivedKey, ULONG cbDerivedKey,
                                           ULONG* pcbResult, ULONG dwFlags) {
    CALL_GUARD(utils::get_now_ns());
    if (!LoadRealBcrypt()) return (LONG)STATUS_NOT_IMPLEMENTED;
    auto fn = (PFN_BCryptKeyDerivation)GetProcAddress(g_bcrypt_module, "BCryptKeyDerivation");
    if (fn == nullptr) return (LONG)STATUS_NOT_IMPLEMENTED;
    return (LONG)fn(hKey, pParameterList, pbDerivedKey, cbDerivedKey, pcbResult, dwFlags);
}

extern "C" LONG WINAPI BCryptOpenAlgorithmProvider(void* phAlgorithm, LPCWSTR pszAlgId, LPCWSTR pszImplementation,
                                                   ULONG dwFlags) {
    CALL_GUARD(utils::get_now_ns());
    if (!LoadRealBcrypt()) return (LONG)STATUS_NOT_IMPLEMENTED;
    auto fn = (PFN_BCryptOpenAlgorithmProvider)GetProcAddress(g_bcrypt_module, "BCryptOpenAlgorithmProvider");
    if (fn == nullptr) return (LONG)STATUS_NOT_IMPLEMENTED;
    return (LONG)fn(phAlgorithm, pszAlgId, pszImplementation, dwFlags);
}

extern "C" LONG WINAPI BCryptQueryContextConfiguration(ULONG dwTable, LPCWSTR pszContext, ULONG* pcbBuffer,
                                                       void* ppBuffer) {
    CALL_GUARD(utils::get_now_ns());
    if (!LoadRealBcrypt()) return (LONG)STATUS_NOT_IMPLEMENTED;
    auto fn = (PFN_BCryptQueryContextConfiguration)GetProcAddress(g_bcrypt_module, "BCryptQueryContextConfiguration");
    if (fn == nullptr) return (LONG)STATUS_NOT_IMPLEMENTED;
    return (LONG)fn(dwTable, pszContext, pcbBuffer, ppBuffer);
}

extern "C" LONG WINAPI BCryptQueryContextFunctionConfiguration(ULONG dwTable, LPCWSTR pszContext, ULONG dwInterface,
                                                               ULONG* pcbBuffer, void* ppBuffer) {
    CALL_GUARD(utils::get_now_ns());
    if (!LoadRealBcrypt()) return (LONG)STATUS_NOT_IMPLEMENTED;
    auto fn = (PFN_BCryptQueryContextFunctionConfiguration)GetProcAddress(g_bcrypt_module,
                                                                          "BCryptQueryContextFunctionConfiguration");
    if (fn == nullptr) return (LONG)STATUS_NOT_IMPLEMENTED;
    return (LONG)fn(dwTable, pszContext, dwInterface, pcbBuffer, ppBuffer);
}

extern "C" LONG WINAPI BCryptQueryContextFunctionProperty(ULONG dwTable, LPCWSTR pszContext, ULONG dwInterface,
                                                          LPCWSTR pszFunction, LPCWSTR pszProperty, ULONG* pcbValue,
                                                          void* ppValue) {
    CALL_GUARD(utils::get_now_ns());
    if (!LoadRealBcrypt()) return (LONG)STATUS_NOT_IMPLEMENTED;
    auto fn =
        (PFN_BCryptQueryContextFunctionProperty)GetProcAddress(g_bcrypt_module, "BCryptQueryContextFunctionProperty");
    if (fn == nullptr) return (LONG)STATUS_NOT_IMPLEMENTED;
    return (LONG)fn(dwTable, pszContext, dwInterface, pszFunction, pszProperty, pcbValue, ppValue);
}

extern "C" LONG WINAPI BCryptQueryProviderRegistration(LPCWSTR pszProvider, ULONG dwMode, ULONG dwInterface,
                                                       ULONG* pcbBuffer, void* ppBuffer) {
    CALL_GUARD(utils::get_now_ns());
    if (!LoadRealBcrypt()) return (LONG)STATUS_NOT_IMPLEMENTED;
    auto fn = (PFN_BCryptQueryProviderRegistration)GetProcAddress(g_bcrypt_module, "BCryptQueryProviderRegistration");
    if (fn == nullptr) return (LONG)STATUS_NOT_IMPLEMENTED;
    return (LONG)fn(pszProvider, dwMode, dwInterface, pcbBuffer, ppBuffer);
}

extern "C" LONG WINAPI BCryptRegisterConfigChangeNotify(HANDLE* pEvent) {
    CALL_GUARD(utils::get_now_ns());
    if (!LoadRealBcrypt()) return (LONG)STATUS_NOT_IMPLEMENTED;
    auto fn = (PFN_BCryptRegisterConfigChangeNotify)GetProcAddress(g_bcrypt_module, "BCryptRegisterConfigChangeNotify");
    if (fn == nullptr) return (LONG)STATUS_NOT_IMPLEMENTED;
    return (LONG)fn(pEvent);
}

extern "C" LONG WINAPI BCryptRegisterProvider(LPCWSTR pszProvider, ULONG dwMode, void* pRegistration) {
    CALL_GUARD(utils::get_now_ns());
    if (!LoadRealBcrypt()) return (LONG)STATUS_NOT_IMPLEMENTED;
    auto fn = (PFN_BCryptRegisterProvider)GetProcAddress(g_bcrypt_module, "BCryptRegisterProvider");
    if (fn == nullptr) return (LONG)STATUS_NOT_IMPLEMENTED;
    return (LONG)fn(pszProvider, dwMode, pRegistration);
}

extern "C" LONG WINAPI BCryptRemoveContextFunction(ULONG dwTable, LPCWSTR pszContext, ULONG dwInterface,
                                                   LPCWSTR pszFunction) {
    CALL_GUARD(utils::get_now_ns());
    if (!LoadRealBcrypt()) return (LONG)STATUS_NOT_IMPLEMENTED;
    auto fn = (PFN_BCryptRemoveContextFunction)GetProcAddress(g_bcrypt_module, "BCryptRemoveContextFunction");
    if (fn == nullptr) return (LONG)STATUS_NOT_IMPLEMENTED;
    return (LONG)fn(dwTable, pszContext, dwInterface, pszFunction);
}

extern "C" LONG WINAPI BCryptRemoveContextFunctionProvider(ULONG dwTable, LPCWSTR pszContext, ULONG dwInterface,
                                                           LPCWSTR pszFunction, LPCWSTR pszProvider) {
    CALL_GUARD(utils::get_now_ns());
    if (!LoadRealBcrypt()) return (LONG)STATUS_NOT_IMPLEMENTED;
    auto fn =
        (PFN_BCryptRemoveContextFunctionProvider)GetProcAddress(g_bcrypt_module, "BCryptRemoveContextFunctionProvider");
    if (fn == nullptr) return (LONG)STATUS_NOT_IMPLEMENTED;
    return (LONG)fn(dwTable, pszContext, dwInterface, pszFunction, pszProvider);
}

extern "C" LONG WINAPI BCryptResolveProviders(LPCWSTR pszContext, ULONG dwInterface, LPCWSTR pszFunction,
                                              LPCWSTR pszProvider, ULONG dwMode, ULONG dwFlags, ULONG* pcbBuffer,
                                              void* ppBuffer) {
    CALL_GUARD(utils::get_now_ns());
    if (!LoadRealBcrypt()) return (LONG)STATUS_NOT_IMPLEMENTED;
    auto fn = (PFN_BCryptResolveProviders)GetProcAddress(g_bcrypt_module, "BCryptResolveProviders");
    if (fn == nullptr) return (LONG)STATUS_NOT_IMPLEMENTED;
    return (LONG)fn(pszContext, dwInterface, pszFunction, pszProvider, dwMode, dwFlags, pcbBuffer, ppBuffer);
}

extern "C" LONG WINAPI BCryptSecretAgreement(void* hPrivKey, void* hPubKey, void* phSecret, ULONG dwFlags) {
    CALL_GUARD(utils::get_now_ns());
    if (!LoadRealBcrypt()) return (LONG)STATUS_NOT_IMPLEMENTED;
    auto fn = (PFN_BCryptSecretAgreement)GetProcAddress(g_bcrypt_module, "BCryptSecretAgreement");
    if (fn == nullptr) return (LONG)STATUS_NOT_IMPLEMENTED;
    return (LONG)fn(hPrivKey, hPubKey, phSecret, dwFlags);
}

extern "C" LONG WINAPI BCryptSetAuditingInterface(void* pAuditingInterface) {
    CALL_GUARD(utils::get_now_ns());
    if (!LoadRealBcrypt()) return (LONG)STATUS_NOT_IMPLEMENTED;
    auto fn = (PFN_BCryptSetAuditingInterface)GetProcAddress(g_bcrypt_module, "BCryptSetAuditingInterface");
    if (fn == nullptr) return (LONG)STATUS_NOT_IMPLEMENTED;
    return (LONG)fn(pAuditingInterface);
}

extern "C" LONG WINAPI BCryptSetContextFunctionProperty(ULONG dwTable, LPCWSTR pszContext, ULONG dwInterface,
                                                        LPCWSTR pszFunction, LPCWSTR pszProperty, void* pbValue,
                                                        ULONG cbValue) {
    CALL_GUARD(utils::get_now_ns());
    if (!LoadRealBcrypt()) return (LONG)STATUS_NOT_IMPLEMENTED;
    auto fn = (PFN_BCryptSetContextFunctionProperty)GetProcAddress(g_bcrypt_module, "BCryptSetContextFunctionProperty");
    if (fn == nullptr) return (LONG)STATUS_NOT_IMPLEMENTED;
    return (LONG)fn(dwTable, pszContext, dwInterface, pszFunction, pszProperty, pbValue, cbValue);
}

extern "C" LONG WINAPI BCryptSetProperty(void* hObject, LPCWSTR pszProperty, void* pbInput, ULONG cbInput,
                                         ULONG dwFlags) {
    CALL_GUARD(utils::get_now_ns());
    if (!LoadRealBcrypt()) return (LONG)STATUS_NOT_IMPLEMENTED;
    auto fn = (PFN_BCryptSetProperty)GetProcAddress(g_bcrypt_module, "BCryptSetProperty");
    if (fn == nullptr) return (LONG)STATUS_NOT_IMPLEMENTED;
    return (LONG)fn(hObject, pszProperty, pbInput, cbInput, dwFlags);
}

extern "C" LONG WINAPI BCryptSignHash(void* hKey, void* pPaddingInfo, void* pbInput, ULONG cbInput, void* pbOutput,
                                      ULONG cbOutput, ULONG* pcbResult, ULONG dwFlags) {
    CALL_GUARD(utils::get_now_ns());
    if (!LoadRealBcrypt()) return (LONG)STATUS_NOT_IMPLEMENTED;
    auto fn = (PFN_BCryptSignHash)GetProcAddress(g_bcrypt_module, "BCryptSignHash");
    if (fn == nullptr) return (LONG)STATUS_NOT_IMPLEMENTED;
    return (LONG)fn(hKey, pPaddingInfo, pbInput, cbInput, pbOutput, cbOutput, pcbResult, dwFlags);
}

extern "C" LONG WINAPI BCryptUnregisterConfigChangeNotify(HANDLE hEvent) {
    CALL_GUARD(utils::get_now_ns());
    if (!LoadRealBcrypt()) return (LONG)STATUS_NOT_IMPLEMENTED;
    auto fn =
        (PFN_BCryptUnregisterConfigChangeNotify)GetProcAddress(g_bcrypt_module, "BCryptUnregisterConfigChangeNotify");
    if (fn == nullptr) return (LONG)STATUS_NOT_IMPLEMENTED;
    return (LONG)fn(hEvent);
}

extern "C" LONG WINAPI BCryptUnregisterProvider(LPCWSTR pszProvider) {
    CALL_GUARD(utils::get_now_ns());
    if (!LoadRealBcrypt()) return (LONG)STATUS_NOT_IMPLEMENTED;
    auto fn = (PFN_BCryptUnregisterProvider)GetProcAddress(g_bcrypt_module, "BCryptUnregisterProvider");
    if (fn == nullptr) return (LONG)STATUS_NOT_IMPLEMENTED;
    return (LONG)fn(pszProvider);
}

extern "C" LONG WINAPI BCryptVerifySignature(void* hKey, void* pPaddingInfo, void* pbHash, ULONG cbHash,
                                             void* pbSignature, ULONG cbSignature, ULONG dwFlags) {
    CALL_GUARD(utils::get_now_ns());
    if (!LoadRealBcrypt()) return (LONG)STATUS_NOT_IMPLEMENTED;
    auto fn = (PFN_BCryptVerifySignature)GetProcAddress(g_bcrypt_module, "BCryptVerifySignature");
    if (fn == nullptr) return (LONG)STATUS_NOT_IMPLEMENTED;
    return (LONG)fn(hKey, pPaddingInfo, pbHash, cbHash, pbSignature, cbSignature, dwFlags);
}

extern "C" void* WINAPI GetAsymmetricEncryptionInterface(void) {
    CALL_GUARD(utils::get_now_ns());
    if (!LoadRealBcrypt()) return nullptr;
    auto fn = (PFN_GetAsymmetricEncryptionInterface)GetProcAddress(g_bcrypt_module, "GetAsymmetricEncryptionInterface");
    if (fn == nullptr) return nullptr;
    return fn();
}

extern "C" void* WINAPI GetCipherInterface(void) {
    CALL_GUARD(utils::get_now_ns());
    if (!LoadRealBcrypt()) return nullptr;
    auto fn = (PFN_GetCipherInterface)GetProcAddress(g_bcrypt_module, "GetCipherInterface");
    if (fn == nullptr) return nullptr;
    return fn();
}

extern "C" void* WINAPI GetHashInterface(void) {
    CALL_GUARD(utils::get_now_ns());
    if (!LoadRealBcrypt()) return nullptr;
    auto fn = (PFN_GetHashInterface)GetProcAddress(g_bcrypt_module, "GetHashInterface");
    if (fn == nullptr) return nullptr;
    return fn();
}

extern "C" void* WINAPI GetRngInterface(void) {
    CALL_GUARD(utils::get_now_ns());
    if (!LoadRealBcrypt()) return nullptr;
    auto fn = (PFN_GetRngInterface)GetProcAddress(g_bcrypt_module, "GetRngInterface");
    if (fn == nullptr) return nullptr;
    return fn();
}

extern "C" void* WINAPI GetSecretAgreementInterface(void) {
    CALL_GUARD(utils::get_now_ns());
    if (!LoadRealBcrypt()) return nullptr;
    auto fn = (PFN_GetSecretAgreementInterface)GetProcAddress(g_bcrypt_module, "GetSecretAgreementInterface");
    if (fn == nullptr) return nullptr;
    return fn();
}

extern "C" void* WINAPI GetSignatureInterface(void) {
    CALL_GUARD(utils::get_now_ns());
    if (!LoadRealBcrypt()) return nullptr;
    auto fn = (PFN_GetSignatureInterface)GetProcAddress(g_bcrypt_module, "GetSignatureInterface");
    if (fn == nullptr) return nullptr;
    return fn();
}
