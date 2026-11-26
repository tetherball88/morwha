#pragma once

// MORWHA Public C ABI (v1)
// ========================
// Exported C functions for zero-link runtime resolution via GetProcAddress
// from companion SKSE plugins (e.g., MARAS.dll).
// Safe to call from SKSE::MessagingInterface::kDataLoaded onwards.

#include <cstdint>

inline constexpr std::uint32_t kMORWHA_ApiVersion = 1u;
inline constexpr std::uint32_t kMORWHA_ClearFormId = 0xFFFFFFFFu;
inline constexpr std::uint32_t kMORWHA_SkillCount = 18u;

enum class MORWHA_DonorMode : std::uint8_t {
    ZeroDonorProcedural = 0,
    OneDonorRelative = 1,
    TwoDonorOffspring = 2
};

enum class MORWHA_AgeBracket : std::uint8_t {
    Auto = 0,
    YoungAdult = 1,
    Mature = 2,
    MiddleAged = 3,
    Elder = 4
};

struct MORWHA_FactionRank {
    std::uint32_t factionFormId{ 0 };
    std::int8_t rank{ 0 };
    std::uint8_t reserved[3]{};
};

// Optional archetype & record overrides (0 = keep auto-resolved/unchanged, kMORWHA_ClearFormId = clear).
// Note: voiceTypeFormId != 0 overrides AgeBracket automatic sex-checked voice selection;
//       faceTextureSetFormId != 0 overrides AgeBracket radiometric skin complexion selection.
struct MORWHA_ArchetypeParams {
    std::uint32_t structSize{ sizeof(MORWHA_ArchetypeParams) };
    std::uint32_t voiceTypeFormId{ 0 };
    std::uint32_t npcClassFormId{ 0 };
    std::uint32_t combatStyleFormId{ 0 };
    std::uint32_t defaultOutfitFormId{ 0 };
    std::uint32_t sleepOutfitFormId{ 0 };
    std::uint32_t crimeFactionFormId{ 0 };
    std::uint32_t faceTextureSetFormId{ 0 };
};

// Optional AI disposition & protection flags (-1 = keep default/unchanged).
struct MORWHA_AiParams {
    std::uint32_t structSize{ sizeof(MORWHA_AiParams) };
    std::int8_t aggression{ -1 };   // 0=Unaggressive..3=Frenzied
    std::int8_t confidence{ -1 };   // 0=Cowardly..4=Foolhardy
    std::int8_t assistance{ -1 };   // 0=HelpsNobody..2=HelpsFriendsAndAllies
    std::int8_t morality{ -1 };     // 0=AnyCrime..3=NoCrime
    std::int8_t isProtected{ -1 };  // 0=false, 1=true
    std::int8_t isEssential{ -1 };  // 0=false, 1=true
    std::uint8_t reserved[2]{};
};

// Optional RPG level, H/M/S, 18 skills (OneHanded..Enchanting; 0 = keep default/unchanged),
// perks, base spells, and faction ranks.
struct MORWHA_StatsParams {
    std::uint32_t structSize{ sizeof(MORWHA_StatsParams) };
    std::uint16_t level{ 0 };
    std::uint16_t calcLevelMin{ 0 };
    std::uint16_t calcLevelMax{ 0 };
    std::int8_t autoCalcStats{ -1 };  // -1=unchanged, 0=false, 1=true
    std::int8_t pcLevelMult{ -1 };    // -1=unchanged, 0=false, 1=true
    std::uint16_t health{ 0 };
    std::uint16_t magicka{ 0 };
    std::uint16_t stamina{ 0 };
    std::uint8_t skills[kMORWHA_SkillCount]{};
    std::uint8_t replacePerks{ 0 };
    std::uint8_t replaceSpells{ 0 };
    std::uint8_t replaceFactions{ 0 };
    std::uint8_t reserved0{ 0 };
    const std::uint32_t* perkFormIds{ nullptr };
    std::uint32_t perkCount{ 0 };
    std::uint32_t reserved1{ 0 };
    const std::uint32_t* spellFormIds{ nullptr };
    std::uint32_t spellCount{ 0 };
    std::uint32_t reserved2{ 0 };
    const MORWHA_FactionRank* factions{ nullptr };
    std::uint32_t factionCount{ 0 };
    std::uint32_t reserved3{ 0 };
};

struct MORWHA_SpawnParams {
    std::uint32_t structSize{ sizeof(MORWHA_SpawnParams) };
    MORWHA_DonorMode donorMode{ MORWHA_DonorMode::ZeroDonorProcedural };
    std::int8_t targetSex{ -1 };  // 0=Male, 1=Female, -1=Random
    MORWHA_AgeBracket ageBracket{ MORWHA_AgeBracket::Auto };
    std::uint8_t forcePersist{ 1 };
    std::uint32_t anchorRefFormId{ 0 };
    std::uint32_t raceFormId{ 0 };
    std::uint32_t donorAFormId{ 0 };  // Accepts Actor or base TESNPC FormID
    std::uint32_t donorBFormId{ 0 };  // Accepts Actor or base TESNPC FormID
    std::uint64_t seed{ 0 };          // 0 = auto-generate
    const char* displayName{ nullptr };
    const char* slotKeyPrefix{ nullptr };
    const MORWHA_ArchetypeParams* archetype{ nullptr };
    const MORWHA_AiParams* ai{ nullptr };
    const MORWHA_StatsParams* stats{ nullptr };
};

