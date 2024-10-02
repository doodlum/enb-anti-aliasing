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

	g_ENB->TwAddVarRW(generalBar, "Method", aaType, streamline->featureDLSS ? &settings.upscaleMode : &settings.upscaleModeNoDLSS, "group='ANTIALIASING'");
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
		rcasCS = (ID3D11ComputeShader*)Util::CompileShader(L"Data/SKSE/Plugins/ENBAntiAliasing/RCAS/RCAS.hlsl", "cs_5_0");
	}
	return rcasCS;
}


ID3D11ComputeShader* Upscaling::GetEncodeMaskComputeShader()
{
	if (!encodeMaskCS) {
		logger::debug("Compiling EncodReactiveMaskCS.hlsl");
		encodeMaskCS = (ID3D11ComputeShader*)Util::CompileShader(L"Data/SKSE/Plugins/ENBAntiAliasing/EncodeMaskCS.hlsl", "cs_5_0");
	}
	return encodeMaskCS;
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

	context->CopyResource(upscalingTexture->resource.get(), inputTextureResource);

	static auto gameViewport = RE::BSGraphics::State::GetSingleton();
	uint dispatchX = (uint)std::ceil((float)gameViewport->screenWidth / 8.0f);
	uint dispatchY = (uint)std::ceil((float)gameViewport->screenHeight / 8.0f);

	{	
		static auto& temporalAAMask = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kTEMPORAL_AA_MASK];

		{
			ID3D11ShaderResourceView* views[1] = { temporalAAMask.SRV };
			context->CSSetShaderResources(0, ARRAYSIZE(views), views);

			ID3D11UnorderedAccessView* uavs[1] = { maskTexture->uav.get() };
			context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

			context->CSSetShader(GetEncodeMaskComputeShader(), nullptr, 0);

			context->Dispatch(dispatchX, dispatchY, 1);
		}

		ID3D11ShaderResourceView* views[1] = { nullptr };
		context->CSSetShaderResources(0, ARRAYSIZE(views), views);

		ID3D11UnorderedAccessView* uavs[1] = { nullptr };
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		ID3D11ComputeShader* shader = nullptr;
		context->CSSetShader(shader, nullptr, 0);
	}

	auto upscaleMode = GetUpscaleMode();

	if (upscaleMode == UpscaleMode::kDLSS)
		Streamline::GetSingleton()->Upscale(upscalingTexture, maskTexture);
	else
		FidelityFX::GetSingleton()->Upscale(upscalingTexture, maskTexture);

	if (GetUpscaleMode() != UpscaleMode::kFSR) {
		context->CopyResource(inputTextureResource, upscalingTexture->resource.get());

		{
			{
				ID3D11ShaderResourceView* views[1] = { inputTextureSRV };
				context->CSSetShaderResources(0, ARRAYSIZE(views), views);

				ID3D11UnorderedAccessView* uavs[1] = { upscalingTexture->uav.get() };
				context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

				context->CSSetShader(GetRCASComputeShader(), nullptr, 0);

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

	context->CopyResource(outputTextureResource, upscalingTexture->resource.get());
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

	upscalingTexture = new Texture2D(texDesc);
	upscalingTexture->CreateSRV(srvDesc);
	upscalingTexture->CreateUAV(uavDesc);

	texDesc.Format = DXGI_FORMAT_R8_UNORM;
	srvDesc.Format = texDesc.Format;
	uavDesc.Format = texDesc.Format;

	maskTexture = new Texture2D(texDesc);
	maskTexture->CreateSRV(srvDesc);
	maskTexture->CreateUAV(uavDesc);
}

void Upscaling::DestroyUpscalingResources()
{
	upscalingTexture->srv = nullptr;
	upscalingTexture->uav = nullptr;
	upscalingTexture->resource = nullptr;
	delete upscalingTexture;

	maskTexture->srv = nullptr;
	maskTexture->uav = nullptr;
	maskTexture->resource = nullptr;
	delete maskTexture;
}
