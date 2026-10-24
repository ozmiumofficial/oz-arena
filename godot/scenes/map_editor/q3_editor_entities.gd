class_name Q3EditorEntities
extends Node3D
## The map editor's entities (MAP_EDITOR_PLAN.md E4). Each point entity is drawn as what it is:
## pickups as Quake 3 draws them (Q3Pickup, spinning and bobbing), misc_models as their model, lights
## as a glowing bead with a real Godot light (q3map's falloff), spawns as a player-sized capsule, and
## everything else as a box in its class color with its name. Over them go the links: target lines,
## facing arrows and every jump pad's arc on id's own trajectory (g_trigger.c AimAtTarget).

const ARROW := 40.0              ## facing arrow length (units)
const ARC_STEP := 0.04           ## seconds per arc segment
const ARC_TIME := 4.0            ## the longest fall drawn after the top of an arc
const LINK := Color(0.9, 0.9, 0.5, 0.85)

var doc: Q3MapDoc
var exclude := {}                ## id -> true: drawn elsewhere (the selection)
var lights_only := false         ## the "lights only" preview: map lights cast shadows
var labels := true
var with_lights := true          ## false: light entities draw their bead only (the play-test's markers)

var _nodes := {}                 ## id -> [object, Node3D]
var _dirty := {}
var _lines_dirty := true
var _lines := MeshInstance3D.new()
var _lines_xray := MeshInstance3D.new()
var _boxes := {}                 ## brush id -> [object, AABB] (arc landing tests)
var _arcs := {}                  ## "from|to|g" -> [brush revision, arc]
var _brush_rev := 0              ## bumps when brushes change, so arcs test their landing again
var _arc_budget := 1             ## landing tests left this frame
var _arcs_used := {}             ## arc keys drawn by the last links rebuild


func _ready() -> void:
	for m in [_lines, _lines_xray]:
		m.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		add_child(m)
	_lines.material_override = Q3EditorOverlay._flat(Color.WHITE, false, true)
	_lines_xray.material_override = Q3EditorOverlay._flat(Color(1, 1, 1, 0.22), true, true)


func bind(d: Q3MapDoc) -> void:
	if doc and doc.changed.is_connected(_on_changed):
		doc.changed.disconnect(_on_changed)
	doc = d
	doc.changed.connect(_on_changed)
	for id in _nodes:
		_nodes[id][1].queue_free()
	_nodes.clear()
	_boxes.clear()
	_arcs.clear()
	_brush_rev += 1
	for id in doc.objects:
		_dirty[id] = true
	_lines_dirty = true


func touch(ids: Array) -> void:
	for id in ids:
		_dirty[id] = true
	_lines_dirty = true


func _on_changed(ids: Array) -> void:
	if ids.is_empty():
		for id in _nodes.keys() + doc.objects.keys():
			_dirty[id] = true
		_brush_rev += 1
	else:
		touch(ids)
		# a removed or edited brush can change where an arc lands
		if ids.any(func(i): return doc.get_obj(i).get("type", "brush") == "brush"):
			_brush_rev += 1
	_lines_dirty = true


func _process(_delta: float) -> void:
	flush()


func flush() -> void:
	_arc_budget = 1
	for id in _dirty:
		_build(id)
	_dirty.clear()
	if _lines_dirty:
		_lines_dirty = false
		_build_lines()


func _build(id: int) -> void:
	var o := doc.get_obj(id)
	var show: bool = o.get("type", "") == "entity" and not exclude.has(id) and not doc.hidden.has(id)
	if _nodes.has(id):
		if show and is_same(_nodes[id][0], o):
			return
		_nodes[id][1].queue_free()
		_nodes.erase(id)
	if not show:
		return
	var n := make_visual(o, doc, with_lights, labels)
	if n == null:
		return
	add_child(n)
	_nodes[id] = [o, n]
	_shadows(n)


func set_lights_only(on: bool) -> void:
	lights_only = on
	for id in _nodes:
		_shadows(_nodes[id][1])


func _shadows(n: Node) -> void:
	for l in n.find_children("*", "Light3D", true, false):
		(l as Light3D).shadow_enabled = lights_only


## The light nodes drawn now (tests, the lights-only preview).
func light_nodes() -> Array:
	var out := []
	for id in _nodes:
		out.append_array(_nodes[id][1].find_children("*", "Light3D", true, false))
	return out


