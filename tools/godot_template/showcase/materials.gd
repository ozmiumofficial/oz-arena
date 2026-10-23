extends Node3D
## World material browser: every remastered world material on a 2x2-repeat wall panel with a
## sphere beside it, under a moving light, and every sky.
##   PgUp / PgDn  page        1-7  filter: all, metal, stone, light, organic, liquid, effects (add/blend)
##   K  next sky             Right mouse + WASD fly
## Command-line (after --): page=N filter=name sky=N

const COLS := 8
const ROWS := 4
const FILTERS := ["all", "metal", "stone", "light", "organic", "liquid", "effects"]

var cat: Dictionary
var names: PackedStringArray = []
var skies: PackedStringArray = []
var page := 0
var filter := "all"
var sky_i := 0
var holder: Node3D
var env: Environment
var info: Label
var cam: Camera3D
var mover: OmniLight3D
var _quad: ArrayMesh
var _yaw := 0.0
var _pitch := -0.08


func _ready() -> void:
	cat = Q3.load_json(Q3.LIB + "world/world_materials.json").get("materials", {})
	for n in cat:
		if cat[n].get("kind") == "sky" and cat[n].has("res"):
			skies.append(n)
	skies.sort()
	for a in OS.get_cmdline_user_args():
		var kv := a.split("=", true, 1)
		if kv.size() == 2:
			match kv[0]:
				"page": page = int(kv[1])
				"filter": filter = kv[1]
				"sky": sky_i = int(kv[1])
	_quad = _panel_mesh(2.4, 2.0)
	_setup_world()
	_apply_filter()
	_build_page()
	_apply_sky()


func _panel_mesh(size: float, repeats: float) -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	var h := size * 0.5
	var pts := [Vector3(-h, -h, 0), Vector3(h, -h, 0), Vector3(h, h, 0), Vector3(-h, h, 0)]
	var uvs := [Vector2(0, repeats), Vector2(repeats, repeats), Vector2(repeats, 0), Vector2(0, 0)]
	for i in [0, 2, 1, 0, 3, 2]:  # Godot front faces are clockwise as seen by the camera
		st.set_normal(Vector3(0, 0, 1))
		st.set_uv(uvs[i])
		st.add_vertex(pts[i])
	st.generate_tangents()
	return st.commit()


func _setup_world() -> void:
	env = Environment.new()
	env.background_mode = Environment.BG_SKY
	env.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	env.ambient_light_energy = 0.6
	env.tonemap_mode = Environment.TONE_MAPPER_AGX
	env.glow_enabled = true
	env.glow_intensity = 0.5
	env.ssao_enabled = true
	var we := WorldEnvironment.new()
	we.environment = env
	add_child(we)
	var sun := DirectionalLight3D.new()
	sun.light_energy = 1.2
	sun.rotation_degrees = Vector3(-35, 25, 0)
	sun.shadow_enabled = true
	add_child(sun)
	mover = OmniLight3D.new()
	mover.omni_range = 9.0
	mover.light_energy = 2.5
	mover.light_color = Color(1.0, 0.9, 0.8)
	add_child(mover)
	cam = Camera3D.new()
	cam.fov = 60
	add_child(cam)
	cam.position = Vector3(0, 0.2, 11.5)
	var layer := CanvasLayer.new()
	add_child(layer)
	info = Label.new()
	info.position = Vector2(16, 12)
	info.add_theme_font_size_override("font_size", 17)
	info.add_theme_constant_override("outline_size", 6)
	info.add_theme_color_override("font_outline_color", Color.BLACK)
	layer.add_child(info)


func _apply_filter() -> void:
	names.clear()
	for n in cat:
		var e: Dictionary = cat[n]
		if not e.has("res") or e["kind"] in ["sky", "fog"]:
			continue
		var g: String = e.get("group", "")
		var sh: String = e.get("shader", "")
		var ok := filter == "all" or filter == g or (filter == "effects" and (sh == "world_add" or sh.begins_with("world_blend")))
		if filter == "liquid":
			ok = e["kind"] == "liquid"
		if ok:
			names.append(n)
	names.sort()
	page = clampi(page, 0, maxi(0, (names.size() - 1) / (COLS * ROWS)))


