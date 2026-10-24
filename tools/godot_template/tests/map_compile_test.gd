extends Node
## Map editor compile test (MAP_EDITOR_PLAN.md E5), headless: a small arena built as the editor
## would (a sealed room, a curve, a door, a spawn, a light, a jump pad), written by Q3MapExport,
## compiled by id's q3map (bsp, vis, light) and bspc (aas), read back from the .bsp, loaded in q3core
## with a bot, and a leaking room compiled to show the leak.
##   godot --headless --path . res://tests/map_compile_test.tscn
## Prints one line per check and "MAP COMPILE n/m passed"; exit code 1 on any failure.

const NAME := "ed_compiletest"
const LEAK := "ed_compileleak"
const STOCK := "ed_compilestock"

var passed := 0
var failed := 0


func check(name: String, ok: bool, detail := "") -> void:
	if ok:
		passed += 1
	else:
		failed += 1
	print("  %s  map compile: %s%s" % ["PASS" if ok else "FAIL", name, ("  (" + detail + ")") if detail != "" else ""])


func _ready() -> void:
	_run.call_deferred()


## A 768 x 768 x 512 room with 16-unit walls, a bevel curve, a func_door, a spawn, a light and a
## jump pad to a ledge.
static func arena(sealed := true) -> Q3MapDoc:
	var d := Q3MapDoc.new()
	d.info["name"] = "compiletest"
	var wall := "textures/gothic_block/blocks15"
	var floor_mat := "textures/base_floor/clangdark"
	d.add(Q3MapDoc.box_brush(Vector3(-384, -384, -16), Vector3(384, 384, 0), floor_mat))
	if sealed:
		d.add(Q3MapDoc.box_brush(Vector3(-384, -384, 512), Vector3(384, 384, 528), wall))
	d.add(Q3MapDoc.box_brush(Vector3(-400, -384, -16), Vector3(-384, 384, 528), wall))
	d.add(Q3MapDoc.box_brush(Vector3(384, -384, -16), Vector3(400, 384, 528), wall))
	d.add(Q3MapDoc.box_brush(Vector3(-400, -400, -16), Vector3(400, -384, 528), wall))
	d.add(Q3MapDoc.box_brush(Vector3(-400, 384, -16), Vector3(400, 400, 528), wall))
	# a ledge to jump to, a turned pillar (non-axial planes), a locked-texture block
	d.add(Q3MapDoc.box_brush(Vector3(192, 192, 0), Vector3(384, 384, 128), wall))
	var pillar := Q3MapDoc.box_brush(Vector3(-64, 160, 0), Vector3(0, 224, 192), wall)
	d.add(Q3MapDoc.transform_brush(pillar, Q3MapDoc.yaw_rotation(30, Vector3(-32, 192, 0))))
	var locked := Q3MapDoc.box_brush(Vector3(-256, -256, 0), Vector3(-192, -192, 64), wall)
	for i in locked["faces"].size():
		var f: Dictionary = locked["faces"][i]
		f["uv"] = Q3MapDoc.face_uv(f, Q3MapDoc.plane_of(locked, i))
		f.erase("tex")
		f["lock"] = true
	d.add(Q3MapDoc.transform_brush(locked, Q3MapDoc.yaw_rotation(20, Vector3(-224, -224, 0))))
	# a slab tilted 0.1 degree about X: its top is nearly flat, where q3map snaps the normal
	var c := cos(deg_to_rad(0.1))
	var s := sin(deg_to_rad(0.1))
	var slab := Q3MapDoc.box_brush(Vector3(-320, -320, 0), Vector3(-256, -256, 16), floor_mat)
	d.add(Q3MapDoc.transform_brush(slab, PackedFloat64Array([1, 0, 0, 0, c, -s, 0, s, c, 0, 0, 0])))
	d.add(Q3PatchFactory.make("bevel", Vector3(-320, 256, 0), 64.0, 128.0, wall))
	# a door
	var door := Q3MapEntities.brush_entity("func_door", {"angle": "90"})
	var did := d.add(door)
	var db := Q3MapDoc.box_brush(Vector3(-160, -40, 0), Vector3(-144, 40, 128), "textures/base_trim/dark_tin2")
	db["entity"] = did
	d.add(db)
	# a mover with an origin: q3map maps its faces from the origin (AdjustBrushesForOrigin)
	var bob := d.add(Q3MapEntities.brush_entity("func_bobbing", {"origin": "200 -200 64", "height": "16"}))
	var bb := Q3MapDoc.box_brush(Vector3(176, -224, 40), Vector3(224, -176, 88), "textures/base_trim/dark_tin2")
	bb["entity"] = bob
	for f in bb["faces"]:
		f["tex"] = [5.0, 9.0, 30.0, 0.5, 0.5]
	d.add(bb)
	d.add(Q3MapEntities.point("info_player_deathmatch", Vector3(0, -256, 24), {"angle": "90"}))
	d.add(Q3MapEntities.point("info_player_deathmatch", Vector3(-256, 64, 24), {"angle": "0"}))
	d.add(Q3MapEntities.point("light", Vector3(0, 0, 200), {"light": "400"}))
	d.add(Q3MapEntities.point("weapon_rocketlauncher", Vector3(0, 0, 16)))
	# jump pad at (0, 128) to the ledge
	var g := Q3MapEntities.gravity(d)
	var push := Q3MapEntities.brush_entity("trigger_push", {"target": "pad1"})
	var pid := d.add(push)
	var tb := Q3MapDoc.box_brush(Vector3(-32, 96, 0), Vector3(32, 160, 36), "textures/common/trigger")
	tb["entity"] = pid
	d.add(tb)
	d.add(Q3MapEntities.point("target_position", Q3MapEntities.pad_apex(Vector3(0, 128, 0), Vector3(288, 288, 128), g).round(), {"targetname": "pad1"}))
	d.worldspawn = {"message": "Compile test", "ambient": "8"}
	return d


