class_name Q3Feed
extends RefCounted
## What Gameplay draws a match from (REPLAY_PLAN.md "A feed, not a fork"). Q3LiveFeed passes
## straight through to the game running in this process (Q3Server); Q3ReplayFeed plays a recorded
## match (Q3Replay). Gameplay, Q3Client and the HUD read only this, so the HUD, scoreboard, chat,
## music, announcer and effects work the same live and in playback, and online play will add a
## feed of its own. Values are id units and axes, as Q3Server gives them.


func is_replay() -> bool:
	return false


## Server time of what's shown (ms).
func time() -> int:
	return 0


func running() -> bool:
	return false


func configstring(_index: int) -> String:
	return ""


## A server cvar. Playback only knows the serverinfo ones (g_gametype, timelimit, fraglimit ...).
func cvar(_name: String) -> String:
	return ""


func player_state(_client: int) -> Dictionary:
	return {}


## 4 (active) for a client in the match, as Q3Server.client_state().
func client_state(_client: int) -> int:
	return 0


func snapshot(_client: int) -> Dictionary:
	return {}


## Reliable commands (print, cp, chat, ...) that reached `client` since the last call.
func next_commands(_client: int) -> PackedStringArray:
	return PackedStringArray()


func trace(_start: Vector3, _end: Vector3, _mins: Vector3, _maxs: Vector3, _pass_entity: int, _content_mask: int) -> Dictionary:
	return {}


func point_contents(_point: Vector3, _pass_entity: int) -> int:
	return 0


## The map's items with their respawn state (item timers); playback has none.
func items() -> Array:
	return []
