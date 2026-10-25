class_name Q3LightningBeam
extends Node3D
## The lightning gun's bolt while fire is held (cg_weapons.c CG_LightningBolt, tr_surface.c
## RB_SurfaceLightningBolt): four quads 8 units wide turned 45° apart about the beam, from the
## weapon's flash to where the shot stops, and where it hits something, the crackle model 16 units
## back from the hit, turned at random every frame. Call aim() every frame it should show.
## The remaster's options (after WolfcamQL's): Graphics > Lightning width scales the bolt, and
## Lightning beam "glow" wraps it in a flickering halo with a flare and a light where it hits.

const GLOW_COLOR := Color(0.45, 0.6, 1.0)

var _holder: Node3D
var _quads: Array[MeshInstance3D] = []
var _mat: ShaderMaterial
var _crackle: Node3D
var _glow: Node3D                 ## the halo's holder (glow style)
var _glow_mat: ShaderMaterial
var _flare: MeshInstance3D        ## the hit's flare and light (glow style)
var _light: OmniLight3D


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
	_build_glow()


func _build_glow() -> void:
	_glow = Node3D.new()
	add_child(_glow)
	_glow_mat = Q3FX.glow_beam(_glow, Vector3.ZERO, Vector3.RIGHT, 1.0, GLOW_COLOR,
		{"core": 0.18, "core_gain": 0.6, "halo_gain": 0.45, "flicker": 0.55})
	_flare = MeshInstance3D.new()
	var quad := QuadMesh.new()
	quad.size = Vector2.ONE * 40.0 * Q3.UNIT
	_flare.mesh = quad
	var mat := ShaderMaterial.new()
	mat.shader = Q3.held(Q3FX.SPRITE_ADD)
	mat.set_shader_parameter("tex", Q3.fx_texture("flare"))
	mat.set_shader_parameter("tint", GLOW_COLOR)
	mat.set_shader_parameter("intensity", 1.6 * Q3FX.gain())
	mat.set_shader_parameter("fade_start", 1.0)
	mat.set_shader_parameter("spin_rate", 6.0)
	_flare.material_override = mat
	_flare.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	add_child(_flare)
	_light = OmniLight3D.new()
	_light.light_color = GLOW_COLOR
	_light.omni_range = 140.0 * Q3.UNIT
	_light.shadow_enabled = false
	add_child(_light)


## from: the muzzle; to: the trace end; hit: the trace stopped on something (fraction < 1).
func aim(from: Vector3, to: Vector3, hit: bool) -> void:
	if _holder == null:
		_ready()
	var length := from.distance_to(to)
	var width := Q3FX.LIGHTNING_WIDTH * float(Settings.get_value("graphics/lightning_width"))
	_holder.global_transform = Q3FX.beam_transform(from, to)
	for mi in _quads:
		(mi.mesh as QuadMesh).size = Vector2(maxf(length, 0.001), width * Q3.UNIT)
	_mat.set_shader_parameter("repeat", Vector2(maxf(length / Q3.UNIT / 256.0, 0.01), 1.0))
	if _crackle:
		_crackle.visible = hit
		if hit:
			var back := float(Q3.fx["lightning_beam"]["impact_back"]) * Q3.UNIT
			_crackle.global_position = to - (to - from).normalized() * back
			_crackle.global_basis = Basis.from_euler(Vector3(randf() * TAU, randf() * TAU, randf() * TAU))
	var glow: bool = Settings.get_value("graphics/lightning_style") == "glow"
	_glow.visible = glow
	_flare.visible = glow and hit
	_light.visible = glow and hit
	if not glow:
		return
	Q3FX.glow_beam_size(_glow.get_child(0) as Node3D, _glow_mat, from, to, width * 3.0)
	if hit:
		var back := 4.0 * Q3.UNIT
		_flare.global_position = to - (to - from).normalized() * back
		# the billboard drops the node's scale: its size is the shader's
		var s := randf_range(0.8, 1.15) * width / Q3FX.LIGHTNING_WIDTH
		(_flare.material_override as ShaderMaterial).set_shader_parameter("size", s)
		_light.global_position = _flare.global_position
		_light.light_energy = randf_range(1.6, 2.4) * Q3FX.gain()
