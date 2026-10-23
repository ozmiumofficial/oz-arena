class_name Q3FX
extends RefCounted
## Remastered id Tech 3 effects, built from library/fx/fx.json (values from cgame:
## cg_weapons.c, cg_effects.c, cg_localents.c, cg_marks.c) and the remastered effect textures.
## Each call adds a self-freeing effect under `parent` at a world position and returns it.
##   Q3FX.explosion(self, "rocket", pos, normal)     rocket grenade bfg plasma rail bullet shotgun_pellet lightning
##   Q3FX.rail(self, from, to, color1, color2)
##   Q3FX.lightning(self, from, to, 1.0) / Q3LightningBeam for a held beam
##   Q3FX.muzzle_flash(self, "rocketl", pos)
##   Q3FX.missile(self, weapon) (the game moves it) / Q3FX.rocket(self, from, to) (flies itself)
##   Q3FX.tracer(self, from, to, eye) / Q3FX.brass(self, "machinegun", pos, basis)
##   Q3FX.blood(self, pos) / Q3FX.gibs(self, pos) / Q3FX.teleport(self, pos)
##   mesh.material_overlay = Q3FX.shell_material("quad")

const SPRITE_ADD := "res://shaders/fx_sprite_add.gdshader"
const SPRITE_MIX := "res://shaders/fx_sprite_mix.gdshader"
const SCROLL_ADD := "res://shaders/fx_scroll_add.gdshader"
const SHELL := "res://shaders/powerup_shell.gdshader"
## CG_MakeExplosion isSprite: these blasts are camera-facing sprites growing from 30 to 72 units
## (CG_AddSpriteExplosion); the rest are their model (bullet flash, ring) lying along the normal.
const SPRITE_EXPLOSIONS := ["rocket", "grenade", "bfg"]
## cg_marks.c: marks last 10 s and fade over the last second; energy marks (plasma, rail) fade
## out by 3 s. MAX_MARK_POLYS caps them; the oldest go first.
const MARK_TOTAL_TIME := 10.0
const MARK_FADE_TIME := 1.0
const ENERGY_MARK_TIME := 3.0
const MAX_MARKS := 160
## gfx/misc/lightning3new layers: tcMod scale / scroll and rgbGen wave sin 1 <amp> 0 <hz>
const LIGHTNING_WIDTH := 8.0

static var _marks: Array = []   ## live and expired marks, oldest first


static func _root(parent: Node, pos: Vector3, normal := Vector3.UP) -> Q3FxTimed:
	var r := Q3FxTimed.new()
	parent.add_child(r)
	r.global_position = pos
	r.global_basis = _up_basis(normal)
	return r


## A basis whose +Y is `normal`.
static func _up_basis(normal: Vector3) -> Basis:
	var n := normal.normalized()
	if n.is_equal_approx(Vector3.UP) or n == Vector3.ZERO:
		return Basis.IDENTITY
	if n.is_equal_approx(Vector3.DOWN):
		return Basis(Vector3.RIGHT, PI)
	return Basis(Quaternion(Vector3.UP, n))


static func _flipbook(tex_name: String, tint: Color, intensity: float) -> ShaderMaterial:
	var mode := String(Q3.fx_textures.get(tex_name, {}).get("mode", "add"))
	var mat := ShaderMaterial.new()
	mat.shader = load(SPRITE_MIX if mode == "blend" else SPRITE_ADD)
	mat.set_shader_parameter("tex", Q3.fx_texture(tex_name))
	mat.set_shader_parameter("frames", Q3.fx_frames(tex_name))
	mat.set_shader_parameter("tint", tint)
	mat.set_shader_parameter("intensity", intensity)
	mat.set_shader_parameter("spin", randf() * TAU)
	return mat


## Camera-facing flipbook quad from an effect texture; radius in id units.
static func sprite(r: Q3FxTimed, tex_name: String, radius: float, tint := Color.WHITE, intensity := 1.0) -> MeshInstance3D:
	var mat := _flipbook(tex_name, tint, intensity)
	var mi := MeshInstance3D.new()
	var quad := QuadMesh.new()
	quad.size = Vector2.ONE * radius * 2.0 * Q3.UNIT
	mi.mesh = quad
	mi.material_override = mat
	mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	mi.extra_cull_margin = radius * Q3.UNIT * 2.0
	r.add_child(mi)
	r.materials.append(mat)
	return mi


