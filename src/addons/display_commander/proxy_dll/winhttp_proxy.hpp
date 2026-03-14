/*
 * Official WinHTTP API signatures for proxy forwarding.
 * Do not include winhttp.h here (conflict with extern "C" proxy symbols).
 * Opaque handles (HINTERNET) and WinHTTP-specific struct pointers as void* for ABI compatibility.
 * References: https://learn.microsoft.com/en-us/windows/win32/api/winhttp/ , winhttp.h
 */
#ifndef DISPLAY_COMMANDER_WINHTTP_PROXY_HPP
#define DISPLAY_COMMANDER_WINHTTP_PROXY_HPP

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handle; matches HINTERNET in winhttp.h. */
typedef void* HINTERNET_OPAQUE;

/* INTERNET_PORT as in winhttp.h (unsigned short). */
typedef WORD INTERNET_PORT;

/* Status callback: void CALLBACK (HINTERNET hInternet, DWORD_PTR dwContext, DWORD dwInternetStatus,
 * LPVOID lpvStatusInformation, DWORD dwStatusInformationLength). */
typedef void (CALLBACK* WINHTTP_STATUS_CALLBACK_OPAQUE)(void* hInternet, DWORD_PTR dwContext,
                                                       DWORD dwInternetStatus, LPVOID lpvStatusInformation,
                                                       DWORD dwStatusInformationLength);

/*
 * Function pointer types for forwarding to real winhttp.dll.
 * Names match Microsoft winhttp.h; parameter types use void* for opaque handles and WinHTTP structs.
 * Grouped in blocks of 20 for easier cross-reference with winhttp_proxy.cpp.
 */

/* 1-20 */
typedef BOOL (WINAPI* PFN_WinHttpAddRequestHeaders)(void* hRequest, LPCWSTR pwszHeaders, DWORD dwHeadersLength,
                                                    DWORD dwModifiers);
typedef DWORD (WINAPI* PFN_WinHttpAddRequestHeadersEx)(void* hRequest, DWORD dwModifiers, ULONGLONG ullFlags,
                                                      ULONGLONG ullExtra, DWORD cHeaders, void* pHeaders);
typedef BOOL (WINAPI* PFN_WinHttpCheckPlatform)(void);
typedef BOOL (WINAPI* PFN_WinHttpCloseHandle)(void* hInternet);
typedef void* (WINAPI* PFN_WinHttpConnect)(void* hSession, LPCWSTR pswzServerName, INTERNET_PORT nServerPort,
                                           DWORD dwReserved);
typedef BOOL (WINAPI* PFN_WinHttpCrackUrl)(LPCWSTR pszUrl, DWORD dwUrlLength, DWORD dwFlags, void* lpUrlComponents);
typedef DWORD (WINAPI* PFN_WinHttpCreateProxyResolver)(void* hSession, void** phResolver);
typedef BOOL (WINAPI* PFN_WinHttpCreateUrl)(void* lpUrlComponents, DWORD dwFlags, LPWSTR pwszUrl, LPDWORD pdwUrlLength);
typedef BOOL (WINAPI* PFN_WinHttpDetectAutoProxyConfigUrl)(DWORD dwAutoDetectFlags, LPWSTR* ppwszAutoConfigUrl);
typedef void (WINAPI* PFN_WinHttpFreeProxyResult)(void* pProxyResult);
typedef void (WINAPI* PFN_WinHttpFreeProxySettingsEx)(void* pProxySettingsEx);
typedef void (WINAPI* PFN_WinHttpFreeQueryConnectionGroupResult)(void* pConnectionGroupResult);
typedef BOOL (WINAPI* PFN_WinHttpGetDefaultProxyConfiguration)(void* pProxyInfo);
typedef BOOL (WINAPI* PFN_WinHttpGetIEProxyConfigForCurrentUser)(void* pProxyConfig);
typedef BOOL (WINAPI* PFN_WinHttpGetProxyForUrl)(void* hSession, LPCWSTR pswzUrl, void* pAutoProxyOptions,
                                                 void* pProxyInfo);
typedef DWORD (WINAPI* PFN_WinHttpGetProxyForUrlEx)(void* hResolver, LPCWSTR pcwszUrl, void* pAutoProxyOptions,
                                                    DWORD_PTR pContext);
typedef DWORD (WINAPI* PFN_WinHttpGetProxyResult)(void* hResolver, void* pProxyResult);
typedef DWORD (WINAPI* PFN_WinHttpGetProxySettingsEx)(void* hSession, void* pProxySettingsEx);
typedef DWORD (WINAPI* PFN_WinHttpGetProxySettingsResultEx)(void* hResolver, void* pProxySettingsResultEx);
typedef void* (WINAPI* PFN_WinHttpOpen)(LPCWSTR pszAgentW, DWORD dwAccessType, LPCWSTR pszProxyW,
                                        LPCWSTR pszProxyBypassW, DWORD dwFlags);

/* 21-40 */
typedef void* (WINAPI* PFN_WinHttpOpenRequest)(void* hConnect, LPCWSTR pwszVerb, LPCWSTR pwszObjectName,
                                               LPCWSTR pwszVersion, LPCWSTR pwszReferrer, LPCWSTR* ppwszAcceptTypes,
                                               DWORD dwFlags);
typedef BOOL (WINAPI* PFN_WinHttpQueryAuthSchemes)(void* hRequest, LPDWORD lpdwSupportedSchemes,
                                                  LPDWORD lpdwFirstScheme, LPDWORD pdwAuthTarget);
typedef DWORD (WINAPI* PFN_WinHttpQueryConnectionGroup)(void* hInternet, const void* pGuidConnection,
                                                       ULONGLONG ullFlags, void** ppResult);
