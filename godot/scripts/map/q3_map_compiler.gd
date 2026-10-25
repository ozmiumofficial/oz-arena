class_name Q3MapCompiler
extends Node
## Compiles editor maps with id's q3map and bspc (MAP_EDITOR_PLAN.md E5), run as separate programs
## on a worker thread so a compile never freezes or crashes the game:
##   bsp    q3map <map>          brushes, curves and entities into a .bsp (a leak still writes one)
##   vis    q3map -vis <map>     what can see what (needs a sealed map)
##   light  q3map -light <map>   q3map's lightmaps (needs a sealed map)
##   aas    bspc -bsp2aas <bsp>  the bots' area file (needs a sealed map)
## q3map wants its files under a folder whose name holds "quake", so compiles happen in
## user://map_editor/compile/quake3/baseq3. It reads shader scripts as loose files, unpacked once
## from the player's own paks (shaderlist.txt is the level-editing kit's, so it's written here),
## and textures straight from the paks (Q3MAP_PAKPATH, tools/q3core/patches/id-q3a). The results
## go where q3core finds maps: user://q3/baseq3/maps (fs_homepath).

signal finished(result: Dictionary)

const ROOT := "user://map_editor/compile/quake3/baseq3"
const STAGES := ["bsp", "vis", "light", "aas"]
const WORK_NAME := "editor_map"   ## the .map q3map compiles; the results are copied out under the map's name
static var output_dir := "user://q3/baseq3/maps"     ## tests point this somewhere else

var busy := false
var _thread: Thread


static func tool_path(tool: String) -> String:
	return Q3Host.bin_path().path_join("tools/%s.exe" % tool)


## The player's baseq3 with the paks (the setup screen's), or "".
static func pak_dir() -> String:
	return Q3Ownership.remembered()


## Play-test names start with "ed_", so a map called q3dm17 can't hide the real one (a loose .bsp
## in fs_homepath wins over the paks).
static func output_name(map_name: String) -> String:
	return "ed_" + (map_name if map_name != "" else "untitled")


## A published map's own name, so a later play-test (bsp only) can't overwrite its full compile.
static func published_name(map_name: String) -> String:
	return "pub_" + (map_name if map_name != "" else "untitled")


## Unpacks the shader scripts if the paks changed since the last time. Returns "" or what's wrong.
static func ensure_kit() -> String:
	var paks := pak_dir()
	if paks == "" or not DirAccess.dir_exists_absolute(paks):
		return "Quake 3's files aren't set up (the game's setup screen points at them)."
	var names := []
	for f in DirAccess.get_files_at(paks):
		# id's own paks only: the editor's materials are Quake 3's, and a downloaded map's shader script
		# can crash id's 1999 parser (one beside "woodsteps" in a player's baseq3 stopped every compile)
		if f.to_lower().match("pak?.pk3"):
			names.append(f)
	names.sort()   # pak0 .. pak8: later paks override earlier ones, as Quake 3 loads them
	var stamp := ""
	for f in names:
		stamp += "%s:%d:%d;" % [f, FileAccess.get_size(paks.path_join(f)), FileAccess.get_modified_time(paks.path_join(f))]
	var scripts := ROOT.path_join("scripts")
	var stamp_path := scripts.path_join("kit.stamp")
	if FileAccess.file_exists(stamp_path) and FileAccess.get_file_as_string(stamp_path) == stamp:
		return ""
	DirAccess.make_dir_recursive_absolute(scripts)
	DirAccess.make_dir_recursive_absolute(ROOT.path_join("maps"))
	for old in DirAccess.get_files_at(scripts):   # scripts from other paks than these
		if old.ends_with(".shader"):
			DirAccess.remove_absolute(ProjectSettings.globalize_path(scripts.path_join(old)))
	var shaders := {}
	for f in names:
		var z := ZIPReader.new()
		if z.open(paks.path_join(f)) != OK:
			continue
		for entry in z.get_files():
			var low := entry.to_lower()
			if low.begins_with("scripts/") and low.ends_with(".shader"):
				var out := FileAccess.open(scripts.path_join(entry.get_file().to_lower()), FileAccess.WRITE)
				if out:
					out.store_buffer(z.read_file(entry))
					shaders[entry.get_file().to_lower().get_basename()] = true
		z.close()
	if shaders.is_empty():
		return "No shader scripts in Quake 3's paks at " + paks
	var list := shaders.keys()
	list.sort()
	var lf := FileAccess.open(scripts.path_join("shaderlist.txt"), FileAccess.WRITE)
	if lf == null:
		return "Couldn't write the shader list in " + ProjectSettings.globalize_path(scripts)
	lf.store_string("\n".join(list) + "\n")
	lf.close()
	var sf := FileAccess.open(stamp_path, FileAccess.WRITE)
	if sf:
		sf.store_string(stamp)
	return ""


