#include "morwha/engine/SkeeOverlayClient.h"

#include "SkeeInterfaceDefs.h"
#include "morwha/engine/EngineLogging.h"

namespace MORWHA::Engine {

    namespace {
        constexpr std::uint16_t kOverrideKeyTexture = 9u;
    }  // namespace

    SkeeOverlayClient& SkeeOverlayClient::GetSingleton() noexcept {
        static SkeeOverlayClient instance;
        return instance;
    }

    void SkeeOverlayClient::ExchangeInterfacesAtPostPostLoad() {
        const auto* messaging = SKSE::GetMessagingInterface();
        if (!messaging) {
            return;
        }

        SKEE::InterfaceExchangeMessage msg{};
        messaging->Dispatch(
            SKEE::InterfaceExchangeMessage::kMessage_ExchangeInterface,
            &msg,
            sizeof(SKEE::InterfaceExchangeMessage*),
            "skee");
        if (!msg.interfaceMap) {
            MORWHA_LOG_INFO("SkeeOverlayClient: SKEE interface map unavailable");
            return;
        }

        overrideInterface_ =
            static_cast<SKEE::IOverrideInterface*>(msg.interfaceMap->QueryInterface("Override"));
        if (overrideInterface_) {
            MORWHA_LOG_INFO(
                "SkeeOverlayClient: Connected SKEE Override v{}",
                overrideInterface_->GetVersion());
        } else {
            MORWHA_LOG_WARN("SkeeOverlayClient: SKEE Override interface unavailable");
        }
    }

    bool SkeeOverlayClient::IsAvailable() const noexcept {
        return overrideInterface_ != nullptr;
    }

    void SkeeOverlayClient::ClearHeadNodeTextureOverrides(
        RE::Actor* actor,
        const char* nodeName) {
        if (!actor || !nodeName || nodeName[0] == '\0' || !overrideInterface_) {
            return;
        }
        RE::TESNPC* baseNpc = actor->GetActorBase();
        if (!baseNpc) {
            return;
        }
        const bool isFemale = (baseNpc->GetSex() == RE::SEXES::kFemale);
        auto* refr = static_cast<RE::TESObjectREFR*>(actor);

        bool hasAnyTexOverride = false;
        for (const SKEE::skee_u8 idx : {
                 SKEE::skee_u8{ 0u },
                 SKEE::skee_u8{ 1u },
                 SKEE::skee_u8{ 2u },
                 SKEE::skee_u8{ 3u },
                 SKEE::skee_u8{ 7u } }) {
            if (overrideInterface_->HasNodeOverride(refr, isFemale, nodeName, kOverrideKeyTexture, idx)) {
                hasAnyTexOverride = true;
                break;
            }
        }
        if (hasAnyTexOverride) {
            overrideInterface_->RemoveAllNodeOverridesByNode(refr, isFemale, nodeName);
            overrideInterface_->SetNodeProperties(refr, /*immediate=*/true);
        }
    }

}  // namespace MORWHA::Engine
