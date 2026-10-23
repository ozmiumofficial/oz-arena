class_name Q3ShowcaseStage
extends Node3D
## The main menu's showcase, after Skyrim's loading screens: one model from the library floating
## alone in a dark smoky void, turning slowly and bobbing, while the camera eases between a few
## framings (three-quarter, low hero, close, wide) with depth of field. A soft key from above, a
## colored rim from behind (the Profile's accent color), faint embers rising.
## Every 15-25 s the smoke thickens, the subject changes behind it (weapons/change.wav), and it
## clears. Mostly the 29 characters (idle, now and then a gesture, sometimes a powerup shell),
## sometimes a weapon, powerup, flag or map object.

signal subject_changed(name: String, kind: String)

const SWAP_MIN := 15.0
const SWAP_MAX := 25.0
const FADE := 1.2
const CHARACTER_SHARE := 0.7
const SHELL_CHANCE := 0.2
const SHELLS := ["quad", "battlesuit", "regen"]
const ITEMS := ["weapon_rocketlauncher", "weapon_railgun", "weapon_lightning", "weapon_plasmagun", "weapon_bfg",
	"weapon_shotgun", "weapon_grenadelauncher", "weapon_machinegun", "weapon_gauntlet", "item_quad", "item_regen",
	"item_enviro", "item_haste", "item_invis", "item_flight", "item_health_mega", "item_armor_body", "item_armor_combat",
	"team_CTF_redflag", "team_CTF_blueflag"]
const MAP_OBJECTS := ["mapobjects/bitch/fembotbig", "mapobjects/podium/podium4", "mapobjects/skel/xraybig"]
## Captions for map objects (their files are named for the level designers, not the player)
const PROP_NAMES := {"mapobjects/bitch/fembotbig": "Fembot Statue", "mapobjects/podium/podium4": "Tournament Podium",
	"mapobjects/skel/xraybig": "X-Ray Skeleton"}
## Weapons the characters hold: q3_ui's player model shows one in hand too.
const HELD := ["weapons2/railgun/railgun", "weapons2/rocketl/rocketl", "weapons2/plasma/plasma",
	"weapons2/lightning/lightning", "weapons2/shotgun/shotgun", "weapons2/machinegun/machinegun"]
## Camera framings: yaw/elevation in degrees around the subject, distance in subject heights,
## aim = height on the subject (0 feet, 1 top).
const FRAMINGS := [
	{"name": "three_quarter", "yaw": 32.0, "elev": 7.0, "dist": 1.75, "aim": 0.56},
	{"name": "low_hero", "yaw": -24.0, "elev": -11.0, "dist": 1.45, "aim": 0.62},
	{"name": "close", "yaw": 14.0, "elev": 4.0, "dist": 0.78, "aim": 0.84},
	{"name": "wide", "yaw": -38.0, "elev": 13.0, "dist": 2.5, "aim": 0.5},
]

var cam: Camera3D
var env: Environment
var subject: Node3D            ## spins and bobs
var info: Dictionary = {}      ## id, kind, name, size (meters)
var swap_seconds := Vector2(SWAP_MIN, SWAP_MAX)   ## capture tests shorten it
var fixed_framing := -1
var locked := false
var _holder: Node3D
var _key: SpotLight3D
var _rim: SpotLight3D
var _fill: OmniLight3D
var _fog_mat: ShaderMaterial
var _embers: GPUParticles3D
var _smoke: GPUParticles3D
var _attrs: CameraAttributesPractical
var _swap_in := 0.0
var _fading := false
var _queue: Array = []         ## subject ids waiting (map editor hover, profile preview)
var _loading: Dictionary = {}  ## id -> paths requested
var _frame := 0
var _frame_from := {}
var _frame_t := 1.0
var _frame_in := 10.0
var _closer := 1.0
var _closer_goal := 1.0
var _drift_speed := 1.0
var _drift_goal := 1.0
var _t := 0.0
var _gesture_in := 8.0
var _rng := RandomNumberGenerator.new()


