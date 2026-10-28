class_name Q3BspImport
extends RefCounted
## Quake 3 maps into the editor (MAP_EDITOR_PLAN.md E6): a compiled .bsp (any map in the player's
## paks, or a file) back into editable brushes, curves and entities, after OZMIUM's BspMapImporter.
##   brushes   the brush and brushside lumps: each side's plane and shader. q3map's bevel sides
##             (extra planes for collision that make no face) are dropped. A brush model's brushes
##             (doors, triggers ...) go back to its entity, moved by its origin.
##   mapping   a .bsp keeps no texture axes, only what q3map drew. Each side takes them from the
##             planar surface q3map drew for it (its texture, on its plane, overlapping it): texture
##             coordinates are an affine function of position on the plane, so a least-squares fit
##             recovers them exactly. As Quake 3's shift / rotate / scale when they fit that form,
##             else as explicit axes. Sides nothing drew (caulk, hidden) keep the default.
##   curves    patch surfaces: their control points are the curve, as written.
##   entities  every entity with its keys; "model" "*n" ties a brush entity to its brushes.
## q3map's func_group brushes are part of the world in a .bsp, and misc_models are baked into it
## as triangles (their entities stay, so they come back on the next compile).

const LUMPS := 17
const DETAIL := 0x8000000          ## CONTENTS_DETAIL
const PLANE_EPS := 0.64            ## a surface vertex this close to a side's plane lies on it (units)
const FIT_TOL := 0.01              ## worst fitted st error accepted (repeats)
const TEX_TOL := 0.05              ## worst error, in texels, for writing a mapping as shift/rotate/scale


## The .bsp files in the player's paks (later paks win, as Quake 3 loads them): name -> pak path.
static func maps_in_paks() -> Dictionary:
	var out := {}
	var paks := Q3MapCompiler.pak_dir()
	if paks == "" or not DirAccess.dir_exists_absolute(paks):
		return out
	var names := []
	for f in DirAccess.get_files_at(paks):
		if f.to_lower().ends_with(".pk3"):
			names.append(f)
	names.sort()
	for f in names:
		var z := ZIPReader.new()
		if z.open(paks.path_join(f)) != OK:
			continue
		for e in z.get_files():
			if e.to_lower().begins_with("maps/") and e.to_lower().ends_with(".bsp"):
				out[e.get_file().get_basename().to_lower()] = [paks.path_join(f), e]
		z.close()
	return out


## A map's .bsp bytes from the paks, or empty.
static func read_from_paks(map: String) -> PackedByteArray:
	var where: Array = maps_in_paks().get(map.to_lower(), [])
	if where.is_empty():
		return PackedByteArray()
	var z := ZIPReader.new()
	if z.open(where[0]) != OK:
		return PackedByteArray()
	var b := z.read_file(where[1])
	z.close()
	return b


