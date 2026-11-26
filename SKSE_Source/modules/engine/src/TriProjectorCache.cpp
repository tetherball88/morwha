#include "morwha/engine/TriProjectorCache.h"

#include "morwha/engine/EngineLogging.h"
#include "morwha/engine/FaceAnchorCatalog.h"
#include "morwha/engine/HeadPartPoolIndexer.h"
#include "morwha/engine/LiveActorMeshExtractor.h"
#include "morwha/engine/TesNpcHydrator.h"
#include "morwha/engine/VfsAssetReader.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <format>
#include <span>
#include <vector>

namespace MORWHA::Engine {

    namespace {

        [[nodiscard]] std::string ToAsciiLower(std::string_view text) {
            std::string out(text);
            for (char& ch : out) {
                ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            }
            return out;
        }

        [[nodiscard]] bool IsDynamicDpfNpc(const RE::TESNPC* npc) noexcept {
            if (!npc) {
                return false;
            }
            const auto* defFile = npc->GetFile(0);
            return defFile &&
                   defFile->GetFilename().data() &&
                   HeadPartPoolIndexer::EqualsCaseInsensitive(defFile->GetFilename(), "DPF.esp");
        }

        [[nodiscard]] Core::ExtractedMeshBlock MakeSyntheticEyeSphereClouds(
            Core::Float3 leftCenter,
            Core::Float3 rightCenter) {
            Core::ExtractedMeshBlock eyes{};
            eyes.name = "SyntheticEyes";
            eyes.shaderType = 16u;
            const std::array<std::array<float, 3>, 8> dirs{ {
                { +1.0f, 0.0f, 0.0f }, { -1.0f, 0.0f, 0.0f },
                { 0.0f, +1.0f, 0.0f }, { 0.0f, -1.0f, 0.0f },
                { 0.0f, 0.0f, +1.0f }, { 0.0f, 0.0f, -1.0f },
                { 0.577f, 0.577f, 0.577f }, { -0.577f, 0.577f, -0.577f }
            } };
            auto appendOrb = [&](Core::Float3 c) {
                for (const auto& d : dirs) {
                    eyes.vertices.push_back(c.x + 1.0f * d[0]);
                    eyes.vertices.push_back(c.y + 1.0f * d[1]);
                    eyes.vertices.push_back(c.z + 1.0f * d[2]);
                }
            };
            appendOrb(leftCenter);
            appendOrb(rightCenter);
            eyes.vertexCount = static_cast<std::uint32_t>(eyes.vertices.size() / 3u);
            return eyes;
        }

        [[nodiscard]] std::vector<Core::MeshView> CollectEyeSurfaceViews(
            const Core::ExtractedHeadScene& scene) {
            std::vector<Core::MeshView> views;
            views.reserve(scene.eyeMeshIndices.size());
            for (const std::size_t idx : scene.eyeMeshIndices) {
                if (idx < scene.allMeshes.size()) {
                    const auto& em = scene.allMeshes[idx];
                    views.push_back(Core::MeshView{ em.vertices, em.triangles });
                }
            }
            return views;
        }

    }  // namespace

    TriProjectorCache& TriProjectorCache::GetSingleton() noexcept {
        static TriProjectorCache instance;
        return instance;
    }

    TriProjectorCache::EyeNifMetadata TriProjectorCache::GetOrParseEyeNifMetadataLocked(
        std::string_view modelPath) {
        if (modelPath.empty()) {
            return {};
        }
        const std::string nifPath = BuildMeshVfsPath(modelPath);
        if (nifPath.empty()) {
            return {};
        }
        const std::string key = ToAsciiLower(nifPath);
        if (const auto it = eyeNifMetadataCache_.find(key); it != eyeNifMetadataCache_.end()) {
            return it->second;
        }

        EyeNifMetadata meta{};
        std::vector<std::uint8_t> nifBytes;
        if (ReadVfsFileBytes(nifPath, nifBytes)) {
            Core::ExtractedHeadScene eyeScene{};
            if (Core::ParseHeadSceneNif(nifBytes, eyeScene)) {
                meta.fallbackDiffusePath = eyeScene.eyeDiffuseTexturePath;
                std::vector<Core::MeshView> fitViews;
                auto processEyeMesh = [&](const Core::ExtractedMeshBlock& em) {
                    if (em.hasActiveEmissiveGlow) {
                        meta.hasEmissive = true;
                    }
                    if (em.vertexCount >= 16u && em.vertexCount <= 4096u) {
                        fitViews.push_back(Core::MeshView{ em.vertices, em.triangles });
                    }
                    if (meta.corneaUv[0] < 0.0f && !em.vertices.empty() && !em.uvs.empty()) {
                        meta.corneaUv = Core::LocateEyeTextureCenter(
                            em.vertices, em.uvs, em.triangles);
                        if (meta.corneaUv[0] >= 0.0f && meta.vertices.empty()) {
                            meta.vertices = em.vertices;
                            meta.uvs = em.uvs;
                            meta.triangles = em.triangles;
                        }
                    }
                };
                if (!eyeScene.eyeMeshIndices.empty()) {
                    for (const std::size_t idx : eyeScene.eyeMeshIndices) {
                        if (idx < eyeScene.allMeshes.size()) {
                            processEyeMesh(eyeScene.allMeshes[idx]);
                        }
                    }
                } else {
                    for (const auto& em : eyeScene.allMeshes) {
                        processEyeMesh(em);
                    }
                }
                if (!fitViews.empty()) {
                    meta.restRightEyeCenter = Core::MeasureEyePairRestCenter(fitViews);
                }
            }
        }
        eyeNifMetadataCache_.emplace(key, meta);
        return meta;
    }

    Foundation::TopologyPool TriProjectorCache::SelectDonorTopologyForVertexCountLocked(
        RE::TESRace* donorRace,
        Foundation::Sex donorSex,
        std::uint32_t sourceVertexCount) {
        const auto prefTopo =
            HeadPartPoolIndexer::GetSingleton().SelectPreferredTopology(donorRace, donorSex);
        if (donorRace && sourceVertexCount > 0u) {
            for (const auto candTopo :
                 { Foundation::TopologyPool::Vanilla, Foundation::TopologyPool::HighPolyHead }) {
                const CanonicalTriAsset* asset =
                    GetOrBuildCanonicalTriAssetLocked(donorRace, donorSex, candTopo);
                if (asset && asset->rig &&
                    Core::GetRaceRigVertexCount(asset->rig) == sourceVertexCount) {
                    return candTopo;
                }
            }
        }
        return prefTopo;
    }