## LE_EXPLOSION with a model: the weaphits model drawn with the explosion's animated shader,
## its forward axis on the normal and a random roll (CG_MakeExplosion, RotateAroundDirection).
static func model_flash(r: Q3FxTimed, model_id: String, tex_name: String, tint := Color.WHITE) -> Node3D:
	var model := Q3.instance_prop(model_id)
	if model == null:
		return null
	r.add_child(model)
	# a prop's +Z is id forward: turn it onto the root's +Y (the normal), then roll it
	model.basis = Basis(Vector3.UP, randf() * TAU) * Basis(Vector3.RIGHT, -PI * 0.5)
	# the texture's own blend: railExplosion is blendfunc blend (mostly transparent rings on the
	# ring model), bulletExplosion and plasmaExplosion are add
	var mat := _flipbook(tex_name, tint, 1.0)
	mat.set_shader_parameter("billboard", false)
	mat.set_shader_parameter("spin", 0.0)
	if tex_name == "plasma_explosion":
		# plasmaExplosion: tcMod stretch .6, tcMod rotate 999, rgbGen wave inversesawtooth 1.5 Hz
		mat.set_shader_parameter("uv_stretch", 0.6)
		mat.set_shader_parameter("uv_rotate", deg_to_rad(999.0))
		mat.set_shader_parameter("fade_start", 0.0)
	for mi in model.find_children("*", "GeometryInstance3D", true, false):
		(mi as GeometryInstance3D).material_override = mat
		(mi as GeometryInstance3D).cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	r.materials.append(mat)
	return model


static func light(r: Q3FxTimed, radius_units: float, color: Color, energy := 4.0) -> OmniLight3D:
	var l := OmniLight3D.new()
	l.omni_range = radius_units * Q3.UNIT
	l.light_color = color
	l.light_energy = energy
	l.shadow_enabled = false
	r.add_child(l)
	r.add_light(l)
	return l


## One-shot GPU particles, emitted along the root's +Y (the surface normal). Showcase only: the
## game's effects are Quake 3's own.
static func particles(r: Node3D, tex_name: String, amount: int, life: float, speed: Vector2, spread: float,
		gravity: float, size: Vector2, grow: float, ramp: Gradient, additive: bool) -> GPUParticles3D:
	var p := GPUParticles3D.new()
	p.amount = amount
	p.lifetime = life
	p.one_shot = true
	p.explosiveness = 0.95
	p.randomness = 0.5
	var pm := ParticleProcessMaterial.new()
	pm.direction = Vector3.UP
	pm.spread = spread
	pm.initial_velocity_min = speed.x
	pm.initial_velocity_max = speed.y
	pm.gravity = Vector3(0, -gravity, 0)
	pm.damping_min = 0.5
	pm.damping_max = 2.0
	pm.scale_min = size.x
	pm.scale_max = size.y
	pm.angle_min = 0.0
	pm.angle_max = 360.0
	var curve := Curve.new()
	curve.add_point(Vector2(0, 1.0 / maxf(grow, 1.0)))
	curve.add_point(Vector2(1, 1.0 if grow >= 1.0 else grow))
	var ct := CurveTexture.new()
	ct.curve = curve
	pm.scale_curve = ct
	var gt := GradientTexture1D.new()
	gt.gradient = ramp
	pm.color_ramp = gt
	p.process_material = pm
	var quad := QuadMesh.new()
	quad.size = Vector2.ONE
	var mat := StandardMaterial3D.new()
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mat.billboard_mode = BaseMaterial3D.BILLBOARD_PARTICLES
	mat.billboard_keep_scale = true   # else the particle's scale is dropped and every quad is 1 m
	mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	mat.blend_mode = BaseMaterial3D.BLEND_MODE_ADD if additive else BaseMaterial3D.BLEND_MODE_MIX
	mat.vertex_color_use_as_albedo = true
	mat.albedo_texture = Q3.fx_texture(tex_name)
	mat.disable_receive_shadows = true
	quad.material = mat
	p.draw_pass_1 = quad
	p.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	r.add_child(p)
	p.emitting = true
	return p


static func _ramp(colors: Array) -> Gradient:
	var offsets := PackedFloat32Array()
	for i in colors.size():
		offsets.append(float(i) / float(maxi(colors.size() - 1, 1)))
	var g := Gradient.new()
	g.offsets = offsets
	g.colors = PackedColorArray(colors)
	return g


