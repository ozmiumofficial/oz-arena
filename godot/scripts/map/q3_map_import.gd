class_name Q3MapImport
extends RefCounted
## Radiant .map files into the editor (MAP_EDITOR_PLAN.md E6): GtkRadiant, NetRadiant, TrenchBroom
## and this editor's own export. Every brush form q3map and those editors write:
##   Quake 3     ( p ) ( p ) ( p ) shader shiftS shiftT rotate scaleS scaleT [contents flags value]
##   brushDef    brush primitives: ( p ) ( p ) ( p ) ( ( a b c ) ( d e f ) ) shader [c f v]
##   Valve 220   ( p ) ( p ) ( p ) shader [ Ux Uy Uz offS ] [ Vx Vy Vz offT ] rotate scaleS scaleT [c f v]
##   patchDef2 / patchDef3 curves
## Planes come from the three points as q3map takes them ((p0 - p1) x (p2 - p1) points out).
## Mappings come back exactly: the Quake 3 form as shift/rotate/scale, the others as explicit
## axes, which become shift/rotate/scale when they have that form.

## A file's axes are exact, so they become shift/rotate/scale only when that's as exact (texels).
const EXACT := 0.0001


## {doc, report: {brushes, patches, entities, dropped, errors}}; doc is null when nothing could be read.
static func parse(text: String, name := "imported") -> Dictionary:
	var report := {"brushes": 0, "patches": 0, "entities": 0, "dropped": 0, "errors": []}
	var tk := _Tokens.new(text)
	var doc := Q3MapDoc.new()
	doc.info["name"] = name
	var first := true
	while tk.more():
		if tk.next() != "{":
			report["errors"].append("line %d: expected { to start an entity, found %s" % [tk.line, tk.last])
			return {"doc": null, "report": report}
		var keys := {}
		var prims := []
		while tk.more():
			var t := tk.next()
			if t == "}":
				break
			if t == "{":
				var p := _primitive(tk, report)
				if p.size():
					prims.append(p)
			elif tk.quoted:
				var v := tk.next()
				keys[t] = v
			else:
				report["errors"].append("line %d: unexpected %s" % [tk.line, t])
				return {"doc": null, "report": report}
		var cn := String(keys.get("classname", ""))
		keys.erase("classname")
		var eid := 0
		if cn == "worldspawn" or (first and cn == ""):
			keys.erase("mapversion")   # Valve 220's marker
			doc.worldspawn = keys
		elif cn in Q3MapExport.MERGED:
			pass   # a func_group's brushes are the world's (q3map merges them)
		else:
			eid = doc.add({"type": "entity", "id": 0, "group": 0, "classname": cn, "keys": keys})
			report["entities"] += 1
			if prims.size():
				var gid := doc.new_id()
				doc.groups[gid] = {"id": gid, "name": "%s %d" % [cn, eid], "parent": 0}
				var eo := doc.edit(eid)
				eo["group"] = gid
				doc.put(eo)
		first = false
		var group := int(doc.get_obj(eid).get("group", 0)) if eid != 0 else 0
		# q3map maps a brush entity's faces at its points less its origin; the editor keeps them in place
		var origin := Q3MapExport.entity_origin(keys, prims) if eid != 0 else Vector3.ZERO
		if origin != Vector3.ZERO:
			for p in prims:
				if p["type"] == "brush":
					_to_world(p, origin)
		for p in prims:
			p["entity"] = eid
			p["group"] = group
			doc.add(p)
			report["brushes" if p["type"] == "brush" else "patches"] += 1
	if tk.error != "":
		# past a token out of place, the rest is read out of step: nothing of it can be trusted
		report["errors"].append(tk.error)
		return {"doc": null, "report": report}
	doc.dirty = true
	return {"doc": doc, "report": report}


