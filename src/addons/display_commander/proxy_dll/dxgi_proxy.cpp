/*
 * DXGI Proxy Functions
 * Forwards DXGI calls to the real system dxgi.dll
 */

#include <dxgi.h>
#include <Windows.h>
#include <string>

// Function pointer types
typedef HRESULT(WINAPI* PFN_CreateDXGIFactory)(REFIID riid, void **ppFactory);
typedef HRESULT(WINAPI* PFN_CreateDXGIFactory1)(REFIID riid, void **ppFactory);
typedef HRESULT(WINAPI* PFN_CreateDXGIFactory2)(UINT Flags, REFIID riid, void **ppFactory);
typedef HRESULT(WINAPI* PFN_DXGIGetDebugInterface1)(UINT Flags, REFIID riid, void **pDebug);
typedef HRESULT(WINAPI* PFN_DXGIDeclareAdapterRemovalSupport)();

// Load real DXGI DLL and get function pointers
static HMODULE g_dxgi_module = nullptr;

static bool LoadRealDXGI()
{
	if (g_dxgi_module != nullptr)
		return true;

	WCHAR system_path[MAX_PATH];
	GetSystemDirectoryW(system_path, MAX_PATH);
	std::wstring dxgi_path = std::wstring(system_path) + L"\\dxgi.dll";

	g_dxgi_module = LoadLibraryW(dxgi_path.c_str());
	return g_dxgi_module != nullptr;
}

extern "C" HRESULT WINAPI CreateDXGIFactory(REFIID riid, void **ppFactory)
{
	if (!LoadRealDXGI())
		return E_FAIL;

	auto func = reinterpret_cast<PFN_CreateDXGIFactory>(GetProcAddress(g_dxgi_module, "CreateDXGIFactory"));
	if (func == nullptr)
		return E_FAIL;

	return func(riid, ppFactory);
}

extern "C" HRESULT WINAPI CreateDXGIFactory1(REFIID riid, void **ppFactory)
{
	if (!LoadRealDXGI())
		return E_FAIL;

	auto func = reinterpret_cast<PFN_CreateDXGIFactory1>(GetProcAddress(g_dxgi_module, "CreateDXGIFactory1"));
	if (func == nullptr)
		return E_FAIL;

	return func(riid, ppFactory);
}

extern "C" HRESULT WINAPI CreateDXGIFactory2(UINT Flags, REFIID riid, void **ppFactory)
{
	if (!LoadRealDXGI())
		return E_FAIL;

	auto func = reinterpret_cast<PFN_CreateDXGIFactory2>(GetProcAddress(g_dxgi_module, "CreateDXGIFactory2"));
	if (func == nullptr)
		return E_FAIL;

	return func(Flags, riid, ppFactory);
}

extern "C" HRESULT WINAPI DXGIGetDebugInterface1(UINT Flags, REFIID riid, void **pDebug)
{
	if (!LoadRealDXGI())
		return E_FAIL;

	auto func = reinterpret_cast<PFN_DXGIGetDebugInterface1>(GetProcAddress(g_dxgi_module, "DXGIGetDebugInterface1"));
	if (func == nullptr)
		return E_FAIL;

	return func(Flags, riid, pDebug);
}

extern "C" HRESULT WINAPI DXGIDeclareAdapterRemovalSupport()
{
	if (!LoadRealDXGI())
		return E_FAIL;

	auto func = reinterpret_cast<PFN_DXGIDeclareAdapterRemovalSupport>(GetProcAddress(g_dxgi_module, "DXGIDeclareAdapterRemovalSupport"));
	if (func == nullptr)
		return E_FAIL;

	return func();
}

