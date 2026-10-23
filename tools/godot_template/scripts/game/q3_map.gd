class_name Q3Map
extends Node3D
## A Quake 3 map built by Q3MapBuilder (library/maps/<map>/<map>.tscn): geometry with the
## remastered world materials, sky, baked lights, collision for ragdolls and gibs, fog volumes
## and entity markers. The game itself runs the same map from the .bsp in q3core; this is what
## the player sees, and Gameplay moves its model_<n> movers from the game's entity states.

@export var map := ""
var portals: Array[Q3Portal] = []   ## its mirrors and portal views
var _info: Dictionary = {}


func _ready() -> void:
	portals = Q3Portal.build_all(self)


## The map's description from tools/build_maps.py (entities, lights, models, report).
func info() -> Dictionary:
	if _info.is_empty() and map != "":
		var p: Variant = JSON.parse_string(FileAccess.get_file_as_string("res://library/maps/%s/%s.json" % [map, map]))
		if p is Dictionary:
			_info = p
	return _info


## The mesh of inline model n (a door, plat, button ...), or null when it draws nothing.
func model_node(n: int) -> Node3D:
	return get_node_or_null("Geometry/model_%d" % n) as Node3D


## Entity markers of one classname (metadata "entity" holds the key/values).
func entities_of(classname: String) -> Array[Marker3D]:
	var out: Array[Marker3D] = []
	for m in $Entities.get_children():
		if String(m.get_meta("entity", {}).get("classname", "")) == classname:
			out.append(m)
	return out


## Where Quake 3 puts the camera between matches (info_player_intermission).
func intermission_transform() -> Transform3D:
	var points := entities_of("info_player_intermission")
	return view_transform(points[0]) if points.size() else Transform3D.IDENTITY


## A camera at an entity marker, facing the way the entity does in Quake 3: toward its "target",
## else along "angles" (pitch yaw roll, pitch down positive) or "angle" (yaw). Spawn points get
## the player's eye height (DEFAULT_VIEWHEIGHT 26 above the origin, which is 24 above the feet).
func view_transform(marker: Marker3D) -> Transform3D:
	var e: Dictionary = marker.get_meta("entity", {})
	var pos := marker.global_position
	if String(e.get("classname", "")).begins_with("info_player") or String(e.get("classname", "")).begins_with("team_CTF"):
		pos.y += 26.0 / 32.0
	var dir := Vector3.ZERO
	if e.has("target"):
		for m in $Entities.get_children():
			if String(m.get_meta("entity", {}).get("targetname", "")) == String(e["target"]):
				dir = m.global_position - pos
				break
	if dir.length() < 0.001:
		var pitch := 0.0
		var yaw := float(e.get("angle", "0"))
		if e.has("angles"):
			var a := String(e["angles"]).split_floats(" ", false)
			if a.size() >= 2:
				pitch = a[0]
				yaw = a[1]
		var p := deg_to_rad(pitch)
		var y := deg_to_rad(yaw)
		var f := Vector3(cos(y) * cos(p), sin(y) * cos(p), -sin(p))   # id axes
		dir = Vector3(f.y, f.z, f.x)
	var up := Vector3.UP if absf(dir.normalized().y) < 0.99 else Vector3.FORWARD
	return Transform3D(Basis.looking_at(dir, up), pos)
