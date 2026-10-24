# Trailer: plan (Ozmium 2026-10-23: the flyover and still footage is recorded in both games; the rest waits for his go, after replays)

Ozmium asked for a trailer that gets people excited about the project and makes them want to play it. It shows the new features and the graphical and technical improvements, with in-game footage, cinematic camera angles, a licensed track (below), and side-by-side comparisons against footage recorded in the original game. The edit is done in code, in a Next.js project.

## Status (2026-10-23)

- **Retail Quake 3 footage: recorded** (Ozmium: "work on recording the footage from quake 3"). The `q3cam` mod is built and verified, and every path below is captured: `work/trailer/retail/<path>.mp4`, 1920x1080, 60 fps, with `<path>_sheet.jpg` contact sheets.
  - **Flyovers, 6 s each:** q3dm17 (over the void past the rail platform), q3dm6 (across the central room, under the arch), q3tourney4, q3dm13, q3dm7, q3tourney2 (around the central tower), q3ctf4 and q3dm12. Plus `q3dm17_crane`, the final shot, which ends in black sky for the logo.
  - **Stills, 2 s each:** the intermission cameras of q3dm0, q3dm6, q3dm7, q3tourney2, q3ctf1 and q3dm13.
  - **Not recorded yet:** the weapon close-up (it needs a player holding a gun, not a spectator), and matched action from demos.
