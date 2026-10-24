#pragma once
// id's entityState_t / playerState_t as the Dictionaries Godot reads (q3server.cpp), shared by
// Q3Server (the live match) and Q3Replay (playback), so both hand Gameplay the same shapes.

#include <godot_cpp/variant/dictionary.hpp>

#include "q3_types.h"

godot::Dictionary q3_entity_dict(const q3_entity_state &e);
godot::Dictionary q3_player_dict(const q3_player_state &p);
