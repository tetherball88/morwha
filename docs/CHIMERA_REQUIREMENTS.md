# M.O.R.W.H.A — Beast Chimera Installation Requirements

M.O.R.W.H.A supports two tiers for **Humanoid/Mer × `{Khajiit, Argonian}`** and **`Khajiit × Argonian`** offspring, cascading gracefully per lineage (`Tier 2 -> Tier 1`):

- **Tier 1 (`Tier1DominantMorph`) — Zero Extra Requirements**: Rolls dominant parent race 50/50 and synthesizes the child as a single-parent inheritance from the matching-race parent.
- **Tier 2 (`Tier2EquippedParts` — Default) — Native Base Race + 4-Map Hybrid Skins & Beast Features**:
  - **`Man × {Khajiit, Argonian}` & `Mer × {Khajiit, Argonian}`**: Spawns children on the **Humanoid/Mer** parent's playable race (preserving HPH/Vanilla lip & eye morph diversity, pointed elven ears, and Orc tusks) and generates 4-map hybrid `BGSTextureSet`s (`Face` + `Body+Feet` + `Hands` + `Tail` from MORWHA's shipped 3D `.mrst` stencils), private cloned `WNAM` `TESNPC::skin` tail addons, slit-pupil eyes, sharp beast teeth, and optional cat ears / curated horns.
  - **`Khajiit × Argonian` *(Beast-Cross Lineage)***: Rolls dominant beast parent 50/50 (`KhajiitRace` or `ArgonianRace`) and grafts the opposite beast parent's features — either a **scaly/horned Khajiit** (`ArgonianOnKhajiit` scales across Khajiit face, body, hands, feet + optional keratin-tinted Argonian horns + inherited parental eye color) or a **fur-tufted Argonian** (`KhajiitOnArgonian` pelage fur across Argonian cheek ruff/crown, body, hands, feet + inherited parental eye color).

If a lineage's Tier-2 hybrid skins and accessories are all disabled or absent, that lineage automatically falls back cleanly to **Tier 1**.

---

## 1. Core Framework Prerequisites (Required for Tier 2)