    const Core::OpaqueTriAsset* TriProjectorCache::GetOrLoadTriLocked(std::string_view vfsPath) {
        if (vfsPath.empty()) {
            return nullptr;
        }
        const std::string key = ToAsciiLower(vfsPath);
        if (const auto it = triCache_.find(key); it != triCache_.end()) {
            return it->second.get();
        }

        std::vector<std::uint8_t> bytes;
        if (!ReadVfsFileBytes(vfsPath, bytes)) {
            triCache_.emplace(key, nullptr);
            return nullptr;
        }

        auto parsed = Core::LoadTriAsset(bytes);
        if (!parsed) {
            MORWHA_LOG_WARN("TriProjectorCache: Failed to parse '{}' as FRTRI003", vfsPath);
            triCache_.emplace(key, nullptr);
            return nullptr;
        }

        const auto* rawPtr = parsed.get();
        triCache_.emplace(key, std::move(parsed));
        return rawPtr;
    }

    const Core::OpaqueRaceRig* TriProjectorCache::GetOrBuildBasisLocked(
        const RE::BGSHeadPart* facePart,
        const RE::TESRace* race,
        Foundation::Sex sex) {
        if (!facePart || !race) {
            return nullptr;
        }

        const char* rawChargen =
            facePart->morphs[RE::BGSHeadPart::MorphIndices::kChargenMorph].model.c_str();
        const std::string chargenPath = BuildMeshVfsPath(rawChargen ? rawChargen : "");
        if (chargenPath.empty()) {
            return nullptr;
        }

        const char* rawRace =
            facePart->morphs[RE::BGSHeadPart::MorphIndices::kRaceMorph].model.c_str();
        const std::string racePath = BuildMeshVfsPath(rawRace ? rawRace : "");

        const char* rawDefault =
            facePart->morphs[RE::BGSHeadPart::MorphIndices::kDefaultMorph].model.c_str();
        const std::string defaultPath = BuildMeshVfsPath(rawDefault ? rawDefault : "");

        const auto* chargenTri = GetOrLoadTriLocked(chargenPath);
        const std::uint32_t chargenVerts = Core::GetTriAssetVertexCount(chargenTri);
        if (!chargenTri || chargenVerts == 0u) {
            return nullptr;
        }

        std::string eyeChargenPath;
        std::string eyeRacePath;
        std::string eyeDefaultPath;
        std::string eyeModelPath;
        const Core::OpaqueTriAsset* eyeChargenTri = nullptr;
        const auto& poolIndexer = HeadPartPoolIndexer::GetSingleton();
        for (const auto topo : { Foundation::TopologyPool::Vanilla, Foundation::TopologyPool::HighPolyHead }) {
            const auto* pool = poolIndexer.GetPool(race, sex, topo);
            if (!pool || pool->eyes.empty()) {
                continue;
            }
            for (const auto* eyeHp : pool->eyes) {
                if (!eyeHp) {
                    continue;
                }
                const char* rawEyeTri =
                    eyeHp->morphs[RE::BGSHeadPart::MorphIndices::kChargenMorph].model.c_str();
                const std::string candPath = BuildMeshVfsPath(rawEyeTri ? rawEyeTri : "");
                if (candPath.empty()) {
                    continue;
                }
                const auto* loadedEye = GetOrLoadTriLocked(candPath);
                if (loadedEye && Core::GetTriAssetVertexCount(loadedEye) > 0u) {
                    eyeChargenPath = candPath;
                    eyeChargenTri = loadedEye;
                    const char* rawEyeRace =
                        eyeHp->morphs[RE::BGSHeadPart::MorphIndices::kRaceMorph].model.c_str();
                    eyeRacePath = BuildMeshVfsPath(rawEyeRace ? rawEyeRace : "");
                    const char* rawEyeDefault =
                        eyeHp->morphs[RE::BGSHeadPart::MorphIndices::kDefaultMorph].model.c_str();
                    eyeDefaultPath = BuildMeshVfsPath(rawEyeDefault ? rawEyeDefault : "");
                    eyeModelPath = BuildMeshVfsPath(eyeHp->model.c_str() ? eyeHp->model.c_str() : "");
                    break;
                }
            }
            if (eyeChargenTri != nullptr) {
                break;
            }
        }

        std::string mouthChargenPath;
        std::string mouthRacePath;
        std::string mouthDefaultPath;
        const Core::OpaqueTriAsset* mouthChargenTri = nullptr;
        const bool faceIsHph =
            HeadPartPoolIndexer::ContainsCaseInsensitive(chargenPath, "high poly head") ||
            HeadPartPoolIndexer::ContainsCaseInsensitive(chargenPath, "kl\\");
        const std::array<Foundation::TopologyPool, 2> mouthTopoOrder =
            faceIsHph
                ? std::array<Foundation::TopologyPool, 2>{
                      Foundation::TopologyPool::HighPolyHead,
                      Foundation::TopologyPool::Vanilla }
                : std::array<Foundation::TopologyPool, 2>{
                      Foundation::TopologyPool::Vanilla,
                      Foundation::TopologyPool::HighPolyHead };
        auto tryLoadMouthPart = [&](const RE::BGSHeadPart* mouthHp) -> bool {
            if (!mouthHp) {
                return false;
            }
            const char* rawMouthTri =
                mouthHp->morphs[RE::BGSHeadPart::MorphIndices::kChargenMorph].model.c_str();
            const std::string candPath = BuildMeshVfsPath(rawMouthTri ? rawMouthTri : "");
            if (candPath.empty()) {
                return false;
            }
            const auto* loadedMouth = GetOrLoadTriLocked(candPath);
            if (!loadedMouth || Core::GetTriAssetVertexCount(loadedMouth) == 0u ||
                Core::GetTriAssetTriangles(loadedMouth).empty()) {
                return false;
            }
            mouthChargenPath = candPath;
            mouthChargenTri = loadedMouth;
            const char* rawMouthRace =
                mouthHp->morphs[RE::BGSHeadPart::MorphIndices::kRaceMorph].model.c_str();
            mouthRacePath = BuildMeshVfsPath(rawMouthRace ? rawMouthRace : "");
            const char* rawMouthDefault =
                mouthHp->morphs[RE::BGSHeadPart::MorphIndices::kDefaultMorph].model.c_str();
            mouthDefaultPath = BuildMeshVfsPath(rawMouthDefault ? rawMouthDefault : "");
            return true;
        };
        for (const auto topo : mouthTopoOrder) {
            const auto* pool = poolIndexer.GetPool(race, sex, topo);
            if (!pool) {
                continue;
            }
            if (tryLoadMouthPart(pool->primaryMouth)) {
                break;
            }
            for (const auto* candMouth : pool->mouths) {
                if (tryLoadMouthPart(candMouth)) {
                    break;
                }
            }
            if (mouthChargenTri != nullptr) {
                break;
            }
        }
        if (mouthChargenTri == nullptr) {
            const RE::SEX reSex =
                (sex == Foundation::Sex::Female) ? RE::SEX::kFemale : RE::SEX::kMale;
            (void)tryLoadMouthPart(
                const_cast<RE::TESRace*>(race)->GetHeadPartByType(
                    RE::BGSHeadPart::HeadPartType::kMisc, reSex));
        }

        const auto* raceTri = racePath.empty() ? nullptr : GetOrLoadTriLocked(racePath);
        const std::string_view primaryEdid =
            race->GetFormEditorID() ? race->GetFormEditorID() : "";
        const std::string_view fallbackEdid =
            (race->morphRace && race->morphRace->GetFormEditorID())
                ? race->morphRace->GetFormEditorID()
                : "";
        const std::string matchedRaceKey =
            Core::MatchTriRaceMorphName(raceTri, chargenVerts, primaryEdid, fallbackEdid);

        const std::string basisKey = std::format(
            "{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}",
            ToAsciiLower(chargenPath),
            ToAsciiLower(racePath),
            matchedRaceKey,
            ToAsciiLower(eyeChargenPath),
            ToAsciiLower(eyeRacePath),
            ToAsciiLower(eyeDefaultPath),
            ToAsciiLower(defaultPath),
            ToAsciiLower(eyeModelPath),
            ToAsciiLower(mouthChargenPath),
            ToAsciiLower(mouthRacePath),
            ToAsciiLower(mouthDefaultPath));
        if (const auto it = basisCache_.find(basisKey); it != basisCache_.end()) {
            return it->second.get();
        }

        const auto* exprTri = defaultPath.empty() ? nullptr : GetOrLoadTriLocked(defaultPath);
        const auto* eyeRaceTri = eyeRacePath.empty() ? nullptr : GetOrLoadTriLocked(eyeRacePath);
        const auto* eyeExprTri =
            (eyeChargenTri && !eyeDefaultPath.empty()) ? GetOrLoadTriLocked(eyeDefaultPath) : nullptr;
        const auto* mouthRaceTri = mouthRacePath.empty() ? nullptr : GetOrLoadTriLocked(mouthRacePath);
        const auto* mouthExprTri =
            (mouthChargenTri && !mouthDefaultPath.empty()) ? GetOrLoadTriLocked(mouthDefaultPath) : nullptr;

        EyeNifMetadata eyeMeta{};
        if (!eyeModelPath.empty()) {
            eyeMeta = GetOrParseEyeNifMetadataLocked(eyeModelPath);
        }

        auto builtRig = Core::CreateRaceRigFromTriAssets(
            chargenTri,
            raceTri,
            exprTri,
            eyeChargenTri,
            eyeRaceTri,
            eyeExprTri,
            eyeMeta.vertices,
            eyeMeta.uvs,
            eyeMeta.triangles,
            mouthChargenTri,
            mouthRaceTri,
            mouthExprTri,
            primaryEdid,
            fallbackEdid);
        if (!builtRig) {
            basisCache_.emplace(basisKey, nullptr);
            return nullptr;
        }

        const auto* rawPtr = builtRig.get();
        basisCache_.emplace(basisKey, std::move(builtRig));
        return rawPtr;
    }

