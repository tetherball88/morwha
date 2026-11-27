#pragma once

#include "morwha/engine/SynthesisSettings.h"
#include "morwha/state/RegistryTypes.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace RE {
    class Actor;
}

namespace MORWHA::Engine {

    struct DiagnosticBundleInput {
        RE::Actor* targetActor{ nullptr };
        std::string targetResolutionSource{};
        bool coordinatorReady{ false };
        std::string modVersion{};
        std::size_t registeredSpecCount{ 0 };
        std::size_t uncommittedSpecCount{ 0 };
        SynthesisSettings activeSettings{};
        std::optional<State::DynamicNpcSpec> matchedSpec{};
    };

    struct DiagnosticBundleResult {
        bool ok{ false };
        std::string zipRelativePath{};
        std::size_t archivedFileCount{ 0 };
        std::size_t zipByteSize{ 0 };
        std::string targetSummary{};
        std::string error{};
    };

    /// Builds a self-contained PKZIP diagnostic bundle under
    /// Data/SKSE/Plugins/MORWHA/Diagnostics/ capturing logs, settings, load order,
    /// live 3D shaders/bound textures, NPC/donor records, VFS asset headers, and
    /// 64x64 BMP previews without copying multi-megabyte raw user textures.
    [[nodiscard]] DiagnosticBundleResult BuildDiagnosticBundle(const DiagnosticBundleInput& input);

}  // namespace MORWHA::Engine
