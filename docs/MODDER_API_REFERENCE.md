# M.O.R.W.H.A — Modder API & Extension Reference

Integration guide for SKSE C++ plugin authors, Papyrus scripters, and asset-pack creators building on **M.O.R.W.H.A** (*Mortals On-Demand: Runtime World-Spawning, Heads & Appearance*).

---

## 1. Overview & Runtime Lifecycle

Every mortal spawned through M.O.R.W.H.A receives its own dedicated, persistent `RE::TESNPC` base record allocated dynamically inside `DPF.esp` (**Dynamic Persistent Forms REthinked**). Because each spawned actor owns a unique `TESNPC` (`kUnique`, `faceNPC = nullptr`, `kRespawn` cleared), changes to its face, voice, class, combat style, outfits, AI flags, stats, perks, spells, and factions never bleed onto other NPCs.

### Readiness & Threading Contract
- **Initialization Gate**: All C++ and Papyrus APIs become active at `SKSE::MessagingInterface::kDataLoaded`. Always check `MORWHA_IsReady()` (C++) or `MORWHA.IsReady()` (Papyrus) before spawning, updating, or rerolling.
- **Active Session & Main Thread**: `Spawn*`, `Update*`, and `Reroll*` mutate engine `TESNPC` / `Actor` state and call `PlaceObjectAtMe`. Invoke them during an active game session (loaded save or new game, not on `MainMenu`). From C++ SKSE plugins, dispatch world-spawning/mutation calls on Skyrim's main thread (`SKSE::GetTaskInterface()->AddTask(...)`) or inside native Papyrus/event callbacks.
- **Donor Flexibility (`Actor` or `TESNPC`)**: `MORWHA_SpawnMortal` and `MORWHA.SpawnMortalJson` accept **either** a live `RE::Actor` FormID (Tier-1 live 3D extraction) **or** a base `RE::TESNPC` FormID (static `FaceGeom` `.nif` + `TESNPC` record extraction — useful when a parent is dead, unspawned, or stored only by base FormID).

### Persistence & Commit-on-Save Staging
- Appearance and archetype specs are written atomically to `Data/SKSE/Plugins/MORWHA/DynamicNPCRegistry.json`, while per-save spawned actor links (`actorRefId` $\leftrightarrow$ `slotKey` / `dpfLocalId`) are saved in the `'MRWH'` SKSE co-save.
- **Zero Orphan FormID Leaks**: Newly spawned mortals are staged with `committed = false`. They are promoted to `committed = true` (along with any MORWHA-spawned `donorA`/`donorB` ancestors) only when the player actually saves the game. Reloading/quitting without saving purges uncommitted `DPF.esp` slots automatically, and boot-time garbage collection (`mspawn gc`) reclaims slots from deleted saves across `<My Games>/Skyrim Special Edition/Saves/`.

### `AgeBracket` Resolution & Override Precedence
`AgeBracket` (`Auto`, `YoungAdult`, `Mature`, `MiddleAged`, `Elder`) resolves **two** traits automatically during synthesis:
1. **Skin Complexity (`faceTextureSet` + Age Tints + Hair Silvering)**:
   - `Auto` resolves to `YoungAdult` for `TwoDonorOffspring` (e.g., graduated adult children) and selects an adult age bracket automatically for `ZeroDonorProcedural` and `OneDonorRelative`.
   - Selects age-appropriate `faceDetailsTextureSets` for the target `(TESRace, Sex)`, applies age-graded facial tint layers, and blends silver hair pigment on `Elder`.
   - **Override rule**: Passing an explicit `faceTextureSet` overrides automatic complexion selection.
2. **Voice Type (`voiceType`)**:
   - Maps `(TESRace, Sex, AgeBracket)` to curated `Skyrim.esm` adult `BGSVoiceType` pools (using young non-raspy voices `YoungEager` / `EvenToned` / `Commoner` for `YoungAdult` across Khajiit, Argonian, and Chimera lineages while preserving racial Khajiit/Argonian voices on `Mature`+, and grading human/mer/orc voices by age) and randomly picks one using the mortal's seed.
   - Enforces `VOICE_TYPE_DATA::Flag::kFemale == (sex == Female)` on every pool candidate so male and female voices never cross-assign.
   - **Override rule**: Passing an explicit `voiceType` always overrides automatic age-group voice selection.