func _run() -> void:
	var t0 := Time.get_ticks_msec()
	var d := arena()
	var ex := Q3MapExport.to_map(d)
	var text: String = ex["text"]
	check("export: brushDef brushes, a patchDef2, entities in order, the door, mover and pad as brush models *1 to *3",
			text.count("brushDef") == 13 and text.count("patchDef2") == 1 and text.begins_with("//") and ex["models"].size() == 3
			and ex["warnings"].is_empty(), "%d brushDef, models %s, %s" % [text.count("brushDef"), ex["models"], ex["warnings"]])

	var c := Q3MapCompiler.new()
	add_child(c)
	var kit := Q3MapCompiler.ensure_kit()
	check("kit: shader scripts unpacked from the player's paks, with a shader list",
			kit == "" and FileAccess.file_exists(Q3MapCompiler.ROOT.path_join("scripts/shaderlist.txt"))
			and FileAccess.file_exists(Q3MapCompiler.ROOT.path_join("scripts/common.shader")), kit)

	c.compile(NAME, text, ["bsp", "vis", "light", "aas"])
	var r: Dictionary = await c.finished
	check("q3map bsp, vis and light, then bspc aas: no leak, the files land where q3core looks",
			r["ok"] and not r["leaked"] and FileAccess.file_exists(r["bsp"]) and FileAccess.file_exists(r["aas"]),
			"%s (%.1f s)%s" % [r["error"], r["seconds"], "" if r["ok"] else "\n" + String(r["log"]).right(1500)])
	if not r["ok"]:
		_finish(t0)
		return

	# the .bsp itself
	var bsp := _read_bsp(r["bsp"])
	check("the .bsp: version 46 with lightmaps, vis data and the entities", bsp.get("version", 0) == 46
			and bsp["lightmaps"] > 0 and bsp["vis"] > 0 and bsp["entities"].contains("info_player_deathmatch"),
			"version %s, %d lightmaps, %d vis bytes" % [bsp.get("version"), bsp.get("lightmaps", 0), bsp.get("vis", 0)])
	var models_ok := true
	for e in ex["models"]:
		var cn: String = d.get_obj(e)["classname"]
		if not bsp["entities"].contains("\"model\" \"*%d\"\n\"classname\" \"%s\"" % [ex["models"][e], cn]) and \
				not _entity_has(bsp["entities"], cn, "*%d" % ex["models"][e]):
			models_ok = false
	check("q3map numbered the brush models as the export said (door *1, mover *2, pad *3)", models_ok, str(ex["models"]))
	# texture coordinates: every compiled floor and wall vertex matches the editor's own mapping
	var worst := _st_error(d, bsp, ex["models"])
	check("texture coordinates in the .bsp match the editor's (world-projected, turned, texture-locked faces, a mover mapped from its origin)",
			worst[0] < 0.002 and worst[1] > 50 and worst[3] >= 24, "worst %.5f repeats over %d vertices (%d in brush models): %s" % [worst[0], worst[1], worst[3], worst[2]])

	# q3core plays it
	var err := Q3Host.start()
	Q3Server.set_cvar("g_gametype", "0")
	Q3Server.set_cvar("fraglimit", "0")
	Q3Server.set_cvar("timelimit", "0")
	Q3Server.set_cvar("g_doWarmup", "0")
	var loaded := err == "" and Q3Server.exec_now("map " + NAME) == 0 and Q3Server.running()
	var me := -1
	var spawned := false
	var bot_moved := false
	if loaded:
		Q3Server.exec("addbot sarge 3")
		Q3Server.frame(Q3Host.FRAME_MSEC)
		me = Q3Server.connect_local(Q3Host.userinfo({"name": "Tester", "model": "sarge"}))
		var bot := -1
		var bot_start := Vector3.ZERO
		for i in 500:
			Q3Server.frame(Q3Host.FRAME_MSEC)
			if bot < 0:
				for cl in 8:
					if cl != me and Q3Server.client_state(cl) == 4:
						bot = cl
						bot_start = Q3Server.player_state(cl)["origin"]
			elif (Q3Server.player_state(bot)["origin"] as Vector3).distance_to(bot_start) > 64:
				bot_moved = true
		var ps := Q3Server.player_state(me) if me >= 0 else {}
		var o: Vector3 = ps.get("origin", Vector3.INF)
		spawned = o.z > 0 and o.z < 64 and absf(o.x) < 384 and absf(o.y) < 384
		Q3Server.exec_now("killserver")
	check("q3core loads the compiled map, the player spawns on a spawn point, and a bot finds its way around (the .aas)",
			loaded and spawned and bot_moved, "loaded %s (%s), spawned %s, bot moved %s" % [loaded, err if err != "" else Q3Server.error(), spawned, bot_moved])

	# a room without a ceiling leaks: the BSP is still written, vis and light are skipped
	c.compile(LEAK, Q3MapExport.to_map(arena(false))["text"], ["bsp", "vis", "light"])
	var lr: Dictionary = await c.finished
	check("a leaking map: reported with q3map's leak line, still compiled (vis and light skipped)",
			lr["leaked"] and lr["leak"].size() >= 2 and lr["ok"] and String(lr["log"]).contains("vis skipped"),
			"leaked %s, %d leak points, ok %s" % [lr["leaked"], lr["leak"].size(), lr["ok"]])

	# a stock map through the whole loop (E6): imported from its .bsp, exported, compiled by q3map
	# again, and the new .bsp imported: the same brushes, curves and entities, every surface aligned
	if Q3BspImport.maps_in_paks().has("q3dm17"):
		var orig: Dictionary = Q3BspImport.import_bsp(Q3BspImport.read_from_paks("q3dm17"), "q3dm17")
		var od: Q3MapDoc = orig["doc"]
		var sx := Q3MapExport.to_map(od)
		c.compile(STOCK, sx["text"], ["bsp"])
		var sr: Dictionary = await c.finished
		var again: Dictionary = Q3BspImport.import_bsp(FileAccess.get_file_as_bytes(sr["bsp"]), STOCK) if sr["ok"] else {"report": {}}
		var ar: Dictionary = again["report"]
		check("q3dm17 imported, exported and compiled again: the same brushes, curves and entities",
				sr["ok"] and not sr["leaked"] and ar.get("brushes", -1) == orig["report"]["brushes"] and ar.get("patches", -1) == orig["report"]["patches"]
				and ar.get("entities", -1) == orig["report"]["entities"],
				"%s (%.1f s): brushes %s/%s, detail %s/%s, curves %s/%s, entities %s/%s" % [sr["error"], sr["seconds"], ar.get("brushes"),
				orig["report"]["brushes"], ar.get("detail"), orig["report"]["detail"], ar.get("patches"), orig["report"]["patches"],
				ar.get("entities"), orig["report"]["entities"]])
		# detail kept: q3map's tree is about the size id's was (structural trim would multiply it)
		var tree_a := _tree_size(Q3BspImport.read_from_paks("q3dm17"))
		var tree_b := _tree_size(FileAccess.get_file_as_bytes(sr["bsp"])) if sr["ok"] else Vector2i(1 << 30, 1 << 30)
		check("q3dm17 recompiled: the tree is as small as id's (detail stayed detail)",
				tree_b.x <= tree_a.x * 1.1 and tree_b.y <= tree_a.y * 1.1, "nodes %d vs id's %d, leaves %d vs %d" % [tree_b.x, tree_a.x, tree_b.y, tree_a.y])
		# the recompiled surfaces carry the mapping the import gave the editor, brush models included
		var sw := _st_error(od, _read_bsp(sr["bsp"]), sx["models"]) if sr["ok"] else [INF, 0, "", 0]
		check("q3dm17 recompiled: every drawn vertex's texture coordinates are the imported map's",
				sw[0] < 0.002 and sw[1] > 3000, "worst %.5f repeats over %d vertices (%d in brush models): %s" % [sw[0], sw[1], sw[3], sw[2]])

	await _publish_check()

	# play-test drawing: the map node Gameplay gets
	var pm := Q3PlayMap.build(d, ex["models"])
	add_child(pm)
	await get_tree().process_frame
	var world := pm.get_node("Geometry/world") as Q3MapMesher
	world.flush()
	var door_node := pm.model_node(int(ex["models"][ex["models"].keys()[0]]))
	var markers := pm.entities_of("info_player_deathmatch")
	var col := pm.get_node("Collision")
	check("the play-test map: world and brush-model nodes, spawn markers, the map's light, collision hulls",
			world.cell_count() > 0 and door_node != null and markers.size() == 2 and pm.get_node("Lights").get_child_count() == 1
			and col.get_child_count() >= 10, "%d cells, door %s, %d markers, %d hulls" % [world.cell_count(), door_node, markers.size(), col.get_child_count()])
	pm.queue_free()

	for n in [NAME, LEAK, STOCK]:
		for ext in [".bsp", ".aas"]:
			var p := Q3MapCompiler.output_dir.path_join(n + ext)
			if FileAccess.file_exists(p):
				DirAccess.remove_absolute(ProjectSettings.globalize_path(p))
	_finish(t0)


