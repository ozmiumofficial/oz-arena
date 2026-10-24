extends Node
## Every sound in the library decoded start to end through its playback, as the game's players and
## the editor's waveform previews do. A WAV loop past the last sample made Godot read past a QOA
## buffer and crash (CLAUDE.md "WAV loop points"), so it also fails any detected loop.
##   godot --headless --path . res://tests/audio_decode_test.tscn [-- only=res://library/sounds/x.wav]
## Each path goes to user://audio_decode.progress before it's decoded, so a crash names its file.
## Prints "AUDIO DECODE ok|FAILED".


func _ready() -> void:
	var only := ""
	for a in OS.get_cmdline_user_args():
		if a.begins_with("only="):
			only = a.substr(5)
	var files: PackedStringArray = [only] if only != "" else _find("res://library", ["wav", "ogg"])
	files.sort()
	var failed: PackedStringArray = []
	for p in files:
		var f := FileAccess.open("user://audio_decode.progress", FileAccess.WRITE)
		f.store_line(p)
		f.close()
		var s := load(p) as AudioStream
		if s == null:
			failed.append(p + ": not loaded")
			continue
		if s is AudioStreamWAV and (s as AudioStreamWAV).loop_mode != AudioStreamWAV.LOOP_DISABLED:
			failed.append("%s: loop %d..%d from the file" % [p, s.loop_begin, s.loop_end])
			continue
		var pb := s.instantiate_playback()
		pb.start(0.0)
		var frames := 0
		var limit := int(s.get_length() * 44100.0) + 44100
		while frames < limit:
			var got := pb.mix_audio(1.0, 4096)
			if got.is_empty():
				break
			frames += got.size()
	for line in failed:
		print("  FAIL ", line)
	print("AUDIO DECODE %s: %d sounds, %d failed" % ["ok" if failed.is_empty() else "FAILED", files.size(), failed.size()])
	get_tree().quit(0 if failed.is_empty() else 1)


func _find(dir: String, exts: Array) -> PackedStringArray:
	var out: PackedStringArray = []
	for d in DirAccess.get_directories_at(dir):
		out.append_array(_find(dir.path_join(d), exts))
	for f in DirAccess.get_files_at(dir):
		if f.get_extension().to_lower() in exts:
			out.append(dir.path_join(f))
	return out
