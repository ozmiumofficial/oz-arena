#pragma once
// The entity classes the map editor knows (ENGINE_PLAN.md P13, engine/EDITOR.md): id's (the game
// code's spawn table and pickups), CPMA's and Defrag's additions, and the classes of any Radiant
// .def file a mod ships (Defrag's defrag.def), read from the player's install. What a class is
// (a point with a box, or brushes), how it's drawn (its color, its models), where the Entities panel
// lists it, its keys (typed, with what they do) and its spawnflags. Plain C++ like the document.
//
// Also id's numbers the editor shows: the jump pad's throw (g_trigger.c AimAtTarget) and the map's
// gravity.

#include "ed_doc.h"

#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace oz::ed {

enum class KeyType : uint8_t {
	Text, Number, Bool,  // Bool: "1", or absent
	Target,              // another entity's targetname
	Name,                // this one's targetname
	Color,               // "r g b", 0 to 1
	Angle,               // a heading in degrees (-1 up, -2 down)
	Sound,               // a path under sound/
	Model,               // a path under models/
};

struct KeyDef {
	std::string name;
	KeyType type = KeyType::Text;
	std::string def;   // the game's default, "" when it has none
	std::string help;  // what it does
};

struct FlagDef {
	int bit = 0;
	std::string label;
};

struct EntityDef {
	std::string name;
	std::string section;  // the Entities panel's ("Weapons", "Spawns" ...); "" for worldspawn and func_group
	std::string mod;      // "" for id's; "CPMA", "Defrag", or the .def file it came from
	std::string help;     // what it is
	bool brush = false;   // its shape is its brushes (doors, triggers); else a point with a box
	bool faces = false;   // its "angle" is a heading (spawns, teleporter exits, models)
	Vec3 lo, hi;          // a point class's box around its origin
	Vec3 color;           // 0 to 1
	std::vector<std::string> models;  // drawn at its origin ("models/powerups/armor/armor_red.md3")
	std::string icon;     // the panel's picture ("icons/iconr_red")
	double model_scale = 1;           // the game draws weapons half again as big
	bool item = false;    // a pickup: spins and bobs as in the game
	std::vector<KeyDef> keys;
	std::vector<FlagDef> flags;
};

// Every class, id's first, in the Entities panel's order.
const std::vector<EntityDef> &entity_defs();
// nullptr for a class the editor doesn't know (it still keeps and writes every key).
const EntityDef *entity_def(const std::string &classname);
// The keys every entity takes besides its own (g_spawn.c's game type filters, CPMA's).
const std::vector<KeyDef> &common_keys();
// A key's definition on this class (its own, then the common ones), nullptr when unknown.
const KeyDef *key_def(const std::string &classname, const std::string &key);

// A Radiant .def file (/*QUAKED name (r g b) (mins) (maxs) FLAGS ... text */): its classes the
// catalog doesn't have join it under source; one it has gains the flags it lacks. How many joined.
int read_def_file(std::string_view text, const std::string &source);

// ---- links and names
// Where an entity is: its origin, or the middle of its brushes (a brush entity without one).
bool entity_center(const Doc &doc, Id entity, Vec3 &out);
// prefix and the first number no entity uses as a targetname, nor taken.
std::string unique_targetname(const Doc &doc, const std::string &prefix, const std::set<std::string> &taken = {});

// ---- id's numbers
double map_gravity(const Doc &doc);  // worldspawn's "gravity", else 800
// g_trigger.c AimAtTarget: the throw from `from` that tops out at `target`; false when it isn't above.
bool push_velocity(const Vec3 &from, const Vec3 &target, double gravity, Vec3 &velocity);
// Where a jump pad's target goes so a throw from `from` comes down at `landing` (the top clears the
// higher end by 64 units and a third of the distance; ceiling keeps it 40 under a roof).
Vec3 apex_for(const Vec3 &from, const Vec3 &landing, double gravity, double ceiling = 1e30);

}  // namespace oz::ed
