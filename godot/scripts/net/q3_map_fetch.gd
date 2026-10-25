class_name Q3MapFetch
extends RefCounted
## Downloading a map this player doesn't have from the player or server hosting the match. Quake 3
## had autodownload; this is that for the Maps folder: the host sends the pk3 its map came from
## (Q3MapPacks), the joiner saves it to its own Maps folder, and q3core reads the paks again
## (Q3Net.reload_map). Only Maps folder pk3s are ever sent, never id's paks.
##
## The file goes over a direct connection between the two PCs (Q3DirectLink), never the relay
## (Ozmium, 2026-10-25). That shows each side the other's IP address, so both must allow it
## (Settings > Network > Allow direct map downloads; a dedicated server always does). The relayed
## connection the match already has (Q3RtcPeer.files) carries only the request and the direct
## link's setup, a few KB.
##
## Messages start with a type byte: MSG_JSON then UTF-8 JSON, or MSG_DATA then file bytes in order.
##   relayed, joiner -> host   {"want": map, "offset": bytes already here}
##   relayed, host -> joiner   {"map", "file" (pk3 name), "size", "md5"} or {"error": why}
##   relayed, both ways        {"rtc": the direct link's offer / answer / candidate}
##   direct, host -> joiner    MSG_DATA from the offset to the end
## The match's connection can move to new relay credentials (Q3RtcTransport's handover): the request
## goes out again on the new one from the bytes already here, over a new direct link.

const MSG_JSON := 0
const MSG_DATA := 1
const CHUNK := 16384
const MAX_SIZE := 512 * 1024 * 1024     ## a pk3 bigger than this isn't fetched (or served)
const ANSWER_TIMEOUT := 30.0            ## s without a word from the host

var map := ""
var state := "asking"    ## asking, connecting, downloading, done or failed
var error := ""
var size := 0
var got := 0
var file_name := ""      ## where it was saved (in the Maps folder), once done

var _transport: Q3RtcTransport
var _peer: Q3RtcPeer     ## the relayed connection the request went out on
var _link: Q3DirectLink
var _md5 := ""
var _part := ""
var _out: FileAccess
var _quiet := 0.0


func _init(transport: Q3RtcTransport, map_name: String) -> void:
	_transport = transport
	map = map_name.to_lower()


## Whether this player lets a download show their IP address to the host.
static func allowed() -> bool:
	return bool(Settings.get_value("network/direct_downloads"))


## 0..1 once the size is known.
func progress() -> float:
	return float(got) / size if size > 0 else 0.0


## Call every frame until state is done or failed.
func poll(delta: float) -> void:
	if state == "done" or state == "failed":
		return
	if not allowed():
		_fail("This match is on %s, which you don't have. Turn on Settings > Network > Allow direct map downloads to get it from the host (the two PCs see each other's IP address while it downloads), or put its pk3 in your Maps folder." % map)
		return
	if _transport == null or not _transport.is_open():
		_fail("The connection to the host was lost.")
		return
	if _transport.peer != _peer:
		# first call, or the handover moved the match to a new connection: ask (again) from what's here
		if _transport.peer.state != "open":
			return
		_peer = _transport.peer
		_quiet = 0.0
		_close_link()
		_peer.files_send(json({"want": map, "offset": got}))
	_quiet += delta
	for m in _peer.files_receive():
		_quiet = 0.0
		var b: PackedByteArray = m
		if not b.is_empty() and b[0] == MSG_JSON:
			_on_json(JSON.parse_string(b.slice(1).get_string_from_utf8()))
		if state == "failed":
			return
	if _link != null:
		_link.poll(delta)
		for o in _link.outgoing():
			_peer.files_send(json({"rtc": o}))
		if _link.state == "failed":
			_fail(_link.error)
			return
		if _link.state == "open":
			state = "downloading"
		var link := _link
		for m in link.receive():
			_quiet = 0.0
			var b: PackedByteArray = m
			if not b.is_empty() and b[0] == MSG_DATA and _out != null:
				_out.store_buffer(b.slice(1))
				got += b.size() - 1
				if got >= size:
					_finish()
					return
		if link.state == "closed":
			_fail(link.error)   # gone before the whole file came
			return
	if _quiet > ANSWER_TIMEOUT:
		_fail("The host stopped sending %s." % map)


func cancel() -> void:
	if state != "done":
		_fail("Cancelled.")


func _on_json(d: Variant) -> void:
	if not d is Dictionary:
		return
	if d.has("error"):
		_fail(String(d["error"]))
		return
	if d.has("rtc"):
		if _link != null and d["rtc"] is Dictionary:
			_link.apply(d["rtc"])
		return
	var file := String(d.get("file", "")).get_file()
	size = int(d.get("size", 0))
	_md5 = String(d.get("md5", ""))
	if not file.to_lower().ends_with(".pk3") or size <= 0 or size > MAX_SIZE:
		_fail("The host offered a file that isn't a map package.")
		return
	state = "connecting"
	_link = Q3DirectLink.new()
	if not _link.start(false):
		_fail(_link.error)
		return
	if _out != null:
		return   # asked again after the handover: carry on writing
	var dir := Q3MapPacks.dir()
	DirAccess.make_dir_recursive_absolute(dir)
	file_name = _free_name(dir, file)
	_part = dir.path_join(file_name + ".download")
	_out = FileAccess.open(_part, FileAccess.WRITE)
	if _out == null:
		_fail("Couldn't write to %s (%s)." % [dir, error_string(FileAccess.get_open_error())])


## The host's pk3 name, unless this player already has a different file of that name: then the
## name with the start of its md5, so their own copy stays (it loads after, so this match's wins).
func _free_name(dir: String, file: String) -> String:
	var path := dir.path_join(file)
	if not FileAccess.file_exists(path) or FileAccess.get_md5(path) == _md5:
		return file
	return "%s_%s.pk3" % [file.get_basename(), _md5.substr(0, 8)]


func _finish() -> void:
	_close_link()
	_out.close()
	_out = null
	if _md5 != "" and FileAccess.get_md5(_part) != _md5:
		DirAccess.remove_absolute(_part)
		_fail("%s arrived damaged." % map)
		return
	var dest := Q3MapPacks.dir().path_join(file_name)
	if FileAccess.file_exists(dest):
		DirAccess.remove_absolute(dest)
	if DirAccess.rename_absolute(_part, dest) != OK:
		_fail("Couldn't save %s." % file_name)
		return
	state = "done"


func _fail(why: String) -> void:
	_close_link()
	if _out != null:
		_out.close()
		_out = null
	if _part != "" and FileAccess.file_exists(_part):
		DirAccess.remove_absolute(_part)
	error = why
	state = "failed"


func _close_link() -> void:
	if _link != null:
		_link.close()
		_link = null


static func json(d: Dictionary) -> PackedByteArray:
	var b := PackedByteArray([MSG_JSON])
	b.append_array(JSON.stringify(d).to_utf8_buffer())
	return b
