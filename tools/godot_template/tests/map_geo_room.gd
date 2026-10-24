extends Node3D
## A small room built from editor brushes with the remastered world materials: a rotated pillar, a
## doorway cut with subtract, a tinted pane, a texture-locked block and a curved patch. For checking
## Q3MapMesher's output by eye:
##   godot --path . res://tests/capture.tscn -- scene=res://tests/map_geo_room.tscn out=C:/temp/room count=1


func _ready() -> void:
	var doc := Q3MapDoc.new()
	var floor_mat := "textures/base_floor/clangdark"
	var wall := "textures/gothic_block/blocks15"
	var trim := "textures/base_trim/dark_tin2"
	# floor, ceiling, walls (512 x 512 x 256 inside)
	doc.add(Q3MapDoc.box_brush(Vector3(-272, -272, -16), Vector3(272, 272, 0), floor_mat))
	doc.add(Q3MapDoc.box_brush(Vector3(-272, -272, 256), Vector3(272, 272, 272), wall))
	doc.add(Q3MapDoc.box_brush(Vector3(-272, -272, 0), Vector3(-256, 272, 256), wall))
	doc.add(Q3MapDoc.box_brush(Vector3(256, -272, 0), Vector3(272, 272, 256), wall))
	doc.add(Q3MapDoc.box_brush(Vector3(-256, 256, 0), Vector3(256, 272, 256), wall))
	# the back wall with a doorway subtracted
	var back := Q3MapDoc.box_brush(Vector3(-256, -272, 0), Vector3(256, -256, 256), wall)
	var door := Q3MapGeo.box(PackedFloat64Array([-48, -300, 0]), PackedFloat64Array([48, -200, 128]))
	var cut: Dictionary = Q3MapGeo.subtract(back["planes"], door)
	for p in cut["pieces"]:
		var piece := Q3MapDoc.brush(p["planes"], wall)
		for i in p["attr"].size():
			piece["faces"][i] = back["faces"][p["attr"][i]].duplicate(true)
		doc.add(piece)
	# a pillar turned 30 degrees, and a block with texture lock turned 45
	var pillar := Q3MapDoc.box_brush(Vector3(-32, -32, 0), Vector3(32, 32, 256), trim)
	doc.add(Q3MapDoc.transform_brush(pillar, Q3MapDoc.yaw_rotation(30, Vector3.ZERO)))
	var block := Q3MapDoc.box_brush(Vector3(96, 64, 0), Vector3(192, 160, 96), floor_mat)
	for f in block["faces"]:
		f["lock"] = true
	doc.add(Q3MapDoc.transform_brush(block, Q3MapDoc.yaw_rotation(45, Vector3(144, 112, 0))))
	# a tinted pane
	var pane := Q3MapDoc.box_brush(Vector3(-200, 100, 0), Vector3(-196, 220, 160), wall)
	for f in pane["faces"]:
		f["color"] = Color(0.6, 0.8, 1.0)
	doc.add(pane)
	# a curved patch: a quarter pipe along the floor-wall edge
	var ctrl := PackedFloat64Array()
	for j in 3:
		for i in 3:
			var x := -256.0 + i * 256.0
			var pts := [Vector2(192, 0), Vector2(256, 0), Vector2(256, 64)]   # (y, z): the floor, the corner, up the wall
			var p: Vector2 = pts[j]
			ctrl.append_array([x, p.x, p.y, i * 1.0, j * 0.5])
	doc.add({"type": "patch", "id": 0, "entity": 0, "group": 0, "width": 3, "height": 3, "ctrl": ctrl, "material": wall})

	var mesher := Q3MapMesher.new()
	add_child(mesher)
	mesher.bind(doc)

	var env := WorldEnvironment.new()
	env.environment = Environment.new()
	env.environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.environment.ambient_light_color = Color(0.25, 0.25, 0.28)
	env.environment.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	add_child(env)
	for p in [Vector3(-3, 6, 3), Vector3(4, 5, -4), Vector3(0, 7, -7)]:
		var l := OmniLight3D.new()
		l.position = p
		l.omni_range = 14
		l.light_energy = 2.5
		l.shadow_enabled = true
		add_child(l)
	var cam := Camera3D.new()
	cam.fov = 80
	add_child(cam)
	# Godot space: x = id Y, y = id Z, z = id X; meters. view=patch looks at the curved floor edge.
	if "view=patch" in OS.get_cmdline_user_args():
		cam.position = Vector3(3.0, 2.2, -4.0)
		cam.look_at(Vector3(8.0, 0.3, 1.0))
	else:
		cam.position = Vector3(-6.5, 4.5, -6.5)
		cam.look_at(Vector3(1.5, 1.5, 1.5))
	cam.current = true
