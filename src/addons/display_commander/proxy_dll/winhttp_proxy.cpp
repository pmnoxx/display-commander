/*
 * winhttp.dll proxy. Forwards all WinHTTP API calls to the system winhttp.dll.
 * Signatures from winhttp_proxy.hpp (official winhttp.h).
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

// Source Code <Display Commander>
#include "winhttp_proxy.hpp"

// Libraries <standard C++>
#include <string>

static HMODULE g_winhttp_module = nullptr;

static bool LoadRealWinhttp() {
    if (g_winhttp_module != nullptr) return true;
    WCHAR system_path[MAX_PATH];
    if (GetSystemDirectoryW(system_path, MAX_PATH) == 0) return false;
    std::wstring path = std::wstring(system_path) + L"\\winhttp.dll";
    g_winhttp_module = LoadLibraryW(path.c_str());
    return g_winhttp_module != nullptr;
}

#define WINHTTP_FWD_BOOL(name) \
    auto fn = (PFN_##name)(LoadRealWinhttp() ? GetProcAddress(g_winhttp_module, #name) : nullptr); \
    if (!fn) return FALSE; \
    return fn

extern "C" BOOL WINAPI WinHttpAddRequestHeaders(void* hRequest, LPCWSTR pwszHeaders, DWORD dwHeadersLength,
                                                DWORD dwModifiers) {
    WINHTTP_FWD_BOOL(WinHttpAddRequestHeaders)(hRequest, pwszHeaders, dwHeadersLength, dwModifiers);
}
#undef WINHTTP_FWD_BOOL

#define WINHTTP_FWD(name) \
    auto fn = (PFN_##name)(LoadRealWinhttp() ? GetProcAddress(g_winhttp_module, #name) : nullptr); \
    if (!fn) return FALSE; \
    return fn

/* 1-20 */
extern "C" DWORD WINAPI WinHttpAddRequestHeadersEx(void* hRequest, DWORD dwModifiers, ULONGLONG ullFlags,
                                                   ULONGLONG ullExtra, DWORD cHeaders, void* pHeaders) {
    auto fn = (PFN_WinHttpAddRequestHeadersEx)(LoadRealWinhttp() ? GetProcAddress(g_winhttp_module, "WinHttpAddRequestHeadersEx") : nullptr);
    if (!fn) return (DWORD)-1;
    return fn(hRequest, dwModifiers, ullFlags, ullExtra, cHeaders, pHeaders);
}
extern "C" BOOL WINAPI WinHttpCheckPlatform(void) {
    WINHTTP_FWD(WinHttpCheckPlatform)();
}
extern "C" BOOL WINAPI WinHttpCloseHandle(void* hInternet) {
    WINHTTP_FWD(WinHttpCloseHandle)(hInternet);
}
extern "C" BOOL WINAPI WinHttpCrackUrl(LPCWSTR pszUrl, DWORD dwUrlLength, DWORD dwFlags, void* lpUrlComponents) {
    WINHTTP_FWD(WinHttpCrackUrl)(pszUrl, dwUrlLength, dwFlags, lpUrlComponents);
}
extern "C" DWORD WINAPI WinHttpCreateProxyResolver(void* hSession, void** phResolver) {
    auto fn = (PFN_WinHttpCreateProxyResolver)(LoadRealWinhttp() ? GetProcAddress(g_winhttp_module, "WinHttpCreateProxyResolver") : nullptr);
    if (!fn) return (DWORD)-1;
    return fn(hSession, phResolver);
}
extern "C" BOOL WINAPI WinHttpCreateUrl(void* lpUrlComponents, DWORD dwFlags, LPWSTR pwszUrl, LPDWORD pdwUrlLength) {
    WINHTTP_FWD(WinHttpCreateUrl)(lpUrlComponents, dwFlags, pwszUrl, pdwUrlLength);
}
extern "C" BOOL WINAPI WinHttpDetectAutoProxyConfigUrl(DWORD dwAutoDetectFlags, LPWSTR* ppwszAutoConfigUrl) {
    WINHTTP_FWD(WinHttpDetectAutoProxyConfigUrl)(dwAutoDetectFlags, ppwszAutoConfigUrl);
}
extern "C" BOOL WINAPI WinHttpGetDefaultProxyConfiguration(void* pProxyInfo) {
    WINHTTP_FWD(WinHttpGetDefaultProxyConfiguration)(pProxyInfo);
}
extern "C" BOOL WINAPI WinHttpGetIEProxyConfigForCurrentUser(void* pProxyConfig) {
    WINHTTP_FWD(WinHttpGetIEProxyConfigForCurrentUser)(pProxyConfig);
}
extern "C" BOOL WINAPI WinHttpGetProxyForUrl(void* hSession, LPCWSTR pswzUrl, void* pAutoProxyOptions,
                                            void* pProxyInfo) {
    WINHTTP_FWD(WinHttpGetProxyForUrl)(hSession, pswzUrl, pAutoProxyOptions, pProxyInfo);
}
/* 21-40 */
extern "C" BOOL WINAPI WinHttpQueryAuthSchemes(void* hRequest, LPDWORD lpdwSupportedSchemes,
                                              LPDWORD lpdwFirstScheme, LPDWORD pdwAuthTarget) {
    WINHTTP_FWD(WinHttpQueryAuthSchemes)(hRequest, lpdwSupportedSchemes, lpdwFirstScheme, pdwAuthTarget);
}
extern "C" BOOL WINAPI WinHttpQueryDataAvailable(void* hRequest, LPDWORD lpdwNumberOfBytesAvailable) {
    WINHTTP_FWD(WinHttpQueryDataAvailable)(hRequest, lpdwNumberOfBytesAvailable);
}
extern "C" BOOL WINAPI WinHttpQueryHeaders(void* hRequest, DWORD dwInfoLevel, LPCWSTR pwszName, LPVOID lpBuffer,
                                          LPDWORD lpdwBufferLength, LPDWORD lpdwIndex) {
    WINHTTP_FWD(WinHttpQueryHeaders)(hRequest, dwInfoLevel, pwszName, lpBuffer, lpdwBufferLength, lpdwIndex);
}
extern "C" DWORD WINAPI WinHttpQueryHeadersEx(void* hRequest, DWORD dwInfoLevel, ULONGLONG ullFlags, UINT uiCodePage,
                                             DWORD* pdwIndex, void* pHeaderName, LPVOID pBuffer,
                                             LPDWORD pdwBufferLength, void** ppHeaders, LPDWORD pdwHeadersCount) {
    auto fn = (PFN_WinHttpQueryHeadersEx)(LoadRealWinhttp() ? GetProcAddress(g_winhttp_module, "WinHttpQueryHeadersEx") : nullptr);
    if (!fn) return (DWORD)-1;
    return fn(hRequest, dwInfoLevel, ullFlags, uiCodePage, pdwIndex, pHeaderName, pBuffer, pdwBufferLength,
              ppHeaders, pdwHeadersCount);
}
extern "C" BOOL WINAPI WinHttpQueryOption(void* hInternet, DWORD dwOption, LPVOID lpBuffer,
                                          LPDWORD lpdwBufferLength) {
    WINHTTP_FWD(WinHttpQueryOption)(hInternet, dwOption, lpBuffer, lpdwBufferLength);
}
extern "C" BOOL WINAPI WinHttpReadData(void* hRequest, LPVOID lpBuffer, DWORD dwNumberOfBytesToRead,
                                      LPDWORD lpdwNumberOfBytesRead) {
    WINHTTP_FWD(WinHttpReadData)(hRequest, lpBuffer, dwNumberOfBytesToRead, lpdwNumberOfBytesRead);
}
extern "C" DWORD WINAPI WinHttpReadDataEx(void* hRequest, LPVOID lpBuffer, DWORD dwNumberOfBytesToRead,
                                         LPDWORD lpdwNumberOfBytesRead, ULONGLONG ullFlags, DWORD cbProperty,
                                         void* pvProperty) {
    auto fn = (PFN_WinHttpReadDataEx)(LoadRealWinhttp() ? GetProcAddress(g_winhttp_module, "WinHttpReadDataEx") : nullptr);
    if (!fn) return (DWORD)-1;
    return fn(hRequest, lpBuffer, dwNumberOfBytesToRead, lpdwNumberOfBytesRead, ullFlags, cbProperty, pvProperty);
}
extern "C" BOOL WINAPI WinHttpReceiveResponse(void* hRequest, LPVOID lpReserved) {
    WINHTTP_FWD(WinHttpReceiveResponse)(hRequest, lpReserved);
}
extern "C" DWORD WINAPI WinHttpRegisterProxyChangeNotification(ULONGLONG ullFlags, void* pfnCallback, void* pvContext,
                                                              void** phRegistration) {
    auto fn = (PFN_WinHttpRegisterProxyChangeNotification)(LoadRealWinhttp() ? GetProcAddress(g_winhttp_module, "WinHttpRegisterProxyChangeNotification") : nullptr);
    if (!fn) return (DWORD)-1;
    return fn(ullFlags, pfnCallback, pvContext, phRegistration);
}
extern "C" BOOL WINAPI WinHttpResetAutoProxy(void* hSession, DWORD dwFlags) {
    WINHTTP_FWD(WinHttpResetAutoProxy)(hSession, dwFlags);
}
extern "C" BOOL WINAPI WinHttpSendRequest(void* hRequest, LPCWSTR lpszHeaders, DWORD dwHeadersLength,
                                         LPVOID lpOptional, DWORD dwOptionalLength, DWORD dwTotalLength,
                                         DWORD_PTR dwContext) {
    WINHTTP_FWD(WinHttpSendRequest)(hRequest, lpszHeaders, dwHeadersLength, lpOptional, dwOptionalLength,
                                    dwTotalLength, dwContext);
}
extern "C" BOOL WINAPI WinHttpSetCredentials(void* hRequest, DWORD dwAuthTargets, DWORD dwAuthScheme,
                                            LPCWSTR pwszUserName, LPCWSTR pwszPassword, LPVOID pAuthParams) {
    WINHTTP_FWD(WinHttpSetCredentials)(hRequest, dwAuthTargets, dwAuthScheme, pwszUserName, pwszPassword,
                                       pAuthParams);
}
extern "C" BOOL WINAPI WinHttpSetDefaultProxyConfiguration(void* pProxyInfo) {
    WINHTTP_FWD(WinHttpSetDefaultProxyConfiguration)(pProxyInfo);
}
extern "C" BOOL WINAPI WinHttpSetOption(void* hInternet, DWORD dwOption, LPVOID lpBuffer, DWORD dwBufferLength) {
    WINHTTP_FWD(WinHttpSetOption)(hInternet, dwOption, lpBuffer, dwBufferLength);
}
extern "C" BOOL WINAPI WinHttpSetTimeouts(void* hInternet, int nResolveTimeout, int nConnectTimeout,
                                         int nSendTimeout, int nReceiveTimeout) {
    WINHTTP_FWD(WinHttpSetTimeouts)(hInternet, nResolveTimeout, nConnectTimeout, nSendTimeout, nReceiveTimeout);
}
extern "C" BOOL WINAPI WinHttpTimeFromSystemTime(const SYSTEMTIME* pst, LPWSTR pwszTime, DWORD cchTime) {
    WINHTTP_FWD(WinHttpTimeFromSystemTime)(pst, pwszTime, cchTime);
}
extern "C" BOOL WINAPI WinHttpTimeToSystemTime(LPCWSTR pwszTime, SYSTEMTIME* pst) {
    WINHTTP_FWD(WinHttpTimeToSystemTime)(pwszTime, pst);
}
/* 41-48 */
extern "C" DWORD WINAPI WinHttpUnregisterProxyChangeNotification(void* hRegistration) {
    auto fn = (PFN_WinHttpUnregisterProxyChangeNotification)(LoadRealWinhttp() ? GetProcAddress(g_winhttp_module, "WinHttpUnregisterProxyChangeNotification") : nullptr);
    if (!fn) return (DWORD)-1;
    return fn(hRegistration);
}
extern "C" BOOL WINAPI WinHttpWriteData(void* hRequest, LPCVOID lpBuffer, DWORD dwNumberOfBytesToWrite,
                                       LPDWORD lpdwNumberOfBytesWritten) {
    WINHTTP_FWD(WinHttpWriteData)(hRequest, lpBuffer, dwNumberOfBytesToWrite, lpdwNumberOfBytesWritten);
}

