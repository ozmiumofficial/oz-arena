class_name Q3ClipExport
extends RefCounted
## Clip export (REPLAY_PLAN.md R8): a marked range of a replay rendered at a fixed frame rate by
## Godot's Movie Maker. The game starts a second copy of itself with --write-movie and
## --fixed-fps; that copy plays the range (Session's clip= arguments: the camera, whose view, the
## HUD on or off) with no controls and quits at its end, so every frame is rendered, however long
## each one takes. The file is an AVI (Motion JPEG with the game's sound), or numbered PNGs for a
## .png name. Its frames are the project's base size (display/window/size/viewport_*, 1600x900):
## Movie Maker takes that, whatever the window or --resolution says.
## From the editor the copy opens the Gameplay scene directly; an exported game can't take a
## scene on its command line, so Session sends it straight to Gameplay when it sees clip=.
## The copy quits at the range's end; --quit-after (the range's frames plus LEAD_FRAMES) and a
## wall-clock limit here stop it whatever else happens.

const DIR := "user://clips"
const FPS := 60
const LEAD_FRAMES := 600         ## frames allowed for the copy to load before the range starts

var path := ""        ## the file being written
var pid := -1
var expected := 0     ## the range's frames
var _deadline := 0    ## Time.get_ticks_msec() past which the render is stopped
var _cancelled := false


## Starts the render of `replay` from `from_ms` to `to_ms` (server time). `view` carries the
## viewer's camera as it is: "pos" and "angles" (free camera, id units and degrees), "orbit"
## (yaw, pitch), "names" and "outlines". "" on success, else why not.
func start(replay: String, from_ms: int, to_ms: int, cam: String, watch: int, hud := true, view := {}) -> String:
	var size := movie_size()
	if to_ms - from_ms < 250:
		return "the clip is too short"
	expected = ceili((to_ms - from_ms) / 1000.0 * FPS)
	DirAccess.make_dir_recursive_absolute(DIR)
	var t := Time.get_datetime_dict_from_system()
	path = ProjectSettings.globalize_path(DIR.path_join("%s_%04d%02d%02d_%02d%02d%02d.avi" % [replay.get_file().get_basename().left(48),
		t.year, t.month, t.day, t.hour, t.minute, t.second]))
	var args := PackedStringArray()
	if OS.has_feature("editor"):
		args.append_array(["--path", ProjectSettings.globalize_path("res://")])
	args.append_array(["--write-movie", path, "--fixed-fps", str(FPS), "--quit-after", str(expected + LEAD_FRAMES)])
	if OS.has_feature("editor"):
		args.append(Session.GAMEPLAY)
	args.append_array(["--", "clip=" + ProjectSettings.globalize_path(replay), "clip_from=%d" % from_ms, "clip_to=%d" % to_ms,
		"clip_cam=" + cam, "clip_watch=%d" % watch, "clip_hud=" + ("1" if hud else "0"), "window=%dx%d" % [size.x, size.y]])
	if view.has("pos"):
		var p: Vector3 = view["pos"]
		var a: Vector3 = view["angles"]
		args.append("clip_free=%f,%f,%f,%f,%f" % [p.x, p.y, p.z, a.x, a.y])
	if view.has("orbit"):
		var o: Vector2 = view["orbit"]
		args.append("clip_orbit=%f,%f" % [o.x, o.y])
	args.append("clip_tags=%d%d" % [int(view.get("names", false)), int(view.get("outlines", false))])
	pid = OS.create_process(OS.get_executable_path(), args)
	if pid <= 0:
		pid = -1
		return "couldn't start the render"
	# rendering runs well under real time on a slow machine; a minute to load, then 20x the clip
	_deadline = Time.get_ticks_msec() + 60000 + (to_ms - from_ms) * 20
	_cancelled = false
	return ""


## Stops the render (the file keeps the frames written so far). The copy's diagnostics session
## goes too: a process that vanished would be reported as a crash at the next launch.
func cancel() -> void:
	if pid > 0 and OS.is_process_running(pid):
		OS.kill(pid)
		for f in ["%d.json", "%d.errors.log"]:
			DirAccess.remove_absolute(Diagnostics.SESSIONS.path_join(f % pid))
	_cancelled = true
	pid = -1


## The frame size Movie Maker writes: the project's base size.
static func movie_size() -> Vector2i:
	return Vector2i(int(ProjectSettings.get_setting("display/window/size/viewport_width", 1600)),
		int(ProjectSettings.get_setting("display/window/size/viewport_height", 900)))


## Whether the render is still running (one past its time limit is stopped here).
func running() -> bool:
	if pid > 0 and OS.is_process_running(pid) and Time.get_ticks_msec() > _deadline:
		cancel()
	return pid > 0 and OS.is_process_running(pid)


## The finished file's size in bytes (0 if it isn't there).
func size() -> int:
	var f := FileAccess.open(path, FileAccess.READ)
	return f.get_length() if f else 0


## After the render: "" when the file holds the whole range, else what went wrong. Movie Maker
## writes the frame count into the header as it closes the file, so a copy that crashed or was
## stopped leaves fewer (or none).
func result() -> String:
	if _cancelled:
		return "stopped"
	var frames := int(avi_header(path).get("frames", 0))
	if frames == 0:
		return "it didn't render"
	if frames < expected:
		return "it stopped early (%d of %d frames)" % [frames, expected]
	return ""


## An AVI's main header (avih): microseconds per frame, total frames, width, height.
static func avi_header(file: String) -> Dictionary:
	var f := FileAccess.open(file, FileAccess.READ)
	if f == null or f.get_length() < 64:
		return {}
	var head := f.get_buffer(mini(4096, f.get_length()))
	for i in head.size() - 64:
		if head[i] == 0x61 and head[i + 1] == 0x76 and head[i + 2] == 0x69 and head[i + 3] == 0x68:   # "avih"
			return {"us_per_frame": head.decode_u32(i + 8), "frames": head.decode_u32(i + 24), "width": head.decode_u32(i + 40),
				"height": head.decode_u32(i + 44)}
	return {}
