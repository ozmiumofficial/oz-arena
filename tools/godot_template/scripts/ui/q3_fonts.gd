class_name Q3Fonts
extends RefCounted
## Quake 3's menu fonts, remastered by tools/make_ui.py (library/ui/fonts/fonts.json):
##   prop()    font1_prop as a signed-distance font: crisp at any size, supports outlines
##   glow()    font1_prop_glo, the soft glow q3_ui pulses behind a focused item; same metrics
##   banner()  font2_prop, the red banner font (caps only), a 4x bitmap
## The prop fonts have capitals only (lower case draws as capitals, like q3_ui). Characters
## they lack fall back to Godot's default font.

const FONTS := "res://library/ui/fonts/"
static var _cache: Dictionary = {}


static func prop() -> Font:
	return _font("prop", "file")


static func glow() -> Font:
	return _font("prop", "file_glow")


static func banner() -> Font:
	return _font("banner", "file")


static func _font(name: String, file_key: String) -> Font:
	var id := name + file_key
	if _cache.has(id):
		return _cache[id]
	var meta: Dictionary = Q3.load_json(FONTS + "fonts.json").get(name, {})
	var font: Font = ThemeDB.fallback_font
	if meta.has(file_key):
		var f := _build(meta, load("res://library/ui/" + String(meta[file_key])) as Texture2D, file_key == "file" and meta["kind"] == "sdf")
		if f:
			font = f
	_cache[id] = font
	return font


static func _build(meta: Dictionary, tex: Texture2D, sdf: bool) -> FontFile:
	if tex == null:
		return null
	var img := tex.get_image()
	if img.is_compressed():
		img.decompress()
	var size: int = meta["size"]
	var f := FontFile.new()
	if sdf:
		f.multichannel_signed_distance_field = true
		f.msdf_pixel_range = int(meta["sdf_range"])
		f.msdf_size = size
		img.convert(Image.FORMAT_RGB8)
	else:
		f.fixed_size = size
		f.fixed_size_scale_mode = TextServer.FIXED_SIZE_SCALE_ENABLED
		img.convert(Image.FORMAT_RGBA8)
	f.antialiasing = TextServer.FONT_ANTIALIASING_GRAY
	f.generate_mipmaps = not sdf
	var sz := Vector2i(size, 0)
	f.set_texture_image(0, sz, 0, img)
	f.set_cache_ascent(0, size, float(meta["ascent"]))
	f.set_cache_descent(0, size, float(meta["descent"]))
	var m := float(meta["margin"])
	var gap := float(meta["gap"])
	for k in meta["glyphs"]:
		var g: Array = meta["glyphs"][k]
		var c := int(k)
		var w := float(g[2])
		var h := float(g[3])
		f.set_glyph_advance(0, size, c, Vector2(w - 2.0 * m + gap, 0))
		f.set_glyph_offset(0, sz, c, Vector2(-m, -float(meta["ascent"]) - m))
		f.set_glyph_size(0, sz, c, Vector2(w, h))
		f.set_glyph_uv_rect(0, sz, c, Rect2(g[0], g[1], w, h))
		f.set_glyph_texture_idx(0, sz, c, 0)
	f.set_glyph_advance(0, size, 32, Vector2(float(meta["space"]), 0))
	f.set_glyph_size(0, sz, 32, Vector2.ZERO)
	f.fallbacks = [ThemeDB.fallback_font]
	return f