func _ready() -> void:
	_rng.randomize()
	_environment()
	_lights()
	_particles()
	cam = Camera3D.new()
	cam.fov = 34.0
	cam.near = 0.05
	_attrs = CameraAttributesPractical.new()
	_attrs.dof_blur_far_enabled = true
	_attrs.dof_blur_near_enabled = true
	_attrs.dof_blur_amount = 0.07
	cam.attributes = _attrs
	add_child(cam)
	_holder = Node3D.new()
	add_child(_holder)
	_frame = _rng.randi() % FRAMINGS.size()
	_frame_from = FRAMINGS[_frame]
	Settings.environment_changed.connect(_apply_settings)
	Settings.changed.connect(func(k, _v): if k == "profile/team_color": _rim.light_color = _accent())
	_apply_settings()
	var first: String = Session.get_meta("first_subject", "") if Session.has_meta("first_subject") else ""
	show_subject(first if first != "" else _random_id(), true)


# ---------------------------------------------------------------- subjects

func _random_id() -> String:
	var cur := String(info.get("id", ""))
	for i in 8:
		var id := ""
		if _rng.randf() < CHARACTER_SHARE and Q3.characters.size() > 0:
			id = "char:" + Q3.characters[_rng.randi() % Q3.characters.size()]
		elif _rng.randf() < 0.8:
			id = "item:" + ITEMS[_rng.randi() % ITEMS.size()]
		else:
			id = "prop:" + MAP_OBJECTS[_rng.randi() % MAP_OBJECTS.size()]
		if id != cur and _valid(id):
			return id
	return "char:" + (Q3.characters[0] if Q3.characters.size() else "sarge")


func _valid(id: String) -> bool:
	var kind := id.get_slice(":", 0)
	var name := id.get_slice(":", 1)
	match kind:
		"char": return Q3.characters.has(name)
		"item": return Q3.entities.has(name)
		"prop": return Q3.props.has(name)
	return false


static func paths_for(id: String) -> PackedStringArray:
	var name := id.get_slice(":", 1)
	match id.get_slice(":", 0):
		"char":
			return PackedStringArray([Q3.LIB + "characters/%s.glb" % name])
		"item":
			var out: PackedStringArray = []
			for m in Q3.entities.get(name, {}).get("models", []):
				out.append(Q3.LIB + "props/%s.glb" % m)
			return out
		"prop":
			return PackedStringArray([Q3.LIB + "props/%s.glb" % name])
	return PackedStringArray()


## Starts streaming a subject's models in the background (the splash calls this for the first one).
static func preload_subject(id: String) -> void:
	for p in paths_for(id):
		if ResourceLoader.exists(p) and ResourceLoader.load_threaded_get_status(p) == ResourceLoader.THREAD_LOAD_INVALID_RESOURCE:
			ResourceLoader.load_threaded_request(p)


func _ready_to_build(id: String) -> bool:
	for p in paths_for(id):
		var s := ResourceLoader.load_threaded_get_status(p)
		if s == ResourceLoader.THREAD_LOAD_IN_PROGRESS:
			return false
	return true


## Shows a subject: "char:<model>", "item:<classname>" or "prop:<path>". `now` skips the smoke fade.
func show_subject(id: String, now := false) -> void:
	if not _valid(id):
		return
	if now:
		preload_subject(id)
		_build(id)
		return
	_queue = [id]
	preload_subject(id)
	_swap_in = 0.0


func lock_character(model: String) -> void:
	locked = model != ""
	if locked and info.get("id", "") != "char:" + model:
		show_subject("char:" + model)


## Menu hover reactions: Play eases closer, Settings slows the drift, Map editor brings a map object.
func nudge(id: String) -> void:
	_closer_goal = 0.82 if id == "play" else 1.0
	_drift_goal = 0.45 if id == "settings" else 1.0
	if id == "map_editor" and String(info.get("kind", "")) != "Map object" and not _fading:
		show_subject("prop:" + MAP_OBJECTS[_rng.randi() % MAP_OBJECTS.size()])


