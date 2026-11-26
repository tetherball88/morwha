# M.O.R.W.H.A — Engineering & Architecture Rules

Project-wide engineering rules for the **M.O.R.W.H.A** SKSE64 runtime NPC synthesis framework.

---

## 0. Scope, Diagnostics & Build Policy

- **Strict Scope Discipline**: Modify **only** files inside `MORWHA Dev` (and sibling `../MORWHA-Core` when explicitly modifying synthesis/chimera domain math). Never modify unrelated sibling mod folders unless requested.
- **Generic Root-Cause Engineering**: Never hardcode per-NPC (`Serana`, `Farkas`, `Player`) or per-mod special cases; rely on structural engine flags, VFS assets, and declarative JSON catalogs.
- **Build & Test Command**:
  `powershell -NoProfile -ExecutionPolicy Bypass -File ".agents/skills/morwha-build-and-test/scripts/build-and-test.ps1" -Target all`
  (add `-Reconfigure` after modifying `CMakeLists.txt`; use `-Target tests` for fast headless unit test runs).
- **Locked Target DLL (`SkyrimSE.exe` Open)**: If Skyrim is open, `SKSE/Plugins/MORWHA.dll` is locked by Windows and only the `POST_BUILD` copy fails after `SKSE_Source/build/release/Release/MORWHA.dll` links cleanly. Treat compilation as succeeded.

---

## 1. Dual-Mode Module DAG (`SKSE_Source/`)

- **Pure Engine-Free Layers (`pch_core.h` — zero `<RE/...>`, `<SKSE/...>`, `Windows.h`, `nlohmann/*`, or `spdlog/*`)**:
  1. `morwha_foundation`: Strong IDs (`FormRawId`, `Seed64`), `DomainTypes.h`, callback-sink `Logging`.
  2. `morwha_state`: `SessionEpoch`, `JsonDom`, `DynamicNpcRegistry`, `MortalRequestJson`, `RegistryGarbageCollector`, `ChimeraAccessoryManifest`, `MorwhaIniConfig`, `FaceAnchorPreset`, `'MRWH'` binary `CoSaveCodec`.
  3. `morwha_core` (**Dual-Mode Opaque `MORWHA-Core` SDK**): Built from private `../../MORWHA-Core` when present locally (running `morwha_core_tests.exe` and syncing `MorwhaCore.h` + `morwha_core.lib`), or linked directly from `external/morwha_core/include/morwha/core/MorwhaCore.h` + `external/morwha_core/lib/Release/morwha_core.lib` when building standalone. Owns all pure synthesis, chimera, asset-decoding, and `.mrst` hybrid-skin rules behind `MORWHA::Core`. Zero internal `morwha/domain/*` headers or offline tools live in `MORWHA Dev`.
- **Engine, Application & Bridge OBJECT Modules (`pch_engine.h`)**:
  4. `morwha_engine`: `EngineLogging`, `DpfClient`, `SkeeOverlayClient`, `HeadPartPoolIndexer`, `FaceAnchorCatalog`, `ChimeraAccessoryCatalog`, `ChimeraMeshTinter`, `HybridSkinService`, `MortalHeritageKeywordService`, `AgeProfileResolver`, `VfsAssetReader`, `LiveActorMeshExtractor`, `TriProjectorCache`, `DonorPhenotypeExtractor`, `BeastTintSynthesizer`, `GeneticSpecBuilder`, `ChimeraSpecBuilder`, `ChimeraEngineSupport`, `TesNpcHydrator`, `MortalSpawner`, `Mortal3dSyncService`, `SkseSerializationCallbacks`.
  5. `morwha_app`: `SynthesisCoordinator`, `MortalSpawnTypes`, `MortalOverridesApplier`, `SynthesisRegistryMaintenance`.
  6. `morwha_bridge_papyrus`: `PapyrusInterface` (`MORWHA.psc`), `ConsoleCommands` (`mspawn`), & `ConsoleRacePairSpawner` (dynamic `(Race, Sex)` donor pools for `<RaceARaceB>` batch spawns).
  7. `morwha_bridge_c_api`: `MORWHA_PublicAPI.h` (`extern "C" __declspec(dllexport)` zero-link ABI v1).

---

## 2. Engine & Persistence Invariants

1. **`RE::TESNPC::FaceData::Parts` Indices**: `kNose = 0`, `kUnknown = 1` (`0x7F7FFFFF`), `kEyes = 2`, `kMouth = 3`.
2. **`RE::TESNPC::Layer` (`tintLayers`)**: Opacity uses `std::uint16_t interpolationValue` (`0..100`), resolved per `(TESRace*, Sex)` from `TESRace::FaceRelatedData::TintAsset`.
3. **Dual-Layer Persistence & Commit-on-Save Staging**:
   - `DPF.esp` must remain uncompacted and non-ESL.
   - Global NPC specs persist atomically in `Data/SKSE/Plugins/MORWHA/DynamicNPCRegistry.json`; per-save actor bindings persist in the `'MRWH'` SKSE co-save.
   - Mid-session spawns stage with `committed = false`, promote to `committed = true` (with transitive donor retention) on `SaveCallback`, purge uncommitted slots on session revert, and garbage-collect unreferenced slots across `.ess`-paired `.skse` co-saves before `kDataLoaded` hydration.
   - Every hydrated `RE::TESNPC` executes `InitItem()` before `PlaceObjectAtMe`, sets `faceNPC = nullptr`, and clears `kRespawn`.
4. **Matched-Topology HeadPart Pools**: Keep `Vanilla` and `High Poly Head` face/brow/beard/scar headparts isolated by vertex topology, and gate SMP hair parts on `hdtSMP64.dll`.
