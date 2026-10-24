extends Node
## Frame-time probe for a release build (tools/perf/build_perf.py loads it as an autoload through
## override.cfg). Measures the Gameplay scene once a match is up. Args, plain like map=:
##   perf=<seconds measured> perf_warm=<seconds first, default 8> perf_out=<json of every frame>
##   perf_set=k=v;k=v   Settings overrides (in memory; a later save in the game can keep them)
##   perf_move=0        stand still (default: run forward, fire, turn)
##   perf_shadows=0 / perf_lights=0 / perf_lightmask=world / perf_shadow_top=<n>   map light experiments
##   perf_hold=1        keep every effect texture, shader, prop and character loaded up front
##   perf_bench=1       time single calls: model loads, instancing, each Q3FX effect, a corpse burning
##   perf_ab=<prefix>   frozen frame shot four ways: as built, no light shadows, lights on the world only, no lights
##   perf_shot=<png> / perf_fxshot=<png>   a shot when measuring starts / effects spawned in view, 0.25 s on
##   perf_scene=<res path>   change to that scene once the main menu is up (run a test from the build)
## Prints PERF lines: fps, frame / GPU / render-CPU percentiles, stalls with phase times and the nodes
## created (PERFTL), and stalls after _process (PERFTAIL: timers, tweens, deferred calls, frees).

var _args := {}
var _game: Node
var _t := 0.0
var _done := false
var _frames: Array = []
var _vp: RID
var _early: Node
var _last_us := 0
var _phys_us := 0
var _phys_steps := 0
var _phys_t0 := 0
var _proc_t0 := 0
var _srv_last := 0
var _pre_us := 0
var _post_us := 0
var _end_us := 0
var _removed := {}   ## class -> count of nodes leaving the tree since the last process
var _tail: Array = []   ## per frame: [after-process to pre-draw, draw, post-draw to next frame] ms


## Runs first in each physics step and process: stamps the phase starts.
class Early extends Node:
	var probe: Node
	func _ready() -> void:
		process_priority = -100000
		process_physics_priority = -100000
	func _physics_process(_d: float) -> void:
		probe._phys_t0 = Time.get_ticks_usec()
	func _process(_d: float) -> void:
		probe._proc_t0 = Time.get_ticks_usec()


var _held: Array = []
var _fx_n := 0
var _fx_t := 0
var _added: Array = []     ## [usec, description, parent] of nodes entering the tree this frame
var _timelines: Array = []


func _on_added(n: Node) -> void:
	if _proc_t0 == 0 or _added.size() > 400:
		return
	var s: Script = n.get_script()
	_added.append([Time.get_ticks_usec(), "%s %s<%s>" % [n.get_class(), n.name, s.resource_path.get_file() if s else ""],
		String(n.get_parent().name) if n.get_parent() else ""])


func _physics_process(_d: float) -> void:
	if _phys_t0 > 0:
		_phys_us += Time.get_ticks_usec() - _phys_t0
		_phys_steps += 1


func _ready() -> void:
	process_physics_priority = 100000
	_early = Early.new()
	_early.probe = self
	add_child(_early)
	process_priority = 1000
	for a in OS.get_cmdline_args() + OS.get_cmdline_user_args():
		var i := String(a).find("=")
		if i > 0:
			_args[String(a).substr(0, i)] = String(a).substr(i + 1)
	if _args.has("perf_scene"):
		get_tree().process_frame.connect(func():
			var cs := get_tree().current_scene
			if cs and cs.scene_file_path.contains("main_menu") and not _args.has("_switched"):
				_args["_switched"] = true
				print("PERF switching to ", _args["perf_scene"])
				get_tree().change_scene_to_file.call_deferred(_args["perf_scene"]))
	if not _args.has("perf"):
		set_process(false)
		return
	_vp = get_viewport().get_viewport_rid()
	get_tree().node_added.connect(_on_added)
	get_tree().node_removed.connect(func(n: Node): _removed[n.get_class()] = _removed.get(n.get_class(), 0) + 1)
	RenderingServer.frame_pre_draw.connect(func(): _pre_us = Time.get_ticks_usec())
	RenderingServer.frame_post_draw.connect(func(): _post_us = Time.get_ticks_usec())
	RenderingServer.viewport_set_measure_render_time(_vp, true)
	for kv in String(_args.get("perf_set", "")).split(";", false):
		var p := kv.split("=")
		var cur: Variant = Settings.get_value(p[0])
		var v: Variant = p[1]
		if cur is bool:
			v = p[1] in ["true", "1", "on"]
		elif cur is int:
			v = int(p[1])
		elif cur is float:
			v = float(p[1])
		Settings.set_value(p[0], v, false)
		print("PERF set ", p[0], " = ", v)


