# Quake Live rules and physics

**Decided by Ozmium, 2026-10-23:** Quake Live's competitive rules and physics become the game's standard. No crouch slide. Quake 3's maps stay exactly as id made them: Quake Live's item placement was planned, then dropped the same day ("just keep quake 3 original maps"). Quake Live's own maps are left to the community, who can port them with the map editor's BSP import (below).

This changes the project's old core rule ("the original game feel is sacred", meaning id 1.32 exactly). The new rule: **Quake Live's competitive feel is the standard; id 1.32 stays one setting away** (the Classic ruleset, see decision 2).

The research behind this plan is [tools/ql/QL_CATALOG.md](tools/ql/QL_CATALOG.md): every value with its source and confidence, and the gaps.

## Sources

Quake Live's game code is closed, so nothing is ported; we write our own code to match its numbers and measured behavior.

- **Authoritative, from Ozmium's install** (`C:\Program Files (x86)\Steam\steamapps\common\Quake Live`):
  - The cvar table in `bin.pk3/qagamex86.dll`: every rule default (pmove, damage, splash, knockback, refire, projectile speeds, starting ammo, respawn delays). It matches a community `cvarlist` dump of the final build (1069) exactly.
  - The item table in the same DLL: pickup amounts.
  - `pak00.pk3/scripts/factories.txt`: the per-mode overrides (22 factories).
  - Raw dumps live in `work/ql/` (gitignored). Phase Q1 made the extraction a tool.
  - **`tools/ql/ql_rules.json` is the kept record** (Ozmium, 2026-10-23: "keep the config"). Quake Live is no longer installed here, so the extractor can't be rerun on this PC, and nothing else reads Quake Live.
- **Mechanics** (how step jumps, chain jumps and autohop behave): Kr3m/QuakeLive, a reconstruction from the retail binaries, plus patch notes. Use it as a description only and write our own code: its license is unclear, and this repo is GPL. Anything it marks unverified stays marked so in the catalog.

## What changes (QL against id 1.32)

The full tables are in the catalog. Here are the differences that change play:

- **Movement:**
  - Jump speed 275 (270); step height 22 (18).
  - Step jump: +48 ups when jumping as you step up.
  - Chain jump: 385 ups within about 500 ms of the last takeoff.
  - A jump interval minimum of 50 ms (duel set).
  - Autohop: hold jump to keep jumping.
  - Air steps cost 3% of speed; weapon raise 200 ms (250).
  - No crouch slide.
- **Weapons:**
  - MG 5. LG 6 with knockback 1.75. Rail 80.
  - Rocket splash 84 at 1000 ups, knockback 0.9 enemy / 1.1 self.
  - Shotgun 20 × 5 in a fixed pattern.
  - BFG 300 ms, radius 80, 1800 ups.
  - Knockback capped at 120 with a fixed +24 vertical kick, so rocket jumps are weaker and capped.
- **Items and rules:**
  - Weapons respawn in 5 s in every mode.
  - The first powerup spawns at 45 s plus up to 15 s.
  - Battle Suit leaves 25% of damage.
  - The MG drops on death, and powerups drop and decay.
  - Respawn takes 2.1–2.4 s; /kill costs 1 s.
  - Ammo pickups change: MG weapon 100, shells 5, slugs 5, cells 50, lightning 50.
- **Modes (factories):**
  - FFA 15 min; duel 10 min with no frag limit; TDM 150 frags / 15 min; CTF 15 min.
  - CA: 200 health + 100 armor, every weapon, no self or falling damage.
  - Warmup and ready-up are on.
- **Not taken:** crouch slide (Ozmium), PQL ("Turbo") air control, Team Arena runes, the Race and Vampiric CA variants.

## Maps

Quake 3's maps keep id's items and spawns. Quake Live remade 29 of our 35 maps (q3dm6 is Campgrounds, q3dm17 The Longest Yard) with edited geometry and new item placement. Matching them took shared angled BSP planes, not spawn points. That placement was extracted, then dropped (Ozmium, 2026-10-23). If it ever comes back, the method is in this file's history.

