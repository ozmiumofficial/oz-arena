"""Entity catalog: every item and map entity class, linked to the remastered models.

python make_entities.py <extracted baseq3> <out json>

Items follow bg_itemlist[] in id's GPL game code (code/game/bg_misc.c): classname, pickup
sound, world models, icon, name, quantity, type, tag. Respawn times are g_items.c's
RESPAWN_* defines (seconds); weapons use g_weaponrespawn's default 5 (30 in team games).
Pickup display follows cg_ents.c CG_Item: items spin and bob, weapons draw at 1.5x.
Map entities are the spawn functions in g_spawn.c. Team Arena items are omitted unless
their models exist in the extracted files.
"""
import os, sys, json

ROOT, OUT = sys.argv[1], sys.argv[2]
# classname, pickup sound, model1, model2, icon, name, quantity, type, tag
ITEMS = [
    ("item_armor_shard", "sound/misc/ar1_pkup.wav", "powerups/armor/shard", "powerups/armor/shard_sphere", "icons/iconr_shard", "Armor Shard", 5, "armor", ""),
    ("item_armor_combat", "sound/misc/ar2_pkup.wav", "powerups/armor/armor_yel", None, "icons/iconr_yellow", "Armor", 50, "armor", ""),
    ("item_armor_body", "sound/misc/ar2_pkup.wav", "powerups/armor/armor_red", None, "icons/iconr_red", "Heavy Armor", 100, "armor", ""),
    ("item_health_small", "sound/items/s_health.wav", "powerups/health/small_cross", "powerups/health/small_sphere", "icons/iconh_green", "5 Health", 5, "health", ""),
    ("item_health", "sound/items/n_health.wav", "powerups/health/medium_cross", "powerups/health/medium_sphere", "icons/iconh_yellow", "25 Health", 25, "health", ""),
    ("item_health_large", "sound/items/l_health.wav", "powerups/health/large_cross", "powerups/health/large_sphere", "icons/iconh_red", "50 Health", 50, "health", ""),
    ("item_health_mega", "sound/items/m_health.wav", "powerups/health/mega_cross", "powerups/health/mega_sphere", "icons/iconh_mega", "Mega Health", 100, "health", ""),
    ("weapon_gauntlet", "sound/misc/w_pkup.wav", "weapons2/gauntlet/gauntlet", None, "icons/iconw_gauntlet", "Gauntlet", 0, "weapon", "WP_GAUNTLET"),
    ("weapon_shotgun", "sound/misc/w_pkup.wav", "weapons2/shotgun/shotgun", None, "icons/iconw_shotgun", "Shotgun", 10, "weapon", "WP_SHOTGUN"),
    ("weapon_machinegun", "sound/misc/w_pkup.wav", "weapons2/machinegun/machinegun", None, "icons/iconw_machinegun", "Machinegun", 40, "weapon", "WP_MACHINEGUN"),
    ("weapon_grenadelauncher", "sound/misc/w_pkup.wav", "weapons2/grenadel/grenadel", None, "icons/iconw_grenade", "Grenade Launcher", 10, "weapon", "WP_GRENADE_LAUNCHER"),
    ("weapon_rocketlauncher", "sound/misc/w_pkup.wav", "weapons2/rocketl/rocketl", None, "icons/iconw_rocket", "Rocket Launcher", 10, "weapon", "WP_ROCKET_LAUNCHER"),
    ("weapon_lightning", "sound/misc/w_pkup.wav", "weapons2/lightning/lightning", None, "icons/iconw_lightning", "Lightning Gun", 100, "weapon", "WP_LIGHTNING"),
    ("weapon_railgun", "sound/misc/w_pkup.wav", "weapons2/railgun/railgun", None, "icons/iconw_railgun", "Railgun", 10, "weapon", "WP_RAILGUN"),
    ("weapon_plasmagun", "sound/misc/w_pkup.wav", "weapons2/plasma/plasma", None, "icons/iconw_plasma", "Plasma Gun", 50, "weapon", "WP_PLASMAGUN"),
    ("weapon_bfg", "sound/misc/w_pkup.wav", "weapons2/bfg/bfg", None, "icons/iconw_bfg", "BFG10K", 20, "weapon", "WP_BFG"),
    ("weapon_grapplinghook", "sound/misc/w_pkup.wav", "weapons2/grapple/grapple", None, "icons/iconw_grapple", "Grappling Hook", 0, "weapon", "WP_GRAPPLING_HOOK"),
    ("ammo_shells", "sound/misc/am_pkup.wav", "powerups/ammo/shotgunam", None, "icons/icona_shotgun", "Shells", 10, "ammo", "WP_SHOTGUN"),
    ("ammo_bullets", "sound/misc/am_pkup.wav", "powerups/ammo/machinegunam", None, "icons/icona_machinegun", "Bullets", 50, "ammo", "WP_MACHINEGUN"),
    ("ammo_grenades", "sound/misc/am_pkup.wav", "powerups/ammo/grenadeam", None, "icons/icona_grenade", "Grenades", 5, "ammo", "WP_GRENADE_LAUNCHER"),
    ("ammo_cells", "sound/misc/am_pkup.wav", "powerups/ammo/plasmaam", None, "icons/icona_plasma", "Cells", 30, "ammo", "WP_PLASMAGUN"),
    ("ammo_lightning", "sound/misc/am_pkup.wav", "powerups/ammo/lightningam", None, "icons/icona_lightning", "Lightning", 60, "ammo", "WP_LIGHTNING"),
    ("ammo_rockets", "sound/misc/am_pkup.wav", "powerups/ammo/rocketam", None, "icons/icona_rocket", "Rockets", 5, "ammo", "WP_ROCKET_LAUNCHER"),
    ("ammo_slugs", "sound/misc/am_pkup.wav", "powerups/ammo/railgunam", None, "icons/icona_railgun", "Slugs", 10, "ammo", "WP_RAILGUN"),
    ("ammo_bfg", "sound/misc/am_pkup.wav", "powerups/ammo/bfgam", None, "icons/icona_bfg", "Bfg Ammo", 15, "ammo", "WP_BFG"),
    ("holdable_teleporter", "sound/items/holdable.wav", "powerups/holdable/teleporter", None, "icons/teleporter", "Personal Teleporter", 60, "holdable", "HI_TELEPORTER"),
    ("holdable_medkit", "sound/items/holdable.wav", "powerups/holdable/medkit", "powerups/holdable/medkit_sphere", "icons/medkit", "Medkit", 60, "holdable", "HI_MEDKIT"),
    ("item_quad", "sound/items/quaddamage.wav", "powerups/instant/quad", "powerups/instant/quad_ring", "icons/quad", "Quad Damage", 30, "powerup", "PW_QUAD"),
    ("item_enviro", "sound/items/protect.wav", "powerups/instant/enviro", "powerups/instant/enviro_ring", "icons/envirosuit", "Battle Suit", 30, "powerup", "PW_BATTLESUIT"),
    ("item_haste", "sound/items/haste.wav", "powerups/instant/haste", "powerups/instant/haste_ring", "icons/haste", "Speed", 30, "powerup", "PW_HASTE"),
    ("item_invis", "sound/items/invisibility.wav", "powerups/instant/invis", "powerups/instant/invis_ring", "icons/invis", "Invisibility", 30, "powerup", "PW_INVIS"),
    ("item_regen", "sound/items/regeneration.wav", "powerups/instant/regen", "powerups/instant/regen_ring", "icons/regen", "Regeneration", 30, "powerup", "PW_REGEN"),
    ("item_flight", "sound/items/flight.wav", "powerups/instant/flight", "powerups/instant/flight_ring", "icons/flight", "Flight", 60, "powerup", "PW_FLIGHT"),
    ("team_CTF_redflag", None, "flags/r_flag", None, "icons/iconf_red1", "Red Flag", 0, "team", "PW_REDFLAG"),
    ("team_CTF_blueflag", None, "flags/b_flag", None, "icons/iconf_blu1", "Blue Flag", 0, "team", "PW_BLUEFLAG"),
]
RESPAWN = dict(armor=25, health=35, ammo=40, holdable=60, powerup=120, weapon=5, team=0)  # g_items.c (seconds)

