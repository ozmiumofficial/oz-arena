extends Node
## Headless self-test for the library. Run from the project folder:
##   godot --headless --path . res://tests/selftest.tscn
## Prints one line per check and "SELFTEST n/m passed"; exit code 1 on any failure.

var passed := 0
var failed := 0


func check(name: String, ok: bool, detail := "") -> void:
	if ok:
		passed += 1
		print("  PASS  ", name, ("  (" + detail + ")") if detail != "" else "")
	else:
		failed += 1
		print("  FAIL  ", name, ("  (" + detail + ")") if detail != "" else "")


func _ready() -> void:
	_run.call_deferred()


# ---------------------------------------------------------------- player cards (library/cards)

func _card_checks() -> void:
	var cat := Q3Cards.catalog()
	# the catalog: unique ids, art on disk, rules that point at real things, a level track with no gaps
	var bad: PackedStringArray = []
	var n := 0
	var by_level := {}
	var ach_ids := {}
	for a in Q3Cards.achievements():
		ach_ids[a["id"]] = true
	for kind in cat.get("parts", {}):
		var seen := {}
		for p in cat["parts"][kind]:
			n += 1
			if seen.has(p["id"]):
				bad.append("duplicate %s" % p["id"])
			seen[p["id"]] = true
			if p.has("art") and not ResourceLoader.exists(Q3.LIB + p["art"]):
				bad.append("no art " + p["art"])
			var rules: Array = p["unlock"].get("any", [p["unlock"]])
			for r in rules:
				if r.has("achievement") and not ach_ids.has(r["achievement"]):
					bad.append("%s: unknown achievement" % p["id"])
				if r.has("level"):
					by_level[int(r["level"])] = true
	var gap := 0
	var run := 0
	for l in range(1, 101):
		run = 0 if by_level.has(l) or l == 1 else run + 1
		gap = maxi(gap, run)
	var def := Q3Cards.default_card()
	var fresh := Q3Cards.new_profile()
	for slot in Q3Cards.SLOTS:
		var p := Q3Cards.part(Q3Cards.SLOTS[slot], String(def.get(slot, "")))
		if p.is_empty() or not Q3Cards.is_unlocked(p["unlock"], fresh):
			bad.append("default %s not free" % slot)
	check("cards: catalog, art and unlock rules", bad.is_empty() and n >= 350 and ach_ids.size() >= 80 and gap < 5,
			"%d parts, %d achievements, longest level gap %d%s" % [n, ach_ids.size(), gap, (", " + ", ".join(bad.slice(0, 4))) if bad.size() else ""])

	# rules: stats unlock parts, achievements come from stats, forged cards fall back
	var p0 := Q3Cards.new_profile()
	var forged := {"banner": "map_q3dm17", "emblem": "iconw_railgun", "title": "title_excellent", "frame": "frame_gold",
		"accent": "accent_gold", "badges": ["medal_excellent_100"], "stats": ["excellents", "nope"]}
	var shown := Q3Cards.enforce_unlocks(forged, p0)
	var locked_ok: bool = shown["banner"] == def["banner"] and shown["title"] == def["title"] and shown["frame"] == def["frame"] \
			and shown["badges"].is_empty() and shown["stats"] == ["excellents"]
	var p1 := {"level": 80, "stats": {"wins_map": {"q3dm17": 1}, "medal": {"excellent": 100},
		"frags_weapon": {"railgun": 100}}, "campaign": {"q3dm0": 5, "q3dm1": 2}}
	var shown1 := Q3Cards.enforce_unlocks(forged, p1)
	var earned_ok: bool = shown1["banner"] == "map_q3dm17" and shown1["emblem"] == "iconw_railgun" \
			and shown1["title"] == "title_excellent" and shown1["frame"] == "frame_gold" and shown1["accent"] == def["accent"] \
			and shown1["badges"] == ["medal_excellent_100"]
	var tourist := Q3Cards.achievement("tourist")
	var camp := Q3Cards.achievement("campaign_training")
	var lv := Q3Cards.level_for(Q3Cards.xp_to_next(1) + Q3Cards.xp_to_next(2) + 10)
	check("cards: stats unlock parts, achievements computed, forged cards fall back",
			locked_ok and earned_ok and Q3Cards.missing(tourist, p1).size() == int(tourist["target"]) - 1
			and Q3Cards.is_achieved(camp, p1) and lv["level"] == 3 and lv["xp"] == 10
			and Q3Cards.unlock_text(Q3Cards.part("titles", "title_rail_god")) == "ACHIEVEMENT: RAILGUN MASTER"
			and Q3Cards.stat_value("kd", {"stats": {"frags": 30, "deaths": 12}}) == "2.50",
			"fresh: %s, earned: %s" % [locked_ok, earned_ok])


