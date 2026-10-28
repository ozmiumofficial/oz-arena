class_name Q3UpdateInstall
extends Node
## Downloads and installs the release Q3UpdateCheck found (UPDATES_PLAN.md U3, U4).
##
##   1. Fetch the release's manifest.json and manifest.sig and verify them against the key the game
##      ships (Q3UpdateManifest.open). Nothing unsigned is ever downloaded.
##   2. Compare its SHA-256s with the installed files (hashes cached by size and time, so the 1.1 GB
##      pack isn't read every launch) and fetch only what differs: usually patch.pck, a few MB.
##   3. Download each into <game>/.update/stage/ in 16 MB ranges (resumable: a .part file keeps what
##      came), check its hash, and list it in .update/apply.txt.
##   4. Ready: "Restart now" quits into Q3Updater.exe, which swaps the files and starts the new build
##      (rolling back if it never reaches the menu); otherwise the update installs when the game quits.
##
## Off in the editor and headless unless `force` (tests). Settings > Network > Update automatically
## (on) starts the download as soon as a release is found; off, the menu's Update button does.

signal status_changed(status: Dictionary)

const DOWNLOAD_BASE := "https://github.com/" + Q3UpdateCheck.REPO + "/releases/download/"
const UPDATER := "Q3Updater.exe"
const CHUNK := 16 << 20
const RETRIES := 4
const HASHES := "user://update/hashes.cfg"

static var _instance: Q3UpdateInstall

var game_dir := OS.get_executable_path().get_base_dir()   ## tests point it at a scratch install
var base_url := Q3UpdateCheck.server_override() + "download/" if Q3UpdateCheck.server_override() != "" else DOWNLOAD_BASE   ## tests: a local server
var pem := ""                                             ## tests sign with their own key
var force := false
var state := "idle"     ## idle, checking, downloading, ready, applying, failed, current
var tag := ""
var error := ""
var done := 0           ## bytes downloaded (or already staged) of `total`
var total := 0
var file := ""          ## the file being downloaded
var _files: Array = []  ## the manifest entries this update installs
var _http: HTTPRequest


static func instance() -> Q3UpdateInstall:
	if not is_instance_valid(_instance):
		_instance = Q3UpdateInstall.new()
		_instance.name = "Q3UpdateInstall"
		(Engine.get_main_loop() as SceneTree).root.add_child.call_deferred(_instance)
	return _instance


## An update is downloaded and waiting for the game to quit.
static func pending() -> bool:
	return is_instance_valid(_instance) and _instance.state == "ready"


func status() -> Dictionary:
	return {"state": state, "tag": tag, "version": tag.trim_prefix("v"), "done": done, "total": total, "file": file, "error": error}


func stage_dir() -> String:
	return game_dir.path_join(".update")


func wanted() -> bool:
	if force:
		return true
	if Q3UpdateCheck.server_override() != "":
		return FileAccess.file_exists(game_dir.path_join(UPDATER))
	return not OS.has_feature("editor") and DisplayServer.get_name() != "headless" \
		and FileAccess.file_exists(game_dir.path_join(UPDATER))


func _to(s: String, why := "") -> void:
	state = s
	error = why
	status_changed.emit(status())


## Downloads release `release_tag` (Q3UpdateCheck's info["tag"]). Safe to call again: it resumes.
func start(release_tag: String) -> void:
	if not wanted() or state in ["checking", "downloading", "ready", "applying"]:
		return
	if not is_inside_tree():
		await tree_entered
	tag = release_tag
	done = 0
	total = 0
	file = ""
	_to("checking")
	var manifest := await _manifest(tag)
	if manifest.is_empty():
		return
	_files = Q3UpdateManifest.needed(manifest, _local_hashes(manifest))
	if _files.is_empty():
		_to("current")
		return
	# a different update's leftovers go; the same one's files are kept and checked again
	var stage := stage_dir()
	if _read(stage.path_join("stage_tag")) != tag:
		_remove_tree(stage.path_join("stage"))
	DirAccess.make_dir_recursive_absolute(stage.path_join("stage"))
	_write(stage.path_join("stage_tag"), tag)
	for e in _files:
		total += int(e["size"])
	var free := DirAccess.open(game_dir).get_space_left() if DirAccess.open(game_dir) else 0
	if free > 0 and free < total + (64 << 20):
		_to("failed", "not enough disk space: the update needs %d MB" % (total >> 20))
		return
	_to("downloading")
	for e in _files:
		var why := await _download(manifest, e)
		if why != "":
			_to("failed", why)
			return
	var lines := PackedStringArray([tag])
	for e in _files:
		lines.append(String(e["path"]))
	_write(stage.path_join("apply.txt"), "\n".join(lines) + "\n")
	file = ""
	_to("ready")


