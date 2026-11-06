# texswap: drop-in texture replacement for PK3s

> **Playing Oz Arena?** You don't need this tool. The game reads a `custom_textures` folder in your
> user folder itself, with the same naming rules, and changes no pak. Settings > Graphics > Texture
> folder opens it. texswap is for other id Tech 3 engines, or for building changed PK3s to share.

Drop an image into a folder and texswap replaces the texture of the same name inside the game's PK3
archives, whatever format the archive holds it in. It works like a texture replacement folder:
`wall01.png` replaces `textures/.../wall01.tga`, `wall01.jpg` or `wall01.dds`.

It's a small command-line tool: one Python file using only the standard library (Python 3.8 or newer).
It runs on Windows, Linux and macOS. It targets engines that load PNG and DDS: Oz Arena, Quake3e and
ioquake3. The original 1.32 executable reads only TGA and JPG, so with it, drop TGA or JPG files.

## Install

1. Install Python 3.8 or newer.
2. Copy `texswap.example.json` to `texswap.json` and edit the paths (see Config below).
3. Run `python texswap.py list` to see what each dropped file would replace.

## Usage

```
python texswap.py watch     # apply what's in the folder, then apply every new or changed drop
python texswap.py apply     # apply what's in the folder once, then exit
python texswap.py list      # what each drop would replace; writes nothing
python texswap.py restore   # put every original PK3 back (<pak>.texswap-orig)
```

All four take `--config <file>` (default `texswap.json`). `apply` and `watch` also take `--dry-run`,
which logs every swap they would make and writes nothing. `apply --only <file> ...` applies just those
drops.

Close the game before you write. It holds its PK3s open, and Windows won't replace an open file. On
Linux and macOS the game keeps reading the old file until it restarts. Either way, the game reads PK3s
only at startup.

## Config

| Key | Default | What it does |
|---|---|---|
| `game_dirs` | `[]` | Folders whose `*.pk3` are searched, lowest priority first: `baseq3`, then a mod folder. |
| `pk3s` | `[]` | Extra PK3 files, searched after (above) the `game_dirs`. |
| `watch_folder` | `texture_replacements` | The drop folder. Relative paths are relative to the config file. |
| `match` | `"all"` | `"all"` replaces the texture in every PK3 that has it. `"first"` replaces only the copy the game actually loads (the highest-priority PK3). |
| `path_aware` | `true` | Subfolders in the drop folder narrow the match (see Matching). |
| `extensions` | png, jpg, jpeg, tga, dds, bmp, pcx | Image types, on both the drop side and the PK3 side. |
| `dry_run` | `false` | Log only; never write. |
| `backup` | `true` | The first time a PK3 changes, keep its original as `<pak>.texswap-orig` beside it. |
| `protect_id_paks` | `true` | Never write `baseq3/pak0.pk3` to `pak8.pk3` (see below). |
| `poll_seconds` | `1.0` | How often `watch` looks at the folder. |
| `settle_seconds` | `1.5` | A drop must stop changing this long before it's applied, so half-copied files are never used. |
| `log_file` | `texswap.log` | The log, inside the drop folder. `""` turns it off. |

`texswap.example.json` points at a Steam install's `baseq3` and the Oz Arena folder that
OzArenaSetup makes inside it. For a mod, add its folder after `baseq3` (for example `.../cpma`).

## How matching works

- A drop's name without its extension is its **stem**: `wall01` from `wall01.png`.
- Every image entry in the configured PK3s is compared by its **leaf stem**: the last part of its path,
  without the extension, ignoring case. `textures/base_wall/wall01.tga` matches `wall01`. Its
  extension doesn't matter.
- **Path-aware drops:** put a drop in subfolders that copy the game's path, and only entries whose path
  ends with those folders match. `texture_replacements/textures/base_wall/wall01.png` and
  `texture_replacements/base_wall/wall01.png` both match `textures/base_wall/wall01.*` but not
  `textures/gothic_wall/wall01.*`. A drop at the top of the folder matches the stem anywhere.
- The match is exact: `wall01.png` never touches `wall01_n.dds` or `wall01blood.tga`.
- When a stem matches entries in more than one folder, the log warns. All of them are still replaced;
  use a subfolder to narrow it.
- A drop that matches nothing is logged as a warning and left in the folder. texswap never deletes,
  moves or edits a drop.

## How extension mismatches are handled

**The choice: the entry is renamed to the drop's extension, in the same folder with the same stem, and
holds the drop's bytes unchanged.** `wall01.png` dropped over `textures/base_wall/wall01.tga` removes
that entry and writes `textures/base_wall/wall01.png` in its place.