## CG_ImpactMark: a mark projected onto the surface under the root (its -Y); radius in id units.
## Energy marks fade out by 3 s, the rest stay 10 s and fade over the last second.
static func decal(r: Q3FxTimed, tex_name: String, radius: float, energy := false, tint := Color.WHITE) -> Decal:
	var d := Decal.new()
	var s := radius * 2.0 * Q3.UNIT
	d.size = Vector3(s, 16.0 * Q3.UNIT, s)
	d.texture_albedo = Q3.fx_texture(tex_name)
	d.modulate = tint
	d.upper_fade = 0.3
	d.lower_fade = 0.3
	d.rotation.y = randf() * TAU
	r.add_child(d)
	r.decals.append(d)
	var total := ENERGY_MARK_TIME if energy else MARK_TOTAL_TIME
	r.linger = maxf(r.linger, total - r.life)
	r.decal_fade = 1.7 if energy else MARK_FADE_TIME
	_marks.append(d)
	while _marks.size() > MAX_MARKS:
		var old: Variant = _marks.pop_front()   # marks that already faded out are freed objects
		if is_instance_valid(old):
			(old as Decal).queue_free()
	return d


## CG_MissileHitWall's visuals for one kind: the explosion (sprite or model), its dynamic light
## and the impact mark. `tint` colors the explosion (the rail's color1), `mark_tint` the mark
## (the rail's color2). Sounds are the caller's (Q3Client).
static func explosion(parent: Node, kind: String, pos: Vector3, normal := Vector3.UP, tint := Color.WHITE,
		mark_tint := Color.WHITE) -> Q3FxTimed:
	var d: Dictionary = Q3.fx.get("explosions", {}).get(kind, {})
	var r := _root(parent, pos, normal)
	r.life = maxf(float(d.get("duration", 600)), 1.0) / 1000.0
	if d.get("sprite") and kind in SPRITE_EXPLOSIONS:
		# LE_SPRITE_EXPLOSION: 16 units off the wall, radius 30 -> 72 over its life
		var s := sprite(r, d["sprite"], 72.0, tint, 1.0)
		s.position = Vector3.UP * 16.0 * Q3.UNIT
		(s.material_override as ShaderMaterial).set_shader_parameter("grow_from", 30.0 / 72.0)
	elif d.get("model") and d.get("sprite"):
		model_flash(r, d["model"], d["sprite"], tint)
	if float(d.get("light", 0)) > 0.0:
		var lc: Array = d["light_color"]
		light(r, float(d["light"]), Color(lc[0], lc[1], lc[2]), 2.5)
		r.light_hold = true
	if d.get("mark") and Settings.get_value("graphics/decals"):
		decal(r, d["mark"], float(d["mark_radius"]), String(d["mark"]) == "mark_energy", mark_tint)
	return r


## CG_RailTrail: the core from the muzzle (4 units lower) to the end in color1, fading over
## cg_railTrailTime 400 ms, and the spiral of discs in color2 starting 20 units out.
static func rail(parent: Node, from: Vector3, to: Vector3, color1 := Color(1, 0, 0), color2 := Color(1, 0, 1)) -> Q3FxTimed:
	var cfg: Dictionary = Q3.fx["rail_trail"]
	var r := Q3FxTimed.new()
	parent.add_child(r)
	r.life = float(cfg["core_life"]) / 1000.0
	r.linger = 1.2
	var start := from + Vector3.DOWN * 4.0 * Q3.UNIT
	r.materials.append(_beam(r, start, to, cfg["core"], 6.0, color1 * 0.75, Vector2.ZERO, 0.0, {}))
	var rings := MultiMeshInstance3D.new()
	rings.set_script(load("res://scripts/fx/fx_rail_rings.gd"))
	r.add_child(rings)
	rings.setup(start + (to - start).normalized() * float(cfg["core_start"]) * Q3.UNIT, to, cfg, color2)
	return r


## A lightning beam that lasts `seconds` (showcase); the game holds a Q3LightningBeam instead.
static func lightning(parent: Node, from: Vector3, to: Vector3, seconds := 1.0) -> Q3FxTimed:
	var r := Q3FxTimed.new()
	parent.add_child(r)
	r.life = seconds
	var beam := Q3LightningBeam.new()
	r.add_child(beam)
	beam.aim(from, to, true)
	return r