    Core::Float3 TriProjectorCache::GetCanonicalRestEyeCenterLocked(
        RE::TESRace* race,
        Foundation::Sex sex) {
        if (!race) {
            return Core::Float3{ 2.42f, 7.35f, 2.76f };
        }
        const std::string key = std::format("{:08X}_{}", race->GetFormID(), static_cast<unsigned>(sex));
        if (const auto it = restEyeCenterCache_.find(key); it != restEyeCenterCache_.end()) {
            return it->second;
        }

        Core::Float3 resolved{ 2.42f, 7.35f, 2.76f };
        const auto& poolIndexer = HeadPartPoolIndexer::GetSingleton();
        for (const auto topo : { Foundation::TopologyPool::Vanilla, Foundation::TopologyPool::HighPolyHead }) {
            const auto* pool = poolIndexer.GetPool(race, sex, topo);
            if (!pool || pool->eyes.empty()) {
                continue;
            }
            for (const auto* eyeHp : pool->eyes) {
                if (!eyeHp || !eyeHp->model.c_str()) {
                    continue;
                }
                const EyeNifMetadata meta = GetOrParseEyeNifMetadataLocked(eyeHp->model.c_str());
                if (meta.restRightEyeCenter.has_value()) {
                    resolved = *meta.restRightEyeCenter;
                    restEyeCenterCache_.emplace(key, resolved);
                    return resolved;
                }
            }
        }

        restEyeCenterCache_.emplace(key, resolved);
        return resolved;
    }

