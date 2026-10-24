class_name Q3MapExport
extends RefCounted
## The editor document as a Radiant .map for id's q3map (MAP_EDITOR_PLAN.md E5).
## Every brush is written in Radiant's brush-primitives form (brushDef): each face's three plane
## points plus a 2x3 texture matrix in q3map's ComputeAxisBase frame. That carries any mapping
## exactly, locked and imported faces included; the older form reads shift and rotation as whole
## numbers. q3map takes a brushDef face's texture coordinates straight from the matrix, so the
## compiled map is mapped exactly as the editor shows it. Patches are patchDef2.
## worldspawn comes first with the world's brushes and patches (func_group's too: q3map merges
## them into the world); then every entity, a brush entity with its own. q3map numbers brush
## models (*1, *2 ...) over the entities that keep a brush or patch, in file order (SetModelNumbers),
## which `models` reports.
## `format` "valve220" writes Valve 220 faces instead (texel axes, offsets and scales, with
## "mapversion" "220" in worldspawn): TrenchBroom's Quake 3 (Valve) format, which q3map2 also reads.

const MAX_SHADER := 63           ## q3map copies shader names into 64-byte buffers (MAX_QPATH)
const MAX_KEY := 31              ## ParseEpair: keys under 32 characters, values under 1024
const MAX_VALUE := 1023
const MAX_PATCH := 32            ## MAX_PATCH_SIZE
const NORMAL_EPSILON := 0.00001  ## map.c SnapVector / SnapPlane
const DIST_EPSILON := 0.01
## Brushes q3map keeps out of the brush model (FinishBrush): the origin brush sets the entity's
## rotation origin instead, and area portals belong to the world only.
const NOT_KEPT := ["textures/common/origin", "textures/common/areaportal"]
const MERGED := ["func_group", "group_info"]


## {text, models: {entity id: inline model number}, warnings: [String]}
static func to_map(doc: Q3MapDoc, format := "brushdef") -> Dictionary:
	var warn := {}
	var own := Q3MapEntities.owned(doc)
	var merged := {}
	for id in doc.objects:
		var o: Dictionary = doc.objects[id]
		if o["type"] == "entity" and String(o["classname"]) in MERGED:
			merged[id] = true
	var out := PackedStringArray()
	out.append("// %s, written by the Quake III Arena Remastered map editor" % String(doc.info.get("name", "untitled")).replace("\n", " ").replace("\r", " "))
	# worldspawn
	var ws := doc.worldspawn.duplicate()
	ws.erase("classname")
	out.append("// entity 0")
	out.append("{")
	out.append(_pair("classname", "worldspawn"))
	ws.erase("mapversion")
	if format == "valve220":
		out.append(_pair("mapversion", "220"))
	var keys := ws.keys()
	keys.sort()
	for k in keys:
		_add_pair(out, k, ws[k], warn)
	var n := 0
	for id in _sorted(doc.objects.keys()):
		var o: Dictionary = doc.objects[id]
		var e := int(o.get("entity", 0))
		if o["type"] != "entity" and (e == 0 or merged.has(e) or not doc.objects.has(e)):
			n = _write_object(out, doc, id, n, warn, format)
	out.append("}")
	# entities, in id order
	var models := {}
	var ent_index := 0
	var model := 0
	for id in _sorted(doc.objects.keys()):
		var e: Dictionary = doc.objects[id]
		if e["type"] != "entity" or merged.has(id):
			continue
		var cn: String = e["classname"]
		var parts: Array = _sorted(own.get(id, []))
		var kept := parts.filter(func(p): return _kept(doc, p))
		if Q3MapEntities.is_brush_class(cn) and kept.is_empty() and not e["keys"].has("model"):
			# it would spawn without a model, and the game stops at SV_SetBrushModel's ERR_DROP
			warn["%s (entity %d) has no brushes and was left out" % [cn, id]] = true
			continue
		ent_index += 1
		out.append("// entity %d" % ent_index)
		out.append("{")
		out.append(_pair("classname", cn))
		var ek: Array = e["keys"].keys()
		ek.sort()
		for k in ek:
			if k != "classname" and not (k == "model" and String(e["keys"][k]).begins_with("*")):
				_add_pair(out, k, e["keys"][k], warn)
		if kept.size():
			model += 1
			models[id] = model
		var bn := 0
		var origin := entity_origin(e["keys"], parts.map(func(p): return doc.get_obj(p)))
		for pid in parts:
			bn = _write_object(out, doc, pid, bn, warn, format, origin)
		out.append("}")
	return {"text": "\n".join(out) + "\n", "models": models, "warnings": warn.keys()}


