/*
 * Version.dll Proxy Functions
 * Forwards version.dll calls to the real system version.dll
 */

#include <Windows.h>
#include <winver.h>
#include <string>

// Function pointer types for version.dll functions
typedef BOOL(WINAPI* PFN_GetFileVersionInfoA)(LPCSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData);
typedef BOOL(WINAPI* PFN_GetFileVersionInfoByHandle)(HANDLE hFile, LPCWSTR lpSubBlock, LPVOID* lplpBuffer, PUINT puLen);
typedef BOOL(WINAPI* PFN_GetFileVersionInfoExA)(DWORD dwFlags, LPCSTR lpwstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData);
typedef BOOL(WINAPI* PFN_GetFileVersionInfoExW)(DWORD dwFlags, LPCWSTR lpwstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData);
typedef DWORD(WINAPI* PFN_GetFileVersionInfoSizeA)(LPCSTR lptstrFilename, LPDWORD lpdwHandle);
typedef DWORD(WINAPI* PFN_GetFileVersionInfoSizeExA)(DWORD dwFlags, LPCSTR lpwstrFilename, LPDWORD lpdwHandle);
typedef DWORD(WINAPI* PFN_GetFileVersionInfoSizeExW)(DWORD dwFlags, LPCWSTR lpwstrFilename, LPDWORD lpdwHandle);
typedef DWORD(WINAPI* PFN_GetFileVersionInfoSizeW)(LPCWSTR lptstrFilename, LPDWORD lpdwHandle);
typedef BOOL(WINAPI* PFN_GetFileVersionInfoW)(LPCWSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData);
typedef DWORD(WINAPI* PFN_VerFindFileA)(DWORD uFlags, LPCSTR szFileName, LPCSTR szWinDir, LPCSTR szAppDir, LPSTR szCurDir, PUINT puCurDirLen, LPSTR szDestDir, PUINT puDestDirLen);
typedef DWORD(WINAPI* PFN_VerFindFileW)(DWORD uFlags, LPCWSTR szFileName, LPCWSTR szWinDir, LPCWSTR szAppDir, LPWSTR szCurDir, PUINT puCurDirLen, LPWSTR szDestDir, PUINT puDestDirLen);
typedef DWORD(WINAPI* PFN_VerInstallFileA)(DWORD uFlags, LPCSTR szSrcFileName, LPCSTR szDestFileName, LPCSTR szSrcDir, LPCSTR szDestDir, LPCSTR szCurDir, LPSTR szTmpFile, PUINT puTmpFileLen);
typedef DWORD(WINAPI* PFN_VerInstallFileW)(DWORD uFlags, LPCWSTR szSrcFileName, LPCWSTR szDestFileName, LPCWSTR szSrcDir, LPCWSTR szDestDir, LPCWSTR szCurDir, LPWSTR szTmpFile, PUINT puTmpFileLen);
typedef DWORD(WINAPI* PFN_VerLanguageNameA)(DWORD wLang, LPSTR szLang, DWORD nSize);
typedef DWORD(WINAPI* PFN_VerLanguageNameW)(DWORD wLang, LPWSTR szLang, DWORD nSize);
typedef BOOL(WINAPI* PFN_VerQueryValueA)(LPCVOID pBlock, LPCSTR lpSubBlock, LPVOID* lplpBuffer, PUINT puLen);
typedef BOOL(WINAPI* PFN_VerQueryValueW)(LPCVOID pBlock, LPCWSTR lpSubBlock, LPVOID* lplpBuffer, PUINT puLen);

// Load real version.dll and get function pointers
static HMODULE g_version_module = nullptr;

static bool LoadRealVersion()
{
	if (g_version_module != nullptr)
		return true;

	WCHAR system_path[MAX_PATH];
	GetSystemDirectoryW(system_path, MAX_PATH);
	std::wstring version_path = std::wstring(system_path) + L"\\version.dll";

	g_version_module = LoadLibraryW(version_path.c_str());
	return g_version_module != nullptr;
}

extern "C" BOOL WINAPI GetFileVersionInfoA(LPCSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData)
{
	if (!LoadRealVersion())
		return FALSE;

	auto func = reinterpret_cast<PFN_GetFileVersionInfoA>(GetProcAddress(g_version_module, "GetFileVersionInfoA"));
	if (func == nullptr)
		return FALSE;

	return func(lptstrFilename, dwHandle, dwLen, lpData);
}

