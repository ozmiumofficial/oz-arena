extends Node
## Windowed check of a frag in a real match: devmap q3dm17, give all, the test player rails the
## bot (setviewpos next to it), then keeps looking at the corpse while the bot respawns, saving
## frames as it falls, lies there and burns away (10 s after the respawn). Prints what happened.
##   godot --path . res://tests/corpse_test.tscn -- out=C:/temp/corpse [map=q3dm0] [weapon=3]

var out := "user://corpse"
var map := "q3dm17"
var weapon := 7
var _game: Q3Gameplay
var _t := 0.0
var _hop := 0.0
var _side := 0
var _corpse: Q3Character
var _killed_at := -1.0
var _shots := [0.3, 1.0, 2.5, 6.0, 11.0, 12.2, 13.0, 14.5]
var _shot := 0


func _ready() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("out="):
			out = a.substr(4)
		elif a.begins_with("map="):
			map = a.substr(4)
		elif a.begins_with("weapon="):
			weapon = int(a.substr(7))
	Engine.max_fps = 60
	var s := Session.new_setup("ffa", map)
	s.bots = [{"name": "Sarge", "skill": 1, "team": ""}]
	s.rules = {"fraglimit": 50, "timelimit": 0}
	s.set_meta("devmap", true)
	Session.setup = s
	_game = load("res://scenes/gameplay/gameplay.tscn").instantiate()
	_game.test_input = true
	add_child(_game)


func _bot() -> int:
	for cl in 8:
		if cl != _game.me and Q3Server.client_state(cl) >= 4:
			return cl
	return -1


func _process(delta: float) -> void:
	_t += delta
	if _t > 2.0 and _t - delta <= 2.0:
		Q3Server.client_command(_game.me, "give all")
		Q3Server.client_command(_game.me, "god")
	var bot := _bot()
	if bot < 0:
		return
	var e: Dictionary = _game.client._ents.get(bot, {})
	if _killed_at < 0.0:
		_game._select_weapon(weapon)
		if _t > 3.0 and not Input.is_action_pressed("q3_attack"):
			Input.action_press("q3_attack")
		_hop -= delta
		var ps := Q3Server.player_state(bot)
		if _hop <= 0.0 and _t > 3.0 and not ps.is_empty():
			_hop = 1.5
			_side += 1
			var at: Vector3 = ps["origin"]
			var a := _side * TAU / 8.0
			Q3Server.client_command(_game.me, "setviewpos %d %d %d 0" % [int(at.x + cos(a) * 120.0), int(at.y + sin(a) * 120.0), int(at.z) + 8])
		if not ps.is_empty():
			_aim(ps["origin"])
		if e.get("ragdolled", false):
			_killed_at = _t
			_corpse = e["node"]
			Input.action_release("q3_attack")
			var bodies := _corpse.find_children("Phys_*", "PhysicalBone3D", true, false).size()
			print("fragged at %.1f s: ragdoll with %d bodies, character at %s" % [_t, bodies, _corpse.global_position])
			var space := _corpse.get_world_3d().direct_space_state
			var q := PhysicsRayQueryParameters3D.create(_corpse.global_position + Vector3.UP, _corpse.global_position + Vector3.DOWN * 5.0, 1)
			print("  floor under it: ", space.intersect_ray(q).get("position"))
			var mis := _corpse.find_children("*", "MeshInstance3D", true, false)
			for mi in mis:
				var m := mi as MeshInstance3D
				print("  mesh %s visible %s in tree %s layers %d aabb %s skin %s" % [m.name, m.visible, m.is_visible_in_tree(), m.layers, m.get_aabb(), m.skeleton])
			var sk := _corpse.skeleton
			print("  skeleton pelvis pose (global): ", sk.global_transform * sk.get_bone_global_pose(sk.find_bone("pelvis")).origin, " tree active ", _corpse.tree.active)
	elif is_instance_valid(_corpse):
		var pelvis: Vector3 = _corpse.global_position
		for pb in _corpse.find_children("Phys_pelvis", "PhysicalBone3D", true, false):
			pelvis = (pb as PhysicalBone3D).global_position
		_aim(Vector3(pelvis.z, pelvis.x, pelvis.y) * 32.0)
		if _shot < _shots.size() and _t > _killed_at + _shots[_shot]:
			var k := _shot
			_shot += 1
			await RenderingServer.frame_post_draw
			get_viewport().get_texture().get_image().save_png("%s_%d.png" % [out, k])
			var low := INF
			var low_name := ""
			for pb in _corpse.find_children("Phys_*", "PhysicalBone3D", true, false):
				if (pb as Node3D).global_position.y < low:
					low = (pb as Node3D).global_position.y
					low_name = pb.name
			print("  +%.1f s: corpse at %s, pelvis body at %s, lowest part %s at y %.2f, in corpses %s" % [_t - _killed_at,
				_corpse.global_position.snapped(Vector3.ONE * 0.01), pelvis.snapped(Vector3.ONE * 0.01), low_name, low,
				_game.client._corpses.has(_corpse)])
	if _killed_at > 0.0 and (_t > _killed_at + 16.0 or (not is_instance_valid(_corpse) and _shot >= 6)):
		print("corpse freed: %s after %.1f s" % [not is_instance_valid(_corpse), _t - _killed_at])
		print("CORPSE done")
		Q3Server.exec_now("killserver")
		get_tree().quit()


## Turn the view to a point (id units).
func _aim(target: Vector3) -> void:
	var me := Q3Server.player_state(_game.me)
	if me.is_empty():
		return
	var d: Vector3 = target - (me["origin"] + Vector3(0, 0, float(me["viewheight"])))
	var da: PackedInt32Array = me["delta_angles"]
	_game._view = Vector3(-rad_to_deg(atan2(d.z, Vector2(d.x, d.y).length())) - da[0] * 360.0 / 65536.0,
		rad_to_deg(atan2(d.y, d.x)) - da[1] * 360.0 / 65536.0, 0)
