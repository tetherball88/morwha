#pragma once

#include "morwha/foundation/DomainTypes.h"

#include <string_view>

namespace RE {
    class Actor;
    class TESRace;
}

namespace MORWHA::Bridge::Papyrus {

    /// Resolves a playable TESRace* by EditorID, shorthand without trailing "Race",
    /// or lore Mer alias (Altmer, Dunmer, Bosmer, Orsimer).
    [[nodiscard]] RE::TESRace* FindRaceByToken(std::string_view token);

    /// Attempts to parse `rawToken` as a two-race pair (`NordOrc`, `OrcNord`, `AltmerBreton`,
    /// `Imperial-Khajiit`, `NordNord`, etc.). When matched, builds `(Race, Sex)` NPC donor pools
    /// for both races and spawns 3M + 3F offspring near `player`, rolling a fresh random Male+Female
    /// parent pair for each spawned child. Returns false when `rawToken` is not a race-pair token.
    [[nodiscard]] bool TryExecuteRacePairBatchSpawn(
        std::string_view rawToken,
        Foundation::AgeBracket batchAge,
        RE::Actor* player);

}  // namespace MORWHA::Bridge::Papyrus
