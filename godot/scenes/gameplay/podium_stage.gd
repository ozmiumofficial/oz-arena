class_name Q3PodiumStage
extends SubViewportContainer
## The results screen's podium: Quake 3's mapobjects/podium/podium4 with the top three standing on it where
## g_arenas.c SpawnPodium puts them (offsetFirst/Second/Third, forward-right-up from the podium in id units, at the
## player's origin, 24 units over the feet). A world of its own, drawn over the results screen's dark backdrop.
## play_taunt() plays a player's gesture with a taunt's sound: the local player's buttons and everyone else's
## "ptaunt" server commands both come here. Bots taunt once by themselves.

const PODIUM := "mapobjects/podium/podium4"
## [forward, right, up] (g_arenas.c), for 1st, 2nd and 3rd
const OFFSETS := [[0.0, 0.0, 74.0], [-10.0, 60.0, 54.0], [-19.0, -60.0, 45.0]]

var _vp: SubViewport
var _chars := {}          ## client -> Q3Character
var _rng := RandomNumberGenerator.new()


## rows: the top of the standings, best first: [{client, model, skin, bot}, ...] (up to 3 are used).
func setup(rows: Array) -> void:
	stretch = true
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	_vp = SubViewport.new()
	_vp.own_world_3d = true
	_vp.transparent_bg = true
	_vp.msaa_3d = Viewport.MSAA_4X
	add_child(_vp)
	var env := Environment.new()
	env.background_mode = Environment.BG_CLEAR_COLOR
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.55, 0.5, 0.5)
	env.ambient_light_energy = 0.6
	env.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	var we := WorldEnvironment.new()
	we.environment = env
	_vp.add_child(we)
	var key := DirectionalLight3D.new()
	key.light_energy = 1.4
	key.rotation_degrees = Vector3(-35.0, 20.0, 0.0)
	_vp.add_child(key)
	var rim := OmniLight3D.new()
	rim.light_color = Color(1.0, 0.45, 0.25)
	rim.light_energy = 3.0
	rim.omni_range = 6.0
	rim.position = Vector3(0.0, 3.2, -2.2)
	_vp.add_child(rim)
	var podium := Q3.instance_prop(PODIUM)
	if podium:
		_vp.add_child(podium)
	for i in mini(rows.size(), OFFSETS.size()):
		var r: Dictionary = rows[i]
		var ch := Q3Character.new()
		ch.model = String(r.get("model", "sarge"))
		ch.skin = String(r.get("skin", "default"))
		ch.legs_clip = "LEGS_IDLE"
		ch.torso_clip = "TORSO_STAND"
		ch.position = _place(OFFSETS[i])
		_vp.add_child(ch)
		_chars[int(r.get("client", -1))] = ch
		if bool(r.get("bot", false)):
			get_tree().create_timer(_rng.randf_range(1.5, 4.5)).timeout.connect(func(): _gesture(ch))
	var cam := Camera3D.new()
	cam.fov = 38.0
	cam.position = Vector3(0.0, 2.4, 6.4)
	_vp.add_child(cam)
	cam.look_at(Vector3(0.0, 1.55, 0.0))


## A podium offset (forward, right, up; id units, at the player's origin) as the feet's place in the stage: id's
## forward is toward the camera (+Z here), its right is -X, and the feet are 24 units under the origin.
func _place(o: Array) -> Vector3:
	return Vector3(-float(o[1]), float(o[2]) - 24.0, float(o[0])) * Q3.UNIT


## Whether this player stands on the podium (the taunt buttons show only for them).
func has_player(client: int) -> bool:
	return _chars.has(client)


## A podium taunt: `client`'s gesture with the sound of taunt `id` (the card catalog's taunts).
func play_taunt(client: int, id: String) -> void:
	var ch: Q3Character = _chars.get(client)
	if ch == null or not is_instance_valid(ch):
		return
	var t := Q3Cards.part("taunts", id)
	if not t.is_empty():
		UiSound.play_file(Q3.LIB + "sounds/" + String(t.get("sound", "")).trim_prefix("sound/"))
	_gesture(ch)


func _gesture(ch: Q3Character) -> void:
	if not is_instance_valid(ch) or ch.player == null or not ch.player.has_animation("TORSO_GESTURE"):
		return
	ch.play_torso("TORSO_GESTURE")
	var back := ch.player.get_animation("TORSO_GESTURE").length
	get_tree().create_timer(back).timeout.connect(func(): if is_instance_valid(ch): ch.play_torso("TORSO_STAND"))
