#include "Hooks.h"

#include <d3d11.h>

#include "Streamline.h"

decltype(&D3D11CreateDeviceAndSwapChain) ptrD3D11CreateDeviceAndSwapChain;

HRESULT WINAPI hk_D3D11CreateDeviceAndSwapChainNoStreamline(
	IDXGIAdapter* pAdapter,
	D3D_DRIVER_TYPE DriverType,
	HMODULE Software,
	UINT Flags,
	[[maybe_unused]] const D3D_FEATURE_LEVEL* pFeatureLevels,
	[[maybe_unused]] UINT FeatureLevels,
	UINT SDKVersion,
	const DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
	IDXGISwapChain** ppSwapChain,
	ID3D11Device** ppDevice,
	D3D_FEATURE_LEVEL* pFeatureLevel,
	ID3D11DeviceContext** ppImmediateContext)
{
	const D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_1;  // Create a device with only the latest feature level
	return ptrD3D11CreateDeviceAndSwapChain(pAdapter,
		DriverType,
		Software,
		Flags,
		&featureLevel,
		1,
		SDKVersion,
		pSwapChainDesc,
		ppSwapChain,
		ppDevice,
		pFeatureLevel,
		ppImmediateContext);
}

HRESULT WINAPI hk_D3D11CreateDeviceAndSwapChain(
	IDXGIAdapter* pAdapter,
	D3D_DRIVER_TYPE DriverType,
	HMODULE Software,
	UINT Flags,
	[[maybe_unused]] const D3D_FEATURE_LEVEL* pFeatureLevels,
	[[maybe_unused]] UINT FeatureLevels,
	UINT SDKVersion,
	const DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
	IDXGISwapChain** ppSwapChain,
	ID3D11Device** ppDevice,
	D3D_FEATURE_LEVEL* pFeatureLevel,
	ID3D11DeviceContext** ppImmediateContext)
{
	const D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_1;  // Create a device with only the latest feature level

	auto result = Streamline::GetSingleton()->CreateDeviceAndSwapChain(
		pAdapter,
		DriverType,
		Software,
		Flags,
		&featureLevel,
		1,
		SDKVersion,
		pSwapChainDesc,
		ppSwapChain,
		ppDevice,
		pFeatureLevel,
		ppImmediateContext);
	if (SUCCEEDED(result)) {
		return result;
	}
	return ptrD3D11CreateDeviceAndSwapChain(pAdapter,
		DriverType,
		Software,
		Flags,
		&featureLevel,
		1,
		SDKVersion,
		pSwapChainDesc,
		ppSwapChain,
		ppDevice,
		pFeatureLevel,
		ppImmediateContext);
}

namespace Hooks
{
	void InstallD3DHooks()
	{
		if (!REL::Module::IsVR()) {
			auto streamline = Streamline::GetSingleton();

			streamline->LoadInterposer();

			logger::info("Hooking D3D11CreateDeviceAndSwapChain");

			if (streamline->interposer)
				*(uintptr_t*)&ptrD3D11CreateDeviceAndSwapChain = SKSE::PatchIAT(hk_D3D11CreateDeviceAndSwapChain, "d3d11.dll", "D3D11CreateDeviceAndSwapChain");
			else 
				*(uintptr_t*)&ptrD3D11CreateDeviceAndSwapChain = SKSE::PatchIAT(hk_D3D11CreateDeviceAndSwapChainNoStreamline, "d3d11.dll", "D3D11CreateDeviceAndSwapChain");
		}
	}
}