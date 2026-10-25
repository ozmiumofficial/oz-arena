class_name Q3ClassicMap
extends Q3Map
## A map from the Maps folder (Q3MapPacks) that the pipeline hasn't remastered, drawn straight from
## its .bsp at load time; q3core plays the same .bsp. What the remastered scenes are built offline
## from (tools/build_maps.py, a Godot bake), this builds in one pass:
##   Geometry/lm_<page>   the world's drawn surfaces (planar, triangle soups with q3map's misc_models,
##                        patches tessellated) grouped by lightmap page, with the page coordinates as
##                        UV2: a LightmapGIData made from the .bsp's own lightmaps lights them, so
##                        the world shaders light as in a baked scene, with no bake
##   Geometry/vl_<k>      surfaces q3map lit per vertex (no lightmap): each triangle gets a 4x4 cell
##                        of extra lightmap pages holding its vertex colors, laid out so the lightmap's
##                        bilinear filter gives back the per-vertex gradient Quake 3 drew
##   Geometry/model_<n>   each brush model (doors, plats ...), moved by the game's entity states
##   Entities, Collision, WorldEnvironment (the sky), LightmapGI
## Materials: the remastered one when the library has the shader (world_materials.json), else the
## pk3's shader script (base stage, blend, alphaFunc, a glow layer, tcMod scale/scroll), else the
## image named like the shader. Fog volumes and portals aren't drawn.

const UNIT := 1.0 / 32.0
const SURF_NODRAW := 0x80
const SURF_SKY := 0x4
const CONTENTS_FOG := 64
const CONTENTS_TRANSLUCENT := 0x20000000
const MST_PLANAR := 1
const MST_PATCH := 2
const MST_TRIANGLE_SOUP := 3
const PATCH_ERROR := 2.0      ## chord error (units) the curves are tessellated to (Quake 3's r_subdivisions 4)
## Quake 3 multiplies in gamma space (texture x 2 x byte / 255, overbright). Taken literally (as
## linear light, (2 b / 255) ^ 2.2) the maps came out nearly black: Quake 3's own gamma and
## overbright brighten them on screen. The light here is LIFT_FLOOR + LIFT_GAIN x (2 b / 255) ^
## LIFT_POWER: a dark corner keeps a minimum (q3map2's _minlight idea), byte 22 (Rustgrad's median)
## gives 0.54, byte 128 (Quake 3's full bright) 1.66 (2026-10-25, Ozmium: "SO dark", then "still
## slightly too dark" at 0.12 + 1.2x).
const LIFT_FLOOR := 0.16
const LIFT_GAIN := 1.5
const LIFT_POWER := 0.8
const CELL := 4               ## texels a side of a vertex-lit triangle's lightmap cell

var _bsp := PackedByteArray()
var _shaders: Array = []         ## [name, surface flags, contents]
var _scripts: Dictionary = {}    ## shader name -> parsed stages (pk3 scripts)
var _images: Dictionary = {}     ## lower-case image path -> pk3 path (every pk3 in the Maps folder)
var _materials: Dictionary = {}
var _entities_list: Array = []
var _info_cache: Dictionary = {}


## Builds the map, or returns null when its .bsp can't be read.
static func build(map_name: String) -> Q3ClassicMap:
	var b := Q3MapPacks.read(map_name, "maps/%s.bsp" % map_name)
	if b.size() < 8 + 17 * 8 or b.slice(0, 4).get_string_from_ascii() != "IBSP" or b.decode_s32(4) != 46:
		push_error("Q3ClassicMap: maps/%s.bsp isn't a Quake 3 map" % map_name)
		return null
	var m := Q3ClassicMap.new()
	m.name = "ClassicMap"
	m.map = map_name
	m._bsp = b
	m._index_pk3s()
	m._build()
	return m


## What Q3Map's features read (Q3Portal, light shafts, probes): this map has none of them yet.
func info() -> Dictionary:
	if _info_cache.is_empty():
		_info_cache = {"map": map, "entities": _entities_list, "fogs": [], "portals": [], "probes": [], "sky": []}
	return _info_cache


