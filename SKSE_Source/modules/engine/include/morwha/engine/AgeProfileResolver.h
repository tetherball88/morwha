#pragma once

#include "morwha/foundation/DomainTypes.h"
#include "morwha/foundation/StrongIds.h"
#include "morwha/state/RegistryTypes.h"

#include <RE/T/TESRace.h>

namespace RE {
    class BGSTextureSet;
    class BGSVoiceType;
    class TESNPC;
}

namespace MORWHA::Engine {

    /// Returns true iff voice is non-null, non-deleted, and its VOICE_TYPE_DATA::Flag::kFemale
    /// bit matches targetSex.
    [[nodiscard]] bool IsVoiceTypeSexCompatible(
        const RE::BGSVoiceType* voice,
        Foundation::Sex targetSex) noexcept;

    /// Selects a sex-verified BGSVoiceType from curated Skyrim.esm age & race pools (seeded).
    [[nodiscard]] RE::BGSVoiceType* ResolveVoiceTypeForAgeAndRace(
        const RE::TESRace* targetRace,
        Foundation::Sex targetSex,
        Foundation::AgeBracket effectiveBracket,
        Foundation::Seed64 seed);

    /// Selects an age-appropriate BGSTextureSet from the target race/sex's faceDetailsTextureSets.
    [[nodiscard]] RE::BGSTextureSet* ResolveComplexionForAgeAndRace(
        RE::TESRace* targetRace,
        const RE::TESRace::FaceRelatedData* faceRelated,
        Foundation::Sex targetSex,
        Foundation::AgeBracket effectiveBracket,
        Foundation::Seed64 seed);

    /// Resolves ageBracket (Auto -> concrete), voiceTypeRef, faceTextureSetRef, age tint layers
    /// (LaughLines, EyeSocketLower, Forehead), and Elder hair silvering onto spec.
    void ApplyAgeProfileToSpec(
        State::DynamicNpcSpec& spec,
        RE::TESRace* targetRace,
        const RE::TESRace::FaceRelatedData* faceRelated,
        Foundation::AgeBracket requestedBracket);

    /// Synchronizes optional runtime age-normal-map compatibility keywords (e.g. RSVyoung / RSVold
    /// when Racial Skin Variance is installed and RSVignore is absent) onto targetNpc so external
    /// SPID/NiOverride head-normal scripts honor spec.ageBracket.
    void SyncNpcAgeCompatibilityKeywords(
        RE::TESNPC* targetNpc,
        const State::DynamicNpcSpec& spec);

}  // namespace MORWHA::Engine
