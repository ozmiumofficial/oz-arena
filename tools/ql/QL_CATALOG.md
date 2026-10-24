# Quake Live (VQL) vs Quake III Arena 1.32: rules, physics, weapons, items, maps

Compiled 2026-10-23 for the Q3Remaster project. QL = Quake Live's final build (v1069, June 2016, the last update; Steam app 282440, dedicated server 349090). Q3 = id's Quake III Arena 1.32 game code (`tools/q3core/thirdparty/id-q3a/code/game`).

## How to read this

Every value carries a source tag:

| Tag | Source | Confidence |
|---|---|---|
| **[DLL]** | QL qagamex86.dll cvar table (defaults extracted from the installed game, `work/ql/qagame_cvars.txt`). Cross-checked against a `cvarlist` dump of build 1069 (vic7or777 `defaults_ql.txt`): every gameplay default matches except the cvars the FFA factory had set at dump time. | Verified (authoritative) |
| **[FAC]** | QL pak00 `scripts/factories.txt` (`work/ql/factories.txt`). Byte-for-byte equal in content to the cstewart90 gist and the dngrtech/qlsm factory index. | Verified (authoritative) |
| **[PAK]** | Read directly from the installed `pak00.pk3` (BSP headers, worldspawn keys, file listing, `arenas.txt`, mappools). | Verified |
| **[Q3]** | id's 1.32 game source in this repo. | Verified |
| **[PN]** | Official patch notes (Steam announcements, quakelive.com notes quoted on NeoGAF). | Official, but may be superseded by later builds |
| **[RE]** | Kr3m/QuakeLive, a source reconstruction from the retail binaries (HLIL/Ghidra). Its movement code (`bg_pmove.c`, `bg_slidemove.c`) is marked "closed after retail-backed runtime validation". Its own ledger calls its ammo pickup and cap tables "unverified Q3 defaults". | Medium (movement); low (items, ammo, anything not in the cvar table) |
| **[COM]** | Community claims: wikis, ESReality, Steam forums, Liquipedia. | Low to medium |

Where the **[DLL]** value exists it wins. "Unknown" means no source settled it.

**One big caveat about "standard VQL".** QL never had a single ruleset. In the Aug 2014 patch, id made a new default ruleset with autohop, bunny hop, a 100 ms jump interval, loadouts and global ammo, and kept a "Classic" ruleset for duel. The Steam-era factories keep that split [FAC]:

- The **cvar defaults** are the 2014 "Quake Live ruleset" physics: BunnyHop 1, CrouchStepJump 1, JumpTimeDeltaMin 100, swim 0.6 / wade 0.8.
- The **duel factory** puts back the classic values: BunnyHop 0, CrouchStepJump 0, JumpTimeDeltaMin 50, swim 0.5 / wade 0.75.
- Competitive duel, TDM, CTF and CA pickups have run "classic" movement (`pmove_BunnyHop 0`) ever since [COM, Steam forum].

So "VQL as competitive players know it" means the duel factory values. The remaster needs a decision on which one becomes the standard.

---

## 1. Movement and physics

### 1.1 Every pmove cvar

QL default from [DLL]. The duel column is from [FAC], blank if the duel factory doesn't override it. "Q3 1.32" is the hard-coded equivalent [Q3].