## Starts a compile of map text (Q3MapExport) as `name`. `finished` brings
## {ok, name, stages, bsp, aas, leaked, leak: PackedVector3Array (id units), log, error, seconds}.
## Returns ERR_BUSY while one is running.
func compile(name: String, map_text: String, stages: Array) -> Error:
	if busy:
		return ERR_BUSY
	var err := ensure_kit()
	if err != "":
		finished.emit.call_deferred(_failed(name, stages, err))
		return OK
	for t in ["q3map", "bspc"]:
		if not FileAccess.file_exists(tool_path(t)):
			finished.emit.call_deferred(_failed(name, stages, "%s isn't built (python tools/build_q3core.py)" % t))
			return OK
	# always the same working name: q3map looks for "quake" in the path from the end, so a map named
	# quakefort would be taken for the game folder (SetQdirFromPath)
	var map_path := ROOT.path_join("maps/%s.map" % WORK_NAME)
	DirAccess.make_dir_recursive_absolute(map_path.get_base_dir())
	for ext in [".bsp", ".lin", ".prt", ".aas"]:
		var old: String = map_path.get_basename() + ext
		if FileAccess.file_exists(old):
			DirAccess.remove_absolute(ProjectSettings.globalize_path(old))
	var f := FileAccess.open(map_path, FileAccess.WRITE)
	if f == null:
		finished.emit.call_deferred(_failed(name, stages, "Couldn't write " + map_path))
		return OK
	f.store_string(map_text)
	f.close()
	# the tools inherit these: the player's paks, and where bspc keeps its log (else the game's folder)
	OS.set_environment("Q3MAP_PAKPATH", pak_dir())
	OS.set_environment("BSPC_LOG", ProjectSettings.globalize_path(ROOT.path_join("maps/bspc.log")))
	busy = true
	_thread = Thread.new()
	_thread.start(_work.bind({"name": name, "stages": stages, "map": ProjectSettings.globalize_path(map_path),
			"q3map": tool_path("q3map"), "bspc": tool_path("bspc"),
			"out": ProjectSettings.globalize_path(output_dir)}))
	return OK


func _failed(name: String, stages: Array, why: String) -> Dictionary:
	return {"ok": false, "name": name, "stages": stages, "error": why, "log": "", "leaked": false,
			"leak": PackedVector3Array(), "bsp": "", "aas": "", "seconds": 0.0}


## The worker thread: each stage in turn, stopping at the first failure.
func _work(job: Dictionary) -> void:
	var t0 := Time.get_ticks_msec()
	var r := _failed(job["name"], job["stages"], "")
	var map: String = job["map"]
	var bsp := map.get_basename() + ".bsp"
	var steps := PackedStringArray()
	for stage in job["stages"]:
		var exe: String = job["q3map"]
		var args := PackedStringArray()
		match stage:
			"bsp": args = [map]
			"vis": args = ["-vis", map]
			"light": args = ["-light", map]
			"aas":
				exe = job["bspc"]
				args = ["-bsp2aas", bsp]
		if stage in ["vis", "light", "aas"] and r["leaked"]:
			steps.append("(%s skipped: the map leaks)" % stage)
			continue
		var output := []
		var code := OS.execute(exe, args, output, true)
		var text := "\n".join(output).replace("\r", "")
		steps.append("> %s %s\n%s" % [exe.get_file(), " ".join(args), _trim_log(text)])
		if stage == "bsp" and text.contains("leaked"):
			r["leaked"] = true
			r["leak"] = _leak_line(map.get_basename() + ".lin")
		var made := FileAccess.file_exists(bsp) if stage != "aas" else FileAccess.file_exists(map.get_basename() + ".aas")
		if code != 0 or not made or text.contains("************ ERROR ************"):
			r["error"] = _error_of(text, stage, code)
			break
	if r["error"] == "":
		# into q3core's maps folder
		DirAccess.make_dir_recursive_absolute(job["out"])
		for ext in [".bsp", ".aas"]:
			var src: String = map.get_basename() + ext
			var dst: String = String(job["out"]).path_join(String(job["name"]) + ext)
			if FileAccess.file_exists(src):
				var cerr := DirAccess.copy_absolute(src, dst)
				if cerr != OK:
					r["error"] = "Couldn't copy %s to %s (error %d): is the map still loaded?" % [src.get_file(), dst, cerr]
					break
				r[ext.substr(1)] = dst
			elif FileAccess.file_exists(dst) and ext == ".aas":
				DirAccess.remove_absolute(dst)   # an old area file wouldn't match the new map
		r["ok"] = r["error"] == "" and r["bsp"] != ""
	r["log"] = "\n".join(steps)
	r["seconds"] = (Time.get_ticks_msec() - t0) / 1000.0
	_done.call_deferred(r)


func _done(r: Dictionary) -> void:
	if _thread:
		_thread.wait_to_finish()
		_thread = null
	busy = false
	finished.emit(r)


## q3map prints every shader script it reads ("entering ..."); those lines aren't worth keeping.
static func _trim_log(text: String) -> String:
	var keep := PackedStringArray()
	for line in text.split("\n"):
		if not line.begins_with("entering ") and not line.contains("Unknown surfaceparm: \"nomipmaps\""):
			keep.append(line)
	return "\n".join(keep).strip_edges()


## The line after q3map's or bspc's ERROR banner, or the exit code.
static func _error_of(text: String, stage: String, code: int) -> String:
	var lines := text.split("\n")
	for i in lines.size():
		if lines[i].contains("ERROR") and i + 1 < lines.size() and lines[i + 1].strip_edges() != "":
			return "%s: %s" % [stage, lines[i + 1].strip_edges()]
	if code != 0:
		return "%s stopped (exit code %d)" % [stage, code]
	return "%s produced nothing" % stage


## q3map's leak file (<map>.lin): the path from an entity out to the void, one point a line.
static func _leak_line(path: String) -> PackedVector3Array:
	var out := PackedVector3Array()
	if not FileAccess.file_exists(path):
		return out
	for line in FileAccess.get_file_as_string(path).split("\n"):
		var v := line.strip_edges().split_floats(" ", false)
		if v.size() == 3:
			out.append(Vector3(v[0], v[1], v[2]))
	return out


## Leaving mid-compile waits for the tool to finish (the editor refuses to leave while busy, so this
## is the game quitting); the wait isn't a freeze for Diagnostics.
func _exit_tree() -> void:
	if _thread and _thread.is_started():
		Diagnostics.expect_stall(true)
		_thread.wait_to_finish()
		Diagnostics.expect_stall(false)
		_thread = null
		busy = false