- **Remaster footage: recorded the same day** (Ozmium: "now we can record the fly over footage for the remastered version"). Every path flown in the remaster at Ultra: `work/trailer/remaster/<path>.mp4`, 1920x1080, 60 fps, with contact sheets. `work/trailer/compare/<path>.jpg` splits each pair with a comparison line and blends them 50/50; the blends show no doubled edges, so the two games line up frame for frame.
- **Fighters in the red mist (recorded 2026-10-23):** ten 5 s shots of fighters taunting in the main menu's showcase, at its four framings (`tools/trailer/showcase.json`, `capture_remaster.py --showcase`): Sarge, Xaero, Klesk, Doom, Anarki, Visor, Orbb, Uriel, Keel and Hunter.
- **All 35 maps rebaked (2026-10-23)** with q3map's two-sided surface lights and backsplash, then the remaster footage was recorded again.
- **The rough cut (2026-10-23):** `trailer/` (T1 and T2). The beat map puts the song's drop at 0:34.27, where the script already had "Fight!". The edit follows the song's sections: Act 1 on the intro and build; the flyover tour on the first loud section (bars 21-41); fights, fighters, deaths and effects on the second (41-64); the "feel" and feature segments on the breakdown (64-85); editor, online and the montage on the next loud section (85-105); the one-bar stop at 2:53; the crane and DOWNLOAD NOW on the last section. That makes it 3:21, the song's length. Placeholders mark what isn't recorded: the fights F1-F7 and the feature footage (T5).
- **Replays are built** (Ozmium's commit 64278990), so the choreographed fights (T6, T6c) can start.
- **Ozmium's notes on the rough cut (2026-10-23), acted on:**
  - Headlines in Teko 600.
  - "Fight!" must lead into a fight, not the levels.
  - Less repeated footage.
  - No space maps (The Longest Yard, Space CTF): indoor arenas like Campgrounds read better.
  - The spectator chase-cam matches weren't cinematic, so action now comes from bot-match replays filmed by a director (directed action, CLAUDE.md "Trailer facts"), until the choreographed fights are staged.
  - Also fixed on the way: light surfaces drew up to about 12x bright, and the map's baked lights lit the volumetric fog (Lost World drowned in yellow haze).
- **The paths are a first cut, laid out by hand.** The indoor ones orbit a point along each map's intermission camera view, the shot the level designers chose. q3dm12's is the weakest (cramped), and q3tourney4's passes close to a pillar.
- **Remaster side:** `tests/trailer_shot.tscn` (run by `tools/trailer/capture_remaster.py`) evaluates the same spline as `cam_path.evaluate` and `CG_CamEval`, drops items as the game does, renders offscreen at exactly 1920x1080 with `--fixed-fps 60`, and pipes raw frames into ffmpeg. Movie Maker was tried first: it took its size from the project's 1600x900 window and its PNGs made it about 6x slower (119 s against 20 s for a 6 s path).

**The goal is hype, not a feature list.** Every beat has to land on the music, and the comparisons have to make people say "wait, that's the same map?" The features get a second or two each, as fast cuts, never as a slideshow.

## What we have to work with

- **Original game:** the retail 1.32 `quake3.exe` in `C:\Program Files (x86)\Steam\steamapps\common\Quake 3 Arena`. Its renderer is the honest 1999 look, which is the point of the comparison.
  - `cl_avidemo <fps>` writes one TGA screenshot per frame at a fixed game rate (no sound, which we don't need).
  - `r_mode -1` with `r_customwidth` / `r_customheight` renders 1920x1080. `cg_draw2D 0` and `cg_drawGun 0` give a clean frame.
  - `setviewpos x y z yaw` can place the camera, but it takes only yaw, and it moves the player through `TeleportPlayer`, whose `EF_TELEPORT_BIT` turns interpolation off. So it can't fly a smooth path, and the trailer uses the `q3cam` mod instead (Footage, item 1).
- **Remaster:** Godot's Movie Maker (`--write-movie ... --fixed-fps 60`) records perfectly paced video with sound at any render cost, so footage can use maximum settings on the RTX 3060. `tests/record_preview.tscn` already records a bot match this way, and `tests/capture.tscn` already places cameras in id-space maps (`cam=x,y,z,lx,ly,lz`).
- **Same coordinates:** both games use id's map coordinates and the same `CG_CalcFov`, so a shot list in id units with one horizontal fov should frame identically in both. Verifying this is the first job of T4.
- **Music (Ozmium, 2026-10-23):** "Data Destruction" by Skyth x Boom Kitty. Ozmium has the artists' permission to use it. Quake 3's soundtrack isn't used. The file is in `work/trailer/music/Skyth x Boom Kitty - Data Destruction.mp3` (gitignored, never committed; copied from the Unity project's `Assets/Audio/OST`). It runs 3:21 at 44.1 kHz, longer than the 2:50 edit, so the trailer uses an edit of it cut on its sections. The edit is cut to its beat grid and structure.
- **Announcer:** `sound/feedback/` (prepare, fight, excellent, impressive, humiliation, perfect...). The trailer uses it as its voice.
- **Menu and web layer shots:** the composite method in CLAUDE.md ("For a picture of both"): 3D from Godot, page from headless Chrome with a transparent background, alpha-composited. A video version of it is needed for menu footage.
- **ffmpeg:** imageio-ffmpeg's ffmpeg 7.1 (no ffmpeg on PATH; the tools find it the way `make_cinematics.py` does).

## The editor: Remotion inside Next.js

"Edit the videos with Next.js" means **Remotion**: video compositions written as React components, previewed in the browser and rendered frame-exact to MP4 through ffmpeg. It fits next to the menu's Next.js 16 / React 19 stack.

- A new `trailer/` folder at the repo root: a Next.js app with the Remotion compositions, a preview page (`@remotion/player`, with a scrub bar and the beat grid drawn over it), and `npm run render`, which runs Remotion's renderer.
- **The edit is data:** `trailer/edit/trailer.json` lists the shots (clip, in and out points, which effect: cut, wipe, split, flash, speed ramp) on a beat grid. Changing the trailer means editing that file and re-rendering. No timeline dragging, and every version can be diffed.
- **Beat sync:** `tools/trailer/beats.py` finds the track's beats and downbeats with librosa and writes `beats.json`. Cuts snap to beats, and the big hits (the drop, the logo) snap to downbeats.
- **Components:**
  - `Wipe`: a before/after slider sweeping across matched shots (the classic remaster reveal).
  - `Split`: side by side with "1999" and "REMASTERED" labels.
  - `CompareLine`: the comparison line for the flyovers. Both flights play full-frame on top of each other, and one glowing line divides them: Quake 3 on one side, the remaster on the other. Its look:
    - A thin bright core with a soft glow in the menu's red accent, tilted a few degrees so it reads as motion, not as a split screen.
    - "1999" and "REMASTERED" ride along the top, one on each side of the line.
    - The map's name and code sit lower left (THE LONGEST YARD / q3dm17).
    - The line sweeps across during each flight on keyframes snapped to the beat, so the remaster takes over the frame as the camera moves. It can hold mid-screen, and it changes direction on the downbeat that cuts to the next map.
  - `FlashCut`: a white frame plus a light chromatic shake on a downbeat.
  - `Title`: kinetic text in the menu's look (Barlow, the menu's glass and red accents).
  - `SpeedRamp`: slow motion into and out of a frag.
  - `Transition`: the game-made transitions listed under "Script" (jump pad, rocket smash, rail wipe, teleporter flash, portal, card slide, quad pulse, whip pan, 1999 glitch), each driven by its own footage element.
  - `FeatureCard`: a headline plus the one line that explains it, over moving footage.
  - `EndCard`: logo, call to action and disclaimer.
