#include "morwha/engine/HybridSkinService.h"

#include "morwha/engine/ChimeraAccessoryCatalog.h"
#include "morwha/engine/EngineLogging.h"
#include "morwha/engine/TesNpcHydrator.h"
#include "morwha/engine/VfsAssetReader.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <thread>

namespace MORWHA::Engine {

    namespace {

        constexpr std::string_view kGeneratedRelDir = "Actors\\Character\\MORWHA\\Generated\\";
        constexpr std::string_view kGeneratedDiskDir = "Data/Textures/Actors/Character/MORWHA/Generated";
        constexpr std::string_view kCacheManifestPath = "Data/SKSE/Plugins/MORWHA/GeneratedSkinCache.json";
        constexpr std::uint32_t kCacheFormatVersion = 3u;

        constexpr std::uint32_t kNordRaceFormId = 0x013746u;
        constexpr std::uint32_t kKhajiitRaceFormId = 0x013745u;
        constexpr std::uint32_t kArgonianRaceFormId = 0x013740u;
        constexpr std::uint32_t kSkinNakedFormId = 0x000D64u;
        constexpr std::uint32_t kSkinNakedBeastFormId = 0x069CE3u;

        [[nodiscard]] std::string ToLowerAscii(std::string_view sv) {
            std::string out;
            out.reserve(sv.size());
            for (const unsigned char ch : sv) {
                out.push_back(static_cast<char>(std::tolower(ch)));
            }
            return out;
        }

        [[nodiscard]] std::string StripTexturesPrefix(std::string_view raw) {
            while (!raw.empty() && (raw.front() == '\\' || raw.front() == '/' || raw.front() == ' ')) {
                raw.remove_prefix(1u);
            }
            std::string norm;
            norm.reserve(raw.size());
            for (const char ch : raw) {
                norm.push_back((ch == '/') ? '\\' : ch);
            }
            const std::string low = ToLowerAscii(norm);
            if (low.starts_with("data\\")) {
                norm.erase(0u, 5u);
            }
            const std::string low2 = ToLowerAscii(norm);
            if (low2.starts_with("textures\\")) {
                norm.erase(0u, 9u);
            }
            return norm;
        }

        [[nodiscard]] bool VfsTextureExists(std::string_view relTexPath) {
            if (relTexPath.empty()) {
                return false;
            }
            std::array<std::uint8_t, 16u> hdr{};
            std::size_t bytesRead = 0u, totalBytes = 0u;
            return ReadVfsFileHeaderAndTotalSize(BuildTextureVfsPath(relTexPath), hdr, bytesRead, totalBytes) &&
                   totalBytes >= 128u;
        }

        void FnvMixBytes(std::uint64_t& h, const void* data, std::size_t len) noexcept {
            const auto* p = static_cast<const std::uint8_t*>(data);
            for (std::size_t i = 0u; i < len; ++i) {
                h ^= p[i];
                h *= 0x100000001B3ull;
            }
        }

        void FnvMixString(std::uint64_t& h, std::string_view s) noexcept {
            FnvMixBytes(h, s.data(), s.size());
            const std::uint8_t sep = 0xFFu;
            FnvMixBytes(h, &sep, 1u);
        }

        [[nodiscard]] std::string_view SlotTagFor(auto slot) noexcept {
            using SlotEnum = std::decay_t<decltype(slot)>;
            if (slot == SlotEnum::Face) {
                return "face";
            }
            return (slot == SlotEnum::Body) ? "body" : "hands";
        }

        [[nodiscard]] std::string MakeCoatSlotCacheKey(
            std::string_view coatId,
            Foundation::Sex sex,
            std::string_view slotTag,
            State::GeneratedSkinResolution mode) {
            return std::format(
                "{}_{}_{}_{}",
                coatId,
                (sex == Foundation::Sex::Female) ? 'f' : 'm',
                slotTag,
                State::GeneratedSkinResolutionToString(mode));
        }

        [[nodiscard]] std::string MakeArchetypeCacheKey(
            std::string_view coatId,
            Foundation::Sex sex,
            State::GeneratedSkinResolution mode) {
            return std::format(
                "{}_{}_{}",
                coatId,
                (sex == Foundation::Sex::Female) ? 'F' : 'M',
                State::GeneratedSkinResolutionToString(mode));
        }

        void ParallelSliceRunner(
            std::uint32_t totalItems,
            const std::function<void(std::uint32_t, std::uint32_t)>& body) {
            if (totalItems == 0u) {
                return;
            }
            const std::uint32_t maxWorkers =
                (std::min)(8u, (std::max)(1u, std::thread::hardware_concurrency()));
            if (maxWorkers <= 1u || totalItems < 16u) {
                body(0u, totalItems);
                return;
            }
            const std::uint32_t chunk = (std::max)(4u, (totalItems + maxWorkers * 2u - 1u) / (maxWorkers * 2u));
            std::atomic<std::uint32_t> next{ 0u };
            std::vector<std::jthread> workers;
            workers.reserve(maxWorkers);
            for (std::uint32_t w = 0u; w < maxWorkers; ++w) {
                workers.emplace_back([&]() {
                    while (true) {
                        const std::uint32_t b = next.fetch_add(chunk);
                        if (b >= totalItems) {
                            break;
                        }
                        body(b, (std::min)(totalItems, b + chunk));
                    }
                });
            }
        }

        [[nodiscard]] bool DecodeVfsTextureOrSolid(
            std::string_view relTexPath,
            std::uint32_t width,
            std::uint32_t height,
            std::array<std::uint8_t, 4> fallbackRgba,
            std::vector<std::uint8_t>& outRgba) {
            if (!relTexPath.empty()) {
                const std::string vfsPath = BuildTextureVfsPath(relTexPath);
                std::array<std::uint8_t, 148u> hdr{};
                std::size_t hdrRead = 0u, totalBytes = 0u;
                if (ReadVfsFileHeaderAndTotalSize(vfsPath, hdr, hdrRead, totalBytes) && hdrRead >= 128u) {
                    Core::DdsSlicePlan plan{};
                    if (Core::PlanDdsSlice(
                            std::span<const std::uint8_t>(hdr.data(), hdrRead),
                            totalBytes,
                            plan,
                            width)) {
                        std::vector<std::uint8_t> slice;
                        if (ReadVfsFileSlice(vfsPath, plan.fileByteOffset, plan.sliceByteSize, slice) &&
                            Core::DecodeDdsSliceRgba(plan, slice, width, height, outRgba)) {
                            return true;
                        }
                    }
                }
            }
            const std::size_t pxCount = static_cast<std::size_t>(width) * height;
            outRgba.resize(pxCount * 4u);
            for (std::size_t i = 0u; i < pxCount; ++i) {
                std::memcpy(outRgba.data() + i * 4u, fallbackRgba.data(), 4u);
            }
            return false;
        }

        [[nodiscard]] bool WriteBinaryFileAtomic(
            const std::filesystem::path& dstPath,
            std::span<const std::uint8_t> bytes) {
            std::error_code ec;
            if (const auto parent = dstPath.parent_path(); !parent.empty()) {
                std::filesystem::create_directories(parent, ec);
            }
            std::filesystem::path tmpPath = dstPath;
            tmpPath += ".tmp";
            {
                std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
                if (!out.is_open()) {
                    return false;
                }
                out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
                out.flush();
                if (!out.good()) {
                    out.close();
                    std::filesystem::remove(tmpPath, ec);
                    return false;
                }
            }
            std::filesystem::rename(tmpPath, dstPath, ec);
            if (ec) {
                std::filesystem::copy_file(
                    tmpPath, dstPath, std::filesystem::copy_options::overwrite_existing, ec);
                std::error_code rmEc;
                std::filesystem::remove(tmpPath, rmEc);
                return !ec;
            }
            return true;
        }