func _process(delta: float) -> void:
	if _done:
		return
	var scene := get_tree().current_scene
	if _game == null:
		if scene and scene.scene_file_path.ends_with("gameplay.tscn") and scene.get("map_scene") != null:
			_game = scene
			_setup()
		return
	_t += delta
	if _args.get("perf_move", "1") != "0" and _game.get("_view") != null:
		_game._view.y += delta * 50.0
	if _t < float(_args.get("perf_warm", "8")):
		return
	if _args.has("perf_ab"):
		_ab_step()
		return
	if _args.has("perf_fxshot"):
		_fx_n += 1
		if _fx_n == 1:
			seed(12345)
			var cam := get_viewport().get_camera_3d()
			var f := -cam.global_basis.z
			var o := cam.global_position + f * 3.0
			var r := cam.global_basis.x
			Q3FX.explosion(_game.client, "rocket", o - r * 1.2, -f)
			Q3FX.explosion(_game.client, "plasma", o + r * 1.2 + Vector3.UP * 0.6, -f)
			Q3FX.pickup(_game.client, o + Vector3.DOWN * 0.8, Color(0.2, 0.6, 1.0))
			Q3FX.rail(_game.client, o - r * 2.0 + Vector3.UP * 1.0, o + r * 2.0 + Vector3.UP * 1.2)
			Q3FX.gibs(_game.client, o)
			_fx_t = Time.get_ticks_msec()
		elif _fx_t > 0 and Time.get_ticks_msec() - _fx_t >= 250:
			_fx_t = -1
			get_viewport().get_texture().get_image().save_png(_args["perf_fxshot"])
	if _args.get("perf_bench", "0") == "1" and _frames.is_empty():
		_bench()
	if _args.has("perf_shot") and _frames.is_empty():
		get_viewport().get_texture().get_image().save_png(_args["perf_shot"])
	var now := Time.get_ticks_usec()
	if _end_us and _pre_us > _end_us and _post_us >= _pre_us:
		_tail.append([_t, (_pre_us - _end_us) / 1000.0, (_post_us - _pre_us) / 1000.0, (_proc_t0 - _post_us) / 1000.0])
		if (_pre_us - _end_us) > 20000:
			var adds := {}
			for e in _added:
				if e[0] < _proc_t0:
					adds[e[1].get_slice(" ", 0) + "/" + e[2]] = adds.get(e[1].get_slice(" ", 0) + "/" + e[2], 0) + 1
			_timelines.append("t=%.2f after-process %.1f ms: added %s removed %s" % [_t, (_pre_us - _end_us) / 1000.0, adds, _removed])
	_removed = {}
	var srv: int = Q3Server.time()
	# [t, frame ms (wall), gpu, render cpu, draws, process ms, nodes, physics ms, physics steps, server ms advanced]
	_frames.append([_t, (now - _last_us) / 1000.0 if _last_us else delta * 1000.0, RenderingServer.viewport_get_measured_render_time_gpu(_vp),
		RenderingServer.viewport_get_measured_render_time_cpu(_vp) + RenderingServer.get_frame_setup_time_cpu(),
		Performance.get_monitor(Performance.RENDER_TOTAL_DRAW_CALLS_IN_FRAME),
		(now - _proc_t0) / 1000.0, Performance.get_monitor(Performance.OBJECT_NODE_COUNT),
		_phys_us / 1000.0, _phys_steps, srv - _srv_last])
	_srv_last = srv
	if (now - _proc_t0) > 25000 and _timelines.size() < 40:
		var lines: Array = ["t=%.2f process %.1f ms, %d nodes added" % [_t, (now - _proc_t0) / 1000.0, _added.size()]]
		var prev := _proc_t0
		var run := ""
		var count := 0
		for e in _added:
			var key: String = e[1].get_slice(" ", 0) + " under " + e[2]
			if e[0] - prev > 2000 or key != run:
				if count > 0:
					lines.append("    x%d %s" % [count, run])
				if e[0] - prev > 2000:
					lines.append("  +%.1f ms gap before: %s (under %s)" % [(e[0] - prev) / 1000.0, e[1], e[2]])
				run = key
				count = 0
			count += 1
			prev = e[0]
		if count > 0:
			lines.append("    x%d %s" % [count, run])
		lines.append("  +%.1f ms to the end of process" % ((now - prev) / 1000.0))
		_timelines.append("\n".join(lines))
	_added.clear()
	_last_us = now
	_end_us = Time.get_ticks_usec()
	_phys_us = 0
	_phys_steps = 0
	if _t < float(_args.get("perf_warm", "8")) + float(_args["perf"]):
		return
	_done = true
	_report()
	Input.action_release("q3_forward")
	Input.action_release("q3_attack")
	Q3Server.exec_now("killserver")
	Session.quit_game()