    const TriProjectorCache::CanonicalTriAsset* TriProjectorCache::GetOrBuildCanonicalTriAssetLocked(
        RE::TESRace* race,
        Foundation::Sex sex,
        Foundation::TopologyPool topology) {
        if (!race) {
            return nullptr;
        }
        const std::string key = std::format(
            "{:08X}_{}_{}", race->GetFormID(), static_cast<unsigned>(sex), static_cast<unsigned>(topology));
        if (const auto it = canonicalAssetCache_.find(key); it != canonicalAssetCache_.end()) {
            return it->second.get();
        }

        const auto& poolIndexer = HeadPartPoolIndexer::GetSingleton();
        const auto* pool = poolIndexer.GetPool(race, sex, topology);
        if ((!pool || !pool->primaryFace) && topology == Foundation::TopologyPool::HighPolyHead) {
            pool = poolIndexer.GetPool(race, sex, Foundation::TopologyPool::Vanilla);
        }
        if (!pool || !pool->primaryFace) {
            canonicalAssetCache_.emplace(key, nullptr);
            return nullptr;
        }

        const RE::BGSHeadPart* facePart = pool->primaryFace;
        const char* rawChargen =
            facePart->morphs[RE::BGSHeadPart::MorphIndices::kChargenMorph].model.c_str();
        const std::string chargenPath = BuildMeshVfsPath(rawChargen ? rawChargen : "");
        const auto* chargenTri = GetOrLoadTriLocked(chargenPath);
        const auto* rig = GetOrBuildBasisLocked(facePart, race, sex);
        if (!chargenTri || !rig || Core::GetRaceRigVertexCount(rig) == 0u) {
            canonicalAssetCache_.emplace(key, nullptr);
            return nullptr;
        }

        auto asset = std::make_unique<CanonicalTriAsset>();
        asset->chargenTri = chargenTri;
        asset->rig = rig;
        asset->restRightEyeCenter = GetCanonicalRestEyeCenterLocked(race, sex);
        for (const auto topo : { Foundation::TopologyPool::Vanilla, Foundation::TopologyPool::HighPolyHead }) {
            const auto* eyePool = poolIndexer.GetPool(race, sex, topo);
            if (!eyePool || eyePool->eyes.empty()) {
                continue;
            }
            for (const auto* eyeHp : eyePool->eyes) {
                if (!eyeHp) {
                    continue;
                }
                const char* rawEyeTri =
                    eyeHp->morphs[RE::BGSHeadPart::MorphIndices::kChargenMorph].model.c_str();
                const std::string eyeTriPath = BuildMeshVfsPath(rawEyeTri ? rawEyeTri : "");
                if (eyeTriPath.empty()) {
                    continue;
                }
                if (const auto* loadedEye = GetOrLoadTriLocked(eyeTriPath);
                    loadedEye && Core::GetTriAssetVertexCount(loadedEye) > 0u) {
                    asset->eyeChargenTri = loadedEye;
                    break;
                }
            }
            if (asset->eyeChargenTri != nullptr) {
                break;
            }
        }

        const auto* rawPtr = asset.get();
        canonicalAssetCache_.emplace(key, std::move(asset));
        return rawPtr;
    }

    const TriProjectorCache::TargetSynthesisContext* TriProjectorCache::GetOrBuildTargetContextLocked(
        RE::TESRace* targetRace,
        Foundation::Sex targetSex,
        Foundation::TopologyPool targetTopology) {
        if (!targetRace) {
            return nullptr;
        }
        const std::string key = std::format(
            "{:08X}_{}_{}", targetRace->GetFormID(), static_cast<unsigned>(targetSex), static_cast<unsigned>(targetTopology));
        if (const auto it = targetContextCache_.find(key); it != targetContextCache_.end()) {
            return it->second.get();
        }

        const CanonicalTriAsset* canon =
            GetOrBuildCanonicalTriAssetLocked(targetRace, targetSex, targetTopology);
        if (!canon || !canon->rig || !canon->chargenTri) {
            targetContextCache_.emplace(key, nullptr);
            return nullptr;
        }

        const auto anchorPool =
            FaceAnchorCatalog::GetSingleton().GatherMorphPoolForRace(targetRace, targetSex);
        auto targetRig = Core::CreateTargetRig(canon->rig, canon->restRightEyeCenter, anchorPool);
        if (!targetRig) {
            targetContextCache_.emplace(key, nullptr);
            return nullptr;
        }

        auto ctx = std::make_unique<TargetSynthesisContext>();
        ctx->canonical = *canon;
        ctx->targetRig = std::move(targetRig);
        ctx->valid = true;

        MORWHA_LOG_INFO(
            "TriProjectorCache: Built TargetSynthesisContext key={} verts={}",
            key,
            Core::GetRaceRigVertexCount(canon->rig));

        const auto* rawPtr = ctx.get();
        targetContextCache_.emplace(key, std::move(ctx));
        return rawPtr;
    }

    const Core::OpaqueDonorRig* TriProjectorCache::GetOrBuildDonorBaselineLocked(
        const TargetSynthesisContext& targetCtx,
        std::string_view targetKey,
        RE::TESRace* donorRace,
        Foundation::Sex donorSex,
        Foundation::TopologyPool donorTopology) {
        if (!donorRace || !targetCtx.targetRig) {
            return nullptr;
        }
        const std::string key = std::format(
            "{}|{:08X}_{}_{}",
            targetKey,
            donorRace->GetFormID(),
            static_cast<unsigned>(donorSex),
            static_cast<unsigned>(donorTopology));
        if (const auto it = donorBaselineCache_.find(key); it != donorBaselineCache_.end()) {
            return it->second.get();
        }

        const CanonicalTriAsset* donorCanon =
            GetOrBuildCanonicalTriAssetLocked(donorRace, donorSex, donorTopology);
        if (!donorCanon || !donorCanon->rig || !donorCanon->chargenTri) {
            donorBaselineCache_.emplace(key, nullptr);
            return nullptr;
        }

        auto donorRig = Core::CreateDonorRig(
            targetCtx.targetRig.get(), donorCanon->rig, donorCanon->restRightEyeCenter);
        if (!donorRig) {
            donorBaselineCache_.emplace(key, nullptr);
            return nullptr;
        }

        const auto* rawPtr = donorRig.get();
        donorBaselineCache_.emplace(key, std::move(donorRig));
        return rawPtr;
    }

