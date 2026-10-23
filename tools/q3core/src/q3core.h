#pragma once
// Q3Core: the Godot face of id's game code. G0 exposes the build info and the item table;
// G2 adds Q3Server (the match) and Q3Pmove (prediction).

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>

namespace godot {

class Q3Core : public RefCounted {
	GDCLASS(Q3Core, RefCounted)

protected:
	static void _bind_methods();

public:
	static String source_version();
	static PackedStringArray item_classnames();
	static int item_quantity(const String &classname);
	static bool error_recovers();
};

} // namespace godot