# g_spawn.c spawn functions: classname -> (category, what it is)
MAP_ENTITIES = {
    "worldspawn": ("world", "Map settings: message, music, gravity, ambient light"),
    "info_player_start": ("spawn", "Player spawn (old name, same as info_player_deathmatch)"),
    "info_player_deathmatch": ("spawn", "Deathmatch spawn point"),
    "info_player_intermission": ("spawn", "Intermission camera position"),
    "team_CTF_redplayer": ("spawn", "Red team initial spawn"), "team_CTF_blueplayer": ("spawn", "Blue team initial spawn"),
    "team_CTF_redspawn": ("spawn", "Red team respawn"), "team_CTF_bluespawn": ("spawn", "Blue team respawn"),
    "info_null": ("marker", "Removed on load; used as a light target"), "info_notnull": ("marker", "Positional target"),
    "info_camp": ("marker", "Bot camping spot"), "path_corner": ("marker", "Waypoint for func_train"),
    "target_position": ("marker", "Destination for jump pads and targets"),
    "misc_teleporter_dest": ("teleport", "Teleporter exit"), "trigger_teleport": ("teleport", "Teleporter volume"),
    "target_teleporter": ("teleport", "Teleports the activator"),
    "trigger_push": ("movement", "Jump pad volume, aims at its target"), "target_push": ("movement", "Push the activator"),
    "func_door": ("mover", "Sliding door"), "func_button": ("mover", "Button"), "func_plat": ("mover", "Lift platform"),
    "func_rotating": ("mover", "Constantly rotating brush"), "func_bobbing": ("mover", "Bobbing brush"),
    "func_pendulum": ("mover", "Swinging brush"), "func_train": ("mover", "Brush following path_corners"),
    "func_static": ("mover", "Static brush model"), "func_timer": ("logic", "Fires targets on a timer"),
    "func_group": ("world", "Editor grouping, merged into the world"),
    "trigger_always": ("logic", "Fires once at map start"), "trigger_multiple": ("logic", "Repeatable trigger volume"),
    "trigger_hurt": ("hazard", "Damage volume (lava, void)"),
    "target_give": ("logic", "Gives items to the activator"), "target_remove_powerups": ("logic", "Strips powerups"),
    "target_delay": ("logic", "Delayed relay"), "target_relay": ("logic", "Relay"), "target_kill": ("logic", "Kills the activator"),
    "target_speaker": ("audio", "Sound emitter"), "target_print": ("logic", "Prints a message"),
    "target_laser": ("hazard", "Damaging laser"), "target_score": ("logic", "Adds score"),
    "target_location": ("marker", "Named location for team chat"), "light": ("world", "Light, baked by the map compiler"),
    "misc_model": ("decoration", "Static model placed in the map (models/mapobjects)"),
    "misc_portal_surface": ("render", "Mirror or portal surface"), "misc_portal_camera": ("render", "Portal view camera"),
    "shooter_rocket": ("hazard", "Fires rockets"), "shooter_grenade": ("hazard", "Fires grenades"),
    "shooter_plasma": ("hazard", "Fires plasma"),
}

