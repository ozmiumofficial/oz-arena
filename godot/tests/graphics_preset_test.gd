extends Node
## Headless check of the Graphics presets: Classic turns off every screen-space effect (SSR, AO,
## bloom, volumetric fog, heat shimmer, SDFGI), each preset sets its whole table, and changing one
## setting afterward switches the preset to Custom. Every setting it touches is put back.
##   godot --headless --path . res://tests/graphics_preset_test.tscn

var _ok := true


func _check(label: String, cond: bool) -> void:
	print(("ok   " if cond else "FAIL ") + label)
	if not cond:
		_ok = false


func _ready() -> void:
	var saved := {"graphics/preset": Settings.get_value("graphics/preset")}
	for k in Q3SettingsSchema.PRESETS:
		saved[k] = Settings.get_value(k)
	_check("Classic is a preset option", Q3SettingsSchema.PRESET_NAMES.has("classic"))
	for k in Q3SettingsSchema.PRESETS:
		_check("%s has a value for every preset" % k, Q3SettingsSchema.PRESETS[k].size() == Q3SettingsSchema.PRESET_NAMES.size())
	for name in Q3SettingsSchema.PRESET_NAMES:
		Settings.set_value("graphics/preset", name, false)
		var i: int = Q3SettingsSchema.PRESET_NAMES.find(name)
		var all := true
		for k in Q3SettingsSchema.PRESETS:
			all = all and Settings._same(Settings.get_value(k), Q3SettingsSchema.PRESETS[k][i])
		_check("%s applies its whole table" % name, all and Settings.get_value("graphics/preset") == name)
	Settings.set_value("graphics/preset", "classic", false)
	_check("Classic: SSR off", Settings.get_value("graphics/reflections") == false)
	_check("Classic: AO off", Settings.get_value("graphics/ao") == "off")
	_check("Classic: bloom off", Settings.get_value("graphics/bloom") == false)
	_check("Classic: volumetric fog off", Settings.get_value("graphics/volumetric_fog") == false)
	_check("Classic: heat shimmer off", Settings.get_value("graphics/heat_shimmer") == false)
	_check("Classic: SDFGI off", Settings.get_value("graphics/gi") == "off")
	Settings.set_value("graphics/bloom", true, false)
	_check("Changing one setting makes it Custom", Settings.get_value("graphics/preset") == "custom")
	for k in saved:
		if k != "graphics/preset":
			Settings.set_value(k, saved[k], false)
	Settings.set_value("graphics/preset", saved["graphics/preset"], false)
	if saved["graphics/preset"] == "custom":
		for k in Q3SettingsSchema.PRESETS:
			Settings.set_value(k, saved[k], false)
	Settings.save()
	print("graphics preset test: ", "PASS" if _ok else "FAIL")
	get_tree().quit(0 if _ok else 1)
