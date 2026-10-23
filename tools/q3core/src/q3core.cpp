#include "q3core.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "q3_api.h"

using namespace godot;

void Q3Core::_bind_methods() {
	ClassDB::bind_static_method("Q3Core", D_METHOD("source_version"), &Q3Core::source_version);
	ClassDB::bind_static_method("Q3Core", D_METHOD("item_classnames"), &Q3Core::item_classnames);
	ClassDB::bind_static_method("Q3Core", D_METHOD("item_quantity", "classname"), &Q3Core::item_quantity);
	ClassDB::bind_static_method("Q3Core", D_METHOD("error_recovers"), &Q3Core::error_recovers);
}

String Q3Core::source_version() {
	return String(q3_source_version());
}

// Every real item in bg_itemlist, in table order (index 0 is the empty slot).
PackedStringArray Q3Core::item_classnames() {
	PackedStringArray out;
	for (int i = 1; i < q3_item_count(); i++) {
		out.push_back(String(q3_item_classname(i)));
	}
	return out;
}

int Q3Core::item_quantity(const String &classname) {
	for (int i = 1; i < q3_item_count(); i++) {
		if (classname == String(q3_item_classname(i))) {
			return q3_item_quantity(i);
		}
	}
	return -1;
}

// True when a Com_Error inside id's code returns control to Godot instead of aborting.
bool Q3Core::error_recovers() {
	return q3_test_error() == 1;
}