#undef WINHTTP_FWD

extern "C" void* WINAPI WinHttpConnect(void* hSession, LPCWSTR pswzServerName, INTERNET_PORT nServerPort,
                                      DWORD dwReserved) {
    auto fn = (PFN_WinHttpConnect)(LoadRealWinhttp() ? GetProcAddress(g_winhttp_module, "WinHttpConnect") : nullptr);
    return fn ? fn(hSession, pswzServerName, nServerPort, dwReserved) : nullptr;
}
extern "C" void* WINAPI WinHttpOpen(LPCWSTR pszAgentW, DWORD dwAccessType, LPCWSTR pszProxyW,
                                    LPCWSTR pszProxyBypassW, DWORD dwFlags) {
    auto fn = (PFN_WinHttpOpen)(LoadRealWinhttp() ? GetProcAddress(g_winhttp_module, "WinHttpOpen") : nullptr);
    return fn ? fn(pszAgentW, dwAccessType, pszProxyW, pszProxyBypassW, dwFlags) : nullptr;
}
extern "C" void* WINAPI WinHttpOpenRequest(void* hConnect, LPCWSTR pwszVerb, LPCWSTR pwszObjectName,
                                           LPCWSTR pwszVersion, LPCWSTR pwszReferrer, LPCWSTR* ppwszAcceptTypes,
                                           DWORD dwFlags) {
    auto fn =
        (PFN_WinHttpOpenRequest)(LoadRealWinhttp() ? GetProcAddress(g_winhttp_module, "WinHttpOpenRequest") : nullptr);
    return fn ? fn(hConnect, pwszVerb, pwszObjectName, pwszVersion, pwszReferrer, ppwszAcceptTypes, dwFlags) : nullptr;
}
extern "C" void* WINAPI WinHttpWebSocketCompleteUpgrade(void* hRequest, DWORD_PTR dwContext) {
    auto fn = (PFN_WinHttpWebSocketCompleteUpgrade)(LoadRealWinhttp()
                                                       ? GetProcAddress(g_winhttp_module, "WinHttpWebSocketCompleteUpgrade")
                                                       : nullptr);
    return fn ? fn(hRequest, dwContext) : nullptr;
}

