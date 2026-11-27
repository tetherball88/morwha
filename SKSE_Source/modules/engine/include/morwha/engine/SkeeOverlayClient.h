#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace RE {
    class Actor;
}

namespace MORWHA::Engine {

    namespace SKEE {
        class IOverrideInterface;
    }

    /// Exchanges RaceMenu SKEE IOverrideInterface at kPostPostLoad so custom MORWHA face and
    /// beast-mouth headpart nodes can strip any external NiOverride texture overrides.
    class SkeeOverlayClient {
    public:
        static SkeeOverlayClient& GetSingleton() noexcept;

        void ExchangeInterfacesAtPostPostLoad();
        [[nodiscard]] bool IsAvailable() const noexcept;

        /// Strips external SKEE NiOverride texture overrides (key=9) from a custom MORWHA headpart node.
        void ClearHeadNodeTextureOverrides(RE::Actor* actor, const char* nodeName);

        /// Read-only inspection of active SKEE NiOverride texture slots (0..7) on a headpart node.
        [[nodiscard]] std::vector<std::pair<std::uint8_t, std::string>> InspectHeadNodeTextureOverrides(
            RE::Actor* actor,
            const char* nodeName) const;

        /// Read-only inspection of active SKEE NiOverride skin texture slots (0..7) for a biped slot mask.
        [[nodiscard]] std::vector<std::pair<std::uint8_t, std::string>> InspectSkinTextureOverrides(
            RE::Actor* actor,
            std::uint32_t slotMask) const;

    private:
        SkeeOverlayClient() = default;

        SKEE::IOverrideInterface* overrideInterface_{ nullptr };
    };

}  // namespace MORWHA::Engine