typedef BOOL (WINAPI* PFN_WinHttpQueryDataAvailable)(void* hRequest, LPDWORD lpdwNumberOfBytesAvailable);
typedef BOOL (WINAPI* PFN_WinHttpQueryHeaders)(void* hRequest, DWORD dwInfoLevel, LPCWSTR pwszName, LPVOID lpBuffer,
                                              LPDWORD lpdwBufferLength, LPDWORD lpdwIndex);
typedef DWORD (WINAPI* PFN_WinHttpQueryHeadersEx)(void* hRequest, DWORD dwInfoLevel, ULONGLONG ullFlags,
                                                  UINT uiCodePage, DWORD* pdwIndex, void* pHeaderName,
                                                  LPVOID pBuffer, LPDWORD pdwBufferLength, void** ppHeaders,
                                                  LPDWORD pdwHeadersCount);
typedef BOOL (WINAPI* PFN_WinHttpQueryOption)(void* hInternet, DWORD dwOption, LPVOID lpBuffer,
                                              LPDWORD lpdwBufferLength);
typedef BOOL (WINAPI* PFN_WinHttpReadData)(void* hRequest, LPVOID lpBuffer, DWORD dwNumberOfBytesToRead,
                                         LPDWORD lpdwNumberOfBytesRead);
typedef DWORD (WINAPI* PFN_WinHttpReadDataEx)(void* hRequest, LPVOID lpBuffer, DWORD dwNumberOfBytesToRead,
                                             LPDWORD lpdwNumberOfBytesRead, ULONGLONG ullFlags,
                                             DWORD cbProperty, void* pvProperty);
typedef BOOL (WINAPI* PFN_WinHttpReceiveResponse)(void* hRequest, LPVOID lpReserved);
typedef DWORD (WINAPI* PFN_WinHttpRegisterProxyChangeNotification)(ULONGLONG ullFlags, void* pfnCallback,
                                                                   void* pvContext, void** phRegistration);
typedef BOOL (WINAPI* PFN_WinHttpResetAutoProxy)(void* hSession, DWORD dwFlags);
typedef BOOL (WINAPI* PFN_WinHttpSendRequest)(void* hRequest, LPCWSTR lpszHeaders, DWORD dwHeadersLength,
                                             LPVOID lpOptional, DWORD dwOptionalLength, DWORD dwTotalLength,
                                             DWORD_PTR dwContext);
typedef BOOL (WINAPI* PFN_WinHttpSetCredentials)(void* hRequest, DWORD dwAuthTargets, DWORD dwAuthScheme,
                                                LPCWSTR pwszUserName, LPCWSTR pwszPassword, LPVOID pAuthParams);
typedef BOOL (WINAPI* PFN_WinHttpSetDefaultProxyConfiguration)(void* pProxyInfo);
typedef BOOL (WINAPI* PFN_WinHttpSetOption)(void* hInternet, DWORD dwOption, LPVOID lpBuffer, DWORD dwBufferLength);
typedef WINHTTP_STATUS_CALLBACK_OPAQUE (WINAPI* PFN_WinHttpSetStatusCallback)(void* hInternet,
                                                                              WINHTTP_STATUS_CALLBACK_OPAQUE lpfnInternetCallback,
                                                                              DWORD dwNotificationFlags,
                                                                              DWORD_PTR dwReserved);
typedef BOOL (WINAPI* PFN_WinHttpSetTimeouts)(void* hInternet, int nResolveTimeout, int nConnectTimeout,
                                              int nSendTimeout, int nReceiveTimeout);
typedef BOOL (WINAPI* PFN_WinHttpTimeFromSystemTime)(const SYSTEMTIME* pst, LPWSTR pwszTime, DWORD cchTime);
typedef BOOL (WINAPI* PFN_WinHttpTimeToSystemTime)(LPCWSTR pwszTime, SYSTEMTIME* pst);

/* 41-48 */
typedef DWORD (WINAPI* PFN_WinHttpUnregisterProxyChangeNotification)(void* hRegistration);
typedef BOOL (WINAPI* PFN_WinHttpWriteData)(void* hRequest, LPCVOID lpBuffer, DWORD dwNumberOfBytesToWrite,
                                           LPDWORD lpdwNumberOfBytesWritten);
typedef DWORD (WINAPI* PFN_WinHttpWebSocketClose)(void* hWebSocket, USHORT usStatus, LPVOID pvReason,
                                                  DWORD dwReasonLength);
typedef void* (WINAPI* PFN_WinHttpWebSocketCompleteUpgrade)(void* hRequest, DWORD_PTR dwContext);
typedef DWORD (WINAPI* PFN_WinHttpWebSocketQueryCloseStatus)(void* hWebSocket, USHORT* pusStatus, LPVOID pvReason,
                                                             DWORD dwReasonLength, LPDWORD pdwReasonLengthConsumed);
typedef DWORD (WINAPI* PFN_WinHttpWebSocketReceive)(void* hWebSocket, LPVOID pvBuffer, DWORD dwBufferLength,
                                                    DWORD* pdwBytesRead, void* peBufferType);
typedef DWORD (WINAPI* PFN_WinHttpWebSocketSend)(void* hWebSocket, DWORD eBufferType, LPVOID pvBuffer,
                                                DWORD dwBufferLength);
typedef DWORD (WINAPI* PFN_WinHttpWebSocketShutdown)(void* hWebSocket, USHORT usStatus, LPVOID pvReason,
                                                     DWORD dwReasonLength);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_COMMANDER_WINHTTP_PROXY_HPP */
