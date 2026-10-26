class_name Q3Character
extends Node3D
## A remastered id Tech 3 player: one skinned mesh whose legs and torso play independent
## clips, the way the original layered lower.md3 and upper.md3. An AnimationTree blends the
## legs clip with the torso clip through a filter on the torso bones (rig.json "half").
## ragdoll() swaps animation for physics bones built from the skeleton. The aim_* values turn the
## legs, torso and head toward where the player looks, as cg_players.c CG_PlayerAngles turns the
## legs, torso and head models.

@export var model := "keel"
## Quake 3's skin: "default", a team's "red" or "blue", or an alternate the model ships (Daemia is
## major's "daemia"). rig.json "skins" (tools/build_skins.py) says which materials it repaints.
@export var skin := "default"
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
## CG_PlayerAngles' "always center": while the legs aren't idle or the torso isn't standing, the
## torso and legs keep easing to their angles instead of waiting until they're far behind
var aim_center := false
var aim_roll := 0.0               ## CG_AddPainTwitch: the torso's roll in degrees (the head stays level)
## CG_PlayerAngles' yaw, with `turn` on (players in a match; anything else keeps the node's own yaw):
## the legs face the view yaw (aim_yaw) turned by the movement direction (move_dir, es.angles2[YAW]:
## 22 or 45 degrees off while strafing), the torso a quarter of that turn, the head the aim, each
## swinging after its angle. The node itself turns with the legs.
var turn := false
var aim_yaw := 0.0
var move_dir := 0
var velocity := Vector3.ZERO      ## id units a second (es.pos.trDelta): the legs lean into it
## Match bodies run on the match's clock: Q3Client calls advance() with how far it moved, so they
## stop behind the pause and in a paused replay, and follow the replay's speed. Anything else (the
## menu showcase, card renders, tests) animates on idle time.
var match_clock := false:
	set(on):
		match_clock = on
		_clock_mode()
var _aim: Aim
var _ease: Ease
var _yaw_legs := Swing.new()
var _yaw_torso := Swing.new()
var _pitch_torso := Swing.new()
var _lean := Vector2.ZERO         ## the legs' lean into the direction of travel: pitch, roll (degrees)
var _shown_legs := ""             ## the legs clip playing: legs_clip, or LEGS_TURN while the legs shuffle round
var _clip_fps := {}               ## clip -> frames a second (rig.json)
const MOVE_OFFSETS := [0.0, 22.0, 45.0, -22.0, 0.0, 22.0, -45.0, -22.0]   ## CG_PlayerAngles movementOffsets
const FILL_LAYER := 1 << 20       ## render layer 21: the match's player fill lights (Q3Client) light only this
const SWING_SPEED := 0.3          ## cg_swingSpeed: degrees a millisecond
const LEAN_MAX := 20.0            ## degrees: the lean into the direction of travel never goes past this (running is 16)
const LEAN_EASE_MS := 120.0       ## how fast the lean follows the velocity (a hit's knockback mustn't snap it)
var _bright := Color(0, 0, 0, 0)  ## the bright skin color drawn (alpha 0: none)
static var _skin_mats := {}       ## "model/skin/stem" -> WeakRef to the material bodies share


## One of cgame's swinging angles (lerpFrame_t's yawAngle and yawing, or pitchAngle and pitching).
class Swing:
	var angle := 0.0
	var swinging := false

	## cg_players.c CG_SwingAngles: starts after `dest` (degrees) once more than `tolerance` off,
	## moves `speed` degrees a millisecond (half that within half the tolerance, double beyond it)
	## and never stays more than `limit` behind.
	func step(dest: float, tolerance: float, limit: float, speed: float, ms: float) -> void:
		if not swinging and absf(wrapf(angle - dest, -180.0, 180.0)) > tolerance:
			swinging = true
		if not swinging:
			return
		var gap := wrapf(dest - angle, -180.0, 180.0)
		var move := ms * speed * (0.5 if absf(gap) < tolerance * 0.5 else 1.0 if absf(gap) < tolerance else 2.0)
		if move >= absf(gap):
			angle = dest
			swinging = false
		else:
			angle += signf(gap) * move
		gap = wrapf(dest - angle, -180.0, 180.0)
		if gap > limit:
			angle = dest - (limit - 1.0)
		elif gap < -limit:
			angle = dest + (limit - 1.0)
		angle = wrapf(angle, -180.0, 180.0)


## CG_PlayerAngles applied over the animation. The node already faces the legs' yaw. The legs lean
## about the player's origin (24 units over the feet), turning the whole skeleton; the torso,
## turned at its first bone (the top of the legs), stands back up and takes its own yaw, three
## quarters of the view pitch and the pain twitch's roll; the head, at its own bone, looks exactly
## where the player aims.
class Aim extends SkeletonModifier3D:
	var character: Q3Character
	var torso_bone := -1
	var head_bone := -1

	func _process_modification_with_delta(_delta: float) -> void:
		var sk := get_skeleton()
		var c := character
		if sk == null or c == null or c.ragdolled:
			return
		# character space (x left, y up, z forward: id's axes turned to the legs' yaw) to skeleton space
		var q := (sk.global_basis.inverse() * c.global_basis).orthonormalized()
		var qi := q.inverse()
		var lean := q * Q3Character.axes(c._lean.x, 0.0, c._lean.y) * qi
		var torso := q * Q3Character.axes(c._pitch_torso.angle, c.torso_turn(), c.aim_roll) * qi
		var head := q * Q3Character.axes(wrapf(c.aim_pitch, -180.0, 180.0), c.head_turn(), 0.0) * qi
		if c._lean != Vector2.ZERO:
			var pivot := sk.global_transform.affine_inverse() * (c.global_transform * Vector3(0.0, 24.0 * Q3.UNIT, 0.0))
			for b in sk.get_parentless_bones():
				var gp := sk.get_bone_global_pose(b)
				sk.set_bone_pose(b, Transform3D(lean * gp.basis, pivot + lean * (gp.origin - pivot)))
		_turn(sk, torso_bone, torso * lean.inverse())
		_turn(sk, head_bone, head * torso.inverse())

	## Turns a bone (and what hangs from it) by `r` (skeleton space) about its own origin.
	func _turn(sk: Skeleton3D, bone: int, r: Basis) -> void:
		if bone < 0:
			return
		var gp := sk.get_bone_global_pose(bone)
		var parent := sk.get_bone_parent(bone)
		var pg := sk.get_bone_global_pose(parent) if parent >= 0 else Transform3D.IDENTITY
		sk.set_bone_pose(bone, pg.affine_inverse() * Transform3D(r * gp.basis, gp.origin))