func _setup() -> void:
	var lights := 0
	var shadowed := 0
	for n in _game.map_scene.find_children("*", "Light3D", true, false):
		var l := n as Light3D
		lights += 1
		if l.shadow_enabled:
			shadowed += 1
		if _args.get("perf_shadows", "1") == "0" and not (l is DirectionalLight3D):
			l.shadow_enabled = false
		if _args.get("perf_lightmask", "") == "world" and l.light_bake_mode == Light3D.BAKE_STATIC:
			l.light_cull_mask = Q3Map.WORLD_LAYER
		if _args.get("perf_lights", "1") == "0" and l.light_bake_mode == Light3D.BAKE_STATIC:
			l.visible = false
	print("PERF map lights %d, shadowed %d" % [lights, shadowed])
	if _args.get("perf_hold", "0") == "1":
		var t0 := Time.get_ticks_msec()
		for k in Q3.fx_textures:
			_held.append(Q3.fx_texture(k))
		for p in ["fx_sprite_add", "fx_sprite_mix", "fx_scroll_add", "powerup_shell", "fx_heat", "character_dissolve", "fx_puff", "fx_disc", "fx_spark"]:
			_held.append(load("res://shaders/%s.gdshader" % p))
		for s in ["res://scripts/fx/fx_rail_rings.gd", "res://scripts/fx/fx_projectile.gd"]:
			_held.append(load(s))
		for p in Q3.props:
			_held.append(Q3.prop_scene(p))
		for c in Q3.characters:
			_held.append(Q3.character_scene(c))
		print("PERF held %d resources in %d ms" % [_held.size(), Time.get_ticks_msec() - t0])
	if _args.has("perf_shadow_top"):
		var ls: Array = []
		for n in _game.map_scene.find_children("*", "Light3D", true, false):
			if not (n is DirectionalLight3D) and (n as Light3D).light_bake_mode == Light3D.BAKE_STATIC:
				ls.append(n)
		ls.sort_custom(func(a, b): return a.light_energy > b.light_energy)
		for i in ls.size():
			(ls[i] as Light3D).shadow_enabled = i < int(_args["perf_shadow_top"])
	if _args.get("perf_move", "1") != "0":
		if "test_input" in _game:
			_game.test_input = true
		Input.action_press("q3_forward")
		Input.action_press("q3_attack")


## Same spot, same frame-to-frame scene: shots with the map's static lights as built (a), without
## their shadows (b), lighting only the world, i.e. not players/guns/items (c), hidden (d).
var _ab := 0
func _ab_step() -> void:
	_ab += 1
	var ls: Array = []
	for n in _game.map_scene.find_children("*", "Light3D", true, false):
		if not (n is DirectionalLight3D) and (n as Light3D).light_bake_mode == Light3D.BAKE_STATIC:
			ls.append(n)
	var pre: String = _args["perf_ab"]
	match _ab:
		1:
			Engine.time_scale = 0.0
		6:
			get_viewport().get_texture().get_image().save_png(pre + "_a.png")
			for l in ls: l.shadow_enabled = false
		12:
			get_viewport().get_texture().get_image().save_png(pre + "_b.png")
			for l in ls: l.shadow_enabled = true; l.light_cull_mask = Q3Map.WORLD_LAYER
		18:
			get_viewport().get_texture().get_image().save_png(pre + "_c.png")
			for l in ls: l.visible = false
		24:
			get_viewport().get_texture().get_image().save_png(pre + "_d.png")
			_done = true
			Engine.time_scale = 1.0
			Q3Server.exec_now("killserver")
			Session.quit_game()


func _us(c: Callable, n := 3) -> String:
	var out: Array = []
	for i in n:
		var t := Time.get_ticks_usec()
		c.call()
		out.append("%.2f" % ((Time.get_ticks_usec() - t) / 1000.0))
	return " ".join(out)


