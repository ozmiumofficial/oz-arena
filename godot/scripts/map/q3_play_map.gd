class_name Q3PlayMap
extends Q3Map
## A map from the editor, as Gameplay draws it during a play-test (MAP_EDITOR_PLAN.md E5): the
## game runs the compiled .bsp in q3core, and this is what the player sees, built from the same
## document instead of a pre-built scene. It has what Q3Map offers Gameplay:
##   Geometry/world      the world brushes and curves, without the q3map tool shaders the game never
##                       draws (caulk, clip, triggers, hints ...)
##   Geometry/model_<n>  each brush entity, numbered as q3map numbers brush models, for the movers
##   Entities            markers carrying the key/values (intermission camera, spawns)
##   Lights, Collision   the map's own lights (or the editor's preview light when it has none), and
##                       hulls for ragdolls and gibs
## Tint and the remastered materials show as in the editor.

const SKIP := {"hidden": true, "fog": true}
## Faces that don't stop a body (Quake 3 contents): liquids, triggers, hints and the like.
const NONSOLID := ["textures/common/hint", "textures/common/skip", "textures/common/trigger",
		"textures/common/nodrawnonsolid", "textures/common/clusterportal", "textures/common/areaportal",
		"textures/common/donotenter", "textures/common/clip", "textures/common/full_clip",
		"textures/common/weapclip", "textures/common/missileclip"]

var doc: Q3MapDoc
var markers: Q3EditorEntities    ## F7: the entities as the editor draws them (hidden at first)


## Builds the map from a document and q3map's brush model numbers ({entity id: n}, from
## Q3MapExport.to_map).
static func build(d: Q3MapDoc, models: Dictionary) -> Q3PlayMap:
	var m := Q3PlayMap.new()
	m.name = "PlayMap"
	m.doc = d
	var geo := Node3D.new()
	geo.name = "Geometry"
	m.add_child(geo)
	var own := Q3MapEntities.owned(d)
	var in_models := {}
	for e in models:
		for id in own.get(e, []):
			in_models[id] = true
	var world := Q3MapMesher.new()
	world.name = "world"
	world.skip_kinds = SKIP
	world.show_hidden = true      # hidden in the editor, but in the .bsp all the same
	world.exclude = in_models     # brush models draw on their own nodes, where Gameplay moves them
	geo.add_child(world)
	world.bind(d)
	for e in models:
		# q3map stores a brush model relative to its origin (the "origin" key, or an origin brush's
		# center: AdjustBrushesForOrigin), and the client puts model_<n> at the entity's position,
		# so the mesh, built in map space, sits back by the origin under the node Gameplay moves
		var holder := Node3D.new()
		holder.name = "model_%d" % int(models[e])
		geo.add_child(holder)
		var mm := Q3MapMesher.new()
		mm.name = "mesh"
		mm.skip_kinds = SKIP
		mm.show_hidden = true
		var ids := {}
		for id in own.get(e, []):
			ids[id] = true
		mm.only = ids
		mm.position = -Q3MapEditor.to_godot(model_origin(d, d.get_obj(e), own.get(e, [])))
		holder.add_child(mm)
		mm.bind(d)
	m._entities(d)
	m._lights(d)
	m._collision(d, in_models)
	m.markers = Q3EditorEntities.new()
	m.markers.name = "Markers"
	m.markers.with_lights = false
	m.markers.visible = false
	m.add_child(m.markers)
	m.markers.bind(d)
	return m


## A brush model's origin as q3map sets it: the "origin" key, else the center of an origin brush,
## else the map's origin.
static func model_origin(d: Q3MapDoc, e: Dictionary, parts: Array) -> Vector3:
	if e.get("keys", {}).has("origin"):
		return Q3MapEntities.origin(e)
	for id in parts:
		var b := d.get_obj(id)
		if b.get("type", "") == "brush" and b["faces"].any(func(f): return String(f["material"]) == "textures/common/origin"):
			return Q3MapEditor.bounds_in(d, [id]).get_center()
	return Vector3.ZERO


func _entities(d: Q3MapDoc) -> void:
	var holder := Node3D.new()
	holder.name = "Entities"
	add_child(holder)
	for id in d.objects:
		var e: Dictionary = d.objects[id]
		if e["type"] != "entity" or not e["keys"].has("origin"):
			continue
		var cn: String = e["classname"]
		if cn in ["light", "misc_model"]:
			continue
		var marker := Marker3D.new()
		marker.name = "%s_%d" % [cn, id]
		marker.position = Q3MapEditor.to_godot(Q3MapEntities.origin(e))
		if e["keys"].has("angle"):
			marker.rotation.y = deg_to_rad(float(e["keys"]["angle"])) - PI * 0.5   # as Q3MapBuilder
		var keys: Dictionary = e["keys"].duplicate()
		keys["classname"] = cn
		marker.set_meta("entity", keys)
		holder.add_child(marker)


