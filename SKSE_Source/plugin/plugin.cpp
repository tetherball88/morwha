#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

#include "morwha/app/SynthesisCoordinator.h"
#include "morwha/bridge_papyrus/ConsoleCommands.h"
#include "morwha/bridge_papyrus/PapyrusInterface.h"
#include "morwha/engine/EngineLogging.h"
#include "morwha/engine/Mortal3dSyncService.h"
#include "morwha/engine/SkeeOverlayClient.h"
#include "morwha/engine/SkseSerializationCallbacks.h"
#include "morwha/state/CoSaveCodec.h"

SKSEPluginInfo(
    .Version = REL::Version{ 0, 1, 0, 0 },
    .Name = "MORWHA",
    .Author = "tetherball88",
    .SupportEmail = "",
    .StructCompatibility = SKSE::StructCompatibility::Independent,
    .RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary,
    .MinimumSKSEVersion = REL::Version{ 0, 0, 0, 0 }
)

SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse);

    MORWHA::Engine::InitializeFileLogger();
    MORWHA_LOG_INFO("MORWHA plugin loading...");

    if (const auto* serialization = SKSE::GetSerializationInterface()) {
        serialization->SetUniqueID(MORWHA::State::kMorwhaPluginId);
        serialization->SetSaveCallback(MORWHA::Serialization::SaveCallback);
        serialization->SetLoadCallback(MORWHA::Serialization::LoadCallback);
        serialization->SetRevertCallback(MORWHA::Serialization::RevertCallback);
        MORWHA::Serialization::SetPostSaveLinksCallback(
            [](std::span<const MORWHA::State::SpawnedMortalLink> savedLinks) {
                MORWHA::App::SynthesisCoordinator::GetSingleton().OnGameSaved(savedLinks);
            });
        MORWHA::Serialization::SetPostRevertCallback([]() {
            MORWHA::App::SynthesisCoordinator::GetSingleton().OnSessionRevert();
        });
        MORWHA_LOG_INFO("Registered 'MRWH' SKSE serialization callbacks");
    } else {
        MORWHA_LOG_ERROR("SKSE serialization interface unavailable");
        return false;
    }

    if (const auto* messaging = SKSE::GetMessagingInterface()) {
        if (!messaging->RegisterListener([](SKSE::MessagingInterface::Message* message) {
                auto& coordinator = MORWHA::App::SynthesisCoordinator::GetSingleton();
                switch (message->type) {
                    case SKSE::MessagingInterface::kPostPostLoad:
                        MORWHA::Engine::SkeeOverlayClient::GetSingleton().ExchangeInterfacesAtPostPostLoad();
                        break;

                    case SKSE::MessagingInterface::kPreLoadGame:
                    case SKSE::MessagingInterface::kNewGame:
                        coordinator.OnSessionRevert();
                        break;

                    case SKSE::MessagingInterface::kPostLoadGame:
                        coordinator.OnPostLoadGame();
                        break;

                    case SKSE::MessagingInterface::kInputLoaded:
                        MORWHA::Engine::Mortal3dSyncService::GetSingleton().RegisterInputSink();
                        break;

                    case SKSE::MessagingInterface::kDataLoaded:
                        coordinator.OnDataLoaded();
                        (void)MORWHA::Bridge::Papyrus::RegisterConsoleCommands();
                        if (auto* console = RE::ConsoleLog::GetSingleton()) {
                            console->Print("MORWHA: Ready (type 'mspawn' or 'mspawn help')");
                        }
                        break;

                    default:
                        break;
                }
            })) {
            MORWHA_LOG_ERROR("Failed to register SKSE messaging listener");
            return false;
        }
    } else {
        MORWHA_LOG_ERROR("SKSE messaging interface unavailable");
        return false;
    }

    if (const auto* papyrus = SKSE::GetPapyrusInterface()) {
        if (!papyrus->Register(MORWHA::Bridge::Papyrus::RegisterPapyrusFunctions)) {
            MORWHA_LOG_ERROR("Failed to register MORWHA Papyrus bindings");
            return false;
        }
    } else {
        MORWHA_LOG_ERROR("SKSE Papyrus interface unavailable");
        return false;
    }

    return true;
}