---

## 2. C++ SKSE Public C ABI (`MORWHA_PublicAPI.h` v1)

Copy [`MORWHA_PublicAPI.h`](../SKSE_Source/modules/bridge_c_api/include/MORWHA_PublicAPI.h) into your SKSE plugin's include path. No `.lib` linking is required — resolve exports at runtime via `GetModuleHandleW(L"MORWHA.dll")` and `GetProcAddress`.

### 2.1. Zero-Link Loader Snippet

```cpp
#include "MORWHA_PublicAPI.h"
#include <Windows.h>

struct MorwhaApi {
    decltype(&MORWHA_GetApiVersion)     GetApiVersion{ nullptr };
    decltype(&MORWHA_IsReady)           IsReady{ nullptr };
    decltype(&MORWHA_SpawnMortal)       SpawnMortal{ nullptr };
    decltype(&MORWHA_UpdateMortal)      UpdateMortal{ nullptr };
    decltype(&MORWHA_RerollMortal)      RerollMortal{ nullptr };
    decltype(&MORWHA_GetMortalHeritage) GetMortalHeritage{ nullptr };

    [[nodiscard]] bool Connect() noexcept {
        if (SpawnMortal) {
            return true;
        }
        const HMODULE mod = ::GetModuleHandleW(L"MORWHA.dll");
        if (!mod) {
            return false;
        }
        GetApiVersion     = reinterpret_cast<decltype(GetApiVersion)>(::GetProcAddress(mod, "MORWHA_GetApiVersion"));
        IsReady           = reinterpret_cast<decltype(IsReady)>(::GetProcAddress(mod, "MORWHA_IsReady"));
        SpawnMortal       = reinterpret_cast<decltype(SpawnMortal)>(::GetProcAddress(mod, "MORWHA_SpawnMortal"));
        UpdateMortal      = reinterpret_cast<decltype(UpdateMortal)>(::GetProcAddress(mod, "MORWHA_UpdateMortal"));
        RerollMortal      = reinterpret_cast<decltype(RerollMortal)>(::GetProcAddress(mod, "MORWHA_RerollMortal"));
        GetMortalHeritage = reinterpret_cast<decltype(GetMortalHeritage)>(::GetProcAddress(mod, "MORWHA_GetMortalHeritage"));
        return GetApiVersion && IsReady && GetApiVersion() == kMORWHA_ApiVersion && SpawnMortal != nullptr;
    }
};
```

### 2.2. `structSize` Validation & Sentinel Conventions
- Every parameter/result struct starts with `std::uint32_t structSize{ sizeof(StructType) };`. Always default-initialize structs (`MORWHA_SpawnParams params{};`) so `structSize` and sentinel defaults are populated automatically.
- `MORWHA.dll` validates exact `caller->structSize == sizeof(T)` (`kMORWHA_ApiVersion = 1`).
- **FormID Sentinels** (`MORWHA_ArchetypeParams`):
  - `0` = leave auto-resolved (on spawn) or unchanged (on update/reroll).
  - `kMORWHA_ClearFormId` (`0xFFFFFFFFu`) = explicitly clear an optional form reference (e.g., `sleepOutfitFormId`, `crimeFactionFormId`) during `MORWHA_UpdateMortal`.
- **Signed Byte Sentinels** (`MORWHA_AiParams`, `MORWHA_StatsParams`, `MORWHA_UpdateParams`):
  - `-1` = leave default (on spawn) or unchanged (on update/reroll).
  - `0` / `1` (for boolean fields `isProtected`, `isEssential`, `autoCalcStats`, `pcLevelMult`, `forcePersist`) = explicitly set `false` / `true`.

### 2.3. Exported C Functions