## After the "{" of a brush or patch: the object, or {} when it made nothing.
static func _primitive(tk: _Tokens, report: Dictionary) -> Dictionary:
	var t := tk.peek()
	if t == "patchDef2" or t == "patchDef3":
		tk.next()
		return _patch(tk, t == "patchDef3", report)
	var primit := false
	if t == "brushDef":
		tk.next()
		tk.expect("{")
		primit = true
	var planes := PackedFloat64Array()
	var faces := []
	while tk.more():
		t = tk.next()
		if t == "}":
			break
		if t != "(":
			report["errors"].append("line %d: a brush face starts with (, found %s" % [tk.line, t])
			tk.skip_to("}")
			return {}
		var pts := [tk.vec3_rest(), tk.vec3(), tk.vec3()]
		var face := {}
		var mat := [] as Array
		if primit:
			tk.expect("(")
			tk.expect("(")
			mat = [tk.num(), tk.num(), tk.num()]
			tk.expect(")")
			tk.expect("(")
			mat.append_array([tk.num(), tk.num(), tk.num()])
			tk.expect(")")
			tk.expect(")")
		var shader := _shader(tk.next())
		var plane := _plane(pts)
		face["material"] = shader
		if primit:
			face["uv"] = _uv_from_primit(mat, plane, Q3MapMaterials.size(shader))
		elif tk.peek() == "[":
			# Valve 220: texel axes and offsets; rotate and scale come after (the axes already carry
			# the turn; the scale divides them)
			tk.next()
			var u := Vector3(tk.num(), tk.num(), tk.num())
			var uoff := tk.num()
			tk.expect("]")
			tk.expect("[")
			var v := Vector3(tk.num(), tk.num(), tk.num())
			var voff := tk.num()
			tk.expect("]")
			tk.num()   # rotate
			var ss := tk.num()
			var st := tk.num()
			ss = ss if ss != 0.0 else 1.0
			st = st if st != 0.0 else 1.0
			face["uv"] = PackedFloat64Array([u.x / ss, u.y / ss, u.z / ss, uoff, v.x / st, v.y / st, v.z / st, voff])
		else:
			face["tex"] = [tk.num(), tk.num(), tk.num(), tk.num(), tk.num()]
		# contents, flags, value when the line has them
		if tk.number_on_line():
			var c := int(tk.num())
			var fl := int(tk.num())
			var val := int(tk.num())
			if c != 0 or fl != 0 or val != 0:
				face["flags"] = [c, fl, val]
		planes.append_array(plane)
		faces.append(face)
	if primit:
		tk.expect("}")
	if faces.size() < 4:
		report["dropped"] += 1
		return {}
	planes = Q3MapGeo.clean(planes)
	# faces whose planes make no polygon go (duplicates, planes outside the brush)
	var ws: Array = Q3MapGeo.brush_windings(planes)
	var kept := PackedFloat64Array()
	var kept_faces := []
	for i in faces.size():
		if i < ws.size() and (ws[i] as PackedFloat64Array).size() >= 9:
			kept.append_array(planes.slice(i * 4, i * 4 + 4))
			var f: Dictionary = faces[i]
			if f.has("uv"):
				var tex := Q3BspImport.uv_to_tex(f["uv"], planes.slice(i * 4, i * 4 + 4), ws[i], Q3MapMaterials.size(f["material"]), EXACT)
				if tex.size():
					f.erase("uv")
					f["tex"] = tex
			kept_faces.append(f)
	if kept_faces.size() < 4 or not Q3MapGeo.brush_valid(kept):
		report["dropped"] += 1
		return {}
	return {"type": "brush", "id": 0, "entity": 0, "group": 0, "planes": kept, "faces": kept_faces}


## A brush entity's faces, mapped as q3map maps them (at each point less the entity's origin), as
## the editor keeps them: in place. The inverse of Q3MapExport.model_space.
static func _to_world(b: Dictionary, origin: Vector3) -> void:
	var ws: Array = Q3MapGeo.brush_windings(b["planes"])
	for i in b["faces"].size():
		var f: Dictionary = b["faces"][i]
		var pl := Q3MapDoc.plane_of(b, i)
		var uv := Q3MapDoc.face_uv(f, pl)
		uv[3] -= uv[0] * origin.x + uv[1] * origin.y + uv[2] * origin.z
		uv[7] -= uv[4] * origin.x + uv[5] * origin.y + uv[6] * origin.z
		var tex := Q3BspImport.uv_to_tex(uv, pl, ws[i], Q3MapMaterials.size(f["material"]), EXACT)
		f.erase("uv")
		f.erase("tex")
		if tex.size():
			f["tex"] = tex
		else:
			f["uv"] = uv


## patchDef2 { shader ( w h 0 0 0 ) ( ( (x y z s t) ... ) ... ) }; patchDef3 has two more numbers
## in the size row. Columns of `height` points, as q3map reads them.
static func _patch(tk: _Tokens, def3: bool, report: Dictionary) -> Dictionary:
	tk.expect("{")
	var shader := _shader(tk.next())
	tk.expect("(")
	var w := int(tk.num())
	var h := int(tk.num())
	for i in (5 if def3 else 3):
		tk.num()
	tk.expect(")")
	tk.expect("(")
	var ctrl := PackedFloat64Array()
	ctrl.resize(w * h * 5)
	for col in w:
		tk.expect("(")
		for row in h:
			tk.expect("(")
			for k in 5:
				ctrl[(row * w + col) * 5 + k] = tk.num()
			tk.expect(")")
		tk.expect(")")
	tk.expect(")")
	# key/values some editors write inside the patch, then its two closing braces
	while tk.more() and tk.peek() != "}":
		tk.next()
	tk.expect("}")
	tk.expect("}")
	if w < 3 or h < 3 or w % 2 == 0 or h % 2 == 0:
		report["dropped"] += 1
		return {}
	return {"type": "patch", "id": 0, "entity": 0, "group": 0, "width": w, "height": h, "ctrl": ctrl, "material": shader}


