class_name Q3PatchFactory
extends RefCounted
## Ready-made curved surfaces as biquadratic control nets, after OZMIUM's PatchFactory and the way
## Radiant mappers build them: a quarter circle is one quadratic arc whose middle control point sits
## on the corner of its square (the "bevel"), a half circle is two, a full circle four. Columns run
## along the arc (counter-clockwise seen from above), rows up the height, so du x dv faces outward.
## id space (Z up, units). Patch objects as Q3MapDoc stores them, texture coordinates in repeats at
## Quake 3's default scale (2 texels a unit).

const KINDS := ["bevel", "curve", "cylinder", "arch"]


## Control points of a circular arc around center in the XY plane, in 90 degree pieces.
static func arc(center: Vector3, radius: float, from_deg: float, to_deg: float) -> Array:
	var pts := []
	var quarters := maxi(1, roundi(absf(to_deg - from_deg) / 90.0))
	var step := (to_deg - from_deg) / quarters
	for q in quarters:
		var a0 := deg_to_rad(from_deg + step * q)
		var a1 := deg_to_rad(from_deg + step * (q + 1))
		var am := (a0 + a1) * 0.5
		var rc := radius / cos((a1 - a0) * 0.5)   # the tangents' intersection
		pts.append(center + Vector3(radius * cos(a0), radius * sin(a0), 0))
		pts.append(center + Vector3(rc * cos(am), rc * sin(am), 0))
		if q == quarters - 1:
			pts.append(center + Vector3(radius * cos(a1), radius * sin(a1), 0))
	return pts


## A patch object from rows of points (rows[j][i]); s runs along the rows by distance, t up by height.
static func from_rows(rows: Array, material: String) -> Dictionary:
	var size := Q3MapMaterials.size(material)
	var w: int = rows[0].size()
	var h: int = rows.size()
	var ctrl := PackedFloat64Array()
	for j in h:
		var along := 0.0
		for i in w:
			var p: Vector3 = rows[j][i]
			if i > 0:
				along += p.distance_to(rows[j][i - 1])
			var up := 0.0
			for k in j:
				up += (rows[k + 1][i] as Vector3).distance_to(rows[k][i])
			ctrl.append_array([_r(p.x), _r(p.y), _r(p.z), along * 2.0 / size.x, up * 2.0 / size.y])
	return {"type": "patch", "id": 0, "entity": 0, "group": 0, "width": w, "height": h, "ctrl": ctrl, "material": material}


## Rounds away float noise from the trigonometry (cos 90 degrees is 6e-17, not 0).
static func _r(v: float) -> float:
	var r := roundf(v * 1024.0) / 1024.0
	return r + 0.0


## A vertical curved wall: the arc as columns, three rows from center.z up to height.
static func wall(center: Vector3, radius: float, height: float, from_deg: float, to_deg: float, material: String, inside := false) -> Dictionary:
	var a := arc(center, radius, from_deg, to_deg)
	if inside:
		a.reverse()
	var rows := []
	for row in 3:
		var z := height * row * 0.5
		rows.append(a.map(func(p): return p + Vector3(0, 0, z)))
	return from_rows(rows, material)


## kind: bevel (quarter circle), curve (half), cylinder, arch (a vault over an opening along Y).
static func make(kind: String, at: Vector3, radius: float, height: float, material: String) -> Dictionary:
	match kind:
		"bevel": return wall(at, radius, height, 0, 90, material)
		"curve": return wall(at, radius, height, 0, 180, material)
		"cylinder": return wall(at, radius, height, 0, 360, material)
		"arch":
			# springing line at at.z, opening 2 * radius wide along X, 32 units deep along Y, facing down
			var rows := []
			for row in 3:
				var y := at.y + 16.0 * row
				rows.append([Vector3(at.x + radius, y, at.z), Vector3(at.x + radius, y, at.z + radius), Vector3(at.x, y, at.z + radius),
						Vector3(at.x - radius, y, at.z + radius), Vector3(at.x - radius, y, at.z)])
			return from_rows(rows, material)
	return {}
