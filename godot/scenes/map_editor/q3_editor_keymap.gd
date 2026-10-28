class_name Q3EditorKeymap
extends RefCounted
## The map editor's hotkeys, after OZMIUM's MapEditorKeymap: one table that the dispatcher reads and
## the F1 sheet is drawn from, so the two never disagree. The player's rebinds sit on top in
## user://map_editor/keymap.json.

const CTRL := 1
const SHIFT := 2
const ALT := 4
const OVERRIDES := "user://map_editor/keymap.json"

## [command, keycode, mods, category, label]; mouse rows have command "" and the gesture as text.
const BINDINGS := [
	# Tools
	["tool.select", KEY_ESCAPE, 0, "Tools", "Select tool / cancel / deselect"],
	["tool.create", KEY_B, 0, "Tools", "Create brush"],
	["csg.subtract", KEY_X, CTRL | SHIFT, "Edit", "CSG subtract: carve the selected brushes out of the brushes they overlap, then delete them"],
	["tool.face", KEY_T, 0, "Tools", "Face / texture tool"],
	["tool.painttarget", KEY_T, SHIFT, "Tools", "Face tool: single faces or whole objects"],
	["face.lockTexture", KEY_T, CTRL, "Faces", "Lock / free the texture on the selected faces or brushes (a locked texture moves with its brush)"],
	["tool.vertex", KEY_V, 0, "Tools", "Vertex tool (corners and patch control points)"],
	["tool.spawn", KEY_P, 0, "Entities", "Spawn point tool (faces the way you look)"],
	["entity.spawnAtCamera", KEY_P, SHIFT, "Entities", "Spawn point where the camera is"],
	["tool.jumppad", KEY_J, 0, "Entities", "Jump pad tool (click the pad, then where to land)"],
	["tool.teleporter", KEY_K, 0, "Entities", "Teleporter tool (click the pad, then the exit)"],
	["tool.weapon", KEY_I, 0, "Entities", "Weapon pickup tool"],
	["tool.item", KEY_O, 0, "Entities", "Item pickup tool: health, armor, ammo, powerups"],
	["tool.sound", KEY_N, 0, "Entities", "Ambient sound tool (a looping target_speaker)"],
	["tool.light", KEY_L, 0, "Entities", "Light tool (Shift+click: spot light aimed at the surface)"],
	["tool.startline", KEY_Y, 0, "Race", "Start line tool (click one side of the course, then the other)"],
	["tool.finishline", KEY_U, 0, "Race", "Finish line tool"],
	["tool.checkpoint", KEY_C, 0, "Race", "Checkpoint tool"],
	["race.check", KEY_C, SHIFT, "Race", "Check the race course (what's missing or never fired)"],
	["race.settings", KEY_Y, SHIFT, "Race", "Course settings: style and author"],
	["pickup.prev", KEY_COMMA, 0, "Entities", "Previous weapon / item (the tool, or the selected pickups)"],
	["pickup.next", KEY_PERIOD, 0, "Entities", "Next weapon / item"],
	["prefab.save", KEY_P, CTRL, "Entities", "Save the selection as a prefab"],
	["", "R (prefab tool)", 0, "Entities", "Turn the prefab 90° (Shift+R the other way)"],
	["", "LMB", 0, "Tools", "Select (Shift add, Ctrl toggle)"],
	["", "LMB drag", 0, "Tools", "Move the selection on the grid (Alt vertical, Ctrl no snap)"],
	["", "LMB drag on empty", 0, "Tools", "Box select"],
	["", "LMB drag a diamond", 0, "Tools", "Push / pull that face of a selected brush (Ctrl no snap)"],
	["", "Shift+LMB drag a face", 0, "Tools", "Push / pull the face under the cursor"],
	["", "LMB a corner square", 0, "Tools", "Pick the corner (drag moves it, Del removes it)"],
	["", "Double-click a group", 0, "Tools", "Select just the object under the cursor"],
	# Grid
	["grid.1", KEY_1, 0, "Grid", "Grid 1 unit"],
	["grid.2", KEY_2, 0, "Grid", "Grid 2 units"],
	["grid.3", KEY_3, 0, "Grid", "Grid 4 units"],
	["grid.4", KEY_4, 0, "Grid", "Grid 8 units"],
	["grid.5", KEY_5, 0, "Grid", "Grid 16 units (0.5 m)"],
	["grid.6", KEY_6, 0, "Grid", "Grid 32 units (1 m)"],
	["grid.7", KEY_7, 0, "Grid", "Grid 64 units (2 m)"],
	["grid.8", KEY_8, 0, "Grid", "Grid 128 units (4 m)"],
	["grid.9", KEY_9, 0, "Grid", "Grid 256 units (8 m)"],
	["grid.smaller", KEY_BRACKETLEFT, 0, "Grid", "Smaller grid"],
	["grid.bigger", KEY_BRACKETRIGHT, 0, "Grid", "Bigger grid"],
	["grid.toggle", KEY_G, 0, "Grid", "Show / hide grid"],
	["grid.snap", KEY_G, SHIFT, "Grid", "Grid snapping on / off (Ctrl while dragging skips it once)"],
	["grid.vertexSnap", KEY_V, SHIFT, "Grid", "Vertex snap: dragged corners land on other brushes' corners"],
	["", "Ctrl+Scroll", 0, "Grid", "Step grid size"],
	# Edit
	["edit.undo", KEY_Z, CTRL, "Edit", "Undo"],
	["edit.redo", KEY_Y, CTRL, "Edit", "Redo"],
	["edit.redo", KEY_Z, CTRL | SHIFT, "Edit", "Redo"],
	["edit.delete", KEY_DELETE, 0, "Edit", "Delete selection"],
	["edit.delete", KEY_BACKSPACE, 0, "Edit", "Delete selection"],
	["edit.duplicate", KEY_D, CTRL, "Edit", "Duplicate selection"],
	["edit.copy", KEY_C, CTRL, "Edit", "Copy"],
	["edit.paste", KEY_V, CTRL, "Edit", "Paste at cursor"],
	["select.all", KEY_A, CTRL, "Edit", "Select all"],
	["edit.rotate", KEY_R, 0, "Edit", "Rotate 90° around the vertical"],
	["edit.rotateNeg", KEY_R, SHIFT, "Edit", "Rotate -90° around the vertical"],
	["edit.rotate15", KEY_R, CTRL, "Edit", "Rotate +15° around the vertical"],
	["edit.rotate15Neg", KEY_R, CTRL | SHIFT, "Edit", "Rotate -15° around the vertical"],
	["edit.mirrorX", KEY_M, 0, "Edit", "Mirror the selection front-back (across X)"],
	["edit.mirrorY", KEY_M, SHIFT, "Edit", "Mirror the selection left-right (across Y)"],
	["edit.mirrorZ", KEY_M, CTRL | SHIFT, "Edit", "Mirror the selection upside down (across Z)"],
	["edit.nudge.left", KEY_LEFT, 0, "Edit", "Nudge left one grid step"],
	["edit.nudge.right", KEY_RIGHT, 0, "Edit", "Nudge right"],
	["edit.nudge.forward", KEY_UP, 0, "Edit", "Nudge forward"],
	["edit.nudge.back", KEY_DOWN, 0, "Edit", "Nudge back"],
	["edit.nudge.up", KEY_PAGEUP, 0, "Edit", "Nudge up"],
	["edit.nudge.down", KEY_PAGEDOWN, 0, "Edit", "Nudge down"],
	["edit.hide", KEY_H, 0, "Edit", "Hide selection"],
	["edit.unhideAll", KEY_H, SHIFT, "Edit", "Unhide all"],
	["edit.lock", KEY_L, CTRL, "Edit", "Lock selection (visible, but the viewport can't pick it)"],
	["edit.unlockAll", KEY_L, CTRL | SHIFT, "Edit", "Unlock all"],
	["view.isolate", KEY_I, SHIFT, "Edit", "Isolate selection: hide everything else, press again to restore"],
	["edit.group", KEY_G, CTRL, "Edit", "Group selection"],
	["edit.ungroup", KEY_U, CTRL, "Edit", "Ungroup selection"],
	["edit.snapVertices", KEY_G, CTRL | SHIFT, "Edit", "Snap selected vertices to the grid"],
	# Face tool
	["", "LMB drag face", 0, "Faces", "Push / pull face along its normal"],
	["", "Alt+LMB", 0, "Faces", "Eyedropper: pick a face's material and alignment"],
	["", "Arrows", 0, "Faces", "Shift texture (Ctrl+Up/Down scale, Ctrl+Left/Right rotate)"],
	# File
	["file.new", KEY_N, CTRL, "File", "New map"],
	["file.open", KEY_O, CTRL, "File", "Open map"],
	["file.save", KEY_S, CTRL, "File", "Save"],
	["file.saveAs", KEY_S, CTRL | SHIFT, "File", "Save as"],
	["file.publish", KEY_P, CTRL | SHIFT, "File", "Publish: compile in full (vis, light, bots) and list the map in Create Match"],
	["file.importQ3", KEY_I, CTRL, "File", "Import one of the original maps from your paks (its .bsp, as editable brushes)"],
	["file.importMap", KEY_O, CTRL | SHIFT, "File", "Import a Radiant .map (GtkRadiant, NetRadiant, TrenchBroom)"],
	["file.importBsp", KEY_I, CTRL | SHIFT, "File", "Import any .bsp file, or a map from a .pk3 package"],
	["file.exportBsp", KEY_B, CTRL | SHIFT, "File", "Export the compiled map as a .bsp, or a .pk3 that plays in the original game (compiles in full)"],
	["file.exportMap", KEY_E, CTRL | SHIFT, "File", "Export a .map for GtkRadiant and NetRadiant (brush primitives)"],
	["file.exportValve", KEY_E, CTRL | SHIFT | ALT, "File", "Export a .map for TrenchBroom (Valve 220)"],
	["session.leave", KEY_M, CTRL, "File", "Leave to the main menu (asks about unsaved changes)"],
	# View
	["view.focus", KEY_F, 0, "View", "Frame selection"],
	["view.home", KEY_HOME, 0, "View", "Reset camera"],
	["help.cheatsheet", KEY_F1, 0, "View", "Hotkey sheet"],
	["view.explorer", KEY_TAB, 0, "View", "Show / hide the browser and inspector"],
	["view.top", KEY_F2, 0, "View", "Top view (2D, X right, Y up); again: back to 3D"],
	["view.front", KEY_F3, 0, "View", "Front view (2D, X right, Z up); again: back to 3D"],
	["view.side", KEY_F4, 0, "View", "Side view (2D, Y right, Z up); again: back to 3D"],
	["view.lighting", KEY_L, SHIFT, "View", "Lighting preview: the map's own lights only"],
	["view.filter.caulk", KEY_1, ALT, "View", "Hide / show caulk"],
	["view.filter.tools", KEY_2, ALT, "View", "Hide / show clip, hint, triggers and other tool brushes"],
	["view.filter.sky", KEY_3, ALT, "View", "Hide / show sky brushes"],
	["view.filter.fog", KEY_4, ALT, "View", "Hide / show fog"],
	["view.filter.curves", KEY_5, ALT, "View", "Hide / show curves"],
	["view.filter.entities", KEY_6, ALT, "View", "Hide / show entities"],
	["view.filter.none", KEY_0, ALT, "View", "Show everything the view filters hide"],
	["map.playtest", KEY_F5, 0, "Play", "Play-test: play the map from where the camera is (F6 in the game comes back); an unchanged map skips the compile"],
	["map.compile", KEY_F5, CTRL, "Play", "Full compile (bsp, vis, light, bots) to check the map; a leak is drawn in red"],
	["map.bots", KEY_F5, SHIFT, "Play", "Play-test with or without bots"],
	["map.fromCamera", KEY_F5, ALT, "Play", "Play-tests start where the camera is, or at a spawn point"],
	["", "RMB hold", 0, "View", "Look around"],
	["", "W A S D / Q E", 0, "View", "Fly while looking (Shift fast, Ctrl slow)"],
	["", "Scroll", 0, "View", "Dolly forward / back (2D views: zoom)"],
	["", "RMB drag (2D)", 0, "View", "Pan a 2D view"],
	["", "Click again", 0, "Tools", "Select the next object behind the one picked (a 2D view's floor under its roof)"],
]

