#include "morwha/engine/SkseSerializationCallbacks.h"

#include "morwha/engine/EngineLogging.h"
#include "morwha/state/CoSaveCodec.h"
#include "morwha/state/SessionEpoch.h"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

namespace MORWHA::Serialization {

    namespace {

        std::mutex& LinksMutex() noexcept {
            static std::mutex s_mutex;
            return s_mutex;
        }

        std::vector<State::SpawnedMortalLink>& ActiveLinks() noexcept {
            static std::vector<State::SpawnedMortalLink> s_links;
            return s_links;
        }

        std::atomic<PostSaveLinksCallback>& PostSaveHookSlot() noexcept {
            static std::atomic<PostSaveLinksCallback> s_hook{ nullptr };
            return s_hook;
        }

        std::atomic<PostRevertCallback>& PostRevertHookSlot() noexcept {
            static std::atomic<PostRevertCallback> s_hook{ nullptr };
            return s_hook;
        }

    }  // namespace

    void SetPostSaveLinksCallback(PostSaveLinksCallback callback) noexcept {
        PostSaveHookSlot().store(callback, std::memory_order_release);
    }

    void SetPostRevertCallback(PostRevertCallback callback) noexcept {
        PostRevertHookSlot().store(callback, std::memory_order_release);
    }

    void SaveCallback(SKSE::SerializationInterface* serialization) {
        if (!serialization) {
            return;
        }

        std::vector<State::SpawnedMortalLink> snapshot;
        {
            std::lock_guard lock(LinksMutex());
            snapshot = ActiveLinks();
        }

        if (!serialization->OpenRecord(State::kLinksChunkId, State::kCoSaveFormatVersion)) {
            MORWHA_LOG_ERROR("SaveCallback: Failed to open 'LNKS' co-save record");
            return;
        }

        const auto bytes = State::EncodeSpawnedLinks(snapshot);
        if (!bytes.empty() && !serialization->WriteRecordData(bytes.data(), static_cast<std::uint32_t>(bytes.size()))) {
            MORWHA_LOG_ERROR("SaveCallback: Failed to write {} bytes to 'LNKS' record", bytes.size());
            return;
        }
        if (const auto hook = PostSaveHookSlot().load(std::memory_order_acquire)) {
            hook(snapshot);
        }
        MORWHA_LOG_INFO("SaveCallback: Saved {} dynamic mortal link(s) to 'MRWH' co-save", snapshot.size());
    }

    void LoadCallback(SKSE::SerializationInterface* serialization) {
        if (!serialization) {
            return;
        }

        std::vector<State::SpawnedMortalLink> loadedLinks;
        std::uint32_t type = 0;
        std::uint32_t version = 0;
        std::uint32_t length = 0;

        while (serialization->GetNextRecordInfo(type, version, length)) {
            if (type != State::kLinksChunkId) {
                continue;
            }
            if (version != State::kCoSaveFormatVersion ||
                length > (16u * 1024u * 1024u)) {
                MORWHA_LOG_WARN("LoadCallback: Unsupported 'LNKS' version={} or length={}", version, length);
                continue;
            }

            std::vector<std::uint8_t> payload(length);
            if (length > 0 && !serialization->ReadRecordData(payload.data(), length)) {
                MORWHA_LOG_ERROR("LoadCallback: Truncated 'LNKS' payload (expected {} bytes)", length);
                RevertCallback(serialization);
                return;
            }

            if (!State::DecodeSpawnedLinks(payload, loadedLinks)) {
                MORWHA_LOG_ERROR("LoadCallback: Malformed 'LNKS' payload; reverting co-save state");
                RevertCallback(serialization);
                return;
            }

            auto resolveOrClear = [&](Foundation::FormRawId& id, std::string_view fieldName, std::string_view slotKey) {
                if (id.IsNull()) {
                    return;
                }
                RE::FormID resolved = 0;
                if (serialization->ResolveFormID(id.raw(), resolved)) {
                    id = Foundation::FormRawId{ resolved };
                } else {
                    MORWHA_LOG_WARN(
                        "LoadCallback: Failed to resolve {} {:08X} for key '{}'",
                        fieldName,
                        id.raw(),
                        slotKey);
                    id = Foundation::FormRawId{ 0 };
                }
            };

            for (auto& link : loadedLinks) {
                resolveOrClear(link.actorRefId, "actorRefId", link.slotKey);
                resolveOrClear(link.donorARefId, "donorARefId", link.slotKey);
                resolveOrClear(link.donorBRefId, "donorBRefId", link.slotKey);
            }
        }

        const std::size_t restoredCount = loadedLinks.size();
        {
            std::lock_guard lock(LinksMutex());
            ActiveLinks() = std::move(loadedLinks);
        }
        MORWHA_LOG_INFO("LoadCallback: Restored {} dynamic mortal link(s) from 'MRWH' co-save",
                        restoredCount);
    }

