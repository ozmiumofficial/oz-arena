class_name Q3Pickup
extends Node3D
## An item from the entity catalog, displayed like cg_ents.c CG_Item: spins once every
## 2048 ms, bobs 4 + cos((t + 1000) * 0.005) * 4 units, weapons drawn at 1.5x and centered
## on their bounds. The second world model (health sphere, powerup ring) shares the spin.

@export var classname := "item_armor_body"
var item: Dictionary
var _visual: Node3D
var _bob := 0.005


func _ready() -> void:
	item = Q3.entities.get(classname, {})
	if item.is_empty():
		push_warning("Q3Pickup: unknown classname " + classname)
		return
	_visual = Node3D.new()
	add_child(_visual)
	# cg_ents.c CG_Item: the second world model (the health sphere, the powerup ring) is drawn for
	# health and powerups only; the armor shard's shard_sphere never shows in Quake 3
	var models: Array = item.get("models", [])
	var second := String(item.get("type", "")) in ["health", "powerup"]
	for i in models.size():
		if i > 0 and not second:
			break
		var n := Q3.instance_prop(models[i])
		if n:
			_visual.add_child(n)
	_visual.scale = Vector3.ONE * float(item.get("scale", 1.0))
	if item.get("type", "") == "weapon":
		var box := _bounds(_visual)
		for c in _visual.get_children():
			c.position -= Vector3(box.get_center().x, 0.0, box.get_center().z)
	# cg_ents.c: scale = 0.005 + entity number * 0.00001, so neighbors don't bob in sync
	_bob = 0.005 + float(get_instance_id() % 97) * 0.00001


func _process(_delta: float) -> void:
	if _visual == null:
		return
	var ms := float(Time.get_ticks_msec())
	rotation.y = fmod(ms, 2048.0) / 2048.0 * TAU
	_visual.position.y = (4.0 + cos((ms + 1000.0) * _bob) * 4.0) * Q3.UNIT


## The remaster's pickup effect (Quake 3 just stops drawing the item): the item swells, spins and
## shrinks away while Q3FX.pickup bursts in its color. Frees the pickup.
func collect() -> void:
	if _visual == null:
		queue_free()
		return
	set_process(false)
	Q3FX.pickup(get_parent(), _visual.global_position + Vector3.UP * 0.4, color())
	var s := _visual.scale
	var pop := create_tween()
	pop.tween_property(_visual, "scale", s * 1.25, 0.07).set_trans(Tween.TRANS_QUAD).set_ease(Tween.EASE_OUT)
	pop.tween_property(_visual, "scale", Vector3.ZERO, 0.18).set_trans(Tween.TRANS_BACK).set_ease(Tween.EASE_IN)
	pop.tween_callback(queue_free)
	var lift := create_tween().set_parallel(true)
	lift.tween_property(self, "rotation:y", rotation.y + TAU, 0.25)
	lift.tween_property(_visual, "position:y", _visual.position.y + 0.35, 0.25).set_ease(Tween.EASE_OUT)


## The color an item's pickup burst takes: Quake 3's own color for it (the health sphere's, the
## armor's, the powerup's shell).
func color() -> Color:
	var cls := classname
	match String(item.get("type", "")):
		"health":
			var q := int(item.get("quantity", 25))
			return Color(0.3, 1.0, 0.35) if q <= 5 else Color(1.0, 0.85, 0.3) if q <= 25 				else Color(1.0, 0.55, 0.2) if q <= 50 else Color(0.35, 0.55, 1.0)
		"armor":
			return Color(0.5, 1.0, 0.4) if cls.ends_with("shard") else Color(1.0, 0.25, 0.2) if cls.ends_with("body") 				else Color(1.0, 0.85, 0.3)
		"powerup":
			for k in POWERUP_COLORS:
				if cls.contains(k):
					return POWERUP_COLORS[k]
			return Color(0.9, 0.9, 1.0)
		"ammo":
			return Color(1.0, 0.85, 0.55)
	return Color(0.7, 0.85, 1.0)


const POWERUP_COLORS := {"quad": Color(0.35, 0.45, 1.0), "enviro": Color(1.0, 0.75, 0.25), "haste": Color(1.0, 0.9, 0.3),
	"invis": Color(0.85, 0.9, 1.0), "regen": Color(1.0, 0.25, 0.25), "flight": Color(0.8, 0.4, 1.0)}


func _bounds(n: Node) -> AABB:
	var box := AABB()
	var first := true
	for m in n.find_children("*", "MeshInstance3D", true, false):
		var b: AABB = (m as MeshInstance3D).get_aabb()
		b = (n as Node3D).global_transform.affine_inverse() * (m as MeshInstance3D).global_transform * b
		box = b if first else box.merge(b)
		first = false
	return box
