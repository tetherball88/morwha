#include "morwha/state/RegistryGarbageCollector.h"

#include "morwha/state/CoSaveCodec.h"

#include <cctype>
#include <cstring>
#include <format>
#include <fstream>
#include <unordered_map>

namespace MORWHA::State {

    namespace {

        constexpr std::size_t kSkseFileHeaderBytes = 20u;
        constexpr std::size_t kSksePluginHeaderBytes = 12u;
        constexpr std::size_t kSkseChunkHeaderBytes = 12u;
        constexpr std::uint32_t kMaxLinksChunkBytes = 16u * 1024u * 1024u;

        [[nodiscard]] bool EqualsAsciiCaseInsensitive(std::string_view a, std::string_view b) noexcept {
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

        [[nodiscard]] std::uint32_t ReadLeU32(const std::uint8_t* p) noexcept {
            std::uint32_t v = 0;
            std::memcpy(&v, p, sizeof(v));
            return v;
        }

        [[nodiscard]] bool DecodeLinksPayloadIntoKeys(
            std::uint32_t chunkVersion,
            std::span<const std::uint8_t> payload,
            std::unordered_set<std::string>& outKeys,
            std::string& outError) {
            if (chunkVersion != kCoSaveFormatVersion) {
                outError = std::format("Unsupported 'LNKS' chunk version {}", chunkVersion);
                return false;
            }
            std::vector<SpawnedMortalLink> links;
            if (!DecodeSpawnedLinks(payload, links)) {
                outError = "Malformed 'MRWH'/'LNKS' payload";
                return false;
            }
            for (const auto& link : links) {
                if (!link.slotKey.empty()) {
                    outKeys.insert(link.slotKey);
                }
            }
            return true;
        }

        [[nodiscard]] bool HasCompanionEssSave(const std::filesystem::path& sksePath) noexcept {
            std::error_code ec;
            std::filesystem::path essLower = sksePath;
            essLower.replace_extension(".ess");
            if (std::filesystem::is_regular_file(essLower, ec) && !ec) {
                return true;
            }
            ec.clear();
            std::filesystem::path essUpper = sksePath;
            essUpper.replace_extension(".ESS");
            return std::filesystem::is_regular_file(essUpper, ec) && !ec;
        }

    }  // namespace

    SkseCoSaveParseResult ExtractSlotKeysFromSkseBytes(std::span<const std::uint8_t> bytes) {
        SkseCoSaveParseResult res{};
        if (bytes.size() < kSkseFileHeaderBytes || bytes.size() > kMaxSkseCoSaveFileBytes) {
            res.error = "Invalid .skse file size";
            return res;
        }

        const std::uint32_t sig = ReadLeU32(bytes.data() + 0u);
        const std::uint32_t fmtVer = ReadLeU32(bytes.data() + 4u);
        const std::uint32_t numPlugins = ReadLeU32(bytes.data() + 16u);
        if (sig != kSkseCoSaveSignature || fmtVer != kSkseCoSaveFormatVersion) {
            res.error = std::format("Invalid .skse header (sig=0x{:08X}, ver={})", sig, fmtVer);
            return res;
        }

        std::size_t offset = kSkseFileHeaderBytes;
        for (std::uint32_t p = 0; p < numPlugins; ++p) {
            if (bytes.size() - offset < kSksePluginHeaderBytes) {
                res.error = "Truncated .skse plugin header";
                return res;
            }
            const std::uint32_t uid = ReadLeU32(bytes.data() + offset);
            const std::uint32_t numChunks = ReadLeU32(bytes.data() + offset + 4u);
            const std::uint32_t pluginLen = ReadLeU32(bytes.data() + offset + 8u);
            offset += kSksePluginHeaderBytes;

            if (bytes.size() - offset < pluginLen) {
                res.error = "Truncated .skse plugin block";
                return res;
            }

            if (uid != kMorwhaPluginId) {
                offset += pluginLen;
                continue;
            }

            res.hasMorwhaPlugin = true;
            std::size_t pluginConsumed = 0;
            for (std::uint32_t c = 0; c < numChunks; ++c) {
                if (pluginLen - pluginConsumed < kSkseChunkHeaderBytes) {
                    res.error = "Truncated 'MRWH' chunk header";
                    return res;
                }
                const std::uint8_t* chPtr = bytes.data() + offset + pluginConsumed;
                const std::uint32_t chType = ReadLeU32(chPtr);
                const std::uint32_t chVer = ReadLeU32(chPtr + 4u);
                const std::uint32_t chLen = ReadLeU32(chPtr + 8u);
                pluginConsumed += kSkseChunkHeaderBytes;

                if (pluginLen - pluginConsumed < chLen) {
                    res.error = "Truncated 'MRWH' chunk payload";
                    return res;
                }
                if (chType == kLinksChunkId) {
                    if (chLen > kMaxLinksChunkBytes) {
                        res.error = "'LNKS' chunk exceeds maximum size";
                        return res;
                    }
                    const std::span<const std::uint8_t> payload(
                        bytes.data() + offset + pluginConsumed, chLen);
                    if (!DecodeLinksPayloadIntoKeys(chVer, payload, res.slotKeys, res.error)) {
                        return res;
                    }
                }
                pluginConsumed += chLen;
            }

            if (pluginConsumed != pluginLen) {
                res.error = "'MRWH' chunk sizes do not match plugin block length";
                return res;
            }
            offset += pluginLen;
        }

        if (offset != bytes.size()) {
            res.error = "Trailing or misaligned bytes at end of .skse file";
            return res;
        }

        res.ok = true;
        return res;
    }