## CG_SetLerpFrameAnimation's initialLerp: a new clip doesn't snap in. Its half of the body eases
## from the pose last drawn into the clip over one of the clip's frame times (1000 / fps ms) of
## the character's clock. Runs before Aim, which turns the eased pose.
class Ease extends SkeletonModifier3D:
	var character: Q3Character
	var bones := {"legs": PackedInt32Array(), "torso": PackedInt32Array()}
	var _eases := {}                  ## half -> [ms gone, ms long, from positions, rotations, scales]
	var _drawn := false               ## a body never drawn has no pose to ease from

	## Starts easing `half` from the pose drawn last. The skeleton still holds the animation's pose
	## (it gets it back after the modifiers every frame), so only an ease still going is blended in.
	func start(half: String, ms: float) -> void:
		var sk := get_skeleton()
		if sk == null or not _drawn or ms <= 0.0:
			return
		var old: Array = _eases.get(half, [])
		var k := minf(float(old[0]) / float(old[1]), 1.0) if old.size() else 1.0
		var n := sk.get_bone_count()
		var pos := PackedVector3Array()
		var rot: Array[Quaternion] = []
		var scl := PackedVector3Array()
		pos.resize(n)
		rot.resize(n)
		scl.resize(n)
		for b in bones[half]:
			pos[b] = sk.get_bone_pose_position(b)
			rot[b] = sk.get_bone_pose_rotation(b)
			scl[b] = sk.get_bone_pose_scale(b)
			if k < 1.0:
				pos[b] = (old[2] as PackedVector3Array)[b].lerp(pos[b], k)
				rot[b] = (old[3][b] as Quaternion).slerp(rot[b], k)
				scl[b] = (old[4] as PackedVector3Array)[b].lerp(scl[b], k)
		_eases[half] = [0.0, ms, pos, rot, scl]

	func tick(ms: float) -> void:
		for e in _eases.values():
			e[0] = float(e[0]) + ms

	func clear() -> void:
		_eases.clear()

	func _process_modification_with_delta(_delta: float) -> void:
		var sk := get_skeleton()
		if sk == null or character == null or character.ragdolled:
			return
		_drawn = true
		for half in _eases.keys():
			var e: Array = _eases[half]
			var k := float(e[0]) / float(e[1])
			if k >= 1.0:
				_eases.erase(half)
				continue
			var pos: PackedVector3Array = e[2]
			var rot: Array[Quaternion] = e[3]
			var scl: PackedVector3Array = e[4]
			for b in bones[half]:
				sk.set_bone_pose_position(b, pos[b].lerp(sk.get_bone_pose_position(b), k))
				sk.set_bone_pose_rotation(b, rot[b].slerp(sk.get_bone_pose_rotation(b), k))
				sk.set_bone_pose_scale(b, scl[b].lerp(sk.get_bone_pose_scale(b), k))


## q_math.c AnglesToAxis for id angles in degrees, in character space (x left, y up, z forward).
static func axes(pitch: float, yaw: float, roll: float) -> Basis:
	return Basis(Vector3.UP, deg_to_rad(yaw)) * Basis(Vector3.RIGHT, deg_to_rad(pitch)) * Basis(Vector3.BACK, deg_to_rad(roll))


## The torso's yaw off the legs', and the head's (degrees).
func torso_turn() -> float:
	return wrapf(_yaw_torso.angle - _yaw_legs.angle, -180.0, 180.0) if turn else 0.0


func head_turn() -> float:
	return wrapf(aim_yaw - _yaw_legs.angle, -180.0, 180.0) if turn else 0.0


func _ready() -> void:
	if body == null:
		load_model(model)


func _process(delta: float) -> void:
	if not match_clock:
		_step(delta * 1000.0)


## The match's clock moved `ms` (Q3Client, for bodies on match_clock): the angles swing, then the
## legs and torso play on.
func advance(ms: float) -> void:
	_step(ms)
	if tree and tree.active:
		tree.advance(ms * 0.001)


## CG_PlayerAngles, then CG_PlayerAnimation's choice of legs clip: the swinging angles, the lean,
## and the shuffle turn frames while the legs swing round over the idle stance.
func _step(ms: float) -> void:
	if ragdolled or skeleton == null:
		return
	if aim_center:   # not standing still: always point all in the same direction
		_pitch_torso.swinging = true
		_yaw_torso.swinging = true
		_yaw_legs.swinging = true
	var legs_yaw := rad_to_deg(rotation.y)
	if turn:
		var offset: float = MOVE_OFFSETS[move_dir] if move_dir >= 0 and move_dir < MOVE_OFFSETS.size() else 0.0
		_yaw_torso.step(aim_yaw + 0.25 * offset, 25.0, 90.0, SWING_SPEED, ms)
		_yaw_legs.step(aim_yaw + offset, 40.0, 90.0, SWING_SPEED, ms)
		legs_yaw = _yaw_legs.angle
		rotation = Vector3(0.0, deg_to_rad(legs_yaw), 0.0)
	_pitch_torso.step(wrapf(aim_pitch, -180.0, 180.0) * 0.75, 15.0, 30.0, 0.1, ms)
	# 0.05 degrees per unit a second along the legs' forward (pitch) and left (roll, the other way)
	var y := deg_to_rad(legs_yaw)
	var lean := Vector2(0.05 * (velocity.x * cos(y) + velocity.y * sin(y)), -0.05 * (velocity.y * cos(y) - velocity.x * sin(y)))
	# eased (about 120 ms) and held within 20 degrees: a hit's knockback is a sudden 200-400 ups shove, and followed at
	# once it snapped the body 10-20 degrees over in one frame, a twitch at every hit
	lean = lean.clamp(Vector2(-LEAN_MAX, -LEAN_MAX), Vector2(LEAN_MAX, LEAN_MAX))
	_lean = _lean.lerp(lean, 1.0 - exp(-ms / LEAN_EASE_MS))
	var shown := "LEGS_TURN" if turn and _yaw_legs.swinging and legs_clip == "LEGS_IDLE" and _clip_fps.has("LEGS_TURN") \
		else legs_clip
	if shown != _shown_legs:
		_show_legs(shown)
	if _ease:
		_ease.tick(ms)