## Decisions

**All decided by Ozmium on 2026-10-23, as recommended:** the duel movement set in every mode, the Classic ruleset kept, and cylinder hitboxes later. Lag compensation stays with the multiplayer plan. The HMG, ammo packs and QL spawn points (decisions 3 and 4) were for the item placement, so they went with it.

1. **Which QL movement set.** QL has two:
   - The cvar defaults: the 2014 "Quake Live ruleset", with bunny hop, crouch step jumps and a 100 ms jump interval.
   - The duel factory: the classic set, with no bunny hop, no crouch step jump, a 50 ms interval, and swim/wade 0.5/0.75.

   Competitive pickups in every mode have played the classic set since 2014. **Recommended: the duel set in every mode.** It is the competitive one, and it skips the bunny-hop formula, which nobody has reconstructed.
2. **Keep id 1.32 as a "Classic" ruleset.** Every QL change sits behind a cvar anyway, so Classic is a preset that sets them back. The test that it plays frame for frame like today's game keeps it honest. **Recommended: yes.** It replaces the unbuilt "Physics: VQ3 / CPM" match option.
3. ~~HMG and ammo packs~~: dropped with the item placement. No Quake 3 map places them.
4. ~~QL spawn points~~: dropped with the item placement.
5. **Cylinder hitboxes** (`g_playerCylinders` 1, model scale 1.1). They change hit registration for every weapon. **Recommended: yes, as a later phase**, after the rest is measured.
6. **Lag compensation** (QL rewinds hitscan by up to 80 ms). This belongs to MULTIPLAYER_PLAN.md's decision 2, which recommends Classic (none) for now. QL's 80 ms is a natural choice once matches go online.

## Phases