## The map's lights with q3map's falloff, casting shadows. A map without lights gets the editor's
## preview light instead (a Quake 3 map compiled without lighting draws full bright).
func _lights(d: Q3MapDoc) -> void:
	var holder := Node3D.new()
	holder.name = "Lights"
	add_child(holder)
	var any := false
	for id in d.objects:
		var e: Dictionary = d.objects[id]
		if e["type"] == "entity" and e["classname"] == "light":
			var l := Q3EditorEntities._light(e, Q3MapEntities.light_params(e["keys"]), Q3EditorEntities.spot_aim(e, d))
			l.position = Q3MapEditor.to_godot(Q3MapEntities.origin(e))
			l.shadow_enabled = true
			holder.add_child(l)
			any = true
	var env := WorldEnvironment.new()
	env.name = "Environment"
	env.environment = Environment.new()
	var en := env.environment
	en.background_mode = Environment.BG_COLOR
	en.background_color = Color(0.03, 0.03, 0.035)
	var sky: Sky = Q3MapMaterials.sky_resource(String(d.worldspawn.get("_sky", "")))
	if sky:
		en.sky = sky
		en.background_mode = Environment.BG_SKY
	en.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	en.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	if any:
		# worldspawn "ambient" is q3map's minimum light; a faint floor, as Q3MapBuilder does it
		var amb := float(d.worldspawn.get("ambient", "0"))
		en.ambient_light_color = Q3MapBuilder._color(String(d.worldspawn.get("_color", "1 1 1")))
		en.ambient_light_energy = clampf(0.15 + amb / 64.0, 0.15, 0.6)
	else:
		en.ambient_light_color = Color(0.42, 0.4, 0.4)
		en.ambient_light_energy = 1.0
		var sun := DirectionalLight3D.new()
		sun.rotation_degrees = Vector3(-55, 35, 0)
		sun.light_energy = 1.1
		sun.shadow_enabled = true
		holder.add_child(sun)
	add_child(env)


## Ragdolls and gibs collide with Godot physics (the game collides through q3core and the .bsp):
## a convex hull per solid world brush, the curves as triangles.
func _collision(d: Q3MapDoc, skip: Dictionary) -> void:
	var body := StaticBody3D.new()
	body.name = "Collision"
	body.collision_layer = Q3MapBuilder.LAYER_SOLID
	body.collision_mask = 0
	add_child(body)
	var tris := PackedVector3Array()
	for id in d.objects:
		var o: Dictionary = d.objects[id]
		if skip.has(id) or not world_part(d, o):
			continue
		match o["type"]:
			"brush":
				if not _solid(o):
					continue
				var pts := PackedVector3Array()
				var v: PackedFloat64Array = Q3MapGeo.brush_vertices(o["planes"])
				for i in v.size() / 3:
					pts.append(Q3MapEditor.to_godot(Vector3(v[i * 3], v[i * 3 + 1], v[i * 3 + 2])))
				if pts.size() >= 4:
					var shape := ConvexPolygonShape3D.new()
					shape.points = pts
					var cs := CollisionShape3D.new()
					cs.shape = shape
					body.add_child(cs)
			"patch":
				if SKIP.has(Q3MapMaterials.kind(o["material"])):
					continue
				var t: Dictionary = d.patch_tess(id)
				var p: PackedFloat64Array = t.get("points", PackedFloat64Array())
				var w: int = t.get("width", 0)
				var h: int = t.get("height", 0)
				for y in h - 1:
					for x in w - 1:
						var a := y * w + x
						for tri in [[a, a + 1, a + w + 1], [a, a + w + 1, a + w]]:
							for k in tri:
								tris.append(Q3MapEditor.to_godot(Vector3(p[k * 3], p[k * 3 + 1], p[k * 3 + 2])))
	if tris.size():
		var shape := ConcavePolygonShape3D.new()
		shape.backface_collision = true
		shape.set_faces(tris)
		var cs := CollisionShape3D.new()
		cs.name = "curves"
		cs.shape = shape
		body.add_child(cs)


## Whether a brush or patch is part of the world in the compiled map: no entity, or a func_group
## (q3map merges those into the world).
static func world_part(d: Q3MapDoc, o: Dictionary) -> bool:
	var e := int(o.get("entity", 0))
	return e == 0 or String(d.get_obj(e).get("classname", "")) in Q3MapExport.MERGED


## A brush stops a body unless every face is a liquid or a non-solid tool shader.
static func _solid(b: Dictionary) -> bool:
	for f in b["faces"]:
		var m := String(f["material"])
		if not (m in NONSOLID or Q3MapMaterials.kind(m) == "liquid"):
			return true
	return false
