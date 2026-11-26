#pragma once

#include "morwha/foundation/DomainTypes.h"

#include <optional>

namespace RE {
    class Actor;
    class BSFaceGenNiNode;
    class NiAVObject;
}

namespace MORWHA::Engine {

    /// Applies per-actor runtime shader tints onto loaded Tier-2 Beast Chimera tails (`Slot 40`),
    /// Argonian/Dragon horn headparts, and Khajiit cat-ear fur (`BSFaceGenNiNode` subtrees) with
    /// zero new TESForms.
    void ApplyActorChimeraPartTints(
        RE::Actor* actor,
        RE::NiAVObject* root3D,
        RE::BSFaceGenNiNode* faceNode,
        std::optional<Foundation::RgbColor> tailColor,
        std::optional<Foundation::RgbColor> hornColor,
        std::optional<Foundation::RgbColor> earColor = std::nullopt);

}  // namespace MORWHA::Engine
