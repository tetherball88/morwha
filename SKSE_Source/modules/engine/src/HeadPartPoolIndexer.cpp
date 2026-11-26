#include "morwha/engine/HeadPartPoolIndexer.h"

#include "morwha/core/MorwhaCore.h"
#include "morwha/engine/EngineLogging.h"
#include "morwha/engine/TesNpcHydrator.h"
#include "morwha/engine/VfsAssetReader.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace MORWHA::Engine {
    namespace {

        [[nodiscard]] bool StartsWithCaseInsensitive(
            std::string_view text,
            std::string_view prefix) noexcept {
            if (text.size() < prefix.size()) {
                return false;
            }
            return HeadPartPoolIndexer::EqualsCaseInsensitive(text.substr(0, prefix.size()), prefix);
        }

        [[nodiscard]] bool ContainsCustomFrameworkMarker(
            std::string_view text,
            bool isPluginFilename) noexcept {
            if (text.empty()) {
                return false;
            }
            if (HeadPartPoolIndexer::ContainsCaseInsensitive(text, "CharmersOfTheReach") ||
                HeadPartPoolIndexer::ContainsCaseInsensitive(text, "CotR") ||
                HeadPartPoolIndexer::ContainsCaseInsensitive(text, "CITRUS")) {
                return true;
            }
            if (isPluginFilename && StartsWithCaseInsensitive(text, "UBE")) {
                return true;
            }
            return HeadPartPoolIndexer::ContainsCaseInsensitive(text, "UBE_") ||
                   HeadPartPoolIndexer::ContainsCaseInsensitive(text, "UBE ") ||
                   HeadPartPoolIndexer::ContainsCaseInsensitive(text, "UBE.") ||
                   HeadPartPoolIndexer::ContainsCaseInsensitive(text, "UBE\\") ||
                   HeadPartPoolIndexer::ContainsCaseInsensitive(text, "UBE/") ||
                   HeadPartPoolIndexer::ContainsCaseInsensitive(text, "\\UBE") ||
                   HeadPartPoolIndexer::ContainsCaseInsensitive(text, "/UBE");
        }

        [[nodiscard]] bool HasValidPlayableEyeSetup(
            const RE::BGSHeadPart* hp,
            bool requireRaceTri) noexcept {
            if (!hp || hp->type.get() != RE::BGSHeadPart::HeadPartType::kEyes) {
                return false;
            }
            const std::string_view defaultTri =
                hp->morphs[RE::BGSHeadPart::MorphIndices::kDefaultMorph].model.c_str()
                    ? hp->morphs[RE::BGSHeadPart::MorphIndices::kDefaultMorph].model.c_str()
                    : "";
            const std::string_view chargenTri =
                hp->morphs[RE::BGSHeadPart::MorphIndices::kChargenMorph].model.c_str()
                    ? hp->morphs[RE::BGSHeadPart::MorphIndices::kChargenMorph].model.c_str()
                    : "";
            if (defaultTri.empty() || chargenTri.empty()) {
                return false;
            }
            if (requireRaceTri) {
                const std::string_view raceTri =
                    hp->morphs[RE::BGSHeadPart::MorphIndices::kRaceMorph].model.c_str()
                        ? hp->morphs[RE::BGSHeadPart::MorphIndices::kRaceMorph].model.c_str()
                        : "";
                if (raceTri.empty()) {
                    return false;
                }
            }
            return true;
        }

        using EyeNifGeoCache = std::unordered_map<std::string, Core::EyeMeshProbe>;

        [[nodiscard]] std::string NormalizeAssetPath(std::string_view raw);

        [[nodiscard]] Core::EyeMeshProbe InspectEyeNifGeometry(
            std::string_view rawModelPath,
            EyeNifGeoCache& cache) {
            const std::string key = NormalizeAssetPath(rawModelPath);
            if (key.empty()) {
                return {};
            }
            if (const auto it = cache.find(key); it != cache.end()) {
                return it->second;
            }

            Core::EyeMeshProbe info{};
            const std::string vfsPath = BuildMeshVfsPath(rawModelPath);
            std::vector<std::uint8_t> nifBytes;
            if (!vfsPath.empty() && ReadVfsFileBytes(vfsPath, nifBytes)) {
                info = Core::ProbeEyeMeshNif(nifBytes);
            }
            cache.emplace(key, info);
            return info;
        }

        [[nodiscard]] bool HasOnlyBilateralEyeExtraParts(
            const RE::BGSHeadPart* hp,
            EyeNifGeoCache& cache) {
            if (!hp) {
                return false;
            }
            for (const auto* extra : hp->extraParts) {
                if (!extra || extra->model.c_str() == nullptr || extra->model.c_str()[0] == '\0') {
                    return false;
                }
                const auto info = InspectEyeNifGeometry(extra->model.c_str(), cache);
                if (!Core::IsEyeMeshProbeValidAndBilateral(info)) {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool AreEyeCentersCompatible(
            const Core::EyeMeshProbe& a,
            const Core::EyeMeshProbe& b) noexcept {
            return Core::AreEyeMeshProbesCompatible(a, b);
        }

        constexpr std::uint32_t kMinSharedNpcUsageCount = 3u;

        [[nodiscard]] std::string NormalizeAssetPath(std::string_view raw) {
            std::string normalized;
            normalized.reserve(raw.size());
            for (const char ch : raw) {
                const char slash = (ch == '/') ? '\\' : ch;
                normalized.push_back(static_cast<char>(
                    std::tolower(static_cast<unsigned char>(slash))));
            }
            while (!normalized.empty() &&
                   (normalized.front() == '\\' ||
                    std::isspace(static_cast<unsigned char>(normalized.front())) != 0)) {
                normalized.erase(normalized.begin());
            }
            while (!normalized.empty() &&
                   std::isspace(static_cast<unsigned char>(normalized.back())) != 0) {
                normalized.pop_back();
            }
            constexpr std::string_view kDataPrefix = "data\\";
            if (normalized.starts_with(kDataPrefix)) {
                normalized.erase(0, kDataPrefix.size());
                while (!normalized.empty() && normalized.front() == '\\') {
                    normalized.erase(normalized.begin());
                }
            }
            constexpr std::string_view kMeshesPrefix = "meshes\\";
            if (normalized.starts_with(kMeshesPrefix)) {
                normalized.erase(0, kMeshesPrefix.size());
                while (!normalized.empty() && normalized.front() == '\\') {
                    normalized.erase(normalized.begin());
                }
            }
            return normalized;
        }

        struct GeometrySignature {
            std::string model;
            std::string raceTri;
            std::string defaultTri;
            std::string chargenTri;

            [[nodiscard]] bool operator==(const GeometrySignature& other) const noexcept = default;
        };

        [[nodiscard]] GeometrySignature MakeGeometrySignature(const RE::BGSHeadPart* hp) {
            if (!hp) {
                return {};
            }
            return GeometrySignature{
                NormalizeAssetPath(hp->model.c_str() ? hp->model.c_str() : ""),
                NormalizeAssetPath(
                    hp->morphs[RE::BGSHeadPart::MorphIndices::kRaceMorph].model.c_str()
                        ? hp->morphs[RE::BGSHeadPart::MorphIndices::kRaceMorph].model.c_str()
                        : ""),
                NormalizeAssetPath(
                    hp->morphs[RE::BGSHeadPart::MorphIndices::kDefaultMorph].model.c_str()
                        ? hp->morphs[RE::BGSHeadPart::MorphIndices::kDefaultMorph].model.c_str()
                        : ""),
                NormalizeAssetPath(
                    hp->morphs[RE::BGSHeadPart::MorphIndices::kChargenMorph].model.c_str()
                        ? hp->morphs[RE::BGSHeadPart::MorphIndices::kChargenMorph].model.c_str()
                        : "")
            };
        }

        [[nodiscard]] bool MatchesAnySignature(
            const GeometrySignature& sig,
            const std::vector<GeometrySignature>& canonical) noexcept {
            for (const auto& entry : canonical) {
                if (sig == entry) {
                    return true;
                }
            }
            return false;
        }

        void AppendUniqueSignature(
            std::vector<GeometrySignature>& dest,
            GeometrySignature&& sig) {
            if (!MatchesAnySignature(sig, dest)) {
                dest.push_back(std::move(sig));
            }
        }

        [[nodiscard]] bool IsVanillaOrDlcMaster(const RE::TESFile* file) noexcept {
            if (!file) {
                return false;
            }
            const std::string_view name = file->GetFilename();
            return HeadPartPoolIndexer::EqualsCaseInsensitive(name, "Skyrim.esm") ||
                   HeadPartPoolIndexer::EqualsCaseInsensitive(name, "Update.esm") ||
                   HeadPartPoolIndexer::EqualsCaseInsensitive(name, "Dawnguard.esm") ||
                   HeadPartPoolIndexer::EqualsCaseInsensitive(name, "HearthFires.esm") ||
                   HeadPartPoolIndexer::EqualsCaseInsensitive(name, "Dragonborn.esm");
        }

        [[nodiscard]] bool IsBaseOrHphMaster(const RE::TESFile* file) noexcept {
            if (!file) {
                return false;
            }
            if (IsVanillaOrDlcMaster(file)) {
                return true;
            }
            const std::string_view name = file->GetFilename();
            return HeadPartPoolIndexer::EqualsCaseInsensitive(name, "High Poly Head.esm") ||
                   HeadPartPoolIndexer::EqualsCaseInsensitive(name, "KLH_HighPolyHead.esp") ||
                   HeadPartPoolIndexer::EqualsCaseInsensitive(name, "High Poly Head Vampire Fix.esp");
        }

        [[nodiscard]] bool IsAuthoritativeMasterForRace(
            const RE::TESFile* file,
            const RE::TESRace* race) noexcept {
            if (!file) {
                return false;
            }
            if (IsBaseOrHphMaster(file)) {
                return true;
            }
            return race &&
                   (file == race->GetFile(0) ||
                    (race->morphRace && file == race->morphRace->GetFile(0)));
        }

        [[nodiscard]] bool IsOfficialPatchFile(const RE::TESFile* file) noexcept {
            if (!file) {
                return false;
            }
            const std::string_view name = file->GetFilename();
            return HeadPartPoolIndexer::EqualsCaseInsensitive(name, "unofficial skyrim special edition patch.esp") ||
                   HeadPartPoolIndexer::EqualsCaseInsensitive(name, "Unofficial Skyrim Modders Patch.esp");
        }

        [[nodiscard]] bool IsAuthoritativeForMorphHarvest(
            const RE::TESFile* file,
            const RE::TESRace* race) noexcept {
            return IsAuthoritativeMasterForRace(file, race) || IsOfficialPatchFile(file);
        }

        [[nodiscard]] bool IsBespokeFollowerPart(
            const RE::BGSHeadPart* hp,
            const std::unordered_set<const RE::TESFile*>& pluginsWithNonPresetNpcs,
            const std::unordered_map<const RE::BGSHeadPart*, std::uint32_t>& npcUsageCount) {
            if (!hp) {
                return false;
            }
            const auto* originFile = hp->GetFile(0);
            if (!originFile || IsBaseOrHphMaster(originFile)) {
                return false;
            }
            if (!pluginsWithNonPresetNpcs.contains(originFile)) {
                return false;
            }
            const auto it = npcUsageCount.find(hp);
            const std::uint32_t usedBy = (it != npcUsageCount.end()) ? it->second : 0u;
            return usedBy < kMinSharedNpcUsageCount;
        }

        [[nodiscard]] bool PassesCanonicalGeometryGate(
            const RE::BGSHeadPart* hp,
            const RE::TESRace* race,
            const std::vector<GeometrySignature>& canonicalEyeSignatures,
            const std::vector<GeometrySignature>& canonicalBrowSignatures,
            EyeNifGeoCache& eyeGeoCache) {
            if (!hp) {
                return false;
            }
            if (IsAuthoritativeMasterForRace(hp->GetFile(0), race)) {
                return true;
            }
            const auto type = hp->type.get();
            if (type == RE::BGSHeadPart::HeadPartType::kEyes) {
                if (!HasValidPlayableEyeSetup(hp, /*requireRaceTri=*/false) ||
                    !HasOnlyBilateralEyeExtraParts(hp, eyeGeoCache)) {
                    return false;
                }
                if (canonicalEyeSignatures.empty()) {
                    return true;
                }
                const auto sig = MakeGeometrySignature(hp);
                if (MatchesAnySignature(sig, canonicalEyeSignatures)) {
                    return true;
                }
                const auto hpGeo = InspectEyeNifGeometry(
                    hp->model.c_str() ? hp->model.c_str() : "", eyeGeoCache);
                for (const auto& canon : canonicalEyeSignatures) {
                    if (sig.defaultTri != canon.defaultTri || sig.chargenTri != canon.chargenTri) {
                        continue;
                    }
                    if (!sig.raceTri.empty() && sig.raceTri != canon.raceTri) {
                        continue;
                    }
                    const auto canonGeo = InspectEyeNifGeometry(canon.model, eyeGeoCache);
                    if (AreEyeCentersCompatible(hpGeo, canonGeo)) {
                        return true;
                    }
                }
                return false;
            }
            if (type == RE::BGSHeadPart::HeadPartType::kEyebrows) {
                return canonicalBrowSignatures.empty() ||
                       MatchesAnySignature(MakeGeometrySignature(hp), canonicalBrowSignatures);
            }
            return true;
        }

        [[nodiscard]] bool MatchesSexFlag(
            const RE::BGSHeadPart* hp,
            Foundation::Sex sex) noexcept {
            if (!hp) {
                return false;
            }
            if (hp->flags.any(RE::BGSHeadPart::Flag::kMale, RE::BGSHeadPart::Flag::kFemale)) {
                const bool wantFemale = (sex == Foundation::Sex::Female);
                if (wantFemale ? !hp->flags.all(RE::BGSHeadPart::Flag::kFemale)
                               : !hp->flags.all(RE::BGSHeadPart::Flag::kMale)) {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool ContainsHphPathOrEdidMarker(std::string_view text, bool isEdid) noexcept {
            if (text.empty()) {
                return false;
            }
            if (isEdid) {
                return HeadPartPoolIndexer::ContainsCaseInsensitive(text, "00KLH_") ||
                       HeadPartPoolIndexer::ContainsCaseInsensitive(text, "KLH_") ||
                       HeadPartPoolIndexer::ContainsCaseInsensitive(text, "_HPH") ||
                       HeadPartPoolIndexer::ContainsCaseInsensitive(text, "HPH_");
            }
            return HeadPartPoolIndexer::ContainsCaseInsensitive(text, "High Poly Head") ||
                   HeadPartPoolIndexer::ContainsCaseInsensitive(text, "KL\\High Poly Head") ||
                   HeadPartPoolIndexer::ContainsCaseInsensitive(text, "KL/High Poly Head");
        }

        [[nodiscard]] const RE::TESRace::FaceRelatedData* ResolveRaceFaceData(
            const RE::TESRace* race,
            Foundation::Sex sex) noexcept {
            if (!race) {
                return nullptr;
            }
            const auto idx = static_cast<std::size_t>(sex);
            if (race->faceRelatedData[idx]) {
                return race->faceRelatedData[idx];
            }
            if (race->morphRace && race->morphRace->faceRelatedData[idx]) {
                return race->morphRace->faceRelatedData[idx];
            }
            return nullptr;
        }

        [[nodiscard]] bool MatchesPartSexAndRace(
            const RE::BGSHeadPart* hp,
            const RE::TESRace* race,
            Foundation::Sex sex) noexcept {
            if (!hp || !race || !MatchesSexFlag(hp, sex) || !hp->validRaces) {
                return false;
            }
            return hp->validRaces->HasForm(race) ||
                   (race->morphRace && hp->validRaces->HasForm(race->morphRace));
        }

        [[nodiscard]] bool HasValidFaceChargenTri(const RE::BGSHeadPart* hp) noexcept {
            if (!hp || hp->type.get() != RE::BGSHeadPart::HeadPartType::kFace) {
                return true;
            }
            const char* tri = hp->morphs[RE::BGSHeadPart::MorphIndices::kChargenMorph].model.c_str();
            return tri != nullptr && tri[0] != '\0';
        }

        [[nodiscard]] bool IsUndeadRace(const RE::TESRace* race) noexcept {
            constexpr RE::FormID kActorTypeUndeadKeywordFormId = 0x013796u;
            return race != nullptr && race->HasKeywordID(kActorTypeUndeadKeywordFormId);
        }

        [[nodiscard]] std::string BuildCanonicalHphFaceEdid(
            const RE::TESRace* race,
            Foundation::Sex sex) {
            if (!race) {
                return {};
            }
            const std::string_view rawEdid = race->GetFormEditorID() ? race->GetFormEditorID() : "";
            const std::string norm = Core::NormalizeRaceKey(rawEdid);

            std::string_view token;
            if (norm == "nord") {
                token = "Nord";
            } else if (norm == "imperial") {
                token = "Imperial";
            } else if (norm == "breton") {
                token = "Breton";
            } else if (norm == "redguard") {
                token = "Redguard";
            } else if (norm == "highelf") {
                token = "HighElf";
            } else if (norm == "darkelf") {
                token = "DarkElf";
            } else if (norm == "woodelf") {
                token = "WoodElf";
            } else if (norm == "orc") {
                token = "Orc";
            } else if (norm == "argonian") {
                token = "Argonian";
            } else if (norm == "khajiit") {
                token = "Khajiit";
            } else {
                return {};
            }

            std::string edid = "00KLH_";
            edid += (sex == Foundation::Sex::Female) ? "FemaleHead" : "MaleHead";
            edid += token;
            if (IsUndeadRace(race)) {
                edid += "Vampire";
            }
            return edid;
        }

        void AppendUniquePart(std::vector<RE::BGSHeadPart*>& vec, RE::BGSHeadPart* part) {
            if (!part) {
                return;
            }
            for (const auto* existing : vec) {
                if (existing == part) {
                    return;
                }
            }
            vec.push_back(part);
        }

        void AddToPoolSlot(RaceSexHeadPartPool& pool, RE::BGSHeadPart* part) {
            switch (part->type.get()) {
            case RE::BGSHeadPart::HeadPartType::kFace:
                AppendUniquePart(pool.faces, part);
                break;
            case RE::BGSHeadPart::HeadPartType::kMisc:
                AppendUniquePart(pool.mouths, part);
                break;
            case RE::BGSHeadPart::HeadPartType::kEyes:
                AppendUniquePart(pool.eyes, part);
                break;
            case RE::BGSHeadPart::HeadPartType::kHair:
                AppendUniquePart(pool.hairs, part);
                break;
            case RE::BGSHeadPart::HeadPartType::kEyebrows:
                AppendUniquePart(pool.eyebrows, part);
                break;
            case RE::BGSHeadPart::HeadPartType::kFacialHair:
                AppendUniquePart(pool.facialHairs, part);
                break;
            case RE::BGSHeadPart::HeadPartType::kScar:
                AppendUniquePart(pool.scars, part);
                break;
            default:
                break;
            }
        }

        void CollectCanonicalSignatures(
            Foundation::TopologyPool topology,
            const RE::TESRace* race,
            const RE::TESRace::FaceRelatedData* faceRelated,
            Foundation::Sex sex,
            bool smpLoaded,
            const std::vector<RE::BGSHeadPart*>& allCandidates,
            std::vector<GeometrySignature>& outEyeSignatures,
            std::vector<GeometrySignature>& outBrowSignatures) {
            auto recordIfAuthoritative = [&](const RE::BGSHeadPart* hp) {
                if (!hp) {
                    return;
                }
                const auto type = hp->type.get();
                if (type != RE::BGSHeadPart::HeadPartType::kEyes &&
                    type != RE::BGSHeadPart::HeadPartType::kEyebrows) {
                    return;
                }
                if (!IsAuthoritativeMasterForRace(hp->GetFile(0), race)) {
                    return;
                }
                if (!HeadPartPoolIndexer::IsPartCompatibleWithTopology(hp, topology, smpLoaded) ||
                    !HasValidFaceChargenTri(hp)) {
                    return;
                }
                auto sig = MakeGeometrySignature(hp);
                if (type == RE::BGSHeadPart::HeadPartType::kEyes) {
                    AppendUniqueSignature(outEyeSignatures, std::move(sig));
                } else {
                    AppendUniqueSignature(outBrowSignatures, std::move(sig));
                }
            };

            for (const auto* hp : allCandidates) {
                if (!hp || !MatchesPartSexAndRace(hp, race, sex) ||
                    !hp->flags.all(RE::BGSHeadPart::Flag::kPlayable) ||
                    (hp->GetFormFlags() & RE::BGSHeadPart::RecordFlags::kNonPlayable) != 0) {
                    continue;
                }
                recordIfAuthoritative(hp);
            }

            if (faceRelated && faceRelated->headParts) {
                for (const auto* hp : *faceRelated->headParts) {
                    if (!hp || hp->flags.all(RE::BGSHeadPart::Flag::kIsExtraPart) ||
                        (hp->GetFormFlags() & RE::BGSHeadPart::RecordFlags::kNonPlayable) != 0 ||
                        !MatchesSexFlag(hp, sex)) {
                        continue;
                    }
                    recordIfAuthoritative(hp);
                }
            }
        }

        [[nodiscard]] bool IsCrownBaldHairMesh(
            const RE::BGSHeadPart* hp,
            std::unordered_map<std::string, bool>& cache,
            std::size_t& filteredCounter) {
            if (!hp || hp->type.get() != RE::BGSHeadPart::HeadPartType::kHair ||
                hp->model.c_str() == nullptr || hp->model.c_str()[0] == '\0') {
                return false;
            }
            const std::string cacheKey = NormalizeAssetPath(hp->model.c_str());
            if (cacheKey.empty()) {
                return false;
            }
            if (const auto it = cache.find(cacheKey); it != cache.end()) {
                return it->second;
            }

            bool isBald = false;
            const std::string vfsPath = BuildMeshVfsPath(hp->model.c_str());
            std::vector<std::uint8_t> nifBytes;
            if (!vfsPath.empty() && ReadVfsFileBytes(vfsPath, nifBytes)) {
                isBald = Core::IsHairMeshCrownSparse(nifBytes);
            }
            if (isBald) {
                ++filteredCounter;
            }
            cache.emplace(cacheKey, isBald);
            return isBald;
        }

        void PopulateRaceSexPool(
            RaceSexHeadPartPool& pool,
            Foundation::TopologyPool topology,
            const RE::TESRace* race,
            const RE::TESRace::FaceRelatedData* faceRelated,
            Foundation::Sex sex,
            bool smpLoaded,
            const std::vector<RE::BGSHeadPart*>& allCandidates,
            const std::unordered_set<const RE::TESFile*>& pluginsWithNonPresetNpcs,
            const std::unordered_map<const RE::BGSHeadPart*, std::uint32_t>& npcUsageCount,
            std::unordered_map<std::string, bool>& crownBaldHairCache,
            std::size_t& crownBaldCount,
            EyeNifGeoCache& eyeGeoCache) {
            std::vector<GeometrySignature> canonicalEyeSignatures;
            std::vector<GeometrySignature> canonicalBrowSignatures;
            CollectCanonicalSignatures(
                topology,
                race,
                faceRelated,
                sex,
                smpLoaded,
                allCandidates,
                canonicalEyeSignatures,
                canonicalBrowSignatures);

            const std::string canonicalHphFaceEdid =
                (topology == Foundation::TopologyPool::HighPolyHead)
                    ? BuildCanonicalHphFaceEdid(race, sex)
                    : std::string{};
            const auto sexIdx = static_cast<std::size_t>(sex);
            const RE::TESRace::FaceRelatedData* ownFaceRelated =
                race ? race->faceRelatedData[sexIdx] : nullptr;
            const RE::TESFile* raceOriginFile = race ? race->GetFile(0) : nullptr;
            auto isListedInOwnRaceHeadParts = [&](const RE::BGSHeadPart* hp) noexcept {
                if (!hp || !ownFaceRelated || !ownFaceRelated->headParts) {
                    return false;
                }
                for (const auto* entry : *ownFaceRelated->headParts) {
                    if (entry == hp) {
                        return true;
                    }
                }
                return false;
            };
            auto passesHphFaceFilter = [&](const RE::BGSHeadPart* hp) noexcept {
                if (topology != Foundation::TopologyPool::HighPolyHead ||
                    hp->type.get() != RE::BGSHeadPart::HeadPartType::kFace) {
                    return true;
                }
                if (!canonicalHphFaceEdid.empty()) {
                    const std::string_view faceEdid = hp->GetFormEditorID() ? hp->GetFormEditorID() : "";
                    return HeadPartPoolIndexer::EqualsCaseInsensitive(faceEdid, canonicalHphFaceEdid);
                }
                // Custom races often append themselves to vanilla Skyrim.esm FormLists
                // (e.g. HeadPartsNordandVampire) to share hairs/brows. Require both direct race
                // membership AND a custom-race ownership link so base HPH faces (00KLH_*HeadNord)
                // never leak onto custom races via shared vanilla FormLists.
                const bool directlyAllowsRace =
                    (hp->validRaces != nullptr && hp->validRaces->HasForm(race)) ||
                    isListedInOwnRaceHeadParts(hp);
                if (!directlyAllowsRace) {
                    return false;
                }
                if (raceOriginFile == nullptr || IsVanillaOrDlcMaster(raceOriginFile)) {
                    return true;
                }
                const bool fromCustomPlugin =
                    hp->GetFile(0) != nullptr && !IsBaseOrHphMaster(hp->GetFile(0));
                const bool hasCustomValidRacesList =
                    hp->validRaces != nullptr &&
                    hp->validRaces->GetFile(0) != nullptr &&
                    !IsBaseOrHphMaster(hp->validRaces->GetFile(0));
                const bool hasRaceOwnedTextureSet =
                    hp->textureSet != nullptr && hp->textureSet->GetFile(0) == raceOriginFile;
                return fromCustomPlugin || hasCustomValidRacesList || hasRaceOwnedTextureSet ||
                       isListedInOwnRaceHeadParts(hp);
            };

            std::vector<RE::BGSHeadPart*> baldFallbackHairs;
            for (auto* hp : allCandidates) {
                if (!hp) {
                    continue;
                }
                const auto type = hp->type.get();
                if (type == RE::BGSHeadPart::HeadPartType::kMisc &&
                    !IsAuthoritativeMasterForRace(hp->GetFile(0), race)) {
                    continue;
                }
                if (!HeadPartPoolIndexer::IsPartCompatibleWithTopology(hp, topology, smpLoaded)) {
                    continue;
                }
                if (!MatchesPartSexAndRace(hp, race, sex)) {
                    continue;
                }
                if (!hp->flags.all(RE::BGSHeadPart::Flag::kPlayable) ||
                    (hp->GetFormFlags() & RE::BGSHeadPart::RecordFlags::kNonPlayable) != 0) {
                    continue;
                }
                if (!HasValidFaceChargenTri(hp) || !passesHphFaceFilter(hp)) {
                    continue;
                }
                if (!PassesCanonicalGeometryGate(
                        hp, race, canonicalEyeSignatures, canonicalBrowSignatures, eyeGeoCache)) {
                    continue;
                }
                if (type == RE::BGSHeadPart::HeadPartType::kHair &&
                    IsCrownBaldHairMesh(hp, crownBaldHairCache, crownBaldCount)) {
                    baldFallbackHairs.push_back(hp);
                    continue;
                }
                AddToPoolSlot(pool, hp);
            }

            if (faceRelated && faceRelated->headParts) {
                for (auto* hp : *faceRelated->headParts) {
                    if (!hp) {
                        continue;
                    }
                    const auto type = hp->type.get();
                    if (type == RE::BGSHeadPart::HeadPartType::kMisc &&
                        !IsAuthoritativeMasterForRace(hp->GetFile(0), race)) {
                        continue;
                    }
                    if (!HeadPartPoolIndexer::IsPartCompatibleWithTopology(hp, topology, smpLoaded)) {
                        continue;
                    }
                    if (IsBespokeFollowerPart(hp, pluginsWithNonPresetNpcs, npcUsageCount)) {
                        continue;
                    }
                    if (!MatchesSexFlag(hp, sex)) {
                        continue;
                    }
                    if ((hp->GetFormFlags() & RE::BGSHeadPart::RecordFlags::kNonPlayable) != 0) {
                        continue;
                    }
                    if (type != RE::BGSHeadPart::HeadPartType::kFace &&
                        type != RE::BGSHeadPart::HeadPartType::kMisc &&
                        !hp->flags.all(RE::BGSHeadPart::Flag::kPlayable)) {
                        continue;
                    }
                    if (!HasValidFaceChargenTri(hp) || !passesHphFaceFilter(hp)) {
                        continue;
                    }
                    if (!PassesCanonicalGeometryGate(
                            hp, race, canonicalEyeSignatures, canonicalBrowSignatures, eyeGeoCache)) {
                        continue;
                    }
                    if (type == RE::BGSHeadPart::HeadPartType::kHair &&
                        IsCrownBaldHairMesh(hp, crownBaldHairCache, crownBaldCount)) {
                        baldFallbackHairs.push_back(hp);
                        continue;
                    }
                    AddToPoolSlot(pool, hp);
                }
            }

            if (pool.hairs.empty() && !baldFallbackHairs.empty()) {
                for (auto* hp : baldFallbackHairs) {
                    AddToPoolSlot(pool, hp);
                }
            }

            const RE::SEX reSex =
                (sex == Foundation::Sex::Female) ? RE::SEX::kFemale : RE::SEX::kMale;
            if (topology == Foundation::TopologyPool::HighPolyHead) {
                if (!canonicalHphFaceEdid.empty()) {
                    for (auto* face : pool.faces) {
                        const std::string_view faceEdid =
                            face->GetFormEditorID() ? face->GetFormEditorID() : "";
                        if (HeadPartPoolIndexer::EqualsCaseInsensitive(faceEdid, canonicalHphFaceEdid)) {
                            pool.primaryFace = face;
                            break;
                        }
                    }
                } else if (raceOriginFile != nullptr) {
                    for (auto* face : pool.faces) {
                        const bool matchesRaceAssets =
                            (face->GetFile(0) == raceOriginFile) ||
                            (face->textureSet && face->textureSet->GetFile(0) == raceOriginFile) ||
                            (face->validRaces && face->validRaces->GetFile(0) == raceOriginFile);
                        if (matchesRaceAssets) {
                            pool.primaryFace = face;
                            break;
                        }
                    }
                }
                if (!pool.primaryFace && !pool.faces.empty()) {
                    pool.primaryFace = pool.faces.front();
                }
            } else {
                auto tryRaceFace = [&](const RE::TESRace* r) -> RE::BGSHeadPart* {
                    if (!r) {
                        return nullptr;
                    }
                    auto* part = const_cast<RE::TESRace*>(r)->GetHeadPartByType(
                        RE::BGSHeadPart::HeadPartType::kFace, reSex);
                    if (part &&
                        HeadPartPoolIndexer::IsPartCompatibleWithTopology(part, topology, smpLoaded) &&
                        HasValidFaceChargenTri(part)) {
                        return part;
                    }
                    return nullptr;
                };
                pool.primaryFace = tryRaceFace(race);
                if (!pool.primaryFace && race->morphRace) {
                    pool.primaryFace = tryRaceFace(race->morphRace);
                }
                if (!pool.primaryFace && !pool.faces.empty()) {
                    pool.primaryFace = pool.faces.front();
                }
                if (pool.primaryFace) {
                    AppendUniquePart(pool.faces, pool.primaryFace);
                }
            }

            auto tryRaceMouth = [&](const RE::TESRace* r) -> RE::BGSHeadPart* {
                if (!r) {
                    return nullptr;
                }
                auto* part = const_cast<RE::TESRace*>(r)->GetHeadPartByType(
                    RE::BGSHeadPart::HeadPartType::kMisc, reSex);
                if (part &&
                    IsAuthoritativeMasterForRace(part->GetFile(0), race) &&
                    HeadPartPoolIndexer::IsPartCompatibleWithTopology(part, topology, smpLoaded)) {
                    return part;
                }
                return nullptr;
            };
            RE::BGSHeadPart* raceNativeMouth = tryRaceMouth(race);
            const bool preserveRaceNativeMouth =
                (raceNativeMouth != nullptr) &&
                (!raceNativeMouth->extraParts.empty() ||
                 (raceNativeMouth->GetFile(0) == race->GetFile(0) &&
                  raceNativeMouth->textureSet != nullptr));
            if (preserveRaceNativeMouth) {
                pool.primaryMouth = raceNativeMouth;
            } else if (topology == Foundation::TopologyPool::HighPolyHead) {
                for (auto* mouth : pool.mouths) {
                    if (HeadPartPoolIndexer::ClassifyHeadPart(mouth) ==
                        Foundation::TopologyPool::HighPolyHead) {
                        pool.primaryMouth = mouth;
                        break;
                    }
                }
            }
            if (!pool.primaryMouth) {
                pool.primaryMouth = raceNativeMouth;
            }
            if (!pool.primaryMouth && race->morphRace) {
                pool.primaryMouth = tryRaceMouth(race->morphRace);
            }
            if (!pool.primaryMouth && !pool.mouths.empty()) {
                pool.primaryMouth = pool.mouths.front();
            }
            if (pool.primaryMouth) {
                AppendUniquePart(pool.mouths, pool.primaryMouth);
            }
        }

    }  // namespace

    HeadPartPoolIndexer& HeadPartPoolIndexer::GetSingleton() noexcept {
        static HeadPartPoolIndexer instance;
        return instance;
    }

    bool HeadPartPoolIndexer::ContainsCaseInsensitive(
        std::string_view haystack,
        std::string_view needle) noexcept {
        if (needle.empty() || haystack.size() < needle.size()) {
            return false;
        }
        for (std::size_t i = 0; i <= haystack.size() - needle.size(); ++i) {
            bool match = true;
            for (std::size_t j = 0; j < needle.size(); ++j) {
                const auto a = static_cast<unsigned char>(haystack[i + j]);
                const auto b = static_cast<unsigned char>(needle[j]);
                if (std::tolower(a) != std::tolower(b)) {
                    match = false;
                    break;
                }
            }
            if (match) {
                return true;
            }
        }
        return false;
    }

    bool HeadPartPoolIndexer::EqualsCaseInsensitive(
        std::string_view a,
        std::string_view b) noexcept {
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

    Foundation::TopologyPool HeadPartPoolIndexer::ClassifyHeadPart(
        const RE::BGSHeadPart* hp) noexcept {
        if (!hp) {
            return Foundation::TopologyPool::Vanilla;
        }

        const auto* file = hp->GetFile(0);
        const std::string_view filename = file ? file->GetFilename() : "";
        const std::string_view edid = hp->GetFormEditorID() ? hp->GetFormEditorID() : "";
        const std::string_view modelPath = hp->model.c_str() ? hp->model.c_str() : "";

        if (ContainsCustomFrameworkMarker(filename, true) ||
            ContainsCustomFrameworkMarker(edid, false) ||
            ContainsCustomFrameworkMarker(modelPath, false)) {
            return Foundation::TopologyPool::CustomFallback;
        }
        for (const auto& morphTri : hp->morphs) {
            const std::string_view triPath = morphTri.model.c_str() ? morphTri.model.c_str() : "";
            if (ContainsCustomFrameworkMarker(triPath, false)) {
                return Foundation::TopologyPool::CustomFallback;
            }
        }

        if (!filename.empty() &&
            (EqualsCaseInsensitive(filename, "High Poly Head.esm") ||
             EqualsCaseInsensitive(filename, "KLH_HighPolyHead.esp") ||
             EqualsCaseInsensitive(filename, "High Poly Head Vampire Fix.esp") ||
             ContainsCaseInsensitive(filename, "High Poly Head"))) {
            return Foundation::TopologyPool::HighPolyHead;
        }
        if (ContainsHphPathOrEdidMarker(edid, true) ||
            ContainsHphPathOrEdidMarker(modelPath, false)) {
            return Foundation::TopologyPool::HighPolyHead;
        }
        for (const auto& morphTri : hp->morphs) {
            const std::string_view triPath = morphTri.model.c_str() ? morphTri.model.c_str() : "";
            if (ContainsHphPathOrEdidMarker(triPath, false)) {
                return Foundation::TopologyPool::HighPolyHead;
            }
        }

        return Foundation::TopologyPool::Vanilla;
    }

    bool HeadPartPoolIndexer::IsSmpHairPart(const RE::BGSHeadPart* hp) noexcept {
        if (!hp || hp->type.get() != RE::BGSHeadPart::HeadPartType::kHair) {
            return false;
        }
        const std::string_view edid = hp->GetFormEditorID() ? hp->GetFormEditorID() : "";
        const std::string_view model = hp->model.c_str() ? hp->model.c_str() : "";
        if (ContainsCaseInsensitive(edid, "SMP") || ContainsCaseInsensitive(edid, "HDT") ||
            ContainsCaseInsensitive(model, "SMP") || ContainsCaseInsensitive(model, "HDT")) {
            return true;
        }
        for (const auto* extra : hp->extraParts) {
            if (!extra) {
                continue;
            }
            const std::string_view extraEdid = extra->GetFormEditorID() ? extra->GetFormEditorID() : "";
            const std::string_view extraModel = extra->model.c_str() ? extra->model.c_str() : "";
            if (ContainsCaseInsensitive(extraEdid, "SMP") || ContainsCaseInsensitive(extraEdid, "HDT") ||
                ContainsCaseInsensitive(extraModel, "SMP") || ContainsCaseInsensitive(extraModel, "HDT")) {
                return true;
            }
        }
        return false;
    }

    bool HeadPartPoolIndexer::IsPartCompatibleWithTopology(
        const RE::BGSHeadPart* hp,
        Foundation::TopologyPool topology,
        bool smpLoaded) noexcept {
        if (!hp || hp->flags.all(RE::BGSHeadPart::Flag::kIsExtraPart)) {
            return false;
        }
        if ((hp->GetFormFlags() & RE::BGSHeadPart::RecordFlags::kNonPlayable) != 0) {
            return false;
        }
        if (!smpLoaded && IsSmpHairPart(hp)) {
            return false;
        }

        const auto cls = ClassifyHeadPart(hp);
        if (cls == Foundation::TopologyPool::CustomFallback) {
            return false;
        }

        const auto type = hp->type.get();
        if (type == RE::BGSHeadPart::HeadPartType::kEyes &&
            !HasValidPlayableEyeSetup(hp, /*requireRaceTri=*/false)) {
            return false;
        }
        if (type == RE::BGSHeadPart::HeadPartType::kHair ||
            type == RE::BGSHeadPart::HeadPartType::kEyes ||
            type == RE::BGSHeadPart::HeadPartType::kMisc) {
            if (topology == Foundation::TopologyPool::HighPolyHead) {
                return true;
            }
            return cls == Foundation::TopologyPool::Vanilla;
        }

        return cls == topology;
    }

    void HeadPartPoolIndexer::BuildIndex(bool smpLoaded) {
        pools_.clear();
        nativeMorphPresets_.clear();
        humanoidChargenTris_.clear();
        highPolyHeadAvailable_ = false;
        built_ = false;

        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            MORWHA_LOG_WARN("HeadPartPoolIndexer::BuildIndex: TESDataHandler unavailable");
            return;
        }

        // Pass 1: TESNPC ownership graph scan (separates pure cosmetic packs from follower/overhaul ESPs)
        // and native racial morph preset harvesting immune to RaceMenu skee64.dll bAllowAllMorphs=1.
        std::unordered_set<const RE::TESFile*> pluginsWithNonPresetNpcs;
        std::unordered_map<const RE::BGSHeadPart*, std::uint32_t> npcUsageCount;
        for (auto* npc : dataHandler->GetFormArray<RE::TESNPC>()) {
            if (!npc) {
                continue;
            }
            const auto* defFile = npc->GetFile(0);
            if (defFile && HeadPartPoolIndexer::EqualsCaseInsensitive(defFile->GetFilename(), "DPF.esp")) {
                continue;
            }
            const auto* overrideFile = npc->GetFile();
            if (overrideFile && HeadPartPoolIndexer::EqualsCaseInsensitive(overrideFile->GetFilename(), "DPF.esp")) {
                continue;
            }

            if (!npc->IsDynamicForm() && npc->race && npc->faceData && defFile &&
                IsAuthoritativeForMorphHarvest(defFile, npc->race) &&
                (!overrideFile || IsAuthoritativeForMorphHarvest(overrideFile, npc->race))) {
                const auto npcSex =
                    (npc->GetSex() == RE::SEX::kFemale) ? Foundation::Sex::Female : Foundation::Sex::Male;
                auto& slots = nativeMorphPresets_[MakeRaceSexKey(npc->race, npcSex)];
                for (const std::size_t slot : { std::size_t{ 0 }, std::size_t{ 2 }, std::size_t{ 3 } }) {
                    const std::int32_t v = npc->faceData->parts[slot];
                    if (v >= 0 && v < 128) {
                        slots[slot].push_back(v);
                    }
                }
            }

            if (npc->IsPreset()) {
                continue;
            }
            if (defFile) {
                pluginsWithNonPresetNpcs.insert(defFile);
            }
            if (overrideFile) {
                pluginsWithNonPresetNpcs.insert(overrideFile);
            }
            if (npc->headParts && npc->numHeadParts > 0) {
                for (std::int8_t i = 0; i < npc->numHeadParts; ++i) {
                    if (const auto* part = npc->headParts[i]) {
                        ++npcUsageCount[part];
                    }
                }
            }
        }

        std::vector<RE::BGSHeadPart*> candidates;
        const auto& allHeadParts = dataHandler->GetFormArray<RE::BGSHeadPart>();
        candidates.reserve(allHeadParts.size());

        bool anyHphParts = false;
        std::size_t quarantinedCount = 0;
        std::size_t bespokeSkippedCount = 0;
        for (auto* hp : allHeadParts) {
            if (!hp || hp->flags.all(RE::BGSHeadPart::Flag::kIsExtraPart)) {
                continue;
            }
            if ((hp->GetFormFlags() & RE::BGSHeadPart::RecordFlags::kNonPlayable) != 0) {
                continue;
            }
            if (!smpLoaded && IsSmpHairPart(hp)) {
                continue;
            }
            const auto cls = ClassifyHeadPart(hp);
            if (cls == Foundation::TopologyPool::CustomFallback) {
                ++quarantinedCount;
                continue;
            }
            if (IsBespokeFollowerPart(hp, pluginsWithNonPresetNpcs, npcUsageCount)) {
                ++bespokeSkippedCount;
                continue;
            }
            if (cls == Foundation::TopologyPool::HighPolyHead) {
                anyHphParts = true;
            }
            candidates.push_back(hp);
        }

        highPolyHeadAvailable_ =
            (dataHandler->LookupModByName("High Poly Head.esm") != nullptr ||
             dataHandler->LookupModByName("KLH_HighPolyHead.esp") != nullptr ||
             anyHphParts);

        std::size_t hphReadyEntries = 0;
        std::unordered_map<std::string, bool> crownBaldHairCache;
        std::size_t crownBaldCount = 0;
        EyeNifGeoCache eyeGeoCache;
        for (auto* race : dataHandler->GetFormArray<RE::TESRace>()) {
            if (!race) {
                continue;
            }
            for (const auto sex : { Foundation::Sex::Male, Foundation::Sex::Female }) {
                const auto* faceRelated = ResolveRaceFaceData(race, sex);
                if (!faceRelated) {
                    continue;
                }

                DualTopologyPools entry{};
                entry.race = race;
                PopulateRaceSexPool(
                    entry.vanilla,
                    Foundation::TopologyPool::Vanilla,
                    race,
                    faceRelated,
                    sex,
                    smpLoaded,
                    candidates,
                    pluginsWithNonPresetNpcs,
                    npcUsageCount,
                    crownBaldHairCache,
                    crownBaldCount,
                    eyeGeoCache);

                if (highPolyHeadAvailable_) {
                    PopulateRaceSexPool(
                        entry.highPolyHead,
                        Foundation::TopologyPool::HighPolyHead,
                        race,
                        faceRelated,
                        sex,
                        smpLoaded,
                        candidates,
                        pluginsWithNonPresetNpcs,
                        npcUsageCount,
                        crownBaldHairCache,
                        crownBaldCount,
                        eyeGeoCache);
                    if (entry.highPolyHead.primaryFace && !entry.highPolyHead.hairs.empty() &&
                        !entry.highPolyHead.eyes.empty()) {
                        ++hphReadyEntries;
                    }
                }

                if (faceRelated->presetNPCs) {
                    auto& slots = nativeMorphPresets_[MakeRaceSexKey(race, sex)];
                    for (const auto* preset : *faceRelated->presetNPCs) {
                        if (!preset || preset->IsDynamicForm() || !preset->faceData ||
                            !IsAuthoritativeForMorphHarvest(preset->GetFile(0), race)) {
                            continue;
                        }
                        for (const std::size_t slot : { std::size_t{ 0 }, std::size_t{ 2 }, std::size_t{ 3 } }) {
                            const std::int32_t v = preset->faceData->parts[slot];
                            if (v >= 0 && v < 128) {
                                slots[slot].push_back(v);
                            }
                        }
                    }
                }

                pools_.emplace(MakeRaceSexKey(race, sex), std::move(entry));
            }
        }

        for (auto& [key, slots] : nativeMorphPresets_) {
            (void)key;
            for (auto& vec : slots) {
                std::sort(vec.begin(), vec.end());
                vec.erase(std::unique(vec.begin(), vec.end()), vec.end());
            }
        }

        auto recordHumanoidChargenTri = [&](const RE::BGSHeadPart* facePart) {
            if (!facePart) {
                return;
            }
            const char* rawTri =
                facePart->morphs[RE::BGSHeadPart::MorphIndices::kChargenMorph].model.c_str();
            std::string normTri = NormalizeAssetPath(rawTri ? rawTri : "");
            if (!normTri.empty() &&
                std::ranges::find(humanoidChargenTris_, normTri) == humanoidChargenTris_.end()) {
                humanoidChargenTris_.push_back(std::move(normTri));
            }
        };
        for (const auto& [key, dualPool] : pools_) {
            (void)key;
            const RE::TESRace* r = dualPool.race;
            if (!r || r->IsChildRace() || !IsVanillaOrDlcMaster(r->GetFile(0))) {
                continue;
            }
            const std::string_view rawEdid = r->GetFormEditorID() ? r->GetFormEditorID() : "";
            if (Core::ClassifyRaceKey(Core::NormalizeRaceKey(rawEdid)) !=
                Core::RaceGroup::Humanoid) {
                continue;
            }
            recordHumanoidChargenTri(dualPool.vanilla.primaryFace);
            recordHumanoidChargenTri(dualPool.highPolyHead.primaryFace);
        }

        built_ = true;
        MORWHA_LOG_INFO(
            "HeadPartPoolIndexer: Indexed {} race/sex entries (HPH available={}, HPH ready={}, quarantined custom={}, bespoke={}, crownBaldHair={})",
            pools_.size(),
            highPolyHeadAvailable_,
            hphReadyEntries,
            quarantinedCount,
            bespokeSkippedCount,
            crownBaldCount);
    }

    Foundation::TopologyPool HeadPartPoolIndexer::SelectPreferredTopology(
        const RE::TESRace* race,
        Foundation::Sex sex) const noexcept {
        if (!race || !highPolyHeadAvailable_) {
            return Foundation::TopologyPool::Vanilla;
        }
        const auto it = pools_.find(MakeRaceSexKey(race, sex));
        if (it == pools_.end()) {
            return Foundation::TopologyPool::Vanilla;
        }
        const auto& hph = it->second.highPolyHead;
        const auto& van = it->second.vanilla;
        if (!hph.primaryFace || hph.hairs.empty() || hph.eyes.empty()) {
            return Foundation::TopologyPool::Vanilla;
        }
        if (!van.eyebrows.empty() && hph.eyebrows.empty()) {
            return Foundation::TopologyPool::Vanilla;
        }
        return Foundation::TopologyPool::HighPolyHead;
    }

    const RaceSexHeadPartPool* HeadPartPoolIndexer::GetPool(
        const RE::TESRace* race,
        Foundation::Sex sex,
        Foundation::TopologyPool topology) const noexcept {
        if (!race) {
            return nullptr;
        }
        const auto it = pools_.find(MakeRaceSexKey(race, sex));
        if (it == pools_.end()) {
            return nullptr;
        }
        if (topology == Foundation::TopologyPool::HighPolyHead) {
            return &it->second.highPolyHead;
        }
        if (topology == Foundation::TopologyPool::Vanilla) {
            return &it->second.vanilla;
        }
        return nullptr;
    }

    bool HeadPartPoolIndexer::IsPartInIndexedPool(
        const RE::BGSHeadPart* hp,
        const RE::TESRace* race,
        Foundation::Sex sex,
        Foundation::TopologyPool topology) const noexcept {
        if (!built_ || !hp || !race) {
            return false;
        }
        const auto* pool = GetPool(race, sex, topology);
        if (!pool) {
            return false;
        }
        auto containsPart = [hp](const std::vector<RE::BGSHeadPart*>& vec) noexcept {
            for (const auto* entry : vec) {
                if (entry == hp) {
                    return true;
                }
            }
            return false;
        };
        switch (hp->type.get()) {
        case RE::BGSHeadPart::HeadPartType::kFace:
            return hp == pool->primaryFace || containsPart(pool->faces);
        case RE::BGSHeadPart::HeadPartType::kMisc:
            return hp == pool->primaryMouth || containsPart(pool->mouths);
        case RE::BGSHeadPart::HeadPartType::kEyes:
            return containsPart(pool->eyes);
        case RE::BGSHeadPart::HeadPartType::kHair:
            return containsPart(pool->hairs);
        case RE::BGSHeadPart::HeadPartType::kEyebrows:
            return containsPart(pool->eyebrows);
        case RE::BGSHeadPart::HeadPartType::kFacialHair:
            return containsPart(pool->facialHairs);
        case RE::BGSHeadPart::HeadPartType::kScar:
            return containsPart(pool->scars);
        default:
            return false;
        }
    }

    bool HeadPartPoolIndexer::IsCompatibleIndexedEyeForRace(
        const RE::BGSHeadPart* eyePart,
        const RE::TESRace* targetRace,
        Foundation::Sex sex,
        Foundation::TopologyPool topology) const noexcept {
        if (!built_ || !eyePart || !targetRace ||
            eyePart->type.get() != RE::BGSHeadPart::HeadPartType::kEyes) {
            return false;
        }
        if (IsPartInIndexedPool(eyePart, targetRace, sex, topology)) {
            return true;
        }
        const auto* targetPool = GetPool(targetRace, sex, topology);
        if (!targetPool || targetPool->eyes.empty()) {
            return false;
        }
        if (!HaveCompatibleEyeGeometry(eyePart, targetPool->eyes.front())) {
            return false;
        }
        const std::uint64_t sexBit = static_cast<std::uint64_t>(sex);
        for (const auto& [key, dualPool] : pools_) {
            if ((key & 1ull) != sexBit) {
                continue;
            }
            const auto& candEyes = (topology == Foundation::TopologyPool::HighPolyHead)
                                       ? dualPool.highPolyHead.eyes
                                       : dualPool.vanilla.eyes;
            for (const auto* entry : candEyes) {
                if (entry == eyePart) {
                    return true;
                }
            }
        }
        return false;
    }

    std::vector<std::int32_t> HeadPartPoolIndexer::GetNativeMorphPresets(
        const RE::TESRace* race,
        Foundation::Sex sex,
        std::size_t slot) const {
        if (!built_ || !race || slot >= 4u) {
            return {};
        }

        static const std::vector<std::int32_t> kEmptyPresets{};
        const auto ownIt = nativeMorphPresets_.find(MakeRaceSexKey(race, sex));
        const auto& ownPresets =
            (ownIt != nativeMorphPresets_.end()) ? ownIt->second[slot] : kEmptyPresets;
        if (Core::HasSufficientPresetDiversity(ownPresets.size())) {
            return ownPresets;
        }

        const auto morphIt = race->morphRace
                                 ? nativeMorphPresets_.find(MakeRaceSexKey(race->morphRace, sex))
                                 : nativeMorphPresets_.end();
        const auto& morphPresets =
            (morphIt != nativeMorphPresets_.end()) ? morphIt->second[slot] : kEmptyPresets;

        const auto direct = Core::ResolveNativeMorphPresetPool(ownPresets, morphPresets, {});
        if (Core::HasSufficientPresetDiversity(direct.size())) {
            return direct;
        }

        auto extractChargenTriPair = [&](const RE::TESRace* r) -> std::pair<std::string, std::string> {
            if (!r) {
                return {};
            }
            const auto it = pools_.find(MakeRaceSexKey(r, sex));
            if (it == pools_.end()) {
                return {};
            }
            auto getTri = [](const RE::BGSHeadPart* fp) -> std::string {
                if (!fp) {
                    return {};
                }
                const char* raw =
                    fp->morphs[RE::BGSHeadPart::MorphIndices::kChargenMorph].model.c_str();
                return NormalizeAssetPath(raw ? raw : "");
            };
            return { getTri(it->second.vanilla.primaryFace), getTri(it->second.highPolyHead.primaryFace) };
        };

        auto [refVanTri, refHphTri] = extractChargenTriPair(race);
        if (refVanTri.empty() && refHphTri.empty() && race->morphRace) {
            std::tie(refVanTri, refHphTri) = extractChargenTriPair(race->morphRace);
        }
        if (refVanTri.empty() && refHphTri.empty()) {
            return direct;
        }

        const std::string_view rawEdid = race->GetFormEditorID() ? race->GetFormEditorID() : "";
        const std::string targetNormKey = Core::NormalizeRaceKey(rawEdid);
        const bool targetIsElder = EqualsCaseInsensitive(rawEdid, "ElderRace");
        const bool targetIsOrc = (targetNormKey == "orc");
        const Core::RaceGroup targetGroup = ResolveRaceGroup(race);
        const bool targetIsMer = IsMerRace(race);

        std::vector<Core::SharedTriPeerPresets> peers;
        const std::uint64_t sexBit = static_cast<std::uint64_t>(sex);
        for (const auto& [key, dualPool] : pools_) {
            if ((key & 1ull) != sexBit || !dualPool.race || dualPool.race == race) {
                continue;
            }
            const auto getTri = [](const RE::BGSHeadPart* fp) -> std::string {
                if (!fp) {
                    return {};
                }
                const char* raw =
                    fp->morphs[RE::BGSHeadPart::MorphIndices::kChargenMorph].model.c_str();
                return NormalizeAssetPath(raw ? raw : "");
            };
            const std::string candVanTri = getTri(dualPool.vanilla.primaryFace);
            const std::string candHphTri = getTri(dualPool.highPolyHead.primaryFace);
            const bool sharesTri =
                (!refVanTri.empty() && candVanTri == refVanTri) ||
                (!refHphTri.empty() && candHphTri == refHphTri);
            if (!sharesTri) {
                continue;
            }
            const auto nIt = nativeMorphPresets_.find(key);
            if (nIt == nativeMorphPresets_.end() || nIt->second[slot].empty()) {
                continue;
            }
            const std::string_view candEdid =
                dualPool.race->GetFormEditorID() ? dualPool.race->GetFormEditorID() : "";
            const std::string candNormKey = Core::NormalizeRaceKey(candEdid);
            peers.push_back(Core::SharedTriPeerPresets{
                .presets = nIt->second[slot],
                .sameRaceGroup = (ResolveRaceGroup(dualPool.race) == targetGroup),
                .sameMerClass = (IsMerRace(dualPool.race) == targetIsMer),
                .isElder = EqualsCaseInsensitive(candEdid, "ElderRace"),
                .isOrc = (candNormKey == "orc")
            });
        }

        return Core::ResolveNativeMorphPresetPool(
            ownPresets, morphPresets, peers, targetIsElder, targetIsOrc);
    }

    bool HeadPartPoolIndexer::SharesHumanoidChargenTri(const RE::TESRace* race) const noexcept {
        if (!built_ || !race || humanoidChargenTris_.empty()) {
            return false;
        }
        auto matchesRace = [&](const RE::TESRace* r) noexcept -> bool {
            if (!r) {
                return false;
            }
            for (const auto sex : { Foundation::Sex::Male, Foundation::Sex::Female }) {
                const auto it = pools_.find(MakeRaceSexKey(r, sex));
                if (it == pools_.end()) {
                    continue;
                }
                for (const RE::BGSHeadPart* fp :
                     { it->second.vanilla.primaryFace, it->second.highPolyHead.primaryFace }) {
                    if (!fp) {
                        continue;
                    }
                    const char* raw =
                        fp->morphs[RE::BGSHeadPart::MorphIndices::kChargenMorph].model.c_str();
                    const std::string norm = NormalizeAssetPath(raw ? raw : "");
                    if (!norm.empty() &&
                        std::ranges::find(humanoidChargenTris_, norm) != humanoidChargenTris_.end()) {
                        return true;
                    }
                }
            }
            return false;
        };
        return matchesRace(race) || matchesRace(race->morphRace);
    }

    std::vector<RE::TESNPC*> HeadPartPoolIndexer::GatherCompatiblePeerPresets(
        const RE::TESRace* race,
        Foundation::Sex sex) const {
        std::vector<RE::TESNPC*> out;
        if (!built_ || !race) {
            return out;
        }

        auto getTriPair = [&](const RE::TESRace* r) -> std::pair<std::string, std::string> {
            if (!r) {
                return {};
            }
            const auto it = pools_.find(MakeRaceSexKey(r, sex));
            if (it == pools_.end()) {
                return {};
            }
            auto getTri = [](const RE::BGSHeadPart* fp) -> std::string {
                if (!fp) {
                    return {};
                }
                const char* raw =
                    fp->morphs[RE::BGSHeadPart::MorphIndices::kChargenMorph].model.c_str();
                return NormalizeAssetPath(raw ? raw : "");
            };
            return { getTri(it->second.vanilla.primaryFace), getTri(it->second.highPolyHead.primaryFace) };
        };

        auto [refVanTri, refHphTri] = getTriPair(race);
        if (refVanTri.empty() && refHphTri.empty() && race->morphRace) {
            std::tie(refVanTri, refHphTri) = getTriPair(race->morphRace);
        }
        if (refVanTri.empty() && refHphTri.empty()) {
            return out;
        }

        const std::string_view rawEdid = race->GetFormEditorID() ? race->GetFormEditorID() : "";
        const bool targetIsElder = EqualsCaseInsensitive(rawEdid, "ElderRace");
        const bool targetIsOrc = (Core::NormalizeRaceKey(rawEdid) == "orc");
        const Core::RaceGroup targetGroup = ResolveRaceGroup(race);
        const bool targetIsMer = IsMerRace(race);
        const auto sexIdx = static_cast<std::size_t>(sex);
        const std::uint64_t sexBit = static_cast<std::uint64_t>(sex);

        for (const auto& [key, dualPool] : pools_) {
            if ((key & 1ull) != sexBit || !dualPool.race || dualPool.race == race) {
                continue;
            }
            const std::string_view candEdid =
                dualPool.race->GetFormEditorID() ? dualPool.race->GetFormEditorID() : "";
            if (!targetIsElder && EqualsCaseInsensitive(candEdid, "ElderRace")) {
                continue;
            }
            if (!targetIsOrc && Core::NormalizeRaceKey(candEdid) == "orc") {
                continue;
            }
            if (ResolveRaceGroup(dualPool.race) != targetGroup ||
                IsMerRace(dualPool.race) != targetIsMer) {
                continue;
            }
            auto [candVanTri, candHphTri] = getTriPair(dualPool.race);
            const bool sharesTri =
                (!refVanTri.empty() && candVanTri == refVanTri) ||
                (!refHphTri.empty() && candHphTri == refHphTri);
            if (!sharesTri) {
                continue;
            }
            const auto* fr = dualPool.race->faceRelatedData[sexIdx];
            if (!fr || !fr->presetNPCs) {
                continue;
            }
            for (auto* preset : *fr->presetNPCs) {
                if (preset && preset->faceData &&
                    std::ranges::find(out, preset) == out.end()) {
                    out.push_back(preset);
                }
            }
        }
        return out;
    }

    std::vector<std::int32_t> HeadPartPoolIndexer::GetSharedTriMorphPresets(
        const RE::TESRace* race,
        Foundation::Sex sex,
        std::size_t slot) const {
        if (!built_ || !race || slot >= 4u) {
            return {};
        }
        auto resolveRefChargenTri = [&](const RE::TESRace* r) -> std::string {
            if (!r) {
                return {};
            }
            const auto it = pools_.find(MakeRaceSexKey(r, sex));
            if (it == pools_.end()) {
                return {};
            }
            const RE::BGSHeadPart* refFace =
                it->second.vanilla.primaryFace ? it->second.vanilla.primaryFace
                                               : it->second.highPolyHead.primaryFace;
            if (!refFace) {
                return {};
            }
            const char* rawTri =
                refFace->morphs[RE::BGSHeadPart::MorphIndices::kChargenMorph].model.c_str();
            return NormalizeAssetPath(rawTri ? rawTri : "");
        };

        std::string refChargenTri = resolveRefChargenTri(race);
        if (refChargenTri.empty() && race->morphRace) {
            refChargenTri = resolveRefChargenTri(race->morphRace);
        }
        if (refChargenTri.empty()) {
            return GetNativeMorphPresets(race, sex, slot);
        }

        std::vector<std::int32_t> merged;
        const std::uint64_t sexBit = static_cast<std::uint64_t>(sex);
        for (const auto& [key, dualPool] : pools_) {
            if ((key & 1ull) != sexBit) {
                continue;
            }
            const RE::BGSHeadPart* candFace =
                dualPool.vanilla.primaryFace ? dualPool.vanilla.primaryFace
                                             : dualPool.highPolyHead.primaryFace;
            if (!candFace) {
                continue;
            }
            const char* candRaw =
                candFace->morphs[RE::BGSHeadPart::MorphIndices::kChargenMorph].model.c_str();
            if (NormalizeAssetPath(candRaw ? candRaw : "") != refChargenTri) {
                continue;
            }
            if (const auto nIt = nativeMorphPresets_.find(key); nIt != nativeMorphPresets_.end()) {
                const auto& vec = nIt->second[slot];
                merged.insert(merged.end(), vec.begin(), vec.end());
            }
        }
        if (merged.empty()) {
            return GetNativeMorphPresets(race, sex, slot);
        }
        std::sort(merged.begin(), merged.end());
        merged.erase(std::unique(merged.begin(), merged.end()), merged.end());
        return merged;
    }

    bool HeadPartPoolIndexer::HaveCompatibleEyeGeometry(
        const RE::BGSHeadPart* a,
        const RE::BGSHeadPart* b) noexcept {
        if (!a || !b ||
            a->type.get() != RE::BGSHeadPart::HeadPartType::kEyes ||
            b->type.get() != RE::BGSHeadPart::HeadPartType::kEyes) {
            return false;
        }
        const auto sigA = MakeGeometrySignature(a);
        const auto sigB = MakeGeometrySignature(b);
        if (sigA == sigB) {
            return true;
        }
        if (sigA.defaultTri != sigB.defaultTri || sigA.chargenTri != sigB.chargenTri) {
            return false;
        }
        if (!sigA.raceTri.empty() && !sigB.raceTri.empty() && sigA.raceTri != sigB.raceTri) {
            return false;
        }
        try {
            thread_local EyeNifGeoCache s_eyeGeoCache;
            const auto geoA = InspectEyeNifGeometry(sigA.model, s_eyeGeoCache);
            const auto geoB = InspectEyeNifGeometry(sigB.model, s_eyeGeoCache);
            return AreEyeCentersCompatible(geoA, geoB);
        } catch (...) {
            return false;
        }
    }

    std::uint64_t HeadPartPoolIndexer::MakeRaceSexKey(
        const RE::TESRace* race,
        Foundation::Sex sex) noexcept {
        const std::uint64_t formId = race ? static_cast<std::uint64_t>(race->GetFormID()) : 0ull;
        return (formId << 1u) | static_cast<std::uint64_t>(sex);
    }

}  // namespace MORWHA::Engine
