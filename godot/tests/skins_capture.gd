extends Node
## Team and alternate skins in a real (windowed) match: every player's body has the model and skin
## cg_players.c would pick, and a picture of each.
##   godot --path . res://tests/skins_capture.tscn -- mode=ctf map=q3ctf1 bots=8 out=C:/shots
##   godot --path . res://tests/skins_capture.tscn -- campaign=q3dm2 out=C:/shots   (Phobos's arena)
## Team games: each body is red or blue by its CS_PLAYERS "t" (Sarge for a model with no team skin).
## Otherwise each is in its own skin (Phobos is doom/phobos). The local player spectates and follows
## each player in turn with the chase camera (the snapshot only holds what the followed player can
## see); the followed body (the client's own-body copy, as mirrors draw it) and every other body in
## view are checked, and each player is photographed once with the game frozen:
## out/<client>_<name>_<model>_<skin>.png and out/sheet.png. The campaign runs dry (nothing saved).

const E := preload("res://scripts/game/q3_enums.gd")

var seconds := 8.0
var out := ""
var _game: Node
var _t := 0.0
var _cam: Camera3D
var _queue: Array[int] = []    ## players still to follow
var _follow := -1
var _since := 0.0
var _shot: Array = []          ## [client, body, name] being photographed
var _wait := 0
var _done := {}                ## client -> true once photographed
var _images: Array[Image] = []
var _ok := true
var _checked := 0


func _ready() -> void:
	var mode := "ctf"
	var map := "q3ctf1"
	var bots := 8
	var campaign := ""
	for a in OS.get_cmdline_user_args():
		var kv := a.split("=", true, 1)
		if kv.size() < 2:
			continue
		match kv[0]:
			"mode": mode = kv[1]
			"map": map = kv[1]
			"bots": bots = int(kv[1])
			"campaign": campaign = kv[1]
			"seconds": seconds = float(kv[1])
			"out": out = kv[1]
	var s: Q3MatchSetup
	if campaign != "":
		Session.dry_run = true
		s = Session.campaign.setup_for(campaign, 3)
	else:
		s = Session.new_setup(mode, map)
		s.bots = []
		var names := ["Sarge", "Xaero", "Major", "Doom", "Grunt", "Ranger", "Klesk", "Daemia"]
		for i in bots:
			s.bots.append({"name": names[i % names.size()], "skill": 2, "team": "red" if i % 2 == 0 else "blue"})
	s.spectate = true
	Session.setup = s
	Settings.set_value("gameplay/spec_view", "chase", false)   # this run only
	Settings.set_value("gameplay/spec_director", false, false)
	_game = load("res://scenes/gameplay/gameplay.tscn").instantiate()
	_game.test_input = true
	add_child(_game)
	process_priority = 1000   # after Gameplay and the client
	_cam = Camera3D.new()
	_cam.fov = 50.0
	_cam.near = 0.05
	add_child(_cam)


func _process(delta: float) -> void:
	_t += delta
	if _t < seconds:
		return
	if not _shot.is_empty():
		_photograph()
		return
	if _queue.is_empty() and _follow < 0:
		for cl in E.MAX_CLIENTS:
			if cl != _game.me and String(_info(cl).get("n", "")) != "":
				_queue.append(cl)
		if _queue.is_empty():
			_finish()
			return
	_since += delta
	if _follow < 0 or _since > 6.0:
		if _queue.is_empty():
			_finish()
			return
		_follow = _queue.pop_front()
		_since = 0.0
		Q3Server.client_command(_game.me, "follow %d" % _follow)
		return
	if _since < 1.5:
		return
	# the followed player's own-body copy, and everyone it can see
	var bodies := {}
	var me: Dictionary = _client()._self
	if _client().local_client == _follow and not me.is_empty() and is_instance_valid(me["node"]) and (me["node"] as Node3D).visible:
		bodies[_follow] = me["node"]
	var seen := _client().player_nodes()
	for cl in seen:
		bodies[cl] = seen[cl]
	for cl in bodies:
		if not _done.has(cl):
			_check(cl, bodies[cl])
			_done[cl] = true
			_aim(bodies[cl])
			Engine.time_scale = 0.0   # the body holds still for its picture
			_cam.make_current()
			_wait = 4
			return
	if _done.has(_follow):
		_since = 99.0   # on to the next