## cg_players.c CG_ResetPlayerEntity: the player just came into view or teleported (a respawn too).
## The legs and torso face where they look with nothing swinging, and both clips start over
## without easing (CG_ClearLerpFrame).
func reset_motion() -> void:
	_yaw_legs.angle = wrapf(aim_yaw, -180.0, 180.0)
	_yaw_torso.angle = _yaw_legs.angle
	_pitch_torso.angle = wrapf(aim_pitch, -180.0, 180.0)
	for s in [_yaw_legs, _yaw_torso, _pitch_torso]:
		(s as Swing).swinging = false
	if turn:
		rotation = Vector3(0.0, deg_to_rad(_yaw_legs.angle), 0.0)
	if _legs:
		_show_legs(legs_clip, false)
		play_torso(torso_clip, false)
	if _ease:
		_ease.clear()


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
	for g in body.find_children("*", "GeometryInstance3D", true, false):
		(g as GeometryInstance3D).layers |= FILL_LAYER
	player = body.find_child("AnimationPlayer", true, false)
	skeleton = body.find_child("*Skeleton*", true, false)
	if player == null or skeleton == null:
		push_warning("Q3Character: %s has no AnimationPlayer/Skeleton3D" % m)
		return
	_clip_fps.clear()
	for c in rig.get("clips", []):
		if player.has_animation(c["name"]):
			player.get_animation(c["name"]).loop_mode = Animation.LOOP_LINEAR if c["loop"] else Animation.LOOP_NONE
			_clip_fps[c["name"]] = int(c.get("fps", 0))
	_build_tree()
	_build_aim()
	play_legs(legs_clip)
	play_torso(torso_clip)
	set_skin(skin)


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
	bt.add_node("speed", AnimationNodeTimeScale.new(), Vector2(600, 100))
	bt.connect_node("seek_legs", 0, "legs")
	bt.connect_node("seek_torso", 0, "torso")
	bt.connect_node("mix", 0, "seek_legs")
	bt.connect_node("mix", 1, "seek_torso")
	bt.connect_node("speed", 0, "mix")
	bt.connect_node("output", 0, "speed")
	tree.tree_root = bt
	tree.set("parameters/mix/blend_amount", 1.0)
	tree.set("parameters/speed/scale", 1.0)
	_clock_mode()
	tree.active = true


func _clock_mode() -> void:
	if tree:
		tree.callback_mode_process = AnimationMixer.ANIMATION_CALLBACK_MODE_PROCESS_MANUAL if match_clock \
			else AnimationMixer.ANIMATION_CALLBACK_MODE_PROCESS_IDLE


## cg_players.c CG_PlayerAnimation's speedScale: haste plays the legs and torso 1.5 times as fast.
func set_anim_speed(scale: float) -> void:
	if tree and float(tree.get("parameters/speed/scale")) != scale:
		tree.set("parameters/speed/scale", scale)


## The modifiers over the animation: Ease, then Aim.
func _build_aim() -> void:
	_ease = Ease.new()
	_ease.name = "Ease"
	_ease.character = self
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
	# the halves the AnimationTree's filter splits (the root goes with the legs)
	for i in skeleton.get_bone_count():
		_ease.bones["torso" if half.get(skeleton.get_bone_name(i), "") == "torso" else "legs"].append(i)
	skeleton.add_child(_ease)
	skeleton.add_child(_aim)


## cg_players.c CG_ParseAnimationFile: walking and crouch-walking backward are the forward cycles
## played in reverse (they aren't baked as clips of their own)
const REVERSED_LEGS := {"LEGS_BACKCR": "LEGS_WALKCR", "LEGS_BACKWALK": "LEGS_WALK"}


## LEGS_* clip, or a BOTH_* clip which drives the whole body. It starts over (the toggle bit) and
## eases in from the pose shown, unless `ease` is off.
func play_legs(clip: String, ease := true) -> void:
	if _legs == null or not player.has_animation(REVERSED_LEGS.get(clip, clip)):
		return
	legs_clip = clip
	if clip == "LEGS_IDLE" and _shown_legs == "LEGS_TURN" and _yaw_legs.swinging:
		return   # the shuffle goes on: it isn't the idle stance's clip that starts over
	_show_legs(clip, ease)


func _show_legs(clip: String, ease := true) -> void:
	var src: String = REVERSED_LEGS.get(clip, clip)
	if _legs == null or not player.has_animation(src):
		return
	_shown_legs = clip
	if ease:
		_ease_in(src, ["legs", "torso"] if clip.begins_with("BOTH") else ["legs"])
	_legs.animation = src
	_legs.play_mode = AnimationNodeAnimation.PLAY_MODE_BACKWARD if REVERSED_LEGS.has(clip) else AnimationNodeAnimation.PLAY_MODE_FORWARD
	tree.set("parameters/seek_legs/seek_request", 0.0)
	if clip.begins_with("BOTH"):
		_torso.animation = clip
		tree.set("parameters/seek_torso/seek_request", 0.0)


func play_torso(clip: String, ease := true) -> void:
	if _torso == null or not player.has_animation(clip) or legs_clip.begins_with("BOTH"):
		return
	torso_clip = clip
	if ease:
		_ease_in(clip, ["torso"])
	_torso.animation = clip
	tree.set("parameters/seek_torso/seek_request", 0.0)


## initialLerp is 1000 / fps, whole milliseconds (cg_players.c CG_ParseAnimationFile).
func _ease_in(clip: String, halves: Array) -> void:
	var fps: int = _clip_fps.get(clip, 0)
	if _ease == null or fps <= 0:
		return
	for h in halves:
		_ease.start(h, floorf(1000.0 / fps))


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


