class_name Q3EditorGrid
extends MeshInstance3D
## The editor's ground grid and snapping, as OZMIUM's MapEditorGrid: nine sizes from 1 to 256 units
## (16 by default), lines every step and a brighter line every 8, on a quad that follows the camera.

const SIZES := [1, 2, 4, 8, 16, 32, 64, 128, 256]
const QUAD := 512.0          ## meters each side of the camera

var index := 4
var snapping := true
var follow: Node3D


func _ready() -> void:
	var m := PlaneMesh.new()
	m.size = Vector2(QUAD * 2, QUAD * 2)
	mesh = m
	cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	var mat := ShaderMaterial.new()
	mat.shader = preload("res://scenes/map_editor/editor_grid.gdshader")
	material_override = mat
	_apply()


func units() -> int:
	return SIZES[index]


func label() -> String:
	return "%du (%s m)" % [units(), String.num(units() / 32.0, 4)]


func set_index(i: int) -> void:
	index = clampi(i, 0, SIZES.size() - 1)
	_apply()


func step(delta: int) -> void:
	set_index(index + delta)


## Rounds an id coordinate to the grid (always, with force; otherwise only while snapping is on).
func snap(v: float, force := false) -> float:
	if not snapping and not force:
		return v
	return roundf(v / units()) * units()


func snap_vec(v: Vector3, force := false) -> Vector3:
	return Vector3(snap(v.x, force), snap(v.y, force), snap(v.z, force))


func _apply() -> void:
	if material_override:
		(material_override as ShaderMaterial).set_shader_parameter("cell", units() / 32.0)


func _process(_delta: float) -> void:
	if follow:
		var major := units() / 32.0 * 8.0
		var p := follow.global_position
		# a hair under z = 0, so a floor's top face there wins the depth test
		global_position = Vector3(roundf(p.x / major) * major, -0.002, roundf(p.z / major) * major)