## {doc, report} from a .bsp's bytes; doc is null when they aren't one. report: brushes, faces,
## fitted (faces whose mapping came from a drawn surface), tex (of those, written as shift/rotate/
## scale), undrawn (textured faces q3map drew nothing for: pressed against other brushes), patches, entities, surfaces (planar ones drawn),
## covered (of those, the ones some fitted face reproduces), worst (largest fit error, repeats),
## dropped (brushes that made no solid), detail (brushes imported as detail), errors.
static func import_bsp(b: PackedByteArray, name: String, audit := true) -> Dictionary:
	var report := {"brushes": 0, "detail": 0, "faces": 0, "fitted": 0, "tex": 0, "undrawn": 0, "patches": 0, "entities": 0,
			"surfaces": 0, "covered": 0, "worst": 0.0, "dropped": 0, "errors": [], "uncovered": []}
	if b.size() < 8 + LUMPS * 8 or b.slice(0, 4).get_string_from_ascii() != "IBSP":
		report["errors"].append("not an id Tech 3 .bsp")
		return {"doc": null, "report": report}
	if b.decode_s32(4) != 46:
		report["errors"].append("BSP version %d, not id Tech 3's 46" % b.decode_s32(4))
		return {"doc": null, "report": report}
	var lump := func(i: int) -> PackedByteArray:
		var off := b.decode_s32(8 + i * 8)
		var size := b.decode_s32(12 + i * 8)
		return b.slice(off, off + size)
	var shader_lump: PackedByteArray = lump.call(1)
	var shaders := []
	var contents := []
	for i in shader_lump.size() / 72:
		shaders.append(shader_lump.slice(i * 72, i * 72 + 64).get_string_from_ascii())
		contents.append(shader_lump.decode_s32(i * 72 + 68))
	var planes: PackedFloat32Array = (lump.call(2) as PackedByteArray).to_float32_array()
	var models: PackedInt32Array = (lump.call(7) as PackedByteArray).to_int32_array()   # 10 ints each
	var brushes: PackedInt32Array = (lump.call(8) as PackedByteArray).to_int32_array()  # firstSide, numSides, shader
	var sides: PackedInt32Array = (lump.call(9) as PackedByteArray).to_int32_array()    # plane, shader
	var verts: PackedByteArray = lump.call(10)
	var indexes: PackedInt32Array = (lump.call(11) as PackedByteArray).to_int32_array()
	var surfs: PackedByteArray = lump.call(13)
	var ents := parse_entities((lump.call(0) as PackedByteArray).get_string_from_utf8())
	# q3map keeps a brush's shader contents but not the .map's detail flag. Its tree is cut only on
	# structural brushes, so a structural (opaque) brush's inside is a solid leaf (cluster -1), while a
	# detail brush sits in an open leaf it never cut: the leaf at a world brush's middle tells them apart.
	var nodes: PackedInt32Array = (lump.call(3) as PackedByteArray).to_int32_array()   # plane, front, back, bounds: 9 ints
	var leaves: PackedInt32Array = (lump.call(4) as PackedByteArray).to_int32_array()  # cluster first: 12 ints
	var leaf_cluster := func(p: Vector3) -> int:
		var n := 0
		for guard in nodes.size() / 9 + 1:
			var po := nodes[n * 9] * 4
			var side := 1 if planes[po] * p.x + planes[po + 1] * p.y + planes[po + 2] * p.z - planes[po + 3] >= 0.0 else 2
			n = nodes[n * 9 + side]
			if n < 0:
				var leaf := -n - 1
				return leaves[leaf * 12] if leaf * 12 < leaves.size() else -1
			if n * 9 >= nodes.size():
				break
		return -1

	var vpos := PackedVector3Array()
	var vst := PackedVector2Array()
	var vnorm := PackedVector3Array()
	for i in verts.size() / 44:
		vpos.append(Vector3(verts.decode_float(i * 44), verts.decode_float(i * 44 + 4), verts.decode_float(i * 44 + 8)))
		vst.append(Vector2(verts.decode_float(i * 44 + 12), verts.decode_float(i * 44 + 16)))
		vnorm.append(Vector3(verts.decode_float(i * 44 + 28), verts.decode_float(i * 44 + 32), verts.decode_float(i * 44 + 36)))
	# the surfaces q3map drew: planar ones by shader (for fitting), patches by model
	var nsurf := surfs.size() / 104
	var surf_model := PackedInt32Array()
	surf_model.resize(nsurf)
	for m in models.size() / 10:
		var o := m * 10
		# mins and maxs are floats; firstSurface, numSurfaces are ints 6 and 7
		for s in range(models[o + 6], models[o + 6] + models[o + 7]):
			if s < nsurf:
				surf_model[s] = m
	var planar := {}   # model -> shader -> [surface record]
	var near := {}     # model -> shader -> NEAR_CELL cell -> [surface record]
	for s in nsurf:
		var so := s * 104
		if surfs.decode_s32(so + 8) != 1:   # MST_PLANAR
			continue
		var rec := {"first": surfs.decode_s32(so + 12), "count": surfs.decode_s32(so + 16),
				"first_index": surfs.decode_s32(so + 20), "indexes": surfs.decode_s32(so + 24)}
		var c := Vector3.ZERO
		var box := AABB(vpos[rec["first"]], Vector3.ZERO) if rec["count"] > 0 else AABB()
		for k in rec["count"]:
			c += vpos[rec["first"] + k]
			box = box.expand(vpos[rec["first"] + k])
		rec["centroid"] = c / maxf(rec["count"], 1)
		rec["box"] = box.grow(1.0)   # a quick reject before the plane and polygon tests
		# which way it faces: where two brushes meet on a plane, each draws a surface there, facing
		# opposite ways
		rec["normal"] = vnorm[rec["first"]] if rec["count"] > 0 else Vector3.ZERO
		# its triangles (absolute vertex indices), their middles and areas: newer q3maps merge coplanar
		# sides into one surface, so a side is matched and checked a triangle at a time
		var tris := PackedInt32Array()
		var tc := PackedVector3Array()
		var ta := PackedFloat32Array()
		for k in range(0, rec["indexes"] - 2, 3):
			var ii := [0, 0, 0]
			var good := true
			for j in 3:
				var idx: int = rec["first_index"] + k + j
				if idx >= indexes.size() or indexes[idx] >= rec["count"]:
					good = false
					break
				ii[j] = rec["first"] + indexes[idx]
			if not good:
				continue
			tris.append_array(ii)
			tc.append((vpos[ii[0]] + vpos[ii[1]] + vpos[ii[2]]) / 3.0)
			ta.append(0.5 * (vpos[ii[1]] - vpos[ii[0]]).cross(vpos[ii[2]] - vpos[ii[0]]).length())
		rec["tris"] = tris
		rec["tc"] = tc
		rec["ta"] = ta
		var m := surf_model[s]
		# by shader name: newer q3maps list a name more than once (one entry per set of flags), so a
		# side and the surface drawn for it can point at different entries
		var sh := material_of(shaders, surfs.decode_s32(so))
		if Q3MapMaterials.kind(sh) in ["fog", "sky"]:
			continue   # skies draw by direction and fogs by depth: their st aren't used
		if not planar.has(m):
			planar[m] = {}
		if not planar[m].has(sh):
			planar[m][sh] = []
		planar[m][sh].append(rec)
		report["surfaces"] += 1
		# and by where they are: a side looks only at the surfaces in the cells it touches
		if not near.has(m):
			near[m] = {}
		if not near[m].has(sh):
			near[m][sh] = {}
		for cell in _cells(rec["box"]):
			if not near[m][sh].has(cell):
				near[m][sh][cell] = []
			near[m][sh][cell].append(rec)

	var doc := Q3MapDoc.new()
	doc.info["name"] = name
	# entities: worldspawn's keys, then the rest; brush models go with theirs
	var model_entity := {0: 0}       # model -> entity id (0: the world)
	var model_origin := {0: Vector3.ZERO}
	for e in ents:
		var cn := String(e.get("classname", ""))
		if cn == "worldspawn":
			for k in e:
				if k != "classname":
					doc.worldspawn[k] = e[k]
			continue
		var keys: Dictionary = e.duplicate()
		keys.erase("classname")
		var model := -1
		if String(keys.get("model", "")).begins_with("*"):
			model = int(String(keys["model"]).substr(1))
			keys.erase("model")
		var obj := {"type": "entity", "id": 0, "group": 0, "classname": cn, "keys": keys}
		var eid := doc.add(obj)
		report["entities"] += 1
		if model > 0:
			model_entity[model] = eid
			model_origin[model] = Q3MapEntities.vec(String(keys.get("origin", "0 0 0")))
			# a brush entity selects as one: it and its brushes share a group
			var gid := doc.new_id()
			doc.groups[gid] = {"id": gid, "name": "%s %d" % [cn, eid], "parent": 0}
			var eo := doc.edit(eid)
			eo["group"] = gid
			doc.put(eo)
	# brushes
	var fitted := {}   # [model, shader] -> [{n, d, poly, uv}] in model space, for the coverage check
	for m in models.size() / 10:
		if not model_entity.has(m):
			continue
		var o := m * 10
		var eid: int = model_entity[m]
		var origin: Vector3 = model_origin.get(m, Vector3.ZERO)
		var group := int(doc.get_obj(eid).get("group", 0)) if eid != 0 else 0
		for bi in range(models[o + 8], models[o + 8] + models[o + 9]):
			var first := brushes[bi * 3]
			var count := brushes[bi * 3 + 1]
			var bcontents: int = contents[brushes[bi * 3 + 2]] if brushes[bi * 3 + 2] < contents.size() else 0
			var pl := PackedFloat64Array()
			var side_shader := []
			for si in range(first, first + count):
				var p := sides[si * 2] * 4
				var plane := Q3MapExport.snap_plane(PackedFloat64Array([planes[p], planes[p + 1], planes[p + 2], planes[p + 3]]))
				var n := Vector3(plane[0], plane[1], plane[2])
				var ln := n.length()
				plane = PackedFloat64Array([plane[0] / ln, plane[1] / ln, plane[2] / ln, plane[3] / ln + n.dot(origin) / ln])
				var sh := sides[si * 2 + 1]
				# newer q3maps add bevel sides on a real side's plane, with "noshader": one side a
				# plane, the textured one
				var dup := -1
				for j in side_shader.size():
					if plane[0] * pl[j * 4] + plane[1] * pl[j * 4 + 1] + plane[2] * pl[j * 4 + 2] > 1.0 - 1e-6 and absf(plane[3] - pl[j * 4 + 3]) < 0.1:
						dup = j
						break
				if dup >= 0:
					if material_of(shaders, side_shader[dup]) == "textures/common/caulk" and sh >= 0 and sh < shaders.size() and String(shaders[sh]) != "noshader":
						side_shader[dup] = sh
					continue
				pl.append_array(plane)
				side_shader.append(sh)
			# q3map2's bevels are its "noshader" sides, and some come out a hair off their side's plane
			# and shave a sliver off it; the brush as drawn is the one without them, when that closes
			var bare := PackedFloat64Array()
			var bare_shader := []
			for j in side_shader.size():
				var s: int = side_shader[j]
				if s < 0 or s >= shaders.size() or String(shaders[s]) != "noshader":
					bare.append_array(pl.slice(j * 4, j * 4 + 4))
					bare_shader.append(side_shader[j])
			if bare_shader.size() < side_shader.size() and bare_shader.size() >= 4 and Q3MapGeo.brush_valid(bare):
				pl = bare
				side_shader = bare_shader
			pl = Q3MapGeo.clean(pl)
			# keep the sides that make a face (q3map's bevels make none)
			var ws: Array = Q3MapGeo.brush_windings(pl)
			if m == 0 and nodes.size() >= 9 and not (bcontents & DETAIL):
				var mid := Vector3.ZERO
				var corners := 0
				for w in ws:
					for k in (w as PackedFloat64Array).size() / 3:
						mid += Vector3(w[k * 3], w[k * 3 + 1], w[k * 3 + 2])
						corners += 1
				if corners > 0 and leaf_cluster.call(mid / corners) != -1:
					bcontents |= DETAIL
			var kept := PackedFloat64Array()
			var faces := []
			var kept_ws := []
			for i in side_shader.size():
				if i < ws.size() and (ws[i] as PackedFloat64Array).size() >= 9:
					kept.append_array(pl.slice(i * 4, i * 4 + 4))
					var sh: int = side_shader[i]
					var f := {"material": material_of(shaders, sh),
							"tex": Q3MapDoc.DEFAULT_TEX.duplicate(), "_shader": material_of(shaders, sh)}
					if bcontents & DETAIL:
						f["flags"] = [DETAIL, 0, 0]
					faces.append(f)
					kept_ws.append(ws[i])
			if faces.size() < 4 or not Q3MapGeo.brush_valid(kept):
				report["dropped"] += 1
				continue
			var brush := {"type": "brush", "id": 0, "entity": eid, "group": group, "planes": kept, "faces": faces}
			for i in faces.size():
				_fit_face(brush, i, kept_ws[i], near.get(m, {}), vpos, vst, origin, report, fitted, m)
				faces[i].erase("_shader")
			doc.add(brush)
			report["brushes"] += 1
			report["faces"] += faces.size()
			if bcontents & DETAIL:
				report["detail"] += 1
	# curves
	for s in nsurf:
		var so := s * 104
		if surfs.decode_s32(so + 8) != 2:   # MST_PATCH
			continue
		var m := surf_model[s]
		if not model_entity.has(m):
			continue
		var w := surfs.decode_s32(so + 96)
		var h := surfs.decode_s32(so + 100)
		var first := surfs.decode_s32(so + 12)
		var origin: Vector3 = model_origin.get(m, Vector3.ZERO)
		var ctrl := PackedFloat64Array()
		for k in w * h:
			var p := vpos[first + k] + origin
			ctrl.append_array([p.x, p.y, p.z, vst[first + k].x, vst[first + k].y])
		var sh := surfs.decode_s32(so)
		var eid: int = model_entity[m]
		doc.add({"type": "patch", "id": 0, "entity": eid, "group": int(doc.get_obj(eid).get("group", 0)) if eid != 0 else 0,
				"width": w, "height": h, "ctrl": ctrl, "material": material_of(shaders, sh)})
		report["patches"] += 1
	# the audit: every planar surface q3map drew is reproduced by some imported face (the editor
	# skips it; covered stays 0)
	for m in (planar if audit else {}):
		for sh in planar[m]:
			for rec in planar[m][sh]:
				var missed := _uncovered_triangles(rec, fitted.get("%d|%s" % [m, sh], []), vpos, vst, Q3MapMaterials.size(sh))
				if missed == 0:
					report["covered"] += 1
				elif report["uncovered"].size() < 40:
					report["uncovered"].append({"shader": sh, "model": m,
							"at": rec["centroid"], "verts": rec["count"], "triangles": (rec["tc"] as PackedVector3Array).size(), "missed": missed})
	doc.dirty = true
	return {"doc": doc, "report": report}