extern "C" BOOL WINAPI GetFileVersionInfoByHandle(HANDLE hFile, LPCWSTR lpSubBlock, LPVOID* lplpBuffer, PUINT puLen)
{
	if (!LoadRealVersion())
		return FALSE;

	auto func = reinterpret_cast<PFN_GetFileVersionInfoByHandle>(GetProcAddress(g_version_module, "GetFileVersionInfoByHandle"));
	if (func == nullptr)
		return FALSE;

	return func(hFile, lpSubBlock, lplpBuffer, puLen);
}

extern "C" BOOL WINAPI GetFileVersionInfoExA(DWORD dwFlags, LPCSTR lpwstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData)
{
	if (!LoadRealVersion())
		return FALSE;

	auto func = reinterpret_cast<PFN_GetFileVersionInfoExA>(GetProcAddress(g_version_module, "GetFileVersionInfoExA"));
	if (func == nullptr)
		return FALSE;

	return func(dwFlags, lpwstrFilename, dwHandle, dwLen, lpData);
}

extern "C" BOOL WINAPI GetFileVersionInfoExW(DWORD dwFlags, LPCWSTR lpwstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData)
{
	if (!LoadRealVersion())
		return FALSE;

	auto func = reinterpret_cast<PFN_GetFileVersionInfoExW>(GetProcAddress(g_version_module, "GetFileVersionInfoExW"));
	if (func == nullptr)
		return FALSE;

	return func(dwFlags, lpwstrFilename, dwHandle, dwLen, lpData);
}

extern "C" DWORD WINAPI GetFileVersionInfoSizeA(LPCSTR lptstrFilename, LPDWORD lpdwHandle)
{
	if (!LoadRealVersion())
		return 0;

	auto func = reinterpret_cast<PFN_GetFileVersionInfoSizeA>(GetProcAddress(g_version_module, "GetFileVersionInfoSizeA"));
	if (func == nullptr)
		return 0;

	return func(lptstrFilename, lpdwHandle);
}

extern "C" DWORD WINAPI GetFileVersionInfoSizeExA(DWORD dwFlags, LPCSTR lpwstrFilename, LPDWORD lpdwHandle)
{
	if (!LoadRealVersion())
		return 0;

	auto func = reinterpret_cast<PFN_GetFileVersionInfoSizeExA>(GetProcAddress(g_version_module, "GetFileVersionInfoSizeExA"));
	if (func == nullptr)
		return 0;

	return func(dwFlags, lpwstrFilename, lpdwHandle);
}

extern "C" DWORD WINAPI GetFileVersionInfoSizeExW(DWORD dwFlags, LPCWSTR lpwstrFilename, LPDWORD lpdwHandle)
{
	if (!LoadRealVersion())
		return 0;

	auto func = reinterpret_cast<PFN_GetFileVersionInfoSizeExW>(GetProcAddress(g_version_module, "GetFileVersionInfoSizeExW"));
	if (func == nullptr)
		return 0;

	return func(dwFlags, lpwstrFilename, lpdwHandle);
}

extern "C" DWORD WINAPI GetFileVersionInfoSizeW(LPCWSTR lptstrFilename, LPDWORD lpdwHandle)
{
	if (!LoadRealVersion())
		return 0;

	auto func = reinterpret_cast<PFN_GetFileVersionInfoSizeW>(GetProcAddress(g_version_module, "GetFileVersionInfoSizeW"));
	if (func == nullptr)
		return 0;

	return func(lptstrFilename, lpdwHandle);
}

extern "C" BOOL WINAPI GetFileVersionInfoW(LPCWSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData)
{
	if (!LoadRealVersion())
		return FALSE;

	auto func = reinterpret_cast<PFN_GetFileVersionInfoW>(GetProcAddress(g_version_module, "GetFileVersionInfoW"));
	if (func == nullptr)
		return FALSE;

	return func(lptstrFilename, dwHandle, dwLen, lpData);
}

