class_name Q3Map
extends Node3D
## A Quake 3 map built by Q3MapBuilder (library/maps/<map>/<map>.tscn): geometry with the
## remastered world materials, sky, baked lights, collision for ragdolls and gibs, fog volumes
## and entity markers. The game itself runs the same map from the .bsp in q3core; this is what
## the player sees, and Gameplay moves its model_<n> movers from the game's entity states.

const WORLD_LAYER := 1 << 10   ## the map's own geometry: all the reflection probes capture
const PROBE_BLEND := 0.5        ## meters each probe reaches past its box, fading into its neighbors
const SHAFT_DENSITY := 0.0015   ## the faint haze that shows the sun's light shafts
const SHAFT_LIGHT := 0.01       ## the shaft light's own energy on surfaces: next to nothing (the sun is baked)
const SHAFT_GAIN := 1.5         ## shaft brightness against the baked sun's energy

@export var map := ""
var portals: Array[Q3Portal] = []   ## its mirrors and portal views
var _info: Dictionary = {}
var _shaft: DirectionalLight3D      ## lights the haze for light shafts (sun maps only)


func _ready() -> void:
	portals = Q3Portal.build_all(self)
	var geo := get_node_or_null("Geometry")
	for gi in (geo.find_children("*", "GeometryInstance3D", true, false) if geo else []):
		(gi as GeometryInstance3D).layers |= WORLD_LAYER
	_add_probes()
	_add_shaft_light()
	Settings.environment_changed.connect(_apply_settings)
	_apply_settings()


## The Graphics settings on the map's environment. Quake 3's fog volumes are part of the map and
## always drawn; the light shafts' haze follows Graphics > Volumetric fog, and needs Shadows on
## (without the map's shadows the haze would glow evenly, with no shafts).
func _apply_settings() -> void:
	var we := get_node_or_null("WorldEnvironment") as WorldEnvironment
	if we == null or we.environment == null:
		return
	var env := we.environment
	Settings.apply_environment(env)
	var shafts := _shaft != null and bool(Settings.get_value("graphics/volumetric_fog")) and Settings.shadows_on()
	env.volumetric_fog_enabled = shafts or not (info().get("fogs", []) as Array).is_empty()
	env.volumetric_fog_density = SHAFT_DENSITY if shafts else 0.0
	if _shaft:
		_shaft.visible = shafts


## Light shafts where the map's sun (q3map_sun) comes in through windows and openings: a real-time
## copy of the baked sun that lights only the haze (and the map's surfaces by a hundredth, too
## little to see), shadowed by the map's geometry. Space maps (blacksky) have no air to light.
func _add_shaft_light() -> void:
	var sun: Dictionary = info().get("sky_light", {}).get("sun", {})
	if sun.is_empty():
		return
	for s in info().get("sky", []):
		if String(s).contains("blacksky"):
			return
	var we := get_node_or_null("WorldEnvironment") as WorldEnvironment
	if we == null or we.environment == null:
		return
	var env := we.environment
	env.volumetric_fog_albedo = Color.WHITE
	env.volumetric_fog_anisotropy = 0.6   # forward scattering: shafts glow looking toward the sun
	env.volumetric_fog_length = 64.0
	env.volumetric_fog_sky_affect = 0.0
	env.volumetric_fog_gi_inject = 0.0
	_shaft = DirectionalLight3D.new()
	_shaft.name = "ShaftSun"
	add_child(_shaft)
	var d := _vec3(sun["direction"])
	_shaft.look_at_from_position(Vector3.ZERO, d, Vector3.UP if absf(d.normalized().y) < 0.99 else Vector3.FORWARD)
	var c: Array = sun["color"]
	_shaft.light_color = Color(float(c[0]), float(c[1]), float(c[2]))
	_shaft.light_energy = SHAFT_LIGHT
	_shaft.light_volumetric_fog_energy = float(sun["energy"]) / SHAFT_LIGHT * SHAFT_GAIN
	_shaft.light_specular = 0.0
	_shaft.light_bake_mode = Light3D.BAKE_DISABLED
	_shaft.light_cull_mask = WORLD_LAYER   # only the map shadows the shafts
	_shaft.shadow_enabled = true
	_shaft.directional_shadow_mode = DirectionalLight3D.SHADOW_PARALLEL_2_SPLITS
	_shaft.directional_shadow_max_distance = 96.0


