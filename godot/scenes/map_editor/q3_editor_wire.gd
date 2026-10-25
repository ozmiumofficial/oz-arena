extends MeshInstance3D
## The map as the 2D views draw it (Radiant's): every brush's edges, tool brushes in their tool color,
## brush entities' in their class color, the curves' control nets, the point entities' boxes, all
## through each other (a top view would otherwise show only roofs). Rebuilt while shown, after the
## document changes.

const BRUSH := Color(0.8, 0.77, 0.74, 0.55)
const SKY := Color(0.45, 0.7, 1.0, 0.5)
const PATCH := Color(1.0, 0.8, 0.3, 0.6)

var editor: Q3MapEditor
var _key := []


func _ready() -> void:
	var m := StandardMaterial3D.new()
	m.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	m.vertex_color_use_as_albedo = true
	m.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	m.no_depth_test = true
	material_override = m
	cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	visible = false


func _process(_delta: float) -> void:
	if not visible:
		return
	var key := [editor.doc, editor.doc_revision(), editor.doc.hidden.size(), editor.filters.size(), editor.is_isolated()]
	if key != _key:
		_key = key
		_build()


func _build() -> void:
	var d := editor.doc
	var im := ImmediateMesh.new()
	var any := false
	im.surface_begin(Mesh.PRIMITIVE_LINES)
	var owners := {}
	for id in d.objects:
		var o: Dictionary = d.objects[id]
		if o["type"] == "brush" and o.get("entity", -1) != -1:
			owners[id] = o["entity"]
	for id in d.objects:
		if d.hidden.has(id) or (editor.filters.size() and editor.filtered(id)):
			continue
		var o: Dictionary = d.objects[id]
		match o["type"]:
			"brush":
				var w := d.windings(id)
				var c := _brush_color(o, owners.get(id, -1))
				for poly in w:
					any = _poly(im, poly, c) or any
			"patch":
				any = _net(im, o) or any
			"entity":
				var box := Q3MapEditor.entity_box(o)
				if box.size != Vector3.ZERO:
					var c := Q3MapEntities.color(String(o["classname"]))
					for poly in Q3MapGeo.brush_windings(Q3MapGeo.box(Q3MapEditor.f64(box.position), Q3MapEditor.f64(box.end))):
						any = _poly(im, poly, c) or any
	if not any:
		# an empty surface is an error: one invisible line keeps the mesh valid
		im.surface_set_color(Color(0, 0, 0, 0))
		im.surface_add_vertex(Vector3.ZERO)
		im.surface_set_color(Color(0, 0, 0, 0))
		im.surface_add_vertex(Vector3.ZERO)
	im.surface_end()
	mesh = im


## A brush's line color: its brush entity's class, its tool shader's (all faces tools), sky, or plain.
func _brush_color(b: Dictionary, owner: int) -> Color:
	if owner != -1 and editor.doc.objects.has(owner):
		return Color(Q3MapEntities.color(String(editor.doc.objects[owner].get("classname", ""))), 0.8)
	var kinds := {}
	for f in b["faces"]:
		kinds[Q3MapMaterials.kind(f["material"])] = f["material"]
	if kinds.size() == 1 and kinds.has("hidden"):
		return Color(Q3MapMaterials._tool_look(String(kinds["hidden"]))[0], 0.75)
	if kinds.has("sky"):
		return SKY
	return BRUSH


func _poly(im: ImmediateMesh, poly: PackedFloat64Array, c: Color) -> bool:
	var n := poly.size() / 3
	if n < 2:
		return false
	for i in n:
		var j := (i + 1) % n
		im.surface_set_color(c)
		im.surface_add_vertex(Q3MapEditor.to_godot(Vector3(poly[i * 3], poly[i * 3 + 1], poly[i * 3 + 2])))
		im.surface_set_color(c)
		im.surface_add_vertex(Q3MapEditor.to_godot(Vector3(poly[j * 3], poly[j * 3 + 1], poly[j * 3 + 2])))
	return true


## A curve's control net: its rows and columns.
func _net(im: ImmediateMesh, p: Dictionary) -> bool:
	var c: PackedFloat64Array = p["ctrl"]
	var w: int = p["width"]
	var h: int = p["height"]
	if w < 2 or h < 2:
		return false
	for j in h:
		for i in w:
			var a := Q3MapEditor.to_godot(Vector3(c[(j * w + i) * 5], c[(j * w + i) * 5 + 1], c[(j * w + i) * 5 + 2]))
			for nb in [[i + 1, j], [i, j + 1]]:
				if nb[0] < w and nb[1] < h:
					var k: int = nb[1] * w + nb[0]
					im.surface_set_color(PATCH)
					im.surface_add_vertex(a)
					im.surface_set_color(PATCH)
					im.surface_add_vertex(Q3MapEditor.to_godot(Vector3(c[k * 5], c[k * 5 + 1], c[k * 5 + 2])))
	return true
