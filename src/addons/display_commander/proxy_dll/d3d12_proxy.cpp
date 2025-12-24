/*
 * D3D12 Proxy Functions
 * Forwards D3D12 calls to the real system d3d12.dll
 */

#include <d3d12.h>
#include <Windows.h>
#include <string>

// Function pointer types
typedef HRESULT(WINAPI* PFN_D3D12CreateDevice)(IUnknown *pAdapter, D3D_FEATURE_LEVEL MinimumFeatureLevel, REFIID riid, void **ppDevice);
typedef HRESULT(WINAPI* PFN_D3D12GetDebugInterface)(REFIID riid, void **ppvDebug);
typedef HRESULT(WINAPI* PFN_D3D12CreateRootSignatureDeserializer)(LPCVOID pSrcData, SIZE_T SrcDataSizeInBytes, REFIID pRootSignatureDeserializerInterface, void **ppRootSignatureDeserializer);
typedef HRESULT(WINAPI* PFN_D3D12CreateVersionedRootSignatureDeserializer)(LPCVOID pSrcData, SIZE_T SrcDataSizeInBytes, REFIID pRootSignatureDeserializerInterface, void **ppRootSignatureDeserializer);
typedef HRESULT(WINAPI* PFN_D3D12EnableExperimentalFeatures)(UINT NumFeatures, const IID *pIIDs, void *pConfigurationStructs, UINT *pConfigurationStructSizes);
typedef HRESULT(WINAPI* PFN_D3D12GetInterface)(REFCLSID rclsid, REFIID riid, void **ppvDebug);
typedef HRESULT(WINAPI* PFN_D3D12SerializeRootSignature)(const D3D12_ROOT_SIGNATURE_DESC *pRootSignature, D3D_ROOT_SIGNATURE_VERSION Version, ID3DBlob **ppBlob, ID3DBlob **ppErrorBlob);
typedef HRESULT(WINAPI* PFN_D3D12SerializeVersionedRootSignature)(const D3D12_VERSIONED_ROOT_SIGNATURE_DESC *pRootSignature, ID3DBlob **ppBlob, ID3DBlob **ppErrorBlob);

// Load real D3D12 DLL and get function pointers
static HMODULE g_d3d12_module = nullptr;

static bool LoadRealD3D12()
{
	if (g_d3d12_module != nullptr)
		return true;

	WCHAR system_path[MAX_PATH];
	GetSystemDirectoryW(system_path, MAX_PATH);
	std::wstring d3d12_path = std::wstring(system_path) + L"\\d3d12.dll";

	g_d3d12_module = LoadLibraryW(d3d12_path.c_str());
	return g_d3d12_module != nullptr;
}

extern "C" HRESULT WINAPI D3D12CreateDevice(IUnknown *pAdapter, D3D_FEATURE_LEVEL MinimumFeatureLevel, REFIID riid, void **ppDevice)
{
	if (!LoadRealD3D12())
		return E_FAIL;

	auto func = reinterpret_cast<PFN_D3D12CreateDevice>(GetProcAddress(g_d3d12_module, "D3D12CreateDevice"));
	if (func == nullptr)
		return E_FAIL;

	return func(pAdapter, MinimumFeatureLevel, riid, ppDevice);
}

extern "C" HRESULT WINAPI D3D12GetDebugInterface(REFIID riid, void **ppvDebug)
{
	if (!LoadRealD3D12())
		return E_FAIL;

	auto func = reinterpret_cast<PFN_D3D12GetDebugInterface>(GetProcAddress(g_d3d12_module, "D3D12GetDebugInterface"));
	if (func == nullptr)
		return E_FAIL;

	return func(riid, ppvDebug);
}

extern "C" HRESULT WINAPI D3D12CreateRootSignatureDeserializer(LPCVOID pSrcData, SIZE_T SrcDataSizeInBytes, REFIID pRootSignatureDeserializerInterface, void **ppRootSignatureDeserializer)
{
	if (!LoadRealD3D12())
		return E_FAIL;

	auto func = reinterpret_cast<PFN_D3D12CreateRootSignatureDeserializer>(GetProcAddress(g_d3d12_module, "D3D12CreateRootSignatureDeserializer"));
	if (func == nullptr)
		return E_FAIL;

	return func(pSrcData, SrcDataSizeInBytes, pRootSignatureDeserializerInterface, ppRootSignatureDeserializer);
}

extern "C" HRESULT WINAPI D3D12CreateVersionedRootSignatureDeserializer(LPCVOID pSrcData, SIZE_T SrcDataSizeInBytes, REFIID pRootSignatureDeserializerInterface, void **ppRootSignatureDeserializer)
{
	if (!LoadRealD3D12())
		return E_FAIL;

	auto func = reinterpret_cast<PFN_D3D12CreateVersionedRootSignatureDeserializer>(GetProcAddress(g_d3d12_module, "D3D12CreateVersionedRootSignatureDeserializer"));
	if (func == nullptr)
		return E_FAIL;

	return func(pSrcData, SrcDataSizeInBytes, pRootSignatureDeserializerInterface, ppRootSignatureDeserializer);
}

extern "C" HRESULT WINAPI D3D12EnableExperimentalFeatures(UINT NumFeatures, const IID *pIIDs, void *pConfigurationStructs, UINT *pConfigurationStructSizes)
{
	if (!LoadRealD3D12())
		return E_FAIL;

	auto func = reinterpret_cast<PFN_D3D12EnableExperimentalFeatures>(GetProcAddress(g_d3d12_module, "D3D12EnableExperimentalFeatures"));
	if (func == nullptr)
		return E_FAIL;

	return func(NumFeatures, pIIDs, pConfigurationStructs, pConfigurationStructSizes);
}

extern "C" HRESULT WINAPI D3D12GetInterface(REFCLSID rclsid, REFIID riid, void **ppvDebug)
{
	if (!LoadRealD3D12())
		return E_FAIL;

	auto func = reinterpret_cast<PFN_D3D12GetInterface>(GetProcAddress(g_d3d12_module, "D3D12GetInterface"));
	if (func == nullptr)
		return E_FAIL;

	return func(rclsid, riid, ppvDebug);
}

extern "C" HRESULT WINAPI D3D12SerializeRootSignature(const D3D12_ROOT_SIGNATURE_DESC *pRootSignature, D3D_ROOT_SIGNATURE_VERSION Version, ID3DBlob **ppBlob, ID3DBlob **ppErrorBlob)
{
	if (!LoadRealD3D12())
		return E_FAIL;

	auto func = reinterpret_cast<PFN_D3D12SerializeRootSignature>(GetProcAddress(g_d3d12_module, "D3D12SerializeRootSignature"));
	if (func == nullptr)
		return E_FAIL;

	return func(pRootSignature, Version, ppBlob, ppErrorBlob);
}

extern "C" HRESULT WINAPI D3D12SerializeVersionedRootSignature(const D3D12_VERSIONED_ROOT_SIGNATURE_DESC *pRootSignature, ID3DBlob **ppBlob, ID3DBlob **ppErrorBlob)
{
	if (!LoadRealD3D12())
		return E_FAIL;

	auto func = reinterpret_cast<PFN_D3D12SerializeVersionedRootSignature>(GetProcAddress(g_d3d12_module, "D3D12SerializeVersionedRootSignature"));
	if (func == nullptr)
		return E_FAIL;

	return func(pRootSignature, ppBlob, ppErrorBlob);
}