# ---------------------------------------------------------------- main menu (scenes/, webui/)

func _menu_checks() -> void:
	Session.dry_run = true   # record scene changes instead of making them

	# the three top-level scenes and the splash
	var splash: Node = load(Session.SPLASH).instantiate()
	add_child(splash)
	var said: String = splash.text()
	check("splash: fan-project disclaimer", said.contains("fan project") and said.contains("belong to id Software")
			and said.contains("Quake III Arena is required to play"), "%d lines" % said.count("\n"))
	splash.free()
	var menu: Node = load(Session.MAIN_MENU).instantiate()
	add_child(menu)
	await get_tree().process_frame
	var stubs_ok := true
	for p in [Session.GAMEPLAY, Session.MAP_EDITOR]:
		var s: Node = load(p).instantiate() if ResourceLoader.exists(p) else null
		stubs_ok = stubs_ok and s != null
		if s:
			s.free()
	check("menu: main menu, showcase, logo and stub scenes build", menu.stage.subject != null and menu.logo != null and stubs_ok,
			"subject %s" % menu.stage.info.get("name", "?"))

	# the web menu's tree: every page has a component in the build, every action reaches Godot
	var tree: Dictionary = Q3.load_json("res://webui/menu.json")
	var js := ""
	for f in DirAccess.get_files_at("res://webui/_next/static/chunks"):
		if f.ends_with(".js"):
			js += FileAccess.get_file_as_string("res://webui/_next/static/chunks/" + f)
	var bad: PackedStringArray = []
	var bridge: Q3MenuBridge = menu.bridge
	var posts: Array = []
	bridge.post.connect(func(m): posts.append(m))
	var quit_asked := [false]
	bridge.quit_requested.connect(func(): quit_asked[0] = true)
	var n := 0
	var groups := ["main"]
	for it in tree.get("main", []):
		if it.has("submenu"):
			groups.append(it["submenu"])
			if not tree.has(it["submenu"]):
				bad.append(it["submenu"])
	for group in groups:
		for it in tree.get(group, []):
			n += 1
			if it.has("page"):
				var in_build: bool = js.contains('"%s"' % it["page"]) or js.contains("%s:" % it["page"])
				if not tree["pages"].has(it["page"]) or not in_build:
					bad.append(it["page"])
				# a page named like a submenu never opens (MenuApp takes a submenu's id as "no page"):
				# Profile > Player Card did nothing in v0.6.0-beta
				if it["page"] in groups:
					bad.append(it["page"] + " (also a submenu)")
			elif it.has("action"):
				Session.last_request = ""
				if it["action"] == "continue":
					var f := FileAccess.open(Session.LAST_SETUP, FileAccess.WRITE)
					f.store_string(JSON.stringify(Session.new_setup("ffa", "q3dm17").to_dict()))
					f.close()
				bridge.handle({"type": it["action"]})
				var want: String = {"map_editor": Session.MAP_EDITOR, "continue": Session.GAMEPLAY}.get(it["action"], "")
				if want != "" and Session.last_request != want:
					bad.append(it["action"])
	Session.last_request = ""
	bridge.handle({"type": "start", "setup": {"mode": "tdm", "map": "q3dm7", "bots": [{"name": "Sarge", "skill": 3, "team": "red"}]}})
	if Session.last_request != Session.GAMEPLAY:
		bad.append("start")
	bridge.handle({"type": "quit"})
	if not quit_asked[0]:
		bad.append("quit")
	check("menu: every item reachable", bad.is_empty() and n >= 13 and FileAccess.file_exists("res://webui/index.html"),
			"%d items%s" % [n, (", broken: " + ", ".join(bad)) if bad.size() else ""])

	posts.clear()
	bridge.handle({"type": "ready"})
	var init: Dictionary = posts[0] if posts.size() else {}
	check("menu: bridge sends the whole menu state", init.get("type") == "init" and init["schema"].size() == Settings.entries.size()
			and init["maps"].size() >= 30 and init["modes"].size() == 8 and init["characters"].size() == Q3.characters.size()
			and init["banner"]["glyphs"].size() >= 26,
			"%d settings, %d maps, %d modes, %d bots" % [init.get("schema", []).size(), init.get("maps", []).size(), init.get("modes", []).size(), init.get("bots", []).size()])

	# Quake 3's menu sounds, played the way q3_ui plays them
	var lengths: PackedStringArray = []
	var sounds_ok := true
	for k in UiSound.SOUNDS:
		var s: AudioStream = UiSound.streams.get(k)
		sounds_ok = sounds_ok and s != null and s.get_length() > 0.05
		lengths.append("%s %.2fs" % [k, s.get_length() if s else 0.0])
	UiSound.history.clear()
	for e in ["move", "out", "buzz", "change"]:
		bridge.handle({"type": "sound", "name": e})
	bridge.handle({"type": "sound", "name": "in"})
	await get_tree().process_frame
	await get_tree().process_frame
	check("menu: sounds load and fire (menu1-4, change)", sounds_ok and Array(UiSound.history) == ["move", "out", "buzz", "change", "in"],
			", ".join(lengths))

	# settings: every entry survives a trip to disk
	var keep := {}
	for e in Settings.entries:
		keep[e["key"]] = Settings.get_value(e["key"])
	var real_path := Settings.path
	Settings.path = "user://settings_selftest.cfg"
	var want := {}
	Settings.set_value("graphics/preset", "custom", false)
	for e in Settings.entries:
		var v: Variant = _other_value(e)
		if v == null:
			continue
		Settings.set_value(e["key"], v, false)
		want[e["key"]] = Settings.get_value(e["key"])
	Settings.save(Settings.path)
	for e in Settings.entries:
		Settings.set_value(e["key"], Settings.default_of(e["key"]), false)
	var loaded := Settings.load_from(Settings.path)
	var diff: PackedStringArray = []
	for k in want:
		if not Settings._same(Settings.get_value(k), want[k]):
			diff.append(k)
	check("settings: every setting round-trips to disk", loaded and diff.is_empty() and want.size() > 100,
			"%d settings%s" % [want.size(), (", differ: " + ", ".join(diff.slice(0, 5))) if diff.size() else ""])

	# binds: capture through the bridge (a key here, the page's bind_input), the moved key, conflict
	# detection, the controller slot, the InputMap
	for e in Settings.entries:
		Settings.set_value(e["key"], Settings.default_of(e["key"]), false)
	var had_w: bool = (Settings.get_value("controls/forward") as Array).has("key:W")
	bridge.handle({"type": "capture_bind", "key": "controls/jump", "slot": 1})
	var w := InputEventKey.new()
	w.physical_keycode = KEY_W
	w.pressed = true
	var used := bridge.capture_input(w)
	var moved := had_w and not (Settings.get_value("controls/forward") as Array).has("key:W")
	var mapped := false
	for ev in InputMap.action_get_events("q3_jump"):
		mapped = mapped or (ev is InputEventKey and ev.physical_keycode == KEY_W)
	bridge.handle({"type": "capture_bind", "key": "controls/jump", "slot": 1})
	bridge.handle({"type": "bind_input", "value": "key:NotAKey"})   # refused, the capture goes on
	var still := bridge.capture_key == "controls/jump"
	bridge.handle({"type": "bind_input", "value": "mouse:5"})
	var wheel: bool = Settings.get_value("controls/jump")[1] == "mouse:5"
	bridge.handle({"type": "capture_bind", "key": "controls/jump", "slot": 2})
	bridge.handle({"type": "bind_input", "value": "key:F"})          # the controller slot takes pads only
	var pad_only: bool = bridge.capture_key != "" and Settings.get_value("controls/jump")[2] != "key:F"
	var esc := InputEventKey.new()
	esc.physical_keycode = KEY_ESCAPE
	esc.pressed = true
	bridge.capture_input(esc)
	Settings.set_bind("controls/forward", 0, "key:Space")            # set directly: no move, a conflict
	var conflicts := Settings.bind_conflicts("controls/forward")
	check("settings: bind capture, moved key, conflicts, InputMap", used and moved and mapped and still and wheel
			and pad_only and bridge.capture_key == "" and conflicts.size() > 0,
			"jump = %s, forward = %s, conflicts %s" % [Settings.get_value("controls/jump"), Settings.get_value("controls/forward"), conflicts])
	for k in keep:
		Settings.set_value(k, keep[k], false)
	Settings.path = real_path
	DirAccess.remove_absolute(ProjectSettings.globalize_path("user://settings_selftest.cfg"))

	# match setup: game modes are data, setups validate and survive a round trip
	var good := Session.new_setup("ffa", "q3dm17")
	var ctf_wrong := Session.new_setup("ctf", "q3dm17")
	var duel_full := Session.new_setup("duel", "q3tourney2")
	duel_full.bots = [{"name": "Sarge", "skill": 3, "team": ""}, {"name": "Doom", "skill": 3, "team": ""}]
	var again := Q3MatchSetup.from_dict(JSON.parse_string(JSON.stringify(good.to_dict())))
	Session.last_request = ""
	Session.start_match(good)
	check("match setup: modes load, validation, round trip, hand-off", Session.mode_ids().size() == 8 and good.validate().is_empty()
			and ctf_wrong.validate().size() > 0 and duel_full.validate().size() > 0 and again.to_dict() == good.to_dict()
			and Session.last_request == Session.GAMEPLAY and Session.setup == good,
			"%s; ctf on q3dm17: %s" % [", ".join(Session.mode_ids()), ctf_wrong.validate()[0] if ctf_wrong.validate().size() else "accepted"])
	Session.setup = null
	DirAccess.remove_absolute(ProjectSettings.globalize_path(Session.LAST_SETUP))

	# single-player campaign: Quake 3's tiers, unlocks, arena setups
	var camp := Q3Campaign.new()
	camp.path = "user://campaign_selftest.json"
	camp.reset()
	# q3_ui: Training first, then each tier after the one before
	var t1_locked := not camp.tier_open(1)
	camp.record_result(camp.tiers[0]["arenas"][0]["map"], 3, true)
	var t1_open := camp.tier_open(1)
	var t2_locked := not camp.tier_open(2)
	for a in camp.tiers[1]["arenas"]:
		camp.record_result(a["map"], 3, true)
	var t2_opens := camp.tier_open(2)
	var first: Dictionary = camp.tiers[1]["arenas"][0]
	var arena := camp.setup_for(first["map"], 4)
	var final_setup := camp.setup_for(camp.tiers[camp.tiers.size() - 1]["arenas"][0]["map"], 5)
	var training := camp.setup_for(camp.tiers[0]["arenas"][0]["map"], 1)
	var arenas := 0
	for t in camp.tiers:
		arenas += t["arenas"].size()
	check("campaign: tiers, unlocks and arena setups", camp.tiers.size() == 8 and arenas == 26 and camp.tiers[0]["name"] == "Training"
			and camp.tiers[7]["name"] == "Final" and t1_locked and t1_open and t2_locked and t2_opens and arena.validate().is_empty()
			and arena.bots.size() > 0 and int(arena.bots[0]["skill"]) == 4 and final_setup.mode == "duel" and training.validate().is_empty(),
			"%d tiers, %d arenas; %s vs %s, final vs %s" % [camp.tiers.size(), arenas, first["name"], arena.bots.map(func(b): return b["name"]),
			final_setup.bots.map(func(b): return b["name"])])
	DirAccess.remove_absolute(ProjectSettings.globalize_path(camp.path))

	# cinematics: every movie converted and loadable, q3_ui's unlocks, the player builds
	var streams := 0
	for c in Session.cinematics:
		var vs: Resource = load(Q3CinematicPlayer.path_for(c["id"])) if Q3CinematicPlayer.exists(c["id"]) else null
		streams += int(vs is VideoStream)
	var real := Session.campaign
	Session.campaign = camp
	camp.reset()
	var locked_before := not Session.cinematic_unlocked("tier1") and not Session.cinematic_unlocked("end")
	var unlocks := camp.record_result(camp.tiers[0]["arenas"][0]["map"], 2, true)
	var tier1_open := Session.cinematic_unlocked("tier1")
	Session.campaign = real
	DirAccess.remove_absolute(ProjectSettings.globalize_path(camp.path))
	var player := Q3CinematicPlayer.new()
	player.queue = PackedStringArray(["idlogo"])
	add_child(player)
	await get_tree().process_frame
	var playing: bool = player.current == "idlogo"
	player.queue_free()
	check("cinematics: converted, unlock with the campaign, play", Session.cinematics.size() == 10 and streams == 10
			and Session.cinematic_unlocked("intro") and locked_before and unlocks == "tier1" and tier1_open and playing,
			"%d movies, %d streams; winning Training unlocks %s" % [Session.cinematics.size(), streams, unlocks])

	# characters: id Tech 3 draw modes from rig.json, and weapons held at tag_weapon
	var modes_ok := true
	var mode_info: PackedStringArray = []
	for m in ["bones", "hunter", "orbb", "razor", "slash", "uriel"]:
		if not Q3.characters.has(m):
			continue
		var c := Q3Character.new()
		c.model = m
		add_child(c)
		for stem in Q3.character_rig(m).get("material_modes", {}):
			var md: Dictionary = Q3.character_rig(m)["material_modes"][stem]
			for mi in c.find_children("*", "MeshInstance3D", true, false):
				for i in (mi as MeshInstance3D).mesh.get_surface_count():
					var mat := (mi as MeshInstance3D).mesh.surface_get_material(i) as BaseMaterial3D
					if mat and mat.resource_name == stem:
						var ok: bool = (md["blend"] != "add" or mat.blend_mode == BaseMaterial3D.BLEND_MODE_ADD) \
							and (md["blend"] != "mask" or mat.transparency == BaseMaterial3D.TRANSPARENCY_ALPHA_SCISSOR)
						modes_ok = modes_ok and ok
						mode_info.append("%s %s%s" % [stem, md["blend"], "" if ok else " NOT APPLIED"])
		c.free()
	var holder := Q3Character.new()
	holder.model = "sarge" if Q3.characters.has("sarge") else Q3.characters[0]
	add_child(holder)
	var gun := holder.attach("weapons2/railgun/railgun")
	await get_tree().process_frame
	var tag_x: Array = holder.rig.get("tags", {}).get("weapon", {}).get("x", [0, 0, 0])
	var rest := holder.skeleton.global_transform * holder.skeleton.get_bone_global_rest(holder.skeleton.find_bone("weapon"))
	var aim := (rest.basis * gun.basis).z.normalized() if gun else Vector3.ZERO
	var dot := aim.dot(Vector3(tag_x[0], tag_x[1], tag_x[2]).normalized())
	holder.free()
	check("characters: draw modes applied, weapons held at tag_weapon", modes_ok and mode_info.size() >= 6 and dot > 0.99,
			"%s; railgun along the tag: %.3f" % [", ".join(mode_info), dot])

	# Quake 3's fonts, remastered (make_ui.py)
	var prop := Q3Fonts.prop() as FontFile
	var banner := Q3Fonts.banner() as FontFile
	var missing := ""
	for ch in "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789":
		if prop == null or not prop.has_char(ch.unicode_at(0)):
			missing += ch
		if ch.unicode_at(0) >= 65 and (banner == null or not banner.has_char(ch.unicode_at(0))):
			missing += ch.to_lower()
	check("fonts: Q3 prop (signed distance) and banner fonts", prop != null and prop.multichannel_signed_distance_field and missing == "",
			"missing: " + missing if missing else "A-Z, 0-9")
	check("webview: godot_wry extension loads", ClassDB.class_exists("WebView"))

	# pickups and effect materials draw their Quake 3 shader stages (make_prop_shaders.py)
	var stage_mats := 0
	var want_stages: Dictionary = Q3.load_json("res://library/props/prop_shaders.json")
	var pickup_ids := ["powerups/instant/quad", "powerups/health/mega_sphere", "powerups/armor/armor_red", "powerups/instant/regen"]
	var missing_stages: PackedStringArray = []
	for id in pickup_ids:
		var pickup := Q3.instance_prop(id)
		var found := false
		if pickup:
			for mi in pickup.find_children("*", "MeshInstance3D", true, false):
				for i in (mi as MeshInstance3D).mesh.get_surface_count():
					var sm := (mi as MeshInstance3D).mesh.surface_get_material(i) as ShaderMaterial
					if sm and String(sm.shader.resource_path).contains("q3_stages"):
						found = true
						stage_mats += 1
			pickup.free()
		if not found:
			missing_stages.append(id)
	check("pickups: Quake 3 shader stages (chrome, energy, electricity)", want_stages.size() >= 40 and missing_stages.is_empty(),
			"%d stage materials listed, %d on %d pickups%s" % [want_stages.size(), stage_mats, pickup_ids.size(),
			(", missing on: " + ", ".join(missing_stages)) if missing_stages.size() else ""])
	menu.free()
	Session.dry_run = false


