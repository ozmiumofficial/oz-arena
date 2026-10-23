class_name Q3Character
extends Node3D
## A remastered id Tech 3 player: one skinned mesh whose legs and torso play independent
## clips, the way the original layered lower.md3 and upper.md3. An AnimationTree blends the
## legs clip with the torso clip through a filter on the torso bones (rig.json "half").
## ragdoll() swaps animation for physics bones built from the skeleton. aim_pitch bends the torso
## and head toward where the player looks, as cg_players.c CG_PlayerAngles turns the torso and
## head models.

@export var model := "keel"
@export var legs_clip := "LEGS_IDLE"
@export var torso_clip := "TORSO_STAND"

var rig: Dictionary
var body: Node3D
var player: AnimationPlayer
var skeleton: Skeleton3D
var tree: AnimationTree
var ragdolled := false
var _legs: AnimationNodeAnimation
var _torso: AnimationNodeAnimation
var _sim: PhysicalBoneSimulator3D
var aim_pitch := 0.0              ## view pitch in degrees, id's way (positive looks down)
var _aim: Aim


## CG_PlayerAngles' pitch: the torso takes three quarters of the view pitch, swinging after it
## (CG_SwingAngles: it starts moving once 15 degrees behind, and stays within 30), and the head
## the rest, so the face looks exactly where the player aims. Applied over the animation,
## turning the torso at the top of the legs (its first bone) and the head at its own bone, about
## the body's left-right axis.
class Aim extends SkeletonModifier3D:
	var character: Q3Character
	var torso_bone := -1
	var head_bone := -1
	var torso_pitch := 0.0
	var _swinging := false

	func _process_modification_with_delta(delta: float) -> void:
		var sk := get_skeleton()
		if sk == null or character == null or character.ragdolled:
			return
		var view := wrapf(character.aim_pitch, -180.0, 180.0)
		var dest := view * 0.75
		var gap := dest - torso_pitch
		if absf(gap) > 15.0:
			_swinging = true
		if _swinging:
			# CG_SwingAngles: 0.1 degree a millisecond, half that near the end, double far off
			var move := delta * 1000.0 * 0.1 * (0.5 if absf(gap) < 7.5 else 1.0 if absf(gap) < 15.0 else 2.0)
			if absf(gap) <= move:
				torso_pitch = dest
				_swinging = false
			else:
				torso_pitch += signf(gap) * move
		torso_pitch = clampf(torso_pitch, dest - 30.0, dest + 30.0)   # never lags more than 30
		var axis := (sk.global_basis.inverse() * character.global_basis.x).normalized()
		_turn(sk, torso_bone, axis, deg_to_rad(torso_pitch))
		_turn(sk, head_bone, axis, deg_to_rad(view - torso_pitch))

	func _turn(sk: Skeleton3D, bone: int, axis: Vector3, angle: float) -> void:
		if bone < 0 or absf(angle) < 0.0001:
			return
		var gp := sk.get_bone_global_pose(bone)
		var parent := sk.get_bone_parent(bone)
		var pg := sk.get_bone_global_pose(parent) if parent >= 0 else Transform3D.IDENTITY
		sk.set_bone_pose(bone, pg.affine_inverse() * Transform3D(Basis(axis, angle) * gp.basis, gp.origin))


func _ready() -> void:
	if body == null:
		load_model(model)


func load_model(m: String) -> void:
	if body:
		body.queue_free()
		body = null
	ragdolled = false
	_sim = null
	model = m
	rig = Q3.character_rig(m)
	var scene := Q3.character_scene(m)
	if scene == null:
		push_warning("Q3Character: no model " + m)
		return
	body = scene.instantiate()
	add_child(body)
	player = body.find_child("AnimationPlayer", true, false)
	skeleton = body.find_child("*Skeleton*", true, false)
	if player == null or skeleton == null:
		push_warning("Q3Character: %s has no AnimationPlayer/Skeleton3D" % m)
		return
	for c in rig.get("clips", []):
		if player.has_animation(c["name"]):
			player.get_animation(c["name"]).loop_mode = Animation.LOOP_LINEAR if c["loop"] else Animation.LOOP_NONE
	_build_tree()
	_build_aim()
	play_legs(legs_clip)
	play_torso(torso_clip)


