#include "morwha/engine/ChimeraAccessoryCatalog.h"

#include "morwha/engine/EngineLogging.h"
#include "morwha/engine/FormRefResolver.h"
#include "morwha/engine/HeadPartPoolIndexer.h"
#include "morwha/engine/HybridSkinService.h"
#include "morwha/engine/TesNpcHydrator.h"
#include "morwha/engine/VfsAssetReader.h"
#include "morwha/state/ChimeraAccessoryManifest.h"

#include <Windows.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <memory>
#include <random>
#include <string_view>

namespace MORWHA::Engine {

    namespace {

        constexpr std::uint32_t kNakedTailKhajiitFormId = 0x0B79B9u;
        constexpr std::uint32_t kNakedTailArgonianFormId = 0x069CE2u;
        constexpr std::uint32_t kHeadPartsAllRacesMinusBeastFormId = 0x0A803Fu;
        constexpr std::uint32_t kNordRaceFormId = 0x013746u;
        constexpr std::uint32_t kRaceMenuHornHeadPartType = 32u;

        [[nodiscard]] std::string NormalizeLowerVfsMeshKey(const char* rawPath) {
            if (!rawPath || rawPath[0] == '\0') {
                return {};
            }
            std::string vfs = BuildMeshVfsPath(rawPath);
            for (char& ch : vfs) {
                if (ch == '/') {
                    ch = '\\';
                } else {
                    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                }
            }
            return vfs;
        }

        [[nodiscard]] std::uint8_t LineageToBitMask(State::ChimeraManifestLineage lineage) noexcept {
            switch (lineage) {
            case State::ChimeraManifestLineage::HumanoidKhajiit:
                return 0x1u;
            case State::ChimeraManifestLineage::HumanoidArgonian:
                return 0x2u;
            case State::ChimeraManifestLineage::AnyChimera:
                return 0x3u;
            }
            return 0u;
        }

        [[nodiscard]] std::uint8_t DomainLineageBit(Core::ChimeraLineageKind kind) noexcept {
            switch (kind) {
            case Core::ChimeraLineageKind::HumanoidKhajiit:
                return 0x1u;
            case Core::ChimeraLineageKind::HumanoidArgonian:
                return 0x2u;
            default:
                return 0u;
            }
        }

        [[nodiscard]] std::uint8_t SexFilterToMask(State::ChimeraAccessorySexFilter sex) noexcept {
            switch (sex) {
            case State::ChimeraAccessorySexFilter::Male:
                return 0x1u;
            case State::ChimeraAccessorySexFilter::Female:
                return 0x2u;
            case State::ChimeraAccessorySexFilter::Any:
                return 0x3u;
            }
            return 0x3u;
        }

        [[nodiscard]] std::uint8_t DomainSexBit(Foundation::Sex sex) noexcept {
            return (sex == Foundation::Sex::Male) ? 0x1u : 0x2u;
        }

        [[nodiscard]] bool VfsAssetExists(std::string_view vfsPath) {
            if (vfsPath.empty()) {
                return false;
            }
            std::array<std::uint8_t, 16u> hdr{};
            std::size_t bytesRead = 0u;
            std::size_t totalBytes = 0u;
            return ReadVfsFileHeaderAndTotalSize(vfsPath, hdr, bytesRead, totalBytes) &&
                   totalBytes > 0u;
        }

