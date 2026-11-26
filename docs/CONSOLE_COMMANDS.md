# M.O.R.W.H.A — Console Command Reference

Complete reference for the in-game Skyrim Special Edition / Anniversary Edition console commands registered by **M.O.R.W.H.A**.

---

## 1. Overview & Entry Points

- **Command Name**: `morwha_spawn`
- **Short Alias**: `mspawn` *(all examples below use `mspawn`; `morwha_spawn` is interchangeable)*
- **Case Sensitivity**: Case-insensitive across all subcommands, sex/race/boolean tokens, and hex FormIDs.
- **Argument Limit**: Skyrim's `SCRIPT_FUNCTION` binding accepts up to **4 string arguments** (`arg1`–`arg4`). Enclose multi-word names in quotes (`"Bjorn Iron-Jaw"` or `'Bjorn Iron-Jaw'`) so they count as a single argument.
- **Session Availability**:
  - **Available anytime (including Main Menu)**: `status`, `list`, `set`, `reload`, `gc`, `help` / `?`.
  - **Requires an active loaded game session** (`PlayerCharacter` loaded in a cell): 0-donor spawn (`mspawn` / `spawn`), `rel`, `child`, and `reroll`.

---

## 2. Quick Reference Table

| Command | Mode / Purpose | Default Behavior (When Optional Args Omitted) |
| :--- | :--- | :--- |
| `mspawn [spawn] [args...]` | Spawn 0-donor procedural mortal | Spawns female of selected actor's race (or Player's race; Nord fallback) named `"Test Mortal"` at selected Actor / Player |
| `mspawn rel [args...]` | Spawn 1-donor genetic relative | Uses selected / crosshair Actor (else Player), random sex (50/50), donor's race, named `"Relative Mortal"`, spawns at Player |
| `mspawn child [args...]` | Spawn 2-donor genetic offspring | Uses Player + selected / crosshair Actor, random sex (50/50), 50/50 parent race, named `"Offspring Mortal"`, spawns at Player |
| `mspawn <RaceARaceB> [age]` | Spawn 6-NPC (3M + 3F) dynamic pair batch | Combines **any** two loaded playable races or aliases (`NordOrc`, `OrcNord`, `ImperialBreton`, `AltmerKhajiit`, `NordNord`, `nord-dunmer`, etc.), pulling a fresh random Male + Female NPC donor pair from `(Race, Sex)` pools for each of the 6 children |
| `mspawn reroll [args...]` | Reroll existing MORWHA mortal in-place | Targets selected / crosshair Actor; locks current **Sex**, **Race**, and **Name**; draws new seed from stored donor(s) |
| `mspawn set [setting] [value]` | Inspect or update runtime synthesis flags | Prints current `super_eyes`, `scars`, `chimera_tier`, and Tier-2 lineage feature flags if called with no/incomplete args |
| `mspawn reload` | Hot-reload `MORWHA.ini` from VFS | Re-reads `Data/SKSE/Plugins/MORWHA/MORWHA.ini` (or creates it in MO2 `Overwrite` if absent) and updates active settings |
| `mspawn gc` | Garbage-collect orphaned NPC slots | Scans `.ess`-paired `.skse` co-saves and active session links, releasing uncommitted & unreferenced `DPF.esp` / registry slots |
| `mspawn status` | Print plugin & integration readiness | Displays version, ready state, `DPF.esp`, `FaceGenFixes.dll`, `hdtSMP64.dll`, spec/uncommitted/link counts, active settings, and detected beast accessory mods |
| `mspawn list` | List active spawned mortals in current save | Prints `key`, `actor` ref FormID, `base` `TESNPC` FormID, `dpfLocalId`, `donors=(A,B)`, `loaded3D`, and `name` |
| `mspawn help` / `mspawn ?` | Print concise usage help | Displays quick-usage summary in the console |

---

## 3. Argument & Target Resolution Rules

### 3.1 Target / World Actor Fallback
When an expected actor argument is omitted, MORWHA resolves the world target in this priority order:
1. **Console-selected reference** (`a_thisObj` — clicked in the console or targeted with `prid <FormID>`), if it is an `Actor`.
2. **Crosshair pick target** (`CrosshairPickData::GetActiveTarget()`), if it is an `Actor`.

### 3.2 Actor Tokens (`<actor>`)
Used by `rel`, `child`, and `reroll`:
- `player` — resolves directly to `PlayerCharacter` (`00000014`).
- **Hex FormID** (`1`–`8` hex digits, optional `0x` / `0X` prefix, non-zero) — e.g., `14`, `0x14`, `0001C19E`, `FF000802`. Resolved via `RE::TESForm::LookupByID<RE::Actor>`.