static var _effective: Array = []
static var version := 0      ## bumps on every change, so the sheet knows to redraw


## The bindings in force: the defaults with keymap.json on top.
static func effective() -> Array:
	if _effective.is_empty():
		load_overrides()
	return _effective


static func load_overrides() -> void:
	_effective = []
	for b in BINDINGS:
		_effective.append(b.duplicate())
	var data: Variant = JSON.parse_string(FileAccess.get_file_as_string(OVERRIDES)) if FileAccess.file_exists(OVERRIDES) else null
	if data is Dictionary:
		for o in data.get("overrides", []):
			var i := _index_of(String(o.get("command", "")), int(o.get("slot", 0)))
			if i >= 0:
				_effective[i][1] = int(o.get("key", 0))
				_effective[i][2] = int(o.get("mods", 0))
	version += 1


## The nth keyboard binding of a command (edit.redo has two).
static func _index_of(command: String, slot: int) -> int:
	var n := 0
	for i in BINDINGS.size():
		if BINDINGS[i][0] == command and command != "":
			if n == slot:
				return i
			n += 1
	return -1


static func slot_of(index: int) -> int:
	var n := 0
	for i in index:
		if BINDINGS[i][0] == BINDINGS[index][0]:
			n += 1
	return n


static func mods_of(ev: InputEventWithModifiers) -> int:
	return (CTRL if ev.ctrl_pressed or ev.meta_pressed else 0) | (SHIFT if ev.shift_pressed else 0) | (ALT if ev.alt_pressed else 0)


