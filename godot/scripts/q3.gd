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
## An exported game's world textures ship as packs beside the exe, because a GitHub release file
## must be under 2 GiB (tools/export_game.py writes them with tests/pack_textures.tscn).
const TEXTURE_PACKS := 3
var missing_packs: PackedStringArray = []
## Game time in ms: every frame's delta added up, so Engine.time_scale and --fixed-fps (Movie
## Maker, the trailer captures) hold. Time.get_ticks_msec() is wall time: in a recording that
## renders slower than it plays, pickups spun and portal cameras swung far too fast. Anything
## Quake 3 times by cg.time reads this.
var game_ms := 0.0
## held(): resources kept loaded. Godot frees a resource when its last user goes, so an effect's
## texture or shader loaded per shot was read again (and the shader compiled again) on every shot,
## and a player's model on every respawn: 25-45 ms stalls several times a second in a bot match
## (2026-10-24, tests/match_perf.tscn).
var _held: Dictionary = {}
## Models (prop and character scenes) are kept too, but only while a match holds them
## (hold_models): the menu shows them one at a time and shouldn't pile them up.
var _held_models: Dictionary = {}
var _model_holders := 0
var _loading := {}   ## model paths on the loader thread (preload_character)


func _init() -> void:
	if OS.has_feature("editor"):
		return
	var dir := OS.get_executable_path().get_base_dir()
	for i in TEXTURE_PACKS:
		var pack := "textures-%d.pck" % (i + 1)
		if not FileAccess.file_exists(dir.path_join(pack)) or not ProjectSettings.load_resource_pack(dir.path_join(pack), false):
			missing_packs.append(pack)


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
	return _model(LIB + "characters/%s.glb" % model)


func character_rig(model: String) -> Dictionary:
	if not _rigs.has(model):
		_rigs[model] = load_json(LIB + "characters/%s.rig.json" % model)
	return _rigs[model]


func prop_scene(id: String) -> PackedScene:
	var path := LIB + "props/%s.glb" % id
	if not ResourceLoader.exists(path):
		push_warning("Q3: no prop " + id)
		return null
	return _model(path)


func instance_prop(id: String) -> Node3D:
	var s := prop_scene(id)
	return s.instantiate() if s else null


func fx_texture(name: String) -> Texture2D:
	if not fx_textures.has(name):
		push_warning("Q3: no fx texture " + name)
		return null
	return held(LIB + "fx/" + String(fx_textures[name]["file"]))


## load() that keeps the resource for the rest of the run: effect textures, shaders and sounds,
## which are small and used again and again.
func held(path: String) -> Resource:
	var r: Resource = _held.get(path)
	if r == null:
		r = load(path)
		if r:
			_held[path] = r
	return r


## Keeps one material of a built-in kind (StandardMaterial3D, ParticleProcessMaterial) alive under
## `key`. Godot shares the shader it generates for one set of features only while a material with
## those features exists, so a one-shot effect's materials were generated and compiled again on
## every use.
func keep(key: String, material: Material) -> void:
	if not _held.has(key):
		_held[key] = material


## A match keeps every model it loads until it ends (Q3Client: true on entering the tree, false on
## leaving it).
func hold_models(on: bool) -> void:
	_model_holders = maxi(_model_holders + (1 if on else -1), 0)
	if _model_holders == 0:
		_held_models.clear()


## A character skin's map (library/characters/<model>_<stem>_<map>, tools/build_skins.py), kept
## like the models while a match holds them, so a respawn in that skin doesn't read it again.
func character_texture(path: String) -> Texture2D:
	var t: Texture2D = _held_models.get(path)
	if t == null:
		t = load(path) as Texture2D
		if t and _model_holders > 0:
			_held_models[path] = t
	return t


## A material made for a body (Q3Character's bright skins), kept like the models while a match holds
## them: a new StandardMaterial3D generates and compiles its shader again, about 95 ms at every
## respawn (2026-10-25). `make` builds it on the first ask.
func character_material(key: String, make: Callable) -> Material:
	var m: Material = _held_models.get(key)
	if m == null:
		m = make.call()
		if m and _model_holders > 0:
			_held_models[key] = m
	return m


## Starts loading a player model on a loader thread while a match holds models (Q3Client, as players
## join): the first body in a model otherwise read its glTF mid-fight, a 20-40 ms stall.
func preload_character(model: String) -> void:
	var path := LIB + "characters/%s.glb" % model
	if _model_holders == 0 or _held_models.has(path) or _loading.has(path) or not ResourceLoader.exists(path):
		return
	if ResourceLoader.load_threaded_request(path) == OK:
		_loading[path] = true


## Whether a player model is loaded and kept (preload_character finished).
func has_character(model: String) -> bool:
	return _held_models.has(LIB + "characters/%s.glb" % model)


## Keeps the models the loader thread has finished.
func poll_preloads() -> void:
	for path in _loading.keys():
		if ResourceLoader.load_threaded_get_status(path) != ResourceLoader.THREAD_LOAD_IN_PROGRESS:
			_model(path)


func _model(path: String) -> PackedScene:
	var s: PackedScene = _held_models.get(path)
	if s == null and _loading.has(path):
		_loading.erase(path)
		s = ResourceLoader.load_threaded_get(path) as PackedScene   # waits if it's still loading
		if s and _model_holders > 0:
			_held_models[path] = s
	if s == null:
		s = load(path)
		if s and _model_holders > 0:
			_held_models[path] = s
	return s


func fx_frames(name: String) -> int:
	return int(fx_textures.get(name, {}).get("frames", 1))


func _process(delta: float) -> void:
	game_ms += delta * 1000.0