| Function | Signature | Description |
| :--- | :--- | :--- |
| `MORWHA_GetApiVersion` | `uint32_t MORWHA_GetApiVersion()` | Returns `kMORWHA_ApiVersion` (`1`). |
| `MORWHA_IsReady` | `bool MORWHA_IsReady()` | Returns `true` once `kDataLoaded` indexing, GC, and slot hydration have finished. |
| `MORWHA_SpawnMortal` | `bool MORWHA_SpawnMortal(const MORWHA_SpawnParams* params, MORWHA_SpawnResult* outResult)` | Synthesizes, persists, hydrates, and spawns a unique `DPF.esp` `TESNPC` actor at `anchorRefFormId`. `outResult` may be `nullptr`. |
| `MORWHA_UpdateMortal` | `bool MORWHA_UpdateMortal(const MORWHA_UpdateParams* params)` | Modifies name, `ageBracket`, archetype forms, AI flags, stats, perks, spells, or factions on an existing MORWHA actor in-place and saves to `DynamicNPCRegistry.json`. |
| `MORWHA_RerollMortal` | `bool MORWHA_RerollMortal(const MORWHA_RerollParams* params)` | Re-rolls facial/body appearance (and age-pool voice) from stored donor provenance while preserving existing class, outfits, AI, stats, perks, spells, and factions unless overridden. |
| `MORWHA_GetMortalHeritage` | `bool MORWHA_GetMortalHeritage(uint32_t targetFormId, MORWHA_HeritageInfo* outInfo)` | Queries mixed-race heritage, macro-group cross, directional `<Base><Secondary>` race pair, and Tamrielic lore naming for a MORWHA-spawned `Actor` **or** `DPF.esp` base `TESNPC` FormID. |

### 2.4. Struct Field Reference

#### `MORWHA_SpawnParams` & `MORWHA_SpawnResult`
| Field | Type | Default | Semantics |
| :--- | :--- | :--- | :--- |
| `donorMode` | `MORWHA_DonorMode` | `ZeroDonorProcedural` (`0`) | `0` = Procedural, `1` = `OneDonorRelative` (`donorAFormId`), `2` = `TwoDonorOffspring` (`donorAFormId` + `donorBFormId`). |
| `targetSex` | `int8_t` | `-1` | `0` = Male, `1` = Female, `-1` = Random (`50/50` seeded). |
| `ageBracket` | `MORWHA_AgeBracket` | `Auto` (`0`) | `0` = Auto, `1` = `YoungAdult`, `2` = `Mature`, `3` = `MiddleAged`, `4` = `Elder`. |
| `forcePersist` | `uint8_t` | `1` | Passed to `PlaceObjectAtMe(..., forcePersist != 0)` and persisted on the spec. |
| `anchorRefFormId` | `uint32_t` | `0` | **Required.** Live `RE::Actor` FormID where the mortal is placed. |
| `raceFormId` | `uint32_t` | `0` | Optional `TESRace` override (`0` = anchor race for 0-donor, donor inheritance/chimera plan for 1/2-donor). |
| `donorAFormId` / `donorBFormId` | `uint32_t` | `0` | Live `RE::Actor` **or** base `RE::TESNPC` FormIDs. Must be distinct for `TwoDonorOffspring`. |
| `seed` | `uint64_t` | `0` | Deterministic synthesis seed (`0` = auto-generate unique 64-bit seed). |
| `displayName` | `const char*` | `nullptr` | UTF-8 full name (`nullptr`/empty uses mode default). |
| `slotKeyPrefix` | `const char*` | `nullptr` | Prefix for `slotKey` (`"<prefix>_NNNN"`, sanitized to `[A-Za-z0-9_-]`, e.g., `"maras_child"`). |
| `archetype` / `ai` / `stats` | `const MORWHA_*Params*` | `nullptr` | Optional nullable pointers to extension blocks below. |

`MORWHA_SpawnResult` populates `actorFormId`, `baseNpcFormId` (the unique `DPF.esp` `TESNPC`), `resolvedRaceFormId`, `resolvedSex` (`0`=Male, `1`=Female), `resolvedAgeBracket`, and NUL-terminated `slotKey[64]`.

