class_name Q3WeaponFx
extends RefCounted
## One drawn weapon's moving parts, cg_weapons.c CG_AddPlayerWeapon: the muzzle flash model on
## tag_flash (shown for MUZZLE_FLASH_TIME after each shot, held while the gauntlet, lightning gun
## or grapple fires, rolled ±10° at random each frame), its dynamic light (300 + rand(0..31) units
## in the weapon's flashDlightColor) and the spinning barrel (CG_MachinegunSpinAngle). Bound to a
## weapon prop: the view weapon or the one in a player's hand.
## The railgun's glow surfaces (railgun2.glow, railgun3's glow, the scrolling railgun4) are
## Quake 3's `rgbGen entity` stages, grayscale textures the engine tints. cg_weapons.c tints your
## own gun white when ready and dark-to-magenta through the 1.5 s reload; the remaster tints it
## with the player's rail color (rail_color, as Quake Live does): dark right after a shot, filling
## up with a flicker as it recharges, a flash when ready, then a slow breathing glow.
##   var fx := Q3WeaponFx.bind(prop, weapon); fx.fire(now_ms); fx.update(now_ms, firing)

const MUZZLE_FLASH_TIME := 20.0
const SPIN_SPEED := 0.9            ## degrees per ms
const COAST_TIME := 1000.0
const CONTINUOUS := [1, 6, 10]     ## gauntlet, lightning gun, grapple: the flash stays while firing
## flashDlightColor per weapon (CG_RegisterWeapon)
const FLASH_COLORS := [Color(1, 1, 1), Color(0.6, 0.6, 1.0), Color(1, 1, 0), Color(1, 1, 0), Color(1, 0.7, 0),
	Color(1, 0.75, 0), Color(0.6, 0.6, 1.0), Color(1, 0.5, 0), Color(0.6, 0.6, 1.0), Color(1, 0.7, 1), Color(0.6, 0.6, 1.0)]

var weapon := 0
var root: Node3D
var flash: Node3D
var barrel: Node3D
var light: OmniLight3D
var muzzle_ms := -100000.0
var _flash_basis := Basis.IDENTITY
var _barrel_basis := Basis.IDENTITY
var _barrel_spinning := false
var _barrel_time := 0.0
var _barrel_angle := 0.0
var _pending := false              ## a shot whose flash hasn't been on screen yet
var _rail: Array = []              ## [material, kind]: "glow" (additive), "emit" (emission), "scroll" (emission + tcMod scroll 0 1)
var _rail_ready := true
var _rail_pop := 0.0
var _rail_scroll := 0.0
var _rail_t := -1.0
var rail_color := Color(1, 1, 1)   ## the player's color1 (Q3Client.client_colors)


static func bind(prop: Node3D, w: int) -> Q3WeaponFx:
	var fx := Q3WeaponFx.new()
	fx.weapon = w
	fx.root = prop
	fx.flash = prop.find_child("flash", true, false) as Node3D
	fx.barrel = prop.find_child("barrel", true, false) as Node3D
	if fx.flash:
		fx._flash_basis = fx.flash.basis
		fx.flash.visible = false
		for mi in fx.flash.find_children("*", "GeometryInstance3D", true, false):
			(mi as GeometryInstance3D).cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	if fx.barrel:
		fx._barrel_basis = fx.barrel.basis
	if w == 7:
		fx._bind_rail()
	fx.light = OmniLight3D.new()
	fx.light.light_color = FLASH_COLORS[w] if w >= 0 and w < FLASH_COLORS.size() else Color.WHITE
	fx.light.light_energy = 1.2
	fx.light.shadow_enabled = false
	fx.light.visible = false
	(fx.flash if fx.flash else prop).add_child(fx.light)
	return fx


func valid() -> bool:
	return root != null and is_instance_valid(root)


## CG_FireWeapon: cent->muzzleFlashTime = cg.time.
func fire(now_ms: float) -> void:
	muzzle_ms = now_ms
	_pending = true


## Per frame. `firing` is the entity's EF_FIRING.
func update(now_ms: float, firing: bool) -> void:
	if not valid():
		return
	_spin_barrel(now_ms, firing)
	# a 20 ms flash is shorter than a 60 Hz frame: every shot still gets one frame
	var on := (firing and weapon in CONTINUOUS) or now_ms - muzzle_ms <= MUZZLE_FLASH_TIME or _pending
	_pending = false
	if flash:
		flash.visible = on
		if on:
			flash.basis = _flash_basis * Basis(Vector3.BACK, deg_to_rad(randf_range(-10.0, 10.0)))
	light.visible = on
	light.light_energy = 1.2 * Q3FX.gain()
	if on:
		light.omni_range = (300.0 + float(randi() & 31)) * Q3.UNIT


