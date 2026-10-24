class_name Q3Sparks
extends MultiMeshInstance3D
## The remaster's impact sparks (Quake 3 had none): hot streaks thrown off bullet, rail and
## explosion hits that fall, bounce off the map and cool from white through orange to dark red.
## They fly in id units and bounce off the map through the game's own collision (Q3Server.trace
## against solid, the same world the shots hit), so they never pass through walls or floors.
## All of them are one MultiMesh of streaks stretched along their motion and turned to the camera.
##   var sparks := Q3Sparks.create(self)
##   sparks.burst(at, normal, 6, 320.0)   # Godot position and normal; speed in id units/s
##   sparks.step(delta, eye)              # every frame

const CAPACITY := 384
const GRAVITY := 800.0      ## g_gravity, units/s^2
const BOUNCE := 0.45        ## share of the speed into the surface kept on a bounce
const FRICTION := 0.7       ## share of the speed along the surface kept on a bounce
const MIN_SPEED := 40.0     ## units/s: slower after a bounce and the spark dies
const STREAK := 0.025       ## seconds of travel a streak shows
const WIDTH := 0.9          ## units
const MASK_SOLID := 1
const ENTITYNUM_NONE := 1023
## Graphics > Particles: share of the sparks each burst throws
const AMOUNT := {"low": 0.35, "medium": 0.65, "high": 1.0}

var _sparks: Array[Dictionary] = []


static func create(parent: Node) -> Q3Sparks:
	var s := Q3Sparks.new()
	var mm := MultiMesh.new()
	mm.transform_format = MultiMesh.TRANSFORM_3D
	mm.use_colors = true
	var quad := QuadMesh.new()
	quad.size = Vector2.ONE
	mm.mesh = quad
	mm.instance_count = CAPACITY
	mm.visible_instance_count = 0
	s.multimesh = mm
	var mat := ShaderMaterial.new()
	mat.shader = load("res://shaders/fx_spark.gdshader")
	s.material_override = mat
	s.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	s.extra_cull_margin = 16384.0   # instances spread over the whole map
	parent.add_child(s)
	return s


## `count` sparks from `at` (Godot meters) thrown off the surface with normal `normal` (Godot
## axes), `spread` 0..1 from straight out to any direction off the surface; speed in units/s,
## life in seconds.
func burst(at: Vector3, normal: Vector3, count: int, speed: float, life := 0.5, spread := 0.8) -> void:
	count = int(round(count * float(AMOUNT.get(Settings.get_value("graphics/particles"), 1.0))))
	var n := Vector3(normal.z, normal.x, normal.y).normalized()   # id axes
	if n == Vector3.ZERO:
		n = Vector3(0, 0, 1)
	var pos := Vector3(at.z, at.x, at.y) / Q3.UNIT + n * 2.0
	for i in count:
		var d := (n + Vector3(randf_range(-1, 1), randf_range(-1, 1), randf_range(-1, 1)).normalized() * spread * randf()).normalized()
		if d.dot(n) < 0.05:
			d = (d - n * (d.dot(n) - 0.05)).normalized()
		if _sparks.size() >= CAPACITY:
			_sparks.pop_front()
		var l := life * randf_range(0.6, 1.4)
		_sparks.append({"pos": pos, "vel": d * speed * randf_range(0.5, 1.2), "age": 0.0, "life": l})


## Every spark gone at once (a replay jumped).
func clear() -> void:
	_sparks.clear()
	multimesh.visible_instance_count = 0


func step(delta: float, eye: Vector3) -> void:
	var mm := multimesh
	var gain := float(Settings.get_value("graphics/effect_intensity"))
	var keep: Array[Dictionary] = []
	var n := 0
	for s in _sparks:
		s["age"] = float(s["age"]) + delta
		if float(s["age"]) >= float(s["life"]):
			continue
		var vel: Vector3 = s["vel"]
		vel.z -= GRAVITY * delta
		var from: Vector3 = s["pos"]
		var to := from + vel * delta
		var tr := Q3Server.trace(from, to, Vector3.ZERO, Vector3.ZERO, ENTITYNUM_NONE, MASK_SOLID)
		if not tr.is_empty() and float(tr["fraction"]) < 1.0:
			if bool(tr["startsolid"]):
				continue
			var nrm: Vector3 = tr["normal"]
			var into := vel.dot(nrm)
			vel = (vel - nrm * into) * FRICTION - nrm * into * BOUNCE
			to = (tr["endpos"] as Vector3) + nrm * 0.25
			if vel.length() < MIN_SPEED:
				continue
		s["pos"] = to
		s["vel"] = vel
		keep.append(s)
		# a streak from where it was STREAK ago to where it is, turned to face the eye
		var p := Q3.q3_to_godot(to)
		var along := Q3.q3_to_godot(vel) * STREAK
		if along.length() < WIDTH * Q3.UNIT:
			along = along.normalized() * WIDTH * Q3.UNIT if along != Vector3.ZERO else Vector3.UP * WIDTH * Q3.UNIT
		var side := along.cross(eye - p).normalized() * WIDTH * Q3.UNIT
		if side == Vector3.ZERO:
			continue
		mm.set_instance_transform(n, Transform3D(Basis(along, side, along.cross(side).normalized()), p - along * 0.5))
		var heat := 1.0 - float(s["age"]) / float(s["life"])   # 1 hot .. 0 cold
		var c := Color(1.0, 0.85, 0.55).lerp(Color(1.0, 0.45, 0.12), 1.0 - heat).lerp(Color(0.6, 0.1, 0.03), clampf(0.5 - heat, 0.0, 0.5) * 2.0)
		c.a = heat * gain
		mm.set_instance_color(n, c)
		n += 1
	_sparks = keep
	mm.visible_instance_count = n