## What q3map moves a brush entity's brushes by (AdjustBrushesForOrigin), and so where its texture
## mappings are evaluated from: the middle of its origin brush in whole units (cut toward 0, as q3map
## writes it), else its "origin" key. objs: the brushes and curves it owns.
static func entity_origin(keys: Dictionary, objs: Array) -> Vector3:
	for o in objs:
		if o.get("type") != "brush":
			continue
		for f in o["faces"]:
			if f["material"] == "textures/common/origin":
				var bb: PackedFloat64Array = Q3MapGeo.brush_bounds(o["planes"])
				return Vector3(int((bb[0] + bb[3]) * 0.5), int((bb[1] + bb[4]) * 0.5), int((bb[2] + bb[5]) * 0.5))
	return Q3MapEntities.vec(String(keys.get("origin", "0 0 0"))) if keys.has("origin") else Vector3.ZERO


## A face's mapping as q3map applies it in a brush entity moved by origin: texels at p - origin
## are the editor's at p, so the offsets gain U . origin; the plane is the moved one.
static func model_space(uv: PackedFloat64Array, pl: PackedFloat64Array, origin: Vector3) -> Array:
	if origin == Vector3.ZERO:
		return [uv, pl]
	var u := uv.duplicate()
	u[3] += uv[0] * origin.x + uv[1] * origin.y + uv[2] * origin.z
	u[7] += uv[4] * origin.x + uv[5] * origin.y + uv[6] * origin.z
	return [u, PackedFloat64Array([pl[0], pl[1], pl[2], pl[3] - (pl[0] * origin.x + pl[1] * origin.y + pl[2] * origin.z)])]


static func _sorted(ids: Array) -> Array:
	var a := ids.duplicate()
	a.sort()
	return a


## Whether q3map keeps this brush or patch in its entity's model: a closed brush that isn't an
## origin or area portal brush, or a patch it can read.
static func _kept(doc: Q3MapDoc, id: int) -> bool:
	var o: Dictionary = doc.objects[id]
	if o["type"] == "patch":
		return int(o["width"]) <= MAX_PATCH and int(o["height"]) <= MAX_PATCH
	var faces := 0
	var ws := doc.windings(id)
	for w in ws:
		if (w as PackedFloat64Array).size() >= 9:
			faces += 1
	if faces < 4:
		return false
	for f in o["faces"]:
		if String(f["material"]) in NOT_KEPT:
			return false
	return true


## Keys and values can't hold quotes (q3map's tokenizer ends at one), so they become apostrophes.
static func _pair(k: String, v: Variant) -> String:
	return "\"%s\" \"%s\"" % [k.replace("\"", "'"), String(v).replace("\"", "'").replace("\n", " ")]


static func _add_pair(out: PackedStringArray, k: String, v: Variant, warn: Dictionary) -> void:
	if k.length() > MAX_KEY or String(v).length() > MAX_VALUE:
		warn["key \"%s\" is too long for q3map and was left out" % k.left(40)] = true
		return
	out.append(_pair(k, v))


## A brush or patch as the n-th primitive of its entity; returns n + 1 (or n when it was left out).
static func _write_object(out: PackedStringArray, doc: Q3MapDoc, id: int, n: int, warn: Dictionary, format: String,
		origin := Vector3.ZERO) -> int:
	var o: Dictionary = doc.objects[id]
	match o["type"]:
		"brush":
			var faces := _brush_faces(doc, o, warn, format, origin)
			if faces.size() < 4:
				warn["brush %d isn't a closed solid and was left out" % id] = true
				return n
			out.append("// brush %d" % n)
			out.append("{")
			if format == "valve220":
				out.append_array(faces)
			else:
				out.append("brushDef")
				out.append("{")
				out.append_array(faces)
				out.append("}")
			out.append("}")
			return n + 1
		"patch":
			var w: int = o["width"]
			var h: int = o["height"]
			if w > MAX_PATCH or h > MAX_PATCH:
				warn["curve %d is bigger than q3map's 32 x 32 control points and was left out" % id] = true
				return n
			var shader := _shader(String(o["material"]), warn)
			var c: PackedFloat64Array = o["ctrl"]
			out.append("// brush %d" % n)
			out.append("{")
			out.append("patchDef2")
			out.append("{")
			out.append(shader)
			out.append("( %d %d 0 0 0 )" % [w, h])
			out.append("(")
			# q3map reads columns of `height` points (ParsePatch: verts[row * width + column])
			for col in w:
				var line := "( "
				for row in h:
					var k := (row * w + col) * 5
					line += "( %s %s %s %s %s ) " % [_n(c[k]), _n(c[k + 1]), _n(c[k + 2]), _n(c[k + 3]), _n(c[k + 4])]
				out.append(line + ")")
			out.append(")")
			out.append("}")
			out.append("}")
			return n + 1
	return n


