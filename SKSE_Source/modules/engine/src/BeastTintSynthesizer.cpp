#include "morwha/engine/BeastTintSynthesizer.h"

#include "morwha/core/MorwhaCore.h"
#include "morwha/engine/HeadPartPoolIndexer.h"
#include "morwha/engine/TesNpcHydrator.h"
#include "morwha/state/DynamicNpcRegistry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace MORWHA::Engine {

    namespace {

        constexpr std::uint32_t kPlayerFormId = 0x00000014u;
        constexpr std::size_t kSemanticSlotCount = 16u;

        [[nodiscard]] bool IsBeastMarkingSemantic(std::uint8_t sem) noexcept {
            return sem >= static_cast<std::uint8_t>(State::TintSemanticType::LipColor) &&
                   sem <= static_cast<std::uint8_t>(State::TintSemanticType::Forehead) &&
                   sem != static_cast<std::uint8_t>(State::TintSemanticType::SkinTone) &&
                   sem != static_cast<std::uint8_t>(State::TintSemanticType::Dirt);
        }

        [[nodiscard]] std::vector<Core::BeastTintAssetInput> BuildTargetDescriptors(
            const RE::TESRace::FaceRelatedData* faceRelated) {
            std::vector<Core::BeastTintAssetInput> out;
            if (!faceRelated || !faceRelated->tintMasks) {
                return out;
            }
            out.reserve(faceRelated->tintMasks->size());
            for (const auto* asset : *faceRelated->tintMasks) {
                if (!asset) {
                    continue;
                }
                Core::BeastTintAssetInput desc{};
                desc.semanticType = asset->texture.skinTone.underlying();
                desc.explicitTintIndex = asset->texture.index;
                const std::uint32_t count = asset->presets.colors.size();
                desc.swatches.reserve(count);
                for (std::uint32_t i = 0; i < count; ++i) {
                    const auto* col = asset->presets.colors[i];
                    if (!col) {
                        continue;
                    }
                    const float defVal =
                        (i < asset->presets.defaultValues.size() &&
                         std::isfinite(asset->presets.defaultValues[i]))
                            ? asset->presets.defaultValues[i]
                            : 1.0f;
                    desc.swatches.push_back(Core::BeastSwatchInput{
                        .color = ToRgbColor(col->color),
                        .defaultOpacity = defVal
                    });
                }
                out.push_back(std::move(desc));
            }
            return out;
        }

        [[nodiscard]] std::vector<Core::DonorBeastLayerInput> ExtractDonorMarkings(
            const ExtractedEngineDonor* donor,
            Core::RaceGroup targetGroup) {
            std::vector<Core::DonorBeastLayerInput> out;
            if (!donor || donor->phenotype.raceGroup != targetGroup || !donor->race) {
                return out;
            }
            const auto* donorFaceRelated =
                ResolveRaceFaceRelatedData(donor->race, donor->phenotype.sex);
            if (!donorFaceRelated || !donorFaceRelated->tintMasks) {
                return out;
            }

            struct AssetSlotInfo {
                std::uint8_t semanticType{ 0 };
                std::size_t ordinal{ 0 };
                std::string_view texName{};
            };
            std::unordered_map<std::uint16_t, AssetSlotInfo> byIndex;
            std::unordered_map<const RE::TESTexture*, AssetSlotInfo> byTexturePtr;
            std::array<std::size_t, kSemanticSlotCount> semanticCounters{};

            for (const auto* asset : *donorFaceRelated->tintMasks) {
                if (!asset) {
                    continue;
                }
                const std::uint8_t sem = asset->texture.skinTone.underlying();
                const std::size_t ord =
                    (sem < kSemanticSlotCount) ? (semanticCounters[sem]++) : 0u;
                if (!IsBeastMarkingSemantic(sem)) {
                    continue;
                }
                const std::string_view texName =
                    asset->texture.file.textureName.c_str()
                        ? asset->texture.file.textureName.c_str()
                        : "";
                const AssetSlotInfo info{ sem, ord, texName };
                byIndex.emplace(asset->texture.index, info);
                byTexturePtr.emplace(&asset->texture.file, info);
            }

            if (donor->rootFaceNpc && donor->rootFaceNpc->tintLayers) {
                for (const auto* layer : *donor->rootFaceNpc->tintLayers) {
                    if (!layer || layer->interpolationValue == 0u) {
                        continue;
                    }
                    const auto it = byIndex.find(layer->tintIndex);
                    if (it == byIndex.end()) {
                        continue;
                    }
                    out.push_back(Core::DonorBeastLayerInput{
                        .semanticType = it->second.semanticType,
                        .ordinalInSemanticType = it->second.ordinal,
                        .color = ToRgbColor(layer->tintColor),
                        .interpolationValue =
                            static_cast<std::uint16_t>(std::min<std::uint16_t>(100u, layer->interpolationValue))
                    });
                }
            }

            const bool isPlayer =
                (donor->baseNpc && donor->baseNpc->GetFormID() == kPlayerFormId);
            if (out.empty() && isPlayer) {
                if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                    const RE::BSTArray<RE::TintMask*>* masks = nullptr;
                    if (REL::Module::IsVR()) {
                        if (const auto* vrData = player->GetVRPlayerRuntimeData()) {
                            masks = vrData->overlayTintMasks ? vrData->overlayTintMasks
                                                             : &vrData->tintMasks;
                        }
                    } else {
                        const auto& flatData = player->GetPlayerRuntimeData();
                        masks = flatData.overlayTintMasks ? flatData.overlayTintMasks
                                                          : &flatData.tintMasks;
                    }
                    if (masks) {
                        for (const auto* mask : *masks) {
                            if (!mask || !std::isfinite(mask->alpha) || mask->alpha <= 0.05f) {
                                continue;
                            }
                            const auto* texPtr = mask->texture;
                            const AssetSlotInfo* matched = nullptr;
                            if (texPtr) {
                                if (const auto it = byTexturePtr.find(texPtr);
                                    it != byTexturePtr.end()) {
                                    matched = &it->second;
                                } else if (texPtr->textureName.c_str() != nullptr &&
                                           texPtr->textureName.c_str()[0] != '\0') {
                                    const std::string_view maskTex = texPtr->textureName.c_str();
                                    for (const auto& [ptr, info] : byTexturePtr) {
                                        if (!info.texName.empty() &&
                                            HeadPartPoolIndexer::EqualsCaseInsensitive(
                                                maskTex, info.texName)) {
                                            matched = &info;
                                            break;
                                        }
                                    }
                                }
                            }
                            if (!matched) {
                                continue;
                            }
                            const auto interp = static_cast<std::uint16_t>(
                                std::clamp<long>(std::lround(mask->alpha * 100.0f), 1l, 100l));
                            out.push_back(Core::DonorBeastLayerInput{
                                .semanticType = matched->semanticType,
                                .ordinalInSemanticType = matched->ordinal,
                                .color = ToRgbColor(mask->color),
                                .interpolationValue = interp
                            });
                        }
                    }
                }
            }

            return out;
        }

    }  // namespace

    bool TryAppendBeastRacialMarkings(
        std::vector<State::TintLayerSpec>& ioTintLayers,
        const RE::TESRace* targetRace,
        const RE::TESRace::FaceRelatedData* targetFaceRelated,
        const ExtractedEngineDonor* optionalPrimaryDonor,
        const ExtractedEngineDonor* optionalSecondaryDonor,
        Foundation::Seed64 seed) {
        const auto targetGroup = ResolveRaceGroup(targetRace);
        if (targetGroup != Core::RaceGroup::Argonian &&
            targetGroup != Core::RaceGroup::Khajiit) {
            return false;
        }
        if (!targetFaceRelated || !targetFaceRelated->tintMasks) {
            return true;
        }

        const auto descriptors = BuildTargetDescriptors(targetFaceRelated);
        const auto primaryMarkings = ExtractDonorMarkings(optionalPrimaryDonor, targetGroup);
        const auto secondaryMarkings = ExtractDonorMarkings(optionalSecondaryDonor, targetGroup);

        const auto synthesized = Core::SynthesizeBeastLayers(
            targetGroup, descriptors, primaryMarkings, secondaryMarkings, seed);

        std::unordered_set<std::uint16_t> existingIndices;
        for (const auto& existing : ioTintLayers) {
            if (existing.explicitTintIndex != State::kUnsetExplicitTintIndex) {
                existingIndices.insert(existing.explicitTintIndex);
            }
        }

        for (const auto& m : synthesized) {
            if (ioTintLayers.size() >= State::kMaxTintLayersPerNpc) {
                break;
            }
            if (m.explicitTintIndex != State::kUnsetExplicitTintIndex &&
                existingIndices.contains(m.explicitTintIndex)) {
                continue;
            }
            if (m.explicitTintIndex != State::kUnsetExplicitTintIndex) {
                existingIndices.insert(m.explicitTintIndex);
            }
            ioTintLayers.push_back(State::TintLayerSpec{
                .semanticType = static_cast<State::TintSemanticType>(m.semanticType),
                .explicitTintIndex = m.explicitTintIndex,
                .color = m.color,
                .interpolationValue = m.interpolationValue
            });
        }
        return true;
    }

}  // namespace MORWHA::Engine