struct MORWHA_SpawnResult {
    std::uint32_t structSize{ sizeof(MORWHA_SpawnResult) };
    std::uint32_t actorFormId{ 0 };
    std::uint32_t baseNpcFormId{ 0 };
    std::uint32_t resolvedRaceFormId{ 0 };
    std::uint8_t resolvedSex{ 0 };
    MORWHA_AgeBracket resolvedAgeBracket{ MORWHA_AgeBracket::YoungAdult };
    std::uint8_t reserved[2]{};
    char slotKey[64]{};
};

struct MORWHA_UpdateParams {
    std::uint32_t structSize{ sizeof(MORWHA_UpdateParams) };
    std::uint32_t targetActorFormId{ 0 };
    MORWHA_AgeBracket ageBracket{ MORWHA_AgeBracket::Auto };  // Auto = unchanged
    std::int8_t forcePersist{ -1 };                            // -1 = unchanged, 0/1 = set
    std::uint8_t refreshOutfit{ 0 };
    std::uint8_t reserved{ 0 };
    const char* displayName{ nullptr };
    const MORWHA_ArchetypeParams* archetype{ nullptr };
    const MORWHA_AiParams* ai{ nullptr };
    const MORWHA_StatsParams* stats{ nullptr };
};

struct MORWHA_RerollParams {
    std::uint32_t structSize{ sizeof(MORWHA_RerollParams) };
    std::uint32_t targetActorFormId{ 0 };
    std::uint32_t raceFormId{ 0 };
    std::int8_t sexOverride{ -1 };
    std::uint8_t rerollRaceFromDonors{ 0 };
    MORWHA_AgeBracket ageBracketOverride{ MORWHA_AgeBracket::Auto };
    std::uint8_t reserved{ 0 };
    const char* nameOverride{ nullptr };
    const MORWHA_ArchetypeParams* archetype{ nullptr };
    const MORWHA_AiParams* ai{ nullptr };
    const MORWHA_StatsParams* stats{ nullptr };
};

struct MORWHA_HeritageInfo {
    std::uint32_t structSize{ sizeof(MORWHA_HeritageInfo) };
    std::uint8_t isMorwhaMortal{ 0 };
    std::uint8_t isMixedHeritage{ 0 };
    std::uint8_t chimeraTier{ 0 };  // 0=None/Disabled, 1=Tier1Dominant, 2=Tier2Hybrid
    std::uint8_t reserved{ 0 };
    std::uint32_t primaryRaceFormId{ 0 };
    std::uint32_t secondaryRaceFormId{ 0 };
    char primaryRace[32]{};    // e.g. "Orsimer", "Khajiit", "Nord"
    char secondaryRace[32]{};  // e.g. "Khajiit", "Argonian", "Redguard", or ""
    char groupPair[24]{};      // e.g. "MerBeast", "ManBeast", "BeastBeast", "ManMer", "ManMan", "MerMer", or ""
    char racePair[48]{};       // e.g. "OrsimerKhajiit", "KhajiitArgonian", "ArgonianKhajiit", "NordRedguard", or ""
    char lineageLabel[64]{};   // e.g. "Orsimer-Khajiit", "Khajiit-Argonian", "Nord"
    char loreName[64]{};       // e.g. "Ohmes-kin", "Hist-kin", "Hist-raht", "Moon-scale", "Half-Altmer", "Nord-Redguard"
};

extern "C" {

// Returns the binary ABI version implemented by MORWHA.dll (1).
__declspec(dllexport) std::uint32_t MORWHA_GetApiVersion();

// Returns true once MORWHA has completed kDataLoaded initialization.
__declspec(dllexport) bool MORWHA_IsReady();

// Synthesizes, persists, hydrates, and spawns a unique DPF TESNPC actor at anchorRefFormId.
// outResult may be nullptr if caller only needs pass/fail.
__declspec(dllexport) bool MORWHA_SpawnMortal(
    const MORWHA_SpawnParams* params,
    MORWHA_SpawnResult* outResult);

// Updates archetype, voice, class, outfits, AI, stats, perks, spells, or factions on an existing
// MORWHA-spawned actor in-place, persisting changes to DynamicNPCRegistry.json.
__declspec(dllexport) bool MORWHA_UpdateMortal(const MORWHA_UpdateParams* params);

// Re-rolls an existing MORWHA-spawned actor in-place from its stored donor provenance and refreshes 3D.
__declspec(dllexport) bool MORWHA_RerollMortal(const MORWHA_RerollParams* params);

// Queries mixed-heritage, macro-group, directional race-pair, and lore naming metadata for a
// MORWHA-spawned live Actor OR base TESNPC FormID. Returns true and populates outInfo if registered.
__declspec(dllexport) bool MORWHA_GetMortalHeritage(
    std::uint32_t targetFormId,
    MORWHA_HeritageInfo* outInfo);

}  // extern "C"
