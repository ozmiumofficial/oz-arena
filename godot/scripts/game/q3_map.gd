class_name Q3Map
extends Node3D
## A Quake 3 map built by Q3MapBuilder (library/maps/<map>/<map>.tscn): geometry with the
## remastered world materials, sky, baked lights, collision for ragdolls and gibs, fog volumes
## and entity markers. The game itself runs the same map from the .bsp in q3core; this is what
## the player sees, and Gameplay moves its model_<n> movers from the game's entity states.

const WORLD_LAYER := 1 << 10   ## the map's own geometry: all the reflection probes capture
## the static world only, not the movers: what impact marks land on (tr_marks.c R_MarkFragments
## walks the world BSP alone, so marks never paint players, items, guns or doors)
const MARK_LAYER := 1 << 11
const PROBE_BLEND := 0.5        ## meters each probe reaches past its box, fading into its neighbors
const SHAFT_DENSITY := 0.0011   ## the faint haze that shows the sun's light shafts (0.0015 until 2026-10-24: too foggy)
const SHAFT_LIGHT := 0.01       ## the shaft light's own energy on surfaces: next to nothing (the sun is baked)
const SHAFT_GAIN := 1.5         ## shaft brightness against the baked sun's energy
const FOG_LIGHT_CAP := 6.0      ## the most any one map light adds to the mist (typical lamps: 0.2 to 10)
const FOG_LIGHT_SCALE := 0.6    ## the mist under full strength (Ozmium: "tone the fog down a little", twice: 0.8, then 0.6)
## At run time the lamps light only bodies, items, guns and the mist (the world has them baked), yet
## every world pixel walks every lamp whose range covers it: q3dm6's 596 lamps, reaching to where
## they fade to 1/255, were half its GPU frame (5.8 ms at 1080p, 2026-10-25, tests/gpu_bench.tscn).
## They reach half as far (4/255 at the edge; bodies look the same), 4.0 ms. The mist lost the faint
## sum of their far tails, so each lamp's share of it is raised to make that up.
const LAMP_RANGE := 0.5
const LAMP_FOG_GAIN := 2.5

@export var map := ""
var portals: Array[Q3Portal] = []   ## its mirrors and portal views
var _info: Dictionary = {}
var _shaft: DirectionalLight3D      ## lights the haze for light shafts (sun maps only)
var _lamps: Array[Light3D] = []     ## the map's baked omni and spot lights (see _apply_settings)


func _ready() -> void:
	portals = Q3Portal.build_all(self)
	var geo := get_node_or_null("Geometry")
	for gi in (geo.find_children("*", "GeometryInstance3D", true, false) if geo else []):
		(gi as GeometryInstance3D).layers |= WORLD_LAYER
		if not String(geo.get_path_to(gi)).begins_with("model_"):
			(gi as GeometryInstance3D).layers |= MARK_LAYER
	_add_probes()
	# The map's lights also glow in the volumetric fog: the mist. The strongest would flood a room
	# (Lost World's fire column, energy 64, drowned its courtyard in yellow, 2026-10-23), so each
	# light's share of the fog is capped at what a light of FOG_LIGHT_CAP gives; lamps keep theirs.
	var lights := get_node_or_null("Lights")
	for l in (lights.find_children("*", "Light3D", true, false) if lights else []):
		var light := l as Light3D
		light.light_volumetric_fog_energy = FOG_LIGHT_SCALE * minf(1.0, FOG_LIGHT_CAP / maxf(light.light_energy, 0.001))
		if light is OmniLight3D:
			(light as OmniLight3D).omni_range *= LAMP_RANGE
		elif light is SpotLight3D:
			(light as SpotLight3D).spot_range *= LAMP_RANGE
		if not light is DirectionalLight3D:
			light.light_volumetric_fog_energy *= LAMP_FOG_GAIN
			_lamps.append(light)
	_add_shaft_light()
	Settings.environment_changed.connect(_apply_settings)
	_apply_settings()


## The Graphics settings on the map's environment. Quake 3's fog volumes are part of the map and
## always drawn; the light shafts' haze follows Graphics > Volumetric fog, and needs Shadows on
## (without the map's shadows the haze would glow evenly, with no shafts).
func _apply_settings() -> void:
	# The baked lights' shadows were for the bake. At run time a static light skips the lightmapped
	# world and lights only players, items, guns and the mist, where its shadow hardly shows; but
	# Godot redraws a shadow every frame while an animated material is in reach (the world shaders
	# all are), so 180-4,000 shadowed lamps a map cost most of the frame: q3dm6 ran at 38 fps with
	# them and 222 without (2026-10-24). Ultra keeps them.
	var lamp_shadows: bool = Settings.get_value("graphics/shadows") == "ultra"
	for l in _lamps:
		l.shadow_enabled = lamp_shadows
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
	# Mild forward scattering. At 0.6 the haze was (1.6 / 0.4)^3 = 64x brighter facing the sun than
	# away, so a sunlit courtyard turned blue-gray or stayed warm depending on where you looked
	# (phantq3dm1_rev, oxodm76; Ozmium, 2026-10-25: "like 2 different setups at the same time").
	env.volumetric_fog_anisotropy = 0.2
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


## Reflection probes render one at a time, about eight frames each, and every finished one pops the
## specular lighting of everything it reaches (Toxicity flickered all over for its first 3 seconds).
## A match calls this: a black cover over the screen until the probes are done, then a short fade.
func cover_probes() -> void:
	if not has_node("Probes") or has_node("ProbeCover"):
		return
	var frames := mini(get_node("Probes").get_child_count() * 8 + 12, 300)
	var layer := CanvasLayer.new()
	layer.name = "ProbeCover"
	layer.layer = 90
	var rect := ColorRect.new()
	rect.color = Color.BLACK
	rect.set_anchors_preset(Control.PRESET_FULL_RECT)
	rect.mouse_filter = Control.MOUSE_FILTER_IGNORE
	layer.add_child(rect)
	add_child(layer)
	for i in frames:
		await get_tree().process_frame
		if not is_inside_tree():
			return
	var tw := create_tween()
	tw.tween_property(rect, "color:a", 0.0, 0.3)
	await tw.finished
	layer.queue_free()


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
