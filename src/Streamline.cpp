#include "Streamline.h"

#include <magic_enum.hpp>

#include "Util.h"

void Streamline::LoadInterposer()
{
	interposer = LoadLibraryW(L"Data/SKSE/Plugins/ENBAntiAliasing/Streamline/sl.interposer.dll");
	if (interposer == nullptr) {
		DWORD errorCode = GetLastError();
		logger::info("[Streamline] Failed to load interposer: Error Code {0:x}", errorCode);
	} else {
		logger::info("[Streamline] Interposer loaded at address: {0:p}", static_cast<void*>(interposer));
	}
}

void Streamline::Initialize()
{
	logger::info("[Streamline] Initializing Streamline");

	sl::Preferences pref;

	sl::Feature featuresToLoad[] = { sl::kFeatureDLSS };
	pref.featuresToLoad = featuresToLoad;
	pref.numFeaturesToLoad = _countof(featuresToLoad);

	pref.logLevel = sl::LogLevel::eOff;
	pref.logMessageCallback = nullptr;
	pref.showConsole = false;

	pref.engine = sl::EngineType::eCustom;
	pref.engineVersion = "1.0.0";
	pref.projectId = "f8776929-c969-43bd-ac2b-294b4de58aac";
	pref.flags |= sl::PreferenceFlags::eUseManualHooking;

	pref.renderAPI = sl::RenderAPI::eD3D11;

	// Hook up all of the functions exported by the SL Interposer Library
	slInit = (PFun_slInit*)GetProcAddress(interposer, "slInit");
	slShutdown = (PFun_slShutdown*)GetProcAddress(interposer, "slShutdown");
	slIsFeatureSupported = (PFun_slIsFeatureSupported*)GetProcAddress(interposer, "slIsFeatureSupported");
	slIsFeatureLoaded = (PFun_slIsFeatureLoaded*)GetProcAddress(interposer, "slIsFeatureLoaded");
	slSetFeatureLoaded = (PFun_slSetFeatureLoaded*)GetProcAddress(interposer, "slSetFeatureLoaded");
	slEvaluateFeature = (PFun_slEvaluateFeature*)GetProcAddress(interposer, "slEvaluateFeature");
	slAllocateResources = (PFun_slAllocateResources*)GetProcAddress(interposer, "slAllocateResources");
	slFreeResources = (PFun_slFreeResources*)GetProcAddress(interposer, "slFreeResources");
	slSetTag = (PFun_slSetTag*)GetProcAddress(interposer, "slSetTag");
	slGetFeatureRequirements = (PFun_slGetFeatureRequirements*)GetProcAddress(interposer, "slGetFeatureRequirements");
	slGetFeatureVersion = (PFun_slGetFeatureVersion*)GetProcAddress(interposer, "slGetFeatureVersion");
	slUpgradeInterface = (PFun_slUpgradeInterface*)GetProcAddress(interposer, "slUpgradeInterface");
	slSetConstants = (PFun_slSetConstants*)GetProcAddress(interposer, "slSetConstants");
	slGetNativeInterface = (PFun_slGetNativeInterface*)GetProcAddress(interposer, "slGetNativeInterface");
	slGetFeatureFunction = (PFun_slGetFeatureFunction*)GetProcAddress(interposer, "slGetFeatureFunction");
	slGetNewFrameToken = (PFun_slGetNewFrameToken*)GetProcAddress(interposer, "slGetNewFrameToken");
	slSetD3DDevice = (PFun_slSetD3DDevice*)GetProcAddress(interposer, "slSetD3DDevice");

	if (SL_FAILED(res, slInit(pref, sl::kSDKVersion))) {
		logger::critical("[Streamline] Failed to initialize Streamline");
	} else {
		initialized = true;
		logger::info("[Streamline] Sucessfully initialized Streamline");
	}
}

extern decltype(&D3D11CreateDeviceAndSwapChain) ptrD3D11CreateDeviceAndSwapChain;

