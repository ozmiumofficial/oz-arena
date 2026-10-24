extends Node
## Radiant .map import and export (MAP_EDITOR_PLAN.md E6), headless: a map with every kind of face
## mapping, flags, a curve, brush entities (one with an origin key, one with an origin brush) and a
## func_group goes out as brushDef (GtkRadiant, NetRadiant) and as Valve 220 (TrenchBroom) and comes
## back with every plane and every corner's texel coordinates as they were; hand-written files in
## each form q3map and those editors write read as those editors show them.
##   godot --headless --path . res://tests/map_format_test.tscn
## Prints one line per check and "MAP FORMAT n/m passed"; exit code 1 on any failure.

const TEXEL_TOL := 1e-4
const DETAIL := 0x8000000

var passed := 0
var failed := 0


func check(name: String, ok: bool, detail := "") -> void:
	if ok:
		passed += 1
	else:
		failed += 1
	print("  %s  map format: %s%s" % ["PASS" if ok else "FAIL", name, ("  (" + detail + ")") if detail != "" else ""])


func _ready() -> void:
	_run.call_deferred()


## Every kind of face the editor makes: default and turned/scaled projections, texture-locked
## explicit axes on a turned brush, detail flags, a curve, a door, movers with an origin (q3map maps
## their faces from it) and a func_group.
static func sample_doc() -> Q3MapDoc:
	var d := Q3MapDoc.new()
	d.info["name"] = "format_test"
	d.worldspawn = {"message": "Format test", "_color": "1 0.9 0.8"}
	d.add(Q3MapDoc.box_brush(Vector3(-256, -256, -16), Vector3(256, 256, 0), "textures/base_floor/clangdark"))
	var b := Q3MapDoc.box_brush(Vector3(0, 0, 0), Vector3(64, 64, 128), "textures/base_wall/concrete")
	for f in b["faces"]:
		f["tex"] = [16.0, 8.0, 45.0, 0.5, 0.25]
	d.add(b)
	var locked := Q3MapDoc.box_brush(Vector3(-128, -32, 0), Vector3(-64, 32, 96), "textures/gothic_block/blocks18c")
	for f in locked["faces"]:
		f["lock"] = true
		f["tex"] = [5.0, 3.0, 0.0, 0.5, 0.5]
	d.add(Q3MapDoc.transform_brush(locked, Q3MapDoc.yaw_rotation(15.0, Vector3(-96, 0, 0))))
	var detail := Q3MapDoc.box_brush(Vector3(96, 96, 0), Vector3(128, 128, 32), "textures/base_trim/pewter")
	for f in detail["faces"]:
		f["flags"] = [DETAIL, 0, 0]
	d.add(detail)
	var ctrl := PackedFloat64Array()
	for row in 3:
		for col in 3:
			ctrl.append_array([200.0 + col * 64.0, row * 64.0, 32.0 * (1 - absi(col - 1)), col * 0.5, row * 0.5])
	d.add({"type": "patch", "id": 0, "entity": 0, "group": 0, "width": 3, "height": 3, "ctrl": ctrl, "material": "textures/base_wall/concrete"})
	var grp := d.add({"type": "entity", "id": 0, "group": 0, "classname": "func_group", "keys": {}})
	var gb := Q3MapDoc.box_brush(Vector3(-200, 100, 0), Vector3(-160, 140, 40), "textures/base_wall/concrete")
	gb["entity"] = grp
	d.add(gb)
	var door := d.add({"type": "entity", "id": 0, "group": 0, "classname": "func_door", "keys": {"angle": "90", "speed": "200"}})
	var db := Q3MapDoc.box_brush(Vector3(-32, 200, 0), Vector3(32, 216, 128), "textures/base_door/shinymetaldoor")
	db["entity"] = door
	for f in db["faces"]:
		f["tex"] = [0.0, 0.0, 90.0, 1.0, 1.0]
	d.add(db)
	var bob := d.add({"type": "entity", "id": 0, "group": 0, "classname": "func_bobbing", "keys": {"origin": "100 -150 40"}})
	var bb := Q3MapDoc.box_brush(Vector3(80, -170, 0), Vector3(120, -130, 80), "textures/base_wall/concrete")
	bb["entity"] = bob
	for f in bb["faces"]:
		f["tex"] = [7.0, 3.0, 30.0, 0.5, 0.5]
	d.add(bb)
	var rot := d.add({"type": "entity", "id": 0, "group": 0, "classname": "func_rotating", "keys": {"speed": "90"}})
	var rb := Q3MapDoc.box_brush(Vector3(-100, -230, 0), Vector3(-36, -170, 64), "textures/base_wall/concrete")
	rb["entity"] = rot
	d.add(rb)
	var ob := Q3MapDoc.box_brush(Vector3(-77, -209, 23), Vector3(-59, -191, 41), "textures/common/origin")
	ob["entity"] = rot
	d.add(ob)
	return d


