#pragma once

#include "morwha/core/MorwhaCore.h"

namespace RE {
    class Actor;
}

namespace MORWHA::Engine {

    struct LiveExtractedFaceScene {
        Core::ExtractedHeadScene scene{};
        bool valid{ false };
    };

    /// Reads live morphed/sculpted 3D head, eyeball, and mouth sub-mesh geometry plus live shader
    /// diffuse eye texture and hair tint directly from an Actor's loaded FaceGen scenegraph in RAM.
    [[nodiscard]] LiveExtractedFaceScene ExtractLiveActorFaceScene(RE::Actor* actor);

}  // namespace MORWHA::Engine
