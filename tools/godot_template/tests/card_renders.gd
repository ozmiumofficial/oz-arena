extends Node
## Renders player card banners for tools/build_cards.py, then quits. Windowed (needs a GPU):
##   godot --path . res://tests/card_renders.tscn -- job=C:/.../_render_job.json
## The job is {"size": [w, h], "items": [{"id", "kind", "subject", "out"}]}:
##   char      subject = model: the menu showcase (smoky void, key and rim light), close framing,
##             the fighter on the right so the card's emblem has the left
##   sky       subject = res:// Sky resource: looking just above the horizon
##   material  subject = res:// world material: a lit wall at Quake 3's texel scale, light raking
##             from the upper left so the normal maps read
## Each picture is written as a PNG to `out` (an OS path).

const WARMUP := 150   ## frames before the first shot of a group (smoke, particles, shader compiles)
const SETTLE := 40    ## frames per subject after it changes

var size := Vector2i(2048, 512)
var items: Array = []
var _vp: SubViewport
var _group := ""
var _stage: Q3ShowcaseStage
var _holder: Node3D
var _i := 0
var _wait := 0
var rot := -0.6   ## character yaw (job "rot" overrides)


func _ready() -> void:
	var job := ""
	for a in OS.get_cmdline_user_args():
		if a.begins_with("job="):
			job = a.substr(4)
	var d: Variant = Q3.load_json(job) if job != "" else null
	if not d is Dictionary:
		push_error("card_renders: no job (job=<path>)")
		get_tree().quit(1)
		return
	size = Vector2i(int(d["size"][0]), int(d["size"][1]))
	items = d["items"]
	rot = float(d.get("rot", rot))
	Engine.max_fps = 60
	_start_item()


func _process(_d: float) -> void:
	if _i >= items.size():
		return
	_wait -= 1
	if _wait > 0:
		return
	var it: Dictionary = items[_i]
	var img := _vp.get_texture().get_image()
	img.convert(Image.FORMAT_RGB8)
	DirAccess.make_dir_recursive_absolute(String(it["out"]).get_base_dir())
	img.save_png(String(it["out"]))
	print("card_renders: ", it["id"])
	_i += 1
	if _i >= items.size():
		print("card_renders: done, %d" % items.size())
		get_tree().quit()
		return
	_start_item()


## A fresh viewport per kind (each has its own world and environment); subjects swap inside it.
func _start_item() -> void:
	var it: Dictionary = items[_i]
	var kind := String(it["kind"])
	if kind != _group:
		_new_group(kind)
		_wait = WARMUP
	else:
		_wait = SETTLE
	match kind:
		"char":
			_stage.set("_rng", _seeded(String(it["subject"])))
			_stage.show_subject("char:" + String(it["subject"]), true)
			for m in _stage.find_children("*", "MeshInstance3D", true, false):
				(m as MeshInstance3D).material_overlay = null   # no random powerup shell
			_stage.subject.rotation.y = rot   # turned toward the card's middle, the gun pointing past the camera
			_stage.call("_next_framing", true)
			_stage.set("_closer", 0.9)
			_stage.set("_closer_goal", 0.9)
		"sky":
			var env := (_vp.get_child(0) as WorldEnvironment).environment
			env.sky = load(String(it["subject"])) as Sky
		"material":
			_material_wall(load(String(it["subject"])) as Material)


func _seeded(key: String) -> RandomNumberGenerator:
	var r := RandomNumberGenerator.new()
	r.seed = hash(key)
	return r


func _new_group(kind: String) -> void:
	if _vp:
		_vp.queue_free()
	_group = kind
	_vp = SubViewport.new()
	_vp.size = size
	_vp.own_world_3d = true
	_vp.msaa_3d = Viewport.MSAA_4X
	_vp.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	add_child(_vp)
	match kind:
		"char":
			Session.set_meta("first_subject", "char:" + String(items[_i]["subject"]))
			_stage = Q3ShowcaseStage.new()
			_stage.locked = true
			_stage.fixed_framing = 2   # close: head and shoulders
			_vp.add_child(_stage)
			_stage.cam.attributes.exposure_multiplier = 1.4   # a touch brighter than the menu: cards are small
		"sky":
			var we := WorldEnvironment.new()
			var env := Environment.new()
			env.background_mode = Environment.BG_SKY
			env.tonemap_mode = Environment.TONE_MAPPER_AGX
			env.glow_enabled = true
			env.glow_intensity = 0.4
			we.environment = env
			_vp.add_child(we)
			var cam := Camera3D.new()
			cam.keep_aspect = Camera3D.KEEP_WIDTH
			cam.fov = 100.0
			cam.rotation_degrees = Vector3(14.0, 0.0, 0.0)
			_vp.add_child(cam)
		"material":
			var we := WorldEnvironment.new()
			var env := Environment.new()
			env.background_mode = Environment.BG_COLOR
			env.background_color = Color.BLACK
			env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
			env.ambient_light_color = Color(0.5, 0.48, 0.46)
			env.ambient_light_energy = 0.35
			env.tonemap_mode = Environment.TONE_MAPPER_AGX
			env.glow_enabled = true
			env.glow_intensity = 0.5
			env.glow_hdr_threshold = 1.0
			we.environment = env
			_vp.add_child(we)
			var sun := DirectionalLight3D.new()
			sun.light_energy = 2.2
			sun.light_color = Color(1.0, 0.95, 0.88)
			sun.rotation_degrees = Vector3(-48.0, -38.0, 0.0)   # from the upper left, raking across the wall
			_vp.add_child(sun)
			var cam := Camera3D.new()
			cam.projection = Camera3D.PROJECTION_ORTHOGONAL
			cam.keep_aspect = Camera3D.KEEP_HEIGHT
			cam.size = 128.0 * Q3.UNIT   # the banner shows a wall 128 units high (two players' height)
			cam.position = Vector3(0, 0, 4)
			_vp.add_child(cam)
			_holder = Node3D.new()
			_vp.add_child(_holder)


## A wall facing the camera, its UVs in Quake 3's default scale (one texel per unit).
func _material_wall(mat: Material) -> void:
	for c in _holder.get_children():
		c.queue_free()
	var texels := 256.0
	if mat is ShaderMaterial:
		var t: Variant = (mat as ShaderMaterial).get_shader_parameter("albedo_tex")
		if t is Texture2D:
			texels = float((t as Texture2D).get_height())
		var name := String(mat.resource_name)
		var info: Dictionary = Q3.load_json(Q3.LIB + "world/world_materials.json").get("materials", {}).get(name, {})
		if info.has("size"):
			texels = float(info["size"][0])
	var w := 640.0 * Q3.UNIT
	var h := 160.0 * Q3.UNIT
	var pm := PlaneMesh.new()
	pm.size = Vector2(w, h)
	pm.subdivide_width = 32
	pm.subdivide_depth = 8
	pm.orientation = PlaneMesh.FACE_Z
	var arr := pm.get_mesh_arrays()
	var uv: PackedVector2Array = arr[Mesh.ARRAY_TEX_UV]
	var rep := Vector2(640.0 / texels, 160.0 / texels)
	for k in uv.size():
		uv[k] = uv[k] * rep
	arr[Mesh.ARRAY_TEX_UV] = uv
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arr)
	var mi := MeshInstance3D.new()
	mi.mesh = mesh
	mi.material_override = mat
	_holder.add_child(mi)
