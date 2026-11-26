---
name: morwha-release
description: >-
  Prepare, bump, and verify a new M.O.R.W.H.A version release (vX.Y.Z or vX.Y.Z-alpha). Activate when asked to prepare a release, bump version numbers, generate or finalize CHANGELOG.md for a release, audit release readiness (MORWHA.dll, morwha_core.lib symbol hygiene, 62 .mrst stencils, Papyrus .pex, JSON/.jslot manifests, requirements), or prepare GitHub/Nexus release notes.
---

# M.O.R.W.H.A Version Release Runbook

Use this workflow whenever preparing a new M.O.R.W.H.A release (`vX.Y.Z` or `vX.Y.Z-alpha`).

## How M.O.R.W.H.A Releases Work

- Pushing a `v*` tag (`.\tag-push.bat vX.Y.Z`) triggers [.github/workflows/release-zip.yml](../../../.github/workflows/release-zip.yml) on `ubuntu-latest`.
- The workflow **does not compile code in CI**; it stamps `SKSE/Plugins/MORWHA/version.txt` and packages the committed `Scripts/` and `SKSE/` directories into `MORWHA_vX.Y.Z.zip` (excluding `.pdb`, `.gitkeep`, `MORWHA.ini`, `DynamicNPCRegistry.json*`, and `GeneratedSkinCache.json*`).
- On a live tag push (or `workflow_dispatch` with `dry_run: false`), the workflow:
  1. Extracts the `## vX.Y.Z` section from `CHANGELOG.md` (or falls back to `"Automated release for vX.Y.Z"` if absent) and converts it to plain text for Nexus.
  2. Creates the GitHub Release with `MORWHA_vX.Y.Z.zip` attached (marking `alpha`/`beta`/`rc` tags as `prerelease: true`).
  3. Renames the archive to `MORWHA_X.Y.Z.zip` and publishes to **Nexus Mods** via `Nexus-Mods/upload-action@v1.0.0-beta.11` using the `nexusmods` GitHub Environment (`secrets.NEXUSMODS_API_KEY`, `vars.NEXUSMODS_MOD_ID` / `secrets.NEXUSMODS_MOD_ID`, `vars.NEXUSMODS_FILE_ID` / `secrets.NEXUSMODS_FILE_ID`) whenever Nexus credentials are configured.
- You can run a manual `workflow_dispatch` with `dry_run: true` on GitHub Actions to test zip creation and changelog extraction without publishing.
- Every compiled artifact (`SKSE/Plugins/MORWHA.dll`, `SKSE_Source/external/morwha_core/lib/Release/morwha_core.lib`, `SKSE/Plugins/MORWHA/SkinStencils/*.mrst`, and `Scripts/MORWHA.pex`) **must** be up to date and committed before tagging.

---

## Release Preparation Steps

### 1. Inspect Changes Since Previous Release

1. Find the latest release tag and review everything queued since that tag:
   ```powershell
   git tag --list "v*" --sort=-creatordate | Select-Object -First 5
   git log <prev_tag>..HEAD --stat
   git status -s
   ```
2. Determine the target semantic version `vX.Y.Z` or prerelease `vX.Y.Z-alpha` (e.g., `v0.1.0-alpha`, `v0.2.0`).

### 2. Bump Version Metadata

Run [prepare_release.ps1](./scripts/prepare_release.ps1) with `-ApplyVersion`:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".agents/skills/morwha-release/scripts/prepare_release.ps1" -Version vX.Y.Z -ApplyVersion
```

This updates the version strings across:
- `SKSE/Plugins/MORWHA/version.txt` (`X.Y.Z` or `X.Y.Z-alpha`)
- `SKSE_Source/CMakeLists.txt` (`project(MORWHA VERSION X.Y.Z LANGUAGES CXX)`)
- `SKSE_Source/version.rc` (`FILEVERSION`, `PRODUCTVERSION`, `FileVersion`, `ProductVersion`)
- `meta.ini` (`version=X.Y.Z`, if present locally)

### 3. Update `CHANGELOG.md` & Audit Requirements

1. Prepend a `## vX.Y.Z` section (`### Added`, `### Changed`, `### Fixed`) above the previous release in `CHANGELOG.md`.
2. Inspect commits since `<prev_tag>` for added, removed, or bumped dependencies and integrations:
   - Core dependencies (`SKSE64`, `Address Library`, `Dynamic Persistent Forms REthinked`, `FaceGenFixes.dll`).
   - Tier-2 Chimera integrations (`XPMSSE`, `RaceMenu`, `ChooeyDintEarsEdit.esp`, `NK_HornSlider.esp`, `PraedysHorns.esp`, `BnP RSV Teeth`, `Sharpened Teeth`).
3. Update `README.md` and `docs/CHIMERA_REQUIREMENTS.md` if any requirement, public C ABI (`MORWHA_PublicAPI.h`), or `MORWHA.ini` key changed.

### 4. Rebuild Compiled Binaries & Run Unit Tests

1. Activate [morwha-build-and-test](../morwha-build-and-test/SKILL.md) and rebuild using `-Reconfigure` so updated `version.rc` / `CMakeLists.txt` metadata is embedded in `MORWHA.dll` and all unit tests pass:
   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File ".agents/skills/morwha-build-and-test/scripts/build-and-test.ps1" -Target all -Reconfigure
   ```
2. Verify `SkyrimSE.exe` is closed before final commit/tag so `SKSE/Plugins/MORWHA.dll` receives the post-build copy.

### 5. Run Full Pre-Release Audit

Re-run [prepare_release.ps1](./scripts/prepare_release.ps1) in audit mode:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".agents/skills/morwha-release/scripts/prepare_release.ps1" -Version vX.Y.Z
```

Confirm all automated checks report `[PASS]` (including `MORWHA.dll` SHA-256 parity, `morwha_core.lib` symbol hygiene, `62` `.mrst` stencils, `.pex` freshness, JSON/`.jslot` validity, and zero personal runtime cache/registry files inside `SKSE/`).

### 6. Hand-off Checklist (Never Auto-Commit or Auto-Tag)

Do **not** commit, push, or run `tag-push.bat` unless the user explicitly asks you to. Instead, provide:
1. The `prepare_release.ps1` audit summary (`PASS` / `WARN` / `FAIL`).
2. Any manual actions required before tagging (recompiling flagged `.pex` files, closing `SkyrimSE.exe` if DLL copy was blocked, updating Nexus requirements).
3. Suggested release commit & tag commands:
   ```powershell
   git commit -m "release vX.Y.Z"
   git push
   .\tag-push.bat vX.Y.Z
   ```