func _build_page() -> void:
	if holder:
		holder.queue_free()
	holder = Node3D.new()
	add_child(holder)
	var start := page * COLS * ROWS
	for i in COLS * ROWS:
		var k := start + i
		if k >= names.size():
			break
		var e: Dictionary = cat[names[k]]
		var mat: Material = load(e["res"])
		var x := (float(i % COLS) - (COLS - 1) * 0.5) * 3.1
		var y := (float(ROWS - 1) * 0.5 - float(i / COLS)) * 3.0
		var panel := MeshInstance3D.new()
		panel.mesh = _quad
		panel.material_override = mat
		panel.position = Vector3(x - 0.35, y, 0)
		holder.add_child(panel)
		var ball := MeshInstance3D.new()
		var sm := SphereMesh.new()
		sm.radius = 0.42
		sm.height = 0.84
		ball.mesh = sm
		ball.material_override = mat
		ball.position = Vector3(x + 1.05, y - 0.7, 0.55)
		holder.add_child(ball)
		var l := Label3D.new()
		l.text = String(names[k]).replace("textures/", "")
		l.font_size = 22
		l.pixel_size = 0.0045
		l.outline_size = 6
		l.position = Vector3(x, y - 1.35, 0.05)
		holder.add_child(l)
	_update_info()


func _apply_sky() -> void:
	if skies.is_empty():
		return
	sky_i = wrapi(sky_i, 0, skies.size())
	var s: Sky = load(cat[skies[sky_i]]["res"])
	env.sky = s
	_update_info()


func _update_info() -> void:
	if info == null:
		return
	var pages := maxi(1, int(ceil(float(names.size()) / float(COLS * ROWS))))
	info.text = "World materials  %d shown (%s)   page %d / %d   sky: %s\n" % [names.size(), filter, page + 1, pages,
			(skies[sky_i] if skies.size() else "-")] \
		+ "PgUp/PgDn page   1-7 filter (%s)   K next sky   Right mouse + WASD fly" % ", ".join(FILTERS)


func _process(delta: float) -> void:
	var t := Time.get_ticks_msec() / 1000.0
	mover.position = Vector3(sin(t * 0.6) * 10.0, cos(t * 0.45) * 3.0, 2.2)
	if Input.is_mouse_button_pressed(MOUSE_BUTTON_RIGHT):
		var v := Vector3(float(Input.is_key_pressed(KEY_D)) - float(Input.is_key_pressed(KEY_A)),
			float(Input.is_key_pressed(KEY_SPACE)) - float(Input.is_key_pressed(KEY_C)),
			float(Input.is_key_pressed(KEY_S)) - float(Input.is_key_pressed(KEY_W)))
		cam.position += cam.global_basis * v * 6.0 * delta


func _unhandled_input(e: InputEvent) -> void:
	if e is InputEventMouseMotion and Input.is_mouse_button_pressed(MOUSE_BUTTON_RIGHT):
		_yaw -= e.relative.x * 0.004
		_pitch = clampf(_pitch - e.relative.y * 0.004, -1.4, 1.4)
		cam.rotation = Vector3(_pitch, _yaw, 0)
	elif e is InputEventKey and e.pressed and not e.echo:
		match e.keycode:
			KEY_PAGEDOWN:
				page += 1
				_apply_filter()
				_build_page()
			KEY_PAGEUP:
				page = maxi(page - 1, 0)
				_build_page()
			KEY_K:
				sky_i += 1
				_apply_sky()
			_:
				var n: int = e.keycode - KEY_1
				if n >= 0 and n < FILTERS.size():
					filter = FILTERS[n]
					page = 0
					_apply_filter()
					_build_page()
