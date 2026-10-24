class_name Q3Ownership
extends RefCounted
## The ownership check: the game launches only with the player's own retail Quake III Arena pak0.pk3.
## The player drags it (or its baseq3 folder) onto the setup screen once; the game checks it the way
## ioquake3 does (files.c FS_CheckPak0: the pak checksum against id's table) and remembers the
## folder. Nothing is copied. Every start checks the file again, so a moved or replaced pak0 brings
## the setup screen back. Q3Host.start() hands the remembered folder to the engine (fs_steampath),
## which also picks up pak1-8 beside it.

const CONFIG := "user://q3/ownership.cfg"
## files.c pak_checksums: baseq3/pak0.pk3 .. pak8.pk3 of the retail game and its point releases.
const PAK_CHECKSUMS := [1566731103, 298122907, 412165236, 2991495316, 1197932710, 4087071573, 3709064859,
	908855077, 977125798]
const DEMO_PAK0 := 2985612116   ## files.c DEMO_PAK0_CHECKSUM: the demo's pak0, which doesn't count
## Where installs usually are, for the setup screen's "found it here" hint.
const LIKELY := ["C:/Program Files (x86)/Steam/steamapps/common/Quake 3 Arena",
	"C:/Program Files/Steam/steamapps/common/Quake 3 Arena", "C:/GOG Games/Quake III Arena",
	"C:/Program Files (x86)/GOG Galaxy/Games/Quake III Arena", "C:/Program Files (x86)/Quake III Arena"]
const STEAM_LIBRARIES := "C:/Program Files (x86)/Steam/steamapps/libraryfolders.vdf"


## The remembered baseq3 folder when its pak0.pk3 is still the retail one, else "".
static func verified() -> String:
	var dir := remembered()
	if dir == "" or pak_checksum(dir.path_join("pak0.pk3")) != PAK_CHECKSUMS[0]:
		return ""
	return dir


## The remembered baseq3 folder, unchecked.
static func remembered() -> String:
	# A server has no setup screen: its operator points Q3_BASEQ3 at the folder holding their pak0.pk3
	var env := OS.get_environment("Q3_BASEQ3").replace("\\", "/").trim_suffix("/")   # Q3Host looks for a name ending in baseq3
	if env != "" and FileAccess.file_exists(env.path_join("pak0.pk3")):
		return env
	var cfg := ConfigFile.new()
	if cfg.load(CONFIG) != OK:
		return ""
	return String(cfg.get_value("quake3", "baseq3", ""))


static func forget() -> void:
	DirAccess.remove_absolute(ProjectSettings.globalize_path(CONFIG))


## Checks what the player dropped: pak0.pk3, another pak from the same folder, the baseq3 folder or
## the Quake III Arena folder. Remembers the folder when its pak0 is retail. Returns {ok, baseq3,
## paks (how many of pak0-8 are retail), message}.
static func accept(path: String) -> Dictionary:
	path = path.replace("\\", "/").trim_suffix("/")
	var dir := ""
	if DirAccess.dir_exists_absolute(path):
		for d in [path, path.path_join("baseq3")]:
			if FileAccess.file_exists(d.path_join("pak0.pk3")):
				dir = d
				break
		if dir == "":
			return _fail("There's no pak0.pk3 in that folder. It's in Quake III Arena's baseq3 folder.")
	elif path.get_extension().to_lower() == "pk3":
		dir = path.get_base_dir()
		if not FileAccess.file_exists(dir.path_join("pak0.pk3")):
			return _fail("That's %s. Drag pak0.pk3 from the same folder." % path.get_file())
	else:
		return _fail("That's not a Quake III Arena file. Drag pak0.pk3 from the game's baseq3 folder.")
	var sum := pak_checksum(dir.path_join("pak0.pk3"))
	if sum == DEMO_PAK0:
		return _fail("That's the Quake III Arena demo. The full game is needed.")
	if sum != PAK_CHECKSUMS[0]:
		return _fail("That pak0.pk3 isn't the retail Quake III Arena file, so it can't be used.")
	var cfg := ConfigFile.new()
	cfg.set_value("quake3", "baseq3", dir)
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(CONFIG.get_base_dir()))
	cfg.save(CONFIG)
	var paks := retail_paks(dir)
	var msg := "Quake III Arena verified."
	if paks < PAK_CHECKSUMS.size():
		msg += " Only %d of its 9 paks are there, so the patch content (pro maps, fixes) may be missing." % paks
	return {"ok": true, "baseq3": dir, "paks": paks, "message": msg}


## How many of pak0-8 in `dir` (or bundled with an exported game) are id's retail files.
static func retail_paks(dir: String) -> int:
	var n := 0
	var bundled := Q3Host.bin_path().path_join("baseq3")   # an exported game ships pak1-8 there
	for i in PAK_CHECKSUMS.size():
		for d in [dir, bundled]:
			if pak_checksum(d.path_join("pak%d.pk3" % i)) == PAK_CHECKSUMS[i]:
				n += 1
				break
	return n