func _build_tree() -> void:
	var torso_bones := {}
	for b in rig.get("bones", []):
		if b["half"] == "torso":
			torso_bones[b["name"]] = true
	tree = AnimationTree.new()
	tree.name = "LegsTorso"
	body.add_child(tree)
	tree.anim_player = tree.get_path_to(player)
	var bt := AnimationNodeBlendTree.new()
	_legs = AnimationNodeAnimation.new()
	_torso = AnimationNodeAnimation.new()
	var mix := AnimationNodeBlend2.new()
	mix.filter_enabled = true
	# every clip keys every bone, so any clip's track list gives the bone paths
	var probe := player.get_animation(player.get_animation_list()[0])
	for i in probe.get_track_count():
		var p := probe.track_get_path(i)
		if torso_bones.has(String(p.get_concatenated_subnames())):
			mix.set_filter_path(p, true)
	bt.add_node("legs", _legs, Vector2(0, 0))
	bt.add_node("seek_legs", AnimationNodeTimeSeek.new(), Vector2(200, 0))
	bt.add_node("torso", _torso, Vector2(0, 200))
	bt.add_node("seek_torso", AnimationNodeTimeSeek.new(), Vector2(200, 200))
	bt.add_node("mix", mix, Vector2(400, 100))
	bt.connect_node("seek_legs", 0, "legs")
	bt.connect_node("seek_torso", 0, "torso")
	bt.connect_node("mix", 0, "seek_legs")
	bt.connect_node("mix", 1, "seek_torso")
	bt.connect_node("output", 0, "mix")
	tree.tree_root = bt
	tree.set("parameters/mix/blend_amount", 1.0)
	tree.active = true


## LEGS_* clip, or a BOTH_* clip which drives the whole body.
func _build_aim() -> void:
	_aim = Aim.new()
	_aim.name = "Aim"
	_aim.character = self
	var half := {}
	for b in rig.get("bones", []):
		half[String(b["name"])] = b["half"]
	for b in rig.get("bones", []):
		# the torso's first bone hangs from a legs bone (tag_torso)
		if b["half"] == "torso" and half.get(String(b.get("parent", "")), "") != "torso" and _aim.torso_bone < 0:
			_aim.torso_bone = skeleton.find_bone(String(b["name"]))
	_aim.head_bone = skeleton.find_bone("head")
	skeleton.add_child(_aim)


func play_legs(clip: String) -> void:
	if _legs == null or not player.has_animation(clip):
		return
	legs_clip = clip
	_legs.animation = clip
	tree.set("parameters/seek_legs/seek_request", 0.0)
	if clip.begins_with("BOTH"):
		_torso.animation = clip
		tree.set("parameters/seek_torso/seek_request", 0.0)


func play_torso(clip: String) -> void:
	if _torso == null or not player.has_animation(clip) or legs_clip.begins_with("BOTH"):
		return
	torso_clip = clip
	_torso.animation = clip
	tree.set("parameters/seek_torso/seek_request", 0.0)


func clips(prefix: String) -> PackedStringArray:
	var out: PackedStringArray = []
	for c in rig.get("clips", []):
		if String(c["name"]).begins_with(prefix):
			out.append(c["name"])
	return out


## Attaches a prop at a tag, the way cgame attaches the weapon at tag_weapon: the model's own axes
## line up with the tag's. The bone only keeps the tag's position and forward axis (its roll is
## arbitrary), so the full rest frame comes from rig.json "tags" (tools/rig_extras.py).
func attach(prop_id: String, tag := "weapon") -> Node3D:
	if skeleton == null or skeleton.find_bone(tag) < 0:
		return null
	var n := Q3.instance_prop(prop_id)
	if n == null:
		return null
	var att := BoneAttachment3D.new()
	att.name = "Attach_" + tag
	att.bone_name = tag
	skeleton.add_child(att)
	att.add_child(n)
	var t: Dictionary = rig.get("tags", {}).get(tag, {})
	if t.has("x"):
		var v := func(a: Array) -> Vector3: return Vector3(a[0], a[1], a[2])
		# a prop's local axes are id's (y, z, x), so its basis columns are the tag's y, z, x
		var frame := Basis(v.call(t["y"]), v.call(t["z"]), v.call(t["x"])).orthonormalized()
		n.basis = skeleton.get_bone_global_rest(skeleton.find_bone(tag)).basis.orthonormalized().inverse() * frame
	return n


