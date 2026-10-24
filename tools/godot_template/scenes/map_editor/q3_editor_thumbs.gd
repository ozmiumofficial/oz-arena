class_name Q3EditorThumbs
extends SubViewport
## The map editor's thumbnail camera (OZMIUM's PrefabThumbnail and AssetThumbnail): a world of its
## own where prefabs and pickups are posed and rendered to small images for the browser's tiles.
## Jobs queue and run one at a time, since each needs a frame drawn; finished images are written
## to disk and announced with `rendered`. Headless runs have no renderer and produce nothing.

signal rendered(key: String, texture: Texture2D)

const SIZE := 192
const ITEM_DIR := "user://map_editor/thumbs/items_v1"

var _queue: Array = []           ## [{key, objects | classname, path}]
var _busy := false
var _stage: Node3D
var _cam: Camera3D
static var _textures := {}       ## key -> Texture2D


func _ready() -> void:
	size = Vector2i(SIZE, SIZE)
	own_world_3d = true
	transparent_bg = false
	msaa_3d = Viewport.MSAA_4X
	render_target_update_mode = SubViewport.UPDATE_DISABLED
	var env := WorldEnvironment.new()
	env.environment = Environment.new()
	env.environment.background_mode = Environment.BG_COLOR
	env.environment.background_color = Color(0.075, 0.08, 0.095)
	env.environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.environment.ambient_light_color = Color(0.5, 0.5, 0.52)
	env.environment.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	add_child(env)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-50, 30, 0)
	sun.light_energy = 1.2
	add_child(sun)
	_cam = Camera3D.new()
	_cam.fov = 40.0
	_cam.near = 0.05
	_cam.far = 2000.0
	add_child(_cam)
	_stage = Node3D.new()
	add_child(_stage)


## A pickup's tile: from disk when it was rendered before, else null now and `rendered` later.
func item(cn: String) -> Texture2D:
	var key := "item:" + cn
	if _textures.has(key):
		return _textures[key]
	var p := ITEM_DIR.path_join(cn + ".png")
	if FileAccess.file_exists(p):
		var img := Image.load_from_file(ProjectSettings.globalize_path(p))
		if img and not img.is_empty():
			_textures[key] = ImageTexture.create_from_image(img)
			return _textures[key]
	_add({"key": key, "classname": cn, "path": p})
	return null


## Renders map objects (a prefab) to path; `rendered` says when.
func render_objects(key: String, objs: Array, path: String) -> void:
	_textures.erase(key)
	_add({"key": key, "objects": objs, "path": path})


func pending() -> int:
	return _queue.size() + (1 if _busy else 0)


func _add(job: Dictionary) -> void:
	for j in _queue:
		if j["key"] == job["key"]:
			return
	_queue.append(job)
	if not _busy:
		_busy = true      # now, not when _run starts: more jobs can arrive this frame
		_run.call_deferred()


func _run() -> void:
	while _queue.size() and is_inside_tree():
		var job: Dictionary = _queue.pop_front()
		for c in _stage.get_children():
			c.queue_free()
		var box := _pose(job)
		if box.size == Vector3.ZERO:
			continue
		var dir := Vector3(-1.0, -0.75, -1.0).normalized()
		var radius := maxf(box.size.length() * 0.5, 0.3)
		var dist := radius / tan(deg_to_rad(_cam.fov * 0.5)) * 1.1
		_cam.look_at_from_position(box.get_center() - dir * dist, box.get_center(), Vector3.UP)
		_cam.far = dist + radius * 4.0
		render_target_update_mode = SubViewport.UPDATE_ONCE
		await RenderingServer.frame_post_draw
		await RenderingServer.frame_post_draw
		var img := get_texture().get_image() if is_inside_tree() else null
		if img == null or img.is_empty():
			continue
		DirAccess.make_dir_recursive_absolute(String(job["path"]).get_base_dir())
		img.save_png(job["path"])
		var tex := ImageTexture.create_from_image(img)
		_textures[job["key"]] = tex
		rendered.emit(job["key"], tex)
	for c in _stage.get_children():
		c.queue_free()
	_busy = false


## Builds the job's subject on the stage; returns its bounds in Godot space.
func _pose(job: Dictionary) -> AABB:
	if job.has("classname"):
		var p := Q3Pickup.new()
		p.classname = job["classname"]
		_stage.add_child(p)
		p.set_process(false)
		p.rotation.y = -0.6   # side on to the camera, a little toward it
		return _bounds(p)
	var tmp := Q3MapDoc.new()
	for o in job["objects"]:
		tmp.objects[int(o["id"])] = o
	var mesher := Q3MapMesher.new()
	_stage.add_child(mesher)
	mesher.bind(tmp)
	mesher.flush()
	var ents := Q3EditorEntities.new()
	ents.labels = false
	_stage.add_child(ents)
	ents.bind(tmp)
	ents.flush()
	var b := Q3MapEditor.bounds_in(tmp, tmp.objects.keys())
	return AABB(Q3MapEditor.to_godot(b.position), Vector3(b.size.y, b.size.z, b.size.x) / 32.0)


## The bounds of what's drawn, from the triangles themselves: a mesh's own AABB can be far bigger
## (the weapon props report about 1.7 m tall).
static func _bounds(n: Node) -> AABB:
	var out := AABB()
	var first := true
	for m in n.find_children("*", "MeshInstance3D", true, false):
		var mi := m as MeshInstance3D
		if mi.mesh == null or not mi.is_visible_in_tree():
			continue
		var xf := mi.global_transform
		for v in mi.mesh.get_faces():
			var p := xf * v
			if first:
				out = AABB(p, Vector3.ZERO)
				first = false
			else:
				out = out.expand(p)
	return out
