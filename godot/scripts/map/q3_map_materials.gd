class_name Q3MapMaterials
extends RefCounted
## Quake 3 shader names -> the remastered world materials (library/world/world_materials.json), for the
## map editor's brushes. Names are full shader names ("textures/base_wall/concrete"), as in a BSP.
## Kinds: surface and liquid (the remastered materials), sky (the face shows the sky), fog (a
## see-through box in the fog's color), hidden (q3map's tool shaders: caulk, clip, trigger ... in
## Radiant-style colors); unshipped names have no images and stay out of the browser.

const WORLD_MATERIALS := "res://library/world/world_materials.json"
const DEFAULT := "textures/base_wall/concrete"    ## new brushes (concrete4 is id's lava, despite the name)
const FALLBACK_SIZE := Vector2(128, 128)          ## texels for a name the library doesn't have
static var prefs_path := "user://map_editor/materials.json"   ## favorites and recents (tests point it elsewhere)
const RECENT_MAX := 24
const SKY_FACE := "res://scenes/map_editor/editor_sky_face.gdshader"

## Tool shaders as Radiant draws them: [color, see-through].
const TOOL_LOOK := {
	"caulk": [Color(0.85, 0.35, 0.65), false], "nodraw": [Color(0.3, 0.3, 0.32), false],
	"nodrawnonsolid": [Color(0.35, 0.35, 0.4), true], "clip": [Color(0.55, 0.3, 0.85), true],
	"full_clip": [Color(0.55, 0.3, 0.85), true], "weapclip": [Color(0.45, 0.35, 0.95), true],
	"missileclip": [Color(0.4, 0.4, 0.95), true], "trigger": [Color(1.0, 0.55, 0.1), true],
	"hint": [Color(0.3, 0.6, 1.0), true], "skip": [Color(0.3, 0.45, 0.8), true],
	"origin": [Color(1.0, 0.6, 0.2), false], "areaportal": [Color(1.0, 0.2, 0.2), true],
	"clusterportal": [Color(0.9, 0.3, 0.3), true], "donotenter": [Color(0.9, 0.85, 0.2), true],
	"nodrop": [Color(0.75, 0.2, 0.2), true], "cushion": [Color(0.3, 0.8, 0.5), true],
	"slick": [Color(0.6, 0.85, 1.0), true], "invisible": [Color(0.7, 0.7, 0.7), true],
}

static var _info: Dictionary = {}
static var _cache: Dictionary = {}
static var _thumbs: Dictionary = {}
static var _missing: Material
static var _prefs: Dictionary = {}


static func all() -> Dictionary:
	if _info.is_empty():
		var p: Variant = JSON.parse_string(FileAccess.get_file_as_string(WORLD_MATERIALS))
		if p is Dictionary:
			_info = p.get("materials", {})
	return _info


static func has(shader: String) -> bool:
	return all().has(shader)


static func kind(shader: String) -> String:
	return String(all().get(shader, {}).get("kind", "missing"))


## Base image size in texels: the texture scale for face mapping, as in Quake 3.
static func size(shader: String) -> Vector2:
	var s: Variant = all().get(shader, {}).get("size")
	return Vector2(s[0], s[1]) if s is Array and s.size() == 2 else FALLBACK_SIZE


## The names the browser offers, in the given group: "surface" (surfaces and liquids), "sky", "tool"
## (hidden and fog), sorted.
static func names(group: String) -> PackedStringArray:
	var out := PackedStringArray()
	for n in all():
		var k := kind(n)
		var ok := false
		match group:
			"surface": ok = k == "surface" or k == "liquid"
			"sky": ok = k == "sky"
			"tool": ok = k == "hidden" or k == "fog"
		if ok:
			out.append(n)
	out.sort()
	return out


## "base_wall" for "textures/base_wall/concrete"; "models" for model shaders.
static func folder(shader: String) -> String:
	var parts := shader.split("/")
	return parts[1] if parts[0] == "textures" and parts.size() > 2 else parts[0]


static func short_name(shader: String) -> String:
	return shader.trim_prefix("textures/")


## The material for a face of this shader, tinted (white: the shared material itself).
static func material(shader: String, tint := Color.WHITE) -> Material:
	var key := shader if tint == Color.WHITE else shader + "|" + tint.to_html(false)
	if _cache.has(key):
		return _cache[key]
	var m: Material
	if tint != Color.WHITE:
		m = material(shader).duplicate()
		if m is ShaderMaterial:
			(m as ShaderMaterial).set_shader_parameter("tint", tint)
		elif m is BaseMaterial3D:
			(m as BaseMaterial3D).albedo_color *= tint
	else:
		match kind(shader):
			"sky": m = _sky_face(shader)
			"hidden": m = _tool(shader)
			"fog": m = _fog(shader)
			_:
				var res := String(all().get(shader, {}).get("res", ""))
				m = load(res) if res != "" and ResourceLoader.exists(res) else missing()
	_cache[key] = m
	return m


## Magenta and black checks, the classic missing-texture look.
static func missing() -> Material:
	if _missing == null:
		var img := Image.create(2, 2, false, Image.FORMAT_RGB8)
		img.fill(Color.BLACK)
		img.set_pixel(0, 0, Color.MAGENTA)
		img.set_pixel(1, 1, Color.MAGENTA)
		var m := StandardMaterial3D.new()
		m.albedo_texture = ImageTexture.create_from_image(img)
		m.texture_filter = BaseMaterial3D.TEXTURE_FILTER_NEAREST
		m.roughness = 0.9
		_missing = m
	return _missing


