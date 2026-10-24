extends Node
## Map editor geometry self-test (MAP_EDITOR_PLAN.md E0): the Q3MapGeo kernel, Q3MapDoc (undo, files)
## and Q3MapMesher, after OZMIUM's MapGeometrySelfTest.
##   godot --headless --path . res://tests/map_geo_test.tscn
## Prints one line per check and "MAP GEO n/m passed"; exit code 1 on any failure. The self-test runs
## the same checks through run_checks().


func _ready() -> void:
	var checks := run_checks(self)
	var ok := 0
	for c in checks:
		ok += int(c[1])
		print("  %s  %s%s" % ["PASS" if c[1] else "FAIL", c[0], ("  (" + c[2] + ")") if c[2] != "" else ""])
	print("MAP GEO %d/%d passed" % [ok, checks.size()])
	get_tree().quit(0 if ok == checks.size() else 1)


static func _box(a: Vector3, b: Vector3) -> PackedFloat64Array:
	return Q3MapGeo.box(PackedFloat64Array([a.x, a.y, a.z]), PackedFloat64Array([b.x, b.y, b.z]))


static func _v(a: PackedFloat64Array, i: int) -> Vector3:
	return Vector3(a[i * 3], a[i * 3 + 1], a[i * 3 + 2])


## Volume by the divergence theorem, and whether every winding runs counter-clockwise from outside.
## In doubles (GDScript floats), not Vector3, which is 32-bit.
static func _volume(planes: PackedFloat64Array) -> Array:
	var vol := 0.0
	var outward := true
	var ws: Array = Q3MapGeo.brush_windings(planes)
	for f in ws.size():
		var w: PackedFloat64Array = ws[f]
		if w.is_empty():
			continue
		var sx := 0.0
		var sy := 0.0
		var sz := 0.0
		for i in range(2, w.size() / 3):
			var ax := w[(i - 1) * 3] - w[0]
			var ay := w[(i - 1) * 3 + 1] - w[1]
			var az := w[(i - 1) * 3 + 2] - w[2]
			var bx := w[i * 3] - w[0]
			var by := w[i * 3 + 1] - w[1]
			var bz := w[i * 3 + 2] - w[2]
			sx += ay * bz - az * by
			sy += az * bx - ax * bz
			sz += ax * by - ay * bx
		var along := sx * planes[f * 4] + sy * planes[f * 4 + 1] + sz * planes[f * 4 + 2]
		outward = outward and along > 0
		vol += along * 0.5 * planes[f * 4 + 3] / 3.0
	return [vol, outward]


static func _faces(planes: PackedFloat64Array) -> int:
	return Q3MapGeo.brush_face_planes(planes).size()


static func _all_integer(a: PackedFloat64Array) -> bool:
	for x in a:
		if x != roundf(x):
			return false
	return true