    Core::EyeSample TriProjectorCache::DecodeIrisTextureLocked(
        std::string_view rawTexturePath,
        std::array<float, 2> uvCenter) {
        const std::string vfsPath = BuildTextureVfsPath(rawTexturePath);
        if (vfsPath.empty()) {
            return {};
        }

        const std::string cacheKey = std::format(
            "{}|{:.2f},{:.2f}", ToAsciiLower(vfsPath), uvCenter[0], uvCenter[1]);
        if (const auto it = irisTextureCache_.find(cacheKey); it != irisTextureCache_.end()) {
            return it->second;
        }

        std::array<std::uint8_t, 148u> hdrBuf{};
        std::size_t hdrBytesRead = 0u;
        std::size_t totalFileBytes = 0u;
        if (!ReadVfsFileHeaderAndTotalSize(vfsPath, hdrBuf, hdrBytesRead, totalFileBytes)) {
            irisTextureCache_.emplace(cacheKey, Core::EyeSample{});
            return {};
        }

        Core::DdsSlicePlan plan{};
        if (!Core::PlanDdsSlice(
                std::span<const std::uint8_t>(hdrBuf.data(), hdrBytesRead),
                totalFileBytes,
                plan,
                32u)) {
            MORWHA_LOG_DEBUG("TriProjectorCache: Skip DDS '{}'", vfsPath);
            irisTextureCache_.emplace(cacheKey, Core::EyeSample{});
            return {};
        }

        std::vector<std::uint8_t> mipSlice;
        if (!ReadVfsFileSlice(vfsPath, plan.fileByteOffset, plan.sliceByteSize, mipSlice)) {
            irisTextureCache_.emplace(cacheKey, Core::EyeSample{});
            return {};
        }

        std::array<std::uint8_t, 32u * 32u * 4u> decoded{};
        if (!Core::DecodeDdsSlice32x32(plan, mipSlice, decoded)) {
            MORWHA_LOG_DEBUG("TriProjectorCache: DDS decode failed '{}'", vfsPath);
            irisTextureCache_.emplace(cacheKey, Core::EyeSample{});
            return {};
        }

        std::array<std::uint8_t, 64u * 64u * 4u> decoded64{};
        bool hasDecoded64 = false;
        Core::DdsSlicePlan plan64{};
        if (Core::PlanDdsSlice(
                std::span<const std::uint8_t>(hdrBuf.data(), hdrBytesRead),
                totalFileBytes,
                plan64,
                64u)) {
            std::vector<std::uint8_t> mipSlice64;
            if (ReadVfsFileSlice(vfsPath, plan64.fileByteOffset, plan64.sliceByteSize, mipSlice64) &&
                Core::DecodeDdsSlice64x64(plan64, mipSlice64, decoded64)) {
                hasDecoded64 = true;
            }
        }

        const auto sig = Core::AnalyzeEyeMipmaps(
            decoded, hasDecoded64 ? &decoded64 : nullptr, uvCenter);
        irisTextureCache_.emplace(cacheKey, sig);
        return sig;
    }

    Core::EyeSample TriProjectorCache::GetOrDecodeIrisTexture(
        std::string_view rawTexturePath,
        std::array<float, 2> uvCenter) {
        if (rawTexturePath.empty()) {
            return {};
        }
        std::lock_guard lock(mutex_);
        return DecodeIrisTextureLocked(rawTexturePath, uvCenter);
    }

    Core::EyeSample TriProjectorCache::GetOrDecodeEyeHeadPartIris(
        const RE::BGSHeadPart* eyePart) {
        if (!eyePart) {
            return {};
        }
        std::lock_guard lock(mutex_);
        const RE::FormID partId = eyePart->GetFormID();
        if (const auto it = headPartIrisCache_.find(partId); it != headPartIrisCache_.end()) {
            return it->second;
        }

        std::string texPath;
        if (eyePart->textureSet != nullptr) {
            const char* p =
                eyePart->textureSet->textures[RE::BSTextureSet::Texture::kDiffuse].textureName.c_str();
            if (p && p[0] != '\0') {
                texPath = p;
            }
        }

        const EyeNifMetadata meta = GetOrParseEyeNifMetadataLocked(
            eyePart->model.c_str() ? eyePart->model.c_str() : "");
        if (texPath.empty()) {
            texPath = meta.fallbackDiffusePath;
        }

        Core::EyeSample sig{};
        if (!texPath.empty()) {
            sig = DecodeIrisTextureLocked(texPath, meta.corneaUv);
        }
        if (meta.hasEmissive) {
            Core::SetEyeSampleEmissive(sig, true);
        }
        headPartIrisCache_.emplace(partId, sig);
        return sig;
    }

