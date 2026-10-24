extends Node
## Frame times in a real (windowed) bot match: what costs frames, and what causes the stalls.
##   godot --path . res://tests/match_perf.tscn -- map=q3dm6 seconds=40 bots=5 set=graphics/ao=off,graphics/reflections=false
## `set=` overrides settings for this run only (nothing is saved). The local player runs, fires and
## turns. Prints the frame rate, median / p99 / worst frame (wall clock: `delta` is smoothed), the
## GPU and render-thread times, and every stall (a frame over 2x the median and 25 ms) with what
## it spent its time on and the nodes created in it. A stall with normal GPU and render times is
## script or server work; the nodes created say which effect (2026-10-24: per-shot reloads of
## effect shaders, first-use shader compiles, every map light's shadow redrawn each frame).

var seconds := 40.0
var warmup := 6.0
var _game: Node
var _t := 0.0
var _vp: RID
var _last_us := 0
var _frames: Array = []      ## [t, frame ms, gpu ms, render cpu ms, draw calls, nodes created]
var _added: Dictionary = {}  ## class -> count, this frame


func _ready() -> void:
	var map := "q3dm17"
	var bots := 5
	for a in OS.get_cmdline_user_args():
		if a.begins_with("seconds="):
			seconds = float(a.substr(8))
		elif a.begins_with("map="):
			map = a.substr(4)
		elif a.begins_with("bots="):
			bots = int(a.substr(5))
		elif a.begins_with("set="):
			for kv in a.substr(4).split(","):
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
	var s := Session.new_setup("ffa", map)
	s.bots = []
	var names := ["Sarge", "Xaero", "Crash", "Visor", "Grunt", "Hunter", "Bones", "Orbb"]
	for i in bots:
		s.bots.append({"name": names[i % names.size()], "skill": 3, "team": ""})
	Session.setup = s
	_vp = get_viewport().get_viewport_rid()
	RenderingServer.viewport_set_measure_render_time(_vp, true)
	get_tree().node_added.connect(func(n: Node): _added[n.get_class()] = _added.get(n.get_class(), 0) + 1)
	process_priority = 1000   # after Gameplay and the client
	_game = load("res://scenes/gameplay/gameplay.tscn").instantiate()
	_game.test_input = true   # fire without a captured mouse
	add_child(_game)
	Input.action_press("q3_forward")
	Input.action_press("q3_attack")


func _process(delta: float) -> void:
	_t += delta
	_game._view.y += delta * 50.0
	var now := Time.get_ticks_usec()
	if _t > warmup and _last_us:
		_frames.append([_t, (now - _last_us) / 1000.0, RenderingServer.viewport_get_measured_render_time_gpu(_vp),
			RenderingServer.viewport_get_measured_render_time_cpu(_vp) + RenderingServer.get_frame_setup_time_cpu(),
			Performance.get_monitor(Performance.RENDER_TOTAL_DRAW_CALLS_IN_FRAME), _added.duplicate()])
	_last_us = now
	_added.clear()
	if _t < warmup + seconds:
		return
	set_process(false)
	Input.action_release("q3_forward")
	Input.action_release("q3_attack")
	_report()
	Q3Server.exec_now("killserver")
	get_tree().quit(0)


func _report() -> void:
	var n := _frames.size()
	if n == 0:
		print("PERF no frames")
		return
	var ms: Array = []
	var gpu: Array = []
	var cpu: Array = []
	var total := 0.0
	var draws := 0.0
	for f in _frames:
		ms.append(f[1])
		gpu.append(f[2])
		cpu.append(f[3])
		total += f[1]
		draws += f[4]
	ms.sort()
	gpu.sort()
	cpu.sort()
	var median: float = ms[n / 2]
	var stall := maxf(median * 2.0, 25.0)
	print("PERF %s: %d frames, %.0f fps; frame median %.2f, p99 %.2f, worst %.2f ms" % [
		Session.setup.map, n, 1000.0 * n / total, median, ms[int(n * 0.99)], ms[n - 1]])
	print("PERF gpu median %.2f, p99 %.2f ms; render cpu median %.2f, p99 %.2f ms; %.0f draw calls" % [
		gpu[n / 2], gpu[int(n * 0.99)], cpu[n / 2], cpu[int(n * 0.99)], draws / n])
	var stalls := _frames.filter(func(f): return f[1] > stall)
	print("PERF stalls (over %.1f ms): %d" % [stall, stalls.size()])
	stalls.sort_custom(func(a, b): return a[1] > b[1])
	for f in stalls.slice(0, 15):
		var made: Array = (f[5] as Dictionary).keys().map(func(k): return "%s x%d" % [k, f[5][k]])
		print("PERF   t=%.2f %.1f ms (gpu %.1f, render cpu %.1f, the rest script/server) made: %s" % [
			f[0], f[1], f[2], f[3], ", ".join(made) if made.size() else "nothing"])