func _bench() -> void:
	var q: Object = get_node("/root/Q3")
	print("PERFB Q3 has held %s hold_models %s source len %d" % [q.has_method("held"), q.has_method("hold_models"), (q.get_script() as GDScript).source_code.length()])
	print("PERFB props ", q.get_property_list().filter(func(d): return String(d["name"]).begins_with("_")).map(func(d): return d["name"]))
	var gib: String = Q3.fx["gibs"]["models"][0]
	var path := Q3.LIB + "props/%s.glb" % gib
	print("PERFB exists(prop) ms: ", _us(func(): ResourceLoader.exists(path)))
	print("PERFB prop_scene ms: ", _us(func(): Q3.prop_scene(gib)))
	var holder := Node3D.new()
	_game.add_child(holder)
	print("PERFB instance_prop ms: ", _us(func(): holder.add_child(Q3.instance_prop(gib))))
	var ch: String = Q3.characters[0]
	print("PERFB character_scene ms: ", _us(func(): Q3.character_scene(ch)))
	print("PERFB character instantiate ms: ", _us(func(): holder.add_child(Q3.character_scene(ch).instantiate())))
	print("PERFB Curve+CurveTexture ms: ", _us(func():
		var curve := Curve.new(); curve.add_point(Vector2(0, 1.0)); curve.add_point(Vector2(1, 0.2))
		var ct := CurveTexture.new(); ct.curve = curve))
	print("PERFB Gradient+GradientTexture1D ms: ", _us(func():
		var g := Gradient.new(); var gt := GradientTexture1D.new(); gt.gradient = g))
	print("PERFB ParticleProcessMaterial ms: ", _us(func():
		var pm := ParticleProcessMaterial.new(); pm.spread = 70.0; pm.gravity = Vector3(0, -7, 0)))
	print("PERFB StandardMaterial3D ms: ", _us(func():
		var mat := StandardMaterial3D.new(); mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		mat.billboard_mode = BaseMaterial3D.BILLBOARD_PARTICLES; mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
		mat.proximity_fade_enabled = true))
	print("PERFB GPUParticles3D new ms: ", _us(func(): var p := GPUParticles3D.new(); p.amount = 26; p.free()))
	print("PERFB Q3FX.pickup ms: ", _us(func(): Q3FX.pickup(holder, Vector3.ZERO, Color.RED), 4))
	print("PERFB Q3FX.gibs ms: ", _us(func(): Q3FX.gibs(holder, Vector3.ZERO), 3))
	for k in ["rocket", "plasma", "rail", "bullet", "grenade", "bfg", "lightning"]:
		print("PERFB Q3FX.explosion %s ms: " % k, _us(func(): Q3FX.explosion(holder, k, Vector3.ZERO, Vector3.UP), 2))
	for i in 3:
		var c := Q3Character.new()
		holder.add_child(c)
		c.load_model(Q3.characters[1 + i])
		print("PERFB disintegrate ms: ", _us(func(): c.disintegrate(), 1))
	holder.queue_free()


func _pct(a: Array, q: float) -> float:
	return a[mini(int(a.size() * q), a.size() - 1)]


func _report() -> void:
	var n := _frames.size()
	var dt: Array = []
	var gpu: Array = []
	var cpu: Array = []
	var script: Array = []
	var dc := 0.0
	for f in _frames:
		dt.append(f[1]); gpu.append(f[2]); cpu.append(f[3]); script.append(f[5]); dc += f[4]
	var sdt := dt.duplicate(); sdt.sort()
	var sg := gpu.duplicate(); sg.sort()
	var sc := cpu.duplicate(); sc.sort()
	var ss := script.duplicate(); ss.sort()
	var total := 0.0
	for x in dt:
		total += x
	var med: float = _pct(sdt, 0.5)
	print("PERF frames %d  fps %.1f  frame median %.2f p99 %.2f max %.2f ms" % [n, 1000.0 * n / total, med, _pct(sdt, 0.99), sdt[-1]])
	print("PERF gpu median %.2f p99 %.2f | render cpu median %.2f p99 %.2f | script median %.2f p99 %.2f | draw calls avg %.0f" % [
		_pct(sg, 0.5), _pct(sg, 0.99), _pct(sc, 0.5), _pct(sc, 0.99), _pct(ss, 0.5), _pct(ss, 0.99), dc / n])
	var hitches: Array = []
	for f in _frames:
		if f[1] > maxf(med * 2.0, 25.0):
			hitches.append(f)
	print("PERF hitches (>2x median and >25 ms): %d" % hitches.size())
	hitches.sort_custom(func(a, b): return a[1] > b[1])
	for h in hitches.slice(0, 15):
		print("PERF   t=%.2f frame %.1f gpu %.1f rcpu %.1f draws %d process %.1f nodes %d physics %.1f (%d steps) server +%d ms" % h)
	for tl in _tail:
		if tl[1] + tl[2] + tl[3] > 20.0:
			print("PERFTAIL t=%.2f after-process %.1f draw %.1f post-draw %.1f" % tl)
	for tl in _timelines:
		print("PERFTL ", tl)
	if _args.has("perf_out"):
		var f := FileAccess.open(_args["perf_out"], FileAccess.WRITE)
		if f:
			f.store_string(JSON.stringify({"args": _args, "frames": _frames}))
