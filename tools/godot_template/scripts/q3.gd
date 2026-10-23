extends Node
## Autoload "Q3": the remastered library. Catalogs, unit conversion, loaders.
## library/index.json      characters + props lists
## library/entities.json   items (bg_misc.c) and map entity classes (g_spawn.c)
## library/fx/fx.json      effect textures + effect parameters from cgame (id units, ms)

const UNIT := 1.0 / 32.0          ## one id Tech 3 unit in meters
const GRAVITY := 800.0 * UNIT     ## g_gravity 800 units/s^2 = 25 m/s^2 (project default gravity)
const LIB := "res://library/"

var characters: PackedStringArray = []
var props: PackedStringArray = []
var entities: Dictionary = {}       ## classname -> item
var map_entities: Dictionary = {}   ## classname -> map entity class
var pickup_display: Dictionary = {}
var fx: Dictionary = {}
var fx_textures: Dictionary = {}
var _rigs: Dictionary = {}


func _ready() -> void:
	var index: Dictionary = load_json(LIB + "index.json")
	characters = PackedStringArray(index.get("characters", []))
	props = PackedStringArray(index.get("props", []))
	var cat: Dictionary = load_json(LIB + "entities.json")
	for it in cat.get("items", []):
		entities[it["classname"]] = it
	for me in cat.get("map_entities", []):
		map_entities[me["classname"]] = me
	pickup_display = cat.get("pickup_display", {})
	var f: Dictionary = load_json(LIB + "fx/fx.json")
	fx = f.get("effects", {})
	fx_textures = f.get("textures", {})


func load_json(path: String) -> Variant:
	var f := FileAccess.open(path, FileAccess.READ)
	if f == null:
		push_warning("Q3: missing " + path)
		return {}
	var data: Variant = JSON.parse_string(f.get_as_text())
	return data if data != null else {}


## id units -> meters, id axes (x fwd, y left, z up) -> Godot (glTF: z fwd, y up).
func q3_to_godot(v: Vector3) -> Vector3:
	return Vector3(v.y, v.z, v.x) * UNIT


func character_scene(model: String) -> PackedScene:
	return load(LIB + "characters/%s.glb" % model)


func character_rig(model: String) -> Dictionary:
	if not _rigs.has(model):
		_rigs[model] = load_json(LIB + "characters/%s.rig.json" % model)
	return _rigs[model]


func prop_scene(id: String) -> PackedScene:
	var path := LIB + "props/%s.glb" % id
	if not ResourceLoader.exists(path):
		push_warning("Q3: no prop " + id)
		return null
	return load(path)


func instance_prop(id: String) -> Node3D:
	var s := prop_scene(id)
	return s.instantiate() if s else null


func fx_texture(name: String) -> Texture2D:
	if not fx_textures.has(name):
		push_warning("Q3: no fx texture " + name)
		return null
	return load(LIB + "fx/" + String(fx_textures[name]["file"]))


func fx_frames(name: String) -> int:
	return int(fx_textures.get(name, {}).get("frames", 1))
