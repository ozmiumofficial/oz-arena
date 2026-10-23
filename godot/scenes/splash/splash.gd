extends Control
## Launch splash: the fan-project disclaimer, shown while the main menu and its first showcase
## model load in the background. Continues on its own once loaded and shown for MIN_SECONDS;
## any key, click or pad button skips ahead once loading is done.

const MIN_SECONDS := 5.0
const SKIP_AFTER := 1.5
## The statements the self-test checks for.
const LINES := [
	"Q3Remaster is a non-commercial fan project. It is not made, endorsed or supported by id Software, Bethesda or Microsoft.",
	"All models, textures, sounds and maps are derived from id Software's Quake III Arena and belong to id Software.",
	"Your own copy of Quake III Arena is required to play.",
]
const SETUP_GATE := preload("res://scenes/splash/setup_gate.gd")

var _t := 0.0
var _loaded := false
var _leaving := false
var _status: Label
var _bar: ColorRect
var _paths: PackedStringArray = []
var _box: Control
var _gate: Control   ## the setup screen, while no verified Quake III Arena is known


func _ready() -> void:
	set_anchors_preset(Control.PRESET_FULL_RECT)
	theme = Q3Theme.build(Settings.get_value("accessibility/high_contrast"))
	_build()
	# stream the menu and its first subject in while the disclaimer is up
	_paths.append(Session.MAIN_MENU)
	ResourceLoader.load_threaded_request(Session.MAIN_MENU)
	if Q3.characters.size():
		var first := "char:" + Q3.characters[randi() % Q3.characters.size()]
		Session.set_meta("first_subject", first)
		Q3ShowcaseStage.preload_subject(first)
		_paths.append_array(Q3ShowcaseStage.paths_for(first))
	modulate.a = 0.0
	create_tween().tween_property(self, "modulate:a", 1.0, 0.6)


func _build() -> void:
	var bg := ColorRect.new()
	bg.color = Color(0.012, 0.01, 0.01)
	bg.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(bg)
	var glow := TextureRect.new()
	var g := GradientTexture2D.new()
	g.fill = GradientTexture2D.FILL_RADIAL
	g.fill_from = Vector2(0.5, 0.5)
	g.fill_to = Vector2(1.0, 0.5)
	var grad := Gradient.new()
	grad.set_color(0, Color(0.35, 0.04, 0.02, 0.35))
	grad.set_color(1, Color(0, 0, 0, 0))
	g.gradient = grad
	glow.texture = g
	glow.set_anchors_preset(Control.PRESET_FULL_RECT)
	glow.stretch_mode = TextureRect.STRETCH_SCALE
	add_child(glow)

	var box := VBoxContainer.new()
	box.set_anchors_preset(Control.PRESET_CENTER)
	box.custom_minimum_size = Vector2(980, 0)
	box.position = Vector2(-490, -200)
	box.add_theme_constant_override("separation", 22)
	box.alignment = BoxContainer.ALIGNMENT_CENTER
	add_child(box)
	_box = box
	var head := Label.new()
	head.text = "FAN PROJECT"
	head.theme_type_variation = "HeaderLabel"
	head.add_theme_font_override("font", Q3Theme.banner_font(10))
	head.add_theme_font_size_override("font_size", 52)
	head.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	box.add_child(head)
	var rule := ColorRect.new()
	rule.color = Color(Q3Theme.RED, 0.7)
	rule.custom_minimum_size = Vector2(0, 2)
	box.add_child(rule)
	for i in LINES.size():
		var l := Label.new()
		l.text = LINES[i]
		l.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		l.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
		l.add_theme_font_size_override("font_size", 26 if i < 2 else 30)
		l.add_theme_color_override("font_color", Q3Theme.TEXT_HOT if i == 2 else Q3Theme.TEXT)
		if i == 2:
			l.add_theme_font_override("font", Q3Fonts.prop())
			l.add_theme_color_override("font_outline_color", Color(0.5, 0.05, 0.02))
			l.add_theme_constant_override("outline_size", 6)
		l.name = "Line%d" % i
		box.add_child(l)

	_bar = ColorRect.new()
	_bar.color = Q3Theme.RED
	_bar.anchor_left = 0.5
	_bar.anchor_right = 0.5
	_bar.anchor_top = 1.0
	_bar.anchor_bottom = 1.0
	_bar.offset_left = -200
	_bar.offset_top = -92
	_bar.offset_bottom = -90
	_bar.offset_right = -200
	add_child(_bar)
	_status = Label.new()
	_status.text = "LOADING"
	_status.add_theme_font_override("font", Q3Fonts.prop())
	_status.add_theme_font_size_override("font_size", 18)
	_status.add_theme_color_override("font_color", Q3Theme.DIM)
	_status.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_status.anchor_left = 0.5
	_status.anchor_right = 0.5
	_status.anchor_top = 1.0
	_status.anchor_bottom = 1.0
	_status.offset_left = -300
	_status.offset_right = 300
	_status.offset_top = -78
	_status.offset_bottom = -50
	add_child(_status)


## Every disclaimer line, for the self-test.
func text() -> String:
	var out: PackedStringArray = []
	for c in find_children("Line*", "Label", true, false):
		out.append((c as Label).text)
	return "\n".join(out)


func _process(delta: float) -> void:
	_t += delta
	var done := 0.0
	var all := true
	for p in _paths:
		var prog := []
		var s := ResourceLoader.load_threaded_get_status(p, prog)
		if s == ResourceLoader.THREAD_LOAD_IN_PROGRESS:
			all = false
			done += float(prog[0]) if prog.size() else 0.0
		else:
			done += 1.0
	var frac := done / maxf(float(_paths.size()), 1.0)
	_bar.offset_right = lerpf(_bar.offset_right, -200.0 + 400.0 * frac, 0.2)
	if all and not _loaded:
		_loaded = true
		ResourceLoader.load_threaded_get(Session.MAIN_MENU)   # keeps it cached for the scene change
	if Q3.missing_packs.size():
		_status.text = "MISSING %s: PUT IT NEXT TO Q3Remaster.exe" % ", ".join(Q3.missing_packs)
		return
	if _loaded:
		_status.text = "PRESS ANY KEY" if _t >= SKIP_AFTER else "LOADING"
		_status.modulate.a = 0.55 + 0.45 * sin(_t * 4.0) if not Settings.get_value("accessibility/reduced_flashing") else 1.0
		if _t >= MIN_SECONDS:
			_leave()


func _input(ev: InputEvent) -> void:
	var pressed := (ev is InputEventKey or ev is InputEventMouseButton or ev is InputEventJoypadButton) and ev.is_pressed()
	if pressed and _loaded and _t >= SKIP_AFTER:
		_leave()


func _leave() -> void:
	if _leaving or _gate or Q3.missing_packs.size():
		return
	if Q3Ownership.verified() == "":
		_show_gate()
		return
	_leaving = true
	var tw := create_tween()
	tw.tween_property(self, "modulate:a", 0.0, 0.5)
	tw.tween_callback(Session.after_splash)


## No verified Quake III Arena yet: the disclaimer gives way to the setup screen, and the game goes
## on only once the player's retail pak0.pk3 is accepted.
func _show_gate() -> void:
	for c in [_box, _bar, _status]:
		c.visible = false
	_gate = SETUP_GATE.new()
	_gate.verified.connect(func():
		_gate.queue_free()
		_gate = null
		_leave())
	add_child(_gate)
