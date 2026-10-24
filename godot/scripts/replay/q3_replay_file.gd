class_name Q3ReplayFile
extends RefCounted
## The replay library on disk: user://replays/*.q3replay, written by Q3ReplayRecorder and played by
## Q3Replay (q3core). A file is "Q3RP", the format version and a JSON header (map, mode, date,
## the players as the match began), then the recorded frames, then a JSON trailer (length, the
## final standings with cards and results) and "Q3RT". A recording the game didn't finish has no
## trailer and still plays up to its last whole frame.
## Names and favorites live beside the files in library.json, so renaming never rewrites a replay.
## Quake 3's own demos (.dm_68) are listed too: ones dropped into this folder, and the ones in the
## Quake 3 install's baseq3/demos (where "quake3"; never deleted from there). Q3Replay reads them.

const DIR := "user://replays"
const EXT := "q3replay"
const LIBRARY := DIR + "/library.json"
const VERSION := 1
const MAGIC := "Q3RP"
const TRAILER_MAGIC := "Q3RT"

## Tests point the library somewhere else.
static var dir := DIR
static var _demo_cache: Dictionary = {}   ## "path|mtime" -> a demo's line ({} if it didn't read)


static func library_path() -> String:
	return dir.path_join("library.json")


## A replay's header merged with its trailer, plus path, file, bytes and "complete" (has a
## trailer), or {} if it isn't a replay. Reads only the two JSON blocks.
static func read_info(path: String) -> Dictionary:
	var f := FileAccess.open(path, FileAccess.READ)
	if f == null or f.get_length() < 12:
		return {}
	if f.get_buffer(4).get_string_from_ascii() != MAGIC:
		return {}
	var version := f.get_32()
	var hlen := f.get_32()
	if version != VERSION or 12 + hlen > f.get_length():
		return {}
	var head = JSON.parse_string(f.get_buffer(hlen).get_string_from_utf8())
	if not head is Dictionary:
		return {}
	var info: Dictionary = head
	info["complete"] = false
	var n := f.get_length()
	if n - 12 - hlen >= 8:
		f.seek(n - 4)
		if f.get_buffer(4).get_string_from_ascii() == TRAILER_MAGIC:
			f.seek(n - 8)
			var tlen := f.get_32()
			if n - 8 - tlen >= 12 + hlen:
				f.seek(n - 8 - tlen)
				var tail = JSON.parse_string(f.get_buffer(tlen).get_string_from_utf8())
				if tail is Dictionary:
					info.merge(tail, true)
					info["complete"] = true
	info["path"] = path
	info["file"] = path.get_file()
	info["bytes"] = n
	return info


static func _library() -> Dictionary:
	var v = JSON.parse_string(FileAccess.get_file_as_string(library_path())) if FileAccess.file_exists(library_path()) else null
	return v if v is Dictionary else {}


static func _save_library(lib: Dictionary) -> void:
	DirAccess.make_dir_recursive_absolute(dir)
	var f := FileAccess.open(library_path(), FileAccess.WRITE)
	if f:
		f.store_string(JSON.stringify(lib, "\t"))


## Every replay, newest first, each with its library entry's "name" (your rename, else "") and
## "favorite".
static func list() -> Array:
	var out: Array = []
	var lib := _library()
	for where in ["replays", "quake3"]:
		var folder := folder_of(where)
		if folder == "" or not DirAccess.dir_exists_absolute(folder):
			continue
		for file in DirAccess.get_files_at(folder):
			var info := {}
			if file.get_extension() == EXT and where == "replays":
				info = read_info(folder.path_join(file))
			elif is_demo(file):
				info = read_demo_info(folder.path_join(file))
			if info.is_empty():
				continue
			info["where"] = where
			var entry: Dictionary = lib.get(key_of(file, where), {})
			info["name"] = String(entry.get("name", ""))
			info["favorite"] = bool(entry.get("favorite", false))
			out.append(info)
	out.sort_custom(func(a, b): return int(a.get("unix", 0)) > int(b.get("unix", 0)))
	return out


