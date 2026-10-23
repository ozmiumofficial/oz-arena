class_name Q3MenuBridge
extends RefCounted
## The protocol between the web menu (webui/, a Next.js app in a godot_wry WebView) and Godot.
## Pure logic: main_menu.gd wires it to the WebView, the self-test drives it directly.
## Messages are JSON objects with a "type".
##   web -> Godot  ready, log, sound, cinematic, campaign_start, campaign_skill, campaign_reset, hover, page, layout, set, reset, reset_tab, capture_bind,
##                 cancel_capture, clear_bind, export_code, import_code, validate, start, continue,
##                 map_editor, quit, profile_preview, open_url, social {op, ...}
##                 pause menu: resume, leave_match, restart_match, ready_up, crosshair_preview {weapon, hit}
##   Godot -> web  init, campaign, changed, conflicts, capture_started, bind_captured, capture_cancelled,
##                 validation, start_failed, code, imported, subject, nav, input_mode,
##                 social {status, reason, state, chat}, social_notice, party_chat
##                 pause menu: pause {match}

signal post(msg: Dictionary)                 ## to the web page
signal page_changed(id: String, wide: bool)
signal hovered(id: String)
signal layout_changed(msg: Dictionary)
signal profile_preview(model: String)
signal quit_requested
signal cinematic_requested(id: String)
signal received_ready                        ## the page's handlers are up
signal resume_requested                      ## pause menu: back to the match
signal leave_requested                       ## pause menu: leave the match
signal restart_requested                     ## pause menu: restart the match (offline)
signal crosshair_preview(weapon: int, hit: bool)   ## crosshair editor: show this weapon's / flash the hit color
signal ready_requested                       ## pause menu: ready up (warmup)

var context := "menu"                        ## "pause" when the page is Gameplay's pause menu
var match_info: Dictionary = {}              ## pause menu: the match on show (Gameplay fills it)

var capture_key := ""
var capture_slot := -1
var received: Array = []                     ## every message handled (the self-test reads it)


func _init() -> void:
	Settings.changed.connect(_on_setting_changed)
	Online.changed.connect(func(): post.emit(social_payload()))
	Online.notice.connect(func(text, tone): post.emit({"type": "social_notice", "text": text, "tone": tone}))
	Online.party_chat.connect(func(m): post.emit({"type": "party_chat", "from": m.get("from", {}), "text": m.get("text", ""), "at": m.get("at", 0)}))


## The page is treated as untrusted input (Ozmium, 2026-10-22: nobody injects code through the web
## UI). Godot only ever acts on the fixed message types below, with checked fields; anything else,
## anything oversized and anything that isn't a JSON object is dropped. main_menu.gd also only
## listens while the web view shows our own bundled page.
const MAX_MESSAGE := 65536
const TYPES := ["ready", "sound", "hover", "page", "layout", "set", "reset", "reset_tab", "capture_bind",
	"cancel_capture", "clear_bind", "export_code", "import_code", "validate", "start", "continue", "campaign_start",
	"campaign_skill", "campaign_reset", "cinematic", "map_editor", "quit", "profile_preview", "log", "social",
	"open_url", "modes", "bots", "banner", "crosshairs", "cinematics",
	"resume", "leave_match", "restart_match", "ready_up", "crosshair_preview"]
## Hosts open_url may send the player's browser to; nothing else leaves the game.
const URL_HOSTS := ["store.steampowered.com", "steamcommunity.com", "github.com"]


func handle_json(text: String) -> void:
	if text.length() > MAX_MESSAGE:
		push_warning("menu bridge: dropped an oversized message (%d bytes)" % text.length())
		return
	var d: Variant = JSON.parse_string(text)
	if d is Dictionary:
		handle(d)
	else:
		push_warning("menu bridge: not a JSON object: " + text.left(200))


