#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace RE {
    class TESNPC;
}

namespace MORWHA::Engine {

    struct DpfSlotResult {
        RE::TESNPC* npc{ nullptr };
        std::uint32_t pluginNumber{ 0 };
        std::uint32_t localId{ 0 };
        bool existed{ false };
    };

    /// Interfaces with Dynamic Persistent Forms RE (DPF.esp) and checks runtime FaceGen prerequisites.
    class DpfClient {
    public:
        static DpfClient& GetSingleton() noexcept;

        void InitializeAtDataLoaded() noexcept;

        [[nodiscard]] bool IsDpfAvailable() const noexcept { return dpfAvailable_; }
        [[nodiscard]] bool IsFaceGenFixesLoaded() const noexcept { return faceGenFixesLoaded_; }
        [[nodiscard]] bool IsSmpLoaded() const noexcept { return smpLoaded_; }

        [[nodiscard]] std::optional<DpfSlotResult> AcquireOrRecoverNpcSlot(
            std::string_view slotKey) const noexcept;

        [[nodiscard]] bool ReleaseNpcSlot(std::string_view slotKey) const noexcept;
        [[nodiscard]] std::uint32_t ReleaseAllMorwhaSlots() const noexcept;

    private:
        DpfClient() = default;

        bool dpfAvailable_{ false };
        bool faceGenFixesLoaded_{ false };
        bool smpLoaded_{ false };
    };

}  // namespace MORWHA::Engine
