extends Node
## Renders every map's levelshot from the remastered map (windowed: needs the GPU), the way
## Quake 3's `levelshot` command took its own: from the intermission point, with the items in
## place. Rendered at twice the size and scaled down for clean edges, 4:3 at 90 degrees across.
##   godot --path . res://tests/levelshots.tscn -- out=C:/path/work/_levelshots [maps=q3dm0,q3dm17] [size=1280] [nofog=q3tourney5]
## nofog= lists maps shot without their fog volumes (q3tourney5's intermission sits inside its fog).
## Writes <out>/<map>.jpg (tools/make_ui.py and tools/build_cards.py prefer these to the 128 px
## originals).

const ITEM_PREFIXES := ["item_", "weapon_", "ammo_", "holdable_", "team_CTF_redflag", "team_CTF_blueflag"]
var out := "user://levelshots"
var width := 1280
var maps: PackedStringArray = []
var nofog: PackedStringArray = ["q3tourney5"]
var _vp: SubViewport


func _ready() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("out="):
			out = a.substr(4)
		elif a.begins_with("maps="):
			maps = a.substr(5).split(",", false)
		elif a.begins_with("nofog="):
			nofog = a.substr(6).split(",", false)
		elif a.begins_with("size="):
			width = int(a.substr(5))
	if maps.is_empty():
		for m in Q3.load_json(Q3.LIB + "ui/maps.json").get("maps", []):
			maps.append(String(m["map"]))
	DirAccess.make_dir_recursive_absolute(out)
	Engine.max_fps = 60
	_vp = SubViewport.new()
	_vp.size = Vector2i(width * 2, width * 3 / 2)
	_vp.own_world_3d = true
	_vp.msaa_3d = Viewport.MSAA_4X
	_vp.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	add_child(_vp)
	_run()


func _run() -> void:
	for m in maps:
		var path := Q3MapBuilder.scene_path(m)
		if not ResourceLoader.exists(path):
			print("LEVELSHOT %s: no scene" % m)
			continue
		var map := (load(path) as PackedScene).instantiate() as Q3Map
		_vp.add_child(map)
		if m in nofog:
			for f in map.find_children("*", "FogVolume", true, false):
				(f as Node3D).visible = false
		var cam := Camera3D.new()
		cam.fov = rad_to_deg(2.0 * atan(tan(deg_to_rad(45.0)) * 3.0 / 4.0))   # 90 across at 4:3
		cam.near = 0.05
		cam.far = 500.0
		cam.transform = map.intermission_transform()
		_vp.add_child(cam)
		cam.make_current()
		await get_tree().physics_frame
		await get_tree().physics_frame
		_place_items(map)
		# shaders compile, portals and pickups settle
		for i in 90:
			await get_tree().process_frame
		await RenderingServer.frame_post_draw
		var img := _vp.get_texture().get_image()
		img.resize(width, width * 3 / 4, Image.INTERPOLATE_LANCZOS)
		img.save_jpg("%s/%s.jpg" % [out, m], 0.92)
		print("LEVELSHOT %s ok" % m)
		map.queue_free()
		cam.queue_free()
		await get_tree().process_frame
	print("LEVELSHOTS done: %d maps" % maps.size())
	get_tree().quit()


## Items where the game puts them: dropped to the floor (G_SpawnItem, FinishSpawningItem's trace
## with the item's 15-unit box) unless suspended (spawnflags 1).
func _place_items(map: Q3Map) -> void:
	var space := _vp.find_world_3d().direct_space_state
	for marker in map.get_node("Entities").get_children():
		var e: Dictionary = marker.get_meta("entity", {})
		var cls := String(e.get("classname", ""))
		if not ITEM_PREFIXES.any(func(p: String) -> bool: return cls.begins_with(p)) or not Q3.entities.has(cls) \
				or not Q3Map.spawns_in_ffa(e):
			continue
		var p := Q3Pickup.new()
		p.classname = cls
		map.add_child(p)
		var at: Vector3 = (marker as Node3D).global_position
		if not (int(e.get("spawnflags", "0")) & 1):
			var q := PhysicsRayQueryParameters3D.create(at + Vector3.UP * 0.05, at + Vector3.DOWN * 128.0)
			var hit := space.intersect_ray(q)
			if hit:
				at = hit["position"] + Vector3.UP * 15.0 * Q3.UNIT
		p.global_position = at
