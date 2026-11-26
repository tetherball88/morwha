#include "morwha/engine/ChimeraEngineSupport.h"

#include "morwha/engine/ChimeraAccessoryCatalog.h"
#include "morwha/engine/HybridSkinService.h"

#include <string_view>

namespace MORWHA::Engine {

    namespace {

        [[nodiscard]] bool IsAnyLoadedMod(
            RE::TESDataHandler* dataHandler,
            std::string_view filename) noexcept {
            if (!dataHandler || filename.empty()) {
                return false;
            }
            return (dataHandler->LookupLoadedModByName(filename) != nullptr) ||
                   (dataHandler->LookupLoadedLightModByName(filename) != nullptr);
        }

    }  // namespace

    InstalledChimeraModsReport InspectInstalledChimeraMods() noexcept {
        InstalledChimeraModsReport report{};
        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            return report;
        }

        const auto tier2Avail = ChimeraAccessoryCatalog::GetSingleton().GetTierAvailability();
        report.wearableBeastPartsLoaded =
            IsAnyLoadedMod(dataHandler, "ChooeyDintEarsEdit.esp") ||
            IsAnyLoadedMod(dataHandler, "NK_HornSlider.esp") ||
            IsAnyLoadedMod(dataHandler, "PraedysHorns.esp") ||
            IsAnyLoadedMod(dataHandler, "FeralEyes.esp") ||
            IsAnyLoadedMod(dataHandler, "Sharpened Teeth - Character Creation.esp") ||
            tier2Avail.tier2KhajiitAccessoriesReady ||
            tier2Avail.tier2ArgonianAccessoriesReady;
        return report;
    }

    Core::TierAvailability QueryActiveChimeraTierAvailability() noexcept {
        auto avail = ChimeraAccessoryCatalog::GetSingleton().GetTierAvailability();
        const auto& hyb = HybridSkinService::GetSingleton();
        if (hyb.IsCoatGroupReadyForSettings(Core::HybridCoatGroup::HumanoidKhajiitFur, Foundation::Sex::Female, true) &&
            hyb.IsCoatGroupReadyForSettings(Core::HybridCoatGroup::HumanoidKhajiitFur, Foundation::Sex::Male, true)) {
            avail.tier2KhajiitAccessoriesReady = true;
        }
        if (hyb.IsCoatGroupReadyForSettings(Core::HybridCoatGroup::HumanoidArgonianLizard, Foundation::Sex::Female, true) &&
            hyb.IsCoatGroupReadyForSettings(Core::HybridCoatGroup::HumanoidArgonianLizard, Foundation::Sex::Male, true)) {
            avail.tier2ArgonianAccessoriesReady = true;
        }
        if (hyb.IsCoatGroupReadyForSettings(Core::HybridCoatGroup::ArgonianKhajiitFur, Foundation::Sex::Female, true) &&
            hyb.IsCoatGroupReadyForSettings(Core::HybridCoatGroup::ArgonianKhajiitFur, Foundation::Sex::Male, true)) {
            avail.tier2KhajiitOnArgonianReady = true;
        }
        if (hyb.IsCoatGroupReadyForSettings(Core::HybridCoatGroup::KhajiitArgonianLizard, Foundation::Sex::Female, true) &&
            hyb.IsCoatGroupReadyForSettings(Core::HybridCoatGroup::KhajiitArgonianLizard, Foundation::Sex::Male, true)) {
            avail.tier2ArgonianOnKhajiitReady = true;
        }
        return avail;
    }

    Core::TierAvailability QueryActiveChimeraTierAvailability(
        const SynthesisSettings& settings) noexcept {
        auto avail = ChimeraAccessoryCatalog::GetSingleton().GetTierAvailability(
            settings.khajiitTier2, settings.argonianTier2);
        const auto& hyb = HybridSkinService::GetSingleton();
        if (settings.khajiitTier2.enableHybridSkin) {
            if (hyb.IsCoatGroupReadyForSettings(
                    Core::HybridCoatGroup::HumanoidKhajiitFur,
                    Foundation::Sex::Female,
                    settings.enableGeneratedHybridSkins) &&
                hyb.IsCoatGroupReadyForSettings(
                    Core::HybridCoatGroup::HumanoidKhajiitFur,
                    Foundation::Sex::Male,
                    settings.enableGeneratedHybridSkins)) {
                avail.tier2KhajiitAccessoriesReady = true;
            }
            if (hyb.IsCoatGroupReadyForSettings(
                    Core::HybridCoatGroup::ArgonianKhajiitFur,
                    Foundation::Sex::Female,
                    settings.enableGeneratedHybridSkins) &&
                hyb.IsCoatGroupReadyForSettings(
                    Core::HybridCoatGroup::ArgonianKhajiitFur,
                    Foundation::Sex::Male,
                    settings.enableGeneratedHybridSkins)) {
                avail.tier2KhajiitOnArgonianReady = true;
            }
        }
        if (settings.argonianTier2.enableHybridSkin) {
            if (hyb.IsCoatGroupReadyForSettings(
                    Core::HybridCoatGroup::HumanoidArgonianLizard,
                    Foundation::Sex::Female,
                    settings.enableGeneratedHybridSkins) &&
                hyb.IsCoatGroupReadyForSettings(
                    Core::HybridCoatGroup::HumanoidArgonianLizard,
                    Foundation::Sex::Male,
                    settings.enableGeneratedHybridSkins)) {
                avail.tier2ArgonianAccessoriesReady = true;
            }
            if (hyb.IsCoatGroupReadyForSettings(
                    Core::HybridCoatGroup::KhajiitArgonianLizard,
                    Foundation::Sex::Female,
                    settings.enableGeneratedHybridSkins) &&
                hyb.IsCoatGroupReadyForSettings(
                    Core::HybridCoatGroup::KhajiitArgonianLizard,
                    Foundation::Sex::Male,
                    settings.enableGeneratedHybridSkins)) {
                avail.tier2ArgonianOnKhajiitReady = true;
            }
        }
        avail.tier2HumanoidAccessoriesReady =
            avail.tier2KhajiitAccessoriesReady && avail.tier2ArgonianAccessoriesReady;
        return avail;
    }

    Core::TierAvailability QueryActiveChimeraTierAvailability(
        const SynthesisSettings& settings,
        Foundation::Sex targetSex) noexcept {
        auto avail = ChimeraAccessoryCatalog::GetSingleton().GetTierAvailability(
            settings.khajiitTier2, settings.argonianTier2);
        const auto& hyb = HybridSkinService::GetSingleton();
        if (settings.khajiitTier2.enableHybridSkin) {
            if (hyb.IsCoatGroupReadyForSettings(
                    Core::HybridCoatGroup::HumanoidKhajiitFur,
                    targetSex,
                    settings.enableGeneratedHybridSkins)) {
                avail.tier2KhajiitAccessoriesReady = true;
            }
            if (hyb.IsCoatGroupReadyForSettings(
                    Core::HybridCoatGroup::ArgonianKhajiitFur,
                    targetSex,
                    settings.enableGeneratedHybridSkins)) {
                avail.tier2KhajiitOnArgonianReady = true;
            }
        }
        if (settings.argonianTier2.enableHybridSkin) {
            if (hyb.IsCoatGroupReadyForSettings(
                    Core::HybridCoatGroup::HumanoidArgonianLizard,
                    targetSex,
                    settings.enableGeneratedHybridSkins)) {
                avail.tier2ArgonianAccessoriesReady = true;
            }
            if (hyb.IsCoatGroupReadyForSettings(
                    Core::HybridCoatGroup::KhajiitArgonianLizard,
                    targetSex,
                    settings.enableGeneratedHybridSkins)) {
                avail.tier2ArgonianOnKhajiitReady = true;
            }
        }
        avail.tier2HumanoidAccessoriesReady =
            avail.tier2KhajiitAccessoriesReady && avail.tier2ArgonianAccessoriesReady;
        return avail;
    }

}  // namespace MORWHA::Engine