func handle(m: Dictionary) -> void:
	if not m.get("type") is String or not TYPES.has(m["type"]):
		push_warning("menu bridge: dropped unknown message type " + str(m.get("type")).left(40))
		return
	received.append(m)
	match String(m.get("type", "")):
		"ready":
			post.emit(init_payload())
			post.emit({"type": "conflicts", "map": _conflict_map()})
			post.emit(social_payload())
			received_ready.emit()
		"sound":
			var n := String(m.get("name", ""))
			if n == "in":
				UiSound.play_in()
			elif UiSound.SOUNDS.has(n):
				UiSound.play(n)
		"hover":
			hovered.emit(String(m.get("id", "")))
		"page":
			page_changed.emit(String(m.get("id", "main")), bool(m.get("wide", false)))
		"layout":
			layout_changed.emit(m)
		"set":
			Settings.set_value(String(m["key"]), decode(String(m["key"]), m.get("value")))
		"reset":
			Settings.reset(String(m["key"]))
		"reset_tab":
			Settings.reset_tab(String(m["tab"]))
		"capture_bind":
			capture_key = String(m["key"])
			capture_slot = int(m.get("slot", 0))
			post.emit({"type": "capture_started", "key": capture_key, "slot": capture_slot})
		"cancel_capture":
			_end_capture(true)
		"clear_bind":
			Settings.set_bind(String(m["key"]), int(m["slot"]), "")
		"export_code":
			var code := Settings.export_code(String(m.get("prefix", "")))
			if DisplayServer.get_name() != "headless":
				DisplayServer.clipboard_set(code)
			post.emit({"type": "code", "prefix": m.get("prefix", ""), "code": code})
		"import_code":
			post.emit({"type": "imported", "count": Settings.import_code(String(m.get("code", "")))})
		"validate":
			post.emit({"type": "validation", "errors": Array(_setup_from(m).validate())})
		"start":
			_start(_setup_from(m))
		"continue":
			var s := Session.last_setup()
			if s:
				_start(s)
			else:
				post.emit({"type": "start_failed", "errors": ["No match to continue."]})
		"campaign_start":
			var c := Session.campaign
			var map := String(m.get("map", ""))
			var tier := c.arena_tier(map)
			if tier < 0 or not c.tier_open(tier):
				UiSound.buzz()
				post.emit({"type": "start_failed", "errors": ["Win every arena of the tier before to open this one."]})
				return
			c.progress["skill"] = clampi(int(m.get("skill", 3)), 1, 5)
			c.save()
			_start(c.setup_for(map, int(c.progress["skill"])))
		"campaign_skill":
			Session.campaign.progress["skill"] = clampi(int(m.get("skill", 3)), 1, 5)
			Session.campaign.save()
		"campaign_reset":
			Session.campaign.reset()
			post.emit({"type": "campaign", "campaign": Session.campaign.to_dict(), "cinematics": cinematics()})
		"cinematic":
			var id := String(m.get("id", ""))
			if Session.cinematic_unlocked(id) and Q3CinematicPlayer.exists(id):
				cinematic_requested.emit(id)
			else:
				UiSound.buzz()
				post.emit({"type": "start_failed", "errors": ["Finish more of the campaign to unlock this movie."]})
		"map_editor":
			Session.open_map_editor()
		"quit":
			quit_requested.emit()
		"profile_preview":
			profile_preview.emit(String(m.get("model", "")))
		"log":
			# the page's own errors and milestones, so they show in Godot's log
			print("web menu: ", m.get("text", ""))
		"social":
			# friends and parties: the Online autoload passes these on to the hub
			if not Online.request(String(m.get("op", "")), m):
				UiSound.buzz()
		"resume":
			resume_requested.emit()
		"leave_match":
			leave_requested.emit()
		"restart_match":
			restart_requested.emit()
		"ready_up":
			ready_requested.emit()
		"crosshair_preview":
			crosshair_preview.emit(clampi(int(m.get("weapon", 0)), 0, 10), bool(m.get("hit", false)))
		"open_url":
			var url := String(m.get("url", ""))
			var host := url.trim_prefix("https://").get_slice("/", 0).to_lower()
			if url.begins_with("https://") and URL_HOSTS.has(host) and not url.contains(" ") and url.length() < 512:
				OS.shell_open(url)
			else:
				push_warning("menu bridge: refused to open " + url.left(80))


## While a bind is being captured, main_menu.gd routes input here. Returns true when used.
## Escape cancels, Backspace/Delete clears the slot; any other key, mouse button (wheel
## included) or pad button/trigger becomes the bind.
func capture_input(ev: InputEvent) -> bool:
	if capture_key == "" or not ev.is_pressed() or ev.is_echo():
		return false
	if ev is InputEventMouseMotion:
		return false
	if ev is InputEventKey:
		var code: Key = ev.physical_keycode if ev.physical_keycode != KEY_NONE else ev.keycode
		if code == KEY_ESCAPE:
			_end_capture(true)
			return true
		if code == KEY_BACKSPACE or code == KEY_DELETE:
			Settings.set_bind(capture_key, capture_slot, "")
			_end_capture(false)
			return true
	var s := Settings.event_to_string(ev)
	if s == "":
		return false
	Settings.set_bind(capture_key, capture_slot, s)
	post.emit({"type": "bind_captured", "key": capture_key, "slot": capture_slot, "value": s, "label": Settings.bind_label(s)})
	_end_capture(false)
	return true