func _build(id: String) -> void:
	for c in _holder.get_children():
		c.queue_free()
	var kind := id.get_slice(":", 0)
	var name := id.get_slice(":", 1)
	var inner := Node3D.new()
	_holder.add_child(inner)
	var title := ""
	var label := ""
	match kind:
		"char":
			var c := Q3Character.new()
			c.model = name
			c.legs_clip = "LEGS_IDLE"
			c.torso_clip = "TORSO_STAND"
			inner.add_child(c)
			_arm(c)
			if _rng.randf() < SHELL_CHANCE:
				var shell := Q3FX.shell_material(SHELLS[_rng.randi() % SHELLS.size()])
				for m in c.find_children("*", "MeshInstance3D", true, false):
					(m as MeshInstance3D).material_overlay = shell
			title = character_name(name)
			label = "Character"
			_gesture_in = _rng.randf_range(5.0, 9.0)
		"item":
			var it: Dictionary = Q3.entities[name]
			for m in it.get("models", []):
				var n := Q3.instance_prop(m)
				if n:
					inner.add_child(n)
			title = String(it.get("name", name))
			label = {"weapon": "Weapon", "powerup": "Powerup", "armor": "Armor", "health": "Health", "team": "Flag"}.get(it.get("type", ""), "Item")
		"prop":
			var n := Q3.instance_prop(name)
			if n:
				inner.add_child(n)
			title = PROP_NAMES.get(name, name.get_file().capitalize())
			label = "Map object"
	# fit: characters keep their real size; everything else is scaled to read at the same distance.
	# `size` frames the camera; `radius` (half the bounding diagonal) sets the depth of field, so a
	# long weapon seen end-on stays inside the sharp zone instead of blurring out.
	var box := _bounds(inner)
	var size := maxf(box.size.y, 0.01)
	if kind != "char":
		var target := 1.0 if label == "Weapon" else 1.25
		var s := target / maxf(maxf(box.size.x, box.size.y), box.size.z)
		inner.scale = Vector3.ONE * s
		box = _bounds(inner)
		size = maxf(maxf(box.size.x, box.size.y), maxf(box.size.z, 0.3))
	inner.position = -box.get_center()
	info = {"id": id, "kind": label, "name": title, "size": size, "radius": box.size.length() * 0.5}
	_swap_in = _rng.randf_range(swap_seconds.x, swap_seconds.y)
	_holder.rotation.y = _rng.randf_range(-0.6, 0.6)
	subject = _holder
	subject_changed.emit(title, label)


## The character's name as Quake 3 spells it (bots.txt: "TankJr", "Bitterman"), else the model's.
static func character_name(model: String) -> String:
	for b in Session.bots:
		if b["model"] == model and b.get("skin", "default") == "default":
			return b["name"]
	return model.capitalize()


## A weapon in the character's hand, at tag_weapon like cgame draws it.
func _arm(c: Q3Character) -> void:
	c.attach(HELD[_rng.randi() % HELD.size()], "weapon")


func _bounds(n: Node3D) -> AABB:
	var box := AABB()
	var first := true
	var inv := n.global_transform.affine_inverse()
	for m in n.find_children("*", "MeshInstance3D", true, false):
		var mi := m as MeshInstance3D
		if not mi.visible or mi.mesh == null:
			continue
		var b := inv * mi.global_transform * mi.get_aabb()
		box = b if first else box.merge(b)
		first = false
	return n.transform * box if not first else AABB(Vector3(-0.5, -0.5, -0.5), Vector3.ONE)


# ---------------------------------------------------------------- per frame

func _process(delta: float) -> void:
	_t += delta * _drift_speed
	_closer = lerpf(_closer, _closer_goal, 1.0 - exp(-delta * 1.5))
	_drift_speed = lerpf(_drift_speed, _drift_goal, 1.0 - exp(-delta * 1.0))
	if subject:
		subject.rotation.y += delta * TAU / 40.0
		subject.position.y = sin(_t * 0.8) * 0.045
	_update_swap(delta)
	_update_gesture(delta)
	_update_camera(delta)


func _update_swap(delta: float) -> void:
	if _fading:
		return
	if _queue.is_empty() and not locked:
		_swap_in -= delta
		if _swap_in <= 0.0:
			_queue = [_random_id()]
			preload_subject(_queue[0])
	if _queue.size() and _swap_in <= 0.0 and _ready_to_build(_queue[0]):
		_fade_to(_queue.pop_front())