## A brush's faces as brushDef (or Valve 220) lines, one per face with a polygon (planes that cut
## nothing are dropped, as q3map would). origin: the brush entity's (entity_origin).
static func _brush_faces(doc: Q3MapDoc, b: Dictionary, warn: Dictionary, format := "brushdef", origin := Vector3.ZERO) -> PackedStringArray:
	var lines := PackedStringArray()
	var ws := doc.windings(b["id"])
	for i in b["faces"].size():
		if i >= ws.size() or (ws[i] as PackedFloat64Array).size() < 9:
			continue
		var pl := Q3MapDoc.plane_of(b, i)
		var pts := _plane_points(ws[i], Vector3(pl[0], pl[1], pl[2]))
		var f: Dictionary = b["faces"][i]
		var ms := model_space(Q3MapDoc.face_uv(f, pl), pl, origin)
		var uv: PackedFloat64Array = ms[0]
		var flags: Array = f.get("flags", [0, 0, 0])
		if format == "valve220":
			lines.append(_valve_face(pts, uv, _shader(String(f["material"]), warn), flags))
			continue
		var m := _tex_matrix(uv, ms[1], Q3MapMaterials.size(f["material"]))
		lines.append("( %s ) ( %s ) ( %s ) ( ( %s %s %s ) ( %s %s %s ) ) %s %d %d %d" % [
				_v(pts[0]), _v(pts[1]), _v(pts[2]), _n(m[0]), _n(m[1]), _n(m[2]), _n(m[3]), _n(m[4]), _n(m[5]),
				_shader(String(f["material"]), warn), int(flags[0]), int(flags[1]), int(flags[2])])
	return lines


## ( p ) ( p ) ( p ) shader [ Ux Uy Uz offS ] [ Vx Vy Vz offT ] rotate scaleS scaleT contents flags value:
## texels = (p . axis) / scale + offset, so each axis is the face's texel axis made unit length and
## its scale one over that length. The rotation is already in the axes; editors show it as 0.
static func _valve_face(pts: Array, uv: PackedFloat64Array, shader: String, flags: Array) -> String:
	var u := Vector3(uv[0], uv[1], uv[2])
	var v := Vector3(uv[4], uv[5], uv[6])
	var su := 1.0 / u.length() if u.length() > 1e-9 else 1.0
	var sv := 1.0 / v.length() if v.length() > 1e-9 else 1.0
	return "( %s ) ( %s ) ( %s ) %s [ %s %s %s %s ] [ %s %s %s %s ] 0 %s %s %d %d %d" % [_v(pts[0]), _v(pts[1]), _v(pts[2]), shader,
			_n(uv[0] * su), _n(uv[1] * su), _n(uv[2] * su), _n(uv[3]), _n(uv[4] * sv), _n(uv[5] * sv), _n(uv[6] * sv), _n(uv[7]),
			_n(su), _n(sv), int(flags[0]), int(flags[1]), int(flags[2])]


## Three corners of a face's polygon: the best-conditioned triple (the largest triangle), ordered so
## q3map's MapPlaneFromPoints ((p0 - p1) x (p2 - p1)) gives the outward normal. In doubles.
static func _plane_points(w: PackedFloat64Array, nrm: Vector3) -> Array:
	var count := int(w.size() / 3.0)
	var pts := []
	for i in count:
		pts.append([w[i * 3], w[i * 3 + 1], w[i * 3 + 2]])
	var best := [0, 1, 2]
	var best_area := -1.0
	for i in count:
		for j in range(i + 1, count):
			for k in range(j + 1, count):
				var c := _cross(_sub(pts[i], pts[j]), _sub(pts[k], pts[j]))
				var area: float = c[0] * c[0] + c[1] * c[1] + c[2] * c[2]
				if area > best_area:
					best_area = area
					best = [i, j, k]
	var c := _cross(_sub(pts[best[0]], pts[best[1]]), _sub(pts[best[2]], pts[best[1]]))
	if c[0] * nrm.x + c[1] * nrm.y + c[2] * nrm.z < 0.0:
		best = [best[2], best[1], best[0]]
	return [pts[best[0]], pts[best[1]], pts[best[2]]]