static func run_checks(host: Node) -> Array:
	var out := []
	var check := func(name: String, ok: bool, detail := "") -> void:
		out.append([name, ok, detail])
	if not ClassDB.class_exists("Q3MapGeo"):
		check.call("map geo: Q3MapGeo loads", false, "run tools/build_q3core.py")
		return out

	# ---- brushes
	var box := _box(Vector3(0, 0, 0), Vector3(64, 32, 16))
	var bv: Array = _volume(box)
	check.call("map geo: box has 6 faces, 8 corners, volume 64x32x16, windings face out",
			_faces(box) == 6 and Q3MapGeo.brush_vertices(box).size() == 24 and absf(bv[0] - 32768.0) < 1e-6 and bv[1],
			"volume %.3f" % bv[0])
	var open := box.slice(0, 20)
	var flat := _box(Vector3(0, 0, 0), Vector3(64, 32, 0))
	check.call("map geo: open and zero-thickness brushes are invalid",
			Q3MapGeo.brush_valid(box) and not Q3MapGeo.brush_valid(open) and not Q3MapGeo.brush_valid(flat))
	var dup := box.duplicate()
	dup.append_array(box.slice(0, 4))
	check.call("map geo: a duplicate plane makes no second face", _faces(dup) == 6 and Q3MapGeo.brush_valid(dup))
	var extra := box.duplicate()
	extra.append_array([1, 0, 0, 200])   # outside: no face
	check.call("map geo: a plane outside the solid makes no face", _faces(extra) == 6)

	# ---- transforms
	var r90: PackedFloat64Array = Q3MapGeo.transform(box, Q3MapDoc.yaw_rotation(90, Vector3(8, 8, 0)))
	check.call("map geo: 90 degree rotation stays on integers",
			_all_integer(Q3MapGeo.brush_vertices(r90)) and absf(_volume(r90)[0] - 32768.0) < 1e-9,
			str(Q3MapGeo.brush_bounds(r90)))
	var r15: PackedFloat64Array = Q3MapGeo.transform(box, Q3MapDoc.yaw_rotation(15, Vector3(0, 0, 0)))
	var r15b: PackedFloat64Array = Q3MapGeo.transform(r15, Q3MapDoc.yaw_rotation(-15, Vector3(0, 0, 0)))
	check.call("map geo: 15 degree rotation keeps the volume, and back again returns the corners",
			absf(_volume(r15)[0] - 32768.0) < 1e-6 and Q3MapGeo.brush_bounds(r15b) == PackedFloat64Array([0, 0, 0, 64, 32, 16]),
			"volume %.9f, back %s" % [_volume(r15)[0], Q3MapGeo.brush_bounds(r15b)])
	var mir: PackedFloat64Array = Q3MapGeo.transform(box, Q3MapDoc.mirror(0, Vector3(0, 0, 0)))
	var mv: Array = _volume(mir)
	check.call("map geo: a mirror keeps faces pointing out", Q3MapGeo.brush_valid(mir) and mv[1] and absf(mv[0] - 32768.0) < 1e-9,
			str(Q3MapGeo.brush_bounds(mir)))

	# ---- convex hull (vertex tool)
	var corners := Q3MapGeo.brush_vertices(_box(Vector3(0, 0, 0), Vector3(32, 32, 32)))
	var hull: PackedFloat64Array = Q3MapGeo.hull(corners)
	var cut := PackedFloat64Array()
	for i in corners.size() / 3:
		if _v(corners, i) != Vector3(32, 32, 32):
			cut.append_array([corners[i * 3], corners[i * 3 + 1], corners[i * 3 + 2]])
	var chamfer: PackedFloat64Array = Q3MapGeo.hull(cut)
	var wedge_pts := PackedFloat64Array()
	for i in corners.size() / 3:
		var p := _v(corners, i)
		if not (p.x == 32 and p.z == 32):
			wedge_pts.append_array([p.x, p.y, p.z])
	var wedge: PackedFloat64Array = Q3MapGeo.hull(wedge_pts)
	check.call("map geo: hull of a cube's corners is the cube; one corner off gives 7 faces; an edge off a wedge",
			_faces(hull) == 6 and absf(_volume(hull)[0] - 32768.0) < 1e-9
			and _faces(chamfer) == 7 and absf(_volume(chamfer)[0] - (32768.0 - 32768.0 / 6.0)) < 1e-6
			and _faces(wedge) == 5 and absf(_volume(wedge)[0] - 16384.0) < 1e-6,
			"%d / %d / %d faces" % [_faces(hull), _faces(chamfer), _faces(wedge)])
	check.call("map geo: flat points make no hull",
			Q3MapGeo.hull(PackedFloat64Array([0, 0, 0, 32, 0, 0, 0, 32, 0, 32, 32, 0])).is_empty())

	# ---- split and subtract
	var sp: Dictionary = Q3MapGeo.split(box, PackedFloat64Array([1, 0, 0, 20]))
	var sp_out: Dictionary = Q3MapGeo.split(box, PackedFloat64Array([1, 0, 0, 100]))
	check.call("map geo: split by a plane gives two pieces that add up; a plane outside leaves one",
			absf(_volume(sp["front"])[0] - 44 * 32 * 16) < 1e-9 and absf(_volume(sp["back"])[0] - 20 * 32 * 16) < 1e-9
			and sp_out["front"].is_empty() and _faces(sp_out["back"]) == 6)
	var cutter := _box(Vector3(16, -8, -8), Vector3(48, 40, 8))
	var s: Dictionary = Q3MapGeo.subtract(box, cutter)
	var total := 0.0
	var inherit := true
	for p in s["pieces"]:
		total += _volume(p["planes"])[0]
		for i in p["attr"].size():
			# every face takes attributes from a target face pointing the same way
			var a: int = p["attr"][i]
			inherit = inherit and a >= 0 and Vector3(p["planes"][i * 4], p["planes"][i * 4 + 1], p["planes"][i * 4 + 2]).dot(
					Vector3(box[a * 4], box[a * 4 + 1], box[a * 4 + 2])) > 0.99
	check.call("map geo: subtract cuts a slot: pieces fill the rest, cut faces inherit from parallel faces",
			s["touched"] and s["pieces"].size() == 3 and absf(total - (32768.0 - 32 * 32 * 8)) < 1e-6 and inherit,
			"%d pieces, volume %.1f" % [s["pieces"].size(), total])
	var apart: Dictionary = Q3MapGeo.subtract(box, _box(Vector3(100, 0, 0), Vector3(132, 32, 32)))
	var touching: Dictionary = Q3MapGeo.subtract(box, _box(Vector3(64, 0, 0), Vector3(96, 32, 16)))
	var inside: Dictionary = Q3MapGeo.subtract(box, _box(Vector3(-8, -8, -8), Vector3(72, 40, 24)))
	check.call("map geo: subtract leaves apart and touching brushes alone, and a swallowed brush disappears",
			not apart["touched"] and not touching["touched"] and inside["touched"] and inside["pieces"].is_empty())
	var tilted := Q3MapGeo.transform(_box(Vector3(-16, -16, -64), Vector3(16, 16, 64)), Q3MapDoc.yaw_rotation(30, Vector3.ZERO))
	var big := _box(Vector3(-128, -128, -32), Vector3(128, 128, 32))
	var hole: Dictionary = Q3MapGeo.subtract(big, tilted)
	var hv := 0.0
	var all_valid := true
	for p in hole["pieces"]:
		hv += _volume(p["planes"])[0]
		all_valid = all_valid and Q3MapGeo.brush_valid(p["planes"])
	check.call("map geo: a rotated cutter punches a clean hole", hole["touched"] and all_valid
			and absf(hv - (256.0 * 256 * 64 - 32.0 * 32 * 64)) < 1e-4, "%d pieces, %.4f" % [hole["pieces"].size(), hv])

	# ---- picking
	var hit: PackedFloat64Array = Q3MapGeo.ray(box, PackedFloat64Array([-100, 16, 8]), PackedFloat64Array([1, 0, 0]))
	var from_inside: PackedFloat64Array = Q3MapGeo.ray(box, PackedFloat64Array([8, 8, 8]), PackedFloat64Array([1, 0, 0]))
	var miss: PackedFloat64Array = Q3MapGeo.ray(box, PackedFloat64Array([-100, 100, 8]), PackedFloat64Array([1, 0, 0]))
	check.call("map geo: ray picks the face it enters, misses past the brush and from inside",
			hit.size() == 2 and hit[0] == 100.0 and int(hit[1]) == 1 and from_inside.is_empty() and miss.is_empty(), str(hit))

	# ---- texture mapping
	var floor_ax: PackedFloat64Array = Q3MapGeo.texture_axes(PackedFloat64Array([0, 0, 1]))
	var wall_ax: PackedFloat64Array = Q3MapGeo.texture_axes(PackedFloat64Array([1, 0, 0]))
	check.call("map geo: Quake 3's default texture axes (floor, west wall)",
			floor_ax == PackedFloat64Array([1, 0, 0, 0, -1, 0]) and wall_ax == PackedFloat64Array([0, 1, 0, 0, 0, -1]))
	var face := {"material": "x", "tex": [16.0, 8.0, 0.0, 0.5, 0.5]}
	var top := PackedFloat64Array([0, 0, 1, 16])
	var uv := Q3MapDoc.face_uv(face, top)
	check.call("map geo: world projection follows QuakeTextureVecs (scale 0.5, shift)",
			uv == PackedFloat64Array([2, 0, 0, 16, 0, -2, 0, 8]), str(uv))
	var rot90 := Q3MapDoc.face_uv({"material": "x", "tex": [0.0, 0.0, 90.0, 1.0, 1.0]}, top)
	check.call("map geo: texture rotation 90 is exact", rot90 == PackedFloat64Array([0, 1, 0, 0, 1, 0, 0, 0]), str(rot90))
	var lock_ok := true
	for x in [Q3MapDoc.translation(Vector3(37, -5, 3)), Q3MapDoc.yaw_rotation(15, Vector3(10, 20, 0)), Q3MapDoc.mirror(1, Vector3(0, 7, 0))]:
		var moved: PackedFloat64Array = Q3MapGeo.transform_uv(uv, x)
		for p in [Vector3(3, 4, 16), Vector3(-20, 7, 16), Vector3(50, 31, 16)]:
			var q := Vector3(x[0] * p.x + x[1] * p.y + x[2] * p.z + x[9], x[3] * p.x + x[4] * p.y + x[5] * p.z + x[10],
					x[6] * p.x + x[7] * p.y + x[8] * p.z + x[11])
			var before := Vector2(uv[0] * p.x + uv[1] * p.y + uv[2] * p.z + uv[3], uv[4] * p.x + uv[5] * p.y + uv[6] * p.z + uv[7])
			var after := Vector2(moved[0] * q.x + moved[1] * q.y + moved[2] * q.z + moved[3], moved[4] * q.x + moved[5] * q.y + moved[6] * q.z + moved[7])
			lock_ok = lock_ok and before.distance_to(after) < 1e-4
	check.call("map geo: texture lock keeps texels in place through move, rotate and mirror", lock_ok)

	# ---- meshing
	var doc := Q3MapDoc.new()
	var b := Q3MapDoc.box_brush(Vector3(0, 0, 0), Vector3(64, 64, 64))
	b["faces"][4]["material"] = "textures/base_floor/clangdark"
	var bid := doc.add(b)
	var ws := doc.windings(bid)
	var planes: PackedFloat64Array = doc.get_obj(bid)["planes"]
	var uvs := PackedFloat64Array()
	var sizes := PackedFloat64Array()
	var slots := PackedInt32Array()
	for i in 6:
		uvs.append_array(Q3MapDoc.face_uv(doc.get_obj(bid)["faces"][i], Q3MapDoc.plane_of(doc.get_obj(bid), i)))
		sizes.append_array([256, 256])
		slots.append(1 if i == 4 else 0)
	var surf: Dictionary = Q3MapGeo.mesh_faces(ws, planes, uvs, sizes, slots, PackedColorArray())
	var tris := 0
	var winding_ok := true
	var tangent_ok := true
	var mesh := ArrayMesh.new()
	for slot in surf:
		var a: Array = surf[slot]
		var v: PackedVector3Array = a[Mesh.ARRAY_VERTEX]
		var n: PackedVector3Array = a[Mesh.ARRAY_NORMAL]
		var t: PackedFloat32Array = a[Mesh.ARRAY_TANGENT]
		var tuv: PackedVector2Array = a[Mesh.ARRAY_TEX_UV]
		var idx: PackedInt32Array = a[Mesh.ARRAY_INDEX]
		tris += idx.size() / 3
		for k in range(0, idx.size(), 3):
			# Godot's front faces are clockwise as seen, so the triangle's cross product points in
			var g := (v[idx[k + 1]] - v[idx[k]]).cross(v[idx[k + 2]] - v[idx[k]])
			winding_ok = winding_ok and g.dot(n[idx[k]]) < 0
		for k in v.size():
			# moving along the tangent raises u
			var tan := Vector3(t[k * 4], t[k * 4 + 1], t[k * 4 + 2])
			tangent_ok = tangent_ok and absf(tan.dot(n[k])) < 1e-5
		mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, a)
	# Godot's own tangents (mikktspace) for the same mesh must agree with ours, sign included
	var agree := true
	for si in mesh.get_surface_count():
		var st := SurfaceTool.new()
		var a2: Array = mesh.surface_get_arrays(si)
		var ours: PackedFloat32Array = a2[Mesh.ARRAY_TANGENT]
		a2[Mesh.ARRAY_TANGENT] = null
		st.create_from_arrays(a2)
		st.generate_tangents()
		var theirs: PackedFloat32Array = st.commit_to_arrays()[Mesh.ARRAY_TANGENT]
		for k in ours.size() / 4:
			var o := Vector3(ours[k * 4], ours[k * 4 + 1], ours[k * 4 + 2])
			var g := Vector3(theirs[k * 4], theirs[k * 4 + 1], theirs[k * 4 + 2])
			agree = agree and o.dot(g) > 0.99 and signf(ours[k * 4 + 3]) == signf(theirs[k * 4 + 3])
	check.call("map geo: a box meshes to 12 triangles in 2 material surfaces, front faces out",
			surf.size() == 2 and tris == 12 and winding_ok, "%d surfaces, %d triangles" % [surf.size(), tris])
	check.call("map geo: face tangents lie in the face and match Godot's mikktspace tangents", tangent_ok and agree)

	# ---- patches
	var flatp := PackedFloat64Array()
	for j in 3:
		for i in 3:
			flatp.append_array([i * 32.0, j * 32.0, 0.0, i * 0.5, j * 0.5])
	var pf: Dictionary = Q3MapGeo.patch_mesh(flatp, 3, 3, 4.0, Color.WHITE)
	var bent := PackedFloat64Array()
	for j in 3:
		for i in 3:
			# a quarter pipe: the middle control row pulled up
			bent.append_array([i * 64.0, j * 64.0, (128.0 if j == 1 else 0.0), i * 0.5, j * 0.5])
	var pb: Dictionary = Q3MapGeo.patch_mesh(bent, 3, 3, 4.0, Color.WHITE)
	var pts: PackedFloat64Array = pb["points"]
	var corners_ok := _v(pts, 0) == Vector3(0, 0, 0) and _v(pts, pb["width"] * pb["height"] - 1) == Vector3(128, 128, 0)
	# the curve's peak is half the control height, and the chord error stays within tolerance
	var peak := 0.0
	for i in pts.size() / 3:
		peak = maxf(peak, pts[i * 3 + 2])
	check.call("map geo: patches: a flat 3x3 is one quad, a bend subdivides to tolerance, corners are exact",
			pf["width"] == 2 and pf["height"] == 2 and pb["height"] >= 5 and corners_ok and absf(peak - 64.0) < 1e-6,
			"bent grid %dx%d" % [pb["width"], pb["height"]])

	# ---- document: undo, files
	var d2 := Q3MapDoc.new()
	var ids := []
	for i in 5:
		ids.append(d2.add(Q3MapDoc.box_brush(Vector3(i * 64, 0, 0), Vector3(i * 64 + 32, 32, 32))))
	d2.checkpoint("move")
	var keep: Dictionary = d2.get_obj(ids[1])
	d2.put(Q3MapDoc.transform_brush(d2.get_obj(ids[0]), Q3MapDoc.translation(Vector3(0, 0, 16))))
	var moved_bounds := Q3MapGeo.brush_bounds(d2.get_obj(ids[0])["planes"])
	var touched := []
	d2.changed.connect(func(x): touched.assign(x))
	d2.undo()
	var back_bounds := Q3MapGeo.brush_bounds(d2.get_obj(ids[0])["planes"])
	d2.redo()
	check.call("map geo: undo/redo restore a move, report only the moved brush, and share untouched brushes",
			moved_bounds[2] == 16.0 and back_bounds[2] == 0.0 and touched == [ids[0]] and is_same(d2.get_obj(ids[1]), keep)
			and Q3MapGeo.brush_bounds(d2.get_obj(ids[0])["planes"])[2] == 16.0)
	var locked := d2.edit(ids[2])
	locked["faces"][4]["lock"] = true
	locked["faces"][3]["color"] = Color(0.5, 0.75, 1.0)
	d2.put(locked)
	d2.put(Q3MapDoc.transform_brush(d2.get_obj(ids[2]), Q3MapDoc.yaw_rotation(15, Vector3(1, 2, 0))))
	d2.add({"type": "entity", "id": 0, "group": 0, "classname": "info_player_deathmatch", "keys": {"origin": "16 16 24", "angle": "90"}})
	d2.add({"type": "patch", "id": 0, "entity": 0, "group": 0, "width": 3, "height": 3, "ctrl": bent, "material": "textures/base_wall/concrete4"})
	d2.worldspawn = {"message": "Selftest Arena", "music": "music/sonic1.wav"}
	var path := OS.get_user_data_dir().path_join("map_geo_test" + Q3MapDoc.EXTENSION)
	var e1 := d2.save(path)
	var e2 := d2.save(path)   # over an existing file
	var d3 := Q3MapDoc.new()
	var err := d3.load_file(path)
	var j2 := JSON.stringify(d2.to_data(), "", false, true)
	var j3 := JSON.stringify(d3.to_data(), "", false, true)
	var at := 0
	while at < mini(j2.length(), j3.length()) and j2[at] == j3[at]:
		at += 1
	DirAccess.remove_absolute(path)
	check.call("map geo: save and load round-trip exactly (planes, locked UVs, tints, entities, patches)",
			e1 == OK and e2 == OK and err == "" and j2 == j3 and not d3.dirty,
			"save %d/%d, load '%s'%s" % [e1, e2, err, "" if j2 == j3 else ", differs at: " + j2.substr(maxi(at - 60, 0), 120) + " | " + j3.substr(maxi(at - 60, 0), 120)])

	# ---- mesher: chunks rebuild only where something changed
	var mesher := Q3MapMesher.new()
	host.add_child(mesher)
	var d4 := Q3MapDoc.new()
	var far_ids := []
	for i in 4:
		far_ids.append(d4.add(Q3MapDoc.box_brush(Vector3(i * 1024, 0, 0), Vector3(i * 1024 + 64, 64, 64))))
	mesher.bind(d4)
	var cells := mesher.cell_count()
	var before := mesher.rebuilds
	d4.put(Q3MapDoc.transform_brush(d4.get_obj(far_ids[0]), Q3MapDoc.translation(Vector3(8, 0, 0))))
	mesher.flush()
	var one := mesher.rebuilds - before
	before = mesher.rebuilds
	d4.put(Q3MapDoc.transform_brush(d4.get_obj(far_ids[0]), Q3MapDoc.translation(Vector3(1024 * 2, 0, 0))))
	mesher.flush()
	var two := mesher.rebuilds - before
	check.call("map geo: mesher keeps one cell per 16 m and rebuilds only changed cells",
			cells == 4 and one == 1 and two == 2 and mesher.cell_count() == 3, "%d cells, rebuilds %d then %d" % [cells, one, two])

	# ---- speed: an arena-sized map
	var d5 := Q3MapDoc.new()
	var rng := RandomNumberGenerator.new()
	rng.seed = 7
	for i in 2000:
		var o := Vector3(rng.randi_range(-64, 64), rng.randi_range(-64, 64), rng.randi_range(0, 16)) * 32
		var bb := Q3MapDoc.box_brush(o, o + Vector3(rng.randi_range(1, 8), rng.randi_range(1, 8), rng.randi_range(1, 4)) * 16)
		if i % 3 == 0:
			bb = Q3MapDoc.transform_brush(bb, Q3MapDoc.yaw_rotation(rng.randi_range(1, 89), o))
		d5.add(bb)
	var t0 := Time.get_ticks_usec()
	var m2 := Q3MapMesher.new()
	host.add_child(m2)
	m2.bind(d5)
	var ms := (Time.get_ticks_usec() - t0) / 1000.0
	check.call("map geo: 2,000 brushes (a third rotated) mesh in under a second", ms < 1000.0,
			"%.0f ms, %d cells" % [ms, m2.cell_count()])
	mesher.queue_free()
	m2.queue_free()
	return out
