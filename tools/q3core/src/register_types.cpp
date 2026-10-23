// Entry point of the q3core GDExtension (q3core.gdextension: entry_symbol).

#include <gdextension_interface.h>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "q3_api.h"
#include "q3core.h"
#include "q3mapgeo.h"
#include "q3server.h"

using namespace godot;

// id's Com_Printf -> Godot's output; Com_Error -> push_error.
static void q3_print(const char *text, int is_error) {
	String s = String::utf8(text).strip_edges(false, true);
	if (is_error) {
		UtilityFunctions::push_error("q3core: ", s);
	} else {
		UtilityFunctions::print("q3core: ", s);
	}
}

static void initialize_q3core(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	q3_set_print(q3_print);
	GDREGISTER_CLASS(Q3Core);
	GDREGISTER_CLASS(Q3Server);
	GDREGISTER_CLASS(Q3MapGeo);
}

static void uninitialize_q3core(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	q3_set_print(nullptr);
}

extern "C" {
GDExtensionBool GDE_EXPORT q3core_library_init(GDExtensionInterfaceGetProcAddress p_get_proc_address,
		const GDExtensionClassLibraryPtr p_library, GDExtensionInitialization *r_initialization) {
	GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);
	init_obj.register_initializer(initialize_q3core);
	init_obj.register_terminator(uninitialize_q3core);
	init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
	return init_obj.init();
}
}