    DonorGeometryBundle TriProjectorCache::ExtractDonorGeometryAndIris(
        RE::Actor* optionalActor,
        RE::TESNPC* rootFaceNpc,
        RE::TESRace* originalRace,
        Foundation::Sex sex,
        bool isPlayer,
        const Foundation::FaceMorphVector& tier1Morphs,
        bool tier1HasNonZero,
        RE::BGSHeadPart* eyePart) {
        DonorGeometryBundle out{};
        out.donorSex = sex;

        RE::TESRace* unwrappedRace = UnwrapToPlayableSynthesisRace(originalRace, sex);
        if (!unwrappedRace && rootFaceNpc) {
            unwrappedRace = UnwrapToPlayableSynthesisRace(rootFaceNpc->GetRace(), sex);
        }
        out.unwrappedDonorRace = unwrappedRace;

        const RE::FormID rootFormId = rootFaceNpc ? rootFaceNpc->GetFormID() : 0u;
        const std::string_view npcName =
            (rootFaceNpc && rootFaceNpc->GetName()) ? rootFaceNpc->GetName() : "";

        // Priority 1 (Player, or non-VFS loaded fallback): Live RaceMenu/UBE/COtR FaceGen SceneGraph.
        if (isPlayer && optionalActor && optionalActor->Is3DLoaded()) {
            auto live = ExtractLiveActorFaceScene(optionalActor);
            if (live.valid && live.scene.HeadMesh() != nullptr) {
                out.has3dGeometry = true;
                out.sourceTier = DonorExtractionTier::Tier2NifProjected;
                out.sourceVertexCount = live.scene.HeadMesh()->vertexCount;
                out.nifHairShaderColor = live.scene.hairShaderColor;
                out.faceScene = std::move(live.scene);
            }
        }

        // Priority 2 (Static / Overhaul NPCs): VFS FaceGeom .nif (immune to idle blinks & dialogue smiles).
        if (!out.has3dGeometry && !isPlayer && rootFaceNpc && !IsDynamicDpfNpc(rootFaceNpc)) {
            std::lock_guard lock(mutex_);
            if (const auto cachedIt = npcNifSceneCache_.find(rootFormId);
                cachedIt != npcNifSceneCache_.end()) {
                out.faceScene = cachedIt->second;
            } else {
                const std::string faceGeomPath = BuildNpcFaceGeomVfsPath(rootFaceNpc);
                std::vector<std::uint8_t> nifBytes;
                if (!faceGeomPath.empty() && ReadVfsFileBytes(faceGeomPath, nifBytes)) {
                    RE::BGSHeadPart* faceHp =
                        rootFaceNpc->GetCurrentHeadPartByType(RE::BGSHeadPart::HeadPartType::kFace);
                    if (!faceHp) {
                        faceHp = rootFaceNpc->GetHeadPartByType(RE::BGSHeadPart::HeadPartType::kFace);
                    }
                    const std::string_view prefEdid =
                        (faceHp && faceHp->GetFormEditorID()) ? faceHp->GetFormEditorID() : "";
                    Core::ExtractedHeadScene parsed{};
                    if (Core::ParseHeadSceneNif(nifBytes, parsed, prefEdid, 0u)) {
                        npcNifSceneCache_.emplace(rootFormId, parsed);
                        out.faceScene = std::move(parsed);
                    } else {
                        MORWHA_LOG_WARN(
                            "TriProjectorCache: Failed FaceGeom parse '{}' for {:08X} ('{}')",
                            faceGeomPath,
                            rootFormId,
                            npcName);
                        npcNifSceneCache_.emplace(rootFormId, Core::ExtractedHeadScene{});
                    }
                } else {
                    npcNifSceneCache_.emplace(rootFormId, Core::ExtractedHeadScene{});
                }
            }
            if (const auto* headPtr = out.faceScene.HeadMesh();
                headPtr != nullptr && headPtr->vertexCount >= 100u) {
                out.has3dGeometry = true;
                out.sourceTier = DonorExtractionTier::Tier2NifProjected;
                out.sourceVertexCount = headPtr->vertexCount;
                out.nifHairShaderColor = out.faceScene.hairShaderColor;
            }
        }

        // Priority 3: Loaded non-player actor lacking a VFS FaceGeom .nif (e.g. DPF spawn or live mod NPC).
        if (!out.has3dGeometry && !isPlayer && optionalActor && optionalActor->Is3DLoaded()) {
            auto live = ExtractLiveActorFaceScene(optionalActor);
            if (live.valid && live.scene.HeadMesh() != nullptr) {
                out.has3dGeometry = true;
                out.sourceTier = DonorExtractionTier::Tier2NifProjected;
                out.sourceVertexCount = live.scene.HeadMesh()->vertexCount;
                out.nifHairShaderColor = live.scene.hairShaderColor;
                out.faceScene = std::move(live.scene);
            }
        }

        // Priority 4 (Unloaded Tier-1 FaceData synthesis -> 3D vertices + shifted eyeballs).
        if (!out.has3dGeometry && unwrappedRace != nullptr && (tier1HasNonZero || !isPlayer)) {
            std::lock_guard lock(mutex_);
            const auto prefTopo =
                HeadPartPoolIndexer::GetSingleton().SelectPreferredTopology(unwrappedRace, sex);
            const auto* canon =
                GetOrBuildCanonicalTriAssetLocked(unwrappedRace, sex, prefTopo);
            if (canon && canon->chargenTri && canon->rig) {
                auto verts = Core::BuildMorphedVerticesFromTri(canon->rig, canon->chargenTri, tier1Morphs);
                if (!verts.empty()) {
                    const std::uint32_t vertCount = Core::GetRaceRigVertexCount(canon->rig);
                    const auto triSpan = Core::GetTriAssetTriangles(canon->chargenTri);
                    Core::ExtractedMeshBlock synthHead{};
                    synthHead.name = "Tier1SynthesizedHead";
                    synthHead.blockType = "BSDynamicTriShape";
                    synthHead.vertexCount = vertCount;
                    synthHead.vertices = std::move(verts);
                    synthHead.triangles.assign(triSpan.begin(), triSpan.end());

                    const auto shift = Core::EvaluateEyeTriMorphShift(canon->eyeChargenTri, tier1Morphs);
                    const Core::Float3 rightC{
                        canon->restRightEyeCenter.x + shift.x,
                        canon->restRightEyeCenter.y + shift.y,
                        canon->restRightEyeCenter.z + shift.z
                    };
                    const Core::Float3 leftC{ -rightC.x, rightC.y, rightC.z };
                    auto synthEyes = MakeSyntheticEyeSphereClouds(leftC, rightC);

                    out.faceScene.allMeshes.clear();
                    out.faceScene.allMeshes.push_back(std::move(synthHead));
                    out.faceScene.headMeshIndex = 0u;
                    out.faceScene.allMeshes.push_back(std::move(synthEyes));
                    out.faceScene.eyeMeshIndices = { 1u };
                    out.has3dGeometry = true;
                    out.sourceTier = DonorExtractionTier::Tier1LiveFaceData;
                    out.sourceVertexCount = vertCount;
                }
            }
        }

        bool hasEmissive = false;
        std::array<float, 2> uvCenter{ -1.0f, -1.0f };
        for (const std::size_t idx : out.faceScene.eyeMeshIndices) {
            if (idx < out.faceScene.allMeshes.size()) {
                const auto& em = out.faceScene.allMeshes[idx];
                if (em.hasActiveEmissiveGlow) {
                    hasEmissive = true;
                }
                if (uvCenter[0] < 0.0f && !em.vertices.empty() && !em.uvs.empty()) {
                    uvCenter = Core::LocateEyeTextureCenter(
                        em.vertices, em.uvs, em.triangles);
                }
            }
        }

        EyeNifMetadata eyeMeta{};
        if (eyePart && eyePart->model.c_str() != nullptr) {
            std::lock_guard lock(mutex_);
            eyeMeta = GetOrParseEyeNifMetadataLocked(eyePart->model.c_str());
            if (eyeMeta.hasEmissive) {
                hasEmissive = true;
            }
        }
        if (uvCenter[0] < 0.0f && eyeMeta.corneaUv[0] >= 0.0f) {
            uvCenter = eyeMeta.corneaUv;
        }

        std::string texToDecode = out.faceScene.eyeDiffuseTexturePath;
        if (isPlayer && eyePart && eyePart->textureSet != nullptr) {
            const char* hpTex =
                eyePart->textureSet->textures[RE::BSTextureSet::Texture::kDiffuse].textureName.c_str();
            if (hpTex && hpTex[0] != '\0') {
                const std::string normScene = ToAsciiLower(BuildTextureVfsPath(texToDecode));
                const std::string normFallback =
                    ToAsciiLower(BuildTextureVfsPath(eyeMeta.fallbackDiffusePath));
                if (normScene.empty() || (!normFallback.empty() && normScene == normFallback)) {
                    texToDecode = hpTex;
                }
            }
        }

        if (!texToDecode.empty()) {
            out.irisSignature = GetOrDecodeIrisTexture(texToDecode, uvCenter);
        }
        if (!Core::IsEyeSampleValid(out.irisSignature) && eyePart != nullptr) {
            out.irisSignature = GetOrDecodeEyeHeadPartIris(eyePart);
        }
        if (hasEmissive) {
            Core::SetEyeSampleEmissive(out.irisSignature, true);
        }

        const auto previewRgb = Core::GetEyeSamplePreviewRgb(out.irisSignature);
        MORWHA_LOG_INFO(
            "TriProjectorCache: donor={:08X} ('{}') tier={} verts={} eyeOk={} eyeRgb=({},{},{}) eyeTex='{}' mortalEye={} emissive={}",
            rootFormId,
            npcName,
            static_cast<unsigned>(out.sourceTier),
            out.sourceVertexCount,
            Core::IsEyeSampleValid(out.irisSignature),
            previewRgb.r,
            previewRgb.g,
            previewRgb.b,
            texToDecode,
            Core::IsEyeSampleNormalMortal(out.irisSignature),
            Core::HasEyeSampleEmissive(out.irisSignature));

        return out;
    }

