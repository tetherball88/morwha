#pragma once

#include "morwha/state/RegistryTypes.h"

#include <filesystem>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace SKSE {
    class SerializationInterface;
}

namespace MORWHA::Serialization {

    using PostSaveLinksCallback = void (*)(std::span<const State::SpawnedMortalLink> savedLinks);
    using PostRevertCallback = void (*)();

    void SetPostSaveLinksCallback(PostSaveLinksCallback callback) noexcept;
    void SetPostRevertCallback(PostRevertCallback callback) noexcept;

    void SaveCallback(SKSE::SerializationInterface* serialization);
    void LoadCallback(SKSE::SerializationInterface* serialization);
    void RevertCallback(SKSE::SerializationInterface* serialization);

    void RegisterSpawnedLink(State::SpawnedMortalLink link);
    void UnregisterSpawnedLink(Foundation::FormRawId actorRefId);
    [[nodiscard]] std::optional<State::SpawnedMortalLink> FindActiveLinkByActor(Foundation::FormRawId actorRefId);
    [[nodiscard]] std::optional<State::SpawnedMortalLink> FindActiveLinkBySlotKey(std::string_view slotKey);
    [[nodiscard]] std::vector<State::SpawnedMortalLink> GetActiveLinksSnapshot();
    [[nodiscard]] std::vector<State::SpawnedMortalLink> ReconcileLinksOnPostLoad();
    [[nodiscard]] std::vector<std::filesystem::path> ResolveActiveSaveDirectories();

}  // namespace MORWHA::Serialization
