@tool
extends RefCounted
## Bakes a map's LightmapGI in the open editor. Godot has no script API for lightmap baking, so
## this presses the editor's own Bake Lightmaps button and answers its file dialog. Run each step
## as its own call (doing them all in one editor-script call crashed the editor once):
##   var B = load("res://tools/bake_maps.gd")
##   B.rebuild("q3dm17")   # closes the map if open, rebuilds it (Q3MapBuilder), opens it
##   B.bake("q3dm17")      # bakes to <map>.lmbake (blocking, with the editor's progress dialog)
##   B.save("q3dm17")      # saves the scene and checks it references the bake
##   B.measure("q3dm17")   # mean / median of the baked light (L0), for calibration
## A whole queue: tools/bake_runner.gd (a node that runs these steps a few frames apart).
## Needs a GPU (the editor's renderer); headless runs can't bake.


const TEXEL := 0.25   ## lightmap texel size in meters (Quake 3's lightmaps were 16 units = 0.5 m)


## Switches a map's .glb import (written by Godot on its first scan) to static lightmaps, which
## generates the UV2 the bake needs, and reimports it. Godot ignores hand-written .import files,
## so this edits the one it made. Returns which maps were reimported.
static func configure(maps: PackedStringArray) -> Dictionary:
	# Never reimport while the editor is scanning or importing: the bridge's calls can run inside
	# the import progress loop, and a nested reimport_files() crashes the editor (2026-10-22).
	var fs := EditorInterface.get_resource_filesystem()
	if fs.is_scanning() or fs.is_importing():
		return {"reimported": 0, "busy": true}
	var files := PackedStringArray()
	for map in maps:
		var imp := "res://library/maps/%s/%s.glb.import" % [map, map]
		var txt := FileAccess.get_file_as_string(imp)
		if txt == "" or not txt.contains("path="):
			continue   # not imported yet: scan first
		var want := {"meshes/light_baking": "2", "meshes/lightmap_texel_size": str(TEXEL)}
		var changed := false
		for k in want:
			var re := RegEx.create_from_string("(?m)^" + k.replace("/", "\\/") + "=.*$")
			var line: String = k + "=" + want[k]
			if re.search(txt):
				if re.search(txt).get_string() != line:
					txt = re.sub(txt, line)
					changed = true
			else:
				txt = txt.replace("[params]\n", "[params]\n\n" + line + "\n")
				changed = true
		if changed or not FileAccess.file_exists("res://library/maps/%s/%s.glb.unwrap_cache" % [map, map]):
			var f := FileAccess.open(imp, FileAccess.WRITE)
			f.store_string(txt)
			f.close()
			files.append("res://library/maps/%s/%s.glb" % [map, map])
	if files.size():
		EditorInterface.get_resource_filesystem().reimport_files(files)
	return {"reimported": files.size()}


static func _path(map: String) -> String:
	return "res://library/maps/%s/%s.tscn" % [map, map]


static func rebuild(map: String) -> Dictionary:
	var path := _path(map)
	if EditorInterface.get_open_scenes().has(path):
		EditorInterface.open_scene_from_path(path)
		EditorInterface.close_scene()
	# the editor caches the builder class; pick up edits made on disk
	var builder := load("res://scripts/game/q3_map_builder.gd") as GDScript
	builder.source_code = FileAccess.get_file_as_string(builder.resource_path)
	builder.reload(true)
	var err := Q3MapBuilder.build(map)
	if err != OK:
		return {"ok": false, "error": error_string(err)}
	EditorInterface.get_resource_filesystem().update_file(path)
	EditorInterface.open_scene_from_path(path)
	return {"ok": true}


static func bake(map: String) -> Dictionary:
	var t := Time.get_ticks_msec()
	var root := EditorInterface.get_edited_scene_root()
	if root == null or root.scene_file_path != _path(map):
		EditorInterface.open_scene_from_path(_path(map))
		root = EditorInterface.get_edited_scene_root()
	var lm := root.get_node_or_null("LightmapGI") as LightmapGI
	if lm == null:
		return {"ok": false, "error": "no LightmapGI"}
	EditorInterface.get_selection().clear()
	EditorInterface.get_selection().add_node(lm)
	EditorInterface.edit_node(lm)
	var btn: Button = null
	for b in EditorInterface.get_base_control().find_children("*", "Button", true, false):
		if (b as Button).text == "Bake Lightmaps" and b.is_visible_in_tree():
			btn = b
	if btn == null:
		return {"ok": false, "error": "the Bake Lightmaps button isn't showing"}
	btn.pressed.emit()
	for d in EditorInterface.get_base_control().find_children("*", "EditorFileDialog", true, false):
		var dlg := d as EditorFileDialog
		if dlg.visible and dlg.title == "Select lightmap bake file:":
			dlg.hide()
			dlg.file_selected.emit("res://library/maps/%s/%s.lmbake" % [map, map])
			break
	return {"ok": lm.light_data != null, "ms": Time.get_ticks_msec() - t}


static func save(map: String) -> Dictionary:
	var root := EditorInterface.get_edited_scene_root()
	var lm := root.get_node_or_null("LightmapGI") as LightmapGI if root else null
	var data_path := "res://library/maps/%s/%s.lmbake" % [map, map]
	if lm and (lm.light_data == null or lm.light_data.resource_path != data_path) and ResourceLoader.exists(data_path):
		lm.light_data = ResourceLoader.load(data_path, "", ResourceLoader.CACHE_MODE_REPLACE)
	EditorInterface.mark_scene_as_unsaved()
	var err := EditorInterface.save_scene()
	var txt := FileAccess.get_file_as_string(_path(map))
	return {"ok": err == OK and txt.contains(".lmbake"), "error": error_string(err)}


## Mean and median of the baked L0 (the lightmap's overall light), sampled every 4 texels.
## Quake 3's own lightmaps (tools/build_maps.py report "q3_lightmap") give the target.
static func measure(map: String) -> Dictionary:
	var d := load("res://library/maps/%s/%s.lmbake" % [map, map]) as LightmapGIData
	if d == null:
		return {"ok": false}
	var vals: PackedFloat32Array = []
	for t in d.lightmap_textures:
		var tl := t as TextureLayered
		var step := 4 if d.is_using_spherical_harmonics() else 1   # directional: L0 then 3 L1 layers
		for li in range(0, tl.get_layers(), step):
			var img := tl.get_layer_data(li)
			if img.is_compressed():
				img.decompress()
			img.convert(Image.FORMAT_RGBAF)
			for y in range(0, img.get_height(), 4):
				for x in range(0, img.get_width(), 4):
					var c := img.get_pixel(x, y)
					var v := (c.r + c.g + c.b) / 3.0
					if v > 0.0:
						vals.append(v)
	vals.sort()
	var sum := 0.0
	for v in vals:
		sum += v
	return {"ok": vals.size() > 0, "mean": sum / maxf(vals.size(), 1), "median": vals[vals.size() / 2] if vals.size() else 0.0,
		"samples": vals.size()}