- **Sound mix:** music, announcer lines and game sound from the Movie Maker tracks, with the game sound ducked under the music. Rendered in Remotion, with loudness normalized to about -14 LUFS for YouTube.
- **Output (Ozmium, 2026-10-23):** 1920x1080 at 60 fps, H.264. Both games' footage is captured at 1920x1080 and 60 fps, so nothing is scaled or retimed.
- Remotion is free for individuals and teams of up to three, so this project needs no license.

## Footage

Footage goes in `work/trailer/` (gitignored, like all of `work/`). Only the `trailer/` code, the shot lists and the edit file are committed. The finished video is uploaded, not committed.

1. **The retail camera mod, `q3cam` (Ozmium, 2026-10-23: modify Quake 3 for flyovers and capture from it):** id's 1.32 game source is GPL and in the repo (`tools/q3core/thirdparty/id-q3a`, with id's own `lcc` and `q3asm` and `cgame.bat`). So the retail game gets a small mod, built the way every 1.32 mod was: id's cgame and game code with a camera added, compiled to QVMs, packed in `q3cam/pak0.pk3` in the Quake 3 folder. Retail `quake3.exe` and its renderer are untouched, so the footage is the real 1999 look. `baseq3` is never touched either.
   - **cgame:** `cam_play <path>` and `cam_stop` console commands. While a path plays, the end of `CG_CalcViewValues` replaces the view origin, angles and fov with the path's spline at `cg.time`, so the flight is smooth at any frame rate, with pitch and roll.
   - **game:** while a path plays, every entity is sent as `SVF_BROADCAST`, so items, doors and lifts far from the spectator still exist in the snapshot. A spectator's own PVS would otherwise drop them.
   - **Capture:** `tools/trailer/capture_q3.py` installs the pk3, then launches `quake3.exe +set fs_game q3cam +set sv_pure 0 +map <map>` with 1920x1080 (`r_mode -1`), `cg_draw2D 0`, `cg_drawGun 0`, `team spectator`, `cl_avidemo 60` and `cam_play <path>`. `cl_avidemo` steps the game clock exactly 1/60 s per frame, so the TGAs are frame-exact. They're encoded with ffmpeg.
   - **One path file, both games:** paths are plain text, `tools/trailer/paths/<name>.cam` (one key per line: time, id-space position, pitch, yaw, roll, fov), which cgame reads with `trap_FS_FOpenFile` and the remaster reads directly. Both games evaluate the same spline from the same keys, so the frames line up by construction. `capture_q3.py --check` overlays both first frames at 50% to prove it.
   - **Retail demos too:** the same mod plays retail `.dm_68` demos with a camera path (demos played under `fs_game q3cam` use its cgame). A demo only holds what its player's PVS let through, so those paths stay near the action.
   - The mod is a capture tool kept in `tools/trailer/q3cam/` (id's files plus our patch and a build script, GPL like the rest). It never ships with the game.
2. **Matched flyovers:** cinematic flights through the most popular maps, flown on exactly the same path in Quake 3 (through `q3cam`) and in the remaster, so a comparison line can split one continuous view.
   - **Maps:** q3dm17 (The Longest Yard), q3dm6 (Campgrounds), q3tourney4 (Vertical Vengeance), q3dm13 (Lost World), q3dm7 (Temple of Retribution), q3tourney2 (The Proving Grounds), q3ctf4 (Space CTF) and q3dm12 (The Dredwerkz). Ozmium can change the list.
   - **Paths:** `.cam` files in id coordinates, 6 s of flight per map (the edit uses about 4 s). The first cut was laid out by hand: from floor plans rendered from the BSPs, and orbits around the intermission cameras' views. Every path is checked against the map's solid brushes (`cam_path.py check`). Later ones can come from the replay viewer's camera paths (T6). Items stay up and no players are in the map.
   - **Remaster:** `tests/trailer_shot.tscn` renders each path offscreen at 1920x1080 (`--fixed-fps 60`) into ffmpeg; `tools/trailer/compare.py` checks each pair.
3. **Matched stills (1999 vs remaster):** for Act 1's fast wipes, about 6 shots, each a `.cam` path with one key held for its duration, so stills can pitch too. Shots: the intermission cameras of q3dm0, q3dm6, q3dm7, q3tourney2, q3ctf1 and q3dm13, each held 2 s (two equal keys, so animated shaders and items keep moving). Still to do: a weapon close-up (retail viewmodel vs ours), which needs a player holding the gun rather than a spectator. A slow push-in in Remotion, applied identically to both, reads as a dolly.
4. **Choreographed fights (Ozmium, 2026-10-23):** the trailer's gameplay is staged, not found. Seven fight sequences (F1-F7 under "Script") are planned beat by beat and filmed with cinematic cameras.
   - **Fight scripts:** `tools/trailer/fights/<name>.json`. Each actor has a model, a team color, a spawn point and angles, a starting weapon and health, and is invulnerable until their death beat. Each has a timeline of actions at set times:
     - move to a point (run, walk, crouch or strafe-jump)
     - jump, or take a jump pad
     - aim at a point or at another actor (with lead for rockets)
     - start and stop firing, switch weapon
     - pick up an item, taunt
     - become killable
   - **Puppets:** q3core runs the actors as server-side clients whose usercmds come from the script instead of a player or bot AI. Everything else is the real game: id's movement and physics, real projectiles, splash damage and knockback, real deaths with ragdolls, every effect and announcer line. With `pmove_fixed` and fixed random seeds a take plays the same way every time, so timings are tuned until the rocket lands, then left alone.
   - **Filming:** the take is recorded with the replay recorder (REPLAY_PLAN.md R1) and filmed as often as needed with **keyframed camera paths** in the replay viewer's free cam (T6): smooth spline, ease, and a time scale for slow motion and speed ramps. Recorded offscreen at 1080p60 like the flyovers. Players get the camera paths too, for frag movies (with R8 clip export).
   - **First person:** some beats are the actor's own view with the HUD on (the replay viewer's first-person camera), so the trailer also shows what playing feels like.
   - **1999 vs remastered in motion:** the q3cam mod's game module is id's game code too, so the same puppet driver can run in retail Quake 3. F6 then plays identically in both games for a split-screen fight.
   - **Unscripted backup:** bot matches (six skill-5 bots on q3dm17, q3dm6 and q3tourney4) recorded with replays, for montage filler between the staged beats.
