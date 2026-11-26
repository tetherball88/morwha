#include "morwha/bridge_papyrus/ConsoleCommands.h"

#include "ConsoleRacePairSpawner.h"
#include "morwha/app/SynthesisCoordinator.h"
#include "morwha/engine/ChimeraEngineSupport.h"
#include "morwha/engine/DpfClient.h"
#include "morwha/engine/EngineLogging.h"
#include "morwha/engine/HybridSkinService.h"
#include "morwha/engine/SkseSerializationCallbacks.h"
#include "morwha/state/MorwhaIniConfig.h"

#include <array>
#include <cctype>
#include <charconv>
#include <format>
#include <string>
#include <string_view>

namespace MORWHA::Bridge::Papyrus {

    namespace {

        constexpr std::array<std::string_view, 6> kHostCandidates = {
            "DumpTexturePalette",
            "ToggleWaterRadius",
            "DumpNiUpdates",
            "ToggleCastShadows",
            "TestSeenData",
            "ClearAdaptedLight",
        };

        void PrintConsoleLine(std::string_view message) {
            if (auto* console = RE::ConsoleLog::GetSingleton()) {
                const std::string owned(message);
                console->Print("%s", owned.c_str());
            }
        }

        [[nodiscard]] std::string_view TrimView(std::string_view sv) noexcept {
            while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front()))) {
                sv.remove_prefix(1);
            }
            while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back()))) {
                sv.remove_suffix(1);
            }
            return sv;
        }

        [[nodiscard]] std::string_view StripSurroundingQuotes(std::string_view sv) noexcept {
            sv = TrimView(sv);
            if (sv.size() >= 2 &&
                ((sv.front() == '"' && sv.back() == '"') ||
                 (sv.front() == '\'' && sv.back() == '\''))) {
                sv = TrimView(sv.substr(1, sv.size() - 2));
            }
            return sv;
        }

        [[nodiscard]] bool CaseInsensitiveEquals(std::string_view a, std::string_view b) noexcept {
            if (a.size() != b.size()) {
                return false;
            }
            for (std::size_t i = 0; i < a.size(); ++i) {
                if (std::tolower(static_cast<unsigned char>(a[i])) !=
                    std::tolower(static_cast<unsigned char>(b[i]))) {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool TryParseSex(std::string_view token, std::int32_t& outSex) noexcept {
            if (CaseInsensitiveEquals(token, "female") ||
                CaseInsensitiveEquals(token, "f") ||
                token == "1") {
                outSex = 1;
                return true;
            }
            if (CaseInsensitiveEquals(token, "male") ||
                CaseInsensitiveEquals(token, "m") ||
                token == "0") {
                outSex = 0;
                return true;
            }
            return false;
        }

        [[nodiscard]] bool TryParseAgeBracket(
            std::string_view token,
            Foundation::AgeBracket& outBracket) noexcept {
            if (CaseInsensitiveEquals(token, "young") ||
                CaseInsensitiveEquals(token, "youngadult") ||
                CaseInsensitiveEquals(token, "young_adult")) {
                outBracket = Foundation::AgeBracket::YoungAdult;
                return true;
            }
            if (CaseInsensitiveEquals(token, "mature") ||
                CaseInsensitiveEquals(token, "adult")) {
                outBracket = Foundation::AgeBracket::Mature;
                return true;
            }
            if (CaseInsensitiveEquals(token, "middle") ||
                CaseInsensitiveEquals(token, "middleaged") ||
                CaseInsensitiveEquals(token, "middle_aged")) {
                outBracket = Foundation::AgeBracket::MiddleAged;
                return true;
            }
            if (CaseInsensitiveEquals(token, "elder") ||
                CaseInsensitiveEquals(token, "old")) {
                outBracket = Foundation::AgeBracket::Elder;
                return true;
            }
            return false;
        }

        [[nodiscard]] bool TryParseBool(std::string_view token, bool& outVal) noexcept {
            if (CaseInsensitiveEquals(token, "on") ||
                CaseInsensitiveEquals(token, "true") ||
                token == "1" ||
                CaseInsensitiveEquals(token, "yes")) {
                outVal = true;
                return true;
            }
            if (CaseInsensitiveEquals(token, "off") ||
                CaseInsensitiveEquals(token, "false") ||
                token == "0" ||
                CaseInsensitiveEquals(token, "no")) {
                outVal = false;
                return true;
            }
            return false;
        }

        [[nodiscard]] std::string_view FormatChimeraTier(
            Core::BeastChimeraTier tier) noexcept {
            switch (tier) {
                case Core::BeastChimeraTier::Disabled:
                    return "off";
                case Core::BeastChimeraTier::Tier1DominantMorph:
                    return "tier1";
                case Core::BeastChimeraTier::Tier2EquippedParts:
                    return "tier2";
            }
            return "tier1";
        }

        [[nodiscard]] bool TryParseChimeraTier(
            std::string_view token,
            Core::BeastChimeraTier& outTier) noexcept {
            if (CaseInsensitiveEquals(token, "off") ||
                CaseInsensitiveEquals(token, "false") ||
                token == "0" ||
                CaseInsensitiveEquals(token, "none") ||
                CaseInsensitiveEquals(token, "disabled")) {
                outTier = Core::BeastChimeraTier::Disabled;
                return true;
            }
            if (CaseInsensitiveEquals(token, "1") ||
                CaseInsensitiveEquals(token, "tier1") ||
                CaseInsensitiveEquals(token, "t1") ||
                CaseInsensitiveEquals(token, "on") ||
                CaseInsensitiveEquals(token, "true")) {
                outTier = Core::BeastChimeraTier::Tier1DominantMorph;
                return true;
            }
            if (CaseInsensitiveEquals(token, "2") ||
                CaseInsensitiveEquals(token, "tier2") ||
                CaseInsensitiveEquals(token, "t2")) {
                outTier = Core::BeastChimeraTier::Tier2EquippedParts;
                return true;
            }
            return false;
        }

        [[nodiscard]] RE::Actor* ResolveCrosshairOrConsoleActor(RE::TESObjectREFR* consoleRef) {
            if (consoleRef) {
                if (auto* refActor = consoleRef->As<RE::Actor>()) {
                    return refActor;
                }
            }
            if (auto* pick = RE::CrosshairPickData::GetSingleton()) {
                const auto handle = pick->GetActiveTarget();
                if (const auto refPtr = handle.get()) {
                    if (auto* pickActor = refPtr->As<RE::Actor>()) {
                        return pickActor;
                    }
                }
            }
            return nullptr;
        }

        [[nodiscard]] bool TryParseActorToken(std::string_view token, RE::Actor*& outActor) {
            outActor = nullptr;
            if (token.empty()) {
                return false;
            }
            if (CaseInsensitiveEquals(token, "player")) {
                outActor = RE::PlayerCharacter::GetSingleton();
                return outActor != nullptr;
            }

            std::string_view hexStr = token;
            if (hexStr.size() >= 2 && hexStr[0] == '0' && (hexStr[1] == 'x' || hexStr[1] == 'X')) {
                hexStr.remove_prefix(2);
            }
            if (hexStr.empty() || hexStr.size() > 8) {
                return false;
            }
            for (const unsigned char ch : hexStr) {
                if (!std::isxdigit(ch)) {
                    return false;
                }
            }
            std::uint32_t formId = 0;
            const auto [ptr, ec] = std::from_chars(hexStr.data(), hexStr.data() + hexStr.size(), formId, 16);
            if (ec != std::errc{} || ptr != hexStr.data() + hexStr.size() || formId == 0) {
                return false;
            }
            if (formId == 0x00000014u) {
                outActor = RE::PlayerCharacter::GetSingleton();
                return outActor != nullptr;
            }
            outActor = RE::TESForm::LookupByID<RE::Actor>(formId);
            return outActor != nullptr;
        }

        void PrintUsage() {
            PrintConsoleLine("MORWHA Console Usage:");
            PrintConsoleLine("  mspawn [female|male] [Race] [young|mature|middle|elder] [\"Name\"]");
            PrintConsoleLine("  mspawn rel [actorHex|player] [sex] [age] [\"Name\"] -> Spawn 1-donor relative");
            PrintConsoleLine("  mspawn child [parentA] [parentB] [sex] [age] [\"Name\"] -> Spawn 2-donor offspring");
            PrintConsoleLine("  mspawn <RaceARaceB> [age]                   -> Spawn 3M + 3F from random M+F pools of any two races (e.g. NordOrc, AltmerBreton, ImperialKhajiit, NordNord)");
            PrintConsoleLine("  mspawn reroll [actorHex] [anyrace|Race] [female|male|anysex] [age] [\"Name\"] -> Reroll mortal");
            PrintConsoleLine("  mspawn set <super_eyes|scars> <on|off> | set chimera_tier <off|1|2> -> Toggle synthesis flags");
            PrintConsoleLine("  mspawn reload                               -> Reload Data/SKSE/Plugins/MORWHA/MORWHA.ini (creates default if absent)");
            PrintConsoleLine("  mspawn gc                                   -> Garbage-collect uncommitted & orphaned DPF/registry slots across saves");
            PrintConsoleLine("  mspawn status                               -> Show DPF / FaceGenFixes / registry / chimera status");
            PrintConsoleLine("  mspawn list                                 -> List active spawned mortals in current save");
        }

        void PrintStatus() {
            const auto& coord = App::SynthesisCoordinator::GetSingleton();
            const auto& dpf = Engine::DpfClient::GetSingleton();
            const auto activeLinks = Serialization::GetActiveLinksSnapshot();
            const auto settings = coord.GetSettings();
            const auto chimeraMods = Engine::InspectInstalledChimeraMods();
            const std::size_t stencilCount =
                Engine::HybridSkinService::GetSingleton().GetDiscoveredStencilCount();
            PrintConsoleLine(std::format(
                "MORWHA Status: version={}, ready={}, DPF.esp={}, FaceGenFixes.dll={}, hdtSMP64.dll={}, skinStencils={}, registrySpecs={} (uncommitted={}), activeSaveLinks={}, superEyes={}, scars={}, chimeraTier={}, wearableBeastParts={}",
                coord.GetVersion(),
                coord.IsReady() ? "yes" : "no",
                dpf.IsDpfAvailable() ? "ready" : "missing",
                dpf.IsFaceGenFixesLoaded() ? "loaded" : "missing",
                dpf.IsSmpLoaded() ? "loaded" : "missing",
                stencilCount,
                coord.GetRegisteredSpecCount(),
                coord.GetUncommittedSpecCount(),
                activeLinks.size(),
                settings.allowSupernaturalEyes ? "on" : "off",
                settings.allowScars ? "on" : "off",
                FormatChimeraTier(settings.beastChimeraTier),
                chimeraMods.wearableBeastPartsLoaded ? "yes" : "no"));
            if (settings.enableGeneratedHybridSkins && stencilCount == 0u) {
                PrintConsoleLine(
                    "MORWHA Note: 0 .mrst stencils found; install 'M.O.R.W.H.A - Hybrid Skin Stencils' for Tier-2 fur/scale hybrid skins.");
            }
        }

        void PrintActiveLinks() {
            const auto links = Serialization::GetActiveLinksSnapshot();
            if (links.empty()) {
                PrintConsoleLine("MORWHA: (no active MORWHA links in current save)");
                return;
            }
            PrintConsoleLine(std::format("MORWHA: {} active spawned mortal link(s):", links.size()));
            for (std::size_t i = 0; i < links.size(); ++i) {
                const auto& link = links[i];
                auto* actor = RE::TESForm::LookupByID<RE::Actor>(link.actorRefId.raw());
                const auto* base = actor ? actor->GetActorBase() : nullptr;
                const RE::FormID baseId = base ? base->GetFormID() : 0u;
                const bool loaded3D = actor ? actor->Is3DLoaded() : false;
                const char* actorName = actor ? actor->GetName() : "";
                PrintConsoleLine(std::format(
                    "  [{}] key='{}' actor={:08X} base={:08X} dpfLocalId={:06X} donors=({:08X},{:08X}) loaded3D={} name='{}'",
                    i,
                    link.slotKey,
                    link.actorRefId.raw(),
                    baseId,
                    link.dpfLocalId,
                    link.donorARefId.raw(),
                    link.donorBRefId.raw(),
                    loaded3D ? "yes" : "no",
                    actorName ? actorName : ""));
            }
        }

        [[nodiscard]] bool IsActiveGameSessionReady() {
            if (auto* ui = RE::UI::GetSingleton(); ui && ui->IsMenuOpen(RE::MainMenu::MENU_NAME)) {
                return false;
            }
            auto* player = RE::PlayerCharacter::GetSingleton();
            return player != nullptr && player->GetParentCell() != nullptr;
        }

        bool ExecuteMorwhaConsoleCommand(
            const RE::SCRIPT_PARAMETER* a_paramInfo,
            RE::SCRIPT_FUNCTION::ScriptData* a_scriptData,
            RE::TESObjectREFR* a_thisObj,
            RE::TESObjectREFR* a_containingObj,
            RE::Script* a_scriptObj,
            RE::ScriptLocals* a_locals,
            double& a_result,
            std::uint32_t& a_opcodeOffsetPtr) {
            a_result = 0.0;

            char rawArg1[0x400]{};
            char rawArg2[0x400]{};
            char rawArg3[0x400]{};
            char rawArg4[0x400]{};
            char rawArg5[0x400]{};
            char rawArg6[0x400]{};
            if (a_paramInfo && a_scriptData) {
                if (!RE::Script::ParseParameters(
                        a_paramInfo,
                        a_scriptData,
                        a_opcodeOffsetPtr,
                        a_thisObj,
                        a_containingObj,
                        a_scriptObj,
                        a_locals,
                        static_cast<char*>(rawArg1),
                        static_cast<char*>(rawArg2),
                        static_cast<char*>(rawArg3),
                        static_cast<char*>(rawArg4),
                        static_cast<char*>(rawArg5),
                        static_cast<char*>(rawArg6))) {
                    PrintConsoleLine("MORWHA: Failed to parse console parameters.");
                    PrintUsage();
                    return true;
                }
            }
            rawArg1[sizeof(rawArg1) - 1] = '\0';
            rawArg2[sizeof(rawArg2) - 1] = '\0';
            rawArg3[sizeof(rawArg3) - 1] = '\0';
            rawArg4[sizeof(rawArg4) - 1] = '\0';
            rawArg5[sizeof(rawArg5) - 1] = '\0';
            rawArg6[sizeof(rawArg6) - 1] = '\0';

            std::string_view arg1 = StripSurroundingQuotes(rawArg1);
            std::string_view arg2 = StripSurroundingQuotes(rawArg2);
            std::string_view arg3 = StripSurroundingQuotes(rawArg3);
            std::string_view arg4 = StripSurroundingQuotes(rawArg4);
            std::string_view arg5 = StripSurroundingQuotes(rawArg5);
            std::string_view arg6 = StripSurroundingQuotes(rawArg6);

            if (CaseInsensitiveEquals(arg1, "help") || arg1 == "?") {
                PrintUsage();
                return true;
            }
            if (CaseInsensitiveEquals(arg1, "status")) {
                PrintStatus();
                return true;
            }
            if (CaseInsensitiveEquals(arg1, "list")) {
                PrintActiveLinks();
                return true;
            }
            if (CaseInsensitiveEquals(arg1, "gc")) {
                auto& coord = App::SynthesisCoordinator::GetSingleton();
                if (!coord.IsReady()) {
                    PrintConsoleLine("MORWHA: Not ready yet.");
                    return true;
                }
                const auto rep = coord.RunGarbageCollection(/*includeCrossSaveScan=*/true);
                PrintConsoleLine(std::format(
                    "MORWHA GC: ok={}, scannedSaves={} (morwhaSaves={}, orphanSkseSkipped={}), prunedUncommitted={}, prunedOrphaned={}, releasedDpfSlots={}, remainingSpecs={}{}{}",
                    rep.ok ? "yes" : "no",
                    rep.scannedCoSaveCount,
                    rep.matchedMorwhaSaveCount,
                    rep.skippedOrphanCoSaveCount,
                    rep.prunedUncommittedCount,
                    rep.prunedCommittedCount,
                    rep.releasedDpfSlotCount,
                    rep.remainingSpecCount,
                    rep.error.empty() ? "" : ", note=",
                    rep.error));
                return true;
            }
            if (CaseInsensitiveEquals(arg1, "reload")) {
                auto& coord = App::SynthesisCoordinator::GetSingleton();
                const bool ok = coord.ReloadSettingsFromIni();
                const auto settings = coord.GetSettings();
                PrintConsoleLine(std::format(
                    "MORWHA: {} '{}' (chimera_tier={}, khajiit=[eyes={},ears={},teeth={},tails={},hyb={}], argonian=[eyes={},horns={},teeth={},tails={},hyb={}])",
                    ok ? "Reloaded" : "Failed reloading",
                    State::kDefaultMorwhaIniPath,
                    FormatChimeraTier(settings.beastChimeraTier),
                    settings.khajiitTier2.enableEyes ? "on" : "off",
                    settings.khajiitTier2.enableHeadFeature ? "on" : "off",
                    settings.khajiitTier2.enableTeeth ? "on" : "off",
                    settings.khajiitTier2.enableTails ? "on" : "off",
                    settings.khajiitTier2.enableHybridSkin ? "on" : "off",
                    settings.argonianTier2.enableEyes ? "on" : "off",
                    settings.argonianTier2.enableHeadFeature ? "on" : "off",
                    settings.argonianTier2.enableTeeth ? "on" : "off",
                    settings.argonianTier2.enableTails ? "on" : "off",
                    settings.argonianTier2.enableHybridSkin ? "on" : "off"));
                return true;
            }
            if (CaseInsensitiveEquals(arg1, "set")) {
                auto& coord = App::SynthesisCoordinator::GetSingleton();
                auto settings = coord.GetSettings();
                if (!arg2.empty() && !arg3.empty() &&
                    CaseInsensitiveEquals(arg2, "chimera_tier")) {
                    Core::BeastChimeraTier tierVal = settings.beastChimeraTier;
                    if (TryParseChimeraTier(arg3, tierVal)) {
                        settings.beastChimeraTier = tierVal;
                        coord.SetSettings(settings);
                        const auto avail = Engine::QueryActiveChimeraTierAvailability(settings);
                        const bool tierReady =
                            (tierVal == Core::BeastChimeraTier::Disabled) ||
                            (tierVal == Core::BeastChimeraTier::Tier1DominantMorph) ||
                            (tierVal == Core::BeastChimeraTier::Tier2EquippedParts &&
                             (avail.tier2KhajiitAccessoriesReady || avail.tier2ArgonianAccessoriesReady));
                        PrintConsoleLine(std::format(
                            "MORWHA: chimera_tier = {} (tier2=[khajiit={},argonian={},beastCross={}/{}]){}",
                            FormatChimeraTier(tierVal),
                            avail.tier2KhajiitAccessoriesReady ? "ready" : "off",
                            avail.tier2ArgonianAccessoriesReady ? "ready" : "off",
                            avail.tier2KhajiitOnArgonianReady ? "ready" : "off",
                            avail.tier2ArgonianOnKhajiitReady ? "ready" : "off",
                            tierReady ? "" : " (falls back toward tier1)"));
                        return true;
                    }
                }
                bool val = false;
                if (!arg2.empty() && !arg3.empty() && TryParseBool(arg3, val)) {
                    if (CaseInsensitiveEquals(arg2, "super_eyes")) {
                        settings.allowSupernaturalEyes = val;
                        coord.SetSettings(settings);
                        PrintConsoleLine(std::format("MORWHA: super_eyes = {}", val ? "on" : "off"));
                        return true;
                    }
                    if (CaseInsensitiveEquals(arg2, "scars")) {
                        settings.allowScars = val;
                        coord.SetSettings(settings);
                        PrintConsoleLine(std::format("MORWHA: scars = {}", val ? "on" : "off"));
                        return true;
                    }
                }
                PrintConsoleLine(std::format(
                    "MORWHA Settings: super_eyes={}, scars={}, chimera_tier={}, khajiit=[eyes={},ears={},teeth={},tails={},hyb={}], argonian=[eyes={},horns={},teeth={},tails={},hyb={}]",
                    settings.allowSupernaturalEyes ? "on" : "off",
                    settings.allowScars ? "on" : "off",
                    FormatChimeraTier(settings.beastChimeraTier),
                    settings.khajiitTier2.enableEyes ? "on" : "off",
                    settings.khajiitTier2.enableHeadFeature ? "on" : "off",
                    settings.khajiitTier2.enableTeeth ? "on" : "off",
                    settings.khajiitTier2.enableTails ? "on" : "off",
                    settings.khajiitTier2.enableHybridSkin ? "on" : "off",
                    settings.argonianTier2.enableEyes ? "on" : "off",
                    settings.argonianTier2.enableHeadFeature ? "on" : "off",
                    settings.argonianTier2.enableTeeth ? "on" : "off",
                    settings.argonianTier2.enableTails ? "on" : "off",
                    settings.argonianTier2.enableHybridSkin ? "on" : "off"));
                PrintConsoleLine("  Usage: mspawn set <super_eyes|scars> <on|off> | mspawn set chimera_tier <off|1|2> | mspawn reload");
                return true;
            }

            if (!IsActiveGameSessionReady()) {
                PrintConsoleLine("MORWHA: Load a save game (or start a new game) before spawning mortals.");
                return true;
            }

            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player) {
                PrintConsoleLine("MORWHA: PlayerCharacter unavailable.");
                return true;
            }

            {
                Foundation::AgeBracket batchAge = Foundation::AgeBracket::Auto;
                (void)TryParseAgeBracket(arg2, batchAge);
                if (TryExecuteRacePairBatchSpawn(arg1, batchAge, player)) {
                    return true;
                }
            }

            if (CaseInsensitiveEquals(arg1, "reroll")) {
                const std::array<std::string_view, 5> trailing{ arg2, arg3, arg4, arg5, arg6 };
                RE::Actor* targetActor = nullptr;
                RE::TESRace* raceOverride = nullptr;
                bool rerollRaceFromDonors = false;
                std::int32_t sexOverride = -1;
                bool hasSex = false;
                Foundation::AgeBracket ageOverride = Foundation::AgeBracket::Auto;
                bool hasAge = false;
                std::string nameOverride;

                for (const std::string_view tok : trailing) {
                    if (tok.empty()) {
                        continue;
                    }
                    if (CaseInsensitiveEquals(tok, "anyrace") || CaseInsensitiveEquals(tok, "rerollrace")) {
                        rerollRaceFromDonors = true;
                        continue;
                    }
                    if (!hasSex &&
                        (CaseInsensitiveEquals(tok, "anysex") || CaseInsensitiveEquals(tok, "randomsex"))) {
                        sexOverride = -2;
                        hasSex = true;
                        continue;
                    }
                    std::int32_t tmpSex = -1;
                    if (!hasSex && TryParseSex(tok, tmpSex)) {
                        sexOverride = tmpSex;
                        hasSex = true;
                        continue;
                    }
                    Foundation::AgeBracket tmpAge = Foundation::AgeBracket::Auto;
                    if (!hasAge && TryParseAgeBracket(tok, tmpAge)) {
                        ageOverride = tmpAge;
                        hasAge = true;
                        continue;
                    }
                    RE::Actor* tmpActor = nullptr;
                    if (!targetActor && TryParseActorToken(tok, tmpActor)) {
                        targetActor = tmpActor;
                        continue;
                    }
                    if (!raceOverride) {
                        if (auto* foundRace = FindRaceByToken(tok)) {
                            raceOverride = foundRace;
                            continue;
                        }
                    }
                    if (nameOverride.empty()) {
                        nameOverride = std::string(tok);
                        continue;
                    }
                    PrintConsoleLine(std::format("MORWHA: Unrecognized reroll argument '{}'.", tok));
                    PrintUsage();
                    return true;
                }

                if (!targetActor) {
                    targetActor = ResolveCrosshairOrConsoleActor(a_thisObj);
                }
                if (!targetActor) {
                    PrintConsoleLine(
                        "MORWHA: Reroll needs a target actor (aim at/select an NPC in console or pass FormID: mspawn reroll <actorHex>).");
                    return true;
                }

                App::MortalRerollRequest rerollReq{};
                rerollReq.raceOverride = raceOverride;
                rerollReq.rerollRaceFromDonors = rerollRaceFromDonors;
                rerollReq.sexOverride = sexOverride;
                rerollReq.ageBracketOverride = ageOverride;
                rerollReq.nameOverride = std::move(nameOverride);
                if (!App::SynthesisCoordinator::GetSingleton().RerollMortal(
                        targetActor, rerollReq)) {
                    PrintConsoleLine(
                        "MORWHA: Reroll failed; check 'mspawn list' or inspect Documents/My Games/Skyrim Special Edition/SKSE/MORWHA.log.");
                }
                return true;
            }

            const bool isRelativeCmd =
                CaseInsensitiveEquals(arg1, "rel") || CaseInsensitiveEquals(arg1, "relative");
            const bool isOffspringCmd =
                CaseInsensitiveEquals(arg1, "child") || CaseInsensitiveEquals(arg1, "offspring");

            if (isRelativeCmd || isOffspringCmd) {
                const std::array<std::string_view, 5> trailing{ arg2, arg3, arg4, arg5, arg6 };
                const std::size_t maxActorArgs = isOffspringCmd ? 2u : 1u;

                std::array<RE::Actor*, 2> parsedActors{};
                std::size_t parsedActorCount = 0;
                std::int32_t sexInt = -1;
                bool hasSex = false;
                Foundation::AgeBracket ageBracket = Foundation::AgeBracket::Auto;
                bool hasAge = false;
                RE::TESRace* raceOverride = nullptr;
                std::string name;

                for (const std::string_view tok : trailing) {
                    if (tok.empty()) {
                        continue;
                    }
                    std::int32_t tmpSex = -1;
                    if (!hasSex && TryParseSex(tok, tmpSex)) {
                        sexInt = tmpSex;
                        hasSex = true;
                        continue;
                    }
                    Foundation::AgeBracket tmpAge = Foundation::AgeBracket::Auto;
                    if (!hasAge && TryParseAgeBracket(tok, tmpAge)) {
                        ageBracket = tmpAge;
                        hasAge = true;
                        continue;
                    }
                    RE::Actor* tmpActor = nullptr;
                    if (parsedActorCount < maxActorArgs && TryParseActorToken(tok, tmpActor)) {
                        parsedActors[parsedActorCount++] = tmpActor;
                        continue;
                    }
                    if (!raceOverride) {
                        if (auto* foundRace = FindRaceByToken(tok)) {
                            raceOverride = foundRace;
                            continue;
                        }
                    }
                    if (name.empty()) {
                        name = std::string(tok);
                        continue;
                    }
                    PrintConsoleLine(std::format("MORWHA: Unrecognized argument '{}'.", tok));
                    PrintUsage();
                    return true;
                }

                RE::Actor* worldTarget = ResolveCrosshairOrConsoleActor(a_thisObj);
                RE::Actor* spawned = nullptr;

                if (isRelativeCmd) {
                    RE::Actor* donorA = (parsedActorCount >= 1)
                                            ? parsedActors[0]
                                            : (worldTarget ? worldTarget : player);
                    if (name.empty()) {
                        name = "Relative Mortal";
                    }
                    App::MortalSpawnRequest req{};
                    req.anchor = player;
                    req.donorMode = Foundation::DonorMode::OneDonorRelative;
                    req.donorA = donorA;
                    req.raceOverride = raceOverride;
                    req.sexInt = sexInt;
                    req.ageBracket = ageBracket;
                    req.displayName = std::move(name);
                    req.slotKeyPrefix = "relative_mortal";
                    spawned = App::SynthesisCoordinator::GetSingleton().SpawnMortal(req).actor;
                } else {
                    RE::Actor* parentA = nullptr;
                    RE::Actor* parentB = nullptr;
                    if (parsedActorCount >= 2) {
                        parentA = parsedActors[0];
                        parentB = parsedActors[1];
                    } else if (parsedActorCount == 1) {
                        RE::Actor* tokActor = parsedActors[0];
                        if (worldTarget && !worldTarget->IsPlayerRef() && worldTarget != tokActor) {
                            parentA = worldTarget;
                            parentB = tokActor;
                        } else {
                            parentA = player;
                            parentB = tokActor;
                        }
                    } else {
                        parentA = player;
                        parentB = worldTarget;
                    }

                    if (!parentA || !parentB || parentA == parentB) {
                        PrintConsoleLine(
                            "MORWHA: Child synthesis needs 2 distinct parents (aim at/click an NPC or pass FormIDs: mspawn child <hexA> <hexB>).");
                        return true;
                    }
                    if (name.empty()) {
                        name = "Offspring Mortal";
                    }
                    App::MortalSpawnRequest req{};
                    req.anchor = player;
                    req.donorMode = Foundation::DonorMode::TwoDonorOffspring;
                    req.donorA = parentA;
                    req.donorB = parentB;
                    req.raceOverride = raceOverride;
                    req.sexInt = sexInt;
                    req.ageBracket = ageBracket;
                    req.displayName = std::move(name);
                    req.slotKeyPrefix = "offspring_mortal";
                    spawned = App::SynthesisCoordinator::GetSingleton().SpawnMortal(req).actor;
                }

                if (!spawned) {
                    PrintConsoleLine(
                        "MORWHA: Spawn failed; inspect Documents/My Games/Skyrim Special Edition/SKSE/MORWHA.log.");
                }
                return true;
            }

            std::array<std::string_view, 6> spawnTokens{ arg1, arg2, arg3, arg4, arg5, arg6 };
            std::size_t startIdx = 0;
            if (CaseInsensitiveEquals(spawnTokens[0], "spawn")) {
                startIdx = 1;
            }

            std::int32_t sexInt = 1;
            bool hasSex = false;
            Foundation::AgeBracket ageBracket = Foundation::AgeBracket::Auto;
            bool hasAge = false;
            RE::TESRace* race = nullptr;
            std::string name;

            for (std::size_t i = startIdx; i < spawnTokens.size(); ++i) {
                const std::string_view tok = spawnTokens[i];
                if (tok.empty()) {
                    continue;
                }
                std::int32_t tmpSex = 1;
                if (!hasSex && TryParseSex(tok, tmpSex)) {
                    sexInt = tmpSex;
                    hasSex = true;
                    continue;
                }
                Foundation::AgeBracket tmpAge = Foundation::AgeBracket::Auto;
                if (!hasAge && TryParseAgeBracket(tok, tmpAge)) {
                    ageBracket = tmpAge;
                    hasAge = true;
                    continue;
                }
                if (!race) {
                    if (auto* foundRace = FindRaceByToken(tok)) {
                        race = foundRace;
                        continue;
                    }
                }
                if (name.empty()) {
                    name = std::string(tok);
                    continue;
                }
                PrintConsoleLine(std::format("MORWHA: Unrecognized argument '{}'.", tok));
                PrintUsage();
                return true;
            }

            if (name.empty()) {
                name = "Test Mortal";
            }

            RE::Actor* anchor = a_thisObj ? a_thisObj->As<RE::Actor>() : nullptr;
            if (!anchor) {
                anchor = RE::PlayerCharacter::GetSingleton();
            }

            App::MortalSpawnRequest req{};
            req.anchor = anchor;
            req.donorMode = Foundation::DonorMode::ZeroDonorProcedural;
            req.raceOverride = race;
            req.sexInt = (sexInt == 0) ? 0 : 1;
            req.ageBracket = ageBracket;
            req.displayName = std::move(name);
            req.slotKeyPrefix = "test_mortal";
            auto* spawned = App::SynthesisCoordinator::GetSingleton().SpawnMortal(req).actor;
            if (!spawned) {
                PrintConsoleLine(
                    "MORWHA: Spawn failed; inspect Documents/My Games/Skyrim Special Edition/SKSE/MORWHA.log.");
            }
            return true;
        }

    }  // namespace

    bool RegisterConsoleCommands() {
        static bool s_registered = false;
        if (s_registered) {
            return true;
        }

        RE::SCRIPT_FUNCTION* cmd = nullptr;
        std::string_view claimedSlot{};
        for (const std::string_view candidate : kHostCandidates) {
            if (auto* found = RE::SCRIPT_FUNCTION::LocateConsoleCommand(candidate);
                found != nullptr && found->compileFunction != nullptr) {
                cmd = found;
                claimedSlot = candidate;
                break;
            }
        }

        if (!cmd) {
            MORWHA_LOG_ERROR("RegisterConsoleCommands: Could not locate an available SCRIPT_FUNCTION slot");
            return false;
        }

        static RE::SCRIPT_PARAMETER s_params[] = {
            { "Action or Sex (optional)", RE::SCRIPT_PARAM_TYPE::kChar, true },
            { "Arg 2 (optional)", RE::SCRIPT_PARAM_TYPE::kChar, true },
            { "Arg 3 (optional)", RE::SCRIPT_PARAM_TYPE::kChar, true },
            { "Arg 4 (optional)", RE::SCRIPT_PARAM_TYPE::kChar, true },
            { "Arg 5 (optional)", RE::SCRIPT_PARAM_TYPE::kChar, true },
            { "Arg 6 (optional)", RE::SCRIPT_PARAM_TYPE::kChar, true },
        };

        cmd->functionName = "morwha_spawn";
        cmd->shortName = "mspawn";
        cmd->helpString = "mspawn [rel|child|reroll|gc|set|reload|female|male|status|list|help] [Args...]";
        cmd->referenceFunction = false;
        cmd->SetParameters(s_params);
        cmd->executeFunction = &ExecuteMorwhaConsoleCommand;
        cmd->conditionFunction = nullptr;
        cmd->editorFilter = false;
        cmd->invalidatesCellList = false;

        s_registered = true;
        MORWHA_LOG_INFO(
            "Registered console command 'morwha_spawn' ('mspawn') on slot '{}'",
            claimedSlot);
        return true;
    }

}  // namespace MORWHA::Bridge::Papyrus
