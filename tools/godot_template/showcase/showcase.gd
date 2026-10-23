extends Node3D
## Library showcase: characters with layered legs/torso clips, every catalog item, and the
## remastered effects firing on a loop.
##   Left / Right   featured character      Up / Down   legs clip      PgUp / PgDn   torso clip
##   R  ragdoll / stand up                  Q  quad shell on/off        1-9  fire one effect
##   Tab  effect loop on/off                Right mouse + WASD, Space/C  fly the camera

const LINEUP := [
	["sarge", "LEGS_RUN", "TORSO_ATTACK"], ["visor", "LEGS_WALK", "TORSO_STAND"],
	["orbb", "LEGS_RUN", "TORSO_STAND"], ["doom", "LEGS_IDLECR", "TORSO_ATTACK2"],
	["anarki", "LEGS_JUMP", "TORSO_RAISE"], ["xaero", "LEGS_TURN", "TORSO_GESTURE"],
	["klesk", "LEGS_BACK", "TORSO_STAND"], ["bones", "LEGS_WALKCR", "TORSO_ATTACK"],
]
const EFFECTS := ["rocket", "rail", "plasma", "lightning", "grenade", "bfg", "bullets", "gibs", "teleport"]

var featured: Q3Character
var cam: Camera3D
var info: Label
var fx_origin := Vector3(10, 0, -1)
var fx_loop := true
var quad_on := false
var _fx_timer := 1.0
var _fx_index := 0
var _yaw := -0.35
var _pitch := -0.32
var _char := 0
var _legs := 0
var _torso := 0


func _ready() -> void:
	_environment()
	_ground()
	_fx_range()
	cam = Camera3D.new()
	cam.fov = 70.0
	add_child(cam)
	cam.position = Vector3(1.5, 2.8, 8.5)
	cam.look_at(Vector3(1.5, 0.9, -2.0))
	_pitch = cam.rotation.x
	_yaw = cam.rotation.y
	_ui()
	_char = maxi(Q3.characters.find("keel"), 0)
	featured = _character(Q3.characters[_char] if Q3.characters.size() > 0 else "keel", Vector3(0, 0, 2.2),
			"LEGS_IDLE", "TORSO_GESTURE")
	for i in LINEUP.size():
		var e: Array = LINEUP[i]
		if Q3.characters.has(e[0]):
			_character(e[0], Vector3(-6.0 + float(i) * 1.7, 0, -0.5), e[1], e[2])
	_items()
	_update_info()


func _character(model: String, pos: Vector3, legs: String, torso: String) -> Q3Character:
	var c := Q3Character.new()
	c.model = model
	c.legs_clip = legs
	c.torso_clip = torso
	add_child(c)
	c.position = pos
	var tag := Label3D.new()
	tag.text = model
	tag.font_size = 48
	tag.pixel_size = 0.004
	tag.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	tag.position = Vector3(0, 2.25, 0)
	tag.modulate = Color(1, 0.85, 0.65)
	c.add_child(tag)
	return c


func _items() -> void:
	var names := Q3.entities.keys()
	var per_row := 9
	for i in names.size():
		var p := Q3Pickup.new()
		p.classname = names[i]
		var row := i / per_row
		var col := i % per_row
		var pos := Vector3(-8.0 + float(col) * 2.1, 0.35, -4.0 - float(row) * 2.4)
		var ped := MeshInstance3D.new()
		var cyl := CylinderMesh.new()
		cyl.top_radius = 0.45
		cyl.bottom_radius = 0.5
		cyl.height = 0.35
		ped.mesh = cyl
		ped.material_override = _mat(Color(0.16, 0.14, 0.13), 0.8, 0.3)
		add_child(ped)
		ped.position = pos - Vector3(0, 0.175, 0)
		add_child(p)
		p.position = pos
		var l := Label3D.new()
		l.text = String(Q3.entities[names[i]]["name"])
		l.font_size = 28
		l.pixel_size = 0.004
		l.billboard = BaseMaterial3D.BILLBOARD_ENABLED
		l.position = pos + Vector3(0, -0.05, 0.55)
		l.modulate = Color(0.9, 0.9, 0.95)
		add_child(l)


