# M.O.R.W.H.A — System Architecture Overview

High-level architecture of **M.O.R.W.H.A — Mortals On-Demand: Runtime World-Spawning, Heads & Appearance**.

For third-party C++, Papyrus, and JSON/JSLOT integration details, see [MODDER_API_REFERENCE.md](MODDER_API_REFERENCE.md). For in-game console commands, see [CONSOLE_COMMANDS.md](CONSOLE_COMMANDS.md). For chimera mod setup and `MORWHA.ini` configuration, see [CHIMERA_REQUIREMENTS.md](CHIMERA_REQUIREMENTS.md).

---

## 1. Layered Module Architecture

M.O.R.W.H.A is structured as a strict directed acyclic graph (DAG) in `SKSE_Source/`:

1. **`morwha_foundation` (`STATIC`, engine-free)**:
   - Core strong identifiers (`FormRawId`, `Seed64`), value types (`Sex`, `DonorMode`, `TopologyPool`, `AgeBracket`, `FaceMorphVector`, `RgbColor`), and sink-agnostic logging.
2. **`morwha_state` (`STATIC`, engine-free)**:
   - Crash-safe atomic `DynamicNPCRegistry.json` serialization & validation (`DynamicNpcRegistry`), JSON spawn/update request parsing (`MortalRequestJson`), cross-save `'MRWH'` `.skse` co-save binary codec & garbage collector (`CoSaveCodec`, `RegistryGarbageCollector`), `MORWHA.ini` parser (`MorwhaIniConfig`), and declarative `ChimeraAccessories` / `FaceAnchors` `.jslot` manifest loaders.
3. **`morwha_core` (Dual-Mode Opaque `MORWHA-Core` SDK)**:
   - Opaque black-box synthesis, asset-decoding (`.tri` / `.nif` / `.dds`), `.mrst` hybrid-skin generation, age/voice-pool, and chimera library exposed solely through `morwha/core/MorwhaCore.h` (`MORWHA::Core`). Built from sibling `MORWHA-Core` when present locally or linked from the prebuilt `morwha_core.lib` under `SKSE_Source/external/morwha_core/`.
4. **`morwha_engine` (`OBJECT`, CommonLibSSE-NG)**:
   - `DpfClient` (`DPF.esp` persistent `TESNPC` slot management), `SkeeOverlayClient` (RaceMenu `IOverrideInterface` head-node cleanup), `HeadPartPoolIndexer` (topology-matched `Vanilla` / `High Poly Head` headpart indexing), `AgeProfileResolver` (`AgeBracket` complexion & sex-checked `Skyrim.esm` voice resolution), `DonorPhenotypeExtractor` (live `Actor` and static `TESNPC` donor extraction), `HybridSkinService` (4-map `.mrst` hybrid skin generation & caching), `MortalHeritageKeywordService` (ESP-less runtime `MORWHA_*` heritage & lore `BGSKeyword` registration/attachment), `GeneticSpecBuilder` & `ChimeraSpecBuilder`, `TesNpcHydrator` (in-memory `TESNPC` & live `Actor` hydration), `MortalSpawner`, `Mortal3dSyncService` (async 3D shader/tint application), and `SkseSerializationCallbacks` (`'MRWH'` co-save hooks).
5. **`morwha_app` (`OBJECT`)**:
   - `SynthesisCoordinator`, `MortalOverridesApplier`, and `SynthesisRegistryMaintenance` — orchestrating `kDataLoaded` readiness, `SpawnMortal` / `UpdateMortal` / `RerollMortal`, archetype/AI/RPG overrides, commit-on-save staging, and cross-save garbage collection.
6. **`morwha_bridge_papyrus` & `morwha_bridge_c_api` (`OBJECT`)**:
   - Native `MORWHA.psc` bindings (`PapyrusInterface`), `morwha_spawn` (`mspawn`) console interface (`ConsoleCommands`), and the zero-link `MORWHA_PublicAPI.h` v1 C ABI.

---

## 2. Persistence & Save Lifecycle

1. **Boot Garbage Collection & Slot Hydration (`kDataLoaded`)**:
   - Loads `Data/SKSE/Plugins/MORWHA/MORWHA.ini` (creating defaults if absent), indexes headparts and declarative chimera/anchor catalogs, loads `DynamicNPCRegistry.json` (falling back to `.bak` if interrupted), prunes uncommitted/orphaned `DPF.esp` slots across active `.ess`-paired `.skse` saves (`RegistryGarbageCollector`), and hydrates surviving `RE::TESNPC` records in memory before save loading begins.
2. **Commit-on-Save Staging & Session Revert**:
   - Mid-session spawns write `committed = false` to `DynamicNPCRegistry.json` as a write-ahead record for `DPF_Cache.bin`.
   - Saving the game (`SaveCallback`) writes active `SpawnedMortalLink` entries to the `'MRWH'` co-save and promotes referenced `slotKey`s (plus transitive `donorASlotKey` / `donorBSlotKey` ancestors) to `committed = true`.
   - Reloading or exiting without saving (`OnSessionRevert`) releases uncommitted `DPF.esp` slots via `DPF::ReleaseByOwnerKey` and prunes them from `DynamicNPCRegistry.json`.
3. **Async 3D Sync (`Mortal3dSyncService`)**:
   - Applies per-NPC hair/skin/chimera shader tints and idempotent mixed-heritage pinna ear-morph deltas (`FGBM` + `BSDynamicTriShape`) cleanly once `Is3DLoaded()` and `GetFaceNodeSkinned()` finish streaming on unpaused frames, without burning `BGSColorForm` FormID slots or triggering redundant neck-seam resets.
