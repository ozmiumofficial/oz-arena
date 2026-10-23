class_name Q3SettingsSchema
extends RefCounted
## Every setting as data. The Settings autoload, the settings menu (rows, search, tooltips,
## reset) and the self-test all read this list; adding a setting is one line here.
## Entry: key "tab/name", label, type (bool int float enum color string bind), default, tip,
## plus min/max/step/unit (numbers), options [[value, label]...] (enum), group (sub-heading),
## wired = false when the system it drives doesn't exist yet (stored and shown, marked),
## restart = true when it only takes effect on the next launch.

const TABS := [["video", "Video"], ["graphics", "Graphics"], ["audio", "Audio"], ["controls", "Controls"],
	["mouse", "Mouse"], ["crosshair", "Crosshair"], ["hud", "HUD"], ["gameplay", "Gameplay"],
	["network", "Network"], ["accessibility", "Accessibility"]]

## Graphics presets: setting -> value at [low, medium, high, ultra].
const PRESETS := {
	"graphics/shadows": ["low", "medium", "high", "ultra"],
	"graphics/gi": ["off", "off", "off", "sdfgi"],
	"graphics/reflections": [false, false, true, true],
	"graphics/ao": ["off", "low", "medium", "high"],
	"graphics/volumetric_fog": [false, true, true, true],
	"graphics/bloom": [true, true, true, true],
	"graphics/antialiasing": ["fxaa", "msaa2", "msaa4", "taa"],
	"graphics/anisotropy": [2, 4, 16, 16],
	"graphics/texture_quality": ["low", "medium", "high", "high"],
	"graphics/particles": ["low", "medium", "high", "high"],
	"graphics/detail": ["low", "medium", "high", "high"],
}
const PRESET_NAMES := ["low", "medium", "high", "ultra"]

## Controls: action id, label, default binds [primary, secondary, controller]. Action names in
## the InputMap are "q3_" + id. Weapon numbers follow Quake 3 (1 gauntlet ... 9 BFG).
const BINDS := [
	["forward", "Move forward", ["key:W", "key:Up", ""]],
	["back", "Move back", ["key:S", "key:Down", ""]],
	["moveleft", "Strafe left", ["key:A", "key:Left", ""]],
	["moveright", "Strafe right", ["key:D", "key:Right", ""]],
	["jump", "Jump / swim up", ["key:Space", "", "joy:0"]],
	["crouch", "Crouch / swim down", ["key:C", "key:Ctrl", "joy:1"]],
	["walk", "Walk", ["key:Shift", "", "joy:7"]],
	["attack", "Attack", ["mouse:1", "", "joyaxis:5:1"]],
	["zoom", "Zoom", ["mouse:2", "", "joyaxis:4:1"]],
	["weapnext", "Next weapon", ["mouse:5", "", "joy:10"]],
	["weapprev", "Previous weapon", ["mouse:4", "", "joy:9"]],
	["weapon1", "Gauntlet", ["key:1", "", ""]],
	["weapon2", "Machine gun", ["key:2", "", ""]],
	["weapon3", "Shotgun", ["key:3", "", ""]],
	["weapon4", "Grenade launcher", ["key:4", "", ""]],
	["weapon5", "Rocket launcher", ["key:5", "", ""]],
	["weapon6", "Lightning gun", ["key:6", "", ""]],
	["weapon7", "Railgun", ["key:7", "", ""]],
	["weapon8", "Plasma gun", ["key:8", "", ""]],
	["weapon9", "BFG10K", ["key:9", "", ""]],
	["use_item", "Use item", ["key:Enter", "", "joy:3"]],
	["gesture", "Gesture", ["key:G", "", ""]],
	["scores", "Scoreboard", ["key:Tab", "", "joy:4"]],
	["chat", "Chat", ["key:T", "", ""]],
	["teamchat", "Team chat", ["key:Y", "", ""]],
	["voice", "Push to talk", ["key:V", "", ""]],
	["screenshot", "Screenshot", ["key:F12", "", ""]],
	["console", "Console", ["key:QuoteLeft", "", ""]],
]
const WEAPONS := [["gauntlet", "Gauntlet"], ["machinegun", "Machine gun"], ["shotgun", "Shotgun"],
	["grenadel", "Grenade launcher"], ["rocketl", "Rocket launcher"], ["lightning", "Lightning gun"],
	["railgun", "Railgun"], ["plasma", "Plasma gun"], ["bfg", "BFG10K"]]