HRESULT Streamline::CreateDeviceAndSwapChain(IDXGIAdapter* pAdapter,
	D3D_DRIVER_TYPE DriverType,
	HMODULE Software,
	UINT Flags,
	const D3D_FEATURE_LEVEL* pFeatureLevels,
	UINT FeatureLevels,
	UINT SDKVersion,
	const DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
	IDXGISwapChain** ppSwapChain,
	ID3D11Device** ppDevice,
	D3D_FEATURE_LEVEL* pFeatureLevel,
	ID3D11DeviceContext** ppImmediateContext)
{
	if (!initialized)
		Initialize();

	DXGI_ADAPTER_DESC adapterDesc;
	pAdapter->GetDesc(&adapterDesc);

	sl::AdapterInfo adapterInfo;
	adapterInfo.deviceLUID = (uint8_t*)&adapterDesc.AdapterLuid;
	adapterInfo.deviceLUIDSizeInBytes = sizeof(LUID);

	slIsFeatureLoaded(sl::kFeatureDLSS, featureDLSS);
	if (featureDLSS) {
		logger::info("[Streamline] DLSS feature is loaded");
		featureDLSS = slIsFeatureSupported(sl::kFeatureDLSS, adapterInfo) == sl::Result::eOk;
	} else {
		logger::info("[Streamline] DLSS feature is not loaded");
		sl::FeatureRequirements featureRequirements;
		sl::Result result = slGetFeatureRequirements(sl::kFeatureDLSS, featureRequirements);
		if (result != sl::Result::eOk) {
			logger::info("[Streamline] DLSS feature failed to load due to: {}", magic_enum::enum_name(result));
		}
	}

	logger::info("[Streamline] DLSS {} available", featureDLSS ? "is" : "is not");

	HRESULT hr = ptrD3D11CreateDeviceAndSwapChain(
			pAdapter,
			DriverType,
			Software,
			Flags,
			pFeatureLevels,
			FeatureLevels,
			SDKVersion,
			pSwapChainDesc,
			ppSwapChain,
			ppDevice,
			pFeatureLevel,
			ppImmediateContext);

	slSetD3DDevice(*ppDevice);

	if (featureDLSS) {
		slGetFeatureFunction(sl::kFeatureDLSS, "slDLSSGetOptimalSettings", (void*&)slDLSSGetOptimalSettings);
		slGetFeatureFunction(sl::kFeatureDLSS, "slDLSSGetState", (void*&)slDLSSGetState);
		slGetFeatureFunction(sl::kFeatureDLSS, "slDLSSSetOptions", (void*&)slDLSSSetOptions);
	}

	return hr;
}

void Streamline::Upscale(Texture2D* a_upscaleTexture, Texture2D* a_mask)
{
	UpdateConstants();

	static auto renderer = RE::BSGraphics::Renderer::GetSingleton();
	static auto& depthTexture = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY];
	static auto& motionVectorsTexture = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMOTION_VECTOR];

	static auto gameViewport = RE::BSGraphics::State::GetSingleton();
	static auto context = reinterpret_cast<ID3D11DeviceContext*>(renderer->GetRuntimeData().context);

	{
		sl::DLSSOptions dlssOptions{};
		dlssOptions.mode = sl::DLSSMode::eMaxQuality;
		dlssOptions.outputWidth = gameViewport->screenWidth;
		dlssOptions.outputHeight = gameViewport->screenHeight;
		dlssOptions.colorBuffersHDR = sl::Boolean::eFalse;
		dlssOptions.preExposure = 1.0f;
		dlssOptions.sharpness = 0.0f;

		if (SL_FAILED(result, slDLSSSetOptions(viewport, dlssOptions))) {
			logger::critical("[Streamline] Could not enable DLSS");
		}
	}

	{
		sl::Extent fullExtent{ 0, 0, gameViewport->screenWidth, gameViewport->screenHeight };

		sl::Resource colorIn = { sl::ResourceType::eTex2d, a_upscaleTexture->resource.get(), 0 };
		sl::Resource colorOut = { sl::ResourceType::eTex2d, a_upscaleTexture->resource.get(), 0 };
		sl::Resource depth = { sl::ResourceType::eTex2d, depthTexture.texture, 0 };
		sl::Resource mvec = { sl::ResourceType::eTex2d, motionVectorsTexture.texture, 0 };

		sl::ResourceTag colorInTag = sl::ResourceTag{ &colorIn, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eOnlyValidNow, &fullExtent };
		sl::ResourceTag colorOutTag = sl::ResourceTag{ &colorOut, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eOnlyValidNow, &fullExtent };
		sl::ResourceTag depthTag = sl::ResourceTag{ &depth, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilPresent, &fullExtent };
		sl::ResourceTag mvecTag = sl::ResourceTag{ &mvec, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilPresent, &fullExtent };

		sl::Resource transparencyMask = { sl::ResourceType::eTex2d, a_mask->resource.get(), 0 };
		sl::ResourceTag transparencyMaskTag = sl::ResourceTag{ &transparencyMask, sl::kBufferTypeTransparencyHint, sl::ResourceLifecycle::eValidUntilPresent, &fullExtent };

		sl::ResourceTag resourceTags[] = { colorInTag, colorOutTag, depthTag, mvecTag, transparencyMaskTag };
		slSetTag(viewport, resourceTags, _countof(resourceTags), context);
	}

	sl::ViewportHandle view(viewport);
	const sl::BaseStructure* inputs[] = { &view };
	slEvaluateFeature(sl::kFeatureDLSS, *frameToken, inputs, _countof(inputs), context);
}