| Cvar | QL default | Duel | Q3 1.32 | What it does (mechanics source) |
|---|---|---|---|---|
| `pmove_JumpVelocity` | 275 | | 270 (`JUMP_VELOCITY`) | Base upward speed of a jump. |
| `pmove_JumpTimeDeltaMin` | 100 ms | **50** | none | Minimum time between two jumps; a jump attempted sooner is refused [RE]. The Aug 2014 notes say "increased from 50ms to 100ms" [PN]. The same value is also the "recent ground contact" window for step jumps [RE]. |
| `pmove_JumpVelocityScaleAdd` | 0.4 | | none | Vertical boost for a quick second jump. If the time since the previous jump is no more than TimeThreshold + Offset, the jump speed is multiplied by (1 + ScaleAdd), giving 275 × 1.4 = 385 ups, capped at JumpVelocityMax [RE]. This is QL's CPMA-style "double jump" (CPMA adds +100 ups within 400 ms). |
| `pmove_JumpVelocityTimeThreshold` | 500 ms | | none | Window for the boost above [RE]. |
| `pmove_JumpVelocityTimeThresholdOffset` | 0.6 | | none | Added to the threshold, so the effective window is 500.6 ms [RE]. Why a 0.6 offset exists is unknown. |
| `pmove_JumpVelocityMax` | 700 | | none | Cap on vertical takeoff speed after every boost (chain, step) [RE]. |
| `pmove_ChainJump` | 1 | | none | Enables the chain-jump path [RE]. |
| `pmove_ChainJumpVelocity` | 110 | | none | [RE] applies it as a *minimum horizontal speed* (110 ups along the velocity or facing) on a chain jump. **Unverified.** Note that 275 + 110 = 385 = 275 × 1.4, so it may instead be the additive form of the same vertical boost. Test in QL before porting. |
| `pmove_StepJump` | 1 | | none | Stair jump (below). |
| `pmove_StepJumpVelocity` | 48 | | none | Extra upward speed on a step jump: +48, capped at 700 [RE]. |
| `pmove_CrouchStepJump` | 1 | **0** | none | Allows step jumps while ducked (below). |
| `pmove_StepHeight` | 22 | | 18 (`STEPSIZE`) | Maximum step-up height. QL steps 4 units higher. |
| `pmove_AirSteps` | 1 | | (Q3 has a fixed equivalent) | With ≤ 0, stepping up in the air is refused unless the projected end point finds walkable ground within StepHeight [RE]. Q3 1.32 already refuses a step while moving upward with no support below (`bg_slidemove.c:254`). The PQL bundle uses 3. |
| `pmove_AirStepFriction` | 0.03 | | none | When a step-up happens in the air while rising, horizontal speed is multiplied by (1 − 0.03) [RE]. This is a small speed loss for air steps. |
| `pmove_AirAccel` | 1.0 | | 1.0 (`pm_airaccelerate`) | Air acceleration. |
| `pmove_WalkAccel` | 10 | | 10 (`pm_accelerate`) | Ground acceleration. |
| `pmove_WalkFriction` | 6 | | 6 (`pm_friction`) | Ground friction. Stop speed stays 100 [RE]. |
| `pmove_AirControl` | 0 | | none | **Not a strength: a switch.** Any value above 0 swaps in a hard-coded PQL ("Turbo") bundle [RE]: air control 150, air accel 1.1, air stop accel 2.5, circle-strafe friction 5.5, strafe accel 70, walk accel 15, air steps 3, wish speed 35. Air control is CPMA's: `k = 32 × aircontrol × dot² × frametime`, applied when forward is held [RE]. The race and vca factories turn it on. |
| `pmove_AirStopAccel` | 1.0 | | none | PQL only: the acceleration used when the wish direction opposes velocity [RE]. |
| `pmove_StrafeAccel` | 1.0 | | none | PQL only: the acceleration for pure sidestep (A/D) in the air [RE]. |
| `pmove_WishSpeed` | 400 | | none | PQL only: the wish-speed clamp for pure sidestep in the air (the bundle uses 35) [RE]. |
| `pmove_CircleStrafeFriction` | 6.0 | | none | Ground friction while holding forward plus strafe diagonally. In [RE] it applies only while the PQL bundle is active, so in VQL it equals walk friction. |
| `pmove_AutoHop` | 1 | | none (jump must be released) | Hold jump to keep jumping; no release needed [RE][PN]. Update 23 (Apr 2015): "Enabled autoHop in all modes"; clients can opt out with `cg_autoHop 0` [PN]. |
| `pmove_BunnyHop` | 1 | **0** | none | Aug 2014 notes: holding forward and jumping lets players "slowly gain up to 2x their base movement speed" without strafing [PN]. [COM] also says it affects air movement while holding forward and makes movement fps dependent. **The formula is unknown**: [RE] only uses it to drop the jump-release requirement and hasn't reconstructed the speed gain. |
| `pmove_DoubleJump` | 0 | | none | Optional mid-air second jump, once per airtime, needing a fresh press [RE]. A "Create Match" option since 2014 [PN]. |
| `pmove_RampJump` | 0 | | none | Takeoff from a sloped surface (normal z < 0.999) scales horizontal speed by RampJumpScale [RE]. On in race and vca. |
| `pmove_RampJumpScale` | 1.0 | | none | See above. |
| `pmove_CrouchSlide` | 0 | | none | CPMA-style crouch slide (off in every stock factory) [RE]. Slide time charges at 5 × msec while not ducking, up to CrouchSlideTime, and drains while sliding on the ground. While sliding, friction is CrouchSlideFriction and the duck speed limit is 0.75 instead of 0.25. |
| `pmove_CrouchSlideFriction` | 0.5 | | none | See above. |
| `pmove_CrouchSlideTime` | 2000 ms | | none | See above. |
| `pmove_WaterSwimScale` | 0.6 | **0.5** | 0.50 | Swim speed scale. |
| `pmove_WaterWadeScale` | 0.8 | **0.75** | 0.70, declared but **unused** in Q3 1.32 | QL applies it at waterlevel 1. Q3 only scales by (1 − 0.5 × waterlevel/3) [RE][Q3]. |
| `pmove_WeaponDropTime` | 200 ms | | 200 | Weapon lower time. |
| `pmove_WeaponRaiseTime` | 200 ms | | **250** | Weapon raise time. QL switches 50 ms faster. Race and vca use 10 / 10. |
| `pmove_noPlayerClip` | 0 | | none | Players pass through each other (race; forced on by instagib) [RE][FAC]. |
| `pmove_velocity_gh` | 800 | | 800 (hook pull) | Grapple pull speed. |
| `g_speed` | 320 | | 320 | Run speed. |
| `g_gravity` | 800 | | 800 | Gravity. |
| `g_gauntletSpeedFactor` | 1.0 | | none | Run-speed factor while holding the gauntlet. Aug 2014 notes announced +6.25% [PN], but the shipped default is 1.0 (no bonus). |

### 1.2 Step jumps, exactly [RE]

Inside `PM_StepSlideMove`, after a successful step-up (delta > 0), a step jump fires when all of these hold:

1. `pmove_StepJump` is on.
2. Jump is held (upmove ≥ 10), and the player isn't `PMF_RESPAWNED`.
3. The jump-release rule passes; with AutoHop or BunnyHop on, no release is needed.
4. The player touched ground within `JumpTimeDeltaMin` ms (100, or 50 in duel), from the ground-trace history.
5. A trace at the projected end point (origin + velocity × frametime), from +StepHeight down to −StepHeight, finds walkable ground (normal z ≥ 0.7).
6. **Crouch variant** (needs `CrouchStepJump`): ducked, no ground plane, vertical speed ≥ 0, and a 64-unit downward trace from the origin (box shrunk by 1 unit) hits nothing.

The jump then takes off normally (with the chain boost if it qualifies), and **+48 ups** is added to vertical speed, capped at 700. In practice, jumping as you run up a stair or onto a low ledge gives about 323 ups instead of 275.

### 1.3 Chain / double jump timing [RE]

- t = command time − time of the last takeoff (`ps.jumpTime`).
- If t < JumpTimeDeltaMin, the jump is refused (upmove cleared).
- If t ≤ 500.6 ms, vertical takeoff = min(275 × 1.4, 700) = **385 ups**, and the chain flag is set.
- Otherwise vertical takeoff = 275.

The window counts from the previous **takeoff**, not the landing. Treat that detail, and the ChainJumpVelocity meaning, as needing a test in QL.

### 1.4 Frame rate, tick and netcode

- `sv_fps` 40 [DLL], locked read-only since v1066 [PN]. Q3 default is 20.
- There is **no `pmove_fixed` / `pmove_msec` cvar** in QL; neither name appears in the QLDS `listcvars` [COM, dngrtech]. Client `com_maxfps` defaults to 125 in the cvar dump.
- [RE] keeps Q3's 66 ms move chopping and velocity `SnapVector`, so QL physics may stay as frame-rate sensitive as Q3's. Whether QL forced a fixed physics rate is **unknown**.
- Hitscan lag compensation: `g_lagHaxMs` 80, `g_lagHaxHistory` 4 [DLL]. It rewinds MG, HMG, SG, LG, RG and CG traces by up to 80 ms [RE]. Q3 has none.
- Player hitboxes are cylinders: `g_playerCylinders` 1 [DLL], `g_playerModelScale` 1.1. Q3 uses axis-aligned boxes.

---

## 2. Weapons

### 2.1 Per weapon

All QL numbers are [DLL] unless tagged. Splash "radius" is in units. Reload is the refire interval. Knockback is a multiplier on damage. Q3 1.32 values are from [Q3].