func _fade_to(id: String) -> void:
	_fading = true
	var tw := create_tween().set_trans(Tween.TRANS_SINE)
	tw.tween_method(_set_burst, 0.0, 1.0, FADE)
	tw.parallel().tween_property(_attrs, "exposure_multiplier", 0.18, FADE)
	tw.tween_callback(func():
		_build(id)
		UiSound.change()
		_next_framing(true))
	tw.tween_method(_set_burst, 1.0, 0.0, FADE * 1.3)
	tw.parallel().tween_property(_attrs, "exposure_multiplier", 1.0, FADE * 1.3)
	tw.tween_callback(func():
		_fading = false
		_swap_in = _rng.randf_range(swap_seconds.x, swap_seconds.y))


func _set_burst(v: float) -> void:
	if _fog_mat:
		_fog_mat.set_shader_parameter("burst", v * 1.6)
	_smoke.amount_ratio = clampf(Settings.particle_ratio() * (1.0 + v * 2.0), 0.0, 1.0)


func _update_gesture(delta: float) -> void:
	if not String(info.get("id", "")).begins_with("char:") or subject == null:
		return
	_gesture_in -= delta
	if _gesture_in > 0.0:
		return
	_gesture_in = _rng.randf_range(6.0, 10.0)
	var ch := character()
	if ch == null or ch.player == null or not ch.player.has_animation("TORSO_GESTURE"):
		return
	ch.play_torso("TORSO_GESTURE")
	var back := ch.player.get_animation("TORSO_GESTURE").length
	get_tree().create_timer(back).timeout.connect(func(): if is_instance_valid(ch): ch.play_torso("TORSO_STAND"))
	_gesture_in += back


## The character on show, or null for items and map objects.
func character() -> Q3Character:
	if subject == null:
		return null
	for n in subject.get_children():
		for k in n.get_children():
			if k is Q3Character:
				return k
	return null


func _next_framing(jump := false) -> void:
	var cur := _framing_now()
	if fixed_framing >= 0:
		_frame = fixed_framing
	else:
		var n := _frame
		while n == _frame and FRAMINGS.size() > 1:
			n = _rng.randi() % FRAMINGS.size()
		_frame = n
	_frame_from = FRAMINGS[_frame] if jump else cur
	_frame_t = 1.0 if jump else 0.0
	_frame_in = _rng.randf_range(9.0, 13.0)


func _framing_now() -> Dictionary:
	var to: Dictionary = FRAMINGS[_frame]
	var k := smoothstep(0.0, 1.0, _frame_t)
	var out := {}
	for key in ["yaw", "elev", "dist", "aim"]:
		out[key] = lerpf(float(_frame_from[key]), float(to[key]), k)
	return out


func _update_camera(delta: float) -> void:
	if subject == null:
		return
	_frame_t = minf(_frame_t + delta / 6.0, 1.0)
	if fixed_framing < 0 and not _fading:
		_frame_in -= delta
		if _frame_in <= 0.0:
			_next_framing()
	var f := _framing_now()
	var size: float = info.get("size", 1.8)
	var yaw := deg_to_rad(float(f["yaw"]) + 6.0 * sin(_t * 0.07))
	var elev := deg_to_rad(float(f["elev"]) + 3.0 * sin(_t * 0.05 + 1.0))
	var dist := float(f["dist"]) * size * _closer * (1.0 + 0.05 * sin(_t * 0.09)) + 0.6
	# characters are framed at the framing's height (face, chest); items and objects on their center
	var aim := float(f["aim"]) if String(info.get("kind", "")) == "Character" else 0.5
	var target := subject.global_position + Vector3(0, (aim - 0.5) * size, 0)
	var dir := Vector3(sin(yaw) * cos(elev), sin(elev), cos(yaw) * cos(elev))
	cam.global_position = target + dir * dist
	cam.look_at(target)
	# the subject sits in the middle of the right two-thirds of the screen (the menu is on the left)
	var vp := get_viewport().get_visible_rect().size
	var aspect := vp.x / maxf(vp.y, 1.0)
	var half_h := atan(tan(deg_to_rad(cam.fov) * 0.5) * aspect)
	cam.rotate_object_local(Vector3.UP, atan(tan(half_h) / 3.0))
	# the whole subject stays sharp (it turns, so any part can face the camera); only what's
	# in front of or behind it blurs
	var radius: float = info.get("radius", size * 0.6)
	_attrs.dof_blur_far_distance = dist + radius + 0.3
	_attrs.dof_blur_far_transition = radius * 2.0 + 0.5
	_attrs.dof_blur_near_distance = maxf(dist - radius - 0.3, 0.05)
	_attrs.dof_blur_near_transition = maxf(radius * 0.5, 0.15)
	# lights follow the camera: key from above-front-left, rim from behind, a faint fill
	var right := cam.global_basis.x
	_key.global_position = target + Vector3.UP * size * 1.8 + dir * size * 0.9 - right * size * 0.8
	_key.look_at(target)
	_rim.global_position = target - dir * size * 1.1 + Vector3.UP * size * 0.55 + right * size * 0.35
	_rim.look_at(target)
	_rim.spot_range = size * 4.0
	_fill.global_position = target + dir * size * 1.2 + right * size * 1.2