# ---------------------------------------------------------------- maps (tools/build_maps.py, Q3MapBuilder)

const STOCK_MAPS := 35   ## q3dm0-19, q3tourney1-6 + 6_ctf, q3ctf1-4, the four pro maps


func _map_checks() -> void:
	var built := 0
	var baked := 0
	var unresolved := 0
	for d in DirAccess.get_directories_at("res://library/maps"):
		var base := "res://library/maps/%s/%s" % [d, d]
		if ResourceLoader.exists(base + ".tscn"):
			built += 1
			var txt := FileAccess.get_file_as_string(base + ".tscn")
			if txt.contains(".lmbake"):
				baked += 1
		if not FileAccess.file_exists(base + ".json"):   # portal/ holds shared portal assets, not a map
			continue
		var info: Variant = JSON.parse_string(FileAccess.get_file_as_string(base + ".json"))
		if info is Dictionary:
			unresolved += info["report"]["missing"].size()
	check("maps: every stock map imported, built and baked", built == STOCK_MAPS and baked == STOCK_MAPS and unresolved == 0,
			"%d built, %d baked, %d shaders without a material" % [built, baked, unresolved])
	# one map in depth: The Longest Yard
	var ps := load("res://library/maps/q3dm17/q3dm17.tscn") as PackedScene
	if ps == null:
		check("maps: q3dm17 loads with materials, lights, collision, entities", false, "no q3dm17.tscn")
		return
	var map := ps.instantiate()
	var tris := 0
	var fallback := 0
	for mi in map.find_children("*", "MeshInstance3D", true, false):
		for i in mi.mesh.get_surface_count():
			tris += mi.mesh.surface_get_array_index_len(i) / 3
			var m: Material = mi.mesh.surface_get_material(i)
			if m == null or m.resource_name == "missing":
				fallback += 1
	var hulls: int = map.get_node("Collision").get_child_count()
	var spawns: int = map.entities_of("info_player_deathmatch").size()
	var lm := map.get_node("LightmapGI") as LightmapGI
	check("maps: q3dm17 loads with materials, lights, collision, entities",
			tris == 9688 and fallback == 0 and hulls > 400 and spawns > 0 and lm.light_data != null and map.model_node(1) == null,
			"%d triangles, %d fallback materials, %d hulls, %d spawns, %d lights, lightmap %s" % [tris, fallback, hulls, spawns,
			map.get_node("Lights").get_child_count(), "baked" if lm.light_data else "missing"])
	map.free()
	# mirrors and portal views (Q3Portal): q3dm0 has a mirror and a teleporter looking at its exit
	var dm0: Variant = JSON.parse_string(FileAccess.get_file_as_string("res://library/maps/q3dm0/q3dm0.json"))
	var views: Array = dm0.get("portals", []) if dm0 is Dictionary else []
	var mirrors := views.filter(func(v: Dictionary) -> bool: return v["mirror"]).size()
	var cams := views.filter(func(v: Dictionary) -> bool: return not v["mirror"] and v["camera"] is Dictionary).size()
	var tex_ok := views.all(func(v: Dictionary) -> bool:
		return (v["textures"] as Array).all(func(t: String) -> bool: return ResourceLoader.exists("res://library/maps/portal/%s.png" % t)))
	check("maps: q3dm0's mirror and portal view", mirrors == 1 and cams == 1 and tex_ok,
			"%d mirror, %d camera view, stage images %s" % [mirrors, cams, "found" if tex_ok else "missing"])


