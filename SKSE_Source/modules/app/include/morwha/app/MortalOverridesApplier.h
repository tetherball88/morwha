#pragma once

#include "morwha/app/MortalSpawnTypes.h"
#include "morwha/state/MortalRequestJson.h"
#include "morwha/state/RegistryTypes.h"

#include <string>

namespace MORWHA::App {

    /// Applies explicit caller archetype, AI, and RPG stat/perk/spell/faction overrides onto spec.
    /// Explicit voiceType and faceTextureSet overrides supersede age-bracket defaults when specified.
    void ApplyMortalOverridesToSpec(
        State::DynamicNpcSpec& spec,
        const MortalArchetypeOverrides& archetype,
        const MortalAiOverrides& ai,
        const MortalStatsOverrides& stats);

    /// Carries forward non-appearance archetype, AI, and RPG stat fields from previousSpec onto a freshly
    /// rerolled newSpec so rerolling appearance does not reset classes, outfits, factions, or combat stats.
    void CopyPreservedArchetypeAndStats(
        const State::DynamicNpcSpec& previousSpec,
        State::DynamicNpcSpec& newSpec);

    /// Resolves engine TESForm selectors from a parsed JsonSpawnRequest into a MortalSpawnRequest.
    [[nodiscard]] bool BuildMortalSpawnRequestFromJson(
        RE::Actor* anchor,
        const State::JsonSpawnRequest& parsed,
        MortalSpawnRequest& outRequest,
        std::string* outError = nullptr);

    /// Resolves engine TESForm selectors from a parsed JsonUpdateRequest into a MortalUpdateRequest.
    [[nodiscard]] bool BuildMortalUpdateRequestFromJson(
        const State::JsonUpdateRequest& parsed,
        MortalUpdateRequest& outRequest,
        std::string* outError = nullptr);

}  // namespace MORWHA::App