## Material for the lightning gun's bolt (shader lightningBoltNew), shared by Q3LightningBeam.
static func lightning_material() -> ShaderMaterial:
	var cfg: Dictionary = Q3.fx["lightning_beam"]
	var mat := ShaderMaterial.new()
	mat.shader = load(SCROLL_ADD)
	mat.set_shader_parameter("tex", Q3.fx_texture(cfg["sprite"]))
	mat.set_shader_parameter("color", Color.WHITE)
	mat.set_shader_parameter("intensity", 1.0)
	var sc: Array = cfg["scroll"]
	var wv: Array = cfg["wave_hz"]
	mat.set_shader_parameter("scale_a", Vector2(2, 1))
	mat.set_shader_parameter("scroll_a", Vector2(sc[0], 0))
	mat.set_shader_parameter("wave_a", float(wv[0]))
	mat.set_shader_parameter("amp_a", 5.1)
	mat.set_shader_parameter("layer_b", 1.0)
	mat.set_shader_parameter("scale_b", Vector2(-1.3, -1))
	mat.set_shader_parameter("scroll_b", Vector2(sc[1], 0))
	mat.set_shader_parameter("wave_b", float(wv[1]))
	mat.set_shader_parameter("amp_b", 8.3)
	return mat


## Crossed quads from `from` to `to` (width in id units) with the scrolling shader; the texture
## repeats every 256 units along the beam, as tr_surface.c DoRailCore maps it.
static func _beam(r: Node3D, from: Vector3, to: Vector3, tex_name: String, width: float, color: Color,
		scroll: Vector2, wave: float, layer_b: Dictionary) -> ShaderMaterial:
	var mat := ShaderMaterial.new()
	mat.shader = load(SCROLL_ADD)
	mat.set_shader_parameter("tex", Q3.fx_texture(tex_name))
	mat.set_shader_parameter("color", color)
	mat.set_shader_parameter("intensity", 1.5)
	mat.set_shader_parameter("scroll_a", scroll)
	mat.set_shader_parameter("wave_a", wave)
	var length := from.distance_to(to)
	mat.set_shader_parameter("repeat", Vector2(maxf(length / Q3.UNIT / 256.0, 0.01), 1.0))
	if not layer_b.is_empty():
		mat.set_shader_parameter("layer_b", 1.0)
		for k in layer_b:
			mat.set_shader_parameter(k, layer_b[k])
	var holder := Node3D.new()
	r.add_child(holder)
	holder.global_transform = beam_transform(from, to)
	for i in 2:
		var mi := MeshInstance3D.new()
		var quad := QuadMesh.new()
		quad.size = Vector2(length, width * Q3.UNIT)
		mi.mesh = quad
		mi.material_override = mat
		mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		mi.rotation.x = PI * 0.5 * float(i)
		holder.add_child(mi)
	return mat


## A transform at the middle of from..to with +X along it.
static func beam_transform(from: Vector3, to: Vector3) -> Transform3D:
	var axis := (to - from).normalized()
	if axis == Vector3.ZERO:
		axis = Vector3.RIGHT
	var ref := Vector3.UP if absf(axis.dot(Vector3.UP)) < 0.95 else Vector3.RIGHT
	var y := ref.cross(axis).normalized()
	return Transform3D(Basis(axis, y, axis.cross(y)), (from + to) * 0.5)


## A flash light alone (showcase). In the game the flash is the weapon's own (Q3WeaponFx).
static func muzzle_flash(parent: Node, weapon: String, pos: Vector3) -> Q3FxTimed:
	var mf: Dictionary = Q3.fx["muzzle_flash"]
	var c: Array = mf.get(weapon, [1, 1, 1])
	var r := _root(parent, pos)
	r.life = 0.06
	light(r, float(mf["_light_radius"]) + randf() * 31.0, Color(c[0], c[1], c[2]), 6.0)
	return r