| Weapon | Damage QL / Q3 | Splash dmg / radius QL / Q3 | Reload ms QL / Q3 | Projectile speed QL / Q3 | Knockback QL (cvar) | Notes |
|---|---|---|---|---|---|---|
| Gauntlet | 50 / 50 | none | 400 / 400 | melee | 1 (`g_knockback_g`) | |
| Machinegun | **5** / 7 (Q3 team: 5) | none | 100 / 100 | hitscan | 1 | QL TDM: 4 per bullet [COM: quake wiki, QSS `g_damage_mg_tdm`]. No such cvar is in the DLL table, so it would be hard-coded if true: unverified. `g_ironsights_mg` 1.0 scales spread while crouched [RE]. Spread: Q3 200; QL 150 per [RE] (unverified). |
| Heavy MG (new) | 8 | none | 75 | hitscan | 1 | Added Aug 2014 [PN]; spread 350 [RE]. |
| Shotgun | **5 × 20 pellets = 100** / 10 × 11 = 110 | none | 1000 / 1000 | hitscan | 1 | Fixed pattern, below. `g_damage_sg_outer` 5 (outer ring), `g_range_sg_falloff` 768, `g_damage_sg_falloff` 0 (off). QL TDM: 4 per pellet = 80 [COM]. |
| Grenade L. | 100 / 100 | 100 / 150 (Q3 100 / 150) | 800 / 800 | 700 / 700 | 1.10 | Rockets no longer detonate grenades [COM]. |
| Rocket L. | 100 / 100 | **84 / 120** (Q3 100 / 120) | 800 / 800 | **1000** / 900 | 0.90 enemy, **1.10 self** | `g_rocketsplashOffset` −10. `g_accelRate_rl` 16 / `g_accelFactor_rl` 1 (no acceleration by default). The 2014 leak said "splash 84 to 100", but that never shipped. |
| Lightning | **6** / 8 | none | 50 / 50 | beam, range 768 / 768 | **1.75** | Cell dumps: 7 and kb 1.50 in the Apr 2015 build (1013); 6 in the Aug 2014 notes [PN] and in 1069. `g_damage_lg_falloff` 0 (off), `g_range_lg_falloff` 768. |
| Railgun | **80** / 100 | none | 1500 / 1500 | hitscan | 0.85 | `g_headShotDamage_rg` 0, `g_railJump` 0. vca factory: reload 1250. |
| Plasma | 20 / 20 | 15 / 20 (same) | 100 / 100 | 2000 / 2000 | 1.10 enemy, **1.30 self** | [COM] says plasma "pre-steps" about 20 ft. The `g_accelRate_pg` 16 / `g_accelFactor_pg` 1 cvars exist, but the mechanism is unknown. |
| BFG | 100 / 100 | 100 / **80** (Q3 100 / 120) | **300** / 200 | **1800** / 2000 | 1 | |
| Grapple | 10 | none | 100 / 400 | `g_velocity_gh` 1800 / 800 | −5 (pulls) | |
| Nailgun (TA) | 12 × 10 nails | none | 1000 / 1000 | 1000 | 1 | `g_nailspread` 400, bounce 65% (on). Q3 TA: 20 per nail, 15 nails. Removed from most maps in 2014 [PN]. |
| Prox L. (TA) | 0 | 100 / 150 | 800 / 800 | 700 (Q3) | 1 | Timeout 20 s. Removed from all maps in 2014 [PN]. |
| Chaingun (TA) | 8 / 7 | none | **50** / 30 | hitscan | 1 | Spread 700 to 1400 with spin-up [RE] (Q3 600). Removed from several maps [PN]. |

### 2.2 Knockback model

QL knockback = damage × per-weapon multiplier, capped at **`g_max_knockback` 120** (Q3 caps at 200). It is then scaled by `g_knockback` 1000 / mass 200 [DLL][RE]. On top of that, QL adds a fixed vertical kick: `g_knockback_z` 24 for knockback from enemies, `g_knockback_z_self` 24 for self-inflicted knockback [DLL]. Per [RE] it is added to the pushed velocity's z.

Result: rocket jumps are weaker than in Q3 and capped. Quad no longer multiplies jump height past the cap [COM].

Race and vca change these: `g_knockback_z` 40, `g_max_knockback` 160, PG 1.25, RL 1.10, `g_splashradius_pg` 32, `g_velocity_gl` 800 [FAC].

### 2.3 Shotgun pattern [RE, unverified]

Twenty fixed pellets (Q3: 11 random from a seed), traced 131,072 units out:

| Ring | Pellets | Angles | Offset |
|---|---|---|---|
| Inner | 6 | every 60° from 0° | 4000 (about 1.75°) |
| Middle | 6 | every 60° from 30° | 8000 (about 3.5°) |
| Outer | 8 | every 45° | 12000 (about 5.2°) |

[COM] (Quake wiki) describes a different split: 10 outer, 5 middle, 5 inner, with a 1000-unit spread radius and 1500-unit range. The two sources disagree, so verify in QL. `cg_trueShotgun` is visual only; the spread is fixed server-side [COM, ESR 2014].

### 2.4 Ammo

**Starting ammo** (`g_startingAmmo_*`) [DLL]. QL gives `g_startingWeapons` 3 (gauntlet + MG) with MG 100 bullets. Q3 1.32 gives 100 bullets in team mode, otherwise 50.

| Cvar suffix | Default |
|---|---|
| mg | 100 |
| sg | 10 |
| gl | 10 |
| rl | 5 |
| lg | 100 |
| rg | 5 |
| pg | 50 |
| bfg | 10 |
| hmg | 50 |
| cg | 100 |
| ng | 10 |
| pl | 5 |

**Pickup quantities.** QL from [RE] `bg_itemlist`: likely lifted from the binary, but **unverified**. Q3 from [Q3].

| Item | QL | Q3 |
|---|---|---|
| Shotgun weapon | 10 | 10 |
| MG weapon | 100 | 40 |
| GL weapon | 10 | 10 |
| RL weapon | 10 | 10 |
| LG weapon | 100 | 100 |
| RG weapon | 10 | 10 |
| PG weapon | 50 | 50 |
| BFG weapon | 10 | 20 |
| HMG weapon | 100 | none |
| CG weapon | 100 | 80 |
| Shells | 5 | 10 |
| Bullets | 50 | 50 |
| Grenades | 5 | 5 |
| Cells | 50 | 30 |
| Lightning | 50 | 60 |
| Rockets | 5 | 5 |
| Slugs | 5 | 10 |
| BFG ammo | 5 | 15 |
| Heavy bullets | 50 | none |

[RE] also has a second table: `bg_weaponStats` "pack/max" pairs (MG 50/200, SG 10/40, GL 5/20, RL 5/20, LG 60/240, RG 10/40, PG 30/120, BFG 15/60). The ledger calls those "unverified Q3 defaults". The RGBA floats in that table look like weapon colors, so the table's meaning is uncertain.