5. **Matched action (optional, best):** R7 plays `.dm_68` demos. A demo recorded in the retail game then plays in both games, so the same frag can play side by side, in motion, with pitch. Worth it for one or two shots if R7 is done in time.
6. **Characters:** a turntable of the 29 fighters (the showcase stage, `Q3ShowcaseStage`), the retail MD3 next to the rigged model, ragdoll and dissolve close-ups (`tests/ragdoll_test.tscn`).
7. **Features:**
   - The menu with the floating showcase.
   - The four HUD presets and the strafe-jump speedometer.
   - The crosshair editor.
   - The player card editor, level-ups and medals.
   - Friends and parties.
   - Settings search.
   - Controller play.
   - Recorded with the composite method, extended to video: Movie Maker for the 3D plus a CDP screencast of the page, joined in Remotion.
8. **Map editor:** a time-lapse of building a small arena, importing q3dm17, and one-click play-test (`tests/map_editor_shot.tscn` extended to Movie Maker).
9. **Transition elements:** a rocket explosion and a teleport effect filling the camera, a rail spiral alone on black, a jump pad ride to the apex, and a fly-through of a q3dm0 portal. These come from the effects scene (`capture.tscn view=fx`, `fire=`) and from replays, rendered on black or with a matte so Remotion can key them.
10. **Headline footage:** the player card editor, the menu's Ranked page, a crowded bot match, and the map editor's import and play-test. The COMING SOON cards sit over these (decision 6).

## Script (draft, about 2:50; Ozmium's lines and direction, 2026-10-23)

Ozmium's direction: cinematic, with cool gameplay clips and cool transitions between them, the cool features explained, and cinematic flyovers of the most popular maps in both games with a comparison line drawn between them.

Two acts. **Act 1 is the promise:** Ozmium's lines as full-screen title cards, each slammed onto a downbeat over a flash of footage that proves it. **Act 2 is the showcase:** the features, with comparisons between Quake 3 and the remaster all the way through. It ends on DOWNLOAD NOW.

The beats are fixed; the times move to the track's beat grid and its sections (build, drop, breakdown). **No narrator:** the announcer is the trailer's voice, and the text is short and loud.

**Act 1: the promise**