## cg_ents.c CG_Missile's look for a missile the game moves: the rocket, grenade and BFG models
## (spun about their flight by the caller), the plasma ball sprite (radius 16, tcMod rotate 931),
## the rocket's 200-unit light and the fly loop (rockfly, lasfly). Trails are the caller's.
static func missile(parent: Node, weapon: int) -> Node3D:
	var p := Node3D.new()
	parent.add_child(p)
	var pr: Dictionary = Q3.fx["projectiles"]
	var d: Dictionary = {}
	match weapon:
		5: d = pr["rocket"]
		4: d = pr["grenade"]
		8: d = pr["plasma"]
		9: d = pr["bfg"]
	if weapon == 8:
		var holder := Q3FxTimed.new()
		holder.life = 1.0e6
		p.add_child(holder)
		var s := sprite(holder, "plasma_ball", 16.0)
		(s.material_override as ShaderMaterial).set_shader_parameter("spin_rate", deg_to_rad(931.0))
	elif d.has("model"):
		var spin := Node3D.new()
		spin.name = "Spin"
		p.add_child(spin)
		var model := Q3.instance_prop(d["model"])
		if model:
			spin.add_child(model)
			for mi in model.find_children("*", "GeometryInstance3D", true, false):
				(mi as GeometryInstance3D).cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	if float(d.get("light", 0)) > 0.0:
		var lc: Array = d["light_color"]
		var l := OmniLight3D.new()
		l.omni_range = float(d["light"]) * Q3.UNIT
		l.light_color = Color(lc[0], lc[1], lc[2])
		l.light_energy = 3.0
		l.shadow_enabled = false
		p.add_child(l)
	var fly := {5: "rocket/rockfly", 8: "plasma/lasfly", 9: "rocket/rockfly"}
	if fly.has(weapon):
		var path := "res://library/sounds/weapons/%s.wav" % fly[weapon]
		if ResourceLoader.exists(path):
			var a := AudioStreamPlayer3D.new()
			a.stream = Q3FX.looped(load(path))
			a.unit_size = 12.0
			a.max_distance = 60.0
			a.bus = "SFX" if AudioServer.get_bus_index("SFX") >= 0 else "Master"
			a.autoplay = true
			p.add_child(a)
	return p


## A looping copy of a sound (trap_S_AddLoopingSound plays the whole file over and over).
static func looped(stream: AudioStream) -> AudioStream:
	if stream is AudioStreamWAV:
		var w := (stream as AudioStreamWAV).duplicate() as AudioStreamWAV
		w.loop_mode = AudioStreamWAV.LOOP_FORWARD
		w.loop_begin = 0
		w.loop_end = int(w.get_length() * w.mix_rate)
		return w
	return stream


## Showcase rockets and plasma that fly themselves to `to` and explode there.
static func rocket(parent: Node, from: Vector3, to: Vector3) -> Node3D:
	var d: Dictionary = Q3.fx["projectiles"]["rocket"]
	var p := _projectile(parent, from, to, 900.0, "rocket")
	var model := Q3.instance_prop(d["model"])
	if model:
		p.add_child(model)
	var lc: Array = d["light_color"]
	var l := OmniLight3D.new()
	l.omni_range = float(d["light"]) * Q3.UNIT
	l.light_color = Color(lc[0], lc[1], lc[2])
	l.light_energy = 3.0
	p.add_child(l)
	var trail := particles(p, "smoke_puff", int(float(d["trail_life"]) / float(d["trail_step"])), float(d["trail_life"]) / 1000.0,
			Vector2(0.0, 0.2), 180.0, -0.3, Vector2.ONE * float(d["trail_radius"]) * Q3.UNIT * 0.5, 2.0,
			_ramp([Color(0.8, 0.78, 0.75, float(d["trail_alpha"])), Color(0.6, 0.6, 0.6, 0.0)]), false)
	trail.one_shot = false
	trail.explosiveness = 0.0
	trail.local_coords = false
	p.trail = trail
	return p


static func plasma(parent: Node, from: Vector3, to: Vector3) -> Node3D:
	var p := _projectile(parent, from, to, 2000.0, "plasma")
	var holder := Q3FxTimed.new()
	holder.life = 1000.0
	p.add_child(holder)
	var s := sprite(holder, "plasma_ball", 16.0, Color.WHITE, 1.0)
	(s.material_override as ShaderMaterial).set_shader_parameter("spin_rate", deg_to_rad(931.0))
	return p


static func _projectile(parent: Node, from: Vector3, to: Vector3, speed_units: float, kind: String) -> Node3D:
	var p := Node3D.new()
	p.set_script(load("res://scripts/fx/fx_projectile.gd"))
	parent.add_child(p)
	p.global_position = from
	p.target = to
	p.speed = speed_units * Q3.UNIT
	p.explode_kind = kind
	return p


