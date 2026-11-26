# M.O.R.W.H.A — Mortals On-Demand: Runtime World-Spawning, Heads & Appearance

**M.O.R.W.H.A** is an ESP-less C++23 SKSE64 framework for Skyrim Special Edition / Anniversary Edition that synthesizes visually unique, persistent `RE::TESNPC` records and in-memory FaceGen heads at runtime — without static dummy `ActorBase` pools or prebaked head `.nif` / `.dds` exports.

---

## Core Capabilities

- **Zero-Donor Procedural NPCs**: Generates balanced, non-caricature unique `TESNPC` records from `(race, sex, ageBracket, seed)` with automatic age-graded skin complexions and sex-checked `Skyrim.esm` voice pools.
- **One-Donor Relatives & Two-Donor Offspring**: Extracts parental traits from live `Actor`s or base `TESNPC` records across `Vanilla` and `High Poly Head` topologies to synthesize family resemblances, stature, skin, hair, and iris inheritance.
- **2-Tier Beast & Beast-Cross Chimeras**: Supports Tier-1 dominant-parent inheritance and Tier-2 native-race 4-map `.mrst` hybrid fur/scale skins + beast ears/horns/tails/slit-eyes/sharp-teeth across Humanoid/Mer × `{Khajiit, Argonian}` and `Khajiit × Argonian` pairings (cascading automatically `Tier 2 -> Tier 1` per lineage), with auto-detected body UV support (`CBBE`/`3BA`/`TBD`, `BHUNP`/`UNP`, `Vanilla` F/M, `UBE 2.0`, `HIMBO`/`TNG`/`SOS`).
- **ESP-Less Mixed-Heritage Keywords & Lore Naming**: Pre-registers runtime `MORWHA_*` `BGSKeyword`s (`MORWHA_Group_*`, directional `MORWHA_Mix_<Base><Secondary>`, `MORWHA_Ancestry_*`, `MORWHA_Chimera_Tier*`, and `MORWHA_Heritage_*`) directly onto hydrated `TESNPC`s without touching `TESRace`.
- **Commit-on-Save Persistence & Cross-Save GC**: Reserves persistent `DPF.esp` `TESNPC` slots via **Dynamic Persistent Forms REthinked (`DPF RE`)**, hydrates full appearance, classes, combat styles, outfits, AI flags, stats, perks, spells, and factions from `DynamicNPCRegistry.json`, and automatically garbage-collects unsaved or deleted-save slots so FormIDs never leak across playthroughs.
- **Modder-Ready API**: Versioned zero-link C ABI (`MORWHA_PublicAPI.h` v1 via `GetProcAddress`) and native Papyrus / JSON bindings (`MORWHA.psc`) for consumer mods such as **M.A.R.A.S**, SkyrimNet world populators, and quest mods.

---

## Requirements

