class_name Q3EditorCamera
extends Camera3D
## The map editor's fly camera, as OZMIUM's MapEditorCamera: hold the right mouse button to look,
## W A S D and Q / E to fly while looking (Shift fast, Ctrl slow), the wheel dollies. Godot space,
## meters.

const MOVE_SPEED := 8.0
const FAST := 4.0
const SLOW := 0.25
const LOOK := 0.12          ## degrees per pixel
const DOLLY := 1.5
const MAX_PITCH := 89.0
const HOME_POSITION := Vector3(-8, 6, -12)
const HOME_YAW := -145.0    ## looking back across the origin
const HOME_PITCH := -25.0

var looking := false
var input_enabled := true
var yaw := HOME_YAW
var pitch := HOME_PITCH


func _ready() -> void:
	fov = 80.0
	near = 0.05
	far = 4000.0
	home()


func home() -> void:
	set_pose(HOME_POSITION, HOME_YAW, HOME_PITCH)


func set_pose(pos: Vector3, y: float, p: float) -> void:
	position = pos
	yaw = y
	pitch = clampf(p, -MAX_PITCH, MAX_PITCH)
	rotation_degrees = Vector3(pitch, yaw, 0)


## Backs off along the view direction until a sphere of this center and radius fills the view.
func focus(center: Vector3, radius: float) -> void:
	var r := maxf(radius, 1.0)
	var dist := r / tan(deg_to_rad(fov * 0.5)) * 1.2
	position = center + global_transform.basis.z * dist


func start_looking() -> void:
	if looking or not input_enabled:
		return
	looking = true
	Input.mouse_mode = Input.MOUSE_MODE_CAPTURED


func stop_looking() -> void:
	if not looking:
		return
	looking = false
	Input.mouse_mode = Input.MOUSE_MODE_VISIBLE


func look(relative: Vector2) -> void:
	yaw -= relative.x * LOOK
	pitch = clampf(pitch - relative.y * LOOK, -MAX_PITCH, MAX_PITCH)
	rotation_degrees = Vector3(pitch, yaw, 0)


func dolly(steps: float, fast: bool) -> void:
	position += -global_transform.basis.z * steps * DOLLY * (FAST if fast else 1.0)


func _process(delta: float) -> void:
	if not looking:
		return
	if not Input.is_mouse_button_pressed(MOUSE_BUTTON_RIGHT):
		stop_looking()
		return
	# movement only while looking (TrenchBroom style), so typing never flies the camera
	var move := Vector3.ZERO
	if Input.is_physical_key_pressed(KEY_W):
		move.z -= 1
	if Input.is_physical_key_pressed(KEY_S):
		move.z += 1
	if Input.is_physical_key_pressed(KEY_D):
		move.x += 1
	if Input.is_physical_key_pressed(KEY_A):
		move.x -= 1
	if Input.is_physical_key_pressed(KEY_E):
		move.y += 1
	if Input.is_physical_key_pressed(KEY_Q):
		move.y -= 1
	if move == Vector3.ZERO:
		return
	var speed := MOVE_SPEED
	if Input.is_key_pressed(KEY_SHIFT):
		speed *= FAST
	if Input.is_key_pressed(KEY_CTRL):
		speed *= SLOW
	var b := global_transform.basis
	var world := b.x * move.x + Vector3.UP * move.y + b.z * move.z
	position += world.normalized() * speed * delta
