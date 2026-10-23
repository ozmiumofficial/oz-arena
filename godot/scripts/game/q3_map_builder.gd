class_name Q3MapBuilder
extends RefCounted
## Builds library/maps/<map>/<map>.tscn from what tools/build_maps.py wrote next to it:
##   <map>.glb   drawn surfaces (world_* chunks, model_<n> movers), remastered world materials
##   <map>.json  entities, lights, sky, fog volumes, collision hulls
## The scene: the geometry, a WorldEnvironment with the map's sky, the map's lights (baked),
## Godot collision for ragdolls and gibs (the game itself collides through q3core and the .bsp),
## fog volumes, entity markers and a LightmapGI node, ready to bake in the editor.
## Run headless with res://tools/build_map_scenes.tscn, or from the editor: Q3MapBuilder.build("q3dm17").

const DIR := "res://library/maps/%s/"
const LAYER_SOLID := 1           ## brushes with CONTENTS_SOLID (gibs, ragdolls)
const LAYER_CLIP := 1 << 1       ## playerclip brushes (ragdolls; Quake 3's corpses hit these too)
const UNIT := 1.0 / 32.0
const SKY_SCALE := 4.0           ## sky surfacelight -> bake sky energy (calibrated with LIGHT_SCALE in build_maps.py)


static func scene_path(map: String) -> String:
	return (DIR % map) + map + ".tscn"


## Builds and saves the scene; returns OK or the error.
static func build(map: String) -> Error:
	var dir := DIR % map
	var info: Variant = JSON.parse_string(FileAccess.get_file_as_string(dir + map + ".json"))
	if not info is Dictionary:
		push_error("Q3MapBuilder: no %s.json" % map)
		return ERR_FILE_NOT_FOUND
	var geo := load(dir + map + ".glb") as PackedScene
	if geo == null:
		push_error("Q3MapBuilder: %s.glb isn't imported" % map)
		return ERR_FILE_UNRECOGNIZED
	var root := Node3D.new()
	root.name = map
	root.set_script(load("res://scripts/game/q3_map.gd"))
	root.set("map", map)

	var g := geo.instantiate()
	g.name = "Geometry"
	_add(root, g)
	_environment(root, info)
	_lights(root, info)
	_collision(root, info)
	_fogs(root, info)
	_entities(root, info)

	var lm := LightmapGI.new()
	lm.name = "LightmapGI"
	lm.quality = LightmapGI.BAKE_QUALITY_HIGH
	lm.bounces = 3
	lm.use_denoiser = true
	lm.directional = true
	# q3map_surfacelight on the sky: light from every sky face, here as the bake's sky color
	var sky_sl := float(info.get("sky_light", {}).get("surfacelight", 0.0))
	lm.environment_mode = LightmapGI.ENVIRONMENT_MODE_CUSTOM_COLOR
	lm.environment_custom_color = Color.WHITE
	lm.environment_custom_energy = sky_sl / 255.0 * SKY_SCALE * float(info.get("light_scale", 1.0))
	lm.texel_scale = 1.0
	_add(root, lm)

	for n in root.find_children("*", "", true, false):
		if n.owner == null:
			n.owner = root
	var packed := PackedScene.new()
	var err := packed.pack(root)
	if err == OK:
		err = ResourceSaver.save(packed, scene_path(map))
	root.free()
	return err


static func _add(parent: Node, child: Node) -> void:
	parent.add_child(child)


static func _environment(root: Node3D, info: Dictionary) -> void:
	var env := Environment.new()
	var wm := _world_materials()
	var sky_name: String = info["sky"][0] if info["sky"].size() else ""
	var sky_res := String(wm.get(sky_name, {}).get("res", ""))
	if sky_res != "" and ResourceLoader.exists(sky_res):
		env.background_mode = Environment.BG_SKY
		env.sky = load(sky_res)
		env.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	else:
		env.background_mode = Environment.BG_COLOR
		env.background_color = Color.BLACK
		env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	# worldspawn "ambient" is q3map's minimum light (0-255-ish); keep it a faint floor
	var ws: Dictionary = info.get("worldspawn", {})
	var amb := float(ws.get("ambient", "0"))
	env.ambient_light_color = _color(ws.get("_color", "1 1 1"))
	env.ambient_light_energy = clampf(0.15 + amb / 64.0, 0.15, 0.6)
	env.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	env.glow_enabled = true
	env.glow_intensity = 0.6
	env.glow_bloom = 0.05
	env.ssao_enabled = true
	env.ssr_enabled = false
	env.volumetric_fog_enabled = info["fogs"].size() > 0
	env.volumetric_fog_density = 0.0   # only the map's fog volumes
	var we := WorldEnvironment.new()
	we.name = "WorldEnvironment"
	we.environment = env
	_add(root, we)


