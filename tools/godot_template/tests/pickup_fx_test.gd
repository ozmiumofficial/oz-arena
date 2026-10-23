extends Node3D
## Windowed look at the pickup effect (Q3Pickup.collect, Q3FX.pickup): four items on q3dm0's
## floor (shard, armor, mega health, quad) collected together, frames saved as they go.
## Saves <out>_<n>.png. tests/pickup_events.tscn checks the effect fires on real pickups.
##   godot --path . res://tests/pickup_fx_test.tscn -- out=C:/temp/pickup

const ITEMS := ["item_armor_shard", "item_armor_combat", "item_health_mega", "item_quad"]
const AT := [0.9, 1.05, 1.12, 1.2, 1.3, 1.5]
var out := "user://pickup"
var _t := 0.0
var _shot := 0
var _items: Array[Q3Pickup] = []


func _ready() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("out="):
			out = a.substr(4)
	Engine.max_fps = 60
	add_child((load(Q3MapBuilder.scene_path("q3dm0")) as PackedScene).instantiate())
	# the armor shard row on q3dm0 (id -336, -52, 12), seen from 3 m back
	var floor_at := Q3.q3_to_godot(Vector3(-336, -52, 12))
	for i in ITEMS.size():
		var p := Q3Pickup.new()
		p.classname = ITEMS[i]
		add_child(p)
		p.global_position = floor_at + Vector3((i - 1.5) * 0.9, 0.3, 0)
		_items.append(p)
	var cam := Camera3D.new()
	add_child(cam)
	cam.global_position = floor_at + Vector3(0, 1.1, 3.2)
	cam.look_at(floor_at + Vector3(0, 0.4, 0))
	cam.make_current()


func _process(delta: float) -> void:
	_t += delta
	if _t > 1.0 and _t - delta <= 1.0:
		for p in _items:
			p.collect()
	if _shot < AT.size() and _t > AT[_shot]:
		_shot += 1
		await RenderingServer.frame_post_draw
		get_viewport().get_texture().get_image().save_png("%s_%d.png" % [out, _shot - 1])
	if _shot >= AT.size():
		get_tree().quit()