## Publish (E7) in the real editor: the arena saved, compiled in full as pub_<name>, listed for match
## setup, accepted by its validation, and drawn from its saved document as Gameplay draws it.
func _publish_check() -> void:
	var maps_dir := Q3MapEditor.maps_dir
	var recent := Q3MapEditor.recent
	var pub_path := Q3MapPublish.path
	Q3MapEditor.maps_dir = "user://map_compile_test/maps"
	Q3MapEditor.recent = "user://map_compile_test/recent.json"
	Q3MapPublish.path = "user://map_compile_test/published.json"
	var ed: Q3MapEditor = load("res://scenes/map_editor/map_editor.tscn").instantiate()
	add_child(ed)
	await get_tree().process_frame
	ed._set_doc(arena(), "")
	ed.save_map("pubtest")
	ed.publish()
	var r: Dictionary = await ed.compiler.finished
	await get_tree().process_frame
	var entry := Q3MapPublish.find("pub_pubtest")
	var listed := Session.map_info("pub_pubtest")
	var s := Session.new_setup("ffa", "pub_pubtest")
	var errs := s.validate()
	var d := Q3MapDoc.new()
	var load_err := d.load_file(String(entry.get("doc", "")))
	var models := {}
	for e in entry.get("models", {}):
		models[int(e)] = int(entry["models"][e])
	var pm := Q3PlayMap.build(d, models) if load_err == "" else null
	check("Publish: compiled in full as pub_pubtest (vis, light, bots), listed for match setup, valid for a free-for-all, drawn from its document",
			r.get("ok", false) and r.get("aas", "") != "" and entry.size() > 0 and listed.get("special", "") == "custom"
			and Array(listed.get("types", [])).has("ffa") and errs.is_empty() and pm != null and pm.model_node(1) != null,
			"%s, stages %s, entry %s, errors %s" % [r.get("error", ""), r.get("stages", []), entry.keys(), errs])
	if pm:
		pm.free()
	ed.queue_free()
	Q3MapPublish.remove("pub_pubtest")
	Session.refresh_custom_maps()
	for ext in [".bsp", ".aas", Q3MapDoc.EXTENSION]:
		var p := Q3MapCompiler.output_dir.path_join("pub_pubtest" + ext)
		if FileAccess.file_exists(p):
			DirAccess.remove_absolute(ProjectSettings.globalize_path(p))
	for f in ["maps/pubtest.q3map.json", "maps/pubtest.png", "maps/_autosave.q3map.json", "recent.json", "published.json"]:
		DirAccess.remove_absolute(ProjectSettings.globalize_path("user://map_compile_test/" + f))
	Q3MapEditor.maps_dir = maps_dir
	Q3MapEditor.recent = recent
	Q3MapPublish.path = pub_path