extern "C" void WINAPI WinHttpFreeProxyResult(void* pProxyResult) {
    auto fn =
        (PFN_WinHttpFreeProxyResult)(LoadRealWinhttp() ? GetProcAddress(g_winhttp_module, "WinHttpFreeProxyResult") : nullptr);
    if (fn) fn(pProxyResult);
}
extern "C" void WINAPI WinHttpFreeProxySettingsEx(void* pProxySettingsEx) {
    auto fn = (PFN_WinHttpFreeProxySettingsEx)(LoadRealWinhttp()
                                                   ? GetProcAddress(g_winhttp_module, "WinHttpFreeProxySettingsEx")
                                                   : nullptr);
    if (fn) fn(pProxySettingsEx);
}
extern "C" void WINAPI WinHttpFreeQueryConnectionGroupResult(void* pConnectionGroupResult) {
    auto fn = (PFN_WinHttpFreeQueryConnectionGroupResult)(
        LoadRealWinhttp() ? GetProcAddress(g_winhttp_module, "WinHttpFreeQueryConnectionGroupResult") : nullptr);
    if (fn) fn(pConnectionGroupResult);
}

extern "C" WINHTTP_STATUS_CALLBACK_OPAQUE WINAPI WinHttpSetStatusCallback(
    void* hInternet, WINHTTP_STATUS_CALLBACK_OPAQUE lpfnInternetCallback, DWORD dwNotificationFlags,
    DWORD_PTR dwReserved) {
    auto fn = (PFN_WinHttpSetStatusCallback)(
        LoadRealWinhttp() ? GetProcAddress(g_winhttp_module, "WinHttpSetStatusCallback") : nullptr);
    return fn ? fn(hInternet, lpfnInternetCallback, dwNotificationFlags, dwReserved) : nullptr;
}