## A baseq3 folder that looks like an install (pak0.pk3 present, not yet checked), for the hint.
static func find_install() -> String:
	var roots: Array = LIKELY.duplicate()
	var vdf := FileAccess.get_file_as_string(STEAM_LIBRARIES)
	for m in RegEx.create_from_string("\"path\"\\s+\"([^\"]+)\"").search_all(vdf):
		roots.append(m.get_string(1).replace("\\\\", "/").path_join("steamapps/common/Quake 3 Arena"))
	for r in roots:
		if FileAccess.file_exists(String(r).path_join("baseq3/pak0.pk3")):
			return String(r).path_join("baseq3")
	return ""


## files.c FS_LoadZipFile's pack->checksum: Com_BlockChecksum (MD4, digest words xored) over the
## little-endian CRC-32 of every non-empty file, in central directory order. -1 if unreadable.
static func pak_checksum(path: String) -> int:
	var f := FileAccess.open(path, FileAccess.READ)
	if f == null:
		return -1
	var size := f.get_length()
	# the end-of-central-directory record: in the last 22 bytes plus up to 64 KB of comment
	var tail := mini(size, 65557)
	f.seek(size - tail)
	var end := f.get_buffer(tail)
	var at := -1
	for i in range(end.size() - 22, -1, -1):
		if end.decode_u32(i) == 0x06054b50:
			at = i
			break
	if at < 0:
		return -1
	var count := end.decode_u16(at + 10)
	var cd_size := end.decode_u32(at + 12)
	f.seek(end.decode_u32(at + 16))
	var cd := f.get_buffer(cd_size)
	var crcs := PackedByteArray()
	var p := 0
	for i in count:
		if p + 46 > cd.size() or cd.decode_u32(p) != 0x02014b50:
			return -1
		if cd.decode_u32(p + 24) > 0:   # uncompressed size
			crcs.append_array(cd.slice(p + 16, p + 20))
		p += 46 + cd.decode_u16(p + 28) + cd.decode_u16(p + 30) + cd.decode_u16(p + 32)
	var d := md4(crcs)
	return d.decode_u32(0) ^ d.decode_u32(4) ^ d.decode_u32(8) ^ d.decode_u32(12)


## RFC 1320 MD4 (Quake 3's mdfour), which Godot's HashingContext doesn't offer.
static func md4(msg: PackedByteArray) -> PackedByteArray:
	var bits := msg.size() * 8
	var m := msg.duplicate()
	m.append(0x80)
	while m.size() % 64 != 56:
		m.append(0)
	var len_bytes := PackedByteArray()
	len_bytes.resize(8)
	len_bytes.encode_u64(0, bits)
	m.append_array(len_bytes)
	var h := [0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476]
	var x: Array[int] = []
	x.resize(16)
	for off in range(0, m.size(), 64):
		for i in 16:
			x[i] = m.decode_u32(off + i * 4)
		var a: int = h[0]
		var b: int = h[1]
		var c: int = h[2]
		var dd: int = h[3]
		for i in 16:   # round 1: F, shifts 3 7 11 19
			var k := i
			var r: int = [3, 7, 11, 19][i % 4]
			var t := (a + ((b & c) | (~b & dd)) + x[k]) & 0xffffffff
			a = dd; dd = c; c = b; b = _rol(t, r)
		for i in 16:   # round 2: G, shifts 3 5 9 13
			var k: int = [0, 4, 8, 12, 1, 5, 9, 13, 2, 6, 10, 14, 3, 7, 11, 15][i]
			var r: int = [3, 5, 9, 13][i % 4]
			var t := (a + ((b & c) | (b & dd) | (c & dd)) + x[k] + 0x5a827999) & 0xffffffff
			a = dd; dd = c; c = b; b = _rol(t, r)
		for i in 16:   # round 3: H, shifts 3 9 11 15
			var k: int = [0, 8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7, 15][i]
			var r: int = [3, 9, 11, 15][i % 4]
			var t := (a + (b ^ c ^ dd) + x[k] + 0x6ed9eba1) & 0xffffffff
			a = dd; dd = c; c = b; b = _rol(t, r)
		h[0] = (h[0] + a) & 0xffffffff
		h[1] = (h[1] + b) & 0xffffffff
		h[2] = (h[2] + c) & 0xffffffff
		h[3] = (h[3] + dd) & 0xffffffff
	var out := PackedByteArray()
	out.resize(16)
	for i in 4:
		out.encode_u32(i * 4, h[i])
	return out


static func _rol(v: int, s: int) -> int:
	return ((v << s) | (v >> (32 - s))) & 0xffffffff


static func _fail(msg: String) -> Dictionary:
	return {"ok": false, "baseq3": "", "paks": 0, "message": msg}