## A first install without its texture packs (the zip alone): fetches `paths` ("textures-1.pck" ...)
## from GitHub's Textures release, checked against this version's signed manifest, straight into the
## game folder (nothing holds them open yet). The splash shows `status()` and mounts them after.
## Returns "" or why it couldn't.
func fetch_missing(paths: PackedStringArray) -> String:
	if not is_inside_tree():
		await tree_entered
	tag = "v" + Q3UpdateCheck.current_version()
	done = 0
	total = 0
	_to("checking")
	var manifest := await _manifest(tag)
	if manifest.is_empty():
		return error
	_files = []
	for e in manifest["files"]:
		if paths.has(String(e["path"])):
			_files.append(e)
			total += int(e["size"])
	if _files.size() != paths.size():
		_to("failed", "this version's manifest doesn't list " + ", ".join(paths))
		return error
	var free := DirAccess.open(game_dir).get_space_left() if DirAccess.open(game_dir) else 0
	if free > 0 and free < total + (64 << 20):
		_to("failed", "not enough disk space: the textures need %.1f GB" % (total / 1e9))
		return error
	_to("downloading")
	for e in _files:
		var why := await _download(manifest, e)
		if why == "" and DirAccess.rename_absolute(stage_dir().path_join("stage").path_join(e["path"]), game_dir.path_join(e["path"])) != OK:
			why = "couldn't move %s into the game folder" % e["path"]
		if why != "":
			_to("failed", why)
			return error
	file = ""
	_to("idle")
	return ""


## The verified manifest of release `release_tag`, or {} (state "failed", with why).
func _manifest(release_tag: String) -> Dictionary:
	var data := await _fetch(base_url + release_tag.uri_encode() + "/manifest.json")
	var sig := await _fetch(base_url + release_tag.uri_encode() + "/manifest.sig")
	if data.is_empty() or sig.is_empty():
		_to("failed", "couldn't download %s's manifest from GitHub" % release_tag)
		return {}
	var opened := Q3UpdateManifest.open(data, sig, pem)
	if not opened["ok"]:
		_to("failed", "the download didn't verify: " + String(opened["error"]))
		return {}
	var manifest: Dictionary = opened["manifest"]
	if String(manifest["version"]) != release_tag:
		_to("failed", "the manifest is for %s, not %s" % [manifest["version"], release_tag])
		return {}
	return manifest


## Hands over to Q3Updater.exe. With `relaunch` the game quits and the new build starts; without it
## (Session.quit_game, already quitting) the files are swapped once the game has exited.
func apply(relaunch: bool) -> bool:
	if state != "ready":
		return false
	var stage := stage_dir()
	# a new updater in this update runs at once; the folder's copy can then be replaced too
	var src := stage.path_join("stage").path_join(UPDATER)
	if not FileAccess.file_exists(src):
		src = game_dir.path_join(UPDATER)
	var run := stage.path_join("Q3Updater.run.exe")
	if DirAccess.copy_absolute(src, run) != OK:
		_to("failed", "couldn't start the updater")
		return false
	var pid := OS.create_process(run, [game_dir, str(OS.get_process_id()), "1" if relaunch else "0",
		OS.get_executable_path().get_file()])
	if pid <= 0:
		_to("failed", "couldn't start the updater")
		return false
	_to("applying")
	if relaunch:
		Session.quit_game()
	return true


## Called when the main menu opens. A build Q3Updater.exe just installed tells it it started (so it
## keeps the update); an update installed at quit clears its backup here, on its first good start.
## Returns a message for the player when an update had to be rolled back, else "".
static func confirm_started(dir := "") -> String:
	if dir == "":
		dir = OS.get_executable_path().get_base_dir()
	var stage := dir.path_join(".update")
	var msg := ""
	if FileAccess.file_exists(stage.path_join("failed")):
		msg = _read(stage.path_join("failed"))
		DirAccess.remove_absolute(stage.path_join("failed"))
	var how := _read(stage.path_join("applied"))
	if how == "":
		return msg
	_write(stage.path_join("started"), "ok")
	if how == "quit":
		_remove_tree(stage.path_join("backup"))
		_remove_tree(stage.path_join("stage"))
		for f in ["apply.txt", "applied", "started", "stage_tag"]:
			DirAccess.remove_absolute(stage.path_join(f))
	return msg


