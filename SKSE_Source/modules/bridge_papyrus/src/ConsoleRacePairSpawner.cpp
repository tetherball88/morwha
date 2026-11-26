#include "ConsoleRacePairSpawner.h"

#include "morwha/app/SynthesisCoordinator.h"
#include "morwha/engine/HeadPartPoolIndexer.h"

#include <RE/A/Actor.h>
#include <RE/C/ConsoleLog.h>
#include <RE/T/TESDataHandler.h>
#include <RE/T/TESFile.h>
#include <RE/T/TESNPC.h>
#include <RE/T/TESRace.h>

#include <chrono>
#include <cctype>
#include <format>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace MORWHA::Bridge::Papyrus {

    namespace {

        constexpr RE::FormID kPlayerBaseNpcFormId = 0x00000007u;
        constexpr RE::FormID kActorTypeUndeadKeywordFormId = 0x013796u;
        constexpr RE::FormID kActorTypeGhostKeywordFormId = 0x0D205Eu;

        [[nodiscard]] bool EqualsIgnoreCase(std::string_view a, std::string_view b) noexcept {
            return Engine::HeadPartPoolIndexer::EqualsCaseInsensitive(a, b);
        }

        [[nodiscard]] bool ContainsIgnoreCase(std::string_view text, std::string_view sub) noexcept {
            return Engine::HeadPartPoolIndexer::ContainsCaseInsensitive(text, sub);
        }

        void PrintLine(std::string_view msg) {
            if (auto* console = RE::ConsoleLog::GetSingleton()) {
                const std::string owned(msg);
                console->Print("%s", owned.c_str());
            }
        }

        [[nodiscard]] bool IsReservedConsoleKeyword(std::string_view token) noexcept {
            static constexpr std::string_view kReserved[] = {
                "help", "?", "status", "list", "gc",
                "reload", "set", "reroll", "rel", "relative",
                "child", "offspring", "spawn", "player",
                "male", "m", "female", "f", "anysex", "randomsex", "anyrace", "rerollrace",
                "young", "youngadult", "young_adult", "mature", "adult",
                "middle", "middleaged", "middle_aged", "elder", "old"
            };
            for (const auto kw : kReserved) {
                if (EqualsIgnoreCase(token, kw)) {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] std::string FormatShortRaceName(const RE::TESRace* race) {
            if (!race) {
                return "Unknown";
            }
            const char* rawEdid = race->GetFormEditorID();
            std::string_view edid = (rawEdid && rawEdid[0] != '\0') ? rawEdid : "";
            if (edid.size() > 4u &&
                EqualsIgnoreCase(edid.substr(edid.size() - 4u), "Race")) {
                edid.remove_suffix(4u);
            }
            if (!edid.empty()) {
                return std::string(edid);
            }
            const char* fullName = race->GetFullName();
            return (fullName && fullName[0] != '\0') ? std::string(fullName) : "Mortal";
        }

        [[nodiscard]] bool TrySplitIntoTwoRaces(
            std::string_view rawToken,
            RE::TESRace*& outRaceA,
            RE::TESRace*& outRaceB) {
            outRaceA = nullptr;
            outRaceB = nullptr;
            if (rawToken.size() < 4u || IsReservedConsoleKeyword(rawToken)) {
                return false;
            }
            // Single race tokens (e.g. "HighElf", "DarkElf") belong to 0-donor spawn.
            if (FindRaceByToken(rawToken) != nullptr) {
                return false;
            }

            // 1. Explicit delimiter ("Nord-Orc", "Altmer/Breton", "Imperial_Khajiit", "Nord+Orc").
            for (std::size_t i = 1u; i + 1u < rawToken.size(); ++i) {
                const char ch = rawToken[i];
                if (ch == '-' || ch == '/' || ch == '_' || ch == '+') {
                    const std::string_view left = rawToken.substr(0u, i);
                    const std::string_view right = rawToken.substr(i + 1u);
                    if (left.empty() || right.empty()) {
                        return false;
                    }
                    auto* rA = FindRaceByToken(left);
                    auto* rB = FindRaceByToken(right);
                    if (rA && rB) {
                        outRaceA = rA;
                        outRaceB = rB;
                        return true;
                    }
                    return false;
                }
            }

            // 2. Concatenated pair ("NordOrc", "OrcNord", "AltmerOrc", "ImperialBreton", "NordNord").
            for (std::size_t split = rawToken.size() - 2u; split >= 2u; --split) {
                const std::string_view left = rawToken.substr(0u, split);
                const std::string_view right = rawToken.substr(split);
                auto* rA = FindRaceByToken(left);
                if (!rA) {
                    continue;
                }
                auto* rB = FindRaceByToken(right);
                if (rB) {
                    outRaceA = rA;
                    outRaceB = rB;
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] bool HasExcludedEditorIdMarker(std::string_view edid) noexcept {
            if (edid.empty()) {
                return false;
            }
            return ContainsIgnoreCase(edid, "Test") ||
                   ContainsIgnoreCase(edid, "Corpse") ||
                   ContainsIgnoreCase(edid, "Dead") ||
                   ContainsIgnoreCase(edid, "Dummy") ||
                   ContainsIgnoreCase(edid, "Preset") ||
                   ContainsIgnoreCase(edid, "Chargen") ||
                   ContainsIgnoreCase(edid, "MQ101");
        }

        [[nodiscard]] std::vector<RE::TESNPC*> CollectRaceSexDonorPool(
            const RE::TESRace* targetRace,
            Foundation::Sex wantSex) {
            std::vector<RE::TESNPC*> strictPool;
            std::vector<RE::TESNPC*> relaxedPool;
            if (!targetRace || targetRace->IsChildRace()) {
                return strictPool;
            }
            auto* dataHandler = RE::TESDataHandler::GetSingleton();
            if (!dataHandler) {
                return strictPool;
            }

            const bool wantFemale = (wantSex == Foundation::Sex::Female);
            for (auto* npc : dataHandler->GetFormArray<RE::TESNPC>()) {
                if (!npc || npc->IsDeleted() ||
                    npc->GetFormID() == kPlayerBaseNpcFormId ||
                    npc->race != targetRace) {
                    continue;
                }
                const auto* originFile = npc->GetFile(0);
                if (!originFile || EqualsIgnoreCase(originFile->GetFilename(), "DPF.esp")) {
                    continue;
                }
                const bool isFemale = (npc->GetSex() == RE::SEX::kFemale);
                if (isFemale != wantFemale) {
                    continue;
                }
                if (npc->HasKeywordID(kActorTypeUndeadKeywordFormId) ||
                    npc->HasKeywordID(kActorTypeGhostKeywordFormId)) {
                    continue;
                }
                if (npc->actorData.actorBaseFlags.any(
                        RE::ACTOR_BASE_DATA::Flag::kUsesTemplate,
                        RE::ACTOR_BASE_DATA::Flag::kSimpleActor,
                        RE::ACTOR_BASE_DATA::Flag::kIsGhost) ||
                    npc->baseTemplateForm != nullptr ||
                    npc->faceNPC != nullptr) {
                    continue;
                }
                if (npc->faceData == nullptr && npc->numHeadParts <= 0) {
                    continue;
                }

                relaxedPool.push_back(npc);

                if (!npc->IsUnique() || npc->IsPreset()) {
                    continue;
                }
                const char* fullName = npc->GetFullName();
                if (!fullName || fullName[0] == '\0') {
                    continue;
                }
                const std::string_view edid =
                    npc->GetFormEditorID() ? npc->GetFormEditorID() : "";
                if (HasExcludedEditorIdMarker(edid)) {
                    continue;
                }
                strictPool.push_back(npc);
            }

            return !strictPool.empty() ? strictPool : relaxedPool;
        }

        [[nodiscard]] RE::TESNPC* PickRandomDifferentNpc(
            const std::vector<RE::TESNPC*>& pool,
            const RE::TESNPC* exclude,
            std::mt19937_64& rng) {
            if (pool.empty()) {
                return nullptr;
            }
            if (pool.size() == 1u) {
                return (pool[0] != exclude) ? pool[0] : nullptr;
            }
            std::uniform_int_distribution<std::size_t> dist(0u, pool.size() - 1u);
            for (int attempt = 0; attempt < 8; ++attempt) {
                RE::TESNPC* picked = pool[dist(rng)];
                if (picked != exclude) {
                    return picked;
                }
            }
            for (auto* cand : pool) {
                if (cand != exclude) {
                    return cand;
                }
            }
            return nullptr;
        }

    }  // namespace

    RE::TESRace* FindRaceByToken(std::string_view token) {
        if (token.empty()) {
            return nullptr;
        }
        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            return nullptr;
        }

        std::string_view aliasTarget{};
        if (EqualsIgnoreCase(token, "altmer")) {
            aliasTarget = "HighElfRace";
        } else if (EqualsIgnoreCase(token, "dunmer")) {
            aliasTarget = "DarkElfRace";
        } else if (EqualsIgnoreCase(token, "bosmer")) {
            aliasTarget = "WoodElfRace";
        } else if (EqualsIgnoreCase(token, "orsimer")) {
            aliasTarget = "OrcRace";
        }

        const std::string suffixed = std::string(token) + "Race";
        for (auto* race : dataHandler->GetFormArray<RE::TESRace>()) {
            if (!race || race->IsChildRace()) {
                continue;
            }
            const char* rawEdid = race->GetFormEditorID();
            if (!rawEdid || rawEdid[0] == '\0') {
                continue;
            }
            const std::string_view edid(rawEdid);
            if (EqualsIgnoreCase(edid, token) ||
                EqualsIgnoreCase(edid, suffixed) ||
                (!aliasTarget.empty() && EqualsIgnoreCase(edid, aliasTarget))) {
                return race;
            }
        }
        return nullptr;
    }

    bool TryExecuteRacePairBatchSpawn(
        std::string_view rawToken,
        Foundation::AgeBracket batchAge,
        RE::Actor* player) {
        RE::TESRace* raceA = nullptr;
        RE::TESRace* raceB = nullptr;
        if (!TrySplitIntoTwoRaces(rawToken, raceA, raceB) || !player) {
            return false;
        }

        const auto poolAMale = CollectRaceSexDonorPool(raceA, Foundation::Sex::Male);
        const auto poolAFemale = CollectRaceSexDonorPool(raceA, Foundation::Sex::Female);
        const auto poolBMale = CollectRaceSexDonorPool(raceB, Foundation::Sex::Male);
        const auto poolBFemale = CollectRaceSexDonorPool(raceB, Foundation::Sex::Female);

        const bool canAMaleBFemale = !poolAMale.empty() && !poolBFemale.empty();
        const bool canAFemaleBMale = !poolAFemale.empty() && !poolBMale.empty();
        if (!canAMaleBFemale && !canAFemaleBMale) {
            PrintLine(std::format(
                "MORWHA: Insufficient adult NPC donors for pair {}-{} (A: {}M/{}F, B: {}M/{}F).",
                FormatShortRaceName(raceA),
                FormatShortRaceName(raceB),
                poolAMale.size(),
                poolAFemale.size(),
                poolBMale.size(),
                poolBFemale.size()));
            return true;
        }

        const std::string pairLabel =
            std::format("{}-{}", FormatShortRaceName(raceA), FormatShortRaceName(raceB));
        PrintLine(std::format(
            "MORWHA: Spawning 3M + 3F {} batch (pools: {}={}M/{}F, {}={}M/{}F):",
            pairLabel,
            FormatShortRaceName(raceA),
            poolAMale.size(),
            poolAFemale.size(),
            FormatShortRaceName(raceB),
            poolBMale.size(),
            poolBFemale.size()));

        static std::uint64_t s_batchNonce = 0;
        const auto tickSeed = static_cast<std::uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());
        std::mt19937_64 rng(tickSeed ^ ((++s_batchNonce) * 0x9E3779B97F4A7C15ull));

        auto& coord = App::SynthesisCoordinator::GetSingleton();
        for (const std::int32_t sex : { 0, 1 }) {
            for (int n = 1; n <= 3; ++n) {
                bool useAMaleBFemale = canAMaleBFemale;
                if (canAMaleBFemale && canAFemaleBMale) {
                    useAMaleBFemale = ((rng() & 1ull) == 0ull);
                }

                RE::TESNPC* parentA = nullptr;
                RE::TESNPC* parentB = nullptr;
                if (useAMaleBFemale) {
                    parentA = PickRandomDifferentNpc(poolAMale, nullptr, rng);
                    parentB = PickRandomDifferentNpc(poolBFemale, parentA, rng);
                } else {
                    parentA = PickRandomDifferentNpc(poolAFemale, nullptr, rng);
                    parentB = PickRandomDifferentNpc(poolBMale, parentA, rng);
                }
                if (!parentA || !parentB || parentA == parentB) {
                    continue;
                }

                App::MortalSpawnRequest req{};
                req.anchor = player;
                req.donorMode = Foundation::DonorMode::TwoDonorOffspring;
                req.donorA = parentA;
                req.donorB = parentB;
                req.sexInt = sex;
                req.ageBracket = batchAge;
                req.displayName = std::format("{} {}{}", pairLabel, sex == 0 ? 'M' : 'F', n);
                req.slotKeyPrefix = "offspring_mortal";

                const auto outcome = coord.SpawnMortal(req);
                if (outcome.actor) {
                    PrintLine(std::format(
                        "  {} -> '{}' ({:08X}) x '{}' ({:08X})",
                        req.displayName,
                        parentA->GetFullName() ? parentA->GetFullName() : "",
                        parentA->GetFormID(),
                        parentB->GetFullName() ? parentB->GetFullName() : "",
                        parentB->GetFormID()));
                }
            }
        }
        return true;
    }

}  // namespace MORWHA::Bridge::Papyrus
