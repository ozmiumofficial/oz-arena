class_name Q3UpdateCheck
extends Node
## Asks GitHub once per run whether a newer release exists (UPDATES_PLAN.md, phase U1). It only
## tells the menu: nothing is downloaded or installed. The menu bridge posts `available` to the
## page, which asks the player ("Update available"); the player can skip a version for good.
##
## Pure logic is static (parse_version, compare, pick, parse_feed) so tests run without a network.
## Off with Settings > Network > Check for updates, in the editor, and headless.

signal available(info: Dictionary)

const REPO := "ozmiumofficial/quake-3-arena-remastered-fan-project"
const FEED := "https://api.github.com/repos/" + REPO + "/releases?per_page=15"
const RELEASES_URL := "https://github.com/" + REPO + "/releases"
const STORE := "user://updates.cfg"
const TAG_PATTERN := "^v?(\\d+)\\.(\\d+)\\.(\\d+)(?:-([0-9A-Za-z.]+))?$"
const MAX_NOTES := 3000

static var _instance: Q3UpdateCheck

var info: Dictionary = {}            ## the newest newer release, once found ({} until then)
var feed_url := FEED                 ## tests point this at a local server
var force := false                   ## check even in the editor or headless
var _started := false


## The one checker of this run, a child of the root so it outlives the menu scene.
static func instance() -> Q3UpdateCheck:
	if not is_instance_valid(_instance):
		_instance = Q3UpdateCheck.new()
		_instance.name = "Q3UpdateCheck"
		(Engine.get_main_loop() as SceneTree).root.add_child.call_deferred(_instance)
	return _instance


static func current_version() -> String:
	return String(ProjectSettings.get_setting("application/config/version", "0.0.0"))


## "v0.4.0-beta" -> {core: [0, 4, 0], pre: "beta"}; {} when the tag isn't a version (the permanent
## "textures" release, for one).
static func parse_version(tag: String) -> Dictionary:
	var m := RegEx.create_from_string(TAG_PATTERN).search(tag.strip_edges())
	if m == null:
		return {}
	return {"core": [int(m.get_string(1)), int(m.get_string(2)), int(m.get_string(3))], "pre": m.get_string(4)}


## -1, 0 or 1 as semver orders them: a release is newer than its own pre-releases, and
## pre-release names compare part by part (numbers as numbers, below words). A tag that isn't a
## version compares equal to everything.
static func compare(a: String, b: String) -> int:
	var va := parse_version(a)
	var vb := parse_version(b)
	if va.is_empty() or vb.is_empty():
		return 0
	for i in 3:
		if va["core"][i] != vb["core"][i]:
			return -1 if va["core"][i] < vb["core"][i] else 1
	var pa: String = va["pre"]
	var pb: String = vb["pre"]
	if pa == pb:
		return 0
	if pa == "":
		return 1
	if pb == "":
		return -1
	var xa := pa.split(".")
	var xb := pb.split(".")
	for i in mini(xa.size(), xb.size()):
		if xa[i] == xb[i]:
			continue
		var na: bool = xa[i].is_valid_int()
		var nb: bool = xb[i].is_valid_int()
		if na and nb:
			return -1 if int(xa[i]) < int(xb[i]) else 1
		if na != nb:
			return -1 if na else 1
		return -1 if xa[i] < xb[i] else 1
	return 0 if xa.size() == xb.size() else (-1 if xa.size() < xb.size() else 1)


## The newest release in a GitHub "list releases" answer that is newer than `current`, as the
## menu shows it, or {}. Drafts, tags that aren't versions and (unless `with_pre`) pre-releases are
## ignored.
static func pick(releases: Array, current: String, with_pre: bool) -> Dictionary:
	var best := {}
	for r in releases:
		if not r is Dictionary or bool(r.get("draft", false)):
			continue
		var tag := String(r.get("tag_name", ""))
		var v := parse_version(tag)
		if v.is_empty():
			continue
		if (bool(r.get("prerelease", false)) or v["pre"] != "") and not with_pre:
			continue
		if compare(tag, current) <= 0:
			continue
		if not best.is_empty() and compare(tag, best["tag"]) <= 0:
			continue
		var bytes := 0
		for a in r.get("assets", []):
			if a is Dictionary and String(a.get("name", "")).ends_with(".zip"):
				bytes += int(a.get("size", 0))
		var url := String(r.get("html_url", ""))
		if not url.begins_with("https://github.com/" + REPO + "/") or url.contains(" "):
			url = RELEASES_URL
		best = {"tag": tag, "version": tag.trim_prefix("v"), "name": String(r.get("name", "")).left(120),
			"notes": String(r.get("body", "")).replace("\r", "").left(MAX_NOTES), "url": url, "size": bytes,
			"published": String(r.get("published_at", "")).left(10), "beta": v["pre"] != ""}
	return best


static func skipped() -> String:
	var c := ConfigFile.new()
	c.load(STORE)
	return String(c.get_value("updates", "skipped", ""))


## "Skip this version": that tag is never offered again (a newer one still is).
static func skip(tag: String) -> void:
	if parse_version(tag).is_empty():
		return
	var c := ConfigFile.new()
	c.load(STORE)
	c.set_value("updates", "skipped", tag)
	c.save(STORE)


func _wanted() -> bool:
	if force:
		return true
	return Settings.get_value("network/check_updates") and not OS.has_feature("editor") and DisplayServer.get_name() != "headless"


## Fetches the feed once per run. Silent on any failure (offline, rate limit, bad answer).
func check() -> void:
	if _started or not _wanted():
		return
	_started = true
	if not is_inside_tree():
		await tree_entered
	var http := HTTPRequest.new()
	http.timeout = 12.0
	add_child(http)
	var err := http.request(feed_url, ["User-Agent: Q3Remaster/" + current_version(), "Accept: application/vnd.github+json",
		"X-GitHub-Api-Version: 2022-11-28"])
	if err != OK:
		http.queue_free()
		return
	var res: Array = await http.request_completed
	http.queue_free()
	if res[0] == HTTPRequest.RESULT_SUCCESS and res[1] == 200:
		handle_body((res[3] as PackedByteArray).get_string_from_utf8())


## The feed's answer: finds the newest newer release and announces it unless it was skipped.
func handle_body(text: String) -> void:
	var d: Variant = JSON.parse_string(text)
	if not d is Array:
		return
	# pre-releases are offered when asked for, and always to a build that is itself one
	var with_pre: bool = Settings.get_value("network/update_betas") or parse_version(current_version()).get("pre", "") != ""
	var found := pick(d, current_version(), with_pre)
	if found.is_empty() or found["tag"] == skipped():
		return
	info = found
	available.emit(info)
