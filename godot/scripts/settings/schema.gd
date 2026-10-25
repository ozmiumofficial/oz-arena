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
	"graphics/heat_shimmer": [false, true, true, true],
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
	["replay_play", "Play / pause", ["key:K", "key:Pause", ""]],
	["replay_back", "Back 5 seconds", ["key:J", "", ""]],
	["replay_forward", "Forward 5 seconds", ["key:L", "", ""]],
	["replay_slower", "Slower", ["key:Minus", "", ""]],
	["replay_faster", "Faster", ["key:Equal", "", ""]],
	["replay_step_back", "Frame back", ["key:Comma", "", ""]],
	["replay_step_forward", "Frame forward", ["key:Period", "", ""]],
	["replay_prev_frag", "Previous frag", ["key:BracketLeft", "", ""]],
	["replay_next_frag", "Next frag", ["key:BracketRight", "", ""]],
	["replay_camera", "Next camera", ["key:F", "", ""]],
	["replay_prev_player", "Previous player", ["key:Q", "", ""]],
	["replay_next_player", "Next player", ["key:E", "", ""]],
	["replay_players", "Player list", ["key:P", "", ""]],
	["replay_hud", "HUD on / off", ["key:H", "", ""]],
	["replay_names", "Name tags", ["key:N", "", ""]],
	["replay_outlines", "Outlines", ["key:O", "", ""]],
	["replay_bar", "Hide the controls", ["key:B", "", ""]],
	["replay_mark_in", "Clip start", ["key:Home", "", ""]],
	["replay_mark_out", "Clip end", ["key:End", "", ""]],
	["replay_export", "Export the clip", ["key:F9", "", ""]],
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
	L.append(_e("video/fov", "Field of view", "float", 90.0, "Horizontal degrees at 4:3, like Quake 3's cg_fov. Wider screens see more at the sides (Hor+), so it's correct at any aspect.", g,
		{"min": 60.0, "max": 130.0, "step": 1.0, "unit": "deg"}))
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
	L.append(_e("graphics/shadows", "Shadows", "enum", "high", "Shadow map resolution and filtering. Ultra also gives every map light a live shadow on players and the mist: barely visible, and several times slower.", g, {"options": off + [["ultra", "Ultra"]]}))
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
	L.append(_e("graphics/effect_intensity", "Effect intensity", "float", 1.0, "Brightness of explosions, beams, flashes and their lights.", g, {"min": 0.25, "max": 1.0, "step": 0.05, "unit": "x"}))
	L.append(_e("graphics/heat_shimmer", "Heat shimmer", "bool", true, "Air ripples behind rockets and around explosions.", g))
	L.append(_e("graphics/decals", "Marks and decals", "bool", true, "Bullet holes, burns and blood on walls.", g))
	L.append(_e("graphics/gibs", "Gibs", "bool", true, "Bodies burst into gibs on big hits (cg_gibs). Off: they burn away instead.", g))

	# ---- Audio
	g = "Volume"
	for b in [["master", "Master"], ["music", "Music"], ["effects", "Effects"], ["voice", "Voice chat"], ["announcer", "Announcer"], ["ui", "Menu sounds"]]:
		L.append(_e("audio/vol_" + b[0], b[1], "int", 80 if b[0] == "music" else 100, "%s volume." % b[1], g, {"min": 0, "max": 100, "step": 1, "unit": "%"}))
	L.append(_e("audio/menu_music", "Menu music", "bool", true, "Quake 3's soundtrack, shuffled, in the main menu.", g))
	L.append(_e("audio/menu_ambient", "Menu ambience", "bool", false, "A quiet ambient drone in the menu. Quake 3's menu had no music, so it's off by default.", g))
	L.append(_e("audio/mute_unfocused", "Mute when in background", "bool", true, "Silence the game while its window isn't focused.", g))
	g = "Devices"
	L.append(_e("audio/output_device", "Output device", "enum", "Default", "Where sound plays.", g, {"options": [["Default", "Default"]], "dynamic": "output_devices"}))
	L.append(_e("audio/input_device", "Voice chat device", "enum", "Default", "Microphone for voice chat.", g, {"options": [["Default", "Default"]], "dynamic": "input_devices"}))
	g = "Game sounds"
	L.append(_e("audio/hit_sound", "Hit sound", "enum", "classic", "The beep when you hit an enemy. Pitch by damage beeps higher for small hits and lower for big ones.", g,
		{"options": [["off", "Off"], ["classic", "Quake 3 classic"], ["pitch", "Pitch by damage"]]}))
	L.append(_e("audio/spatial", "Spatial audio", "bool", true, "Distant sounds are muffled and placed more sharply left and right. Off is Quake 3's plain stereo panning.", g))
	L.append(_e("audio/announcer", "Announcer", "bool", true, "The voice: \"Excellent!\", \"Impressive!\", lead changes, warnings, the countdown and \"Fight!\".", g))

	# ---- Controls
	g = "Movement"
	for b in BINDS:
		if b[0] == "attack":
			g = "Weapons"
		elif b[0] == "use_item":
			g = "Actions"
		elif b[0] == "replay_play":
			g = "Replays"
		L.append(_e("controls/" + b[0], b[1], "bind", b[2], "Binds for %s: two for keyboard/mouse, one for the controller. Click and press a key, mouse button, wheel or pad button." % b[1].to_lower(), g))
	g = "Options"
	L.append(_e("controls/toggle_crouch", "Toggle crouch", "bool", false, "Tap to crouch instead of holding.", g, {}))
	L.append(_e("controls/toggle_zoom", "Toggle zoom", "bool", false, "Tap to zoom instead of holding.", g, {}))
	L.append(_e("controls/pad_deadzone", "Stick deadzone", "float", 0.15, "Controller stick deadzone, menus included.", g, {"min": 0.0, "max": 0.5, "step": 0.01}))
	L.append(_e("controls/pad_look", "Stick look speed", "float", 3.0, "Controller look speed at full tilt, in turns per second.", g, {"min": 0.5, "max": 8.0, "step": 0.1}))
	L.append(_e("controls/stick_layout", "Stick layout", "enum", "default", "Which controller stick moves and which looks.", g,
		{"options": [["default", "Left moves, right looks"], ["southpaw", "Southpaw: left looks, right moves"]]}))

	# ---- Mouse
	g = "Sensitivity"
	L.append(_e("mouse/sensitivity", "Sensitivity", "float", 5.0, "Quake 3 units: degrees per count = sensitivity x 0.022 (m_yaw). The cm/360 below is the distance for a full turn.", g,
		{"min": 0.1, "max": 30.0, "step": 0.05}))
	L.append(_e("mouse/dpi", "Mouse DPI", "int", 800, "Your mouse's DPI (counts per inch). Only used to show cm/360.", g, {"min": 100, "max": 32000, "step": 50}))
	L.append(_e("mouse/zoom_sensitivity", "Zoom sensitivity", "float", 1.0, "Multiplier while zoomed.", g, {"min": 0.1, "max": 2.0, "step": 0.05, "unit": "x"}))
	g = "Input"
	L.append(_e("mouse/acceleration", "Acceleration", "float", 0.0, "Quake 3's cl_mouseAccel: faster swipes turn further. 0 = off. Mouse look always reads the mouse raw, without Windows' pointer speed.", g, {"min": 0.0, "max": 1.0, "step": 0.01}))
	L.append(_e("mouse/invert", "Invert vertical", "bool", false, "Mouse up looks down.", g, {}))

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
	L.append(_e("hud/show_speedometer", "Speedometer", "bool", true, "Your speed in units per second, with the strafe-jump readout (speed at each takeoff and the gain on the last).", g, {}))
	L.append(_e("hud/speedometer", "Speedometer style", "enum", "arc", "Arc gauge or a compact bar. The Competitive and Quake 3 classic presets always use the compact bar.", g,
		{"options": [["arc", "Arc gauge"], ["compact", "Compact"]]}))
	L.append(_e("hud/item_timers", "Item timers", "bool", false, "Countdowns for the armors, Mega Health and powerups once they're taken, left of the screen.", g, {}))
	L.append(_e("hud/damage_numbers", "Damage numbers", "bool", true, "Damage dealt floats over the target.", g, {}))
	L.append(_e("hud/kill_feed", "Kill feed", "bool", true, "Who killed whom, top right.", g, {}))
	L.append(_e("hud/ammo_bar", "Ammo bar", "bool", true, "Ammo for every weapon along the bottom.", g, {}))
	L.append(_e("hud/net_graph", "Net graph", "bool", false, "Quake 3's lagometer, bottom right: frame timing above, snapshot ping below, with ping and frame time.", g, {}))
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
		{"options": [["right", "Right"], ["center", "Center"], ["left", "Left"], ["hidden", "Hidden"]]}))
	L.append(_e("gameplay/gun_fov", "Gun field of view", "float", 0.0, "Field of view the gun is drawn at. 0 draws it at your Field of view, as Quake 3 does.", g, {"min": 0.0, "max": 120.0, "step": 1.0, "unit": "deg"}))
	L.append(_e("gameplay/gun_widescreen", "Widescreen gun placement", "bool", true, "Keep the gun as far toward the edge as Quake 3 drew it on a 4:3 screen. Off draws it where a wide screen puts it, nearer the middle.", g, {}))
	L.append(_e("gameplay/gun_offset", "Gun side offset", "float", 0.0, "Moves the gun sideways (cg_gunY): toward the edge its side is on, or back toward the middle.", g, {"min": -10.0, "max": 10.0, "step": 0.5, "unit": "units"}))
	L.append(_e("gameplay/view_bob", "View bob", "bool", true, "The view bobs as you run (cg_bobup/cg_bobpitch).", g, {}))
	g = "Players"
	L.append(_e("gameplay/force_model", "Force enemy model", "enum", "off", "Show every enemy as one model (cg_forceModel).", g,
		{"options": [["off", "Off"]], "dynamic": "characters"}))
	L.append(_e("gameplay/bright_skins", "Bright skins", "bool", false, "Players drawn in one flat bright color: enemies in Enemy color, teammates in Team color.", g))
	L.append(_e("gameplay/enemy_color", "Enemy color", "color", Color(0.3, 1.0, 0.2), "Bright skin color for enemies.", g))
	L.append(_e("gameplay/team_color", "Team color", "color", Color(0.2, 0.6, 1.0), "Bright skin color for teammates.", g))
	L.append(_e("gameplay/spawn_effect", "Spawn effects", "bool", true, "Teleport flash when players spawn and teleport.", g))
	L.append(_e("gameplay/pain_twitch", "Hit twitch", "bool", false, "Players' upper bodies jerk to the side when they're hit, as in Quake 3 (CG_AddPainTwitch). Off keeps them steady.", g))
	g = "Cinematics"
	L.append(_e("gameplay/intro", "Intro movie", "enum", "first", "Quake 3's id logo and intro when the game starts.", g,
		{"options": [["first", "First launch only"], ["always", "Every launch"], ["never", "Never"]]}))
	L.append(_e("gameplay/tier_movies", "Tier movies", "bool", true, "Play Quake 3's tier movie when you finish a campaign tier.", g))
	g = "Replays"
	L.append(_e("gameplay/record_replays", "Record replays", "bool", true, "Record every match you play: the whole match, every player, watchable from any view (Play > Replays).", g))
	L.append(_e("gameplay/replays_kept", "Replays kept", "int", 50, "How many recent replays stay. Older ones are deleted, except favorites, which stay forever.", g, {"min": 5, "max": 500, "step": 5}))
	g = "Death cam"
	L.append(_e("gameplay/death_cam", "Death cam", "bool", true, "While you're dead the camera follows your body, then shows who killed you with their player card. Respawning works as in Quake 3.", g))
	g = "Spectating"
	L.append(_e("gameplay/spec_view", "Spectator camera", "enum", "first", "How you see the player you follow while spectating: through their eyes, the chase camera behind them, or the cinematic camera, which picks its own shots (slow orbits, tracking shots, wide shots of the fight) and cuts to the action, with the auto-director choosing who to watch. Use item switches between them.", g,
		{"options": [["first", "First person"], ["chase", "Chase camera"], ["cinematic", "Cinematic"]]}))
	L.append(_e("gameplay/spec_director", "Auto-director", "bool", false, "While you spectate, the camera follows the action by itself: a flag carrier, whoever just fragged, the closest fight. Gesture turns it on and off.", g))

	# ---- Network
	g = "Connection"
	L.append(_e("network/region", "Region", "enum", "auto", "Matchmaking region, and the Server Browser's first choice. Automatic finds matches in your closest region first and widens if the wait runs long; a named region stays strict.", g,
		{"options": [["auto", "Automatic"], ["na_east", "North America East"], ["na_west", "North America West"], ["eu", "Europe"], ["sa", "South America"], ["asia", "Asia"], ["oce", "Oceania"], ["meaf", "Middle East and Africa"]]}))
	L.append(_e("network/max_ping", "Max ping", "int", 120, "Hide servers and matches above this ping (the Server Browser's filter).", g, {"min": 20, "max": 400, "step": 10, "unit": "ms"}))
	L.append(_e("network/interp", "Interpolation", "int", 16, "Delay before other players are drawn in an online match, which smooths their motion. Lower is sharper, higher is smoother.", g,
		{"min": 0, "max": 100, "step": 1, "unit": "ms"}))
	L.append(_e("network/rate", "Connection speed", "enum", "fast", "Bandwidth a server may send you (your rate userinfo).", g,
		{"options": [["slow", "Slow (25 KB/s)"], ["medium", "Medium (50 KB/s)"], ["fast", "Fast (100 KB/s)"], ["lan", "LAN (unlimited)"]]}))
	g = "Online"
	L.append(_e("network/online", "Online services", "bool", true, "Friends, parties and presence. Off keeps you offline.", g))
	L.append(_e("network/online_server", "Online server", "string", "", "Leave empty for the default. A local test server is ws://127.0.0.1:8787.", g, {"max": 200}))
	L.append(_e("network/direct_downloads", "Allow direct map downloads", "bool", false, "When a match is on a map one of you doesn't have, send or get it over a direct connection between the two PCs instead of through the relay. While it runs, the other player (or server) sees your IP address; the match itself always stays on the relay. Both sides need this on (dedicated servers always allow it).", g))

	# ---- Accessibility
	g = "Display"
	L.append(_e("accessibility/ui_scale", "Menu and text size", "float", 1.0, "Size of the menu, text and HUD.", g, {"min": 0.75, "max": 1.5, "step": 0.05, "unit": "x"}))
	L.append(_e("accessibility/high_contrast", "High-contrast menus", "bool", false, "Solid backgrounds and brighter text in menus.", g))
	L.append(_e("accessibility/reduced_flashing", "Reduced flashing", "bool", false, "Calms pulsing, embers, muzzle flashes and screen flashes.", g))
	L.append(_e("accessibility/colorblind", "Colorblind mode", "enum", "off", "Shifts the game's colors so the ones your eyes confuse stay apart (a daltonize filter over the 3D and the HUD).", g,
		{"options": [["off", "Off"], ["protanopia", "Protanopia"], ["deuteranopia", "Deuteranopia"], ["tritanopia", "Tritanopia"]]}))
	g = "Captions"
	L.append(_e("accessibility/subtitles", "Subtitles", "bool", false, "Captions for the announcer, bottom middle.", g))
	L.append(_e("accessibility/subtitle_size", "Subtitle size", "enum", "medium", "Caption text size.", g, {"options": lmh}))

	# ---- Profile (shown on the Profile page, not in Settings)
	L.append(_e("profile/name", "Player name", "string", "Player", "Your name in matches.", "Profile", {"max": 32}))
	L.append(_e("profile/model", "Character", "enum", "sarge", "Your character.", "Profile", {"options": [], "dynamic": "characters_only"}))
	L.append(_e("profile/skin", "Skin", "enum", "default", "Your character's skin. Team games paint you red or blue.", "Profile", {"options": [["default", "Default"]], "dynamic": "skins"}))
	L.append(_e("profile/team_color", "Accent color", "color", Color(0.85, 0.08, 0.04), "Your color: the menu's rim light and your rail trail.", "Profile"))
	_list = L
	return L


static func _e(key: String, label: String, type: String, default: Variant, tip: String, group: String, extra := {}) -> Dictionary:
	var d := {"key": key, "tab": key.get_slice("/", 0), "label": label, "type": type, "default": default,
		"tip": tip, "group": group, "wired": true, "restart": false}
	d.merge(extra, true)
	return d