## The skins this model has: "default" and rig.json "skins" (red, blue and its alternates).
static func skins_of(m: String) -> PackedStringArray:
	var out := PackedStringArray(["default"])
	out.append_array(PackedStringArray(Q3.character_rig(m).get("skins", {}).keys()))
	return out


## Draws the body in a skin (cg_players.c CG_RegisterClientSkin): each surface the skin paints
## differently gets the skin's maps over its default material's settings; one the model doesn't have
## is the default. A bright skin stays on top.
func set_skin(s: String) -> void:
	skin = s if s in skins_of(model) else "default"
	if body == null:
		return
	for m in _surfaces():
		for i in m.mesh.get_surface_count():
			m.set_surface_override_material(i, skin_material(m, i))
	if _bright.a > 0.0:
		bright_skin(_bright)


## The skin's material for one surface, or null where it's the default's.
func skin_material(m: MeshInstance3D, i: int) -> Material:
	var base := m.mesh.surface_get_material(i) as StandardMaterial3D
	var paint: Dictionary = rig.get("skins", {}).get(skin, {})
	if base == null or not paint.get("materials", {}).has(base.resource_name):
		return null
	var stem := String(paint["materials"][base.resource_name])
	var key := "%s/%s/%s" % [model, skin, stem]
	var ref: WeakRef = _skin_mats.get(key)
	var mat: StandardMaterial3D = ref.get_ref() if ref else null
	if mat:
		return mat
	mat = base.duplicate() as StandardMaterial3D
	mat.resource_name = stem
	var lib := Q3.LIB + "characters/%s_%s_" % [model, stem]
	mat.albedo_texture = Q3.character_texture(lib + ("albedo.png" if ResourceLoader.exists(lib + "albedo.png") else "albedo.jpg"))
	if base.normal_texture:
		mat.normal_texture = Q3.character_texture(lib + "normal.jpg")
	if base.roughness_texture:
		mat.roughness_texture = Q3.character_texture(lib + "orm.jpg")
	if base.metallic_texture:
		mat.metallic_texture = Q3.character_texture(lib + "orm.jpg")
	if base.emission_texture:
		mat.emission_texture = Q3.character_texture(lib + "emissive.jpg")
	var modes: Dictionary = rig.get("material_modes", {})
	var mode: Dictionary = paint.get("material_modes", {}).get(stem, modes.get(stem, {}))
	if mode != modes.get(base.resource_name, {}):
		_draw_mode(mat, mode)
	_skin_mats[key] = weakref(mat)
	return mat


## id Tech 3's draw mode on a character material, as the import script sets it
## (q3_post_import.gd _apply): additive skins (Krusade's hidden cigar) draw unshaded and add.
static func _draw_mode(mat: StandardMaterial3D, mode: Dictionary) -> void:
	var blend := String(mode.get("blend", "opaque"))
	mat.cull_mode = BaseMaterial3D.CULL_DISABLED if mode.get("two_sided", false) else mat.cull_mode
	mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA if blend in ["add", "blend"] \
		else BaseMaterial3D.TRANSPARENCY_ALPHA_SCISSOR if blend == "mask" else BaseMaterial3D.TRANSPARENCY_DISABLED
	mat.alpha_scissor_threshold = 0.5
	var add := blend == "add"
	mat.blend_mode = BaseMaterial3D.BLEND_MODE_ADD if add else BaseMaterial3D.BLEND_MODE_MIX
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED if add else BaseMaterial3D.SHADING_MODE_PER_PIXEL
	mat.depth_draw_mode = BaseMaterial3D.DEPTH_DRAW_DISABLED if add else BaseMaterial3D.DEPTH_DRAW_OPAQUE_ONLY
	mat.disable_fog = add   # fog on an additive surface adds the haze color over it
	mat.emission_enabled = not add and mat.emission_texture != null
	if add:
		mat.albedo_color = Color(1, 1, 1)


## The body's own mesh surfaces (not the gun hung on it).
func _surfaces() -> Array[MeshInstance3D]:
	var out: Array[MeshInstance3D] = []
	for mi in body.find_children("*", "MeshInstance3D", true, false):
		var m := mi as MeshInstance3D
		if m.mesh and not String(m.get_path()).contains("Attach_"):
			out.append(m)
	return out


const BRIGHT_ALBEDO := 0.5   ## a bright skin's lit color, of the setting's
const BRIGHT_GLOW := 0.15    ## and its steady glow, so it reads in dark corners


## Gameplay > Bright skins: every surface in one flat bright color, still lit so the shape reads
## (Quake Live's brightskins); a color with alpha 0 puts the model's own materials (in its skin)
## back. Cut-out surfaces keep their alpha; additive ones (Bones' x-ray) stay as they are.
func bright_skin(color: Color) -> void:
	_bright = color
	if body == null:
		return
	for m in _surfaces():
		for i in m.mesh.get_surface_count():
			var own := skin_material(m, i)
			var src := (own if own else m.mesh.surface_get_material(i)) as StandardMaterial3D
			if color.a <= 0.0 or src == null:
				m.set_surface_override_material(i, own)
				continue
			if src.blend_mode != BaseMaterial3D.BLEND_MODE_MIX or src.transparency == BaseMaterial3D.TRANSPARENCY_ALPHA:
				m.set_surface_override_material(i, own)
				continue
			var key := "bright:%s/%s/%s/%d/%s" % [model, skin, m.name, i, color.to_html()]
			m.set_surface_override_material(i, Q3.character_material(key, func() -> Material:
				# a lit base at half the color with a dim steady glow: at full color the map's lights took it
				# far past 1, and with 0.35 emission it tone-mapped to white and bloomed into a halo
				# (Ozmium, 2026-10-25: "some angelic extreme glow")
				var b := StandardMaterial3D.new()
				b.albedo_color = Color(color.r * BRIGHT_ALBEDO, color.g * BRIGHT_ALBEDO, color.b * BRIGHT_ALBEDO)
				if src.transparency == BaseMaterial3D.TRANSPARENCY_ALPHA_SCISSOR:
					b.albedo_texture = src.albedo_texture
					b.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA_SCISSOR
					b.alpha_scissor_threshold = src.alpha_scissor_threshold
				b.normal_enabled = src.normal_enabled
				b.normal_texture = src.normal_texture
				b.roughness = 1.0
				b.metallic_specular = 0.0   # no highlights to bloom
				b.emission_enabled = true
				b.emission = color
				b.emission_energy_multiplier = BRIGHT_GLOW
				b.cull_mode = src.cull_mode
				return b))


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
	var shader := Q3.held("res://shaders/character_dissolve.gdshader") as Shader
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


