class_name Q3MenuBridge
extends RefCounted
## The protocol between the web menu (webui/, a Next.js app in a godot_wry WebView) and Godot.
## Pure logic: main_menu.gd wires it to the WebView, the self-test drives it directly.
## Messages are JSON objects with a "type".
##   web -> Godot  ready, log, sound, cinematic, campaign_start, campaign_skill, campaign_reset, hover, page, layout, set, reset, reset_tab, capture_bind,
##                 cancel_capture, clear_bind, export_code, import_code, validate, start, continue,
##                 map_editor, quit, profile_preview, open_url, social {op, ...}
##                 card_set {slot, id}, card_showcase {kind: stats|badges, ids}, card_seen {keys}, taunt {id}
##                 update_skip {tag}
##                 net_browse {region?}, net_join {host, password?}, net_cancel (online matches, MULTIPLAYER_PLAN.md M5)
##                 queue_join {modes, ranked}, queue_leave (matchmaking, M6)
##                 dedicated_get, dedicated_start {config}, dedicated_stop, dedicated_command {line}, dedicated_autostart {on} (M7)
##                 pause menu: resume, leave_match, restart_match, ready_up, crosshair_preview {weapon, hit}
##   Godot -> web  init, campaign, changed, conflicts, capture_started, bind_captured, capture_cancelled,
##                 validation, start_failed, code, imported, subject, nav, input_mode,
##                 social {status, reason, state, chat}, social_notice, party_chat, profile {profile},
##                 net_matches {matches, region}, net_join_state {status, reason, host}, queue {state, ratings, rtt}, dedicated {config, status, autostart},
##                 leaderboard {board} (asked for with social {op: "leaderboard", board, scope})
##                 update {info} (a newer release exists: tag, version, name, notes, url, size, published, beta)
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
signal spectate_requested(on: bool)           ## pause menu: become a spectator (true) or join the match

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
	Online.leaderboard.connect(func(b): post.emit({"type": "leaderboard", "board": b}))
	Online.queue_changed.connect(func(): post.emit(queue_payload()))
	Online.matches.connect(func(m): post.emit({"type": "net_matches", "matches": m.get("matches", []), "region": m.get("region", "")}))
	Online.join_state.connect(func(s): post.emit({"type": "net_join_state", "status": s.get("status", ""), "reason": s.get("reason", ""), "host": s.get("host", "")}))
	# the last run crashed: say so once (Diagnostics wrote the report at this launch)
	if Diagnostics.notice != "":
		var text := Diagnostics.notice
		Diagnostics.notice = ""
		received_ready.connect(func(): post.emit({"type": "social_notice", "text": text, "tone": "warn"}), CONNECT_ONE_SHOT)


## Tells the page about a newer release: now if the check already found one, else when it does.
func _offer_update() -> void:
	var u := Q3UpdateCheck.instance()
	if not u.info.is_empty():
		post.emit({"type": "update", "info": u.info})
		return
	if not u.available.is_connected(_on_update_available):
		u.available.connect(_on_update_available)
	u.check()


func _on_update_available(info: Dictionary) -> void:
	post.emit({"type": "update", "info": info})


## The page is treated as untrusted input (Ozmium, 2026-10-22: nobody injects code through the web
## UI). Godot only ever acts on the fixed message types below, with checked fields; anything else,
## anything oversized and anything that isn't a JSON object is dropped. main_menu.gd also only
## listens while the web view shows our own bundled page.
const MAX_MESSAGE := 65536
const TYPES := ["ready", "sound", "hover", "page", "layout", "set", "reset", "reset_tab", "capture_bind",
	"cancel_capture", "clear_bind", "export_code", "import_code", "validate", "start", "continue", "campaign_start",
	"campaign_skill", "campaign_reset", "cinematic", "map_editor", "quit", "profile_preview", "log", "social",
	"open_url", "modes", "bots", "banner", "crosshairs", "cinematics",
	"resume", "leave_match", "restart_match", "ready_up", "crosshair_preview",
	"card_set", "card_showcase", "card_seen", "taunt", "update_skip",
	"net_browse", "net_join", "net_cancel", "queue_join", "queue_leave", "dedicated_get", "dedicated_start", "dedicated_stop", "dedicated_command", "dedicated_autostart",
	"spectate", "replays", "replay_watch", "replay_rename", "replay_favorite", "replay_delete", "replay_folder", "maps_folder"]