## A Quake 3 demo by its extension (.dm_68; Q3Replay reads protocol 68).
static func is_demo(file: String) -> bool:
	return file.get_extension().to_lower() == "dm_68"


## The folder of a list entry's "where": this library, or the Quake 3 install's demos.
static func folder_of(where: String) -> String:
	if where == "replays":
		return dir
	if where == "quake3":
		var base := Q3Ownership.remembered()
		return base.path_join("demos") if base != "" else ""
	return ""


## The library's key for a file: its name here, "quake3/<name>" for the install's demos.
static func key_of(file: String, where: String) -> String:
	return file if where == "replays" else where + "/" + file


## The full path of a bare file name in a list folder, "" unless it's one of ours that exists.
static func path_of(file: String, where := "replays") -> String:
	if file == "" or file != file.get_file() or not (file.get_extension() == EXT or is_demo(file)):
		return ""
	var folder := folder_of(where)
	var p := folder.path_join(file) if folder != "" else ""
	return p if p != "" and FileAccess.file_exists(p) else ""


## A demo's line (Q3Replay.demo_info: the start of the file only; needs the engine), {} if
## unreadable. Kept per file, size and time for the session, failures too: the list is built on
## every visit to the Replays page.
static func read_demo_info(path: String) -> Dictionary:
	var mtime := int(FileAccess.get_modified_time(path))
	var key := "%s|%d" % [path, mtime]
	if _demo_cache.has(key):
		return _demo_cache[key].duplicate()
	if Q3Host.start() != "" or Q3Server.running():
		return {}
	var info: Dictionary = Q3Replay.demo_info(path)
	if info.has("error"):
		_demo_cache[key] = {}
		return {}
	info["path"] = path
	info["file"] = path.get_file()
	info["unix"] = mtime
	info["result"] = ""
	_demo_cache[key] = info.duplicate()
	return info


static func set_favorite(file: String, on: bool, where := "replays") -> void:
	var lib := _library()
	var entry: Dictionary = lib.get(key_of(file, where), {})
	entry["favorite"] = on
	lib[key_of(file, where)] = entry
	_save_library(lib)


static func rename(file: String, name: String, where := "replays") -> void:
	var lib := _library()
	var entry: Dictionary = lib.get(key_of(file, where), {})
	entry["name"] = name.strip_edges().left(64)
	lib[key_of(file, where)] = entry
	_save_library(lib)


static func delete(file: String) -> bool:
	if file.get_file() != file or not (file.get_extension() == EXT or is_demo(file)):
		return false   # a bare file name in the library folder, nothing else (never the install's demos)
	var lib := _library()
	lib.erase(file)
	_save_library(lib)
	return DirAccess.remove_absolute(dir.path_join(file)) == OK


## Keeps the newest `keep` replays that aren't favorites (favorites are kept forever), never the
## one being written (`spare`). Returns how many were deleted.
static func clean(keep: int, spare := "") -> int:
	var n := 0
	var kept := 0
	for info in list():
		if info["favorite"] or info["file"] == spare or info.get("demo", false):
			continue
		kept += 1
		if kept > keep and delete(info["file"]):
			n += 1
	return n


## A file name for a new replay: <date>_<time>_<map>_<mode>.q3replay.
static func new_path(map: String, mode: String) -> String:
	var t := Time.get_datetime_dict_from_system()
	var stamp := "%04d-%02d-%02d_%02d%02d%02d" % [t.year, t.month, t.day, t.hour, t.minute, t.second]
	var base := "%s_%s_%s" % [stamp, map.validate_filename(), mode.validate_filename()]
	var path := dir.path_join(base + "." + EXT)
	var i := 2
	while FileAccess.file_exists(path):
		path = dir.path_join("%s_%d.%s" % [base, i, EXT])
		i += 1
	return path
