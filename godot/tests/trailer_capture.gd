extends Node
## The trailer captures' recorder (TRAILER_PLAN.md), shared by tests/trailer_shot.tscn (camera paths
## through maps) and tests/trailer_showcase.tscn (fighters in the menu's mist). The scene renders
## into `vp`, an offscreen 1920x1080 SubViewport (the window just shows it, so its size doesn't
## matter), and each frame past the preroll goes straight into ffmpeg as raw pixels. Run with
## --fixed-fps 60: the game steps 1/60 s a frame however long a frame takes, as Quake 3's
## cl_avidemo does. The owner calls tick() once per _process and reads the frame index it returns;
## after `frames` recorded frames the recorder prints "TRAILER SHOT done <frames>" and quits.
## User args: out=<mp4> ffmpeg=<exe> [stills=<frame,frame,...> stills_dir=<folder>] [preroll=<n>]

const FPS := 60.0
const SIZE := Vector2i(1920, 1080)
## the root viewport's quality settings (Settings applies them there), copied to ours
const VIEWPORT_PROPS := ["msaa_3d", "screen_space_aa", "use_taa", "use_debanding", "mesh_lod_threshold",
	"anisotropic_filtering_level", "texture_mipmap_bias", "positional_shadow_atlas_size",
	"positional_shadow_atlas_16_bits"]

var vp: Viewport   ## what's recorded: our offscreen view, or the window's own (use_window)
var tag := "shot"            ## names the still images
var preroll := 120           ## frames rendered before recording (shaders compile, things settle)
var frames := 0              ## frames to record
var out_mp4 := ""
var ffmpeg := ""
var stills: PackedInt32Array = []
var stills_dir := ""
var encoder := "nvenc"       ## "nvenc" (the GPU's), or "x264" where there's no NVIDIA card
var _frame := -1
var _written := 0    ## frames sent to ffmpeg
var _src := SIZE     ## the view's real size (ffmpeg scales it to SIZE)
var _last := PackedByteArray()   ## the last frame sent, repeated for a frame that wasn't drawn
var _pipe: FileAccess
var _pid := -1
var _done := false


## The Ultra preset plus the settings outside it, for this run only: Settings saves to a scratch
## file, never the player's settings.
static func quality() -> void:
	Settings.path = "user://trailer_settings.cfg"
	Settings.set_value("graphics/preset", "ultra", false)
	Settings.set_value("video/render_scale", 1.0, false)
	Settings.set_value("video/upscaler", "bilinear", false)
	for k in ["video/brightness", "video/contrast", "accessibility/colorblind", "accessibility/reduced_flashing",
			"profile/team_color"]:
		Settings.set_value(k, Settings.default_of(k), false)
	AudioServer.set_bus_mute(0, true)


func read_args() -> void:
	for a in OS.get_cmdline_user_args():
		var kv := a.split("=", true, 1)
		if kv.size() == 2:
			match kv[0]:
				"preroll": preroll = int(kv[1])
				"out": out_mp4 = kv[1]
				"ffmpeg": ffmpeg = kv[1]
				"stills": stills = PackedInt32Array(Array(kv[1].split(",", false)).map(func(s: String) -> int: return int(s)))
				"stills_dir": stills_dir = kv[1]
				"encoder": encoder = kv[1]


## The offscreen view to put the scene in, shown in the window as it renders.
func make_viewport(own_world := true) -> SubViewport:
	vp = SubViewport.new()
	vp.size = SIZE
	vp.own_world_3d = own_world
	vp.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	for p in VIEWPORT_PROPS:
		vp.set(p, get_tree().root.get(p))
	add_child(vp)
	var show := TextureRect.new()
	show.texture = vp.get_texture()
	show.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	show.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	show.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(show)
	return vp


## Records the window's own view instead (scenes that draw to it, like Gameplay, HUD included). Run
## with window=1920x1080; begin() refuses a view that isn't 16:9 at 1920 or wider.
func use_window() -> Viewport:
	vp = get_tree().root
	return vp


