extends Node3D
## Windowed check of the ragdolls: four very different player models (a human, Klesk's long
## limbs, Orbb's two legs and one eye, Uriel's wings) fragged on a floor, two of them thrown
## sideways like a rocket hit, saved at 0.25, 0.7, 1.5 and 3 s.
##   godot --path . res://tests/ragdoll_test.tscn -- out=C:/temp/rag
## models=a,b,c,d picks the bodies; shapes=1 draws the physics capsules over them; push=x,y,z
## is seq's shove (m/s, default 2.5,0.5,0: toward the body's front); wall=1 puts a 16-unit wall
## 0.7 m in front of seq's body (a rocket into a wall: nothing may end up behind it).
## Prints "RAGDOLL done" plus each body's spread (how far its farthest part ended from its
## pelvis, a stretch check), then a close shot of each body from above.

var _models := ["sarge", "klesk", "orbb", "uriel"]
const SHOTS := [0.25, 0.7, 1.5, 3.0]
var out := "user://rag"
var _chars: Array[Q3Character] = []
var _t := 0.0
var _shot := 0
var _standing := {}
var _burn := false        ## burn=1: after the shots, the bodies disintegrate (frames <out>_burn_<n>)
var _seq := ""            ## seq=<model>: that model alone from the side, a frame every 0.1 s
var _shapes := false
var _push := Vector3(2.5, 0.5, 0.0)
var _wall := false


## Head to the farther foot (a curled-up body has its feet by its head), from the physics bodies
## once it's down, from the animated pose while standing.
func _reach(ch: Q3Character) -> float:
	var pos := {}
	for pb in ch.find_children("Phys_*", "PhysicalBone3D", true, false):
		pos[String((pb as PhysicalBone3D).bone_name)] = (pb as PhysicalBone3D).global_position
	var sk := ch.skeleton
	if pos.is_empty() or not ch.ragdolled:
		for b in sk.get_bone_count():
			pos[sk.get_bone_name(b)] = sk.global_transform * sk.get_bone_global_pose(b).origin
	if not pos.has("head"):
		return 0.0
	var best := 0.0
	for k in pos:
		if String(k).begins_with("foot") or String(k).begins_with("calf"):
			best = maxf(best, (pos[k] as Vector3).distance_to(pos["head"]))
	return best


func _ready() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("out="):
			out = a.substr(4)
		if a.begins_with("seq="):
			_seq = a.substr(4)
		if a == "burn=1":
			_burn = true
		if a.begins_with("models="):
			_models = a.substr(7).split(",")
		if a == "shapes=1":
			_shapes = true
		if a == "wall=1":
			_wall = true
		if a.begins_with("push="):
			var v := a.substr(5).split_floats(",")
			_push = Vector3(v[0], v[1], v[2])
	Engine.max_fps = 60
	var floor := StaticBody3D.new()
	var shape := CollisionShape3D.new()
	var box := BoxShape3D.new()
	box.size = Vector3(40, 1, 40)
	shape.shape = box
	shape.position.y = -0.5
	floor.add_child(shape)
	var mesh := MeshInstance3D.new()
	var pm := PlaneMesh.new()
	pm.size = Vector2(40, 40)
	mesh.mesh = pm
	floor.add_child(mesh)
	add_child(floor)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-50, 30, 0)
	add_child(sun)
	var env := WorldEnvironment.new()
	env.environment = Environment.new()
	env.environment.background_mode = Environment.BG_COLOR
	env.environment.background_color = Color(0.2, 0.22, 0.25)
	env.environment.ambient_light_color = Color(0.6, 0.6, 0.6)
	env.environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	add_child(env)
	var cam := Camera3D.new()
	cam.position = Vector3(0, 3.2, 6.5)
	cam.rotation_degrees = Vector3(-24, 0, 0)
	add_child(cam)
	var models: Array = [_seq] if _seq != "" else _models
	for i in models.size():
		var ch := Q3Character.new()
		ch.model = models[i]
		add_child(ch)
		ch.position = Vector3(-3.6 + i * 2.4, 0, 0) if _seq == "" else Vector3.ZERO
		ch.rotation.y = PI * 0.5 if _seq != "" else 0.0
		_chars.append(ch)
	if _wall:
		var wall := StaticBody3D.new()
		var ws := CollisionShape3D.new()
		var wb := BoxShape3D.new()
		wb.size = Vector3(0.5, 3.0, 4.0)
		ws.shape = wb
		wall.add_child(ws)
		var wm := MeshInstance3D.new()
		var bm := BoxMesh.new()
		bm.size = wb.size
		wm.mesh = bm
		wall.add_child(wm)
		wall.position = Vector3(0.95, 1.5, 0.0)
		add_child(wall)
	if _seq != "":
		cam.position = Vector3(0.4, 1.1, 3.0)
		cam.rotation_degrees = Vector3(-12, 0, 0)