    Target3dMorphSynthesisResult TriProjectorCache::SynthesizeTargetMorphsFromDonors3d(
        RE::TESRace* targetRace,
        Foundation::Sex targetSex,
        Foundation::TopologyPool targetTopology,
        const DonorGeometryBundle* donorA,
        const DonorGeometryBundle* donorB,
        const std::array<float, static_cast<std::size_t>(Foundation::FaceCluster::kCount)>& dominanceA,
        const Foundation::FaceMorphVector& individualAnchor,
        const std::array<float, 18>& morphNoise,
        Foundation::Seed64 seed,
        bool isOneDonorRelative,
        bool allowCrossRaceMorphs) {
        Target3dMorphSynthesisResult out{};
        if (!targetRace) {
            return out;
        }

        RE::TESRace* unwrappedTarget = UnwrapToPlayableSynthesisRace(targetRace, targetSex);
        if (!unwrappedTarget) {
            unwrappedTarget = targetRace;
        }

        std::lock_guard lock(mutex_);
        const std::string targetKey = std::format(
            "{:08X}_{}_{}",
            unwrappedTarget->GetFormID(),
            static_cast<unsigned>(targetSex),
            static_cast<unsigned>(targetTopology));

        const TargetSynthesisContext* ctx =
            GetOrBuildTargetContextLocked(unwrappedTarget, targetSex, targetTopology);
        if (!ctx || !ctx->valid || !ctx->targetRig || !ctx->canonical.rig) {
            return out;
        }

        struct PreparedDonorBuffers {
            std::vector<Core::MeshView> eyeViews{};
            Core::DonorMeshInput input{};
        };

        auto prepareDonor = [&](const DonorGeometryBundle* donor, PreparedDonorBuffers& buf) {
            if (!donor || !donor->has3dGeometry || !donor->unwrappedDonorRace) {
                return;
            }
            const auto* headMesh = donor->faceScene.HeadMesh();
            if (!headMesh || headMesh->vertexCount < 12u) {
                return;
            }
            const auto donorTopo = SelectDonorTopologyForVertexCountLocked(
                donor->unwrappedDonorRace, donor->donorSex, donor->sourceVertexCount);
            const CanonicalTriAsset* donorCanon =
                GetOrBuildCanonicalTriAssetLocked(donor->unwrappedDonorRace, donor->donorSex, donorTopo);
            if (!donorCanon || !donorCanon->rig || !donorCanon->chargenTri) {
                return;
            }
            const Core::OpaqueDonorRig* baseline = GetOrBuildDonorBaselineLocked(
                *ctx, targetKey, donor->unwrappedDonorRace, donor->donorSex, donorTopo);
            if (!baseline) {
                return;
            }
            buf.eyeViews = CollectEyeSurfaceViews(donor->faceScene);
            const auto* mouthPtr = donor->faceScene.MouthMesh();
            buf.input.valid = true;
            buf.input.donorSex = donor->donorSex;
            buf.input.donorCanonicalRig = donorCanon->rig;
            buf.input.donorBaselineRig = baseline;
            buf.input.headMesh = Core::MeshView{ headMesh->vertices, headMesh->triangles };
            buf.input.eyeMeshes = buf.eyeViews;
            buf.input.mouthVertices =
                mouthPtr ? std::span<const float>(mouthPtr->vertices) : std::span<const float>{};
        };

        PreparedDonorBuffers bufA{};
        PreparedDonorBuffers bufB{};
        prepareDonor(donorA, bufA);
        if (!isOneDonorRelative) {
            prepareDonor(donorB, bufB);
        }
        if (!bufA.input.valid && !bufB.input.valid) {
            return out;
        }

        const RE::TESRace* morphAllowRace = targetRace;
        const auto* faceRelated = ResolveRaceFaceRelatedData(targetRace, targetSex);
        if (!faceRelated) {
            faceRelated = ResolveRaceFaceRelatedData(unwrappedTarget, targetSex);
            morphAllowRace = unwrappedTarget;
        }
        std::vector<std::int32_t> allowedNose =
            ExtractAllowedMorphPresetIndices(morphAllowRace, targetSex, faceRelated, 0u);
        std::vector<std::int32_t> allowedEyes =
            ExtractAllowedMorphPresetIndices(morphAllowRace, targetSex, faceRelated, 2u);
        const auto allowedMouth = ExtractAllowedMorphPresetIndices(morphAllowRace, targetSex, faceRelated, 3u);

        const RE::TESRace* secondRaceForNoseEyes = nullptr;
        if (allowCrossRaceMorphs && !isOneDonorRelative && donorA && donorB &&
            donorA->unwrappedDonorRace && donorB->unwrappedDonorRace &&
            donorA->unwrappedDonorRace != donorB->unwrappedDonorRace) {
            const auto* canonA = GetOrBuildCanonicalTriAssetLocked(
                donorA->unwrappedDonorRace, targetSex, targetTopology);
            const auto* canonB = GetOrBuildCanonicalTriAssetLocked(
                donorB->unwrappedDonorRace, targetSex, targetTopology);
            if (canonA && canonA->chargenTri == ctx->canonical.chargenTri &&
                canonB && canonB->chargenTri == ctx->canonical.chargenTri) {
                secondRaceForNoseEyes = (donorA->unwrappedDonorRace != morphAllowRace)
                                            ? donorA->unwrappedDonorRace
                                            : donorB->unwrappedDonorRace;

                const auto* frA = ResolveRaceFaceRelatedData(donorA->unwrappedDonorRace, targetSex);
                auto unionNose = CollectAllowedMorphPresets(
                    donorA->unwrappedDonorRace, targetSex, frA, 0u, donorB->unwrappedDonorRace, false);
                auto unionEyes = CollectAllowedMorphPresets(
                    donorA->unwrappedDonorRace, targetSex, frA, 2u, donorB->unwrappedDonorRace, false);
                if (!unionNose.empty()) {
                    allowedNose = std::move(unionNose);
                }
                if (!unionEyes.empty()) {
                    allowedEyes = std::move(unionEyes);
                }
            }
        }

        const CanonicalRaceGeometry targetGeom{
            .chargenTri = ctx->canonical.chargenTri,
            .rig = ctx->canonical.rig,
            .restRightEyeCenter = ctx->canonical.restRightEyeCenter
        };
        const std::vector<float> anchorVerts = SynthesizeSanitizedAnchorVertices(
            targetGeom, morphAllowRace, targetSex, faceRelated, secondRaceForNoseEyes,
            allowedNose, allowedEyes, individualAnchor);

        Core::MeshProjectionRequest projReq{};
        projReq.targetRig = ctx->targetRig.get();
        projReq.targetSex = targetSex;
        projReq.isOneDonorRelative = isOneDonorRelative;
        projReq.donorA = bufA.input.valid ? &bufA.input : nullptr;
        projReq.donorB = (!isOneDonorRelative && bufB.input.valid) ? &bufB.input : nullptr;
        projReq.clusterDominanceA = dominanceA;
        projReq.sanitizedAnchorVertices = anchorVerts;
        projReq.morphNoise = morphNoise;
        projReq.allowedNosePresets = allowedNose;
        projReq.allowedEyesPresets = allowedEyes;
        projReq.allowedMouthPresets = allowedMouth;
        projReq.seed = seed;

        const auto proj = Core::ProjectDonorMeshesToTargetMorphs(projReq);
        out.skullResidualA = proj.donorAResidual;
        out.skullResidualB = proj.donorBResidual;
        out.projectedRmse = proj.fitError;
        out.rSquared = proj.fitScore;

        if (proj.succeeded) {
            out.succeeded = true;
            out.effectiveTier = DonorExtractionTier::Tier2NifProjected;
            out.faceMorphs = proj.faceMorphs;

            MORWHA_LOG_INFO(
                "TriProjectorCache: Synthesized 3D->{} target ({:08X}, sex={}) donorOk=({}/{}) res=({:.3f}/{:.3f}) fitErr={:.3f} fitScore={:.3f} parts=({},{},{})",
                static_cast<unsigned>(targetTopology),
                unwrappedTarget->GetFormID(),
                static_cast<unsigned>(targetSex),
                bufA.input.valid,
                bufB.input.valid,
                out.skullResidualA,
                out.skullResidualB,
                out.projectedRmse,
                out.rSquared,
                proj.faceMorphs.parts[0],
                proj.faceMorphs.parts[2],
                proj.faceMorphs.parts[3]);
        } else {
            MORWHA_LOG_WARN(
                "TriProjectorCache: 3D->target projection fell back (fitErr={:.3f}, donorOk=({}/{}))",
                proj.fitError,
                bufA.input.valid,
                bufB.input.valid);
        }
        return out;
    }