    SkseCoSaveParseResult ExtractSlotKeysFromSkseFile(const std::filesystem::path& sksePath) {
        SkseCoSaveParseResult res{};
        std::error_code ec;
        if (!std::filesystem::is_regular_file(sksePath, ec) || ec) {
            res.error = "Co-save path is not a regular file";
            return res;
        }
        const auto fileSize = std::filesystem::file_size(sksePath, ec);
        if (ec || fileSize < kSkseFileHeaderBytes || fileSize > kMaxSkseCoSaveFileBytes) {
            res.error = "Co-save file size is invalid or out of bounds";
            return res;
        }

        std::ifstream in(sksePath, std::ios::binary);
        if (!in.is_open()) {
            res.error = "Failed to open .skse file for reading";
            return res;
        }

        std::uint8_t hdr[kSkseFileHeaderBytes]{};
        in.read(reinterpret_cast<char*>(hdr), sizeof(hdr));
        if (!in || static_cast<std::size_t>(in.gcount()) != sizeof(hdr)) {
            res.error = "Failed to read .skse header";
            return res;
        }

        const std::uint32_t sig = ReadLeU32(hdr + 0u);
        const std::uint32_t fmtVer = ReadLeU32(hdr + 4u);
        const std::uint32_t numPlugins = ReadLeU32(hdr + 16u);
        if (sig != kSkseCoSaveSignature || fmtVer != kSkseCoSaveFormatVersion) {
            res.error = std::format("Invalid .skse header (sig=0x{:08X}, ver={})", sig, fmtVer);
            return res;
        }

        std::uintmax_t offset = kSkseFileHeaderBytes;
        for (std::uint32_t p = 0; p < numPlugins; ++p) {
            if (fileSize - offset < kSksePluginHeaderBytes) {
                res.error = "Truncated .skse plugin header";
                return res;
            }
            std::uint8_t pHdr[kSksePluginHeaderBytes]{};
            in.read(reinterpret_cast<char*>(pHdr), sizeof(pHdr));
            if (!in || static_cast<std::size_t>(in.gcount()) != sizeof(pHdr)) {
                res.error = "Failed reading .skse plugin header";
                return res;
            }
            const std::uint32_t uid = ReadLeU32(pHdr);
            const std::uint32_t numChunks = ReadLeU32(pHdr + 4u);
            const std::uint32_t pluginLen = ReadLeU32(pHdr + 8u);
            offset += kSksePluginHeaderBytes;

            if (fileSize - offset < pluginLen) {
                res.error = "Truncated .skse plugin block";
                return res;
            }

            if (uid != kMorwhaPluginId) {
                if (pluginLen > 0 && !in.seekg(static_cast<std::streamoff>(pluginLen), std::ios::cur)) {
                    res.error = "Failed seeking past plugin block";
                    return res;
                }
                offset += pluginLen;
                continue;
            }

            res.hasMorwhaPlugin = true;
            std::uint32_t pluginConsumed = 0;
            for (std::uint32_t c = 0; c < numChunks; ++c) {
                if (pluginLen - pluginConsumed < kSkseChunkHeaderBytes) {
                    res.error = "Truncated 'MRWH' chunk header";
                    return res;
                }
                std::uint8_t cHdr[kSkseChunkHeaderBytes]{};
                in.read(reinterpret_cast<char*>(cHdr), sizeof(cHdr));
                if (!in || static_cast<std::size_t>(in.gcount()) != sizeof(cHdr)) {
                    res.error = "Failed reading 'MRWH' chunk header";
                    return res;
                }
                const std::uint32_t chType = ReadLeU32(cHdr);
                const std::uint32_t chVer = ReadLeU32(cHdr + 4u);
                const std::uint32_t chLen = ReadLeU32(cHdr + 8u);
                pluginConsumed += static_cast<std::uint32_t>(kSkseChunkHeaderBytes);

                if (pluginLen - pluginConsumed < chLen) {
                    res.error = "Truncated 'MRWH' chunk payload";
                    return res;
                }
                if (chType == kLinksChunkId) {
                    if (chLen > kMaxLinksChunkBytes) {
                        res.error = "'LNKS' chunk exceeds maximum size";
                        return res;
                    }
                    std::vector<std::uint8_t> payload(chLen);
                    if (chLen > 0) {
                        in.read(reinterpret_cast<char*>(payload.data()), chLen);
                        if (!in || static_cast<std::uint32_t>(in.gcount()) != chLen) {
                            res.error = "Failed reading 'LNKS' payload";
                            return res;
                        }
                    }
                    if (!DecodeLinksPayloadIntoKeys(chVer, payload, res.slotKeys, res.error)) {
                        return res;
                    }
                } else if (chLen > 0) {
                    if (!in.seekg(static_cast<std::streamoff>(chLen), std::ios::cur)) {
                        res.error = "Failed seeking past non-'LNKS' chunk";
                        return res;
                    }
                }
                pluginConsumed += chLen;
            }

            if (pluginConsumed != pluginLen) {
                res.error = "'MRWH' chunk sizes do not match plugin block length";
                return res;
            }
            offset += pluginLen;
        }

        if (offset != fileSize) {
            res.error = "Trailing or misaligned bytes at end of .skse file";
            return res;
        }

        res.ok = true;
        return res;
    }