## A side's shader as an editor material: q3map's "noshader" (a side with no texture) is caulk.
static func material_of(shaders: Array, sh: int) -> String:
	if sh < 0 or sh >= shaders.size():
		return Q3MapMaterials.DEFAULT
	var s := String(shaders[sh])
	return "textures/common/caulk" if s == "noshader" or s == "" else s


const NEAR_CELL := 256.0   ## units: the grid the drawn surfaces are filed in for _fit_face


## The NEAR_CELL cells a box touches.
static func _cells(box: AABB) -> Array:
	var lo := (box.position / NEAR_CELL).floor()
	var hi := (box.end / NEAR_CELL).floor()
	var out := []
	for x in range(int(lo.x), int(hi.x) + 1):
		for y in range(int(lo.y), int(hi.y) + 1):
			for z in range(int(lo.z), int(hi.z) + 1):
				out.append(Vector3i(x, y, z))
	return out


## Gives face i the mapping of the surface q3map drew for it, if one matches. by_shader: the model's
## drawn surfaces, shader -> cell -> [record].
static func _fit_face(b: Dictionary, i: int, w: PackedFloat64Array, by_shader: Dictionary, vpos: PackedVector3Array,
		vst: PackedVector2Array, origin: Vector3, report: Dictionary, fitted: Dictionary, model: int) -> void:
	var f: Dictionary = b["faces"][i]
	if not by_shader.has(f["_shader"]):
		if Q3MapMaterials.kind(f["material"]) not in ["hidden", "fog", "sky"] and not f["material"].begins_with("textures/common/"):
			report["undrawn"] += 1
		return
	var pl := Q3MapDoc.plane_of(b, i)
	var n := Vector3(pl[0], pl[1], pl[2])
	var poly := PackedVector3Array()
	for k in w.size() / 3:
		poly.append(Vector3(w[k * 3], w[k * 3 + 1], w[k * 3 + 2]) - origin)   # the surfaces are in model space
	var d := pl[3] - n.dot(origin)
	var pc := Vector3.ZERO
	for p in poly:
		pc += p
	pc /= poly.size()
	var size := Q3MapMaterials.size(f["material"])
	var pbox := AABB(poly[0], Vector3.ZERO)
	for p in poly:
		pbox = pbox.expand(p)
	var grid: Dictionary = by_shader[f["_shader"]]
	var seen := {}
	var cands := []
	for cell in _cells(pbox.grow(1.0)):
		for rec in grid.get(cell, []):
			if not seen.has(rec["first"]):
				seen[rec["first"]] = true
				cands.append(rec)
	# the triangles q3map drew over the side: of each surface on its plane, those whose middle lies in
	# the side's polygon (where another brush's coplanar face overlaps this one, its surface lies here
	# too)
	var order := []
	for rec in cands:
		if not (rec["box"] as AABB).intersects(pbox) or not _on_plane(rec, vpos, n, d):
			continue
		var tris: PackedInt32Array = rec["tris"]
		var tc: PackedVector3Array = rec["tc"]
		var ta: PackedFloat32Array = rec["ta"]
		var idx := PackedInt32Array()
		var area := 0.0
		var total := 0.0
		for t in tc.size():
			total += ta[t]
			if _in_polygon(poly, n, tc[t]):
				idx.append_array([tris[t * 3], tris[t * 3 + 1], tris[t * 3 + 2]])
				area += ta[t]
		if idx.size():
			# a surface lying wholly inside the side before one spilling out of it (a bigger coplanar
			# neighbor's), then one no other side took (two brushes' coincident sides each draw one,
			# in brush order), then the bigger
			order.append([snappedf(area / maxf(total, 1e-6), 0.01), area, idx, rec])
	if order.is_empty():
		# a side smaller than the triangles drawn over it: the triangles holding its middle
		for rec in cands:
			if not _on_plane(rec, vpos, n, d):
				continue
			var tris: PackedInt32Array = rec["tris"]
			var idx := PackedInt32Array()
			for t in tris.size() / 3:
				var tri := PackedVector3Array([vpos[tris[t * 3]], vpos[tris[t * 3 + 1]], vpos[tris[t * 3 + 2]]])
				if _in_polygon(tri, n, pc):
					idx.append_array([tris[t * 3], tris[t * 3 + 1], tris[t * 3 + 2]])
			if idx.size():
				order.append([0.0, 0.0, idx, rec])
	var before := func(x: Array, y: Array) -> bool:
		if x[0] != y[0]:
			return x[0] > y[0]
		var xt: bool = x[3].has("taken")
		if xt != y[3].has("taken"):
			return not xt
		if x[1] != y[1]:
			return x[1] > y[1]
		return int(x[3]["first"]) < int(y[3]["first"])
	order.sort_custom(before)
	for cand in order:
		var fit := _fit(cand[2], vpos, vst, n, d, size)
		if fit.is_empty():
			continue
		cand[3]["taken"] = true
		var key := "%d|%s" % [model, f["_shader"]]
		if not fitted.has(key):
			fitted[key] = []
		fitted[key].append({"n": n, "d": d, "poly": poly, "center": pc, "box": pbox.grow(1.0), "uv": (fit[0] as PackedFloat64Array).duplicate()})
		report["fitted"] += 1
		report["worst"] = maxf(report["worst"], fit[1])
		var uv: PackedFloat64Array = fit[0]
		if origin != Vector3.ZERO:
			# the fit is in model space: u = U . (p - origin) + Uoff in the world
			uv[3] -= uv[0] * origin.x + uv[1] * origin.y + uv[2] * origin.z
			uv[7] -= uv[4] * origin.x + uv[5] * origin.y + uv[6] * origin.z
		var tex := uv_to_tex(uv, pl, w, size)
		if tex.is_empty():
			f.erase("tex")
			f["uv"] = uv
		else:
			f["tex"] = tex
			report["tex"] += 1
		return
	if Q3MapMaterials.kind(f["material"]) not in ["hidden", "fog"]:
		report["undrawn"] += 1


