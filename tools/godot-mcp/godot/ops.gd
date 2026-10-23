extends SceneTree
# Headless operations runner for godot-mcp.
# Invoked as: godot --headless --path <project> --script ops.gd -- <base64-json>
# Prints one line: GODOT_MCP_RESULT:{"ok":true,"result":...} or {"ok":false,"error":"..."}

func _initialize() -> void:
	var result := {}
	var user_args := OS.get_cmdline_user_args()
	if user_args.size() < 1:
		_emit(false, null, "missing base64 payload argument")
		return
	var payload_text := Marshalls.base64_to_utf8(user_args[0])
	var parsed = JSON.parse_string(payload_text)
	if parsed == null or typeof(parsed) != TYPE_DICTIONARY:
		_emit(false, null, "could not parse payload JSON")
		return
	var args: Dictionary = parsed
	var op: String = args.get("op", "")
	match op:
		"get_tree":
			_op_get_tree(args)
		"create_scene":
			_op_create_scene(args)
		"add_node":
			_op_add_node(args)
		"set_property":
			_op_set_property(args)
		"get_property":
			_op_get_property(args)
		"attach_script":
			_op_attach_script(args)
		"validate_script":
			_op_validate_script(args)
		_:
			_emit(false, null, "unknown op: " + op)

func _emit(ok: bool, result, error) -> void:
	var out := {"ok": ok}
	if ok:
		out["result"] = result
	else:
		out["error"] = str(error)
	print("GODOT_MCP_RESULT:" + JSON.stringify(out))
	quit(0)

func _jsonify(value):
	match typeof(value):
		TYPE_NIL, TYPE_BOOL, TYPE_INT, TYPE_FLOAT, TYPE_STRING:
			return value
		TYPE_ARRAY:
			var arr := []
			for v in value:
				arr.append(_jsonify(v))
			return arr
		TYPE_DICTIONARY:
			var d := {}
			for k in value.keys():
				d[str(k)] = _jsonify(value[k])
			return d
		_:
			return str(value)

func _convert_value(value):
	# Strings that look like Godot constructors ("Vector2(1, 2)") are parsed
	# with str_to_var; everything else passes through as-is.
	if typeof(value) == TYPE_STRING:
		var re := RegEx.new()
		re.compile("^[A-Z][A-Za-z0-9]*\\s*\\(.*\\)$")
		if re.search(value) != null:
			var parsed = str_to_var(value)
			if parsed != null:
				return parsed
	return value

func _load_scene(args: Dictionary):
	var scene_path: String = args.get("scene", "")
	if not ResourceLoader.exists(scene_path):
		_emit(false, null, "scene not found: " + scene_path)
		return null
	var packed = load(scene_path)
	if packed == null or not (packed is PackedScene):
		_emit(false, null, "could not load scene: " + scene_path)
		return null
	var root = packed.instantiate()
	if root == null:
		_emit(false, null, "could not instantiate scene: " + scene_path)
		return null
	return root

func _save_scene(root: Node, scene_path: String) -> bool:
	var packed := PackedScene.new()
	var err := packed.pack(root)
	if err != OK:
		_emit(false, null, "pack failed with error %d" % err)
		return false
	err = ResourceSaver.save(packed, scene_path)
	if err != OK:
		_emit(false, null, "save failed with error %d" % err)
		return false
	return true

func _walk(node: Node, root: Node) -> Dictionary:
	var entry := {
		"name": node.name,
		"type": node.get_class(),
		"path": str(root.get_path_to(node)) if node != root else ".",
	}
	var script = node.get_script()
	if script != null and script.resource_path != "":
		entry["script"] = script.resource_path
	if node is Node and node.scene_file_path != "" and node != root:
		entry["instance_of"] = node.scene_file_path
	var children := []
	for child in node.get_children():
		children.append(_walk(child, root))
	if children.size() > 0:
		entry["children"] = children
	return entry

# --- ops ---

func _op_get_tree(args: Dictionary) -> void:
	var root = _load_scene(args)
	if root == null:
		return
	var tree := _walk(root, root)
	root.free()
	_emit(true, {"scene": args.get("scene", ""), "tree": tree}, null)

