# Race mode (Defrag) and the map editor's race elements

**Asked for by Ozmium, 2026-10-25:** "the game needs a Race mode and race mode elements in the map editor. Defrag!" Defrag is the Quake 3 mod for timed movement courses: you run from a start line to a finish line as fast as you can, with checkpoints along the way. Its maps (q3df.org has thousands) are the genre.

## What is in `C:\steam\steamapps\common\Quake 3 Arena\defrag`

Ozmium said the race maps are there. They aren't. It is the Defrag 1.91.31 bundle (neyoneit's "DeFRaG Bundle for newcomers"), which holds the mod, its docs and a few example maps, but no course:

- **The mod:** `defrag/cgamex86_64.dll`, `vm/*.qvm` (closed source; the DLL and QVMs are not code we can port), `oDFe` engine builds.
- **The mapping kit** (`defrag/scripts/defrag.def`, `defrag/misc/maps/`): the entity definitions and nine example `.map` files (spawn room with a door, accuracy, level, shooters, velocity pads, weapon jumps, a blackscreen box). Plus `defrag_gallery.bsp` in `zz-defrag_media_190.pk3`, and five test BSPs in `misc/pk3s/zz-defrag_testmaps.pk3` (`df-black`, `df-black-grid`, `df-weapon1`, `df-shooter1`, `df-velopad1`).
- **42 `.defi` files** (`zz-defrag_fix_mapscripts_190.pk3`): they name the classic race maps (o3j-fusion, o3j-velocity, runbfg, tow1f, Ujump, Level-168, and so on) so the menu can list them, but the BSPs themselves are not in the bundle. The bundle says to download them from q3df.org.
- **Ghosts, demos, HUD configs and docs** (78 docs): the reference for what players expect (timer, checkpoints, ghost, accel meter, savepos).

`defrag/baseq3` holds only `zzz-safe-textures.pk3`.

## Defrag in short (what we build)

- **Run:** `target_startTimer` fires when you cross the start trigger, `target_stopTimer` at the finish, `target_checkpoint` on the way (split times). Leaving the start zone again restarts the timer.
- **Map styles** (a `.defi` field): `training`, `run`, `accuracy` (shoot targets to stop the timer), `level` (collect items). The `.defi` also says `vq3` and/or `cpm` physics; we play `vq3` maps only.
- **Physics:** VQ3 is id 1.32 movement, which is our Classic ruleset (`g_ruleset classic`). CPM (Challenge ProMode) is a different set (more air control, a double jump, faster strafing) and is not built. Quake Live's rules are not Defrag's.
- **Weapons:** rocket, plasma and grenade jumping with no self damage and unlimited ammo; no one else in the way (players don't collide).
- **Map entities** (from `defrag.def`): `target_startTimer`, `target_stopTimer`, `target_checkpoint`, `target_fragsFilter`, `target_init`, `target_smallprint` / `target_print`, `target_speed`, `target_multimanager`, `trigger_push_velocity` (a jump pad that can add to your speed), `trigger_teleport` with KEEP_SPEED, and `shooter_grenade/plasma/rocket_targetplayer` (turrets that lead you).
- **Player aids:** instant restart, savepos/loadpos for practice, ghosts of your best run or the record, a personal-best record per map and physics, an acceleration meter (CGaz), speedometer and jump counter.

## Plan

The game code is id's, run through q3core, so the entities and the timer are new `qagame` code in a patch (`0006-q3core-race.patch`), written from the entity docs (the Defrag mod's own code is closed). Race is a mode like the others: data in `godot/game/modes/race.tres`, loaded into the Gameplay scene.

