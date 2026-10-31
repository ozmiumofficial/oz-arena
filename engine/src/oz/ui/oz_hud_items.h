// the game's item list (bg_misc.c bg_itemlist, id 1.32, without MISSIONPACK): index 0 is none.
// Generated from thirdparty/ioq3/code/game/bg_misc.c; baseq3, CPMA and Defrag number their items alike.
typedef struct { const char *icon, *name; int quantity, type, tag; } ozHudItem_t;
static const ozHudItem_t hudItems[] = {
	{ NULL, NULL, 0, 0, 0 },
	{ "icons/iconr_shard", "Armor Shard", 5, IT_ARMOR, 0 },	// item_armor_shard
	{ "icons/iconr_yellow", "Armor", 50, IT_ARMOR, 0 },	// item_armor_combat
	{ "icons/iconr_red", "Heavy Armor", 100, IT_ARMOR, 0 },	// item_armor_body
	{ "icons/iconh_green", "5 Health", 5, IT_HEALTH, 0 },	// item_health_small
	{ "icons/iconh_yellow", "25 Health", 25, IT_HEALTH, 0 },	// item_health
	{ "icons/iconh_red", "50 Health", 50, IT_HEALTH, 0 },	// item_health_large
	{ "icons/iconh_mega", "Mega Health", 100, IT_HEALTH, 0 },	// item_health_mega
	{ "icons/iconw_gauntlet", "Gauntlet", 0, IT_WEAPON, WP_GAUNTLET },	// weapon_gauntlet
	{ "icons/iconw_shotgun", "Shotgun", 10, IT_WEAPON, WP_SHOTGUN },	// weapon_shotgun
	{ "icons/iconw_machinegun", "Machinegun", 40, IT_WEAPON, WP_MACHINEGUN },	// weapon_machinegun
	{ "icons/iconw_grenade", "Grenade Launcher", 10, IT_WEAPON, WP_GRENADE_LAUNCHER },	// weapon_grenadelauncher
	{ "icons/iconw_rocket", "Rocket Launcher", 10, IT_WEAPON, WP_ROCKET_LAUNCHER },	// weapon_rocketlauncher
	{ "icons/iconw_lightning", "Lightning Gun", 100, IT_WEAPON, WP_LIGHTNING },	// weapon_lightning
	{ "icons/iconw_railgun", "Railgun", 10, IT_WEAPON, WP_RAILGUN },	// weapon_railgun
	{ "icons/iconw_plasma", "Plasma Gun", 50, IT_WEAPON, WP_PLASMAGUN },	// weapon_plasmagun
	{ "icons/iconw_bfg", "BFG10K", 20, IT_WEAPON, WP_BFG },	// weapon_bfg
	{ "icons/iconw_grapple", "Grappling Hook", 0, IT_WEAPON, WP_GRAPPLING_HOOK },	// weapon_grapplinghook
	{ "icons/icona_shotgun", "Shells", 10, IT_AMMO, WP_SHOTGUN },	// ammo_shells
	{ "icons/icona_machinegun", "Bullets", 50, IT_AMMO, WP_MACHINEGUN },	// ammo_bullets
	{ "icons/icona_grenade", "Grenades", 5, IT_AMMO, WP_GRENADE_LAUNCHER },	// ammo_grenades
	{ "icons/icona_plasma", "Cells", 30, IT_AMMO, WP_PLASMAGUN },	// ammo_cells
	{ "icons/icona_lightning", "Lightning", 60, IT_AMMO, WP_LIGHTNING },	// ammo_lightning
	{ "icons/icona_rocket", "Rockets", 5, IT_AMMO, WP_ROCKET_LAUNCHER },	// ammo_rockets
	{ "icons/icona_railgun", "Slugs", 10, IT_AMMO, WP_RAILGUN },	// ammo_slugs
	{ "icons/icona_bfg", "Bfg Ammo", 15, IT_AMMO, WP_BFG },	// ammo_bfg
	{ "icons/teleporter", "Personal Teleporter", 60, IT_HOLDABLE, HI_TELEPORTER },	// holdable_teleporter
	{ "icons/medkit", "Medkit", 60, IT_HOLDABLE, HI_MEDKIT },	// holdable_medkit
	{ "icons/quad", "Quad Damage", 30, IT_POWERUP, PW_QUAD },	// item_quad
	{ "icons/envirosuit", "Battle Suit", 30, IT_POWERUP, PW_BATTLESUIT },	// item_enviro
	{ "icons/haste", "Speed", 30, IT_POWERUP, PW_HASTE },	// item_haste
	{ "icons/invis", "Invisibility", 30, IT_POWERUP, PW_INVIS },	// item_invis
	{ "icons/regen", "Regeneration", 30, IT_POWERUP, PW_REGEN },	// item_regen
	{ "icons/flight", "Flight", 60, IT_POWERUP, PW_FLIGHT },	// item_flight
	{ "icons/iconf_red1", "Red Flag", 0, IT_TEAM, PW_REDFLAG },	// team_CTF_redflag
	{ "icons/iconf_blu1", "Blue Flag", 0, IT_TEAM, PW_BLUEFLAG },	// team_CTF_blueflag
};