#### Extension Blocks (`archetype`, `ai`, `stats`)
- **`MORWHA_ArchetypeParams`**:
  - `voiceTypeFormId` (`BGSVoiceType` — overrides `AgeBracket` voice when non-zero)
  - `npcClassFormId` (`TESClass` — defaults to `Citizen` `0x01317E` if unset)
  - `combatStyleFormId` (`TESCombatStyle` — defaults to `csDefault` `0x003D08` if unset; synced to `ExtraCombatStyle`)
  - `defaultOutfitFormId` (`BGSOutfit` — defaults to `FarmClothesOutfit01` `0x028A72` if unset)
  - `sleepOutfitFormId` (`BGSOutfit`)
  - `crimeFactionFormId` (`TESFaction`)
  - `faceTextureSetFormId` (`BGSTextureSet` — overrides `AgeBracket` complexion when non-zero)
- **`MORWHA_AiParams`**:
  - `aggression` (`0` Unaggressive .. `3` Frenzied; default `0`)
  - `confidence` (`0` Cowardly .. `4` Foolhardy; default `2` Average)
  - `assistance` (`0` HelpsNobody .. `2` HelpsFriendsAndAllies; default `1` HelpsAllies)
  - `morality` (`0` AnyCrime .. `3` NoCrime; default `3`)
  - `isProtected` (`0`/`1`; default `0`), `isEssential` (`0`/`1`; default `0`)
- **`MORWHA_StatsParams`**:
  - `level` (default `10`), `calcLevelMin` (default `1`), `calcLevelMax` (default `50`), `autoCalcStats` (`0`/`1`, default `1`), `pcLevelMult` (`0`/`1`, default `0`)
  - `health`, `magicka`, `stamina` (`uint16_t`, default `50` each; pushed directly to live `ActorValueOwner` when `autoCalcStats == 0`)
  - `skills[18]` (`uint8_t`, `0` = keep default `15`/unchanged; indices match `RE::TESNPC::Skills`):
    `0` OneHanded, `1` TwoHanded, `2` Marksman/Archery, `3` Block, `4` Smithing, `5` HeavyArmor, `6` LightArmor, `7` Pickpocket, `8` Lockpicking, `9` Sneak, `10` Alchemy, `11` Speechcraft, `12` Alteration, `13` Conjuration, `14` Destruction, `15` Illusion, `16` Restoration, `17` Enchanting.
  - `perkFormIds` + `perkCount` (`<= 64`, `replacePerks = 1` with `perkCount = 0` clears perks on update)
  - `spellFormIds` + `spellCount` (`<= 64`, `replaceSpells = 1` with `spellCount = 0` clears spells on update)
  - `factions` (`MORWHA_FactionRank{ factionFormId, rank }`) + `factionCount` (`<= 32`, `rank >= -1`)

### 2.5. Full C++ Example (Spawning an Adult Offspring Warrior)

```cpp
MORWHA_ArchetypeParams arch{};
arch.npcClassFormId      = 0x00013176; // CombatWarrior1H
arch.combatStyleFormId   = 0x0003BE1C; // csHumanMelee1H
arch.defaultOutfitFormId = 0x00037C4F; // ArmorSteelOutfit
// voiceTypeFormId left 0 -> MORWHA picks a sex-checked YoungAdult voice for the child's race!

MORWHA_AiParams ai{};
ai.confidence  = 4; // Foolhardy
ai.assistance  = 2; // Helps Friends and Allies
ai.isProtected = 1;

const std::uint32_t perks[] = { 0x000BABE4 }; // Armsman01
const MORWHA_FactionRank factions[] = {
    { 0x0005C84D, 0 },  // PotentialFollowerFaction
    { 0x0005C84E, -1 }, // CurrentFollowerFaction (dormant rank -1)
};

MORWHA_StatsParams stats{};
stats.level         = 20;
stats.autoCalcStats = 0;
stats.health        = 220;
stats.stamina       = 180;
stats.skills[0]     = 65; // OneHanded
stats.skills[3]     = 55; // Block
stats.skills[5]     = 60; // HeavyArmor
stats.perkFormIds   = perks;
stats.perkCount     = 1;
stats.factions      = factions;
stats.factionCount  = 2;

MORWHA_SpawnParams spawn{};
spawn.donorMode       = MORWHA_DonorMode::TwoDonorOffspring;
spawn.targetSex       = 1; // Female
spawn.ageBracket      = MORWHA_AgeBracket::YoungAdult;
spawn.anchorRefFormId = 0x00000014; // Player
spawn.donorAFormId    = parentAFormId; // Actor OR TESNPC FormID
spawn.donorBFormId    = parentBFormId; // Actor OR TESNPC FormID
spawn.displayName     = "Freydis Battle-Born";
spawn.slotKeyPrefix   = "maras_child";
spawn.archetype       = &arch;
spawn.ai              = &ai;
spawn.stats           = &stats;

MORWHA_SpawnResult result{};
if (morwha.Connect() && morwha.IsReady() && morwha.SpawnMortal(&spawn, &result)) {
    // result.actorFormId, result.baseNpcFormId, result.slotKey are ready for quest/alias binding
}
```