func _environment() -> void:
	var env := Environment.new()
	var sky := Sky.new()
	var sm := ProceduralSkyMaterial.new()
	sm.sky_top_color = Color(0.09, 0.08, 0.1)
	sm.sky_horizon_color = Color(0.32, 0.22, 0.18)
	sm.ground_horizon_color = Color(0.2, 0.15, 0.12)
	sm.ground_bottom_color = Color(0.05, 0.04, 0.04)
	sky.sky_material = sm
	env.background_mode = Environment.BG_SKY
	env.sky = sky
	env.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	env.ambient_light_energy = 1.0
	env.tonemap_mode = Environment.TONE_MAPPER_AGX
	env.glow_enabled = true
	env.glow_intensity = 0.6
	env.glow_bloom = 0.08
	env.ssao_enabled = true
	env.fog_enabled = true
	env.fog_density = 0.006
	env.fog_light_color = Color(0.3, 0.24, 0.2)
	var we := WorldEnvironment.new()
	we.environment = env
	add_child(we)
	var sun := DirectionalLight3D.new()
	sun.light_color = Color(1.0, 0.88, 0.75)
	sun.light_energy = 2.1
	sun.shadow_enabled = true
	sun.rotation_degrees = Vector3(-52, -35, 0)
	add_child(sun)
	var rim := DirectionalLight3D.new()
	rim.light_color = Color(0.55, 0.65, 1.0)
	rim.light_energy = 0.5
	rim.rotation_degrees = Vector3(-25, 150, 0)
	add_child(rim)


func _mat(c: Color, rough: float, metal: float) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = c
	m.roughness = rough
	m.metallic = metal
	return m


func _ground() -> void:
	var body := StaticBody3D.new()
	var shape := CollisionShape3D.new()
	var box := BoxShape3D.new()
	box.size = Vector3(80, 1, 80)
	shape.shape = box
	shape.position = Vector3(0, -0.5, 0)
	body.add_child(shape)
	var mesh := MeshInstance3D.new()
	var plane := PlaneMesh.new()
	plane.size = Vector2(80, 80)
	mesh.mesh = plane
	mesh.material_override = _mat(Color(0.11, 0.1, 0.095), 0.88, 0.0)
	body.add_child(mesh)
	add_child(body)


func _fx_range() -> void:
	# a wall to shoot at, facing -X toward the shooter
	var wall := StaticBody3D.new()
	var shape := CollisionShape3D.new()
	var box := BoxShape3D.new()
	box.size = Vector3(0.6, 4, 7)
	shape.shape = box
	wall.add_child(shape)
	var mesh := MeshInstance3D.new()
	var bm := BoxMesh.new()
	bm.size = box.size
	mesh.mesh = bm
	mesh.material_override = _mat(Color(0.2, 0.18, 0.17), 0.85, 0.2)
	wall.add_child(mesh)
	add_child(wall)
	wall.position = fx_origin + Vector3(3.3, 2.0, 0)


func _fire(name: String) -> void:
	var shooter := fx_origin + Vector3(-4.0, 1.4, 0)
	var on_wall := fx_origin + Vector3(3.0, 1.4, randf_range(-2.0, 2.0))
	match name:
		"rocket":
			Q3FX.muzzle_flash(self, "rocketl", shooter)
			Q3FX.rocket(self, shooter, on_wall)
		"rail":
			Q3FX.muzzle_flash(self, "railgun", shooter)
			Q3FX.rail(self, shooter, on_wall, Color(0.15, 0.55, 1.0), Color(0.9, 0.95, 1.0))
		"plasma":
			Q3FX.muzzle_flash(self, "plasma", shooter)
			Q3FX.plasma(self, shooter, on_wall)
		"lightning":
			Q3FX.lightning(self, shooter, on_wall, 1.2)
			Q3FX.explosion(self, "lightning", on_wall, Vector3.LEFT)
		"grenade":
			Q3FX.explosion(self, "grenade", fx_origin + Vector3(0.5, 0.02, randf_range(-1.5, 1.5)), Vector3.UP)
		"bfg":
			Q3FX.explosion(self, "bfg", on_wall, Vector3.LEFT)
		"bullets":
			Q3FX.muzzle_flash(self, "machinegun", shooter)
			for i in 6:
				Q3FX.explosion(self, "bullet", fx_origin + Vector3(3.0, randf_range(0.6, 2.4), randf_range(-2.5, 2.5)), Vector3.LEFT)
		"gibs":
			Q3FX.gibs(self, fx_origin + Vector3(0, 0.5, 0))
		"teleport":
			Q3FX.teleport(self, fx_origin + Vector3(-1.5, 0, 1.5))