## Hosts open_url may send the player's browser to; nothing else leaves the game.
const URL_HOSTS := ["store.steampowered.com", "steamcommunity.com", "github.com", "discord.gg"]


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
			post.emit(queue_payload())
			if context == "menu":
				_offer_update()
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
		"net_browse":
			if not Online.net_browse(String(m.get("region", ""))):
				post.emit({"type": "net_matches", "matches": [], "region": "", "offline": true})
		"net_join":
			var host := String(m.get("host", ""))
			if host.length() == 20 and Online.client.status == "online":   # a player id: 20 hex digits
				Online.net_join(host, String(m.get("password", "")).left(40))
			else:
				UiSound.buzz()
				post.emit({"type": "net_join_state", "status": "failed", "reason": "Not connected to the online service.", "host": host})
		"net_cancel":
			Online.net_cancel()
		"queue_join":
			var modes: Array = []
			for x in (m.get("modes", []) if m.get("modes", []) is Array else []):
				if String(x) in ["duel", "ffa", "tdm", "ctf"]:
					modes.append(String(x))
			if modes.is_empty() or Online.client.status != "online" or not Online.queue_join(modes, bool(m.get("ranked", false))):
				UiSound.buzz()
				post.emit({"type": "queue", "state": {"status": "refused", "reason": "Not connected to the online service." if Online.client.status != "online" else "Pick a playlist."}, "ratings": Online.ratings, "rtt": Online.client.rtt})
		"queue_leave":
			Online.queue_leave()
		"dedicated_get":
			post.emit(dedicated_payload())
		"dedicated_start":
			var c: Variant = m.get("config", {})
			var why := Q3DedicatedLauncher.start(_dedicated_config(c as Dictionary) if c is Dictionary else Q3Dedicated.load_config(Q3Dedicated.config_path()))
			if why != "":
				UiSound.buzz()
				post.emit({"type": "social_notice", "text": why, "tone": "warn"})
			post.emit(dedicated_payload())
		"dedicated_stop":
			Q3DedicatedLauncher.stop()
			post.emit(dedicated_payload())
		"dedicated_command":
			Q3DedicatedLauncher.command(String(m.get("line", "")).left(200))
		"dedicated_autostart":
			if not Q3DedicatedLauncher.set_autostart(bool(m.get("on", false))):
				UiSound.buzz()
			post.emit(dedicated_payload())
		"resume":
			resume_requested.emit()
		"leave_match":
			leave_requested.emit()
		"restart_match":
			restart_requested.emit()
		"ready_up":
			ready_requested.emit()
		"spectate":
			spectate_requested.emit(bool(m.get("on", false)))
		"crosshair_preview":
			crosshair_preview.emit(clampi(int(m.get("weapon", 0)), 0, 10), bool(m.get("hit", false)))
		"card_set":
			# the page only offers earned parts; Q3Profile checks again, and the page redraws from the answer
			if not Q3Profile.set_card(String(m.get("slot", "")), String(m.get("id", ""))):
				UiSound.buzz()
			post.emit(profile_payload())
		"card_showcase":
			var ids: Variant = m.get("ids", [])
			if not (ids is Array and Q3Profile.set_showcase(String(m.get("kind", "")), ids)):
				UiSound.buzz()
			post.emit(profile_payload())
		"card_seen":
			var keys: Variant = m.get("keys", [])
			if keys is Array:
				Q3Profile.mark_seen(keys)
		"taunt":
			var t := Q3Cards.part("taunts", String(m.get("id", "")))
			if t.is_empty() or not UiSound.play_file(Q3.LIB + "sounds/" + String(t.get("sound", "")).trim_prefix("sound/")):
				UiSound.buzz()
		"replays":
			post.emit(replays_payload())
		"replay_watch":
			var path := _replay_file(m)
			if path == "":
				UiSound.buzz()
				post.emit({"type": "start_failed", "errors": ["That replay is gone."]})
				return
			# the file's own map, not the page's word for it
			var info := Q3ReplayFile.read_demo_info(path) if Q3ReplayFile.is_demo(path) else Q3ReplayFile.read_info(path)
			var map := String(info.get("map", ""))
			if map != "" and Session.map_info(map).is_empty():
				UiSound.buzz()
				post.emit({"type": "start_failed", "errors": ["%s isn't one of the remaster's maps, so there's nothing to draw it with." % map]})
				return
			Session.watch_replay(path, maxi(int(m.get("at", 0)), 0), "replays")
		"replay_rename", "replay_favorite", "replay_delete":
			var path := _replay_file(m)
			var where := String(m.get("where", "replays"))
			if path == "":
				UiSound.buzz()
			elif m["type"] == "replay_rename":
				Q3ReplayFile.rename(path.get_file(), String(m.get("name", "")), where)
			elif m["type"] == "replay_favorite":
				Q3ReplayFile.set_favorite(path.get_file(), bool(m.get("on", false)), where)
			elif where != "replays" or not Q3ReplayFile.delete(path.get_file()):
				UiSound.buzz()   # the Quake 3 install's demos stay where they are
			post.emit(replays_payload())
		"replay_folder":
			DirAccess.make_dir_recursive_absolute(Q3ReplayFile.dir)
			OS.shell_open(ProjectSettings.globalize_path(Q3ReplayFile.dir))
		"maps_folder":   # the Maps folder (community pk3s, race maps), made if it isn't there yet
			var dir := Q3MapPacks.dir()
			DirAccess.make_dir_recursive_absolute(dir)
			OS.shell_open(dir)
		"update_skip":
			Q3UpdateCheck.skip(String(m.get("tag", "")))
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
			# the page closes its "press a key" veil on this; without it the veil stayed up until its timeout
			post.emit({"type": "bind_captured", "key": capture_key, "slot": capture_slot, "value": "", "label": Settings.bind_label("")})
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
		"skills": Q3MatchSetup.SKILLS, "characters": Array(Q3.characters), "skins": skins_payload(), "banner": banner,
		"crosshairs": Q3.load_json(Q3.LIB + "ui/ui.json").get("crosshairs", []),
		"continue": last.summary() if last else "", "m_yaw": Settings.M_YAW, "context": context, "match": match_info,
		"progress": {"level": int(Q3Profile.level()["level"]), "ranked_level": Q3Profile.RANKED_LEVEL}, "campaign": Session.campaign.to_dict(), "cinematics": cinematics(),
		"cards": cards_payload(), "profile": Q3Profile.menu_payload(), "race_records": Q3RaceRecords.summary()}


