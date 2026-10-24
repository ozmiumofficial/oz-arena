class_name Q3LiveFeed
extends Q3Feed
## The match running in this process: every call goes straight to Q3Server.

static var _shared: Q3LiveFeed


## The one live feed (Q3Client and the HUD start on it).
static func shared() -> Q3LiveFeed:
	if _shared == null:
		_shared = Q3LiveFeed.new()
	return _shared


func time() -> int:
	return Q3Server.time()


func running() -> bool:
	return Q3Server.running()


func configstring(index: int) -> String:
	return Q3Server.configstring(index)


func cvar(name: String) -> String:
	return Q3Server.cvar(name)


func player_state(client: int) -> Dictionary:
	return Q3Server.player_state(client)


func client_state(client: int) -> int:
	return Q3Server.client_state(client)


func snapshot(client: int) -> Dictionary:
	return Q3Server.snapshot(client)


func next_commands(client: int) -> PackedStringArray:
	return Q3Server.next_commands(client)


func trace(start: Vector3, end: Vector3, mins: Vector3, maxs: Vector3, pass_entity: int, content_mask: int) -> Dictionary:
	return Q3Server.trace(start, end, mins, maxs, pass_entity, content_mask)


func point_contents(point: Vector3, pass_entity: int) -> int:
	return Q3Server.point_contents(point, pass_entity)


func items() -> Array:
	return Q3Server.items() if ClassDB.class_has_method("Q3Server", "items") else []
