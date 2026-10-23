# Audit: ioquake3 game module vs id's Quake III Arena 1.32

Date: 2026-10-22. Sources compared:

- id: `thirdparty/id-q3a` at `dbe4ddb1` (`code/game/*`, including id's copies of `q_shared.h`, `q_shared.c`, `q_math.c`)
- ioq3: `thirdparty/ioq3` at `87a99d18` (`code/game/*`, `code/qcommon/q_shared.h`, `q_shared.c`, `q_math.c`)

Method: `diff -u -w --strip-trailing-cr` on every file pair, then a read of every hunk. Ignored: renames, comments and typos, license text, include paths, 64-bit type fixes, `#ifdef MISSIONPACK` code, and refactors that are provably identical. The defines that replace literals were checked: `PLAYER_WIDTH` = 15, `DEFAULT_HEIGHT` = 32, `CROUCH_HEIGHT` = 16, `DEAD_HEIGHT` = -8, `INVUL_RADIUS` = 42, and `FL_TEAMMEMBER` = `FL_TEAMSLAVE` = 0x400. They are identical.

Scope: the game module only (what `SConstruct` builds into `qagame`, plus the `bg_*` files the engine compiles for `Q3Pmove`). The engine side (`botlib`, `cm_*` collision, `server/`) is also ioq3 and is **not** covered here.

Context that matters for several items: q3core does not run a cgame. Anything ioq3 moved from the game module into its cgame is simply gone unless Godot's presentation layer recreates it.

## Summary

| Recommendation | Count |
|---|---|
| REVERT to id's behavior | 13 |
| ASK Ozmium | 6 |
| KEEP | 92 (table rows; several rows group related fixes) |

Most of the diff is safe: buffer and format-string fixes, crash guards, spectator and vote fixes, and log/print changes. The movement code (`bg_pmove.c`, `bg_slidemove.c`) is effectively unchanged apart from one water-jump test. Weapon damage, spread, fire rates, item amounts and respawn times are unchanged.

### REVERT list

| # | File | Function | What changed |
|---|---|---|---|
| R1 | bg_pmove.c | PM_CheckWaterJump | Water jump now allowed when the space above the ledge holds water, fog or a trigger |
| R2 | g_combat.c | CanDamage | Splash line-of-sight checks 8 corners with Z offsets instead of 4 flat corners |
| R3 | g_combat.c | G_Damage | No hit beep when shooting shootable doors and buttons |
| R4 | g_combat.c | player_die | Items are tossed (then deleted) when dying in a nodrop volume |
| R5 | g_active.c | P_WorldEffects | Drowning gurp / drown sounds removed from the game module |
| R6 | g_team.c | Team_FragBonuses | Flag carrier no longer gets the carrier-danger bonus |
| R7 | g_team.c | Team_TouchOurFlag | Capture assist rules changed (capturer excluded, both assist types stack) |
| R8 | g_main.c | CheckExitRules | Frag/capture limit now ends a match with fewer than 2 players |
| R9 | g_trigger.c | SP_trigger_hurt | Every trigger_hurt is now toggleable by targeting |
| R10 | ai_main.c | BotInputToUserCommand | Bot move input normalized to full speed; vertical swim input enabled |
| R11 | ai_dmnet.c | BotSelectActivateWeapon | Bots may pick the grenade launcher (ahead of rail/rocket) to shoot buttons |
| R12 | ai_dmq3.c | BotMapScripts | q3tourney6 crusher button logic rewritten |
| R13 | ai_team.c | BotCTFOrders_FlagNotAtBase | CTF bot leader orders changed (attacker share, one order flipped) |

### ASK list

Decided by Ozmium on 2026-10-22: A2 and A5 use id's 1.32 code (in the patch); A1, A3, A4 and A6 keep ioquake3's behavior.


| # | File | Function | Question |
|---|---|---|---|
| A1 | g_client.c | ClientDisconnect | Tourney: a duelist leaving during intermission now restarts the map at once. Keep? |
| A2 | g_team.c | Team_FragBonuses | id's v1/v2 typo decides when the "carrier protect" bonus and Defense medal fire. Fix or keep id's? |
| A3 | g_main.c | CheckTournament | TDM warmup (only with g_doWarmup 1) now waits for a player on each team. Keep? |
| A4 | g_cmds.c | SetTeam | g_teamForceBalance no longer applies to the host or bots. Keep? |
| A5 | ai_dmnet.c | AIEnter_Battle_Fight | In 1.32 a bot that once fought "suicidally" never retreats again that map. ioq3 resets it. Keep id's? |
| A6 | ai_dmq3.c | BotCheckAttack | 1.32 bots ignore client 0 (usually the local human) in their friendly-fire check. Fix or keep? |

Recommended way to apply reverts: keep them as a patch file in `tools/q3core/` that `build_q3core.py` applies after fetching the pinned ioq3 commit, so a refetch can't silently drop them. Estimated effort for all 13 reverts plus a rebuild and self-test: about 3 hours.

---

## bg_pmove.c, bg_slidemove.c, bg_public.h, bg_local.h

| Function | id 1.32 vs ioq3 | Category | Impact | Rec |
|---|---|---|---|---|
| PM_CheckWaterJump | id refuses a water jump if the point 20 units above the ledge has *any* contents (water, slime, fog, trigger). ioq3 refuses only for SOLID, PLAYERCLIP or BODY. Swimming waist-deep into a submerged step now pops you out of the water (350 up, 200 forward) where 1.32 kept you swimming. | behavior change | noticeable (water maps) | **REVERT** (R1) |
| PmoveSingle | EF_FIRING is no longer set while in PM_NOCLIP. | cosmetic | none | KEEP |
| PM_WaterMove | `watertype == CONTENTS_WATER` became `&`. This code sits inside `#if 0` in both trees. | none | none | (no effect) |
| PM_SlideMove | `endVelocity` clipping is now guarded by `if (gravity)`. When gravity is false, `endVelocity` was uninitialized and never read, so results are identical. | bug fix | none | KEEP |
| PM_StepSlideMove | Unused `down_o`/`down_v` commented out. | cleanup | none | KEEP |
| pm_wadeScale | Removed; it was never read. | cleanup | none | KEEP |
| GAME_VERSION | `BASEGAME "-1"` = "baseq3-1", same string. | none | none | KEEP |

Verified unchanged: `trap_SnapVector` rounds to nearest in both (id: x87 `fistp` on Windows, `rint` on Linux; q3core: `Q_SnapVector` via SSE, `src/q3_api.c`). Frame-rate-dependent jump behavior should match 1.32 on Windows.

## bg_misc.c

| Function | id 1.32 vs ioq3 | Category | Impact | Rec |
|---|---|---|---|---|
| bg_itemlist | `0` replaced by `NULL` in model arrays; counts via `ARRAY_LEN`. | refactor | none | KEEP |
| BG_EvaluateTrajectory(Delta) | Error message prints trType instead of trTime. | cosmetic-print | none | KEEP |
| eventnames[] | Missing names added so the debug table lines up with the enum. | cosmetic-print | none | KEEP |

## g_weapon.c

| Function | id 1.32 vs ioq3 | Category | Impact | Rec |
|---|---|---|---|---|
| CheckGauntletAttack | A player in noclip can't gauntlet. | bug fix (cheat mode) | none | KEEP |
| SnapVectorTowards | `(int)v` / `(int)v + 1` became `floor` / `ceil`. 1.32 rounded wrongly for negative coordinates, shifting bullet impacts and missile explosion origins by up to 1 unit (1/32 m). Splash damage changes by at most about 1 point. | bug fix | tiny | KEEP |
| ShotgunPellet | Accuracy hit is logged before G_Damage instead of after, so a killing pellet counts. Damage is unchanged. | bug fix (stats) | none | KEEP |
| Weapon_LightningFire | Same accuracy reorder as the shotgun. Damage unchanged. | bug fix (stats) | none | KEEP |
| Bullet_Fire | Takes `mod` as a parameter; the machinegun still passes MOD_MACHINEGUN. | refactor | none | KEEP |

## g_missile.c

| Function | id 1.32 vs ioq3 | Category | Impact | Rec |
|---|---|---|---|---|
| G_BounceMissile | A grenade that comes to rest sets `s.time`, so ioq3's cgame keeps its spin angle instead of snapping it. Only matters if Godot reads `s.time` for resting grenades. | cosmetic | none | KEEP |

## g_items.c

| Function | id 1.32 vs ioq3 | Category | Impact | Rec |
|---|---|---|---|---|
| RespawnItem | Null guards on the entity and the team chain walk. | crash fix | none | KEEP |
| G_CheckTeamItems | Missing newline added to warnings. | cosmetic-print | none | KEEP |

Verified unchanged: pickup amounts, respawn times, Touch_Item, Pickup_*, team item randomization.

## g_combat.c

| Function | id 1.32 vs ioq3 | Category | Impact | Rec |
|---|---|---|---|---|
| CanDamage | id traces to the target's center, then to 4 corners offset ±15 in X/Y at the center's height. ioq3 traces to 8 corners offset ±15 in X, Y **and Z**. Splash now reaches targets over ledges, around floor edges and past low cover more often. Affects rocket, grenade, plasma and BFG splash. | behavior change | noticeable | **REVERT** (R2) |
| G_Damage | The PERS_HITS counter (the "hit" beep) now requires the target to be a client. In 1.32, shooting a shootable door or button with health also beeped. | behavior change | tiny | **REVERT** (R3) |
| player_die | id: in a nodrop volume, drop nothing and return any carried flag directly. ioq3: always TossClientItems; G_RunItem deletes the items on the next frame and returns flags. The outcome is nearly the same, but one frame of dropped items and a brief "flag dropped" status now exist. | behavior change | tiny | **REVERT** (R4) |
| player_die | Gib-in-nodrop check reads contents later; same position, same result. | refactor | none | KEEP |
| CheckArmor/knockback | Unused `save` removed. | cleanup | none | KEEP |

## g_active.c

| Function | id 1.32 vs ioq3 | Category | Impact | Rec |
|---|---|---|---|---|
| P_WorldEffects | id plays `sound/player/gurp1/2.wav` on each drowning tick, or `*drown.wav` on the tick that will kill. ioq3 removed these from the game and moved similar logic into its cgame (pain/death events while underwater). q3core runs no cgame, so drowning is currently silent apart from whatever Godot adds. The matching precache in G_InitGame was removed too. | behavior change | noticeable (audio) | **REVERT** (R5) |
| SpectatorThink | Free spectators use PM_SPECTATOR (PM_NOCLIP with noclip); a broken follow state uses PM_FREEZE. id always used PM_SPECTATOR. | spectator fix | none | KEEP |
| ClientThink_real | `trap_Cvar_Update` after clamping pmove_msec, so the clamp takes effect at once. | bug fix | none | KEEP |
| ClientEndFrame | EF_CONNECTION is set on `ps.eFlags`. In 1.32 it was set on `s.eFlags` and then overwritten by BG_PlayerStateToEntityState, so it never reached clients. It now works (a lagging player can show the "connection" icon if Godot draws it). | bug fix | none | KEEP |
| SpectatorClientEndFrame | A follower whose target disappears is moved back to free spectating correctly. | spectator fix | none | KEEP |
| ClientEvents (EV_USE_ITEM teleporter) | Passes `isbot = qfalse` to SelectSpawnPoint; see g_client.c. | refactor | none | KEEP |

## g_client.c

| Function | id 1.32 vs ioq3 | Category | Impact | Rec |
|---|---|---|---|---|
| SelectRandomFurthestSpawnPoint, SelectRandomDeathmatchSpawnPoint, SelectInitialSpawnPoint, ClientSpawn | id picked from the furthest half of all free spawns and re-rolled if a spot was flagged nobots/nohumans (could loop forever if none fit). ioq3 filters flagged spots first, then picks from the furthest half. The list grew from 64 to 128 and gained overflow guards. Only maps with nobots/nohumans spawns (q3dm0) see a slightly different spawn distribution. The random formula is unchanged. | bug fix | tiny | KEEP |
| ClientSpawn | Reordered: spectators no longer fire spawn-point targets; no KillBox or targets during intermission; the "teleport in" event moved here from respawn()/ClientBegin and is created before the drop-to-floor think (effect up to 9 units higher); dead players respawned at intermission get no teleport effect. | bug fix / cosmetic | none | KEEP |
| ClientCleanName | Rewritten. Same rules (no leading spaces, at most 3 spaces in a row, no black color) with correct color-code counting. | cosmetic | none | KEEP |
| ClientUserinfoChanged | Invalid userinfo now drops the client. Bot configstring team uses sessionTeam instead of re-running PickTeam. Color/team strings use bounded copies. | security fix / bug fix | none | KEEP |
| ClientConnect | If the slot is still in use, ClientDisconnect runs first (a fast reconnect could lose a carried flag). Session data is read after the bot flag is set. | bug fix | none | KEEP |
| ClientDisconnect | Guard against a double disconnect. | crash fix | none | KEEP |
| ClientDisconnect | Tournament: if a duelist (TEAM_FREE) leaves during intermission, ioq3 issues `map_restart 0` immediately. 1.32 waited for intermission to end normally. | behavior change | tiny | **ASK** (A1) |

## g_session.c, g_main.c tournament queue

| Function | id 1.32 vs ioq3 | Category | Impact | Rec |
|---|---|---|---|---|
| AddTournamentPlayer, SortRanks, AddTournamentQueue, SetTeam | The tourney waiting line used `spectatorTime` (level.time when you became a spectator). level.time restarts every map, so newcomers on a new map could jump the line. ioq3 uses a queue number. The intended order (longest waiting plays next) is unchanged. | bug fix | tiny | KEEP |
| G_InitSessionData | Initial team now comes from userinfo `teampref` (bots, and the start-server menu via `g_localTeamPref`), applied through SetTeam. 1.32 read userinfo `team` for FFA spectating and used PickTeam for g_teamAutoJoin. End results match. **Integration note:** Godot's menu must send `teampref`, not `team`. | behavior change (plumbing) | none | KEEP |

## g_team.c

| Function | id 1.32 vs ioq3 | Category | Impact | Rec |
|---|---|---|---|---|
| Team_InitGame | 1.32 indexed the flag-status table with -1 for one frame. | bug fix | none | KEEP |
| Team_FragBonuses | 1.32 has two "carrier danger protect" blocks. The second (no check that the attacker isn't the carrier) let the flag carrier himself earn +2 and the Defense medal for killing someone who recently hurt him. ioq3 deleted the second block. | rules change | tiny | **REVERT** (R6) |
| Team_FragBonuses | Typo: 1.32 writes the attacker-to-carrier vector into `v1` twice, so `v2` still holds the attacker-to-flag-base vector. The "carrier protect" bonus (+1, Defense medal) fired when the attacker was near the carrier with the carrier seeing the victim, or near his own base with the carrier seeing him. ioq3 uses the intended victim-to-carrier and attacker-to-carrier distances. | bug fix | tiny | **ASK** (A2) |
| Team_TouchOurFlag | 1.32 ran the capturer through the assist loop (he could earn a return/frag assist and get the Assist medal instead of the Capture sprite), and gave at most one assist type per player (`else if`). ioq3 skips the capturer and lets both assist types stack (up to +3). | rules change | tiny | **REVERT** (R7) |
| Team_TouchOurFlag, Team_TouchEnemyFlag | `AddScore(CTF_TEAM_BONUS)` and `AddScore(CTF_FLAG_BONUS)` are now MISSIONPACK-only. In baseq3 both values are 0, so scores are unchanged; the only loss is a "0" score-plum event. | cosmetic | none | KEEP |
| PrintMsg | Bounded vsnprintf. | security fix | none | KEEP |
| TeamplayInfoMessage | Spectators following a player get that team's overlay. | spectator feature | none | KEEP |

## g_main.c

| Function | id 1.32 vs ioq3 | Category | Impact | Rec |
|---|---|---|---|---|
| CheckExitRules | 1.32 returns before the frag and capture limit checks when fewer than 2 players are playing. ioq3 removed that return. A lone player (others left, or a solo CTF capture run) can now end the match by hitting the limit. | rules change | tiny | **REVERT** (R8) |
| CheckExitRules | Negative or overflowing timelimit/fraglimit/capturelimit are reset to 0. | bug fix | none | KEEP |
| CheckTournament | g_warmup 0 or 1 now means no warmup restart (1.32 restarted immediately). Only with g_doWarmup 1. | bug fix | none | KEEP |
| CheckTournament | Team warmup check changed from `> GT_TEAM` to `>= GT_TEAM`: TDM warmup now needs a player on each team. 1.32 started TDM warmup with any 2 players. Only with g_doWarmup 1 (default 0). | rules change | tiny | **ASK** (A3) |
| CheckIntermissionExit | With only bots connected, 1.32 never left intermission. ioq3 uses the 10-second timeout. | bug fix | none | KEEP |
| BeginIntermission, MoveClientToIntermission | Dead clients are respawned and moved before the SP podium is built (1.32 could leave a gibbed player invisible on the podium). FindIntermissionPoint runs per client, which only matters on maps without info_player_intermission. | bug fix | none | KEEP |
| G_InitGame | CS_INTERMISSION cleared at init; out-of-range g_gametype is applied at once; client slots get a classname. | bug fix | none | KEEP |
| CheckTeamLeader | 1.32 could make two leaders (a human, then the first member again). | bug fix | none | KEEP |
| G_RunThink | `thinktime` is int instead of float (float loses precision after about 4.6 hours of uptime). | bug fix | none | KEEP |
| ExitLevel | Handles `nextmap "map_restart 0"` with the d1/d2 rotation idiom. | server admin | none | KEEP |
| gameCvarTable | g_doWarmup gains CVAR_ARCHIVE; g_logsync renamed; g_localTeamPref added. | config | none | KEEP |
| G_LogPrintf | Log timestamps relative to match start. | cosmetic-print | none | KEEP |

## g_cmds.c

| Function | id 1.32 vs ioq3 | Category | Impact | Rec |
|---|---|---|---|---|
| SetTeam | g_teamForceBalance is skipped for the local host and for bots. In 1.32 they were refused like anyone else. Only when g_teamForceBalance is 1 (default 0). | rules change | tiny | **ASK** (A4) |
| SetTeam | No body left for clients not yet connected; tourney line reset only on a real team change; team broadcast and ClientBegin skipped for clients not yet in game. | bug fix | none | KEEP |
| DeathmatchScoreboardMessage | Buffer cut to 1000 bytes (fits the 1024-byte command limit); no scores sent to bots. | bug fix | none | KEEP |
| ClientNumberFromString, Cmd_Tell_f, Cmd_GameCommand_f, Cmd_CallVote_f, Cmd_CallTeamVote_f, G_Say | Bounds checks (1.32 `gc` read past its array), newline/`;` injection blocked, players matched by clean name or number, host can't be vote-kicked, no new vote while a map change is pending. | security fix | none | KEEP |
| Cmd_Vote_f, Cmd_TeamVote_f | 1.32 tested `msg[1]` for 'Y'/'1', so "Y" and "1" counted as No. Fixed. | bug fix | none | KEEP |
| StopFollowing, Cmd_FollowCycle_f | Fixes dead view angles after following; dedicated follow modes cycle correctly. | spectator fix | none | KEEP |
| Cmd_LevelShot_f, ClientCommand | levelshot is local-only; a local client's early `team` command is accepted. | bug fix | none | KEEP |
| Cmd_Where_f | Prints currentOrigin. | cosmetic-print | none | KEEP |
| vsay/vtell/vtaunt | Moved under MISSIONPACK. baseq3's cgame never handled voice chats, so nothing was audible. | cleanup | none | KEEP |

## g_mover.c, g_trigger.c, g_misc.c, g_target.c, g_spawn.c, g_utils.c, g_svcmds.c, g_arenas.c

| Function | id 1.32 vs ioq3 | Category | Impact | Rec |
|---|---|---|---|---|
| SP_trigger_hurt | 1.32 only made a trigger_hurt usable when spawnflag 2 was set (a flag the editor docs mark unused), so targeting a normal trigger_hurt did nothing. ioq3 makes every trigger_hurt toggle when targeted, matching the docs. Maps built and tested on 1.32 may now behave differently. | behavior change | tiny to noticeable, map-dependent | **REVERT** (R9) |
| Touch_DoorTriggerSpectator, TeleportPlayer | Spectators pass through doors only when inside the door's own extent, and keep their view angle and speed (1.32 snapped the view through the door and pushed them at 400 u/s). | spectator change | none | KEEP |
| Think_SpawnNewDoorTrigger | Null guard. | crash fix | none | KEEP |
| Reached_Train | A train segment with near-zero duration gets 1 ms and is hidden (1.32 divided by zero). | crash fix | none | KEEP |
| G_SpawnGEntityFromSpawnVars | When a gametype filter removes a mover, its areaportal is opened. In 1.32 the portal stayed closed and could hide entities behind an open doorway. | bug fix | tiny | KEEP |
| G_SpawnEntitiesFromString | ENTITYNUM_NONE entity initialized. | bug fix | none | KEEP |
| G_Spawn, G_EntitiesFree | When the entity list is full, ioq3 reuses recently freed slots instead of erroring. | crash fix | none | KEEP |
| UpdateIPBans, Svcmd_* | Buffers zeroed, usage checks, newline on server say. | bug fix | none | KEEP |
| UpdateTournamentInfo | Postgame message length counted correctly. | bug fix | none | KEEP |

## g_bot.c

| Function | id 1.32 vs ioq3 | Category | Impact | Rec |
|---|---|---|---|---|
| G_AddRandomBot, G_SelectRandomBotInfo | bot_minplayers fill: 1.32 picked a random bot not already present. ioq3 picks a random bot among the least-used types via `addbot random`. ioq3 bug: `random() * (num - 1)` almost never picks the last candidate. Single-player arena bot lists are not affected. | bot change | tiny | KEEP (fix the off-by-one if bot_minplayers is used) |
| G_CountBotPlayers | Counts connecting bots and drops the spawn-queue double count, so bot_minplayers stops over-adding. | bug fix | none | KEEP |
| G_AddBot, Svcmd_AddBot_f | Client slot allocated first and freed on error; skill clamped to 1–5; default FFA team "free"; team passed as `teampref`. | bug fix | none | KEEP |

## ai_main.c

| Function | id 1.32 vs ioq3 | Category | Impact | Rec |
|---|---|---|---|---|
| BotInputToUserCommand | 1.32 projected the move direction onto view axes, so a bot moving diagonally to its aim got components below 127 and moved at about 71% speed. Keyboard actions were *added* (with signed-char wraparound), and vertical input used `abs()` on a float, which is 0 unless looking straight up or down. ioq3 rescales so the largest component is full speed, sets keyboard actions instead of adding, and uses `fabs`. Bots now strafe-while-aiming at full speed and swim up and down smoothly. | bot change | noticeable | **REVERT** (R10) |
| BotChangeViewAngles | `abs` → `fabs` in the bot_challenge smoothing path (bot_challenge defaults to 0). | bot fix | none | KEEP |
| BotAI_GetClientState callers, BotAISetupClient | Failure checks and null guards. | crash fix | none | KEEP |
| BotWriteSessionData/BotReadSessionData | formation_dist survives map restarts. | bot fix | none | KEEP |
| BotInitLibrary | maxclients from level.maxclients; "log" and "nochat" libvars get the right values (1.32 passed wrong strings). Bot chat is still gated by `bot_nochat` in ai_chat.c. | bug fix | none | KEEP |
| BotAI_Trace | Surface flags moved from `surface.value` to `surface.flags`; no game AI code reads either. | bug fix | none | KEEP |

## ai_dmnet.c

| Function | id 1.32 vs ioq3 | Category | Impact | Rec |
|---|---|---|---|---|
| BotSelectActivateWeapon | Grenade launcher added as an option for shooting buttons and doors, checked **before** railgun and rocket launcher. A bot holding GL and rail now lobs grenades at buttons. | bot change | tiny | **REVERT** (R11) |
| AIEnter_Battle_Fight | Clears BFL_FIGHTSUICIDAL. In 1.32 the flag, set once on "battle retreat: no way out", was never cleared, so that bot never retreated again until the map changed. ioq3 bots retreat normally. | bot change | noticeable over a long match | **ASK** (A5) |
| AINode_Seek_LTG (LTG_KILL) | "kill_done" only fires for a kill made after the order. | bot fix | none | KEEP |

## ai_dmq3.c

| Function | id 1.32 vs ioq3 | Category | Impact | Rec |
|---|---|---|---|---|
| BotMapScripts (q3tourney6) | 1.32: shoot the crusher button when an enemy is in the X/Y box below Z 672 and the bot isn't. ioq3: a different box (Z 480–680 inside the crush zone), in CTF only for the current enemy, and adds q3tourney6_ctf and mpq3tourney6. This changes when Xaero (and any bot) uses the crusher. | bot change | noticeable on q3tourney6 | **REVERT** (R12) |
| BotCheckAttack | Friendly-fire check `trace.ent > 0 && <= MAX_CLIENTS` became `>= 0 && < MAX_CLIENTS`. 1.32 bots ignored client 0 (normally the local human) and fired through you at enemies. ioq3 bots hold fire. | bot fix | noticeable in team games | **ASK** (A6) |
| BotWantsToCamp | Typo `inventory[INVENTORY_ROCKETS < 10]` fixed: a bot now needs 10+ rockets (not any) to count the RL as a camping weapon. | bot fix | tiny | KEEP |
| BotTeam, BotSameTeam | Read sessionTeam directly instead of the configstring. | bot fix | none | KEEP |
| BotFindEnemy | Skips players with notarget (cheat). | bot fix | none | KEEP |
| BotEntityVisible, BotIsDead | Ignore invalid entity info / failed state reads. | bot fix | none | KEEP |
| BotCheckEvents | Sound index bounds `>` → `>=`. | bug fix | none | KEEP |
| BotDeathmatchAI | Bots no longer send `team <name>` on first think; their team comes from `teampref` at connect. | bot plumbing | none | KEEP |
| BotTeamGoals (teamtaskpreference) | Driven only by Team Arena persistent powerups, which baseq3 never gives. | none | none | KEEP |
| BotWantsToRetreat/Chase (obelisk `\|\|`→`&&`) | MISSIONPACK-only. | n/a | none | KEEP |

## ai_chat.c, ai_cmd.c, ai_vcmd.c, ai_team.c

| Function | id 1.32 vs ioq3 | Category | Impact | Rec |
|---|---|---|---|---|
| BotCTFOrders_FlagNotAtBase | Passive mode with many teammates: attackers 70% → 60%. Aggressive mode with 2 teammates: teammates[0] is told "get flag" instead of "defend base". Changes how a bot team leader splits a CTF team when your flag is taken. Voice-order changes in this file are MISSIONPACK-only. | bot change | tiny to noticeable in CTF | **REVERT** (R13) |
| BotSortTeamMatesByBaseTravelTime | Null goal guard (1.32 could crash outside CTF); arrays zeroed. | crash fix | none | KEEP |
| BotValidChatPosition | Bots don't start chatting while holding the Battle Suit. | bot chat | tiny | KEEP |
| BotChat_EnemySuicide | Uses CHAT_ENEMYSUICIDE instead of CHAT_KILL for its chance (copy-paste fix). | bot chat | tiny | KEEP |
| BotChat_HitTalking | Weapon name now from the means of death (1.32 passed a client number). | bot chat | none | KEEP |
| ai_chat.c (vtaunt) | Bots' `vtaunt` commands moved under MISSIONPACK; baseq3 never played them. | cleanup | none | KEEP |
| FindClientByName etc. | `level.maxclients` replaces cached sv_maxclients; bounded string copies. | refactor | none | KEEP |

## q_shared.h, q_shared.c, q_math.c

| Item | id 1.32 vs ioq3 | Category | Impact | Rec |
|---|---|---|---|---|
| VectorNormalize | Length computed as `length * (1/sqrt(length))` instead of `sqrt(length)`. Differs by at most 1 ULP. | refactor | none | KEEP |
| SnapVector macro | Unchanged (truncation). | none | none | KEEP |
| Q_crandom / Q_random | Same LCG (69069, now unsigned); shotgun spread patterns are identical. | none | none | KEEP |
| BoxOnPlaneSide | x86 asm and switch replaced by a loop with the same math. | refactor | none | KEEP |
| Q_rsqrt, Q_fabs | Union instead of pointer casts. | refactor | none | KEEP |
| Q_IsColorString, ColorIndex | Color codes now require `^` + alphanumeric; affects name cleaning only. | cosmetic | none | KEEP |
| COM_Parse*, Info_*, Q_strncpyz etc. | Token-length and buffer fixes, line counting. | security fix | none | KEEP |
| Q_acos, Q_isnan, BoundsIntersect* | New helpers. | addition | none | KEEP |

## Notes outside the diff

- **Native DLL vs QVM.** 1.32's baseq3 normally ran as `qagame.qvm`, whose `rand()` came from bg_lib.c (LCG 69069, 15 bits). q3core builds native code, where bg_lib.c is compiled out and `rand()` is MSVC's CRT (also 15 bits, different sequence). Distributions match; exact random sequences (spawn picks, bot decisions, item team picks) don't. This is not fixable by reverting ioq3 changes and isn't worth changing.
- **Verify R1 on real maps.** The map importer has the shader data. Listing water brushes that sit directly above a solid ledge would show which stock maps feel the water-jump change.
- **Verify R9 on real maps.** Checking the stock .bsp entity lumps for any `trigger_hurt` with a `targetname` and without spawnflag 2 would show whether any shipped map is affected.
