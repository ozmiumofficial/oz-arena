![Quake 3 Arena Remastered: Fan Project](docs/promo/banner.jpg)

# Quake 3 Arena Remastered: Fan Project

**The 1999 arena shooter, rebuilt for today without touching what made it great.**

Rocket jumps, strafe jumping, rail duels, and Sarge, Xaero and the rest of the roster, now with modern characters, lighting, effects, menus and online features. It's an unofficial, non-commercial fan project. You need your own copy of Quake III Arena to play.

> **Open beta, now online.** The [open beta for Windows](https://github.com/ozmiumofficial/quake-3-arena-remastered-fan-project/releases) adds online multiplayer: host a match from your own PC or run a dedicated server, find matches in the server browser, join friends, queue for matchmaking and ranked duels. Every connection is relayed, so no one sees anyone's IP address. The campaign, offline matches against bots and the map editor are all still there.

### Menus

| | |
|---|---|
| ![Main menu](docs/screenshots/menu-main.jpg) | ![Campaign](docs/screenshots/menu-single-player.jpg) |
| **Main menu:** a fighter or item turning in a smoky void, with Quake 3's own menu sounds and its soundtrack on shuffle | **Campaign:** id's single player campaign, tier by tier, with skill levels and medals |
| ![Create match](docs/screenshots/menu-create-match.jpg) | ![Profile](docs/screenshots/menu-profile.jpg) |
| **Create match:** every mode, map and rule, with modifiers on top | **Profile:** your player card, with banners, emblems, frames and titles you earn by playing |

![Settings](docs/screenshots/menu-settings.jpg)

**Settings:** search every option, with a description of each. The weapon effects can be Quake 3's classic look or glowing rail and lightning beams, missile trails with a burning core, and explosions with flying embers and smoke.

### In game

| | |
|---|---|
| ![The Longest Yard, railgun](docs/screenshots/game-longest-yard-railgun.jpg) | ![The Longest Yard, lightning gun](docs/screenshots/game-longest-yard-lightning.jpg) |
| **The Longest Yard** (q3dm17), a glowing rail | **The Longest Yard**, the glowing lightning gun |
| ![The Campgrounds](docs/screenshots/game-campgrounds.jpg) | ![Temple of Retribution](docs/screenshots/game-temple-of-retribution.jpg) |
| **The Campgrounds** (q3dm6) | **Temple of Retribution** (q3dm7) |
| ![Lost World](docs/screenshots/game-lost-world.jpg) | ![The Proving Grounds](docs/screenshots/game-proving-grounds.jpg) |
| **Lost World** (q3dm13) | **The Proving Grounds** (q3tourney2) |

All screenshots are real matches against id's bots, captured in the engine at 1920x1080 (`godot/tests/screenshots.tscn`). Key art and a social preview card are in [docs/promo](docs/promo).

## id's own game, Quake Live's rules

Most remakes copy the original from memory. This one runs id Software's own game code.

- **The original engine code runs every match.** Movement, weapons, items and damage come straight from Quake III Arena's released source, so strafe jumps, rocket jumps and circle jumps behave exactly as they did.
- **Quake Live's rules are the standard.** Matches use Quake Live's weapon balance: damage, splash and knockback, projectile speeds, refire times, its tight shotgun pattern, starting weapons and item amounts. Respawns work as in Quake Live too, with fire bringing you back after 2.1 seconds. Its movement is [coming next](#planned-features).
- **The 1999 rules are kept, checked line by line.** The modern open-source port of Quake 3 changed a few things over the years. Every change that affected play was found and put back the way id shipped it, and id's original rules will be a Classic choice in match setup.
- **The original bots, personalities and all.** Sarge, Xaero, Klesk, Doom and the rest find their way around the original maps the same way, pick fights the same way, and still talk trash in chat.
- **The classic 125 Hz physics.** Movement runs at the fixed rate competitive players have always used, so jump heights and speeds match.

## What's new

| | Quake III Arena (1999) | Remastered |
|---|---|---|
| **Characters** | Low-poly models with stiff, frame-by-frame animation | All 29 fighters rebuilt with real skeletons, smooth 60 fps animation and much finer meshes |
| **Deaths** | Canned death animations | Physics ragdolls that react to the hit that killed them, then burn away |
| **Textures** | Hand-painted 1999 textures | Every surface in the game remade as a modern matte material that reacts to light, with less of the repeating-tile look |
| **Lighting** | 1999 lightmaps | All 35 original maps relit with modern global illumination |
| **Effects** | Sprite explosions and beams | Remade explosions, smoke, rail trails, lightning, blood and impacts, using the original effect timings |
| **Movies** | Low-resolution intro and tier movies | All 10 cinematics upscaled |
| **Main menu** | Static menu screens | A cinematic menu: one fighter or weapon floating in a dark, smoky void while the camera drifts, with the original menu sounds and Quake 3's soundtrack on shuffle |
| **HUD** | Fixed classic HUD | A Quake Live-style HUD with four presets, a speedometer that shows your strafe-jump gain, a kill feed, damage direction arcs and award medals |
| **Chat** | A line of text at the top of the screen | A chat box with history, team chat, name completion and a live preview of colored text; colored names show everywhere, from the kill feed to the scoreboard |
| **Crosshair** | Pick one of ten | A full crosshair editor with per-weapon crosshairs and shareable codes, previewed live in the match behind it |
| **Settings** | Basic options, console variables | A full settings menu with search, live preview and shareable config codes |
| **Replays** | Demos you had to remember to record, seen only through one player's eyes | Every match recorded automatically: watch from anyone's eyes, behind them or with a free camera, and save clips as video |
| **Map editor** | A separate editor and command-line compile tools | A map editor built into the game, with one-key play-tests against bots |
| **Online** | Server browser, IP addresses | Host from your own PC or a dedicated server, join friends, matchmaking and ranked; every connection relayed so no one sees your IP, and no accounts to sign up for |

## In the game now

- **Online multiplayer:**
  - Host a match from Create Match and it's listed for everyone, with an optional password. Or run a dedicated server from the menu that keeps going after you close the game.
  - A server browser with search, region and mode filters. Join friends and party members from the Friends panel.
  - Quick Play matchmaking for Duel, Free For All, Team Deathmatch and Capture the Flag, and ranked queues for Duel, Team Deathmatch and Capture the Flag with an Elo ladder, seasons and tiers from Bronze to Godlike (from level 30).
  - Every connection goes through a relay, so no one sees another player's IP address, the host included. No ports to open.
  - Vote-kick troublemakers in a match, Quake 3 style.
- **Single player:** the original campaign, from Training through six tiers to the final duel with Xaero, with medals, skill levels and the tier movies.
- **Offline vs bots and training:** any original map, any mode, any bot lineup.
- **All 35 original maps,** relit, from q3dm0 to the pro duel maps.
- **Game modes:** Free For All, Tournament (Duel), Team Deathmatch and Capture the Flag.
- **Pause menu:** change any setting or edit your crosshair without leaving the match.
- **Map editor:**
  - Build arenas inside the game with brushes, curves, every remastered texture, pickups, jump pads, teleporters and lights.
  - Shape brushes right where they are: grab a face's handle to push or pull it, or a corner to move it.
  - Work in the 3D view or in top, front and side views drawn as lines, Radiant style; resize or fold the side panels to give the view more room. Grid snapping can be turned off.
  - Bring in any Quake 3 map from a .bsp or .pk3 file, and export yours as a .pk3 that plays in the original Quake 3.
  - Open any original map from your own copy of Quake 3 and change it, or bring maps in and out of GtkRadiant, NetRadiant and TrenchBroom.
  - Play-test instantly against bots, starting right where the editor's camera is, then publish a finished map to play it from Create Match.

  | | |
  |---|---|
  | ![Map editor: The Campgrounds](docs/screenshots/editor-campgrounds.jpg) | ![Map editor: building a room](docs/screenshots/editor-objects.jpg) |
  | **The Campgrounds** (q3dm6) opened from the original game, ready to edit | **Building a room:** every brush, group and spawn listed; drag the selected block's corners and face handles to reshape it |
- **Replays:**
  - Every match is recorded automatically, all of it: every player, so you can watch from anyone's eyes, from behind them, circling them, with a free camera that flies anywhere, or through the cinematic camera.
  - A replay viewer: pause, slow motion and fast forward, frame by frame, jump to the next or last frag, a timeline marked with your frags, deaths and medals, name tags and outlines.
  - A Replays page: rename, keep favorites forever, watch again from the results screen.
  - Save a clip of any moment as a video.
  - Plays Quake 3's own demo files too.
- **Race:** Defrag-style timed courses, with Quake 3's own movement.
  - Run from the start line to the finish through the checkpoints; the timer shows your split against your best at each one.
  - Restart with R, or save a spot and come back to it to practice a jump. No health to lose, and rocket jumps cost nothing.
  - Your best run is kept for every course and races beside you as a ghost.
  - Plays Defrag courses you drop into the Maps folder.
  - Build your own in the map editor: start and finish lines and checkpoints in a click each, a check that the course works, and a play-test with the timer.

  | | |
  |---|---|
  | ![Race: a run against your ghost](docs/screenshots/race-run.jpg) | ![Map editor: race tools](docs/screenshots/editor-race.jpg) |
  | **A run:** the timer, your best time, and the ghost of your best run ahead of you | **Race tools in the map editor:** start line, checkpoint and finish line placed, and the course checked |
- **Spectating:** follow anyone in a live match, switch to a chase camera, or let an auto-director follow the action. The cinematic camera films the match like a broadcast by itself: slow orbits, tracking shots and wide shots of the fight, cutting to the frags.
- **Death cam:** when you're fragged, the camera follows your body as it falls, then shows who got you with their player card.
- **Match stats:** the Tab scoreboard shows your match: damage and accuracy with every weapon, distance traveled and your medals.
- **Friends and parties:**
  - Add friends with a short friend code, and see who's online and what they're playing.
  - Form a party of up to 8 and chat. No email, password or account needed.
- **Leaderboards:** see where your level and your ranked rating stand against everyone or just your friends, and open any player's card.
- **Player cards and levels:**
  - Build your own card from about 380 Quake 3-themed banners, emblems, frames, titles, accent colors, rail colors and taunts.
  - Every match played to the end earns XP, bots included. Levels and 89 achievements unlock new parts, and the Profile shows what each locked part needs.
  - Your card appears on the scoreboard and the results screen. Progression is cosmetic only and never changes the game.
- **Developer console:** Quake 3's drop-down console on the key under Esc, with the original commands, binds and config files.

  ![Developer console](docs/screenshots/console.jpg)
- **Controller support** in the menus and in matches, with a southpaw stick layout.
- **Competitive options:** item respawn timers, bright enemy skins, one forced enemy model, gun position and field of view, hit sounds pitched by damage, mouse acceleration and a lagometer.
- **Accessibility:** colorblind filters, subtitles for the announcer, and a reduced-flashing mode.

## Planned features

None of these are in the open beta yet. They're listed roughly in the order they're coming.

- **The rest of Quake Live's rules:** its movement (jumps, step jumps and chain jumps), bots tuned for the new weapons, and a Classic choice in match setup for id's original rules.
- **Dedicated servers on Linux,** for running a server on a rented machine.
- **Player cards online:** your card in friends' lists and lobbies, both cards sliding in on the duel versus screen, and a card image to share.
- **A Workshop:** share the maps you build and play other people's, straight from the menu.
- **Editing maps together** with friends in the map editor.
- **More modes:** Clan Arena, Freeze Tag and Instagib as optional rule sets.
- **Linux and macOS** versions.

## What stays the same

Some things shouldn't change, so they don't:

- Movement, weapon damage, fire rates, item timers and spawn rules.
- The original announcer and soundtrack.
- Everything the original game gave everyone for free, like every character and skin, colored names and rail colors. Unlocks are cosmetic extras on top.

## Playing it

**You'll need:**

- Windows, 64-bit.
- Quake III Arena, the full game (Steam, GOG or the CD release), not the demo.

**Installing the open beta:**

1. From the [Releases](https://github.com/ozmiumofficial/quake-3-arena-remastered-fan-project/releases) page, first download the three texture packs (`textures-1.pck`, `textures-2.pck` and `textures-3.pck`, about 4 GB) from the [Textures release](https://github.com/ozmiumofficial/quake-3-arena-remastered-fan-project/releases/tag/textures), then the game's zip from the newest release.
2. Unzip the game anywhere, and put all three texture packs in the `Q3Remaster` folder, next to `Q3Remaster.exe`.
3. Run `Q3Remaster.exe`.

You can also build the game yourself from this source code and the Quake III Arena files on your PC; [DEVELOPMENT.md](DEVELOPMENT.md) walks through it.

**First launch:** the game asks for your copy before it goes any further.

1. Start the remaster. After the disclaimer you'll see **Quake III Arena required**.
2. Open your Quake III Arena folder (on Steam: `Steam\steamapps\common\Quake 3 Arena\baseq3`). If the game finds an install, **Open that folder** takes you there.
3. Drag `pak0.pk3` onto the game window. You can also drag the `baseq3` folder or the whole game folder, or use **Browse**.
4. The game checks the file against id's retail release, the same check Quake 3 itself runs. If it matches, the game starts. If it doesn't, the game won't launch.

Nothing is copied or uploaded. The game remembers where the file is and checks it again every time it starts, so if you move or remove your Quake III Arena, it asks again.

## For developers

[DEVELOPMENT.md](DEVELOPMENT.md) covers how everything is built: the asset tools, the Godot project, the game code, tests and folder layout. [PLAN.md](PLAN.md) is the roadmap and design.

## License

The code in this repository is licensed under the GNU General Public License v2.0 (`LICENSE`), the license of id Software's Quake III Arena source release and ioquake3, whose game code this project runs.

The license covers code only. The repository also carries the remastered asset library (`godot/library` and `godot/webui`, stored with Git LFS): models, textures, lightmaps, sounds, music, cinematics and menu art remastered from id Software's originals. Those remain id Software's property and aren't covered by the GPL. id's original game files (the `pak` files) aren't included, and the game won't run without your own copy of Quake III Arena.

This is an unofficial fan project, not affiliated with or endorsed by id Software, Bethesda or ZeniMax. Quake is a trademark of ZeniMax Media.