## Commands bound to this key press, exact modifiers only.
static func commands_for(ev: InputEventKey) -> PackedStringArray:
	var out := PackedStringArray()
	var key := ev.keycode if ev.keycode != KEY_NONE else ev.physical_keycode
	var m := mods_of(ev)
	for b in effective():
		if b[0] != "" and b[1] is int and b[1] == key and b[2] == m:
			out.append(b[0])
	return out


## Gives binding `index` a new key and saves the overlay. Returns the label of another command on
## the same key (the rebind still happens; the sheet shows the clash), or "".
static func rebind(index: int, key: int, mods: int) -> String:
	effective()
	_effective[index][1] = key
	_effective[index][2] = mods
	_save()
	version += 1
	for i in _effective.size():
		if i != index and _effective[i][0] != "" and _effective[i][1] is int and _effective[i][1] == key and _effective[i][2] == mods:
			return _effective[i][4]
	return ""


static func reset() -> void:
	if FileAccess.file_exists(OVERRIDES):
		DirAccess.remove_absolute(ProjectSettings.globalize_path(OVERRIDES))
	load_overrides()


static func _save() -> void:
	var overrides := []
	for i in BINDINGS.size():
		var d: Array = BINDINGS[i]
		var b: Array = _effective[i]
		if d[0] != "" and (b[1] != d[1] or b[2] != d[2]):
			overrides.append({"command": d[0], "slot": slot_of(i), "key": b[1], "mods": b[2]})
	DirAccess.make_dir_recursive_absolute(OVERRIDES.get_base_dir())
	var f := FileAccess.open(OVERRIDES, FileAccess.WRITE)
	if f:
		f.store_string(JSON.stringify({"overrides": overrides}, "\t"))


