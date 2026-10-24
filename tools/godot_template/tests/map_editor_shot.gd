extends Node
## The Map editor with something in it, for captures (maps go to a scratch folder):
##   godot --path . res://tests/capture.tscn -- scene=res://tests/map_editor_shot.tscn out=C:/temp/ed count=1
## sheet=1 opens the F1 hotkey sheet; dialog=1 the unsaved-changes dialog; diag=1 adds the F10
## diagnostics overlay. e4=1 adds entities (spawns, pickups, a jump pad, a teleporter, lights, a
## sound) with the Entities tab and the jump pad selected; e4=lights the same lit by its own lights;
## e4=prefab saves a prefab and shows the Prefabs tab with the prefab tool's ghost. e6=<map> imports
## one of Quake 3's maps from the paks.

var ed: Q3MapEditor


func _ready() -> void:
	Q3MapEditor.maps_dir = "user://map_editor_test/maps"
	Q3MapEditor.recent = "user://map_editor_test/recent.json"
	# every shot starts from a new map, not whatever the last one saved
	DirAccess.remove_absolute(ProjectSettings.globalize_path(Q3MapEditor.recent))
	ed = load("res://scenes/map_editor/map_editor.tscn").instantiate()
	add_child(ed)
	var d := ed.doc
	var wall := "textures/gothic_block/blocks15"
	var trim := "textures/base_trim/dark_tin2"
	d.add(Q3MapDoc.box_brush(Vector3(-512, 256, 0), Vector3(512, 272, 192), wall))
	d.add(Q3MapDoc.box_brush(Vector3(256, -256, 0), Vector3(272, 256, 192), wall))
	var steps := []
	for i in 4:
		steps.append(d.add(Q3MapDoc.box_brush(Vector3(-256 + i * 32, -64, 0), Vector3(-224 + i * 32, 64, 16 + i * 16), trim)))
	var pillar := Q3MapDoc.box_brush(Vector3(96, 96, 0), Vector3(160, 160, 192), trim)
	d.add(Q3MapDoc.transform_brush(pillar, Q3MapDoc.yaw_rotation(45, Vector3(128, 128, 0))))
	var block := d.add(Q3MapDoc.box_brush(Vector3(-64, -192, 0), Vector3(64, -96, 64), wall))
	d.dirty = true
	ed.select([block] + steps.slice(3))
	ed.camera.set_pose(Vector3(-11, 7.5, -12), -138, -24)
	var args := OS.get_cmdline_user_args()
	for a in args:
		if a.begins_with("e6="):
			_e6(a.substr(3))
			return
		if a.begins_with("e7="):
			_e7(a.substr(3), block, steps)
			return
	if "e4=1" in args or "e4=lights" in args or "e4=prefab" in args:
		_e4(args)
		return
	if "e3=1" in args:
		# a sky over part of the room, tool brushes, a lava pool, a tinted block, a face picked
		d.add(Q3MapDoc.box_brush(Vector3(-512, -256, 256), Vector3(0, 256, 272), "textures/skies/hellsky"))
		d.add(Q3MapDoc.box_brush(Vector3(160, 160, 0), Vector3(224, 224, 64), "textures/common/caulk"))
		d.add(Q3MapDoc.box_brush(Vector3(-400, -200, 0), Vector3(-336, -120, 128), "textures/common/clip"))
		d.add(Q3MapDoc.box_brush(Vector3(-160, 96, -2), Vector3(-32, 224, 0), "textures/liquids/lavahell"))
		var tinted := Q3MapDoc.box_brush(Vector3(96, -224, 0), Vector3(160, -160, 96), "textures/gothic_block/blocks15")
		for f in tinted["faces"]:
			f["color"] = Color(0.55, 0.75, 1.0)
		d.add(tinted)
		ed.camera.set_pose(Vector3(-11, 5.5, -12), -138, -14)
		ed.set_tool("face")
		ed.select_faces([Vector2i(block, 4)])
		ed.material = "textures/gothic_block/blocks15"
		ed.ui.browser.folder = "gothic_block"
		ed.ui.browser._folder.select(ed.ui.browser._folder.get_item_count() - 1)
		for i in ed.ui.browser._folder.item_count:
			if ed.ui.browser._folder.get_item_metadata(i) == "gothic_block":
				ed.ui.browser._folder.select(i)
		ed.ui.browser.rebuild()
		return
	if "e2=vertex" in args or "e2=face" in args:
		# curves beside the steps, a chamfered block, then the vertex or face tool on it
		var curves := []
		for k in [["bevel", Vector3(-320, -192, 0)], ["cylinder", Vector3(160, -160, 0)], ["arch", Vector3(-64, 160, 0)]]:
			curves.append(ed.doc.add(Q3PatchFactory.make(k[0], k[1], 64.0, 128.0, trim if k[0] != "arch" else wall)))
		var b: Dictionary = d.get_obj(block)
		var pts := ed.object_points(block)
		var keep := PackedFloat64Array()
		for i in pts.size() / 3:
			if not (pts[i * 3] == 64 and pts[i * 3 + 1] == -96 and pts[i * 3 + 2] == 64):
				keep.append_array([pts[i * 3], pts[i * 3 + 1], pts[i * 3 + 2]])
		d.put(Q3MapEditor.rebuild_from_points(b, keep))
		if "e2=vertex" in args:
			ed.select([block, curves[0]])
			ed.set_tool("vertex")
			ed.verts[Vector2i(block, 0)] = true
		else:
			ed.clear_selection()
			ed.set_tool("face")
			ed.select_faces([Vector2i(block, 0), Vector2i(steps[0], 4), Vector2i(steps[1], 4)])
		ed.notify("Grid 16u (0.5 m)")
		return
	if "sheet=1" in args:
		ed.ui.toggle_sheet()
	elif "dialog=1" in args:
		ed.execute("session.leave")
	else:
		# the create tool part way through: a footprint on the floor, height being set
		ed.set_tool("create")
		var ct: Q3CreateTool = ed.tool
		ct._axis = 2
		ct._sign = 1.0
		ct._plane = 0.0
		ct._a = Vector3(-128, 96, 0)
		ct._b = Vector3(0, 192, 0)
		ct.height = 48
		ct._enter_height()
	ed.notify("Grid 16u (0.5 m)")
	if "diag=1" in args:
		push_error("a sample error for the diagnostics overlay")
		Diagnostics.toggle_overlay()