## How far apart two brushes' mappings are, in texels, at every corner of every face (planes matched
## by direction and distance); INF when a face has no match.
static func mapping_error(da: Q3MapDoc, ia: int, db: Q3MapDoc, ib: int) -> float:
	var a := da.get_obj(ia)
	var b := db.get_obj(ib)
	var wa := da.windings(ia)
	var worst := 0.0
	for i in a["faces"].size():
		var pa := Q3MapDoc.plane_of(a, i)
		var j := -1
		for k in b["faces"].size():
			var pb := Q3MapDoc.plane_of(b, k)
			if pa[0] * pb[0] + pa[1] * pb[1] + pa[2] * pb[2] > 1.0 - 1e-9 and absf(pa[3] - pb[3]) < 1e-6:
				j = k
				break
		if j < 0:
			return INF
		if a["faces"][i]["material"] != b["faces"][j]["material"]:
			return INF
		var ua := Q3MapDoc.face_uv(a["faces"][i], pa)
		var ub := Q3MapDoc.face_uv(b["faces"][j], Q3MapDoc.plane_of(b, j))
		var w: PackedFloat64Array = wa[i]
		var size := Q3MapMaterials.size(a["faces"][i]["material"])
		for o in [0, 4]:
			# a shift may come back moved by whole repeats (it's kept under one texture's size), which
			# draws the same: the whole face moves by the same whole number of repeats
			var span: float = size.x if o == 0 else size.y
			var first := INF
			for k in w.size() / 3:
				var p := [w[k * 3], w[k * 3 + 1], w[k * 3 + 2]]
				var ta: float = ua[o] * p[0] + ua[o + 1] * p[1] + ua[o + 2] * p[2] + ua[o + 3]
				var tb: float = ub[o] * p[0] + ub[o + 1] * p[1] + ub[o + 2] * p[2] + ub[o + 3]
				if first == INF:
					first = ta - tb
					worst = maxf(worst, absf(first - roundf(first / span) * span))
				worst = maxf(worst, absf(ta - tb - first))
	return worst


func _round_trip(format: String) -> void:
	var d := sample_doc()
	var ex := Q3MapExport.to_map(d, format)
	var text: String = ex["text"]
	var r := Q3MapImport.parse(text, "format_test")
	var back: Q3MapDoc = r["doc"]
	if back == null:
		check("%s: the export reads back" % format, false, str(r["report"]["errors"]))
		return
	var ba := d.ids_of("brush")
	var bb := back.ids_of("brush")
	ba.sort()
	bb.sort()
	var worst := 0.0
	if ba.size() == bb.size():
		for i in ba.size():
			worst = maxf(worst, mapping_error(d, ba[i], back, bb[i]))
	else:
		worst = INF
	check("%s: every plane and every corner's texel coordinates come back" % format, worst < TEXEL_TOL,
			"%d -> %d brushes, worst %s texels" % [ba.size(), bb.size(), String.num_scientific(worst)])
	var det := false
	for id in bb:
		for f in back.get_obj(id)["faces"]:
			if f.has("flags") and int(f["flags"][0]) & DETAIL:
				det = true
	var pa: Dictionary = back.get_obj(back.ids_of("patch")[0]) if back.ids_of("patch").size() else {}
	var po: Dictionary = d.get_obj(d.ids_of("patch")[0])
	var same_patch: bool = pa.size() > 0 and pa["width"] == po["width"] and pa["height"] == po["height"] and pa["ctrl"] == po["ctrl"]
	var ents := back.ids_of("entity")
	var door := {}
	var door_brush := false
	for e in ents:
		if back.get_obj(e)["classname"] == "func_door":
			door = back.get_obj(e)
			for id in bb:
				if int(back.get_obj(id).get("entity", 0)) == e:
					door_brush = true
	check("%s: detail flags, the curve, the door with its keys and brush, func_group into the world" % format,
			det and same_patch and door.get("classname", "") == "func_door" and door.get("keys", {}) == {"angle": "90", "speed": "200"} and door_brush,
			"detail %s, curve %s, entities %d" % [det, same_patch, ents.size()])
	check("%s: worldspawn keys kept, %s" % [format, "mapversion 220 written and read" if format == "valve220" else "no mapversion"],
			back.worldspawn == d.worldspawn and (text.contains("\"mapversion\" \"220\"") == (format == "valve220"))
			and text.contains("brushDef") == (format != "valve220"), str(back.worldspawn))