items = []
for cls, snd, m1, m2, icon, name, qty, typ, tag in ITEMS:
    models = [m for m in (m1, m2) if m and os.path.isfile(os.path.join(ROOT, "models", m + ".md3"))]
    items.append(dict(classname=cls, name=name, type=typ, tag=tag, quantity=qty, respawn=RESPAWN[typ],
                      models=models, pickup_sound=snd, icon=icon, scale=1.5 if typ == "weapon" else 1.0))
mapobjects = sorted(os.path.relpath(os.path.join(d, f), os.path.join(ROOT, "models")).replace("\\", "/")[:-4]
                    for d, _, fs in os.walk(os.path.join(ROOT, "models", "mapobjects")) for f in fs
                    if f.endswith(".md3") and not f[:-4].rsplit("_", 1)[-1].isdigit())
cat = dict(
    _note="Items from bg_misc.c bg_itemlist; respawn seconds from g_items.c; map entities from g_spawn.c. "
          "Model ids are paths under models/ without .md3 and match library/props/<id>.glb.",
    pickup_display=dict(spin_period_ms=2048, bob_units=4, bob_rate=0.005, weapon_scale=1.5),
    items=items,
    map_entities=[dict(classname=k, category=v[0], description=v[1],
                       models=mapobjects if k == "misc_model" else []) for k, v in MAP_ENTITIES.items()],
)
os.makedirs(os.path.dirname(OUT), exist_ok=True)
json.dump(cat, open(OUT, "w"), indent=1)
missing = [m for i in ITEMS for m in (i[2], i[3]) if m and not os.path.isfile(os.path.join(ROOT, "models", m + ".md3"))]
print("%d items, %d map entity classes, %d misc_model objects; models not in your paks: %s" % (
    len(items), len(MAP_ENTITIES), len(mapobjects), missing or "none"))
