class_name Q3PrefabTool
extends Q3EditorTool
## Stamps a prefab, after OZMIUM's PlacePrefabTool: the prefab follows the cursor as a ghost in its
## real materials, its bottom center on the surface and its corner on the grid; R turns it 90° (Shift
## the other way), a click places a grouped copy, Esc goes back to the select tool.

var prefab := ""                 ## the prefab's name
var objects: Array = []          ## as saved
var turns := 0                   ## quarter turns about the vertical
var at := Vector3.ZERO           ## where the next copy's min corner goes (id)
var _turned: Array = []          ## objects turned, min corner at the origin
var _size := Vector3.ZERO
var _ghost: Node3D
var _valid := false


func _init(e: Q3MapEditor) -> void:
	super(e)
	title = "Prefab"


func set_prefab(name: String, objs: Array) -> void:
	prefab = name
	objects = objs
	turns = 0
	_rebuild()


func cancel() -> void:
	_valid = false
	if _ghost:
		_ghost.visible = false


func activate() -> void:
	if objects.size() and _ghost == null:
		_rebuild()


## The prefab turned by `turns` quarter turns, moved so its min corner sits at the origin.
static func turned(objs: Array, quarter_turns: int) -> Array:
	var tmp := Q3MapDoc.new()
	for o in objs:
		tmp.objects[int(o["id"])] = o
	var box := Q3MapEditor.bounds_in(tmp, tmp.objects.keys())
	var out := []
	var rot := Q3MapDoc.yaw_rotation(90.0 * posmod(quarter_turns, 4), box.get_center())
	for o in objs:
		out.append(Q3MapEditor.transform_object(o, rot, 90.0 * posmod(quarter_turns, 4)))
	tmp.objects.clear()
	for o in out:
		tmp.objects[int(o["id"])] = o
	var b2 := Q3MapEditor.bounds_in(tmp, tmp.objects.keys())
	var back := Q3MapDoc.translation(-b2.position)
	return out.map(func(o): return Q3MapEditor.transform_object(o, back))


func _rebuild() -> void:
	if _ghost:
		_ghost.queue_free()
		_ghost = null
	_turned = turned(objects, turns)
	var tmp := Q3MapDoc.new()
	for o in _turned:
		tmp.objects[int(o["id"])] = o
	_size = Q3MapEditor.bounds_in(tmp, tmp.objects.keys()).size
	_ghost = Node3D.new()
	_ghost.name = "PrefabGhost"
	_ghost.visible = false
	editor.add_child(_ghost)
	var mesher := Q3MapMesher.new()
	_ghost.add_child(mesher)
	mesher.bind(tmp)
	var ents := Q3EditorEntities.new()
	ents.labels = false
	_ghost.add_child(ents)
	ents.bind(tmp)
	# an outline of the whole prefab on top, so it reads as a ghost
	var edges := MeshInstance3D.new()
	edges.material_override = Q3EditorOverlay._flat(Color(0.45, 0.85, 1.0, 0.8), true, true)
	var im := ImmediateMesh.new()
	im.surface_begin(Mesh.PRIMITIVE_LINES)
	Q3EntityTool._box(im, Vector3.ZERO, _size, Color(0.45, 0.85, 1.0))
	im.surface_end()
	edges.mesh = im
	_ghost.add_child(edges)


func free_ghost() -> void:
	if _ghost:
		_ghost.queue_free()
		_ghost = null


func hover(pos: Vector2, mods: int) -> void:
	var p: Variant = editor.cursor_point(pos)
	_valid = p != null and objects.size() > 0
	if not _valid:
		if _ghost:
			_ghost.visible = false
		return
	var target: Vector3 = p - Vector3(_size.x * 0.5, _size.y * 0.5, 0)
	if mods & Q3EditorKeymap.CTRL == 0:
		target.x = editor.grid.snap(target.x, true)
		target.y = editor.grid.snap(target.y, true)
	at = target
	if _ghost:
		_ghost.position = Q3MapEditor.to_godot(at)
		_ghost.visible = true


func click(pos: Vector2, mods: int, _double: bool) -> void:
	hover(pos, mods)
	if _valid:
		editor.place_prefab(prefab, _turned, at)


func key(ev: InputEventKey) -> bool:
	match ev.keycode:
		KEY_R:
			if ev.ctrl_pressed:
				return false
			turns += -1 if ev.shift_pressed else 1
			_rebuild()
			hover(editor.mouse_pos, 0)
			UiSound.move()
			return true
		KEY_ESCAPE:
			editor.set_tool("select")
			return true
	return false


func hint() -> String:
	if prefab == "":
		return "Pick a prefab in the Prefabs tab"
	return "Click to place '%s' · R / Shift+R: turn 90° · Ctrl: off the grid · Esc: done" % prefab