func _op_create_scene(args: Dictionary) -> void:
	var scene_path: String = args.get("scene_path", "")
	var root_type: String = args.get("root_type", "Node")
	var root_name: String = args.get("root_name", root_type)
	if not ClassDB.class_exists(root_type):
		_emit(false, null, "unknown class: " + root_type)
		return
	if not ClassDB.can_instantiate(root_type):
		_emit(false, null, "class cannot be instantiated: " + root_type)
		return
	var node = ClassDB.instantiate(root_type)
	if not (node is Node):
		_emit(false, null, root_type + " is not a Node subclass")
		return
	node.name = root_name
	var dir := scene_path.get_base_dir()
	if dir != "res://" and not DirAccess.dir_exists_absolute(dir):
		DirAccess.make_dir_recursive_absolute(dir)
	if not _save_scene(node, scene_path):
		return
	node.free()
	_emit(true, {"created": scene_path, "root_type": root_type, "root_name": root_name}, null)

func _op_add_node(args: Dictionary) -> void:
	var root = _load_scene(args)
	if root == null:
		return
	var parent_path: String = args.get("parent", ".")
	var parent: Node = root if parent_path == "." else root.get_node_or_null(parent_path)
	if parent == null:
		root.free()
		_emit(false, null, "parent node not found: " + parent_path)
		return
	var type: String = args.get("type", "")
	if not ClassDB.class_exists(type) or not ClassDB.can_instantiate(type):
		root.free()
		_emit(false, null, "unknown or non-instantiable class: " + type)
		return
	var node = ClassDB.instantiate(type)
	if not (node is Node):
		root.free()
		_emit(false, null, type + " is not a Node subclass")
		return
	node.name = str(args.get("name", type))
	var props: Dictionary = args.get("properties", {})
	var applied := []
	for key in props.keys():
		node.set(key, _convert_value(props[key]))
		applied.append(key)
	parent.add_child(node)
	node.owner = root
	if not _save_scene(root, args.get("scene", "")):
		return
	var node_path := str(root.get_path_to(node))
	root.free()
	_emit(true, {"added": node_path, "type": type, "properties_applied": applied}, null)

func _op_set_property(args: Dictionary) -> void:
	var root = _load_scene(args)
	if root == null:
		return
	var node_path: String = args.get("node_path", ".")
	var node: Node = root if node_path == "." else root.get_node_or_null(node_path)
	if node == null:
		root.free()
		_emit(false, null, "node not found: " + node_path)
		return
	var property: String = args.get("property", "")
	var value = _convert_value(args.get("value"))
	node.set(property, value)
	var readback = node.get(property)
	if not _save_scene(root, args.get("scene", "")):
		return
	root.free()
	_emit(true, {"node": node_path, "property": property, "value_now": _jsonify(readback)}, null)

func _op_get_property(args: Dictionary) -> void:
	var root = _load_scene(args)
	if root == null:
		return
	var node_path: String = args.get("node_path", ".")
	var node: Node = root if node_path == "." else root.get_node_or_null(node_path)
	if node == null:
		root.free()
		_emit(false, null, "node not found: " + node_path)
		return
	var property: String = args.get("property", "")
	var value = node.get(property)
	root.free()
	_emit(true, {"node": node_path, "property": property, "value": _jsonify(value)}, null)

func _op_attach_script(args: Dictionary) -> void:
	var root = _load_scene(args)
	if root == null:
		return
	var node_path: String = args.get("node_path", ".")
	var node: Node = root if node_path == "." else root.get_node_or_null(node_path)
	if node == null:
		root.free()
		_emit(false, null, "node not found: " + node_path)
		return
	var script_path: String = args.get("script_path", "")
	var script = load(script_path)
	if script == null:
		root.free()
		_emit(false, null, "could not load script: " + script_path)
		return
	node.set_script(script)
	if not _save_scene(root, args.get("scene", "")):
		return
	root.free()
	_emit(true, {"attached": script_path, "to": node_path}, null)

func _op_validate_script(args: Dictionary) -> void:
	var script_path: String = args.get("script_path", "")
	if not FileAccess.file_exists(script_path):
		_emit(false, null, "script not found: " + script_path)
		return
	var f := FileAccess.open(script_path, FileAccess.READ)
	var source := f.get_as_text()
	f.close()
	var script := GDScript.new()
	script.source_code = source
	var err := script.reload()
	if err != OK:
		_emit(true, {"valid": false, "error_code": err, "script": script_path}, null)
		return
	_emit(true, {"valid": true, "script": script_path}, null)