**Weapon pickup rule** (same as Q3) [RE][Q3]: a map weapon tops you up to its count if you have less, else gives 1 shot. TDM and dropped weapons give the full count.

**Max ammo: unknown.** Q3 caps every weapon at 200. The Quake wiki lists QL caps (RL 50, GL 50, BFG 50, LG 150, PG 150, RG 25), but those match the CA *starting* ammo and look like a mistake [COM]. [RE]'s 4× table above is unverified. Test in QL.

**Global ammo packs:** `g_ammoPack` 0 by default; 1 in CTF, 1FCTF, harvester and actf. Each pack gives some ammo for every weapon you hold [PN]. `g_ammoRespawn` 40 s by default (Q3 40); 10 in those factories [FAC].

`g_infiniteAmmo` is on in race, vca, `_rj` and `_sj`.

### 2.5 Per-gametype weapon differences

Only the race and vca factories change weapon cvars [FAC]:

- **vca:** `weapon_reload_rg` 1250, `weapon_reload_sg` 950, vampiric 0.75, infinite ammo, plus the race knockback set.
- **race:** pmove and knockback changes.

The TDM MG and SG reductions are **[COM] only**; nothing in the cvar table or factories implements them. Q3 1.32 itself uses MG 5 in TDM (`MACHINEGUN_TEAM_DAMAGE`).

---

## 3. Items and player rules

