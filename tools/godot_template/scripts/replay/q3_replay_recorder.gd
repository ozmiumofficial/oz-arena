class_name Q3ReplayRecorder
extends RefCounted
## Writes one match to a replay file while it plays (REPLAY_PLAN.md R1). q3core records on the
## server (engine/q3_replay.c: every entity and every player's playerState, 40 times a second,
## delta and Huffman coded, a keyframe every 5 s); this class writes the file around those frames
## (Q3ReplayFile has the layout): the header when the match starts, each frame block as it's
## pulled, the trailer with the results when it ends. A file is written as it goes, so a crash
## still leaves a replay that plays to where it stopped.

const HZ := 40

var path := ""
var _file: FileAccess
var _start_time := 0
var _end_time := 0
var _frames := 0


## Starts recording the running match. `header` is what the browser lists (map, mode, title,
## players ...); the format fields are added here. "" on success, else why not.
func start(header: Dictionary, file_path := "") -> String:
	if not Q3Server.running():
		return "no match is running"
	DirAccess.make_dir_recursive_absolute(Q3ReplayFile.dir)
	path = file_path if file_path != "" else Q3ReplayFile.new_path(String(header.get("map", "map")), String(header.get("mode", "ffa")))
	_file = FileAccess.open(path, FileAccess.WRITE)
	if _file == null:
		return "can't write %s (%s)" % [path, error_string(FileAccess.get_open_error())]
	var h := header.duplicate(true)
	h["format"] = Q3ReplayFile.VERSION
	h["rate"] = HZ
	h["date"] = Time.get_datetime_string_from_system(false, true)
	h["unix"] = int(Time.get_unix_time_from_system())
	h["map_checksum"] = Q3Server.cvar("sv_mapChecksum")
	h["q3core"] = Q3Core.source_version() if ClassDB.class_exists("Q3Core") else ""
	h["game_version"] = String(ProjectSettings.get_setting("application/config/version", ""))
	var json := JSON.stringify(h).to_utf8_buffer()
	_file.store_buffer(Q3ReplayFile.MAGIC.to_ascii_buffer())
	_file.store_32(Q3ReplayFile.VERSION)
	_file.store_32(json.size())
	_file.store_buffer(json)
	_start_time = Q3Server.time()
	_end_time = _start_time
	if not Q3Server.record_start(HZ):
		_file = null
		DirAccess.remove_absolute(path)
		return "q3core: " + Q3Server.error()
	return ""


func recording() -> bool:
	return _file != null


## Every frame after the server runs: the frames recorded since the last pull go to the file.
func pump() -> void:
	if _file == null:
		return
	var bytes := Q3Server.record_pull()
	if bytes.size():
		_file.store_buffer(bytes)
		_end_time = Q3Server.time()
	_frames = Q3Server.record_frames()


## Ends the file with the trailer (`results`: the final standings, the winner, how it ended) and
## keeps the library to Gameplay > Replays kept. Returns the file's path ("" if nothing was recorded).
func finish(results := {}) -> String:
	if _file == null:
		return ""
	Q3Server.record_stop()   # (writes one last frame for commands that came after the last one)
	pump()
	var t := results.duplicate(true)
	t["frames"] = _frames
	t["start_time"] = _start_time
	t["end_time"] = _end_time
	t["length_ms"] = maxi(_end_time - _start_time, 0)
	var json := JSON.stringify(t).to_utf8_buffer()
	_file.store_buffer(json)
	_file.store_32(json.size())
	_file.store_buffer(Q3ReplayFile.TRAILER_MAGIC.to_ascii_buffer())
	_file.close()
	_file = null
	if _frames == 0:
		DirAccess.remove_absolute(path)
		return ""
	Q3ReplayFile.clean(int(Settings.get_value("gameplay/replays_kept")), path.get_file())
	return path
