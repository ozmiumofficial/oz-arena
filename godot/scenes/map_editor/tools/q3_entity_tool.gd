class_name Q3EntityTool
extends Q3EditorTool
## Places entities, after OZMIUM's EntityTool and LightTool. One tool with several kinds:
##   spawn (P)       info_player_deathmatch on the floor, facing the way the camera looks
##   jumppad (J)     click the pad, then where to land: the pad, its trigger_push and the
##                   target_position at the top of the arc, which lands on the second click
##   teleporter (K)  click the pad, then the exit: trigger_teleport and misc_teleporter_dest
##   weapon (I), item (O)  a pickup on the floor; , and . change which one
##   sound (N)       a looping target_speaker
##   light (L)       a light one grid step off the surface; Shift: a spot aimed at the surface
##   class           any point class picked in the Entities tab
## Clicking an entity selects it instead of placing one. Esc drops a half-placed pad.

const K = preload("res://scenes/map_editor/q3_editor_keymap.gd")

var kind := "spawn"
var classname := "info_player_deathmatch"   ## the class kind "class" places
var weapon := "weapon_rocketlauncher"
var item := "item_armor_combat"
var first: Variant = null      ## the pad's floor point after a jump pad or teleporter's first click
var _ghost: Node3D
var _ghost_key := ""
var _lines: MeshInstance3D
var _ceil_key := ""
var _ceil := INF


func _init(e: Q3MapEditor) -> void:
	super(e)
	title = "Entity"


func activate() -> void:
	first = null


func busy() -> bool:
	return first != null


func cancel() -> void:
	first = null
	_clear_ghost()


## The class this kind places now.
func current_class() -> String:
	match kind:
		"spawn": return "info_player_deathmatch"
		"jumppad": return "trigger_push"
		"teleporter": return "trigger_teleport"
		"weapon": return weapon
		"item": return item
		"sound": return "target_speaker"
		"light": return "light"
	return classname


## Where the cursor would put things: {point, normal, origin} in id units, or {} off the map.
func placement(pos: Vector2) -> Dictionary:
	var p := editor.pick(pos)
	var point: Variant
	var n := Vector3(0, 0, 1)
	if p.size():
		point = p["point"]
		n = p["normal"]
	else:
		point = editor.plane_point(pos, Vector3.ZERO, n)
	if point == null:
		return {}
	# on the grid along the surface, exactly on it across
	var axis := n.abs().max_axis_index()
	var base := editor.grid.snap_vec(point, true)
	base[axis] = point[axis]
	var cn := current_class()
	var origin := base
	var on_floor := n.z > 0.7
	if kind == "light":
		origin = editor.grid.snap_vec(point + n * maxf(editor.grid.units(), 16.0), true)
	elif on_floor:
		origin.z += -Q3MapEntities.bounds(cn).position.z if not Q3MapEntities.is_brush_class(cn) else 0.0
	else:
		origin += n * 16.0
	return {"point": base, "normal": n, "origin": origin, "floor": on_floor}


## The camera's heading as an id yaw, to 15 degrees.
func camera_yaw() -> float:
	var g := -editor.camera.global_transform.basis.z
	var a := rad_to_deg(atan2(g.x, g.z))
	return fposmod(roundf(a / 15.0) * 15.0, 360.0)


func click(pos: Vector2, mods: int, _double: bool) -> void:
	var ep := editor.pick(pos, true)
	if first == null and ep.size() and ep["kind"] == "entity" and not ep["locked"]:
		editor.select([ep["id"]], "toggle" if mods & K.CTRL else ("add" if mods & K.SHIFT else "set"))
		UiSound.move()
		return
	var pl := placement(pos)
	if pl.is_empty():
		return
	match kind:
		"jumppad", "teleporter":
			if first == null:
				first = pl["point"]
				UiSound.move()
				editor.ui.set_hint("")
				return
			if kind == "jumppad":
				editor.place_jump_pad(first, pl["point"])
			else:
				editor.place_teleporter(first, pl["point"], camera_yaw())
			first = null
			_clear_ghost()
		"light":
			editor.place_light(pl["origin"], pl["point"] if mods & K.SHIFT else null)
		"sound":
			editor.place_entity("target_speaker", pl["origin"], {"noise": "sound/world/drone6.wav", "spawnflags": "1"})
		_:
			var cn := current_class()
			if Q3MapEntities.is_brush_class(cn):
				editor.notify("Select brushes, then pick %s in the Entities tab to make them one" % cn, true)
				return
			var keys := {}
			if Q3MapEntities.faces(cn):
				keys["angle"] = Q3MapEditor._num(camera_yaw())
			editor.place_entity(cn, pl["origin"], keys)


func hover(pos: Vector2, _mods: int) -> void:
	var pl := placement(pos)
	if pl.is_empty():
		_clear_ghost()
		return
	_show_ghost(pl)


func key(ev: InputEventKey) -> bool:
	if ev.keycode == KEY_ESCAPE and first != null:
		cancel()
		editor.notify("Cancelled")
		return true
	return false


