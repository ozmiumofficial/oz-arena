@tool
extends Node
## Bakes a queue of maps in the open editor, one step per few frames, through bake_maps.gd's
## static steps (rebuild, bake, save, measure). A node rather than a coroutine: the editor
## reloads changed tool scripts, which kills a suspended coroutine but not a node's _process.
## Start from the editor bridge:
##   var r = load("res://tools/bake_runner.gd").new(); r.queue = PackedStringArray([...])
##   EditorInterface.get_base_control().add_child(r)
## Progress and results: library/maps/bake_report.json. The node frees itself when done.
## A bake pumps the editor's frames, so _process runs again inside a step; _busy makes those
## nested calls return at once (without it the runner recursed until the editor crashed).
## Don't send bridge calls while it runs (they'd run nested too); watch the report file instead.

const B := "res://tools/bake_maps.gd"
var queue: PackedStringArray = []
var _i := 0
var _step := 0
var _wait := 0
var _t := 0
var _r := {}
var _report := {"done": [], "current": "", "finished": false}
var _busy := false


func _ready() -> void:
	_report["started"] = Time.get_datetime_string_from_system()
	_write()


func _process(_d: float) -> void:
	if _busy:
		return
	_busy = true
	_tick()
	_busy = false


func _tick() -> void:
	if _wait > 0:
		_wait -= 1
		return
	var fs := EditorInterface.get_resource_filesystem()
	if fs.is_scanning() or fs.is_importing():
		_wait = 30
		return
	if _i >= queue.size():
		_report["current"] = ""
		_report["finished"] = true
		_write()
		queue_free()
		return
	var map := queue[_i]
	var b: GDScript = load(B)
	match _step:
		0:
			_t = Time.get_ticks_msec()
			_r = {"map": map}
			_report["current"] = map
			_write()
			_r["rebuild"] = b.rebuild(map)
			_step = 1 if _r["rebuild"]["ok"] else 3
		1:
			_r["bake"] = b.bake(map)
			_step = 2
		2:
			_r["save"] = b.save(map)
			_r["light"] = b.measure(map)
			EditorInterface.close_scene()
			_step = 3
		3:
			_r["seconds"] = (Time.get_ticks_msec() - _t) / 1000.0
			_report["done"].append(_r)
			_write()
			_i += 1
			_step = 0
	_wait = 10


func _write() -> void:
	var f := FileAccess.open("res://library/maps/bake_report.json", FileAccess.WRITE)
	if f:
		f.store_string(JSON.stringify(_report, " "))
