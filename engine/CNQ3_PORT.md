# CNQ3's extensions in the Oz engine

CNQ3 (bitbucket.org/CPMADevs/cnq3, GPL-2.0, by Gian 'myT' Schellenbaum) is CPMA's own engine. CPMA turns on extra features when the engine offers them through `trap_GetValue` (syscall 700, announced in the `//trap_GetValue` cvar, the same mechanism Quake3e uses). The Oz engine now offers CNQ3's set, so CPMA behaves as it does on CNQ3 (ENGINE_PLAN.md PC step 2). Ported from CNQ3 `ef32da8c`.

Mods take the syscall numbers from `trap_GetValue`, so Oz's numbers (after 700 in `cg_public.h` and `ui_public.h`) don't have to match CNQ3's.

## What CPMA 1.53 asks for, and what the engine answers

Traced on 2026-10-28 with the CPMA 1.53 install in the Quake 3 folder.

| Key | cgame | UI | Status |
|---|---|---|---|
| `trap_LocateInteropData` | yes | yes | done (buffers kept per VM) |
| `trap_SetInputForwarding` | yes | | done: mouse (1) and keys (2) also go to the cgame, keys instead of their bindings, never escape |
| `trap_CNQ3_SetCharEvents` | | | done |
| `trap_Cvar_SetRange` | yes | yes | done: CNQ3's float, integer, bitmask and bool types on Quake3e's validators; strings and colors unchecked |
| `trap_Cvar_SetHelp` | yes | yes | done (Quake3e's `Cvar_SetDescription2`) |
| `trap_Cmd_SetHelp` | yes | yes | accepted, ignored (Quake3e keeps no command help) |
| `trap_MatchAlertEvent` | yes | | done: task bar flash and beep, `cl_matchAlerts` as in CNQ3 (unmute, 8, isn't supported) |
| `trap_Error2` | yes | yes | done (ERR_DROP) |
| `trap_CNQ3_R_RenderScene` | yes | | done. Its second argument is CNQ3's renderer time in microseconds, not a size. |
| `trap_CNQ3_R_AddRefEntityToScene` | | | done (no motion blur) |
| `trap_CNQ3_FS_GetFileList` | | | done |
| `trap_CNQ3_NDP_*` | yes | | done: the new demo player, below |
| `cap_ExtraColorCodes` | yes | yes | 1: the CPMA color table is Quake3e's |
| `cap_DepthClamp` | yes, every frame | | 0: no renderer offers depth clamping yet |
| `trap_R_AddRefEntityToScene2`, `trap_R_ForceFixedDLights`, `trap_IsRecordingDemo` | yes | | already in Quake3e |
| `screenshotnc`, `screenshotncJPEG` | yes | yes | not offered yet (screenshots without the HUD) |
| `trap_EnableErrorCallback` | | yes | not offered yet (the UI shows engine errors itself) |
| `trap_CNQ3_NK_*` (Nuklear menus) | no | no | not built: CPMA 1.53 never asks for them |

## The new demo player (`client/cl_ndp.c`)

CNQ3's `cl_demo.cpp`, translated to C. A CPMA demo is parsed once into a compressed, seekable copy in memory, with a full snapshot every 8 seconds. CPMA's demo interface then seeks and rewinds through it: the timeline, `demo_seek`, event markers, and video export through `NDP_StartVideo`.

- `cl_demoPlayer 1` (default) uses it for mods that support it. Oz plays every mod, while CNQ3 only runs CPMA. When a demo's cgame doesn't call `trap_CNQ3_NDP_Enable`, the engine shuts that cgame down and reopens the file, then plays it the classic way. The game name is remembered for the session, so the next demo of that mod loads once.
- Protocol 68 demos (Quake3e's "compat") go through it too; that is CNQ3's format.
- A read error while loading ends the loading (`CL_NDP_HandleError` from `Com_Error`), and the demo plays as if truncated, as in CNQ3.
- `vid_restart` loads the demo again, and the cgame restores its own state, as in CNQ3.
- Hooks: `cl_cgame.c` (snapshots, commands and the clock go to the player while `clc.newDemoPlayer` is set, the `NDP_*` syscalls, and the `CL_CGNDP_*` calls into CPMA), `cl_main.c` (demo start, `vid_restart`), `common.c` (`Com_Error`). Quake3e's `MSG_ReadBits` is public again for it.
- `cl_demoDump <file>` (Oz) lists the snapshots it hands the cgame, each once and not during the load's analysis pass, so a seek shows as a jump in server time.
- `cl.snap` and `cl.parseEntities` (Oz), for the engine's HUD: CNQ3 leaves them empty while this player plays (`CL_ClearState` at the gamestate). Each snapshot handed to the cgame goes there too (`SetClientSnapshot`): the newest one, or the first one after a seek, which may be older. `cl.serverTime` is the time the cgame last read up to (`trap_CNQ3_NDP_ReadUntil`, every frame, with CPMA's own playback clock). CPMA's playback doesn't use either: `feature_test.py`'s seeks and rewind give the same results with them.

## Checked (2026-10-28)

- CPMA 1.53 plays cpm22 (duel) with its own HUD on the engine.
- **A recorded 40-second CPMA demo** loads in the new player in 0.7 s, and CPMA's timeline shows a frag marker.
  - `demo_seek relativeTime 20` jumps from 0:05 to 0:27.
  - `demo_seek relativeTime -15` rewinds to 0:13.
  - `vid_restart` reloads and resumes at the same time.
- id's `four.dm_68` (baseq3 cgame, no support) falls back and plays the classic way.
- 2026-10-29, `engine/tests/compat/feature_test.py`: CPMA's main and in-game menus draw (`togglemenu` opens the in-game one). A recorded demo seeks +15 s and -18 s, and plays on after the rewind with the same snapshots as the first pass.