# ---------------------------------------------------------------- the void

func _accent() -> Color:
	var c: Color = Settings.get_value("profile/team_color")
	return c if c is Color else Color(0.85, 0.08, 0.04)


func _environment() -> void:
	env = Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.006, 0.005, 0.006)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.08, 0.07, 0.07)
	env.ambient_light_energy = 0.6
	env.tonemap_mode = Environment.TONE_MAPPER_AGX
	env.glow_intensity = 0.55
	env.glow_bloom = 0.06
	env.glow_hdr_threshold = 0.9
	env.volumetric_fog_density = 0.004
	env.volumetric_fog_albedo = Color(0.32, 0.3, 0.29)
	env.volumetric_fog_length = 32.0
	env.volumetric_fog_detail_spread = 1.5
	env.volumetric_fog_temporal_reprojection_enabled = true
	env.volumetric_fog_ambient_inject = 0.05
	var we := WorldEnvironment.new()
	we.environment = env
	add_child(we)
	var fog := FogVolume.new()
	fog.size = Vector3(16, 9, 16)
	fog.shape = RenderingServer.FOG_VOLUME_SHAPE_BOX
	_fog_mat = ShaderMaterial.new()
	_fog_mat.shader = load("res://shaders/menu/smoke_fog.gdshader")
	var noise := NoiseTexture3D.new()
	noise.width = 64
	noise.height = 64
	noise.depth = 64
	noise.seamless = true
	var fn := FastNoiseLite.new()
	fn.noise_type = FastNoiseLite.TYPE_SIMPLEX_SMOOTH
	fn.frequency = 0.06
	fn.fractal_octaves = 3
	noise.noise = fn
	_fog_mat.set_shader_parameter("noise_tex", noise)
	fog.material = _fog_mat
	add_child(fog)


func _lights() -> void:
	_key = SpotLight3D.new()
	_key.light_color = Color(1.0, 0.93, 0.85)
	_key.light_energy = 9.0
	_key.spot_range = 14.0
	_key.spot_angle = 32.0
	_key.spot_angle_attenuation = 1.6
	_key.light_size = 0.4
	_key.light_volumetric_fog_energy = 0.35
	add_child(_key)
	# a spot from behind, so only the smoke around the subject catches the color
	_rim = SpotLight3D.new()
	_rim.light_color = _accent()
	_rim.light_energy = 9.0
	_rim.spot_range = 7.0
	_rim.spot_angle = 30.0
	_rim.spot_angle_attenuation = 1.4
	_rim.light_volumetric_fog_energy = 0.7
	add_child(_rim)
	_fill = OmniLight3D.new()
	_fill.light_color = Color(0.55, 0.62, 0.8)
	_fill.light_energy = 0.35
	_fill.omni_range = 8.0
	_fill.light_volumetric_fog_energy = 0.0
	add_child(_fill)


