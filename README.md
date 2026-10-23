![Quake 3 Arena Remastered: Fan Project](docs/promo/banner.jpg)

# Quake 3 Arena Remastered: Fan Project

**The 1999 arena shooter, rebuilt for today without touching what made it great.**

Rocket jumps, strafe jumping, rail duels, and Sarge, Xaero and the rest of the roster, now with modern characters, lighting, effects, menus and online features. It's an unofficial, non-commercial fan project. You need your own copy of Quake III Arena to play.

> **Open beta: single player only.** The [open beta for Windows](https://github.com/ozmiumofficial/quake-3-arena-remastered-fan-project/releases) is out. You can play the campaign and offline matches against bots. Friends, parties and leaderboards are online, but you can't play matches against other people yet. Online multiplayer is [planned](#planned-features).

### Menus

| | |
|---|---|
| ![Main menu](docs/screenshots/menu-main.jpg) | ![Single player campaign](docs/screenshots/menu-single-player.jpg) |
| **Main menu:** a fighter or item turning in a smoky void, with Quake 3's own menu sounds | **Single player:** id's campaign, tier by tier, with skill levels and medals |
| ![Create match](docs/screenshots/menu-create-match.jpg) | ![Profile](docs/screenshots/menu-profile.jpg) |
| **Create match:** every mode, map and rule, with modifiers on top | **Profile:** your player card, with banners, emblems, frames and titles you earn by playing |

![Settings](docs/screenshots/menu-settings.jpg)

### In game

| | |
|---|---|
| ![The Longest Yard, railgun](docs/screenshots/game-longest-yard-railgun.jpg) | ![The Longest Yard, lightning gun](docs/screenshots/game-longest-yard-lightning.jpg) |
| **The Longest Yard** (q3dm17), railgun | **The Longest Yard**, lightning gun |
| ![The Campgrounds](docs/screenshots/game-campgrounds.jpg) | ![Temple of Retribution](docs/screenshots/game-temple-of-retribution.jpg) |
| **The Campgrounds** (q3dm6) | **Temple of Retribution** (q3dm7) |
| ![Lost World](docs/screenshots/game-lost-world.jpg) | ![The Proving Grounds](docs/screenshots/game-proving-grounds.jpg) |
| **Lost World** (q3dm13) | **The Proving Grounds** (q3tourney2) |

All screenshots are real matches against id's bots, captured in the engine at 1920x1080 (`godot/tests/screenshots.tscn`). Key art and a social preview card are in [docs/promo](docs/promo).

## The same game, down to the frame

Most remakes copy the original from memory. This one runs id Software's own game code.

- **The original engine code runs every match.** Movement, weapons, items, damage and the rules come straight from Quake III Arena's released source, so strafe jumps, rocket jumps and circle jumps behave exactly as they did.
- **Checked line by line against the 1999 release.** The modern open-source port of Quake 3 changed a few things over the years. Every change that affected play was found and put back the way id shipped it.
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
| **Main menu** | Static menu screens | A cinematic menu: one fighter or weapon floating in a dark, smoky void while the camera drifts, with the original menu sounds |
| **HUD** | Fixed classic HUD | A Quake Live-style HUD with four presets, a speedometer that shows your strafe-jump gain, a kill feed, damage direction arcs and award medals |
| **Chat** | A line of text at the top of the screen | A chat box with history, team chat, name completion and a live preview of colored text; colored names show everywhere, from the kill feed to the scoreboard |
| **Crosshair** | Pick one of ten | A full crosshair editor with per-weapon crosshairs and shareable codes, previewed live in the match behind it |
| **Settings** | Basic options, console variables | A full settings menu with search, live preview and shareable config codes |
| **Online** | Server browser, IP addresses | Friends, parties and presence, with no accounts to sign up for |

## In the game now

- **Single player:** the original campaign, from Training through six tiers to the final duel with Xaero, with medals, skill levels and the tier movies.
- **Offline vs bots and training:** any original map, any mode, any bot lineup.
- **All 35 original maps,** relit, from q3dm0 to the pro duel maps.
- **Game modes:** Free For All, Tournament (Duel), Team Deathmatch and Capture the Flag.
- **Pause menu:** change any setting or edit your crosshair without leaving the match.
- **Friends and parties:**
  - Add friends with a short friend code, and see who's online and what they're playing.
  - Form a party of up to 8 and chat. No email, password or account needed.
- **Leaderboards:** see where your level ranks against everyone or just your friends, and open any player's card. A ranked board fills in once ranked play starts.
- **Player cards and levels:**
  - Build your own card from about 380 Quake 3-themed banners, emblems, frames, titles, accent colors, rail colors and taunts.
  - Every match played to the end earns XP, bots included. Levels and 89 achievements unlock new parts, and the Profile shows what each locked part needs.
  - Your card appears on the scoreboard and the results screen. Progression is cosmetic only and never changes the game.
- **Controller support** throughout the menus.

## Planned features

None of these are in the open beta yet. They're listed roughly in the order they're coming.

- **A built-in map editor** (being built now): build arenas inside the game, import the original maps, play-test instantly, and edit together with friends.
- **Replays and spectating:**
  - Every match recorded automatically, with a replay viewer: pause, slow motion, seeking, and free or follow cameras.
  - Spectator mode for live matches, and an optional killcam that shows how you died.
- **Online matches without paying for servers:**
  - Anyone can host from their own PC, and one match browser shows every match. Optional dedicated servers.
  - Every connection is relayed, so no one sees another player's IP address. You're matched in the closest region first, or you can pick one.
  - Lobbies vote-kick troublemakers instead of relying on anti-cheat software.
- **Ranked play:** a duel queue with an Elo ladder, seasons and tiers from Bronze to Godlike.
- **Player cards online:** your card in friends' lists and lobbies, both cards sliding in on the duel versus screen, and a card image to share.
- **More modes:** Clan Arena, Freeze Tag and Instagib as optional rule sets.
- **A Workshop** for sharing maps, skins and mods.
- **Accessibility:** colorblind modes and subtitles for the announcer.
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

1. From the [Releases](https://github.com/ozmiumofficial/quake-3-arena-remastered-fan-project/releases) page, download the game's zip and both texture packs (`textures-1.pck` and `textures-2.pck`).
2. Unzip the game anywhere, and put both texture packs in the `Q3Remaster` folder, next to `Q3Remaster.exe`.
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