## A corpse burning away (fragged bodies, when they're done lying there): every surface becomes
## shaders/character_dissolve.gdshader with its own textures, the noise eats it from 0 to 1 over
## `seconds` with glowing edges, embers rise off the body, then the character frees itself.
## Additive surfaces (Bones' x-ray) fade out instead.
func disintegrate(seconds := 1.6) -> void:
	if body == null:
		queue_free()
		return
	var mats: Array[ShaderMaterial] = []
	var fades: Array = []
	var shader := load("res://shaders/character_dissolve.gdshader") as Shader
	for mi in body.find_children("*", "MeshInstance3D", true, false):
		var m := mi as MeshInstance3D
		if m.mesh == null or String(m.get_path()).contains("Attach_"):
			continue
		m.material_overlay = null   # no armor shell on a corpse
		for i in m.mesh.get_surface_count():
			var src := m.get_active_material(i) as StandardMaterial3D
			if src == null:
				continue
			if src.transparency == BaseMaterial3D.TRANSPARENCY_ALPHA or src.blend_mode != BaseMaterial3D.BLEND_MODE_MIX:
				var fade := src.duplicate() as StandardMaterial3D
				m.set_surface_override_material(i, fade)
				fades.append(fade)
				continue
			var d := ShaderMaterial.new()
			d.shader = shader
			d.set_shader_parameter("albedo_tex", src.albedo_texture)
			d.set_shader_parameter("albedo_color", src.albedo_color)
			d.set_shader_parameter("use_normal", src.normal_enabled and src.normal_texture != null)
			d.set_shader_parameter("normal_tex", src.normal_texture)
			d.set_shader_parameter("use_orm", src.roughness_texture != null)
			d.set_shader_parameter("orm_tex", src.roughness_texture)
			d.set_shader_parameter("roughness", src.roughness)
			d.set_shader_parameter("metallic", src.metallic)
			if src.emission_enabled:
				d.set_shader_parameter("emission_tex", src.emission_texture)
				d.set_shader_parameter("emission_color", Vector3(src.emission.r, src.emission.g, src.emission.b))
				d.set_shader_parameter("emission_energy", src.emission_energy_multiplier)
			if src.transparency == BaseMaterial3D.TRANSPARENCY_ALPHA_SCISSOR:
				d.set_shader_parameter("alpha_scissor", src.alpha_scissor_threshold)
			m.set_surface_override_material(i, d)
			mats.append(d)
	_embers(seconds)
	var burn := func(v: float) -> void:
		for d in mats:
			d.set_shader_parameter("dissolve", v)
		for f in fades:
			var fm := f as StandardMaterial3D
			var c := fm.albedo_color
			c.a = 1.0 - v
			fm.albedo_color = c
	var tw := create_tween()
	tw.tween_method(burn, 0.0, 1.0, seconds).set_ease(Tween.EASE_IN)
	tw.tween_callback(queue_free)


## Embers off a burning body: orange sparks drifting up from where its parts lie.
func _embers(seconds: float) -> void:
	var at := global_position
	var n := 0
	if _sim:
		var sum := Vector3.ZERO
		for pb in _sim.get_children():
			if pb is PhysicalBone3D:
				sum += (pb as PhysicalBone3D).global_position
				n += 1
		if n:
			at = sum / n
	var p := GPUParticles3D.new()
	p.amount = 36
	p.lifetime = 1.1
	p.one_shot = false
	p.explosiveness = 0.0
	var pm := ParticleProcessMaterial.new()
	pm.emission_shape = ParticleProcessMaterial.EMISSION_SHAPE_BOX
	pm.emission_box_extents = Vector3(0.45, 0.12, 0.45)
	pm.direction = Vector3.UP
	pm.spread = 25.0
	pm.initial_velocity_min = 0.4
	pm.initial_velocity_max = 1.4
	pm.gravity = Vector3(0, 1.2, 0)
	pm.scale_min = 0.02
	pm.scale_max = 0.045
	var g := Gradient.new()
	g.offsets = PackedFloat32Array([0.0, 0.5, 1.0])
	g.colors = PackedColorArray([Color(1.0, 0.6, 0.2, 0.9), Color(0.9, 0.25, 0.03, 0.7), Color(0.5, 0.06, 0.0, 0.0)])
	var gt := GradientTexture1D.new()
	gt.gradient = g
	pm.color_ramp = gt
	p.process_material = pm
	var quad := QuadMesh.new()
	var mat := StandardMaterial3D.new()
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mat.billboard_mode = BaseMaterial3D.BILLBOARD_PARTICLES
	mat.billboard_keep_scale = true   # else the particle's scale is dropped and every quad is 1 m
	mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	mat.blend_mode = BaseMaterial3D.BLEND_MODE_ADD
	mat.vertex_color_use_as_albedo = true
	mat.albedo_texture = Q3.fx_texture("flare")
	quad.material = mat
	p.draw_pass_1 = quad
	p.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	# the embers outlive the body by their lifetime, so they hang off the parent
	var host := get_parent()
	host.add_child(p)
	p.global_position = at
	p.emitting = true
	get_tree().create_timer(seconds * 0.85).timeout.connect(func():
		if is_instance_valid(p):
			p.emitting = false)
	get_tree().create_timer(seconds + p.lifetime + 0.2).timeout.connect(func():
		if is_instance_valid(p):
			p.queue_free())


