
#include <ENB/ENBSeriesAPI.h>

#include "Hooks.h"
#include "Upscaling.h"

#define DLLEXPORT __declspec(dllexport)

std::list<std::string> errors;

bool Load();

void InitializeLog([[maybe_unused]] spdlog::level::level_enum a_level = spdlog::level::info)
{
#ifndef NDEBUG
	auto sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
#else
	auto path = logger::log_directory();
	if (!path) {
		util::report_and_fail("Failed to find standard logging directory"sv);
	}

	*path /= std::format("{}.log"sv, Plugin::NAME);
	auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
#endif

#ifndef NDEBUG
	const auto level = spdlog::level::trace;
#else
	const auto level = a_level;
#endif

	auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));
	log->set_level(level);
	log->flush_on(spdlog::level::info);

	spdlog::set_default_logger(std::move(log));
	spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] [%s:%#] %v");
}

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* a_skse)
{
#ifndef NDEBUG
	while (!REX::W32::IsDebuggerPresent()) {};
#endif
	InitializeLog();
	logger::info("Loaded {} {}", Plugin::NAME, Plugin::VERSION.string());
	SKSE::Init(a_skse);
	return Load();
}

extern "C" DLLEXPORT constinit auto SKSEPlugin_Version = []() noexcept {
	SKSE::PluginVersionData v;
	v.PluginName(Plugin::NAME.data());
	v.PluginVersion(Plugin::VERSION);
	v.UsesAddressLibrary(true);
	v.HasNoStructUse();
	return v;
}();

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Query(const SKSE::QueryInterface*, SKSE::PluginInfo* pluginInfo)
{
	pluginInfo->name = SKSEPlugin_Version.pluginName;
	pluginInfo->infoVersion = SKSE::PluginInfo::kVersion;
	pluginInfo->version = SKSEPlugin_Version.pluginVersion;
	return true;
}

ENB_API::ENBSDKALT1001* g_ENB = nullptr;

bool Load()
{
	if (REL::Module::IsVR()) {
		logger::info("Skyrim VR detected, disabling all hooks and features");
		return true;
	}

	g_ENB = reinterpret_cast<ENB_API::ENBSDKALT1001*>(ENB_API::RequestENBAPI(ENB_API::SDKVersion::V1001));

	if (g_ENB) {
		logger::info("Obtained ENB API, installing hooks");

		g_ENB->SetCallbackFunction([](ENBCallbackType calltype) {
			switch (calltype) {
			case ENBCallbackType::ENBCallback_PostLoad:
				Upscaling::GetSingleton()->RefreshUI();
				Upscaling::GetSingleton()->LoadINI();
				break;
			case ENBCallbackType::ENBCallback_PostReset:
				Upscaling::GetSingleton()->RefreshUI();
				break;
			case ENBCallbackType::ENBCallback_PreSave:
				Upscaling::GetSingleton()->SaveINI();
				break;
			}
		});

		Hooks::InstallD3DHooks();
		Upscaling::InstallHooks();
	} else {
		logger::info("Unable to acquire ENB API, disabling hooks");
	}

	return true;
}