## Whether the view renders 16:9 at 1920 or wider (the window's real pixels, not its stretch base
## size: with Windows display scaling a 1920x1080 window renders larger). ffmpeg scales it to SIZE.
func size_ok() -> bool:
	var s := Vector2i(vp.get_texture().get_size())
	return s.x * 9 == s.y * 16 and s.x >= SIZE.x


## Starts ffmpeg. False (with an error printed) when it can't.
func begin() -> bool:
	if out_mp4 == "" or ffmpeg == "" or frames <= 0:
		push_error("trailer_capture: out=, ffmpeg= and a frame count are needed")
		return false
	if not size_ok():
		push_error("trailer_capture: the view renders at %s, not 16:9 at %d or wider (window=1920x1080?)" % [vp.get_texture().get_size(), SIZE.x])
		return false
	# blocking: a full pipe waits for ffmpeg
	_src = Vector2i(vp.get_texture().get_size())
	var args := ["-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgba",
		"-s", "%dx%d" % [_src.x, _src.y], "-r", str(int(FPS)), "-i", "-"]
	if _src != SIZE:
		args += ["-vf", "scale=%d:%d:flags=lanczos" % [SIZE.x, SIZE.y]]
	# the graphics card's encoder: about 4x x264 medium's speed at the same look (SSIM 0.991 vs 0.994,
	# 2026-10-23); x264 on the CPU had the game waiting on every frame
	if encoder == "x264":
		args += ["-c:v", "libx264", "-preset", "medium", "-crf", "12"]
	else:
		args += ["-c:v", "h264_nvenc", "-preset", "p5", "-rc", "vbr", "-cq", "14", "-b:v", "0"]
	args += ["-pix_fmt", "yuv420p", out_mp4]
	var pipe := OS.execute_with_pipe(ffmpeg, args)
	if pipe.is_empty():
		push_error("trailer_capture: could not start %s" % ffmpeg)
		return false
	_pipe = pipe["stdio"]
	_pid = int(pipe["pid"])
	RenderingServer.frame_post_draw.connect(_on_drawn)
	return true


## Once per frame, from the owner's _process: the recorded frame this one will be (negative during
## the preroll).
func tick() -> int:
	_frame += 1
	return _frame - preroll


## After each frame is drawn: past the preroll, its pixels go to ffmpeg.
func _on_drawn() -> void:
	var f := _frame - preroll
	if _done or _frame < 0 or f < 0:
		return
	var img := vp.get_texture().get_image()
	if img.get_format() != Image.FORMAT_RGBA8:
		img.convert(Image.FORMAT_RGBA8)
	if Vector2i(img.get_size()) != _src:   # the window resized mid-take: keep ffmpeg's frame size
		img.resize(_src.x, _src.y, Image.INTERPOLATE_BILINEAR)
	while _written < f and _last.size() > 0:
		# a frame that wasn't drawn (a minimized or hidden window skips drawing): repeat the last
		# one, so the video keeps its timing
		push_warning("trailer_capture: frame %d wasn't drawn; the frame before stands in" % _written)
		_pipe.store_buffer(_last)
		_written += 1
	_last = img.get_data()
	_pipe.store_buffer(_last)
	_written += 1
	if f in stills and stills_dir != "":
		if Vector2i(img.get_size()) != SIZE:
			img.resize(SIZE.x, SIZE.y, Image.INTERPOLATE_LANCZOS)
		img.save_png(stills_dir.path_join("%s_f%05d.png" % [tag, f]))
	if f >= frames - 1:
		_finish()


## Ends the take now, for scenes whose length isn't known up front (set `frames` high to begin).
func finish_now() -> void:
	if not _done and _pipe:
		_finish()


func _finish() -> void:
	_done = true
	RenderingServer.frame_post_draw.disconnect(_on_drawn)
	_pipe.close()   # end of input: ffmpeg finishes the file
	Diagnostics.expect_stall(true)
	while OS.is_process_running(_pid):
		OS.delay_msec(50)
	Diagnostics.expect_stall(false)
	print("TRAILER SHOT done %d" % _written)   # fewer than `frames` fails the capture script's check
	get_tree().quit()