func _client() -> Q3Client:
	return _game.client


func _info(cl: int) -> Dictionary:
	return Q3Host.parse_info(_client().feed.configstring(E.CS_PLAYERS + cl))


## One body against what CG_FindClientModelFile and CG_LoadClientInfo pick.
func _check(cl: int, ch: Q3Character) -> void:
	var team_game := Q3Server.cvar("g_gametype").to_int() >= E.GT_TEAM
	var info := _info(cl)
	var spec := String(info.get("model", "")).to_lower()
	var m := spec.get_slice("/", 0)
	var want_skin := spec.get_slice("/", 1) if spec.contains("/") else "default"
	var want_model := m
	if team_game:
		want_skin = "blue" if int(info.get("t", "0")) == E.TEAM_BLUE else "red"
		if not want_skin in Q3Character.skins_of(m):
			want_model = "sarge"
	var good := ch.model == want_model and ch.skin == want_skin
	# the surfaces really wear it: any skin but default swaps at least one material
	var painted := want_skin == "default"
	for mi in ch.body.find_children("*", "MeshInstance3D", true, false):
		var mesh := (mi as MeshInstance3D).mesh
		for i in mesh.get_surface_count():
			var o := (mi as MeshInstance3D).get_surface_override_material(i)
			if o and mesh.surface_get_material(i) and o.resource_name != mesh.surface_get_material(i).resource_name:
				painted = true
	print("SKINS %s %-10s t=%s %-14s -> %s/%s%s" % ["ok " if good and painted else "BAD", info.get("n", "?"),
		info.get("t", "?"), spec, ch.model, ch.skin, "" if painted else " (no material swapped)"])
	_ok = _ok and good and painted
	_checked += 1
	_shot = [cl, ch, "%02d_%s_%s_%s" % [cl, String(info.get("n", "x")).to_lower(), ch.model, ch.skin]]


func _photograph() -> void:
	_wait -= 1
	if _wait > 0:
		return
	var img := get_viewport().get_texture().get_image()
	_images.append(img)
	if out != "":
		img.save_png(out.path_join(String(_shot[2]) + ".png"))
	_shot = []
	Engine.time_scale = 1.0
	_game.cam.make_current()


## The camera in front of a body, on a side the map leaves open.
func _aim(ch: Q3Character) -> void:
	var chest := ch.global_position + Vector3.UP * 1.0
	var best := chest + ch.global_basis.z * 2.2 + Vector3.UP * 0.25
	for k in 8:
		var dir := ch.global_basis.z.rotated(Vector3.UP, k * TAU / 8.0)
		var spot := chest + dir * 2.2 + Vector3.UP * 0.25
		var tr := _client().feed.trace(_q3(chest), _q3(spot), Vector3(-6, -6, -6), Vector3(6, 6, 6), 1023, 1)
		if tr.is_empty() or float(tr.get("fraction", 1.0)) >= 1.0:
			best = spot
			break
	_cam.global_position = best
	_cam.look_at(chest, Vector3.UP)


static func _q3(v: Vector3) -> Vector3:
	return Vector3(v.z, v.x, v.y) / Q3.UNIT


func _finish() -> void:
	set_process(false)
	if out != "" and not _images.is_empty():
		var w := 640
		var h := int(w * float(_images[0].get_height()) / _images[0].get_width())
		var cols := mini(4, _images.size())
		var sheet := Image.create(w * cols, h * ceili(_images.size() / float(cols)), false, Image.FORMAT_RGB8)
		for i in _images.size():
			var img := _images[i]
			img.convert(Image.FORMAT_RGB8)
			img.resize(w, h, Image.INTERPOLATE_BILINEAR)
			sheet.blit_rect(img, Rect2i(0, 0, w, h), Vector2i((i % cols) * w, (i / cols) * h))
		sheet.save_png(out.path_join("sheet.png"))
	_ok = _ok and _checked > 0
	print("SKINS %d bodies checked: %s" % [_checked, "PASS" if _ok else "FAIL"])
	Engine.time_scale = 1.0
	Q3Server.exec_now("killserver")
	get_tree().quit(0 if _ok else 1)