func node_of(id: int) -> Node3D:
	return _nodes[id][1] if _nodes.has(id) else null


# ---------------------------------------------------------------- one entity

## A point entity's look, placed at its origin (Godot space); null for brush entities without an
## origin (their brushes are the look). with_light adds the real light for "light" entities.
static func make_visual(e: Dictionary, d: Q3MapDoc, with_light := true, label := true) -> Node3D:
	var cn := String(e.get("classname", ""))
	var keys: Dictionary = e.get("keys", {})
	if Q3MapEntities.is_brush_class(cn) and not keys.has("origin"):
		return null
	var root := Node3D.new()
	root.name = "Entity%d" % int(e.get("id", 0))
	root.position = Q3MapEditor.to_godot(Q3MapEntities.origin(e))
	var col := Q3MapEntities.color(cn)
	var box := Q3MapEntities.bounds(cn)
	if Q3MapEntities.is_item(cn):
		var p := Q3Pickup.new()
		p.classname = cn
		root.add_child(p)
		return root
	match cn:
		"light":
			var lp := Q3MapEntities.light_params(keys)
			var bead := MeshInstance3D.new()
			var sm := SphereMesh.new()
			sm.radius = 5.0 / 32.0
			sm.height = 10.0 / 32.0
			bead.mesh = sm
			var m := StandardMaterial3D.new()
			m.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
			m.albedo_color = lp["color"]
			bead.material_override = m
			bead.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
			root.add_child(bead)
			if with_light:
				var aim: Variant = spot_aim(e, d)
				root.set_meta("aim", aim)
				root.add_child(_light(e, lp, aim))
			return root
		"misc_model":
			var id := String(keys.get("model", "")).trim_prefix("models/").trim_suffix(".md3")
			if id != "" and ResourceLoader.exists(Q3.LIB + "props/%s.glb" % id):
				var n := Q3.instance_prop(id)
				if n:
					n.rotation.y = deg_to_rad(Q3MapEntities.angle(e))
					n.scale = Vector3.ONE * float(keys.get("modelscale", "1"))
					root.add_child(n)
					return root
	var mi := MeshInstance3D.new()
	if Q3MapEntities.category(cn) == "spawn" or cn == "misc_teleporter_dest":
		var cap := CapsuleMesh.new()
		cap.radius = 15.0 / 32.0
		cap.height = 56.0 / 32.0
		mi.mesh = cap
	else:
		var bm := BoxMesh.new()
		bm.size = Vector3(box.size.y, box.size.z, box.size.x) / 32.0
		mi.mesh = bm
	mi.position = Q3MapEditor.to_godot(box.get_center())
	var mat := StandardMaterial3D.new()
	mat.albedo_color = Color(col, 0.55)
	mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	mat.roughness = 0.8
	mi.material_override = mat
	mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	root.add_child(mi)
	if label:
		var l := Label3D.new()
		l.text = cn
		l.billboard = BaseMaterial3D.BILLBOARD_ENABLED
		l.fixed_size = true
		l.pixel_size = 0.0007
		l.font_size = 26
		l.outline_size = 8
		l.modulate = col.lightened(0.35)
		l.position = Q3MapEditor.to_godot(Vector3(0, 0, box.end.z + 10))
		l.visibility_range_end = 24.0
		root.add_child(l)
	return root


## Where a spot light aims (its first target's origin, or a brush target's center), or null.
static func spot_aim(e: Dictionary, d: Q3MapDoc, names := {}, own := {}) -> Variant:
	if d == null or not e["keys"].has("target"):
		return null
	var targets := Q3MapEntities.targets_of(d, e, names)
	return Q3MapEntities.center(d, d.get_obj(targets[0]), own) if targets.size() else null


