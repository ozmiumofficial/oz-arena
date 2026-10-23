class_name Q3Portal
extends Node3D
## A mirror or portal view (tr_main.c R_MirrorViewBySurface), from a map's "portals"
## (tools/build_maps.py portals()). The renderer maps the viewer through the surface's frame onto
## the camera's (R_MirrorPoint): a point at (x, y, z) along the surface axes lands at (x, y, z)
## along the camera axes. A mirror's camera axes are the surface's with axis 0 negated; a portal
## camera is a misc_portal_camera, swinging or turning as the game set it up.
##
## Here the view is a SubViewport camera at that mapped eye, looking square at the mapped surface
## rectangle through an off-axis frustum whose near plane is that rectangle, so everything behind
## the surface is cut off (Quake 3's portal clip plane) and the image maps onto the rectangle by
## position alone. The surface draws that image and then its shader's stages (q3_portal.gdshader).
## The views skip the surfaces themselves and the first-person gun, and see the local player's
## body, which only they see (RF_THIRD_PERSON: "don't draw through eyes, only mirrors").

const LAYER := 1 << 19          ## portal surfaces: no view draws them
const VIEW_LAYER := 1 << 18     ## the first-person gun: only the player's own eyes see it
const MIRROR_LAYER := 1 << 17   ## the local player's body: only the views see it
const CLIP := 0.02              ## meters past the surface where the view starts
const LOOKS := {"mirror": 0, "bugmirror": 1, "portal_sfx": 2}
const SHADER := preload("res://shaders/q3_portal.gdshader")

var data: Dictionary = {}
var _vp: SubViewport
var _cam: Camera3D
var _mat: ShaderMaterial
var _aabb: AABB
var _s_origin: Vector3
var _s: Array = []     ## surface axes 0 (normal), 1, 2
var _c_origin: Vector3
var _c: Array = []     ## camera axes before any swing
var _rect: Rect2       ## along surface axes 1 and 2, meters


## Every portal of a built map: the views, and the map's own copies of portal surfaces hidden.
static func build_all(map: Q3Map) -> Array[Q3Portal]:
	var out: Array[Q3Portal] = []
	var hide := {}
	for d in map.info().get("portals", []):
		var p := Q3Portal.new()
		p.name = "Portal_%d" % out.size()
		p.data = d
		map.add_child(p)
		out.append(p)
		if d.get("in_glb", false):
			hide[String(d["shader"])] = true
	if hide.is_empty():
		return out
	var none := ShaderMaterial.new()
	none.shader = Shader.new()
	none.shader.code = "shader_type spatial;\nrender_mode unshaded;\nvoid fragment() { discard; }\n"
	var geo := map.get_node_or_null("Geometry")
	for mi in (geo.find_children("*", "MeshInstance3D", true, false) if geo else []):
		var m := mi as MeshInstance3D
		for i in (m.mesh.get_surface_count() if m.mesh else 0):
			if hide.has(String(m.mesh.surface_get_name(i))):
				m.set_surface_override_material(i, none)
	return out


static func _v(a: Array) -> Vector3:
	return Vector3(a[0], a[1], a[2])


func _ready() -> void:
	_s_origin = _v(data["surface"]["origin"])
	_s = (data["surface"]["axes"] as Array).map(func(a: Array) -> Vector3: return _v(a))
	var cam: Variant = data.get("camera")
	if cam is Dictionary:
		_c_origin = _v(cam["origin"])
		_c = (cam["axes"] as Array).map(func(a: Array) -> Vector3: return _v(a))
	else:
		_c_origin = _s_origin
		_c = [-_s[0], _s[1], _s[2]]
	var r: Array = data["rect"]
	_rect = Rect2(r[0], r[1], float(r[2]) - float(r[0]), float(r[3]) - float(r[1]))
	_build_mesh()
	_build_view()


func _build_mesh() -> void:
	var v: Array = data["verts"]
	var n: Array = data["normals"]
	var t: Array = data["uvs"]
	var idx: Array = data["indices"]
	var pts := PackedVector3Array()
	var nrm := PackedVector3Array()
	var uv := PackedVector2Array()
	for i in v.size() / 3:
		pts.append(Vector3(v[i * 3], v[i * 3 + 1], v[i * 3 + 2]))
		nrm.append(Vector3(n[i * 3], n[i * 3 + 1], n[i * 3 + 2]))
		uv.append(Vector2(t[i * 2], t[i * 2 + 1]))
	var tris := PackedInt32Array()
	for i in idx.size() / 3:   # counter-clockwise (glTF) -> Godot's clockwise front faces
		tris.append_array([idx[i * 3], idx[i * 3 + 2], idx[i * 3 + 1]])
	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = pts
	arrays[Mesh.ARRAY_NORMAL] = nrm
	arrays[Mesh.ARRAY_TEX_UV] = uv
	arrays[Mesh.ARRAY_INDEX] = tris
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	_aabb = mesh.get_aabb().grow(0.1)
	_mat = ShaderMaterial.new()
	_mat.shader = SHADER
	_mat.set_shader_parameter("look", LOOKS.get(String(data.get("look", "mirror")), 0))
	_mat.set_shader_parameter("s_origin", _s_origin)
	_mat.set_shader_parameter("s_u", _s[1])
	_mat.set_shader_parameter("s_v", _s[2])
	_mat.set_shader_parameter("window", Vector4(_rect.get_center().x, _rect.get_center().y, _rect.size.x, _rect.size.y))
	_mat.set_shader_parameter("portal_range", float(data.get("range", 8.0)))
	var tex: Array = data.get("textures", [])
	for i in tex.size():
		var p := "res://library/maps/portal/%s.png" % tex[i]
		if ResourceLoader.exists(p):
			_mat.set_shader_parameter("stage%d" % i, load(p))
	var mi := MeshInstance3D.new()
	mi.name = "Surface"
	mi.mesh = mesh
	mi.material_override = _mat
	mi.layers = LAYER
	mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	add_child(mi)


