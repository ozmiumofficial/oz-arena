class_name Q3RaceCourses
extends Node
## The four starter courses (RACE_PLAN.md D6), made with the map editor's own tools and written to
## Maps/race_starter.pk3 (the repo's Maps folder: the editor lists them, release exports ship them):
##   Warm-up Hall   training   hurdles to jump, a straight run
##   Gap Runner     run        platforms over a pit, wider gaps, a stair to the finish
##   Target Practice accuracy  four wall buttons to shoot, then the finish
##   Shard Hunt     level      four armor shards to collect, then the finish
## Gaps: 96, 112, 128 and 144 units (a running jump clears about 200). Every course runs along +X from a spawn at the origin; id 1.32 movement only.
##   godot --path . res://tools/race_courses.tscn                (windowed: also renders the level shots)
##   godot --headless --path . res://tools/race_courses.tscn     (no level shots)
## tests/race_courses_test.tscn plays every course in the pk3 with scripted input.

const WALL := "textures/gothic_block/blocks15"
const FLOOR := "textures/base_floor/clangdark"
const TRIM := "textures/base_trim/dark_tin2"
const TRIGGER := "textures/common/trigger"
const PK3 := "race_starter.pk3"
const SCRATCH := "user://race_courses"
const AUTHOR := "Q3Remaster"

## id -> {name, style, camera: [id position, yaw, pitch]}
const COURSES := {
	"race_warmup": {"name": "Warm-up Hall", "style": "training", "camera": [Vector3(-40, 0, 110), 180.0, -9.0]},
	"race_gaps": {"name": "Gap Runner", "style": "run", "camera": [Vector3(-40, 0, 190), 180.0, -24.0]},
	"race_targets": {"name": "Target Practice", "style": "accuracy", "camera": [Vector3(-40, 0, 100), 180.0, -6.0]},
	"race_shards": {"name": "Shard Hunt", "style": "level", "camera": [Vector3(-40, 0, 100), 180.0, -6.0]},
}

var ed: Q3MapEditor
var shots := true


## The folder the pk3 goes to: the repo's Maps (next to godot/), or an override for tests.
static func out_dir() -> String:
	return ProjectSettings.globalize_path("res://").trim_suffix("/").get_base_dir().path_join("Maps")


# ---------------------------------------------------------------- geometry

static func _box(d: Q3MapDoc, a: Vector3, b: Vector3, mat: String) -> int:
	return d.add(Q3MapDoc.box_brush(a, b, mat))


## A closed hall along X from x0 to x1: floor, roof, side and end walls (16 thick), y +-128.
static func _hall(d: Q3MapDoc, x0: float, x1: float, z0: float, z1: float) -> void:
	_box(d, Vector3(x0, -128, z1), Vector3(x1, 128, z1 + 16), WALL)                  # roof
	_box(d, Vector3(x0 - 16, -144, z0 - 16), Vector3(x0, 144, z1 + 16), WALL)         # back wall
	_box(d, Vector3(x1, -144, z0 - 16), Vector3(x1 + 16, 144, z1 + 16), WALL)         # end wall
	_box(d, Vector3(x0, -144, z0 - 16), Vector3(x1, -128, z1 + 16), WALL)             # right wall
	_box(d, Vector3(x0, 128, z0 - 16), Vector3(x1, 144, z1 + 16), WALL)               # left wall


static func _lights(d: Q3MapDoc, x0: float, x1: float, z: float, every := 360.0, level := "500") -> void:
	var x := x0 + 120.0
	while x < x1:
		d.add(Q3MapEntities.point("light", Vector3(x, 0, z), {"light": level}))
		x += every


static func _base(name: String, title: String, style: String) -> Q3MapDoc:
	var d := Q3MapDoc.new()
	d.info["name"] = name
	d.worldspawn = {"message": title, "ambient": "10", "race_style": style, "race_author": AUTHOR}
	d.add(Q3MapEntities.point("info_player_deathmatch", Vector3(0, 0, 24), {"angle": "0"}))
	return d


