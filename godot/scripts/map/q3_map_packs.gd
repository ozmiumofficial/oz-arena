class_name Q3MapPacks
extends RefCounted
## Maps dropped into the game's Maps folder as Quake 3 .pk3 files, found at startup: no menu
## rebuild, no tool run. q3core loads every pk3 there (fs_mapspath, patch 0011), so the server,
## bots (.aas) and collision see them like baseq3 paks; this lists them for the menu.
##   <exe dir>/Maps       an exported game
##   <repo>/Maps          the editor (next to godot/)
## Each map comes from its pk3's scripts/*.arena (map, longname, type, bots, limits) or, without
## one, from its maps/*.bsp alone (free for all). Entries carry the arena fields Session.maps uses
## plus special "pack", pk3 (its path), and levelshot as a data: URL read from the pk3 (the web
## menu only serves files built into it). Gameplay draws a pack map from its remastered scene when
## the pipeline made one (library/maps/<map>), else from the .bsp itself (Q3ClassicMap).

const DIR_NAME := "Maps"
const SHOT_WIDTH := 384
## arena "type" words that mean a mode's own type (game/modes/*.tres map_types)
const TYPES := {"tdm": "team", "team": "team", "ffa": "ffa", "dm": "ffa", "tourney": "tourney",
		"duel": "tourney", "1v1": "tourney", "ctf": "ctf", "single": "single"}

## Duel-pool maps (DUEL_MAPS_PLAN.md) by bsp name: their menu names, whatever the author's arena file says
const POOL_NAMES := {"hub3aeroq3": "Aerowalk", "ztn3tourney1": "Blood Run", "phantq3dm1_rev": "Battleforged",
		"hub3dm1": "Dismemberment", "pukka3tourney2": "Toxicity", "oxodm76": "Silence"}

static var _cache: Array = []
static var _stamp := ""
static var dir_override := ""   ## tests: another folder (a joiner without the host's maps)


## The Maps folder (it may not exist).
static func dir() -> String:
	if dir_override != "":
		return dir_override
	if OS.has_feature("editor"):
		return ProjectSettings.globalize_path("res://").trim_suffix("/").get_base_dir().path_join(DIR_NAME)
	return OS.get_executable_path().get_base_dir().path_join(DIR_NAME)


## The pk3 files in it, in the order Quake 3 loads them.
static func pk3s() -> PackedStringArray:
	var out := PackedStringArray()
	var d := dir()
	if not DirAccess.dir_exists_absolute(d):
		return out
	var names := []
	for f in DirAccess.get_files_at(d):
		if f.to_lower().ends_with(".pk3"):
			names.append(f)
	names.sort_custom(func(x, y): return x.to_lower() < y.to_lower())
	for f in names:
		out.append(d.path_join(f))
	return out


## Every map in the folder's pk3s as a Session.maps entry; `skip` holds map names already listed
## (Quake 3's own arenas win). Cached until a pk3 is added, removed or changed.
static func list(skip := {}) -> Array:
	var files := pk3s()
	var stamp := ""
	for p in files:
		stamp += "%s:%d;" % [p, FileAccess.get_modified_time(p)]
	if stamp != _stamp:
		_cache = _scan(files)
		_stamp = stamp
	return _cache.filter(func(m): return not skip.has(m["map"]))


## The pk3 holding a map's .bsp (later pk3s win), or "".
static func pk3_of(map: String) -> String:
	for m in list():
		if m["map"] == map.to_lower():
			return m["pk3"]
	return ""


static var _md5s: Dictionary = {}   ## pk3 path -> [modified time, md5]


## A pk3's md5 (what a joiner checks a download against), cached until the file changes.
static func md5_of(path: String) -> String:
	var t := FileAccess.get_modified_time(path)
	var c: Array = _md5s.get(path, [])
	if c.is_empty() or int(c[0]) != t:
		c = [t, FileAccess.get_md5(path)]
		_md5s[path] = c
	return String(c[1])