## q3map's light: an omni, or a spot aimed at `aim` (radius 64 units across there).
static func _light(e: Dictionary, lp: Dictionary, aim: Variant) -> Light3D:
	var keys: Dictionary = e["keys"]
	var light: Light3D
	if aim != null:
		var to := Q3MapEditor.to_godot((aim as Vector3) - Q3MapEntities.origin(e))
		var spot := SpotLight3D.new()
		var r := float(keys.get("radius", "64")) / 32.0
		spot.spot_angle = clampf(rad_to_deg(atan2(r, maxf(to.length(), 0.01))), 5.0, 89.0)
		spot.spot_range = lp["range"]
		spot.spot_attenuation = 1.0
		if to.length() > 0.001:
			spot.basis = Basis.looking_at(to, Vector3.UP if absf(to.normalized().y) < 0.99 else Vector3.RIGHT)
		light = spot
	else:
		var omni := OmniLight3D.new()
		omni.omni_range = lp["range"]
		omni.omni_attenuation = 1.0
		light = omni
	light.light_color = lp["color"]
	light.light_energy = lp["energy"]
	light.shadow_enabled = false
	return light


# ---------------------------------------------------------------- links

func _build_lines() -> void:
	var im := ImmediateMesh.new()
	var any := draw_links(im, doc, exclude, _arc_cached)
	_lines.mesh = im if any else null
	_lines_xray.mesh = im if any else null
	# spots whose target moved aim again
	var names := Q3MapEntities.targetnames(doc)
	var own := {}
	for id in _nodes.keys():
		var o: Dictionary = _nodes[id][0]
		if o["classname"] == "light" and o["keys"].has("target"):
			if own.is_empty():
				own = Q3MapEntities.owned(doc)
			if spot_aim(o, doc, names, own) != _nodes[id][1].get_meta("aim", null):
				_nodes[id][1].queue_free()
				_nodes.erase(id)
				_build(id)
	# arcs no longer drawn drop out of the cache
	if _arcs.size() > 64:
		for k in _arcs.keys():
			if not _arcs_used.has(k):
				_arcs.erase(k)
	_arcs_used.clear()


## Target lines, facing arrows and jump pad arcs for every entity in d into im. arc_fn(from, to, g)
## -> [points, climb] gives the arcs (default: arc_points without landing tests). Returns whether
## anything was drawn.
static func draw_links(im: ImmediateMesh, d: Q3MapDoc, skip := {}, arc_fn := Callable()) -> bool:
	var names := Q3MapEntities.targetnames(d)
	var own := Q3MapEntities.owned(d)
	var g := Q3MapEntities.gravity(d)
	var started := false
	for id in d.objects:
		var e: Dictionary = d.objects[id]
		if e["type"] != "entity" or d.hidden.has(id):
			continue
		var cn: String = e["classname"]
		var from := Q3MapEntities.center(d, e, own)
		if Q3MapEntities.faces(cn) and not skip.has(id):
			if not started:
				im.surface_begin(Mesh.PRIMITIVE_LINES)
				started = true
			_arrow(im, from, Q3MapEntities.angle(e), Q3MapEntities.color(cn).lightened(0.3))
		for t in Q3MapEntities.targets_of(d, e, names):
			var to := Q3MapEntities.center(d, d.get_obj(t), own)
			if not started:
				im.surface_begin(Mesh.PRIMITIVE_LINES)
				started = true
			if cn == "trigger_push" or cn == "target_push":
				var arc: Array = arc_fn.call(from, to, g) if arc_fn.is_valid() else arc_points(from, to, g)
				_draw_arc(im, from, to, arc)
			else:
				_line(im, from, to, LINK)
	if started:
		im.surface_end()
	return started


static func _line(im: ImmediateMesh, a: Vector3, b: Vector3, c: Color) -> void:
	im.surface_set_color(c)
	im.surface_add_vertex(Q3MapEditor.to_godot(a))
	im.surface_set_color(c)
	im.surface_add_vertex(Q3MapEditor.to_godot(b))


static func _arrow(im: ImmediateMesh, at: Vector3, yaw: float, c: Color) -> void:
	var f := Vector3(cos(deg_to_rad(yaw)), sin(deg_to_rad(yaw)), 0)
	var s := Vector3(-f.y, f.x, 0)
	var tip := at + f * ARROW
	_line(im, at, tip, c)
	_line(im, tip, tip - f * 12 + s * 8, c)
	_line(im, tip, tip - f * 12 - s * 8, c)


