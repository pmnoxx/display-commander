/*
 * D3D11 Proxy Functions
 * Forwards D3D11 calls to the real system d3d11.dll
 */

#include <d3d11.h>
#include <Windows.h>
#include <string>

// Function pointer types
typedef HRESULT(WINAPI* PFN_D3D11CreateDevice)(IDXGIAdapter *pAdapter, D3D_DRIVER_TYPE DriverType, HMODULE Software, UINT Flags, const D3D_FEATURE_LEVEL *pFeatureLevels, UINT FeatureLevels, UINT SDKVersion, ID3D11Device **ppDevice, D3D_FEATURE_LEVEL *pFeatureLevel, ID3D11DeviceContext **ppImmediateContext);
typedef HRESULT(WINAPI* PFN_D3D11CreateDeviceAndSwapChain)(IDXGIAdapter *pAdapter, D3D_DRIVER_TYPE DriverType, HMODULE Software, UINT Flags, const D3D_FEATURE_LEVEL *pFeatureLevels, UINT FeatureLevels, UINT SDKVersion, const DXGI_SWAP_CHAIN_DESC *pSwapChainDesc, IDXGISwapChain **ppSwapChain, ID3D11Device **ppDevice, D3D_FEATURE_LEVEL *pFeatureLevel, ID3D11DeviceContext **ppImmediateContext);
typedef HRESULT(WINAPI* PFN_D3D11On12CreateDevice)(IUnknown *pDevice, UINT Flags, CONST D3D_FEATURE_LEVEL *pFeatureLevels, UINT FeatureLevels, IUnknown *CONST *ppCommandQueues, UINT NumQueues, UINT NodeMask, ID3D11Device **ppDevice, ID3D11DeviceContext **ppImmediateContext, D3D_FEATURE_LEVEL *pChosenFeatureLevel);

// Load real D3D11 DLL and get function pointers
static HMODULE g_d3d11_module = nullptr;

static bool LoadRealD3D11()
{
	if (g_d3d11_module != nullptr)
		return true;

	WCHAR system_path[MAX_PATH];
	GetSystemDirectoryW(system_path, MAX_PATH);
	std::wstring d3d11_path = std::wstring(system_path) + L"\\d3d11.dll";

	g_d3d11_module = LoadLibraryW(d3d11_path.c_str());
	return g_d3d11_module != nullptr;
}

extern "C" HRESULT WINAPI D3D11CreateDevice(IDXGIAdapter *pAdapter, D3D_DRIVER_TYPE DriverType, HMODULE Software, UINT Flags, const D3D_FEATURE_LEVEL *pFeatureLevels, UINT FeatureLevels, UINT SDKVersion, ID3D11Device **ppDevice, D3D_FEATURE_LEVEL *pFeatureLevel, ID3D11DeviceContext **ppImmediateContext)
{
	if (!LoadRealD3D11())
		return E_FAIL;

	auto func = reinterpret_cast<PFN_D3D11CreateDevice>(GetProcAddress(g_d3d11_module, "D3D11CreateDevice"));
	if (func == nullptr)
		return E_FAIL;

	return func(pAdapter, DriverType, Software, Flags, pFeatureLevels, FeatureLevels, SDKVersion, ppDevice, pFeatureLevel, ppImmediateContext);
}

extern "C" HRESULT WINAPI D3D11CreateDeviceAndSwapChain(IDXGIAdapter *pAdapter, D3D_DRIVER_TYPE DriverType, HMODULE Software, UINT Flags, const D3D_FEATURE_LEVEL *pFeatureLevels, UINT FeatureLevels, UINT SDKVersion, const DXGI_SWAP_CHAIN_DESC *pSwapChainDesc, IDXGISwapChain **ppSwapChain, ID3D11Device **ppDevice, D3D_FEATURE_LEVEL *pFeatureLevel, ID3D11DeviceContext **ppImmediateContext)
{
	if (!LoadRealD3D11())
		return E_FAIL;

	auto func = reinterpret_cast<PFN_D3D11CreateDeviceAndSwapChain>(GetProcAddress(g_d3d11_module, "D3D11CreateDeviceAndSwapChain"));
	if (func == nullptr)
		return E_FAIL;

	return func(pAdapter, DriverType, Software, Flags, pFeatureLevels, FeatureLevels, SDKVersion, pSwapChainDesc, ppSwapChain, ppDevice, pFeatureLevel, ppImmediateContext);
}

extern "C" HRESULT WINAPI D3D11On12CreateDevice(IUnknown *pDevice, UINT Flags, CONST D3D_FEATURE_LEVEL *pFeatureLevels, UINT FeatureLevels, IUnknown *CONST *ppCommandQueues, UINT NumQueues, UINT NodeMask, ID3D11Device **ppDevice, ID3D11DeviceContext **ppImmediateContext, D3D_FEATURE_LEVEL *pChosenFeatureLevel)
{
	if (!LoadRealD3D11())
		return E_FAIL;

	auto func = reinterpret_cast<PFN_D3D11On12CreateDevice>(GetProcAddress(g_d3d11_module, "D3D11On12CreateDevice"));
	if (func == nullptr)
		return E_FAIL;

	return func(pDevice, Flags, pFeatureLevels, FeatureLevels, ppCommandQueues, NumQueues, NodeMask, ppDevice, ppImmediateContext, pChosenFeatureLevel);
}

