class_name Q3PodiumStage
extends Node3D
## The results screen's podium: Quake 3's mapobjects/podium/podium4 with the top three standing on it where
## g_arenas.c SpawnPodium puts them (offsetFirst/Second/Third, forward-right-up from the podium in id units, at the
## player's origin, 24 units over the feet). Like Quake 3's own, it stands in the map, in front of the intermission
## camera, so the map is really behind it and lights it, and its energy columns draw as they do in the game (in a
## transparent view of their own, the additive glow came out black). While the results show, frame_camera() holds
## the camera on it. play_taunt() plays one of TAUNTS for a player, always in their own character's voice (Quake 3
## gives each character one taunt line): the local player's buttons and everyone else's "ptaunt" server commands
## both come here. Bots taunt once by themselves.
## Drawn on top: everything here is on LAYER, which the match camera leaves out; an overlay camera in the same world
## (overlay(), following the match camera) draws only LAYER over the map, so a wall in front can't cut the podium.

const PODIUM := "mapobjects/podium/podium4"
## [forward, right, up] (g_arenas.c), for 1st, 2nd and 3rd
const OFFSETS := [[0.0, 0.0, 74.0], [-10.0, 60.0, 54.0], [-19.0, -60.0, 45.0]]
## the podium taunts, id -> button name: the gesture, a gauntlet punch (twice), a victory hop
const TAUNTS := {"gesture": "Taunt", "gauntlet": "Gauntlet", "hop": "Hop"}
const HOP_HEIGHT := 0.22   ## on top of LEGS_JUMP's own lift
## the camera, in the stage's space (it faces +Z): where it is, what it looks at, its vertical fov
const VIEW_FROM := Vector3(0.0, 2.4, 6.4)
const VIEW_AT := Vector3(0.0, 1.55, 0.0)
const LAYER := 1 << 16   ## render layer 17: the podium and its players
const VIEW_FOV := 60.0   ## vertical: the podium about 60% of the screen's height, between the two columns of text

var _chars := {}          ## client -> Q3Character
var _feet := {}           ## client -> where their feet rest
var _rng := RandomNumberGenerator.new()
var _overlay_cam: Camera3D
static var _nothing: ShaderMaterial


## rows: the top of the standings, best first: [{client, model, skin, bot}, ...] (up to 3 are used). `cam`: the
## match camera, at the intermission spot; the podium goes in front of it, level, facing it.
func setup(rows: Array, cam: Camera3D) -> void:
	var f := -cam.global_transform.basis.z
	f.y = 0.0
	f = f.normalized() if f.length() > 0.01 else Vector3.FORWARD
	rotation.y = atan2(-f.x, -f.z)   # the stage's +Z toward the camera
	global_position = cam.global_position + f * VIEW_FROM.z - Vector3(0.0, VIEW_FROM.y, 0.0)
	var key := OmniLight3D.new()   # the map's own light is baked into its surfaces, so the stage brings some
	key.light_energy = 2.0
	key.omni_range = 9.0
	key.position = Vector3(1.5, 4.0, 4.0)
	add_child(key)
	var rim := OmniLight3D.new()
	rim.light_color = Color(1.0, 0.45, 0.25)
	rim.light_energy = 3.0
	rim.omni_range = 6.0
	rim.position = Vector3(0.0, 3.2, -2.2)
	add_child(rim)
	var podium := Q3.instance_prop(PODIUM)
	if podium:
		add_child(podium)
	for i in mini(rows.size(), OFFSETS.size()):
		var r: Dictionary = rows[i]
		var ch := Q3Character.new()
		ch.model = String(r.get("model", "sarge"))
		ch.skin = String(r.get("skin", "default"))
		ch.legs_clip = "LEGS_IDLE"
		ch.torso_clip = "TORSO_STAND"
		ch.position = _place(OFFSETS[i])
		add_child(ch)
		var cl := int(r.get("client", -1))
		_chars[cl] = ch
		_feet[cl] = ch.position
		if bool(r.get("bot", false)):
			var pick: String = TAUNTS.keys()[_rng.randi() % TAUNTS.size()]
			get_tree().create_timer(_rng.randf_range(1.5, 4.5)).timeout.connect(func(): play_taunt(cl, pick))


## Holds `cam` on the podium (every frame while the results show), and the overlay's camera with it.
func frame_camera(cam: Camera3D) -> void:
	cam.global_position = to_global(VIEW_FROM)
	cam.look_at(to_global(VIEW_AT))
	cam.fov = VIEW_FOV
	cam.cull_mask &= ~LAYER
	_put_on_layer(self)   # the characters build their meshes as they load
	if _overlay_cam:
		_overlay_cam.global_transform = cam.global_transform
		_overlay_cam.fov = cam.fov
		_overlay_cam.near = cam.near
		_overlay_cam.far = cam.far