const STANDARD := """// Quake 3 form, as GtkRadiant 1.3 and q3map's own test maps
{
"classname" "worldspawn"
"message" "standard"
// brush 0
{
( 64 64 64 ) ( 64 0 64 ) ( 0 64 64 ) base_wall/concrete 16 8 45 0.5 0.25 0 0 0
( 0 0 0 ) ( 64 0 0 ) ( 0 64 0 ) base_wall/concrete 0 0 0 0.5 0.5 0 0 0
( 0 0 0 ) ( 0 0 64 ) ( 64 0 0 ) base_wall/concrete 0 0 0 0.5 0.5 134217728 0 0
( 0 64 0 ) ( 64 64 0 ) ( 0 64 64 ) base_wall/concrete 0 0 0 0.5 0.5 0 0 0
( 0 0 0 ) ( 0 64 0 ) ( 0 0 64 ) base_wall/concrete 0 0 0 0.5 0.5 0 0 0
( 64 0 0 ) ( 64 0 64 ) ( 64 64 0 ) base_wall/concrete 0 0 0 0.5 0.5 0 0 0
}
}
{
"classname" "func_group"
{
( 64 64 64 ) ( 64 0 64 ) ( 0 64 64 ) base_wall/concrete 0 0 0 0.5 0.5
( 0 0 -64 ) ( 64 0 -64 ) ( 0 64 -64 ) base_wall/concrete 0 0 0 0.5 0.5
( 0 0 0 ) ( 0 0 64 ) ( 64 0 0 ) base_wall/concrete 0 0 0 0.5 0.5
( 0 64 0 ) ( 64 64 0 ) ( 0 64 64 ) base_wall/concrete 0 0 0 0.5 0.5
( 0 0 0 ) ( 0 64 0 ) ( 0 0 64 ) base_wall/concrete 0 0 0 0.5 0.5
( 64 0 0 ) ( 64 0 64 ) ( 64 64 0 ) base_wall/concrete 0 0 0 0.5 0.5
}
}
{
"classname" "info_player_deathmatch"
"origin" "32 32 100"
"angle" "90"
}
"""

const VALVE := """// TrenchBroom, Quake 3 (Valve)
{
"classname" "worldspawn"
"mapversion" "220"
{
( 0 0 64 ) ( 0 1 64 ) ( 1 0 64 ) base_wall/concrete [ 1 0 0 16 ] [ 0 -1 0 8 ] 0 0.5 0.5 0 0 0
( 0 0 0 ) ( 1 0 0 ) ( 0 1 0 ) base_wall/concrete [ 1 0 0 0 ] [ 0 -1 0 0 ] 0 1 1 0 0 0
( 0 0 0 ) ( 0 0 1 ) ( 1 0 0 ) base_wall/concrete [ 1 0 0 0 ] [ 0 0 -1 0 ] 0 1 1 0 0 0
( 0 64 0 ) ( 1 64 0 ) ( 0 64 1 ) base_wall/concrete [ 1 0 0 0 ] [ 0 0 -1 0 ] 0 1 1 0 0 0
( 0 0 0 ) ( 0 1 0 ) ( 0 0 1 ) base_wall/concrete [ 0 1 0 0 ] [ 0 0 -1 0 ] 0 1 1 0 0 0
( 64 0 0 ) ( 64 0 1 ) ( 64 1 0 ) base_wall/concrete [ 0.6 0.8 0 4 ] [ 0 0 -1 2 ] 0 0.25 2 0 0 0
}
}
"""