## Each character's skins after default (Q3Character.skins_of): the Profile page's Skin choices.
static func skins_payload() -> Dictionary:
	var out := {}
	for c in Q3.characters:
		out[c] = Array(Q3Character.skins_of(c).slice(1))
	return out


## The Replays page's list: every replay in user://replays and every Quake 3 demo found, newest
## first, with what the page shows of its header and trailer (Q3ReplayFile.list).
func replays_payload() -> Dictionary:
	var out: Array = []
	for r in Q3ReplayFile.list():
		var map := String(r.get("map", ""))
		var mode := Session.mode(String(r.get("mode", "")))
		var rows: Array = []
		for s in r.get("standings", []):
			if s is Dictionary:
				rows.append({"name": String(s.get("name", "")), "score": int(s.get("score", 0)), "local": bool(s.get("local", false)),
					"bot": bool(s.get("bot", false)), "model": String(s.get("model", "")), "team": int(s.get("team", 0))})
		out.append({"file": r["file"], "where": String(r.get("where", "replays")), "demo": bool(r.get("demo", false)),
			"name": r["name"], "favorite": r["favorite"], "complete": r["complete"],
			"title": String(r.get("title", "")), "map": map,
			"map_name": String(r.get("map_name", Session.map_info(map).get("name", map))) if not r.get("demo", false) else String(Session.map_info(map).get("name", map)),
			"mode": String(r.get("mode", "")), "mode_name": String(r.get("mode_name", mode.display_name if mode else r.get("mode", ""))),
			"date": String(r.get("date", "")), "unix": int(r.get("unix", 0)), "length_ms": int(r.get("length_ms", 0)),
			"result": String(r.get("result", "")), "place": int(r.get("place", 0)), "ended": String(r.get("ended", "")),
			"player": String(r.get("player", "")), "bytes": int(r.get("bytes", 0)), "players": rows,
			"playable": not Session.map_info(map).is_empty()})
	return {"type": "replays", "list": out, "kept": int(Settings.get_value("gameplay/replays_kept"))}


