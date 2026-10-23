extends Node
## Main menu scene. Three layers:
##   1. ShowcaseStage: the 3D void with the featured model (Godot)
##   2. the banner5 logo in a transparent SubViewport, placed where the web menu reserves it
##   3. the menu itself: the Next.js app in webui/, running in a transparent godot_wry WebView
## The web page and Godot talk through Q3MenuBridge, over a Q3WebLayer. Godot keeps the settings,
## the match setup, the sounds and the controller; the page does the drawing, keyboard and mouse.
## Command-line options for captures and tests (after --): subject=char:sarge|item:weapon_railgun|
## prop:<path>, framing=0-3, swap=<seconds>, page=<menu page id, or "friends" for the Friends panel>.

## Quake 3's main-menu logo (q3_ui ui_menu.c MAIN_BANNER_MODEL). It reads "QUAKE III ARENA",
## which PLAN.md's licensing mitigation 3 says to drop: swap this one constant once the project
## has its own name.
const TITLE_PROP := "mapobjects/banner/banner5"

var stage: Q3ShowcaseStage
var bridge: Q3MenuBridge
var web_layer: Q3WebLayer       ## the web view (scripts/ui/q3_web_layer.gd)
var logo_view: SubViewportContainer
var logo: Node3D
var _logo_vp: SubViewport
var _logo_box := AABB()      ## the banner model's bounds, for fitting it to the page's slot
var _open_page := ""


func _ready() -> void:
	stage = Q3ShowcaseStage.new()
	add_child(stage)
	bridge = Q3MenuBridge.new()
	bridge.post.connect(_post)
	bridge.hovered.connect(stage.nudge)
	bridge.layout_changed.connect(_on_layout)
	bridge.profile_preview.connect(stage.lock_character)
	bridge.quit_requested.connect(func(): if not Session.dry_run: get_tree().quit())
	bridge.cinematic_requested.connect(play_cinematic)
	stage.subject_changed.connect(func(n, k): _post({"type": "subject", "name": n, "kind": k}))
	_args()
	_build_logo()
	_build_web()
	UiSound.play_in()


func _args() -> void:
	for a in OS.get_cmdline_user_args():
		var kv := a.split("=", true, 1)
		if kv.size() != 2:
			continue
		match kv[0]:
			"subject": stage.show_subject(kv[1], true)
			"framing":
				stage.fixed_framing = int(kv[1])
				stage._next_framing(true)
			"swap": stage.swap_seconds = Vector2(float(kv[1]), float(kv[1]))
			"page": _open_page = kv[1]


# ---------------------------------------------------------------- the logo

## q3_ui draws banner5 into a 640x120 strip at 60 degrees horizontal fov, 300 units ahead and
## 32 below the eye, its yaw swaying 5 * sin(t / 5000) around facing the viewer.
func _build_logo() -> void:
	var layer := CanvasLayer.new()
	layer.layer = 1
	add_child(layer)
	logo_view = SubViewportContainer.new()
	logo_view.stretch = true
	logo_view.mouse_filter = Control.MOUSE_FILTER_IGNORE
	logo_view.size = Vector2(480, 90)
	logo_view.position = Vector2(56, 32)
	layer.add_child(logo_view)
	_logo_vp = SubViewport.new()
	_logo_vp.transparent_bg = true
	_logo_vp.own_world_3d = true
	_logo_vp.msaa_3d = Viewport.MSAA_4X
	logo_view.add_child(_logo_vp)
	var cam := Camera3D.new()
	cam.keep_aspect = Camera3D.KEEP_WIDTH
	cam.fov = 60.0
	_logo_vp.add_child(cam)
	var e := Environment.new()
	e.background_mode = Environment.BG_CLEAR_COLOR
	e.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	e.ambient_light_color = Color(1.0, 0.95, 0.9)
	e.ambient_light_energy = 1.8
	e.tonemap_mode = Environment.TONE_MAPPER_AGX
	e.glow_enabled = true
	e.glow_intensity = 0.4
	var we := WorldEnvironment.new()
	we.environment = e
	_logo_vp.add_child(we)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-25, 15, 0)
	sun.light_energy = 4.0
	_logo_vp.add_child(sun)
	var fill := DirectionalLight3D.new()
	fill.rotation_degrees = Vector3(-5, -25, 0)
	fill.light_energy = 1.4
	fill.light_color = Color(1.0, 0.9, 0.8)
	_logo_vp.add_child(fill)
	logo = Q3.instance_prop(TITLE_PROP)
	if logo:
		var holder := Node3D.new()
		holder.add_child(logo)
		_logo_vp.add_child(holder)
		# q3_ui's placement (300 units ahead, 32 below), pulled back if the remastered model is
		# wider than the strip so it always fits whole
		_brighten_logo(logo)
		_logo_box = _mesh_bounds(logo)
		logo.position = -_logo_box.get_center()
		logo = holder
		_fit_logo(640.0 / 120.0)


