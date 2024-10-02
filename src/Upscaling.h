#pragma once

#include "Buffer.h"
#include "FidelityFX.h"
#include "Streamline.h"

class Upscaling
{
public:
	static Upscaling* GetSingleton()
	{
		static Upscaling singleton;
		return &singleton;
	}

	void RefreshUI();

	enum class UpscaleMode
	{
		kTAA,
		kFSR,
		kDLSS
	};

	struct Settings
	{
		uint upscaleMode = (uint)UpscaleMode::kDLSS;
		uint upscaleModeNoDLSS = (uint)UpscaleMode::kFSR;
	};

	Settings settings;

	UpscaleMode GetUpscaleMode();

	void CheckResources();

	ID3D11ComputeShader* rcasCS;
	ID3D11ComputeShader* GetRCASComputeShader();

	ID3D11ComputeShader* encodeMaskCS;
	ID3D11ComputeShader* GetEncodeMaskComputeShader();

	void Upscale();

	Texture2D* upscalingTexture;
	Texture2D* maskTexture;

	void CreateUpscalingResources();
	void DestroyUpscalingResources();

	bool validTaaPass = false;

	struct TAA_BeginTechnique
	{
		static void thunk(RE::BSImagespaceShaderISTemporalAA* a_shader, RE::BSTriShape* a_null)
		{
			func(a_shader, a_null);
			GetSingleton()->validTaaPass = true;
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct TAA_EndTechnique
	{
		static void thunk(RE::BSImagespaceShaderISTemporalAA* a_shader, RE::BSTriShape* a_null)
		{
			auto singleton = GetSingleton();
			if (singleton->GetUpscaleMode() != UpscaleMode::kTAA && singleton->validTaaPass)
				singleton->Upscale();
			else
				func(a_shader, a_null);
			singleton->validTaaPass = false;
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	static void InstallHooks()
	{
		if (!REL::Module::IsVR()) {
			stl::write_thunk_call<TAA_BeginTechnique>(REL::RelocationID(100540, 107270).address() + REL::Relocate(0x3E9, 0x3EA, 0x448));
			stl::write_thunk_call<TAA_EndTechnique>(REL::RelocationID(100540, 107270).address() + REL::Relocate(0x3F3, 0x3F4, 0x452));
		}
	}
};
