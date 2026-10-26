class_name Q3MapRace
extends RefCounted
## A map as a Defrag course (RACE_PLAN.md D5): which entities make it one, its settings, and the
## checks the editor runs on it. A course has a start line (target_startTimer, fired by a trigger the
## player crosses) and a finish line (target_stopTimer), with checkpoints between. The settings live on
## worldspawn: "message" is the course's name, "race_style" and "race_author" what the menu lists.

const START := "target_startTimer"
const STOP := "target_stopTimer"
const CHECKPOINT := "target_checkpoint"
const STYLES := ["run", "training", "accuracy", "level"]
const TRIGGER_HEIGHT := 128.0    ## a line's volume, floor to this high (or the roof)
const LINE_DEPTH := 32.0         ## and how thick, along the way the player runs

## the classes Race adds, for the editor's checks
const CLASSES := ["target_startTimer", "target_stopTimer", "target_checkpoint", "target_fragsFilter", "target_init",
		"target_smallprint", "target_speed", "target_multimanager", "trigger_push_velocity",
		"shooter_rocket_targetplayer", "shooter_plasma_targetplayer", "shooter_grenade_targetplayer"]


static func ids_of(doc: Q3MapDoc, cn: String) -> Array:
	return doc.ids_of("entity").filter(func(i): return String(doc.get_obj(i)["classname"]) == cn)


## A start and a finish: what makes a map a course (and a race play-test).
static func has_course(doc: Q3MapDoc) -> bool:
	return not ids_of(doc, START).is_empty() and not ids_of(doc, STOP).is_empty()


static func style(doc: Q3MapDoc) -> String:
	var s := String(doc.worldspawn.get("race_style", "run")).to_lower()
	return s if s in STYLES else "run"


static func author(doc: Q3MapDoc) -> String:
	return String(doc.worldspawn.get("race_author", "")).strip_edges()


## "1 start, 1 finish, 3 checkpoints" for a toast.
static func summary(doc: Q3MapDoc) -> String:
	var cps := ids_of(doc, CHECKPOINT).size()
	return "%d start, %d finish, %d checkpoint%s" % [ids_of(doc, START).size(), ids_of(doc, STOP).size(), cps, "" if cps == 1 else "s"]


## Defrag's map definition file (scripts/<map>.defi) for an exported package, so the game's menus list
## the course: id 1.32 movement only.
static func defi(map: String, doc: Q3MapDoc) -> String:
	var name := String(doc.worldspawn.get("message", "")).replace("\\n", " ").replace("\"", "'").strip_edges()
	return "{\nmap \"%s\"\nlongname \"%s\"\nstyle \"%s\"\ncpm \"0\"\nvq3 \"1\"\nauthor \"%s\"\n}\n" % [
			map, name if name != "" else map, style(doc), author(doc).replace("\"", "'")]


## What is wrong with the course, as [{text, ids, error}]: errors stop a publish, the rest are worth a
## look. `ids` are the objects to select. A map with no race entities at all has no problems: it isn't
## a course.
static func problems(doc: Q3MapDoc) -> Array:
	var out := []
	var present := false
	for cn in CLASSES:
		if not ids_of(doc, cn).is_empty():
			present = true
	if not present:
		return out
	var starts := ids_of(doc, START)
	var stops := ids_of(doc, STOP)
	var cps := ids_of(doc, CHECKPOINT)
	var names := Q3MapEntities.targetnames(doc)
	var firers := {}   # targetname -> the entities with that "target"
	for id in doc.ids_of("entity"):
		var t := String(doc.get_obj(id)["keys"].get("target", ""))
		if t != "":
			if not firers.has(t):
				firers[t] = []
			firers[t].append(id)
	if starts.is_empty():
		out.append({"text": "The course has no start line (Y places one).", "ids": [], "error": true})
	if stops.is_empty():
		out.append({"text": "The course has no finish line (U places one).", "ids": [], "error": true})
	if starts.size() > 1:
		out.append({"text": "%d start lines: crossing any of them starts the timer." % starts.size(), "ids": starts, "error": false})
	var spawns: Array = doc.ids_of("entity").filter(func(i): return String(doc.get_obj(i)["classname"]) in ["info_player_deathmatch", "info_player_start"])
	if spawns.is_empty():
		out.append({"text": "The course has no spawn point (P places one).", "ids": [], "error": true})
	# a timer nothing fires does nothing
	for pair in [["start line", starts], ["finish line", stops], ["checkpoint", cps]]:
		for id in pair[1]:
			var tn := String(doc.get_obj(id)["keys"].get("targetname", ""))
			if tn == "" or not firers.has(tn):
				out.append({"text": "A %s isn't fired by any trigger: nothing will ever reach it." % pair[0], "ids": [id], "error": pair[0] != "checkpoint"})
	# a spawn inside a start line starts the timer at once
	var own := Q3MapEntities.owned(doc)
	for s in starts:
		var tn := String(doc.get_obj(s)["keys"].get("targetname", ""))
		for trig in firers.get(tn, []):
			var te: Dictionary = doc.get_obj(trig)
			if not Q3MapEntities.is_brush_class(String(te["classname"])) or not own.has(trig):
				continue
			var box: AABB = Q3MapEditor.bounds_in(doc, own[trig])
			for sp in spawns:
				if box.has_point(Q3MapEntities.origin(doc.get_obj(sp))):
					out.append({"text": "A spawn point is inside the start line: the timer starts the moment the player spawns.", "ids": [sp, trig], "error": false})
	# a race entity pointing at a name nobody has
	for id in doc.ids_of("entity"):
		var e: Dictionary = doc.get_obj(id)
		if String(e["classname"]) not in CLASSES:
			continue
		var t := String(e["keys"].get("target", ""))
		if t != "" and not names.has(t):
			out.append({"text": "%s points at \"%s\", which nothing is named." % [e["classname"], t], "ids": [id], "error": false})
	# items that never come back (Defrag: wait -1 respawns only when triggered)
	for id in doc.ids_of("entity"):
		var e: Dictionary = doc.get_obj(id)
		if Q3MapEntities.is_item(String(e["classname"])) and String(e["keys"].get("wait", "")) == "-1":
			var tn := String(e["keys"].get("targetname", ""))
			if tn == "" or not firers.has(tn):
				out.append({"text": "A pickup with wait -1 never comes back, and nothing respawns it: a restart will find it gone.", "ids": [id], "error": false})
	return out