## CG_Tracer: a 100-unit streak of the bullet's path, 2 units wide, facing the view, for one frame.
static func tracer(parent: Node, from: Vector3, to: Vector3, eye: Vector3) -> void:
	var cfg: Dictionary = Q3.fx["tracer"]
	var u := Q3.UNIT
	var dir := to - from
	var length := dir.length()
	if length < 100.0 * u:
		return
	dir /= length
	var begin := 50.0 * u + randf() * (length - 60.0 * u)
	var end := minf(begin + float(cfg["length"]) * u, length)
	var a := from + dir * begin
	var b := from + dir * end
	var r := Q3FxTimed.new()
	parent.add_child(r)
	r.life = 0.02
	var mid := (a + b) * 0.5
	var to_eye := eye - mid
	var z := (to_eye - dir * to_eye.dot(dir)).normalized()
	if z == Vector3.ZERO:
		z = Vector3.UP
	var mat := ShaderMaterial.new()
	mat.shader = load(SCROLL_ADD)
	mat.set_shader_parameter("tex", Q3.fx_texture(cfg["sprite"]))
	mat.set_shader_parameter("intensity", 1.0)
	var mi := MeshInstance3D.new()
	var quad := QuadMesh.new()
	quad.size = Vector2(end - begin, float(cfg["width"]) * 2.0 * u)
	mi.mesh = quad
	mi.material_override = mat
	mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	r.add_child(mi)
	mi.global_transform = Transform3D(Basis(dir, z.cross(dir), z), mid)


## CG_MachineGunEjectBrass / CG_ShotgunEjectBrass: shells thrown out of the gun's right side,
## tumbling and bouncing (0.4 / 0.3) for cg_brassTime. `at` is the player origin, `axis` its view
## axes as Q3Client.angles_basis gives them (columns: id left, up, forward).
static func brass(parent: Node, kind: String, at: Vector3, axis: Basis) -> void:
	var d: Dictionary = Q3.fx["brass"].get(kind, {})
	if d.is_empty():
		return
	var mat := PhysicsMaterial.new()
	mat.bounce = float(d["bounce"])
	mat.friction = 0.6
	var off: Array = d["offset"]
	var life: Array = d["life"]
	for i in int(d.get("count", 1)):
		var cr := func() -> float: return randf_range(-1.0, 1.0)
		# id velocity (forward, left, up) in the view axes
		var local := Vector3(0.0, -50.0 + 40.0 * cr.call(), 100.0 + 50.0 * cr.call())
		if kind == "shotgun":
			local = Vector3(60.0 + 60.0 * cr.call(), (40.0 if i == 0 else -40.0) + 10.0 * cr.call(), 100.0 + 50.0 * cr.call())
		var to_world := func(o: Vector3) -> Vector3:
			return axis.z * o.x + axis.x * o.y + axis.y * o.z
		var body := RigidBody3D.new()
		body.physics_material_override = mat
		body.collision_layer = 4
		body.collision_mask = 1
		body.mass = 0.05
		var shape := CollisionShape3D.new()
		var box := BoxShape3D.new()
		box.size = Vector3(1.0, 1.0, 2.5) * Q3.UNIT
		shape.shape = box
		body.add_child(shape)
		var model := Q3.instance_prop(d["model"])
		if model:
			body.add_child(model)
			for mi in model.find_children("*", "GeometryInstance3D", true, false):
				(mi as GeometryInstance3D).cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		parent.add_child(body)
		body.global_position = at + to_world.call(Vector3(float(off[0]), float(off[1]), float(off[2]))) * Q3.UNIT
		body.global_basis = Basis.from_euler(Vector3(randf() * 0.5, randf() * 0.5, randf() * 0.5))
		body.linear_velocity = to_world.call(local) * Q3.UNIT
		body.angular_velocity = axis * Vector3(randf_range(-12, 12), randf_range(-12, 12), randf_range(-6, 6))
		var t := parent.get_tree().create_timer(randf_range(float(life[0]), float(life[1])) / 1000.0)
		t.timeout.connect(body.queue_free)


## CG_Bleed: the blood sprite, 24 units, 500 ms.
static func blood(parent: Node, pos: Vector3) -> Q3FxTimed:
	var d: Dictionary = Q3.fx["blood"]
	var r := _root(parent, pos)
	r.life = float(d["duration"]) / 1000.0
	sprite(r, d["sprite"], float(d["radius"]))
	return r