        /// Resolves a HeadPart model path in VFS, falling back to stripping accidental
        /// mod-author "<mod>\Meshes\" prefixes (e.g. "Sharpened Teeth\Meshes\MouthHumanF.NIF" -> "MouthHumanF.NIF").
        [[nodiscard]] std::string ResolveHeadPartModelRelPath(
            std::string_view rawModel,
            bool& outOverridden) {
            outOverridden = false;
            if (rawModel.empty()) {
                return {};
            }
            if (VfsAssetExists(BuildMeshVfsPath(rawModel))) {
                return std::string(rawModel);
            }

            std::string normalized;
            normalized.reserve(rawModel.size());
            std::string lowerNorm;
            lowerNorm.reserve(rawModel.size());
            for (const char ch : rawModel) {
                const char slash = (ch == '/') ? '\\' : ch;
                normalized.push_back(slash);
                lowerNorm.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(slash))));
            }
            constexpr std::string_view kMeshesNeedle = "\\meshes\\";
            const std::size_t pos = lowerNorm.find(kMeshesNeedle);
            if (pos != std::string::npos) {
                std::string stripped = normalized.substr(pos + kMeshesNeedle.size());
                if (!stripped.empty() && VfsAssetExists(BuildMeshVfsPath(stripped))) {
                    outOverridden = true;
                    return stripped;
                }
            }
            return {};
        }

        [[nodiscard]] bool VerifyAndResolveHeadPartMeshes(
            const RE::BGSHeadPart* hp,
            std::string& outResolvedModelRelPath,
            bool& outNeedsOverride) {
            outResolvedModelRelPath.clear();
            outNeedsOverride = false;
            if (!hp || hp->model.c_str() == nullptr || hp->model.c_str()[0] == '\0') {
                return false;
            }
            outResolvedModelRelPath = ResolveHeadPartModelRelPath(hp->model.c_str(), outNeedsOverride);
            if (outResolvedModelRelPath.empty()) {
                return false;
            }
            for (const auto* extra : hp->extraParts) {
                if (!extra || extra->model.c_str() == nullptr || extra->model.c_str()[0] == '\0') {
                    return false;
                }
                bool dummyOverride = false;
                if (ResolveHeadPartModelRelPath(extra->model.c_str(), dummyOverride).empty()) {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool VerifyArmaMeshesExist(
            const RE::TESObjectARMA* arma,
            State::ChimeraAccessorySexFilter sexFilter) {
            if (!arma) {
                return false;
            }
            const auto hasModel = [&](RE::SEX sexIdx) {
                const char* model = arma->bipedModels[sexIdx].model.c_str();
                return (model != nullptr) && (model[0] != '\0') &&
                       VfsAssetExists(BuildMeshVfsPath(model));
            };
            switch (sexFilter) {
            case State::ChimeraAccessorySexFilter::Male:
                return hasModel(RE::SEXES::kMale);
            case State::ChimeraAccessorySexFilter::Female:
                return hasModel(RE::SEXES::kFemale);
            case State::ChimeraAccessorySexFilter::Any:
                return hasModel(RE::SEXES::kMale) && hasModel(RE::SEXES::kFemale);
            }
            return false;
        }

        /// Deep-copies TESModelTextureSwap explicitly because TESModelTextureSwap::CopyComponent
        /// is a stripped editor-only no-op in SkyrimSE.exe runtime builds.
        void CopyModelTextureSwap(
            RE::TESModelTextureSwap& dst,
            const RE::TESModelTextureSwap& src) {
            const char* modelPath = src.GetModel();
            dst.SetModel((modelPath && modelPath[0] != '\0') ? modelPath : "");
            if (src.alternateTextures && src.numAlternateTextures > 0u) {
                auto* arr =
                    RE::calloc<RE::TESModelTextureSwap::AlternateTexture>(src.numAlternateTextures);
                if (arr) {
                    for (std::uint32_t i = 0; i < src.numAlternateTextures; ++i) {
                        std::construct_at(&arr[i]);
                        arr[i].textureSet = src.alternateTextures[i].textureSet;
                        arr[i].index3D = src.alternateTextures[i].index3D;
                        arr[i].unk0C = src.alternateTextures[i].unk0C;
                        arr[i].name3D = src.alternateTextures[i].name3D;
                    }
                    dst.alternateTextures = arr;
                    dst.numAlternateTextures = src.numAlternateTextures;
                }
            }
        }

        template <typename T>
        [[nodiscard]] T* DeterministicPick(
            const std::vector<T*>& candidates,
            Foundation::Seed64 seed,
            std::uint64_t salt) {
            if (candidates.empty()) {
                return nullptr;
            }
            std::mt19937_64 rng(seed.raw() ^ salt);
            std::uniform_int_distribution<std::size_t> dist(0u, candidates.size() - 1u);
            return candidates[dist(rng)];
        }

    }  // namespace

    ChimeraAccessoryCatalog& ChimeraAccessoryCatalog::GetSingleton() noexcept {
        static ChimeraAccessoryCatalog instance;
        return instance;
    }

    void ChimeraAccessoryCatalog::AppendUniqueRaces(
        RE::TESObjectARMA* arma,
        const std::vector<RE::TESRace*>& humanoidRaces) {
        if (!arma) {
            return;
        }
        for (auto* r : humanoidRaces) {
            if (!r || arma->race == r) {
                continue;
            }
            const bool alreadyPresent = std::ranges::any_of(arma->additionalRaces, [r](const RE::TESRace* existing) {
                return existing == r;
            });
            if (!alreadyPresent) {
                arma->additionalRaces.push_back(r);
            }
        }
    }

    RE::TESObjectARMA* ChimeraAccessoryCatalog::CloneRuntimeArmaForHumanoids(
        RE::TESObjectARMA* srcArma,
        std::string_view editorId) const {
        if (!srcArma) {
            return nullptr;
        }
        auto* clone = RE::IFormFactory::Create<RE::TESObjectARMA>();
        if (!clone) {
            return nullptr;
        }

        clone->race = srcArma->race;
        clone->bipedModelData = srcArma->bipedModelData;
        clone->data = srcArma->data;
        for (const auto idx : { RE::SEXES::kMale, RE::SEXES::kFemale }) {
            CopyModelTextureSwap(clone->bipedModels[idx], srcArma->bipedModels[idx]);
            CopyModelTextureSwap(clone->bipedModel1stPersons[idx], srcArma->bipedModel1stPersons[idx]);
            clone->skinTextures[idx] = srcArma->skinTextures[idx];
            clone->skinTextureSwapLists[idx] = srcArma->skinTextureSwapLists[idx];
        }
        clone->footstepSet = srcArma->footstepSet;
        clone->artObject = srcArma->artObject;
        for (auto* r : srcArma->additionalRaces) {
            if (r) {
                clone->additionalRaces.push_back(r);
            }
        }
        AppendUniqueRaces(clone, humanoidRaces_);

        const std::string edidOwned(editorId);
        clone->SetFormEditorID(edidOwned.c_str());
        clone->InitItem();
        return clone;
    }

    bool ChimeraAccessoryCatalog::MatchesEntryFilter(
        const RE::BGSHeadPart* part,
        std::uint8_t sexMask,
        const RE::TESRace* race,
        Foundation::Sex sex) noexcept {
        if (!part || !race) {
            return false;
        }
        if ((sexMask & DomainSexBit(sex)) == 0u) {
            return false;
        }
        if (part->flags.any(RE::BGSHeadPart::Flag::kMale, RE::BGSHeadPart::Flag::kFemale)) {
            const bool wantFemale = (sex == Foundation::Sex::Female);
            if (wantFemale ? !part->flags.all(RE::BGSHeadPart::Flag::kFemale)
                           : !part->flags.all(RE::BGSHeadPart::Flag::kMale)) {
                return false;
            }
        }
        if (part->validRaces) {
            if (!part->validRaces->HasForm(race) &&
                !(race->morphRace && part->validRaces->HasForm(race->morphRace))) {
                // Multi-race humanoid accessory headParts (e.g. NK_HornSlider) can be cloned onto Khajiit via humanoidValidRacesList_.
                if (!(ResolveRaceGroup(race) == Core::RaceGroup::Khajiit &&
                      part->validRaces->forms.size() >= 4u)) {
                    return false;
                }
            }
        }
        return true;
    }

    RE::BGSTextureSet* ChimeraAccessoryCatalog::BuildMouthTextureSetIfPresent(
        std::string_view folderName,
        std::string_view editorId) {
        const std::string relDir = std::format("Actors\\Character\\Mouth\\{}\\", folderName);
        const std::string diffuseRel = relDir + "mouthhuman.dds";
        if (!VfsAssetExists(BuildTextureVfsPath(diffuseRel))) {
            return nullptr;
        }

        auto* txst = RE::IFormFactory::Create<RE::BGSTextureSet>();
        if (!txst) {
            return nullptr;
        }

        txst->flags = RE::BGSTextureSet::Flag::kNone;
        txst->textures[RE::BSTextureSet::Texture::kDiffuse].textureName = diffuseRel.c_str();

        const std::string normRel = relDir + "mouthhuman_n.dds";
        if (VfsAssetExists(BuildTextureVfsPath(normRel))) {
            txst->textures[RE::BSTextureSet::Texture::kNormal].textureName = normRel.c_str();
        }
        const std::string skRel = relDir + "mouthhuman_sk.dds";
        if (VfsAssetExists(BuildTextureVfsPath(skRel))) {
            txst->textures[RE::BSTextureSet::Texture::kSubsurfaceTint].textureName = skRel.c_str();
        }
        const std::string specRel = relDir + "mouthhuman_s.dds";
        if (VfsAssetExists(BuildTextureVfsPath(specRel))) {
            txst->textures[RE::BSTextureSet::Texture::kSpecular].textureName = specRel.c_str();
        }

        const std::string edidOwned(editorId);
        txst->SetFormEditorID(edidOwned.c_str());
        txst->InitItem();
        return txst;
    }

    void ChimeraAccessoryCatalog::InitializeAtDataLoaded() {
        humanoidRaces_.clear();
        humanoidValidRacesList_ = nullptr;
        canonicalHumanMouthSignatures_.clear();
        clonedTailKhajiit_ = nullptr;
        clonedTailArgonian_ = nullptr;
        sharpMouthTxst_ = nullptr;

        khajiitEarParts_.clear();
        argonianHornParts_.clear();
        chimeraMouthParts_.clear();
        argonianSkinHornAddons_.clear();
        approvedHeadParts_.clear();
        approvedSkinAddons_.clear();
        runtimeMouthCache_.clear();
        runtimeCatalogMouthCache_.clear();
        runtimeMouthSet_.clear();
        runtimePromotedHornCache_.clear();
        runtimePromotedHornSet_.clear();
        runtimeClonedEarSet_.clear();
        runtimeRaceCompatHeadPartCache_.clear();
        runtimeAccessoryHeadPartCloneSet_.clear();
        runtimeSkinCache_.clear();
        runtimeChimeraSkinSet_.clear();
        {
            std::lock_guard lock(accessorySyncMutex_);
            npcChimeraStates_.clear();
        }

        skee64Loaded_ = (::GetModuleHandleW(L"skee64.dll") != nullptr);

        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            initialized_ = true;
            return;
        }

        std::vector<RE::TESRace*> beastPlayableRaces;
        for (auto* race : dataHandler->GetFormArray<RE::TESRace>()) {
            if (!race || race->IsChildRace()) {
                continue;
            }
            const auto grp = ResolveRaceGroup(race);
            if (grp == Core::RaceGroup::Humanoid) {
                humanoidRaces_.push_back(race);
            } else if (grp == Core::RaceGroup::Khajiit || grp == Core::RaceGroup::Argonian) {
                beastPlayableRaces.push_back(race);
            }
        }

        if (auto* validList = RE::IFormFactory::Create<RE::BGSListForm>()) {
            if (auto* baseList = dataHandler->LookupForm<RE::BGSListForm>(
                    kHeadPartsAllRacesMinusBeastFormId, "Skyrim.esm")) {
                for (auto* form : baseList->forms) {
                    if (form) {
                        validList->forms.push_back(form);
                    }
                }
            }
            for (auto* race : humanoidRaces_) {
                if (!race) {
                    continue;
                }
                const bool present = std::ranges::any_of(
                    validList->forms, [race](const RE::TESForm* existing) {
                        return existing == race;
                    });
                if (!present) {
                    validList->forms.push_back(race);
                }
            }
            for (auto* race : beastPlayableRaces) {
                if (!race) {
                    continue;
                }
                const bool present = std::ranges::any_of(
                    validList->forms, [race](const RE::TESForm* existing) {
                        return existing == race;
                    });
                if (!present) {
                    validList->forms.push_back(race);
                }
            }
            validList->SetFormEditorID("MORWHA_ValidRaces_Humanoid");
            validList->InitItem();
            humanoidValidRacesList_ = validList;
        }

        auto* vanillaKhajiitTail =
            dataHandler->LookupForm<RE::TESObjectARMA>(kNakedTailKhajiitFormId, "Skyrim.esm");
        auto* vanillaArgonianTail =
            dataHandler->LookupForm<RE::TESObjectARMA>(kNakedTailArgonianFormId, "Skyrim.esm");
        clonedTailKhajiit_ =
            CloneRuntimeArmaForHumanoids(vanillaKhajiitTail, "MORWHA_ARMA_TailKhajiit");
        clonedTailArgonian_ =
            CloneRuntimeArmaForHumanoids(vanillaArgonianTail, "MORWHA_ARMA_TailArgonian");

        sharpMouthTxst_ = BuildMouthTextureSetIfPresent("Sharp", "MORWHA_Txst_MouthSharp");

        if (auto* nordRace = dataHandler->LookupForm<RE::TESRace>(kNordRaceFormId, "Skyrim.esm")) {
            auto recordMouthSig = [&](const RE::BGSHeadPart* hp) {
                if (!hp || hp->type.get() != RE::BGSHeadPart::HeadPartType::kMisc ||
                    !hp->extraParts.empty()) {
                    return;
                }
                CanonicalMouthSignature sig{
                    .modelVfs = NormalizeLowerVfsMeshKey(hp->model.c_str()),
                    .chargenTriVfs = NormalizeLowerVfsMeshKey(
                        hp->morphs[RE::BGSHeadPart::MorphIndices::kChargenMorph].model.c_str())
                };
                if (!sig.modelVfs.empty() && !sig.chargenTriVfs.empty() &&
                    std::ranges::find(canonicalHumanMouthSignatures_, sig) ==
                        canonicalHumanMouthSignatures_.end()) {
                    canonicalHumanMouthSignatures_.push_back(std::move(sig));
                }
            };
            const auto& indexer = HeadPartPoolIndexer::GetSingleton();
            for (const auto sex : { Foundation::Sex::Male, Foundation::Sex::Female }) {
                const auto reSex =
                    (sex == Foundation::Sex::Female) ? RE::SEXES::kFemale : RE::SEXES::kMale;
                recordMouthSig(
                    nordRace->GetHeadPartByType(RE::BGSHeadPart::HeadPartType::kMisc, reSex));
                if (indexer.IsBuilt()) {
                    if (const auto* vPool = indexer.GetPool(
                            nordRace, sex, Foundation::TopologyPool::Vanilla)) {
                        recordMouthSig(vPool->primaryMouth);
                    }
                    if (const auto* hPool = indexer.GetPool(
                            nordRace, sex, Foundation::TopologyPool::HighPolyHead)) {
                        recordMouthSig(hPool->primaryMouth);
                    }
                }
            }
        }

        std::size_t filesScanned = 0u;
        std::size_t filesFailed = 0u;
        const auto manifests = State::LoadChimeraAccessoryManifestsFromDirectory(
            State::kDefaultChimeraAccessoriesDir, &filesScanned, &filesFailed);

        std::size_t skippedMissingPlugin = 0u;
        std::size_t skippedNoSkee = 0u;
        std::size_t skippedMissingMesh = 0u;

        for (const auto& manifest : manifests) {
            if (!dataHandler->LookupLoadedModByName(manifest.plugin) &&
                !dataHandler->LookupLoadedLightModByName(manifest.plugin)) {
                ++skippedMissingPlugin;
                continue;
            }
            if (manifest.requiresSkee64 && !skee64Loaded_) {
                ++skippedNoSkee;
                continue;
            }

            for (const auto& entry : manifest.entries) {
                const State::PluginFormRef ref{ manifest.plugin, entry.localId };
                auto* form = ResolvePluginForm(ref);
                if (!form) {
                    continue;
                }
                const std::uint8_t lMask = LineageToBitMask(entry.lineage);
                const std::uint8_t sMask = SexFilterToMask(entry.sex);

                if (entry.slotKind == State::ChimeraAccessorySlotKind::HeadPart) {
                    auto* hp = form->As<RE::BGSHeadPart>();
                    if (!hp || hp->IsExtraPart() ||
                        (hp->GetFormFlags() & RE::BGSHeadPart::RecordFlags::kNonPlayable) != 0) {
                        continue;
                    }
                    std::string resolvedModelRelPath;
                    bool needsModelOverride = false;
                    if (!VerifyAndResolveHeadPartMeshes(hp, resolvedModelRelPath, needsModelOverride)) {
                        ++skippedMissingMesh;
                        continue;
                    }
                    const bool isMouth =
                        (hp->type.get() == RE::BGSHeadPart::HeadPartType::kMisc);
                    approvedHeadParts_[hp] = HeadPartEntry{
                        .part = hp,
                        .lineageMask = lMask,
                        .sexMask = sMask,
                        .isMouth = isMouth,
                        .resolvedModelRelPath = std::move(resolvedModelRelPath),
                        .needsModelOverride = needsModelOverride
                    };
                    if (isMouth) {
                        chimeraMouthParts_.push_back(hp);
                    } else {
                        if ((lMask & 0x1u) != 0u) {
                            khajiitEarParts_.push_back(hp);
                        }
                        if ((lMask & 0x2u) != 0u) {
                            argonianHornParts_.push_back(hp);
                        }
                    }
                } else if (entry.slotKind == State::ChimeraAccessorySlotKind::SkinArmorAddon) {
                    auto* arma = form->As<RE::TESObjectARMA>();
                    if (!arma || !VerifyArmaMeshesExist(arma, entry.sex)) {
                        ++skippedMissingMesh;
                        continue;
                    }
                    approvedSkinAddons_[arma] = SkinAddonEntry{
                        .arma = arma,
                        .lineageMask = lMask,
                        .sexMask = sMask
                    };
                    if ((lMask & 0x2u) != 0u) {
                        argonianSkinHornAddons_.push_back(arma);
                    }
                }
            }
        }

        initialized_ = true;
        MORWHA_LOG_INFO(
            "ChimeraAccessoryCatalog: Initialized (skee64={}, clonedTails=({}/{}), bnpSharp={}, manifests={}, failed={}, skippedPlugin={}, skippedNoSkee={}, skippedMesh={}, catEars={}, hornHeadParts={}, skinHorns={}, mouths={})",
            skee64Loaded_,
            clonedTailKhajiit_ != nullptr,
            clonedTailArgonian_ != nullptr,
            sharpMouthTxst_ != nullptr,
            manifests.size(),
            filesFailed,
            skippedMissingPlugin,
            skippedNoSkee,
            skippedMissingMesh,
            khajiitEarParts_.size(),
            argonianHornParts_.size(),
            argonianSkinHornAddons_.size(),
            chimeraMouthParts_.size());
    }

    Core::TierAvailability ChimeraAccessoryCatalog::GetTierAvailability() const noexcept {
        return GetTierAvailability(
            State::MakeDefaultKhajiitTier2Config(),
            State::MakeDefaultArgonianTier2Config());
    }

    Core::TierAvailability ChimeraAccessoryCatalog::GetTierAvailability(
        const State::ChimeraLineageFeatureConfig& khajiitCfg,
        const State::ChimeraLineageFeatureConfig& argonianCfg) const noexcept {
        Core::TierAvailability avail{};
        const bool khajiitAnyUsableFeature =
            khajiitCfg.enableEyes ||
            (khajiitCfg.enableHeadFeature && !khajiitEarParts_.empty()) ||
            khajiitCfg.enableTeeth ||
            (khajiitCfg.enableTails && clonedTailKhajiit_ != nullptr);
        const bool argonianAnyUsableFeature =
            argonianCfg.enableEyes ||
            (argonianCfg.enableHeadFeature && (!argonianHornParts_.empty() || !argonianSkinHornAddons_.empty())) ||
            argonianCfg.enableTeeth ||
            (argonianCfg.enableTails && clonedTailArgonian_ != nullptr);

        avail.tier2KhajiitAccessoriesReady =
            khajiitCfg.HasAnyEnabledFeature() &&
            khajiitAnyUsableFeature &&
            (!khajiitCfg.enableTails || clonedTailKhajiit_ != nullptr) &&
            (!khajiitCfg.enableHeadFeature || !khajiitEarParts_.empty());
        avail.tier2ArgonianAccessoriesReady =
            argonianCfg.HasAnyEnabledFeature() &&
            argonianAnyUsableFeature &&
            (!argonianCfg.enableTails || clonedTailArgonian_ != nullptr) &&
            (!argonianCfg.enableHeadFeature ||
             (!argonianHornParts_.empty() || !argonianSkinHornAddons_.empty()));
        avail.tier2HumanoidAccessoriesReady =
            avail.tier2KhajiitAccessoriesReady && avail.tier2ArgonianAccessoriesReady;
        return avail;
    }

    bool ChimeraAccessoryCatalog::HasBeastMouthTextureSet(
        State::BeastTeethKind teethKind) const noexcept {
        if (teethKind == State::BeastTeethKind::Sharp ||
            teethKind == State::BeastTeethKind::Fangs) {
            return sharpMouthTxst_ != nullptr;
        }
        return false;
    }

    bool ChimeraAccessoryCatalog::IsHumanMouthPartCompatible(
        const RE::BGSHeadPart* mouthPart) const {
        if (!mouthPart ||
            mouthPart->type.get() != RE::BGSHeadPart::HeadPartType::kMisc ||
            !mouthPart->extraParts.empty() ||
            canonicalHumanMouthSignatures_.empty()) {
            return false;
        }
        const CanonicalMouthSignature sig{
            .modelVfs = NormalizeLowerVfsMeshKey(mouthPart->model.c_str()),
            .chargenTriVfs = NormalizeLowerVfsMeshKey(
                mouthPart->morphs[RE::BGSHeadPart::MorphIndices::kChargenMorph].model.c_str())
        };
        return !sig.modelVfs.empty() && !sig.chargenTriVfs.empty() &&
               std::ranges::find(canonicalHumanMouthSignatures_, sig) !=
                   canonicalHumanMouthSignatures_.end();
    }

    bool ChimeraAccessoryCatalog::RaceUsesCanonicalHumanMouth(
        const RE::TESRace* race,
        Foundation::Sex sex) const {
        if (!race || canonicalHumanMouthSignatures_.empty()) {
            return false;
        }
        const auto& indexer = HeadPartPoolIndexer::GetSingleton();
        if (indexer.IsBuilt()) {
            const auto topo = indexer.SelectPreferredTopology(race, sex);
            if (const auto* pool = indexer.GetPool(race, sex, topo);
                pool && pool->primaryMouth) {
                return IsHumanMouthPartCompatible(pool->primaryMouth);
            }
        }
        const auto reSex =
            (sex == Foundation::Sex::Female) ? RE::SEXES::kFemale : RE::SEXES::kMale;
        if (const auto* nativeMouth = const_cast<RE::TESRace*>(race)->GetHeadPartByType(
                RE::BGSHeadPart::HeadPartType::kMisc, reSex)) {
            return IsHumanMouthPartCompatible(nativeMouth);
        }
        if (race->morphRace) {
            if (const auto* morphMouth = race->morphRace->GetHeadPartByType(
                    RE::BGSHeadPart::HeadPartType::kMisc, reSex)) {
                return IsHumanMouthPartCompatible(morphMouth);
            }
        }
        return false;
    }

    RE::BGSHeadPart* ChimeraAccessoryCatalog::PickEarHeadPart(
        Core::ChimeraLineageKind lineage,
        const RE::TESRace* race,
        Foundation::Sex sex,
        Foundation::Seed64 seed) const {
        if (lineage != Core::ChimeraLineageKind::HumanoidKhajiit || !race) {
            return nullptr;
        }
        std::vector<RE::BGSHeadPart*> filtered;
        filtered.reserve(khajiitEarParts_.size());
        for (auto* hp : khajiitEarParts_) {
            const auto it = approvedHeadParts_.find(hp);
            if (it != approvedHeadParts_.end() &&
                MatchesEntryFilter(hp, it->second.sexMask, race, sex)) {
                filtered.push_back(hp);
            }
        }
        return DeterministicPick(filtered, seed, 0xEA750001EA750001ull);
    }

    std::vector<RE::BGSHeadPart*> ChimeraAccessoryCatalog::GetValidHornHeadParts(
        Core::ChimeraLineageKind lineage,
        const RE::TESRace* race,
        Foundation::Sex sex) const {
        std::vector<RE::BGSHeadPart*> filtered;
        if ((lineage != Core::ChimeraLineageKind::HumanoidArgonian &&
             lineage != Core::ChimeraLineageKind::KhajiitArgonian) ||
            !race) {
            return filtered;
        }
        filtered.reserve(argonianHornParts_.size());
        for (auto* hp : argonianHornParts_) {
            const auto it = approvedHeadParts_.find(hp);
            if (it != approvedHeadParts_.end() &&
                MatchesEntryFilter(hp, it->second.sexMask, race, sex)) {
                filtered.push_back(hp);
            }
        }
        return filtered;
    }

    std::vector<RE::TESObjectARMA*> ChimeraAccessoryCatalog::GetValidSkinHornAddons(
        Core::ChimeraLineageKind lineage,
        const RE::TESRace* race,
        Foundation::Sex sex) const {
        std::vector<RE::TESObjectARMA*> filtered;
        const auto raceGrp = race ? ResolveRaceGroup(race) : Core::RaceGroup::Other;
        if ((lineage != Core::ChimeraLineageKind::HumanoidArgonian &&
             lineage != Core::ChimeraLineageKind::KhajiitArgonian) ||
            !race ||
            (raceGrp != Core::RaceGroup::Humanoid && raceGrp != Core::RaceGroup::Khajiit)) {
            return filtered;
        }
        const std::uint8_t sBit = DomainSexBit(sex);
        filtered.reserve(argonianSkinHornAddons_.size());
        for (auto* arma : argonianSkinHornAddons_) {
            const auto it = approvedSkinAddons_.find(arma);
            if (it != approvedSkinAddons_.end() && (it->second.sexMask & sBit) != 0u) {
                filtered.push_back(arma);
            }
        }
        return filtered;
    }

    RE::BGSHeadPart* ChimeraAccessoryCatalog::PickCatalogedMouthHeadPart(
        Core::ChimeraLineageKind lineage,
        const RE::TESRace* race,
        Foundation::Sex sex,
        Foundation::Seed64 seed) const {
        const std::uint8_t lBit = DomainLineageBit(lineage);
        if (lBit == 0u || !race || !RaceUsesCanonicalHumanMouth(race, sex)) {
            return nullptr;
        }
        std::vector<RE::BGSHeadPart*> filtered;
        filtered.reserve(chimeraMouthParts_.size());
        for (auto* hp : chimeraMouthParts_) {
            const auto it = approvedHeadParts_.find(hp);
            if (it != approvedHeadParts_.end() &&
                (it->second.lineageMask & lBit) != 0u &&
                MatchesEntryFilter(hp, it->second.sexMask, race, sex)) {
                filtered.push_back(hp);
            }
        }
        return DeterministicPick(filtered, seed, 0x7EE740017EE74001ull);
    }

    bool ChimeraAccessoryCatalog::IsApprovedAccessoryHeadPart(
        const RE::BGSHeadPart* part,
        const RE::TESRace* race,
        Foundation::Sex sex) const noexcept {
        if (!part || !race) {
            return false;
        }
        const auto it = approvedHeadParts_.find(part);
        if (it == approvedHeadParts_.end()) {
            return false;
        }
        if (it->second.isMouth && !RaceUsesCanonicalHumanMouth(race, sex)) {
            return false;
        }
        return MatchesEntryFilter(part, it->second.sexMask, race, sex);
    }

    bool ChimeraAccessoryCatalog::IsCatalogedAccessoryMouth(
        const RE::BGSHeadPart* part) const noexcept {
        if (!part) {
            return false;
        }
        const auto it = approvedHeadParts_.find(part);
        return (it != approvedHeadParts_.end()) && it->second.isMouth;
    }

    bool ChimeraAccessoryCatalog::IsRuntimeBeastMouthPart(
        const RE::BGSHeadPart* part) const noexcept {
        if (!part) {
            return false;
        }
        return std::ranges::find(runtimeMouthSet_, part) != runtimeMouthSet_.end();
    }

    bool ChimeraAccessoryCatalog::IsRuntimePromotedHornPart(
        const RE::BGSHeadPart* part) const noexcept {
        if (!part || runtimePromotedHornSet_.empty()) {
            return false;
        }
        return std::ranges::find(runtimePromotedHornSet_, part) != runtimePromotedHornSet_.end();
    }

    bool ChimeraAccessoryCatalog::IsCatalogedHornHeadPart(
        const RE::BGSHeadPart* part) const noexcept {
        if (!part || argonianHornParts_.empty()) {
            return false;
        }
        return std::ranges::find(argonianHornParts_, part) != argonianHornParts_.end();
    }

    bool ChimeraAccessoryCatalog::IsCatalogedEarHeadPart(
        const RE::BGSHeadPart* part) const noexcept {
        if (!part) {
            return false;
        }
        return std::ranges::find(khajiitEarParts_, part) != khajiitEarParts_.end() ||
               std::ranges::find(runtimeClonedEarSet_, part) != runtimeClonedEarSet_.end();
    }

    bool ChimeraAccessoryCatalog::IsRuntimeAccessoryHeadPartClone(
        const RE::BGSHeadPart* part) const noexcept {
        if (!part || runtimeAccessoryHeadPartCloneSet_.empty()) {
            return false;
        }
        return std::ranges::find(runtimeAccessoryHeadPartCloneSet_, part) !=
               runtimeAccessoryHeadPartCloneSet_.end();
    }

    RE::BGSHeadPart* ChimeraAccessoryCatalog::ResolveRaceCompatibleAccessoryHeadPart(
        RE::BGSHeadPart* part,
        const RE::TESRace* resolvedRace) {
        if (!part || !resolvedRace || !humanoidValidRacesList_ || !part->validRaces ||
            part->validRaces->HasForm(resolvedRace)) {
            return part;
        }

        const RE::FormID srcId = part->GetFormID();
        if (const auto it = runtimeRaceCompatHeadPartCache_.find(srcId);
            it != runtimeRaceCompatHeadPartCache_.end()) {
            return it->second;
        }

        auto* clone = RE::IFormFactory::Create<RE::BGSHeadPart>();
        if (!clone) {
            return part;
        }

        clone->formFlags = part->formFlags & ~RE::BGSHeadPart::RecordFlags::kNonPlayable;
        clone->SetFullName(part->GetFullName());
        CopyModelTextureSwap(*clone, *part);
        clone->flags = part->flags;
        clone->type = part->type;
        clone->textureSet = part->textureSet;
        clone->color = part->color;
        clone->validRaces = humanoidValidRacesList_;
        for (std::size_t i = 0; i < RE::BGSHeadPart::MorphIndices::kTotal; ++i) {
            clone->morphs[i] = part->morphs[i];
        }
        for (auto* extra : part->extraParts) {
            if (extra) {
                clone->extraParts.push_back(extra);
            }
        }

        const std::string edid = std::format("MORWHA_AccCompat_{:08X}", srcId);
        clone->SetFormEditorID(edid.c_str());
        clone->InitItem();

        if (IsCatalogedHornHeadPart(part)) {
            runtimePromotedHornSet_.push_back(clone);
        }
        if (IsCatalogedEarHeadPart(part)) {
            runtimeClonedEarSet_.push_back(clone);
        }
        runtimeRaceCompatHeadPartCache_.emplace(srcId, clone);
        runtimeAccessoryHeadPartCloneSet_.push_back(clone);
        return clone;
    }

    RE::TESObjectARMA* ChimeraAccessoryCatalog::GetClonedTailArma(
        State::BeastTailKind tailKind) const noexcept {
        if (tailKind == State::BeastTailKind::Khajiit) {
            return clonedTailKhajiit_;
        }
        if (tailKind == State::BeastTailKind::Argonian) {
            return clonedTailArgonian_;
        }
        return nullptr;
    }

    RE::BGSHeadPart* ChimeraAccessoryCatalog::GetOrCreatePromotedHornHeadPart(
        RE::TESObjectARMA* srcArma,
        Foundation::Sex sex) {
        if (!skee64Loaded_ || !srcArma || !approvedSkinAddons_.contains(srcArma)) {
            return nullptr;
        }
        const auto sexIdx = (sex == Foundation::Sex::Female) ? RE::SEXES::kFemale : RE::SEXES::kMale;
        const char* rawModel = srcArma->bipedModels[sexIdx].GetModel();
        if (!rawModel || rawModel[0] == '\0') {
            return nullptr;
        }

        const RE::FormID srcId = srcArma->GetFormID();
        const std::uint64_t cacheKey =
            (static_cast<std::uint64_t>(srcId) << 1u) | static_cast<std::uint64_t>(sexIdx);
        if (const auto it = runtimePromotedHornCache_.find(cacheKey);
            it != runtimePromotedHornCache_.end()) {
            return it->second;
        }

        auto* hp = RE::IFormFactory::Create<RE::BGSHeadPart>();
        if (!hp) {
            return nullptr;
        }

        hp->formFlags = 0;
        hp->SetFullName("Horns");
        CopyModelTextureSwap(*hp, srcArma->bipedModels[sexIdx]);
        hp->flags.reset();
        hp->flags.set(
            RE::BGSHeadPart::Flag::kPlayable,
            RE::BGSHeadPart::Flag::kMale,
            RE::BGSHeadPart::Flag::kFemale);
        hp->type = static_cast<RE::BGSHeadPart::HeadPartType>(kRaceMenuHornHeadPartType);
        hp->textureSet = srcArma->skinTextures[sexIdx];
        hp->color = nullptr;
        hp->validRaces = humanoidValidRacesList_;

        const std::string edid = std::format(
            "MORWHA_HornHP_{:08X}_{}",
            srcId,
            (sexIdx == RE::SEXES::kFemale) ? 'F' : 'M');
        hp->SetFormEditorID(edid.c_str());
        hp->InitItem();

        MORWHA_LOG_INFO(
            "ChimeraAccessoryCatalog: Built promoted horn headPart '{}' id={:08X} arma={:08X} model='{}'",
            edid,
            hp->GetFormID(),
            srcId,
            hp->GetModel() ? hp->GetModel() : "");

        runtimePromotedHornCache_.emplace(cacheKey, hp);
        runtimePromotedHornSet_.push_back(hp);
        return hp;
    }

    std::vector<RE::BGSHeadPart*> ChimeraAccessoryCatalog::ResolvePromotedHornHeadParts(
        RE::TESRace* resolvedRace,
        Foundation::Sex sex,
        std::span<const State::PluginFormRef> addonRefs) {
        std::vector<RE::BGSHeadPart*> out;
        const auto raceGrp = resolvedRace ? ResolveRaceGroup(resolvedRace) : Core::RaceGroup::Other;
        if (!skee64Loaded_ || !resolvedRace || addonRefs.empty() ||
            (raceGrp != Core::RaceGroup::Humanoid && raceGrp != Core::RaceGroup::Khajiit)) {
            return out;
        }

        const std::uint8_t sBit = DomainSexBit(sex);
        out.reserve(addonRefs.size());
        for (const auto& ref : addonRefs) {
            auto* srcArma = ResolvePluginForm<RE::TESObjectARMA>(ref);
            const auto addonIt =
                srcArma ? approvedSkinAddons_.find(srcArma) : approvedSkinAddons_.end();
            if (addonIt == approvedSkinAddons_.end() ||
                (addonIt->second.sexMask & sBit) == 0u) {
                continue;
            }
            if (auto* hp = GetOrCreatePromotedHornHeadPart(srcArma, sex)) {
                if (std::ranges::find(out, hp) == out.end()) {
                    out.push_back(hp);
                }
            }
        }
        return out;
    }

    RE::BGSHeadPart* ChimeraAccessoryCatalog::ResolveHydratedCatalogMouthPart(
        RE::BGSHeadPart* catalogMouth) {
        if (!catalogMouth) {
            return nullptr;
        }
        const auto entryIt = approvedHeadParts_.find(catalogMouth);
        if (entryIt == approvedHeadParts_.end() || !entryIt->second.needsModelOverride) {
            return catalogMouth;
        }

        const RE::FormID srcId = catalogMouth->GetFormID();
        if (const auto cacheIt = runtimeCatalogMouthCache_.find(srcId);
            cacheIt != runtimeCatalogMouthCache_.end()) {
            return cacheIt->second;
        }

        auto* clone = RE::IFormFactory::Create<RE::BGSHeadPart>();
        if (!clone) {
            return catalogMouth;
        }

        clone->formFlags = catalogMouth->formFlags & ~RE::BGSHeadPart::RecordFlags::kNonPlayable;
        clone->model = entryIt->second.resolvedModelRelPath.c_str();
        clone->flags = catalogMouth->flags;
        clone->type = catalogMouth->type;
        clone->textureSet = catalogMouth->textureSet;
        clone->color = catalogMouth->color;
        clone->validRaces = catalogMouth->validRaces;
        for (std::size_t i = 0; i < RE::BGSHeadPart::MorphIndices::kTotal; ++i) {
            clone->morphs[i] = catalogMouth->morphs[i];
        }
        for (auto* extra : catalogMouth->extraParts) {
            if (extra) {
                clone->extraParts.push_back(extra);
            }
        }

        const std::string edid = std::format("MORWHA_MouthCat_{:08X}", srcId);
        clone->SetFormEditorID(edid.c_str());
        clone->InitItem();

        runtimeCatalogMouthCache_.emplace(srcId, clone);
        runtimeMouthSet_.push_back(clone);
        return clone;
    }

    RE::BGSHeadPart* ChimeraAccessoryCatalog::ResolveRuntimeBeastMouthPart(
        RE::BGSHeadPart* baseMouth,
        State::BeastTeethKind teethKind) {
        if (!baseMouth || !IsHumanMouthPartCompatible(baseMouth)) {
            return nullptr;
        }
        RE::BGSTextureSet* txst = nullptr;
        std::string_view tag;
        if (teethKind == State::BeastTeethKind::Sharp ||
            teethKind == State::BeastTeethKind::Fangs) {
            txst = sharpMouthTxst_;
            tag = "Sharp";
        }
        if (!txst) {
            return nullptr;
        }

        const std::string key = std::format("{}_{:08X}", tag, baseMouth->GetFormID());
        if (const auto it = runtimeMouthCache_.find(key); it != runtimeMouthCache_.end()) {
            return it->second;
        }

        auto* clone = RE::IFormFactory::Create<RE::BGSHeadPart>();
        if (!clone) {
            return nullptr;
        }

        clone->formFlags = baseMouth->formFlags & ~RE::BGSHeadPart::RecordFlags::kNonPlayable;
        clone->model = baseMouth->model;
        clone->flags = baseMouth->flags;
        clone->type = baseMouth->type;
        clone->textureSet = txst;
        clone->color = baseMouth->color;
        clone->validRaces = baseMouth->validRaces;
        for (std::size_t i = 0; i < RE::BGSHeadPart::MorphIndices::kTotal; ++i) {
            clone->morphs[i] = baseMouth->morphs[i];
        }
        for (auto* extra : baseMouth->extraParts) {
            if (extra) {
                clone->extraParts.push_back(extra);
            }
        }

        const std::string edid = std::format("MORWHA_Mouth_{}_{:08X}", tag, baseMouth->GetFormID());
        clone->SetFormEditorID(edid.c_str());
        clone->InitItem();

        runtimeMouthCache_.emplace(key, clone);
        runtimeMouthSet_.push_back(clone);
        return clone;
    }

    RE::TESObjectARMO* ChimeraAccessoryCatalog::GetOrCreateChimeraSkinArmor(
        RE::TESObjectARMO* baseSkin,
        RE::TESRace* resolvedRace,
        State::BeastTailKind tailKind) {
        RE::TESObjectARMO* effectiveBaseSkin =
            baseSkin ? baseSkin : (resolvedRace ? resolvedRace->skin : nullptr);
        if (!resolvedRace || !effectiveBaseSkin || tailKind == State::BeastTailKind::None) {
            return nullptr;
        }

        RE::TESObjectARMA* tailArma = nullptr;
        if (tailKind == State::BeastTailKind::Khajiit) {
            tailArma = clonedTailKhajiit_;
        } else if (tailKind == State::BeastTailKind::Argonian) {
            tailArma = clonedTailArgonian_;
        }
        if (!tailArma || !tailArma->IsValidRace(resolvedRace)) {
            return nullptr;
        }

        const std::string cacheKey =
            std::format("{:08X}+{:08X}", effectiveBaseSkin->GetFormID(), tailArma->GetFormID());
        if (const auto it = runtimeSkinCache_.find(cacheKey); it != runtimeSkinCache_.end()) {
            return it->second;
        }

        auto* customSkin = RE::IFormFactory::Create<RE::TESObjectARMO>();
        if (!customSkin) {
            return nullptr;
        }

        customSkin->formFlags =
            effectiveBaseSkin->formFlags | RE::TESObjectARMO::RecordFlags::kNonPlayable;
        customSkin->race = effectiveBaseSkin->race ? effectiveBaseSkin->race : resolvedRace;
        customSkin->armorRating = effectiveBaseSkin->armorRating;
        customSkin->bipedModelData = effectiveBaseSkin->bipedModelData;
        customSkin->bipedModelData.armorType = RE::BIPED_MODEL::ArmorType::kClothing;
        for (auto* a : effectiveBaseSkin->armorAddons) {
            // Strip dormant kTail addons inherited from vanilla SkinNaked / RSV skins
            // so only the chimera's intended cloned tail ARMA owns Slot 40.
            if (a && !a->HasPartOf(RE::BGSBipedObjectForm::BipedObjectSlot::kTail)) {
                customSkin->armorAddons.push_back(a);
                if (a->IsValidRace(resolvedRace)) {
                    using Slot = RE::BGSBipedObjectForm::BipedObjectSlot;
                    for (const auto reqSlot : {
                             Slot::kBody,
                             Slot::kHands,
                             Slot::kFeet,
                             Slot::kModPelvisSecondary }) {
                        if (a->HasPartOf(reqSlot)) {
                            customSkin->AddSlotToMask(reqSlot);
                        }
                    }
                }
            }
        }
        customSkin->armorAddons.push_back(tailArma);
        customSkin->AddSlotToMask(tailArma->GetSlotMask().get());

        const std::string edid = std::format("MORWHA_Skin_{}", runtimeSkinCache_.size() + 1u);
        customSkin->SetFormEditorID(edid.c_str());
        customSkin->InitItem();

        const char* tailModel = tailArma->bipedModels[RE::SEXES::kMale].GetModel();
        MORWHA_LOG_INFO(
            "ChimeraAccessoryCatalog: Built runtime skin '{}' id={:08X} base={:08X} tailArma={:08X} ('{}')",
            edid,
            customSkin->GetFormID(),
            effectiveBaseSkin->GetFormID(),
            tailArma->GetFormID(),
            tailModel ? tailModel : "");

        runtimeSkinCache_.emplace(cacheKey, customSkin);
        runtimeChimeraSkinSet_.push_back(customSkin);
        return customSkin;
    }

    void ChimeraAccessoryCatalog::RecordNpcChimeraState(
        RE::FormID baseNpcFormId,
        RE::TESRace* resolvedRace,
        Foundation::Sex sex,
        State::BeastTailKind tailKind,
        std::string_view hybridSkinId) {
        if (baseNpcFormId == 0u) {
            return;
        }
        std::lock_guard lock(accessorySyncMutex_);
        if ((tailKind == State::BeastTailKind::None && hybridSkinId.empty()) || !resolvedRace) {
            npcChimeraStates_.erase(baseNpcFormId);
        } else {
            npcChimeraStates_.insert_or_assign(
                baseNpcFormId,
                NpcChimeraState{
                    .resolvedRace = resolvedRace,
                    .sex = sex,
                    .tailKind = tailKind,
                    .hybridSkinId = std::string(hybridSkinId)
                });
        }
    }

    bool ChimeraAccessoryCatalog::SyncActorChimeraSkin(RE::Actor* actor) {
        if (!actor) {
            return false;
        }
        RE::TESNPC* baseNpc = actor->GetActorBase();
        if (!baseNpc) {
            return false;
        }

        NpcChimeraState state{};
        {
            std::lock_guard lock(accessorySyncMutex_);
            const auto it = npcChimeraStates_.find(baseNpc->GetFormID());
            if (it == npcChimeraStates_.end()) {
                return false;
            }
            state = it->second;
        }

        if (state.resolvedRace == nullptr ||
            (state.tailKind == State::BeastTailKind::None && state.hybridSkinId.empty())) {
            return false;
        }

        if (!state.hybridSkinId.empty()) {
            auto& hybSvc = HybridSkinService::GetSingleton();
            const bool skinOk = hybSvc.EnsureSkinArmorMatchesHybridState(
                baseNpc->skin, state.resolvedRace, state.sex, state.hybridSkinId, state.tailKind);
            const RE::BGSTextureSet* curFaceTx =
                baseNpc->headRelatedData ? baseNpc->headRelatedData->faceDetails : nullptr;
            const bool faceOk = hybSvc.IsRuntimeHybridFaceTextureSet(curFaceTx);
            if (skinOk && faceOk) {
                return false;
            }

            RE::TESObjectARMO* srcSkin =
                baseNpc->skin ? baseNpc->skin : state.resolvedRace->skin;
            RE::BGSHeadPart* curFaceHp =
                baseNpc->GetHeadPartByType(RE::BGSHeadPart::HeadPartType::kFace);
            const auto forms = hybSvc.ResolveHybridNpcForms(
                state.hybridSkinId, srcSkin, state.resolvedRace, state.sex, state.tailKind, curFaceHp);
            bool changed = false;
            if (!skinOk && forms.skinArmor && baseNpc->skin != forms.skinArmor) {
                baseNpc->skin = forms.skinArmor;
                changed = true;
            }
            if (!faceOk && forms.faceTextureSet && curFaceTx != forms.faceTextureSet) {
                baseNpc->SetFaceTexture(forms.faceTextureSet);
                changed = true;
            }
            if (!faceOk && forms.faceHeadPartClone && curFaceHp != forms.faceHeadPartClone) {
                baseNpc->ChangeHeadPart(forms.faceHeadPartClone);
                changed = true;
            }
            if (changed) {
                MORWHA_LOG_INFO(
                    "ChimeraAccessoryCatalog: Re-applied hybrid skin '{}' (skin={:08X}, faceTx={:08X}) on actor {:08X} (base {:08X})",
                    state.hybridSkinId,
                    baseNpc->skin ? baseNpc->skin->GetFormID() : 0u,
                    forms.faceTextureSet ? forms.faceTextureSet->GetFormID() : 0u,
                    actor->GetFormID(),
                    baseNpc->GetFormID());
            }
            return changed;
        }

        RE::TESObjectARMO* curSkin = baseNpc->skin;
        if (IsRuntimeChimeraSkin(curSkin)) {
            return false;
        }

        RE::TESObjectARMO* srcSkin = curSkin ? curSkin : state.resolvedRace->skin;
        auto* wrapped = GetOrCreateChimeraSkinArmor(srcSkin, state.resolvedRace, state.tailKind);
        if (!wrapped || wrapped == curSkin) {
            return false;
        }

        baseNpc->skin = wrapped;
        MORWHA_LOG_INFO(
            "ChimeraAccessoryCatalog: Wrapped external skin {:08X} -> {:08X} for actor {:08X} (base {:08X})",
            curSkin ? curSkin->GetFormID() : 0u,
            wrapped->GetFormID(),
            actor->GetFormID(),
            baseNpc->GetFormID());
        return true;
    }

    bool ChimeraAccessoryCatalog::IsRuntimeChimeraSkin(
        const RE::TESObjectARMO* skin) const noexcept {
        if (!skin || runtimeChimeraSkinSet_.empty()) {
            return false;
        }
        return std::ranges::find(runtimeChimeraSkinSet_, skin) != runtimeChimeraSkinSet_.end();
    }

}  // namespace MORWHA::Engine