## Where the body is: the middle of its ragdoll's parts once it's limp (the node itself stays where
## it fell from), else a meter above its feet. The death cam follows it.
func body_center() -> Vector3:
	if ragdolled and _sim:
		var sum := Vector3.ZERO
		var n := 0
		for pb in _sim.get_children():
			if pb is PhysicalBone3D:
				sum += (pb as PhysicalBone3D).global_position
				n += 1
		if n:
			return sum / n
	return global_position + Vector3.UP


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
	mat.disable_fog = true   # fog would add the haze color over every additive quad
	mat.vertex_color_use_as_albedo = true
	mat.albedo_texture = Q3.fx_texture("flare")
	quad.material = mat
	Q3.keep("ember_particles", pm)   # their shaders stay built for the next corpse
	Q3.keep("ember_quad", mat)
	p.draw_pass_1 = quad
	p.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	# the embers outlive the body by their lifetime, so they hang off the parent
	var host := get_parent()
	host.add_child(p)
	p.global_position = at
	p.emitting = true
	# weak references: the embers can be freed first (a replay's jump), and a lambda holding a freed
	# node makes Godot print "Lambda capture ... was freed"
	var ref: WeakRef = weakref(p)
	get_tree().create_timer(seconds * 0.85).timeout.connect(func():
		var e := ref.get_ref() as GPUParticles3D
		if e:
			e.emitting = false)
	get_tree().create_timer(seconds + p.lifetime + 0.2).timeout.connect(func():
		var e := ref.get_ref() as GPUParticles3D
		if e:
			e.queue_free())


## Physics takes over from animation. `impulse` is a velocity change (m/s) for every part.
func ragdoll(impulse := Vector3.ZERO) -> void:
	if skeleton == null or ragdolled:
		return
	if _sim == null:
		_build_ragdoll()
	tree.active = false
	_sim.physical_bones_start_simulation()
	_ease_joints()
	_part_overlaps()
	var plan: Array = _ragdoll_plan()["bodies"]
	# every part keeps the body's momentum: impulse is per kilogram here
	for pb in _sim.get_children():
		if pb is PhysicalBone3D:
			# thin limbs at 8 m/s would step through a wall's edge in one tick
			PhysicsServer3D.body_set_enable_continuous_collision_detection(pb.get_rid(), true)
			# a thin capsule has next to no inertia about its length, and a small one with joints
			# far off (Bones' spine, six parts hanging off it up to 60 cm away) has next to none
			# at all: the joints spin them up to Jolt's top speed. Every part gets the inertia of
			# a rod reaching its farthest joint, about all three axes.
			var e: Dictionary = plan[pb.get_index()]
			var cap := ((pb as Node).get_child(0) as CollisionShape3D).shape as CapsuleShape3D
			var length := maxf(cap.height, 2.0 * float(e["reach"]))
			var rod: float = pb.mass * (length * length / 12.0 + cap.radius * cap.radius / 4.0)
			PhysicsServer3D.body_set_param(pb.get_rid(), PhysicsServer3D.BODY_PARAM_INERTIA, Vector3(rod, rod, rod))
			pb.apply_central_impulse(impulse * pb.mass)
	ragdolled = true


func stand_up() -> void:
	if _sim and ragdolled:
		_sim.physical_bones_stop_simulation()
		tree.active = true
		ragdolled = false


## The ragdoll is built from the skin, not from bone names: the rig fit (tools/fit_rig.py) names
## many limbs after the torso (Sarge's left arm is belly_2..4, Mynx's legs are hips_2..10), so a
## body's role comes from where it sits. Every bone that carries enough of the mesh gets a capsule
## fitted to its own vertices; the trunk is the chain from the pelvis to the head, everything else
## hangs off it as a limb, a leg when it reaches the floor. Leg joints take their kind from their
## height: hips swing 50 degrees, the first joint at knee height is a hinge that only bends
## forward, ankles 25. Arms swing 60 at the shoulder and 40 further out, the trunk 12 a joint.
## Weight follows the capsules' volume (about 80 kg in all). Every limit is measured from the
## model's standing pose (see _build_ragdoll).
const RAGDOLL_KG := 80.0
const RAGDOLL_MIN_VERTS := 40     ## bones carrying fewer vertices ride along with their parent
const LAYER_RAGDOLL := 1 << 3     ## ragdoll parts: they hit the world and each other


static var _ragdoll_plans := {}   ## model -> Array of body plans (_ragdoll_plan)


## Each bone's share of the mesh (the vertices it weighs most), in skeleton space in the rest pose.
func _rest_clusters() -> Dictionary:
	var out := {}
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
				if not out.has(bone):
					out[bone] = PackedVector3Array()
				out[bone].append(skeleton.get_bone_global_rest(bone) * (m.skin.get_bind_pose(bind) * verts[v]))
	return out


## A capsule around points: along their principal axis, as long as all but the outer 3% at each
## end, as thick as 80% of them stay within (Q3's bulky models are thicker than their bones).
static func _fit_capsule(p: PackedVector3Array) -> Dictionary:
	var c := Vector3.ZERO
	for q in p:
		c += q
	c /= p.size()
	var xx := 0.0
	var xy := 0.0
	var xz := 0.0
	var yy := 0.0
	var yz := 0.0
	var zz := 0.0
	for q in p:
		var d := q - c
		xx += d.x * d.x
		xy += d.x * d.y
		xz += d.x * d.z
		yy += d.y * d.y
		yz += d.y * d.z
		zz += d.z * d.z
	var cov := Basis(Vector3(xx, xy, xz), Vector3(xy, yy, yz), Vector3(xz, yz, zz))
	var axis := Vector3(1, 0, 0) if xx >= yy and xx >= zz else Vector3(0, 1, 0) if yy >= zz else Vector3(0, 0, 1)
	axis = (axis + Vector3(0.1, 0.2, 0.3)).normalized()
	for i in 32:
		var n := cov * axis
		if n.length_squared() < 1e-12:
			break
		axis = n.normalized()
	var ts := PackedFloat32Array()
	var rs := PackedFloat32Array()
	for q in p:
		var t := (q - c).dot(axis)
		ts.append(t)
		rs.append(((q - c) - axis * t).length())
	ts.sort()
	rs.sort()
	var lo := ts[int(ts.size() * 0.03)]
	var hi := ts[int(ts.size() * 0.97)]
	var r := clampf(rs[int(rs.size() * 0.8)], 0.05, 0.25)
	return {"center": c + axis * (lo + hi) * 0.5, "axis": axis, "radius": r, "height": maxf(hi - lo, r * 2.0 + 0.01)}