static func _tool_look(shader: String) -> Array:
	return TOOL_LOOK.get(shader.get_file(), [Color(0.6, 0.6, 0.65), true])


## q3map's tool shaders: a flat color with a faint grid, see-through when Radiant draws them so.
static func _tool(shader: String) -> Material:
	var look := _tool_look(shader)
	var m := StandardMaterial3D.new()
	m.albedo_texture = _swatch(look[0], 64)
	m.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	if look[1]:
		m.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
		m.albedo_color = Color(1, 1, 1, 0.45)
		m.cull_mode = BaseMaterial3D.CULL_DISABLED
	return m


static func _fog(shader: String) -> Material:
	var c: Variant = all().get(shader, {}).get("color", [0.5, 0.5, 0.5])
	var m := StandardMaterial3D.new()
	m.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	m.albedo_color = Color(c[0], c[1], c[2], 0.35)
	m.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	m.cull_mode = BaseMaterial3D.CULL_DISABLED
	return m


## The sky through a sky face, from the sky's own Sky resource (clouds or panorama).
static func _sky_face(shader: String) -> Material:
	var m := ShaderMaterial.new()
	m.shader = load(SKY_FACE)
	var sky: Sky = _load_sky(shader)
	var sm := sky.sky_material as ShaderMaterial if sky else null
	if sm == null:
		m.set_shader_parameter("horizon_color", Color(0.3, 0.45, 0.7))
		return m
	var panorama := sm.shader.resource_path.ends_with("sky_panorama.gdshader")
	m.set_shader_parameter("mode", 1 if panorama else 0)
	for p in ["layer1", "scale1", "scroll1", "layer2", "scale2", "scroll2", "layer2_add", "layer2_mix", "cloud_height",
			"horizon_color", "panorama", "energy"]:
		var v: Variant = sm.get_shader_parameter(p)
		if v != null:
			m.set_shader_parameter(p, v)
	return m


static func _load_sky(shader: String) -> Sky:
	var res := String(all().get(shader, {}).get("res", ""))
	return load(res) as Sky if res != "" and ResourceLoader.exists(res) else null


static func _swatch(c: Color, n: int) -> ImageTexture:
	var img := Image.create(n, n, false, Image.FORMAT_RGBA8)
	img.fill(c)
	for i in n:
		for j in [0, n / 2]:
			img.set_pixel(i, j, c.darkened(0.35))
			img.set_pixel(j, i, c.darkened(0.35))
	return ImageTexture.create_from_image(img)


## A picture for the browser: the remastered albedo (read from the material file, so its normal and
## ORM maps aren't loaded), the sky's cloud layer or panorama, or the tool color.
static func thumbnail(shader: String) -> Texture2D:
	if _thumbs.has(shader):
		return _thumbs[shader]
	var t: Texture2D
	match kind(shader):
		"hidden":
			t = _swatch(_tool_look(shader)[0], 32)
		"fog":
			var c: Variant = all().get(shader, {}).get("color", [0.5, 0.5, 0.5])
			t = _swatch(Color(c[0], c[1], c[2]), 32)
		"sky":
			var sky := _load_sky(shader)
			var sm := sky.sky_material as ShaderMaterial if sky else null
			if sm:
				t = sm.get_shader_parameter("panorama") if sm.get_shader_parameter("panorama") != null else sm.get_shader_parameter("layer1")
		_:
			var res := String(all().get(shader, {}).get("res", ""))
			var path := _albedo_path(res)
			if path != "" and ResourceLoader.exists(path):
				t = load(path)
	_thumbs[shader] = t
	return t


## The albedo texture a world material file points at, without loading the material.
static func _albedo_path(res: String) -> String:
	if res == "" or not FileAccess.file_exists(res):
		return ""
	var text := FileAccess.get_file_as_string(res)
	var m := RegEx.create_from_string('shader_parameter/albedo_tex = ExtResource\\("([^"]+)"\\)').search(text)
	if m == null:
		return ""
	var id := m.get_string(1)
	var r := RegEx.create_from_string('\\[ext_resource type="Texture2D"[^\\]]*path="([^"]+)" id="' + id + '"\\]').search(text)
	return r.get_string(1) if r else ""


# ---------------------------------------------------------------- favorites and recents

static func prefs() -> Dictionary:
	if _prefs.is_empty():
		var p: Variant = JSON.parse_string(FileAccess.get_file_as_string(prefs_path)) if FileAccess.file_exists(prefs_path) else null
		_prefs = p if p is Dictionary else {}
		for k in ["favorites", "recent"]:
			if not _prefs.get(k) is Array:
				_prefs[k] = []
	return _prefs


static func is_favorite(shader: String) -> bool:
	return prefs()["favorites"].has(shader)


static func toggle_favorite(shader: String) -> void:
	var f: Array = prefs()["favorites"]
	if f.has(shader):
		f.erase(shader)
	else:
		f.append(shader)
	_save_prefs()


static func add_recent(shader: String) -> void:
	var r: Array = prefs()["recent"]
	r.erase(shader)
	r.push_front(shader)
	if r.size() > RECENT_MAX:
		r.resize(RECENT_MAX)
	_save_prefs()


static func _save_prefs() -> void:
	DirAccess.make_dir_recursive_absolute(prefs_path.get_base_dir())
	var f := FileAccess.open(prefs_path, FileAccess.WRITE)
	if f:
		f.store_string(JSON.stringify(_prefs, "\t"))