func _process(delta: float) -> void:
	_t += delta
	if _seq != "":
		_sequence()
		return
	if _shot >= SHOTS.size():
		return   # the bodies may be burning away
	if _t > 0.5 and not _chars[0].ragdolled:
		for ch in _chars:
			_standing[ch.model] = _reach(ch)
		for i in _chars.size():
			# two fall where they stand, two are thrown like a rocket hit
			_chars[i].ragdoll(Vector3(4.0, 3.0, -1.0) if i % 2 else Vector3.ZERO)
			_describe(_chars[i])
	if _shot < SHOTS.size() and _t > 0.5 + SHOTS[_shot]:
		await RenderingServer.frame_post_draw
		get_viewport().get_texture().get_image().save_png("%s_%d.png" % [out, _shot])
		_shot += 1
		if _shot == SHOTS.size():
			for ch in _chars:
				# from the physics bodies themselves: a body that flew apart shows here
				var pelvis := (ch.find_child("Phys_pelvis", true, false) as Node3D).global_position
				var far := 0.0
				for pb in ch.find_children("Phys_*", "PhysicalBone3D", true, false):
					far = maxf(far, (pb as Node3D).global_position.distance_to(pelvis))
				print("%s: farthest part %.2f m from the pelvis%s; head to feet %.2f m standing, %.2f m down" % [ch.model, far, " (FLEW APART)" if far > 1.6 else "", _standing[ch.model], _reach(ch)])
			# a close look at each body from above
			var cam := get_viewport().get_camera_3d()
			for i in _chars.size():
				var at := _chars[i].body_center()
				cam.global_position = at + Vector3(0, 2.6, 0.9)
				cam.look_at(at)
				await RenderingServer.frame_post_draw
				await RenderingServer.frame_post_draw
				get_viewport().get_texture().get_image().save_png("%s_top_%s.png" % [out, _models[i]])
			if _burn:
				cam.position = Vector3(0, 3.2, 6.5)
				cam.rotation_degrees = Vector3(-24, 0, 0)
				cam.position = Vector3(0, 1.6, 3.6)
				cam.rotation_degrees = Vector3(-20, 0, 0)
				for ch in _chars:
					ch.disintegrate(1.6)
				for k in 6:
					await get_tree().create_timer(0.3).timeout
					await RenderingServer.frame_post_draw
					get_viewport().get_texture().get_image().save_png("%s_burn_%d.png" % [out, k])
			print("RAGDOLL done")
			get_tree().quit()


var _frame := 0


func _sequence() -> void:
	if _t > 0.5 and not _chars[0].ragdolled:
		_chars[0].ragdoll(_push)
		_describe(_chars[0])
	if _t > 0.5 + _frame * 0.1 and _frame < 16:
		_frame += 1
		await RenderingServer.frame_post_draw
		get_viewport().get_texture().get_image().save_png("%s_seq_%02d.png" % [out, _frame])
	elif _frame == 16:
		_frame += 1
		# and the body as it lies, from above
		var cam := get_viewport().get_camera_3d()
		var at := _chars[0].body_center()
		cam.global_position = at + Vector3(0, 2.0, 0.6)
		cam.look_at(at)
		await RenderingServer.frame_post_draw
		await RenderingServer.frame_post_draw
		get_viewport().get_texture().get_image().save_png("%s_seq_top.png" % out)
		if _wall:
			for pb in _chars[0].find_children("Phys_*", "PhysicalBone3D", true, false):
				if (pb as Node3D).global_position.x > 0.7:
					print("RAGDOLL through the wall: ", (pb as PhysicalBone3D).bone_name)
		print("RAGDOLL done")
		get_tree().quit()


## Lists the body's parts and their joints, and with shapes=1 draws each capsule.
func _describe(ch: Q3Character) -> void:
	var line := PackedStringArray()
	for pb in ch.find_children("Phys_*", "PhysicalBone3D", true, false):
		var b := pb as PhysicalBone3D
		var kind := "root" if b.joint_type == PhysicalBone3D.JOINT_TYPE_NONE else "hinge %d..%d" % [int(b.get("joint_constraints/angular_limit_lower")), int(b.get("joint_constraints/angular_limit_upper"))] if b.joint_type == PhysicalBone3D.JOINT_TYPE_HINGE else "cone %d" % int(b.get("joint_constraints/swing_span"))
		line.append("%s %.1fkg %s" % [String(b.bone_name), b.mass, kind])
		if _shapes:
			var cs := b.get_child(0) as CollisionShape3D
			var cap := cs.shape as CapsuleShape3D
			var mi := MeshInstance3D.new()
			var cm := CapsuleMesh.new()
			cm.radius = cap.radius
			cm.height = cap.height
			var mat := StandardMaterial3D.new()
			mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
			mat.albedo_color = Color(0.2, 0.8, 1.0, 0.35) if kind.begins_with("cone") else Color(1.0, 0.5, 0.1, 0.5)
			mat.no_depth_test = true
			mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
			cm.material = mat
			mi.mesh = cm
			mi.transform = cs.transform
			b.add_child(mi)
	print("%s: %d parts: %s" % [ch.model, line.size(), ", ".join(line)])
