extends Node
## Headless check of the signed manifest (Q3UpdateManifest, UPDATES_PLAN.md U2): a manifest signed by
## tools/release_manifest.py (Python, RSA-3072 PKCS#1 v1.5 SHA-256) verifies in Godot's Crypto; a changed
## byte, a wrong key or a missing signature does not; unsafe paths and bad fields are refused; `needed()`
## lists what differs. The fixture is made by `python tools/release_manifest.py make-fixture
## godot/tests/fixtures/update` (its private key is thrown away).
##   godot --headless --path . res://tests/update_manifest_test.tscn --quit-after 2000

const FIX := "res://tests/fixtures/update/"

var _ok := true


func _ready() -> void:
	var data := FileAccess.get_file_as_bytes(FIX + "manifest.json")
	var sig := FileAccess.get_file_as_bytes(FIX + "manifest.sig")
	var pem := String(JSON.parse_string(FileAccess.get_file_as_string(FIX + "update_key.json"))["public_key"])
	_check("the fixture is there", data.size() > 100 and sig.size() == 384 and pem.begins_with("-----BEGIN PUBLIC KEY"))
	_check("Python's signature verifies in Godot", Q3UpdateManifest.verify(data, sig, pem))
	var bent := data.duplicate()
	bent[100] ^= 1
	_check("one changed byte fails", not Q3UpdateManifest.verify(bent, sig, pem))
	var bad_sig := sig.duplicate()
	bad_sig[10] ^= 1
	_check("a changed signature fails", not Q3UpdateManifest.verify(data, bad_sig, pem))
	var other := Crypto.new().generate_rsa(2048).save_to_string(true)
	_check("another key fails", not Q3UpdateManifest.verify(data, sig, other))
	_check("no signature, no data or no key fails", not Q3UpdateManifest.verify(data, PackedByteArray(), pem)
		and not Q3UpdateManifest.verify(PackedByteArray(), sig, pem) and not Q3UpdateManifest.verify(data, sig, ""))
	_check("garbage for a key fails", not Q3UpdateManifest.verify(data, sig, "not a key"))

	var o := Q3UpdateManifest.open(data, sig, pem)
	_check("open verifies then parses (%s)" % o["error"], o["ok"] and o["manifest"]["version"] == "v9.9.9-beta" and o["manifest"]["files"].size() == 5)
	_check("a tampered manifest is not opened", not Q3UpdateManifest.open(bent, sig, pem)["ok"])
	_check("the build's own key is absent or valid PEM", Q3UpdateManifest.public_key() == "" or Q3UpdateManifest.public_key().begins_with("-----BEGIN PUBLIC KEY"))
	var m: Dictionary = o["manifest"]
	var by := {}
	for e in m["files"]:
		by[e["path"]] = e
	_check("sources are told apart", by["Q3Remaster.pck"]["source"] == "release" and by["textures-1.pck"]["source"] == "textures"
		and by["q3core/baseq3/pak1.pk3"]["source"] == "retail" and by["q3core/baseq3/qagamex86_64.dll"]["asset"] == "q3core__baseq3__qagamex86_64.dll")

	# needed(): what differs
	var local := {}
	for e in m["files"]:
		local[e["path"]] = e["sha256"]
	_check("nothing is needed when everything matches", Q3UpdateManifest.needed(m, local).is_empty())
	local["Q3Remaster.pck"] = "0".repeat(64)
	local["textures-1.pck"] = ""
	local["q3core/baseq3/pak1.pk3"] = ""
	var need := Q3UpdateManifest.needed(m, local).map(func(e): return e["path"])
	need.sort()
	_check("a different or missing file is needed, retail paks never %s" % [need], need == ["Q3Remaster.pck", "textures-1.pck"])

	# local_hashes() against real files
	var dir := OS.get_user_data_dir().path_join("update_manifest_test")
	DirAccess.make_dir_recursive_absolute(dir)
	var f := FileAccess.open(dir.path_join("Q3Remaster.exe"), FileAccess.WRITE)
	f.store_string("exe")
	f.close()
	var lh := Q3UpdateManifest.local_hashes(m, dir)
	_check("local hashes: the sample exe matches, a missing file is empty", lh["Q3Remaster.exe"] == by["Q3Remaster.exe"]["sha256"] and lh["Q3Remaster.pck"] == "")
	DirAccess.remove_absolute(dir.path_join("Q3Remaster.exe"))
	DirAccess.remove_absolute(dir)

	_shape(m)
	print("UPDATE_MANIFEST_TEST ", "PASS" if _ok else "FAIL")
	get_tree().quit(0 if _ok else 1)


