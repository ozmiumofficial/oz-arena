class_name Q3StubScene
extends Control
## Placeholder for a top-level scene that isn't built yet (Gameplay, Map editor): its title,
## what it received, what's coming, and Back to the main menu (Escape, B, or the button).

@export var title := "GAMEPLAY"
@export_multiline var coming := ""

var _summary: Label


func _ready() -> void:
	set_anchors_preset(Control.PRESET_FULL_RECT)
	theme = Q3Theme.build(Settings.get_value("accessibility/high_contrast"))
	var bg := ColorRect.new()
	bg.color = Color(0.015, 0.012, 0.012)
	bg.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(bg)
	var panel := PanelContainer.new()
	panel.set_anchors_preset(Control.PRESET_CENTER)
	panel.custom_minimum_size = Vector2(900, 0)
	panel.position = Vector2(-450, -260)
	add_child(panel)
	var box := VBoxContainer.new()
	box.add_theme_constant_override("separation", 16)
	panel.add_child(box)
	var head := Label.new()
	head.text = title
	head.theme_type_variation = "HeaderLabel"
	head.add_theme_font_override("font", Q3Theme.banner_font(6))
	box.add_child(head)
	var soon := Label.new()
	soon.text = "COMING SOON"
	soon.theme_type_variation = "TitleLabel"
	box.add_child(soon)
	_summary = Label.new()
	_summary.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_summary.text = summary()
	box.add_child(_summary)
	var more := Label.new()
	more.theme_type_variation = "DimLabel"
	more.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	more.text = coming
	box.add_child(more)
	var b := Button.new()
	b.text = "BACK TO MENU"
	b.size_flags_horizontal = Control.SIZE_SHRINK_BEGIN
	b.pressed.connect(_back)
	box.add_child(b)
	b.grab_focus.call_deferred()
	UiSound.play_in()


## What this scene was handed (the match setup for Gameplay).
func summary() -> String:
	if Session.setup:
		return "Match setup received:\n" + Session.setup.summary()
	return ""


func _unhandled_input(ev: InputEvent) -> void:
	if ev.is_action_pressed("ui_cancel"):
		_back()


func _back() -> void:
	UiSound.out()
	Session.to_main_menu()