void Streamline::UpdateConstants()
{
	auto cameraData = Util::GetCameraData(0);
	auto eyePosition = Util::GetEyePosition(0);

	auto clipToCameraView = cameraData.viewMat.Invert();
	auto cameraToWorld = cameraData.viewProjMatrixUnjittered.Invert();
	auto cameraToWorldPrev = cameraData.previousViewProjMatrixUnjittered.Invert();

	auto gameViewport = RE::BSGraphics::State::GetSingleton();

	float4x4 cameraToPrevCamera;

	calcCameraToPrevCamera(*(sl::float4x4*)&cameraToPrevCamera, *(sl::float4x4*)&cameraToWorld, *(sl::float4x4*)&cameraToWorldPrev);

	float4x4 prevCameraToCamera = cameraToPrevCamera;

	prevCameraToCamera.Invert();

	sl::Constants slConstants = {};
	slConstants.cameraAspectRatio = (float)gameViewport->screenWidth / (float)gameViewport->screenHeight;
	slConstants.cameraFOV = Util::GetVerticalFOVRad();
	slConstants.cameraFar = (*(float*)(REL::RelocationID(517032, 403540).address() + 0x44));
	slConstants.cameraMotionIncluded = sl::Boolean::eTrue;
	slConstants.cameraNear = (*(float*)(REL::RelocationID(517032, 403540).address() + 0x40));
	slConstants.cameraPinholeOffset = { 0.f, 0.f };
	slConstants.cameraPos = *(sl::float3*)&eyePosition;
	slConstants.cameraFwd = *(sl::float3*)&cameraData.viewForward;
	slConstants.cameraUp = *(sl::float3*)&cameraData.viewUp;
	slConstants.cameraRight = *(sl::float3*)&cameraData.viewRight;
	slConstants.cameraViewToClip = *(sl::float4x4*)&cameraData.viewMat;
	slConstants.clipToCameraView = *(sl::float4x4*)&clipToCameraView;
	slConstants.clipToPrevClip = *(sl::float4x4*)&cameraToPrevCamera;
	slConstants.depthInverted = sl::Boolean::eFalse;
	slConstants.jitterOffset = { gameViewport->projectionPosScaleX, gameViewport->projectionPosScaleY };
	slConstants.mvecScale = { 1, 1 };
	slConstants.prevClipToClip = *(sl::float4x4*)&prevCameraToCamera;
	slConstants.reset = sl::Boolean::eFalse;
	slConstants.motionVectors3D = sl::Boolean::eTrue;
	slConstants.motionVectorsInvalidValue = FLT_MIN;
	slConstants.orthographicProjection = sl::Boolean::eFalse;
	slConstants.motionVectorsDilated = sl::Boolean::eFalse;
	slConstants.motionVectorsJittered = sl::Boolean::eFalse;

	if (SL_FAILED(res, slGetNewFrameToken(frameToken, nullptr))) {
		logger::error("[Streamline] Could not get frame token");
	}

	if (SL_FAILED(res, slSetConstants(slConstants, *frameToken, viewport))) {
		logger::error("[Streamline] Could not set constants");
	}
}

void Streamline::DestroyDLSSResources()
{
	sl::DLSSOptions dlssOptions{};
	dlssOptions.mode = sl::DLSSMode::eOff;
	slDLSSSetOptions(viewport, dlssOptions);
	slFreeResources(sl::kFeatureDLSS, viewport);
}