## A box-projected reflection probe for each room tools/map_probes.py found ("probes"), so PBR
## surfaces reflect the room around them instead of the sky. Each captures once, from the open spot
## nearest its middle, and sees only the map's geometry (WORLD_LAYER): players, items and effects
## around at that moment would stay frozen in the reflection. They add reflections only: the
## lighting stays the bake's (AMBIENT_DISABLED).
func _add_probes() -> void:
	var list: Array = info().get("probes", [])
	if list.is_empty():
		return
	var holder := Node3D.new()
	holder.name = "Probes"
	add_child(holder)
	for p: Dictionary in list:
		var lo := _vec3(p["mins"])
		var hi := _vec3(p["maxs"])
		var probe := ReflectionProbe.new()
		probe.size = hi - lo + Vector3.ONE * PROBE_BLEND * 2.0
		probe.blend_distance = PROBE_BLEND
		probe.box_projection = true
		probe.update_mode = ReflectionProbe.UPDATE_ONCE
		probe.ambient_mode = ReflectionProbe.AMBIENT_DISABLED
		probe.cull_mask = WORLD_LAYER
		holder.add_child(probe)
		probe.position = (lo + hi) * 0.5
		probe.origin_offset = _vec3(p["origin"]) - probe.position


static func _vec3(a: Array) -> Vector3:
	return Vector3(float(a[0]), float(a[1]), float(a[2]))


## The map's description from tools/build_maps.py (entities, lights, models, report).
func info() -> Dictionary:
	if _info.is_empty() and map != "":
		var p: Variant = JSON.parse_string(FileAccess.get_file_as_string("res://library/maps/%s/%s.json" % [map, map]))
		if p is Dictionary:
			_info = p
	return _info


## The mesh of inline model n (a door, plat, button ...), or null when it draws nothing.
func model_node(n: int) -> Node3D:
	return get_node_or_null("Geometry/model_%d" % n) as Node3D


## Entity markers of one classname (metadata "entity" holds the key/values).
func entities_of(classname: String) -> Array[Marker3D]:
	var out: Array[Marker3D] = []
	for m in $Entities.get_children():
		if String(m.get_meta("entity", {}).get("classname", "")) == classname:
			out.append(m)
	return out


## Where Quake 3 puts the camera between matches (info_player_intermission).
func intermission_transform() -> Transform3D:
	var points := entities_of("info_player_intermission")
	return view_transform(points[0]) if points.size() else Transform3D.IDENTITY


## A camera at an entity marker, facing the way the entity does in Quake 3: toward its "target",
## else along "angles" (pitch yaw roll, pitch down positive) or "angle" (yaw). Spawn points get
## the player's eye height (DEFAULT_VIEWHEIGHT 26 above the origin, which is 24 above the feet).
func view_transform(marker: Marker3D) -> Transform3D:
	var e: Dictionary = marker.get_meta("entity", {})
	var pos := marker.global_position
	if String(e.get("classname", "")).begins_with("info_player") or String(e.get("classname", "")).begins_with("team_CTF"):
		pos.y += 26.0 / 32.0
	var dir := Vector3.ZERO
	if e.has("target"):
		for m in $Entities.get_children():
			if String(m.get_meta("entity", {}).get("targetname", "")) == String(e["target"]):
				dir = m.global_position - pos
				break
	if dir.length() < 0.001:
		var pitch := 0.0
		var yaw := float(e.get("angle", "0"))
		if e.has("angles"):
			var a := String(e["angles"]).split_floats(" ", false)
			if a.size() >= 2:
				pitch = a[0]
				yaw = a[1]
		var p := deg_to_rad(pitch)
		var y := deg_to_rad(yaw)
		var f := Vector3(cos(y) * cos(p), sin(y) * cos(p), -sin(p))   # id axes
		dir = Vector3(f.y, f.z, f.x)
	var up := Vector3.UP if absf(dir.normalized().y) < 0.99 else Vector3.FORWARD
	return Transform3D(Basis.looking_at(dir, up), pos)