## The two ends of a capsule's inner segment.
static func _segment(center: Vector3, axis: Vector3, radius: float, height: float) -> Array:
	var h := maxf(height * 0.5 - radius, 0.0)
	return [center - axis * h, center + axis * h]


## The closest points of segments a0-a1 and b0-b1.
static func _closest(a0: Vector3, a1: Vector3, b0: Vector3, b1: Vector3) -> Array:
	var d1 := a1 - a0
	var d2 := b1 - b0
	var r := a0 - b0
	var a := d1.dot(d1)
	var e := d2.dot(d2)
	var f := d2.dot(r)
	var s := 0.0
	var t := 0.0
	if a < 1e-9 and e < 1e-9:
		return [a0, b0]
	if a < 1e-9:
		t = clampf(f / e, 0.0, 1.0)
	else:
		var c := d1.dot(r)
		if e < 1e-9:
			s = clampf(-c / a, 0.0, 1.0)
		else:
			var b := d1.dot(d2)
			var den := a * e - b * b
			s = clampf((b * f - c * e) / den, 0.0, 1.0) if den > 1e-9 else 0.0
			t = (b * s + f) / e
			if t < 0.0:
				t = 0.0
				s = clampf(-c / a, 0.0, 1.0)
			elif t > 1.0:
				t = 1.0
				s = clampf((b - c) / a, 0.0, 1.0)
	return [a0 + d1 * s, b0 + d2 * t]