static func _on_plane(rec: Dictionary, vpos: PackedVector3Array, n: Vector3, d: float) -> bool:
	if (rec["normal"] as Vector3).dot(n) < 0.9:
		return false
	for k in rec["count"]:
		if absf(n.dot(vpos[rec["first"] + k]) - d) > PLANE_EPS:
			return false
	return true


## How many of a surface's triangles no fitted face (same model and shader) explains: on the face's
## plane, its middle in the face's polygon, and the face's mapping giving its three st apart from
## whole repeats (q3map moves each surface's st by whole repeats).
static func _uncovered_triangles(rec: Dictionary, faces: Array, vpos: PackedVector3Array, vst: PackedVector2Array, size: Vector2) -> int:
	var tris: PackedInt32Array = rec["tris"]
	var tc: PackedVector3Array = rec["tc"]
	var missed := 0
	var near := []
	var box: AABB = rec["box"]
	for fc in faces:
		if (fc["box"] as AABB).intersects(box) and _on_plane(rec, vpos, fc["n"], float(fc["d"])):
			near.append(fc)
	var ta: PackedFloat32Array = rec["ta"]
	for t in tc.size():
		if ta[t] < 0.01:
			continue   # a triangle with no area (q3map leaves a few, along an edge) draws nothing
		var ok := false
		for fc in near:
			# q3map's T-junction fixing leaves slivers whose corners sit up to a quarter unit off the side
			if not _in_polygon(fc["poly"], fc["n"], tc[t], 0.5):
				continue
			var uv: PackedFloat64Array = fc["uv"]
			var whole := Vector2.INF
			ok = true
			for j in 3:
				var i := tris[t * 3 + j]
				var p := vpos[i]
				var diff := Vector2((uv[0] * p.x + uv[1] * p.y + uv[2] * p.z + uv[3]) / size.x,
						(uv[4] * p.x + uv[5] * p.y + uv[6] * p.z + uv[7]) / size.y) - vst[i]
				if whole == Vector2.INF:
					whole = diff.round()
				if (diff - whole).length() > FIT_TOL * 2.0:
					ok = false
					break
			if ok:
				break
		if not ok:
			missed += 1
	return missed


