#pragma once

#include "morwha/core/MorwhaCore.h"
#include "morwha/state/MorwhaIniConfig.h"

namespace MORWHA::Engine {

    struct SynthesisSettings {
        bool allowSupernaturalEyes{ false };
        bool allowScars{ true };
        float offspringScarChance{ 0.02f };
        float defaultScarChance{ 0.04f };
        Core::BeastChimeraTier beastChimeraTier{
            Core::BeastChimeraTier::Tier2EquippedParts
        };
        bool enableGeneratedHybridSkins{ true };
        State::GeneratedSkinResolution generatedSkinResolution{
            State::GeneratedSkinResolution::Auto
        };
        State::ChimeraLineageFeatureConfig khajiitTier2{ State::MakeDefaultKhajiitTier2Config() };
        State::ChimeraLineageFeatureConfig argonianTier2{ State::MakeDefaultArgonianTier2Config() };
    };

    [[nodiscard]] inline Core::BeastChimeraTier ToDomainChimeraTier(
        State::IniChimeraTier tier) noexcept {
        switch (tier) {
        case State::IniChimeraTier::Disabled:
            return Core::BeastChimeraTier::Disabled;
        case State::IniChimeraTier::Tier1DominantMorph:
            return Core::BeastChimeraTier::Tier1DominantMorph;
        case State::IniChimeraTier::Tier2EquippedParts:
            return Core::BeastChimeraTier::Tier2EquippedParts;
        }
        return Core::BeastChimeraTier::Tier2EquippedParts;
    }

    [[nodiscard]] inline SynthesisSettings SynthesisSettingsFromIni(
        const State::MorwhaIniSettings& ini) {
        SynthesisSettings out{};
        out.allowSupernaturalEyes = ini.allowSupernaturalEyes;
        out.allowScars = ini.allowScars;
        out.offspringScarChance = ini.offspringScarChance;
        out.defaultScarChance = ini.defaultScarChance;
        out.beastChimeraTier = ToDomainChimeraTier(ini.beastChimeraTier);
        out.enableGeneratedHybridSkins = ini.enableGeneratedHybridSkins;
        out.generatedSkinResolution = ini.generatedSkinResolution;
        out.khajiitTier2 = ini.khajiitTier2;
        out.argonianTier2 = ini.argonianTier2;
        return out;
    }

}  // namespace MORWHA::Engine
