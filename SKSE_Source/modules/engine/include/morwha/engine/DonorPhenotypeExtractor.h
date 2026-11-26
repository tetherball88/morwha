#pragma once

#include "morwha/core/MorwhaCore.h"
#include "morwha/engine/TriProjectorCache.h"

#include <optional>

namespace RE {
    class Actor;
    class BGSHeadPart;
    class TESNPC;
    class TESRace;
}

namespace MORWHA::Engine {

    struct ExtractedEngineDonor {
        Core::DonorPhenotype phenotype{};
        RE::TESRace* race{ nullptr };
        RE::TESNPC* baseNpc{ nullptr };
        RE::TESNPC* rootFaceNpc{ nullptr };
        RE::BGSHeadPart* eyePart{ nullptr };
        DonorGeometryBundle geometryBundle{};
        bool extractedLive3dHairShader{ false };
        DonorExtractionTier extractionTier{ DonorExtractionTier::Tier1LiveFaceData };
    };

    /// Extracts live 3D / VFS .nif facial geometry, eye sample, FaceData sliders,
    /// weight, height, live skin/hair colors, and eye headpart from an Actor reference.
    [[nodiscard]] std::optional<ExtractedEngineDonor> ExtractDonorPhenotype(RE::Actor* actor);

    /// Extracts static donor traits from a TESNPC base record without a live Actor instance.
    [[nodiscard]] std::optional<ExtractedEngineDonor> ExtractDonorPhenotypeFromBase(RE::TESNPC* npc);

}  // namespace MORWHA::Engine
