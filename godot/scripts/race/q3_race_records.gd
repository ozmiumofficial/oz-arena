class_name Q3RaceRecords
extends RefCounted
## Your best run on each race map (RACE_PLAN.md D3), kept in user://race/records.json:
##   {map: {ms, splits: [checkpoint ms ...], when: unix time}}
## A map has one record (id 1.32 movement is the only physics). The ghost of the best run sits beside
## it (Q3RaceGhost). A hand-edited or damaged file reads as no records.

static var path := "user://race/records.json"   ## tests point this somewhere else
static var ghost_dir := "user://race/ghosts"


static func all() -> Dictionary:
	if not FileAccess.file_exists(path):
		return {}
	var v: Variant = JSON.parse_string(FileAccess.get_file_as_string(path))
	return v if v is Dictionary else {}


## {ms, splits, when} or {}.
static func best(map: String) -> Dictionary:
	var r: Variant = all().get(map, {})
	if not (r is Dictionary) or int(r.get("ms", 0)) <= 0:
		return {}
	return r


static func best_splits(map: String) -> PackedInt32Array:
	var out := PackedInt32Array()
	for x in best(map).get("splits", []):
		out.append(int(x))
	return out


## Keeps the run if it's your best on the map (or the first). Returns whether it was.
static func submit(map: String, ms: int, splits: PackedInt32Array) -> bool:
	if ms <= 0 or map == "":
		return false
	var cur := best(map)
	if not cur.is_empty() and int(cur["ms"]) <= ms:
		return false
	var d := all()
	d[map] = {"ms": ms, "splits": Array(splits), "when": int(Time.get_unix_time_from_system())}
	DirAccess.make_dir_recursive_absolute(path.get_base_dir())
	var f := FileAccess.open(path, FileAccess.WRITE)
	if f == null:
		return false
	f.store_string(JSON.stringify(d, " "))
	return true


## What the menu shows: {map: ms}.
static func summary() -> Dictionary:
	var out := {}
	for m in all():
		var b := best(String(m))
		if not b.is_empty():
			out[m] = int(b["ms"])
	return out


static func ghost_path(map: String) -> String:
	return ghost_dir.path_join(map.validate_filename() + ".ghost")
