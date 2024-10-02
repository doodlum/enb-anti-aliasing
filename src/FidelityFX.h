#pragma once

#include <FidelityFX/host/backends/dx11/ffx_dx11.h>
#include <FidelityFX/host/ffx_fsr3.h>
#include <FidelityFX/host/ffx_interface.h>

#include "Buffer.h"

class FidelityFX
{
public:
	static FidelityFX* GetSingleton()
	{
		static FidelityFX singleton;
		return &singleton;
	}

	FfxFsr3Context fsrContext;

	Texture2D* reactiveMask;
	Texture2D* opaqueColor;

	void CreateFSRResources();
	void DestroyFSRResources();
	void CopyOpaqueMask();
	void GenerateReactiveMask();
	void Upscale(Texture2D* a_color);
};
