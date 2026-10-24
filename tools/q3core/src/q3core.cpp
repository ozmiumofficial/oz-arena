#include "q3core.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "crash/q3_crash.h"
#include "q3_api.h"

using namespace godot;

void Q3Core::_bind_methods() {
	ClassDB::bind_static_method("Q3Core", D_METHOD("source_version"), &Q3Core::source_version);
	ClassDB::bind_static_method("Q3Core", D_METHOD("item_classnames"), &Q3Core::item_classnames);
	ClassDB::bind_static_method("Q3Core", D_METHOD("item_quantity", "classname"), &Q3Core::item_quantity);
	ClassDB::bind_static_method("Q3Core", D_METHOD("error_recovers"), &Q3Core::error_recovers);
	ClassDB::bind_static_method("Q3Core", D_METHOD("install_crash_dumps", "dir", "tag"), &Q3Core::install_crash_dumps);
	ClassDB::bind_static_method("Q3Core", D_METHOD("crash_test", "kind"), &Q3Core::crash_test);
	ClassDB::bind_static_method("Q3Core", D_METHOD("process_start_time", "pid"), &Q3Core::process_start_time);
}

// When a running process started (Unix seconds), 0 if it isn't running: Diagnostics tells a live
// copy of the game from one that crashed.
double Q3Core::process_start_time(int pid) {
	return q3_process_start_time(pid);
}

// Native crash dumps into dir (an absolute path) for the rest of the process (src/crash/q3_crash.*).
bool Q3Core::install_crash_dumps(const String &dir, const String &tag) {
	return q3_crash_install(dir.utf8().get_data(), tag.utf8().get_data()) == 1;
}

// Crashes the game on purpose (0 null write, 1 abort, 2 stack overflow, 3 trap): the crash report test.
void Q3Core::crash_test(int kind) {
	q3_crash_test(kind);
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