## [uv (texels: U, Uoff, V, Voff), worst error in repeats] fitting a surface's st by least squares on
## the plane (n, d), or [] when it isn't an affine map of position.
static func _fit(idx: PackedInt32Array, vpos: PackedVector3Array, vst: PackedVector2Array, n: Vector3, d: float, size: Vector2) -> Array:
	var e1 := n.cross(Vector3(0, 0, 1) if absf(n.z) < 0.9 else Vector3(1, 0, 0)).normalized()
	var e2 := n.cross(e1).normalized()
	# s = A x + B y + C (and t alike), x, y on the plane, fitted about the surface's middle: the map's
	# coordinates run to thousands, which would swamp a small surface's spread
	var count := idx.size()
	var xs := PackedFloat64Array()
	var ys := PackedFloat64Array()
	var mx := 0.0
	var my := 0.0
	var ms := 0.0
	var mt := 0.0
	for k in count:
		var p := vpos[idx[k]]
		var st := vst[idx[k]]
		xs.append(float(p.x) * e1.x + float(p.y) * e1.y + float(p.z) * e1.z)
		ys.append(float(p.x) * e2.x + float(p.y) * e2.y + float(p.z) * e2.z)
		mx += xs[k]
		my += ys[k]
		ms += st.x
		mt += st.y
	mx /= count
	my /= count
	ms /= count
	mt /= count
	var sxx := 0.0
	var sxy := 0.0
	var syy := 0.0
	var bxs := 0.0
	var bys := 0.0
	var bxt := 0.0
	var byt := 0.0
	for k in count:
		var x := xs[k] - mx
		var y := ys[k] - my
		var st := vst[idx[k]]
		sxx += x * x
		sxy += x * y
		syy += y * y
		bxs += x * (st.x - ms)
		bys += y * (st.x - ms)
		bxt += x * (st.y - mt)
		byt += y * (st.y - mt)
	var det := sxx * syy - sxy * sxy
	if det <= 1e-9 * sxx * syy or sxx < 1e-6 or syy < 1e-6:
		return []   # the points lie on a line
	var a := [(bxs * syy - bys * sxy) / det, (bys * sxx - bxs * sxy) / det]
	var c := [(bxt * syy - byt * sxy) / det, (byt * sxx - bxt * sxy) / det]
	a.append(ms - a[0] * mx - a[1] * my)
	c.append(mt - c[0] * mx - c[1] * my)
	var worst := 0.0
	for k in count:
		var st := vst[idx[k]]
		worst = maxf(worst, maxf(absf(a[0] * xs[k] + a[1] * ys[k] + a[2] - st.x), absf(c[0] * xs[k] + c[1] * ys[k] + c[2] - st.y)))
	if worst > FIT_TOL:
		return []
	# texels: U = W (A e1 + B e2), Uoff = W C; points on the plane are x e1 + y e2 + d n
	var u: Vector3 = (e1 * a[0] + e2 * a[1]) * size.x
	var v: Vector3 = (e1 * c[0] + e2 * c[1]) * size.y
	return [PackedFloat64Array([u.x, u.y, u.z, a[2] * size.x, v.x, v.y, v.z, c[2] * size.y]), worst]