        void CopyModelTextureSwapLocal(
            RE::TESModelTextureSwap& dst,
            const RE::TESModelTextureSwap& src) {
            const char* modelPath = src.GetModel();
            dst.SetModel((modelPath && modelPath[0] != '\0') ? modelPath : "");
            if (src.alternateTextures && src.numAlternateTextures > 0u) {
                auto* arr =
                    RE::calloc<RE::TESModelTextureSwap::AlternateTexture>(src.numAlternateTextures);
                if (arr) {
                    for (std::uint32_t i = 0u; i < src.numAlternateTextures; ++i) {
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

    }  // namespace

    HybridSkinService& HybridSkinService::GetSingleton() noexcept {
        static HybridSkinService instance;
        return instance;
    }

    std::size_t HybridSkinService::GetDiscoveredStencilCount() const noexcept {
        std::lock_guard lock(mutex_);
        return availableStencilFiles_.size();
    }

    HybridSkinService::BaseSkinFamily HybridSkinService::FamilyForCoatGroup(
        Core::HybridCoatGroup group) noexcept {
        switch (group) {
        case Core::HybridCoatGroup::HumanoidKhajiitFur:
        case Core::HybridCoatGroup::HumanoidArgonianLizard:
            return BaseSkinFamily::Humanoid;
        case Core::HybridCoatGroup::ArgonianKhajiitFur:
            return BaseSkinFamily::Argonian;
        case Core::HybridCoatGroup::KhajiitArgonianLizard:
            return BaseSkinFamily::Khajiit;
        }
        return BaseSkinFamily::Humanoid;
    }

    std::filesystem::path HybridSkinService::FindStencilPath(
        BaseSkinFamily family,
        Foundation::Sex sex,
        SkinSurfaceSlot slot,
        Core::HybridStencilKind kind) const {
        const std::string_view sexTag = (sex == Foundation::Sex::Female) ? "f" : "m";
        const std::string_view kindStem = Core::HybridStencilKindStem(kind);

        if (slot == SkinSurfaceSlot::Face) {
            std::string_view famPrefix = "hum";
            if (family == BaseSkinFamily::Khajiit) {
                famPrefix = "khaj";
            } else if (family == BaseSkinFamily::Argonian) {
                famPrefix = "arg";
            }
            const std::string filename = std::format("{}_{}_face_{}.mrst", famPrefix, sexTag, kindStem);
            if (!availableStencilFiles_.contains(ToLowerAscii(filename))) {
                return {};
            }
            return std::filesystem::path(Core::kDefaultHybridStencilsDir) / filename;
        }

        const auto famIdx = static_cast<std::size_t>(family);
        const auto sexIdx = static_cast<std::size_t>(sex);
        const std::string_view slotTag = (slot == SkinSurfaceSlot::Body) ? "body" : "hands";
        const Core::BodyUvFamily uvFamily =
            (famIdx < 3u && sexIdx < 2u)
                ? basePaths_[famIdx][sexIdx].uvFamily
                : ((sex == Foundation::Sex::Female) ? Core::BodyUvFamily::Cbbe : Core::BodyUvFamily::Himbo);

        std::string filename = std::format(
            "hum_{}_{}_{}_{}.mrst", sexTag, Core::BodyUvFamilyStem(uvFamily), slotTag, kindStem);
        if (!availableStencilFiles_.contains(ToLowerAscii(filename))) {
            const std::string_view fallbackUv = (sex == Foundation::Sex::Female) ? "cbbe" : "himbo";
            filename = std::format("hum_{}_{}_{}_{}.mrst", sexTag, fallbackUv, slotTag, kindStem);
            if (!availableStencilFiles_.contains(ToLowerAscii(filename))) {
                return {};
            }
        }
        return std::filesystem::path(Core::kDefaultHybridStencilsDir) / filename;
    }

    void HybridSkinService::DiscoverBaseSkinPathsFromEngine() {
        for (auto& famArr : basePaths_) {
            famArr[0] = FamilySexBasePaths{};
            famArr[1] = FamilySexBasePaths{};
        }
        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            return;
        }

        auto extractFromTxst = [](RE::BGSTextureSet* txst, SurfaceSourcePaths& out) {
            if (!txst) {
                return;
            }
            auto pull = [&](RE::BSTextureSet::Texture slot, std::string& dst) {
                if (const char* p = txst->GetTexturePath(slot); p && p[0] != '\0') {
                    std::string clean = StripTexturesPrefix(p);
                    if (VfsTextureExists(clean)) {
                        dst = std::move(clean);
                    }
                }
            };
            // In Skyrim skin BGSTextureSets (Skyrim.esm SkinBodyFemale_1 / SkinHeadFemaleNord & RSV):
            //   TX00 (kDiffuse)  = diffuse (.dds)
            //   TX01 (kNormal)   = object-space normal (_msn.dds)
            //   TX03 (kGlowMap)  = subsurface / soft-lighting (_sk.dds)  [TX02 is empty!]
            //   TX04 (kHeight)   = head complexion detail map (Head TXSTs only)
            //   TX07 (kSpecular) = specular (_s.dds)
            pull(RE::BSTextureSet::Texture::kDiffuse, out.diffuseRel);
            pull(RE::BSTextureSet::Texture::kNormal, out.normalRel);
            pull(RE::BSTextureSet::Texture::kGlowMap, out.subsurfRel);
            pull(RE::BSTextureSet::Texture::kSpecular, out.specRel);
            pull(RE::BSTextureSet::Texture::kHeight, out.detailRel);
        };

        auto applyFallbacks = [](BaseSkinFamily fam, Foundation::Sex sex, FamilySexBasePaths& paths) {
            const bool fem = (sex == Foundation::Sex::Female);
            std::string_view dir;
            std::string_view headStem, bodyStem, handsStem;
            if (fam == BaseSkinFamily::Humanoid) {
                dir = fem ? "Actors\\Character\\Female\\" : "Actors\\Character\\Male\\";
                headStem = fem ? "FemaleHead" : "MaleHead";
                bodyStem = fem ? "FemaleBody_1" : "MaleBody_1";
                handsStem = fem ? "FemaleHands_1" : "MaleHands_1";
            } else if (fam == BaseSkinFamily::Khajiit) {
                dir = fem ? "Actors\\Character\\KhajiitFemale\\" : "Actors\\Character\\KhajiitMale\\";
                headStem = fem ? "FemaleHead" : "MaleHead";
                bodyStem = fem ? "FemaleBody" : "MaleBody";
                handsStem = fem ? "FemaleHands" : "MaleHands";
            } else {
                dir = fem ? "Actors\\Character\\ArgonianFemale\\" : "Actors\\Character\\ArgonianMale\\";
                headStem = fem ? "ArgonianFemaleHead" : "ArgonianMaleHead";
                bodyStem = fem ? "ArgonianFemaleBody" : "ArgonianMaleBody";
                handsStem = fem ? "ArgonianFemaleHands" : "ArgonianMaleHands";
            }

            auto fillSlot = [&](SurfaceSourcePaths& s, std::string_view stem) {
                const std::string base = std::string(dir) + std::string(stem);
                if (s.diffuseRel.empty() && VfsTextureExists(base + ".dds")) s.diffuseRel = base + ".dds";
                if (s.normalRel.empty() && VfsTextureExists(base + "_msn.dds")) s.normalRel = base + "_msn.dds";
                if (s.subsurfRel.empty() && VfsTextureExists(base + "_sk.dds")) s.subsurfRel = base + "_sk.dds";
                if (s.specRel.empty() && VfsTextureExists(base + "_s.dds")) s.specRel = base + "_s.dds";
            };
            fillSlot(paths.face, headStem);
            fillSlot(paths.body, bodyStem);
            fillSlot(paths.hands, handsStem);
            if (fam == BaseSkinFamily::Khajiit && !fem) {
                fillSlot(paths.face, "KhajiitMaleHead");
                fillSlot(paths.body, "KhajiitMaleBody");
                fillSlot(paths.hands, "KhajiitMaleHands");
            }
            if (paths.face.detailRel.empty()) {
                constexpr std::string_view kBlankDetail = "Actors\\Character\\Male\\BlankDetailmap.dds";
                if (VfsTextureExists(kBlankDetail)) {
                    paths.face.detailRel = std::string(kBlankDetail);
                }
            }
        };

        const std::array<std::pair<BaseSkinFamily, std::uint32_t>, 3> kRaces{ {
            { BaseSkinFamily::Humanoid, kNordRaceFormId },
            { BaseSkinFamily::Khajiit, kKhajiitRaceFormId },
            { BaseSkinFamily::Argonian, kArgonianRaceFormId }
        } };

        std::unordered_map<std::string, Core::BodyUvFamily> bodyUvCache;

        for (const auto [fam, formId] : kRaces) {
            auto* race = dataHandler->LookupForm<RE::TESRace>(formId, "Skyrim.esm");
            for (const auto sex : { Foundation::Sex::Male, Foundation::Sex::Female }) {
                const auto sexIdx = static_cast<std::size_t>(sex);
                const auto reSex = (sex == Foundation::Sex::Female) ? RE::SEXES::kFemale : RE::SEXES::kMale;
                auto& dst = basePaths_[static_cast<std::size_t>(fam)][sexIdx];
                dst.uvFamily =
                    (sex == Foundation::Sex::Female) ? Core::BodyUvFamily::Cbbe : Core::BodyUvFamily::Himbo;
                std::string bodyModelRel{};

                if (race) {
                    if (const auto* fr = ResolveRaceFaceRelatedData(race, sex)) {
                        extractFromTxst(fr->defaultFaceDetailsTextureSet, dst.face);
                    }
                    if (race->skin) {
                        for (auto* arma : race->skin->armorAddons) {
                            if (!arma || !arma->IsValidRace(race)) {
                                continue;
                            }
                            if (arma->HasPartOf(RE::BGSBipedObjectForm::BipedObjectSlot::kBody)) {
                                if (bodyModelRel.empty()) {
                                    if (const char* m = arma->bipedModels[reSex].GetModel(); m && m[0] != '\0') {
                                        bodyModelRel = m;
                                    }
                                }
                                if (dst.body.diffuseRel.empty()) {
                                    extractFromTxst(arma->skinTextures[reSex], dst.body);
                                }
                            }
                            if (arma->HasPartOf(RE::BGSBipedObjectForm::BipedObjectSlot::kHands) &&
                                dst.hands.diffuseRel.empty()) {
                                extractFromTxst(arma->skinTextures[reSex], dst.hands);
                            }
                        }
                    }
                }
                if (bodyModelRel.empty()) {
                    bodyModelRel = (sex == Foundation::Sex::Female)
                        ? "Actors\\Character\\Character Assets\\FemaleBody_1.nif"
                        : "Actors\\Character\\Character Assets\\MaleBody_1.nif";
                }
                {
                    const std::string vfsMeshPath = BuildMeshVfsPath(bodyModelRel);
                    const std::string cacheKey =
                        std::format("{}:{}", (sex == Foundation::Sex::Female) ? 'F' : 'M', ToLowerAscii(vfsMeshPath));
                    if (const auto uvIt = bodyUvCache.find(cacheKey); uvIt != bodyUvCache.end()) {
                        dst.uvFamily = uvIt->second;
                    } else {
                        std::vector<std::uint8_t> nifBytes;
                        if (ReadVfsFileBytes(vfsMeshPath, nifBytes) && !nifBytes.empty()) {
                            dst.uvFamily = Core::ClassifyBodyMeshUvFamily(nifBytes, sex);
                        }
                        bodyUvCache.emplace(cacheKey, dst.uvFamily);
                    }
                }
                applyFallbacks(fam, sex, dst);
                // Humanoid Male/Female head _msn in Nord defaultFaceDetails may point to Nord-specific brow;
                // prefer neutral Female/MaleHead_msn.dds when present so all 8 Human/Mer races share cleanly.
                if (fam == BaseSkinFamily::Humanoid) {
                    const std::string neutralHeadMsn = (sex == Foundation::Sex::Female)
                        ? "Actors\\Character\\Female\\FemaleHead_msn.dds"
                        : "Actors\\Character\\Male\\MaleHead_msn.dds";
                    if (VfsTextureExists(neutralHeadMsn)) {
                        dst.face.normalRel = neutralHeadMsn;
                    }
                }
                dst.valid = !dst.face.diffuseRel.empty() &&
                            !dst.body.diffuseRel.empty() &&
                            !dst.hands.diffuseRel.empty();
            }
        }

        // Discover Khajiit & Argonian tail source textures (preferring NakedTail* ARMA TXSTs if modded,
        // falling back to Beast body maps, with Humanoid _sk.dds fallback matching vanilla tail NIFs).
        for (const auto [tIdx, tailKind, beastFam] : {
                 std::tuple{ 0u, State::BeastTailKind::Khajiit, BaseSkinFamily::Khajiit },
                 std::tuple{ 1u, State::BeastTailKind::Argonian, BaseSkinFamily::Argonian } }) {
            RE::TESObjectARMA* tailArma =
                ChimeraAccessoryCatalog::GetSingleton().GetClonedTailArma(tailKind);
            for (const auto sex : { Foundation::Sex::Male, Foundation::Sex::Female }) {
                const auto sexIdx = static_cast<std::size_t>(sex);
                const auto reSex = (sex == Foundation::Sex::Female) ? RE::SEXES::kFemale : RE::SEXES::kMale;
                auto& dstTail = tailSourcePaths_[tIdx][sexIdx];
                dstTail = {};
                if (tailArma) {
                    extractFromTxst(tailArma->skinTextures[reSex], dstTail);
                }
                const auto& beastBody = basePaths_[static_cast<std::size_t>(beastFam)][sexIdx].body;
                const auto& humBody = basePaths_[static_cast<std::size_t>(BaseSkinFamily::Humanoid)][sexIdx].body;
                if (dstTail.diffuseRel.empty()) dstTail.diffuseRel = beastBody.diffuseRel;
                if (dstTail.normalRel.empty()) dstTail.normalRel = beastBody.normalRel;
                if (dstTail.specRel.empty()) dstTail.specRel = beastBody.specRel;
                if (dstTail.subsurfRel.empty()) {
                    dstTail.subsurfRel = !beastBody.subsurfRel.empty()
                        ? beastBody.subsurfRel
                        : humBody.subsurfRel;
                }
            }
        }
    }

    HybridSkinService::VfsTextureMeta HybridSkinService::ProbeVfsTexture(std::string_view relTexPath) {
        if (relTexPath.empty()) {
            return {};
        }
        const std::string key = ToLowerAscii(relTexPath);
        if (const auto it = vfsMetaCache_.find(key); it != vfsMetaCache_.end()) {
            return it->second;
        }

        VfsTextureMeta meta{};
        std::array<std::uint8_t, 148u> hdr{};
        std::size_t readBytes = 0u, totalBytes = 0u;
        if (ReadVfsFileHeaderAndTotalSize(BuildTextureVfsPath(relTexPath), hdr, readBytes, totalBytes) &&
            readBytes >= 128u && totalBytes >= 128u) {
            std::uint64_t h = 0xCBF29CE484222325ull;
            FnvMixBytes(h, &totalBytes, sizeof(totalBytes));
            FnvMixBytes(h, hdr.data(), readBytes);
            meta.headerHash = h;

            Core::DdsSlicePlan plan{};
            if (Core::PlanDdsSlice(
                    std::span<const std::uint8_t>(hdr.data(), readBytes),
                    totalBytes,
                    plan,
                    4096u)) {
                meta.exists = true;
                meta.topWidth = plan.topWidth;
                meta.topHeight = plan.topHeight;
            }
        }

        vfsMetaCache_.emplace(key, meta);
        return meta;
    }

    void HybridSkinService::LoadCacheManifest() {
        cacheFingerprints_.clear();
        std::error_code ec;
        if (!std::filesystem::is_regular_file(kCacheManifestPath, ec)) {
            return;
        }
        std::ifstream in{ std::filesystem::path(kCacheManifestPath) };
        if (!in.is_open()) {
            return;
        }
        try {
            const auto j = nlohmann::json::parse(in, nullptr, /*allow_exceptions=*/false);
            if (!j.is_object()) {
                return;
            }
            const std::uint32_t diskVer = j.value("version", 0u);
            if (diskVer != kCacheFormatVersion) {
                in.close();
                std::size_t purged = 0u;
                if (std::filesystem::is_directory(kGeneratedDiskDir, ec)) {
                    for (const auto& entry : std::filesystem::directory_iterator(kGeneratedDiskDir, ec)) {
                        if (!entry.is_regular_file(ec)) {
                            continue;
                        }
                        const std::string ext = ToLowerAscii(entry.path().extension().string());
                        if (ext == ".dds" || ext == ".tmp") {
                            std::error_code rmEc;
                            if (std::filesystem::remove(entry.path(), rmEc)) {
                                ++purged;
                            }
                        }
                    }
                }
                cacheDirty_ = true;
                MORWHA_LOG_INFO(
                    "HybridSkinService: Upgraded skin cache manifest v{} -> v{} (purged {} legacy file(s))",
                    diskVer,
                    kCacheFormatVersion,
                    purged);
                return;
            }
            if (const auto it = j.find("entries"); it != j.end() && it->is_object()) {
                for (const auto& [k, v] : it->items()) {
                    if (v.is_string()) {
                        cacheFingerprints_.emplace(k, v.get<std::string>());
                    }
                }
            }
        } catch (...) {
        }
    }

    void HybridSkinService::SaveCacheManifest() const {
        if (!cacheDirty_) {
            return;
        }
        nlohmann::json root = nlohmann::json::object();
        root["version"] = kCacheFormatVersion;
        nlohmann::json entries = nlohmann::json::object();
        for (const auto& [k, v] : cacheFingerprints_) {
            entries[k] = v;
        }
        root["entries"] = std::move(entries);
        const std::string serialized = root.dump(2);
        const auto bytes = std::span<const std::uint8_t>(
            reinterpret_cast<const std::uint8_t*>(serialized.data()), serialized.size());
        (void)WriteBinaryFileAtomic(std::filesystem::path(kCacheManifestPath), bytes);
    }

    void HybridSkinService::InitializeAtDataLoaded(const SynthesisSettings& settings) {
        std::lock_guard lock(mutex_);
        const auto t0 = std::chrono::steady_clock::now();

        enabled_ = settings.enableGeneratedHybridSkins;
        resolutionMode_ = settings.generatedSkinResolution;
        availableStencilFiles_.clear();
        vfsMetaCache_.clear();
        generatedSlotPaths_.clear();
        archetypeTxstCache_.clear();
        hybridArmaCache_.clear();
        hybridSkinArmorCache_.clear();
        hybridFacePartCache_.clear();
        runtimeHybridSkins_.clear();
        runtimeHybridFaceParts_.clear();
        runtimeHybridFaceTxsts_.clear();
        cacheDirty_ = false;

        std::error_code ec;
        if (std::filesystem::is_directory(Core::kDefaultHybridStencilsDir, ec)) {
            for (const auto& entry : std::filesystem::directory_iterator(Core::kDefaultHybridStencilsDir, ec)) {
                if (entry.is_regular_file(ec) &&
                    ToLowerAscii(entry.path().extension().string()) == ".mrst") {
                    availableStencilFiles_.insert(ToLowerAscii(entry.path().filename().string()));
                }
            }
        }

        DiscoverBaseSkinPathsFromEngine();
        LoadCacheManifest();
        initialized_ = true;

        if (enabled_ && availableStencilFiles_.empty()) {
            MORWHA_LOG_WARN(
                "HybridSkinService: No .mrst stencils found in '{}'; install the 'M.O.R.W.H.A - Hybrid Skin Stencils' asset pack to enable Tier-2 fur/scale hybrid skins",
                Core::kDefaultHybridStencilsDir);
        }

        PrewarmStats stats{};
        if (enabled_ && !availableStencilFiles_.empty()) {
            PrewarmAllHybridSkins(stats);
        }

        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count();
        MORWHA_LOG_INFO(
            "HybridSkinService: Initialized (enabled={}, resMode={}, stencils={}, bodyUv=[humF={},humM={},khajF={},khajM={},argF={},argM={}], generatedFiles={}, cachedFiles={}, elapsed={} ms, ready=[humF={},humM={},khajF={},khajM={},argF={},argM={}])",
            enabled_,
            State::GeneratedSkinResolutionToString(resolutionMode_),
            availableStencilFiles_.size(),
            Core::BodyUvFamilyStem(basePaths_[0][1].uvFamily),
            Core::BodyUvFamilyStem(basePaths_[0][0].uvFamily),
            Core::BodyUvFamilyStem(basePaths_[1][1].uvFamily),
            Core::BodyUvFamilyStem(basePaths_[1][0].uvFamily),
            Core::BodyUvFamilyStem(basePaths_[2][1].uvFamily),
            Core::BodyUvFamilyStem(basePaths_[2][0].uvFamily),
            stats.generatedFiles,
            stats.cachedFiles,
            elapsedMs,
            basePaths_[0][1].valid,
            basePaths_[0][0].valid,
            basePaths_[1][1].valid,
            basePaths_[1][0].valid,
            basePaths_[2][1].valid,
            basePaths_[2][0].valid);
    }

    void HybridSkinService::UpdateSettings(const SynthesisSettings& settings) {
        std::lock_guard lock(mutex_);
        const bool wasEnabled = enabled_;
        const auto prevMode = resolutionMode_;
        enabled_ = settings.enableGeneratedHybridSkins;
        resolutionMode_ = settings.generatedSkinResolution;

        if (!initialized_) {
            return;
        }

        const bool modeChanged = (resolutionMode_ != prevMode);
        const bool newlyEnabled = (!wasEnabled && enabled_);
        if (modeChanged) {
            generatedSlotPaths_.clear();
            archetypeTxstCache_.clear();
            hybridArmaCache_.clear();
            hybridSkinArmorCache_.clear();
            hybridFacePartCache_.clear();
        }
        if (enabled_ && (modeChanged || newlyEnabled)) {
            const auto t0 = std::chrono::steady_clock::now();
            vfsMetaCache_.clear();
            PrewarmStats stats{};
            PrewarmAllHybridSkins(stats);
            const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t0).count();
            MORWHA_LOG_INFO(
                "HybridSkinService: Reloaded skin cache (resMode={}, generatedFiles={}, cachedFiles={}, elapsed={} ms)",
                State::GeneratedSkinResolutionToString(resolutionMode_),
                stats.generatedFiles,
                stats.cachedFiles,
                elapsedMs);
        }
    }

    bool HybridSkinService::IsCoatGroupReadyForSettings(
        Core::HybridCoatGroup group,
        Foundation::Sex sex,
        bool enableGeneratedHybridSkins) const noexcept {
        if (!initialized_ || !enableGeneratedHybridSkins) {
            return false;
        }
        const BaseSkinFamily fam = FamilyForCoatGroup(group);
        const auto sexIdx = static_cast<std::size_t>(sex);
        if (sexIdx >= 2u || !basePaths_[static_cast<std::size_t>(fam)][sexIdx].valid) {
            return false;
        }
        const auto sampleKind =
            (group == Core::HybridCoatGroup::HumanoidKhajiitFur ||
             group == Core::HybridCoatGroup::ArgonianKhajiitFur)
                ? Core::HybridStencilKind::FurTabby
                : Core::HybridStencilKind::LizardPebble;
        return !FindStencilPath(fam, sex, SkinSurfaceSlot::Face, sampleKind).empty() &&
               !FindStencilPath(fam, sex, SkinSurfaceSlot::Body, sampleKind).empty() &&
               !FindStencilPath(fam, sex, SkinSurfaceSlot::Hands, sampleKind).empty();
    }

    void HybridSkinService::PrewarmAllHybridSkins(PrewarmStats& outStats) {
        outStats = {};
        const auto catalog = Core::GetHybridCoatCatalog();
        for (const auto family : { BaseSkinFamily::Humanoid, BaseSkinFamily::Khajiit, BaseSkinFamily::Argonian }) {
            std::vector<const Core::HybridCoatDescriptor*> familyCoats;
            for (const auto& coat : catalog) {
                if (FamilyForCoatGroup(coat.group) == family) {
                    familyCoats.push_back(&coat);
                }
            }
            if (familyCoats.empty()) {
                continue;
            }
            for (const auto sex : { Foundation::Sex::Female, Foundation::Sex::Male }) {
                if (!basePaths_[static_cast<std::size_t>(family)][static_cast<std::size_t>(sex)].valid) {
                    continue;
                }
                for (const auto slot : { SkinSurfaceSlot::Face, SkinSurfaceSlot::Body, SkinSurfaceSlot::Hands }) {
                    (void)EnsureSurfaceSlotMaps(family, sex, slot, familyCoats, outStats);
                }
            }
        }

        for (const auto [tailKind, group] : {
                 std::pair{ State::BeastTailKind::Khajiit, Core::HybridCoatGroup::HumanoidKhajiitFur },
                 std::pair{ State::BeastTailKind::Argonian, Core::HybridCoatGroup::HumanoidArgonianLizard } }) {
            std::vector<const Core::HybridCoatDescriptor*> tailCoats;
            for (const auto& coat : catalog) {
                if (coat.group == group) {
                    tailCoats.push_back(&coat);
                }
            }
            for (const auto sex : { Foundation::Sex::Female, Foundation::Sex::Male }) {
                (void)EnsureTailCoatMaps(tailKind, sex, tailCoats, outStats);
            }
        }

        SaveCacheManifest();
        cacheDirty_ = false;
    }

    bool HybridSkinService::EnsureSurfaceSlotMaps(
        BaseSkinFamily family,
        Foundation::Sex sex,
        SkinSurfaceSlot slot,
        std::span<const Core::HybridCoatDescriptor* const> coats,
        PrewarmStats& ioStats) {
        if (coats.empty()) {
            return false;
        }
        const auto famIdx = static_cast<std::size_t>(family);
        const auto sexIdx = static_cast<std::size_t>(sex);
        if (famIdx >= 3u || sexIdx >= 2u || !basePaths_[famIdx][sexIdx].valid) {
            return false;
        }

        const auto& base = basePaths_[famIdx][sexIdx];
        const SurfaceSourcePaths& srcPaths =
            (slot == SkinSurfaceSlot::Face)
                ? base.face
                : ((slot == SkinSurfaceSlot::Body) ? base.body : base.hands);

        const VfsTextureMeta diffMeta = ProbeVfsTexture(srcPaths.diffuseRel);
        if (!diffMeta.exists) {
            return false;
        }
        const VfsTextureMeta msnMeta = ProbeVfsTexture(srcPaths.normalRel);
        const VfsTextureMeta specMeta = ProbeVfsTexture(srcPaths.specRel);
        const VfsTextureMeta skMeta = ProbeVfsTexture(srcPaths.subsurfRel);

        const std::uint32_t maxCap = State::MaxDimensionForSkinResolution(resolutionMode_);
        const std::uint32_t defaultStencilDim = (slot == SkinSurfaceSlot::Body) ? 2048u : 1024u;
        const auto diffDims = Core::ResolveHybridSkinDimensions(
            diffMeta.topWidth, diffMeta.topHeight, defaultStencilDim, maxCap);
        const std::uint32_t reliefFallbackDim = (std::min)(diffDims.width, defaultStencilDim);
        const auto msnDims = Core::ResolveHybridSkinDimensions(
            msnMeta.topWidth, msnMeta.topHeight, reliefFallbackDim, maxCap);
        const auto specDims = Core::ResolveHybridSkinDimensions(
            specMeta.topWidth, specMeta.topHeight, reliefFallbackDim, maxCap);
        const auto skDims = Core::ResolveHybridSkinDimensions(
            skMeta.topWidth, skMeta.topHeight, reliefFallbackDim, maxCap);

        const std::string_view famStr =
            (family == BaseSkinFamily::Humanoid)
                ? "hum"
                : ((family == BaseSkinFamily::Khajiit) ? "khaj" : "arg");
        const std::string_view sexStr = (sex == Foundation::Sex::Female) ? "f" : "m";
        const std::string_view slotStr = SlotTagFor(slot);
        const std::string slotUvStr =
            (slot == SkinSurfaceSlot::Face)
                ? std::string(slotStr)
                : std::format("{}_{}", slotStr, Core::BodyUvFamilyStem(base.uvFamily));

        struct CoatDiffTask {
            const Core::HybridCoatDescriptor* coat{ nullptr };
            std::string stem{};
            std::string relPath{};
            std::filesystem::path diskPath{};
            std::string fpHex{};
            bool cached{ false };
        };

        struct StencilKindBatch {
            Core::HybridStencilKind kind{};
            std::filesystem::path stencilFile{};
            std::uintmax_t stencilBytes{ 0 };

            std::string msnStem{}, msnRel{}, msnFp{};
            std::filesystem::path msnDisk{};
            bool msnCached{ false };

            std::string specStem{}, specRel{}, specFp{};
            std::filesystem::path specDisk{};
            bool specCached{ false };

            std::string skStem{}, skRel{}, skFp{};
            std::filesystem::path skDisk{};
            float skKeep{ 0.10f };
            bool skCached{ false };

            std::vector<CoatDiffTask> coatTasks{};
        };

        auto isFileCached = [&](const std::string& stem,
                                const std::string& fpHex,
                                const std::filesystem::path& diskPath) noexcept {
            const auto it = cacheFingerprints_.find(stem);
            if (it == cacheFingerprints_.end() || it->second != fpHex) {
                return false;
            }
            std::error_code ec;
            return std::filesystem::is_regular_file(diskPath, ec) &&
                   std::filesystem::file_size(diskPath, ec) > 148u;
        };

        auto makeMapFp = [&](const Core::SkinMapDimensions& dims,
                             std::uintmax_t stBytes,
                             std::string_view srcRel,
                             const VfsTextureMeta& meta,
                             const float* extraFloat) {
            std::uint64_t fp = 0xCBF29CE484222325ull;
            FnvMixBytes(fp, &kCacheFormatVersion, sizeof(kCacheFormatVersion));
            FnvMixBytes(fp, &dims.width, sizeof(dims.width));
            FnvMixBytes(fp, &dims.height, sizeof(dims.height));
            FnvMixBytes(fp, &stBytes, sizeof(stBytes));
            if (extraFloat) {
                FnvMixBytes(fp, extraFloat, sizeof(*extraFloat));
            }
            FnvMixString(fp, ToLowerAscii(srcRel));
            FnvMixBytes(fp, &meta.headerHash, sizeof(meta.headerHash));
            return std::format("{:016X}", fp);
        };

        std::vector<StencilKindBatch> batches;
        bool needBaseDiff = false;
        bool needBaseMsn = false;
        bool needBaseSpec = false;
        bool needBaseSk = false;

        for (const auto kind : {
                 Core::HybridStencilKind::FurTabby,
                 Core::HybridStencilKind::FurSpotted,
                 Core::HybridStencilKind::LizardPebble,
                 Core::HybridStencilKind::LizardKeeled }) {
            std::vector<const Core::HybridCoatDescriptor*> matchingCoats;
            for (const auto* c : coats) {
                if (c && c->stencilKind == kind) {
                    matchingCoats.push_back(c);
                }
            }
            if (matchingCoats.empty()) {
                continue;
            }

            const std::filesystem::path stFile = FindStencilPath(family, sex, slot, kind);
            std::error_code ec;
            const auto stSize = stFile.empty() ? 0u : std::filesystem::file_size(stFile, ec);
            if (stFile.empty() || ec || stSize < 40u) {
                continue;
            }

            StencilKindBatch batch{};
            batch.kind = kind;
            batch.stencilFile = stFile;
            batch.stencilBytes = stSize;
            const std::string_view kindStem = Core::HybridStencilKindStem(kind);

            batch.msnStem = std::format("relief_{}_{}_{}_{}_{}_msn", famStr, sexStr, slotUvStr, kindStem, msnDims.width);
            batch.msnRel = std::string(kGeneratedRelDir) + batch.msnStem + ".dds";
            batch.msnDisk = std::filesystem::path(kGeneratedDiskDir) / (batch.msnStem + ".dds");
            batch.msnFp = makeMapFp(msnDims, stSize, srcPaths.normalRel, msnMeta, nullptr);
            batch.msnCached = isFileCached(batch.msnStem, batch.msnFp, batch.msnDisk);

            batch.specStem = std::format("relief_{}_{}_{}_{}_{}_s", famStr, sexStr, slotUvStr, kindStem, specDims.width);
            batch.specRel = std::string(kGeneratedRelDir) + batch.specStem + ".dds";
            batch.specDisk = std::filesystem::path(kGeneratedDiskDir) / (batch.specStem + ".dds");
            batch.specFp = makeMapFp(specDims, stSize, srcPaths.specRel, specMeta, nullptr);
            batch.specCached = isFileCached(batch.specStem, batch.specFp, batch.specDisk);

            batch.skKeep = Core::DefaultSubsurfaceKeepForStencil(kind);
            batch.skStem = std::format("relief_{}_{}_{}_{}_{}_sk", famStr, sexStr, slotUvStr, kindStem, skDims.width);
            batch.skRel = std::string(kGeneratedRelDir) + batch.skStem + ".dds";
            batch.skDisk = std::filesystem::path(kGeneratedDiskDir) / (batch.skStem + ".dds");
            batch.skFp = makeMapFp(skDims, stSize, srcPaths.subsurfRel, skMeta, &batch.skKeep);
            batch.skCached = isFileCached(batch.skStem, batch.skFp, batch.skDisk);

            if (!batch.msnCached) needBaseMsn = true; else ++ioStats.cachedFiles;
            if (!batch.specCached) needBaseSpec = true; else ++ioStats.cachedFiles;
            if (!batch.skCached) needBaseSk = true; else ++ioStats.cachedFiles;

            for (const auto* c : matchingCoats) {
                CoatDiffTask ct{};
                ct.coat = c;
                ct.stem = std::format("{}_{}_{}_{}", c->id, sexStr, slotUvStr, diffDims.width);
                ct.relPath = std::string(kGeneratedRelDir) + ct.stem + ".dds";
                ct.diskPath = std::filesystem::path(kGeneratedDiskDir) / (ct.stem + ".dds");

                std::uint64_t fp = 0xCBF29CE484222325ull;
                FnvMixBytes(fp, &kCacheFormatVersion, sizeof(kCacheFormatVersion));
                FnvMixBytes(fp, &diffDims.width, sizeof(diffDims.width));
                FnvMixBytes(fp, &diffDims.height, sizeof(diffDims.height));
                FnvMixBytes(fp, &c->primaryRgb, sizeof(c->primaryRgb));
                FnvMixBytes(fp, &c->secondaryRgb, sizeof(c->secondaryRgb));
                FnvMixBytes(fp, &c->patternStrength, sizeof(c->patternStrength));
                FnvMixBytes(fp, &c->skinHarmony, sizeof(c->skinHarmony));
                FnvMixBytes(fp, &stSize, sizeof(stSize));
                FnvMixString(fp, ToLowerAscii(srcPaths.diffuseRel));
                FnvMixBytes(fp, &diffMeta.headerHash, sizeof(diffMeta.headerHash));
                ct.fpHex = std::format("{:016X}", fp);
                ct.cached = isFileCached(ct.stem, ct.fpHex, ct.diskPath);
                if (!ct.cached) {
                    needBaseDiff = true;
                } else {
                    ++ioStats.cachedFiles;
                }
                batch.coatTasks.push_back(std::move(ct));
            }
            batches.push_back(std::move(batch));
        }

        if (batches.empty()) {
            return false;
        }

        std::vector<std::uint8_t> baseDiff, baseMsn, baseSpec, baseSk;
        if (needBaseDiff &&
            !DecodeVfsTextureOrSolid(srcPaths.diffuseRel, diffDims.width, diffDims.height, { 176u, 146u, 132u, 255u }, baseDiff)) {
            return false;
        }
        if (needBaseMsn) {
            (void)DecodeVfsTextureOrSolid(srcPaths.normalRel, msnDims.width, msnDims.height, { 128u, 128u, 255u, 255u }, baseMsn);
        }
        if (needBaseSpec) {
            (void)DecodeVfsTextureOrSolid(srcPaths.specRel, specDims.width, specDims.height, { 42u, 42u, 42u, 255u }, baseSpec);
        }
        if (needBaseSk) {
            (void)DecodeVfsTextureOrSolid(srcPaths.subsurfRel, skDims.width, skDims.height, { 64u, 32u, 28u, 255u }, baseSk);
        }

        bool allSucceeded = true;
        std::vector<std::uint8_t> workDds;

        for (auto& batch : batches) {
            const bool anyCoatUncached = std::ranges::any_of(
                batch.coatTasks, [](const CoatDiffTask& t) { return !t.cached; });
            const bool needStencil =
                !batch.msnCached || !batch.specCached || !batch.skCached || anyCoatUncached;

            if (needStencil) {
                auto fullStencil = Core::LoadSkinStencilFile(batch.stencilFile);
                if (!fullStencil) {
                    allSucceeded = false;
                    continue;
                }
                const auto fullDims = Core::GetSkinStencilDimensions(fullStencil.get());

                Core::SkinStencilPtr halfStencil{};
                bool halfAttempted = false;
                auto pickStencilFor = [&](const Core::SkinMapDimensions& dims) -> const Core::OpaqueSkinStencil* {
                    if (fullDims.width == dims.width * 2u && fullDims.height == dims.height * 2u) {
                        if (!halfAttempted) {
                            halfAttempted = true;
                            halfStencil = Core::DownsampleSkinStencil2x2(fullStencil.get());
                        }
                        if (halfStencil) {
                            return halfStencil.get();
                        }
                    }
                    return fullStencil.get();
                };

                if (!batch.msnCached) {
                    const auto* st = pickStencilFor(msnDims);
                    if (Core::BuildHybridNormalDds(baseMsn, msnDims.width, msnDims.height, st, workDds, ParallelSliceRunner) &&
                        WriteBinaryFileAtomic(batch.msnDisk, workDds)) {
                        cacheFingerprints_[batch.msnStem] = batch.msnFp;
                        cacheDirty_ = true;
                        batch.msnCached = true;
                        ++ioStats.generatedFiles;
                    } else {
                        allSucceeded = false;
                    }
                }

                if (!batch.specCached) {
                    const auto* st = pickStencilFor(specDims);
                    if (Core::BuildHybridSpecularDds(baseSpec, specDims.width, specDims.height, st, workDds, ParallelSliceRunner) &&
                        WriteBinaryFileAtomic(batch.specDisk, workDds)) {
                        cacheFingerprints_[batch.specStem] = batch.specFp;
                        cacheDirty_ = true;
                        batch.specCached = true;
                        ++ioStats.generatedFiles;
                    } else {
                        allSucceeded = false;
                    }
                }

                if (!batch.skCached) {
                    const auto* st = pickStencilFor(skDims);
                    if (Core::BuildHybridSubsurfaceDds(baseSk, skDims.width, skDims.height, st, batch.skKeep, workDds, ParallelSliceRunner) &&
                        WriteBinaryFileAtomic(batch.skDisk, workDds)) {
                        cacheFingerprints_[batch.skStem] = batch.skFp;
                        cacheDirty_ = true;
                        batch.skCached = true;
                        ++ioStats.generatedFiles;
                    } else {
                        allSucceeded = false;
                    }
                }

                for (auto& ct : batch.coatTasks) {
                    if (ct.cached) {
                        continue;
                    }
                    const auto* st = pickStencilFor(diffDims);
                    if (Core::BuildHybridDiffuseDds(baseDiff, diffDims.width, diffDims.height, st, ct.coat->id, workDds, ParallelSliceRunner) &&
                        WriteBinaryFileAtomic(ct.diskPath, workDds)) {
                        cacheFingerprints_[ct.stem] = ct.fpHex;
                        cacheDirty_ = true;
                        ct.cached = true;
                        ++ioStats.generatedFiles;
                    } else {
                        allSucceeded = false;
                    }
                }
            }

            if (batch.msnCached && batch.specCached && batch.skCached) {
                for (const auto& ct : batch.coatTasks) {
                    if (!ct.cached) {
                        continue;
                    }
                    GeneratedPartPaths gp{};
                    gp.diffuseRel = ct.relPath;
                    gp.normalRel = batch.msnRel;
                    gp.subsurfRel = batch.skRel;
                    gp.specRel = batch.specRel;
                    if (slot == SkinSurfaceSlot::Face) {
                        gp.detailRel = base.face.detailRel;
                    }
                    generatedSlotPaths_[MakeCoatSlotCacheKey(ct.coat->id, sex, slotStr, resolutionMode_)] =
                        std::move(gp);
                }
            }
        }

        return allSucceeded;
    }

    bool HybridSkinService::EnsureTailCoatMaps(
        State::BeastTailKind tailKind,
        Foundation::Sex sex,
        std::span<const Core::HybridCoatDescriptor* const> coats,
        PrewarmStats& ioStats) {
        if (coats.empty() ||
            (tailKind != State::BeastTailKind::Khajiit && tailKind != State::BeastTailKind::Argonian)) {
            return false;
        }
        const std::size_t tIdx = (tailKind == State::BeastTailKind::Khajiit) ? 0u : 1u;
        const auto sexIdx = static_cast<std::size_t>(sex);
        if (sexIdx >= 2u) {
            return false;
        }

        const SurfaceSourcePaths& srcPaths = tailSourcePaths_[tIdx][sexIdx];
        const VfsTextureMeta diffMeta = ProbeVfsTexture(srcPaths.diffuseRel);
        if (!diffMeta.exists || srcPaths.normalRel.empty()) {
            return false;
        }

        const auto diffDims = Core::ResolveHybridSkinDimensions(
            diffMeta.topWidth,
            diffMeta.topHeight,
            1024u,
            State::MaxDimensionForSkinResolution(resolutionMode_));
        const std::string_view sexStr = (sex == Foundation::Sex::Female) ? "f" : "m";

        struct TailCoatTask {
            const Core::HybridCoatDescriptor* coat{ nullptr };
            std::string stem{};
            std::string relPath{};
            std::filesystem::path diskPath{};
            std::string fpHex{};
            bool cached{ false };
        };

        std::vector<TailCoatTask> tasks;
        tasks.reserve(coats.size());
        bool needBaseDecode = false;

        for (const auto* c : coats) {
            if (!c) {
                continue;
            }
            TailCoatTask tt{};
            tt.coat = c;
            tt.stem = std::format("{}_{}_tail_{}", c->id, sexStr, diffDims.width);
            tt.relPath = std::string(kGeneratedRelDir) + tt.stem + ".dds";
            tt.diskPath = std::filesystem::path(kGeneratedDiskDir) / (tt.stem + ".dds");

            std::uint64_t fp = 0xCBF29CE484222325ull;
            FnvMixBytes(fp, &kCacheFormatVersion, sizeof(kCacheFormatVersion));
            FnvMixBytes(fp, &diffDims.width, sizeof(diffDims.width));
            FnvMixBytes(fp, &diffDims.height, sizeof(diffDims.height));
            FnvMixBytes(fp, &c->primaryRgb, sizeof(c->primaryRgb));
            FnvMixBytes(fp, &c->secondaryRgb, sizeof(c->secondaryRgb));
            FnvMixBytes(fp, &c->patternStrength, sizeof(c->patternStrength));
            FnvMixBytes(fp, &c->skinHarmony, sizeof(c->skinHarmony));
            FnvMixString(fp, ToLowerAscii(srcPaths.diffuseRel));
            FnvMixBytes(fp, &diffMeta.headerHash, sizeof(diffMeta.headerHash));
            tt.fpHex = std::format("{:016X}", fp);

            const auto it = cacheFingerprints_.find(tt.stem);
            std::error_code ec;
            tt.cached = (it != cacheFingerprints_.end() && it->second == tt.fpHex &&
                         std::filesystem::is_regular_file(tt.diskPath, ec) &&
                         std::filesystem::file_size(tt.diskPath, ec) > 148u);
            if (!tt.cached) {
                needBaseDecode = true;
            } else {
                ++ioStats.cachedFiles;
            }
            tasks.push_back(std::move(tt));
        }

        bool allOk = true;
        if (needBaseDecode) {
            std::vector<std::uint8_t> baseTailDiff;
            if (!DecodeVfsTextureOrSolid(
                    srcPaths.diffuseRel, diffDims.width, diffDims.height, { 128u, 102u, 78u, 255u }, baseTailDiff)) {
                return false;
            }
            std::vector<std::uint8_t> workDds;
            for (auto& tt : tasks) {
                if (tt.cached) {
                    continue;
                }
                if (Core::BuildHybridTailDiffuseDds(
                        baseTailDiff, diffDims.width, diffDims.height, tt.coat->id, workDds, ParallelSliceRunner) &&
                    WriteBinaryFileAtomic(tt.diskPath, workDds)) {
                    cacheFingerprints_[tt.stem] = tt.fpHex;
                    cacheDirty_ = true;
                    tt.cached = true;
                    ++ioStats.generatedFiles;
                } else {
                    allOk = false;
                }
            }
        }

        for (const auto& tt : tasks) {
            if (!tt.cached) {
                continue;
            }
            GeneratedPartPaths gp{};
            gp.diffuseRel = tt.relPath;
            gp.normalRel = srcPaths.normalRel;
            gp.subsurfRel = srcPaths.subsurfRel;
            gp.specRel = srcPaths.specRel;
            generatedSlotPaths_[MakeCoatSlotCacheKey(tt.coat->id, sex, "tail", resolutionMode_)] =
                std::move(gp);
        }
        return allOk;
    }

    RE::BGSTextureSet* HybridSkinService::CreateRuntimeSkinTextureSet(
        std::string_view editorId,
        const GeneratedPartPaths& paths) {
        auto* txst = RE::IFormFactory::Create<RE::BGSTextureSet>();
        if (!txst) {
            return nullptr;
        }
        // DNAM = 0x0006 (kFacegenTextures | kHasModelSpaceNormalMap): mandatory on Skyrim body/hand
        // skin TXSTs so tangentless body/hand NIFs (3BA, HIMBO, vanilla) enable kModelSpaceNormals
        // instead of evaluating _msn.dds in view/tangent space.
        txst->flags.reset();
        txst->flags.set(
            RE::BGSTextureSet::Flag::kFacegenTextures,
            RE::BGSTextureSet::Flag::kHasModelSpaceNormalMap);
        txst->textures[RE::BSTextureSet::Texture::kDiffuse].textureName = paths.diffuseRel.c_str();
        txst->textures[RE::BSTextureSet::Texture::kNormal].textureName = paths.normalRel.c_str();
        txst->textures[RE::BSTextureSet::Texture::kGlowMap].textureName = paths.subsurfRel.c_str();
        if (!paths.detailRel.empty()) {
            txst->textures[RE::BSTextureSet::Texture::kHeight].textureName = paths.detailRel.c_str();
        }
        txst->textures[RE::BSTextureSet::Texture::kSpecular].textureName = paths.specRel.c_str();

        const std::string edidOwned(editorId);
        txst->SetFormEditorID(edidOwned.c_str());
        txst->InitItem();
        return txst;
    }

    const HybridSkinService::GeneratedArchetypeSet* HybridSkinService::GetOrGenerateArchetypeTextureSets(
        const Core::HybridCoatDescriptor& coat,
        Foundation::Sex sex) {
        const std::string cacheKey = MakeArchetypeCacheKey(coat.id, sex, resolutionMode_);
        if (const auto it = archetypeTxstCache_.find(cacheKey); it != archetypeTxstCache_.end()) {
            return &it->second;
        }

        const BaseSkinFamily fam = FamilyForCoatGroup(coat.group);
        const auto sexIdx = static_cast<std::size_t>(sex);
        const auto& base = basePaths_[static_cast<std::size_t>(fam)][sexIdx];
        if (!base.valid) {
            return nullptr;
        }

        auto resolveSlotPaths = [&](SkinSurfaceSlot slot, GeneratedPartPaths& outPaths) -> bool {
            const std::string key = MakeCoatSlotCacheKey(coat.id, sex, SlotTagFor(slot), resolutionMode_);
            if (const auto it = generatedSlotPaths_.find(key); it != generatedSlotPaths_.end()) {
                outPaths = it->second;
                return VfsTextureExists(outPaths.diffuseRel) && VfsTextureExists(outPaths.normalRel);
            }
            const Core::HybridCoatDescriptor* singleCoat = &coat;
            PrewarmStats stats{};
            if (!EnsureSurfaceSlotMaps(fam, sex, slot, std::span(&singleCoat, 1u), stats)) {
                return false;
            }
            SaveCacheManifest();
            cacheDirty_ = false;
            const auto it = generatedSlotPaths_.find(key);
            if (it == generatedSlotPaths_.end()) {
                return false;
            }
            outPaths = it->second;
            return VfsTextureExists(outPaths.diffuseRel) && VfsTextureExists(outPaths.normalRel);
        };

        GeneratedPartPaths facePaths{}, bodyPaths{}, handsPaths{};
        if (!resolveSlotPaths(SkinSurfaceSlot::Face, facePaths) ||
            !resolveSlotPaths(SkinSurfaceSlot::Body, bodyPaths) ||
            !resolveSlotPaths(SkinSurfaceSlot::Hands, handsPaths)) {
            MORWHA_LOG_WARN(
                "HybridSkinService: Failed resolving hybrid skin maps for '{}' (sex={})",
                coat.id,
                static_cast<unsigned>(sex));
            return nullptr;
        }

        GeneratedArchetypeSet set{};
        set.faceTxst = CreateRuntimeSkinTextureSet("MORWHA_TxFace_" + cacheKey, facePaths);
        set.bodyTxst = CreateRuntimeSkinTextureSet("MORWHA_TxBody_" + cacheKey, bodyPaths);
        set.handsTxst = CreateRuntimeSkinTextureSet("MORWHA_TxHands_" + cacheKey, handsPaths);
        if (!set.faceTxst || !set.bodyTxst || !set.handsTxst) {
            return nullptr;
        }

        if (coat.group == Core::HybridCoatGroup::HumanoidKhajiitFur ||
            coat.group == Core::HybridCoatGroup::HumanoidArgonianLizard) {
            const auto tailKind = (coat.group == Core::HybridCoatGroup::HumanoidKhajiitFur)
                ? State::BeastTailKind::Khajiit
                : State::BeastTailKind::Argonian;
            const std::string tailKey = MakeCoatSlotCacheKey(coat.id, sex, "tail", resolutionMode_);
            if (!generatedSlotPaths_.contains(tailKey)) {
                const Core::HybridCoatDescriptor* singleCoat = &coat;
                PrewarmStats stats{};
                (void)EnsureTailCoatMaps(tailKind, sex, std::span(&singleCoat, 1u), stats);
                SaveCacheManifest();
                cacheDirty_ = false;
            }
            if (const auto tIt = generatedSlotPaths_.find(tailKey);
                tIt != generatedSlotPaths_.end() && VfsTextureExists(tIt->second.diffuseRel)) {
                set.tailTxst = CreateRuntimeSkinTextureSet("MORWHA_TxTail_" + cacheKey, tIt->second);
            }
        }

        runtimeHybridFaceTxsts_.push_back(set.faceTxst);

        MORWHA_LOG_INFO(
            "HybridSkinService: Bound hybrid TXSTs '{}' (sex={}, resMode={}, faceTx={:08X}, bodyTx={:08X}, handsTx={:08X})",
            coat.id,
            static_cast<unsigned>(sex),
            State::GeneratedSkinResolutionToString(resolutionMode_),
            set.faceTxst->GetFormID(),
            set.bodyTxst->GetFormID(),
            set.handsTxst->GetFormID());

        const auto [it, _] = archetypeTxstCache_.emplace(cacheKey, set);
        return &it->second;
    }

    RE::TESObjectARMA* HybridSkinService::GetOrCreateHybridArmaClone(
        RE::TESObjectARMA* srcArma,
        Foundation::Sex sex,
        RE::BGSTextureSet* overrideTxst,
        std::string_view tag) {
        if (!srcArma || !overrideTxst) {
            return nullptr;
        }
        const std::string key = std::format(
            "{:08X}_{}_{:08X}",
            srcArma->GetFormID(),
            (sex == Foundation::Sex::Female) ? 'F' : 'M',
            overrideTxst->GetFormID());
        if (const auto it = hybridArmaCache_.find(key); it != hybridArmaCache_.end()) {
            return it->second;
        }

        auto* clone = RE::IFormFactory::Create<RE::TESObjectARMA>();
        if (!clone) {
            return nullptr;
        }
        clone->race = srcArma->race;
        clone->bipedModelData = srcArma->bipedModelData;
        clone->data = srcArma->data;
        for (const auto idx : { RE::SEXES::kMale, RE::SEXES::kFemale }) {
            CopyModelTextureSwapLocal(clone->bipedModels[idx], srcArma->bipedModels[idx]);
            CopyModelTextureSwapLocal(clone->bipedModel1stPersons[idx], srcArma->bipedModel1stPersons[idx]);
            clone->skinTextures[idx] = srcArma->skinTextures[idx];
            clone->skinTextureSwapLists[idx] = nullptr;
        }
        const auto targetSexIdx =
            (sex == Foundation::Sex::Female) ? RE::SEXES::kFemale : RE::SEXES::kMale;
        clone->skinTextures[targetSexIdx] = overrideTxst;
        clone->footstepSet = srcArma->footstepSet;
        clone->artObject = srcArma->artObject;
        for (auto* r : srcArma->additionalRaces) {
            if (r) {
                clone->additionalRaces.push_back(r);
            }
        }

        const std::string edid = std::format("MORWHA_HybARMA_{}_{}", tag, hybridArmaCache_.size() + 1u);
        clone->SetFormEditorID(edid.c_str());
        clone->InitItem();

        hybridArmaCache_.emplace(key, clone);
        return clone;
    }

    RE::BGSHeadPart* HybridSkinService::GetOrCreateHybridFaceHeadPartClone(
        RE::BGSHeadPart* baseFacePart,
        RE::BGSTextureSet* faceTxst) {
        if (!baseFacePart || !faceTxst) {
            return baseFacePart;
        }
        const std::string key =
            std::format("{:08X}_{:08X}", baseFacePart->GetFormID(), faceTxst->GetFormID());
        if (const auto it = hybridFacePartCache_.find(key); it != hybridFacePartCache_.end()) {
            return it->second;
        }

        auto* clone = RE::IFormFactory::Create<RE::BGSHeadPart>();
        if (!clone) {
            return baseFacePart;
        }
        clone->formFlags = baseFacePart->formFlags & ~RE::BGSHeadPart::RecordFlags::kNonPlayable;
        clone->SetFullName(baseFacePart->GetFullName());
        CopyModelTextureSwapLocal(*clone, *baseFacePart);
        clone->flags = baseFacePart->flags;
        clone->type = baseFacePart->type;
        clone->textureSet = faceTxst;
        clone->color = baseFacePart->color;
        clone->validRaces = baseFacePart->validRaces;
        for (std::size_t i = 0u; i < RE::BGSHeadPart::MorphIndices::kTotal; ++i) {
            clone->morphs[i] = baseFacePart->morphs[i];
        }
        for (auto* extra : baseFacePart->extraParts) {
            if (extra) {
                clone->extraParts.push_back(extra);
            }
        }

        const std::string edid = std::format("MORWHA_HybFace_{}", hybridFacePartCache_.size() + 1u);
        clone->SetFormEditorID(edid.c_str());
        clone->InitItem();

        hybridFacePartCache_.emplace(key, clone);
        runtimeHybridFaceParts_.push_back(clone);
        return clone;
    }

    HybridSkinService::ResolvedHybridForms HybridSkinService::ResolveHybridNpcForms(
        std::string_view hybridSkinId,
        RE::TESObjectARMO* baseSkin,
        RE::TESRace* resolvedRace,
        Foundation::Sex sex,
        State::BeastTailKind tailKind,
        RE::BGSHeadPart* baseFaceHeadPart) {
        ResolvedHybridForms result{};
        if (hybridSkinId.empty() || !resolvedRace) {
            return result;
        }

        std::lock_guard lock(mutex_);
        if (!initialized_ || !enabled_) {
            return result;
        }

        const auto* coat = Core::FindHybridCoat(hybridSkinId);
        if (!coat) {
            return result;
        }

        const auto* txstSet = GetOrGenerateArchetypeTextureSets(*coat, sex);
        if (!txstSet) {
            return result;
        }

        result.faceTextureSet = txstSet->faceTxst;
        if (baseFaceHeadPart) {
            result.faceHeadPartClone =
                GetOrCreateHybridFaceHeadPartClone(baseFaceHeadPart, txstSet->faceTxst);
        }

        RE::TESObjectARMO* effectiveBaseSkin = baseSkin ? baseSkin : resolvedRace->skin;
        if (!effectiveBaseSkin) {
            return result;
        }

        RE::TESObjectARMA* tailArma =
            ChimeraAccessoryCatalog::GetSingleton().GetClonedTailArma(tailKind);
        if (tailArma && !tailArma->IsValidRace(resolvedRace)) {
            tailArma = nullptr;
        }
        if (tailArma != nullptr && txstSet->tailTxst != nullptr) {
            if (auto* hybridTail = GetOrCreateHybridArmaClone(tailArma, sex, txstSet->tailTxst, "Tail")) {
                tailArma = hybridTail;
            }
        }

        const std::string armoKey = std::format(
            "{:08X}_{:08X}_{}_{}_{:08X}",
            effectiveBaseSkin->GetFormID(),
            resolvedRace->GetFormID(),
            (sex == Foundation::Sex::Female) ? 'F' : 'M',
            coat->id,
            tailArma ? tailArma->GetFormID() : 0u);
        if (const auto it = hybridSkinArmorCache_.find(armoKey); it != hybridSkinArmorCache_.end()) {
            result.skinArmor = it->second;
            return result;
        }

        auto* customSkin = RE::IFormFactory::Create<RE::TESObjectARMO>();
        if (!customSkin) {
            return result;
        }

        customSkin->formFlags =
            effectiveBaseSkin->formFlags | RE::TESObjectARMO::RecordFlags::kNonPlayable;
        customSkin->race = effectiveBaseSkin->race ? effectiveBaseSkin->race : resolvedRace;
        customSkin->armorRating = effectiveBaseSkin->armorRating;
        customSkin->bipedModelData = effectiveBaseSkin->bipedModelData;
        customSkin->bipedModelData.armorType = RE::BIPED_MODEL::ArmorType::kClothing;

        {
            std::vector<RE::BGSKeyword*> copiedKeywords;
            for (auto* kw : effectiveBaseSkin->GetKeywords()) {
                if (!kw) {
                    continue;
                }
                const char* kwEdid = kw->GetFormEditorID();
                const std::string_view kwSv = kwEdid ? std::string_view(kwEdid) : std::string_view{};
                // Never copy TNG's private pool ownership tags onto a non-TNG ARMO; TNG's GetOgSkin
                // expects TNG_CustomSkin forms to exist inside TNG's internal std::map.
                if (kwSv == "TNG_CustomSkin" || kwSv == "TNG_PreSkin") {
                    continue;
                }
                copiedKeywords.push_back(kw);
            }
            if (!copiedKeywords.empty()) {
                (void)customSkin->AddKeywords(copiedKeywords);
            }
        }

        using Slot = RE::BGSBipedObjectForm::BipedObjectSlot;
        const auto targetSexIdx =
            (sex == Foundation::Sex::Female) ? RE::SEXES::kFemale : RE::SEXES::kMale;

        std::uint32_t clonedBodyCount = 0u;
        std::uint32_t clonedHandsCount = 0u;

        auto appendHybridizedAddonsFrom = [&](RE::TESObjectARMO* donorSkin, bool onlyCoreBodyHandsFeet) {
            if (!donorSkin) {
                return;
            }
            for (auto* a : donorSkin->armorAddons) {
                if (!a) {
                    continue;
                }
                if (tailArma != nullptr && a->HasPartOf(Slot::kTail)) {
                    continue;
                }
                const bool isGenitalAddon = a->HasPartOf(Slot::kModPelvisSecondary);
                const bool isCoreBodyFeet = !isGenitalAddon && (a->HasPartOf(Slot::kBody) || a->HasPartOf(Slot::kFeet));
                const bool isCoreHands = !isGenitalAddon && !isCoreBodyFeet && a->HasPartOf(Slot::kHands);
                if (onlyCoreBodyHandsFeet && !isCoreBodyFeet && !isCoreHands) {
                    continue;
                }

                if (a->IsValidRace(resolvedRace)) {
                    const char* sexModel = a->bipedModels[targetSexIdx].GetModel();
                    const bool hasModelForSex = (sexModel != nullptr && sexModel[0] != '\0');
                    if (hasModelForSex && isCoreBodyFeet) {
                        if (auto* bodyClone = GetOrCreateHybridArmaClone(a, sex, txstSet->bodyTxst, "Body")) {
                            customSkin->armorAddons.push_back(bodyClone);
                            ++clonedBodyCount;
                            continue;
                        }
                    } else if (hasModelForSex && isCoreHands) {
                        if (auto* handsClone = GetOrCreateHybridArmaClone(a, sex, txstSet->handsTxst, "Hands")) {
                            customSkin->armorAddons.push_back(handsClone);
                            ++clonedHandsCount;
                            continue;
                        }
                    }
                }
                if (!onlyCoreBodyHandsFeet) {
                    customSkin->armorAddons.push_back(a);
                }
            }
        };

        appendHybridizedAddonsFrom(effectiveBaseSkin, /*onlyCoreBodyHandsFeet=*/false);

        // Safety fallback if effectiveBaseSkin was an unpopulated runtime shell (e.g. TNG_PreSkin):
        if (clonedBodyCount == 0u || clonedHandsCount == 0u) {
            if (auto* dataHandler = RE::TESDataHandler::GetSingleton()) {
                const BaseSkinFamily fam = FamilyForCoatGroup(coat->group);
                const std::uint32_t fallbackFormId =
                    (fam == BaseSkinFamily::Humanoid) ? kSkinNakedFormId : kSkinNakedBeastFormId;
                if (auto* vanillaSkin = dataHandler->LookupForm<RE::TESObjectARMO>(fallbackFormId, "Skyrim.esm");
                    vanillaSkin && vanillaSkin != effectiveBaseSkin) {
                    appendHybridizedAddonsFrom(vanillaSkin, /*onlyCoreBodyHandsFeet=*/true);
                }
            }
        }

        if (tailArma != nullptr) {
            customSkin->armorAddons.push_back(tailArma);
            customSkin->AddSlotToMask(tailArma->GetSlotMask().get());
        }

        // Explicitly union core biped slots across all valid ARMAs on customSkin so runtime skins
        // cloned from BOD2=0 shell records (such as TNG_Skin_Bxx) always equip Body, Hands & Feet.
        for (auto* a : customSkin->armorAddons) {
            if (!a || !a->IsValidRace(resolvedRace)) {
                continue;
            }
            for (const auto reqSlot : {
                     Slot::kBody,
                     Slot::kHands,
                     Slot::kFeet,
                     Slot::kTail,
                     Slot::kModPelvisSecondary }) {
                if (a->HasPartOf(reqSlot)) {
                    customSkin->AddSlotToMask(reqSlot);
                }
            }
        }

        const std::string edid = std::format("MORWHA_HybSkin_{}", hybridSkinArmorCache_.size() + 1u);
        customSkin->SetFormEditorID(edid.c_str());
        customSkin->InitItem();

        MORWHA_LOG_INFO(
            "HybridSkinService: Built runtime hybrid skin '{}' id={:08X} base={:08X} slots=0x{:08X} addons={} cloned=(body={},hands={})",
            edid,
            customSkin->GetFormID(),
            effectiveBaseSkin->GetFormID(),
            static_cast<std::uint32_t>(customSkin->GetSlotMask().underlying()),
            customSkin->armorAddons.size(),
            clonedBodyCount,
            clonedHandsCount);

        hybridSkinArmorCache_.emplace(armoKey, customSkin);
        runtimeHybridSkins_.push_back(customSkin);
        result.skinArmor = customSkin;
        return result;
    }

    bool HybridSkinService::EnsureSkinArmorMatchesHybridState(
        RE::TESObjectARMO* skin,
        RE::TESRace* resolvedRace,
        Foundation::Sex sex,
        std::string_view hybridSkinId,
        State::BeastTailKind tailKind) const noexcept {
        if (!skin || !resolvedRace || hybridSkinId.empty()) {
            return false;
        }
        std::lock_guard lock(mutex_);
        if (std::ranges::find(runtimeHybridSkins_, skin) != runtimeHybridSkins_.end()) {
            return true;
        }

        // Also recognize external runtime wrappers (e.g. TheNewGentleman's TNG_Skin_Bxx) that
        // copied our MORWHA_HybARMA_* & tail addons and appended a Slot 52 genital addon:
        const std::string cacheKey = MakeArchetypeCacheKey(hybridSkinId, sex, resolutionMode_);
        const auto txIt = archetypeTxstCache_.find(cacheKey);
        if (txIt == archetypeTxstCache_.end()) {
            return false;
        }
        const auto& txstSet = txIt->second;
        const auto targetSexIdx =
            (sex == Foundation::Sex::Female) ? RE::SEXES::kFemale : RE::SEXES::kMale;
        RE::TESObjectARMA* expectedTail =
            ChimeraAccessoryCatalog::GetSingleton().GetClonedTailArma(tailKind);
        if (expectedTail && !expectedTail->IsValidRace(resolvedRace)) {
            expectedTail = nullptr;
        }

        using Slot = RE::BGSBipedObjectForm::BipedObjectSlot;
        bool hasHybridBody = false;
        bool hasHybridHands = false;
        bool hasExpectedTail = (expectedTail == nullptr);

        for (auto* a : skin->armorAddons) {
            if (!a) {
                continue;
            }
            if (a == expectedTail ||
                (expectedTail != nullptr && txstSet.tailTxst != nullptr &&
                 a->HasPartOf(Slot::kTail) && a->skinTextures[targetSexIdx] == txstSet.tailTxst)) {
                hasExpectedTail = true;
            }
            if (!a->IsValidRace(resolvedRace)) {
                continue;
            }
            if (a->HasPartOf(Slot::kBody) && a->skinTextures[targetSexIdx] == txstSet.bodyTxst) {
                hasHybridBody = true;
            }
            if (a->HasPartOf(Slot::kHands) && a->skinTextures[targetSexIdx] == txstSet.handsTxst) {
                hasHybridHands = true;
            }
        }

        if (!hasHybridBody || !hasHybridHands || !hasExpectedTail) {
            return false;
        }

        // Repair BOD2 biped slot mask in place if the external wrapper started with BOD2 == 0.
        for (auto* a : skin->armorAddons) {
            if (!a || !a->IsValidRace(resolvedRace)) {
                continue;
            }
            for (const auto reqSlot : {
                     Slot::kBody,
                     Slot::kHands,
                     Slot::kFeet,
                     Slot::kTail,
                     Slot::kModPelvisSecondary }) {
                if (a->HasPartOf(reqSlot)) {
                    skin->AddSlotToMask(reqSlot);
                }
            }
        }
        return true;
    }

    bool HybridSkinService::IsRuntimeHybridSkinArmor(const RE::TESObjectARMO* skin) const noexcept {
        if (!skin) {
            return false;
        }
        std::lock_guard lock(mutex_);
        return std::ranges::find(runtimeHybridSkins_, skin) != runtimeHybridSkins_.end();
    }

    bool HybridSkinService::IsRuntimeHybridFacePart(const RE::BGSHeadPart* part) const noexcept {
        if (!part) {
            return false;
        }
        std::lock_guard lock(mutex_);
        return std::ranges::find(runtimeHybridFaceParts_, part) != runtimeHybridFaceParts_.end();
    }

    bool HybridSkinService::IsRuntimeHybridFaceTextureSet(const RE::BGSTextureSet* txst) const noexcept {
        if (!txst) {
            return false;
        }
        std::lock_guard lock(mutex_);
        return std::ranges::find(runtimeHybridFaceTxsts_, txst) != runtimeHybridFaceTxsts_.end();
    }

}  // namespace MORWHA::Engine