static func _sub(a: Array, b: Array) -> Array:
	return [a[0] - b[0], a[1] - b[1], a[2] - b[2]]


static func _cross(a: Array, b: Array) -> Array:
	return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]


## The face's texel axes as q3map's brush-primitive matrix: s = m0 x + m1 y + m2, t = m3 x + m4 y + m5,
## where x and y are the point projected onto ComputeAxisBase's texX and texY, and s and t are in
## texture repeats (texels over the texture's size). The plane is snapped first as q3map snaps it
## (SnapPlane), since the frame of a nearly axial plane swings around with the tiniest tilt.
static func _tex_matrix(uv: PackedFloat64Array, pl: PackedFloat64Array, size: Vector2) -> PackedFloat64Array:
	var s := snap_plane(pl)
	var nrm := Vector3(s[0], s[1], s[2])
	var ax := axis_base(nrm)
	var tx: Vector3 = ax[0]
	var ty: Vector3 = ax[1]
	# a point on the plane is x tx + y ty + d n, so U . p + Uoff = x (U . tx) + y (U . ty) + d (U . n) + Uoff
	var d := s[3]
	var u := Vector3(uv[0], uv[1], uv[2])
	var v := Vector3(uv[4], uv[5], uv[6])
	var w := size.x if size.x > 0 else 64.0
	var h := size.y if size.y > 0 else 64.0
	return PackedFloat64Array([u.dot(tx) / w, u.dot(ty) / w, (uv[3] + d * u.dot(nrm)) / w,
			v.dot(tx) / h, v.dot(ty) / h, (uv[7] + d * v.dot(nrm)) / h])


## map.c SnapPlane: a normal within 1e-5 of an axis becomes that axis; a distance within 0.01 of a
## whole number becomes it.
static func snap_plane(pl: PackedFloat64Array) -> PackedFloat64Array:
	var out := PackedFloat64Array([pl[0], pl[1], pl[2], pl[3]])
	for i in 3:
		if absf(pl[i] - 1.0) < NORMAL_EPSILON or absf(pl[i] + 1.0) < NORMAL_EPSILON:
			out[0] = 0.0
			out[1] = 0.0
			out[2] = 0.0
			out[i] = signf(pl[i])
			break
	if absf(pl[3] - roundf(pl[3])) < DIST_EPSILON:
		out[3] = roundf(pl[3])
	return out


## q3map's brush_primit.c ComputeAxisBase: [texX, texY] for a plane normal.
static func axis_base(normal: Vector3) -> Array:
	var nx := 0.0 if absf(normal.x) < 1e-6 else normal.x
	var ny := 0.0 if absf(normal.y) < 1e-6 else normal.y
	var nz := 0.0 if absf(normal.z) < 1e-6 else normal.z
	var rot_y := -atan2(nz, sqrt(ny * ny + nx * nx))
	var rot_z := atan2(ny, nx)
	return [Vector3(-sin(rot_z), cos(rot_z), 0.0),
			Vector3(-sin(rot_y) * cos(rot_z), -sin(rot_y) * sin(rot_z), -cos(rot_y))]


## The shader name as q3map wants it on a brush face or patch: without "textures/". Names q3map
## can't take (other folders, too long, with spaces) fall back to caulk, with a warning.
static func _shader(material: String, warn: Dictionary) -> String:
	if not material.begins_with("textures/"):
		warn["%s isn't under textures/ (q3map only takes those); caulk was used" % material] = true
		return "common/caulk"
	var s := material.trim_prefix("textures/")
	if material.length() > MAX_SHADER or s.contains(" ") or s.contains("\t") or s.contains(";"):
		warn["%s can't be written for q3map; caulk was used" % material] = true
		return "common/caulk"
	return s


static func _v(p: Array) -> String:
	return "%s %s %s" % [_n(p[0]), _n(p[1]), _n(p[2])]


## A number as plainly as it reads exactly: whole numbers bare, others to 10 decimals (texture
## matrix slopes are about 1/size, so 6 would drift a texel across a big map).
static func _n(x: float) -> String:
	if absf(x - roundf(x)) < 1e-9:
		return str(int(roundf(x)))
	var s := String.num(x, 10)
	return "0" if s == "-0" else s