## A file's bytes from a map's pk3 (empty if missing).
static func read(map: String, path: String) -> PackedByteArray:
	var p := pk3_of(map)
	if p == "":
		return PackedByteArray()
	var z := ZIPReader.new()
	if z.open(p) != OK:
		return PackedByteArray()
	var b := PackedByteArray()
	for e in z.get_files():
		if e.to_lower() == path.to_lower():
			b = z.read_file(e)
			break
	z.close()
	return b


static func _scan(files: PackedStringArray) -> Array:
	var by_map := {}
	var order: Array = []
	for p in files:
		var z := ZIPReader.new()
		if z.open(p) != OK:
			continue
		var names := {}
		for e in z.get_files():
			names[e.to_lower()] = e
		var arenas := {}
		for e in names:
			if e.begins_with("scripts/") and e.ends_with(".arena"):
				for d in _info_blocks(z.read_file(names[e]).get_string_from_ascii()):
					if d.has("map"):
						arenas[String(d["map"]).to_lower()] = d
		for e in names:
			if not (e.begins_with("maps/") and e.ends_with(".bsp")) or e.count("/") != 1:
				continue
			var map: String = e.get_file().get_basename()
			var d: Dictionary = arenas.get(map, {})
			var types := []
			for t in String(d.get("type", "ffa")).to_lower().split(" ", false):
				if TYPES.has(t) and not types.has(TYPES[t]):
					types.append(TYPES[t])
			if types.is_empty():
				types = ["ffa"]
			if not types.has("ctf") and not types.has("single"):   # a deathmatch map plays every deathmatch mode
				for t in ["ffa", "tourney", "team"]:
					if not types.has(t):
						types.append(t)
			if not by_map.has(map):
				order.append(map)
			by_map[map] = {"map": map, "name": String(POOL_NAMES.get(map, d.get("longname", map))), "types": types, "special": "pack",
				"bots": String(d.get("bots", "")).split(" ", false), "fraglimit": int(d.get("fraglimit", "0")),
				"timelimit": int(d.get("timelimit", "0")), "levelshot": _levelshot(z, names, map), "pk3": p}
		z.close()
	var out := []
	for m in order:
		out.append(by_map[m])
	return out


## levelshots/<map>.jpg|tga|png from the pk3 as a data: URL, or "".
static func _levelshot(z: ZIPReader, names: Dictionary, map: String) -> String:
	for ext in ["jpg", "tga", "png"]:
		var e := "levelshots/%s.%s" % [map, ext]
		if not names.has(e):
			continue
		var img := Image.new()
		var b := z.read_file(names[e])
		var err: Error = (img.load_jpg_from_buffer(b) if ext == "jpg" else
				img.load_tga_from_buffer(b) if ext == "tga" else img.load_png_from_buffer(b))
		if err != OK or img.is_empty():
			continue
		img.convert(Image.FORMAT_RGB8)
		img.resize(SHOT_WIDTH, maxi(1, roundi(SHOT_WIDTH * float(img.get_height()) / img.get_width())), Image.INTERPOLATE_LANCZOS)
		return "data:image/jpeg;base64," + Marshalls.raw_to_base64(img.save_jpg_to_buffer(0.85))
	return ""


## Quake 3 info blocks ("{ key value ... }", // comments) as dictionaries with lower-case keys.
static func _info_blocks(text: String) -> Array:
	var out := []
	var re_comment := RegEx.create_from_string("//[^\\n]*")
	text = re_comment.sub(text, "", true)
	var re_block := RegEx.create_from_string("\\{([^}]*)\\}")
	var re_kv := RegEx.create_from_string("(\\w+)\\s+(\"[^\"]*\"|\\S+)")
	for b in re_block.search_all(text):
		var d := {}
		for kv in re_kv.search_all(b.get_string(1)):
			d[kv.get_string(1).to_lower()] = kv.get_string(2).trim_prefix("\"").trim_suffix("\"")
		out.append(d)
	return out
