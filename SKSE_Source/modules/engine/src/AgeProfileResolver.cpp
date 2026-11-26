#include "morwha/engine/AgeProfileResolver.h"

#include "morwha/core/MorwhaCore.h"
#include "morwha/engine/EngineLogging.h"
#include "morwha/engine/FormRefResolver.h"
#include "morwha/engine/TesNpcHydrator.h"
#include "morwha/engine/VfsAssetReader.h"

#include <RE/B/BGSColorForm.h>
#include <RE/B/BGSKeyword.h>
#include <RE/B/BGSTextureSet.h>
#include <RE/B/BGSVoiceType.h>
#include <RE/T/TESDataHandler.h>
#include <RE/T/TESNPC.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace MORWHA::Engine {

    namespace {

        [[nodiscard]] std::uint32_t ExtractSkyrimBaseLocalId(const RE::TESRace* race) noexcept {
            if (!race || (race->GetFormID() & 0xFF000000u) != 0u) {
                return 0u;
            }
            return race->GetFormID() & 0x00FFFFFFu;
        }

        std::mutex& ComplexionCacheMutex() noexcept {
            static std::mutex s_mutex;
            return s_mutex;
        }

        std::unordered_map<std::string, Core::ComplexionSample>& ComplexionPathCache() noexcept {
            static std::unordered_map<std::string, Core::ComplexionSample> s_cache;
            return s_cache;
        }

        [[nodiscard]] Core::ComplexionSample DecodeAndAnalyzeVfsTexture(
            std::string_view rawTexPath) {
            if (rawTexPath.empty()) {
                return Core::ComplexionSample{};
            }
            const std::string vfsPath = BuildTextureVfsPath(rawTexPath);
            if (vfsPath.empty()) {
                return Core::ComplexionSample{};
            }

            {
                std::lock_guard lock(ComplexionCacheMutex());
                if (const auto it = ComplexionPathCache().find(vfsPath);
                    it != ComplexionPathCache().end()) {
                    return it->second;
                }
            }

            Core::ComplexionSample sig{};
            std::array<std::uint8_t, 160> headerBuf{};
            std::size_t headerRead = 0;
            std::size_t totalFileBytes = 0;
            Core::DdsSlicePlan plan{};
            std::vector<std::uint8_t> mipBytes;
            std::array<std::uint8_t, 32u * 32u * 4u> decoded{};

            if (ReadVfsFileHeaderAndTotalSize(vfsPath, headerBuf, headerRead, totalFileBytes) &&
                Core::PlanDdsSlice(
                    std::span<const std::uint8_t>(headerBuf.data(), headerRead),
                    totalFileBytes,
                    plan,
                    32u) &&
                ReadVfsFileSlice(vfsPath, plan.fileByteOffset, plan.sliceByteSize, mipBytes) &&
                Core::DecodeDdsSlice32x32(plan, mipBytes, decoded)) {
                sig = Core::AnalyzeComplexionMipmap(decoded);
            }

            {
                std::lock_guard lock(ComplexionCacheMutex());
                ComplexionPathCache().emplace(vfsPath, sig);
            }
            return sig;
        }

        void UpsertAgeTintLayer(
            std::vector<State::TintLayerSpec>& tintLayers,
            const RE::TESRace::FaceRelatedData* faceRelated,
            State::TintSemanticType semanticType,
            Foundation::RgbColor color,
            std::uint16_t alpha) {
            if (alpha == 0u) {
                std::erase_if(tintLayers, [&](const State::TintLayerSpec& existing) {
                    return existing.semanticType == semanticType;
                });
                return;
            }
            if (!faceRelated) {
                return;
            }
            const auto* asset =
                FindRaceTintAsset(faceRelated, semanticType, State::kUnsetExplicitTintIndex);
            if (!asset) {
                return;
            }
            const std::uint16_t clamped = std::min<std::uint16_t>(100u, alpha);
            for (auto& existing : tintLayers) {
                if (existing.semanticType == semanticType) {
                    existing.explicitTintIndex = asset->texture.index;
                    existing.color = color;
                    existing.interpolationValue = clamped;
                    return;
                }
            }
            tintLayers.push_back(State::TintLayerSpec{
                .semanticType = semanticType,
                .explicitTintIndex = asset->texture.index,
                .color = color,
                .interpolationValue = clamped
            });
        }

        [[nodiscard]] State::PluginFormRef MatchNearestHairColorFormForAge(
            const RE::TESRace::FaceRelatedData* faceRelated,
            Foundation::RgbColor targetHairColor) {
            if (faceRelated && faceRelated->availableHairColors &&
                !faceRelated->availableHairColors->empty()) {
                RE::BGSColorForm* bestForm = nullptr;
                std::uint32_t bestDist = (std::numeric_limits<std::uint32_t>::max)();
                for (auto* cand : *faceRelated->availableHairColors) {
                    if (!cand) {
                        continue;
                    }
                    const auto dist = ColorDistanceSq(targetHairColor, ToRgbColor(cand->color));
                    if (dist < bestDist) {
                        bestDist = dist;
                        bestForm = cand;
                    }
                }
                if (bestForm) {
                    return MakePluginFormRef(bestForm);
                }
            }
            if (faceRelated && faceRelated->defaultHairColor) {
                return MakePluginFormRef(faceRelated->defaultHairColor);
            }
            return {};
        }

    }  // namespace

    bool IsVoiceTypeSexCompatible(
        const RE::BGSVoiceType* voice,
        Foundation::Sex targetSex) noexcept {
        if (!voice || voice->IsDeleted()) {
            return false;
        }
        const bool isFemaleVoice = voice->data.flags.all(RE::VOICE_TYPE_DATA::Flag::kFemale);
        const bool wantFemale = (targetSex == Foundation::Sex::Female);
        return isFemaleVoice == wantFemale;
    }

    RE::BGSVoiceType* ResolveVoiceTypeForAgeAndRace(
        const RE::TESRace* targetRace,
        Foundation::Sex targetSex,
        Foundation::AgeBracket effectiveBracket,
        Foundation::Seed64 seed) {
        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        std::vector<RE::BGSVoiceType*> validPool;
        validPool.reserve(10u);

        auto pushUniqueValid = [&](RE::BGSVoiceType* v) {
            if (IsVoiceTypeSexCompatible(v, targetSex) &&
                std::ranges::find(validPool, v) == validPool.end()) {
                validPool.push_back(v);
            }
        };

        if (dataHandler) {
            const std::uint32_t primaryLocalId = ExtractSkyrimBaseLocalId(targetRace);
            const std::uint32_t morphLocalId =
                targetRace ? ExtractSkyrimBaseLocalId(targetRace->morphRace) : 0u;
            const auto candidateIds = Core::SelectSkyrimVoicePoolCandidates(
                primaryLocalId, morphLocalId, targetSex, effectiveBracket);
            for (const std::uint32_t localId : candidateIds) {
                pushUniqueValid(dataHandler->LookupForm<RE::BGSVoiceType>(localId, "Skyrim.esm"));
            }
        }

        // Include custom-plugin race default voice when sex-compatible (e.g. bespoke custom races).
        if (targetRace) {
            const auto sexIdx = static_cast<std::size_t>(targetSex);
            if (sexIdx < RE::SEXES::kTotal) {
                auto* raceDefVoice = targetRace->defaultVoiceTypes[sexIdx];
                if (raceDefVoice && (raceDefVoice->GetFormID() & 0xFF000000u) != 0u) {
                    pushUniqueValid(raceDefVoice);
                }
            }
        }

        if (!validPool.empty()) {
            return validPool[Core::PickSeededVoicePoolIndex(validPool.size(), seed)];
        }

        // Strict sex-checked fallback onto race default or any default-dialogue loaded voice.
        if (targetRace) {
            const auto sexIdx = static_cast<std::size_t>(targetSex);
            if (sexIdx < RE::SEXES::kTotal &&
                IsVoiceTypeSexCompatible(targetRace->defaultVoiceTypes[sexIdx], targetSex)) {
                return targetRace->defaultVoiceTypes[sexIdx];
            }
        }
        if (dataHandler) {
            for (auto* v : dataHandler->GetFormArray<RE::BGSVoiceType>()) {
                if (IsVoiceTypeSexCompatible(v, targetSex) &&
                    v->data.flags.all(RE::VOICE_TYPE_DATA::Flag::kAllowDefaultDialogue)) {
                    return v;
                }
            }
            for (auto* v : dataHandler->GetFormArray<RE::BGSVoiceType>()) {
                if (IsVoiceTypeSexCompatible(v, targetSex)) {
                    return v;
                }
            }
        }
        return nullptr;
    }

    RE::BGSTextureSet* ResolveComplexionForAgeAndRace(
        RE::TESRace*,
        const RE::TESRace::FaceRelatedData* faceRelated,
        Foundation::Sex,
        Foundation::AgeBracket effectiveBracket,
        Foundation::Seed64 seed) {
        if (!faceRelated) {
            return nullptr;
        }

        std::vector<RE::BGSTextureSet*> candidates;
        std::size_t defaultIdx = 0u;
        if (faceRelated->defaultFaceDetailsTextureSet) {
            candidates.push_back(faceRelated->defaultFaceDetailsTextureSet);
        }
        if (faceRelated->faceDetailsTextureSets) {
            for (auto* tex : *faceRelated->faceDetailsTextureSets) {
                if (!tex || tex->IsDeleted()) {
                    continue;
                }
                if (std::ranges::find(candidates, tex) == candidates.end()) {
                    candidates.push_back(tex);
                }
            }
        }
        if (candidates.empty()) {
            return nullptr;
        }
        if (candidates.size() == 1u) {
            return candidates.front();
        }

        std::vector<Core::ComplexionSample> detailSigs(candidates.size());
        std::size_t nonBaselineDetailCount = 0u;
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            // Skyrim TXST stores Facegen complexion detail maps (BlankDetailmap / *HeadDetail_*.dds)
            // in TX04 = BSTextureSet::Textures::kHeight (slot 4), NOT kDetailMap/kGlowMap (slot 3 = _sk.dds).
            const char* detailPath =
                candidates[i]->textures[RE::BSTextureSet::Textures::kHeight].textureName.c_str();
            detailSigs[i] = DecodeAndAnalyzeVfsTexture(detailPath ? detailPath : "");
            if (Core::IsComplexionNonBaseline(detailSigs[i])) {
                ++nonBaselineDetailCount;
            }
        }

        std::vector<Core::ComplexionSample> normalSigs;
        if (nonBaselineDetailCount == 0u) {
            bool hasDistinctNormals = false;
            const std::string firstNorm = BuildTextureVfsPath(
                candidates.front()->textures[RE::BSTextureSet::Textures::kNormal].textureName.c_str());
            for (std::size_t i = 1u; i < candidates.size(); ++i) {
                const std::string candNorm = BuildTextureVfsPath(
                    candidates[i]->textures[RE::BSTextureSet::Textures::kNormal].textureName.c_str());
                if (candNorm != firstNorm) {
                    hasDistinctNormals = true;
                    break;
                }
            }
            if (hasDistinctNormals) {
                normalSigs.resize(candidates.size());
                for (std::size_t i = 0; i < candidates.size(); ++i) {
                    const char* normPath =
                        candidates[i]->textures[RE::BSTextureSet::Textures::kNormal].textureName.c_str();
                    normalSigs[i] = DecodeAndAnalyzeVfsTexture(normPath ? normPath : "");
                }
            }
        }

        const std::size_t chosenIdx = Core::SelectComplexionPoolIndex(
            detailSigs, normalSigs, effectiveBracket, defaultIdx, seed);
        return candidates[std::min(chosenIdx, candidates.size() - 1u)];
    }

    void ApplyAgeProfileToSpec(
        State::DynamicNpcSpec& spec,
        RE::TESRace* targetRace,
        const RE::TESRace::FaceRelatedData* faceRelated,
        Foundation::AgeBracket requestedBracket) {
        const Foundation::AgeBracket effectiveBracket =
            Core::ResolveAgeBracket(requestedBracket, spec.donorMode, spec.seed);
        spec.ageBracket = effectiveBracket;

        if (auto* voice = ResolveVoiceTypeForAgeAndRace(
                targetRace, spec.sex, effectiveBracket, spec.seed)) {
            spec.voiceTypeRef = MakePluginFormRef(voice);
        }

        if (auto* faceTex = ResolveComplexionForAgeAndRace(
                targetRace, faceRelated, spec.sex, effectiveBracket, spec.seed)) {
            spec.faceTextureSetRef = MakePluginFormRef(faceTex);
        }

        const bool isBeast = (ResolveRaceGroup(targetRace) != Core::RaceGroup::Humanoid);
        const auto ageAdj = Core::ComputeAgeVisuals(
            effectiveBracket,
            spec.hairColor,
            /*allowHairSilvering=*/!isBeast,
            spec.seed);

        if (ageAdj.hairColor != spec.hairColor) {
            spec.hairColor = ageAdj.hairColor;
            spec.hairColorFormRef = MatchNearestHairColorFormForAge(faceRelated, spec.hairColor);
        }

        if (!isBeast) {
            UpsertAgeTintLayer(
                spec.tintLayers,
                faceRelated,
                State::TintSemanticType::LaughLines,
                ageAdj.ageShadowColor,
                ageAdj.laughLinesAlpha);
            UpsertAgeTintLayer(
                spec.tintLayers,
                faceRelated,
                State::TintSemanticType::EyeSocketLower,
                ageAdj.ageShadowColor,
                ageAdj.eyeSocketLowerAlpha);
            UpsertAgeTintLayer(
                spec.tintLayers,
                faceRelated,
                State::TintSemanticType::Forehead,
                ageAdj.ageShadowColor,
                ageAdj.foreheadAlpha);
        }
    }

    void SyncNpcAgeCompatibilityKeywords(
        RE::TESNPC* targetNpc,
        const State::DynamicNpcSpec& spec) {
        if (!targetNpc) {
            return;
        }
        if (auto* rsvIgnoreKw = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("RSVignore");
            rsvIgnoreKw && targetNpc->HasKeyword(rsvIgnoreKw)) {
            return;
        }
        auto* rsvYoungKw = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("RSVyoung");
        auto* rsvOldKw = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("RSVold");
        if (!rsvYoungKw && !rsvOldKw) {
            return;
        }

        const Foundation::AgeBracket effectiveBracket =
            Core::ResolveAgeBracket(spec.ageBracket, spec.donorMode, spec.seed);
        const bool wantYoung =
            (effectiveBracket == Foundation::AgeBracket::Auto ||
             effectiveBracket == Foundation::AgeBracket::YoungAdult);
        const bool wantOld = (effectiveBracket == Foundation::AgeBracket::Elder);

        if (rsvYoungKw) {
            if (wantYoung) {
                if (!targetNpc->HasKeyword(rsvYoungKw)) {
                    targetNpc->AddKeyword(rsvYoungKw);
                }
            } else if (targetNpc->HasKeyword(rsvYoungKw)) {
                targetNpc->RemoveKeyword(rsvYoungKw);
            }
        }
        if (rsvOldKw) {
            if (wantOld) {
                if (!targetNpc->HasKeyword(rsvOldKw)) {
                    targetNpc->AddKeyword(rsvOldKw);
                }
            } else if (targetNpc->HasKeyword(rsvOldKw)) {
                targetNpc->RemoveKeyword(rsvOldKw);
            }
        }
    }

}  // namespace MORWHA::Engine
