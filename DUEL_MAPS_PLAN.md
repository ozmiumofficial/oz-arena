# Duel map pool and ranked duels

**Decided by Ozmium, 2026-10-25:** these 15 maps are the duel pool. Ranked duels play only these. We ship remastered versions of all of them, and we don't ask the authors for permission. This replaces "Quake 3's maps stay id's, the community ports Quake Live's" for these 15 (the other Quake Live maps stay community ports). The 35 retail Quake 3 maps stay in the game as they are.

Aerowalk, Blood Run, Battleforged, Campgrounds, Cure, Dismemberment, Elder, Furious Heights, Hektik, House of Decay, Lost World, Silence, Sinister, Toxicity, Vertical Vengeance.

The pool data is `godot/game/duel_pool.json`. Duel mode lists the pool's maps first (`Session.duel_pool_entries()`, `ranked_maps()`); a ranked setup is refused on any other map (`Q3MatchSetup.validate`). The hub can't read the file, so its list is `MODES.duel.maps` in `online/src/ranked.ts`: add a map's library id there when it ships.

## Where each map comes from

Status as of 2026-10-25. "In library" means the remastered map already exists in `godot/library/maps`.

| Map | Source | Status |
|---|---|---|
| Campgrounds | id's `pro-q3dm6` (Q3 pak6) | In library |
| Lost World | id's `pro-q3dm13` (Q3 pak6) | In library |
| Vertical Vengeance | id's `pro-q3tourney4` (Q3 pak6) | In library |
| Aerowalk | Hubster's `hub3aeroq3` (lvlworld) | Q3 release exists; import and remaster |
| Blood Run | ztn's `ztn3tourney1` (lvlworld) | Q3 release exists; import and remaster |
| Battleforged | Phantazm11's `map_phantq3dm1_rev` (lvlworld) | Q3 release exists |
| Dismemberment | Hubster's `hub3dm1` (lvlworld) | Q3 release exists |
| Toxicity | thefury's `pukka3tourney2`, "Evolution" (lvlworld) | Q3 release exists |
| Silence | OXOTHuK's `oxodm76`, an unofficial port of cityy's map (lvlworld) | Q3 port exists; QL's own BSP is the better match |
| House of Decay | Team Arena `mptourney1` | In the player's Team Arena paks, if they own them; needs the missionpack path in the importer |
| Hektik | swelt's QL map; a Q3 remake is on ws.q3df.org | Not in lvlworld; find the remake or rebuild |
| Cure | cityy, QL exclusive | No Q3 release; needs QL's BSP (v47) or a rebuild |
| Furious Heights | QL exclusive | Same |
| Sinister | Yellack and akm, QL exclusive | Same (lvlworld's "Sinister Purpose" is another map) |
| Elder | QL exclusive (2015) | Same |

The QL-exclusive five need a source. Quake Live is uninstalled and Ozmium doesn't want it looked at again, so the choices are the BSP import (version 47 support, `QUAKE_LIVE_PLAN.md`) with a BSP Ozmium supplies, or rebuilding the layout in the map editor from screenshots and videos.

## Risk

Most readmes forbid modification and distribution beyond free download (`tools/ql/QL_CATALOG.md` section 6.3). Shipping remastered copies without permission is a derivative-work risk like the id assets (PLAN.md "Licensing"). The mitigations to apply: credit every author in the map's `author` field and in the credits screen, keep each readme's text with the map, and keep the maps out of the repo's main branch history if a takedown arrives (the maps ride in the R2-hosted Workshop pack, M9, not in the LFS quota).

## Phases

| Phase | Work | Claude time |
|---|---|---|
| D1 Pool | `duel_pool.json`; duel mode's map list and the ranked queue read it; the match setup shows the pool; add the three maps already in the library. Tests: the pool's ids all resolve in `Session.maps`. | 1.5 h |
| D2 Importer | BSP version 47; `mptourney1` from the Team Arena paks; the hub's ranked queue refuses maps outside the pool. | 2 h |
| D3 Six Q3 releases | Aerowalk, Blood Run, Battleforged, Dismemberment, Toxicity, Silence: download from lvlworld, import, new textures through the PBR pipeline, items and spawns checked, lighting baked, bots' navigation. About 4 h each. | 24 h |
| D4 House of Decay | Import from Team Arena, remaster. | 4 h |
| D5 Hektik, Cure, Furious Heights, Sinister, Elder | Needs a source (above). From a BSP: about 4 h each. Rebuilt in the editor: about 10 h each. | 20 to 50 h |
| D6 Remaster pass | Each map gets the library's materials, props and mist, then a duel play-test (item timing, spawns, jump pads). | 0.5 h per map |

Total about 55 to 90 h of Claude time, depending on D5.