# ---------------------------------------------------------------- q3core (id's game code, tools/q3core)

func _core_checks(q3: Node) -> void:
	# Called through ClassDB so the test still parses when the extension isn't built.
	if not ClassDB.class_exists("Q3Core"):
		check("q3core: extension loads", false, "Q3Core class missing; run tools/build_q3core.py")
		return
	var items: PackedStringArray = ClassDB.class_call_static("Q3Core", "item_classnames")
	var missing: PackedStringArray = []
	for cn in q3.entities:
		if not items.has(cn):
			missing.append(cn)
	check("q3core: extension loads, bg_itemlist matches the catalog",
			missing.is_empty() and int(ClassDB.class_call_static("Q3Core", "item_quantity", "item_health_mega")) == 100,
			"%s, %d items%s" % [ClassDB.class_call_static("Q3Core", "source_version"), items.size(),
			(", not in game code: " + ", ".join(missing)) if missing.size() else ""])
	# id's server, game module and botlib play a match (tests/q3core_match.gd; needs the player's paks)
	var match_test: Script = load("res://tests/q3core_match.gd")
	var r: Dictionary = match_test.run_match("q3dm17", 20, 3, false) if match_test else {"ok": false, "report": "q3core_match.gd didn't load"}
	check("q3core: q3dm17 with 3 of id's bots for 20 s: bots move and fight, snapshots arrive", r["ok"], String(r["report"]).replace("\n", "; "))
	# online play's client and server over packets only, on a bad link (tests/net_loopback_test.gd, MULTIPLAYER_PLAN.md)
	var net_test: Script = load("res://tests/net_loopback_test.gd")
	var n: Dictionary = net_test.run("q3dm17", 8, 2, 40, 0.05) if net_test else {"ok": false, "report": "net_loopback_test.gd didn't load"}
	check("net: client and server over an 80 ms, 5% loss link for 8 s: snapshots exact, prediction matches, rcon answers, the slot frees",
			n["ok"], String(n["report"]).replace("\n", "; "))
	# last: an ERR_DROP shuts the running server down, as it does in Quake 3
	check("q3core: Com_Error returns to Godot", ClassDB.class_call_static("Q3Core", "error_recovers"),
			"the 'HoldableItem not found' error above is this test")


