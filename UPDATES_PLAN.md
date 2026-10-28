# Auto-update plan

Status: **U1-U5 built and tested (2026-10-27).** The game downloads updates from GitHub itself, with no browser and nothing for the player to download by hand: only the files that changed (usually `patch.pck`, KB to MB), installed when the game quits or on Restart now, rolled back if the new build doesn't reach its menu. A first install from the zip alone fetches its texture packs at the splash. Decision (Ozmium, 2026-10-27, replacing "ask first" of 10-25): **Update automatically is on by default**; off, the menu's Update now button starts it. Players on v0.5.0-beta and older have no updater, so they download the first release with it by hand once.

### As built (2026-10-27)

- **Boot:** `UpdateBoot` (`scripts/updates/update_boot.gd`), the first autoload, mounts `<exe dir>/patch.pck` in `_init`, before the main scene and the other autoloads load. New `class_name` scripts in a patch work (checked with a probe export). It then sets `application/config/version` from `res://game/version.txt`, which `export_game.py` writes from project.godot, so a patch carries the version bump (project.godot itself can't be patched). It prints `[update] Q3Remaster <version> (patch.pck)`. Never change UpdateBoot in a patch.
- **Download:** `Q3UpdateInstall` (`scripts/updates/q3_update_install.gd`): the release's `manifest.json` + `.sig` from `github.com/<repo>/releases/download/<tag>/`, verified; local hashes cached in `user://update/hashes.cfg` by size and time; each needed file in 16 MB `Range` requests into `<game>/.update/stage/<path>.part` (resumes), hash-checked, then `.update/apply.txt`. States go to the menu as `update_status`.
- **Install:** `Q3Updater.exe` (`tools/q3core/src/updater/q3_updater.c`, built by SConstruct.tools, beside the exe in every export). The game copies it to `.update/` and starts it with its pid; it waits for the exit, moves each target to `.update/backup/` and the staged file in, and on any failure puts everything back. Restart now (relaunch 1) starts the new build and waits up to 3 min for `.update/started`, which `Q3UpdateInstall.confirm_started()` writes when the menu opens; if the build exits first it rolls back, starts the old one, and the menu says so. At quit (relaunch 0) the next good start clears the backup; `Q3Updater.exe --rollback <dir>` restores it by hand. Log: `.update/updater.log`.
- **Releases:** `export_game.py --release <tag>` keeps the base pack in `build/release/_base/` and exports `patch.pck` against it (`godot --export-patch`). A new base is cut when project.godot (other than its version line) or UpdateBoot changed, when the patch passes 200 MB, or with `--new-base`; a base release ships a patch.pck holding only `game/version.txt` (`tests/pack_files.tscn`), so it replaces an older base's patch. Never an empty one: Godot's `--export-patch` writes a pack with no files when nothing changed, and mounting it hangs the game at startup (v0.6.0-beta's first export, 2026-10-27). The manifest lists the base pack with `"tag": <base release>`, so it's uploaded once. `build/release/<tag>/assets/` holds every file the updater fetches from this release by flat name; the export prints the `gh` commands. **Keep the base release** when deleting old ones.
- **Menu:** `UpdatePrompt.tsx`: Update now / progress / Restart now; a build without the updater shows View release. Settings > Network > Update automatically (`network/auto_update`).
- **Tests:** `python tools/q3core/test_updater.py` (the updater on a fake install: relaunch, crash rollback, at quit, `--rollback`, missing file, refused path); `python tools/test_update_e2e.py <exported build>` (a real build: a patch release served like GitHub with Range and the real key, downloaded, installed at quit, the new version running, then a deleted texture pack fetched at the splash); `tests/update_check_test.tscn`, `tests/update_manifest_test.tscn`. `-- update_server=http://127.0.0.1:8765/` points a build at a local release server (still signature-checked).

## How it works

1. **Check.** At launch (main menu only, never mid-match) the game asks
   `api.github.com/repos/ozmiumofficial/quake-3-arena-remastered-fan-project/releases/latest` once. No login; the unauthenticated limit (60 an hour per IP) is plenty. It compares the tag with `application/config/version` (now `0.3.0-beta`). Failures are silent: offline players just see no prompt.
2. **Ask.** The menu shows "Update available: vX, N MB" with the release notes and three choices: Update now, Later, Skip this version. Settings > Game > Updates holds: Check for updates (on), Update automatically (off), Include betas.
3. **Download.** Into `user://update/`, resumable (Range requests), with progress in the menu. The game stays playable while it downloads.
4. **Verify.** `manifest.json` (attached to the release) lists every file with its SHA-256 and size, and `manifest.sig` signs its exact bytes with RSA-3072 (PKCS#1 v1.5, SHA-256). Godot has no Ed25519, but its `Crypto` verifies this, and `tests/update_manifest_test.tscn` proves a Python-signed manifest verifies in Godot. The public key ships as `godot/game/update_key.json`. Anything that doesn't verify is deleted and never run.
5. **Apply.** On quit (or "Restart now"), `Q3Updater.exe` (small, separate, in the build) waits for the game to exit, swaps the changed files, keeps the old ones until the new build starts, and relaunches. If the new build fails to start, it puts the old files back.

## What an update downloads

- The **Textures** release (3 packs, permanent) is never part of an update; it changes only when textures change, and the manifest lists those packs separately so the updater can fetch just a changed one.
- A game release is the zip (exe, DLLs, `Q3Remaster.pck`, `webui_assets.pck`, q3core). The updater compares the manifest's hashes with the installed files and downloads only files that differ. To make that cheap, each release also attaches its files individually (the zip stays for first installs).
- **Measured (U2, 2026-10-25, v0.4.5-beta):** the manifest lists 23 files. Release files total 1.30 GB: `Q3Remaster.pck` 1,097 MB, `Q3Remaster.exe` 104 MB (plus the console exe), the rest small DLLs and `webui_assets.pck` (29 MB). Texture packs: 3 x 1.48 GB. Retail paks: 26 MB. The code in `Q3Remaster.pck` is only a few MB; the rest is the library (maps 828 MB, characters, cinematics, props), so any script change would re-download 1.1 GB.
- **Decision: Godot patch packs, not a second library pack.** `godot --export-patch "Windows Desktop" patch.pck --patches base.pck` writes only the files that changed since the base pack. A one-script change gave a **211 KB** patch against the 1,185 MB base, exported in 16 s (a full pack takes 10 min). The base is the pack of the release the player first installed; each release ships a patch against that base (cumulative: it grows as the library changes, and a new full base is cut when it gets large, say over 100 MB). The manifest then lists `Q3Remaster.pck` (base) and `patch-<n>.pck`; the updater downloads a patch and replaces the old one.
- **What U3 and U4 must handle for patches:** the game loads a patch with `ProjectSettings.load_resource_pack(path, true)`, and only scripts loaded after that see it. Project settings and the first autoloads are already loaded, so a tiny first autoload that never changes (`UpdateBoot`) mounts patches before anything else; changes to `Diagnostics` or `project.godot` need a new base. The exe, DLLs and `webui_assets.pck` stay whole files, replaced only when their hash differs.

## Safety

- Signed manifest (`tools/release_manifest.py`), key kept off GitHub: `python tools/release_manifest.py keygen` makes it once in `%USERPROFILE%\.q3remaster
elease_key.pem` (set `Q3_RELEASE_KEY_PASSPHRASE` first to encrypt it) and writes the public half to `godot/game/update_key.json`; `export_game.py --release` then signs every release. Without the key a release gets an unsigned manifest and the updater refuses it. A hijacked GitHub account can't push code to players without it.
- Only HTTPS to `api.github.com` and `github.com` release downloads.
- Never touches the player's Quake 3 files or `user://` (profile, settings, replays, maps).
- Never updates during a match, a replay clip export or a map-editor session.
- The online hub can report a minimum compatible version, so a netcode change shows a stronger "Update required to play online" prompt (multiplayer M-phases).

## Phases

| | Work | Hours |
|---|---|---|
| U1 | Version check, update prompt in the web menu (bridge messages `update_check`, `update_status`), Settings entries, test with a mock release feed | 2 |
| U2 (done) | `tools/release_manifest.py` writes and signs `manifest.json`; `export_game.py --release` calls it; `Q3UpdateManifest` verifies and validates it in the game; `Q3Remaster.pck` measured; patch packs chosen | 3 |
| U3 | Downloader (resume, progress, hash check) in GDScript; `UpdateBoot` autoload that mounts patch packs first; `export_game.py` exports `patch-<n>.pck` against the release's base and attaches release files by their flat asset names | 4 |
| U4 | `Q3Updater.exe` (swap, rollback, relaunch), added to the export | 3 |
| U5 | End-to-end test against a local release folder: old build updates to new, bad signature refused, interrupted download resumes, failed start rolls back; README and DEVELOPMENT.md | 2 |

About 12 hours (U1 took about 2). Check U1 with `tests/update_check_test.tscn` (headless, 30 checks; `-- feed=http://127.0.0.1:8765/releases.json` also fetches a local feed over HTTP) and the web preview's `?update`. U1 is useful alone (players get told a new version exists and a link to it) and carries no risk.
