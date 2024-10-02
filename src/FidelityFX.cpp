#include "FidelityFX.h"

#include "Upscaling.h"
#include "Util.h"
#include <FidelityFX/gpu/fsr3upscaler/ffx_fsr3upscaler_resources.h>

FfxResource ffxGetResource(ID3D11Resource* dx11Resource,
	[[maybe_unused]] wchar_t const* ffxResName,
	FfxResourceStates state /*=FFX_RESOURCE_STATE_COMPUTE_READ*/)
{
	FfxResource resource = {};
	resource.resource = reinterpret_cast<void*>(const_cast<ID3D11Resource*>(dx11Resource));
	resource.state = state;
	resource.description = GetFfxResourceDescriptionDX11(dx11Resource);

#ifdef _DEBUG
	if (ffxResName) {
		wcscpy_s(resource.name, ffxResName);
	}
#endif

	return resource;
}

void FidelityFX::CreateFSRResources()
{
	static auto renderer = RE::BSGraphics::Renderer::GetSingleton();
	static auto gameViewport = RE::BSGraphics::State::GetSingleton();
	static auto context = reinterpret_cast<ID3D11DeviceContext*>(renderer->GetRuntimeData().context);
	static auto device = reinterpret_cast<ID3D11Device*>(renderer->GetRuntimeData().forwarder);

	auto fsrDevice = ffxGetDeviceDX11(device);

	size_t scratchBufferSize = ffxGetScratchMemorySizeDX11(FFX_FSR3UPSCALER_CONTEXT_COUNT);
	void* scratchBuffer = calloc(scratchBufferSize, 1);
	memset(scratchBuffer, 0, scratchBufferSize);

	FfxInterface fsrInterface;
	if (ffxGetInterfaceDX11(&fsrInterface, fsrDevice, scratchBuffer, scratchBufferSize, FFX_FSR3UPSCALER_CONTEXT_COUNT) != FFX_OK)
		logger::critical("[FidelityFX] Failed to initialize FSR3 backend interface!");

	FfxFsr3ContextDescription contextDescription;
	contextDescription.maxRenderSize.width = gameViewport->screenWidth;
	contextDescription.maxRenderSize.height = gameViewport->screenHeight;
	contextDescription.maxUpscaleSize.width = gameViewport->screenWidth;
	contextDescription.maxUpscaleSize.height = gameViewport->screenHeight;
	contextDescription.displaySize.width = gameViewport->screenWidth;
	contextDescription.displaySize.height = gameViewport->screenHeight;
	contextDescription.flags = FFX_FSR3_ENABLE_UPSCALING_ONLY | FFX_FSR3_ENABLE_AUTO_EXPOSURE;
	contextDescription.backBufferFormat = FFX_SURFACE_FORMAT_R8G8B8A8_UNORM;
	contextDescription.backendInterfaceUpscaling = fsrInterface;

	if (ffxFsr3ContextCreate(&fsrContext, &contextDescription) != FFX_OK)
		logger::critical("[FidelityFX] Failed to initialize FSR3 context!");

	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];

	D3D11_TEXTURE2D_DESC texDesc{};
	main.texture->GetDesc(&texDesc);

	texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	opaqueColor = new Texture2D(texDesc);

	texDesc.Format = DXGI_FORMAT_R8_UNORM;
	reactiveMask = new Texture2D(texDesc);
}

void FidelityFX::DestroyFSRResources()
{
	if (ffxFsr3ContextDestroy(&fsrContext) != FFX_OK)
		logger::critical("[FidelityFX] Failed to destroy FSR3 context!");
}

void FidelityFX::CopyOpaqueMask()
{
	static auto renderer = RE::BSGraphics::Renderer::GetSingleton();
	static auto context = reinterpret_cast<ID3D11DeviceContext*>(renderer->GetRuntimeData().context);
	static auto shadowState = RE::BSGraphics::RendererShadowState::GetSingleton();
	auto& preTransparencyColor = renderer->GetRuntimeData().renderTargets[shadowState->GetRuntimeData().renderTargets[0]];

	context->CopyResource(opaqueColor->resource.get(), preTransparencyColor.texture);
}

