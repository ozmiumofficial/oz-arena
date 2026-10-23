class_name Q3LightningBeam
extends Node3D
## The lightning gun's bolt while fire is held (cg_weapons.c CG_LightningBolt, tr_surface.c
## RB_SurfaceLightningBolt): four quads 8 units wide turned 45° apart about the beam, from the
## weapon's flash to where the shot stops, and where it hits something, the crackle model 16 units
## back from the hit, turned at random every frame. Call aim() every frame it should show.

var _holder: Node3D
var _quads: Array[MeshInstance3D] = []
var _mat: ShaderMaterial
var _crackle: Node3D


func _ready() -> void:
	_mat = Q3FX.lightning_material()
	_holder = Node3D.new()
	add_child(_holder)
	for i in 4:
		var mi := MeshInstance3D.new()
		var quad := QuadMesh.new()
		quad.size = Vector2(1.0, Q3FX.LIGHTNING_WIDTH * Q3.UNIT)
		mi.mesh = quad
		mi.material_override = _mat
		mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		mi.rotation.x = PI * 0.25 * float(i)
		_holder.add_child(mi)
		_quads.append(mi)
	_crackle = Q3.instance_prop(String(Q3.fx["lightning_beam"]["impact_model"]))
	if _crackle:
		add_child(_crackle)
		for mi in _crackle.find_children("*", "GeometryInstance3D", true, false):
			(mi as GeometryInstance3D).cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF


## from: the muzzle; to: the trace end; hit: the trace stopped on something (fraction < 1).
func aim(from: Vector3, to: Vector3, hit: bool) -> void:
	if _holder == null:
		_ready()
	var length := from.distance_to(to)
	_holder.global_transform = Q3FX.beam_transform(from, to)
	for mi in _quads:
		(mi.mesh as QuadMesh).size = Vector2(maxf(length, 0.001), Q3FX.LIGHTNING_WIDTH * Q3.UNIT)
	_mat.set_shader_parameter("repeat", Vector2(maxf(length / Q3.UNIT / 256.0, 0.01), 1.0))
	if _crackle:
		_crackle.visible = hit
		if hit:
			var back := float(Q3.fx["lightning_beam"]["impact_back"]) * Q3.UNIT
			_crackle.global_position = to - (to - from).normalized() * back
			_crackle.global_basis = Basis.from_euler(Vector3(randf() * TAU, randf() * TAU, randf() * TAU))