### 3.3 Sex Tokens (`<sex>`)
- **Female**: `female`, `f`, `1`
- **Male**: `male`, `m`, `0`
- *(On `reroll` only)* **Randomize Sex**: `anysex`, `randomsex` (50/50 roll from the new seed)

### 3.4 Race Tokens (`<Race>`)
Matches any loaded playable `TESRace` EditorID:
- **Exact EditorID** or **shorthand without trailing `Race`**, e.g.:
  - `Nord` / `NordRace`
  - `Imperial` / `ImperialRace`
  - `Breton` / `BretonRace`
  - `Redguard` / `RedguardRace`
  - `HighElf` / `HighElfRace`
  - `DarkElf` / `DarkElfRace`
  - `WoodElf` / `WoodElfRace`
  - `Orc` / `OrcRace`
  - `Khajiit` / `KhajiitRace`
  - `Argonian` / `ArgonianRace`
  - Any custom/modded `TESRace` EditorID (with or without `Race` suffix).
- **Lore Mer Aliases**:
  - `Altmer` -> `HighElfRace`
  - `Dunmer` -> `DarkElfRace`
  - `Bosmer` -> `WoodElfRace`
  - `Orsimer` -> `OrcRace`

*(Note: Vampire subraces such as `NordRaceVampire` are unwrapped to their base playable FaceGen race automatically during synthesis.)*

### 3.5 Age Bracket Tokens (`<age>`)
Optional on `mspawn` (`spawn`), `rel`, `child`, and `reroll` (resolves both skin complexion & age-appropriate sex-matched voice):
- **Young Adult (~18–25)**: `young`, `youngadult`, `young_adult`
- **Mature (~26–39)**: `mature`, `adult`
- **Middle-Aged (~40–54)**: `middle`, `middleaged`, `middle_aged`
- **Elder (55+)**: `elder`, `old`

### 3.6 Boolean Tokens (`<bool>`)
Used by `mspawn set`:
- **True**: `on`, `true`, `1`, `yes`
- **False**: `off`, `false`, `0`, `no`

---

## 4. Detailed Subcommand Reference

### 4.1 `mspawn` / `mspawn spawn` — 0-Donor Procedural Mortal
Synthesizes an original adult mortal procedurally from `.jslot` face anchor presets without biological donors.

**Accepted Forms:**
```text
mspawn
mspawn [sex] [Race] ["Name"]
mspawn [Race] [sex] ["Name"]
mspawn ["Name"]
mspawn spawn [sex|Race] ...
```

**Behavior & Constraints:**
- **Anchor**: Uses console-selected reference (`a_thisObj`) if it is an `Actor`; otherwise spawns at `Player`. *(Unlike `rel`/`child`/`reroll`, 0-donor `mspawn` uses the console reference `a_thisObj`, not crosshair pick, for placement.)*
- **Order Flexibility**: If the first argument is `<sex>`, optional second argument is `<Race>` (else treated as `"Name"` when 3rd arg is empty), and third is `"Name"`. If the first argument is `<Race>`, optional second/third arguments accept `<sex>` + `"Name"` or `"Name"` alone.
- **Note**: Prefix `mspawn spawn` shifts arguments by 1 slot, leaving 3 slots after `spawn`.

**Examples:**
```text
mspawn
mspawn male Nord "Bjorn"
mspawn Altmer female "Aurelia"
mspawn "Wanderer"
```

---

### 4.2 `mspawn rel` / `mspawn relative` — 1-Donor Relative
Synthesizes an adult relative inheriting facial structure, skin/hair coloration, height, and eye iris traits from a single donor. Records donor provenance (`donorARefId` / `donorARef` / `donorASlotKey`) for future `reroll` calls.

**Syntax:**
```text
mspawn rel [actor] [sex] [Race] ["Name"]
```
*(Maximum of 3 trailing arguments after `rel`, in any order.)*

**Token Precedence Per Argument:**
`TryParseSex` -> `TryParseActorToken` (max 1 actor) -> `FindRaceByToken` -> `"Name"` (first remaining token).

**Defaults & Anchor:**
- **Donor**: Explicit `<actor>` token if provided; otherwise selected/crosshair `Actor`; falls back to `Player` if no world target is selected.
- **Sex**: Random 50/50 if omitted (`aiSex = -1`).
- **Race**: Donor's unwrapped race unless `<Race>` override is passed.
- **Name**: `"Relative Mortal"` if omitted.
- **Anchor**: Always spawns near `Player`.

**Examples:**
```text
mspawn rel
mspawn rel player female Nord "Astrid"
mspawn rel 0001C19E male
mspawn rel female "Sister"
```

---

### 4.3 `mspawn child` / `mspawn offspring` — 2-Donor Offspring
Synthesizes a child/offspring combining facial structure, eye traits, height, skin, and hair genetics from two distinct parent actors. Stores both `donorA` and `donorB` provenance for `reroll`.