| Requirement | Mod / Plugin | Purpose |
| :--- | :--- | :--- |
| **XPMSSE** | [XP32 Maximum Skeleton Special Extended](https://www.nexusmods.com/skyrimspecialedition/mods/1988) (`XPMSE.esp`, `skeleton.nif`) | Provides `TailBone01`–`TailBone05` on Humanoid/Mer skeletons so Khajiit/Argonian tails animate cleanly without crashing. **Must not** be overwritten by older skeleton files from tail/accessory mods. |
| **RaceMenu** | [RaceMenu](https://www.nexusmods.com/skyrimspecialedition/mods/19080) (`skee64.dll`, `RaceMenu.esp`) | Required for NPCs to render extended `BGSHeadPart` slots (`PNAM = 32` horns and extra headpart ears) directly inside `BSFaceGenNiNode`. |

---

## 2. Supported Tier-2 Feature Mods

### A. Cat Ears (`Humanoid × Khajiit`)

| Mod | Plugin | Mechanism & Notes |
| :--- | :--- | :--- |
| **[Chooey's Dint Ears Edit](https://www.nexusmods.com/skyrimspecialedition/mods/133451)** | `ChooeyDintEarsEdit.esp` | Furry Dint999 cat ears implemented as morphable `BGSHeadPart`s with `HairTint` shader — zero armor slot usage, and ear fur automatically tints to the child's hair color. *(Note: includes `HeadpartWhitelist\ChooeyDintEarsEdit.ini` if using Powerof3/wSkeever's `HeadpartWhitelist`.)* |

### B. Reptilian & Draconic Horns (`Humanoid × Argonian` — install either or both)

| Mod | Plugin | Mechanism & Curation |
| :--- | :--- | :--- |
| **[Horns Aplenty - A RaceMenu Slider Mod](https://www.nexusmods.com/skyrimspecialedition/mods/41022)** | `NK_HornSlider.esp` | RaceMenu `PNAM = 32` `BGSHeadPart` horns & fins that morph with skull sliders. Requires `skee64.dll`. Curated via `Data/SKSE/Plugins/MORWHA/ChimeraAccessories/NK_HornSlider.json` to **Argonian & Dragon horns/fins only** (excludes deer antlers, elk/reindeer, cow, boar, goat, and helmet horns) and combined with `PraedysHorns.esp` into a unified horn pool when both are installed. Dynamically tinted on 3D load (`bMatchHornColor = true`) to match the Argonian parent's horn (`hairColor`) or secondary body/marking color. |
| **[Wearable Horns - SE](https://www.nexusmods.com/skyrimspecialedition/mods/54531)** | `PraedysHorns.esp` | Pooled uniformly alongside `NK_HornSlider.esp`. Requires `skee64.dll` (`requiresSkee64: true`); promotes each curated `PraedysHorns.esp` `TESObjectARMA` at runtime into a private `PNAM = 32` `BGSHeadPart` inside `BSFaceGenNiNode` (`MORWHA_HornHP_*`) — zero `Slot 55` or inventory `ARMO` usage, unlootable, and immune to outfit overrides (`Tailor`, `SPID`, `OStim`), with runtime `kDefault -> kFaceGenRGBTint` material tinting (compensated against horn texture brightness, `bMatchHornColor = true`). Curated via `Data/SKSE/Plugins/MORWHA/ChimeraAccessories/PraedysHorns.json` to **reptilian/draconic horns & spikes only** (excludes antlers). |

### C. Vertical-Slit Beast Eyes for Humanoids *(Auto-Detected — Recommended)*

M.O.R.W.H.A inspects pooled Humanoid `kEyes` `.dds` textures at load time to detect vertical-slit pupils and match the Beast parent's eye color and sclera tone across **any** installed eye mod. Recommended packs:

- **[Feral Eyes - Beast Eyes for Humanoid Races](https://www.nexusmods.com/skyrimspecialedition/mods/73325)** (`FeralEyes.esp`) — Cat, lizard, and snake slit eyes fitted to Human/Mer eye sockets (with light-sclera and dark-sclera variants).
- **[The Eyes Of Beauty SSE](https://www.nexusmods.com/skyrimspecialedition/mods/16185)** (`TheEyesOfBeauty.esp`) — Adds reptile, snake, dragon, and beast slit eyes alongside human eye packs.

### D. Beast Tails (`Humanoid × Khajiit` & `Humanoid × Argonian`)

- **Built-In Core (`Skyrim.esm`)**: Clones private runtime `TESObjectARMA`s (`MORWHA_ARMA_TailKhajiit`, `MORWHA_ARMA_TailArgonian`) from vanilla `NakedTailKhajiit` (`0x0B79B9`) and `NakedTailArgonian` (`0x069CE2`) (`Slot 40`), excludes any dormant `kTail` addon from vanilla `SkinNaked`, attaches them only to per-chimera `TESNPC::skin` (`WNAM`) forms on `XPMSSE` skeletons (automatically re-wrapping external SPID/`Racial Skin Variance - SPID` skin assignments so distributed body skins keep their textures alongside the chimera tail), and dynamically tints the tail mesh per actor on 3D load (`bMatchTailColor = true`, zero extra forms) to match Khajiit `hairColor` or Argonian main scale color — **zero vanilla race/record mutation**, unlootable, and unstrippable by outfits.
- **Optional Physics / Visual Replacers (Auto-Inherited)**:
  - **[Faster HDT-SMP](https://www.nexusmods.com/skyrimspecialedition/mods/57339)** (`hdtSMP64.dll`)
  - **[HDT SMP Beast Race Tail](https://www.nexusmods.com/skyrimspecialedition/mods/91811)** / **[longer HDT SMP fluffy beast race tail](https://www.nexusmods.com/skyrimspecialedition/mods/111629)** 

### E. Sharp Beast Teeth (`Humanoid × Khajiit` & `Humanoid × Argonian` — Optional)

| Mod | Plugin / VFS Assets | Behavior & Lineage Mapping |
| :--- | :--- | :--- |
| **[BnP - Teeth Overhaul](https://www.nexusmods.com/skyrimspecialedition/mods/84288)** + **BnP RSV Teeth Patch** / **[RSV - Teeth Variance](https://www.nexusmods.com/skyrimspecialedition/mods/81668)** | Loose/BSA VFS textures (no extra `.esp` required when textures are present) | Automatically binds `textures\actors\character\mouth\Sharp\mouthhuman.dds` (full row of sharp beast teeth) onto both `Humanoid × Khajiit` and `Humanoid × Argonian` mouths via in-memory mouth `BGSTextureSet` / `BGSHeadPart` cloning. Samples 50/50 with `Sharpened Teeth` when both are installed. |
| **[Sharpened Teeth - Character Creation Addon](https://www.nexusmods.com/skyrimspecialedition/mods/71850)** | `Sharpened Teeth - Character Creation.esp` | Assigns standalone playable sharp-teeth `Mouth` (`kMisc`, `PNAM = 0`) `BGSHeadPart`s (`0DV_SharpsFemale` `0x800`, `0DV_SharpsMale` `0x801`) across both `Humanoid × Khajiit` and `Humanoid × Argonian` chimeras (automatically normalizing `<prefix>\Meshes\` VFS model subpaths if loose meshes live at `Data\Meshes\mouthhuman*.nif`). |

### F. 4-Map Runtime Hybrid Skins *(Shipped — Built-In)*

| Mod | Plugin & Assets | Behavior & Lineage Mapping |
| :--- | :--- | :--- |
| **M.O.R.W.H.A - Hybrid Skin Stencils** *(Companion Asset Pack)* | `MORWHA_SkinStencils_v1.0.0.zip` (`SKSE/Plugins/MORWHA/SkinStencils/*.mrst`) | Separate one-time asset download containing 62 pre-baked `.mrst` hybrid skin stencils (`fur_tabby`, `fur_spotted`, `lizard_pebble`, `lizard_keeled` across `Face`, `Body+Feet`, and `Hands` for `HPH`/Vanilla Heads, Beast-Cross Heads, and automatically detected `CBBE`/`3BA`/`TBD`, `BHUNP`/`UNP`, `Vanilla`, `UBE` Female + `HIMBO`/`SOS`, `Vanilla`/`TNG` Male body meshes). Pre-generates & caches 4-map `BGSTextureSet`s (`.dds` + `_msn.dds` + `_s.dds` + `_sk.dds`) plus coat-matched tail diffuse sets at game start (`kDataLoaded`) in `Data/Textures/Actors/Character/MORWHA/Generated/` — respecting native installed 1K/2K/4K resolution per map (`sGeneratedSkinResolution = auto`), verified in `<15 ms` on subsequent launches via `GeneratedSkinCache.json`, with zero spawn-time wait and zero SKEE `[Ovl0]` mesh duplication. |

---

## 3. Configuration (`Data/SKSE/Plugins/MORWHA/MORWHA.ini`)

If no mod in your load order (nor MO2's `Overwrite`) already provides `Data/SKSE/Plugins/MORWHA/MORWHA.ini`, M.O.R.W.H.A automatically creates a commented default configuration file on startup into **MO2 `Overwrite\SKSE\Plugins\MORWHA\MORWHA.ini`**. Edits to `MORWHA.ini` can be hot-reloaded in-game via `mspawn reload` without restarting Skyrim.

### `[General]` & `[Chimeras]`
- **`[General]`**: `bAllowSupernaturalEyes` (`false`), `bAllowScars` (`true`), `fOffspringScarChance` (`0.02`), `fDefaultScarChance` (`0.04`).
- **`[Chimeras]`**:
  - `sBeastChimeraTier` (`tier2` default; accepts `off`/`0`, `tier1`/`1`, `tier2`/`2`; cascades automatically `tier2 -> tier1` per lineage and sex).
  - `bEnableGeneratedHybridSkins` (`true` default; pre-generates & caches 4-map `.mrst` hybrid skins at `kDataLoaded`).
  - `sGeneratedSkinResolution` (`auto` default; accepts `auto`, `1k` / `1024`, `2k` / `2048`, `4k` / `4096`). Never upscales: `auto` matches each installed VFS skin map's native resolution (`1K` / `2K` / `4K`), whereas `1k` / `2k` / `4k` downscales higher-res maps to the chosen cap while keeping lower-res maps at their native size.
- **`[ChimeraTier2.Khajiit]`**:
  - `bEnableEyes` (`true`) — prefer vertical-slit beast eyes; `false` uses normal humanoid eyes.
  - `bEnableEars` (`true`) — attach furry cat ear headparts (`ChooeyDintEarsEdit.esp`). When `false`, Tier 2 remains active without requiring cat ear mods as long as at least one Tier-2 feature is enabled.
  - `bEnableTeeth` (`true`) — apply sharp beast teeth (`BnP RSV` / `Sharpened Teeth`).
  - `bEnableTails` (`true`) — attach cloned Khajiit tail on `TESNPC::skin`.
  - `bMatchTailColor` (`true`) — dynamically tint the tail mesh when hybrid skins are disabled; `false` leaves the tail skin-tinted.
  - `bEnableHybridSkin` (`true`) — apply 4-map `.mrst` pelage fur hybrid skins.
- **`[ChimeraTier2.Argonian]`**:
  - `bEnableEyes` (`true`) — prefer vertical-slit beast eyes; `false` uses normal humanoid eyes.
  - `bEnableHorns` (`true`) — attach reptilian/draconic horns (`NK_HornSlider.esp` / promoted `PraedysHorns.esp`). When `false`, Tier 2 remains active without requiring horn mods as long as at least one Tier-2 feature is enabled.
  - `bMatchHornColor` (`true`) — dynamically tint horn meshes to match the Argonian parent's horn (`hairColor`) or secondary body/marking color; `false` keeps native horn textures/skin tint.
  - `bEnableTeeth` (`true`) — apply sharp beast teeth (`BnP RSV` / `Sharpened Teeth`).
  - `bEnableTails` (`true`) — attach cloned Argonian tail on `TESNPC::skin`.
  - `bMatchTailColor` (`true`) — dynamically tint the tail mesh when hybrid skins are disabled; `false` leaves the tail skin-tinted.
  - `bEnableHybridSkin` (`true`) — apply 4-map `.mrst` osteoderm/keeled hybrid skins.

*(Note: Disabling all features for a lineage reverts that lineage to Tier-1 dominant-parent inheritance when Tier 2 is requested.)*

---

## 4. Recommended Load-Order & MO2 Overwrite Checks

1. Keep **XP32 Maximum Skeleton Special Extended (`XPMSE`)** loaded cleanly so Humanoid `skeleton.nif` / `skeleton_female.nif` retain `TailBone01`–`TailBone05` and physics nodes.
2. Keep **RaceMenu (`skee64.dll`)** enabled when using **Chooey's Dint Ears Edit** or **NK_Horns Aplenty**.
3. If using **Horns Aplenty** alongside `HeadpartWhitelist`, keep `NK_HornSlider.esp` whitelisted if you also want manual RaceMenu previewing.