func _particles() -> void:
	# embers rising past the subject
	_embers = GPUParticles3D.new()
	_embers.amount = 90
	_embers.lifetime = 7.0
	_embers.preprocess = 7.0
	_embers.visibility_aabb = AABB(Vector3(-6, -3, -6), Vector3(12, 10, 12))
	var pm := ParticleProcessMaterial.new()
	pm.emission_shape = ParticleProcessMaterial.EMISSION_SHAPE_BOX
	pm.emission_box_extents = Vector3(3.5, 0.3, 3.0)
	pm.direction = Vector3.UP
	pm.spread = 25.0
	pm.initial_velocity_min = 0.15
	pm.initial_velocity_max = 0.5
	pm.gravity = Vector3(0, 0.12, 0)
	pm.turbulence_enabled = true
	pm.turbulence_noise_strength = 0.6
	pm.turbulence_noise_scale = 2.5
	pm.scale_min = 0.5
	pm.scale_max = 1.3
	var ramp := Gradient.new()
	ramp.set_color(0, Color(1.0, 0.55, 0.15, 0.0))
	ramp.add_point(0.15, Color(1.0, 0.45, 0.1, 1.0))
	ramp.set_color(ramp.get_point_count() - 1, Color(0.8, 0.1, 0.02, 0.0))
	var rt := GradientTexture1D.new()
	rt.gradient = ramp
	pm.color_ramp = rt
	_embers.process_material = pm
	var quad := QuadMesh.new()
	quad.size = Vector2(0.035, 0.035)
	var em := StandardMaterial3D.new()
	em.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	em.blend_mode = BaseMaterial3D.BLEND_MODE_ADD
	em.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	em.billboard_mode = BaseMaterial3D.BILLBOARD_PARTICLES
	em.vertex_color_use_as_albedo = true
	em.albedo_texture = Q3.fx_texture("flare")
	em.albedo_color = Color(2.5, 2.0, 1.6)
	quad.material = em
	_embers.draw_pass_1 = quad
	_embers.position = Vector3(0, -1.6, 0)
	add_child(_embers)
	# a few big soft smoke puffs drifting through (the volumetric fog does most of the work)
	_smoke = GPUParticles3D.new()
	_smoke.amount = 26
	_smoke.lifetime = 16.0
	_smoke.preprocess = 16.0
	_smoke.visibility_aabb = AABB(Vector3(-10, -5, -10), Vector3(20, 12, 20))
	var sp := ParticleProcessMaterial.new()
	sp.emission_shape = ParticleProcessMaterial.EMISSION_SHAPE_BOX
	sp.emission_box_extents = Vector3(5.0, 1.5, 4.0)
	sp.direction = Vector3(1, 0.3, 0)
	sp.spread = 40.0
	sp.initial_velocity_min = 0.05
	sp.initial_velocity_max = 0.18
	sp.gravity = Vector3.ZERO
	sp.angle_min = -180.0
	sp.angle_max = 180.0
	sp.angular_velocity_min = -6.0
	sp.angular_velocity_max = 6.0
	sp.scale_min = 0.8
	sp.scale_max = 1.6
	var sr := Gradient.new()
	sr.set_color(0, Color(1, 1, 1, 0))
	sr.add_point(0.3, Color(1, 1, 1, 1))
	sr.add_point(0.7, Color(1, 1, 1, 1))
	sr.set_color(sr.get_point_count() - 1, Color(1, 1, 1, 0))
	var st := GradientTexture1D.new()
	st.gradient = sr
	sp.color_ramp = st
	_smoke.process_material = sp
	var sq := QuadMesh.new()
	sq.size = Vector2(3.5, 3.5)
	var sm := StandardMaterial3D.new()
	sm.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	sm.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	sm.billboard_mode = BaseMaterial3D.BILLBOARD_PARTICLES
	sm.billboard_keep_scale = true
	sm.vertex_color_use_as_albedo = true
	sm.albedo_texture = Q3.fx_texture("smoke_puff")
	sm.albedo_color = Color(0.32, 0.29, 0.28, 0.09)
	sm.proximity_fade_enabled = true
	sm.proximity_fade_distance = 1.5
	sm.disable_receive_shadows = true
	sq.material = sm
	_smoke.draw_pass_1 = sq
	add_child(_smoke)


func _apply_settings() -> void:
	Settings.apply_environment(env)
	_key.shadow_enabled = Settings.shadows_on()
	_rim.light_color = _accent()
	var r := Settings.particle_ratio()
	_embers.amount_ratio = r
	# without volumetric fog the puffs carry the smoke on their own
	_smoke.amount_ratio = clampf(r * (1.0 if env.volumetric_fog_enabled else 2.0), 0.0, 1.0)