**Syntax:**
```text
mspawn child [parentA] [parentB] [sex] [Race] ["Name"]
mspawn offspring [parentA] [parentB] [sex] [Race] ["Name"]
```
*(Maximum of 3 trailing arguments after `child` / `offspring`, in any order.)*

**Token Precedence Per Argument:**
`TryParseSex` -> `TryParseActorToken` (max 2 actors) -> `FindRaceByToken` -> `"Name"` (first remaining token).

**Parent Resolution Rules:**
- **2 actor tokens supplied** (`actor1`, `actor2`): `parentA = actor1`, `parentB = actor2`.
- **1 actor token supplied** (`actor1`):
  - If a world target (selected/crosshair `Actor`) exists, is not `Player`, and differs from `actor1`: `parentA = worldTarget`, `parentB = actor1`.
  - Otherwise: `parentA = Player`, `parentB = actor1`.
- **0 actor tokens supplied**: `parentA = Player`, `parentB = selected/crosshair Actor`.

If `parentA` or `parentB` is null or identical (`parentA == parentB`), the command aborts with a message requiring 2 distinct parents.

**Defaults & Console 4-Arg Tip:**
- **Defaults**: Random 50/50 sex, 50/50 parent race inheritance (unless `<Race>` override passed), `"Offspring Mortal"`, spawns near `Player`.
- **4-Argument Limit Tip**: Because `child` takes 1 slot (`arg1`), only 3 slots remain (`arg2`–`arg4`). If you pass **two** explicit FormIDs (`child <hexA> <hexB> ...`), you have **1** slot left for sex, race, or name. To specify more overrides at spawn time, aim at or `prid` one parent first (e.g., `mspawn child 14 female Nord` while aiming at Parent B), or run `mspawn reroll` afterward with overrides.

**Examples:**
```text
mspawn child
mspawn child female "Daughter 1"
mspawn child 14 0001C19E male
mspawn child 0001A697 0001C19E "Heir"
```

---

### 4.4 `mspawn reroll` — In-Place Appearance Reroll
Re-synthesizes an already-spawned MORWHA mortal **in-place** from its stored donor provenance, re-hydrates its `DPF.esp` `TESNPC` record, atomically updates `Data/SKSE/Plugins/MORWHA/DynamicNPCRegistry.json`, updates its `'MRWH'` co-save link with the new seed, and refreshes live 3D on the SKSE main task thread (`SwitchRace` if race changed + `DoReset3D(true)` + `Mortal3dSyncService::QueueActor3dSync`).

**Syntax:**
```text
mspawn reroll [actor] [anyrace|rerollrace|Race] [female|male|anysex|randomsex] ["Name"]
```
*(Maximum of 3 trailing arguments after `reroll`, in any order.)*

**Token Precedence Per Argument:**
`anyrace`/`rerollrace` -> `anysex`/`randomsex` -> `TryParseSex` -> `TryParseActorToken` -> `FindRaceByToken` -> `"Name"`.

**Defaults & Special Flags:**
- **Target**: Explicit `<actor>` token if passed; otherwise console-selected or crosshair `Actor`. Must be an active MORWHA mortal tracked in the current save (`mspawn list`).
- **Locked Defaults**: When omitted, **Sex**, **Race**, and **Name** remain locked to the mortal's current values while rolling a brand-new `Seed64`.
- **Race Unlock (`anyrace` / `rerollrace`)**: Re-rolls race from the stored donor race(s) instead of keeping the current race locked (primarily useful for cross-race 2-donor offspring). Overridden if an explicit `<Race>` is also passed.
- **Sex Unlock (`anysex` / `randomsex`)**: Re-rolls sex 50/50 from the new seed instead of keeping the current sex locked.

**Examples:**
```text
mspawn reroll
mspawn reroll FF000805
mspawn reroll FF000805 anyrace
mspawn reroll FF000805 Nord female "Freya"
mspawn reroll anysex anyrace
```

---

### 4.5 `mspawn set` — Runtime Synthesis Toggles
Inspects or modifies session synthesis flags (`App::SynthesisCoordinator` settings). Works from the Main Menu as well as in-game.

**Syntax:**
```text
mspawn set
mspawn set <super_eyes|scars> <on|off|true|false|1|0|yes|no>
mspawn set chimera_tier <off|0|1|tier1|2|tier2>
```

