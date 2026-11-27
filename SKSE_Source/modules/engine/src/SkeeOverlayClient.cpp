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

    namespace {

        class VariantStringCollector final : public SKEE::IOverrideInterface::GetVariant {
        public:
            void Int(const SKEE::skee_i32 i) override { value = std::to_string(i); }
            void Float(const float f) override { value = std::to_string(f); }
            void String(const char* str) override { value = str ? str : ""; }
            void Bool(const bool b) override { value = b ? "true" : "false"; }
            void TextureSet(const RE::BGSTextureSet* ts) override {
                if (ts) {
                    const char* edid = ts->GetFormEditorID();
                    value = std::format(
                        "TXST:{:08X}({})",
                        ts->GetFormID(),
                        (edid && edid[0] != '\0') ? edid : "");
                }
            }

            std::string value{};
        };

    }  // namespace

    std::vector<std::pair<std::uint8_t, std::string>> SkeeOverlayClient::InspectHeadNodeTextureOverrides(
        RE::Actor* actor,
        const char* nodeName) const {
        std::vector<std::pair<std::uint8_t, std::string>> out;
        if (!actor || !nodeName || nodeName[0] == '\0' || !overrideInterface_) {
            return out;
        }
        const RE::TESNPC* baseNpc = actor->GetActorBase();
        if (!baseNpc) {
            return out;
        }
        const bool isFemale = (baseNpc->GetSex() == RE::SEXES::kFemale);
        auto* refr = static_cast<RE::TESObjectREFR*>(actor);

        for (SKEE::skee_u8 idx = 0u; idx < 8u; ++idx) {
            if (overrideInterface_->HasNodeOverride(refr, isFemale, nodeName, kOverrideKeyTexture, idx)) {
                VariantStringCollector visitor{};
                if (overrideInterface_->GetNodeOverride(
                        refr, isFemale, nodeName, kOverrideKeyTexture, idx, visitor)) {
                    out.emplace_back(idx, std::move(visitor.value));
                }
            }
        }
        return out;
    }

    std::vector<std::pair<std::uint8_t, std::string>> SkeeOverlayClient::InspectSkinTextureOverrides(
        RE::Actor* actor,
        std::uint32_t slotMask) const {
        std::vector<std::pair<std::uint8_t, std::string>> out;
        if (!actor || slotMask == 0u || !overrideInterface_) {
            return out;
        }
        const RE::TESNPC* baseNpc = actor->GetActorBase();
        if (!baseNpc) {
            return out;
        }
        const bool isFemale = (baseNpc->GetSex() == RE::SEXES::kFemale);
        auto* refr = static_cast<RE::TESObjectREFR*>(actor);

        for (SKEE::skee_u8 idx = 0u; idx < 8u; ++idx) {
            if (overrideInterface_->HasSkinOverride(
                    refr, isFemale, /*firstPerson=*/false, slotMask, kOverrideKeyTexture, idx)) {
                VariantStringCollector visitor{};
                if (overrideInterface_->GetSkinOverride(
                        refr,
                        isFemale,
                        /*firstPerson=*/false,
                        slotMask,
                        kOverrideKeyTexture,
                        idx,
                        visitor)) {
                    out.emplace_back(idx, std::move(visitor.value));
                }
            }
        }
        return out;
    }

}  // namespace MORWHA::Engine