func _lump(i: int) -> PackedByteArray:
	var off := _bsp.decode_s32(8 + i * 8)
	return _bsp.slice(off, off + _bsp.decode_s32(12 + i * 8))


func _build() -> void:
	var sl := _lump(1)
	for i in sl.size() / 72:
		_shaders.append([sl.slice(i * 72, i * 72 + 64).get_string_from_ascii().to_lower(), sl.decode_s32(i * 72 + 64), sl.decode_s32(i * 72 + 68)])
	_entities_list = Q3BspImport.parse_entities(_lump(0).get_string_from_utf8())
	var vb := _lump(10)
	var vf := vb.to_float32_array()        # 11 floats a vertex: xyz, st, lightmap st, normal (+ color bytes)
	var idx := _lump(11).to_int32_array()
	var surfs := _lump(13)
	var models := _lump(7).to_int32_array()   # 10 ints each: mins, maxs (floats), surfaces, brushes
	var geo := Node3D.new()
	geo.name = "Geometry"
	add_child(geo)
	# world surfaces by lightmap page; brush models apart
	var model_of := PackedInt32Array()
	model_of.resize(surfs.size() / 104)
	for mi in models.size() / 10:
		for s in range(models[mi * 10 + 6], models[mi * 10 + 6] + models[mi * 10 + 7]):
			if s < model_of.size():
				model_of[s] = mi
	var groups := {}     # node name -> {material key -> [P, N, UV, UV2, I]}
	var collide := PackedVector3Array()
	var sky_shader := ""
	for s in surfs.size() / 104:
		var o := s * 104
		var si := surfs.decode_s32(o)
		var kind := surfs.decode_s32(o + 8)
		if si < 0 or si >= _shaders.size() or kind < MST_PLANAR or kind > MST_TRIANGLE_SOUP:
			continue
		var sh: Array = _shaders[si]
		if sh[1] & SURF_SKY:
			sky_shader = sh[0]
			continue
		if sh[1] & SURF_NODRAW or sh[2] & CONTENTS_FOG:
			continue
		var mat := _material(sh[0])
		if mat == null:
			continue
		var mesh := _surface(kind, surfs, o, vf, vb, idx)
		if mesh.is_empty():
			continue
		var page := surfs.decode_s32(o + 28)
		var node := ("model_%d" % model_of[s]) if model_of[s] > 0 else ("lm_%d" % page if page >= 0 else "vertexlit")
		var g: Dictionary = groups.get_or_add(node, {})
		var part: Array = g.get_or_add(sh[0], [PackedVector3Array(), PackedVector3Array(), PackedVector2Array(), PackedVector2Array(), PackedInt32Array(), PackedColorArray()])
		var base: int = part[0].size()
		part[0].append_array(mesh[0])
		part[1].append_array(mesh[1])
		part[2].append_array(mesh[2])
		part[3].append_array(mesh[3])
		part[5].append_array(mesh[5])
		for k in mesh[4]:
			part[4].append(base + k)
		if model_of[s] == 0 and not (sh[2] & CONTENTS_TRANSLUCENT):
			for k in mesh[4]:
				collide.append(mesh[0][k])
	var pages := _lightmap_pages()
	if groups.has("vertexlit"):
		_cells(groups, pages)
	var users: Array = []
	for node in groups:
		var am := ArrayMesh.new()
		for key in groups[node]:
			var p: Array = groups[node][key]
			var arrays := []
			arrays.resize(Mesh.ARRAY_MAX)
			arrays[Mesh.ARRAY_VERTEX] = p[0]
			arrays[Mesh.ARRAY_NORMAL] = p[1]
			arrays[Mesh.ARRAY_TEX_UV] = p[2]
			arrays[Mesh.ARRAY_TEX_UV2] = p[3]
			arrays[Mesh.ARRAY_INDEX] = p[4]
			am.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
			am.surface_set_material(am.get_surface_count() - 1, _materials[key])
		var mi := MeshInstance3D.new()
		mi.name = node
		mi.mesh = am
		if node.begins_with("lm_") or node.begins_with("vl_"):
			mi.gi_mode = GeometryInstance3D.GI_MODE_STATIC
			users.append([node, int(node.get_slice("_", 1))])
		else:
			mi.gi_mode = GeometryInstance3D.GI_MODE_DYNAMIC
		if node.begins_with("model_"):
			var e := _model_entity(int(node.trim_prefix("model_")))
			if e.has("origin"):
				mi.position = _to_godot(_vec(e["origin"]))
		geo.add_child(mi)
	_lightmaps(pages, users)
	_entities()
	_collision(collide)
	_environment(sky_shader)


