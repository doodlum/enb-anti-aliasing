#pragma once

namespace Util
{
	ID3D11DeviceChild* CompileShader(const wchar_t* FilePath, const std::vector<std::pair<const char*, const char*>>& Defines, const char* ProgramType, const char* Program = "main");

	float GetVerticalFOVRad();

	RE::NiPoint3 GetEyePosition(int eyeIndex);

	RE::BSGraphics::ViewData GetCameraData(int eyeIndex);
}