    void RevertCallback(SKSE::SerializationInterface*) {
        State::BumpSessionEpoch("Serialization::RevertCallback");
        {
            std::lock_guard lock(LinksMutex());
            ActiveLinks().clear();
        }
        if (const auto hook = PostRevertHookSlot().load(std::memory_order_acquire)) {
            hook();
        }
        MORWHA_LOG_INFO("RevertCallback: Cleared MORWHA co-save state");
    }

    void RegisterSpawnedLink(State::SpawnedMortalLink link) {
        std::lock_guard lock(LinksMutex());
        auto& links = ActiveLinks();
        const auto it = std::ranges::find_if(links, [&](const auto& existing) {
            return existing.actorRefId == link.actorRefId;
        });
        if (it != links.end()) {
            *it = std::move(link);
        } else {
            links.push_back(std::move(link));
        }
    }

    void UnregisterSpawnedLink(Foundation::FormRawId actorRefId) {
        if (actorRefId.IsNull()) {
            return;
        }
        std::lock_guard lock(LinksMutex());
        auto& links = ActiveLinks();
        std::erase_if(links, [&](const State::SpawnedMortalLink& existing) {
            return existing.actorRefId == actorRefId;
        });
    }

    std::optional<State::SpawnedMortalLink> FindActiveLinkByActor(Foundation::FormRawId actorRefId) {
        if (actorRefId.IsNull()) {
            return std::nullopt;
        }
        std::lock_guard lock(LinksMutex());
        const auto& links = ActiveLinks();
        const auto it = std::ranges::find_if(links, [&](const auto& existing) {
            return existing.actorRefId == actorRefId;
        });
        if (it == links.end()) {
            return std::nullopt;
        }
        return *it;
    }

    std::optional<State::SpawnedMortalLink> FindActiveLinkBySlotKey(std::string_view slotKey) {
        if (slotKey.empty()) {
            return std::nullopt;
        }
        std::lock_guard lock(LinksMutex());
        const auto& links = ActiveLinks();
        const auto it = std::ranges::find_if(links, [&](const auto& existing) {
            return existing.slotKey == slotKey;
        });
        if (it == links.end()) {
            return std::nullopt;
        }
        return *it;
    }

    std::vector<State::SpawnedMortalLink> GetActiveLinksSnapshot() {
        std::lock_guard lock(LinksMutex());
        return ActiveLinks();
    }

    std::vector<State::SpawnedMortalLink> ReconcileLinksOnPostLoad() {
        std::lock_guard lock(LinksMutex());
        auto& links = ActiveLinks();
        std::erase_if(links, [](const State::SpawnedMortalLink& link) {
            if (link.actorRefId.IsNull()) {
                return true;
            }
            if (auto* form = RE::TESForm::LookupByID(link.actorRefId.raw())) {
                return !form->IsActor() || form->IsDeleted();
            }
            return false;
        });
        return links;
    }

    std::vector<std::filesystem::path> ResolveActiveSaveDirectories() {
        std::vector<std::filesystem::path> dirs;
        auto logDirOpt = SKSE::log::log_directory();
        if (!logDirOpt) {
            return dirs;
        }

        std::filesystem::path skseLogDir = *logDirOpt;
        std::error_code ec;
        if (!std::filesystem::is_directory(skseLogDir, ec)) {
            skseLogDir = skseLogDir.parent_path();
        }
        const std::filesystem::path myGamesSkyrimDir = skseLogDir.parent_path();
        if (myGamesSkyrimDir.empty()) {
            return dirs;
        }

        std::string localSaveSubpath = "Saves\\";
        if (auto* ini = RE::INISettingCollection::GetSingleton()) {
            if (const auto* setting = ini->GetSetting("sLocalSavePath:General")) {
                if (const char* val = setting->GetString(); val != nullptr && val[0] != '\0') {
                    localSaveSubpath = val;
                }
            }
        }

        const std::filesystem::path configuredRelOrAbs(localSaveSubpath);
        const std::filesystem::path primaryDir = configuredRelOrAbs.is_absolute()
                                                     ? configuredRelOrAbs
                                                     : (myGamesSkyrimDir / configuredRelOrAbs);
        dirs.push_back(primaryDir);
        dirs.push_back(myGamesSkyrimDir / "Saves");
        return dirs;
    }

}  // namespace MORWHA::Serialization