## What to build for this model, worked out once from its rest pose: per body its bone, capsule
## (skeleton space, -Z pointing away from its joint), mass, joint and limits, plus which bodies
## overlap standing (they never collide with each other).
func _ragdoll_plan() -> Dictionary:
	if _ragdoll_plans.has(model):
		return _ragdoll_plans[model]
	var clusters := _rest_clusters()
	var pelvis := skeleton.find_bone("pelvis")
	# which bones get a body: enough mesh, and an unbroken line of bodies down to the pelvis
	var is_body := {}
	for i in skeleton.get_bone_count():
		is_body[i] = clusters.has(i) and clusters[i].size() >= RAGDOLL_MIN_VERTS
	is_body[pelvis] = true
	var phys_parent := {}
	for i in skeleton.get_bone_count():
		var p := skeleton.get_bone_parent(i)
		while p >= 0 and not is_body[p]:
			p = skeleton.get_bone_parent(p)
		phys_parent[i] = p
	var bodies: Array[int] = []
	for i in skeleton.get_bone_count():
		var a := i
		while a >= 0 and a != pelvis:
			a = phys_parent[a] if is_body[a] else -1
		if is_body[i] and a == pelvis:
			bodies.append(i)
		else:
			is_body[i] = false
	# each capsule fits its own bone's share only (riders' vertices made Bones' pelvis one capsule
	# around both legs)
	var caps := {}
	var low := {}
	for i in bodies:
		var p: PackedVector3Array = clusters.get(i, PackedVector3Array())
		if p.size() < 4:
			var o := skeleton.get_bone_global_rest(i).origin
			p = PackedVector3Array([o + Vector3(0, -0.05, 0), o + Vector3(0, 0.05, 0), o + Vector3(0.03, 0, 0), o + Vector3(-0.03, 0, 0)])
		caps[i] = _fit_capsule(p)
		var y := 99.0
		for q in p:
			y = minf(y, q.y)
		low[i] = y
	# joints at the bones' own origins: the rig fit put them where the animation turns each part,
	# so the skin bends there without tearing; each capsule points away from its joint
	var joint := {}
	for i in bodies:
		if i == pelvis:
			continue
		var c: Dictionary = caps[i]
		joint[i] = skeleton.get_bone_global_rest(i).origin
		if c["axis"].dot(c["center"] - joint[i]) < 0.0:
			c["axis"] = -c["axis"]
	# the trunk: pelvis to head
	var trunk := {}
	var head := skeleton.find_bone("head")
	while head >= 0 and not is_body[head]:
		head = skeleton.get_bone_parent(head)
	if head < 0:
		head = pelvis
	var t := head
	while t >= 0:
		trunk[t] = true
		t = phys_parent[t] if t != pelvis else -1
	# limbs: the first body off the trunk roots one; a leg when anything in it gets near the floor
	var floor_y := skeleton.get_bone_global_rest(pelvis).origin.y
	var limb_root := {}
	for i in bodies:
		var r := i
		while not trunk.has(r) and not trunk.has(phys_parent[r]):
			r = phys_parent[r]
		limb_root[i] = r
	var leg := {}
	for i in bodies:
		if not trunk.has(i) and low[i] < floor_y * 0.3:
			leg[limb_root[i]] = true
	# chunks hanging off the trunk but inside the body (Keel's belly, a shoulder blade, Mynx's
	# hips) move with the trunk; the limb's first bend is the first body outside
	var spur := {}
	var mid: Vector3 = caps[pelvis]["center"]
	var bottom: float = mid.y - caps[pelvis]["height"] * 0.5
	for i in bodies:
		var chain: Array[int] = []
		var a := i
		while not trunk.has(a):
			chain.push_front(a)
			a = phys_parent[a]
		for k in chain:
			var c: Vector3 = caps[k]["center"]
			var pp: int = phys_parent[k]
			spur[k] = (trunk.has(pp) or spur[pp]) and Vector2(c.x - mid.x, c.z - mid.z).length() < 0.12 \
					and c.y > bottom and c.y < caps[head]["center"].y
	var total := 0.0
	for i in bodies:
		var c: Dictionary = caps[i]
		var seg := maxf(c["height"] - 2.0 * c["radius"], 0.0)
		c["volume"] = PI * c["radius"] * c["radius"] * (seg + c["radius"] * 4.0 / 3.0)
		total += c["volume"]
	# a leg joint's kind by its height on the leg: hip, knee, ankle
	var band := {}
	for i in bodies:
		if i != pelvis and not trunk.has(i) and not spur[i] and leg.has(limb_root[i]):
			var h: float = joint[i].y / maxf(joint[limb_root[i]].y, 0.01)
			band[i] = "hip" if h > 0.75 else "knee" if h > 0.3 else "foot"
	var plan: Array = []
	for i in bodies:
		var c: Dictionary = caps[i]
		var e := {"bone": i, "radius": c["radius"], "height": c["height"], "mass": clampf(RAGDOLL_KG * c["volume"] / total, 1.0, 12.0)}
		var up := Vector3.UP if absf(c["axis"].dot(Vector3.UP)) < 0.95 else Vector3.BACK
		e["xform"] = Transform3D(Basis.looking_at(c["axis"], up), c["center"])
		if i != pelvis:
			e["joint"] = joint[i]
			if trunk.has(i) or spur[i]:
				e["kind"] = "cone"
				e["swing"] = 30.0 if i == head else 12.0
				e["twist"] = 25.0 if i == head else 8.0
			elif band.has(i):
				# one bend of each kind a leg: further joints in the same band stay nearly stiff
				var again := false
				var a: int = phys_parent[i]
				while band.has(a):
					again = again or band[a] == band[i]
					a = phys_parent[a]
				if band[i] == "knee" and not again:
					# Quake 3 stands in a crouch: the knee's 0-140 degrees count from a straight
					# leg, less the bend it already has standing
					e["kind"] = "hinge"
					var up_leg: Vector3 = caps[phys_parent[i]]["axis"]
					var low_leg: Vector3 = c["axis"]
					var tp := Vector2(up_leg.y, up_leg.z)
					var sp := Vector2(low_leg.y, low_leg.z)
					e["bent"] = rad_to_deg(clampf(tp.angle_to(sp), 0.0, deg_to_rad(90.0))) if band.has(phys_parent[i]) else 0.0
				else:
					e["kind"] = "cone"
					e["swing"] = 10.0 if again and band[i] != "foot" else 45.0 if band[i] == "hip" else 25.0
					e["twist"] = 12.0 if band[i] == "hip" else 4.0
					var hang := Vector3(0.0, -cos(PI / 6.0), sin(PI / 6.0))
					if band[i] == "hip" and not again and c["axis"].angle_to(hang) < deg_to_rad(45.0):
						# a hip's cone is centered on a thigh hanging 30 degrees forward, not on
						# the stance (Sarge stands with his thighs spread 30 degrees: from there
						# the legs flopped out like a frog's): forward 75, back 15, out 45. Only
						# for a body that is a thigh (Tank Jr's legs are loose armor plates).
						e["neutral"] = Quaternion(c["axis"], hang)
			else:
				# arms (and tails, wings): the shoulder swings 60, everything further out 40
				var shoulder := true
				var a: int = phys_parent[i]
				while not trunk.has(a):
					shoulder = shoulder and spur[a]
					a = phys_parent[a]
				e["kind"] = "cone"
				e["swing"] = 60.0 if shoulder else 40.0
				e["twist"] = 25.0 if shoulder else 10.0
		plan.append(e)
	var at := {}
	for k in plan.size():
		at[plan[k]["bone"]] = k
	for e in plan:
		e["reach"] = 0.0
	for e in plan:
		if e.has("joint"):
			e["parent"] = phys_parent[e["bone"]]
			e["parent_index"] = at[e["parent"]]
			# how far its joints sit from each part's middle (its own and its children's)
			var up: Dictionary = plan[e["parent_index"]]
			e["reach"] = maxf(e["reach"], (e["joint"] as Vector3).distance_to((e["xform"] as Transform3D).origin))
			up["reach"] = maxf(up["reach"], (e["joint"] as Vector3).distance_to((up["xform"] as Transform3D).origin))
	# the solver copes with a part up to about 2.5 times its parent's weight: a light pelvis with
	# heavy parts hanging off it (Tank Jr's), or Bones' 1 kg spine carrying the torso and both
	# arms, flies apart. The trunk is never light.
	var kg := {}
	for e in plan:
		var pp: int = phys_parent[e["bone"]]
		if e["bone"] == pelvis:
			e["mass"] = maxf(e["mass"], 8.0)
		elif trunk.has(e["bone"]) and e["bone"] != head:
			e["mass"] = maxf(e["mass"], 4.0)
		if kg.has(pp):
			e["mass"] = minf(e["mass"], kg[pp] * 2.5)
		kg[e["bone"]] = e["mass"]
	# bodies touching standing (and every body with its parent) never collide with each other
	var apart: Array = []
	for a in plan.size():
		for b in range(a + 1, plan.size()):
			var ea: Dictionary = plan[a]
			var eb: Dictionary = plan[b]
			if phys_parent[eb["bone"]] == ea["bone"] or phys_parent[ea["bone"]] == eb["bone"]:
				apart.append(Vector2i(a, b))
				continue
			var ca: Dictionary = caps[ea["bone"]]
			var cb: Dictionary = caps[eb["bone"]]
			var sa: Array = _segment(ca["center"], ca["axis"], ca["radius"], ca["height"])
			var sb: Array = _segment(cb["center"], cb["axis"], cb["radius"], cb["height"])
			var cp: Array = _closest(sa[0], sa[1], sb[0], sb[1])
			if (cp[0] as Vector3).distance_to(cp[1]) < ca["radius"] + cb["radius"] + 0.02:
				apart.append(Vector2i(a, b))
	var out := {"bodies": plan, "apart": apart}
	_ragdoll_plans[model] = out
	return out