func _process(delta: float) -> void:
	if fx_loop:
		_fx_timer -= delta
		if _fx_timer <= 0.0:
			_fire(EFFECTS[_fx_index % EFFECTS.size()])
			_fx_index += 1
			_fx_timer = 1.6
	if Input.is_mouse_button_pressed(MOUSE_BUTTON_RIGHT):
		var v := Vector3(
			float(Input.is_key_pressed(KEY_D)) - float(Input.is_key_pressed(KEY_A)),
			float(Input.is_key_pressed(KEY_SPACE)) - float(Input.is_key_pressed(KEY_C)),
			float(Input.is_key_pressed(KEY_S)) - float(Input.is_key_pressed(KEY_W)))
		cam.position += cam.global_basis * v * 6.0 * delta


func _unhandled_input(e: InputEvent) -> void:
	if e is InputEventMouseMotion and Input.is_mouse_button_pressed(MOUSE_BUTTON_RIGHT):
		_yaw -= e.relative.x * 0.004
		_pitch = clampf(_pitch - e.relative.y * 0.004, -1.4, 1.4)
		_apply_look()
	elif e is InputEventKey and e.pressed and not e.echo:
		match e.keycode:
			KEY_RIGHT, KEY_LEFT:
				_char = wrapi(_char + (1 if e.keycode == KEY_RIGHT else -1), 0, Q3.characters.size())
				featured.load_model(Q3.characters[_char])
				if quad_on:
					_set_quad(true)
			KEY_UP, KEY_DOWN:
				var l := featured.clips("LEGS") + featured.clips("BOTH")
				_legs = wrapi(_legs + (1 if e.keycode == KEY_UP else -1), 0, l.size())
				featured.play_legs(l[_legs])
			KEY_PAGEUP, KEY_PAGEDOWN:
				var t := featured.clips("TORSO")
				_torso = wrapi(_torso + (1 if e.keycode == KEY_PAGEUP else -1), 0, t.size())
				featured.play_torso(t[_torso])
			KEY_R:
				if featured.ragdolled:
					featured.stand_up()
				else:
					featured.ragdoll(Vector3(0, 6, -10))
			KEY_Q:
				_set_quad(not quad_on)
			KEY_TAB:
				fx_loop = not fx_loop
			_:
				var n: int = e.keycode - KEY_1
				if n >= 0 and n < EFFECTS.size():
					_fire(EFFECTS[n])
		_update_info()


func _set_quad(on: bool) -> void:
	quad_on = on
	var shell := Q3FX.shell_material("quad") if on else null
	for m in featured.find_children("*", "MeshInstance3D", true, false):
		(m as MeshInstance3D).material_overlay = shell


func _apply_look() -> void:
	cam.rotation = Vector3(_pitch, _yaw, 0)


func _ui() -> void:
	var layer := CanvasLayer.new()
	add_child(layer)
	info = Label.new()
	info.position = Vector2(18, 14)
	info.add_theme_font_size_override("font_size", 17)
	info.add_theme_color_override("font_color", Color(0.95, 0.9, 0.85))
	info.add_theme_color_override("font_outline_color", Color(0, 0, 0))
	info.add_theme_constant_override("outline_size", 6)
	layer.add_child(info)


func _update_info() -> void:
	if featured == null:
		return
	info.text = "Q3Remaster library  -  %d characters, %d items, %d props\n" % [Q3.characters.size(), Q3.entities.size(), Q3.props.size()] \
		+ "Featured: %s   legs %s   torso %s%s\n" % [featured.model, featured.legs_clip, featured.torso_clip, "   (ragdoll)" if featured.ragdolled else ""] \
		+ "Left/Right character   Up/Down legs   PgUp/PgDn torso   R ragdoll   Q quad shell\n" \
		+ "1-9 effects (%s)   Tab effect loop %s   Right mouse + WASD fly" % [", ".join(EFFECTS), "on" if fx_loop else "off"]