func _build_view() -> void:
	_vp = SubViewport.new()
	_vp.name = "View"
	_vp.use_hdr_2d = true   # linear, so the main view's tone mapping is the only one
	_vp.render_target_update_mode = SubViewport.UPDATE_DISABLED
	var main := get_viewport()
	# the longer side at the screen's height: about a pixel per pixel when it fills the view
	var h := float(mini(int(main.get_visible_rect().size.y) if main else 1080, 1440))
	var aspect := _rect.size.x / maxf(_rect.size.y, 0.001)
	var px := Vector2(h, h / aspect) if aspect >= 1.0 else Vector2(h * aspect, h)
	_vp.size = Vector2i(maxi(int(px.x), 16), maxi(int(px.y), 16))
	add_child(_vp)
	_cam = Camera3D.new()
	_cam.cull_mask = 0xFFFFF & ~LAYER & ~VIEW_LAYER
	_cam.keep_aspect = Camera3D.KEEP_HEIGHT
	_vp.add_child(_cam)
	_mat.set_shader_parameter("view_tex", _vp.get_texture())


## The world's environment as the view needs it: no tone mapping, glow or color adjustment of its
## own (the main view does those once, over the surface).
func _view_environment() -> void:
	var we := get_tree().current_scene.find_children("*", "WorldEnvironment", true, false) if get_tree().current_scene else []
	var src: Environment = null
	for w in we:
		if (w as WorldEnvironment).environment:
			src = (w as WorldEnvironment).environment
			break
	if src == null:
		return
	var env := src.duplicate() as Environment
	env.tonemap_mode = Environment.TONE_MAPPER_LINEAR
	env.tonemap_exposure = 1.0
	env.tonemap_white = 1.0
	env.glow_enabled = false
	env.adjustment_enabled = false
	_cam.environment = env


## The camera's axes now (R_GetPortalOrientations): rolled by the camera's "roll", swinging 4
## degrees either way, or turning at its speed.
func _camera_axes(now_ms: float) -> Array:
	var cam: Variant = data.get("camera")
	if not cam is Dictionary:
		return _c
	var deg := 0.0
	if cam.get("swing", true):
		deg = now_ms / 1000.0 * float(cam["speed"]) if float(cam.get("speed", 0.0)) > 0.0 \
			else float(cam["roll"]) + sin(now_ms * 0.003) * 4.0
	else:
		deg = float(cam["roll"])
	if deg == 0.0:
		return _c
	var c1: Vector3 = (_c[1] as Vector3).rotated(_c[0], deg_to_rad(deg))
	return [_c[0], c1, (_c[0] as Vector3).cross(c1)]


func _process(_delta: float) -> void:
	var view := get_viewport().get_camera_3d()
	if view == null or view == _cam:
		return
	var eye := view.global_position
	var rel := eye - _s_origin
	var x := rel.dot(_s[0])
	var on := x > 0.01 and _on_screen(view)
	if on and not data.get("mirror", true):
		# a portal fades to its fog by portalRange and isn't drawn past it (SurfIsOffscreen)
		var near := Vector3(clampf(rel.dot(_s[1]), _rect.position.x, _rect.end.x), clampf(rel.dot(_s[2]), _rect.position.y, _rect.end.y), 0.0)
		on = Vector2(rel.dot(_s[1]) - near.x, rel.dot(_s[2]) - near.y).length_squared() + x * x < pow(float(data.get("range", 8.0)), 2)
	_mat.set_shader_parameter("has_view", on)
	_vp.render_target_update_mode = SubViewport.UPDATE_ALWAYS if on else SubViewport.UPDATE_DISABLED
	if not on:
		return
	if _cam.environment == null:
		_view_environment()
	var c := _camera_axes(float(Time.get_ticks_msec()))
	# R_MirrorPoint: the eye through the surface's frame into the camera's
	var e2: Vector3 = _c_origin + (c[0] as Vector3) * x + (c[1] as Vector3) * rel.dot(_s[1]) + (c[2] as Vector3) * rel.dot(_s[2])
	var z: Vector3 = c[0]
	var y: Vector3 = c[2]
	var xc := y.cross(z)
	# a mirror's frame is left-handed: the image comes out flipped, and the surface flips it back
	_mat.set_shader_parameter("flip_u", signf((c[1] as Vector3).dot(xc)))
	_cam.global_transform = Transform3D(Basis(xc, y, z), e2)
	var center: Vector3 = _c_origin + (c[1] as Vector3) * _rect.get_center().x + (c[2] as Vector3) * _rect.get_center().y
	var k := (x + CLIP) / x
	var off := center - e2
	_cam.set_frustum(_rect.size.y * k, Vector2(off.dot(xc), off.dot(y)) * k, x + CLIP, view.far)


## Whether the surface's box is inside the camera's frustum.
func _on_screen(view: Camera3D) -> bool:
	for plane in view.get_frustum():
		var out := true
		for i in 8:
			if not plane.is_point_over(_aabb.get_endpoint(i)):
				out = false
				break
		if out:
			return false
	return true