| Time | Picture | Sound | Text |
|---|---|---|---|
| 0:00-0:04 | Black. Then retail q3dm17 at 4:3, low-res, pillarboxed, slow push. | Wind, a distant jump pad. | |
| 0:04-0:09 | The picture widens to 16:9 and wipes into the remastered shot. | "Prepare to fight." Music starts. | **EXPERIENCE QUAKE III ARENA LIKE NEVER BEFORE\*** / small, bottom: *\*Requires Quake III Arena* |
| 0:09-0:13 | The logo forms out of the showcase smoke. | Music builds. | **QUAKE III ARENA REMASTERED** / *a fan project* |
| 0:13-0:17 | Three fast 1999/remaster wipes, one per beat. | Hit on each wipe. | **FULLY REBUILT ON A NEW ENGINE.** |
| 0:17-0:20 | The main menu, the showcase fighter turning. | Hit. | **FREE TO PLAY** / second line, as large: **REQUIRES QUAKE III ARENA** |
| 0:20-0:23 | The menu's Ranked page: the tiers from Bronze to Godlike, then a duel in q3tourney2. | Hit. | **RANKED MATCHMAKING** / stamped **COMING SOON** |
| 0:23-0:26 | A crowded eight-player match on q3dm6. | Hit. | **DEDICATED SERVERS** / stamped **COMING SOON** |
| 0:26-0:29 | The map editor importing q3dm17, then the play-test dropping into it. | Hit. | **FULL MOD SUPPORT** / stamped **COMING SOON** |
| 0:29-0:32 | A player card building itself part by part: banner, frame, emblem, title, the level badge ticking up. | Hit, menu sound per part. | **PLAYER PROFILE CUSTOMIZATION** |
| 0:32-0:34 | Hard stop. Black. | Silence, then "Fight!" | |

**Act 2: the showcase**

Act 2 is a run of feature segments. Each one has the same shape: **a gameplay clip, a transition, the feature with one line that says why it's cool, and a transition out into the next clip.** The cinematic action is spread between the features, so the trailer never stops moving. Comparisons with Quake 3 run through every segment where there's an original to compare against.

