#include "Util.h"

#include <d3dcompiler.h>

namespace Util
{
	void SetDirtyStates(bool a_computeShader)
	{
		using func_t = decltype(&SetDirtyStates);
		static REL::Relocation<func_t> func{ REL::RelocationID(75580, 77386) };
		func(a_computeShader);
	}

	ID3D11DeviceChild* CompileShader(const wchar_t* FilePath, const char* ProgramType, const char* Program)
	{
		static auto renderer = RE::BSGraphics::Renderer::GetSingleton();
		static auto device = reinterpret_cast<ID3D11Device*>(renderer->GetRuntimeData().forwarder);

		// Compiler setup
		uint32_t flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;

		ID3DBlob* shaderBlob;
		ID3DBlob* shaderErrors;

		std::string str;
		std::wstring path{ FilePath };
		std::transform(path.begin(), path.end(), std::back_inserter(str), [](wchar_t c) {
			return (char)c;
		});
		if (!std::filesystem::exists(FilePath)) {
			logger::error("Failed to compile shader; {} does not exist", str);
			return nullptr;
		}
		if (FAILED(D3DCompileFromFile(FilePath, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, Program, ProgramType, flags, 0, &shaderBlob, &shaderErrors))) {
			logger::warn("Shader compilation failed:\n\n{}", shaderErrors ? static_cast<char*>(shaderErrors->GetBufferPointer()) : "Unknown error");
			return nullptr;
		}
		if (shaderErrors)
			logger::debug("Shader logs:\n{}", static_cast<char*>(shaderErrors->GetBufferPointer()));

		ID3D11ComputeShader* regShader;
		DX::ThrowIfFailed(device->CreateComputeShader(shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), nullptr, &regShader));
		return regShader;
	}

	// https://github.com/PureDark/Skyrim-Upscaler/blob/fa057bb088cf399e1112c1eaba714590c881e462/src/SkyrimUpscaler.cpp#L88
	float GetVerticalFOVRad()
	{
		static float& fac = (*(float*)(REL::RelocationID(513786, 388785).address()));
		const auto base = fac;
		const auto x = base / 1.30322540f;
		static auto gameViewport = RE::BSGraphics::State::GetSingleton();
		const auto vFOV = 2 * atan(x / ((float)gameViewport->screenWidth / gameViewport->screenHeight));
		return vFOV;
	}

	RE::NiPoint3 GetEyePosition(int eyeIndex)
	{
		static auto shadowState = RE::BSGraphics::RendererShadowState::GetSingleton();
		if (!REL::Module::IsVR())
			return shadowState->GetRuntimeData().posAdjust.getEye();
		return shadowState->GetVRRuntimeData().posAdjust.getEye(eyeIndex);
	}

	RE::BSGraphics::ViewData GetCameraData(int eyeIndex)
	{
		static auto shadowState = RE::BSGraphics::RendererShadowState::GetSingleton();
		if (!REL::Module::IsVR()) {
			return shadowState->GetRuntimeData().cameraData.getEye();
		}
		return shadowState->GetVRRuntimeData().cameraData.getEye(eyeIndex);
	}
}