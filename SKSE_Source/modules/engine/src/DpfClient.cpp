#include "morwha/engine/DpfClient.h"

#include "morwha/engine/EngineLogging.h"

#include "DPFAPI.h"

#include <Windows.h>
#include <string>

namespace MORWHA::Engine {

    DpfClient& DpfClient::GetSingleton() noexcept {
        static DpfClient instance;
        return instance;
    }

    void DpfClient::InitializeAtDataLoaded() noexcept {
        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        const bool isLightDpf =
            (dataHandler != nullptr) && (dataHandler->LookupLoadedLightModByName("DPF.esp") != nullptr);
        const bool isFullDpf =
            (dataHandler != nullptr) && (dataHandler->LookupLoadedModByName("DPF.esp") != nullptr);

        faceGenFixesLoaded_ = (::GetModuleHandleW(L"FaceGenFixes.dll") != nullptr);
        smpLoaded_ = (::GetModuleHandleW(L"hdtSMP64.dll") != nullptr);

        if (isLightDpf) {
            dpfAvailable_ = false;
            MORWHA_LOG_ERROR(
                "DpfClient: DPF.esp is light/ESL-flagged; DPF RE requires an uncompacted full plugin slot");
        } else if (!isFullDpf) {
            dpfAvailable_ = false;
            MORWHA_LOG_WARN("DpfClient: DPF.esp not found in load order; persistent slot spawning disabled");
        } else {
            auto* api = DPF::GetAPI();
            dpfAvailable_ = (api != nullptr);
            if (dpfAvailable_) {
                MORWHA_LOG_INFO(
                    "DpfClient: DPF RE API v{} ready (FaceGenFixes.dll={}, hdtSMP64.dll={})",
                    api->GetVersion(),
                    faceGenFixesLoaded_ ? "loaded" : "missing",
                    smpLoaded_ ? "loaded" : "missing");
            } else {
                MORWHA_LOG_ERROR(
                    "DpfClient: DPF.esp loaded, but DPF.dll C++ API v{} is unavailable",
                    DPF::InterfaceVersion);
            }
        }

        if (!faceGenFixesLoaded_) {
            MORWHA_LOG_WARN(
                "DpfClient: FaceGenFixes.dll (Face Discoloration Fix) not detected; runtime heads may darken");
        }
    }

    std::optional<DpfSlotResult> DpfClient::AcquireOrRecoverNpcSlot(std::string_view slotKey) const noexcept {
        if (!dpfAvailable_) {
            MORWHA_LOG_ERROR("DpfClient: AcquireOrRecoverNpcSlot('{}') rejected; DPF unavailable", slotKey);
            return std::nullopt;
        }
        if (slotKey.empty()) {
            MORWHA_LOG_ERROR("DpfClient: AcquireOrRecoverNpcSlot rejected empty slotKey");
            return std::nullopt;
        }

        auto* api = DPF::GetAPI();
        if (!api) {
            MORWHA_LOG_ERROR("DpfClient: DPF::GetAPI() returned null for slotKey '{}'", slotKey);
            return std::nullopt;
        }

        const std::string keyStr(slotKey);
        std::uint32_t pluginNumber = 0;
        std::uint32_t localId = 0;
        bool existed = false;

        RE::TESForm* form = api->GetOrCreateByOwnerKey(
            "MORWHA",
            keyStr.c_str(),
            static_cast<std::uint32_t>(RE::FormType::NPC),
            &pluginNumber,
            &localId,
            &existed);
        if (!form) {
            MORWHA_LOG_ERROR("DpfClient: GetOrCreateByOwnerKey returned null for slotKey '{}'", slotKey);
            return std::nullopt;
        }

        auto* npc = form->As<RE::TESNPC>();
        if (!npc) {
            MORWHA_LOG_ERROR(
                "DpfClient: Form {:08X} for slotKey '{}' is not TESNPC (type={})",
                form->GetFormID(),
                slotKey,
                static_cast<std::uint32_t>(form->GetFormType()));
            return std::nullopt;
        }

        MORWHA_LOG_DEBUG(
            "DpfClient: Acquired slot '{}' -> {:08X} ({}:{:06X}, existed={})",
            slotKey,
            npc->GetFormID(),
            DPF::PluginNameForNumber(pluginNumber),
            localId,
            existed);

        return DpfSlotResult{
            .npc = npc,
            .pluginNumber = pluginNumber,
            .localId = localId,
            .existed = existed
        };
    }

    bool DpfClient::ReleaseNpcSlot(std::string_view slotKey) const noexcept {
        if (!dpfAvailable_ || slotKey.empty()) {
            return false;
        }
        auto* api = DPF::GetAPI();
        if (!api) {
            return false;
        }
        const std::string keyStr(slotKey);
        const bool released = api->ReleaseByOwnerKey("MORWHA", keyStr.c_str());
        if (released) {
            MORWHA_LOG_DEBUG("DpfClient: Released DPF slot '{}'", slotKey);
        }
        return released;
    }

    std::uint32_t DpfClient::ReleaseAllMorwhaSlots() const noexcept {
        if (!dpfAvailable_) {
            return 0u;
        }
        auto* api = DPF::GetAPI();
        if (!api) {
            return 0u;
        }
        const std::uint32_t count = api->ReleaseOwner("MORWHA");
        if (count > 0u) {
            MORWHA_LOG_INFO("DpfClient: Released {} total 'MORWHA' slot(s) from DPF global cache", count);
        }
        return count;
    }

}  // namespace MORWHA::Engine
