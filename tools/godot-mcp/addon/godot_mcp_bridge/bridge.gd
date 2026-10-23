@tool
extends EditorPlugin
# TCP JSON-lines bridge for the godot-mcp MCP server.
# Protocol: one JSON object per line. Request: {"id": N, "cmd": "...", "args": {...}}
# Response: {"id": N, "ok": true, "result": ...} or {"id": N, "ok": false, "error": "..."}

const PORT := 9080

var _server := TCPServer.new()
var _peers: Array = []

func _enter_tree() -> void:
	var err := _server.listen(PORT, "127.0.0.1")
	if err != OK:
		push_error("godot_mcp_bridge: cannot listen on 127.0.0.1:%d (error %d)" % [PORT, err])
	else:
		print("godot_mcp_bridge: listening on 127.0.0.1:%d" % PORT)
	set_process(true)

func _exit_tree() -> void:
	_server.stop()
	for entry in _peers:
		entry["peer"].disconnect_from_host()
	_peers.clear()

func _process(_delta: float) -> void:
	while _server.is_connection_available():
		var peer := _server.take_connection()
		_peers.append({"peer": peer, "buf": ""})
	for entry in _peers.duplicate():
		var peer: StreamPeerTCP = entry["peer"]
		peer.poll()
		var status := peer.get_status()
		if status == StreamPeerTCP.STATUS_ERROR or status == StreamPeerTCP.STATUS_NONE:
			_peers.erase(entry)
			continue
		var avail := peer.get_available_bytes()
		if avail > 0:
			entry["buf"] += peer.get_utf8_string(avail)
			while true:
				var idx: int = entry["buf"].find("\n")
				if idx < 0:
					break
				var line: String = entry["buf"].substr(0, idx).strip_edges()
				entry["buf"] = entry["buf"].substr(idx + 1)
				if line != "":
					_handle_line(peer, line)

func _handle_line(peer: StreamPeerTCP, line: String) -> void:
	var msg = JSON.parse_string(line)
	if msg == null or typeof(msg) != TYPE_DICTIONARY:
		return
	var id = msg.get("id", 0)
	var cmd: String = msg.get("cmd", "")
	var args: Dictionary = msg.get("args", {})
	var resp := {"id": id}
	match cmd:
		"ping":
			resp["ok"] = true
			resp["result"] = {"pong": true}
		"info":
			resp["ok"] = true
			resp["result"] = _cmd_info()
		"execute":
			var r := _cmd_execute(str(args.get("code", "")))
			resp["ok"] = r["ok"]
			if r["ok"]:
				resp["result"] = r["result"]
			else:
				resp["error"] = r["error"]
		"get_scene_tree":
			var root := EditorInterface.get_edited_scene_root()
			if root == null:
				resp["ok"] = false
				resp["error"] = "no scene is currently open in the editor"
			else:
				resp["ok"] = true
				resp["result"] = {"scene": root.scene_file_path, "tree": _walk(root, root)}
		"open_scene":
			var path := str(args.get("path", ""))
			if not ResourceLoader.exists(path):
				resp["ok"] = false
				resp["error"] = "scene not found: " + path
			else:
				EditorInterface.open_scene_from_path(path)
				resp["ok"] = true
				resp["result"] = {"opened": path}
		_:
			resp["ok"] = false
			resp["error"] = "unknown command: " + cmd
	peer.put_data((JSON.stringify(resp) + "\n").to_utf8_buffer())

func _cmd_info() -> Dictionary:
	var edited := EditorInterface.get_edited_scene_root()
	return {
		"godot_version": Engine.get_version_info()["string"],
		"project_name": str(ProjectSettings.get_setting("application/config/name", "")),
		"project_path": ProjectSettings.globalize_path("res://"),
		"edited_scene": edited.scene_file_path if edited != null else null,
	}

func _cmd_execute(code: String) -> Dictionary:
	var source := "extends RefCounted\nfunc run():\n"
	var lines := code.split("\n")
	if lines.size() == 0 or code.strip_edges() == "":
		return {"ok": false, "error": "empty code"}
	for line in lines:
		source += "\t" + line + "\n"
	var script := GDScript.new()
	script.source_code = source
	var err := script.reload()
	if err != OK:
		return {"ok": false, "error": "GDScript parse error (code %d). Check syntax; code runs as a function body." % err}
	var inst = script.new()
	var result = inst.run()
	return {"ok": true, "result": {"returned": _jsonify(result)}}

func _walk(node: Node, root: Node) -> Dictionary:
	var entry := {
		"name": str(node.name),
		"type": node.get_class(),
		"path": str(root.get_path_to(node)) if node != root else ".",
	}
	var script = node.get_script()
	if script != null and script.resource_path != "":
		entry["script"] = script.resource_path
	var children := []
	for child in node.get_children():
		children.append(_walk(child, root))
	if children.size() > 0:
		entry["children"] = children
	return entry

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