### 1. Core Hard Requirements
- **[Skyrim Script Extender (SKSE64)](https://www.nexusmods.com/skyrimspecialedition/mods/30379)** *(SE 1.5.97 or AE 1.6.x)*
- **[Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444)**
- **[Dynamic Persistent Forms REthinked - DPF RE](https://www.nexusmods.com/skyrimspecialedition/mods/183427)** (`DPF.dll` + `DPF.esp` — **`DPF.esp` must remain uncompacted and non-ESL**)
- **[Face Discoloration Fix](https://www.nexusmods.com/skyrimspecialedition/mods/42441)** (`FaceGenFixes.dll` — prevents dark/grey runtime heads)

### 2. Tier-2 Beast Chimera Prerequisites *(Required for Tier-2 Tails, Ears & Horns)*
- **[XP32 Maximum Skeleton Special Extended (XPMSSE)](https://www.nexusmods.com/skyrimspecialedition/mods/1988)** — provides `TailBone01`–`TailBone05` on Humanoid/Mer skeletons so beast tails animate cleanly without crashing.
- **[RaceMenu](https://www.nexusmods.com/skyrimspecialedition/mods/19080)** (`skee64.dll`) — required to render extended `BGSHeadPart` slots (cat ears & horns) on NPCs.

### 3. Recommended Optional Cosmetic & Chimera Mods *(Auto-Detected)*
- **Heads & Hair Physics**: **High Poly Head** (`High Poly Head.esm` — supported alongside Vanilla heads) and **[Faster HDT-SMP](https://www.nexusmods.com/skyrimspecialedition/mods/57339)** (`hdtSMP64.dll` — unlocks SMP physics hairstyles and SMP beast tails).
- **Cat Ears (`Humanoid/Mer × Khajiit`)**: **[Chooey's Dint Ears Edit](https://www.nexusmods.com/skyrimspecialedition/mods/133451)** (`ChooeyDintEarsEdit.esp`).
- **Reptilian Horns (`Humanoid/Mer × Argonian` & `Khajiit × Argonian`)**: **[Horns Aplenty](https://www.nexusmods.com/skyrimspecialedition/mods/41022)** (`NK_HornSlider.esp`) and/or **[Wearable Horns - SE](https://www.nexusmods.com/skyrimspecialedition/mods/54531)** (`PraedysHorns.esp`).
- **Slit Beast Eyes**: **[Feral Eyes](https://www.nexusmods.com/skyrimspecialedition/mods/73325)** and/or **[The Eyes Of Beauty SSE](https://www.nexusmods.com/skyrimspecialedition/mods/16185)**.
- **Sharp Beast Teeth**: **[BnP - Teeth Overhaul](https://www.nexusmods.com/skyrimspecialedition/mods/84288)** + **[RSV - Teeth Variance](https://www.nexusmods.com/skyrimspecialedition/mods/81668)** and/or **[Sharpened Teeth - Character Creation Addon](https://www.nexusmods.com/skyrimspecialedition/mods/71850)**.

*(First-boot note: on your very first launch — or after switching body/skin mods — main-menu load pauses ~10–25 seconds once while M.O.R.W.H.A caches 4-map hybrid skins into `Textures/Actors/Character/MORWHA/Generated/`. Subsequent launches verify the cache in `<15 ms`. See [docs/CHIMERA_REQUIREMENTS.md](docs/CHIMERA_REQUIREMENTS.md) for `MORWHA.ini` options.)*

---

## Installation (Mod Organizer 2 / Vortex)

M.O.R.W.H.A is split into two archives so routine framework updates stay ~1 MB while the 3D skin stencils only need to be downloaded once:

1. **Install Hard Requirements**:
   - Install **SKSE64**, **Address Library for SKSE Plugins**, **Face Discoloration Fix**, and **Dynamic Persistent Forms REthinked (`DPF RE`)**.
   - Make sure **`DPF.esp`** is enabled in your plugin load order and **never** ESL-flagged or compacted (M.O.R.W.H.A itself is ESP-less and adds `0` `.esp` plugins).
2. **Install `M.O.R.W.H.A` (Core Framework)**:
   - Install `MORWHA_vX.Y.Z.zip` via Mod Organizer 2 or Vortex and enable it.
3. **Install `M.O.R.W.H.A - Hybrid Skin Stencils` *(Recommended — for Tier-2 Beast Chimeras)***:
   - Install `MORWHA_SkinStencils_v1.0.0.zip` as a **separate mod entry** in MO2 / Vortex alongside **XPMSSE** and **RaceMenu**.
   - No FOMOD options are required: M.O.R.W.H.A automatically detects your active female (`CBBE`/`3BA`/`TBD`, `BHUNP`/`UNP`, `Vanilla`, `UBE 2.0`) and male (`HIMBO`/`TNG`/`SOS`, `Vanilla`) body meshes from VFS at startup.
   - **Updating later**: When updating M.O.R.W.H.A to a newer version, you only need to update the small Core mod — leave `M.O.R.W.H.A - Hybrid Skin Stencils` installed as-is.
4. **MO2 `Overwrite` / Runtime Output Tip**:
   - At runtime, M.O.R.W.H.A writes `MORWHA.ini`, `DynamicNPCRegistry.json`, `GeneratedSkinCache.json`, and cached `Textures/Actors/Character/MORWHA/Generated/*.dds` maps into MO2's `Overwrite` folder (or a dedicated empty mod if you redirect MO2 outputs). **Keep `DynamicNPCRegistry.json` alongside `DPF_Cache.bin`** across updates so existing spawned NPCs retain their saved appearances.
   - To verify your installation in-game, open the console (`~`) and run `mspawn status`.

---

## Quick-Start Console Commands (`mspawn`)

All console commands are case-insensitive. Optional age tokens: `young`, `mature`, `middle`, `elder`.

### 1. No Donor (Procedural Random Mortal)
```text
mspawn
mspawn female Nord
mspawn male Altmer elder "Aurelius"
```

### 2. Player + NPC (Child of Player & Target NPC)
Open the console (`~`) and **click on** (or look at) an NPC, then run:
```text
mspawn child
mspawn child female "Runa"
mspawn child male young
```
*(Or pass the NPC's FormID directly without clicking: `mspawn child 0001A69A female`.)*

### 3. NPC + NPC (Child of Two Specific NPCs)
- **Click NPC #1 in the console + pass NPC #2's FormID:**
  ```text
  mspawn child 0001A69A
  mspawn child 0001A69A female "Freydis"
  ```
- **Or pass both NPC FormIDs directly:**
  ```text
  mspawn child 0001A697 0001C19E
  mspawn child 0001A697 0001C19E female "Heir"
  ```

### 4. Batch of 6 Offspring (`3 Male + 3 Female` from Random World Parents)
Combine **any two playable races** (`Nord`, `Imperial`, `Breton`, `Redguard`, `Altmer`/`HighElf`, `Dunmer`/`DarkElf`, `Bosmer`/`WoodElf`, `Orsimer`/`Orc`, `Khajiit`, `Argonian`):
```text
mspawn NordRedguard
mspawn AltmerDunmer
mspawn OrsimerKhajiit
mspawn KhajiitArgonian
mspawn NordNord mature
```

### Handy Testing Utilities
```text
mspawn reroll                  ; Re-roll clicked MORWHA NPC in place (keeps sex & race)
mspawn reroll anyrace anysex   ; Re-roll clicked MORWHA NPC including parent race & sex
mspawn status                  ; Print setup readiness, detected mods, and active settings
mspawn reload                  ; Hot-reload MORWHA.ini in-game
```
Full syntax reference: [docs/CONSOLE_COMMANDS.md](docs/CONSOLE_COMMANDS.md).

---

## Building & Testing

`MORWHA` uses Dual-Mode CMake: it builds `MORWHA-Core` from source automatically if `../MORWHA-Core` is present alongside `MORWHA Dev`, or links the prebuilt Release static archive in `SKSE_Source/external/morwha_core/lib/Release/` for standalone contributors.

```powershell
# Full build (header hygiene + unit tests + MORWHA.dll)
powershell -NoProfile -ExecutionPolicy Bypass -File ".agents/skills/morwha-build-and-test/scripts/build-and-test.ps1" -Target all

# Fast headless unit-test loop
powershell -NoProfile -ExecutionPolicy Bypass -File ".agents/skills/morwha-build-and-test/scripts/build-and-test.ps1" -Target tests
```

---

## Documentation & License

- [docs/CHIMERA_REQUIREMENTS.md](docs/CHIMERA_REQUIREMENTS.md) — Tier-2 chimera mod setup, body support, and `MORWHA.ini` reference
- [docs/CONSOLE_COMMANDS.md](docs/CONSOLE_COMMANDS.md) — Complete `mspawn` console command reference
- [docs/MODDER_API_REFERENCE.md](docs/MODDER_API_REFERENCE.md) — C++ Public C ABI v1, runtime `MORWHA_*` keywords, Papyrus / JSON API, and `.json`/`.jslot` extension packs
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — Module layout and persistence lifecycle
- [LICENSE](LICENSE) — Source-Available Modding License (with MIT License exception for `MORWHA_PublicAPI.h` and `MORWHA.psc`)
