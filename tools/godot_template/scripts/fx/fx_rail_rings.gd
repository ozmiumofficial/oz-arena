extends MultiMeshInstance3D
## The railgun spiral (CG_RailTrail): one disc every SPACING units along the shot, each set
## RADIUS units off the line on a spiral that turns ROTATION_STEP degrees per disc, drifting
## outward at `speed` and fading over (i >> 1) + 600 ms.

var origins: PackedVector3Array = []
var dirs: PackedVector3Array = []
var lifetimes: PackedFloat32Array = []
var speed := 6.0 * Q3.UNIT
var t := 0.0


func setup(from: Vector3, to: Vector3, cfg: Dictionary, color: Color) -> void:
	var u := Q3.UNIT
	var axis := (to - from).normalized()
	var length := from.distance_to(to)
	var side := axis.cross(Vector3.UP if absf(axis.dot(Vector3.UP)) < 0.95 else Vector3.RIGHT).normalized()
	var up := side.cross(axis).normalized()
	var count := int(length / (float(cfg["spacing"]) * u))
	var mm := MultiMesh.new()
	mm.transform_format = MultiMesh.TRANSFORM_3D
	mm.use_custom_data = true
	var quad := QuadMesh.new()
	quad.size = Vector2.ONE * float(cfg["disc_radius"]) * 2.0 * u * 2.0
	mm.mesh = quad
	mm.instance_count = count
	var j := 18  # CG_RailTrail starts the spiral at axis 18 of 36
	for i in count:
		var ang := deg_to_rad(float(j) * float(cfg["rotation_step_deg"]))
		var off := (side * cos(ang) + up * sin(ang))
		origins.append(from + axis * (float(i) * float(cfg["spacing"]) * u) + off * float(cfg["radius"]) * u)
		dirs.append(off)
		lifetimes.append((float(i >> 1) + float(cfg["disc_life"])) / 1000.0)
		mm.set_instance_transform(i, Transform3D(Basis(), origins[i]))
		mm.set_instance_custom_data(i, Color(0, randf() * TAU, 0, 0))
		j = (j + 1) % 36
	multimesh = mm
	speed = float(cfg["disc_speed"]) * u
	var mat := ShaderMaterial.new()
	mat.shader = load("res://shaders/fx_disc.gdshader")
	mat.set_shader_parameter("tex", Q3.fx_texture(cfg["disc"]))
	mat.set_shader_parameter("color", color)
	mat.set_shader_parameter("intensity", 1.6)
	material_override = mat
	cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF


func _process(delta: float) -> void:
	t += delta
	if multimesh == null:
		return
	for i in multimesh.instance_count:
		var k := clampf(t / lifetimes[i], 0.0, 1.0)
		multimesh.set_instance_transform(i, Transform3D(Basis(), origins[i] + dirs[i] * speed * t))
		var c := multimesh.get_instance_custom_data(i)
		c.r = k
		multimesh.set_instance_custom_data(i, c)
