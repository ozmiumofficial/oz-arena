# Running a Q3Remaster server on a machine

A dedicated server is the game run headless: `godot --headless --path godot -- dedicated=<config.json>`. No
window, no menu, no GPU. It lists itself in everyone's Server Browser under its own identity, so players
reach it through Cloudflare's relay over WebRTC, the same way they join a player-hosted match: **you open no
ports and no one sees anyone's IP**. Optional `udp_port` also answers plain Quake 3 clients, LAN queries and
rcon tools on that port (players who come in over UDP have their real addresses seen).

You need your own retail Quake 3 paks (pak0.pk3 and the rest). Nothing from id ships with this.

## Windows (tested with the Godot editor binary; the exported exe is the same code but not tried headless)

The game's menu has Play > Dedicated Server: it writes the config and starts the same headless process, and
can start it with Windows. By hand:

```
Q3Remaster.exe --headless -- dedicated=C:\path\server.json
```

## Linux / a rented server (written, not yet built or run)

The Linux build of the engine library (`tools/build_q3core.py --platform linux`; the SConstruct picks
`sys_unix.c`) and `server/Dockerfile` were written on a Windows PC with no Linux to try them. They are the
plan, not a result: the first build on a Linux machine or CI will likely need small fixes. What it needs:

1. Build `libq3core.linux.template_release.x86_64.so` and `qagamex86_64.so` (the Dockerfile does it). The
   WebRTC addon already ships its Linux library.
2. Godot 4.7.2 for Linux x86-64 and the `godot/` project.
3. Your paks in a folder; set `Q3_BASEQ3` to it.
4. `cp server/server.example.json /data/server.json`, edit, run.

The server keeps its files beside its config (`/data` for `dedicated=/data/server.json`): its identity
`server.key` (keep it; deleting it makes a new server), `status.json`, `commands.txt` and `stop`. (Godot's own
`user://` folder, `$XDG_DATA_HOME/godot/app_userdata/Q3Remaster/` on Linux, holds only its settings and logs.)

Known gaps on Linux: crash detection between runs needs a process start time that is only written for
Windows (`q3_process_start_time`), so the diagnostics may misreport an earlier run there.

Size: the server needs none of the textures, models or sounds. Godot's own Dedicated Server export mode
(Export > Resources > Export Mode "Export as dedicated server", Linux preset; it strips what a headless run
never loads and sets the `dedicated_server` feature) is the right way to package it instead of copying
`godot/` whole as the Dockerfile does for now. Not set up yet: the preset needs trying with q3core's `.so`
and the scripts the server loads (the maps list in `library/maps/*.json` is read, the GLBs are not).

## Running it

Config keys (`server.example.json`): name, password, mode, rotation (map names; empty is every map of the
mode), rules (fraglimit, timelimit ...), max_players, bots (filled up to this many players), bot_skill,
motd, rcon_password, online (list in the Server Browser), idle_sleep, udp_port (0 is off).

Control: with `udp_port` set, any Quake 3 rcon tool works with `rcon_password`. Otherwise append console
commands to `commands.txt` beside the config, or create `stop` there to shut down cleanly. `status.json` is
rewritten every 2 seconds for monitoring (players, map, uptime, whether it's listed, the UDP port opened).

While the server sleeps (no humans, `idle_sleep`), it does not answer UDP queries either; turn
`idle_sleep` off for a LAN or public UDP server that should always answer.