func _local_hashes(manifest: Dictionary) -> Dictionary:
	var cache := ConfigFile.new()
	cache.load(HASHES)
	var out := {}
	for e in manifest["files"]:
		var p: String = e["path"]
		var full := game_dir.path_join(p)
		if not FileAccess.file_exists(full):
			out[p] = ""
			continue
		var size := _size(full)
		if size != int(e["size"]):
			out[p] = "size"   # can't match; no need to read it
			continue
		var stamp := "%d:%d" % [size, FileAccess.get_modified_time(full)]
		var key := full.sha256_text()
		if cache.get_value(key, "stamp", "") == stamp:
			out[p] = cache.get_value(key, "sha256", "")
			continue
		out[p] = FileAccess.get_sha256(full)
		cache.set_value(key, "stamp", stamp)
		cache.set_value(key, "sha256", out[p])
	DirAccess.make_dir_recursive_absolute(HASHES.get_base_dir())
	cache.save(HASHES)
	return out


## One file into stage/<path>, resumed from its .part; "" when it's there with the right hash.
func _download(manifest: Dictionary, e: Dictionary) -> String:
	var path: String = e["path"]
	var size := int(e["size"])
	var dest := stage_dir().path_join("stage").path_join(path)
	var part := dest + ".part"
	file = path
	DirAccess.make_dir_recursive_absolute(dest.get_base_dir())
	if FileAccess.file_exists(dest) and _size(dest) == size and FileAccess.get_sha256(dest) == e["sha256"]:
		done += size
		status_changed.emit(status())
		return ""
	DirAccess.remove_absolute(dest)
	var have := _size(part) if FileAccess.file_exists(part) else 0
	if have > size:
		DirAccess.remove_absolute(part)
		have = 0
	var f := FileAccess.open(part, FileAccess.READ_WRITE if have > 0 else FileAccess.WRITE)
	if f == null:
		return "couldn't write " + path
	f.seek(have)
	done += have
	var url := Q3UpdateManifest.download_url(base_url, manifest, e)
	while have < size:
		var last := mini(have + CHUNK, size) - 1
		var body := PackedByteArray()
		for attempt in RETRIES:
			body = await _fetch(url, ["Range: bytes=%d-%d" % [have, last]], have == 0 and last == size - 1)
			if body.size() == last - have + 1:
				break
			body = PackedByteArray()
			await get_tree().create_timer(2.0 * (attempt + 1)).timeout
		if body.is_empty():
			f.close()
			return "the download of %s stopped; it will resume" % path
		f.store_buffer(body)
		have += body.size()
		done += body.size()
		status_changed.emit(status())
	f.close()
	if FileAccess.get_sha256(part) != e["sha256"]:
		DirAccess.remove_absolute(part)
		return "%s didn't match the signed manifest" % path
	if DirAccess.rename_absolute(part, dest) != OK:
		return "couldn't stage " + path
	return ""


## GET `url` into memory: 200 (or 206 for a range) is the body, anything else empty. `whole_ok` takes a
## 200 for a range request too (a server that ignores Range, when the range is the whole file).
func _fetch(url: String, headers: Array = [], whole_ok := true) -> PackedByteArray:
	if _http == null:
		_http = HTTPRequest.new()
		_http.timeout = 60.0
		_http.max_redirects = 8
		add_child(_http)
	_http.body_size_limit = CHUNK + 4096
	var all := PackedStringArray(["User-Agent: Q3Remaster/" + Q3UpdateCheck.current_version()])
	all.append_array(headers)
	if _http.request(url, all) != OK:
		return PackedByteArray()
	var res: Array = await _http.request_completed
	var code: int = res[1]
	if res[0] != HTTPRequest.RESULT_SUCCESS or not (code == 206 or (code == 200 and (headers.is_empty() or whole_ok))):
		return PackedByteArray()
	return res[3]


static func _size(path: String) -> int:
	var f := FileAccess.open(path, FileAccess.READ)
	return f.get_length() if f else -1


static func _read(path: String) -> String:
	return FileAccess.get_file_as_string(path).strip_edges() if FileAccess.file_exists(path) else ""


static func _write(path: String, text: String) -> void:
	DirAccess.make_dir_recursive_absolute(path.get_base_dir())
	var f := FileAccess.open(path, FileAccess.WRITE)
	if f:
		f.store_string(text)


static func _remove_tree(dir: String) -> void:
	var d := DirAccess.open(dir)
	if d == null:
		return
	d.include_hidden = true
	for sub in d.get_directories():
		_remove_tree(dir.path_join(sub))
	for f in d.get_files():
		DirAccess.remove_absolute(dir.path_join(f))
	DirAccess.remove_absolute(dir)