## One drawn surface as [positions, normals, uv, uv2, indexes, vertex colors] in Godot space, front faces as Godot
## draws them (clockwise seen from the front, which id's own winding already is).
func _surface(kind: int, surfs: PackedByteArray, o: int, vf: PackedFloat32Array, vb: PackedByteArray, idx: PackedInt32Array) -> Array:
	var first := surfs.decode_s32(o + 12)
	var count := surfs.decode_s32(o + 16)
	var P := PackedVector3Array()
	var N := PackedVector3Array()
	var T := PackedVector2Array()
	var L := PackedVector2Array()
	var I := PackedInt32Array()
	var C := PackedColorArray()
	if kind == MST_PATCH:
		return _patch(first, surfs.decode_s32(o + 96), surfs.decode_s32(o + 100), vf, vb)
	for k in count:
		var f := (first + k) * 11
		P.append(_to_godot(Vector3(vf[f], vf[f + 1], vf[f + 2])))
		T.append(Vector2(vf[f + 3], vf[f + 4]))
		L.append(Vector2(vf[f + 5], vf[f + 6]))
		N.append(_dir(Vector3(vf[f + 7], vf[f + 8], vf[f + 9])))
		C.append(_vcolor(vb, first + k))
	var fi := surfs.decode_s32(o + 20)
	for k in surfs.decode_s32(o + 24):
		I.append(idx[fi + k])
	return [P, N, T, L, I, C]


## A drawvert's color (q3map's vertex light), lifted like the lightmaps.
static func _vcolor(vb: PackedByteArray, v: int) -> Color:
	var o := v * 44 + 40
	return Color(_lift(vb[o]), _lift(vb[o + 1]), _lift(vb[o + 2]))


