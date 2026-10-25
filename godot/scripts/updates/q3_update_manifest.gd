class_name Q3UpdateManifest
extends RefCounted
## A release's signed manifest (UPDATES_PLAN.md, U2): what files the release holds and their SHA-256s.
## tools/release_manifest.py writes manifest.json and signs its exact bytes (RSA-3072, PKCS#1 v1.5,
## SHA-256) into manifest.sig; the game trusts only the public key shipped as game/update_key.json.
## Nothing here downloads; U3 fetches what `needed()` lists and checks each file against its hash.
##
## The manifest is untrusted until `open()` has verified it, and its paths and names are checked
## even then: a signed manifest must still never be able to write outside the game folder.

const KEY_FILE := "res://game/update_key.json"
const FORMAT := 1
const SOURCES := ["release", "textures", "retail"]
const MAX_FILES := 2000
const MAX_PATH := 200


## The PEM of the key the game trusts, or "" when the build ships none (then no update verifies).
static func public_key() -> String:
	var f := FileAccess.open(KEY_FILE, FileAccess.READ)
	if f == null:
		return ""
	var d: Variant = JSON.parse_string(f.get_as_text())
	return String(d.get("public_key", "")) if d is Dictionary else ""


static func verify(data: PackedByteArray, sig: PackedByteArray, pem: String) -> bool:
	if pem == "" or sig.is_empty() or data.is_empty():
		return false
	var key := CryptoKey.new()
	if key.load_from_string(pem, true) != OK:
		return false
	var h := HashingContext.new()
	h.start(HashingContext.HASH_SHA256)
	h.update(data)
	return Crypto.new().verify(HashingContext.HASH_SHA256, h.finish(), sig, key)


## "a/b.dll" is fine; "", "/x", "../x", "a//b", "a\\b", "C:/x" and control characters are not.
static func safe_path(p: String) -> bool:
	if p == "" or p.length() > MAX_PATH or p.begins_with("/") or p.ends_with("/") or p.contains("\\") or p.contains(":"):
		return false
	for part in p.split("/"):
		if part == "" or part == "." or part == ".." or part.ends_with(".") or part.ends_with(" "):
			return false
	for i in p.length():
		if p.unicode_at(i) < 32:
			return false
	return true


## Checks the shape of a manifest's JSON: {ok, error, manifest}.
static func parse(data: PackedByteArray) -> Dictionary:
	var d: Variant = JSON.parse_string(data.get_string_from_utf8())
	if not d is Dictionary:
		return _bad("not a JSON object")
	if int(d.get("format", 0)) != FORMAT or String(d.get("game", "")) != "Q3Remaster":
		return _bad("unknown format")
	if Q3UpdateCheck.parse_version(String(d.get("version", ""))).is_empty():
		return _bad("bad version")
	var files: Variant = d.get("files")
	if not files is Array or files.is_empty() or files.size() > MAX_FILES:
		return _bad("bad file list")
	var seen := {}
	var assets := {}
	for e in files:
		if not e is Dictionary:
			return _bad("bad file entry")
		var p := String(e.get("path", ""))
		if not safe_path(p):
			return _bad("unsafe path " + p.left(40))
		if seen.has(p.to_lower()):
			return _bad("duplicate path " + p)
		seen[p.to_lower()] = true
		if not (e.get("size") is float or e.get("size") is int) or float(e["size"]) < 0 or float(e["size"]) != floorf(float(e["size"])):
			return _bad("bad size for " + p)
		var h := String(e.get("sha256", ""))
		if h.length() != 64 or not h.is_valid_hex_number(false) or h != h.to_lower():
			return _bad("bad sha256 for " + p)
		var src := String(e.get("source", ""))
		if not SOURCES.has(src):
			return _bad("bad source for " + p)
		if src == "release":
			var a := String(e.get("asset", ""))
			if a == "" or a.length() > MAX_PATH or a.contains("/") or a.contains("\\") or a.contains(":") or a.begins_with(".") or assets.has(a):
				return _bad("bad asset name for " + p)
			assets[a] = true
	return {"ok": true, "error": "", "manifest": d}


## Verifies, then parses. `pem` is for tests; the game uses the shipped key.
static func open(data: PackedByteArray, sig: PackedByteArray, pem := "") -> Dictionary:
	if pem == "":
		pem = public_key()
	if pem == "":
		return _bad("this build has no update key")
	if not verify(data, sig, pem):
		return _bad("the signature does not match")
	return parse(data)


## The files an update must fetch: entries whose installed hash differs or that are missing.
## `local` maps path -> SHA-256 of the installed file ("" or absent when it isn't there). Retail
## paks are never updated (the first install brings them), and the texture packs come from the
## Textures release (`source` "textures"), listed so the caller can tell them apart.
static func needed(manifest: Dictionary, local: Dictionary) -> Array:
	var out: Array = []
	for e in manifest["files"]:
		if e["source"] == "retail":
			continue
		if String(local.get(e["path"], "")) != e["sha256"]:
			out.append(e)
	return out


## SHA-256 of each manifest file under `dir` ("" for a missing file), for `needed()`.
static func local_hashes(manifest: Dictionary, dir: String) -> Dictionary:
	var out := {}
	for e in manifest["files"]:
		out[e["path"]] = FileAccess.get_sha256(dir.path_join(e["path"])) if FileAccess.file_exists(dir.path_join(e["path"])) else ""
	return out


static func _bad(why: String) -> Dictionary:
	return {"ok": false, "error": why, "manifest": {}}