| Time | Picture | Transition in | Text (headline / the line that explains it) |
|---|---|---|---|
| 0:34-0:38 | **Flyover tour:** q3dm17. The camera sweeps off the edge of The Longest Yard and over the void, the comparison line already splitting 1999 from the remaster. | "Fight!" smashes into the first flight. | **THE LONGEST YARD** |
| 0:38-1:08 | **Flyover tour, continued:** one flight per map on the beat: Campgrounds, Vertical Vengeance, Lost World, Temple of Retribution, The Proving Grounds, Space CTF, The Dredwerkz. On each, the line sweeps across and the remaster takes the frame. | The line flips direction on the downbeat that cuts to the next map. | Each map's name, lower left. Over the tour: **EVERY SURFACE REMADE** / *All 1,919 textures rebuilt as modern materials.* **35 ARENAS, RELIT** / *Every original map, with modern global illumination.* |
| 1:08-1:14 | **F1, rail at the apex:** Sarge launched off The Longest Yard's jump pad, railed at the top. | **Jump pad launch:** the camera rides the pad up with Sarge, and the cut lands on the apex. | |
| 1:14-1:24 | **Fighters:** the retail MD3 beside the rigged model, then a turntable of all 29, cut on beats. | **Rocket smash:** an explosion fills the frame, and its smoke clears into the menu's smoky void. | **29 FIGHTERS REBUILT** / *Real skeletons, smooth animation, finer models.* |
| 1:24-1:36 | **Deaths:** F2, the rocket-jump ambush on Campgrounds: Klesk thrown into a pillar, sliding down and burning away. Then the same kind of hit in retail with its canned death animation, for comparison. | **Rail wipe:** a railgun spiral slices across the screen, and the next shot opens along its trail. | **PHYSICS RAGDOLLS** / *Every frag lands differently, then burns away.* |
| 1:36-1:44 | **Effects:** F3, the lightning duel over Temple of Retribution's lava, beams lighting the gargoyles; then F4's Quad rockets. Each with a split-screen flash of the same weapon in both games. | **Speed ramp:** slow motion into a frag, snapping back to real time on the cut. | **EFFECTS REMADE** / *New explosions, smoke and trails on the original timings.* |
| 1:44-1:50 | First person: a strafe jump with the speedometer climbing. The music drops back. | **Whip pan.** | **THE FEEL? UNTOUCHED.** / *id's original game code runs every frame, at 125 Hz.* |
| 1:50-1:56 | Split screen: F6, the airshot finale, played by the same puppets in both games (or a retail `.dm_68` played in each). | **Teleporter flash:** the teleport effect whites out the frame. | **1999** / **REMASTERED** |
| 1:56-2:04 | **Player profile:** the card editor, a level-up unlocking a part, medals, the achievement list. | **Card slide:** a player card sweeps across the frame as the wipe. | **MAKE IT YOURS** / *378 card parts and 89 achievements. Every unlock is cosmetic.* |
| 2:04-2:10 | **HUD and crosshairs:** the four HUD presets switching on one shot, the speedometer, the kill feed, the crosshair editor. | **Quad pulse:** a Quad Damage blue pulse on the beat. | **YOUR HUD, YOUR CROSSHAIR** / *Four presets, a strafe speedometer, and crosshairs you share as a code.* |
| 2:10-2:18 | **Replays:** a match playing back, the free camera flying through a frag, the death cam on the player's own ragdoll, then the killer's card. | **Portal:** the camera flies into one of q3dm0's portals and comes out in the replay. | **WATCH IT AGAIN** / *Replays with a free camera, and a death cam that shows who got you.* |
| 2:18-2:26 | **Map editor:** a time-lapse of building an arena, importing q3dm17, the one-click play-test dropping into it. | **Rocket smash.** | **BUILD YOUR OWN ARENA** / *A full map editor. Open any original map, play it in one click.* |
| 2:26-2:32 | **Online:** friends, a party invite, the leaderboards, a friend's card opening. | **Whip pan.** | **PLAY TOGETHER** / *Friends and parties, no account to sign up for.* |
| 2:32-2:40 | Gameplay montage, the fastest cuts of the trailer, each on a beat: F5's flag run, F7's gauntlet humiliation, then alternate angles of F1-F6. | **Speed ramp**, then cuts on every beat. | |
| 2:40-2:48 | Final flyover: a crane up out of q3dm17 into the sky, the comparison line sweeping off the frame for good, then the logo. | Last hit, "Fight!", cut to black. | **DOWNLOAD NOW** / *\*Requires Quake III Arena* / GitHub, Discord |
| End | Disclaimer and credit card. | | *Fan project, not affiliated with or endorsed by id Software, ZeniMax or Microsoft.* / *Music: "Data Destruction" by Skyth x Boom Kitty* |