## A biquadratic patch (w x h control points) tessellated so its 3x3 pieces share their seams.
func _patch(first: int, w: int, h: int, vf: PackedFloat32Array, vb: PackedByteArray) -> Array:
	var nu := (w - 1) / 2
	var nv := (h - 1) / 2
	if nu < 1 or nv < 1:
		return []
	var cp := []     # [xyz, st, lm, normal, color] per control point, row major
	for k in w * h:
		var f := (first + k) * 11
		cp.append([Vector3(vf[f], vf[f + 1], vf[f + 2]), Vector2(vf[f + 3], vf[f + 4]), Vector2(vf[f + 5], vf[f + 6]),
				Vector3(vf[f + 7], vf[f + 8], vf[f + 9]), _vcolor(vb, first + k)])
	var bend := 0.0
	for j in h:
		for i in range(0, w - 2, 2):
			bend = maxf(bend, (cp[j * w + i][0] - 2.0 * cp[j * w + i + 1][0] + cp[j * w + i + 2][0]).length())
	var su := clampi(ceili(sqrt(bend / 4.0 / PATCH_ERROR)), 2, 16) if bend > 0.01 else 1
	bend = 0.0
	for j in range(0, h - 2, 2):
		for i in w:
			bend = maxf(bend, (cp[j * w + i][0] - 2.0 * cp[(j + 1) * w + i][0] + cp[(j + 2) * w + i][0]).length())
	var sv := clampi(ceili(sqrt(bend / 4.0 / PATCH_ERROR)), 2, 16) if bend > 0.01 else 1
	var gw := nu * su + 1
	var gh := nv * sv + 1
	var P := PackedVector3Array()
	var N := PackedVector3Array()
	var T := PackedVector2Array()
	var L := PackedVector2Array()
	var C := PackedColorArray()
	P.resize(gw * gh)
	N.resize(gw * gh)
	T.resize(gw * gh)
	L.resize(gw * gh)
	C.resize(gw * gh)
	for pj in nv:
		for pi in nu:
			for b in sv + 1:
				var tv := float(b) / sv
				var bv := [(1 - tv) * (1 - tv), 2 * tv * (1 - tv), tv * tv]
				for a in su + 1:
					var tu := float(a) / su
					var bu := [(1 - tu) * (1 - tu), 2 * tu * (1 - tu), tu * tu]
					var x := Vector3.ZERO
					var n := Vector3.ZERO
					var st := Vector2.ZERO
					var lm := Vector2.ZERO
					var col := Color(0, 0, 0)
					for r in 3:
						for c in 3:
							var wgt: float = bv[r] * bu[c]
							var q: Array = cp[(pj * 2 + r) * w + pi * 2 + c]
							x += q[0] * wgt
							st += q[1] * wgt
							lm += q[2] * wgt
							n += q[3] * wgt
							col += q[4] * wgt
					var gi := (pj * sv + b) * gw + pi * su + a
					P[gi] = _to_godot(x)
					N[gi] = _dir(n.normalized())
					T[gi] = st
					L[gi] = lm
					C[gi] = col
	var I := PackedInt32Array()
	for j in gh - 1:
		for i in gw - 1:
			var a0 := j * gw + i
			var tri := [[a0, a0 + 1, a0 + gw + 1], [a0, a0 + gw + 1, a0 + gw]]
			for t in tri:
				var g := (P[t[1]] - P[t[0]]).cross(P[t[2]] - P[t[0]])
				if g.length_squared() < 1e-12:
					continue
				# clockwise seen from the side the normal faces
				if g.dot(N[t[0]] + N[t[1]] + N[t[2]]) > 0.0:
					I.append_array([t[0], t[2], t[1]])
				else:
					I.append_array([t[0], t[1], t[2]])
	return [P, N, T, L, I, C]


## The .bsp's lightmap pages as linear light (RGBF): the lump's 128 x 128 pages, or q3map2's
## external maps/<map>/lm_NNNN.tga when the lump is empty. Every page takes the first one's size.
func _lightmap_pages() -> Array[Image]:
	var lm := _lump(14)
	var lut := PackedFloat32Array()
	for b in 256:
		lut.append(_lift(b))
	var pages: Array[Image] = []
	var n := lm.size() / (128 * 128 * 3)
	for p in n:
		pages.append(_page(lm.slice(p * 49152, (p + 1) * 49152), 128, 128, lut))
	if n == 0:
		var k := 0
		while true:
			var b := Q3MapPacks.read(map, "maps/%s/lm_%04d.tga" % [map, k])
			if b.is_empty():
				break
			var img := Image.new()
			if img.load_tga_from_buffer(b) != OK:
				break
			img.convert(Image.FORMAT_RGB8)
			var out := _page(img.get_data(), img.get_width(), img.get_height(), lut)
			if pages.size() and (out.get_width() != pages[0].get_width() or out.get_height() != pages[0].get_height()):
				out.resize(pages[0].get_width(), pages[0].get_height())
			pages.append(out)
			k += 1
	return pages


