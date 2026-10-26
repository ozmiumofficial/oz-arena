class_name Q3HudRace
extends Control
## The race HUD (RACE_PLAN.md D4), top center in place of the match clock: the run timer, the
## checkpoint you were last through with its split against your best run, your best, and on the
## finish the time with NEW RECORD or how far off it was. The numbers come from Q3RaceRun.

const S := preload("res://scenes/gameplay/hud/hud_style.gd")
const FLASH_TIME := 3.0
const FINISH_TIME := 6.0
const AHEAD := Color("#7fe08a")
const BEHIND := Color("#ff6a52")

var hud: Q3Hud
var run: Q3RaceRun
var now_ms := 0                ## the server time, for the running clock
var next_text := ""            ## the playlist's next course and its countdown, under the finish
var practice := false          ## a loaded position: the timer is off
var _flash_t := 99.0
var _flash_text := ""
var _flash_color := S.INK
var _finish_t := 99.0
var _finish_text := ""
var _finish_color := S.INK
var _finish_sub := ""


## A checkpoint: its number, time and split against the best run (0 = no best to compare with).
func note_checkpoint(n: int, ms: int, split: int, total: int) -> void:
	_flash_t = 0.0
	var head := "CHECKPOINT %d%s  %s" % [n, "/%d" % total if total > 0 else "", Q3RaceRun.format_time(ms)]
	if split == 0:
		_flash_text = head
		_flash_color = S.INK_HOT
	else:
		_flash_text = "%s   %s%s" % [head, "+" if split > 0 else "-", Q3RaceRun.format_time(absi(split))]
		_flash_color = BEHIND if split > 0 else AHEAD


## The finish: a record, or the gap to your best.
func note_finish(ms: int, record: bool, previous_best: int) -> void:
	_finish_t = 0.0
	_flash_t = 99.0   # the last checkpoint's line would sit on the result
	_finish_text = Q3RaceRun.format_time(ms)
	if record:
		_finish_color = S.MEGA
		_finish_sub = "NEW RECORD" if previous_best <= 0 else "NEW RECORD  -%s" % Q3RaceRun.format_time(previous_best - ms)
	else:
		_finish_color = S.AMBER
		_finish_sub = "+%s  OFF YOUR BEST" % Q3RaceRun.format_time(ms - previous_best) if previous_best > 0 else ""


func note_reset() -> void:
	_flash_t = 99.0
	_finish_t = 99.0


func _process(delta: float) -> void:
	_flash_t += delta
	_finish_t += delta
	queue_redraw()


func _draw() -> void:
	if run == null or hud == null:
		return
	var u := hud.u
	var cx := size.x * 0.5
	var timer_r := Rect2(Vector2(cx - 118 * u, 0), Vector2(236 * u, 62 * u))
	var shown := 0
	var col := S.DIM
	match run.state:
		Q3RaceRun.State.RUNNING:
			shown = run.elapsed(now_ms)
			col = S.INK_HOT
		Q3RaceRun.State.FINISHED:
			shown = run.last_ms
			col = S.MEGA if run.last_record else S.AMBER
	var accent := Color(S.RED, 0.9) if run.state == Q3RaceRun.State.RUNNING else Color(0, 0, 0, 0)
	S.panel(self, timer_r, S.GLASS, accent, 3.0 * u)
	S.text(self, S.num(), Vector2(cx, timer_r.get_center().y + 17 * u), Q3RaceRun.format_time(shown), int(46 * u), col,
		HORIZONTAL_ALIGNMENT_CENTER)
	var sub := ""
	if practice:
		sub = "PRACTICE: TIMER OFF"
	elif run.state == Q3RaceRun.State.IDLE:
		sub = "CROSS THE START LINE"
	elif run.state == Q3RaceRun.State.RUNNING and run.checkpoints_total > 0:
		sub = "CHECKPOINT %d/%d" % [run.splits.size(), run.checkpoints_total]
	S.text(self, S.label(), Vector2(cx, timer_r.end.y + 17 * u), sub, int(13 * u), Color(S.DIM, 0.95), HORIZONTAL_ALIGNMENT_CENTER)
	if run.best_ms > 0:
		S.text(self, S.label(), Vector2(cx, timer_r.end.y + 36 * u), "BEST  " + Q3RaceRun.format_time(run.best_ms), int(13 * u),
			Color(S.MEGA, 0.85), HORIZONTAL_ALIGNMENT_CENTER)
	# a checkpoint: just under the clock, then fading
	if _flash_t < FLASH_TIME:
		var a := clampf((FLASH_TIME - _flash_t) / 0.7, 0.0, 1.0)
		S.text(self, S.num_medium(), Vector2(cx, timer_r.end.y + 66 * u), _flash_text, int(24 * u), Color(_flash_color, a),
			HORIZONTAL_ALIGNMENT_CENTER)
	if next_text != "":
		S.text(self, S.label(), Vector2(cx, size.y * 0.36 + 100 * u), next_text, int(24 * u), S.AMBER, HORIZONTAL_ALIGNMENT_CENTER)
	# the finish: the middle of the screen
	if _finish_t < FINISH_TIME:
		var a2 := clampf((FINISH_TIME - _finish_t) / 1.0, 0.0, 1.0)
		var pop := 1.0 + 0.25 * maxf(0.0, 1.0 - _finish_t * 4.0)
		var mid := Vector2(cx, size.y * 0.36)
		S.text(self, S.num(), mid, _finish_text, int(86 * u * pop), Color(_finish_color, a2), HORIZONTAL_ALIGNMENT_CENTER)
		if _finish_sub != "":
			S.text(self, S.label(), mid + Vector2(0, 44 * u), _finish_sub, int(26 * u), Color(_finish_color, a2), HORIZONTAL_ALIGNMENT_CENTER)
		S.text(self, S.label(), mid + Vector2(0, 76 * u), "R: RESTART", int(15 * u), Color(S.DIM, a2), HORIZONTAL_ALIGNMENT_CENTER)