## A trim stripe across the floor, so distance reads while running (decoration only).
static func _stripe(d: Q3MapDoc, x: float, z: float) -> void:
	_box(d, Vector3(x, -128, z), Vector3(x + 8, 128, z + 1), TRIM)


## id -> {doc, lines: [[kind, a, b]], then: Callable(editor)}: the geometry and entities that aren't a race line.
static func build(id: String) -> Dictionary:
	var c: Dictionary = COURSES[id]
	var d := _base(id, String(c["name"]), String(c["style"]))
	var lines: Array = []
	var then := Callable()
	match id:
		"race_warmup":
			_box(d, Vector3(-80, -128, -16), Vector3(1900, 128, 0), FLOOR)
			_hall(d, -80, 1900, 0, 160)
			for hx in [500, 900, 1300]:   # hurdles across the whole hall: jump them
				_box(d, Vector3(hx, -128, 0), Vector3(hx + 32, 128, 32 if hx < 1300 else 40), TRIM)
			for x in range(160, 1800, 160):
				_stripe(d, x, 0)
			_lights(d, -80, 1900, 140)
			lines = [["startline", Vector3(128, -128, 0), Vector3(128, 128, 0)],
					["checkpoint", Vector3(1040, -128, 0), Vector3(1040, 128, 0)],
					["finishline", Vector3(1700, -128, 0), Vector3(1700, 128, 0)]]
		"race_gaps":
			# platforms over a pit; the pit floor hurts. End to end: 0..2400
			var edges := [[-80, 400], [496, 820], [932, 1220], [1348, 1660], [1804, 2400]]
			for e in edges:
				_box(d, Vector3(e[0], -128, -16), Vector3(e[1], 128, 0), FLOOR)
			for e in edges:
				_stripe(d, e[1] - 24, 0)
			# a stair up at the end, three 16-unit steps, then the finish platform
			_box(d, Vector3(2100, -128, 0), Vector3(2148, 128, 16), TRIM)
			_box(d, Vector3(2148, -128, 0), Vector3(2196, 128, 32), TRIM)
			_box(d, Vector3(2196, -128, 0), Vector3(2400, 128, 48), TRIM)
			_hall(d, -80, 2400, -272, 176)
			_box(d, Vector3(-80, -128, -272), Vector3(2400, 128, -256), FLOOR)   # the pit's floor
			var hurt := d.add(Q3MapEntities.brush_entity("trigger_hurt", {"dmg": "1000", "spawnflags": "8"}))
			var hv := Q3MapDoc.box_brush(Vector3(-80, -128, -256), Vector3(2400, 128, -200), TRIGGER)
			hv["entity"] = hurt
			d.add(hv)
			_lights(d, -80, 2400, 150, 300.0, "600")
			lines = [["startline", Vector3(128, -128, 0), Vector3(128, 128, 0)],
					["checkpoint", Vector3(1060, -128, 0), Vector3(1060, 128, 0)],
					["checkpoint", Vector3(1500, -128, 0), Vector3(1500, 128, 0)],
					["finishline", Vector3(2300, -128, 48), Vector3(2300, 128, 48)]]
		"race_targets":
			_box(d, Vector3(-80, -128, -16), Vector3(1700, 128, 0), FLOOR)
			_hall(d, -80, 1700, 0, 160)
			# four buttons in the walls (a shot presses one); each adds a frag
			var n := 0
			for t in [[400, 1], [600, -1], [800, 1], [1000, -1]]:
				n += 1
				var name := "target%d" % n
				var dir: float = t[1]
				var bx := float(t[0])
				var button := d.add(Q3MapEntities.brush_entity("func_button", {"health": "1", "wait": "5", "target": name,
						"angle": "90" if dir > 0 else "270"}))
				var y0 := 104.0 if dir > 0 else -128.0
				var bb := Q3MapDoc.box_brush(Vector3(bx - 24, y0, 28), Vector3(bx + 24, y0 + 24, 68), TRIM)
				bb["entity"] = button
				d.add(bb)
				d.add(Q3MapEntities.point("target_score", Vector3(bx, 0, 120), {"targetname": name, "count": "1"}))
			_lights(d, -80, 1700, 140)
			lines = [["startline", Vector3(128, -128, 0), Vector3(128, 128, 0)],
					["finishline", Vector3(1500, -128, 0), Vector3(1500, 128, 0)]]
			then = func(editor: Q3MapEditor) -> void: Q3RaceCourses.wire_score_gate(editor, 4)
		"race_shards":
			_box(d, Vector3(-80, -128, -16), Vector3(1700, 128, 0), FLOOR)
			_hall(d, -80, 1700, 0, 160)
			var k := 0
			for s in [[500, -80], [800, 80], [1100, -80], [1400, 80]]:
				k += 1
				var name := "shard%d" % k
				d.add(Q3MapEntities.point("item_armor_shard", Vector3(s[0], s[1], 16), {"target": name, "wait": "5"}))
				d.add(Q3MapEntities.point("target_score", Vector3(s[0], 0, 120), {"targetname": name, "count": "1"}))
			_lights(d, -80, 1700, 140)
			lines = [["startline", Vector3(128, -128, 0), Vector3(128, 128, 0)],
					["finishline", Vector3(1560, -128, 0), Vector3(1560, 128, 0)]]
			then = func(editor: Q3MapEditor) -> void: Q3RaceCourses.wire_score_gate(editor, 4)
	return {"doc": d, "lines": lines, "then": then}


