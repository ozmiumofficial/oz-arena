class_name Q3MapPublish
extends RefCounted
## Maps the editor has published (MAP_EDITOR_PLAN.md E7): compiled in full (bsp, vis, light, aas) and
## listed beside Quake 3's arenas in the menu's match setup. OZMIUM's Publish freezes a copy for
## players; here the frozen copy is the compiled .bsp and .aas in user://q3/baseq3/maps (where q3core
## loads maps from), plus a copy of the document as it was compiled (pub_<name>.q3map.json beside
## them), which Gameplay draws the map from. Editing or deleting the editor's own file changes nothing
## players see until the map is published again.
## Each entry has the arena fields Session.maps carries (map, name, types, special, bots, fraglimit,
## timelimit, levelshot), plus doc (that frozen copy), models (entity id -> brush model) and
## published (unix time). levelshot is a data: URL of the map's thumbnail, since the web menu can't
## read user://.

static var path := "user://maps/published.json"   ## tests point it elsewhere


static func list() -> Array:
	var d: Variant = JSON.parse_string(FileAccess.get_file_as_string(path)) if FileAccess.file_exists(path) else null
	var out := []
	if d is Dictionary:
		for e in d.get("maps", []):
			# a map whose .bsp or document has gone can't be played
			if e is Dictionary and FileAccess.file_exists(Q3MapCompiler.output_dir.path_join(String(e.get("map", "")) + ".bsp")) \
					and FileAccess.file_exists(String(e.get("doc", ""))):
				out.append(e)
	return out


static func find(map: String) -> Dictionary:
	for e in list():
		if e["map"] == map:
			return e
	return {}


## Adds or replaces the entry for entry.map.
static func put(entry: Dictionary) -> Error:
	var all := list().filter(func(e): return e["map"] != entry["map"])
	all.append(entry)
	return _save(all)


static func remove(map: String) -> Error:
	return _save(list().filter(func(e): return e["map"] != map))


static func _save(all: Array) -> Error:
	DirAccess.make_dir_recursive_absolute(path.get_base_dir())
	var tmp := path + ".tmp"
	var f := FileAccess.open(tmp, FileAccess.WRITE)
	if f == null:
		return FileAccess.get_open_error()
	f.store_string(JSON.stringify({"maps": all}, "\t"))
	f.close()
	return DirAccess.rename_absolute(ProjectSettings.globalize_path(tmp), ProjectSettings.globalize_path(path))


## What a map needs before it can be published, as sentences; empty when it's ready. A match needs
## somewhere to spawn, and team modes their own spawns or flags.
static func problems(doc: Q3MapDoc) -> PackedStringArray:
	var out := PackedStringArray()
	var spawns := 0
	for id in doc.ids_of("entity"):
		if String(doc.get_obj(id)["classname"]) in ["info_player_deathmatch", "info_player_start"]:
			spawns += 1
	if spawns == 0:
		out.append("The map has no spawn point (P places one).")
	if doc.ids_of("brush").is_empty():
		out.append("The map has no brushes.")
	return out


## The game types a map suits, as arenas.txt names them: every map plays free-for-all, duel and team
## deathmatch; capture the flag needs both flags.
static func types_of(doc: Q3MapDoc) -> Array:
	var types := ["ffa", "tourney", "team"]
	var flags := {}
	for id in doc.ids_of("entity"):
		flags[String(doc.get_obj(id)["classname"])] = true
	if flags.has("team_CTF_redflag") and flags.has("team_CTF_blueflag"):
		types.append("ctf")
	return types


## A thumbnail image as a data: URL the web menu can show ("" when there's none).
static func data_url(png_path: String) -> String:
	if not FileAccess.file_exists(png_path):
		return ""
	var img := Image.load_from_file(ProjectSettings.globalize_path(png_path))
	if img == null or img.is_empty():
		return ""
	return "data:image/jpeg;base64," + Marshalls.raw_to_base64(img.save_jpg_to_buffer(0.85))
