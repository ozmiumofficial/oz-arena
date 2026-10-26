class_name Q3RaceRecords
extends RefCounted
## Your best run on each race map (RACE_PLAN.md D3), kept in user://race/records.json:
##   {map: {ms, splits: [checkpoint ms ...], when: unix time, runs: [{ms, when} ...]}}
## runs are your fastest finishes on the map (RUNS_KEPT, best first): the Race leaderboard.
## A map has one record (id 1.32 movement is the only physics). The ghost of the best run sits beside
## it (Q3RaceGhost). A hand-edited or damaged file reads as no records.

static var path := "user://race/records.json"   ## tests point this somewhere else
static var ghost_dir := "user://race/ghosts"
const RUNS_KEPT := 10


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
	var runs: Array = cur.get("runs", []) if not cur.is_empty() else []
	d[map] = {"ms": ms, "splits": Array(splits), "when": int(Time.get_unix_time_from_system()), "runs": runs}
	return _write(d)


## Adds a finished run to the map's leaderboard (the fastest RUNS_KEPT stay). Call after submit() so
## the map has its entry; a map without one is ignored.
static func add_run(map: String, ms: int) -> void:
	var d := all()
	if ms <= 0 or not (d.get(map) is Dictionary):
		return
	var runs: Array = d[map].get("runs", [])
	runs.append({"ms": ms, "when": int(Time.get_unix_time_from_system())})
	runs.sort_custom(func(a, b): return int(a["ms"]) < int(b["ms"]))
	d[map]["runs"] = runs.slice(0, RUNS_KEPT)
	_write(d)


## What the Race leaderboard shows: {map: [{ms, when}, ...]} fastest first. A record saved before runs
## were kept counts as its one run.
static func board() -> Dictionary:
	var out := {}
	for m in all():
		var b := best(String(m))
		if b.is_empty():
			continue
		var runs: Array = []
		for r in b.get("runs", []):
			if r is Dictionary and int(r.get("ms", 0)) > 0:
				runs.append({"ms": int(r["ms"]), "when": int(r.get("when", 0))})
		if runs.is_empty():
			runs.append({"ms": int(b["ms"]), "when": int(b.get("when", 0))})
		out[m] = runs
	return out


static func _write(d: Dictionary) -> bool:
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