| Rule | QL | Q3 1.32 | Source |
|---|---|---|---|
| Armor system | `armor_tiered` 0: one armor pool, 66% absorption (`ARMOR_PROTECTION` 0.66), cap 2 × max health = 200 | Same: 0.66, cap 200 | [DLL][RE][Q3] |
| Armor items | Shard 5, **Green 25 (new)**, Yellow 50, Red 100 | Shard 5, Yellow 50, Red 100 | [RE] item table; Green Armor is also in tjone270's item list [COM] |
| Tiered armor (`armor_tiered 1`, PQL/Turbo, not VQL) | RA adds 150, cap 200, tier 2; YA adds 100, cap 150, tier 1; GA adds 50, cap 100, tier 0 | none | [RE]. Per-tier absorption is not in [RE] (CheckArmor always uses 0.66); [COM] says 75/66/50%. Unknown. |
| Armor decay | Above max health (100), −1 per second (not with tiered armor) | Same | [RE][Q3] |
| Health items | 5, 25, 50, Mega 100. The 5 and Mega stack to 2 × max (200); others cap at 100. | Same | [RE][Q3] |
| Health decay | Above 100, −1 per second | Same | [RE][Q3] |
| Mega Health respawn | 35 s, counted from pickup (`RESPAWN_MEGAHEALTH 35`). Maps can override with `wait` (Stonekeep's MH was set to 60 s in Update 23 [PN]). | Same, 35 s from pickup | [RE][Q3]. Whether QL waits for the health to wear off, as CPMA does, is not shown anywhere: [RE] times from pickup. |
| Starting health | 100 + `g_startingHealthBonus` 25 = 125, decaying to 100 | 125 (max + 25) | [DLL][Q3]. CA 200, RR 100, Infected 150, vca 300, all with bonus 0 [FAC]. |
| Starting armor | 0 (CA, AD, RR, DOM: 100) | 0 | [DLL][FAC] |
| Respawn times | Armor 25 s, health 35 s, MH 35 s, ammo 40 s (`g_ammoRespawn`), weapons **5 s in every mode** (`g_weaponRespawn`), holdables 60 s, powerups 120 s (`g_powerupRespawn`) | Same, except weapons 30 s in TDM (`g_weaponTeamRespawn`) | [DLL][RE][PN][Q3] |
| First powerup spawn | 45 s + random 0–15 s after match start (`g_spawnDelay_powerup` / `g_spawnDelayRandom_powerup`); keys 30 + 15 s (v1068) | Powerups spawn at start | [DLL][PN] |
| Powerup durations | Quad, BS, Haste, Invis, Regen 30 s; Flight 60 s | Same | [RE][Q3] |
| Quad factor | `g_quadDamageFactor` 3 | `g_quadfactor` 3 | [DLL][Q3] |
| Battle Suit | `g_battleSuitDampen` 0.25: takes 25% of damage (0.33 in CTF factories); immune to splash and falling damage | Takes 50% | [DLL][FAC][RE]. [COM] reads the 0.25 as "protects 25%"; [RE] multiplies damage by the value. |
| Haste | ×1.3 speed | ×1.3 | [RE] |
| Falling damage | 5 (medium) / 10 (far); `dmflags 8` turns it off | Same | [RE][Q3] |
| Self-damage | Half damage to yourself (after knockback) | Same | [RE][Q3] |
| `dmflags` in elimination modes | 28 (CA, AD, RR, instagib, race, practice) means no self-damage and no falling damage [COM, Steam forum]. Only 8 = no falling damage, 16 = fixed fov and 32 = no footsteps are confirmed [RE]; the 4 bit is unconfirmed. | | |
| Respawn | `g_respawn_delay_min` 2100 ms, `g_respawn_delay_max` 2400 ms [DLL]. Read as: earliest respawn by pressing fire, and a forced auto-respawn after 2.4 s [COM; the mechanism is not in RE]. Race and vca: 500 / 3500. | Can respawn after 1.7 s; forced after 20 s (`g_forcerespawn`) | [DLL][Q3] |
| Self-kill | `g_allowKill` 1000 (ms penalty): "Enabled self-kills with an additional 1 second respawn penalty" | `/kill` free | [DLL][PN] |
| Spawn protection | None by default (`g_spawnArmor` 0; with it on, `g_spawnArmorDmgScale` 0.5) | None | [DLL] |
| Weapon drop on death | Yes, including MG ("Machinegun now drops to grant users mg ammo"). Powerups drop (`g_dropPowerups` 1) and decay (`g_droppedPowerupsDecay` 1). | Yes, except MG and gauntlet; powerups don't drop in FFA | [DLL][PN][Q3] |
| Friendly fire | `g_friendlyFire` 0 everywhere (unified in 2014) | 0 | [DLL][PN] |
| Overtime | `g_overtime` 120 s periods by default (duel keeps it); most factories set 0, meaning sudden death | Sudden death | [DLL][FAC] |
| Warmup / ready-up | `g_doWarmup` 1, `g_warmup` 10 s, `g_warmupDelay` 15 s, `sv_warmupReadyPercentage` 0.51 (server.cfg), F3 = readyup | `g_doWarmup` 0, `g_warmup` 20 | [DLL][PAK server.cfg][Q3] |
| Timeouts | `g_timeoutCount` 0 per player by default, `g_timeoutLen` 60 s | none | [DLL] |
| Item pickup height | `g_itemHeight` 35 (taller pickup boxes, 2014); duel 15 | Q3 boxes | [DLL][FAC][PN] |
| Item timers | `g_itemTimers` 1 (in-world timers); duel 0 | none | [DLL][FAC] |
| Damage through surfaces | `g_forceDmgThroughSurface` 0 (vca 1), dampening 0.5 | none | [DLL][FAC] |
| Regen (DOM) | `g_regenHealth` / `g_regenArmor` 1250 ms delay, rate 133 | none | [FAC] |

---

## 4. Per-gametype factories [FAC]

Defaults that apply when a factory doesn't override them [DLL]: fraglimit 50, timelimit 0, capturelimit 8, scorelimit 150, roundlimit 10, roundtimelimit 180, mercylimit 0, g_overtime 120.

| Factory (basegt) | Overrides |
|---|---|
| ffa | timelimit 15, g_overtime 0 |
| **duel** | timelimit 10, fraglimit 0, g_itemHeight 15, g_itemTimers 0, pmove_BunnyHop 0, pmove_CrouchStepJump 0, pmove_JumpTimeDeltaMin 50, pmove_WaterSwimScale 0.5, pmove_WaterWadeScale 0.75 |
| tdm | g_loadout 1, g_dropCmds 4, timelimit 15, fraglimit 150 |
| ctf | g_ammoPack 1, g_ammoRespawn 10, g_suddenDeathRespawn 1, timelimit 15, g_battleSuitDampen .33, g_overtime 0 |
| ca | starting ammo sg 50, mg 100, lg 150, hmg 150, rl 50, pg 100, rg 25, gl 25; dmflags 28; armor 100; health 200 (bonus 0); weapons 8447; g_allowKill 0; g_overtime 0 |
| ft (freeze tag) | g_dropCmds 6, g_loadout 1, timelimit 15, fraglimit 0, g_overtime 0 |
| ad (attack & defend) | CA-style ammo; dmflags 28; armor 100; health 100; weapons 255; scorelimit 15; g_overtime 0 |
| dom | g_loadout 1; item spawning off (powerups, health, ammo, holdables, weapons, armor); regen health/armor 1250 ms at rate 133, armor after health; armor 100; weapons 1; scorelimit 150; g_domCapTime 4; timelimit 15 |
| har (harvester) | g_ammoPack 1, g_ammoRespawn 10, capturelimit 30, g_suddenDeathRespawn 1, timelimit 15, g_battleSuitDampen .33, g_overtime 0 |
| oneflag | CTF set plus g_throwFlagVelocity 300, g_flagBounce 0.5, g_flagPhysics 1 |
| race | pmove_noPlayerClip 1, AirControl 1, RampJump 1, JumpTimeDeltaMin 50, WeaponRaise/Drop 10; infinite ammo; weapons 145; knockback changes (section 2.2); respawn delay 500–3500; g_itemTimers 0; timelimit 8 |
| rr (red rover) | CA-style ammo, armor 100, health 100, weapons 8447, g_teamForceBalance 0 |
| actf | CTF set plus g_startingWeapons 515, g_runes 1 |
| iffa / ictf / ift (instagib) | g_instagib 1, weapons 65; ictf capturelimit 5 |
| infected (rr) | g_rrInfected 1, health 150, armor 0, weapons 7, g_knockback_pg_self 0 |
| quadhog (ffa) | g_quadHog 1, idle 30, timelimit 10, fraglimit 30 |
| vca | CA plus PQL movement, vampiric 0.75, infinite ammo, health 300, RG 1250 / SG 950 reload |
| _training, _rj, _sj | practice modes |

The 2014 public settings capped team sizes at 5 per side, set CA to 7 rounds, and played CTF and TDM at 15 minutes [PN]. The Steam-era factories leave roundlimit at 10.

---

## 5. Other gameplay changes from Q3

- **Hitboxes:** cylinders (`g_playerCylinders` 1), player model scale 1.1 [DLL].
- **Step height:** 22 instead of 18 [DLL].
- **Holdables:** the medkit and personal teleporter still exist and spawn (`g_spawnItemHoldable` 1) [DLL]. Kamikaze, invulnerability and portal remain from Team Arena code; which maps place them is unknown.
- **Team Arena items:** runes (scout, guard, doubler, armor regen) are off (`g_runes` 0, on only in actf); runes were removed from all maps in 2014 [PN]. The Prox Launcher was removed from all maps; Chaingun and Nailgun from several [PN].
- **Weapon arsenal:** the HMG was added. The BFG still exists with nerfed fire rate, speed and radius. Which maps place it is unknown map by map.
- **Loadouts / starting weapons:** `g_loadout` (TDM, FT, DOM). Since Update 23, players pick one extra starting weapon, and the GL is not offered [PN].
- **Map items:** MG removed from many CTF maps (Update 23); per-map duel item edits (Satanic: Quad replaced with MH in duel; Infinity: base YA became GA) [PN].
- **Damage plums and hit sounds:** cosmetic, server `g_damagePlums` 2 [DLL].
- **Antilag:** 80 ms hitscan rewind (section 1.4).

---

## 6. Competitive maps (INCOMPLETE: map research deferred by the owner, 2026-10-23)

This section stops where the research stopped. Treat the origin and license columns as a starting point, not a finished audit.

### 6.0 Duel map popularity: qlstats.net recent-games samples

qlstats.net's `/maps` statistics need a Steam login, which was not used. The public `/games?type=duel` list was sampled instead: 20 games per page, paged by `start_game_id`. The counts are games per map in each sample window [COM/measured].

| Map | Oct 20–24 2026 (3,000 games) | May 25–26 2024 (760) | Apr 19–20 2018 (1,000) | Feb 14 2017 (1,000) |
|---|---|---|---|---|
| bloodrun | 961 | 298 | 414 | 413 |
| aerowalk | 543 | 116 | 182 | 163 |
| sinister | 242 | 41 | 48 | 108 |
| campgrounds | 235 | 70 | 60 | 54 |
| toxicity | 208 | 42 | 46 | 47 |
| furiousheights | 155 | 25 | 34 | 37 |
| cure | 144 | 48 | 63 | 56 |
| hektik | 122 | 18 | 20 | 11 |
| f7tortured (Workshop, not in the pool) | 103 | 0 | 0 | 0 |
| battleforged | 57 | 6 | <4 | 7 |
| verticalvengeance | 55 | 11 | 4 | 12 |
| lostworld | 49 | 18 | 20 | 26 |
| silence | 38 | <4 | 4 | <4 |
| longestyard (not in the duel pool) | 9 | 19 | 47 | <4 |
| eyetoeye (not in the duel pool) | 15 | <4 | 12 | 28 |
| elder | 4 | 4 | 5 | 7 |

The other pool maps are near zero. Taking play data and tournament use (6.2) together, the top ten are:

1. Blood Run
2. Aerowalk
3. Sinister
4. Campgrounds
5. Toxicity
6. Cure
7. Furious Heights
8. Hektik
9. Lost World
10. Battleforged

Runners-up are Vertical Vengeance, Silence and Elder (a QuakeCon 2015/2016 map).

### 6.1 Official pools [PAK]

These are `mappool_*.txt` in the QLDS install, identical to the quakelive-server-standards copies.

| Pool | Maps |
|---|---|
| Duel (26) | aerowalk, battleforged, bloodrun, campgrounds, concretepalace, cure, delirium, dismemberment, elder, furiousheights, fuse, hektik, houseofdecay, mcsarges, phrantic, refinery, silence, sinister, solarium, sorrow, theedge, theepicenter, toxicity, useandabuse, verticalvengeance, wicked |
| TDM (23) | arcanecitadel, arkinholm, deepinside, devilish, diesirae, dredwerkz, foolishlegacy, grimdungeons, hiddenfortress, leftbehind, leviathan, limbus, purgatory, ragnarok, realmofsteelrats, retribution, solid, terminatria, terminus, theedge, theoldendomain, tornado, warehouse, wargrounds |
| CTF (40) | basesiege, beyondreality, bloodlust, campercrossings, citycrossings, coldcathode, courtyard, cursed, dividedcrossings, doubleimpact, duelingkeeps, electrocution, falloutbunker, finnegans, fluorescent, futurecrossings, gospelcrossings, industrialrevolution, infinity, innersanctums, ironworks, japanesecastles, jumpwerkz, lostparadise, newcerberon, overgrowth, pillbox, pulpfriction, railyard, rebound, reflux, scornforge, shiningforces, siberia, skyward, spacectf, spidercrossings, stonekeep, stronghold, thedukesgarden, troubledwaters, vortexportal, zen |
| CA and FFA | see `work/ql/mappool_ca.txt` and `mappool_ffa.txt` (60 and 44 maps) |

### 6.2 Tournament pools

From maps actually played, per the Liquipedia tournament pages [COM].

| Event | Duel maps |
|---|---|
| FnaticMSI BEAT IT (Nov 2010) | Furious Heights, Lost World, Aerowalk, Hektik, Blood Run, House of Decay |
| QuakeCon 2013 Duel | Furious Heights, Cure, Toxicity, Lost World, Blood Run |
| QuakeCon 2014 Duel Masters | Cure, Furious Heights, Toxicity, Lost World, Aerowalk, Sinister, Campgrounds ("Campgrounds Intel") |
| QuakeCon 2015 Grand Masters | Blood Run, Elder, Furious Heights, Lost World, Sinister |
| QuakeCon 2016 Duel | Sinister, Furious Heights, Blood Run, Cure, Hektik, Elder, Battleforged |
| 250 FPS QL League S4 (2025–26), the current community pool | Aerowalk, Battleforged, Blood Run, Delirium, Furious Heights, Hektik, Lost World, Silence, Sinister |
| QuakeCon 2014 CTF Open | Ironworks, Troubled Waters, Stonekeep, Spider Crossings, Pillbox, New Cerberon, Infinity, Japanese Castles |

The ESL/QPL pools weren't checked; QPL was Quake Champions, not QL.

### 6.3 Map origins, authors and licenses

- **Author** is the BSP worldspawn `author` / `author2` key [PAK] unless tagged.
- **Origin** is the ESR "Quakelive maps names" list (2010) [COM] unless tagged.
- **License** is the readme of the free Q3 release on lvlworld [COM, readme text]; wording is paraphrased.
- The qagame DLL holds a legacy-name table (for example `qztourney7`, `qzdm6`), but the scan pairs each name with its neighbor's value. Use the ESR mapping instead.

| BSP (QL) | Origin / Q3 file | Author | Free Q3 version | Published terms |
|---|---|---|---|---|
| aerowalk | Hubster's hub3aeroq3, a conversion of Preacher's QW Aerowalk. The QL version is based on CPMA `cpm22` [COM Liquipedia]. | Mindi 'Hubster' Burji; Mattias 'Preacher' Konradsson | hub3aeroq3, hub3aeroq3a (lvlworld) | Internet/BBS distribution only; no CD or other distribution without written permission |
| almostlost | pro-q3tourney7, id's unfinished Q3 tourney map (QL qztourney6) | none in BSP; id Software [COM lvlworld] | pro-q3tourney7 (lvlworld), **not** in retail Q3 paks | none found |
| battleforged | phantq3dm1 (2008) | Tom 'phantazm11' Perryman | map_phantq3dm1_rev | Free electronic distribution, unmodified, readme included; ask before map packs or CD |
| bloodrun | ztn3tourney1 (2001; ztn3dm1 2000). QL internal name ztntourney1. | ztn (Sten Uusvali) | ztn3tourney1, ztn3dm1 | ©2001 Sten Uusvali, all rights reserved. Electronic transfer only, free of charge, no modification; no CD-ROM or commercial servers without prior permission. |
| campgrounds | q3dm6 (id) | id | q3dm6 in Q3 pak0; **pro-q3dm6** ("The Campgrounds II") in Q3 **pak6.pk3** [PAK, local Q3 install] | id's |
| concretepalace | QL exclusive (PPak 6) | Pawel 'ShadoW' Chrapka | none known | n/a |
| cure | QL exclusive | Ferdinand 'cityy' List | none known | n/a |
| delirium | unknown | Russell 'bst' Vint | unknown | unknown |
| dismemberment | hub3tourney1 [COM ESR]; lvlworld lists hub3dm1 | Mindi 'Hubster' Burji | hub3dm1 | Not to be used as a base; must ship with the unmodified readme; Internet/BBS only |
| elder | QL original (Update 23, 2015) | none in BSP; Adam 'SyncError' Pyle [PN] | none | n/a |
| furiousheights | QL exclusive (qztourney7) | none in BSP; id [COM] | none | n/a |
| fuse | unknown (likely QL original) | Ferdinand 'cityy' List | unknown | unknown |
| hektik | QL exclusive (PPak 1); a Q3 remake exists (ws.q3df.org) [COM] | Richard 'swelt' Jacques | community remake | unknown |
| houseofdecay | Team Arena mptourney1, Dreamcast dc_map06 (QL qztourney9) | none in BSP (id) | mptourney1 is in Team Arena | id's |
| mcsarges | holiday map; uses `ct_ct3t3` assets | Ferdinand 'cityy' List | unknown | unknown |
| phrantic | Quake 4 swq4dm1 | Richard 'swelt' Jacques | Q4 only | unknown |
| refinery | QL original (Update 22, 2014) | Justin 'StormShadow' Ingels | none | n/a |
| silence | QL original by cityy. `oxodm76` is an *unofficial* Q3 port by OXOTHuK, per lvlworld's review. | Ferdinand 'cityy' List | oxodm76 (unauthorized port) | no readme found |
| sinister | QL original (2012) [COM ESR] | Mike 'Yellack' Schreiber; Gary 'akm' Turner | none known | n/a |
| solarium | QL original (Update 22) | Tom 'Phantazm11' Perryman | none | n/a |
| sorrow | QL exclusive (PPak 8) | Pawel 'ShadoW' Chrapka | none known | n/a |
| theedge | unknown | none in BSP | unknown | unknown |
| theepicenter | unknown | none in BSP | unknown | unknown |
| toxicity | pukka3tourney2 ("Evolution", 1999) | Alexander Mader (thefury / sumatra) | pukka3tourney2 | ©1999. Free electronic distribution, readme included, no modification; no CD-ROM without prior written permission. |
| useandabuse | CPMA cpm3 "Use & Abuse"; remix map_cpm3a | fxr\|jude (50chickens) | map_cpm3a (FxR\|jude & swelt) | No commercial products or non-electronic mass distribution without permission |
| verticalvengeance | q3tourney4 (id) | id | q3tourney4 in pak0; **pro-q3tourney4** in Q3 **pak6** [PAK] | id's |
| wicked | q3jdm8, and CPMA cpm1a | fxr\|jude | q3jdm8, cpm1a | Design ©2000 FxR\|jude; not to be used as a base; no CD/floppy without permission |
| lostworld | q3dm13 (id) | id | **pro-q3dm13** in Q3 pak6 [PAK] | id's |
| provinggrounds | q3tourney2 (id) | id | **pro-q3tourney2** in Q3 pak6 [PAK] | id's |
| hellsgate | q3tourney3 | id | pak0 | id's |
| deepinside (TDM/CA) | ospdm5 [COM ESR]; lvlworld lists teddm1 by Teddy | J. Scott Drader | teddm1 | ©2002, free and unmodified only |
| wargrounds (TDM) | q3shw26 "Battlegrounds" | Pawel 'ShadoW' Chrapka | q3shw26 | **GPL v2 or later** (readme) |
| dreadfulplace (CA/TDM) | q3shw23 [ESR] / q3shw24 [lvlworld] | ShadoW | q3shw24 | **GPL v2 or later** |
| realmofsteelrats (TDM) | CPMA cpm4 | Jens Bergensten | in CPMA | unknown |
| leftbehind (TDM) | ct3dm5 | cityy | ct3dm5 | Free electronic, unmodified, no CD |
| devilish (TDM) | storm3tourney8 | StormShadow | storm3tourney8 | No commercial gain; ship the readme unaltered |
| terminatria (TDM) | bst3dm1 | Russell 'bst' Vint | bst3dm1 | Free electronic, unmodified |
| foolishlegacy (TDM) | nodm5 | Richard 'swelt' Jacques | nodm5 | No commercial products without permission |
| solid (TDM) | CPMA cpm29 | swelt | in CPMA | unknown |
| purgatory, limbus, arcanecitadel, terminus (TDM) | QL originals | akm, Yellack, frs | none | n/a |
| ragnarok (TDM) | QL original (Update 22) | cityy; Timo 'frs' Laakso | none | n/a |
| dredwerkz / retribution / grimdungeons / spacechamber | q3dm12 / q3dm7 / q3dm14 / q3dm18 | id | pak0 | id's |
| hiddenfortress | Dreamcast dc_map07 (qzdm20) | id | Dreamcast pack | id's |
| japanesecastles (CTF) | q3wcp1 / japanc | Mike 'g1zm0' Burbidge | japanc | No commercial products or non-electronic mass distribution without permission |
| troubledwaters (CTF) | q3ctf2 | id | pak0 | id's |
| infinity, ironworks, stonekeep (CTF) | QL originals (Infinity by cityy) | cityy / none in BSP | none | n/a |
| spidercrossings, pillbox, newcerberon (CTF) | q3wcp9 by Scancode / unknown / unknown | Dan 'Scancode' Gold / none / Ricardo Stevenson | Threewave pack (q3wpak1) | Threewave pack terms not checked |

**For the remaster:** id's maps have free Q3 versions the player already owns (pak0, and pak6 for the pro- maps):

- Campgrounds (pro-q3dm6)
- Lost World (pro-q3dm13)
- Vertical Vengeance (pro-q3tourney4)
- Proving Grounds (pro-q3tourney2)
- Hell's Gate, Dredwerkz, Retribution, Grim Dungeons and Space Chamber

Every community map's readme forbids modification and most forbid distribution beyond free download, so a remaster needs each author's permission. The exception is ShadoW's GPL maps (Wargrounds/Battlegrounds, Dreadful Place), which permit modification under GPL v2. QL-exclusive maps (Cure, Furious Heights, Sinister, Elder, ...) are id/Bethesda property with no free Q3 version.

### 6.4 File format facts [PAK]

- **Packaging:** QL ships everything in one `baseq3/pak00.pk3` (about 962 MB, 9,285 entries), a plain zip. The pak encryption was removed in the Aug 2014 patch ("Paks can now be opened like a zip file") [PN]. `bin.pk3` holds the game modules.
- **BSPs:** 149 maps under `maps/`, each with an `.aas`. All are **IBSP version 47** (Q3 is 46).
  - The header has **18 lumps**: Q3's 17 plus lump 17. Its size is 0 to 1,792 bytes, always a multiple of 128, which fits a 128-byte advertisement record (cell id, normal, four corner points, model name) [PAK; the record layout is an inference].
  - Advertisement textures are under `textures/ad_content`.
- **Textures:** 3,855 files in 101 folders, mostly `.jpg` (2,918), plus `.png` (539) and `.tga` (286).
  - QL reuses id's Q3 sets (`base_*`, `gothic_*`, `sfx`, `skies`, ...). **762 of Q3 pak0's 1,116 texture files are byte-identical**, 26 differ, and 964 share a name.
  - It adds per-map folders (`ct_cure`, `ct_silence`, `map_sinister`, `bst_delirium`, `phant*`, `shw`, `storm3dm*`, `se_*`, `ql`, ...).
  - Shaders: 147 `.shader` files in `scripts/`. A shader-by-shader diff against Q3 was not done.
- **Arena data:**
  - One `scripts/arenas.txt` (148 entries: `map`, `longname`, `type` such as "ffa duel tdm ca ft rr dom"), plus `extras.arena` and `holiday.arena`. Q3 uses per-map `.arena` files.
  - `levelshots/<map>.jpg` (326 files).
  - Map pools are `mappool*.txt` (`map|factory`); the factories are `scripts/factories.txt`.

---

## Gaps (unknown or unverified)

1. **BunnyHop:** the "up to 2× speed holding forward" formula. Not reconstructed anywhere.
2. **ChainJumpVelocity 110:** horizontal minimum ([RE]) or additive vertical boost? Also whether the 500.6 ms window counts from takeoff or landing.
3. **Physics rate:** whether QL locks it (no pmove_fixed cvar exists) or stays fps-dependent like Q3.
4. **Ammo:** max ammo per weapon, and whether [RE]'s pickup quantities (shells 5, slugs 5, cells 50, lightning 50, BFG 5, MG weapon 100) are real.
5. **Shotgun pattern:** [RE] 6/6/8 rings vs [COM] 5/5/10 rings with a 1500-unit range.
6. **TDM MG and SG damage** (4 and 4 per pellet, [COM]): no cvar implements them.
7. **Respawn delay:** the exact meaning of `g_respawn_delay_min` / `max`.
8. **Tiered armor:** absorption per tier.
9. **Mega Health:** whether the respawn waits for the health to wear off (CPMA-style) or counts from pickup.
10. **Plasma pre-step** and the `g_accelRate` / `g_accelFactor` mechanics.
11. **dmflags bit 4.**
12. **Map origins:** The Edge, The Epicenter, Delirium, Fuse, McSarge's, Pillbox; licenses for Silence, Almost Lost, the CPMA maps and the Threewave pack.

These can be measured in the installed game: a local listen server with `cg_drawSpeed`, `g_debugDamage`, demos.

## Sources

- QL installed files: `work/ql/qagame_cvars.txt`, `factories.txt`, `mappool*.txt`, `arenas.txt`, `server.cfg`; `C:\Program Files (x86)\Steam\steamapps\common\Quake Live\baseq3\pak00.pk3`, read directly.
- Q3 install: `C:\Program Files (x86)\Steam\steamapps\common\Quake 3 Arena\baseq3\pak0.pk3`, `pak6.pk3` (the pro- maps).
- id Q3 1.32 source: `tools/q3core/thirdparty/id-q3a/code/game`.
- QL 1069 client cvarlist dump: https://github.com/vic7or777/py_scripts (`quake3 clearcfg/defaults_ql.txt`). Older dumps: https://github.com/snappas/ScalaConfigCleaner, https://github.com/elken/games (`quake_live/ConfigCleaner/cvars.txt`, build 1013, Apr 2015).
- QLDS listcvars names and factory index: https://github.com/dngrtech/qlsm (`scripts/cvar-catalog/`).
- Default factories: https://gist.github.com/cstewart90/a1163bad196977e351eb
- QL reconstruction: https://github.com/Kr3m/QuakeLive (`src/code/game/bg_pmove.c`, `bg_slidemove.c`, `bg_pmove_jump.h`, `bg_misc.c`, `g_items.c`, `g_weapon.c`, `g_combat.c`; `docs/gameplay/parity/parity-ledger.md`; `docs/reverse-engineering/cgame-bg-parity-implementation-plan.md`).
- Steam patch notes (v1064 to v1069, Update 22, Update 23, Steamworks launch): https://api.steampowered.com/ISteamNews/GetNewsForApp/v2/?appid=282440 and https://steamcommunity.com/app/282440/announcements/
- Aug 2014 ruleset patch notes (quoted from the quakelive.com forum): https://www.neogaf.com/threads/coming-quake-live-patch-adding-awesome-button-loadouts-global-ammo-and-more.874403/
- quakelive-server-standards (cvar notes, id mappools): https://github.com/quakelive-server-standards/quakelive-server-standards
- tjone270 QL documentation (item ids): https://github.com/tjone270/Quake-Live/tree/master/documentation
- ESR threads:
  - weapons: https://esreality.com/post/2554129/quakelive-weapons-def/
  - LG: https://www.esreality.com/post/2809868/lightning-gun-s-damage-numbers/
  - ammo: https://www.esreality.com/post/2000839/ammo-system-and-weapon-control/
  - map names: https://www.esreality.com/?a=longpost&id=1933573
  - Sinister: https://esreality.com/post/2254509/ql-duel-map-sinister/
  - QL maps for Q3: https://www.esreality.com/post/2652151/re-quakelive-duel-and-other-maps-for-q/
- Damage table (2009): https://quakeliveuk.proboards.com/thread/44/damage-table
- Quake wiki (fandom) weapon pages, via the API, e.g. https://quake.fandom.com/wiki/Lightning_Gun_(Q3), `Shotgun_(Q3)`, `Machine_Gun_(Q3)`, `Rocket_Launcher_(Q3)`, `BFG10K_(Q3)`, `Battle_Suit`, `Heavy_Machine_Gun_(QL)`.
- Steam discussions:
  - dmflags 28: https://steamcommunity.com/app/282440/discussions/0/3818544693875709146
  - bunnyhop/autohop: https://steamcommunity.com/workshop/filedetails/discussion/538941289/483368433107172048/ and https://www.esreality.com/post/2788283/classic-servers-without-classic-moveme/
- Liquipedia (tournaments and map pages): https://liquipedia.net/arenafps/ (QuakeCon/2013, 2014, 2015, 2016 Quake Live; 250 FPS/QL League/Season 4; FnaticMSI/BEAT IT/Finals; Aerowalk, Blood Run, Battleforged, Cure, Elder, Furious Heights, House of Decay, Toxicity map pages).
- qlstats.net public recent-games lists, sampled 2026-10-24: https://qlstats.net/games?type=duel (with `start_game_id` 9500000, 4500000 and 2000000 for the older windows).
- lvlworld Quake Live conversions and readmes: https://lvlworld.com/quakelive/1, https://lvlworld.com/readme/id:800 (Blood Run Tourney), id:203, 836, 1717, 822, 1841, 2043, 2160, 2213, 32, 701, 784, 2084, 2077, 2120, 2121, 2131, 2184, 2190, 2208, 1508, 80, 44, 1279, 1470, 2158.
