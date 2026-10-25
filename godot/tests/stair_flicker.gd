extends Node
## Windowed check for pops while the eye moves up and down (stairs): a camera bobs 0.7 m at a map's
## spawn along a fixed path, and the second difference of consecutive frames (a smooth path has small
## ones; a pop is a spike) is reported.
##   godot --path . res://tests/stair_flicker.tscn -- scene=res://library/maps/pukka3tourney2/pukka3tourney2.tscn probes=0 spawn=1
var scene := "res://library/maps/pukka3tourney2/pukka3tourney2.tscn"
var probes := true
var shafts := true
var spawn := 0
var frames := 420
var amp := 0.35
var spin := false
var pmode := ""
var period := 90
var off := ""
var dump := "user://sf"
var _saved := 0
var _show
var _cam: Camera3D
var _base := Transform3D.IDENTITY
var _n := 0
var _prev: Array = []
var _hist: Array = []
var _cyc: Array = []
var _d2: Array = []


func _ready() -> void:
	for a in OS.get_cmdline_user_args():
		var kv := a.split("=", true, 1)
		if kv.size() == 2:
			match kv[0]:
				"scene": scene = kv[1]
				"probes": probes = kv[1] != "0"
				"shafts": shafts = kv[1] != "0"
				"spawn": spawn = int(kv[1])
				"frames": frames = int(kv[1])
				"amp": amp = float(kv[1])
				"spin": spin = kv[1] == "1"
				"pmode": pmode = kv[1]
				"off": off = kv[1]
				"dump": dump = kv[1]
	Engine.max_fps = 60
	if spin:
		period = 360
	_show = load(scene).instantiate()
	add_child(_show)
	if not probes and _show.has_node("Probes"):
		_show.get_node("Probes").free()
	if not shafts and _show.has_node("ShaftSun"):
		_show.get_node("ShaftSun").free()
	for we in _show.find_children("*", "WorldEnvironment", true, false):
		var env := (we as WorldEnvironment).environment
		for o in off.split(",", false):
			match o:
				"ssao": env.ssao_enabled = false
				"ssr": env.ssr_enabled = false
				"ssil": env.ssil_enabled = false
				"glow": env.glow_enabled = false
				"vfog": env.volumetric_fog_enabled = false
				"fog": env.fog_enabled = false
				"sdfgi": env.sdfgi_enabled = false
	if off.contains("aa"):
		get_viewport().msaa_3d = Viewport.MSAA_DISABLED
		get_viewport().screen_space_aa = Viewport.SCREEN_SPACE_AA_DISABLED
		get_viewport().use_taa = false
	if pmode != "":
		for pr in _show.find_children("*", "ReflectionProbe", true, false):
			(pr as ReflectionProbe).update_mode = ReflectionProbe.UPDATE_ALWAYS if pmode == "always" else ReflectionProbe.UPDATE_ONCE
			if pmode == "always":
				(pr as ReflectionProbe).max_distance = 60.0
	_cam = Camera3D.new()
	_cam.fov = 74.0
	_cam.far = 400.0
	add_child(_cam)
	var spawns: Array = _show.entities_of("info_player_deathmatch")
	_base = _show.view_transform(spawns[spawn % spawns.size()])
	_cam.global_transform = _base
	_cam.make_current()


func _process(_d: float) -> void:
	_n += 1
	var t := float(_n)
	_cam.global_transform = _base
	if spin:
		_cam.global_transform = Transform3D(Basis(Vector3.UP, TAU * t / 360.0) * _base.basis, _base.origin)
	_cam.global_position += Vector3(0, amp * sin(t * TAU / 90.0), 0) + _base.basis.z * 0.0
	if _n < 30:
		return
	var img := get_viewport().get_texture().get_image()
	img.resize(160, 90, Image.INTERPOLATE_BILINEAR)
	img.convert(Image.FORMAT_L8)
	var cur := Array(img.get_data())
	if _prev.size() == cur.size():
		var s := 0.0
		for i in range(0, cur.size(), 3):
			s += absf(float(cur[i]) - float(_prev[i]))
		_d2.append(s / (cur.size() / 3.0))
		if _d2[-1] > 0.3:
			print("DD frame ", _n, " ", snappedf(_d2[-1], 0.01))
		if _d2[-1] > 1.0 and _saved < 6:
			_saved += 1
			get_viewport().get_texture().get_image().save_png("%s_%d.png" % [dump, _n])
			print("SPIKE frame ", _n, " diff ", _d2[-1])
	_hist.append(cur)
	if _hist.size() > period and _n > 400:
		var o: Array = _hist[_hist.size() - 1 - period]
		var q := 0.0
		for i in range(0, cur.size(), 3):
			q += absf(float(cur[i]) - float(o[i]))
		_cyc.append(q / (cur.size() / 3.0))
		if _cyc[-1] > 0.3:
			print("POP frame ", _n, " yaw ", _n % 360, " ", snappedf(_cyc[-1], 0.01))
	if _hist.size() > period + 10:
		_hist.pop_front()
	_prev = cur
	if _n >= frames:
		var d: Array = []
		for i in range(1, _d2.size()):
			d.append(absf(_d2[i] - _d2[i - 1]))
		var sorted := d.duplicate()
		sorted.sort()
		var med: float = sorted[sorted.size() / 2]
		var spikes := 0
		for x in d:
			if x > maxf(3.0 * med, 0.6):
				spikes += 1
		print("FPS ", Engine.get_frames_per_second())
		var cs := _cyc.duplicate()
		cs.sort()
		if cs.size():
			print("CYCLE median=%.3f p90=%.3f max=%.3f n=%d" % [cs[cs.size() / 2], cs[int(cs.size() * 0.9)], cs[-1], cs.size()])
		print("SEQ ", _d2.slice(100, 140).map(func(x): return snappedf(x, 0.01)))
		print("FLICKER probes=%s shafts=%s median_dd=%.3f max_dd=%.3f spikes=%d of %d" % [probes, shafts, med, sorted[-1], spikes, d.size()])
		get_tree().quit()