    SaveDirectoryScanResult ScanSaveDirectoriesForSlotKeys(
        std::span<const std::filesystem::path> candidateDirs,
        bool requireCompanionEss) {
        SaveDirectoryScanResult out{};
        std::vector<std::filesystem::path> uniqueDirs;
        uniqueDirs.reserve(candidateDirs.size());

        for (const auto& candidate : candidateDirs) {
            if (candidate.empty()) {
                continue;
            }
            std::error_code ec;
            if (!std::filesystem::is_directory(candidate, ec) || ec) {
                continue;
            }
            bool duplicate = false;
            for (const auto& existing : uniqueDirs) {
                std::error_code eqEc;
                if (std::filesystem::equivalent(existing, candidate, eqEc) && !eqEc) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate) {
                uniqueDirs.push_back(candidate);
            }
        }

        out.existingDirectoryCount = uniqueDirs.size();
        if (uniqueDirs.empty()) {
            out.ok = true;
            out.canPruneCommitted = false;
            return out;
        }

        for (const auto& dir : uniqueDirs) {
            std::error_code iterEc;
            std::filesystem::directory_iterator it(dir, iterEc);
            if (iterEc) {
                out.ok = false;
                out.canPruneCommitted = false;
                out.error = std::format("Failed iterating save directory '{}'", dir.string());
                return out;
            }

            const std::filesystem::directory_iterator end;
            for (; it != end; it.increment(iterEc)) {
                if (iterEc) {
                    out.ok = false;
                    out.canPruneCommitted = false;
                    out.error = std::format("Error reading save directory entry in '{}'", dir.string());
                    return out;
                }

                std::error_code statusEc;
                if (!it->is_regular_file(statusEc) || statusEc) {
                    continue;
                }

                const auto& entryPath = it->path();
                const std::string ext = entryPath.extension().string();
                if (!EqualsAsciiCaseInsensitive(ext, ".skse")) {
                    continue;
                }

                if (requireCompanionEss && !HasCompanionEssSave(entryPath)) {
                    ++out.skippedOrphanCoSaveCount;
                    continue;
                }

                const auto parseRes = ExtractSlotKeysFromSkseFile(entryPath);
                if (!parseRes.ok) {
                    out.ok = false;
                    out.canPruneCommitted = false;
                    out.error = std::format(
                        "Failed parsing '{}': {}",
                        entryPath.filename().string(),
                        parseRes.error);
                    return out;
                }

                ++out.scannedCoSaveCount;
                if (!parseRes.slotKeys.empty()) {
                    ++out.matchedMorwhaSaveCount;
                    for (const auto& key : parseRes.slotKeys) {
                        out.referencedSlotKeys.insert(key);
                    }
                }
            }
        }

        out.ok = true;
        out.canPruneCommitted = true;
        return out;
    }