| Phase | Work | Claude time |
|---|---|---|
| **D1 Entities and timer** (done 2026-10-25) | Patch 0012 (`g_race.c`): spawn functions for the Defrag entities above (names matched without case, as the maps write them), a per-client run timer (start, checkpoints, stop, restart on leaving the start), reported to Godot with a `race` server command (`start`, `cp n ms`, `stop ms`) that `Q3Server` exposes. `g_race` cvar: players non-solid to each other, no damage to self, unlimited ammo, no items needed, no match end. `target_init` and the shooters that lead the player. The `.defi` fields are read into the arena's data (not yet; D3). Built: all of the above except `target_init` and the shooters, which are written but untested, and `target_multimanager`'s `count` (random subset) and team flags. The `target_speed` rules are read from the entity docs and unverified against the mod. `tests/race_test.tscn` (headless, 15 checks) compiles a corridor with q3map and runs it: timer, checkpoints, stop and record, the multimanager's delays, the frags filter, a velocity pad, `target_speed`, the reset at a new life, a rocket jump without health loss and unlimited ammo. Players still collide with each other in a race (a tracemask change would need the prediction paths too); Race is one human for now. | 3 h |
| **D2 Physics** (done 2026-10-25) | VQ3 = Classic ruleset, forced by the race mode, so the movement matches the mod's. No CPM (Ozmium, 2026-10-25). Maps whose `.defi` says CPM-only are left out of the list, not played with the wrong physics. Built: the Race setup sets `g_ruleset classic` and `g_race 1` (`Q3MatchSetup.game_cvars`); Gameplay puts both back to the defaults for the next non-race match. | 0.25 h |
| **D3 Mode and menu** (done 2026-10-25) | `race.tres` (map type `race`, one player, no bots, no frag or time limit). The menu's Play page gets Race: a map list (style, physics, your best time, the course's checkpoints), Restart and Practice. Maps appear through `.defi` info, or a map's published race settings. Records: `Q3RaceRecords` (`user://race/records.json`, one per map). Built: `race.tres`; the Maps folder's `.defi` files (`Q3MapPacks`) make race maps (style, author, long name; CPM-only maps left out); the menu's Play > Race page (`RacePage.tsx`: courses with style filters, your best on each, Run, Open the Maps folder); Race is not offered in Create Match, Skirmish or the dedicated page. Not built: a Race tab in the profile's career stats, and the editor's published maps as courses (D5). | 2 h |
| **D3b Single Player and playlists** (done 2026-10-25) | Race moved from Play to Single Player. The Race page ticks courses into a playlist (order shown, saved in the menu's local storage) and Run plays them in order: `race_queue` (menu bridge) fills `Session.race_queue`, and after each finish Gameplay starts the next course 5 s later (R repeats the current one). `Q3RaceRecords` keeps your ten fastest finishes a course (`runs`, `board()`); the Leaderboards page has a Race tab (`RaceBoard.tsx`) listing every course with its best and the top ten. Local only: an online board is D7. | 1.5 h |
| **D4 HUD, ghosts and replays** (done 2026-10-25) | HUD: big timer, checkpoint splits against your best, speedometer, jump count, optional CGaz meter. Ghost: the existing replay recorder (`Q3Replay`) saves each run, and the best run replays as a translucent player (same model as the player, `Q3Client` body from the feed) during the next one. Instant restart, savepos/loadpos. The run itself is a replay, so the replay viewer and clip export work on it for free. Built: the timer HUD (`Q3HudRace`: timer, checkpoint splits against your best, best time, the finish with NEW RECORD or the gap, practice note); `R` restarts (`racerestart`: a respawn that is never a wait), `F5` saves a spot and `F9` returns to it with the timer off (`racesave`, `raceload`); the ghost is its own sampler (`Q3RaceGhostRecorder`, about 60 samples a second, `user://race/ghosts/<map>.ghost`) played by `Q3RaceGhost` (your model in a cool tint, faded) from each start, not the replay recorder (decoding a replay to find one player's path was more work than sampling). Gameplay's own replay recording still records the match, `race ...` commands included. Not built: a Settings > Controls entry for the three keys (they are fixed), the acceleration meter, a jump counter. | 2.5 h |
| **D5 Map editor** (done 2026-10-25) | See below. Built: all twelve Defrag classes in the Entities tab (Race group, orange) with their keys and flags in the inspector; the start line (Y), finish line (U) and checkpoint (C) tools; Shift+C checks the course, Shift+Y sets its style and author; a RACE menu beside PLAY; a course publishes as Race only and exports with its `.defi`; its play-test is a run. Not built: prefabs from Defrag's example maps, a check that checkpoints are reachable in order or that a door can be shot open from outside the spawn room (the editor has no way to play the course out). | 3 h |
| **D6 Starter courses** (paused 2026-10-25) | Four original courses built in the editor, one of each style, so the mode ships playable. **Paused:** Ozmium found the first four (straight halls, `godot/tools/race_courses.gd`, test `tests/race_courses_test.gd` unfinished: its script has a parse error and the gap runner's third gap was too long) too plain and wants popular Defrag maps instead, which can't be fetched while q3df.org is down and need each author's terms. The built pk3 sits in `work/race_courses/`, not in `Maps/`, so nothing ships. Players drop their own courses into the Maps folder. | 2 h |
| **D7 Online records** | The Cloudflare hub gets a per-map record table. Times are only trusted with the run's replay (the hub re-reads the usercmds); until then a self-reported board is a joke board, so this phase waits for MULTIPLAYER_PLAN's verified runs. | 2.5 h |
| **D8 Tests and docs** | `tests/race_test.tscn` (headless): the starter maps' timers start, split and stop on a scripted run; both physics reproduce known jump heights; a replay of the run plays back with the same time. Docs: CLAUDE.md, PLAN.md, README (only once built). | 1 h |

**About 14 h** of Claude time. D1 to D4 and D8 make the mode playable on any Defrag map; D5 and D6 add the editor and the shipped courses; D7 is last.

## The map editor's race elements (D5)

- **Entity catalog** (`scripts/map/q3_map_entities.gd`): every Defrag entity with its keys and spawnflags from `defrag.def`, under a Race group in the entity palette.
- **Race tools:** Start line, Finish line and Checkpoint each draw the trigger volume (the brush, textured as a trigger) and its target entity, and link them in one gesture. In Defrag's own workflow that is a trigger, a target and Ctrl+K for each. Checkpoints number themselves in order.
- **Course checks** (a Race panel): exactly one start, at least one finish, checkpoints reachable in order, a player spawn, items with `wait -1` that nothing respawns, a door a player can shoot open from outside the spawn room. Each problem selects its entity.
- **Race settings** (map settings dialog): style, author, long name (the physics is always VQ3). They are written into the BSP's worldspawn and published as the map's `.defi`-style info.
- **Play-test as a run:** the editor's play button in Race plays the course with the timer, splits and ghost.
- **Import and export:** the `.map` and BSP importers already keep every key and value; Defrag maps open as they are. Export writes the same keys back, so a course round-trips to q3map. Unknown classes (the shooters) show as boxes with their keys until the catalog has them (D5 adds them).
- **Prefabs:** not built. The example maps' pieces (spawn room with door, accuracy target, velocity pad, shooter) would be the first ones; their licence is unstated, so they would be our own rebuilt versions.

## Maps and licensing

Defrag maps are community work with their own authors and terms, and most readmes forbid modification and redistribution. Ozmium's exception for the 15 duel maps (2026-10-25) covers those maps only. For race:

- **Players bring them.** The game loads race maps from the player's own folder (`user://maps`, or a pk3 dropped on the window as the ownership gate does) and imports them with the map editor's BSP import. That puts all of q3df.org in reach without shipping any.
- **We ship our own:** the D6 starter courses.
- **The bundle's example maps** are references for entity use, not shipped. Their licence is not stated, so they stay out of the repo (`defrag/` is not in the project).

## Decisions for Ozmium

1. **Which maps ship.** Recommended: only our four starter courses; players install the rest (above). Alternative: ask a handful of well-known authors for permission and ship those. A race map's author is easy to ask, unlike id.
2. ~~CPM physics~~: decided against by Ozmium, 2026-10-25. Race is VQ3 only, so CPM-only maps are out of reach.
3. **Online records (D7).** Recommended: local records and ghosts first, the shared board only once runs can be verified.
4. **Quake Live's Race mode.** QL_PLAN's "Not taken" list names it; this is Defrag's Race, not QL's. Recommended: skip QL's.