## CG_GibPlayer: 10 gibs, xy = crandom * 250, z = 250 + crandom * 250 units/s, bounce 0.6.
static func gibs(parent: Node, pos: Vector3) -> void:
	var d: Dictionary = Q3.fx["gibs"]
	var mat := PhysicsMaterial.new()
	mat.bounce = float(d["bounce"])
	mat.friction = 0.7
	var life: Array = d["life"]
	for id in d["models"]:
		var body := RigidBody3D.new()
		body.physics_material_override = mat
		body.collision_layer = 4
		body.collision_mask = 1
		var shape := CollisionShape3D.new()
		var sph := SphereShape3D.new()
		sph.radius = 5.0 * Q3.UNIT
		shape.shape = sph
		body.add_child(shape)
		var model := Q3.instance_prop(id)
		if model:
			body.add_child(model)
		parent.add_child(body)
		body.global_position = pos + Vector3(randf_range(-0.2, 0.2), 0.3, randf_range(-0.2, 0.2))
		var v := Vector3(randf_range(-1, 1) * 250.0, randf_range(-1, 1) * 250.0, 250.0 + randf_range(-1, 1) * 250.0)
		body.linear_velocity = Q3.q3_to_godot(v)
		body.angular_velocity = Vector3(randf_range(-8, 8), randf_range(-8, 8), randf_range(-8, 8))
		var timer := parent.get_tree().create_timer(randf_range(float(life[0]), float(life[1])) / 1000.0)
		timer.timeout.connect(body.queue_free)
	blood(parent, pos + Vector3.UP * 0.6)


## CG_SpawnEffect: the teleport model drawn with the scrolling teleportEffect shader, 500 ms.
static func teleport(parent: Node, pos: Vector3) -> Q3FxTimed:
	var d: Dictionary = Q3.fx["teleport"]
	var r := _root(parent, pos)
	r.life = float(d["duration"]) / 1000.0
	var model := Q3.instance_prop(d["model"])
	if model:
		r.add_child(model)
		var mat := ShaderMaterial.new()
		mat.shader = load(SCROLL_ADD)
		mat.set_shader_parameter("tex", Q3.fx_texture(d["sprite"]))
		var sc: Array = d["scale"]
		var sp: Array = d["scroll"]
		mat.set_shader_parameter("scale_a", Vector2(sc[0], sc[1]))
		mat.set_shader_parameter("scroll_a", Vector2(sp[0], sp[1]))
		mat.set_shader_parameter("intensity", 1.6)
		for m in model.find_children("*", "MeshInstance3D", true, false):
			(m as MeshInstance3D).material_override = mat
			(m as MeshInstance3D).cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		r.materials.append(mat)
	return r


## The remaster's item pickup burst (Quake 3 only stops drawing the item): a flare swelling and
## fading in the item's color, a spray of sparks going up, and a short light.
static func pickup(parent: Node, pos: Vector3, tint: Color) -> Q3FxTimed:
	var r := _root(parent, pos)
	r.life = 0.35
	r.linger = 0.6
	var flare := sprite(r, "flare", 40.0, tint, 3.0)
	var mat := flare.material_override as ShaderMaterial
	mat.set_shader_parameter("grow_from", 0.35)
	mat.set_shader_parameter("fade_start", 0.2)
	particles(r, "flare", 26, 0.6, Vector2(1.5, 4.5), 70.0, 7.0, Vector2(0.09, 0.17), 0.2,
		_ramp([Color(1, 1, 1, 1), tint, Color(tint.r, tint.g, tint.b, 0.0)]), true)
	light(r, 160.0, tint, 2.5)
	return r


## Overlay material for powerup shells: quad, battlesuit, regen, invis.
static func shell_material(kind: String, on_weapon := false) -> ShaderMaterial:
	var d: Dictionary = Q3.fx["powerup_shells"][kind]
	var mat := ShaderMaterial.new()
	mat.shader = load(SHELL)
	mat.set_shader_parameter("tex", Q3.fx_texture(d["map"]))
	mat.set_shader_parameter("grow", float(d["grow_weapon"] if on_weapon else d["grow"]) * Q3.UNIT)
	mat.set_shader_parameter("rotate_deg", float(d["rotate"]))
	var sc: Array = d["scroll"]
	mat.set_shader_parameter("scroll", Vector2(sc[0], sc[1]))
	mat.set_shader_parameter("intensity", 0.9)
	return mat
