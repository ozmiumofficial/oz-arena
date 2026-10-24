class_name Q3ReplayFeed
extends Q3Feed
## A recorded match as Gameplay's feed (REPLAY_PLAN.md R2). The playhead is server time in ms;
## Gameplay moves it, then steps the decoder frame by frame (each frame's snapshot goes to Q3Client
## once, so every event plays) or jumps (seeking: Q3Client is rebuilt from the target frame with
## its sounds and one-shot effects muted). Between two recorded frames (40 a second) the watched
## player's view is interpolated as cgame's CG_InterpolatePlayerState does; Q3Client interpolates
## the other players (set_next). Only the map's collision is loaded, so effects that trace (the
## lightning beam, shotgun pellets, impacts) hit the world and the frame's players.

const E := preload("res://scripts/game/q3_enums.gd")
const EF_TELEPORT_BIT := 4
## A jump further ahead than this (frames, 2 s) seeks instead of stepping through every frame.
const STEP_LIMIT := 80

var replay: Q3Replay
var header: Dictionary = {}
var playhead := 0.0          ## server time shown (ms)
var frame := -1              ## the frame Q3Client last drew
var _cmd_frame := -1         ## commands are handed out up to this frame
var _clients: PackedInt32Array = []
var _clients_frame := -2
var _serverinfo: Dictionary = {}
var _serverinfo_src := "\u0001"
var _ps_cache: Dictionary = {}
var _ps_cache_at := -1.0
var _next_snap: Dictionary = {}
var _next_key := Vector2i(-2, -2)


func is_replay() -> bool:
	return true


## Opens a replay and loads its map's collision. "" on success, else why not.
func open(path: String) -> String:
	replay = Q3Replay.new()
	var err := replay.open(path)
	if err != "":
		replay = null
		return err
	header = replay.header()
	err = Q3Replay.load_map(String(header.get("map", "")))
	if err != "":
		return "map %s: %s" % [header.get("map", "?"), err]
	if str(Q3Replay.map_checksum()) != String(header.get("map_checksum", str(Q3Replay.map_checksum()))):
		push_warning("replay: %s isn't the map this replay was recorded on (checksum)" % header.get("map"))
	jump(0)
	playhead = start_time()
	return ""


func close() -> void:
	if replay:
		replay.close()
		replay = null
	Q3Replay.unload_map()


func start_time() -> int:
	return replay.frame_time(0) if replay else 0


func end_time() -> int:
	return replay.frame_time(replay.frame_count() - 1) if replay else 0


func at_end() -> bool:
	return replay == null or playhead >= end_time()


## The frame the playhead is in.
func target_frame() -> int:
	return replay.frame_at(int(playhead)) if replay else 0


## Shows the next frame (playing forward). False at the end.
func step() -> bool:
	if replay == null or frame + 1 >= replay.frame_count() or not replay.seek(frame + 1):
		return false
	frame += 1
	return true


## Shows frame `f` from its keyframe; the commands before it are skipped.
func jump(f: int) -> bool:
	if replay == null:
		return false
	f = clampi(f, 0, replay.frame_count() - 1)
	if not replay.seek(f):
		return false
	frame = f
	_cmd_frame = f
	return true


## Whether reaching the playhead means stepping (false) or a jump (true): backward, or far ahead.
func needs_jump() -> bool:
	var t := target_frame()
	return t < frame or t - frame > STEP_LIMIT


## How far the playhead is between this frame and the next (cg.frameInterpolation).
func fraction() -> float:
	if replay == null or not replay.has_next():
		return 0.0
	var a := replay.frame_time(frame)
	var b := replay.frame_time(frame + 1)
	return clampf((playhead - a) / maxf(b - a, 1.0), 0.0, 1.0)


func time() -> int:
	return int(playhead)


func running() -> bool:
	return replay != null


func configstring(index: int) -> String:
	return replay.configstring(index) if replay else ""


func cvar(name: String) -> String:
	var src := configstring(E.CS_SERVERINFO)
	if src != _serverinfo_src:
		_serverinfo_src = src
		_serverinfo = Q3Host.parse_info(src)
	return String(_serverinfo.get(name, ""))


## The frame's own playerState (events, stats): what Q3Client takes with the snapshot.
func frame_player_state(client: int) -> Dictionary:
	return replay.player_state(client, 0) if replay else {}


## The view at the playhead: the frame's playerState with origin, velocity, view angles and bob
## interpolated toward the next frame's (CG_InterpolatePlayerState), unless the player teleported.
func player_state(client: int) -> Dictionary:
	if replay == null:
		return {}
	if _ps_cache_at != playhead or _ps_cache.get("frame", -1) != frame:
		_ps_cache = {"frame": frame}
		_ps_cache_at = playhead
	if _ps_cache.has(client):
		return _ps_cache[client]
	var a := replay.player_state(client, 0)
	var b := replay.player_state(client, 1)
	var out := a
	if not a.is_empty() and not b.is_empty() and not ((int(a["eFlags"]) ^ int(b["eFlags"])) & EF_TELEPORT_BIT):
		var f := fraction()
		out = a.duplicate()
		out["origin"] = (a["origin"] as Vector3).lerp(b["origin"], f)
		out["velocity"] = (a["velocity"] as Vector3).lerp(b["velocity"], f)
		var va: Vector3 = a["viewangles"]
		var vb: Vector3 = b["viewangles"]
		out["viewangles"] = Vector3(lerp_angle_deg(va.x, vb.x, f), lerp_angle_deg(va.y, vb.y, f), lerp_angle_deg(va.z, vb.z, f))
		var bob_b := int(b["bobCycle"])
		if bob_b < int(a["bobCycle"]):
			bob_b += 256
		out["bobCycle"] = int(int(a["bobCycle"]) + f * (bob_b - int(a["bobCycle"]))) & 255
	_ps_cache[client] = out
	return out


## q_math.c LerpAngle, degrees.
static func lerp_angle_deg(a: float, b: float, f: float) -> float:
	if b - a > 180.0:
		b -= 360.0
	if b - a < -180.0:
		b += 360.0
	return a + f * (b - a)


func clients() -> PackedInt32Array:
	if replay == null:
		return PackedInt32Array()
	if _clients_frame != frame:
		_clients_frame = frame
		_clients = replay.clients(0)
	return _clients


func client_state(client: int) -> int:
	return 4 if clients().has(client) else 0


func snapshot(client: int) -> Dictionary:
	return replay.snapshot(client, 0) if replay else {}


## The next frame's snapshot (Q3Client.set_next interpolates the other players toward it).
func next_snapshot(client: int) -> Dictionary:
	if replay == null:
		return {}
	if _next_key != Vector2i(frame, client):   # built once per recorded frame, not per render frame
		_next_key = Vector2i(frame, client)
		_next_snap = replay.snapshot(client, 1)
	return _next_snap


func next_commands(client: int) -> PackedStringArray:
	if replay == null or _cmd_frame >= frame:
		return PackedStringArray()
	var out := replay.commands(_cmd_frame, frame, client)
	_cmd_frame = frame
	return out


func trace(start: Vector3, end: Vector3, mins: Vector3, maxs: Vector3, pass_entity: int, content_mask: int) -> Dictionary:
	return replay.trace(start, end, mins, maxs, pass_entity, content_mask) if replay else {}


func point_contents(point: Vector3, pass_entity: int) -> int:
	return replay.point_contents(point, pass_entity) if replay else 0
