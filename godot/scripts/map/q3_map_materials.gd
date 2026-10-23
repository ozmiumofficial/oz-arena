class_name Q3MapMaterials
extends RefCounted
## Quake 3 shader names -> the remastered world materials (library/world/world_materials.json), for the
## map editor's brushes. Names are full shader names ("textures/base_wall/concrete4"), as in a BSP.

const WORLD_MATERIALS := "res://library/world/world_materials.json"
const DEFAULT := "textures/base_wall/concrete"    ## new brushes (concrete4 is id's lava, despite the name)
const FALLBACK_SIZE := Vector2(128, 128)          ## texels for a name the library doesn't have

static var _info: Dictionary = {}
static var _cache: Dictionary = {}
static var _missing: Material


static func all() -> Dictionary:
	if _info.is_empty():
		var p: Variant = JSON.parse_string(FileAccess.get_file_as_string(WORLD_MATERIALS))
		if p is Dictionary:
			_info = p.get("materials", {})
	return _info


static func has(shader: String) -> bool:
	return all().has(shader)


## Base image size in texels: the texture scale for face mapping, as in Quake 3.
static func size(shader: String) -> Vector2:
	var s: Variant = all().get(shader, {}).get("size")
	return Vector2(s[0], s[1]) if s is Array and s.size() == 2 else FALLBACK_SIZE


static func material(shader: String) -> Material:
	if _cache.has(shader):
		return _cache[shader]
	var res := String(all().get(shader, {}).get("res", ""))
	var m: Material = load(res) if res != "" and ResourceLoader.exists(res) else missing()
	_cache[shader] = m
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