## Vertex-lit surfaces onto extra lightmap pages: every triangle gets its own CELL x CELL cell, its
## corners on texel centers (0, 0), (CELL-1, 0) and (0, CELL-1) and every texel holding the plane
## through the three vertex colors, so bilinear filtering inside the triangle is exactly the
## per-vertex gradient. groups["vertexlit"] becomes vl_<slice> nodes; pages grows by those slices.
func _cells(groups: Dictionary, pages: Array[Image]) -> void:
	var vl: Dictionary = groups["vertexlit"]
	groups.erase("vertexlit")
	var w := pages[0].get_width() if pages.size() else 128
	var h := pages[0].get_height() if pages.size() else 128
	var per_row := w / CELL
	var per_page := per_row * (h / CELL)
	var slot := 0
	var page := PackedFloat32Array()
	var flush := func() -> void:
		if page.size():
			pages.append(Image.create_from_data(w, h, false, Image.FORMAT_RGBF, page.to_byte_array()))
	for key in vl:
		var p: Array = vl[key]
		var I: PackedInt32Array = p[4]
		for t in I.size() / 3:
			if slot % per_page == 0:
				flush.call()
				page = PackedFloat32Array()
				page.resize(w * h * 3)
			var sl := pages.size()
			var node := "vl_%d" % sl
			var g: Dictionary = groups.get_or_add(node, {})
			var part: Array = g.get_or_add(key, [PackedVector3Array(), PackedVector3Array(), PackedVector2Array(), PackedVector2Array(), PackedInt32Array(), PackedColorArray()])
			var cell := slot % per_page
			var cx := (cell % per_row) * CELL
			var cy := (cell / per_row) * CELL
			var c0: Color = p[5][I[t * 3]]
			var c1: Color = p[5][I[t * 3 + 1]]
			var c2: Color = p[5][I[t * 3 + 2]]
			for ty in CELL:
				for tx in CELL:
					var c := c0 + (c1 - c0) * (tx / float(CELL - 1)) + (c2 - c0) * (ty / float(CELL - 1))
					var o := ((cy + ty) * w + cx + tx) * 3
					page[o] = maxf(c.r, 0.0)
					page[o + 1] = maxf(c.g, 0.0)
					page[o + 2] = maxf(c.b, 0.0)
			var corners := [Vector2(cx + 0.5, cy + 0.5), Vector2(cx + CELL - 0.5, cy + 0.5), Vector2(cx + 0.5, cy + CELL - 0.5)]
			for k in 3:
				var v := I[t * 3 + k]
				part[4].append(part[0].size())
				part[0].append(p[0][v])
				part[1].append(p[1][v])
				part[2].append(p[2][v])
				part[3].append(corners[k] / Vector2(w, h))
				part[5].append(p[5][v])
			slot += 1
	flush.call()


## The pages as the LightmapGI's data: one slice each, one mesh per slice.
func _lightmaps(pages: Array[Image], users: Array) -> void:
	if pages.is_empty():
		return
	var arr := Texture2DArray.new()
	arr.create_from_images(pages)
	var data := LightmapGIData.new()
	var textures: Array[TextureLayered] = [arr]
	data.lightmap_textures = textures
	data.uses_spherical_harmonics = false
	var gi := LightmapGI.new()
	gi.name = "LightmapGI"
	add_child(gi)
	for u in users:
		if u[1] < pages.size():
			data.add_user(NodePath("../Geometry/" + String(u[0])), Rect2(0, 0, 1, 1), u[1], -1)
	gi.light_data = data


## RGB8 bytes -> a linear light page (RGBF), through the lift table.
static func _page(rgb: PackedByteArray, w: int, h: int, lut: PackedFloat32Array) -> Image:
	var f := PackedFloat32Array()
	f.resize(rgb.size())
	for i in rgb.size():
		f[i] = lut[rgb[i]]
	return Image.create_from_data(w, h, false, Image.FORMAT_RGBF, f.to_byte_array())


static func _lift(b: int) -> float:
	return LIFT_FLOOR + LIFT_GAIN * pow(minf(2.0 * b / 255.0, 4.0), LIFT_POWER)


func _entities() -> void:
	var holder := Node3D.new()
	holder.name = "Entities"
	add_child(holder)
	var i := 0
	for e in _entities_list:
		var cn := String(e.get("classname", ""))
		if cn in ["worldspawn", "light", "misc_model"] or not e.has("origin"):
			continue
		var m := Marker3D.new()
		m.name = "%s_%d" % [cn, i]
		m.position = _to_godot(_vec(e["origin"]))
		if e.has("angle"):
			m.rotation.y = deg_to_rad(float(e["angle"])) - PI * 0.5   # id yaw 0 = +X = Godot +Z
		m.set_meta("entity", e)
		holder.add_child(m)
		i += 1