## e6=<map>: one of Quake 3's maps imported from the paks, seen from its intermission point, with a
## brush picked in the face tool so the inspector shows its imported alignment.
func _e6(map: String) -> void:
	await ed.import_map("bsp", map)
	var at := Q3MapEditor.to_id(ed.camera.global_position)
	var fwd := Q3MapEditor.to_id(-ed.camera.global_transform.basis.z).normalized()
	var best := Vector2i(-1, -1)
	var best_score := -INF
	for id in ed.doc.ids_of("brush"):
		var b := ed.doc.get_obj(id)
		var ws := ed.doc.windings(id)
		for i in b["faces"].size():
			if Q3MapMaterials.kind(b["faces"][i]["material"]) != "surface" or i >= ws.size():
				continue
			var w: PackedFloat64Array = ws[i]
			var n := w.size() / 3
			if n < 3:
				continue
			var c := Vector3.ZERO
			var lo := Vector3.INF
			var hi := -Vector3.INF
			for k in n:
				var p := Vector3(w[k * 3], w[k * 3 + 1], w[k * 3 + 2])
				c += p / n
				lo = lo.min(p)
				hi = hi.max(p)
			var to := c - at
			var pl := Q3MapDoc.plane_of(b, i)
			# a face of a modest size, 4-20 m ahead near the middle of the view, turned toward the camera
			if (hi - lo).length() > 384.0 or to.length() < 128.0 or to.length() > 640.0 or Vector3(pl[0], pl[1], pl[2]).dot(-to.normalized()) < 0.5:
				continue
			var score := fwd.dot(to.normalized())
			if score > best_score:
				best_score = score
				best = Vector2i(id, i)
	if best.x >= 0:
		ed.set_tool("face")
		ed.select_faces([best])
	ed.notify("Imported " + map)