# ---------------------------------------------------------------- map editor geometry (Q3MapGeo, Q3MapDoc)

func _map_editor_checks() -> void:
	var t: Script = load("res://tests/map_geo_test.gd")
	for c in t.run_checks(self):
		check(c[0], c[1], c[2])


## A valid value different from the entry's default, or null to skip.
func _other_value(e: Dictionary) -> Variant:
	var d: Variant = e["default"]
	match e["type"]:
		"bool": return not d
		"int": return int(e["max"]) if int(d) != int(e["max"]) else int(e["min"])
		"float": return float(e["max"]) if not is_equal_approx(float(d), float(e["max"])) else float(e["min"])
		"color": return Color(0.1, 0.2, 0.3)
		"string": return "Selftest"
		"bind": return ["key:F5", "mouse:9", "joy:3"]
		"enum":
			if e["key"] == "graphics/preset":
				return null
			for o in Settings.options(e["key"]):
				if not Settings._same(o[0], d):
					return o[0]
	return null


func _run() -> void:
	var q3: Node = get_node("/root/Q3")
	check("catalog: characters", q3.characters.size() >= 1, "%d" % q3.characters.size())
	check("catalog: props", q3.props.size() >= 1, "%d" % q3.props.size())
	check("catalog: items", q3.entities.size() >= 30, "%d" % q3.entities.size())
	check("catalog: map entity classes", q3.map_entities.size() >= 40, "%d" % q3.map_entities.size())
	check("catalog: fx", q3.fx.has("explosions") and q3.fx_textures.size() >= 20, "%d textures" % q3.fx_textures.size())

	# import flags from material names
	var rl: Node3D = q3.instance_prop("weapons2/rocketl/rocketl")
	add_child(rl)
	var flash := rl.find_child("flash", true, false) as Node3D
	check("import: weapon assembled with flash on tag_flash", flash != null)
	check("import: flash starts hidden", flash != null and not flash.visible)
	var modes := {}
	for id in q3.props:
		var n: Node3D = q3.instance_prop(id)
		for m in n.find_children("*", "MeshInstance3D", true, false):
			var mesh: Mesh = (m as MeshInstance3D).mesh
			for i in mesh.get_surface_count():
				var mat := mesh.surface_get_material(i) as BaseMaterial3D
				if mat == null:
					continue
				var nm := mat.resource_name
				if "__add" in nm:
					modes["add"] = modes.get("add", 0) + int(mat.blend_mode == BaseMaterial3D.BLEND_MODE_ADD)
				elif "__mask" in nm:
					modes["mask"] = modes.get("mask", 0) + int(mat.transparency == BaseMaterial3D.TRANSPARENCY_ALPHA_SCISSOR)
				if "__2s" in nm:
					modes["2s"] = modes.get("2s", 0) + int(mat.cull_mode == BaseMaterial3D.CULL_DISABLED)
		n.free()
	check("import: additive materials are additive", modes.get("add", 0) > 0, str(modes))
	check("import: cut-out materials use alpha scissor", modes.get("mask", 0) > 0)
	check("import: two-sided materials have culling off", modes.get("2s", 0) > 0)

	# every character: model, AnimationPlayer, 25 clips, legs/torso tree, ragdoll bones
	var ok_chars := 0
	var bad: PackedStringArray = []
	for m in q3.characters:
		var c := Q3Character.new()
		c.model = m
		add_child(c)
		var good: bool = c.player != null and c.skeleton != null and c.tree != null and c.clips("LEGS").size() >= 12 \
			and c.clips("TORSO").size() >= 7 and c.clips("BOTH").size() == 6
		if good:
			c.play_legs("LEGS_RUN")
			c.play_torso("TORSO_ATTACK")
			good = c.legs_clip == "LEGS_RUN" and c.torso_clip == "TORSO_ATTACK"
		if good:
			ok_chars += 1
		else:
			bad.append(m)
		c.free()
	check("characters: all load with layered legs/torso", bad.is_empty(), "%d ok%s" % [ok_chars, (", bad: " + ", ".join(bad)) if bad.size() else ""])

	var keel := Q3Character.new()
	keel.model = "keel" if q3.characters.has("keel") else q3.characters[0]
	add_child(keel)
	keel.ragdoll(Vector3(0, 5, 0))
	await get_tree().process_frame
	await get_tree().physics_frame
	var bones := 0
	for n in keel.find_children("*", "PhysicalBone3D", true, false):
		bones += 1
	check("ragdoll: physical bones built and simulating", keel.ragdolled and bones >= 15, "%d bones" % bones)

	# every catalog item spawns
	var items_ok := 0
	for cls in q3.entities:
		var p := Q3Pickup.new()
		p.classname = cls
		add_child(p)
		if p.get_child_count() > 0 and p.get_child(0).get_child_count() > 0:
			items_ok += 1
		p.free()
	check("items: every catalog item spawns its models", items_ok == q3.entities.size(), "%d/%d" % [items_ok, q3.entities.size()])

	# every effect builds
	var host := Node3D.new()
	add_child(host)
	var kinds: Array = q3.fx["explosions"].keys()
	for k in kinds:
		Q3FX.explosion(host, k, Vector3.ZERO, Vector3.UP)
	Q3FX.rail(host, Vector3(-5, 1, 0), Vector3(5, 1, 0))
	Q3FX.lightning(host, Vector3(-5, 1, 0), Vector3(5, 1, 0), 0.5)
	Q3FX.rocket(host, Vector3(-5, 1, 0), Vector3(5, 1, 0))
	Q3FX.plasma(host, Vector3(-5, 1, 0), Vector3(5, 1, 0))
	Q3FX.muzzle_flash(host, "railgun", Vector3.ZERO)
	Q3FX.gibs(host, Vector3.ZERO)
	Q3FX.teleport(host, Vector3.ZERO)
	var shell := Q3FX.shell_material("quad")
	for i in 30:
		await get_tree().process_frame
	check("fx: every effect builds and runs 30 frames", host.get_child_count() > 0, "%d explosion kinds + rail, lightning, rocket, plasma, muzzle, gibs, teleport" % kinds.size())
	check("fx: powerup shell material", shell != null and shell.shader != null)

	# world materials (only once tools/build_world.py + make_godot_world.py have run)
	if FileAccess.file_exists("res://library/world/world_materials.json"):
		var wm: Dictionary = q3.load_json("res://library/world/world_materials.json").get("materials", {})
		var loaded := {}
		var bad_w: PackedStringArray = []
		for n in wm:
			var e: Dictionary = wm[n]
			if not e.has("res"):
				continue
			var r: Resource = load(e["res"])
			if r == null:
				bad_w.append(n)
			else:
				loaded[r.get_class()] = loaded.get(r.get_class(), 0) + 1
		check("world: every material, sky and fog resource loads", bad_w.is_empty() and loaded.size() > 0,
				"%s%s" % [str(loaded), (", failed: " + ", ".join(bad_w.slice(0, 5))) if bad_w.size() else ""])
		var wkinds := {}
		for n in wm:
			wkinds[wm[n]["kind"]] = wkinds.get(wm[n]["kind"], 0) + 1
		check("world: catalog covers every shader and image", wm.size() > 1500, str(wkinds))

	_card_checks()
	await _menu_checks()
	_map_checks()
	_map_editor_checks()
	_core_checks(q3)

	print("SELFTEST %d/%d passed" % [passed, passed + failed])
	get_tree().quit(1 if failed > 0 else 0)