Why not keep the old name and swap only the bytes:

- The engine picks an image's decoder from the file name's extension and never looks at the bytes
  (`R_LoadImage` in every id Tech 3 renderer). PNG bytes stored as `wall01.tga` go to the TGA reader
  and the texture fails to load.
- The engine finds a renamed entry anyway. A shader or map asks for `textures/base_wall/wall01.tga`.
  When that file is missing, the engine strips the extension and tries `.png`, `.tga`, `.jpg`,
  `.jpeg`, `.pcx` and `.bmp`, in that order, and loads `wall01.png`. Oz Arena's renderer looks for a
  `.dds` beside the name first.
- So the old entry has to go, not just get a neighbor. If `wall01.tga` stayed, the exact name the shader
  asks for would still exist and win.

That's also why `match: "all"` is the default. If another PK3 still holds `wall01.tga`, the game finds
that exact name there before it ever tries `.png`, and the drop doesn't show. With `"first"`, only the
highest-priority copy changes, so use it only when no lower PK3 has the texture under the exact name the
shader asks for.

Other details:

- The bytes go in exactly as dropped: no conversion, resize or recompression. Oz Arena's normal maps
  (`_n`) expect the renderer's green-down convention. A plain `_n.png` replaces them as it is.
- PNG, JPG and DDS are stored uncompressed in the PK3; TGA, BMP and PCX are deflated.
- If one PK3 holds the same stem in two formats in one folder (`wall01.tga` and `wall01.jpg`), both are
  removed and one `wall01.png` replaces them.
- Running again with nothing new changes nothing: a PK3 whose entry already holds the drop's exact bytes
  under the drop's name is skipped. That makes `watch` safe to restart.

## Keeping archives safe

- Each changed PK3 is written whole to `<pak>.texswap-tmp` beside it. Untouched entries are copied
  byte for byte, without decompressing and recompressing.
- The temp file is checked before it's used: every entry's CRC (`testzip`), exactly the entry names
  planned, and each new entry's CRC and size against its drop. If anything fails, the temp file is
  deleted and the PK3 is left as it was.
- Only then is it swapped in. With `backup` on, the original is first renamed to
  `<pak>.texswap-orig`; that's instant, and the first original is never overwritten by later runs. Then
  the temp file is renamed over the PK3. That rename is atomic on the same drive. If it fails (the game
  holds the file), the original is renamed back.
- The game ignores `.texswap-orig` and `.texswap-tmp` files, because it loads only `*.pk3`.
- `restore` renames every `<pak>.texswap-orig` back over its PK3.

## id's paks and online play

- `protect_id_paks` (on by default) keeps texswap out of `baseq3/pak0.pk3` to `pak8.pk3`. A changed
  id pak no longer matches its checksum. The engine warns at every start, Steam's file check puts the
  original back, and a pure server (`sv_pure 1`) refuses or ignores a client whose paks don't match.
- A texture that lives only in id's paks therefore can't be swapped in place. Turn the protection off
  only for offline play, and keep `backup` on so `restore` can undo it.
- The same goes for any PK3 a pure server lists: changed, it no longer matches the server's copy. Oz
  Arena's own world paks are left out of pure checks unless a server asks for them.

## Quake III texture paths inside PK3s

- A PK3 is a zip. Paths use forward slashes and are case-insensitive to the game.
- World textures are `textures/<set>/<name>.tga|.jpg`, for example `textures/base_wall/metalfloor_wall_10.tga`
  or `textures/gothic_block/blocks18c.jpg`. A shader (`scripts/*.shader`) or a map surface names the
  path, usually with `.tga`, sometimes `.jpg`, and the engine loads whatever format exists at that stem.
- Other images: `models/players/<model>/<part>.tga` (player skins), `models/weapons2/<weapon>/`,
  `models/powerups/`, `gfx/` (HUD and effects), `sprites/`, `env/<sky>_<face>.tga` (sky boxes),
  `levelshots/<map>.jpg`, `menu/art/`.
- Oz Arena's remastered textures are `.dds` at id's paths (`textures/gothic_block/blocks18c.dds`), with
  `_n.dds` (normal) and `_s.dds` (specular) beside them, in `oz/pak_oz_world*.pk3`.
- Leaf names repeat across folders, for example `black`, `light` or `metal` in several sets. That's
  why a drop at the top of the folder warns when it lands in more than one folder, and why path-aware
  subfolders exist.
