![Ozmium Arena](docs/screenshots/banner.jpg)

# Oz Arena

**A free, open-source mod for Quake III Arena.** Oz Arena is a heavily modified Quake III Arena client: it takes the game you already own and remakes how it looks, sounds and plays on a PC today: remastered textures and relit arenas, rigged fighters with ragdolls, remade effects, new menus and HUD, a map editor, and a browser that joins any community server: vanilla, CPMA or Defrag.

![q3dm7 in Oz Arena](docs/screenshots/hero.jpg)

**[Download the installer](https://github.com/ozmiumofficial/oz-arena/releases/latest/download/OzArenaSetup.exe)** · Windows 10 or 11, 64-bit · You need your own copy of Quake III Arena.

---

## Contents

- [Install](#install)
- [The look](#the-look)
- [Fighters, effects and sound](#fighters-effects-and-sound)
- [Playing online](#playing-online)
- [Playing alone](#playing-alone)
- [The HUD](#the-hud)
- [Menus and settings](#menus-and-settings)
- [The map editor](#the-map-editor)
- [How it works](#how-it-works)
- [Building from source](#building-from-source)
- [Credits and legal](#credits-and-legal)

---

## Install

1. **Download [`OzArenaSetup.exe`](https://github.com/ozmiumofficial/oz-arena/releases/latest/download/OzArenaSetup.exe)** (about 1 MB) and run it.
2. **Check what it found.** Setup finds your Quake III Arena on its own: in any of your Steam libraries, among your GOG games, or in the usual folders. If it doesn't, press **Locate** and pick the folder that holds `baseq3`.
3. **Press Install.** Setup downloads the latest release (about 4 GB) and puts Oz Arena in a folder of its own inside your Quake III Arena folder, so the two never mix. If the download stops, run Setup again and it carries on where it left off.
4. **Press Play.** Setup also adds Oz Arena to the Start Menu (and your desktop, if you leave that ticked).

Oz Arena reads the game's own files from your copy and never ships or changes them. To update, run Setup again: it skips any world pack you already have. To uninstall, use Windows' **Apps & features**; your settings, career and maps stay.

<details>
<summary>Installing by hand instead</summary>

1. Download `OzArena-<version>-windows.zip`, `pak_oz_world.pk3` and `pak_oz_world1.pk3` from the [latest release](https://github.com/ozmiumofficial/oz-arena/releases/latest).
2. Unzip the zip anywhere. It makes an `OzArena` folder.
3. Put the two world packs in that folder's `oz` folder, beside the other `pak_oz_*.pk3` files. Don't unzip or rename them, and don't put them in `baseq3`.
4. On Steam, Oz finds the game by itself. For a GOG, CD or other copy, copy your `baseq3` folder (the one with `pak0.pk3` to `pak8.pk3`) next to `OzArena.exe`.
5. Run `OzArena.exe`.

Or put all three release files next to `OzArenaSetup.exe` and run it: it installs from them without downloading.
</details>

Settings, saved matches and your career live in `%APPDATA%\Oz Arena`. To play CPMA or Defrag, install them into your Quake III Arena folder as usual (or beside `OzArena.exe` if you copied `baseq3` there); Oz never downloads them for you.

---

## The look

![The arenas](docs/screenshots/world_q3dm1.jpg)

- **Every texture remade.** The world, the fighters, the weapons and items, and the effects: each of id's images remade at high resolution, with id's own colors kept (a new, sharper set since v0.7.7). Each comes with normal, roughness and specular maps, and every surface stays matte.
- **35 arenas relit.** All of id's maps are lit again with q3map2 as id's own compiler lit them, now with directional (deluxe) light maps, so the new normal maps catch the light. Shadows are softer and light bounces, matched to each map's original brightness.
- **A modern renderer.** ioquake3's physically based rend2 renderer, with tone mapping, bloom and AMD FSR 1 upscaling.
- **New level shots and cinematics.** The arenas' pictures are shot in the new look, and the original movies play from a higher-quality pack.

| | |
|---|---|
| ![q3ctf3](docs/screenshots/world_q3ctf3.jpg) | ![q3tourney2](docs/screenshots/world_q3tourney2.jpg) |
| ![q3dm4](docs/screenshots/world_q3dm4.jpg) | ![q3dm0](docs/screenshots/world_q3dm0.jpg) |

From across a room Oz keeps id's look on purpose: the same colors, the same brightness, the same arenas. The difference shows up close, where id's textures turn to blurred blocks and Oz's keep their detail (both zoomed in with the zoom key, the same spot and frame):

![Original and Oz Arena: a wall on q3dm6](docs/screenshots/closeup_q3dm6_wall.jpg)

![Original and Oz Arena: stone blocks on q3dm6](docs/screenshots/closeup_q3dm6_stone.jpg)

And in the light: on q3dm1 the relit lava lights the floor and rock around it.

![Original and Oz Arena: the lava on q3dm1](docs/screenshots/relight_q3dm1.jpg)

Everything Oz adds to the look loads only in its own renderer. Pick the OpenGL or Vulkan renderer and you get id's look, untouched.

---

## Fighters, effects and sound

- **29 rigged fighters.** Every character is a skinned, skeletal model with smooth animation in place of the original's vertex-animated ones. They show on every server and mod, CPMA and Defrag included.
- **Ragdolls.** Bodies fall with Jolt Physics, with muscle tone in the joints. A rail shot throws them with the hit, and moving platforms carry them.
- **Remade effects.** Glowing rail trails, layered lightning, sparks, smoke and scorch marks. Pick your own lightning and plasma colors, or switch back to id's effects.
- **3D sound.** OpenAL Soft with HRTF, and a choice of hit sounds pitched by damage.
- **The same game at every frame rate.** Oz's own game moves in fixed 8 ms steps, so jumps, footsteps and fire rates are the same at 60 fps as at 1000.

---

## Playing online

![The Server Browser](docs/screenshots/server_browser.jpg)

- **Every community server.** The Server Browser asks every master server and lists the vanilla, CPMA and Defrag servers that answer, with search, filters, favorites, history and LAN. **Join** switches to the server's mod by itself and back again when you leave.
- **Pure servers stay playable.** A server that forces its own files is always joinable. Oz leaves its packs out there and keeps the engine's own upgrades: the lighting, the menus and the HUD.
- **The server's rules.** Your game predicts movement with the server's own settings, so a server that runs id's original physics feels right.
- **Lag compensation.** In matches running Oz's own game, an instant-hit shot (machine gun, shotgun, lightning gun, railgun, gauntlet) lands where you saw your target, whatever your ping, and your own rail, shotgun and machine gun shots show the moment you fire. Rockets, plasma, grenades and the BFG are lag-compensated too: they fly from the moment you fired and hit where your screen said they would, your own show and explode the instant you fire, and teleporters move you the moment you step in. A host can even out everyone's ping with the ping equalizer (g_pingEqualizer 1). It's Neil Toronto's Unlagged with Ratmod's lag compensation on top, fitted to Oz.
- **With friends.** Friends, parties, Quick Play and peer-to-peer matches hosted from your own PC, through Oz's online service. Hide my IP sends a match through a relay.

---

## Playing alone

| | |
|---|---|
| ![Skirmish](docs/screenshots/menu_skirmish.jpg) | ![Campaign](docs/screenshots/menu_campaign.jpg) |

- **Skirmish and Custom Match:** bots on any arena, in any mode, with the rules and modifiers you set. Skirmish plays under Oz's own game, CPMA or Defrag (the mods you have installed); under Defrag it races any map you own, with search and an Oz estimate of each course's difficulty to sort by.
- **Campaign:** the original's tiers of arenas, up to the final duel.
- **Training:** movement, aim and duels.
- **Your career:** XP and levels, achievements, records and a player card you build from what you've earned.
- **Replays:** watch your matches from any player's view, seeking both ways.

![Profile](docs/screenshots/menu_profile.jpg)

---

## The HUD

![The HUD in a match](docs/screenshots/hud.jpg)

One HUD on every server and every mod, pure servers included, drawn by the engine itself.

- **Presets:** Modern, Classic, Competitive and Minimal, or set each part yourself.
- **A scoreboard and results** with each player's weapons, accuracy and damage, up to 64 players. Right-click under Tab to open a player's card and add them as a friend.
- **The crosshair editor:** shapes, size, color and outline, previewed exactly as the match draws them.
- **A speedometer, chat with emoji, and item timers for spectators.** Players never see item timers: knowing when items come back would be cheating.

| | |
|---|---|
| ![Scoreboard](docs/screenshots/scoreboard.jpg) | ![Results](docs/screenshots/results.jpg) |

---

## Menus and settings

![Main menu](docs/screenshots/menu_main.jpg)

Every screen is Oz's own: the menus, the pause menu, the connection screen and error dialogs replace id's and the mods' screens. Mouse, keyboard and controller all work.

| | |
|---|---|
| ![Play](docs/screenshots/menu_play.jpg) | ![Settings](docs/screenshots/menu_settings.jpg) |

- **Settings** for video, graphics, audio, controls, mouse, crosshair, HUD, gameplay, network and accessibility, applied as you change them.
- **Accessibility:** menu and text size, high contrast and reduced flashing.

![Crosshair editor](docs/screenshots/menu_crosshair.jpg)

---

## The map editor

![The map editor with q3dm17 open](docs/screenshots/editor.jpg)

A map editor built into the game, from the main menu:

- **Brushes, curves and entities** with select, brush, clip and faces tools, hollow, subtract and merge, and texture lock.
- **One view or four:** 3D, top, front and side.
- **Materials and entities** from your own install and its map packs, with search and favorites.
- **Open any map you own.** Stock maps open through q3map2, ready to change.
- **Compile and play** without leaving: Play drops you in, and Esc brings you back.
- **Maps for Defrag and CPMA** too, with Defrag's timers and entities. Maps are saved as standard `.map` files.
- **Doors, lifts and movers** as id builds them: a door or a double door from a brush, opening when a player comes near, when shot or from a button; lifts, trains, bobbing, rotating and swinging brushes, with where each moves shown and played in the view.
- **Jump pads in two clicks:** the pad, its trigger and its target as one object, re-aimed by clicking where players should land.
- **Collision is yours:** every block collides, whatever its material, until you untick it.
- **Color any face,** and prototype with Oz's grid textures in a dozen colors before the real materials go on.
- **Check the map** before you compile: jump pads that do nothing, doors nothing opens, targets that name nothing.

| | |
|---|---|
| ![The editor's 3D view](docs/screenshots/editor_3d.jpg) | ![A Defrag course](docs/screenshots/editor_defrag.jpg) |

---

## How it works

Oz Arena runs on a modified build of Quake III Arena's own engine (id Tech 3, which id released under the GPL): **Quake3e**'s core, with **ioquake3**'s rend2 renderer and OpenAL sound, **oDFe**'s Defrag support and **CNQ3**'s CPMA extensions. On top of it, Oz adds:

- **Three renderers.** `oz` (rend2, Oz's look), `opengl` and `vulkan` (Quake3e's, id's look). The menus and HUD draw through all three.
- **Native menus and HUD** (`engine/src/oz/ui`, pages in `engine/ui`): RmlUi pages and an engine-drawn HUD that reads the match from the network snapshot, so they work the same under any mod's game code.
- **Oz's packs** (`oz/pak_oz_*.pk3`): remastered images sit beside id's file paths as DDS, and material scripts as `.ozmtr`. Only the `oz` renderer reads them, and they're never on a pure server's list, so nothing Oz adds can get you kicked as unpure.
- **Mods as they are.** Joining a CPMA or Defrag server switches to that mod; its own game code decides movement and rules. Oz never copies or decompiles closed mods.
- **Oz's own game code** (`game/patches`): ioquake3's game code with id's 1.32 gameplay and Oz's changes as a patch series, built to QVMs.
- **Ragdolls** (`engine/src/oz/physics`): Jolt Physics in the renderer, with the map's brushes and moving platforms as colliders.
- **Online** (`online/`): a Cloudflare Worker hub for friends, parties, matchmaking and match records; peer-to-peer matches over ICE (libjuice) with TURN relays.
- **Map editor** (`engine/src/oz/editor`): a C++ kernel for brushes and curves, reading and writing `.map` files in all three of their forms, drawn by the `oz` renderer.

---

## Building from source

You need Windows, **Visual Studio 2022** with the C++ workload (its own CMake and Ninja), **Python 3** and **git**.

```
python engine/build_engine.py
```

The build fetches its libraries (listed in `engine/thirdparty.json`) and ioquake3's game code, applies the patches, and stages the game in `build/engine/`: `OzArena.exe`, the dedicated server `OzArena.ded.exe`, the renderers, and `oz/pak_oz_code.pk3` with the QVMs. Run it with your copy of the game found as above:

```
build/engine/OzArena.exe
```

For the map editor's import and compile, unpack a [NetRadiant-custom](https://github.com/Garux/netradiant-custom/releases) Windows release into `build/tools/` (so its programs are in `build/tools/netradiant-custom-<date>/app`); the build stages its q3map2 and mbspc beside the game.

The asset packs come from `tools/`, which rebuild them from your own copy's files: `make_textures.py` (the texture library, on the GPU with PyTorch and CUDA), `make_pk3s.py` and `q3relight.py` (the world and the relit maps, with q3map2), `make_fighters.py`, `make_props.py`, `make_fx_pk3.py` and `make_levelshots.py`; the modern fighters (Settings > Experimental) come from `fighter_swap/build_fighter.py`, with Blender. The release has them all prebuilt.

The installer is `engine/installer/setup`: `python engine/tools/make_setup.py` builds `build/installer/OzArenaSetup.exe` (it needs the .NET SDK).

| Folder | What's there |
|---|---|
| `engine/quake3e` | The engine: Quake3e with ioquake3's rend2 and OpenAL, oDFe and CNQ3 parts. Oz's edits are marked `Oz:` |
| `engine/src/oz` | Oz's code: menus and HUD, online, the map editor, physics |
| `engine/ui` | The menus' pages, styles and data |
| `engine/patches` | Patches to the libraries (RmlUi, libjuice) |
| `game` | Oz's game code, as patches on ioquake3's, and its shaders |
| `online` | The online hub (Cloudflare Worker) |
| `tools` | The asset pipeline |

---

## Credits and legal

Oz Arena is built on the work of: id Software's GPL source release; [Quake3e](https://github.com/ec-/Quake3e); [ioquake3](https://github.com/ioquake/ioq3) (rend2 and OpenAL); [oDFe](https://github.com/Defrag-racing/oDFe); [CNQ3](https://bitbucket.org/CPMADevs/cnq3); Neil Toronto's [Unlagged](https://www.ra.is/unlagged/) and [Ratmod](https://ratmod.github.io/) (lag compensation); [RmlUi](https://github.com/mikke89/RmlUi); [Jolt Physics](https://github.com/jrouwe/JoltPhysics); [SDL2](https://www.libsdl.org/); [OpenAL Soft](https://github.com/kcat/openal-soft); [libjuice](https://github.com/paullouisageneau/libjuice); [libcurl](https://curl.se/); [Mbed TLS](https://github.com/Mbed-TLS/mbedtls); libtheora and libvorbis; AMD [FidelityFX FSR 1](https://github.com/GPUOpen-Effects/FidelityFX-FSR); [Real-ESRGAN](https://github.com/xinntao/Real-ESRGAN); and [NetRadiant-custom](https://github.com/Garux/netradiant-custom)'s q3map2, which the map editor runs.

The code is **GPL-2.0** (see `LICENSE`).

Oz Arena is an unofficial, non-commercial fan project. It is not affiliated with, endorsed by or sponsored by id Software, Bethesda or ZeniMax. Quake and Quake III Arena are trademarks of ZeniMax Media, named here only to identify the game Oz Arena modifies and requires. You need your own copy of Quake III Arena to play; the remastered assets remain id Software's property.