static func conflict(index: int) -> bool:
	var b: Array = effective()[index]
	if b[0] == "":
		return false
	for i in _effective.size():
		var o: Array = _effective[i]
		if i != index and o[0] != "" and o[1] is int and o[1] == b[1] and o[2] == b[2]:
			return true
	return false


static func key_text(b: Array) -> String:
	if b[0] == "":
		return String(b[1])
	var s := ""
	if b[2] & CTRL:
		s += "Ctrl+"
	if b[2] & SHIFT:
		s += "Shift+"
	if b[2] & ALT:
		s += "Alt+"
	match int(b[1]):
		KEY_ESCAPE: return s + "Esc"
		KEY_DELETE: return s + "Del"
		KEY_PAGEUP: return s + "PgUp"
		KEY_PAGEDOWN: return s + "PgDn"
		KEY_LEFT: return s + "←"
		KEY_RIGHT: return s + "→"
		KEY_UP: return s + "↑"
		KEY_DOWN: return s + "↓"
		KEY_BRACKETLEFT: return s + "["
		KEY_BRACKETRIGHT: return s + "]"
		KEY_COMMA: return s + ","
		KEY_PERIOD: return s + "."
	return s + OS.get_keycode_string(int(b[1]))


## The first key shown for a command ("" if unbound), for tooltips and hints.
static func key_for(command: String) -> String:
	for b in effective():
		if b[0] == command:
			return key_text(b)
	return ""