const CROSSHAIRS := [["none", "None"], ["a", "Quake 3 A"], ["b", "Quake 3 B"], ["c", "Quake 3 C"], ["d", "Quake 3 D"],
	["e", "Quake 3 E"], ["f", "Quake 3 F"], ["g", "Quake 3 G"], ["h", "Quake 3 H"], ["i", "Quake 3 I"], ["j", "Quake 3 J"],
	["cross", "Vector cross"], ["dot", "Vector dot"], ["circle", "Vector circle"], ["t", "Vector T"]]

static var _list: Array = []


static func build() -> Array:
	if _list.size():
		return _list
	var L: Array = []
	var off := [["off", "Off"], ["low", "Low"], ["medium", "Medium"], ["high", "High"]]
	var lmh := [["low", "Low"], ["medium", "Medium"], ["high", "High"]]
	var game := "Stored now; takes effect once the Gameplay scene exists."

	# ---- Video
	var g := "Display"
	L.append(_e("video/display_mode", "Display mode", "enum", "windowed", "Windowed, borderless full screen, or exclusive full screen (lowest latency).", g,
		{"options": [["windowed", "Windowed"], ["borderless", "Borderless"], ["fullscreen", "Full screen"], ["exclusive", "Exclusive full screen"]]}))
	L.append(_e("video/resolution", "Window size", "enum", "1600x900", "Window size in windowed mode. Full screen always uses the monitor's resolution; use Render scale to change the 3D resolution there.", g,
		{"options": [["1280x720", "1280 x 720"], ["1600x900", "1600 x 900"], ["1920x1080", "1920 x 1080"], ["2560x1440", "2560 x 1440"], ["3840x2160", "3840 x 2160"]]}))
	L.append(_e("video/monitor", "Monitor", "int", 0, "Which monitor the game opens on.", g, {"min": 0, "max": 7, "step": 1}))
	L.append(_e("video/vsync", "VSync", "enum", "off", "Off gives the lowest input latency. Adaptive syncs only when above the refresh rate.", g,
		{"options": [["off", "Off"], ["on", "On"], ["adaptive", "Adaptive"], ["mailbox", "Mailbox (fast)"]]}))
	L.append(_e("video/max_fps", "Frame cap", "int", 0, "Maximum frames per second. 0 = uncapped. Physics runs at its own fixed rate, so the cap never changes movement (no 125 fps jump quirk).", g,
		{"min": 0, "max": 1000, "step": 5, "unit": "fps"}))
	g = "View"
	L.append(_e("video/fov", "Field of view", "float", 90.0, "Horizontal degrees at 4:3, like Quake 3's cg_fov. Wider screens see more at the sides (Hor+), so it's correct at any aspect. " + game, g,
		{"min": 60.0, "max": 130.0, "step": 1.0, "unit": "deg", "wired": false}))
	g = "Rendering"
	L.append(_e("video/render_scale", "Render scale", "float", 1.0, "3D resolution relative to the window. The menu and HUD always render at full resolution.", g,
		{"min": 0.5, "max": 2.0, "step": 0.05, "unit": "x"}))
	L.append(_e("video/upscaler", "Upscaler", "enum", "bilinear", "How the 3D image is scaled when render scale isn't 1.", g,
		{"options": [["bilinear", "Off (bilinear)"], ["fsr", "AMD FSR 1"], ["fsr2", "AMD FSR 2"]]}))
	L.append(_e("video/brightness", "Brightness", "float", 1.0, "Overall brightness of the 3D image.", g, {"min": 0.5, "max": 2.0, "step": 0.05, "unit": "x"}))
	L.append(_e("video/contrast", "Contrast", "float", 1.0, "Contrast of the 3D image.", g, {"min": 0.5, "max": 1.5, "step": 0.05, "unit": "x"}))

	# ---- Graphics
	g = "Quality"
	L.append(_e("graphics/preset", "Preset", "enum", "high", "Sets every quality setting below. Changing one of them switches this to Custom.", g,
		{"options": [["low", "Low"], ["medium", "Medium"], ["high", "High"], ["ultra", "Ultra"], ["custom", "Custom"]]}))
	g = "Lighting"
	L.append(_e("graphics/shadows", "Shadows", "enum", "high", "Shadow map resolution and filtering.", g, {"options": off + [["ultra", "Ultra"]]}))
	L.append(_e("graphics/gi", "Global illumination", "enum", "off", "Real-time bounce light (SDFGI). Maps are relit in-engine instead of using baked lightmaps.", g,
		{"options": [["off", "Off"], ["sdfgi", "SDFGI"]]}))
	L.append(_e("graphics/reflections", "Screen-space reflections", "bool", true, "Reflections on wet and polished floors.", g))
	L.append(_e("graphics/ao", "Ambient occlusion", "enum", "medium", "Contact shadows in corners and creases (SSAO).", g, {"options": off}))
	L.append(_e("graphics/volumetric_fog", "Volumetric fog", "bool", true, "Light shafts and the menu's drifting smoke.", g))
	L.append(_e("graphics/bloom", "Bloom", "bool", true, "Glow around bright light and energy weapons.", g))
	g = "Image"
	L.append(_e("graphics/antialiasing", "Anti-aliasing", "enum", "msaa4", "Smooths jagged edges. TAA is smoothest but softens motion.", g,
		{"options": [["off", "Off"], ["fxaa", "FXAA"], ["msaa2", "MSAA 2x"], ["msaa4", "MSAA 4x"], ["msaa8", "MSAA 8x"], ["taa", "TAA"]]}))
	L.append(_e("graphics/anisotropy", "Anisotropic filtering", "enum", 16, "Sharpness of textures seen at an angle.", g,
		{"options": [[2, "2x"], [4, "4x"], [8, "8x"], [16, "16x"]], "restart": true}))
	L.append(_e("graphics/texture_quality", "Texture quality", "enum", "high", "Lower settings use smaller mipmaps (less video memory).", g, {"options": lmh}))
	L.append(_e("graphics/detail", "Model detail", "enum", "high", "How soon distant models switch to simpler meshes.", g, {"options": lmh}))
	g = "Effects"
	L.append(_e("graphics/particles", "Particles", "enum", "high", "Amount of smoke, sparks and embers.", g, {"options": lmh}))
	L.append(_e("graphics/effect_intensity", "Effect intensity", "float", 1.0, "Brightness of explosions and weapon effects. " + game, g, {"min": 0.25, "max": 1.0, "step": 0.05, "unit": "x", "wired": false}))
	L.append(_e("graphics/decals", "Marks and decals", "bool", true, "Bullet holes, burns and blood on walls. " + game, g))
	L.append(_e("graphics/gibs", "Gibs", "bool", true, "Bodies burst into gibs on big hits (cg_gibs). " + game, g, {"wired": false}))

	# ---- Audio
	g = "Volume"
	for b in [["master", "Master"], ["music", "Music"], ["effects", "Effects"], ["voice", "Voice chat"], ["announcer", "Announcer"], ["ui", "Menu sounds"]]:
		L.append(_e("audio/vol_" + b[0], b[1], "int", 80 if b[0] == "music" else 100, "%s volume." % b[1], g, {"min": 0, "max": 100, "step": 1, "unit": "%"}))
	L.append(_e("audio/menu_ambient", "Menu ambience", "bool", false, "A quiet ambient drone in the menu. Quake 3's menu had no music, so it's off by default.", g))
	L.append(_e("audio/mute_unfocused", "Mute when in background", "bool", true, "Silence the game while its window isn't focused.", g))
	g = "Devices"
	L.append(_e("audio/output_device", "Output device", "enum", "Default", "Where sound plays.", g, {"options": [["Default", "Default"]], "dynamic": "output_devices"}))
	L.append(_e("audio/input_device", "Voice chat device", "enum", "Default", "Microphone for voice chat.", g, {"options": [["Default", "Default"]], "dynamic": "input_devices"}))
	L.append(_e("audio/voice_mode", "Voice chat", "enum", "push", "Push to talk uses the Push to talk bind. " + "Voice chat comes with Steam.", g,
		{"options": [["off", "Off"], ["push", "Push to talk"], ["open", "Open mic"]], "wired": false}))
	g = "Game sounds"
	L.append(_e("audio/hit_sound", "Hit sound", "enum", "classic", "The beep when you hit an enemy. " + game, g,
		{"options": [["off", "Off"], ["classic", "Quake 3 classic"], ["pitch", "Pitch by damage"]], "wired": false}))
	L.append(_e("audio/spatial", "Spatial audio (HRTF)", "bool", true, "3D positional sound for headphones. " + game, g, {"wired": false}))
	L.append(_e("audio/announcer", "Announcer", "bool", true, "\"Excellent!\", \"Impressive!\", lead changes. " + game, g, {"wired": false}))

	# ---- Controls
	g = "Movement"
	for b in BINDS:
		if b[0] == "attack":
			g = "Weapons"
		elif b[0] == "use_item":
			g = "Actions"
		L.append(_e("controls/" + b[0], b[1], "bind", b[2], "Binds for %s: two for keyboard/mouse, one for the controller. Click and press a key, mouse button, wheel or pad button." % b[1].to_lower(), g))
	g = "Options"
	L.append(_e("controls/toggle_crouch", "Toggle crouch", "bool", false, "Tap to crouch instead of holding. " + game, g, {"wired": false}))
	L.append(_e("controls/toggle_zoom", "Toggle zoom", "bool", false, "Tap to zoom instead of holding. " + game, g, {"wired": false}))
	L.append(_e("controls/pad_deadzone", "Stick deadzone", "float", 0.15, "Controller stick deadzone, menus included.", g, {"min": 0.0, "max": 0.5, "step": 0.01}))
	L.append(_e("controls/pad_look", "Stick look speed", "float", 3.0, "Controller look speed in turns per second at full tilt. " + game, g, {"min": 0.5, "max": 8.0, "step": 0.1, "wired": false}))
	L.append(_e("controls/steam_input", "Steam Input layout", "enum", "default", "Community controller layouts through Steam Input. Comes with Steam.", g,
		{"options": [["default", "Default"], ["community", "Community layout"]], "wired": false}))

	# ---- Mouse
	g = "Sensitivity"
	L.append(_e("mouse/sensitivity", "Sensitivity", "float", 5.0, "Quake 3 units: degrees per count = sensitivity x 0.022 (m_yaw). The cm/360 below is the distance for a full turn. " + game, g,
		{"min": 0.1, "max": 30.0, "step": 0.05, "wired": false}))
	L.append(_e("mouse/dpi", "Mouse DPI", "int", 800, "Your mouse's DPI (counts per inch). Only used to show cm/360.", g, {"min": 100, "max": 32000, "step": 50}))
	L.append(_e("mouse/zoom_sensitivity", "Zoom sensitivity", "float", 1.0, "Multiplier while zoomed. " + game, g, {"min": 0.1, "max": 2.0, "step": 0.05, "unit": "x", "wired": false}))
	g = "Input"
	L.append(_e("mouse/raw_input", "Raw input", "bool", true, "Read the mouse directly, skipping Windows pointer speed and acceleration. " + game, g, {"wired": false}))
	L.append(_e("mouse/acceleration", "Acceleration", "float", 0.0, "Quake 3's cl_mouseAccel. 0 = off. " + game, g, {"min": 0.0, "max": 1.0, "step": 0.01, "wired": false}))
	L.append(_e("mouse/invert", "Invert vertical", "bool", false, "Mouse up looks down. " + game, g, {"wired": false}))

	# ---- Crosshair
	g = "Shape"
	L.append(_e("crosshair/shape", "Crosshair", "enum", "c", "Quake 3's ten crosshairs or a vector shape.", g, {"options": CROSSHAIRS}))
	L.append(_e("crosshair/size", "Size", "int", 24, "Size in pixels at 1080p (cg_crosshairSize).", g, {"min": 8, "max": 96, "step": 1, "unit": "px"}))
	L.append(_e("crosshair/thickness", "Thickness", "int", 2, "Line thickness of vector shapes.", g, {"min": 1, "max": 8, "step": 1, "unit": "px"}))
	L.append(_e("crosshair/gap", "Gap", "int", 4, "Space in the middle of vector shapes.", g, {"min": 0, "max": 32, "step": 1, "unit": "px"}))
	L.append(_e("crosshair/dot", "Center dot", "bool", false, "Adds a dot in the middle.", g, {}))
	g = "Color"
	L.append(_e("crosshair/color", "Color", "color", Color(1, 1, 1), "Crosshair color.", g, {}))
	L.append(_e("crosshair/outline", "Outline", "bool", true, "Dark outline so it reads on bright walls.", g, {}))
	L.append(_e("crosshair/hit_color", "Hit color", "color", Color(1, 0.2, 0.15), "Flashes this color when you hit an enemy.", g, {}))
	L.append(_e("crosshair/health_color", "Color by health", "bool", false, "Quake 3's cg_crosshairHealth: tint by your health.", g, {}))
	g = "Per weapon"
	L.append(_e("crosshair/per_weapon", "Per-weapon crosshairs", "bool", false, "Use a different crosshair for each weapon.", g, {}))
	for w in WEAPONS:
		L.append(_e("crosshair/weapon_" + w[0], w[1], "enum", "default", "Crosshair for the %s when per-weapon crosshairs are on." % w[1].to_lower(), g,
			{"options": [["default", "Same as above"]] + CROSSHAIRS}))

	# ---- HUD
	g = "Layout"
	L.append(_e("hud/preset", "HUD preset", "enum", "modern", "Layout of the HUD: Modern (glass panels and bars), Competitive (smaller, compact), Minimal (numbers only), or Quake 3 classic (ammo, health and armor in one row).", g,
		{"options": [["classic", "Quake 3 classic"], ["modern", "Modern (Quake Live style)"], ["minimal", "Minimal"], ["competitive", "Competitive"]]}))
	L.append(_e("hud/scale", "HUD scale", "float", 1.0, "Size of the HUD.", g, {"min": 0.5, "max": 2.0, "step": 0.05, "unit": "x"}))
	L.append(_e("hud/opacity", "HUD opacity", "float", 1.0, "Transparency of the HUD.", g, {"min": 0.2, "max": 1.0, "step": 0.05, "unit": "x"}))
	g = "Elements"
	L.append(_e("hud/speedometer", "Speedometer", "enum", "arc", "Your speed in units per second, with the strafe-jump readout (speed at each takeoff and the gain on the last).", g,
		{"options": [["arc", "Arc gauge"], ["compact", "Compact"], ["off", "Off"]]}))
	L.append(_e("hud/item_timers", "Item timers", "bool", false, "Respawn timers for major items, where the mode allows them.", g, {"wired": false}))
	L.append(_e("hud/damage_numbers", "Damage numbers", "bool", true, "Damage dealt floats over the target.", g, {}))
	L.append(_e("hud/kill_feed", "Kill feed", "bool", true, "Who killed whom, top right.", g, {}))
	L.append(_e("hud/ammo_bar", "Ammo bar", "bool", true, "Ammo for every weapon along the bottom.", g, {}))
	L.append(_e("hud/net_graph", "Net graph", "bool", false, "Latency, loss and jitter overlay.", g, {"wired": false}))
	L.append(_e("hud/fps", "Show FPS", "bool", false, "Frame rate counter.", g, {}))
	g = "Chat"
	L.append(_e("hud/chat_time", "Chat time", "int", 10, "How long chat lines stay on screen. The chat key shows the recent history.", g,
		{"min": 3, "max": 30, "step": 1, "unit": "s"}))
	L.append(_e("hud/chat_lines", "Chat lines", "int", 6, "Most chat lines shown at once while playing.", g, {"min": 2, "max": 12, "step": 1}))
	L.append(_e("hud/team_chats_only", "Team chat only", "bool", false, "Hide everyone's chat but your team's (cg_teamChatsOnly).", g, {}))

	# ---- Gameplay
	g = "Weapons"
	L.append(_e("gameplay/autoswitch", "Switch on pickup", "enum", "better", "Switch to a weapon you pick up (cg_autoswitch).", g,
		{"options": [["never", "Never"], ["better", "If better"], ["always", "Always"]]}))
	L.append(_e("gameplay/draw_gun", "Gun position", "enum", "right", "Where your gun is drawn (cg_drawGun).", g,
		{"options": [["right", "Right"], ["center", "Center"], ["left", "Left"], ["hidden", "Hidden"]], "wired": false}))
	L.append(_e("gameplay/gun_fov", "Gun field of view", "float", 90.0, "Field of view of the gun model only.", g, {"min": 54.0, "max": 120.0, "step": 1.0, "unit": "deg", "wired": false}))
	L.append(_e("gameplay/view_bob", "View bob", "bool", true, "The view bobs as you run (cg_bobup/cg_bobpitch).", g, {}))
	g = "Players"
	L.append(_e("gameplay/force_model", "Force enemy model", "enum", "off", "Show every enemy as one model (cg_forceModel).", g,
		{"options": [["off", "Off"]], "dynamic": "characters", "wired": false}))
	L.append(_e("gameplay/bright_skins", "Bright skins", "bool", false, "Flat bright enemy colors for visibility.", g, {"wired": false}))
	L.append(_e("gameplay/enemy_color", "Enemy color", "color", Color(0.3, 1.0, 0.2), "Bright skin color for enemies.", g, {"wired": false}))
	L.append(_e("gameplay/team_color", "Team color", "color", Color(0.2, 0.6, 1.0), "Bright skin color for teammates.", g, {"wired": false}))
	g = "Cinematics"
	L.append(_e("gameplay/intro", "Intro movie", "enum", "first", "Quake 3's id logo and intro when the game starts.", g,
		{"options": [["first", "First launch only"], ["always", "Every launch"], ["never", "Never"]]}))
	L.append(_e("gameplay/tier_movies", "Tier movies", "bool", true, "Play Quake 3's tier movie when you finish a campaign tier. " + game, g, {"wired": false}))
	g = "Players"
	L.append(_e("gameplay/spawn_effect", "Spawn effects", "bool", true, "Teleport flash when players spawn.", g, {"wired": false}))

	# ---- Network
	g = "Connection"
	L.append(_e("network/region", "Region", "enum", "auto", "Matchmaking region.", g,
		{"options": [["auto", "Automatic"], ["na_east", "North America East"], ["na_west", "North America West"], ["eu", "Europe"], ["sa", "South America"], ["asia", "Asia"], ["oce", "Oceania"]], "wired": false}))
	L.append(_e("network/max_ping", "Max ping", "int", 120, "Hide servers and matches above this ping.", g, {"min": 20, "max": 400, "step": 10, "unit": "ms", "wired": false}))
	L.append(_e("network/rate", "Connection speed", "enum", "fast", "Bandwidth the server may use (rate).", g,
		{"options": [["slow", "Slow (25 KB/s)"], ["medium", "Medium (50 KB/s)"], ["fast", "Fast (100 KB/s)"], ["lan", "LAN (unlimited)"]], "wired": false}))
	L.append(_e("network/interp", "Interpolation", "int", 16, "Delay before other players are drawn, which smooths their motion. Lower is sharper, higher is smoother.", g,
		{"min": 0, "max": 100, "step": 1, "unit": "ms", "wired": false}))
	g = "Online"
	L.append(_e("network/online", "Online services", "bool", true, "Friends, parties and presence. Off keeps you offline.", g))
	L.append(_e("network/online_server", "Online server", "string", "", "Leave empty for the default. A local test server is ws://127.0.0.1:8787.", g, {"max": 200}))

	# ---- Accessibility
	g = "Display"
	L.append(_e("accessibility/ui_scale", "Menu and text size", "float", 1.0, "Size of the menu, text and HUD.", g, {"min": 0.75, "max": 1.5, "step": 0.05, "unit": "x"}))
	L.append(_e("accessibility/high_contrast", "High-contrast menus", "bool", false, "Solid backgrounds and brighter text in menus.", g))
	L.append(_e("accessibility/reduced_flashing", "Reduced flashing", "bool", false, "Calms pulsing, embers, muzzle flashes and screen flashes.", g))
	L.append(_e("accessibility/colorblind", "Colorblind mode", "enum", "off", "Shifts team and item colors. " + game, g,
		{"options": [["off", "Off"], ["protanopia", "Protanopia"], ["deuteranopia", "Deuteranopia"], ["tritanopia", "Tritanopia"]], "wired": false}))
	g = "Captions"
	L.append(_e("accessibility/subtitles", "Subtitles", "bool", false, "Captions for the announcer and voice callouts. " + game, g, {"wired": false}))
	L.append(_e("accessibility/subtitle_size", "Subtitle size", "enum", "medium", "Caption text size.", g, {"options": lmh, "wired": false}))

	# ---- Profile (shown on the Profile page, not in Settings)
	L.append(_e("profile/name", "Player name", "string", "Player", "Your name in matches.", "Profile", {"max": 32}))
	L.append(_e("profile/model", "Character", "enum", "sarge", "Your character.", "Profile", {"options": [], "dynamic": "characters_only"}))
	L.append(_e("profile/skin", "Skin", "enum", "default", "Team skins aren't converted yet.", "Profile", {"options": [["default", "Default"]]}))
	L.append(_e("profile/team_color", "Accent color", "color", Color(0.85, 0.08, 0.04), "Your color: the menu's rim light and your rail trail.", "Profile"))
	_list = L
	return L


static func _e(key: String, label: String, type: String, default: Variant, tip: String, group: String, extra := {}) -> Dictionary:
	var d := {"key": key, "tab": key.get_slice("/", 0), "label": label, "type": type, "default": default,
		"tip": tip, "group": group, "wired": true, "restart": false}
	d.merge(extra, true)
	return d
