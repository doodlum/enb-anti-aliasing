#include "Upscaling.h"

#include <ENB/ENBSeriesAPI.h>
extern ENB_API::ENBSDKALT1001* g_ENB;

#include "Util.h"

void Upscaling::RefreshUI()
{
	auto streamline = Streamline::GetSingleton();

	TwEnumVal aaTypes[] = { { 0, "TAA" }, { 1, "AMD FSR 3.1" }, { 2, "NVIDIA DLAA" }};
	TwType aaType = g_ENB->TwDefineEnum("AA_TYPE", aaTypes, streamline->featureDLSS ? 3 : 2);

	auto generalBar = g_ENB->TwGetBarByEnum(ENB_API::ENBWindowType::EditorBarButtons);

	g_ENB->TwAddVarRW(generalBar, "Mode", aaType, streamline->featureDLSS ? &settings.upscaleMode : &settings.upscaleModeNoDLSS, "group='ANTIALIASING'");
}

Upscaling::UpscaleMode Upscaling::GetUpscaleMode()
{
	auto streamline = Streamline::GetSingleton();
	return streamline->featureDLSS ? (UpscaleMode)settings.upscaleMode : (UpscaleMode)settings.upscaleModeNoDLSS;
}

void Upscaling::CheckResources()
{
	static auto previousUpscaleMode = UpscaleMode::kTAA;
	auto currentUpscaleMode = GetUpscaleMode();

	auto streamline = Streamline::GetSingleton();
	auto fidelityFX = FidelityFX::GetSingleton();

	if (previousUpscaleMode != currentUpscaleMode) {
		if (previousUpscaleMode == UpscaleMode::kTAA)
			CreateUpscalingResources();
		else if (previousUpscaleMode == UpscaleMode::kDLSS)
			streamline->DestroyDLSSResources();
		else if (previousUpscaleMode == UpscaleMode::kFSR)
			fidelityFX->DestroyFSRResources();

		if (currentUpscaleMode == UpscaleMode::kTAA)
			DestroyUpscalingResources();
		else if (previousUpscaleMode == UpscaleMode::kFSR)
			fidelityFX->DestroyFSRResources();
		else if (currentUpscaleMode == UpscaleMode::kFSR)
			fidelityFX->CreateFSRResources();

		previousUpscaleMode = currentUpscaleMode;
	}
}

ID3D11ComputeShader* Upscaling::GetRCASComputeShader()
{
	if (!rcasCS) {
		logger::debug("Compiling RCAS.hlsl");
		rcasCS = (ID3D11ComputeShader*)Util::CompileShader(L"Data/SKSE/Plugins/ENBAAUpgrade/RCAS/RCAS.hlsl", "cs_5_0");
	}
	return rcasCS;
}

static void SetDirtyStates(bool a_computeShader)
{
	using func_t = decltype(&SetDirtyStates);
	static REL::Relocation<func_t> func{ REL::RelocationID(75580, 77386) };
	func(a_computeShader);
}

void Upscaling::Upscale()
{
	CheckResources();

	SetDirtyStates(false);

	static auto renderer = RE::BSGraphics::Renderer::GetSingleton();
	static auto context = reinterpret_cast<ID3D11DeviceContext*>(renderer->GetRuntimeData().context);

	ID3D11ShaderResourceView* inputTextureSRV;
	context->PSGetShaderResources(0, 1, &inputTextureSRV);

	inputTextureSRV->Release();

	ID3D11RenderTargetView* outputTextureRTV;
	ID3D11DepthStencilView* dsv;
	context->OMGetRenderTargets(1, &outputTextureRTV, &dsv);
	context->OMSetRenderTargets(0, nullptr, nullptr);

	outputTextureRTV->Release();

	if (dsv)
		dsv->Release();

	ID3D11Resource* inputTextureResource;
	inputTextureSRV->GetResource(&inputTextureResource);

	ID3D11Resource* outputTextureResource;
	outputTextureRTV->GetResource(&outputTextureResource);

	context->CopyResource(upscalingTempTexture->resource.get(), inputTextureResource);

	auto upscaleMode = GetUpscaleMode();

	if (upscaleMode == UpscaleMode::kDLSS)
		Streamline::GetSingleton()->Upscale(upscalingTempTexture);
	else
		FidelityFX::GetSingleton()->Upscale(upscalingTempTexture);

	if (GetUpscaleMode() != UpscaleMode::kFSR) {
		context->CopyResource(inputTextureResource, upscalingTempTexture->resource.get());

		{
			{
				ID3D11ShaderResourceView* views[1] = { inputTextureSRV };
				context->CSSetShaderResources(0, ARRAYSIZE(views), views);

				ID3D11UnorderedAccessView* uavs[1] = { upscalingTempTexture->uav.get() };
				context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

				context->CSSetShader(GetRCASComputeShader(), nullptr, 0);

				static auto gameViewport = RE::BSGraphics::State::GetSingleton();
				uint dispatchX = (uint)std::ceil((float)gameViewport->screenWidth / 8.0f);
				uint dispatchY = (uint)std::ceil((float)gameViewport->screenHeight / 8.0f);

				context->Dispatch(dispatchX, dispatchY, 1);
			}

			ID3D11ShaderResourceView* views[1] = { nullptr };
			context->CSSetShaderResources(0, ARRAYSIZE(views), views);

			ID3D11UnorderedAccessView* uavs[1] = { nullptr };
			context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

			ID3D11ComputeShader* shader = nullptr;
			context->CSSetShader(shader, nullptr, 0);
		}
	}

	context->CopyResource(outputTextureResource, upscalingTempTexture->resource.get());
}

void Upscaling::CreateUpscalingResources()
{
	auto renderer = RE::BSGraphics::Renderer::GetSingleton();
	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

	D3D11_TEXTURE2D_DESC texDesc{};
	main.texture->GetDesc(&texDesc);
	texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
		.Format = texDesc.Format,
		.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
		.Texture2D = {
			.MostDetailedMip = 0,
			.MipLevels = 1 }
	};
	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
		.Format = texDesc.Format,
		.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
		.Texture2D = { .MipSlice = 0 }
	};

	texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

	upscalingTempTexture = new Texture2D(texDesc);
	upscalingTempTexture->CreateSRV(srvDesc);
	upscalingTempTexture->CreateUAV(uavDesc);
}

void Upscaling::DestroyUpscalingResources()
{
	upscalingTempTexture->srv = nullptr;
	upscalingTempTexture->uav = nullptr;
	upscalingTempTexture->resource = nullptr;
	delete upscalingTempTexture;
}