func _entry(p: String, extra := {}) -> Dictionary:
	var e := {"path": p, "size": 3, "sha256": "a".repeat(64), "source": "release", "asset": p.replace("/", "__")}
	e.merge(extra, true)
	return e


func _manifest(files: Array, extra := {}) -> PackedByteArray:
	var m := {"format": 1, "game": "Q3Remaster", "version": "v1.2.3", "files": files}
	m.merge(extra, true)
	return JSON.stringify(m).to_utf8_buffer()


func _shape(good: Dictionary) -> void:
	_check("a plain manifest parses", Q3UpdateManifest.parse(_manifest([_entry("a/b.dll")]))["ok"])
	for p in ["../x", "a/../x", "/x", "C:/x", "a\\b", "a//b", "", "./x", "a/", "x.", "x/ ", "a\u0001b"]:
		_check("unsafe path refused: %s" % [p.c_escape()], not Q3UpdateManifest.parse(_manifest([_entry(p, {"asset": "ok"})]))["ok"])
	_check("a duplicate path is refused, case aside", not Q3UpdateManifest.parse(_manifest([_entry("a.dll"), _entry("A.DLL", {"asset": "b"})]))["ok"])
	_check("a duplicate asset name is refused", not Q3UpdateManifest.parse(_manifest([_entry("a.dll"), _entry("b.dll", {"asset": "a.dll"})]))["ok"])
	_check("an asset name with a slash is refused", not Q3UpdateManifest.parse(_manifest([_entry("a.dll", {"asset": "x/y"})]))["ok"])
	_check("a missing asset name is refused for release files", not Q3UpdateManifest.parse(_manifest([_entry("a.dll", {"asset": ""})]))["ok"])
	_check("a bad hash is refused", not Q3UpdateManifest.parse(_manifest([_entry("a.dll", {"sha256": "xyz"})]))["ok"])
	_check("an upper-case hash is refused", not Q3UpdateManifest.parse(_manifest([_entry("a.dll", {"sha256": "A".repeat(64)})]))["ok"])
	_check("a negative or fractional size is refused", not Q3UpdateManifest.parse(_manifest([_entry("a.dll", {"size": -1})]))["ok"]
		and not Q3UpdateManifest.parse(_manifest([_entry("a.dll", {"size": 1.5})]))["ok"])
	_check("an unknown source is refused", not Q3UpdateManifest.parse(_manifest([_entry("a.dll", {"source": "web"})]))["ok"])
	_check("a bad version is refused", not Q3UpdateManifest.parse(_manifest([_entry("a.dll")], {"version": "latest"}))["ok"])
	_check("another format or game is refused", not Q3UpdateManifest.parse(_manifest([_entry("a.dll")], {"format": 2}))["ok"]
		and not Q3UpdateManifest.parse(_manifest([_entry("a.dll")], {"game": "Other"}))["ok"])
	_check("an empty list is refused", not Q3UpdateManifest.parse(_manifest([]))["ok"])
	_check("not JSON is refused", not Q3UpdateManifest.parse("nope".to_utf8_buffer())["ok"])
	_check("a retail or textures entry needs no asset name", Q3UpdateManifest.parse(_manifest([_entry("textures-1.pck", {"source": "textures", "asset": ""})]))["ok"])


func _check(what: String, ok: bool) -> void:
	_ok = _ok and ok
	print("  [%s] %s" % ["ok" if ok else "FAIL", what])