| Phase | Work | Claude time |
|---|---|---|
| **Q1 Extract** (done 2026-10-23) | `tools/ql/extract_ql.py` reads the player's QL install (about 6 s) and writes `tools/ql/ql_rules.json`: 393 cvars from the DLL's cvar table, 59 items from its item table, all 22 factories, and `standard_movement` (QL's pmove defaults with the duel set: BunnyHop 0, CrouchStepJump 0, JumpTimeDeltaMin 50, swim/wade 0.5/0.75, CrouchSlide 0). It also writes the raw files to `work/ql/`. The output is reproducible run to run, and 18 spot-checked values match the catalog. | 0.5 h |
| ~~Q2 Measure~~ | Dropped (Ozmium, 2026-10-23: no looking at Quake Live; it's uninstalled). The catalog's gaps are settled by the reconstruction's descriptions, marked unverified in the catalog, or by choosing 1.32's behavior where nothing better is known. | |
| **Q3 Rules** (done 2026-10-23) | Patch `0005-q3core-ql-rules.patch`. `g_ruleset` (`ql` by default, or `classic`; serverinfo, systeminfo, latched) switches every change, so Classic runs id's 1.32 code untouched. The game module's `g_ql.c` registers 64 of Quake Live's cvars under its own names, with `ql_rules.json`'s defaults:
- **Weapons:** damage, splash damage and radius, missile speeds (GL, RL, PG, BFG).
- **Knockback:** per-weapon scales including rocket and plasma self-scales, the 120 cap, the +24 vertical kick.
- **Refire times:** `weapon_reload_*`, carried to Pmove in `pmove_t.rules`, which q3core's prediction fills from the same cvars (`BG_QLRules`).
- **Respawn:** `g_respawn_delay_min/max`, and `g_allowKill`'s /kill penalty.
- **Spawning:** `g_starting*` (the loadout), `g_spawnDelay*_powerup`.
- **Items:** `g_battleSuitDampen`, `g_dropPowerups`.
- **Without a cvar:** the 20-pellet fixed shotgun (`BG_QLShotgunPellet`, mirrored in `Q3Client.ql_shotgun_pellet`), the machinegun dropping on death, and QL's seven item amounts.

The HUD's respawn line reads the ruleset's delays. **Left as 1.32:** the grappling hook, splash offsets (`g_splashdamageOffset`, `g_rocketsplashOffset`), max ammo 200, the unverified TDM machinegun/shotgun cuts, and knockback rounding (rounded, unverified). **Checked:** `tests/ruleset_test.tscn` passes for both rulesets, and the self-test is 64/64 under QL. So are the replay, spectator, campaign, pickup, chat, settings, script and cinematic camera tests (the last as documented, `--fixed-fps 60`). | 1 h |
| **Q4 Physics** | `bg_pmove.c` / `bg_slidemove.c`: jump velocity, step height, step jump, chain jump, jump interval, autohop, air-step friction, water scales, weapon raise. The values travel in a struct that both the game and q3core's prediction (`Q3Server.predict`) fill from the same cvars, so prediction can't drift. They are marked `CVAR_SYSTEMINFO` so online clients get the server's. | 1 h |
| **Q5 Modes** | `godot/game/modes/*.tres` take the factory limits. Warmup and ready-up go on by default. The match setup's Physics option becomes Ruleset (Quake Live / Classic). Item timers follow `g_itemTimers` (off in duel, as QL). | 0.5 h |
| **Q6 Bots** | botlib: jump velocity 275 (`phys_jumpvel`) and bot weapon weights for the new damage. Bots also lead projectiles with the botfiles' 1.32 speeds (`BotAimAtEnemy` uses `wi.speed`: rockets 900, BFG 2000), so under QL they over-lead rockets and under-lead the BFG; override those speeds when the ruleset is QL. The maps' AAS was built for 18-unit steps, so bots simply don't use the extra 4. | 0.25 h |
| **Q7 Tests and docs** | `tests/ruleset_test.tscn` (headless): every QL value reaches the game; jump, step-jump and chain-jump heights match the catalog's numbers; Classic matches id 1.32 frame for frame (the `replay_test` way of comparing). Docs: CLAUDE.md, PLAN.md, GAMEPLAY_PLAN.md, README (only once built). | 0.75 h |
| *Later* | Cylinder hitboxes (decision 5); QL's own maps (below). | |

**Total: about 4 h** of Claude time.

## Quake Live's maps: the community's

Ozmium, 2026-10-23: the remaster doesn't ship them. The community ports them through the map editor's BSP import (`Q3BspImport`). For that to work:

- **Version 47:** the importer must accept BSP version 47. Today it refuses everything but 46 (`scripts/map/q3_bsp_import.gd`), although the first 17 lumps are laid out the same.
- **QL-only entities:** HMG, green armor and ammo packs come through the import as entities the game has no spawn function for, so they don't appear in a match.

Notes kept from the research:

- **Shortlist (about 10 duel maps, by qlstats match samples):** Blood Run (about a third of all duels, 2017–2026), Aerowalk, Sinister, Campgrounds, Toxicity, Cure, Furious Heights, Hektik, Lost World, Battleforged. Authors and readme terms are in the catalog, section 6. Most community readmes forbid modification and paid distribution.
- **Format:**
  - pak00 is a plain zip with 149 BSPs.
  - The maps are IBSP version 47, with Q3's 17 lumps plus an advertisement lump. `q3bsp.py` reads them, but `cm_load.c` needs to accept version 47.
  - Every map ships Q3-format bot navigation (EAAS 5).
- **New content:**
  - 2,801 textures Quake 3 doesn't have; we've remastered 1,919 so far.
  - About 120 new models.
  - Green armor, Team Arena weapons and holdables.
- **Hosting:** the library is 2.0 GB, with 3.3 GB already in LFS history against GitHub's free 10 GiB. Ten maps fit; all 149 would not. Cloudflare R2's free tier (10 GB, free downloads; already planned for M9's Workshop) is the way out.