## Ragdolls and gibs collide with the world's opaque drawn surfaces (the remastered scenes use the
## brushes' hulls; the drawn faces are close enough for bodies).
func _collision(faces: PackedVector3Array) -> void:
	var body := StaticBody3D.new()
	body.name = "Collision"
	body.collision_layer = Q3MapBuilder.LAYER_SOLID
	body.collision_mask = 0
	add_child(body)
	if faces.is_empty():
		return
	var shape := ConcavePolygonShape3D.new()
	shape.set_faces(faces)
	var cs := CollisionShape3D.new()
	cs.shape = shape
	body.add_child(cs)


## The sky and the light for everything the lightmaps don't reach (players, items, movers,
## vertex-lit surfaces): the light grid's average.
func _environment(sky_shader: String) -> void:
	var env := Environment.new()
	var sky := _sky(sky_shader)
	if sky:
		env.background_mode = Environment.BG_SKY
		env.sky = sky
	else:
		env.background_mode = Environment.BG_COLOR
		env.background_color = Color.BLACK
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = _grid_light()
	env.ambient_light_energy = 1.0
	env.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	env.glow_enabled = true
	env.glow_intensity = 0.6
	env.glow_bloom = 0.05
	env.ssao_enabled = true
	env.volumetric_fog_density = 0.0
	var we := WorldEnvironment.new()
	we.name = "WorldEnvironment"
	we.environment = env
	add_child(we)


## The light grid's mean ambient plus half its directed light, lifted like the lightmaps.
func _grid_light() -> Color:
	var g := _lump(15)
	var n := g.size() / 8
	if n == 0:
		return Color(0.25, 0.25, 0.25)
	var sum := Vector3.ZERO
	var used := 0
	for i in n:
		var o := i * 8
		var a := Vector3(g[o], g[o + 1], g[o + 2]) + 0.5 * Vector3(g[o + 3], g[o + 4], g[o + 5])
		if a.length_squared() > 0.0:   # cells inside walls are black
			sum += a
			used += 1
	var m := sum / maxi(used, 1)
	return Color(_lift(int(m.x)), _lift(int(m.y)), _lift(int(m.z)))


func _sky(shader: String) -> Sky:
	if shader == "":
		return null
	var wm: Dictionary = Q3MapBuilder._world_materials().get(shader, {})
	if String(wm.get("kind", "")) == "sky" and ResourceLoader.exists(String(wm.get("res", ""))):
		return load(String(wm["res"])) as Sky
	var s: Dictionary = _scripts.get(shader, {})
	var far := String(s.get("skyparms", "-"))
	if far == "-" or far == "":
		# a cloud-layer sky: its first layer over the whole dome
		for st in s.get("stages", []):
			var t := _texture(String(st["map"]))
			if t:
				var mat := PanoramaSkyMaterial.new()
				mat.panorama = t
				var sky := Sky.new()
				sky.sky_material = mat
				return sky
		return null
	var sm := ShaderMaterial.new()
	sm.shader = load("res://shaders/world/sky_box.gdshader")
	for f in ["rt", "lf", "bk", "ft", "up", "dn"]:
		var t := _texture("%s_%s" % [far, f])
		if t:
			sm.set_shader_parameter(f, t)
	var sky := Sky.new()
	sky.sky_material = sm
	return sky


# ------------------------------------------------------------------ materials

## Every image and shader script in the Maps folder's pk3s (later pk3s win, as in Quake 3).
func _index_pk3s() -> void:
	for p in Q3MapPacks.pk3s():
		var z := ZIPReader.new()
		if z.open(p) != OK:
			continue
		for e in z.get_files():
			var low := e.to_lower()
			if low.ends_with(".tga") or low.ends_with(".jpg") or low.ends_with(".png"):
				_images[low.get_basename()] = [p, e]
			elif low.begins_with("scripts/") and low.ends_with(".shader"):
				_parse_shaders(z.read_file(e).get_string_from_ascii())
		z.close()


