class_name Q3DiagOverlay
extends CanvasLayer
## Diagnostics' F10 overlay, over every scene: frame time graph, frame rate, memory, draw calls,
## nodes, the error and warning counts with the latest errors, and the crash reports on file.
## (In the main menu the web view sits on top of Godot's canvas, so it shows there only faintly.)

const GRAPH := 180          ## frames kept for the frame-time graph

var _stats: Label
var _errors: Label
var _reports: Label
var _graph: Control
var _times := PackedFloat32Array()
var _tick := 0.0


func _ready() -> void:
	layer = 125
	var panel := PanelContainer.new()
	panel.theme = Q3Theme.build(false)
	panel.set_anchors_and_offsets_preset(Control.PRESET_TOP_RIGHT)
	panel.offset_left = -470
	panel.offset_right = -16
	panel.offset_top = 64
	panel.grow_horizontal = Control.GROW_DIRECTION_BEGIN
	panel.mouse_filter = Control.MOUSE_FILTER_STOP
	add_child(panel)
	var box := VBoxContainer.new()
	box.add_theme_constant_override("separation", 6)
	panel.add_child(box)
	var head := Label.new()
	head.text = "DIAGNOSTICS  ·  F10"
	head.theme_type_variation = "GroupLabel"
	box.add_child(head)
	_graph = Control.new()
	_graph.custom_minimum_size = Vector2(430, 70)
	_graph.draw.connect(_draw_graph)
	box.add_child(_graph)
	_stats = _label(box, 14)
	_errors = _label(box, 13)
	_errors.add_theme_color_override("font_color", Q3Theme.GLOW.lightened(0.3))
	_reports = _label(box, 13)
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 8)
	box.add_child(row)
	for b in [["OPEN REPORTS", func(): Diagnostics.open_reports_folder()],
			["COPY ERRORS", func(): DisplayServer.clipboard_set("\n".join(Diagnostics.logger.snapshot()))]]:
		var btn := Button.new()
		btn.text = b[0]
		btn.theme_type_variation = "SmallButton"
		btn.focus_mode = Control.FOCUS_NONE
		btn.pressed.connect(b[1])
		row.add_child(btn)
	_refresh()


func _label(parent: Control, size: int) -> Label:
	var l := Label.new()
	l.add_theme_font_size_override("font_size", size)
	l.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	l.custom_minimum_size.x = 430
	parent.add_child(l)
	return l


func _process(delta: float) -> void:
	if not visible:
		return
	_times.append(delta * 1000.0)
	if _times.size() > GRAPH:
		_times = _times.slice(_times.size() - GRAPH)
	_graph.queue_redraw()
	_tick += delta
	if _tick >= 0.25:
		_tick = 0.0
		_refresh()


func _refresh() -> void:
	var s := Diagnostics.state()
	var worst := 0.0
	for t in _times:
		worst = maxf(worst, t)
	_stats.text = "%d fps · %.1f ms process · worst %.1f ms\nmemory %s MB · video %s MB · %d draw calls\n%d nodes · %d objects · up %d s\nscene %s%s" % [
		s["fps"], s["frame_ms"], worst, s["memory_mb"], s["video_mb"], s["draw_calls"], s["nodes"], s["objects"],
		s["uptime_s"], s["scene"].get_file(), ("\n" + str(s["context"])) if s["context"].size() else ""]
	var lines := Diagnostics.logger.snapshot()
	var last := PackedStringArray()
	for i in range(lines.size() - 1, -1, -1):
		if not lines[i].contains(" WARNING: "):
			last.append(lines[i].get_slice("\n", 0))
		if last.size() >= 5:
			break
	_errors.text = "%d errors · %d warnings%s" % [s["errors"], s["warnings"], ("\n" + "\n".join(last)) if last.size() else ""]
	var reps := Diagnostics.reports()
	var crashes := reps.filter(func(r): return r["kind"] != "errors").size()
	_reports.text = "%d crash/freeze reports on file%s · session %s" % [crashes,
			(" · %d new since the last run" % Diagnostics.new_reports.size()) if Diagnostics.new_reports.size() else "", Diagnostics.session_id]


func _draw_graph() -> void:
	var r := Rect2(Vector2.ZERO, _graph.size)
	_graph.draw_rect(r, Color(0, 0, 0, 0.35))
	# 16.7 ms (60 fps) and 33.3 ms (30 fps) lines; the graph tops out at 50 ms
	for ms: float in [16.7, 33.3]:
		var y := r.size.y * (1.0 - ms / 50.0)
		_graph.draw_line(Vector2(0, y), Vector2(r.size.x, y), Color(1, 1, 1, 0.15))
	var n := _times.size()
	if n < 2:
		return
	var pts := PackedVector2Array()
	for i in n:
		pts.append(Vector2(r.size.x * i / float(GRAPH - 1), r.size.y * (1.0 - minf(_times[i], 50.0) / 50.0)))
	_graph.draw_polyline(pts, Q3Theme.AMBER, 1.5)