    std::unordered_set<std::string> ExpandTransitiveDonorSlotKeys(
        std::span<const DynamicNpcSpec> specs,
        const std::unordered_set<std::string>& rootSlotKeys) {
        std::unordered_set<std::string> visited;
        if (rootSlotKeys.empty()) {
            return visited;
        }

        std::unordered_map<std::string_view, std::size_t> indexByKey;
        indexByKey.reserve(specs.size());
        for (std::size_t i = 0; i < specs.size(); ++i) {
            indexByKey.emplace(specs[i].slotKey, i);
        }

        visited.reserve(rootSlotKeys.size() * 2u);
        std::vector<std::string> worklist;
        worklist.reserve(rootSlotKeys.size() * 2u);
        for (const auto& root : rootSlotKeys) {
            if (!root.empty() && visited.insert(root).second) {
                worklist.push_back(root);
            }
        }

        while (!worklist.empty()) {
            const std::string current = std::move(worklist.back());
            worklist.pop_back();

            const auto it = indexByKey.find(current);
            if (it == indexByKey.end()) {
                continue;
            }
            const auto& spec = specs[it->second];
            if (!spec.donorASlotKey.empty() && visited.insert(spec.donorASlotKey).second) {
                worklist.push_back(spec.donorASlotKey);
            }
            if (!spec.donorBSlotKey.empty() && visited.insert(spec.donorBSlotKey).second) {
                worklist.push_back(spec.donorBSlotKey);
            }
        }

        return visited;
    }

    bool MarkCommittedSpecsForLinks(
        std::vector<DynamicNpcSpec>& specs,
        std::span<const SpawnedMortalLink> savedLinks) {
        if (specs.empty() || savedLinks.empty()) {
            return false;
        }

        std::unordered_set<std::string> rootKeys;
        rootKeys.reserve(savedLinks.size());
        for (const auto& link : savedLinks) {
            if (!link.slotKey.empty()) {
                rootKeys.insert(link.slotKey);
            }
        }
        if (rootKeys.empty()) {
            return false;
        }

        const auto reachable = ExpandTransitiveDonorSlotKeys(specs, rootKeys);
        bool anyChanged = false;
        for (auto& spec : specs) {
            if (!spec.committed && reachable.contains(spec.slotKey)) {
                spec.committed = true;
                anyChanged = true;
            }
        }
        return anyChanged;
    }

    GarbageCollectionPlan PlanRegistryGarbageCollection(
        std::span<const DynamicNpcSpec> specs,
        const std::unordered_set<std::string>& activeSessionKeys,
        const std::unordered_set<std::string>& savedCoSaveKeys,
        bool pruneUncommitted,
        bool pruneUnreferencedCommitted) {
        GarbageCollectionPlan plan{};
        plan.survivingSpecs.reserve(specs.size());

        const auto keepUncommitted = ExpandTransitiveDonorSlotKeys(specs, activeSessionKeys);

        std::unordered_set<std::string> committedRoots = activeSessionKeys;
        committedRoots.insert(savedCoSaveKeys.begin(), savedCoSaveKeys.end());
        const auto keepCommitted = ExpandTransitiveDonorSlotKeys(specs, committedRoots);

        for (const auto& spec : specs) {
            if (!spec.committed) {
                if (pruneUncommitted && !keepUncommitted.contains(spec.slotKey)) {
                    plan.prunedUncommittedKeys.push_back(spec.slotKey);
                } else {
                    plan.survivingSpecs.push_back(spec);
                }
            } else {
                if (pruneUnreferencedCommitted && !keepCommitted.contains(spec.slotKey)) {
                    plan.prunedCommittedKeys.push_back(spec.slotKey);
                } else {
                    plan.survivingSpecs.push_back(spec);
                }
            }
        }

        return plan;
    }

}  // namespace MORWHA::State