## Accuracy and level courses count frags: the start line zeroes the score (a restart mid-way keeps the
## frags it had), and the finish line only stops the timer for a player with `needed` of them. The finish
## trigger fires a target_fragsFilter, which fires the stop timer (Defrag's own idiom).
static func wire_score_gate(editor: Q3MapEditor, needed: int) -> void:
	var doc := editor.doc
	var stop: int = Q3MapRace.ids_of(doc, Q3MapRace.STOP)[0]
	var stop_name := String(doc.get_obj(stop)["keys"]["targetname"])        # finish1
	var gate_name := "gate1"
	editor.set_keys([stop], {"targetname": gate_name + "_stop"}, "Rename the stop timer")
	# the finish line's trigger names finish1: the filter takes that name, and fires the stop timer
	var at := Q3MapEntities.origin(doc.get_obj(stop))
	doc.add(Q3MapEntities.point("target_fragsFilter", at + Vector3(0, 24, 0),
			{"targetname": stop_name, "frags": str(needed), "target": gate_name + "_stop"}))
	var start: int = Q3MapRace.ids_of(doc, Q3MapRace.START)[0]
	var start_name := String(doc.get_obj(start)["keys"]["targetname"])
	var s_at := Q3MapEntities.origin(doc.get_obj(start))
	# frags 0, "Reset the score" (8) and "Silent" (4): always passes, and zeroes the score
	doc.add(Q3MapEntities.point("target_fragsFilter", s_at + Vector3(0, 24, 0),
			{"targetname": start_name, "frags": "0", "spawnflags": "12"}))


# ---------------------------------------------------------------- the build

func _ready() -> void:
	for a in OS.get_cmdline_user_args():
		if a == "noshots=1":
			shots = false
	shots = shots and DisplayServer.get_name() != "headless"
	_run.call_deferred()