## Explicit texel axes as Quake 3's world projection [shift s, shift t, rotate, scale s, scale t] when
## they have that form on this plane (the default axes turned and scaled, no shear), checked on the
## face's corners to tol texels; else []. Round values (0.001 for shift and rotation, 0.00001 for scale)
## are tried before exact ones, and shifts are kept within one texture repeat.
static func uv_to_tex(uv: PackedFloat64Array, pl: PackedFloat64Array, w: PackedFloat64Array, size: Vector2, tol := TEX_TOL) -> Array:
	var n := Vector3(pl[0], pl[1], pl[2])
	var ax: PackedFloat64Array = Q3MapGeo.texture_axes(pl)
	var b0 := Vector3(ax[0], ax[1], ax[2])
	var b1 := Vector3(ax[3], ax[4], ax[5])
	# the axis the projection drops: the one neither default axis uses
	var drop := 0
	for k in 3:
		if b0[k] == 0.0 and b1[k] == 0.0:
			drop = k
	if absf(n[drop]) < 1e-6:
		return []
	# the same values on the plane, with nothing along the dropped axis: U' = U - k n gives
	# U' . p = U . p - k d there, so the offset gains k d
	var u := Vector3(uv[0], uv[1], uv[2])
	var v := Vector3(uv[4], uv[5], uv[6])
	var ku := u[drop] / n[drop]
	var kv := v[drop] / n[drop]
	u -= n * ku
	v -= n * kv
	var shift_s := uv[3] + ku * pl[3]
	var shift_t := uv[7] + kv * pl[3]
	var sv := 0 if b0.x != 0.0 else (1 if b0.y != 0.0 else 2)
	var tv := 0 if b1.x != 0.0 else (1 if b1.y != 0.0 else 2)
	# QuakeTextureVecs turns the (sv, tv) components of both default axes by the angle, then divides
	# each by its scale
	var b0v := Vector2(b0[sv], b0[tv])
	var b1v := Vector2(b1[sv], b1[tv])
	var uu := Vector2(u[sv], u[tv])
	var vv := Vector2(v[sv], v[tv])
	if uu.length() < 1e-9 or vv.length() < 1e-9:
		return []
	for flip in [1.0, -1.0]:
		var sx: float = flip / uu.length()
		var rot: float = (uu * sx).angle() - b0v.angle()
		var rb1 := b1v.rotated(rot)
		var sy := 1.0 / vv.dot(rb1) if absf(vv.dot(rb1)) > 1e-9 else 0.0
		if sy == 0.0:
			continue
		var deg := fposmod(rad_to_deg(rot), 360.0)
		for snap in [true, false]:
			var tex := [shift_s, shift_t, deg, sx, sy]
			if snap:
				tex = [snappedf(shift_s, 0.001), snappedf(shift_t, 0.001), snappedf(deg, 0.001), snappedf(sx, 0.00001), snappedf(sy, 0.00001)]
			# whole repeats of shift draw the same
			tex[0] = fposmod(tex[0], size.x)
			tex[1] = fposmod(tex[1], size.y)
			if snap:
				tex[0] = snappedf(tex[0], 0.001)
				tex[1] = snappedf(tex[1], 0.001)
				tex[2] = fposmod(tex[2], 360.0)   # 359.9996 rounds to 360: that's 0
			if _tex_matches(tex, uv, pl, w, size, tol):
				return tex
	return []