## e7=objects | history | open | filters: the Objects tab with a group open, the History tab after a
## few edits, the Open dialog with two saved maps and their thumbnails, q3dm6 with its tool brushes
## filtered.
func _e7(mode: String, block: int, steps: Array) -> void:
	var d := ed.doc
	match mode:
		"objects", "history":
			d.checkpoint("Group")
			var gid := ed._new_group()
			for id in steps:
				var o := d.edit(id)
				o["group"] = gid
				d.put(o)
			d.groups[gid]["name"] = "Steps"
			d.checkpoint("Add spawn")
			var sp := d.add(Q3MapEntities.point("info_player_deathmatch", Vector3(-160, 160, 24), {"angle": "315"}))
			var named := d.edit(sp)
			named["name"] = "Red side spawn"
			d.put(named)
			ed.select([block])
			ed.execute("edit.nudge.up")
			ed.execute("edit.nudge.up")
			ed.doc.undo()
			ed.ui.browser.set_tab(mode)
			if mode == "objects":
				await get_tree().process_frame
				var ol := ed.ui.browser.outliner
				ol.refresh()
				for it in ol._tree.get_root().get_children():
					if it.get_metadata(0) is Dictionary:
						it.collapsed = false
		"open":
			Q3MapEditor.maps_dir = "user://map_editor_test/shot_maps"
			DirAccess.make_dir_recursive_absolute(Q3MapEditor.maps_dir)
			for f in DirAccess.get_files_at(Q3MapEditor.maps_dir):
				DirAccess.remove_absolute(ProjectSettings.globalize_path(Q3MapEditor.maps_dir.path_join(f)))
			ed.save_map("hall_of_steps")
			await _frames(6)
			ed.camera.set_pose(Vector3(4, 3, -9), -200, -12)
			await _frames(2)
			ed.save_map("pillar_court")
			await _frames(6)
			ed.execute("file.open")
		"filters":
			await ed.import_map("bsp", "q3dm6")
			ed.execute("view.filter.tools")
	ed.notify("Grid 16u (0.5 m)")


func _frames(n: int) -> void:
	for i in n:
		await get_tree().process_frame


func _e4(args: PackedStringArray) -> void:
	var d := ed.doc
	Q3MapPrefabs.dir = "user://map_editor_test/prefabs"
	var ent := func(cn: String, at: Vector3, keys := {}) -> int:
		return d.add(Q3MapEntities.point(cn, at, keys))
	ent.call("info_player_deathmatch", Vector3(-160, 160, 24), {"angle": "315"})
	ent.call("info_player_deathmatch", Vector3(192, -192, 24), {"angle": "135"})
	ent.call("weapon_railgun", Vector3(-64, 64, 16))
	ent.call("weapon_rocketlauncher", Vector3(64, 160, 16))
	ent.call("item_armor_body", Vector3(-320, 0, 16))
	ent.call("item_health_mega", Vector3(-200, -40, 16))
	ent.call("item_quad", Vector3(0, -40, 80))
	ent.call("ammo_rockets", Vector3(-20, 160, 16))
	ent.call("target_speaker", Vector3(200, 200, 96), {"noise": "sound/world/drone6.wav", "spawnflags": "1"})
	ent.call("light", Vector3(-160, -96, 160), {"light": "250", "_color": "1 0.55 0.3"})
	ent.call("light", Vector3(200, 64, 150), {"light": "300", "_color": "0.45 0.6 1"})
	ent.call("light", Vector3(-400, 200, 150), {"light": "200"})
	var pad := ed.place_jump_pad(Vector3(-384, -192, 0), Vector3(0, -144, 64))
	ed.place_teleporter(Vector3(352, -160, 0), Vector3(-352, 160, 0), 0)
	d.dirty = true
	ed.camera.set_pose(Vector3(-13, 9, -14), -138, -24)
	ed.ui.browser.set_tab("entity")
	if "e4=lights" in args:
		ed.clear_selection()
		ed.set_lights_only(true)
	elif "e4=prefab" in args:
		ed.select(ed.group_of(pad) + Q3MapEntities.targets_of(d, d.get_obj(pad)))
		ed.save_prefab("jump_pad")
		ed.ui.browser.set_tab("prefab")
		ed.start_prefab("jump_pad")
		ed.mouse_pos = get_viewport().get_visible_rect().size * Vector2(0.45, 0.62)
		ed.tool.hover(ed.mouse_pos, 0)
	else:
		ed.select(ed.group_of(pad))
	ed.notify("Grid 16u (0.5 m)")
