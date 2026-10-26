class_name Q3RaceGhostRecorder
extends RefCounted
## Samples your run for the ghost (RACE_PLAN.md D4): about 60 a second from the playerState while the
## timer runs. A record saves them beside Q3RaceRecords; Q3RaceGhost plays them back in the next run.
##   a sample: [run ms, origin x y z, pitch, yaw, legsAnim, torsoAnim, movementDir]  (id units and degrees)

const STEP_MS := 16
const FIELDS := 10
const MAGIC := "Q3GH1"

var model := "sarge"
var skin := "default"
var samples := PackedFloat32Array()
var _last := -1000


func begin() -> void:
	samples = PackedFloat32Array()
	_last = -1000


func stop() -> void:
	pass


func sample(run_ms: int, ps: Dictionary) -> void:
	if run_ms < 0 or run_ms - _last < STEP_MS or ps.is_empty():
		return
	_last = run_ms
	var o: Vector3 = ps["origin"]
	var va: Vector3 = ps["viewangles"]
	samples.append_array(PackedFloat32Array([run_ms, o.x, o.y, o.z, va.x, va.y,
		int(ps["legsAnim"]), int(ps["torsoAnim"]), int(ps.get("movementDir", 0)), 0.0]))


## Keeps the samples as the map's ghost. Returns whether the file was written.
func save(map: String, ms: int) -> bool:
	if samples.size() < FIELDS * 2:
		return false
	DirAccess.make_dir_recursive_absolute(Q3RaceRecords.ghost_dir)
	var f := FileAccess.open(Q3RaceRecords.ghost_path(map), FileAccess.WRITE)
	if f == null:
		return false
	var head := {"magic": MAGIC, "map": map, "ms": ms, "model": model, "skin": skin, "fields": FIELDS}
	var raw := var_to_bytes([head, samples])
	f.store_32(raw.size())
	f.store_buffer(raw.compress(FileAccess.COMPRESSION_ZSTD))
	return true
