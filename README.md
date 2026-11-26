# M.O.R.W.H.A — Mortals On-Demand: Runtime World-Spawning, Heads & Appearance

**M.O.R.W.H.A** is a standalone C++23 SKSE64 framework for Skyrim Special Edition / Anniversary Edition that synthesizes visually unique, persistent `RE::TESNPC` records and in-memory FaceGen heads at runtime — without static dummy `ActorBase` pools or prebaked `.nif` / `.dds` disk exports.

## Core Capabilities

- **Zero-Donor Procedural NPCs**: Generates balanced, non-caricature unique `TESNPC` records from `(race, sex, ageBracket, seed)` with automatic age-graded skin complexions and sex-checked `Skyrim.esm` voice pools.
- **One-Donor Relatives & Two-Donor Offspring**: Extracts parental traits from live `Actor`s or base `TESNPC` records across `Vanilla` and `High Poly Head` topologies to synthesize family resemblances, stature, skin, hair, and eye inheritance.
- **2-Tier Beast & Beast-Cross Chimeras**: Supports Tier-1 dominant-parent inheritance and Tier-2 native-race 4-map `.mrst` hybrid fur/scale skins + beast ears/horns/tails/slit-eyes/sharp-teeth across Humanoid/Mer × `{Khajiit, Argonian}` and `Khajiit × Argonian` pairings, cascading cleanly `Tier 2 -> Tier 1` per lineage.
- **Commit-on-Save Persistence & Cross-Save GC**: Reserves persistent `DPF.esp` `TESNPC` slots via **Dynamic Persistent Forms REthinked (`DPF RE`)**, hydrates full appearance, classes, combat styles, outfits, AI flags, stats, perks, spells, and factions from `DynamicNPCRegistry.json`, and automatically garbage-collects unsaved or deleted-save slots so FormIDs never leak across playthroughs.
- **Modder-Ready API**: Versioned zero-link C ABI (`MORWHA_PublicAPI.h` v1 via `GetProcAddress`) and native Papyrus / JSON bindings (`MORWHA.psc`) for consumer mods such as **M.A.R.A.S**, SkyrimNet world populators, and quest mods.

## Runtime Requirements

- **Skyrim Special Edition / Anniversary Edition** + **SKSE64**
- **Address Library for SKSE Plugins**
- **Dynamic Persistent Forms REthinked (`DPF.dll` / `DPF.esp`)** *(must remain uncompacted and non-ESL)*
- **Face Discoloration Fix (`FaceGenFixes.dll`)**

## Building & Testing

`MORWHA` uses Dual-Mode CMake: it builds `MORWHA-Core` from source automatically if `../MORWHA-Core` is present alongside `MORWHA Dev`, or links the prebuilt Release static libraries in `SKSE_Source/external/morwha_core/lib/Release/` for standalone contributors.

```powershell
# Full build (header hygiene + unit tests + MORWHA.dll)
powershell -NoProfile -ExecutionPolicy Bypass -File ".agents/skills/morwha-build-and-test/scripts/build-and-test.ps1" -Target all

# Fast headless unit-test loop
powershell -NoProfile -ExecutionPolicy Bypass -File ".agents/skills/morwha-build-and-test/scripts/build-and-test.ps1" -Target tests
```

## Documentation & License

- [docs/MODDER_API_REFERENCE.md](docs/MODDER_API_REFERENCE.md) — C++ Public C ABI v1, Papyrus / JSON API, and declarative JSON/JSLOT extension packs
- [docs/CHIMERA_REQUIREMENTS.md](docs/CHIMERA_REQUIREMENTS.md) — Tier-2 chimera mod requirements and `MORWHA.ini` reference
- [docs/CONSOLE_COMMANDS.md](docs/CONSOLE_COMMANDS.md) — In-game `mspawn` console commands
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — Module layout and persistence lifecycle
- [LICENSE](LICENSE) — Source-Available Modding License (with MIT License exception for `MORWHA_PublicAPI.h` and `MORWHA.psc`)
