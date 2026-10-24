class_name Q3Puffs
extends MultiMeshInstance3D
## cgame's smoke-like local entities, all of one texture in one MultiMesh (cg_effects.c
## CG_SmokePuff, cg_localents.c CG_AddScaleFade / CG_AddMoveScaleFade): each puff fades its alpha
## from `alpha` to 0 over its life and, unless `grow` is off (LEF_PUFF_DONT_SCALE), grows from 8
## units to radius + 8. A puff the camera would sit inside is dropped, as in Quake 3.
##   var smoke := Q3Puffs.create(self, "smoke_puff")
##   smoke.add(pos, Vector3.ZERO, 64.0, 0.33, 2000.0, now_ms)

const CAPACITY := 512

var _puffs: Array[Dictionary] = []


static func create(parent: Node, tex_name: String) -> Q3Puffs:
	var p := Q3Puffs.new()
	var mm := MultiMesh.new()
	mm.transform_format = MultiMesh.TRANSFORM_3D
	mm.use_colors = true
	mm.use_custom_data = true
	var quad := QuadMesh.new()
	quad.size = Vector2.ONE
	mm.mesh = quad
	mm.instance_count = CAPACITY
	mm.visible_instance_count = 0
	p.multimesh = mm
	var mat := ShaderMaterial.new()
	mat.shader = load("res://shaders/fx_puff.gdshader")
	mat.set_shader_parameter("tex", Q3.fx_texture(tex_name))
	p.material_override = mat
	p.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	p.extra_cull_margin = 16384.0   # instances spread over the whole map
	parent.add_child(p)
	return p


## pos and vel in meters (vel per second); radius in id units; life and born in ms.
func add(pos: Vector3, vel: Vector3, radius: float, alpha: float, life_ms: float, born_ms: float,
		grow := true, tint := Color.WHITE) -> void:
	if _puffs.size() >= CAPACITY:
		_puffs.pop_front()
	_puffs.append({"pos": pos, "vel": vel, "radius": radius * Q3.UNIT, "alpha": alpha, "born": born_ms,
		"end": born_ms + life_ms, "grow": grow, "tint": tint, "rot": randf() * TAU})


## Every puff gone at once (a replay jumped: puffs born later would hang in the air).
func clear() -> void:
	_puffs.clear()
	multimesh.visible_instance_count = 0


func step(now_ms: float, eye: Vector3) -> void:
	var mm := multimesh
	var n := 0
	var keep: Array[Dictionary] = []
	for p in _puffs:
		if now_ms >= float(p["end"]):
			continue
		var life: float = float(p["end"]) - float(p["born"])
		var c := clampf((float(p["end"]) - now_ms) / life, 0.0, 1.0)   # 1 at birth, 0 at the end
		var at: Vector3 = p["pos"] + p["vel"] * (maxf(now_ms - float(p["born"]), 0.0) * 0.001)
		var radius: float = p["radius"]
		if at.distance_to(eye) < radius:
			continue   # the view would be inside it
		keep.append(p)
		var r := radius * (1.0 - c) + 8.0 * Q3.UNIT if p["grow"] else radius
		mm.set_instance_transform(n, Transform3D(Basis(), at))
		mm.set_instance_color(n, p["tint"])
		mm.set_instance_custom_data(n, Color(float(p["alpha"]) * c, r, p["rot"], 0.0))
		n += 1
	_puffs = keep
	mm.visible_instance_count = n