## Shader scripts: per shader its stages (map, blend, alphafunc, tcmods), cull, skyparms.
func _parse_shaders(text: String) -> void:
	var re := RegEx.create_from_string("//[^\\n]*")
	text = re.sub(text, "", true).to_lower()
	var tok := []
	for line in text.split("\n"):
		var l := line.strip_edges().replace("{", " { ").replace("}", " } ")
		var words := l.split(" ", false)
		if words.is_empty():
			continue
		tok.append(Array(words))
	var i := 0
	while i < tok.size():
		var w: Array = tok[i]
		if w.size() == 1 and w[0] != "{" and w[0] != "}" and i + 1 < tok.size() and tok[i + 1][0] == "{":
			var sh := {"stages": [], "cull": "back", "skyparms": "", "parms": []}
			var depth := 0
			var stage := {}
			i += 1
			while i < tok.size():
				var t: Array = tok[i]
				for word in t:
					if word == "{":
						depth += 1
						if depth == 2:
							stage = {"map": "", "blend": "opaque", "alphafunc": "", "tcgen": "", "tcmods": []}
					elif word == "}":
						if depth == 2 and stage.get("map", "") != "":
							sh["stages"].append(stage)
						depth -= 1
				if depth == 0:
					break
				var k := String(t[0])
				if depth == 1:
					if k == "cull" and t.size() > 1:
						sh["cull"] = t[1]
					elif k == "skyparms" and t.size() > 1:
						sh["skyparms"] = t[1]
					elif k == "surfaceparm" and t.size() > 1:
						sh["parms"].append(t[1])
				elif depth == 2:
					if (k == "map" or k == "clampmap") and t.size() > 1:
						stage["map"] = t[1]
					elif k == "animmap" and t.size() > 2:
						stage["map"] = t[2]
					elif k == "blendfunc" and t.size() > 1:
						stage["blend"] = _blend(t)
					elif k == "alphafunc" and t.size() > 1:
						stage["alphafunc"] = t[1]
					elif k == "tcgen" and t.size() > 1:
						stage["tcgen"] = t[1]
					elif k == "tcmod" and t.size() > 1:
						stage["tcmods"].append(t.slice(1))
				i += 1
			_scripts[String(w[0]).trim_suffix(".tga").trim_suffix(".jpg")] = sh
		i += 1


static func _blend(t: Array) -> String:
	var pair := [t[1], t[2] if t.size() > 2 else ""]
	match t[1]:
		"add":
			pair = ["gl_one", "gl_one"]
		"filter":
			pair = ["gl_dst_color", "gl_zero"]
		"blend":
			pair = ["gl_src_alpha", "gl_one_minus_src_alpha"]
	if pair == ["gl_one", "gl_one"] or pair == ["gl_src_alpha", "gl_one"]:
		return "add"
	if pair == ["gl_dst_color", "gl_zero"] or pair == ["gl_zero", "gl_src_color"]:
		return "filter"
	if pair == ["gl_zero", "gl_one_minus_src_color"]:
		return "darken"
	if pair == ["gl_src_alpha", "gl_one_minus_src_alpha"]:
		return "blend"
	if pair == ["gl_one", "gl_zero"]:
		return "opaque"
	return "other"


## The material for a shader, or null to leave its surfaces out (tool shaders, hidden, sky, fog).
func _material(shader: String) -> Material:
	if _materials.has(shader):
		return _materials[shader]
	var m: Material = null
	var wm: Dictionary = Q3MapBuilder._world_materials().get(shader, {})
	var kind := String(wm.get("kind", ""))
	if kind in ["hidden", "fog", "sky"] or int(wm.get("sprite", 0)) > 0:
		# sprites (deformVertexes autosprite) face the camera about their own node: in a shared
		# mesh they'd swing about the map's origin. The remastered scenes give each quad a node.
		m = null
	elif wm.has("res") and kind != "unshipped" and ResourceLoader.exists(String(wm["res"])):
		m = load(String(wm["res"]))
	elif shader.begins_with("textures/common/"):
		m = null
	else:
		m = _classic(shader)
	_materials[shader] = m
	return m