## A replay named by the page: the full path of a bare file name of ours (or a demo in the
## Quake 3 install's demos) that exists, else "".
func _replay_file(m: Dictionary) -> String:
	return Q3ReplayFile.path_of(String(m.get("file", "")), String(m.get("where", "replays")))


## The player card catalog for the Profile page: every part with the text saying how it unlocks,
## the achievements, the showcase stats and the ranked tiers (library/cards/cards.json).
func cards_payload() -> Dictionary:
	var cat := Q3Cards.catalog()
	var parts := {}
	for kind in Q3Cards.SLOTS.values():
		parts[kind] = Q3Cards.parts(kind).map(func(p):
			var o: Dictionary = p.duplicate()
			o["how"] = Q3Cards.unlock_text(p)
			o["free"] = p["unlock"].get("free", false)
			o["level"] = int(p["unlock"].get("level", 0))
			if p["unlock"].has("achievement"):
				o["ach"] = p["unlock"]["achievement"]
			o.erase("unlock")
			return o)
	var achs: Array = Q3Cards.achievements().map(func(a):
		var o: Dictionary = a.duplicate()
		o.erase("progress")
		return o)
	return {"parts": parts, "achievements": achs, "card_stats": cat.get("card_stats", []),
		"medal_colors": cat.get("medal_colors", {}), "ranked_tiers": cat.get("ranked_tiers", []), "ranked": cat.get("ranked", {}),
		"default_card": Q3Cards.default_card()}


func profile_payload() -> Dictionary:
	return {"type": "profile", "profile": Q3Profile.menu_payload()}


## Friends and party: connection status, the hub's last picture, and the party chat.
## What the page may set of a dedicated server's config: known keys, bounded values (the file is read by another process).
func _dedicated_config(c: Dictionary) -> Dictionary:
	var out := Q3Dedicated.DEFAULTS.duplicate(true)
	for k in ["name", "password", "motd", "rcon_password"]:
		out[k] = String(c.get(k, out[k])).strip_edges().left(60)
	out["mode"] = String(c.get("mode", "ffa")) if String(c.get("mode", "")) in Session.MODES else "ffa"
	var rot: Array = []
	for x in (c.get("rotation", []) if c.get("rotation", []) is Array else []):
		if not Session.map_info(String(x)).is_empty() and rot.size() < 32:
			rot.append(String(x))
	out["rotation"] = rot
	out["max_players"] = clampi(int(c.get("max_players", 16)), 2, 32)
	out["bots"] = clampi(int(c.get("bots", 0)), 0, 16)
	out["bot_skill"] = clampi(int(c.get("bot_skill", 3)), 1, 5)
	out["idle_sleep"] = bool(c.get("idle_sleep", true))
	out["online"] = bool(c.get("online", true))
	# not on the page, but kept from the saved config it was loaded from (a hand-edited fraglimit, a UDP port)
	out["rules"] = c["rules"] if c.get("rules") is Dictionary else {}
	out["udp_port"] = int(c.get("udp_port", 0))
	return Q3Dedicated.clean_config(out)


func dedicated_payload() -> Dictionary:
	return {"type": "dedicated", "config": Q3Dedicated.load_config(Q3Dedicated.config_path()), "status": Q3DedicatedLauncher.status(),
		"autostart": Q3DedicatedLauncher.autostart_enabled()}


func queue_payload() -> Dictionary:
	return {"type": "queue", "state": Online.queue_state, "ratings": Online.ratings, "rtt": Online.client.rtt}


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
