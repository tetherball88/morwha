# Changelog

## v0.1.1-alpha

### Improved
- **Man x Mer Mixed-Heritage Offspring**: Stronger cross-parent facial structure, eye/eyebrow inheritance, and blended half-pointed / half-rounded Manmer ear shapes regardless of which parent's base race is selected.
- **Documentation**: Added step-by-step installation instructions for the Core framework and Hybrid Skin Stencils archives in `README.md`.

### Fixed
- **Youthful Complexions & RSV Compatibility**: Fixed `YoungAdult` spawns occasionally rolling `Age40`/`Age50` facial detail maps across vanilla and third-party skin overhauls (`BnP`, `Modpocalypse`), refined `YoungAdult` voice pools, and synchronized `RSVyoung` / `RSVold` runtime keywords when **Racial Skin Variance (RSV)** is installed so head normal maps match `AgeBracket`.
- **Eye Pool Filtering**: Excluded unilateral blind and split-eye heterochromia `BGSHeadPart` meshes from generated eye pools.
- **Youthful Hair Pigmentation**: Prevented `YoungAdult` and `Mature` Humanoid offspring from inheriting senescent grey/silver hair from elderly donors or developing greenish hair tint casts.
- **Orc Chimera Mouths**: Prevented human-UV sharp-teeth swaps (`BnP RSV Teeth` / `Sharpened Teeth`) from applying onto Orc tusked mouths.

---

## v0.1.0-alpha

### Added
- Initial public alpha release of **M.O.R.W.H.A — Mortals On-Demand: Runtime World-Spawning, Heads & Appearance** (`MORWHA.dll`, `MORWHA.psc`, `MORWHA_PublicAPI.h` v1, and 2-tier Beast/Mixed-Heritage synthesis).