func _run() -> void:
	Q3MapEditor.maps_dir = SCRATCH.path_join("maps")
	Q3MapEditor.recent = SCRATCH.path_join("recent.json")
	Q3MapMaterials.prefs_path = SCRATCH.path_join("materials.json")
	Q3EditorUi.layout_path = SCRATCH.path_join("layout.json")
	Q3MapMaterials._prefs = {}
	Q3MapPrefabs.dir = SCRATCH.path_join("prefabs")
	DirAccess.make_dir_recursive_absolute(Q3MapEditor.maps_dir)   # the thumbnails go here
	ed = load("res://scenes/map_editor/map_editor.tscn").instantiate()
	add_child(ed)
	await get_tree().process_frame
	if Q3MapCompiler.ensure_kit() != "":
		push_error("race courses: the q3map kit isn't there")
		get_tree().quit(1)
		return
	var c := Q3MapCompiler.new()
	add_child(c)
	var files := {}
	var ok := true
	for id in COURSES:
		var info: Dictionary = COURSES[id]
		var made := build(id)
		var d: Q3MapDoc = made["doc"]
		ed._set_doc(d, "")
		await get_tree().process_frame
		for l in made["lines"]:
			ed.place_race_line(l[0], l[1], l[2])
		if made["then"].is_valid():
			made["then"].call(ed)
		var probs := Q3MapRace.problems(ed.doc)
		for p in probs:
			print("  %s: %s %s" % [id, "ERROR" if p["error"] else "note", p["text"]])
			ok = ok and not p["error"]
		var ex := Q3MapExport.to_map(ed.doc)
		c.compile(id, ex["text"], ["bsp", "vis", "light"])
		var r: Dictionary = await c.finished
		print("%s: %s in %.0f s%s" % [id, "compiled" if r["ok"] else "FAILED", r["seconds"], " (leaks)" if r.get("leaked", false) else ""])
		if not r["ok"] or r["leaked"]:
			print(String(r["log"]).right(1500))
			ok = false
			continue
		files["maps/%s.bsp" % id] = FileAccess.get_file_as_bytes(r["bsp"])
		files["scripts/%s.defi" % id] = Q3MapRace.defi(id, ed.doc).to_utf8_buffer()
		files["scripts/%s.arena" % id] = ('{\n\tmap "%s"\n\tlongname "%s"\n\ttype "race"\n}\n' % [id, info["name"]]).to_utf8_buffer()
		if shots:
			var cam: Array = info["camera"]
			ed.execute("view.3d")
			ed.camera.set_pose(Q3MapEditor.to_godot(cam[0]), cam[1], cam[2])
			ed.clear_selection()
			for i in 8:
				await get_tree().process_frame
			ed.take_thumbnail(id)
			for i in 20:
				await get_tree().process_frame
			var shot := Q3MapEditor.thumb_path(id)
			if FileAccess.file_exists(shot):
				var img := Image.load_from_file(ProjectSettings.globalize_path(shot))
				if img and not img.is_empty():
					files["levelshots/%s.jpg" % id] = img.save_jpg_to_buffer(0.9)
		# the compile also leaves the map in the player's own folder; the pk3 is where it belongs
		for ext in [".bsp", ".aas"]:
			var p := Q3MapCompiler.output_dir.path_join(id + ext)
			if FileAccess.file_exists(p):
				DirAccess.remove_absolute(p)
	if not ok or files.is_empty():
		get_tree().quit(1)
		return
	files["readme.txt"] = README.to_utf8_buffer()
	var dest := out_dir().path_join(PK3)
	DirAccess.make_dir_recursive_absolute(out_dir())
	var zp := ZIPPacker.new()
	if zp.open(dest) != OK:
		push_error("race courses: can't write " + dest)
		get_tree().quit(1)
		return
	for f in files:
		zp.start_file(f)
		zp.write_file(files[f])
		zp.close_file()
	zp.close()
	print("wrote %s: %d files" % [dest, files.size()])
	get_tree().quit(0)


const README := """Q3Remaster starter race courses
================================

Four courses made with the map editor's race tools, for the Race mode. Quake 3's own movement (VQ3).

  race_warmup    Warm-up Hall      training   hurdles to jump, a straight run
  race_gaps      Gap Runner        run        platforms over a pit, wider gaps, a stair to the finish
  race_targets   Target Practice   accuracy   shoot four wall buttons, then reach the finish
  race_shards    Shard Hunt        level      collect four armor shards, then reach the finish

Made for the Q3Remaster fan project (GPL-2.0 for the code). The textures are Quake 3's, id Software's.
Rebuild them with: godot --path godot res://tools/race_courses.tscn
"""