const BRUSHDEF := """// GtkRadiant 1.4 / NetRadiant brush primitives, with curves
{
"classname" "worldspawn"
{
brushDef
{
( 64 64 64 ) ( 64 0 64 ) ( 0 64 64 ) ( ( 0.0078125 0 0.25 ) ( 0 0.0078125 0.5 ) ) base_wall/concrete 0 0 0
( 0 0 0 ) ( 64 0 0 ) ( 0 64 0 ) ( ( 0.0078125 0 0 ) ( 0 0.0078125 0 ) ) base_wall/concrete 0 0 0
( 0 0 0 ) ( 0 0 64 ) ( 64 0 0 ) ( ( 0.0078125 0 0 ) ( 0 0.0078125 0 ) ) base_wall/concrete 0 0 0
( 0 64 0 ) ( 64 64 0 ) ( 0 64 64 ) ( ( 0.0078125 0 0 ) ( 0 0.0078125 0 ) ) base_wall/concrete 0 0 0
( 0 0 0 ) ( 0 64 0 ) ( 0 0 64 ) ( ( 0.0078125 0 0 ) ( 0 0.0078125 0 ) ) base_wall/concrete 0 0 0
( 64 0 0 ) ( 64 0 64 ) ( 64 64 0 ) ( ( 0.0078125 0 0 ) ( 0 0.0078125 0 ) ) base_wall/concrete 0 0 0
}
}
{
patchDef2
{
base_wall/concrete
( 3 3 0 0 0 )
(
( ( 0 0 0 0 0 ) ( 0 64 0 0 1 ) ( 0 128 0 0 2 ) )
( ( 64 0 32 1 0 ) ( 64 64 32 1 1 ) ( 64 128 32 1 2 ) )
( ( 128 0 0 2 0 ) ( 128 64 0 2 1 ) ( 128 128 0 2 2 ) )
)
}
}
{
patchDef3
{
base_wall/concrete
( 3 3 2 2 0 0 0 )
(
( ( 0 0 100 0 0 ) ( 0 64 100 0 1 ) ( 0 128 100 0 2 ) )
( ( 64 0 100 1 0 ) ( 64 64 100 1 1 ) ( 64 128 100 1 2 ) )
( ( 128 0 100 2 0 ) ( 128 64 100 2 1 ) ( 128 128 100 2 2 ) )
)
}
}
}
"""


## The texel coordinates a face gives a point.
static func texel(f: Dictionary, plane: PackedFloat64Array, p: Vector3) -> Vector2:
	var uv := Q3MapDoc.face_uv(f, plane)
	return Vector2(uv[0] * p.x + uv[1] * p.y + uv[2] * p.z + uv[3], uv[4] * p.x + uv[5] * p.y + uv[6] * p.z + uv[7])


## The face of a brush whose outward normal is n.
static func face_toward(b: Dictionary, n: Vector3) -> int:
	for i in b["faces"].size():
		var pl := Q3MapDoc.plane_of(b, i)
		if Vector3(pl[0], pl[1], pl[2]).dot(n) > 0.999:
			return i
	return -1


