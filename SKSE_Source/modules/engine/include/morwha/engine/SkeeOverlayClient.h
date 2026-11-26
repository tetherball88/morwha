#pragma once

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

    private:
        SkeeOverlayClient() = default;

        SKEE::IOverrideInterface* overrideInterface_{ nullptr };
    };

}  // namespace MORWHA::Engine