func _classic(shader: String) -> Material:
	var s: Dictionary = _scripts.get(shader, {})
	if (s.get("parms", []) as Array).has("nodraw"):
		return null
	var stages: Array = s.get("stages", [])
	var layers := stages.filter(func(st): return not String(st["map"]).begins_with("$"))
	var base: Dictionary = {}
	for st in layers:
		if st["tcgen"] != "environment" and st["blend"] != "add":
			base = st
			break
	if base.is_empty() and layers.size():
		base = layers[0]
	var tex := _texture(String(base.get("map", shader)))
	if tex == null:
		return Q3MapMaterials.missing()
	var lit := stages.any(func(st): return String(st["map"]) == "$lightmap")
	var blend := String(base.get("blend", "opaque"))
	var two := String(s.get("cull", "back")) in ["none", "disable", "twosided"]
	var shader_name := "world_opaque_2s" if two else "world_opaque"
	if base.get("alphafunc", "") != "":
		blend = "mask"
	elif blend == "add":
		shader_name = "world_add"
	elif blend == "blend" and (layers.is_empty() or base == layers[0]):
		shader_name = "world_blend_2s" if two else "world_blend"
	elif (blend == "filter" and not lit) or blend == "darken":
		shader_name = "world_mul"
	var mat := ShaderMaterial.new()
	mat.resource_name = shader
	mat.shader = load("res://shaders/world/%s.gdshader" % shader_name)
	mat.set_shader_parameter("albedo_tex", tex)
	mat.set_shader_parameter("orm_tex", _matte())
	if blend == "mask":
		mat.set_shader_parameter("alpha_cut", 0.5)
	if shader_name == "world_mul" and blend == "darken":
		mat.set_shader_parameter("invert", true)
	for tm in base.get("tcmods", []):
		if tm[0] == "scale" and tm.size() >= 3:
			mat.set_shader_parameter("base_scale", Vector2(float(tm[1]), float(tm[2])))
		elif tm[0] == "scroll" and tm.size() >= 3:
			mat.set_shader_parameter("base_scroll", Vector2(float(tm[1]), float(tm[2])))
	for st in layers:
		if st != base and st["blend"] == "add" and st["tcgen"] != "environment":
			var g := _texture(String(st["map"]))
			if g:
				mat.set_shader_parameter("glow1_tex", g)
				break
	return mat


static var _matte_tex: ImageTexture


## A flat ORM for images with no PBR maps: AO 1, rough 0.8, not metal (the white default is metal).
static func _matte() -> ImageTexture:
	if _matte_tex == null:
		var img := Image.create_empty(4, 4, false, Image.FORMAT_RGB8)
		img.fill(Color(1.0, 0.8, 0.0))
		_matte_tex = ImageTexture.create_from_image(img)
	return _matte_tex


## An image from the Maps folder's pk3s, else id's remastered copy of it, else null.
func _texture(path: String) -> Texture2D:
	var key := path.to_lower().get_basename()
	if _materials.has("img:" + key):
		return _materials["img:" + key]
	var t: Texture2D = null
	if _images.has(key):
		var z := ZIPReader.new()
		if z.open(_images[key][0]) == OK:
			var b := z.read_file(_images[key][1])
			var img := Image.new()
			var ext := String(_images[key][1]).get_extension().to_lower()
			var err: Error = (img.load_tga_from_buffer(b) if ext == "tga" else img.load_jpg_from_buffer(b) if ext == "jpg"
					else img.load_png_from_buffer(b))
			if err == OK:
				img.generate_mipmaps()
				t = ImageTexture.create_from_image(img)
			z.close()
	if t == null:
		var stem := key.trim_prefix("textures/").replace("/", "__")
		for suffix in ["_albedo.png", "_albedo.jpg"]:
			var r: String = "res://library/world/textures/" + stem + suffix
			if ResourceLoader.exists(r):
				t = load(r)
				break
	_materials["img:" + key] = t
	return t


func _model_entity(n: int) -> Dictionary:
	for e in _entities_list:
		if String(e.get("model", "")) == "*%d" % n:
			return e
	return {}


static func _vec(s: String) -> Vector3:
	var a := s.split_floats(" ", false)
	return Vector3(a[0], a[1], a[2]) if a.size() >= 3 else Vector3.ZERO


static func _to_godot(p: Vector3) -> Vector3:
	return Vector3(p.y, p.z, p.x) * UNIT


static func _dir(n: Vector3) -> Vector3:
	return Vector3(n.y, n.z, n.x)