## Physics takes over from animation. `impulse` is a velocity change (m/s) for every part.
func ragdoll(impulse := Vector3.ZERO) -> void:
	if skeleton == null or ragdolled:
		return
	if _sim == null:
		_build_ragdoll()
	tree.active = false
	_sim.physical_bones_start_simulation()
	# every part keeps the body's momentum: impulse is per kilogram here
	for pb in _sim.get_children():
		if pb is PhysicalBone3D:
			pb.apply_central_impulse(impulse * pb.mass)
	ragdolled = true


func stand_up() -> void:
	if _sim and ragdolled:
		_sim.physical_bones_stop_simulation()
		tree.active = true
		ragdolled = false


## Bodies only for the parts a body bends at, by the rig's bone names (tools/fit_rig.py): the
## extra bones the fit adds (belly_2, toe_R_2 ...) ride along with their parent, so nothing
## stretches. Weight is spread like a person's (about 85 kg), and every joint has limits for its
## part (the spine segments about 45 degrees together, hips 50, knees 60), so a body falls limp
## instead of folding into a ball. Each body sits along its bone with the joint at the bone's
## start, the way Godot's own "Create Physical Skeleton" builds them, with the cone joint's twist
## axis along the bone.
const RAGDOLL_PARTS := {
	# base name: [mass kg, swing span deg, twist span deg, radius scale]
	"pelvis": [14.0, 8.0, 8.0, 1.4], "waist": [6.0, 10.0, 6.0, 1.3], "spine": [8.0, 12.0, 8.0, 1.3],
	"belly": [8.0, 12.0, 8.0, 1.3], "chest": [12.0, 10.0, 8.0, 1.4], "neck": [2.0, 25.0, 20.0, 0.8],
	"head": [5.0, 30.0, 25.0, 1.3], "thigh": [9.0, 50.0, 10.0, 1.0], "calf": [4.5, 60.0, 4.0, 0.9],
	"foot": [1.2, 25.0, 4.0, 0.8], "upperarm": [2.8, 70.0, 30.0, 0.9], "forearm": [1.8, 60.0, 8.0, 0.8],
	"hand": [0.6, 35.0, 8.0, 0.8],
}


static var _radii_cache := {}   ## model -> {bone index: capsule radius}


## How thick the mesh is around each bone: of the vertices a bone carries most, the distance
## three quarters of them stay within from the bone's segment (bone to child), in the bind pose.
## Q3's bulky models are thicker than their bone lengths suggest. Cached per model.
func _mesh_radii() -> Dictionary:
	if _radii_cache.has(model):
		return _radii_cache[model]
	var dists := {}
	for mi in body.find_children("*", "MeshInstance3D", true, false):
		var m := mi as MeshInstance3D
		if m.skin == null or m.mesh == null:
			continue
		var bind_bone: Array[int] = []
		for b in m.skin.get_bind_count():
			var bn := String(m.skin.get_bind_name(b))
			bind_bone.append(skeleton.find_bone(bn) if bn != "" else m.skin.get_bind_bone(b))
		for si in m.mesh.get_surface_count():
			var arr := m.mesh.surface_get_arrays(si)
			var verts: PackedVector3Array = arr[Mesh.ARRAY_VERTEX]
			var bones: PackedInt32Array = arr[Mesh.ARRAY_BONES] if arr[Mesh.ARRAY_BONES] != null else PackedInt32Array()
			var weights: PackedFloat32Array = arr[Mesh.ARRAY_WEIGHTS] if arr[Mesh.ARRAY_WEIGHTS] != null else PackedFloat32Array()
			if bones.is_empty() or weights.size() != bones.size():
				continue
			var per := bones.size() / verts.size()
			for v in verts.size():
				var best := 0
				for k in per:
					if weights[v * per + k] > weights[v * per + best]:
						best = k
				var bind := bones[v * per + best]
				if bind >= bind_bone.size() or bind_bone[bind] < 0:
					continue
				var bone := bind_bone[bind]
				var local := m.skin.get_bind_pose(bind) * verts[v]
				var tip := Vector3.ZERO
				for kid in skeleton.get_bone_children(bone):
					var off := skeleton.get_bone_rest(kid).origin
					if off.length() > 0.03:
						tip = off
						break
				var t := clampf(local.dot(tip) / tip.length_squared(), 0.0, 1.0) if tip != Vector3.ZERO else 0.0
				if not dists.has(bone):
					dists[bone] = PackedFloat32Array()
				dists[bone].append(local.distance_to(tip * t))
	var out := {}
	for bone in dists:
		var d: PackedFloat32Array = dists[bone]
		if d.size() >= 8:
			d.sort()
			out[bone] = d[int(d.size() * 0.75)]
	_radii_cache[model] = out
	return out