    std::vector<float> TriProjectorCache::SynthesizeSanitizedAnchorVertices(
        const CanonicalRaceGeometry& geom,
        const RE::TESRace* morphAllowRace,
        Foundation::Sex targetSex,
        const RE::TESRace::FaceRelatedData* faceRelated,
        const RE::TESRace* secondRaceForNoseEyes,
        const std::vector<std::int32_t>& allowedNose,
        const std::vector<std::int32_t>& allowedEyes,
        const Foundation::FaceMorphVector& individualAnchor) {
        if (!geom.chargenTri || !geom.rig) {
            return {};
        }
        Foundation::FaceMorphVector sanitizedAnchor = individualAnchor;
        SanitizeFaceMorphPartsForRace(
            sanitizedAnchor, morphAllowRace, targetSex, faceRelated, secondRaceForNoseEyes);
        auto clampSlotToAllowed = [](std::int32_t& partVal,
                                     const std::vector<std::int32_t>& allowed) noexcept {
            if (partVal == Foundation::kUnusedFacePartPreset || partVal < 0 || allowed.empty()) {
                return;
            }
            if (!std::binary_search(allowed.begin(), allowed.end(), partVal)) {
                partVal = allowed[static_cast<std::size_t>(partVal) % allowed.size()];
            }
        };
        const auto allowedMouth = CollectAllowedMorphPresets(
            morphAllowRace, targetSex, faceRelated, 3u, nullptr, false);
        clampSlotToAllowed(sanitizedAnchor.parts[0], allowedNose);
        clampSlotToAllowed(sanitizedAnchor.parts[2], allowedEyes);
        clampSlotToAllowed(sanitizedAnchor.parts[3], allowedMouth);
        Core::SanitizeHeadMorphClearance(geom.rig, sanitizedAnchor, allowedEyes, allowedMouth);
        return Core::BuildMorphedVerticesFromTri(geom.rig, geom.chargenTri, sanitizedAnchor);
    }

    void TriProjectorCache::EnforceOrbitalClearanceForTarget(
        RE::TESRace* targetRace,
        Foundation::Sex targetSex,
        Foundation::TopologyPool targetTopology,
        Foundation::FaceMorphVector& faceMorphs,
        std::span<const std::int32_t> allowedEyesPresets,
        std::span<const std::int32_t> allowedMouthPresets) {
        if (!targetRace) {
            return;
        }
        RE::TESRace* unwrapped = UnwrapToPlayableSynthesisRace(targetRace, targetSex);
        if (!unwrapped) {
            unwrapped = targetRace;
        }
        std::lock_guard lock(mutex_);
        const CanonicalTriAsset* canon =
            GetOrBuildCanonicalTriAssetLocked(unwrapped, targetSex, targetTopology);
        if (!canon || !canon->rig) {
            return;
        }
        Core::SanitizeHeadMorphClearance(
            canon->rig, faceMorphs, allowedEyesPresets, allowedMouthPresets);
    }

}  // namespace MORWHA::Engine
