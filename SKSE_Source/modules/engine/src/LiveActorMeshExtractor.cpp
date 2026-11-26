#include "morwha/engine/LiveActorMeshExtractor.h"

#include "morwha/engine/HeadPartPoolIndexer.h"

#include "RE/B/BSLightingShaderMaterialBase.h"
#include "RE/B/BSLightingShaderMaterialHairTint.h"
#include "RE/B/BSTextureSet.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace MORWHA::Engine {

    namespace {

        constexpr std::uint32_t kShaderTypeDefault = 0u;
        constexpr std::uint32_t kShaderTypeEnvMap = 1u;
        constexpr std::uint32_t kShaderTypeFaceTint = 4u;
        constexpr std::uint32_t kShaderTypeHairTint = 6u;
        constexpr std::uint32_t kShaderTypeEye = 16u;

        [[nodiscard]] bool IsAccessoryMeshName(std::string_view name) noexcept {
            constexpr std::array<std::string_view, 11> kTokens = {
                "Mouth", "Eyes", "Brow", "Hair", "Beard",
                "Mustache", "Scar", "Marks", "Lash", "Tear", "Ears"
            };
            for (const auto tok : kTokens) {
                if (HeadPartPoolIndexer::ContainsCaseInsensitive(name, tok)) {
                    return true;
                }
            }
            return false;
        }

        void ApplySkinToBoneIfRootSpace(
            const RE::NiSkinInstance* skinInst,
            std::vector<float>& vertices) noexcept {
            if (!skinInst || !skinInst->skinData) {
                return;
            }
            const auto* skinData = skinInst->skinData.get();
            const std::uint32_t boneCount = skinData->GetBoneCount();
            if (boneCount == 0u) {
                return;
            }

            bool foundNamedHead = false;
            RE::NiTransform chosen{};
            std::uint16_t maxVerts = 0u;
            bool foundFallback = false;

            for (std::uint32_t b = 0u; b < boneCount; ++b) {
                const auto& xf = skinData->GetBoneDataSkinToBone(b);
                const std::uint16_t nv = skinData->GetBoneDataVerts(b);
                const RE::NiAVObject* boneNode = skinInst->bones ? skinInst->bones[b] : nullptr;
                const std::string_view boneName =
                    (boneNode && boneNode->name.c_str()) ? boneNode->name.c_str() : "";
                if (!boneName.empty() &&
                    HeadPartPoolIndexer::ContainsCaseInsensitive(boneName, "NPC Head")) {
                    foundNamedHead = true;
                    chosen = xf;
                    break;
                }
                if (!foundFallback || nv > maxVerts) {
                    maxVerts = nv;
                    chosen = xf;
                    foundFallback = true;
                }
            }

            if ((!foundNamedHead && !foundFallback) ||
                !std::isfinite(chosen.translate.z) ||
                std::fabs(chosen.translate.z) <= 60.0f) {
                return;
            }

            const std::size_t vCount = vertices.size() / 3u;
            for (std::size_t v = 0u; v < vCount; ++v) {
                const float x = vertices[v * 3u + 0u];
                const float y = vertices[v * 3u + 1u];
                const float z = vertices[v * 3u + 2u];
                vertices[v * 3u + 0u] = chosen.scale *
                                            (chosen.rotate.entry[0][0] * x +
                                             chosen.rotate.entry[0][1] * y +
                                             chosen.rotate.entry[0][2] * z) +
                                        chosen.translate.x;
                vertices[v * 3u + 1u] = chosen.scale *
                                            (chosen.rotate.entry[1][0] * x +
                                             chosen.rotate.entry[1][1] * y +
                                             chosen.rotate.entry[1][2] * z) +
                                        chosen.translate.y;
                vertices[v * 3u + 2u] = chosen.scale *
                                            (chosen.rotate.entry[2][0] * x +
                                             chosen.rotate.entry[2][1] * y +
                                             chosen.rotate.entry[2][2] * z) +
                                        chosen.translate.z;
            }
        }

        void DecodeRawVertexBuffer(
            const std::uint8_t* rawData,
            std::uint16_t numVertices,
            RE::BSGraphics::VertexDesc vDesc,
            std::vector<float>* outPositionsIfEmpty,
            std::vector<float>* outUvsIfEmpty) {
            if (!rawData || numVertices == 0u) {
                return;
            }
            const std::uint32_t stride = vDesc.GetSize();
            if (stride < 8u) {
                return;
            }

            const bool hasPos = vDesc.HasFlag(RE::BSGraphics::Vertex::VF_VERTEX);
            const bool hasUv = vDesc.HasFlag(RE::BSGraphics::Vertex::VF_UV);
            const bool fullPrec = vDesc.HasFlag(RE::BSGraphics::Vertex::VF_FULLPREC);

            if (outPositionsIfEmpty && outPositionsIfEmpty->empty() && hasPos) {
                const std::uint32_t posOff =
                    vDesc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_POSITION);
                const std::uint32_t need = fullPrec ? 12u : 6u;
                if (posOff + need <= stride) {
                    outPositionsIfEmpty->resize(static_cast<std::size_t>(numVertices) * 3u);
                    for (std::uint16_t v = 0u; v < numVertices; ++v) {
                        const std::uint8_t* ptr =
                            rawData + static_cast<std::size_t>(v) * stride + posOff;
                        float x = 0.0f;
                        float y = 0.0f;
                        float z = 0.0f;
                        if (fullPrec) {
                            std::memcpy(&x, ptr + 0u, 4u);
                            std::memcpy(&y, ptr + 4u, 4u);
                            std::memcpy(&z, ptr + 8u, 4u);
                        } else {
                            std::uint16_t hx = 0;
                            std::uint16_t hy = 0;
                            std::uint16_t hz = 0;
                            std::memcpy(&hx, ptr + 0u, 2u);
                            std::memcpy(&hy, ptr + 2u, 2u);
                            std::memcpy(&hz, ptr + 4u, 2u);
                            x = Core::DecodeHalfFloat(hx);
                            y = Core::DecodeHalfFloat(hy);
                            z = Core::DecodeHalfFloat(hz);
                        }
                        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
                            outPositionsIfEmpty->clear();
                            break;
                        }
                        (*outPositionsIfEmpty)[static_cast<std::size_t>(v) * 3u + 0u] = x;
                        (*outPositionsIfEmpty)[static_cast<std::size_t>(v) * 3u + 1u] = y;
                        (*outPositionsIfEmpty)[static_cast<std::size_t>(v) * 3u + 2u] = z;
                    }
                }
            }

            if (outUvsIfEmpty && outUvsIfEmpty->empty() && hasUv) {
                const std::uint32_t uvOff =
                    vDesc.GetAttributeOffset(RE::BSGraphics::Vertex::VA_TEXCOORD0);
                if (uvOff + 4u <= stride) {
                    outUvsIfEmpty->resize(static_cast<std::size_t>(numVertices) * 2u);
                    for (std::uint16_t v = 0u; v < numVertices; ++v) {
                        const std::uint8_t* ptr =
                            rawData + static_cast<std::size_t>(v) * stride + uvOff;
                        std::uint16_t hu = 0;
                        std::uint16_t hv = 0;
                        std::memcpy(&hu, ptr + 0u, 2u);
                        std::memcpy(&hv, ptr + 2u, 2u);
                        const float u = Core::DecodeHalfFloat(hu);
                        const float vv = Core::DecodeHalfFloat(hv);
                        (*outUvsIfEmpty)[static_cast<std::size_t>(v) * 2u + 0u] = std::isfinite(u) ? u : 0.0f;
                        (*outUvsIfEmpty)[static_cast<std::size_t>(v) * 2u + 1u] = std::isfinite(vv) ? vv : 0.0f;
                    }
                }
            }
        }

        void AppendTrianglesFromSkinPartition(
            const RE::NiSkinPartition* skinPartition,
            std::uint16_t totalVertices,
            std::vector<std::uint32_t>& outTriangles) {
            if (!skinPartition || skinPartition->numPartitions == 0u) {
                return;
            }
            for (std::uint32_t p = 0u; p < skinPartition->numPartitions; ++p) {
                const auto& part = skinPartition->partitions[p];
                if (part.triangles == 0u) {
                    continue;
                }
                const std::uint16_t* rawTris =
                    (part.buffData && part.buffData->rawIndexData)
                        ? part.buffData->rawIndexData
                        : ((part.strips == 0u) ? part.triList : nullptr);
                if (!rawTris) {
                    continue;
                }

                const std::size_t idxCount = static_cast<std::size_t>(part.triangles) * 3u;
                bool anyExceedsPartVerts = false;
                if (part.vertexMap != nullptr && part.vertices > 0u) {
                    for (std::size_t k = 0u; k < idxCount; ++k) {
                        if (rawTris[k] >= part.vertices) {
                            anyExceedsPartVerts = true;
                            break;
                        }
                    }
                }
                const bool useVertexMap =
                    (part.vertexMap != nullptr && part.vertices > 0u && !anyExceedsPartVerts);

                outTriangles.reserve(outTriangles.size() + idxCount);
                for (std::uint16_t t = 0u; t < part.triangles; ++t) {
                    const std::size_t base = static_cast<std::size_t>(t) * 3u;
                    std::uint16_t i0 = rawTris[base + 0u];
                    std::uint16_t i1 = rawTris[base + 1u];
                    std::uint16_t i2 = rawTris[base + 2u];
                    if (useVertexMap) {
                        i0 = part.vertexMap[i0];
                        i1 = part.vertexMap[i1];
                        i2 = part.vertexMap[i2];
                    }
                    if (i0 < totalVertices && i1 < totalVertices && i2 < totalVertices) {
                        outTriangles.push_back(i0);
                        outTriangles.push_back(i1);
                        outTriangles.push_back(i2);
                    }
                }
            }
        }

    }  // namespace

    LiveExtractedFaceScene ExtractLiveActorFaceScene(RE::Actor* actor) {
        LiveExtractedFaceScene out{};
        if (!actor || !actor->Is3DLoaded()) {
            return out;
        }
        RE::BSFaceGenNiNode* faceNode = actor->GetFaceNodeSkinned();
        if (!faceNode) {
            return out;
        }

        RE::BSVisit::TraverseScenegraphGeometries(
            faceNode,
            [&](RE::BSGeometry* geom) -> RE::BSVisit::BSVisitControl {
                if (!geom) {
                    return RE::BSVisit::BSVisitControl::kContinue;
                }
                auto* triShape = geom->AsTriShape();
                if (!triShape) {
                    return RE::BSVisit::BSVisitControl::kContinue;
                }
                const auto& triData = triShape->GetTrishapeRuntimeData();
                const std::uint16_t numVertices = triData.vertexCount;
                if (numVertices < 8u) {
                    return RE::BSVisit::BSVisitControl::kContinue;
                }

                const auto& geomData = geom->GetGeometryRuntimeData();
                Core::ExtractedMeshBlock block{};
                block.name = geom->name.c_str() ? geom->name.c_str() : "";
                block.vertexCount = numVertices;

                if (auto* dyn = geom->AsDynamicTriShape()) {
                    block.blockType = "BSDynamicTriShape";
                    auto& dynData = dyn->GetDynamicTrishapeRuntimeData();
                    RE::BSSpinLockGuard guard(dynData.lock);
                    if (dynData.dynamicData != nullptr &&
                        dynData.dataSize == static_cast<std::uint32_t>(numVertices) * 16u) {
                        const auto* rawFloats = static_cast<const float*>(dynData.dynamicData);
                        block.vertices.resize(static_cast<std::size_t>(numVertices) * 3u);
                        bool ok = true;
                        for (std::uint16_t v = 0u; v < numVertices; ++v) {
                            const float x = rawFloats[static_cast<std::size_t>(v) * 4u + 0u];
                            const float y = rawFloats[static_cast<std::size_t>(v) * 4u + 1u];
                            const float z = rawFloats[static_cast<std::size_t>(v) * 4u + 2u];
                            if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
                                ok = false;
                                break;
                            }
                            block.vertices[static_cast<std::size_t>(v) * 3u + 0u] = x;
                            block.vertices[static_cast<std::size_t>(v) * 3u + 1u] = y;
                            block.vertices[static_cast<std::size_t>(v) * 3u + 2u] = z;
                        }
                        if (!ok) {
                            block.vertices.clear();
                        }
                    }
                } else {
                    block.blockType = "BSTriShape";
                }

                if (geomData.rendererData && geomData.rendererData->rawVertexData) {
                    DecodeRawVertexBuffer(
                        geomData.rendererData->rawVertexData,
                        numVertices,
                        geomData.rendererData->vertexDesc,
                        &block.vertices,
                        &block.uvs);
                }

                if (block.vertices.empty()) {
                    return RE::BSVisit::BSVisitControl::kContinue;
                }

                if (geomData.rendererData && geomData.rendererData->rawIndexData &&
                    triData.triangleCount > 0u) {
                    const std::uint16_t* idxPtr = geomData.rendererData->rawIndexData;
                    block.triangles.reserve(static_cast<std::size_t>(triData.triangleCount) * 3u);
                    for (std::uint16_t t = 0u; t < triData.triangleCount; ++t) {
                        const std::uint16_t i0 = idxPtr[static_cast<std::size_t>(t) * 3u + 0u];
                        const std::uint16_t i1 = idxPtr[static_cast<std::size_t>(t) * 3u + 1u];
                        const std::uint16_t i2 = idxPtr[static_cast<std::size_t>(t) * 3u + 2u];
                        if (i0 < numVertices && i1 < numVertices && i2 < numVertices) {
                            block.triangles.push_back(i0);
                            block.triangles.push_back(i1);
                            block.triangles.push_back(i2);
                        }
                    }
                }

                if (const auto* skinInst = geomData.skinInstance.get()) {
                    if (block.triangles.empty() && skinInst->skinPartition) {
                        AppendTrianglesFromSkinPartition(
                            skinInst->skinPartition.get(), numVertices, block.triangles);
                    }
                    if (block.uvs.empty() && skinInst->skinPartition &&
                        skinInst->skinPartition->numPartitions > 0u) {
                        const auto& p0 = skinInst->skinPartition->partitions[0];
                        if (p0.buffData && p0.buffData->rawVertexData &&
                            p0.vertices == numVertices) {
                            DecodeRawVertexBuffer(
                                p0.buffData->rawVertexData,
                                numVertices,
                                p0.vertexDesc,
                                nullptr,
                                &block.uvs);
                        }
                    }
                    ApplySkinToBoneIfRootSpace(skinInst, block.vertices);
                }

                if (auto* shaderProp = geomData.shaderProperty.get()) {
                    if (auto* lighting = netimmerse_cast<RE::BSLightingShaderProperty*>(shaderProp);
                        lighting && lighting->material) {
                        const auto feat = lighting->material->GetFeature();
                        switch (feat) {
                        case RE::BSShaderMaterial::Feature::kFaceGenRGBTint:
                            block.shaderType = kShaderTypeFaceTint;
                            break;
                        case RE::BSShaderMaterial::Feature::kHairTint: {
                            block.shaderType = kShaderTypeHairTint;
                            const auto* hairMat =
                                static_cast<const RE::BSLightingShaderMaterialHairTint*>(lighting->material);
                            if (std::isfinite(hairMat->tintColor.red) &&
                                std::isfinite(hairMat->tintColor.green) &&
                                std::isfinite(hairMat->tintColor.blue)) {
                                block.hasHairTintColor = true;
                                block.hairTintColor = {
                                    hairMat->tintColor.red,
                                    hairMat->tintColor.green,
                                    hairMat->tintColor.blue
                                };
                            }
                            break;
                        }
                        case RE::BSShaderMaterial::Feature::kEye:
                            block.shaderType = kShaderTypeEye;
                            break;
                        case RE::BSShaderMaterial::Feature::kEnvironmentMap:
                            block.shaderType = kShaderTypeEnvMap;
                            break;
                        default:
                            block.shaderType = kShaderTypeDefault;
                            break;
                        }

                        {
                            float maxEmissive = 0.0f;
                            if (lighting->emissiveColor && lighting->emissiveMult > 0.0f) {
                                maxEmissive = (std::max)(
                                    { lighting->emissiveColor->red,
                                      lighting->emissiveColor->green,
                                      lighting->emissiveColor->blue }) *
                                    lighting->emissiveMult;
                            }
                            block.hasActiveEmissiveGlow =
                                (lighting->flags.any(
                                     RE::BSShaderProperty::EShaderPropertyFlag::kGlowMap,
                                     RE::BSShaderProperty::EShaderPropertyFlag::kOwnEmit) ||
                                 lighting->material->GetFeature() == RE::BSShaderMaterial::Feature::kGlowMap) &&
                                (!lighting->emissiveColor || maxEmissive > 0.25f);
                        }

                        if (lighting->material->GetType() == RE::BSShaderMaterial::Type::kLighting) {
                            const auto* baseMat =
                                static_cast<const RE::BSLightingShaderMaterialBase*>(lighting->material);
                            if (baseMat->diffuseTexture &&
                                baseMat->diffuseTexture->name.c_str() != nullptr &&
                                baseMat->diffuseTexture->name.c_str()[0] != '\0') {
                                block.diffuseTexturePath = baseMat->diffuseTexture->name.c_str();
                            }
                            if (block.diffuseTexturePath.empty()) {
                                if (const auto texSet = baseMat->GetTextureSet(); texSet != nullptr) {
                                    if (const char* p = texSet->GetTexturePath(RE::BSTextureSet::Texture::kDiffuse);
                                        p != nullptr && p[0] != '\0') {
                                        block.diffuseTexturePath = p;
                                    }
                                }
                            }
                        }
                    }
                }

                out.scene.allMeshes.push_back(std::move(block));
                return RE::BSVisit::BSVisitControl::kContinue;
            });

        if (out.scene.allMeshes.empty()) {
            return out;
        }

        Core::FinalizeExtractedHeadScene(out.scene);
        if (!out.scene.headMeshIndex.has_value()) {
            for (std::size_t i = 0; i < out.scene.allMeshes.size(); ++i) {
                const auto& m = out.scene.allMeshes[i];
                if ((m.shaderType == kShaderTypeFaceTint || m.blockType == "BSDynamicTriShape") &&
                    m.vertexCount >= 300u && !IsAccessoryMeshName(m.name)) {
                    out.scene.headMeshIndex = i;
                    break;
                }
            }
        }
        out.valid = out.scene.headMeshIndex.has_value();
        return out;
    }

}  // namespace MORWHA::Engine