## The banner's flames are Quake 3's additive stages (scrolling, pulsing on a sine wave that dips
## to black). As the title they should always read: brighter, and pulsing only near full. The
## materials are copied, so the same prop elsewhere keeps the original shader values.
func _brighten_logo(n: Node) -> void:
	for mi in n.find_children("*", "MeshInstance3D", true, false):
		var mesh: Mesh = (mi as MeshInstance3D).mesh
		for i in mesh.get_surface_count():
			var sm := mesh.surface_get_material(i) as ShaderMaterial
			if sm == null or not String(sm.shader.resource_path).contains("q3_stages"):
				continue
			sm = sm.duplicate() as ShaderMaterial
			sm.set_shader_parameter("overbright_add", 2.4)
			for k in 3:
				sm.set_shader_parameter("wave%d" % k, Vector4(0.85, 0.15, 0.0, 0.2))
			(mi as MeshInstance3D).set_surface_override_material(i, sm)


## Pulls the camera back just far enough that the whole banner fits the slot (q3_ui's 300 units
## is the minimum), for the slot's real shape.
func _fit_logo(aspect: float) -> void:
	if logo == null:
		return
	var t := tan(deg_to_rad(30.0))   # half of the 60 degree horizontal fov
	var fit := maxf(_logo_box.size.x * 1.04 * 0.5 / t, _logo_box.size.y * 1.08 * 0.5 / (t / aspect))
	logo.position = Vector3(0, 0, -maxf(Q3.q3_to_godot(Vector3(300, 0, -32)).z, fit))


func _mesh_bounds(n: Node3D) -> AABB:
	var box := AABB()
	var first := true
	for m in n.find_children("*", "MeshInstance3D", true, false):
		var mi := m as MeshInstance3D
		var b := n.global_transform.affine_inverse() * mi.global_transform * mi.get_aabb()
		box = b if first else box.merge(b)
		first = false
	return box


func _on_layout(m: Dictionary) -> void:
	var r: Dictionary = m.get("logo", {})
	if r.is_empty():
		return
	# the box comes as fractions of the page (x, y, w, h in 0-1), so neither display scaling nor
	# the stretch mode can shift it: page fraction * this viewport's visible size
	var vis := get_viewport().get_visible_rect().size
	logo_view.position = Vector2(float(r["x"]), float(r["y"])) * vis
	logo_view.size = Vector2(float(r["w"]), float(r["h"])) * vis
	_fit_logo(logo_view.size.x / maxf(logo_view.size.y, 1.0))


# ---------------------------------------------------------------- the web menu

func _build_web() -> void:
	web_layer = Q3WebLayer.new(bridge)
	add_child(web_layer)
	web_layer.build()
	bridge.received_ready.connect(func():
		# the caption for the subject on show goes with the page's state (the first subject is
		# built before the page exists)
		if stage.info.has("name"):
			_post({"type": "subject", "name": stage.info["name"], "kind": stage.info["kind"]}))


func _post(msg: Dictionary) -> void:
	if msg.get("type") == "init" and _open_page != "":
		# capture option page=...: open it once the menu has drawn
		get_tree().create_timer(0.6).timeout.connect(func(): _post({"type": "open", "page": _open_page}))
	web_layer.post(msg)


## A movie from the Cinematics page: the web view hides (it always draws on top) and comes back after.
func play_cinematic(id: String) -> void:
	web_layer.set_shown(false)
	var p := Q3CinematicPlayer.play(self, PackedStringArray([id]))
	p.finished.connect(func():
		web_layer.set_shown(true)
		UiSound.out())


func _process(_delta: float) -> void:
	if logo:
		logo.rotation.y = deg_to_rad(5.0 * sin(float(Time.get_ticks_msec()) / 5000.0))