## Lights as tools/build_maps.py converted them from q3map's model (energy and range already
## in Godot terms): entity lights (omni, or spot with a "target"), surface-light patches (omni just
## off the glowing face) and the sky shader's q3map_sun (directional). All bake-only.
static func _lights(root: Node3D, info: Dictionary) -> void:
	var holder := Node3D.new()
	holder.name = "Lights"
	_add(root, holder)
	var i := 0
	for l in info["lights"]:
		var light: Light3D
		if l.has("spot_target"):
			var spot := SpotLight3D.new()
			var to: Vector3 = _v(l["spot_target"]) - _v(l["origin"])
			var r := float(l.get("radius", 64.0 / 32.0))
			spot.spot_angle = clampf(rad_to_deg(atan2(r, maxf(to.length(), 0.01))), 5.0, 89.0)
			spot.spot_range = float(l["range"])
			spot.spot_attenuation = 1.0
			light = spot
			holder.add_child(light)
			light.position = _v(l["origin"])
			if to.length() > 0.001:
				light.look_at_from_position(light.position, light.position + to, Vector3.UP if absf(to.normalized().y) < 0.99 else Vector3.RIGHT)
		else:
			var omni := OmniLight3D.new()
			omni.omni_range = float(l["range"])
			omni.omni_attenuation = 1.0
			light = omni
			holder.add_child(light)
			light.position = _v(l["origin"])
		light.name = "%s_%d" % [l.get("kind", "point"), i]
		light.light_color = Color(l["color"][0], l["color"][1], l["color"][2])
		light.light_energy = float(l["energy"])
		light.light_indirect_energy = 1.0
		light.light_bake_mode = Light3D.BAKE_STATIC
		light.shadow_enabled = true   # baked shadows
		i += 1
	var sky: Dictionary = info.get("sky_light", {})
	if sky.has("sun"):
		var sun := DirectionalLight3D.new()
		sun.name = "sun"
		var d: Vector3 = _v(sky["sun"]["direction"])
		holder.add_child(sun)
		sun.look_at_from_position(Vector3.ZERO, d, Vector3.UP if absf(d.normalized().y) < 0.99 else Vector3.FORWARD)
		sun.light_color = Color(sky["sun"]["color"][0], sky["sun"]["color"][1], sky["sun"]["color"][2])
		sun.light_energy = float(sky["sun"]["energy"])
		sun.light_bake_mode = Light3D.BAKE_STATIC
		sun.shadow_enabled = true


static func _collision(root: Node3D, info: Dictionary) -> void:
	var col: Dictionary = info["collision"]
	for part in [["Collision", "solid", LAYER_SOLID], ["PlayerClip", "clip", LAYER_CLIP]]:
		var body := StaticBody3D.new()
		body.name = part[0]
		body.collision_layer = part[2]
		body.collision_mask = 0
		_add(root, body)
		var k := 0
		for flat in col[part[1]]:
			var shape := ConvexPolygonShape3D.new()
			shape.points = _points(flat)
			var cs := CollisionShape3D.new()
			cs.name = "hull_%d" % k
			cs.shape = shape
			body.add_child(cs)
			k += 1
		if part[1] == "solid" and col["patches"].size():
			var faces := PackedVector3Array()
			for flat in col["patches"]:
				faces.append_array(_points(flat))
			var tri := ConcavePolygonShape3D.new()
			tri.set_faces(faces)
			var cs := CollisionShape3D.new()
			cs.name = "patches"
			cs.shape = tri
			body.add_child(cs)


static func _fogs(root: Node3D, info: Dictionary) -> void:
	var wm := _world_materials()
	var i := 0
	for f in info["fogs"]:
		var res := String(wm.get(f["shader"], {}).get("res", ""))
		var fv := FogVolume.new()
		fv.name = "fog_%d" % i
		var mn: Vector3 = _v(f["mins"])
		var mx: Vector3 = _v(f["maxs"])
		fv.size = mx - mn
		fv.position = (mn + mx) * 0.5
		if res != "" and ResourceLoader.exists(res):
			fv.material = load(res)
		_add(root, fv)
		i += 1


## Entities with a place in the world become markers carrying their key/values (metadata
## "entity"). Items, movers and triggers live in the game (q3core); these are for the editor,
## the map editor's import later, and presentation lookups like the intermission camera.
static func _entities(root: Node3D, info: Dictionary) -> void:
	var holder := Node3D.new()
	holder.name = "Entities"
	_add(root, holder)
	var i := 0
	for e in info["entities"]:
		var cn := String(e.get("classname", ""))
		if cn in ["worldspawn", "light", "misc_model"] or not e.has("origin"):
			continue
		var m := Marker3D.new()
		m.name = "%s_%d" % [cn, i]
		var o := _id_vec(e["origin"])
		m.position = Vector3(o.y, o.z, o.x) * UNIT
		if e.has("angle"):
			m.rotation.y = deg_to_rad(float(e["angle"])) - PI * 0.5   # id yaw 0 = +X = Godot +Z
		m.set_meta("entity", e)
		holder.add_child(m)
		i += 1


static var _wm_cache: Dictionary = {}


static func _world_materials() -> Dictionary:
	if _wm_cache.is_empty():
		var p: Variant = JSON.parse_string(FileAccess.get_file_as_string("res://library/world/world_materials.json"))
		if p is Dictionary:
			_wm_cache = p.get("materials", {})
	return _wm_cache


static func _v(a: Array) -> Vector3:
	return Vector3(a[0], a[1], a[2])


static func _id_vec(s: String) -> Vector3:
	var p := s.split_floats(" ", false)
	return Vector3(p[0], p[1], p[2]) if p.size() >= 3 else Vector3.ZERO


static func _color(s: String) -> Color:
	var p := s.split_floats(" ", false)
	if p.size() < 3:
		return Color.WHITE
	var m := maxf(maxf(p[0], p[1]), maxf(p[2], 0.001))
	return Color(p[0] / m, p[1] / m, p[2] / m)


static func _points(flat: Array) -> PackedVector3Array:
	var out := PackedVector3Array()
	out.resize(flat.size() / 3)
	for i in out.size():
		out[i] = Vector3(flat[i * 3], flat[i * 3 + 1], flat[i * 3 + 2])
	return out