---

## 3. Papyrus & JSON API (`MORWHA.psc`)

[`MORWHA.psc`](../Scripts/Source/MORWHA.psc) exposes simple positional helpers for common spawns plus JSON entrypoints (`SpawnMortalJson` / `UpdateMortalJson`) for full archetype, AI, and RPG configuration without 25-argument Papyrus signatures.

### 3.1. Native Global Functions

```papyrus
Scriptname MORWHA Hidden

string Function GetVersion() Global Native
bool Function IsReady() Global Native

Actor Function SpawnTestMortal(Actor akAnchor, Race akRace = None, int aiSex = 1, string asName = "Test Mortal") Global Native
Actor Function SpawnRelativeMortal(Actor akAnchor, Actor akDonor, Race akRace = None, int aiSex = -1, string asName = "Relative Mortal") Global Native
Actor Function SpawnOffspringMortal(Actor akAnchor, Actor akParentA, Actor akParentB, Race akRace = None, int aiSex = -1, string asName = "Offspring Mortal") Global Native
bool Function RerollMortal(Actor akTarget, Race akRaceOverride = None, bool abRerollRaceFromDonors = false, int aiSexOverride = -1, string asNameOverride = "") Global Native

Actor Function SpawnMortalJson(Actor akAnchor, string asOptionsJson) Global Native
bool Function UpdateMortalJson(Actor akTarget, string asPatchJson) Global Native
string Function GetMortalHeritageJson(Actor akTarget) Global Native
```

### 3.2. Accepted Form Reference Formats in JSON
Every form field (`race`, `donorA`/`parentA`, `donorB`/`parentB`, `voiceType`, `npcClass`, `combatStyle`, `defaultOutfit`, `sleepOutfit`, `crimeFaction`, `faceTextureSet`, `perks[]`, `spells[]`, `factions[].faction`) accepts **any** of:
1. **Load-order-independent pipe string** *(recommended)*: `"Skyrim.esm|0x013176"` or `"MyMod.esp|000800"`
2. **Object pair**: `{"plugin": "Skyrim.esm", "localId": "0x013176"}`
3. **Runtime FormID** (from `form.GetFormID()` in Papyrus): integer `78198` or hex string `"0x00013176"`
4. **Clear sentinel** (in `UpdateMortalJson`): `null`, `0`, `""`, or `"none"` clears an optional form reference.

### 3.3. JSON Schema (`SpawnMortalJson` & `UpdateMortalJson`)

```json
{
  "name": "Erik the Younger",
  "slotKeyPrefix": "my_quest_npc",
  "donorMode": "TwoDonorOffspring",
  "donorA": "0x00000014",
  "donorB": "Skyrim.esm|0x01A69A",
  "race": null,
  "sex": "Male",
  "ageBracket": "YoungAdult",
  "forcePersist": true,
  "seed": "0x0",
  "archetype": {
    "voiceType": null,
    "npcClass": "Skyrim.esm|0x013176",
    "combatStyle": "Skyrim.esm|0x03BE1C",
    "defaultOutfit": "Skyrim.esm|0x037C4F",
    "sleepOutfit": "Skyrim.esm|0x01697D",
    "crimeFaction": "Skyrim.esm|0x0267EA",
    "faceTextureSet": null
  },
  "ai": {
    "aggression": 0,
    "confidence": 4,
    "assistance": 2,
    "morality": 3,
    "isProtected": true,
    "isEssential": false
  },
  "stats": {
    "level": 20,
    "calcLevelMin": 1,
    "calcLevelMax": 50,
    "autoCalcStats": false,
    "pcLevelMult": false,
    "health": 220,
    "magicka": 80,
    "stamina": 180,
    "skills": {
      "oneHanded": 70,
      "block": 55,
      "heavyArmor": 60,
      "archery": 45
    }
  },
  "perks": ["Skyrim.esm|0x0BABE4"],
  "spells": ["Skyrim.esm|0x012FCC"],
  "factions": [
    { "faction": "Skyrim.esm|0x05C84D", "rank": 0 }
  ]
}
```