extern "C" DWORD WINAPI VerFindFileA(DWORD uFlags, LPCSTR szFileName, LPCSTR szWinDir, LPCSTR szAppDir, LPSTR szCurDir, PUINT puCurDirLen, LPSTR szDestDir, PUINT puDestDirLen)
{
	if (!LoadRealVersion())
		return 0;

	auto func = reinterpret_cast<PFN_VerFindFileA>(GetProcAddress(g_version_module, "VerFindFileA"));
	if (func == nullptr)
		return 0;

	return func(uFlags, szFileName, szWinDir, szAppDir, szCurDir, puCurDirLen, szDestDir, puDestDirLen);
}

extern "C" DWORD WINAPI VerFindFileW(DWORD uFlags, LPCWSTR szFileName, LPCWSTR szWinDir, LPCWSTR szAppDir, LPWSTR szCurDir, PUINT puCurDirLen, LPWSTR szDestDir, PUINT puDestDirLen)
{
	if (!LoadRealVersion())
		return 0;

	auto func = reinterpret_cast<PFN_VerFindFileW>(GetProcAddress(g_version_module, "VerFindFileW"));
	if (func == nullptr)
		return 0;

	return func(uFlags, szFileName, szWinDir, szAppDir, szCurDir, puCurDirLen, szDestDir, puDestDirLen);
}

extern "C" DWORD WINAPI VerInstallFileA(DWORD uFlags, LPCSTR szSrcFileName, LPCSTR szDestFileName, LPCSTR szSrcDir, LPCSTR szDestDir, LPCSTR szCurDir, LPSTR szTmpFile, PUINT puTmpFileLen)
{
	if (!LoadRealVersion())
		return 0;

	auto func = reinterpret_cast<PFN_VerInstallFileA>(GetProcAddress(g_version_module, "VerInstallFileA"));
	if (func == nullptr)
		return 0;

	return func(uFlags, szSrcFileName, szDestFileName, szSrcDir, szDestDir, szCurDir, szTmpFile, puTmpFileLen);
}

extern "C" DWORD WINAPI VerInstallFileW(DWORD uFlags, LPCWSTR szSrcFileName, LPCWSTR szDestFileName, LPCWSTR szSrcDir, LPCWSTR szDestDir, LPCWSTR szCurDir, LPWSTR szTmpFile, PUINT puTmpFileLen)
{
	if (!LoadRealVersion())
		return 0;

	auto func = reinterpret_cast<PFN_VerInstallFileW>(GetProcAddress(g_version_module, "VerInstallFileW"));
	if (func == nullptr)
		return 0;

	return func(uFlags, szSrcFileName, szDestFileName, szSrcDir, szDestDir, szCurDir, szTmpFile, puTmpFileLen);
}

extern "C" DWORD WINAPI VerLanguageNameA(DWORD wLang, LPSTR szLang, DWORD nSize)
{
	if (!LoadRealVersion())
		return 0;

	auto func = reinterpret_cast<PFN_VerLanguageNameA>(GetProcAddress(g_version_module, "VerLanguageNameA"));
	if (func == nullptr)
		return 0;

	return func(wLang, szLang, nSize);
}

extern "C" DWORD WINAPI VerLanguageNameW(DWORD wLang, LPWSTR szLang, DWORD nSize)
{
	if (!LoadRealVersion())
		return 0;

	auto func = reinterpret_cast<PFN_VerLanguageNameW>(GetProcAddress(g_version_module, "VerLanguageNameW"));
	if (func == nullptr)
		return 0;

	return func(wLang, szLang, nSize);
}

extern "C" BOOL WINAPI VerQueryValueA(LPCVOID pBlock, LPCSTR lpSubBlock, LPVOID* lplpBuffer, PUINT puLen)
{
	if (!LoadRealVersion())
		return FALSE;

	auto func = reinterpret_cast<PFN_VerQueryValueA>(GetProcAddress(g_version_module, "VerQueryValueA"));
	if (func == nullptr)
		return FALSE;

	return func(pBlock, lpSubBlock, lplpBuffer, puLen);
}

extern "C" BOOL WINAPI VerQueryValueW(LPCVOID pBlock, LPCWSTR lpSubBlock, LPVOID* lplpBuffer, PUINT puLen)
{
	if (!LoadRealVersion())
		return FALSE;

	auto func = reinterpret_cast<PFN_VerQueryValueW>(GetProcAddress(g_version_module, "VerQueryValueW"));
	if (func == nullptr)
		return FALSE;

	return func(pBlock, lpSubBlock, lplpBuffer, puLen);
}
