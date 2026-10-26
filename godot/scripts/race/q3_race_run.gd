class_name Q3RaceRun
extends RefCounted
## A race run as the game reports it (RACE_PLAN.md; tools/q3core/thirdparty/ioq3 g_race.c, patch
## 0012). Pure logic: Gameplay feeds it the server's "race ..." commands and the HUD, the menu's
## records and the tests read it. Times are server milliseconds.
##   race reset <checkpoints> <best ms>      spawned: nothing is running
##   race start <server time>                the timer started
##   race cp <n> <ms>                        checkpoint n reached
##   race stop <ms> <record 0|1> <best ms>   finished

signal started
signal checkpoint(n: int, ms: int, split: int)   ## split: ms against the best run's same checkpoint, 0 if none
signal finished(ms: int, record: bool)
signal reset

enum State { IDLE, RUNNING, FINISHED }

var state := State.IDLE
var start_time := 0          ## server time of the start
var checkpoints_total := 0   ## target_checkpoints in the map
var splits := PackedInt32Array()       ## this run's checkpoint times
var best_splits := PackedInt32Array()  ## the best run's, kept to compare against
var best_ms := 0             ## your best finished run on this map, 0 = none (load_best, then every record)
var last_ms := 0             ## the last finished run
var last_record := false     ## the last finished run was a new best
var session_best_ms := 0     ## the game's own best since the map loaded (race reset / stop)


## The best run on file (Q3RaceRecords): what splits are compared against.
func load_best(ms: int, best: PackedInt32Array) -> void:
	best_ms = ms
	best_splits = best


func is_running() -> bool:
	return state == State.RUNNING


## Elapsed ms at a server time while running, else the last result (0 if none).
func elapsed(server_time: int) -> int:
	if state == State.RUNNING:
		return maxi(server_time - start_time, 0)
	return last_ms if state == State.FINISHED else 0


## One "race ..." server command; returns whether it was one.
func feed(command: String) -> bool:
	var a := command.strip_edges().split(" ", false)
	if a.size() < 2 or a[0] != "race":
		return false
	match a[1]:
		"reset":
			state = State.IDLE
			checkpoints_total = int(a[2]) if a.size() > 2 else 0
			session_best_ms = int(a[3]) if a.size() > 3 else session_best_ms
			splits = PackedInt32Array()
			reset.emit()
		"start":
			state = State.RUNNING
			start_time = int(a[2]) if a.size() > 2 else 0
			splits = PackedInt32Array()
			started.emit()
		"cp":
			if a.size() < 4 or state != State.RUNNING:
				return true
			var n := int(a[2])
			var ms := int(a[3])
			splits.append(ms)
			var split := 0
			if n >= 1 and n <= best_splits.size():
				split = ms - best_splits[n - 1]
			checkpoint.emit(n, ms, split)
		"stop":
			if a.size() < 5 or state != State.RUNNING:
				return true
			state = State.FINISHED
			last_ms = int(a[2])
			session_best_ms = int(a[4])
			# a record is against the best on file, which outlives the game's own count since the map loaded
			var record := best_ms == 0 or last_ms < best_ms
			last_record = record
			if record:
				best_ms = last_ms
				best_splits = splits.duplicate()
			finished.emit(last_ms, record)
	return true


## "1:02.345", "12.345" or "0.987": a run time as players read it.
static func format_time(ms: int) -> String:
	var m := ms / 60000
	var s := (ms % 60000) / 1000
	var t := ms % 1000
	if m > 0:
		return "%d:%02d.%03d" % [m, s, t]
	return "%d.%03d" % [s, t]