## Whether a shift/rotate/scale mapping gives the face's corners the same texels as uv, apart from
## whole repeats.
static func _tex_matches(tex: Array, uv: PackedFloat64Array, pl: PackedFloat64Array, w: PackedFloat64Array, size: Vector2, tol: float) -> bool:
	var back := Q3MapDoc.face_uv({"material": "", "tex": tex}, pl)
	var first := Vector2.INF
	for k in w.size() / 3:
		var p := Vector3(w[k * 3], w[k * 3 + 1], w[k * 3 + 2])
		var d := Vector2((back[0] * p.x + back[1] * p.y + back[2] * p.z + back[3]) - (uv[0] * p.x + uv[1] * p.y + uv[2] * p.z + uv[3]),
				(back[4] * p.x + back[5] * p.y + back[6] * p.z + back[7]) - (uv[4] * p.x + uv[5] * p.y + uv[6] * p.z + uv[7]))
		if first == Vector2.INF:
			first = d
			var whole := Vector2(roundf(d.x / size.x) * size.x, roundf(d.y / size.y) * size.y)
			if absf(d.x - whole.x) > tol or absf(d.y - whole.y) > tol:
				return false
		if absf(d.x - first.x) > tol or absf(d.y - first.y) > tol:
			return false
	return true


## Whether p lies in the convex polygon on its plane (either winding), with a little slack.
static func _in_polygon(poly: PackedVector3Array, n: Vector3, p: Vector3, slack := 0.03) -> bool:
	if poly.size() < 3:
		return false
	var c := Vector3.ZERO
	for v in poly:
		c += v
	c /= poly.size()
	for k in poly.size():
		var a := poly[k]
		var e := (poly[(k + 1) % poly.size()] - a).cross(n)
		var sc := e.dot(c - a)
		var sp := e.dot(p - a)
		if sc * sp < 0.0 and absf(sp) > slack * e.length():
			return false
	return true


## The entity lump: [{key: value}] in file order.
static func parse_entities(text: String) -> Array:
	var out := []
	var cur: Variant = null
	var i := 0
	var key := ""
	var have_key := false
	while i < text.length():
		var ch := text[i]
		if ch == "{":
			cur = {}
			have_key = false
		elif ch == "}":
			if cur != null:
				out.append(cur)
			cur = null
		elif ch == "\"":
			var j := text.find("\"", i + 1)
			if j < 0:
				break
			var s := text.substr(i + 1, j - i - 1)
			if cur != null:
				if not have_key:
					key = s
					have_key = true
				else:
					cur[key] = s
					have_key = false
			i = j
		i += 1
	return out