func _end_capture(cancelled: bool) -> void:
	if capture_key != "" and cancelled:
		post.emit({"type": "capture_cancelled", "key": capture_key})
	capture_key = ""
	capture_slot = -1


func _start(s: Q3MatchSetup) -> void:
	var errs := Session.start_match(s)
	if errs.size():
		post.emit({"type": "start_failed", "errors": Array(errs)})


func _setup_from(m: Dictionary) -> Q3MatchSetup:
	var s := Q3MatchSetup.from_dict(m.get("setup", {}))
	var base := Session.new_setup(s.mode, s.map)
	s.player = base.player
	return s


## Everything the web menu needs to draw itself.
func init_payload() -> Dictionary:
	var schema: Array = []
	for e in Settings.entries:
		var o := {}
		for k in ["key", "tab", "group", "label", "type", "tip", "wired", "restart", "min", "max", "step", "unit"]:
			if e.has(k):
				o[k] = e[k]
		o["default"] = encode(e["default"])
		if e["type"] == "enum":
			o["options"] = Settings.options(e["key"]).map(func(x): return [x[0], x[1]])
		schema.append(o)
	var values := {}
	for e in Settings.entries:
		values[e["key"]] = encode(Settings.get_value(e["key"]))
	var modes: Array = []
	for id in Session.MODES:
		var md := Session.mode(id)
		if md:
			modes.append({"id": md.id, "name": md.display_name, "description": md.description, "team": md.team_based,
				"rounds": md.rounds, "map_types": Array(md.map_types), "rules": md.rules, "modifiers": md.modifiers})
	var last := Session.last_setup()
	var fonts: Dictionary = Q3.load_json(Q3Fonts.FONTS + "fonts.json")
	var banner: Dictionary = fonts.get("banner", {})
	banner.erase("file_glow")
	return {"type": "init", "version": String(ProjectSettings.get_setting("application/config/version", "")),
		"tabs": Q3SettingsSchema.TABS, "schema": schema, "values": values, "modes": modes,
		"rules": Q3GameMode.RULES, "modifiers": Session.modifiers, "maps": Session.maps, "bots": Session.bots,
		"skills": Q3MatchSetup.SKILLS, "characters": Array(Q3.characters), "banner": banner,
		"crosshairs": Q3.load_json(Q3.LIB + "ui/ui.json").get("crosshairs", []),
		"continue": last.summary() if last else "", "m_yaw": Settings.M_YAW, "context": context, "match": match_info,
		"progress": {"level": int(Q3Profile.level()["level"]), "ranked_level": Q3Profile.RANKED_LEVEL}, "campaign": Session.campaign.to_dict(), "cinematics": cinematics()}


## Friends and party: connection status, the hub's last picture, and the party chat.
func social_payload() -> Dictionary:
	var d := Online.to_dict()
	d["type"] = "social"
	return d


## The Cinematics page: every movie with whether the campaign has unlocked it yet.
func cinematics() -> Array:
	var out: Array = []
	for c in Session.cinematics:
		var d: Dictionary = c.duplicate()
		d["unlocked"] = Session.cinematic_unlocked(c["id"])
		out.append(d)
	return out


static func encode(v: Variant) -> Variant:
	if v is Color:
		return "#" + v.to_html(false)
	return v


static func decode(key: String, v: Variant) -> Variant:
	var e := Settings.entry(key)
	if e.get("type", "") == "color" and v is String and Color.html_is_valid(v):
		return Color(v)
	if e.get("type", "") == "int" and v is float:
		return int(round(v))
	return v


func _on_setting_changed(key: String, value: Variant) -> void:
	post.emit({"type": "changed", "key": key, "value": encode(value)})
	if Settings.entry(key).get("type", "") == "bind":
		post.emit({"type": "conflicts", "map": _conflict_map()})


func _conflict_map() -> Dictionary:
	var out := {}
	for e in Settings.entries:
		if e["type"] == "bind":
			var c := Settings.bind_conflicts(e["key"])
			if c.size():
				out[e["key"]] = Array(c)
	return out