## Where the flash sits (the beam and trail origin).
## Forgets shots and spin from another time (a replay jumped back: a flash stamped later would
## stay lit until the playhead got there).
func reset(now_ms: float) -> void:
	muzzle_ms = -100000.0
	_pending = false
	_barrel_spinning = false
	_barrel_time = now_ms
	if flash:
		flash.visible = false
	if light:
		light.visible = false


func muzzle() -> Vector3:
	if flash and is_instance_valid(flash):
		return flash.global_position
	return root.global_position


func _spin_barrel(now_ms: float, firing: bool) -> void:
	if barrel == null:
		return
	var delta := now_ms - _barrel_time
	var angle: float
	if _barrel_spinning:
		angle = _barrel_angle + delta * SPIN_SPEED
	else:
		delta = minf(delta, COAST_TIME)
		angle = _barrel_angle + delta * 0.5 * (SPIN_SPEED + (COAST_TIME - delta) / COAST_TIME)
	if _barrel_spinning != firing:
		_barrel_time = now_ms
		_barrel_angle = fmod(angle, 360.0)
		_barrel_spinning = firing
	# ROLL about the weapon's forward axis (a prop's +Z is id's forward)
	barrel.basis = _barrel_basis * Basis(Vector3.BACK, deg_to_rad(angle))


## Own copies of the railgun's glow materials, so each gun charges on its own.
func _bind_rail() -> void:
	for mi in root.find_children("*", "MeshInstance3D", true, false):
		var m := mi as MeshInstance3D
		if flash and flash.is_ancestor_of(m):
			continue
		for i in m.mesh.get_surface_count():
			var src := m.get_active_material(i) as StandardMaterial3D
			if src == null:
				continue
			var name := String(src.resource_name)
			var kind := "glow" if name.begins_with("railgun2") else ("scroll" if name.begins_with("railgun4") else ("emit" if name.begins_with("railgun3") else ""))
			if kind == "":
				continue
			var mat := src.duplicate() as StandardMaterial3D
			if kind != "glow":
				mat.emission_enabled = true
				mat.emission_operator = BaseMaterial3D.EMISSION_OP_MULTIPLY
			if kind == "scroll":
				# railgun4 is one stage, rgbGen entity: the scrolling texture times color1, unlit
				mat.emission_texture = mat.albedo_texture
				mat.albedo_color = Color(0, 0, 0)
				mat.metallic = 0.0
			m.set_surface_override_material(i, mat)
			_rail.append([mat, kind])


## charge: 1 ready, else how far the reload is (1 - weaponTime / 1500). now in ms.
func rail_charge(charge: float, now_ms: float) -> void:
	if _rail.is_empty():
		return
	var dt := 0.0 if _rail_t < 0.0 else clampf((now_ms - _rail_t) / 1000.0, 0.0, 0.1)
	_rail_t = now_ms
	var ready := charge >= 1.0
	if ready and not _rail_ready:
		_rail_pop = 1.0   # charged: a bright flash
	_rail_ready = ready
	_rail_pop = move_toward(_rail_pop, 0.0, dt * 4.0)
	var tint: Color
	var c := clampf(charge, 0.0, 1.0)
	if ready:
		var breath := 0.72 + 0.28 * sin(now_ms * 0.001 * TAU * 0.55)
		tint = rail_color * breath + Color(1, 1, 1) * _rail_pop * 1.2
	else:
		# recharging: from almost dark up to the rail color, flickering as it fills
		var flicker := 0.85 + 0.15 * sin(now_ms * 0.001 * TAU * 9.0)
		tint = rail_color * (0.06 + 0.94 * c * c) * flicker
	# the energy in the barrel creeps while charging, runs once ready (tcMod scroll 0 1)
	_rail_scroll += dt * (0.25 + 0.75 * clampf(charge, 0.0, 1.0))
	for r in _rail:
		var mat: StandardMaterial3D = r[0]
		match r[1]:
			"glow":
				mat.albedo_color = Color(tint.r, tint.g, tint.b, 1.0)
			"emit":
				mat.emission = tint
				mat.emission_energy_multiplier = 1.6
			"scroll":
				mat.emission = tint
				mat.emission_energy_multiplier = 1.4
				mat.uv1_offset = Vector3(0, fmod(_rail_scroll, 1.0), 0)