## The overlay: a view of the same world (`world`) that draws only the podium, `size` pixels, transparent elsewhere.
func overlay(world: World3D, size: Vector2i) -> SubViewport:
	var vp := SubViewport.new()
	vp.world_3d = world
	vp.transparent_bg = true
	vp.size = size
	vp.msaa_3d = Viewport.MSAA_4X
	vp.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	_overlay_cam = Camera3D.new()
	_overlay_cam.cull_mask = LAYER
	vp.add_child(_overlay_cam)
	_overlay_cam.current = true
	return vp


## Everything onto LAYER. The additive surfaces (the energy columns: names carry "__add") draw nothing: in the
## overlay's transparent view they'd write their near-black color as opaque (black columns), and their faint sparks
## barely show in the map anyway.
func _put_on_layer(n: Node) -> void:
	for c in n.get_children():
		if c is VisualInstance3D:
			(c as VisualInstance3D).layers = LAYER
			_hide_additive(c)
		_put_on_layer(c)


func _hide_additive(v: Node) -> void:
	var mi := v as MeshInstance3D
	if mi == null or mi.mesh == null:
		return
	if _nothing == null:
		var sh := Shader.new()
		sh.code = "shader_type spatial;
render_mode unshaded;
void fragment() { discard; }
"
		_nothing = ShaderMaterial.new()
		_nothing.shader = sh
	for i in mi.mesh.get_surface_count():
		var m := mi.mesh.surface_get_material(i)
		if m and m.resource_name.contains("__add") and mi.get_surface_override_material(i) != _nothing:
			mi.set_surface_override_material(i, _nothing)


## A podium offset (forward, right, up; id units, at the player's origin) as the feet's place in the stage: id's
## forward is toward the camera (+Z here), its right is -X, and the feet are 24 units under the origin.
func _place(o: Array) -> Vector3:
	return Vector3(-float(o[1]), float(o[2]) - 24.0, float(o[0])) * Q3.UNIT


## Whether this player stands on the podium (the taunt buttons show only for them).
func has_player(client: int) -> bool:
	return _chars.has(client)


## A podium taunt (an id of TAUNTS) by `client`, in their own character's voice.
func play_taunt(client: int, id: String) -> void:
	var ch: Q3Character = _chars.get(client)
	if ch == null or not is_instance_valid(ch) or ch.player == null or not TAUNTS.has(id):
		return
	var voice := Q3.LIB + "sounds/player/%s/taunt.wav" % ch.model
	UiSound.play_file(voice if ResourceLoader.exists(voice) else Q3.LIB + "sounds/player/sarge/taunt.wav")
	match id:
		"gesture":
			_torso(ch, ["TORSO_GESTURE"])
		"gauntlet":
			_torso(ch, ["TORSO_ATTACK2", "TORSO_ATTACK2"])
		"hop":
			_hop(ch, _feet.get(client, ch.position))


## Torso clips one after another, then back to standing.
func _torso(ch: Q3Character, clips: Array) -> void:
	var t := 0.0
	for c in clips:
		if not ch.player.has_animation(c):
			return
		var clip: String = c
		get_tree().create_timer(t).timeout.connect(func(): if is_instance_valid(ch): ch.play_torso(clip, false))
		t += ch.player.get_animation(clip).length
	get_tree().create_timer(t).timeout.connect(func(): if is_instance_valid(ch): ch.play_torso("TORSO_STAND"))


## A victory hop on the spot: the jump, up and down, the landing, then idle again.
func _hop(ch: Q3Character, feet: Vector3) -> void:
	if not ch.player.has_animation("LEGS_JUMP") or not ch.player.has_animation("LEGS_LAND"):
		return
	ch.play_legs("LEGS_JUMP", false)
	var tw := create_tween()
	tw.tween_property(ch, "position:y", feet.y + HOP_HEIGHT, 0.3).set_trans(Tween.TRANS_QUAD).set_ease(Tween.EASE_OUT)
	tw.tween_property(ch, "position:y", feet.y, 0.3).set_trans(Tween.TRANS_QUAD).set_ease(Tween.EASE_IN)
	tw.tween_callback(func(): if is_instance_valid(ch): ch.play_legs("LEGS_LAND", false))
	tw.tween_interval(ch.player.get_animation("LEGS_LAND").length)
	tw.tween_callback(func(): if is_instance_valid(ch): ch.play_legs("LEGS_IDLE"))