func _finish(t0: int) -> void:
	print("MAP COMPILE %d/%d passed (%.1f s)" % [passed, passed + failed, (Time.get_ticks_msec() - t0) / 1000.0])
	get_tree().quit(1 if failed else 0)


static func _entity_has(ents: String, cn: String, model: String) -> bool:
	for block in ents.split("}"):
		if block.contains("\"classname\" \"%s\"" % cn) and block.contains("\"model\" \"%s\"" % model):
			return true
	return false


## A .bsp's node and leaf counts.
static func _tree_size(b: PackedByteArray) -> Vector2i:
	if b.size() < 8 + 17 * 8:
		return Vector2i(1 << 30, 1 << 30)
	return Vector2i(b.decode_s32(12 + 3 * 8) / 36, b.decode_s32(12 + 4 * 8) / 48)


## The parts of a Quake 3 .bsp this test reads: entities, shaders, draw verts, surfaces, models.
static func _read_bsp(path: String) -> Dictionary:
	var b := FileAccess.get_file_as_bytes(path)
	if b.size() < 8 + 17 * 8 or b.slice(0, 4).get_string_from_ascii() != "IBSP":
		return {}
	var lump := func(i: int) -> PackedByteArray:
		var off := b.decode_s32(8 + i * 8)
		var size := b.decode_s32(12 + i * 8)
		return b.slice(off, off + size)
	var shaders := []
	var sb: PackedByteArray = lump.call(1)
	for i in sb.size() / 72:
		shaders.append(sb.slice(i * 72, i * 72 + 64).get_string_from_ascii())
	return {"version": b.decode_s32(4), "entities": (lump.call(0) as PackedByteArray).get_string_from_ascii(),
			"shaders": shaders, "verts": lump.call(10), "surfaces": lump.call(13), "models": (lump.call(7) as PackedByteArray).to_int32_array(),
			"lightmaps": (lump.call(14) as PackedByteArray).size() / (128 * 128 * 3), "vis": (lump.call(16) as PackedByteArray).size()}


