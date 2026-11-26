#include "morwha/bridge_papyrus/PapyrusInterface.h"

#include "morwha/app/SynthesisCoordinator.h"
#include "morwha/engine/EngineLogging.h"

#include <string>
#include <string_view>

namespace MORWHA::Bridge::Papyrus {

    namespace {

        constexpr const char* kScriptClassName = "MORWHA";

        std::string GetVersion(RE::StaticFunctionTag*) {
            return App::SynthesisCoordinator::GetSingleton().GetVersion();
        }

        bool IsReady(RE::StaticFunctionTag*) {
            return App::SynthesisCoordinator::GetSingleton().IsReady();
        }

        RE::Actor* SpawnTestMortal(
            RE::StaticFunctionTag*,
            RE::Actor* anchor,
            RE::TESRace* race,
            std::int32_t sex,
            RE::BSFixedString name) {
            App::MortalSpawnRequest req{};
            req.anchor = anchor;
            req.donorMode = Foundation::DonorMode::ZeroDonorProcedural;
            req.raceOverride = race;
            req.sexInt = (sex == 0) ? 0 : 1;
            req.displayName = name.c_str() ? std::string(name.c_str()) : std::string{};
            req.slotKeyPrefix = "test_mortal";
            return App::SynthesisCoordinator::GetSingleton().SpawnMortal(req).actor;
        }

        RE::Actor* SpawnRelativeMortal(
            RE::StaticFunctionTag*,
            RE::Actor* anchor,
            RE::Actor* donor,
            RE::TESRace* race,
            std::int32_t sex,
            RE::BSFixedString name) {
            App::MortalSpawnRequest req{};
            req.anchor = anchor;
            req.donorMode = Foundation::DonorMode::OneDonorRelative;
            req.donorA = donor;
            req.raceOverride = race;
            req.sexInt = sex;
            req.displayName = name.c_str() ? std::string(name.c_str()) : std::string{};
            req.slotKeyPrefix = "relative_mortal";
            return App::SynthesisCoordinator::GetSingleton().SpawnMortal(req).actor;
        }

        RE::Actor* SpawnOffspringMortal(
            RE::StaticFunctionTag*,
            RE::Actor* anchor,
            RE::Actor* parentA,
            RE::Actor* parentB,
            RE::TESRace* race,
            std::int32_t sex,
            RE::BSFixedString name) {
            App::MortalSpawnRequest req{};
            req.anchor = anchor;
            req.donorMode = Foundation::DonorMode::TwoDonorOffspring;
            req.donorA = parentA;
            req.donorB = parentB;
            req.raceOverride = race;
            req.sexInt = sex;
            req.displayName = name.c_str() ? std::string(name.c_str()) : std::string{};
            req.slotKeyPrefix = "offspring_mortal";
            return App::SynthesisCoordinator::GetSingleton().SpawnMortal(req).actor;
        }

        bool RerollMortal(
            RE::StaticFunctionTag*,
            RE::Actor* targetActor,
            RE::TESRace* raceOverride,
            bool rerollRaceFromDonors,
            std::int32_t sexOverride,
            RE::BSFixedString nameOverride) {
            App::MortalRerollRequest req{};
            req.raceOverride = raceOverride;
            req.rerollRaceFromDonors = rerollRaceFromDonors;
            req.sexOverride = sexOverride;
            req.nameOverride =
                nameOverride.c_str() ? std::string(nameOverride.c_str()) : std::string{};
            return App::SynthesisCoordinator::GetSingleton().RerollMortal(targetActor, req);
        }

        RE::Actor* SpawnMortalJson(
            RE::StaticFunctionTag*,
            RE::Actor* anchor,
            RE::BSFixedString optionsJson) {
            return App::SynthesisCoordinator::GetSingleton().SpawnMortalJson(
                anchor,
                optionsJson.c_str() ? std::string_view(optionsJson.c_str()) : std::string_view{});
        }

        bool UpdateMortalJson(
            RE::StaticFunctionTag*,
            RE::Actor* targetActor,
            RE::BSFixedString patchJson) {
            return App::SynthesisCoordinator::GetSingleton().UpdateMortalJson(
                targetActor,
                patchJson.c_str() ? std::string_view(patchJson.c_str()) : std::string_view{});
        }

        std::string GetMortalHeritageJson(
            RE::StaticFunctionTag*,
            RE::Actor* targetActor) {
            return App::SynthesisCoordinator::GetSingleton().QueryMortalHeritageJson(targetActor);
        }

    }  // namespace

    bool RegisterPapyrusFunctions(RE::BSScript::IVirtualMachine* vm) {
        if (!vm) {
            MORWHA_LOG_ERROR("RegisterPapyrusFunctions: IVirtualMachine is null");
            return false;
        }

        vm->RegisterFunction("GetVersion", kScriptClassName, GetVersion, true);
        vm->RegisterFunction("IsReady", kScriptClassName, IsReady, true);
        vm->RegisterFunction("SpawnTestMortal", kScriptClassName, SpawnTestMortal, false);
        vm->RegisterFunction("SpawnRelativeMortal", kScriptClassName, SpawnRelativeMortal, false);
        vm->RegisterFunction("SpawnOffspringMortal", kScriptClassName, SpawnOffspringMortal, false);
        vm->RegisterFunction("RerollMortal", kScriptClassName, RerollMortal, false);
        vm->RegisterFunction("SpawnMortalJson", kScriptClassName, SpawnMortalJson, false);
        vm->RegisterFunction("UpdateMortalJson", kScriptClassName, UpdateMortalJson, false);
        vm->RegisterFunction("GetMortalHeritageJson", kScriptClassName, GetMortalHeritageJson, false);

        MORWHA_LOG_INFO("Registered MORWHA Papyrus native functions");
        return true;
    }

}  // namespace MORWHA::Bridge::Papyrus