## The physics bones, from the plan. A joint's limits are measured from where its two bodies are
## when it's made, and Godot makes it where the simulator last saw the bones: the dying
## animation's pose, so a knee bent mid-stride could never straighten and an arm swung back could
## swing as far again. So once every body is in, each is moved to the standing pose (hips turned
## to their neutral) and its joint made again there; starting the simulation puts them back.
func _build_ragdoll() -> void:
	var plan := _ragdoll_plan()
	_sim = PhysicalBoneSimulator3D.new()
	skeleton.add_child(_sim)
	var parts: Array[PhysicalBone3D] = []
	for e in plan["bodies"]:
		var i: int = e["bone"]
		var at: Transform3D = e["xform"]
		var pb := PhysicalBone3D.new()
		pb.name = "Phys_" + skeleton.get_bone_name(i)
		pb.bone_name = skeleton.get_bone_name(i)
		pb.body_offset = skeleton.get_bone_global_rest(i).affine_inverse() * at
		pb.mass = e["mass"]
		pb.friction = 1.0
		pb.bounce = 0.0
		pb.linear_damp = 0.05
		pb.angular_damp = 0.6
		pb.collision_layer = LAYER_RAGDOLL
		pb.collision_mask = 1 | LAYER_RAGDOLL   # the world's solid brushes, and other parts
		if e.has("joint"):
			# set before it's in the tree: once it is, Godot moves the joint to the bone's origin
			var j: Vector3 = at.affine_inverse() * (e["joint"] as Vector3)
			if e["kind"] == "hinge":
				# knees bend one way only: a hinge about the body's left-right axis (skeleton X),
				# the lower leg swinging back (negative), from straight to 140 degrees
				var z := (at.basis.inverse() * Vector3.RIGHT).normalized()
				var x := (Vector3.FORWARD - z * Vector3.FORWARD.dot(z)).normalized()
				pb.joint_offset = Transform3D(Basis(x, z.cross(x), z), j)
			else:
				# the twist axis (X) turned onto the capsule (-Z)
				pb.joint_offset = Transform3D(Basis(Vector3.UP, PI * 0.5), j)
		var cap := CapsuleShape3D.new()
		cap.radius = e["radius"]
		cap.height = e["height"]
		var shape := CollisionShape3D.new()
		shape.shape = cap
		shape.transform = Transform3D(Basis(Vector3.RIGHT, PI * 0.5), Vector3.ZERO)   # capsule height along -Z
		pb.add_child(shape)
		_sim.add_child(pb)
		parts.append(pb)
	# the standing pose, each hip and everything below it turned to the hip's neutral
	var turn := {}
	for e in plan["bodies"]:
		if e.has("neutral"):
			var j: Vector3 = e["joint"]
			var r := Basis(e["neutral"] as Quaternion)
			turn[e["bone"]] = Transform3D(r, j - r * j)
	for k in parts.size():
		var e: Dictionary = plan["bodies"][k]
		var n := Transform3D.IDENTITY
		var a: int = e["bone"]
		while a >= 0:
			if turn.has(a):
				n = turn[a]
				break
			a = skeleton.get_bone_parent(a)
		parts[k].global_transform = skeleton.global_transform * n * (e["xform"] as Transform3D)
	var by_bone := {}
	for k in parts.size():
		by_bone[plan["bodies"][k]["bone"]] = parts[k]
	for k in parts.size():
		var e: Dictionary = plan["bodies"][k]
		if e.has("joint"):
			var up := by_bone[e["parent"]] as PhysicalBone3D
			parts[k].set_meta("ragdoll_axis", up.global_basis.inverse() * -parts[k].global_basis.z)
			_make_joint(parts[k], e)
	for pair in plan["apart"]:
		parts[pair.x].add_collision_exception_with(parts[pair.y])


## (Re)makes a part's joint where its body and its parent's are now.
static func _make_joint(pb: PhysicalBone3D, e: Dictionary) -> void:
	pb.joint_type = PhysicalBone3D.JOINT_TYPE_NONE
	if e["kind"] == "hinge":
		pb.joint_type = PhysicalBone3D.JOINT_TYPE_HINGE
		pb.set("joint_constraints/angular_limit_enabled", true)
		pb.set("joint_constraints/angular_limit_lower", float(e["bent"]) - 140.0)
		pb.set("joint_constraints/angular_limit_upper", float(e["bent"]))
	else:
		pb.joint_type = PhysicalBone3D.JOINT_TYPE_CONE
		pb.set("joint_constraints/swing_span", float(e["swing"]))
		pb.set("joint_constraints/twist_span", float(e["twist"]))


## A part the dying pose already swings past its limit (a gun arm hanging straight off the spine,
## as Bones' does) would be yanked back in one step and spin the body apart: its joint is made in
## the dying pose instead, as Godot would.
func _ease_joints() -> void:
	var plan: Array = _ragdoll_plan()["bodies"]
	var parts := _sim.get_children()
	for k in parts.size():
		var e: Dictionary = plan[k]
		var pb := parts[k] as PhysicalBone3D
		if e.get("kind", "") != "cone":
			continue
		var up := pb.get_parent().get_child(e["parent_index"]) as PhysicalBone3D
		var now := up.global_basis.inverse() * -pb.global_basis.z
		if now.angle_to(pb.get_meta("ragdoll_axis")) > deg_to_rad(float(e["swing"])):
			_make_joint(pb, e)


## Parts that overlap in the pose the body dies in (an arm across the chest, a hand in the hip)
## pass through each other, or the solver would fling them apart on the first step.
func _part_overlaps() -> void:
	var parts: Array[PhysicalBone3D] = []
	var segs: Array = []
	for n in _sim.get_children():
		if n is PhysicalBone3D:
			var pb := n as PhysicalBone3D
			var cap := (pb.get_child(0) as CollisionShape3D).shape as CapsuleShape3D
			var axis := -pb.global_basis.z
			parts.append(pb)
			segs.append(_segment(pb.global_position, axis, cap.radius, cap.height) + [cap.radius])
	for a in parts.size():
		for b in range(a + 1, parts.size()):
			var cp: Array = _closest(segs[a][0], segs[a][1], segs[b][0], segs[b][1])
			if (cp[0] as Vector3).distance_to(cp[1]) < float(segs[a][2]) + float(segs[b][2]):
				parts[a].add_collision_exception_with(parts[b])