## q3map prefixes a face's shader with "textures/"; some editors write it already.
static func _shader(name: String) -> String:
	return name if name.begins_with("textures/") else "textures/" + name


## The plane through three points as q3map's MapPlaneFromPoints: normal (p0 - p1) x (p2 - p1).
static func _plane(pts: Array) -> PackedFloat64Array:
	var a: Array = pts[0]
	var b: Array = pts[1]
	var c: Array = pts[2]
	var t1 := [a[0] - b[0], a[1] - b[1], a[2] - b[2]]
	var t2 := [c[0] - b[0], c[1] - b[1], c[2] - b[2]]
	var n := [t1[1] * t2[2] - t1[2] * t2[1], t1[2] * t2[0] - t1[0] * t2[2], t1[0] * t2[1] - t1[1] * t2[0]]
	var l := sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2])
	if l <= 0.0:
		return PackedFloat64Array([0, 0, 0, 0])   # points in a line: no plane, so it cuts nothing
	n = [n[0] / l, n[1] / l, n[2] / l]
	return Q3MapExport.snap_plane(PackedFloat64Array([n[0], n[1], n[2], a[0] * n[0] + a[1] * n[1] + a[2] * n[2]]))


## Brush primitives back to texel axes: s = m0 x + m1 y + m2 in repeats with x, y along q3map's
## ComputeAxisBase, so U = W (m0 texX + m1 texY), Uoff = W m2 (the plane's distance doesn't enter:
## texX and texY lie in the plane).
static func _uv_from_primit(m: Array, plane: PackedFloat64Array, size: Vector2) -> PackedFloat64Array:
	var ax := Q3MapExport.axis_base(Vector3(plane[0], plane[1], plane[2]))
	var tx: Vector3 = ax[0]
	var ty: Vector3 = ax[1]
	var u: Vector3 = (tx * float(m[0]) + ty * float(m[1])) * size.x
	var v: Vector3 = (tx * float(m[3]) + ty * float(m[4])) * size.y
	return PackedFloat64Array([u.x, u.y, u.z, float(m[2]) * size.x, v.x, v.y, v.z, float(m[5]) * size.y])


## A .map tokenizer: quoted strings, the bracket characters as tokens, // comments skipped.
class _Tokens:
	var text: String
	var i := 0
	var line := 1
	var last := ""
	var quoted := false
	var error := ""
	var _peeked: Variant = null

	func _init(t: String) -> void:
		text = t

	func more() -> bool:
		if _peeked != null:
			return true
		_skip_space()
		return i < text.length()

	func peek() -> String:
		if _peeked == null:
			var q := quoted
			_peeked = [_read(), quoted]
			quoted = q
		return String(_peeked[0])

	func next() -> String:
		if _peeked != null:
			last = _peeked[0]
			quoted = _peeked[1]
			_peeked = null
			return last
		last = _read()
		return last

	func expect(t: String) -> void:
		var got := next()
		if got != t and error == "":
			error = "line %d: expected %s, found %s" % [line, t, got]

	func num() -> float:
		var t := next()
		if not t.is_valid_float() and error == "":
			error = "line %d: expected a number, found %s" % [line, t]
		return t.to_float()

	func vec3() -> Array:
		expect("(")
		return vec3_rest()

	## The rest of "( x y z )" after its "(".
	func vec3_rest() -> Array:
		var v := [num(), num(), num()]
		expect(")")
		return v

	## Whether a number follows on the same line (a face's optional contents / flags / value).
	func number_on_line() -> bool:
		if _peeked != null:
			return String(_peeked[0]).is_valid_float()
		var j := i
		while j < text.length() and text[j] in [" ", "\t", "\r"]:
			j += 1
		if j >= text.length() or text[j] == "\n":
			return false
		return text[j] in "-+0123456789."

	func skip_to(t: String) -> void:
		while more() and next() != t:
			pass

	func _skip_space() -> void:
		while i < text.length():
			var c := text[i]
			if c == "\n":
				line += 1
				i += 1
			elif c in [" ", "\t", "\r"]:
				i += 1
			elif c == "/" and i + 1 < text.length() and text[i + 1] == "/":
				while i < text.length() and text[i] != "\n":
					i += 1
			else:
				break

	func _read() -> String:
		_skip_space()
		quoted = false
		if i >= text.length():
			return ""
		var c := text[i]
		if c == "\"":
			var j := text.find("\"", i + 1)
			if j < 0:
				j = text.length()
			var s := text.substr(i + 1, j - i - 1)
			line += s.count("\n")
			i = j + 1
			quoted = true
			return s
		if c in ["(", ")", "{", "}", "[", "]"]:
			i += 1
			return c
		var start := i
		while i < text.length() and not text[i] in [" ", "\t", "\r", "\n", "(", ")", "{", "}", "[", "]", "\""]:
			i += 1
		return text.substr(start, i - start)
