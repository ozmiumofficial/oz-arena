# oDFe sync: Defrag's engine additions in the Oz engine

oDFe (Defrag-racing/oDFe, GPL-2.0) is a Quake3e fork with Defrag's tools. Its additions are merged into `engine/quake3e` (ENGINE_PLAN.md PC step 1), crediting its authors: Jelle, runaos, mediumBeast, Eugene, neyoneit, KG7x, Jelvan1, Ensiform and 742617000027.

## Sync point

| | Commit | Date |
|---|---|---|
| oDFe last merged Quake3e at | `896bc726` | 2026-07-07 |
| Merged from oDFe | `5c573f4e` (`odfe/main`) | 2026-08-17 |

The whole of oDFe's change since its Quake3e base (`git diff 896bc726 odfe/main -- code`, 128 commits, about 2,300 lines of code) was three-way applied onto Quake3e 2026-10-14 plus Oz's changes. Five files conflicted (resolutions below). Left out: CI workflows, the README, the MSVC project files, and `code/libpcre2` (prebuilt PCRE2 binaries; `build_engine.py` builds PCRE2 10.43, oDFe's version, from source instead).

To sync again: `python engine/tools/upstream_report.py --fetch` lists oDFe's new commits. Apply them with `git diff <old> <new> -- code | git apply -3 --directory=engine/quake3e`, keep the choices below, build, run coldrun, then `upstream_report.py --mark odfe`.

## What came in

- **Trigger, clip and slick views** (`cl_tc_vis.c`): `r_renderTriggerBrushes`, `r_renderClipBrushes`, `r_renderSlickSurfaces` and their shader cvars; weapclip shown.
- **Console:**
  - Filters by regular expression (`con_filter`, `con_filter0`-`9`, PCRE2).
  - Time display (`con_timedisplay`) and `con_scale`.
  - Configurable height (`con_height`, `con_heightShift`, `con_heightCtrl`, `con_heightAlt`).
  - Not printing typed messages twice.
- **Input:** `in_blockWinKey`, `in_useInGamePrintScrn`, model and skin autocompletion, the `cv` command.
- **Downloads:**
  - `dl_source` (Defrag's name for `cl_dlURL`, `%m` for the map), `dl_source2` (a second source with a speed test and fallback), `dl_usebaseq3`.
  - `cl_mapAutoDownload` defaults to 1.
- **Clock:** `cl_localTime`, a PLL-based local rendering clock.
- **Servers:**
  - `getdfstatus` (DFStatus v2, dedicated only).
  - `fs_mapPakDir`, on-demand per-map paks (dedicated only).
  - `sv_noReferencedPaks`.
- **Fixes:**
  - The gamestate overflow on maps like csq3sg1.
  - A `vid_restart` crash, `VM_Call` with no arguments in DLL mode, and the z-fighting of the views.
  - The hunk minimum raised to 128 MB.

## Kept from Quake3e (oDFe changed these for Defrag only; Oz plays every mod)

| | Oz | oDFe |
|---|---|---|
| Window title, `DEFAULT_GAME` | Oz Arena, `oz` | Open DeFRaG Engine, `defrag` |
| `bot_enable` | 1 | 0 |
| `cl_allowDownload` | 1 (community servers send their maps) | 0 |
| `r_singleShader` | a cheat | unlocked |
| `fs_steampath` | found from Steam's libraries (Oz) | empty |
| Connect prefix | `q3a:/` and `defrag:/` both accepted | `defrag:/` |
| Color table `^4`, `^9` | Quake3e's and CNQ3's (CPMA's) | pure blue, gray. Per mod in PC step 3 |

## Conflict resolutions

- `cl_cgame.c`: Quake3e's newer hunk handling at map load, then oDFe's `tc_vis_init()`.
- `files.c`: Oz's `fs_steampath` and `fs_mapspath`, plus oDFe's `fs_mapPakDir`.
- `q_shared.h`: Oz's identity.
- `sv_init.c`: oDFe's `sv_noReferencedPaks` around Quake3e's code (with Quake3e's current two-argument `FS_TouchFileInPak`).
- `win_wndproc.c` (win32 backend only, not used with SDL): the Windows key is caught outside menus and chat when `in_blockWinKey` is on or the key is bound.

## Checked (2026-10-28)

Builds with no new warnings. The Defrag bundle's mod (`fs_game defrag`, its cgame DLL proxy and HUD) runs coldrun, and `r_renderTriggerBrushes 1` draws the course triggers.

2026-10-29, `engine/tests/compat/feature_test.py` on its own course (`maps/oz_defrag.bsp`): Defrag times a run from the start line to the finish line (2:400, 768 units at 320 units/s). `r_renderClipBrushes 1` draws the clip block in red and `r_renderTriggerBrushes 1` the lines in green, with the bundle's `tcRenderShader` (`zzz-trigger-clip-clean-brush.pk3`).