## A jump pad's flight, id space: [points, climb] where points[0..climb] rise to the target (the
## top of the arc) and the rest fall on. hit_fn(a, b) -> Vector3 or null ends the fall where it meets
## something; without one the fall stops back at the launch height. [] when the target isn't above.
static func arc_points(from: Vector3, to: Vector3, g: float, hit_fn := Callable()) -> Array:
	var v := Q3MapEntities.push_velocity(from, to, g)
	if v == Vector3.ZERO:
		return []
	var up := v.z / g
	var pts := PackedVector3Array([from])
	var climb := 0
	var t := 0.0
	while t < up + ARC_TIME:
		t = minf(t + ARC_STEP, up) if t < up else t + ARC_STEP
		var p := from + Vector3(v.x * t, v.y * t, v.z * t - 0.5 * g * t * t)
		if t > up:
			if hit_fn.is_valid():
				var hit: Variant = hit_fn.call(pts[pts.size() - 1], p)
				if hit != null:
					pts.append(hit)
					break
				if p.z < from.z - 4096:
					break
			elif p.z <= from.z:
				pts.append(p)
				break
		pts.append(p)
		if t <= up:
			climb = pts.size() - 1
	return [pts, climb]


static func _draw_arc(im: ImmediateMesh, from: Vector3, to: Vector3, arc: Array) -> void:
	if arc.is_empty():
		_line(im, from, to, Color(1, 0.2, 0.15))
		return
	var pts: PackedVector3Array = arc[0]
	var climb: int = arc[1]
	for i in pts.size() - 1:
		_line(im, pts[i], pts[i + 1], Color(0.35, 1.0, 0.55) if i < climb else Color(0.35, 1.0, 0.55, 0.5))


## Arcs for the layer: cached; a stale one is drawn as it was until its turn to be tested again
## (one per frame, since the landing test walks the brushes).
func _arc_cached(from: Vector3, to: Vector3, g: float) -> Array:
	var key := "%s|%s|%s" % [from, to, g]
	_arcs_used[key] = true
	var c: Array = _arcs.get(key, [])
	if c.size() and c[0] == _brush_rev:
		return c[1]
	if _arc_budget <= 0:
		_lines_dirty = true
		return c[1] if c.size() else arc_points(from, to, g)
	_arc_budget -= 1
	var arc := _arc_landing(from, to, g)
	_arcs[key] = [_brush_rev, arc]
	return arc


func _arc_landing(from: Vector3, to: Vector3, g: float) -> Array:
	# the brushes the free fall's sweep crosses, then each segment against those it overlaps
	var free := arc_points(from, to, g, func(_a, b): return b if b.z < from.z - 4096 else null)
	if free.is_empty():
		return free
	var sweep := AABB(from, Vector3.ZERO)
	for p in free[0]:
		sweep = sweep.expand(p)
	var cands := []
	for id in doc.objects:
		var o: Dictionary = doc.objects[id]
		if o["type"] == "brush" and int(o.get("entity", 0)) == 0 and not doc.hidden.has(id):
			var b := _brush_box(id)
			if b.intersects(sweep.grow(1)):
				cands.append([id, b])
	return arc_points(from, to, g, func(a: Vector3, b: Vector3) -> Variant:
		var seg := AABB(a, Vector3.ZERO).expand(b).grow(0.5)
		var dir := b - a
		var seg_len := dir.length()
		if seg_len < 1e-6:
			return null
		dir /= seg_len
		var best := INF
		for c in cands:
			if not (c[1] as AABB).intersects(seg):
				continue
			var hit: PackedFloat64Array = Q3MapGeo.ray(doc.objects[c[0]]["planes"], Q3MapEditor.f64(a), Q3MapEditor.f64(dir))
			if hit.size() == 2 and hit[0] >= 0.0 and hit[0] <= seg_len and hit[0] < best:
				best = hit[0]
		return a + dir * best if best < INF else null)


func _brush_box(id: int) -> AABB:
	var o := doc.get_obj(id)
	var c: Array = _boxes.get(id, [])
	if c.size() and is_same(c[0], o):
		return c[1]
	var bb: PackedFloat64Array = Q3MapGeo.brush_bounds(o["planes"])
	var box := AABB(Vector3(bb[0], bb[1], bb[2]), Vector3(bb[3] - bb[0], bb[4] - bb[1], bb[5] - bb[2])) if bb.size() == 6 else AABB()
	_boxes[id] = [o, box]
	return box