**Available Keys:**
- `super_eyes`: Controls `allowSupernaturalEyes` (default `off`). When `off`, emissive/supernatural eyes are filtered from procedural synthesis and unprompted inheritance unless needed structurally.
- `scars`: Controls `allowScars` (default `off`). When `off`, scar headpart overlays are excluded from generated specs.
- `chimera_tier`: Controls `beastChimeraTier` (default `tier2`, cascading `Tier 2 -> Tier 1` per lineage and sex when hybrid skins/accessories are disabled).
  - `off` / `0` / `disabled`: Single-parent isolation for Humanoid/Mer × Beast pairings.
  - `1` / `tier1` / `on`: Tier 1 no-requirement Beast Chimeras (50/50 dominant parent race + single-parent dominant inheritance from the matching-race parent).
  - `2` / `tier2`: Tier 2 native base race + 4-map `.mrst` hybrid fur/scale skins and beast accessories (`HybridSkinService` + `ChimeraAccessoryCatalog`; gracefully cascades per lineage to Tier 1 when unavailable).

*(Note: Changes apply in memory for the current game session and affect subsequent spawns and `reroll` calls. Use `mspawn reload` to re-read `Data/SKSE/Plugins/MORWHA/MORWHA.ini`, including `[Chimeras]` and `[ChimeraTier2.*]` settings.)*

**Examples:**
```text
mspawn set
mspawn set super_eyes on
mspawn set scars true
mspawn set chimera_tier tier1
mspawn set chimera_tier off
```

---

### 4.6 `mspawn reload` — Hot-Reload `MORWHA.ini`
Re-reads `Data/SKSE/Plugins/MORWHA/MORWHA.ini` (creating the commented default file in MO2 `Overwrite` if absent from all mods) and refreshes all runtime settings including `[Chimeras]` and `[ChimeraTier2.*]` feature toggles (`bEnableHybridSkin`, `bMatchTailColor`, `bMatchHornColor`). Works from the Main Menu as well as in-game.

**Syntax:**
```text
mspawn reload
```

---

### 4.7 `mspawn gc` — Registry & DPF Slot Garbage Collection
Scans all `.ess`-paired `.skse` co-saves in `<My Games>/Skyrim Special Edition/<sLocalSavePath>` and `Saves/` for `'MRWH'` `'LNKS'` records, retains all slots active in the current session or referenced by any save on disk (including transitive `donorASlotKey` / `donorBSlotKey` ancestors), releases orphaned `DPF.esp` FormIDs via `DPF::ReleaseByOwnerKey` (or `DPF::ReleaseOwner("MORWHA")` when zero live slots remain), and rewrites `DynamicNPCRegistry.json`. Works from both the Main Menu and in-game; fails closed on co-save read/parse errors.

**Syntax:**
```text
mspawn gc
```

---

### 4.8 `mspawn status` — System & Dependency Diagnostics
Prints plugin initialization state, soft/hard dependency detection, loaded registry specs (total and uncommitted), active co-save links, synthesis toggle states, and soft-detected third-party chimera race mod presence.

**Syntax:**
```text
mspawn status
```

**Sample Output:**
```text
MORWHA Status: version=0.1.0, ready=yes, DPF.esp=ready, FaceGenFixes.dll=loaded, hdtSMP64.dll=loaded, registrySpecs=12 (uncommitted=0), activeSaveLinks=6, superEyes=off, scars=off, chimeraTier=tier2, wearableBeastParts=yes
```

---

### 4.9 `mspawn list` — Inspect Active Save Links
Lists every MORWHA-spawned mortal currently tracked in this playthrough's `'MRWH'` co-save (`GetActiveLinksSnapshot()`). Useful for grabbing runtime `FFxxxxxx` actor FormIDs and verifying stored donor IDs before calling `mspawn reroll` or `prid`.

**Syntax:**
```text
mspawn list
```

**Output Fields Per Entry:**
- `[i]` — 0-based index in the active link table
- `key` — `slotKey` in `DynamicNPCRegistry.json` (e.g., `offspring_mortal_0003`)
- `actor` — Live `Actor` reference FormID in this save (e.g., `FF001234`)
- `base` — Hydrated `TESNPC` FormID inside `DPF.esp` (or `00000000` if unloaded)
- `dpfLocalId` — Local FormID inside `DPF.esp`
- `donors=(A,B)` — Stored runtime FormIDs for `donorARefId` and `donorBRefId` (`00000000` when unused)
- `loaded3D` — `yes` / `no` indicating whether 3D is currently loaded
- `name` — Actor display name

---

### 4.10 `mspawn help` / `mspawn ?` — Usage Summary
Prints a concise command cheatsheet directly to the in-game console.

**Syntax:**
```text
mspawn help
mspawn ?
```

---

## 5. Logs & Troubleshooting
- Runtime synthesis details, donor extraction tiers (`Tier1`–`Tier3`), resolved headparts, seed values, and error reasons are written to:
  `Documents/My Games/Skyrim Special Edition/SKSE/MORWHA.log`
- Spawned NPC definitions persist globally in `Data/SKSE/Plugins/MORWHA/DynamicNPCRegistry.json` (with `.bak` atomic backup), while per-save actor and donor links persist inside your SKSE `.skse` co-save under chunk `'MRWH'` / `'LNKS'`.

