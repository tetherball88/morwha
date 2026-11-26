#pragma once

#include "morwha/core/MorwhaCore.h"
#include "morwha/foundation/DomainTypes.h"
#include "morwha/foundation/StrongIds.h"
#include "morwha/state/RegistryTypes.h"

#include "RE/B/BGSHeadPart.h"
#include "RE/C/Color.h"
#include "RE/T/TESRace.h"

#include <optional>
#include <string>
#include <vector>

namespace RE {
    class Actor;
    class TESNPC;
}

namespace MORWHA::Engine {

    [[nodiscard]] constexpr Foundation::RgbColor ToRgbColor(RE::Color c) noexcept {
        return Foundation::RgbColor{ c.red, c.green, c.blue };
    }

    [[nodiscard]] constexpr std::uint32_t ColorDistanceSq(
        Foundation::RgbColor a,
        Foundation::RgbColor b) noexcept {
        const int dr = static_cast<int>(a.r) - static_cast<int>(b.r);
        const int dg = static_cast<int>(a.g) - static_cast<int>(b.g);
        const int db = static_cast<int>(a.b) - static_cast<int>(b.b);
        return static_cast<std::uint32_t>(dr * dr + dg * dg + db * db);
    }

    [[nodiscard]] Foundation::RgbColor CompositeTintOverMidGrey(RE::Color c, float alpha) noexcept;

    [[nodiscard]] Core::RaceGroup ResolveRaceGroup(const RE::TESRace* race);

    [[nodiscard]] bool IsMerRace(const RE::TESRace* race);

    [[nodiscard]] RE::TESRace::FaceRelatedData* ResolveRaceFaceRelatedData(
        RE::TESRace* race,
        Foundation::Sex sex) noexcept;

    /// Unwraps Vampire/Elder/Custom-head-framework races (UBE, COtR, CITRUS, etc.) to a playable
    /// Vanilla/HPH FaceGen TESRace matching their biological Skyrim ancestry (Nord, Imperial, ...).
    [[nodiscard]] RE::TESRace* UnwrapToPlayableSynthesisRace(
        RE::TESRace* candidate,
        Foundation::Sex sex) noexcept;

    [[nodiscard]] const RE::TESRace::FaceRelatedData::TintAsset* FindRaceTintAsset(
        const RE::TESRace::FaceRelatedData* faceRelated,
        State::TintSemanticType semanticType,
        std::uint16_t explicitIndex) noexcept;

    /// Returns true when RaceMenu (bAllowAllMorphs=1) has unlocked cross-race discrete
    /// FacePart morph preset bitmasks in RAM for both NoseType and EyesType.
    [[nodiscard]] bool IsAllRaceMorphsUnlocked(
        const RE::TESRace::FaceRelatedData* faceRelated) noexcept;

    /// Returns sorted 0-based valid preset indices for FacePartSlot (0=Nose, 2=Eyes, 3=Mouth),
    /// intersecting with native authoritative TESNPC presets to remain immune to RaceMenu's
    /// skee64.dll bAllowAllMorphs=1 wiping FaceRelatedData::availableMorphs to 0xFFFFFFFF.
    [[nodiscard]] std::vector<std::int32_t> ExtractAllowedMorphPresetIndices(
        const RE::TESRace* race,
        Foundation::Sex sex,
        const RE::TESRace::FaceRelatedData* faceRelated,
        std::size_t slot);

    /// Collects sorted unique allowed preset indices for slot, merging optionalSecondRace or shared
    /// chargen .tri presets for Nose/Eyes slots (0/2) when RaceMenu bAllowAllMorphs is active.
    [[nodiscard]] std::vector<std::int32_t> CollectAllowedMorphPresets(
        const RE::TESRace* race,
        Foundation::Sex sex,
        const RE::TESRace::FaceRelatedData* faceRelated,
        std::size_t slot,
        const RE::TESRace* optionalSecondRace = nullptr,
        bool allowSharedTri = false);

    /// Clamps/maps discrete FacePartSlot preset indices [0/2/3] against the target race/sex's
    /// allowed presets (optionally permitting cross-race NoseType/EyesType when unlocked in RAM)
    /// and clears slot 1 (Unknown) to kUnusedFacePartPreset.
    void SanitizeFaceMorphPartsForRace(
        Foundation::FaceMorphVector& faceMorphs,
        const RE::TESRace* race,
        Foundation::Sex sex,
        const RE::TESRace::FaceRelatedData* faceRelated,
        const RE::TESRace* optionalSecondRaceForNoseEyes = nullptr,
        bool allowSharedTriNoseEyes = false) noexcept;

    [[nodiscard]] RE::BGSHeadPart* FindFallbackHeadPart(
        RE::BGSHeadPart::HeadPartType type,
        Foundation::TopologyPool topology,
        RE::TESNPC* baselineNpc,
        RE::TESRace* race,
        const RE::TESRace::FaceRelatedData* faceRelated,
        Foundation::Sex sex,
        bool smpLoaded);

    /// Hydrates a DPF-backed RE::TESNPC* completely in memory from spec without template leaks or Respawn.
    [[nodiscard]] bool HydrateTesNpc(
        RE::TESNPC* targetNpc,
        const State::DynamicNpcSpec& spec,
        bool isMidSession);

    /// Synchronizes live Actor values, combat style, perks, spells, and factions from a hydrated DPF TESNPC.
    void SyncLiveActorFromSpec(
        RE::Actor* actor,
        RE::TESNPC* baseNpc,
        const State::DynamicNpcSpec& spec,
        bool refreshOutfit = false);

}  // namespace MORWHA::Engine