**Transitions.** Each one is a Remotion component, and every one but the whip pan and the speed ramp uses the game's own footage or effects, so the transitions look like Quake 3, not like an editing app:
- **Jump pad launch:** a match cut on motion. The camera rides a jump pad up, and the next shot starts at the apex.
- **Rocket smash:** an explosion recorded in front of the camera fills the frame. Its smoke is keyed into a mask that opens onto the next shot (or onto the menu's smoke, a match cut).
- **Rail wipe:** a rail spiral rendered on its own, on black. Its trail is the edge of the wipe.
- **Teleporter flash:** the teleport effect's white peak covers the cut.
- **Portal:** recorded in the engine. The camera flies into a q3dm0 portal, and the cut hides in the frame where the portal fills the screen.
- **Card slide:** a player card at full screen crosses the frame as the wipe edge.
- **Quad pulse:** the Quad Damage blue shell color pulses over the cut, on a beat.
- **Whip pan:** directional motion blur between two shots.
- **Speed ramp:** slow motion into the frag, snapping to real time on the cut.
- **1999 glitch:** the retail footage gets a light CRT look (scanlines, a soft 4:3 frame), which tears open into the remaster. It's used once, at the start.

**Fight sequences (choreographed, Ozmium 2026-10-23).** Staged with puppet actors (Footage, item 4) and filmed with camera paths through the replay. Each sequence is longer than its slot in the edit, so it can be cut on the beat and reused in the montage from another angle. Times are seconds into the take. Every fight runs with no HUD, except the first-person shots.

**F1: Rail at the apex.** The Longest Yard, 6 s. Sarge (rocket launcher) against Xaero (railgun).
- 0.0: Sarge sprints across the center platform toward the big jump pad; Xaero waits on the rail platform out over the void.
- 1.2: Sarge hits the pad and flies toward the Quad tower.
- 2.4: at the top of the arc, Xaero rails him. Sarge's body takes the slug, spins, and tumbles off into the void.
- 3.0: Xaero's second slug finds Anarki on the far wing: "Impressive!"
- **Camera:** starts low behind Sarge as he runs (dolly, slight Dutch tilt) and rides the pad up with him (the jump pad transition lands here). At the apex it ramps to 10% speed and orbits 120° around Sarge as the rail spiral passes about a meter from the lens. Real time returns as the ragdoll falls away against the black sky.
- **Sound:** the pad's boing, the rail's hum and crack, then "Impressive".

**F2: Rocket jump ambush.** Campgrounds, 5 s. Doom against Klesk.
- 0.0: Klesk walks onto the ledge under the arch for the Mega Health.
- 0.8: below, Doom rocket-jumps straight up the drop and lands behind Klesk.
- 1.9: Klesk turns. Doom fires point blank.
- 2.2: the blast throws Klesk into the pillar, and the ragdoll slides down it. The burn, when the corpse goes 10 s later, is its own shot.
- **Camera:** a low dolly tracking Doom's run, tilting up with the jump. Doom lands sharp in the foreground with Klesk soft behind; focus pulls to Klesk as he turns. The explosion fills the frame (the rocket smash transition out).
- **Sound:** the jump's blast and grunt, the Mega Health pickup, the hit.

**F3: Lightning over lava.** Temple of Retribution, 5 s. Anarki against Visor, both on lightning guns.
- 0.0-3.0: they circle-strafe around a gargoyle pillar on the walkway above the lava, beams crossing and lighting the stone.
- 3.2: Visor's strafe misjudges the edge and drops off the walkway, beam still firing upward, into the lava.
- **Camera:** a wide orbit around the pillar against their strafe, both fighters in frame, the gargoyles lit by the beams. It ends looking down on the lava as Visor goes in.
- **Sound:** the lightning hum on both guns, then the lava.

**F4: Quad Damage.** Vertical Vengeance, 6 s. Ranger against Hunter and Major.
- 0.0: Ranger runs to the Quad under the rail platform, beside console03's red and green ring.
- 0.6: the pickup: the Quad sound and the blue shell.
- 1.5: Ranger drops down the shaft, turning in the air.
- 2.4 and 3.0: two Quad rockets, two frags, Hunter then Major.
- 3.4: "Excellent!"
- **Camera:** a hero low angle on the pickup with the console ring glowing behind (the quad pulse transition hits here), then a crane down the shaft with Ranger. The frags play in one take, with no cut.

**F5: Flag run.** Space CTF, 6 s. Bones carries the red flag; two blue defenders (Uriel, Orbb) fire plasma.
- 0.0: Bones grabs the flag (it waves: deformVertexes).
- 1.0: Bones takes the jump pad across the void, plasma bolts streaking past against the black.
- 3.5: Bones lands on the home platform and captures; the defenders' last bolts burst on the platform's edge.
- **Camera:** a wide crane in the void, keeping Bones and the flag silhouetted against the dark and the teleporter rings. It ends close as he lands.
- **Sound:** the flag pickup, plasma, the capture fanfare.

**F6: The airshot.** The Proving Grounds, 5 s. Xaero against Major, a duel.
- 0.0: Major jumps from the ledge beside the central tower toward the Mega Health.
- 0.9: Xaero leads him and fires one rocket.
- 1.6: the rocket meets Major in mid-air: "Excellent". The body flips through the air past the banners.
- **Camera:** side-on, freezing to 5% speed as the rocket closes, then real time for the hit.
- **1999 vs remastered:** F6 is the fight that runs in both games (the puppet driver in q3cam's game module), split by the comparison line. The script stays inside what both games share (no ragdoll timing beats), so the beats line up.

**F7: Humiliation.** Lost World, 2.5 s. Hunter against Orbb.
- 0.0: Hunter runs Orbb down across the bridge, gauntlet out.
- 1.1: the gauntlet connects: "Humiliation!" Hunter taunts over the body.
- **Camera:** handheld close behind Hunter (a little shake), then a cut to his own first-person view with the HUD for the hit.
- **Sound:** the gauntlet whir, the hit, the announcer.

Rules for the script:
- **Only claim what's built when the trailer comes out** (README's rule). Ranked matchmaking, dedicated servers and full mod support aren't built, so their cards are stamped **COMING SOON** (decision 6), and their footage is real footage of what exists, never mock-ups of unbuilt screens.
- **The ownership rule is said outright.** The free-to-play card reads FREE TO PLAY with REQUIRES QUAKE III ARENA right under it, at the same size, never as small print. The first card and DOWNLOAD NOW keep the asterisk footnote as well.
- **The game feel is a selling point:** the strafe-jump beat tells long-time players nothing they love has changed. It also answers "new engine": Godot draws the game, and id's code still plays it.

## Phases

| Phase | What | Hours |
|---|---|---|
| T1 | **Script and music:** `beats.py` on "Data Destruction", map the script's acts to its sections, lock the shot list | 0.25 |
| T2 | **Remotion project:** `trailer/`, components (the game-made transitions included), edit file, preview page with beat grid, render script | 1 |
| T3 | **Retail camera mod `q3cam`:** the cgame and game patch, the QVM build with id's `lcc` and `q3asm`, the pk3, `capture_q3.py`, `--check` alignment. **Done 2026-10-23**, except `--check`, which needs the remaster's frames | 0.75 |
| T4 | **Remaster capture:** `tests/trailer_shot.tscn` from the same shot list (offscreen, piped to ffmpeg; **done 2026-10-23**); check that the framing matches retail; record the comparison set | 0.5 |
| T5 | **Characters, features, editor and transition footage**, including the video version of the menu composite | 0.5 |
| T6 | **Camera paths in the replay viewer** (needs R1-R3), then record the action footage | 0.75 |
| T6c | **Choreographed fights:** the fight-script format and the puppet driver (q3core: scripted usercmds for server-side clients, deterministic), F1-F7 staged and tuned, recorded with replays and filmed with camera paths; optional: the same driver in q3cam's game module for F6 in retail (+0.5) | 2.5 |
| T6b | **Flyovers:** paths for the 8 maps, captured in both games; the `CompareLine` component. **The retail half is done 2026-10-23** | 0.5 |
| T7 | **Edit, mix, render, review** with Ozmium, about two rounds | 0.75 |
| | **Total** | **about 7.5** |

The hours are Claude's working time. Rendering runs on its own in the background (Movie Maker at max settings, retail frame dumps, the final render) and isn't counted, and neither is the time Ozmium spends reviewing.

**Order:** the whole trailer waits until replays (R1-R9) are built (Ozmium, 2026-10-23). Then T1-T7 run in order, and the matched action from `.dm_68` (R7) is available from the start.

## Decisions

1. **When (decided, Ozmium 2026-10-23):** the retail footage is recorded now (his go). The rest waits until Ozmium says go, and then all of it comes after replays.

Still open, for when it's made:

2. **Track (decided, Ozmium 2026-10-23):** "Data Destruction" by Skyth x Boom Kitty, used with the artists' permission. Act 1's cards ride the build, "Fight!" lands on the drop, and the strafe-jump beat sits in the breakdown.
3. **Length:** about 2:50, which the flyover tour and the feature segments need (recommended), plus a 45 s cut for social media from the same edit file: Act 1's cards, the drop and DOWNLOAD NOW. The track runs 3:21, so the 2:50 edit fits inside it: cut it at a section boundary rather than fading it mid-phrase.
4. **Voice:** announcer plus on-screen text, no narrator (recommended).
5. **Resolution (decided, Ozmium 2026-10-23):** 1080p at 60 fps.
6. **Headline claims that aren't built yet (decided, Ozmium 2026-10-23):** the trailer still comes out after replays. Ranked matchmaking and dedicated servers (MULTIPLAYER_PLAN.md M6 and M7) and full mod support (no phase yet) keep their Act 1 cards, stamped **COMING SOON**.
7. **"Free to play" (decided, Ozmium 2026-10-23):** the card reads **FREE TO PLAY / REQUIRES QUAKE III ARENA**. On-screen text uses American spelling, so the profile card reads CUSTOMIZATION.

## Risks

- **Music rights:** the track is used with Skyth x Boom Kitty's permission. Keep that permission in writing with the project's records, and credit both artists on the end card and in the video description. If the track is registered with Content ID, the upload may still be flagged automatically; the artists or their distributor can clear the channel, so ask them to before release.
- **Retail framing:** if fov or eye height differs between the two games, comparisons drift. T4 checks this before any of them are used.
- **Retail loading the mod:** QVMs built with id's own `lcc` and `q3asm` are what 1.32 expects, and `sv_pure 0` lets the local server load the mod's pk3. T3 checks it on q3dm17 before any paths are made.
- **Retail at 1080p:** 1.32's widescreen fov is calculated by `CG_CalcFov` as ours is, but its HUD and console assume 4:3. Both are off during capture, so this shouldn't show.