static func _ragdoll_part(bname: String) -> String:
	var base := bname.get_slice("_", 0)
	return base if RAGDOLL_PARTS.has(base) else ""


func _build_ragdoll() -> void:
	_sim = PhysicalBoneSimulator3D.new()
	skeleton.add_child(_sim)
	var radii := _mesh_radii()
	for i in skeleton.get_bone_count():
		var bname := skeleton.get_bone_name(i)
		var part := _ragdoll_part(bname)
		# numbered extras (belly_2, upperarm_R_2 ...) only when a main part hangs from them
		if part == "" or (bname.get_slice("_", bname.get_slice_count("_") - 1).is_valid_int() and not _leads_to_part(i)):
			continue
		var spec: Array = RAGDOLL_PARTS[part]
		# the body runs from this bone to its child (or a short stub at the chain's end)
		var length := 0.12
		var dir := Vector3.UP
		var kids := skeleton.get_bone_children(i)
		for k in kids:
			var off := skeleton.get_bone_rest(k).origin
			if off.length() > 0.03:
				length = off.length()
				dir = off / length
				break
		var half := length * 0.5
		var up := Vector3.UP if absf(dir.dot(Vector3.UP)) < 0.95 else Vector3.BACK
		var body := Transform3D(Basis.looking_at(dir, up), dir * half)   # body -Z along the bone
		var pb := PhysicalBone3D.new()
		pb.name = "Phys_" + bname
		pb.bone_name = bname
		pb.body_offset = body
		pb.mass = float(spec[0])
		pb.friction = 1.0
		pb.bounce = 0.0
		pb.linear_damp = 0.05
		pb.angular_damp = 0.6
		pb.collision_layer = 2   # ragdoll bones collide with the world (layer 1), not each other
		pb.collision_mask = 1
		var parent := skeleton.get_bone_parent(i)
		if parent >= 0 and skeleton.get_bone_name(parent) != "root" and part == "calf":
			# knees bend one way only: a hinge about the body's side-to-side axis (skeleton X, the
			# model's left), 0-130 degrees, the calf swinging back
			var side := (skeleton.get_bone_global_rest(i).basis.inverse() * Vector3.RIGHT)
			var z := (body.basis.inverse() * side).normalized()
			var x := (Vector3.FORWARD - z * Vector3.FORWARD.dot(z)).normalized()
			pb.joint_type = PhysicalBone3D.JOINT_TYPE_HINGE
			pb.joint_offset = Transform3D(Basis(x, z.cross(x), z), Vector3(0, 0, half))
			pb.set("joint_constraints/angular_limit_enabled", true)
			pb.set("joint_constraints/angular_limit_lower", -130.0)
			pb.set("joint_constraints/angular_limit_upper", 0.0)
		elif parent >= 0 and skeleton.get_bone_name(parent) != "root":
			pb.joint_type = PhysicalBone3D.JOINT_TYPE_CONE
			# the joint at the bone's start, its twist axis (X) turned onto the bone (-Z)
			pb.joint_offset = Transform3D(Basis(Vector3.UP, PI * 0.5), Vector3(0, 0, half))
			pb.set("joint_constraints/swing_span", float(spec[1]))
			pb.set("joint_constraints/twist_span", float(spec[2]))
		var cap := CapsuleShape3D.new()
		# as thick as the mesh around the bone, so no limb sinks into the floor it lies on
		cap.radius = clampf(radii.get(i, length * 0.22 * float(spec[3])), 0.035, 0.22)
		cap.height = maxf(length, cap.radius * 2.0 + 0.01)
		var shape := CollisionShape3D.new()
		shape.shape = cap
		shape.transform = Transform3D(Basis(Vector3.RIGHT, PI * 0.5), Vector3.ZERO)   # capsule height along -Z
		pb.add_child(shape)
		_sim.add_child(pb)


## Whether a main ragdoll part hangs below this bone (so a numbered extra in a limb chain, such
## as upperarm_R_2 above forearm_R, gets a body and keeps the chain together).
func _leads_to_part(i: int) -> bool:
	for k in skeleton.get_bone_children(i):
		var n := skeleton.get_bone_name(k)
		if _ragdoll_part(n) != "" and not n.get_slice("_", n.get_slice_count("_") - 1).is_valid_int():
			return true
		if _leads_to_part(k):
			return true
	return false

