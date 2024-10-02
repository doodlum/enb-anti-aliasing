#pragma once

namespace Util
{
	void SetDirtyStates(bool a_computeShader);

	ID3D11DeviceChild* CompileShader(const wchar_t* FilePath, const char* ProgramType, const char* Program = "main");

	float GetVerticalFOVRad();

	RE::NiPoint3 GetEyePosition(int eyeIndex);

	RE::BSGraphics::ViewData GetCameraData(int eyeIndex);
}