func _samples() -> void:
	# Quake 3 form
	var r := Q3MapImport.parse(STANDARD, "standard")
	var d: Q3MapDoc = r["doc"]
	var ok := d != null and d.ids_of("brush").size() == 2 and d.ids_of("entity").size() == 1 and d.worldspawn == {"message": "standard"}
	var top := {}
	var bounds := PackedFloat64Array()
	var front_detail := false
	if ok:
		var b := d.get_obj(d.ids_of("brush")[0])
		bounds = Q3MapGeo.brush_bounds(b["planes"])
		top = b["faces"][face_toward(b, Vector3(0, 0, 1))]
		var front: Dictionary = b["faces"][face_toward(b, Vector3(0, -1, 0))]
		front_detail = front.has("flags") and int(front["flags"][0]) == DETAIL
	check("Quake 3 form: brushes (func_group's into the world), the spawn, shift/rotate/scale and detail as written",
			ok and bounds == PackedFloat64Array([0, 0, 0, 64, 64, 64]) and top.get("material", "") == "textures/base_wall/concrete"
			and top.get("tex", []) == [16.0, 8.0, 45.0, 0.5, 0.25] and front_detail, "%s, top %s" % [bounds, top])
	# Valve 220: texels = (p . axis) / scale + offset
	r = Q3MapImport.parse(VALVE, "valve")
	d = r["doc"]
	var err := INF
	if d != null and d.ids_of("brush").size() == 1:
		var b := d.get_obj(d.ids_of("brush")[0])
		var it := face_toward(b, Vector3(0, 0, 1))
		var ir := face_toward(b, Vector3(1, 0, 0))
		var p := Vector3(40, 24, 64)
		var q := Vector3(64, 10, 30)
		var want_top := Vector2(40.0 / 0.5 + 16.0, -24.0 / 0.5 + 8.0)
		var want_side := Vector2((0.6 * 64.0 + 0.8 * 10.0) / 0.25 + 4.0, -30.0 / 2.0 + 2.0)
		err = (texel(b["faces"][it], Q3MapDoc.plane_of(b, it), p) - want_top).length() \
				+ (texel(b["faces"][ir], Q3MapDoc.plane_of(b, ir), q) - want_side).length()
	check("Valve 220: axes, offsets and scales as TrenchBroom means them; mapversion isn't kept as a key",
			err < TEXEL_TOL and d != null and not d.worldspawn.has("mapversion"), "error %s texels" % String.num_scientific(err))
	# brushDef: s and t in repeats over q3map's ComputeAxisBase frame (texX = +Y, texY = +X on a floor)
	r = Q3MapImport.parse(BRUSHDEF, "primitives")
	d = r["doc"]
	err = INF
	var curves := false
	if d != null and d.ids_of("brush").size() == 1:
		var b := d.get_obj(d.ids_of("brush")[0])
		var it := face_toward(b, Vector3(0, 0, 1))
		var size := Q3MapMaterials.size("textures/base_wall/concrete")
		var p := Vector3(40, 24, 64)
		var want := Vector2((0.0078125 * 24.0 + 0.25) * size.x, (0.0078125 * 40.0 + 0.5) * size.y)
		err = (texel(b["faces"][it], Q3MapDoc.plane_of(b, it), p) - want).length()
		var ps := d.ids_of("patch")
		if ps.size() == 2:
			var c: PackedFloat64Array = d.get_obj(ps[0])["ctrl"]
			# a file row is a column of `height` points: the point at row 1, column 2 is (128, 64, 0, 2, 1)
			var k := (1 * 3 + 2) * 5
			curves = c.slice(k, k + 5) == PackedFloat64Array([128, 64, 0, 2, 1]) and d.get_obj(ps[1])["width"] == 3
	check("brushDef: the texture matrix in q3map's frame; patchDef2 and patchDef3 with their points in place",
			err < TEXEL_TOL and curves, "error %s texels, curves %s" % [String.num_scientific(err), curves])
	# broken files say where
	r = Q3MapImport.parse("{\n\"classname\" \"worldspawn\"\n{\n( 0 0 0 ) ( 1 0 0 )\n}\n}\n", "broken")
	var errors: Array = r["report"]["errors"]
	check("a broken file reports the line it broke on and imports nothing rather than a garbled map",
			errors.size() > 0 and String(errors[0]).contains("line") and r["doc"] == null, str(errors))


func _run() -> void:
	var t0 := Time.get_ticks_msec()
	_round_trip("brushdef")
	_round_trip("valve220")
	_samples()
	print("MAP FORMAT %d/%d passed (%.1f s)" % [passed, passed + failed, (Time.get_ticks_msec() - t0) / 1000.0])
	get_tree().quit(1 if failed else 0)