## [worst |st - editor st| in repeats, apart from whole repeats, vertices compared, of those in brush
## models] over the planar surfaces of the world and of the brush models (models: entity id -> model
## number, from the export). Each compiled vertex is matched to a face of its own model whose plane it
## lies on and whose material it has; a brush model's vertices are relative to its entity's origin.
static func _st_error(d: Q3MapDoc, bsp: Dictionary, models: Dictionary) -> Array:
	var model_of := {0: 0}   # entity id -> model
	for e in models:
		model_of[e] = models[e]
	var owned := Q3MapEntities.owned(d)
	var origin_of := {0: Vector3.ZERO}   # model -> origin
	for e in models:
		origin_of[models[e]] = Q3MapExport.entity_origin(d.get_obj(e)["keys"], owned.get(e, []).map(func(p): return d.get_obj(p)))
	var faces := {}   # model -> [[plane, uv, size, material]]
	for id in d.objects:
		var o: Dictionary = d.objects[id]
		var e := int(o.get("entity", 0))
		if o["type"] != "brush" or not model_of.has(e):
			continue
		var m: int = model_of[e]
		if not faces.has(m):
			faces[m] = []
		for i in o["faces"].size():
			var f: Dictionary = o["faces"][i]
			var pl := Q3MapDoc.plane_of(o, i)
			faces[m].append([pl, Q3MapDoc.face_uv(f, pl), Q3MapMaterials.size(f["material"]), String(f["material"])])
	var surf_model := {}
	var ml: PackedInt32Array = bsp["models"]
	for m in ml.size() / 10:
		for s in range(ml[m * 10 + 6], ml[m * 10 + 6] + ml[m * 10 + 7]):
			surf_model[s] = m
	var verts: PackedByteArray = bsp["verts"]
	var surfs: PackedByteArray = bsp["surfaces"]
	var worst := 0.0
	var n := 0
	var in_models := 0
	var where := ""
	for s in surfs.size() / 104:
		var o := s * 104
		var shader: String = bsp["shaders"][surfs.decode_s32(o)]
		if surfs.decode_s32(o + 8) != 1:   # MST_PLANAR
			continue
		var m: int = surf_model.get(s, 0)
		var origin: Vector3 = origin_of.get(m, Vector3.ZERO)
		var first := surfs.decode_s32(o + 12)
		var count := surfs.decode_s32(o + 16)
		for k in count:
			var vo := (first + k) * 44
			var p := Vector3(verts.decode_float(vo), verts.decode_float(vo + 4), verts.decode_float(vo + 8)) + origin
			var st := Vector2(verts.decode_float(vo + 12), verts.decode_float(vo + 16))
			var best := INF
			for f in faces.get(m, []):
				# q3map flattens a nearly axial plane onto its axis (SnapPlane), so match the snapped one
				var pl: PackedFloat64Array = Q3MapExport.snap_plane(f[0])
				if f[3] != shader or absf(Vector3(pl[0], pl[1], pl[2]).dot(p) - pl[3]) > 0.1:
					continue
				var uv: PackedFloat64Array = f[1]
				var sz: Vector2 = f[2]
				var e := Vector2((uv[0] * p.x + uv[1] * p.y + uv[2] * p.z + uv[3]) / sz.x,
						(uv[4] * p.x + uv[5] * p.y + uv[6] * p.z + uv[7]) / sz.y)
				# q3map moves each surface's coordinates by whole repeats toward 0 (surface.c), which
				# draws the same, so only the part after the whole repeats counts
				var dd := e - st
				best = minf(best, (dd - dd.round()).length())
			if best < INF:
				if best > worst:
					where = "%s at %s, st %s" % [shader, p, st]
				worst = maxf(worst, best)
				n += 1
				if m != 0:
					in_models += 1
	return [worst, n, where, in_models]