func hint() -> String:
	if first != null:
		return "Click where the player lands · Esc: cancel" if kind == "jumppad" else "Click where the teleporter comes out (facing the way you look) · Esc: cancel"
	match kind:
		"spawn": return "Click the floor for a spawn point (faces the way you look) · Shift+P: one where the camera is"
		"jumppad": return "Click the floor for a jump pad, then where to land · double-click its target to move it alone"
		"teleporter": return "Click the floor for a teleporter, then its exit"
		"weapon": return "Click the floor for a %s · , and . change the weapon" % Q3MapEntities.description(weapon)
		"item": return "Click the floor for %s · , and . change the item" % Q3MapEntities.description(item)
		"sound": return "Click to place a looping sound; pick it in the inspector"
		"light": return "Click: point light · Shift+click: spot aimed at the surface · Shift+L: lights-only preview"
	return "Click to place %s · %s" % [classname, Q3MapEntities.description(classname)]


# ---------------------------------------------------------------- ghost

func _clear_ghost() -> void:
	if _ghost:
		_ghost.queue_free()
		_ghost = null
		_ghost_key = ""
		_lines = null


func _show_ghost(pl: Dictionary) -> void:
	var cn := current_class()
	var ghost_key := cn + ("|pending" if first != null else "")
	if ghost_key != _ghost_key:
		_clear_ghost()
		_ghost_key = ghost_key
		_ghost = Node3D.new()
		_ghost.name = "EntityGhost"
		editor.add_child(_ghost)
		_lines = MeshInstance3D.new()
		_lines.material_override = Q3EditorOverlay._flat(Color.WHITE, true, true)
		_lines.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		_ghost.add_child(_lines)
		if not Q3MapEntities.is_brush_class(cn):
			var v := Q3EditorEntities.make_visual(Q3MapEntities.point(cn, Vector3.ZERO, {"angle": "0"}), null, false, false)
			if v:
				v.name = "Visual"
				_ghost.add_child(v)
	var vis := _ghost.get_node_or_null("Visual") as Node3D
	if vis:
		vis.position = Q3MapEditor.to_godot(pl["origin"])
		if Q3MapEntities.faces(cn):
			vis.rotation.y = deg_to_rad(camera_yaw())
	var im := ImmediateMesh.new()
	im.surface_begin(Mesh.PRIMITIVE_LINES)
	var c := Color(0.45, 0.85, 1.0)
	if kind in ["jumppad", "teleporter"]:
		var at: Vector3 = first if first != null else pl["point"]
		var h := 36.0 if kind == "jumppad" else 64.0
		_box(im, at + Vector3(-Q3MapEntities.PAD / 2, -Q3MapEntities.PAD / 2, 0), at + Vector3(Q3MapEntities.PAD / 2, Q3MapEntities.PAD / 2, h), c)
		if first != null:
			var from: Vector3 = first + Vector3(0, 0, h / 2)
			if kind == "jumppad":
				var g := Q3MapEntities.gravity(editor.doc)
				var apex := Q3MapEntities.pad_apex(first, pl["point"], g, _ceiling(first, pl["point"]))
				var land: float = pl["point"].z + 14.0
				var arc := Q3EditorEntities.arc_points(from, apex, g, func(_a, b: Vector3): return b if b.z <= land else null)
				Q3EditorEntities._draw_arc(im, from, apex, arc)
			else:
				Q3EditorEntities._line(im, from, pl["point"] + Vector3(0, 0, 24), c)
				Q3EditorEntities._arrow(im, pl["point"] + Vector3(0, 0, 24), camera_yaw(), c)
	elif kind == "light":
		Q3EditorEntities._line(im, pl["origin"], pl["point"], c)
	elif Q3MapEntities.faces(cn):
		Q3EditorEntities._arrow(im, pl["origin"], camera_yaw(), c)
	else:
		Q3EditorEntities._line(im, pl["origin"], pl["point"], c)
	im.surface_end()
	_lines.mesh = im


## The editor's ceiling_over for the ghost arc, worked out again only when the landing moves to
## another grid cell (it walks every brush).
func _ceiling(a: Vector3, b: Vector3) -> float:
	var key := "%s|%s" % [a, editor.grid.snap_vec(b, true)]
	if key != _ceil_key:
		_ceil_key = key
		_ceil = editor.ceiling_over(a, b)
	return _ceil


static func _box(im: ImmediateMesh, a: Vector3, b: Vector3, c: Color) -> void:
	var p := [Vector3(a.x, a.y, a.z), Vector3(b.x, a.y, a.z), Vector3(b.x, b.y, a.z), Vector3(a.x, b.y, a.z),
			Vector3(a.x, a.y, b.z), Vector3(b.x, a.y, b.z), Vector3(b.x, b.y, b.z), Vector3(a.x, b.y, b.z)]
	for e in [[0, 1], [1, 2], [2, 3], [3, 0], [4, 5], [5, 6], [6, 7], [7, 4], [0, 4], [1, 5], [2, 6], [3, 7]]:
		Q3EditorEntities._line(im, p[e[0]], p[e[1]], c)