#define WINHTTP_DWORD_FWD(name) \
    auto fn = (PFN_##name)(LoadRealWinhttp() ? GetProcAddress(g_winhttp_module, #name) : nullptr); \
    if (!fn) return (DWORD)-1; \
    return fn

extern "C" DWORD WINAPI WinHttpGetProxyForUrlEx(void* hResolver, LPCWSTR pcwszUrl, void* pAutoProxyOptions,
                                                DWORD_PTR pContext) {
    WINHTTP_DWORD_FWD(WinHttpGetProxyForUrlEx)(hResolver, pcwszUrl, pAutoProxyOptions, pContext);
}
extern "C" DWORD WINAPI WinHttpGetProxyResult(void* hResolver, void* pProxyResult) {
    WINHTTP_DWORD_FWD(WinHttpGetProxyResult)(hResolver, pProxyResult);
}
extern "C" DWORD WINAPI WinHttpGetProxySettingsEx(void* hSession, void* pProxySettingsEx) {
    WINHTTP_DWORD_FWD(WinHttpGetProxySettingsEx)(hSession, pProxySettingsEx);
}
extern "C" DWORD WINAPI WinHttpGetProxySettingsResultEx(void* hResolver, void* pProxySettingsResultEx) {
    WINHTTP_DWORD_FWD(WinHttpGetProxySettingsResultEx)(hResolver, pProxySettingsResultEx);
}
extern "C" DWORD WINAPI WinHttpQueryConnectionGroup(void* hInternet, const void* pGuidConnection,
                                                   ULONGLONG ullFlags, void** ppResult) {
    WINHTTP_DWORD_FWD(WinHttpQueryConnectionGroup)(hInternet, pGuidConnection, ullFlags, ppResult);
}
extern "C" DWORD WINAPI WinHttpWebSocketClose(void* hWebSocket, USHORT usStatus, LPVOID pvReason,
                                             DWORD dwReasonLength) {
    WINHTTP_DWORD_FWD(WinHttpWebSocketClose)(hWebSocket, usStatus, pvReason, dwReasonLength);
}
extern "C" DWORD WINAPI WinHttpWebSocketQueryCloseStatus(void* hWebSocket, USHORT* pusStatus, LPVOID pvReason,
                                                         DWORD dwReasonLength, LPDWORD pdwReasonLengthConsumed) {
    WINHTTP_DWORD_FWD(WinHttpWebSocketQueryCloseStatus)(hWebSocket, pusStatus, pvReason, dwReasonLength,
                                                        pdwReasonLengthConsumed);
}
extern "C" DWORD WINAPI WinHttpWebSocketReceive(void* hWebSocket, LPVOID pvBuffer, DWORD dwBufferLength,
                                                DWORD* pdwBytesRead, void* peBufferType) {
    WINHTTP_DWORD_FWD(WinHttpWebSocketReceive)(hWebSocket, pvBuffer, dwBufferLength, pdwBytesRead, peBufferType);
}
extern "C" DWORD WINAPI WinHttpWebSocketSend(void* hWebSocket, DWORD eBufferType, LPVOID pvBuffer,
                                            DWORD dwBufferLength) {
    WINHTTP_DWORD_FWD(WinHttpWebSocketSend)(hWebSocket, eBufferType, pvBuffer, dwBufferLength);
}
extern "C" DWORD WINAPI WinHttpWebSocketShutdown(void* hWebSocket, USHORT usStatus, LPVOID pvReason,
                                                DWORD dwReasonLength) {
    WINHTTP_DWORD_FWD(WinHttpWebSocketShutdown)(hWebSocket, usStatus, pvReason, dwReasonLength);
}

#undef WINHTTP_DWORD_FWD
