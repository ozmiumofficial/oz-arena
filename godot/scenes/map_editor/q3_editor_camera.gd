class_name Q3EditorCamera
extends Camera3D
## The map editor's fly camera, as OZMIUM's MapEditorCamera: hold the right mouse button to look,
## W A S D and Q / E to fly while looking (Shift fast, Ctrl slow), the wheel dollies. Godot space,
## meters. In the 2D views (set_ortho: Radiant's top, front and side) it's orthographic, looking along
## one of id's axes: the right button drags the view, the wheel zooms about the cursor.

const MOVE_SPEED := 8.0
const FAST := 4.0
const SLOW := 0.25
const LOOK := 0.12          ## degrees per pixel
const DOLLY := 1.5
const MAX_PITCH := 89.0
const HOME_POSITION := Vector3(-8, 6, -12)
const HOME_YAW := -145.0    ## looking back across the origin
const HOME_PITCH := -25.0
const ORTHO_BACK := 1500.0  ## meters from the view's center back to the camera
const ORTHO_LAYER := 1 << 3 ## what only the 3D view draws (the ground grid)
## the 2D views' axes in Godot space: [right, up, back] (top: id X right, Y up; front: X right, Z up;
## side: Y right, Z up)
const ORTHO_BASES := {
	"top": [Vector3(0, 0, 1), Vector3(1, 0, 0), Vector3(0, 1, 0)],
	"front": [Vector3(0, 0, 1), Vector3(0, 1, 0), Vector3(-1, 0, 0)],
	"side": [Vector3(1, 0, 0), Vector3(0, 1, 0), Vector3(0, 0, 1)],
}

var looking := false
var input_enabled := true
var yaw := HOME_YAW
var pitch := HOME_PITCH
var ortho := ""             ## "" (3D), "top", "front" or "side"
var _pose_3d := []          ## [position, yaw, pitch]: where the 3D view was
var _captured := false


func _ready() -> void:
	fov = 80.0
	near = 0.05
	far = 4000.0
	home()


func home() -> void:
	if ortho != "":
		size = 40.0
		_center_ortho(Vector3.ZERO)
		return
	set_pose(HOME_POSITION, HOME_YAW, HOME_PITCH)


## Into a 2D view ("top", "front", "side") centered on `center` (Godot space), or back to 3D ("").
func set_ortho(mode: String, center: Vector3) -> void:
	if mode == ortho:
		return
	if ortho == "":
		_pose_3d = [position, yaw, pitch]
	ortho = mode
	if mode == "":
		projection = PROJECTION_PERSPECTIVE
		cull_mask |= ORTHO_LAYER
		if _pose_3d.size():
			set_pose(_pose_3d[0], _pose_3d[1], _pose_3d[2])
		return
	projection = PROJECTION_ORTHOGONAL
	cull_mask &= ~ORTHO_LAYER
	if size <= 1.0:
		size = 40.0
	var b: Array = ORTHO_BASES[mode]
	global_transform = Transform3D(Basis(b[0], b[1], b[2]), Vector3.ZERO)
	_center_ortho(center)


func _center_ortho(center: Vector3) -> void:
	position = center + global_transform.basis.z * ORTHO_BACK


func set_pose(pos: Vector3, y: float, p: float) -> void:
	position = pos
	yaw = y
	pitch = clampf(p, -MAX_PITCH, MAX_PITCH)
	rotation_degrees = Vector3(pitch, yaw, 0)


## Backs off along the view direction until a sphere of this center and radius fills the view.
func focus(center: Vector3, radius: float) -> void:
	var r := maxf(radius, 1.0)
	if ortho != "":
		size = r * 2.4
		_center_ortho(center)
		return
	var dist := r / tan(deg_to_rad(fov * 0.5)) * 1.2
	position = center + global_transform.basis.z * dist


func start_looking() -> void:
	if looking or not input_enabled:
		return
	looking = true
	# a 2D view drags under the cursor; the 3D view turns, so the cursor goes
	_captured = ortho == ""
	if _captured:
		Input.mouse_mode = Input.MOUSE_MODE_CAPTURED


func stop_looking() -> void:
	if not looking:
		return
	looking = false
	if _captured:
		Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
	_captured = false


func look(relative: Vector2) -> void:
	if ortho != "":
		var k := size / maxf(get_viewport().get_visible_rect().size.y, 1.0)
		var b := global_transform.basis
		position += (-b.x * relative.x + b.y * relative.y) * k
		return
	yaw -= relative.x * LOOK
	pitch = clampf(pitch - relative.y * LOOK, -MAX_PITCH, MAX_PITCH)
	rotation_degrees = Vector3(pitch, yaw, 0)


## A 2D view's zoom, keeping the point under `screen` where it is.
func zoom_at(screen: Vector2, steps: float) -> void:
	var before := project_position(screen, 1.0)
	size = clampf(size * pow(0.85, steps), 0.5, 3000.0)
	position += before - project_position(screen, 1.0)


func dolly(steps: float, fast: bool) -> void:
	position += -global_transform.basis.z * steps * DOLLY * (FAST if fast else 1.0)


func _process(delta: float) -> void:
	if not looking:
		return
	if not Input.is_mouse_button_pressed(MOUSE_BUTTON_RIGHT):
		stop_looking()
		return
	if ortho != "":
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
