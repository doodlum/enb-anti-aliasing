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

	g_ENB->TwAddVarRW(generalBar, "Method", aaType, streamline->featureDLSS ? &settings.upscaleMethod : &settings.upscaleMethodNoDLSS, "group='ANTIALIASING'");
}

Upscaling::UpscaleMethod Upscaling::GetUpscaleMethod()
{
	auto streamline = Streamline::GetSingleton();
	return streamline->featureDLSS ? (UpscaleMethod)settings.upscaleMethod : (UpscaleMethod)settings.upscaleMethodNoDLSS;
}

void Upscaling::CheckResources()
{
	static auto previousUpscaleMode = UpscaleMethod::kTAA;
	auto currentUpscaleMode = GetUpscaleMethod();

	auto streamline = Streamline::GetSingleton();
	auto fidelityFX = FidelityFX::GetSingleton();

	if (previousUpscaleMode != currentUpscaleMode) {
		if (previousUpscaleMode == UpscaleMethod::kTAA)
			CreateUpscalingResources();
		else if (previousUpscaleMode == UpscaleMethod::kDLSS)
			streamline->DestroyDLSSResources();
		else if (previousUpscaleMode == UpscaleMethod::kFSR)
			fidelityFX->DestroyFSRResources();

		if (currentUpscaleMode == UpscaleMethod::kTAA)
			DestroyUpscalingResources();
		else if (previousUpscaleMode == UpscaleMethod::kFSR)
			fidelityFX->DestroyFSRResources();
		else if (currentUpscaleMode == UpscaleMethod::kFSR)
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
		logger::debug("Compiling EncodeMaskCS.hlsl");
		encodeMaskCS = (ID3D11ComputeShader*)Util::CompileShader(L"Data/SKSE/Plugins/ENBAntiAliasing/EncodeMaskCS.hlsl", "cs_5_0");
	}
	return encodeMaskCS;
}

ID3D11ComputeShader* Upscaling::GetEncodeMaskFSRComputeShader()
{
	if (!encodeMaskFSRCS) {
		logger::debug("Compiling EncodeMaskFSRCS.hlsl");
		encodeMaskFSRCS = (ID3D11ComputeShader*)Util::CompileShader(L"Data/SKSE/Plugins/ENBAntiAliasing/EncodeMaskFSRCS.hlsl", "cs_5_0");
	}
	return encodeMaskFSRCS;
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

	auto upscaleMethod = GetUpscaleMethod();

	{	
		static auto& temporalAAMask = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kTEMPORAL_AA_MASK];

		{
			ID3D11ShaderResourceView* views[1] = { temporalAAMask.SRV };
			context->CSSetShaderResources(0, ARRAYSIZE(views), views);

			ID3D11UnorderedAccessView* uavs[1] = { maskTexture->uav.get() };
			context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

			context->CSSetShader(upscaleMethod == UpscaleMethod::kDLSS ? GetEncodeMaskComputeShader() : GetEncodeMaskFSRComputeShader(), nullptr, 0);

			context->Dispatch(dispatchX, dispatchY, 1);
		}

		ID3D11ShaderResourceView* views[1] = { nullptr };
		context->CSSetShaderResources(0, ARRAYSIZE(views), views);

		ID3D11UnorderedAccessView* uavs[1] = { nullptr };
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);

		ID3D11ComputeShader* shader = nullptr;
		context->CSSetShader(shader, nullptr, 0);
	}

	if (upscaleMethod == UpscaleMethod::kDLSS)
		Streamline::GetSingleton()->Upscale(upscalingTexture, maskTexture, exposureTexture);
	else
		FidelityFX::GetSingleton()->Upscale(upscalingTexture, maskTexture, exposureTexture);

	if (GetUpscaleMethod() != UpscaleMethod::kFSR) {
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

	texDesc.Width = 1;
	texDesc.Height = 1;

	exposureTexture = new Texture2D(texDesc);
	exposureTexture->CreateSRV(srvDesc);
	exposureTexture->CreateUAV(uavDesc);

	static auto context = reinterpret_cast<ID3D11DeviceContext*>(renderer->GetRuntimeData().context);

	float clearColor[4] = { 1, 1, 1, 1 };
	context->ClearUnorderedAccessViewFloat(exposureTexture->uav.get(), clearColor);
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

	exposureTexture->srv = nullptr;
	exposureTexture->uav = nullptr;
	exposureTexture->resource = nullptr;
	delete exposureTexture;
}
