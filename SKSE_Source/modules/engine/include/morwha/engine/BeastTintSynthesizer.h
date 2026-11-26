#pragma once

#include "morwha/engine/DonorPhenotypeExtractor.h"
#include "morwha/foundation/StrongIds.h"
#include "morwha/state/RegistryTypes.h"

#include <RE/T/TESRace.h>

#include <vector>

namespace MORWHA::Engine {

    /// Appends coordinated Argonian/Khajiit scale/pelage TintLayerSpecs (or inherits same-species
    /// parental markings across sex-indexed TintAsset ordinals) when targetRace is Argonian/Khajiit.
    /// Returns false without modifying ioTintLayers for non-Beast races.
    bool TryAppendBeastRacialMarkings(
        std::vector<State::TintLayerSpec>& ioTintLayers,
        const RE::TESRace* targetRace,
        const RE::TESRace::FaceRelatedData* targetFaceRelated,
        const ExtractedEngineDonor* optionalPrimaryDonor,
        const ExtractedEngineDonor* optionalSecondaryDonor,
        Foundation::Seed64 seed);

}  // namespace MORWHA::Engine
