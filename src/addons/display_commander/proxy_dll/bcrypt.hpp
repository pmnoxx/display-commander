/*
 * Official BCrypt API signatures for proxy forwarding.
 * Do not include bcrypt.h here (conflict with extern "C" proxy symbols / C2733 on x86).
 * Opaque struct pointers (PCRYPT_*, BCRYPT_*_HANDLE) as void* for ABI compatibility.
 * References: https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/
 */
#ifndef DISPLAY_COMMANDER_BCRYPT_HPP
#define DISPLAY_COMMANDER_BCRYPT_HPP

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/* NTSTATUS = LONG in Windows API */
typedef LONG NTSTATUS;

/*
 * Function pointer types for forwarding to real bcrypt.dll.
 * Names match Microsoft bcrypt.h; parameter types use void* for opaque structs.
 */

typedef NTSTATUS (WINAPI *PFN_BCryptAddContextFunction)(ULONG dwTable, LPCWSTR pszContext, ULONG dwInterface, LPCWSTR pszFunction, ULONG dwPosition);
typedef NTSTATUS (WINAPI *PFN_BCryptAddContextFunctionProvider)(ULONG dwTable, LPCWSTR pszContext, ULONG dwInterface, LPCWSTR pszFunction, LPCWSTR pszProvider, ULONG dwPosition);
typedef NTSTATUS (WINAPI *PFN_BCryptCloseAlgorithmProvider)(void* hAlgorithm, ULONG dwFlags);
typedef NTSTATUS (WINAPI *PFN_BCryptConfigureContext)(ULONG dwTable, LPCWSTR pszContext, void* pConfig);
typedef NTSTATUS (WINAPI *PFN_BCryptConfigureContextFunction)(ULONG dwTable, LPCWSTR pszContext, ULONG dwInterface, LPCWSTR pszFunction, void* pConfig);
typedef NTSTATUS (WINAPI *PFN_BCryptCreateContext)(ULONG dwTable, LPCWSTR pszContext, void* pConfig);
typedef NTSTATUS (WINAPI *PFN_BCryptCreateHash)(void* hAlgorithm, void* phHash, void* pbHashObject, ULONG cbHashObject, void* pbSecret, ULONG cbSecret, ULONG dwFlags);
typedef NTSTATUS (WINAPI *PFN_BCryptDecrypt)(void* hKey, void* pbInput, ULONG cbInput, void* pPaddingInfo, void* pbIV, ULONG cbIV, void* pbOutput, ULONG cbOutput, ULONG* pcbResult, ULONG dwFlags);
typedef NTSTATUS (WINAPI *PFN_BCryptDeleteContext)(ULONG dwTable, LPCWSTR pszContext);
typedef NTSTATUS (WINAPI *PFN_BCryptDeriveKey)(void* hSecret, LPCWSTR pwszKdf, void* pParameterList, void* pbDerivedKey, ULONG cbDerivedKey, ULONG* pcbResult, ULONG dwFlags);
typedef NTSTATUS (WINAPI *PFN_BCryptDeriveKeyCapi)(void* hHash, void* hAlg, void* pbDerivedKey, ULONG cbDerivedKey, ULONG dwFlags);
typedef NTSTATUS (WINAPI *PFN_BCryptDeriveKeyPBKDF2)(void* hPrf, void* pbPassword, ULONG cbPassword, void* pbSalt, ULONG cbSalt, ULONGLONG cIterations, void* pbDerivedKey, ULONG cbDerivedKey, ULONG dwFlags);
typedef NTSTATUS (WINAPI *PFN_BCryptDestroyHash)(void* hHash);
typedef NTSTATUS (WINAPI *PFN_BCryptDestroyKey)(void* hKey);
typedef NTSTATUS (WINAPI *PFN_BCryptDestroySecret)(void* hSecret);
typedef NTSTATUS (WINAPI *PFN_BCryptDuplicateHash)(void* hHash, void* phNewHash, void* pbHashObject, ULONG cbHashObject, ULONG dwFlags);
typedef NTSTATUS (WINAPI *PFN_BCryptDuplicateKey)(void* hKey, void* phNewKey, void* pbKeyObject, ULONG cbKeyObject, ULONG dwFlags);
typedef NTSTATUS (WINAPI *PFN_BCryptEncrypt)(void* hKey, void* pbInput, ULONG cbInput, void* pPaddingInfo, void* pbIV, ULONG cbIV, void* pbOutput, ULONG cbOutput, ULONG* pcbResult, ULONG dwFlags);
typedef NTSTATUS (WINAPI *PFN_BCryptEnumAlgorithms)(ULONG dwAlgOperations, ULONG* pAlgCount, void* ppAlgList, ULONG dwFlags);
typedef NTSTATUS (WINAPI *PFN_BCryptEnumContextFunctionProviders)(ULONG dwTable, LPCWSTR pszContext, ULONG dwInterface, LPCWSTR pszFunction, ULONG* pcbBuffer, void* ppBuffer);
typedef NTSTATUS (WINAPI *PFN_BCryptEnumContextFunctions)(ULONG dwTable, LPCWSTR pszContext, ULONG dwInterface, ULONG* pcbBuffer, void* ppBuffer);
typedef NTSTATUS (WINAPI *PFN_BCryptEnumContexts)(ULONG dwTable, ULONG* pcbBuffer, void* ppBuffer);
typedef NTSTATUS (WINAPI *PFN_BCryptEnumProviders)(ULONG dwAlgOperations, ULONG* pImplCount, void* ppImplList, ULONG dwFlags);
typedef NTSTATUS (WINAPI *PFN_BCryptEnumRegisteredProviders)(ULONG* pcbBuffer, void* ppBuffer);
typedef NTSTATUS (WINAPI *PFN_BCryptExportKey)(void* hKey, void* hExportKey, LPCWSTR pszBlobType, void* pParameterList, void* pbOutput, ULONG cbOutput, ULONG* pcbResult, ULONG dwFlags);
typedef NTSTATUS (WINAPI *PFN_BCryptFinalizeKeyPair)(void* hKey, ULONG dwFlags);
typedef NTSTATUS (WINAPI *PFN_BCryptFinishHash)(void* hHash, void* pbOutput, ULONG cbOutput, ULONG dwFlags);
typedef void (WINAPI *PFN_BCryptFreeBuffer)(void* pvBuffer);
typedef NTSTATUS (WINAPI *PFN_BCryptGenRandom)(void* hAlgorithm, void* pbBuffer, ULONG cbBuffer, ULONG dwFlags);
typedef NTSTATUS (WINAPI *PFN_BCryptGenerateKeyPair)(void* hAlgorithm, void* phKey, ULONG dwLength, ULONG dwFlags);
typedef NTSTATUS (WINAPI *PFN_BCryptGenerateSymmetricKey)(void* hAlgorithm, void* phKey, void* pbKeyObject, ULONG cbKeyObject, void* pbSecret, ULONG cbSecret, ULONG dwFlags);
typedef NTSTATUS (WINAPI *PFN_BCryptGetFipsAlgorithmMode)(void* pfEnabled);
typedef NTSTATUS (WINAPI *PFN_BCryptGetProperty)(void* hObject, LPCWSTR pszProperty, void* pbOutput, ULONG cbOutput, ULONG* pcbResult, ULONG dwFlags);
typedef NTSTATUS (WINAPI *PFN_BCryptHash)(void* hAlgorithm, void* pbInput, ULONG cbInput, void* pbHashObject, ULONG cbHashObject, void* pbOutput, ULONG cbOutput, ULONG dwFlags);
typedef NTSTATUS (WINAPI *PFN_BCryptHashData)(void* hHash, void* pbInput, ULONG cbInput, ULONG dwFlags);
typedef NTSTATUS (WINAPI *PFN_BCryptImportKey)(void* hAlgorithm, void* hImportKey, LPCWSTR pszBlobType, void* phKey, void* pbKeyObject, ULONG cbKeyObject, void* pbInput, ULONG cbInput, ULONG dwFlags);
typedef NTSTATUS (WINAPI *PFN_BCryptImportKeyPair)(void* hAlgorithm, void* hImportKey, LPCWSTR pszBlobType, void* phKey, void* pbInput, ULONG cbInput, ULONG dwFlags);
typedef NTSTATUS (WINAPI *PFN_BCryptKeyDerivation)(void* hKey, void* pParameterList, void* pbDerivedKey, ULONG cbDerivedKey, ULONG* pcbResult, ULONG dwFlags);
typedef NTSTATUS (WINAPI *PFN_BCryptOpenAlgorithmProvider)(void* phAlgorithm, LPCWSTR pszAlgId, LPCWSTR pszImplementation, ULONG dwFlags);
typedef NTSTATUS (WINAPI *PFN_BCryptQueryContextConfiguration)(ULONG dwTable, LPCWSTR pszContext, ULONG* pcbBuffer, void* ppBuffer);
typedef NTSTATUS (WINAPI *PFN_BCryptQueryContextFunctionConfiguration)(ULONG dwTable, LPCWSTR pszContext, ULONG dwInterface, ULONG* pcbBuffer, void* ppBuffer);
typedef NTSTATUS (WINAPI *PFN_BCryptQueryContextFunctionProperty)(ULONG dwTable, LPCWSTR pszContext, ULONG dwInterface, LPCWSTR pszFunction, LPCWSTR pszProperty, ULONG* pcbValue, void* ppValue);
typedef NTSTATUS (WINAPI *PFN_BCryptQueryProviderRegistration)(LPCWSTR pszProvider, ULONG dwMode, ULONG dwInterface, ULONG* pcbBuffer, void* ppBuffer);
typedef NTSTATUS (WINAPI *PFN_BCryptRegisterConfigChangeNotify)(HANDLE* pEvent);
typedef NTSTATUS (WINAPI *PFN_BCryptRegisterProvider)(LPCWSTR pszProvider, ULONG dwMode, void* pRegistration);
typedef NTSTATUS (WINAPI *PFN_BCryptRemoveContextFunction)(ULONG dwTable, LPCWSTR pszContext, ULONG dwInterface, LPCWSTR pszFunction);
typedef NTSTATUS (WINAPI *PFN_BCryptRemoveContextFunctionProvider)(ULONG dwTable, LPCWSTR pszContext, ULONG dwInterface, LPCWSTR pszFunction, LPCWSTR pszProvider);
typedef NTSTATUS (WINAPI *PFN_BCryptResolveProviders)(LPCWSTR pszContext, ULONG dwInterface, LPCWSTR pszFunction, LPCWSTR pszProvider, ULONG* pcbBuffer, void* ppBuffer);
typedef NTSTATUS (WINAPI *PFN_BCryptSecretAgreement)(void* hPrivKey, void* hPubKey, void* phSecret, ULONG dwFlags);
typedef NTSTATUS (WINAPI *PFN_BCryptSetAuditingInterface)(void* pAuditingInterface);
typedef NTSTATUS (WINAPI *PFN_BCryptSetContextFunctionProperty)(ULONG dwTable, LPCWSTR pszContext, ULONG dwInterface, LPCWSTR pszFunction, LPCWSTR pszProperty, void* pbValue, ULONG cbValue);
typedef NTSTATUS (WINAPI *PFN_BCryptSetProperty)(void* hObject, LPCWSTR pszProperty, void* pbInput, ULONG cbInput, ULONG dwFlags);
typedef NTSTATUS (WINAPI *PFN_BCryptSignHash)(void* hKey, void* pPaddingInfo, void* pbInput, ULONG cbInput, void* pbOutput, ULONG cbOutput, ULONG* pcbResult, ULONG dwFlags);
typedef NTSTATUS (WINAPI *PFN_BCryptUnregisterConfigChangeNotify)(HANDLE hEvent);
typedef NTSTATUS (WINAPI *PFN_BCryptUnregisterProvider)(LPCWSTR pszProvider);
typedef NTSTATUS (WINAPI *PFN_BCryptVerifySignature)(void* hKey, void* pPaddingInfo, void* pbHash, ULONG cbHash, void* pbSignature, ULONG cbSignature, ULONG dwFlags);

/* Undocumented Get*Interface exports; return interface pointer (void*). */
typedef void* (WINAPI *PFN_GetAsymmetricEncryptionInterface)(void);
typedef void* (WINAPI *PFN_GetCipherInterface)(void);
typedef void* (WINAPI *PFN_GetHashInterface)(void);
typedef void* (WINAPI *PFN_GetRngInterface)(void);
typedef void* (WINAPI *PFN_GetSecretAgreementInterface)(void);
typedef void* (WINAPI *PFN_GetSignatureInterface)(void);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_COMMANDER_BCRYPT_HPP */