- **Convenience Flattening**: `voiceType`, `npcClass`, `combatStyle`, `defaultOutfit`, `sleepOutfit`, `crimeFaction`, `faceTextureSet`, `perks`, `spells`, and `factions` may be placed either at the top level or inside `"archetype"` / `"stats"`.
- **Auto Donor Mode Inference**: If `"donorMode"` is omitted in `SpawnMortalJson`, supplying both `donorA` (`parentA`) and `donorB` (`parentB`) selects `TwoDonorOffspring`; supplying `donorA` alone selects `OneDonorRelative`; omitting both selects `ZeroDonorProcedural`.
- **Named Skill Keys** (`stats.skills` accepts either an 18-element array `[oneHanded..enchanting]` or a sparse object with case-insensitive keys):
  `oneHanded`, `twoHanded`, `archery` (or `marksman`), `block`, `smithing`, `heavyArmor`, `lightArmor`, `pickpocket`, `lockpicking`, `sneak`, `alchemy`, `speech` (or `speechcraft`), `alteration`, `conjuration`, `destruction`, `illusion`, `restoration`, `enchanting`.
- **`UpdateMortalJson` Extras**: Supports `"refreshOutfit": true` to immediately equip a newly assigned `defaultOutfit` onto the live actor, and `"ageBracket": "Mature"|"MiddleAged"|"Elder"` to age an existing mortal's complexion, voice, and tint layers in-place.

### 3.4. Papyrus Usage Example

```papyrus
Actor player = Game.GetPlayer()
if MORWHA.IsReady()
    string opts = "{\"name\":\"Runa\",\"donorA\":" + player.GetFormID() + ",\"donorB\":\"Skyrim.esm|0x01A69A\",\"sex\":\"Female\",\"ageBracket\":\"YoungAdult\",\"npcClass\":\"Skyrim.esm|0x013179\",\"ai\":{\"confidence\":3,\"isProtected\":true}}"
    Actor daughter = MORWHA.SpawnMortalJson(player, opts)

    if daughter
        ; Later in gameplay: promote her outfit and refresh worn gear
        MORWHA.UpdateMortalJson(daughter, "{\"defaultOutfit\":\"Skyrim.esm|0x037C4F\",\"refreshOutfit\":true}")
    endif
endif
```

---

### 3.5. Runtime Mixed-Heritage `BGSKeyword`s & Lore Naming (`MORWHA_GetMortalHeritage` / `GetMortalHeritageJson`)

At `kDataLoaded`, M.O.R.W.H.A pre-registers 115 ESP-less runtime `RE::BGSKeyword` forms in `TESDataHandler` and `TESForm::GetAllFormsByEditorID()` (queryable immediately via `Actor.HasKeywordString(...)`, `Keyword.GetKeyword(...)`, C++ `RE::TESForm::LookupByEditorID<RE::BGSKeyword>(...)`, OAR, SkyPatcher, and SkyrimNet) without modifying `TESRace`:

- **Canonical Race Stems**: `Nord`, `Imperial`, `Breton`, `Redguard`, `Altmer` (*High Elf*), `Dunmer` (*Dark Elf*), `Bosmer` (*Wood Elf*), `Orsimer` (*Orc*), `Khajiit`, `Argonian` (plus PascalCase stems for custom playable races).
- **Attached Keywords on Hydrated `TESNPC`s**:
  - `MORWHA_SpawnedMortal` *(all MORWHA mortals)* and `MORWHA_MixedHeritage` *(two-parent offspring whose parents have different biological races)*.
  - `MORWHA_Ancestry_<Race>`: attached for the primary race (and secondary parent race when mixed, e.g. `MORWHA_Ancestry_Orsimer` + `MORWHA_Ancestry_Khajiit`).
  - `MORWHA_Group_<GroupA><GroupB>` *(ordered `Man < Mer < Beast`, no separator)*: `MORWHA_Group_ManMan`, `MORWHA_Group_MerMer`, `MORWHA_Group_ManMer`, `MORWHA_Group_ManBeast`, `MORWHA_Group_MerBeast`, `MORWHA_Group_BeastBeast`.
  - `MORWHA_Mix_<BaseRace><SecondaryRace>` *(directional — resolved base `TESRace` first, secondary parent race second, no separator)*: e.g. `MORWHA_Mix_NordRedguard`, `MORWHA_Mix_AltmerDunmer`, `MORWHA_Mix_OrsimerKhajiit`, `MORWHA_Mix_KhajiitArgonian` (*scaly/horned Khajiit*), `MORWHA_Mix_ArgonianKhajiit` (*fur-tufted Argonian*).
  - **Beast Chimera & Lore Tags**:
    - `MORWHA_Chimera_Tier2` + `MORWHA_Heritage_OhmesKin` (`loreName = "Ohmes-kin"` for `Man/Mer` base + `Khajiit`), `MORWHA_Heritage_HistKin` (`"Hist-kin"` for `Man/Mer` base + `Argonian`), `MORWHA_Heritage_HistRaht` (`"Hist-raht"` for `Khajiit` base + `Argonian`), or `MORWHA_Heritage_MoonScale` (`"Moon-scale"` for `Argonian` base + `Khajiit`).
    - `MORWHA_Chimera_Tier1` when a Beast pairing resolves via Tier-1 single-parent inheritance (`loreName` stays `<Primary>-<Secondary>` without claiming Tier-2 hybrid anatomy).
    - `MORWHA_Heritage_Manmer` (`loreName = "Half-<Mer>"`, e.g. `"Half-Altmer"`, `"Half-Orsimer"`) on `ManMer` pairings.

---

## 4. Declarative Asset Extension Packs (Zero Code Required)

Third-party mods can expand M.O.R.W.H.A's procedural face anchors and Tier-2 beast chimera accessories simply by shipping loose JSON / `.jslot` files under `Data/SKSE/Plugins/MORWHA/`.

### 4.1. Procedural Face Anchors (`Data/SKSE/Plugins/MORWHA/FaceAnchors/`)
Drop slider-only RaceMenu `.jslot` presets under:
```text
Data/SKSE/Plugins/MORWHA/FaceAnchors/<raceKey>/<female|male>/<preset_name>.jslot
```
- Supported `<raceKey>` folders: `nord`, `imperial`, `breton`, `redguard`, `highelf`, `darkelf`, `woodelf`, `orc`, `khajiit`, `argonian`, or `humanoid` (shared fallback pool).
- Presets containing vertex-sculpt payloads (`vertex` / sculpt `.nif` blobs) are automatically skipped so morph synthesis stays topology-safe across Vanilla and High Poly Head.

### 4.2. Tier-2 Chimera Accessories (`Data/SKSE/Plugins/MORWHA/ChimeraAccessories/*.json`)
Registers RaceMenu extended `BGSHeadPart` ears/horns/teeth or `TESObjectARMA` horn addons for `Humanoid × Khajiit`, `Humanoid × Argonian`, and cross-beast offspring when the target `plugin` is active in load order:

```json
{
  "schemaVersion": 1,
  "plugin": "MyBeastPack.esp",
  "requiresSkee64": true,
  "entries": [
    {
      "lineage": "HumanoidKhajiit",
      "slotKind": "HeadPart",
      "sex": "Any",
      "localId": "0x000801"
    },
    {
      "lineage": "HumanoidArgonian",
      "slotKind": "SkinArmorAddon",
      "sex": "Female",
      "localId": "0x000802"
    }
  ]
}
```
- `lineage`: `"HumanoidKhajiit"`, `"HumanoidArgonian"`, or `"AnyChimera"`.
- `slotKind`: `"HeadPart"` (`BGSHeadPart`) or `"SkinArmorAddon"` (`TESObjectARMA` skinned to `NPC Head [Head]`).
- `sex`: `"Any"`, `"Male"`, or `"Female"`.
