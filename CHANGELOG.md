# Changelog

## v0.1.1-alpha

### Added
- **Diagnostic Bundle Exporter (`mspawn diag`)**: Added `mspawn diag [actorHex|player]` to package logs, `MORWHA.ini`, load order, loaded SKSE DLLs, NPC/donor records, live 3D shader/material state, VFS `.dds`/`.nif`/`.tri` headers, and `64x64` `.bmp` previews into a compact `.zip` under `Data/SKSE/Plugins/MORWHA/Diagnostics/` for modlist troubleshooting.

### Improved
- **Man x Mer Mixed-Heritage Offspring**: Stronger cross-parent facial structure, eye/eyebrow inheritance, and blended half-pointed / half-rounded Manmer ear shapes regardless of which parent's base race is selected.
- **Documentation**: Added step-by-step installation instructions for the Core framework and Hybrid Skin Stencils archives in `README.md`.

### Fixed
- **Youthful Complexions & RSV Compatibility**: Fixed `YoungAdult` spawns occasionally rolling `Age40`/`Age50` facial detail maps across vanilla and third-party skin overhauls (`BnP`, `Modpocalypse`), refined `YoungAdult` voice pools, synchronized `RSVyoung` / `RSVold` runtime keywords when **Racial Skin Variance (RSV)** is installed, and preserved SPID/external `TESNPC::skin` records across `mspawn reroll` and `UpdateMortal` (respawning cleanly when `race` or `sex` changes) to prevent head/body neck-seam mismatches.
- **Eye Pool Filtering**: Excluded unilateral blind and split-eye heterochromia `BGSHeadPart` meshes from generated eye pools.
- **Youthful Hair Pigmentation**: Prevented `YoungAdult` and `Mature` Humanoid offspring from inheriting senescent grey/silver hair from elderly donors or developing greenish hair tint casts.
- **Hybrid Chimera Skin Specular & Subsurface Seamlessness**: Added native `BC4` (`BC4U` / `ATI1` / `DX10` `BC4_UNORM`), `BC2`, and `24-bit` DDS decoding so single-channel specular maps from skin overhauls (`The New Gentleman`, `CBBE`) preserve their matte specular levels and aspect ratios instead of falling back to glossy highlights, corrected runtime `BGSTextureSet` subsurface (`kSubsurfaceTint`) and detail (`kDetailMap`) slot bindings across head/body/hand/tail hybrid textures to eliminate neck-seam lighting discontinuities, and bumped the generated skin cache format (`v4`) to automatically regenerate stale cached textures on launch.
- **Orc Chimera Mouths**: Prevented human-UV sharp-teeth swaps (`BnP RSV Teeth` / `Sharpened Teeth`) from applying onto Orc tusked mouths.

---

## v0.1.0-alpha

### Added
- Initial public alpha release of **M.O.R.W.H.A — Mortals On-Demand: Runtime World-Spawning, Heads & Appearance** (`MORWHA.dll`, `MORWHA.psc`, `MORWHA_PublicAPI.h` v1, and 2-tier Beast/Mixed-Heritage synthesis).
