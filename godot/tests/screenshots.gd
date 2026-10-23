extends Node
## Screenshots for the README and promotion: a real match against bots, the player given every
## weapon, turning toward the nearest bot in sight and firing, with frames saved (HUD included)
## while a bot is in view. Windowed (needs a GPU):
##   godot --path . res://tests/screenshots.tscn -- window=1920x1080 out=C:/temp/shot map=q3dm6
##     [bots=5] [skill=3] [weapons=5,7,6] [count=8] [every=2.5] [seconds=60] [hud=0]
## Writes <out>_<n>.png (the weapon changes after each shot, through `weapons`) and prints
## "SCREENSHOTS done".

const BOTS := ["Sarge", "Xaero", "Klesk", "Doom", "Anarki", "Hunter", "Major", "Orbb"]
const MASK_SHOT := 0x6000001
const PICKUP_NAMES := ["", "Gauntlet", "Machinegun", "Shotgun", "Grenade Launcher", "Rocket Launcher",
	"Lightning Gun", "Railgun", "Plasma Gun", "BFG10K"]   ## bg_misc.c pickup_name, for "give"

var out := "user://shot"
var map := "q3dm17"
var bots := 5
var skill := 3
var weapons := [5, 7, 6, 8]
var count := 8
var every := 2.5
var seconds := 60.0
var hud := true          ## hud=0 hides the HUD (key art)
var _game: Q3Gameplay
var _t := 0.0
var _given := false
var _next_shot := 6.0
var _shot := 0
var _wander := 0.0


func _ready() -> void:
	for a in OS.get_cmdline_user_args():
		var kv := a.split("=", true, 1)
		if kv.size() != 2:
			continue
		match kv[0]:
			"out": out = kv[1]
			"map": map = kv[1]
			"bots": bots = int(kv[1])
			"skill": skill = int(kv[1])
			"weapons": weapons = Array(kv[1].split(",")).map(func(w): return int(w))
			"count": count = int(kv[1])
			"every": every = float(kv[1])
			"seconds": seconds = float(kv[1])
			"hud": hud = kv[1] != "0"
	Engine.max_fps = 60
	var s := Session.new_setup("ffa", map)
	s.bots = []
	for i in bots:
		s.bots.append({"name": BOTS[i % BOTS.size()], "skill": skill, "team": ""})
	s.set_meta("devmap", true)   # cheats, for "give"
	Session.setup = s
	_game = load("res://scenes/gameplay/gameplay.tscn").instantiate()
	_game.test_input = true   # fire without a captured mouse
	add_child(_game)


func _process(delta: float) -> void:
	_t += delta
	if _t > seconds:
		_finish()
		return
	if _game == null or _game.me < 0:
		return
	var ps := Q3Server.player_state(_game.me)
	if ps.is_empty():
		return
	if not hud:
		_game.hud.visible = false
	if not _given and _t > 2.0:
		_given = true
		# weapons by name, as if picked up twice, so the HUD's ammo looks like play (not give all's 999)
		for w in weapons:
			for k in 2:
				Q3Server.client_command(_game.me, "give " + PICKUP_NAMES[w])
		_game._select_weapon(weapons[0])
	var alive := int(ps["stats"][Q3Gameplay.E.STAT_HEALTH]) > 0
	var eye: Vector3 = ps["origin"] + Vector3(0, 0, float(ps["viewheight"]))
	var target := _nearest_visible(eye) if alive else Vector3.INF
	if target != Vector3.INF:
		_aim(ps, target - eye, delta)
		Input.action_release("q3_forward")
		Input.action_press("q3_attack")
		if _given and _t >= _next_shot:
			await _save()
	else:
		Input.action_release("q3_attack")
		# roam: run forward, turning now and then, so the next bot comes into view
		Input.action_press("q3_forward")
		_wander -= delta
		if _wander <= 0.0:
			_wander = randf_range(1.0, 2.5)
			_game._view.y += randf_range(-120.0, 120.0)
		_game._view.x = lerpf(_game._view.x, 0.0, minf(delta * 4.0, 1.0))
	if not alive:
		Input.action_press("q3_attack")   # respawn


## The closest live bot the player can see, in id coordinates (aimed at the chest), or INF.
func _nearest_visible(eye: Vector3) -> Vector3:
	var best := Vector3.INF
	var best_d := INF
	for num in _game.client._ents:
		var e: Dictionary = _game.client._ents[num]
		var es: Dictionary = e["es"]
		if int(es["eType"]) != Q3Gameplay.E.ET_PLAYER or int(num) == _game.me:
			continue
		if int(es["eFlags"]) & Q3Gameplay.E.EF_DEAD:
			continue
		var at: Vector3 = es["pos"]["trBase"] + Vector3(0, 0, 8)
		var d := eye.distance_to(at)
		if d > best_d or d > 2500.0:
			continue
		var tr := Q3Server.trace(eye, at, Vector3.ZERO, Vector3.ZERO, _game.me, MASK_SHOT)
		if tr.is_empty() or (float(tr["fraction"]) < 1.0 and int(tr["entityNum"]) != int(num)):
			continue
		best = at
		best_d = d
	return best


## Turns the view toward `dir`. The usercmd angles differ from the view by delta_angles, so the
## correction goes on top of the current command angles.
func _aim(ps: Dictionary, dir: Vector3, delta: float) -> void:
	var want_yaw := rad_to_deg(atan2(dir.y, dir.x))
	var want_pitch := -rad_to_deg(atan2(dir.z, Vector2(dir.x, dir.y).length()))
	var va: Vector3 = ps["viewangles"]
	var k := minf(delta * 10.0, 1.0)
	_game._view.y += wrapf(want_yaw - va.y, -180.0, 180.0) * k
	_game._view.x += wrapf(want_pitch - va.x, -180.0, 180.0) * k


func _save() -> void:
	_next_shot = _t + every
	await RenderingServer.frame_post_draw
	var path := "%s_%d.png" % [out, _shot]
	get_viewport().get_texture().get_image().save_png(path)
	print("saved ", path)
	_shot += 1
	if _shot >= count:
		_finish()
		return
	_game._select_weapon(weapons[_shot % weapons.size()])


func _finish() -> void:
	set_process(false)
	Input.action_release("q3_attack")
	Input.action_release("q3_forward")
	print("SCREENSHOTS done")
	get_tree().quit()