void FidelityFX::GenerateReactiveMask()
{
	static auto renderer = RE::BSGraphics::Renderer::GetSingleton();
	static auto context = reinterpret_cast<ID3D11DeviceContext*>(renderer->GetRuntimeData().context);
	
	context->OMSetRenderTargets(0, nullptr, nullptr);

	static auto shadowState = RE::BSGraphics::RendererShadowState::GetSingleton();
	auto& preUpscaleColor = renderer->GetRuntimeData().renderTargets[shadowState->GetRuntimeData().renderTargets[0]];

	FfxFsr3GenerateReactiveDescription generateReactiveParameters = {};
	generateReactiveParameters.commandList = ffxGetCommandListDX11(context);
	generateReactiveParameters.colorOpaqueOnly = ffxGetResource(opaqueColor->resource.get(), L"FSR3_Input_Opaque_Color", FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);
	generateReactiveParameters.colorPreUpscale = ffxGetResource(preUpscaleColor.texture, L"FSR3_Input_PreUpscaleColor", FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);
	generateReactiveParameters.outReactive = ffxGetResource(reactiveMask->resource.get(), L"FSR3_InputReactiveMask", FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);

	static auto gameViewport = RE::BSGraphics::State::GetSingleton();
	generateReactiveParameters.renderSize.width = gameViewport->screenWidth;
	generateReactiveParameters.renderSize.height = gameViewport->screenHeight;
	generateReactiveParameters.scale = 1.f;
	generateReactiveParameters.cutoffThreshold = 0.2f;
	generateReactiveParameters.binaryValue = 0.9f;
	generateReactiveParameters.flags = FFX_FSR3UPSCALER_AUTOREACTIVEFLAGS_APPLY_TONEMAP |
	                                   FFX_FSR3UPSCALER_AUTOREACTIVEFLAGS_APPLY_THRESHOLD |
	                                   FFX_FSR3UPSCALER_AUTOREACTIVEFLAGS_USE_COMPONENTS_MAX;

	ffxFsr3ContextGenerateReactiveMask(&fsrContext, &generateReactiveParameters); // Could maybe fail (non critical), but I have not encountered this

	shadowState->GetRuntimeData().stateUpdateFlags.set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);  // Run OMSetRenderTargets again
	Util::SetDirtyStates(false);
}

void FidelityFX::Upscale(Texture2D* a_color)
{
	static auto renderer = RE::BSGraphics::Renderer::GetSingleton();
	static auto& depthTexture = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY];
	static auto& motionVectorsTexture = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];

	static auto gameViewport = RE::BSGraphics::State::GetSingleton();
	static auto context = reinterpret_cast<ID3D11DeviceContext*>(renderer->GetRuntimeData().context);

	{
		FfxFsr3DispatchUpscaleDescription dispatchParameters{};

		dispatchParameters.commandList = ffxGetCommandListDX11(context);
		dispatchParameters.color = ffxGetResource(a_color->resource.get(), L"FSR3_Input_OutputColor", FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);
		dispatchParameters.depth = ffxGetResource(depthTexture.texture, L"FSR3_InputDepth", FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);
		dispatchParameters.motionVectors = ffxGetResource(motionVectorsTexture.texture, L"FSR3_InputMotionVectors", FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);
		dispatchParameters.exposure = ffxGetResource(nullptr, L"FSR3_InputExposure", FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);
		dispatchParameters.upscaleOutput = dispatchParameters.color;
		dispatchParameters.reactive = ffxGetResource(reactiveMask->resource.get(), L"FSR3_InputReactiveMap", FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);
		dispatchParameters.transparencyAndComposition = ffxGetResource(nullptr, L"FSR3_TransparencyAndCompositionMap", FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);

		dispatchParameters.motionVectorScale.x = (float)gameViewport->screenWidth;
		dispatchParameters.motionVectorScale.y = (float)gameViewport->screenHeight;
		dispatchParameters.renderSize.width = gameViewport->screenWidth;
		dispatchParameters.renderSize.height = gameViewport->screenHeight;
		dispatchParameters.jitterOffset.x = gameViewport->projectionPosScaleX;
		dispatchParameters.jitterOffset.y = gameViewport->projectionPosScaleY;

		static float& deltaTime = (*(float*)REL::RelocationID(523660, 410199).address());
		dispatchParameters.frameTimeDelta = deltaTime * 1000.f;

		static float& cameraNear = (*(float*)(REL::RelocationID(517032, 403540).address() + 0x40));
		static float& cameraFar = (*(float*)(REL::RelocationID(517032, 403540).address() + 0x44));
		dispatchParameters.cameraFar = cameraFar;
		dispatchParameters.cameraNear = cameraNear;

		dispatchParameters.enableSharpening = true;
		dispatchParameters.sharpness = 0.5f;

		dispatchParameters.cameraFovAngleVertical = Util::GetVerticalFOVRad();
		dispatchParameters.viewSpaceToMetersFactor = 0.01428222656f;
		dispatchParameters.reset = false;
		dispatchParameters.preExposure = 1.0f;

		dispatchParameters.flags = 0;

		if (ffxFsr3ContextDispatchUpscale(&fsrContext, &dispatchParameters) != FFX_OK)
			logger::error("[FidelityFX] Failed to dispatch upscaling!");
	}
}
