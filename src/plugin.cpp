#include "log.h"
#include "Config.h"
#include "InputManager.h"
#include "higgsinterface001.h"
#include "ClimbManager.h"
#include "CriticalStrikeManager.h"
#include "ClimbingDamageManager.h"
#include "MenuChecker.h"


void MessageHandler(SKSE::MessagingInterface::Message* a_msg)
{
	switch (a_msg->type) {
	case SKSE::MessagingInterface::kPostLoad:
		spdlog::info("PostLoad");
		break;

	case SKSE::MessagingInterface::kPostPostLoad:
		spdlog::info("PostPostLoad - Getting HIGGS interface");
		{
			auto* messaging = SKSE::GetMessagingInterface();
			HiggsPluginAPI::GetHiggsInterface001(messaging);

			if (g_higgsInterface) {
				spdlog::info("Got HIGGS interface! Build number: {}", g_higgsInterface->GetBuildNumber());
			} else {
				spdlog::error("Failed to get HIGGS interface - is HIGGS installed?");
			}
		}
		break;

	case SKSE::MessagingInterface::kDataLoaded:
		spdlog::info("DataLoaded - Initializing managers");

		// Initialize InputManager first (needs OpenVR hook API)
		InputManager::GetSingleton()->Initialize();

		// Initialize ClimbManager (needs InputManager)
		ClimbManager::GetSingleton()->Initialize();

		// Register CriticalStrikeManager for hit events
		CriticalStrikeManager::GetSingleton()->RegisterEventSink();

		// Register ClimbingDamageManager for hit events (grip release on damage)
		ClimbingDamageManager::GetSingleton()->RegisterEventSink();

		// Register MenuChecker for menu open/close events
		MenuChecker::RegisterEventSink();
		break;

	case SKSE::MessagingInterface::kPreLoadGame:
		break;

	case SKSE::MessagingInterface::kPostLoadGame:
		spdlog::info("VRClimbing: PostLoadGame - Enabling world collision for HIGGS hands");
		if (g_higgsInterface) {
			// Enable world collision for HIGGS hands
			// COL_LAYER::kStatic (1) = Static geometry (walls, floors, architecture)
			// COL_LAYER::kTerrain (13) = Terrain (ground)


			RE::DebugNotification("VRClimbing: World collision enabled");
		} else {
			spdlog::error("VRClimbing: Cannot enable world collision - HIGGS interface not available");
		}
		break;

	case SKSE::MessagingInterface::kNewGame:
		break;
	}
}

SKSEPluginLoad(const SKSE::LoadInterface *skse) {
	SKSE::Init(skse);
	SetupLog();

	spdlog::info("VRClimbing loading...");

	// Install main thread hook early (before other initialization)
	if (!ClimbManager::InstallMainThreadHook()) {
		spdlog::error("Failed to install main thread hook");
		return false;
	}

	// Load configuration from INI (creates default if not found)
	Config::ReadConfigOptions();

	auto messaging = SKSE::GetMessagingInterface();
	if (!messaging->RegisterListener("SKSE", MessageHandler)) {
		spdlog::error("Failed to register SKSE message listener");
		return false;
	}

	spdlog::info("VRClimbing loaded successfully");
	return true;
}