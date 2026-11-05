// The map editor's actions and tools (ENGINE_PLAN.md P13, engine/EDITOR.md): one table of actions
// that the keys, the page's buttons, its hints and `ed <action>` all read, so they never disagree;
// the create tool (after the Godot build's q3_create_tool.gd) and the clipper.

#include "ed_view.h"
#include "ed_engine.h"
#include "ed_mapfile.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
#include "../ui/oz_ui_engine.h"
#include "../../../quake3e/code/client/keycodes.h"
}

namespace oz::ed {

static const double PI = 3.14159265358979323846;

const std::vector<Action> &actions() {
	static const std::vector<Action> table = {
		{"select", "Select", "Esc", "Click to select, drag to move; the corners, edges and diamonds of selected brushes reshape them"},
		{"create", "Create brush", "B", "Drag a footprint on a surface or the grid, then move the mouse to set the height"},
		{"clip", "Clipper", "C", "Click two points on a brush to cut through them (a third tilts the cut); Tab: the side kept; Enter cuts"},
		{"faces", "Faces tool", "T", "Click faces to pick them (Shift adds, Ctrl toggles, double-click: the brush's), drag to push; the arrows move their texture, the panel maps it"},
		{"jump_pad", "Jump pad", "J", "Click the floor where the pad goes, then where the player lands: a bounce pad, its trigger and target, one object (a click takes it all, a double-click one piece)"},
		{"teleporter", "Teleporter", "K", "Click the entrance, then the exit: a trigger_teleport and its misc_teleporter_dest, linked"},
		{"light", "Light", "L", "Click a surface: a light a grid step off it"},
		{"place", "Place", "", "ed place <class>: the place tool puts that down where you click (the Entities panel picks it)"},
		{"to_world", "To the world", "", "Puts the selected brushes of an entity (a door, a trigger) back in the world"},
		{"flag", "Spawnflag", "", "ed flag <bit>: a spawnflag of the selected entities, on or off"},
		{"key_new", "Add a key", "", "ed key_new <key>: one of the class's keys, at the game's default"},
		{"pick_material", "Eyedropper", "I", "The material of the face under the pointer becomes the current one; in the faces tool, Alt+click"},
		{"paint", "Paint", "P", "Paints the current material onto the picked faces, or every face of the selected brushes and curves"},
		{"tex_fit", "Fit", "", "Stretches the texture once across each face (ed tex_fit [across] [down])"},
		{"tex_left", "Align left", "", "Puts each face's left edge on an edge of its texture"},
		{"tex_right", "Align right", "", "Puts each face's right edge on an edge of its texture"},
		{"tex_top", "Align top", "", "Puts each face's top edge on an edge of its texture"},
		{"tex_bottom", "Align bottom", "", "Puts each face's bottom edge on an edge of its texture"},
		{"tex_center", "Center", "", "Puts the middle of the texture on the middle of each face"},
		{"tex_flip_s", "Flip across", "", "Mirrors the texture left to right on each face, in place"},
		{"tex_flip_t", "Flip up and down", "", "Mirrors the texture top to bottom on each face, in place"},
		{"tex_reset", "Reset mapping", "", "The texture back on the default projection at half scale"},
		{"tex_rotate_left", "Turn texture left", "Ctrl+Left", "Turns the texture 15 degrees counter-clockwise on each face"},
		{"tex_rotate_right", "Turn texture right", "Ctrl+Right", "Turns the texture 15 degrees clockwise on each face"},
		{"tex_scale_up", "Texture bigger", "Ctrl+Up", "Doubles the texture's scale on each face"},
		{"tex_scale_down", "Texture smaller", "Ctrl+Down", "Halves the texture's scale on each face"},
		{"tex_nudge", "Move texture on screen", "", "In the faces tool the arrows move the texture a grid step the way pressed, as the camera sees each face (Shift: a texel); ed tex_nudge left|right|up|down [texels]"},
		{"tex_nudge_left", "Texture left", "", "Moves the texture a grid step left on screen (Radiant's keys: Shift+Left)"},
		{"tex_nudge_right", "Texture right", "", "Moves the texture a grid step right on screen"},
		{"tex_nudge_up", "Texture up", "", "Moves the texture a grid step up on screen"},
		{"tex_nudge_down", "Texture down", "", "Moves the texture a grid step down on screen"},
		{"tex_shift", "Move texture", "", "ed tex_shift <s> <t>: moves the texture by texels; in the faces tool the arrows move it a grid step on screen, Shift one texel"},
		{"tex_rotate", "Turn texture", "", "ed tex_rotate <degrees>: turns the texture on each face"},
		{"tex_scale", "Scale texture", "", "ed tex_scale <s> [t]: the scale numbers times these"},
		{"tex_set", "Mapping number", "", "ed tex_set shift_s|shift_t|rotate|scale_s|scale_t <value>: one number of the mapping, on each face"},
		{"delete", "Delete", "Del, Backspace", "Deletes the selection"},
		{"delete_corners", "Delete corners", "Shift+Del", "Deletes the picked corners of the selected brushes: each brush reshaped without them"},
		{"duplicate", "Duplicate", "Ctrl+D, Space", "A copy of the selection beside it, to the right as the camera sees it"},
		{"copy", "Copy", "Ctrl+C", "Copies the selection"},
		{"cut", "Cut", "Ctrl+X", "Copies the selection and deletes it"},
		{"paste", "Paste", "Ctrl+V", "Pastes where the pointer is, on the grid"},
		{"select_all", "Select all", "Ctrl+A", "Selects everything in the map"},
		{"select_none", "Select none", "Ctrl+Shift+A", "Clears the selection"},
		{"rotate_left", "Turn left", "R", "Turns the selection 90 degrees counter-clockwise, seen from above"},
		{"rotate_right", "Turn right", "Shift+R", "Turns the selection 90 degrees clockwise, seen from above"},
		{"rotate_left_15", "Turn left 15", "Ctrl+R", "Turns the selection 15 degrees counter-clockwise"},
		{"rotate_right_15", "Turn right 15", "Ctrl+Shift+R", "Turns the selection 15 degrees clockwise"},
		{"flip_x", "Mirror front to back", "M", "Mirrors the selection along X"},
		{"flip_y", "Mirror left to right", "Shift+M", "Mirrors the selection along Y"},
		{"flip_z", "Mirror upside down", "Ctrl+M", "Mirrors the selection along Z"},
		{"subtract", "CSG subtract", "Ctrl+Shift+X", "Carves the selected brushes out of every brush they overlap, then deletes them"},
		{"hollow", "Hollow", "Ctrl+Shift+H", "Makes each selected brush a room: walls one grid step thick (8 units at least) around its inside; ed hollow <units> for any"},
		{"merge", "Merge", "Ctrl+J", "Makes the selected brushes one: the convex shape around them all"},
		{"tint", "Color", "", "ed tint <r g b> | none: colors the picked faces (or every face of the selected brushes): the texture is multiplied by it, in the view and the game"},
		{"collision", "Collision", "", "The selected brushes' collision on or off: off, players and shots pass through them (they still draw, and still block light). Every brush collides at first, liquids aside"},
		{"detail", "Detail", "Ctrl+Shift+D", "Marks the selected brushes detail, or structural again: detail (trim, pillars, props) doesn't cut the map up for visibility, so it compiles faster and runs smoother; walls that hold the map in stay structural"},
		{"texture_lock", "Texture lock", "Ctrl+T", "On: textures move, turn and mirror with their brushes. Off: they stay put in the world"},
		{"grid_smaller", "Smaller grid", "[", "Halves the grid"},
		{"grid_bigger", "Bigger grid", "]", "Doubles the grid"},
		{"play", "Play", "F5", "Compiles the map as it is (BSP and quick light) and plays it from a spawn point; in the game, Escape then Back to the map editor"},
		{"play_here", "Play from here", "Shift+F5", "The same, the player's eye where the camera is"},
		{"compile", "Compile", "Ctrl+F5", "The whole compile in the background (BSP, vis, full light, the bots' file) into maps/ed_<name>.bsp in your files"},
		{"preview", "Lit preview", "F6", "The map lit by its own lights, as the last compile made it (a quick compile first when the map changed); F6 again leaves it"},
		{"compile_stop", "Stop compiling", "", "Stops the compile under way"},
		{"frame_leak", "Show the leak", "", "The camera onto the leak's line: from an entity out to the void through the gap"},
		{"sky_sun", "Sky and sun", "", "The map's sky and its light: the sky around the map, the sun's color, brightness, height and direction"},
		{"warnings", "Warnings", "", "The last compile's or check's warnings, a failed compile first: pick one to select and frame what it's about"},
		{"warning", "Frame a warning", "", "ed warning <n>: selects and frames what the last compile's warning n is about"},
		{"hide", "Hide", "H", "Hides the selection from every view (the map keeps it: it compiles and saves)"},
		{"hide_others", "Hide the rest", "Alt+H", "Hides everything but the selection"},
		{"show_all", "Show all", "Shift+H", "Shows everything hidden again"},
		{"lock", "Lock", "Ctrl+L", "Locks the selection: drawn, but clicks, boxes and Select all pass it by"},
		{"unlock_all", "Unlock all", "Ctrl+Shift+L", "Unlocks everything locked"},
		{"filter_tools", "Tool brushes", "", "Shows or hides the tool brushes (clip, hint, nodraw, origin ...)"},
		{"filter_triggers", "Triggers", "", "Shows or hides the triggers"},
		{"filter_sky", "Sky", "", "Shows or hides the sky brushes"},
		{"filter_entities", "Point entities", "", "Shows or hides the point entities (pickups, spawns, lights ...)"},
		{"filter_curves", "Curves", "", "Shows or hides the curves"},
		{"prefab_save", "Save as a prefab", "", "ed prefab_save <name>: the selection saved as a prefab, for any map (the Entities panel lists them)"},
		{"prefab_insert", "Insert a prefab", "", "ed prefab_insert <name>: a saved prefab put where the pointer is"},
		{"defrag_start", "Start line", "", "Defrag: click one side of the course, then the other: a trigger across it fires Defrag's start timer"},
		{"defrag_checkpoint", "Checkpoint line", "", "Defrag: a trigger across the course firing a checkpoint (two clicks, side to side)"},
		{"defrag_finish", "Finish line", "", "Defrag: a trigger across the course firing Defrag's stop timer (two clicks, side to side)"},
		{"push_height", "Jump higher", "", "ed push_height <units>: the selected jump pads' throw made higher (negative: lower), still landing where it did"},
		{"push_land", "Land here", "", "The selected jump pads: click where players should come down, and their throw is aimed there"},
		{"door", "Door", "", "The selected brushes become a sliding door (it opens when a player comes near, or when shot; Inspect sets which way and how)"},
		{"door_pair", "Double door", "", "The selected brush cut in two: a pair of doors sliding apart, opening together (id's doors)"},
		{"lift", "Lift", "", "The selected brushes become a lift: build it at the top; it starts lowered by its travel and rises when stepped on"},
		{"door_near", "Door opens near", "", "The selected doors open when a player comes near, or not (ed door_near 1|0)"},
		{"door_shot", "Door opens when shot", "", "The selected doors open when shot, or not (ed door_shot 1|0)"},
		{"door_button", "Add a button", "", "Click a wall: a button there that opens the selected doors (pressed, or shot with health)"},
		{"train_path", "Lay a path", "", "The selected train: click where it goes, corner by corner; the first corner again closes the loop"},
		{"mover_play", "Play movers", "", "The selected doors, lifts, bobbing, rotating and swinging brushes and trains move in the view as in the game"},
		{"lift_travel", "Lift travel", "", "ed lift_travel <units>|+n|-n: how far the selected lifts go down"},
		{"check_map", "Check the map", "", "Looks for what would go wrong in the game: brushes without collision, jump pads that do nothing, targets naming nothing"},
		{"defrag_speed", "Speed pad", "", "Defrag: a pad on the floor launching the player the way the view faces (Defrag's target_speed)"},
		{"defrag_shooter", "Shooter", "", "Defrag: a rocket shooter aimed at the player (two clicks: where it shoots from, the zone that sets it off)"},
		{"brush", "Brush by numbers", "", "ed brush <x0 y0 z0 x1 y1 z1>: a brush of the current material filling that box"},
		{"defrag_check", "Check the course", "", "Defrag: a start, a finish, a spawn, every timer fired by a trigger; what's wrong listed, each framed when picked"},
		{"play_defrag", "Play in Defrag", "", "Compiles the map and plays it under your Defrag, its own timer and all (Disconnect comes back)"},
		{"curve_cylinder", "Cylinder", "", "Each selected brush becomes a curved cylinder filling its box (round Z, or the 2D view's axis)"},
		{"curve_cone", "Cone", "", "Each selected brush becomes a cone filling its box"},
		{"curve_arch", "Arch", "", "Each selected brush becomes an arch: half round across its longer side, seen from inside"},
		{"curve_bevel", "Bevel", "", "Each selected brush becomes a bevel: a quarter round, seen from inside"},
		{"curve_plane", "Flat curve", "", "Each selected brush becomes a flat curve on its top, to bend by its control points"},
		{"curve_invert", "Invert curve", "", "The selected curves turned inside out: their other side shows"},
		{"curve_more_columns", "More curve columns", "", "Each section across the selected curves in two: finer to shape, the curve as it was"},
		{"curve_fewer_columns", "Fewer curve columns", "", "Every two sections across in one, through the point between them"},
		{"curve_more_rows", "More curve rows", "", "Each section down the selected curves in two"},
		{"curve_fewer_rows", "Fewer curve rows", "", "Every two sections down in one"},
		{"curve_caps", "Cap curves", "", "Caps for the selected curves' open ends: a cylinder's discs, an arch's half discs"},
		{"curve_thicken", "Thicken curves", "", "The selected curves made thick, a grid step deep: their other skin and the seams between"},
		{"group", "Group", "Ctrl+G", "Makes the selected brushes and curves a group (func_group): a click takes it whole, a double-click one brush"},
		{"ungroup", "Ungroup", "Ctrl+Shift+G", "Takes the selected groups apart, their brushes back in the world"},
		{"views", "Four views", "V", "The 3D view alone, or four: 3D, top, front and side (Radiant's 2D views)"},
		{"view_next", "Next view", "Ctrl+Tab", "One view: the next of 3D, top, front and side"},
		{"pane", "Pane", "", "ed pane 3d|top|front|side: the pane the tools work in (the tests'; the pointer's otherwise)"},
		{"search", "Search", "F3", "Every action by name: type what you want to do, Enter does it"},
		{"uv", "UV editor", "U", "The first picked face's texture in its own 2D view: drag it to move it, the wheel zooms"},
		{"keymap", "Keys", "", "Every action's keys: set your own, or Radiant's"},
		{"undo", "Undo", "Ctrl+Z", "Undoes the last change"},
		{"redo", "Redo", "Ctrl+Y, Ctrl+Shift+Z", "Redoes what was undone"},
		{"save", "Save", "Ctrl+S", "Saves the map as a .map file (a map never saved asks for its name)"},
		{"save_as", "Save as", "Ctrl+Shift+S", "Saves the map under a new name, in your editor maps folder"},
		{"new", "New map", "Ctrl+N", "A new map: a lit room with a spawn point (with unsaved changes, it asks first)"},
		{"open", "Open map", "Ctrl+O", "Opens one of your maps (any .map file in your editor maps folder)"},
		{"import", "Open a game map", "", "Opens a map from the game's files (q3dm17, a map pack's ...), made editable again by q3map2"},
		{"leave", "Exit to main menu", "", "Back to the main menu: the map stays here as it is until you come back"},
		{"guide", "Getting started", "F1", "The basics on one card: moving around, making brushes, materials, entities, playing"},
		{"show_inspect", "Inspect panel", "", "The panel's Inspect tab: what's selected, its material, texture, size and keys"},
		{"show_materials", "Materials panel", "", "The panel's Materials tab: every material, a click paints the selection"},
		{"show_entities", "Entities panel", "", "The panel's Entities tab: pickups, spawns, lights, triggers, Defrag's lines, prefabs"},
		{"show_objects", "Objects panel", "", "The panel's Objects tab: everything in the map, what shows, hiding and locking"},
		{"show_history", "History panel", "", "The panel's History tab: every change, a click goes back to it"},
		{"answer", "Answer", "", "ed answer save|discard|cancel: what to do with unsaved changes"},
		{"recover", "Recover", "", "Opens the map the autosave kept from a session that didn't save it"},
		{"discard_recovery", "Throw away", "", "Throws away the map the autosave kept"},
		{"frame_selection", "Frame the selection", "F", "Turns the camera onto the selection"},
		{"frame_all", "Frame the map", "Home", "The camera onto the whole map"},
		{"nudge_left", "Nudge left", "Left", "Moves the selection a grid step left (as the camera sees it)"},
		{"nudge_right", "Nudge right", "Right", "Moves the selection a grid step right"},
		{"nudge_forward", "Nudge away", "Up", "Moves the selection a grid step away from the camera"},
		{"nudge_back", "Nudge closer", "Down", "Moves the selection a grid step toward the camera"},
		{"nudge_up", "Nudge up", "PgUp", "Moves the selection a grid step up"},
		{"nudge_down", "Nudge down", "PgDn", "Moves the selection a grid step down"},
		{"apply", "Apply", "Enter", "Makes the brush (create) or cuts (clipper)"},
		{"clip_side", "Side kept", "Tab", "What the clipper keeps: the far side, the near side, or both"},
		{"move", "Move by", "", "ed move <x> <y> <z>: moves the selection"},
		{"rotate", "Turn by", "", "ed rotate <degrees> [x|y|z]: turns the selection about its middle"},
		{"size", "Size", "", "ed size <width> <depth> <height>: resizes the selection, on its floor"},
		{"grid", "Grid", "1 - 9", "ed grid <units>: the grid, any power of two"},
		{"material", "Material", "", "ed material <shader>: what new brushes are made of"},
		{"sky", "Sky and sun setting", "", "ed sky <sky | light_from | color | brightness | height | direction | sky_light> <value>, or ed sky reset: the map's sky and its light"},
		{"key_set", "Set key", "", "ed key_set <key> <value>: on the selected entities (nothing selected: the map's own keys)"},
		{"key_remove", "Remove key", "", "ed key_remove <key>: from the selected entities"},
		{"key_rename", "Rename key", "", "ed key_rename <key> <new name>: on the selected entities"},
		{"history", "History", "", "ed history <n>: back or forward to n steps done"},
		{"row", "Select from the list", "", "ed row <id>: selects it (Shift adds, Ctrl toggles); a double-click frames it"},
		{"frame_ids", "Frame", "", "ed frame_ids <id>...: selects them and turns the camera onto them"},
	};
	return table;
}

const Action *find_action(const std::string &name) {
	for (const Action &a : actions()) {
		if (name == a.name) {
			return &a;
		}
	}
	return nullptr;
}

// ---------------------------------------------------------------- keys

bool Session::key(int key, bool down, bool over) {
	if (down && bind_key(key, mods())) {
		return true;  // the Keys dialog waiting for an action's new keys
	}
	if (key == K_MOUSE2) {
		if (down && over && ortho_axis_ >= 0) {
			if (drag_ == Drag::None) {
				start_pan(2);  // a 2D view: the right button drags it, as in Radiant
			}
			return true;
		}
		if (!down && drag_ == Drag::Pan && press_button_ == 2) {
			drag_ = Drag::None;
			press_button_ = 0;
			return true;
		}
		if (down && over) {
			looking_ = true;
			OzEd_SetLooking(1);
			return true;
		}
		if (!down && looking_) {
			looking_ = false;
			OzEd_SetLooking(0);
			return true;
		}
		return over;
	}
	if (looking_) {
		if (down && (key == K_MWHEELUP || key == K_MWHEELDOWN)) {
			// the wheel while flying sets the speed, as in Radiant and TrenchBroom
			fly_scale = std::max(0.125, std::min(8.0, fly_scale * (key == K_MWHEELUP ? 1.25 : 0.8)));
			char buf[48];
			std::snprintf(buf, sizeof buf, "Fly speed: %d%%", int(std::round(fly_scale * 100)));
			say(buf);
		}
		return true;  // the fly keys, polled in frame()
	}
	if (key == K_MOUSE1 || key == K_MOUSE3) {
		const int b = key == K_MOUSE1 ? 1 : 3;
		if (down ? over && drag_ == Drag::None : press_button_ == b) {
			button(b, down);
			return true;
		}
		return false;
	}
	if (!down) {
		return false;
	}
	const int m = mods();
	if (key == K_MWHEELUP || key == K_MWHEELDOWN) {
		if (!over) {
			return false;
		}
		if (m & CTRL) {
			run(key == K_MWHEELUP ? "grid_bigger" : "grid_smaller", {});
			return true;
		}
		if (ortho_axis_ >= 0) {
			zoom_2d((key == K_MWHEELUP ? 1 : -1) * ((m & SHIFT) ? 3 : 1));
			return true;
		}
		// toward what the pointer is over
		Vec3 o, d;
		ray(mx_, my_, o, d);
		cam.origin = cam.origin + d * ((key == K_MWHEELUP ? 1 : -1) * 64.0 * ((m & SHIFT) ? 4 : 1));
		return true;
	}
	if (drag_ != Drag::None) {
		if (key == K_ESCAPE) {
			cancel_drag();
		}
		return true;  // nothing else while a drag is under way
	}
	if (key >= '1' && key <= '9' && !m) {
		grid = double(1 << (key - '1'));
		return true;
	}
	if (tool == Tool::Faces && faces_key(key, m)) {
		return true;
	}
	if (key == K_ESCAPE && !m) {
		cancel();  // the tools' cancel, then the selection's; nothing else (the menus are on the top bar, F10)
		return true;
	}
	if (const Action *a = action_for(combo(key, m))) {
		run(a->name, {});
		return true;
	}
	return key == K_ESCAPE;
}

// ---------------------------------------------------------------- the actions

Vec3 Session::camera_axis(bool right) const {
	if (ortho_axis_ >= 0) {
		return right ? -left_ : up_;  // a 2D view: across and up it
	}
	// the map axis nearest the camera's right or forward, level
	Vec3 g = right ? -left_ : fwd_;
	Vec3 v(g.x, g.y, 0);
	if (v.length() < 1e-4) {
		return Vec3(1, 0, 0);
	}
	return std::fabs(v.x) >= std::fabs(v.y) ? Vec3(v.x > 0 ? 1 : -1, 0, 0) : Vec3(0, v.y > 0 ? 1 : -1, 0);
}

void Session::transform_selection(const Affine &xf, const char *label, bool lock) {
	if (sel.empty()) {
		say("Nothing is selected");
		return;
	}
	if (transform(doc, selected(), xf, lock, label) && !is_translation(xf)) {
		corners.clear();  // turned brushes number their corners anew
	}
}

void Session::rotate(int axis, double degrees) {
	if (sel.empty()) {
		say("Nothing is selected");
		return;
	}
	// about the middle snapped to the grid, so 90 degree turns of brushes on the grid stay on it
	const Vec3 pivot = snap_vec(selection_bounds().center());
	char label[64];
	std::snprintf(label, sizeof label, "Turn %g", degrees);
	transform_selection(rotation(axis, degrees, pivot), label, texture_lock);
}

void Session::flip(int axis) {
	if (sel.empty()) {
		say("Nothing is selected");
		return;
	}
	transform_selection(mirror(axis, selection_bounds().center()), "Mirror", texture_lock);
}

void Session::resize(const Vec3 &size) {
	const Bounds b = selection_bounds();
	if (sel.empty() || b.empty()) {
		say("Nothing is selected");
		return;
	}
	const Vec3 now = b.size();
	if (size.x <= 0 || size.y <= 0 || size.z <= 0) {
		say("A size must be more than 0", true);
		return;
	}
	const Vec3 k(now.x > 0 ? size.x / now.x : 1, now.y > 0 ? size.y / now.y : 1, now.z > 0 ? size.z / now.z : 1);
	// it stays on its floor, around its middle
	const Vec3 c = b.center();
	transform_selection(scaling(k, Vec3(c.x, c.y, b.lo.z)), "Resize", false);
}

void Session::duplicate() {
	if (sel.empty()) {
		say("Nothing is selected");
		return;
	}
	// beside the original along the camera's right, by the selection's own width (on the grid): a copy one
	// grid step over hid inside the original and compiled as two overlapping brushes
	const Vec3 axis = camera_axis(true);
	const Bounds b = selection_bounds();
	double step = grid;
	if (!b.empty()) {
		const Vec3 size = b.hi - b.lo;
		step = std::max(grid, std::ceil((std::fabs(axis.x) * size.x + std::fabs(axis.y) * size.y + std::fabs(axis.z) * size.z) / grid) * grid);
	}
	std::vector<Id> made = oz::ed::paste(doc, copy_of(doc, selected()), axis * step, "Duplicate");
	corners.clear();
	select(made, SelMode::Set);
}

void Session::copy() {
	if (sel.empty()) {
		say("Nothing is selected");
		return;
	}
	clipboard_ = copy_of(doc, selected());
	say("Copied " + std::to_string(clipboard_.size()));
}

static Bounds clip_bounds(const Clip &clip) {
	Bounds b;
	for (const auto &c : clip) {
		const Object &o = *c.second;
		if (o.kind == Kind::Brush) {
			std::vector<Plane> pl;
			for (const Face &f : o.brush.faces) {
				pl.push_back(f.plane);
			}
			for (const Vec3 &p : q3map::brush_vertices(pl)) {
				b.add(p);
			}
		} else if (o.kind == Kind::Patch) {
			for (size_t i = 0; i + 4 < o.patch.ctrl.size(); i += 5) {
				b.add(Vec3(o.patch.ctrl[i], o.patch.ctrl[i + 1], o.patch.ctrl[i + 2]));
			}
		} else {
			Vec3 lo, hi;
			if (point_box(o, lo, hi)) {
				b.add(lo);
				b.add(hi);
			}
		}
	}
	return b;
}

void Session::paste() {
	if (clipboard_.empty()) {
		say("Nothing has been copied");
		return;
	}
	paste_clip(clipboard_, "Paste");
}

void Session::paste_clip(const Clip &clip, const std::string &label) {
	const Bounds b = clip_bounds(clip);
	Vec3 offset = camera_axis(true) * grid;
	Hit h;
	if (!b.empty() && over_ && pick(mx_, my_, h)) {
		// its middle at the pointer, its floor on the surface there
		Vec3 at = snap_vec(h.point);
		Vec3 c = b.center();
		offset = Vec3(at.x - snap(c.x), at.y - snap(c.y), h.normal.z > 0.7 ? h.point.z - b.lo.z : at.z - snap(c.z));
	}
	std::vector<Id> made = oz::ed::paste(doc, clip, offset, label);
	corners.clear();
	select(made, SelMode::Set);
}

void Session::remove_selection() {
	corners.clear();  // a corner picked by a stray click never turns Delete into a reshape (Shift+Delete does that)
	if (sel.empty()) {
		say("Nothing is selected");
		return;
	}
	remove(doc, selected(), "Delete");
}

void Session::set_tool(Tool t) {
	if (drag_ != Drag::None) {
		cancel_drag();
	}
	create_state_ = 0;
	clip_pts_.clear();
	clip_normals_.clear();
	clip_drag_ = -1;
	place_step_ = 0;
	if (t != Tool::Place) {
		place_class.clear();
	}
	if (t == Tool::Faces && tool != Tool::Faces) {
		// the selected brushes' faces become the picked faces
		std::vector<FaceRef> refs = target_faces();
		corners.clear();
		select_none();
		tool = t;
		select_faces(refs, SelMode::Set);
	} else if (t != Tool::Faces && tool == Tool::Faces) {
		// and back: the picked faces' brushes (with their entities) become the selection
		std::vector<Id> ids;
		for (const FaceRef &r : faces) {
			for (Id g : group_of(r.first)) {
				ids.push_back(g);
			}
		}
		select_faces({}, SelMode::Set);
		tool = t;
		select(ids, SelMode::Set);
	}
	tool = t;
}

bool Session::cancel() {
	if (drag_ != Drag::None) {
		cancel_drag();
		return true;
	}
	if (tool == Tool::Create && create_state_ != 0) {
		create_state_ = 0;
		return true;
	}
	if (tool == Tool::Place && place_step_ != 0) {
		place_step_ = 0;
		return true;
	}
	if (tool == Tool::Clip && !clip_pts_.empty()) {
		clip_pts_.clear();
		clip_normals_.clear();
		return true;
	}
	if (tool == Tool::Faces && !faces.empty()) {
		set_tool(Tool::Select);  // back to selecting, the picked faces' brushes selected
		return true;
	}
	if (tool != Tool::Select) {
		set_tool(Tool::Select);
		return true;
	}
	if (!corners.empty()) {
		corners.clear();
		return true;
	}
	if (!sel.empty()) {
		select_none();
		return true;
	}
	return false;
}

static double arg(const std::vector<std::string> &a, size_t i, double fallback = 0) {
	return i < a.size() ? std::atof(a[i].c_str()) : fallback;
}

static int axis_arg(const std::vector<std::string> &a, size_t i) {
	if (i >= a.size() || a[i].empty()) {
		return 2;
	}
	const char c = char(std::tolower((unsigned char)a[i][0]));
	return c == 'x' ? 0 : c == 'y' ? 1 : 2;
}

bool Session::run(const std::string &action, const std::vector<std::string> &a) {
	std::string note;
	if (faces_action(action, a)) {
		return true;
	}
	if (action == "jump_pad" || action == "teleporter" || action == "light") {
		return entities_action("place", {action});
	}
	if (action == "play" || action == "play_here" || action == "compile") {
		compile(action == "compile", action != "compile", action == "play_here");
		return true;
	}
	if (action == "compile_stop") {
		compile_stop();
		return true;
	}
	if (action == "check_map") {
		check_map();
		return true;
	}
	if (movers_action(action, a)) {
		return true;
	}
	if (action == "preview") {
		toggle_preview();
		return true;
	}
	if (action == "views" || action == "view_next" || action == "pane") {
		Pane p = solo;
		if (action == "views") {
			layout = layout == 4 ? 1 : 4;
			solo = layout == 1 ? active_ : solo;  // one view: the one the pointer was in
		} else if (action == "view_next") {
			layout = 1;
			solo = Pane((int(solo) + 1) % 4);
		} else if (!a.empty() && pane_by_name(a[0], p)) {
			if (layout == 1) {
				solo = p;
			}
			active_ = p;
			use_pane(p);  // its place as last drawn, for the pointer hooks that follow
		}
		say(layout == 4 ? std::string("Four views: 3D, top, front, side") : std::string(pane_name(solo)) + " view" +
			(solo == Pane::Top ? " (X across, Y up)" : solo == Pane::Front ? " (X across, Z up)" : solo == Pane::Side ? " (Y across, Z up)" : ""));
		return true;
	}
	if (action == "brush") {
		// a brush typed by its box: ed brush x0 y0 z0 x1 y1 z1
		Bounds b;
		b.add(Vec3(arg(a, 0), arg(a, 1), arg(a, 2)));
		b.add(Vec3(arg(a, 3), arg(a, 4), arg(a, 5)));
		const Vec3 s = b.size();
		if (a.size() < 6 || s.x <= 0 || s.y <= 0 || s.z <= 0) {
			say("ed brush x0 y0 z0 x1 y1 z1: a box with some size every way", true);
			return true;
		}
		doc.begin("Brush");
		const Id id = doc.add(box_brush(doc, b.lo, b.hi, shader_id(shader), doc.worldspawn()));
		doc.end();
		corners.clear();
		select({id}, SelMode::Set);
		return true;
	}
	if (action == "defrag_start" || action == "defrag_checkpoint" || action == "defrag_finish" || action == "defrag_speed" || action == "defrag_shooter") {
		set_tool(Tool::Place);
		place_class = action;
		place_step_ = 0;
		return true;
	}
	if (action == "defrag_check") {
		defrag_check();
		return true;
	}
	if (action == "play_defrag") {
		std::string why;
		if (!defrag_ready(why)) {
			say(why, true);
		} else {
			compile(false, true, false, true);
		}
		return true;
	}
	if (action.compare(0, 6, "curve_") == 0) {
		curves_action(action.substr(6));
		return true;
	}
	if (action == "prefab_save" || action == "prefab_insert") {
		const std::string name = a.empty() ? "" : a[0];
		action == "prefab_save" ? prefab_save(name) : prefab_insert(name);
		return true;
	}
	if (action == "hide" || action == "hide_others") {
		hide_selection(action == "hide_others");
		return true;
	}
	if (action == "show_all" || action == "lock" || action == "unlock_all" || action == "group" || action == "ungroup") {
		action == "show_all" ? show_all() : action == "lock" ? lock_selection() : action == "unlock_all" ? unlock_all()
			: action == "group" ? group_selection() : ungroup_selection();
		return true;
	}
	if (action.compare(0, 7, "filter_") == 0) {
		static const char *names[] = {"filter_tools", "filter_triggers", "filter_sky", "filter_entities", "filter_curves"};
		for (int i = 0; i < 5; i++) {
			if (action == names[i]) {
				toggle_filter(1u << i);
				return true;
			}
		}
	}
	if (action == "warnings") {
		if (warnings_.empty()) {
			say(compile_report_.empty() ? "No compile yet" : "The last compile had no warnings");
		} else {
			want_warnings = true;
		}
		return true;
	}
	if (action == "warning") {
		const size_t i = a.empty() ? 0 : size_t(std::max(0, std::atoi(a[0].c_str())));
		if (i >= warnings_.size()) {
			return true;
		}
		std::vector<Id> there;
		for (Id id : warnings_[i].ids) {
			if (doc.get(id)) {
				there.push_back(id);
			}
		}
		if (there.empty()) {
			say(warnings_[i].text + (warnings_[i].ids.empty() ? "" : " (what it was about is gone since)"));
			return true;
		}
		set_tool(Tool::Select);
		corners.clear();
		select(there, SelMode::Set);
		frame_selection();
		say(warnings_[i].text);
		return true;
	}
	if (action == "uv") {
		if (target_faces().empty()) {
			say("Select a brush, or pick a face with the faces tool (T), to see its texture");
		} else {
			want_uv = true;
		}
		return true;
	}
	if (action == "sky_sun" || action == "sky") {
		return sky_action(action, a);
	}
	if (action == "search" || action == "keymap") {
		(action == "search" ? want_search : want_keymap) = true;  // the page opens them
		return true;
	}
	if (action == "leave" || action == "guide") {
		if (action == "leave") {
			autosave_write();  // the session keeps the map, but quitting the game from the menu would lose it
		}
		want_page = action;
		return true;
	}
	if (action == "show_inspect" || action == "show_materials" || action == "show_entities" || action == "show_objects" || action == "show_history") {
		want_page = "panel " + action.substr(5);
		return true;
	}
	if (action == "open" || action == "import") {
		// ed open <file.map>, ed import <map>; alone, the page lists what there is (unsaved changes: it asks first)
		if (a.empty()) {
			want_page = action;
		} else if (!ask_first(action + "\t" + a[0])) {
			load(action + "\t" + a[0]);
		}
		return true;
	}
	if (action == "save_as" || (action == "save" && a.empty() && path.empty())) {
		// ed save_as <name>: into the editor's maps folder under that name; alone (or a map never saved), the page asks for it
		if (a.empty()) {
			after_save_.clear();  // asked for plainly: nothing waits on it
			want_page = "save_as";
			return true;
		}
		saved_as(a[0]);
		return true;
	}
	if (action == "bind_start") {
		// ed bind_start <action>: its next keys pressed become its keys (Esc: as they were; Backspace: none)
		if (!a.empty() && find_action(a[0])) {
			binding = a[0];
		}
		return true;
	}
	if (action == "bind") {
		// ed bind <action> [keys ...]: as if pressed ("Ctrl+D"; none: unbound)
		if (!a.empty() && find_action(a[0])) {
			std::string keys;
			for (size_t i = 1; i < a.size(); i++) {
				keys += (i > 1 ? " " : "") + a[i];
			}
			bind(a[0], keys);
		}
		return true;
	}
	if (action == "keymap_preset") {
		keymap_preset(a.empty() ? "oz" : a[0]);
		return true;
	}
	if (action.compare(0, 10, "tex_nudge_") == 0) {
		const std::string d = action.substr(10);
		tex_nudge(d == "left" ? -1 : d == "right" ? 1 : 0, d == "up" ? -1 : d == "down" ? 1 : 0, grid);
		return true;
	}
	if (action == "compile_wait") {
		compile_wait();  // a test hook: until the compile is done
		return true;
	}
	if (action == "frame_leak") {
		frame_leak();
		return true;
	}
	if (entities_action(action, a)) {
		return true;
	}
	if (tool == Tool::Faces && (action == "delete" || action == "duplicate" || action == "cut" || action.compare(0, 6, "rotate") == 0 ||
			action.compare(0, 5, "flip_") == 0 || action.compare(0, 6, "nudge_") == 0 || action == "hollow" || action == "subtract" || action == "merge")) {
		say("That works on brushes: Esc leaves the faces tool with their brushes selected");
		return true;
	}
	if (action == "select") {
		cancel();
	} else if (action == "create") {
		set_tool(tool == Tool::Create ? Tool::Select : Tool::Create);
	} else if (action == "clip") {
		set_tool(tool == Tool::Clip ? Tool::Select : Tool::Clip);
	} else if (action == "delete") {
		remove_selection();
	} else if (action == "delete_corners") {
		if (corners.empty()) {
			say("Pick corners of a selected brush first (click them; Shift adds)");
		} else {
			delete_corners();
		}
	} else if (action == "duplicate") {
		duplicate();
	} else if (action == "copy") {
		copy();
	} else if (action == "cut") {
		copy();
		remove_selection();
	} else if (action == "paste") {
		paste();
	} else if (action == "select_all") {
		if (tool == Tool::Faces) {
			std::vector<FaceRef> all;
			for (Id id : doc.ids_of(Kind::Brush)) {
				for (size_t i = 0; pickable(id) && i < doc.get(id)->brush.faces.size(); i++) {
					all.push_back({id, int(i)});
				}
			}
			select_faces(all, SelMode::Set);
		} else {
			std::vector<Id> all = doc.ids();
			select(all, SelMode::Set);
		}
	} else if (action == "select_none") {
		corners.clear();
		select_none();
		select_faces({}, SelMode::Set);
	} else if (action == "rotate_left" || action == "rotate_right" || action == "rotate_left_15" || action == "rotate_right_15") {
		const double deg = action.find("15") != std::string::npos ? 15 : 90;
		rotate(2, action.compare(0, 11, "rotate_left") == 0 ? deg : -deg);
	} else if (action == "flip_x" || action == "flip_y" || action == "flip_z") {
		flip(action[5] - 'x');
	} else if (action == "subtract") {
		std::vector<Id> made = oz::ed::subtract(doc, selected(), note);
		if (!made.empty()) {
			select_none();
		}
		say(note, made.empty());
	} else if (action == "tint") {
		std::vector<FaceRef> t = target_faces();
		if (t.empty()) {
			say("Select brushes, or pick faces with the faces tool (T), to color them");
			return true;
		}
		int32_t value = 0;
		const bool none = a.empty() || a[0] == "none" || a[0] == "white";
		if (!none) {
			double c[3] = {arg(a, 0, 1), arg(a, 1, 1), arg(a, 2, 1)};
			const double m = std::max({c[0], c[1], c[2]}) > 1 ? 255 : 1;
			int b[3];
			for (int i = 0; i < 3; i++) {
				b[i] = int(std::round(std::max(0.0, std::min(1.0, c[i] / m)) * 255));
			}
			value = TINT_MARK | (b[0] << 16) | (b[1] << 8) | b[2];
			if (b[0] == 255 && b[1] == 255 && b[2] == 255) {
				value = 0;  // white is no color
			}
		}
		std::map<Id, std::vector<int>> by;
		for (const FaceRef &r : t) {
			by[r.first].push_back(r.second);
		}
		doc.begin(value ? "Color" : "No color");
		for (auto &kv : by) {
			auto o = doc.edit(kv.first);
			for (int i : kv.second) {
				if (i >= 0 && i < int(o->brush.faces.size())) {
					Face &f = o->brush.faces[size_t(i)];
					if (value) {
						f.has_flags = true;
						f.value = value;
					} else if ((f.value & 0x7f000000) == TINT_MARK) {
						f.value = 0;
					}
				}
			}
			doc.put(kv.first, o);
		}
		doc.end();
	} else if (action == "collision") {
		// on unless every selected brush is off: the click turns them all the other way
		std::vector<Id> brushes;
		bool off = true;
		for (Id id : sel) {
			const Object *o = doc.get(id);
			if (o && o->kind == Kind::Brush) {
				brushes.push_back(id);
				off &= is_nocollide(*o);
			}
		}
		if (brushes.empty()) {
			say("Select brushes to turn their collision on or off");
			return true;
		}
		doc.begin(off ? "Collision on" : "Collision off");
		for (Id id : brushes) {
			auto o = doc.edit(id);
			for (Face &f : o->brush.faces) {
				f.has_flags = true;
				f.contents = off ? f.contents & ~CONTENTS_OZ_NOCOLLIDE : f.contents | CONTENTS_OZ_NOCOLLIDE;
			}
			doc.put(id, o);
		}
		doc.end();
		say(std::to_string(brushes.size()) + (brushes.size() == 1 ? " brush: " : " brushes: ") + (off ? "collision on, players stand on it and bump into it"
			: "collision off, players and shots pass through (shown washed in blue)"));
	} else if (action == "detail") {
		// q3map2 reads CONTENTS_DETAIL from a face's contents number; every face of a brush the same
		std::vector<Id> brushes;
		bool all = true;
		for (Id id : sel) {
			const Object *o = doc.get(id);
			if (o && o->kind == Kind::Brush) {
				brushes.push_back(id);
				all &= is_detail(*o);
			}
		}
		if (brushes.empty()) {
			say("Select brushes to mark detail (or structural)");
			return true;
		}
		doc.begin(all ? "Structural" : "Detail");
		for (Id id : brushes) {
			auto o = doc.edit(id);
			for (Face &f : o->brush.faces) {
				f.has_flags = true;
				f.contents = all ? f.contents & ~CONTENTS_DETAIL : f.contents | CONTENTS_DETAIL;
			}
			doc.put(id, o);
		}
		doc.end();
		say(std::to_string(brushes.size()) + (brushes.size() == 1 ? " brush " : " brushes ") + (all ? "structural again: it cuts the map up for visibility"
			: "marked detail: it doesn't cut the map up for visibility (never use it for walls that seal the map)"));
	} else if (action == "hollow") {
		const double thick = !a.empty() ? std::max(1.0, arg(a, 0, grid)) : std::max(grid, 8.0);
		std::vector<Id> made = oz::ed::hollow(doc, selected(), thick, note);
		if (!made.empty()) {
			select(made, SelMode::Set);
		}
		say(note, made.empty());
	} else if (action == "merge") {
		std::vector<Id> made = oz::ed::merge(doc, selected(), note);
		if (!made.empty()) {
			corners.clear();
			select(made, SelMode::Set);
		}
		say(note, made.empty());
	} else if (action == "texture_lock") {
		texture_lock = a.empty() ? !texture_lock : std::atoi(a[0].c_str()) != 0;
		say(texture_lock ? "Texture lock on: textures go with their brushes" : "Texture lock off: textures stay put in the world");
	} else if (action == "grid_smaller") {
		grid = std::max(0.125, grid / 2);
	} else if (action == "grid_bigger") {
		grid = std::min(4096.0, grid * 2);
	} else if (action == "grid") {
		const double g = arg(a, 0, grid);
		const double p = std::exp2(std::round(std::log2(std::max(0.125, std::min(4096.0, g)))));
		grid = p;
	} else if (action == "undo" || action == "redo") {
		const bool redo = action == "redo";
		const std::string label = redo ? doc.redo_label() : doc.undo_label();
		if (redo ? doc.redo() : doc.undo()) {
			corners.clear();
			say((redo ? "Redo " : "Undo ") + label);
		} else {
			say(redo ? "Nothing to redo" : "Nothing to undo");
		}
	} else if (action == "save") {
		std::string err;
		const std::string file = !a.empty() ? a[0] : path;
		if (!save(file, err)) {
			say(err, true);
		} else {
			say("Saved " + file);
		}
	} else if (action == "new") {
		if (!ask_first("new")) {
			new_map();
		}
	} else if (action == "answer") {
		answer(a.empty() ? "cancel" : a[0]);
	} else if (action == "autosave_now") {
		autosave_write();  // a test hook: the map written aside now
	} else if (action == "recover") {
		recover();
	} else if (action == "discard_recovery") {
		discard_recovery();
	} else if (action == "frame_selection") {
		frame_selection();
	} else if (action == "frame_all") {
		frame_all();
	} else if (action.compare(0, 6, "nudge_") == 0) {
		const std::string dir = action.substr(6);
		Vec3 d = dir == "left" ? -camera_axis(true) : dir == "right" ? camera_axis(true) : dir == "forward" ? camera_axis(false)
			: dir == "back" ? -camera_axis(false) : dir == "up" ? Vec3(0, 0, 1) : Vec3(0, 0, -1);
		if (sel.empty()) {
			say("Nothing is selected");
		} else {
			nudge(d * grid);
		}
	} else if (action == "apply") {
		if (tool == Tool::Create) {
			create_commit();
		} else if (tool == Tool::Clip) {
			clip_apply();
		} else {
			say("Enter makes the brush (create tool, B) or cuts (clipper, C)");
		}
	} else if (action == "clip_side") {
		if (tool != Tool::Clip) {
			return true;  // Tab is the clipper's alone
		}
		clip_keep_ = (clip_keep_ + 1) % 3;
		say(clip_keep_ == 2 ? "The clipper keeps both sides" : clip_keep_ == 1 ? "The clipper keeps the near side" : "The clipper keeps the far side");
	} else if (action == "move") {
		transform_selection(translation(Vec3(arg(a, 0), arg(a, 1), arg(a, 2))), "Move", texture_lock);
	} else if (action == "rotate") {
		rotate(axis_arg(a, 1), arg(a, 0));
	} else if (action == "size") {
		resize(Vec3(arg(a, 0), arg(a, 1), arg(a, 2)));
	} else if (action == "material") {
		if (!a.empty()) {
			shader = a[0].compare(0, 9, "textures/") == 0 ? a[0].substr(9) : a[0];
		}
		say("New brushes: " + shader);
	} else if (action == "click" || action == "drag" || action == "hover" || action == "pick") {
		// test hooks, in the view's pixels from its top left: ed click x y [mods], ed drag x0 y0 x1 y1 [mods]
		const int x0 = vx_ + int(arg(a, 0)), y0 = vy_ + int(arg(a, 1));
		if (action == "hover") {
			pointer(x0, y0);
		} else if (action == "pick") {
			// what the ray meets there, nearest first
			const std::vector<Hit> hits = pick_all(x0, y0);
			for (const Hit &h : hits) {
				const Object *o = doc.get(h.id);
				const std::string what = o->kind == Kind::Brush ? shader_name(o->brush.faces[size_t(h.face)].shader)
					: o->kind == Kind::Patch ? shader_name(o->patch.shader) : o->entity.classname();
				OzE_Print(("ed pick: " + std::to_string(h.id) + " face " + std::to_string(h.face) + " at " + vec_key(h.point) + " " + what + "\n").c_str());
			}
			if (hits.empty()) {
				OzE_Print("ed pick: nothing\n");
			}
		} else if (action == "click") {
			force_mods(int(arg(a, 2)));
			pointer(x0, y0);
			button(1, true);
			button(1, false);
			force_mods(-1);
		} else {
			const int x1 = vx_ + int(arg(a, 2)), y1 = vy_ + int(arg(a, 3));
			force_mods(int(arg(a, 4)));
			pointer(x0, y0);
			button(1, true);
			for (int i = 1; i <= 8; i++) {
				pointer(x0 + (x1 - x0) * i / 8, y0 + (y1 - y0) * i / 8);
			}
			button(1, false);
			force_mods(-1);
		}
	} else if (action == "click_at" || action == "drag_at" || action == "hover_at") {
		// test hooks at map points, wherever the camera is: ed click_at x y z [mods],
		// ed drag_at x0 y0 z0 x1 y1 z1 [mods], ed hover_at x y z
		auto at = [&](size_t i, int &x, int &y) {
			double sx, sy;
			if (!project(Vec3(arg(a, i), arg(a, i + 1), arg(a, i + 2)), sx, sy)) {
				return false;
			}
			x = int(std::lround(sx)), y = int(std::lround(sy));
			return true;
		};
		int x0, y0, x1, y1;
		if (!at(0, x0, y0) || (action == "drag_at" && !at(3, x1, y1))) {
			say("ed " + action + ": that point is behind the camera", true);
			return true;
		}
		if (action == "hover_at") {
			pointer(x0, y0);
		} else if (action == "click_at") {
			force_mods(int(arg(a, 3)));
			pointer(x0, y0);
			button(1, true);
			button(1, false);
			force_mods(-1);
		} else {
			force_mods(int(arg(a, 6)));
			pointer(x0, y0);
			button(1, true);
			for (int i = 1; i <= 8; i++) {
				pointer(x0 + (x1 - x0) * i / 8, y0 + (y1 - y0) * i / 8);
			}
			button(1, false);
			force_mods(-1);
		}
	} else if (action == "select_ids") {
		std::vector<Id> ids;
		for (const std::string &s : a) {
			ids.push_back(Id(std::atoi(s.c_str())));
		}
		select(ids, SelMode::Set);
	} else if (action == "select_inside") {
		// what lies wholly inside a box of the map: ed select_inside x0 y0 z0 x1 y1 z1
		Bounds box;
		box.add(Vec3(arg(a, 0), arg(a, 1), arg(a, 2)));
		box.add(Vec3(arg(a, 3), arg(a, 4), arg(a, 5)));
		std::vector<Id> ids;
		for (Id id : doc.ids()) {
			const Bounds b = bounds_of(doc, id);
			if (id != doc.worldspawn() && !b.empty() && b.lo.x >= box.lo.x - 0.01 && b.lo.y >= box.lo.y - 0.01 && b.lo.z >= box.lo.z - 0.01 &&
					b.hi.x <= box.hi.x + 0.01 && b.hi.y <= box.hi.y + 0.01 && b.hi.z <= box.hi.z + 0.01) {
				ids.push_back(id);
			}
		}
		corners.clear();
		select(ids, SelMode::Set);
	} else {
		return false;
	}
	return true;
}

// ---------------------------------------------------------------- the create tool

static double comp(const Vec3 &v, int i) {
	return i == 0 ? v.x : i == 1 ? v.y : v.z;
}

static void set_comp(Vec3 &v, int i, double x) {
	(i == 0 ? v.x : i == 1 ? v.y : v.z) = x;
}

// a point kept within the editor's limit (Session::MAP_LIMIT), where the ground grid ends
static Vec3 in_limit(const Vec3 &p) {
	const double m = Session::MAP_LIMIT;
	return Vec3(std::clamp(p.x, -m, m), std::clamp(p.y, -m, m), std::clamp(p.z, -m, m));
}

static int main_axis(const Vec3 &n) {
	const double a = std::fabs(n.x), b = std::fabs(n.y), c = std::fabs(n.z);
	return a >= b && a >= c ? 0 : b >= c ? 1 : 2;
}

void Session::create_press(int m) {
	(void)m;
	Hit h;
	Vec3 at;
	if (ortho_axis_ >= 0) {
		// a 2D view: the footprint in its plane, as deep as the selection, else as the last brush made
		Vec3 o, d;
		ray(mx_, my_, o, d);
		const Bounds b = selection_bounds();
		const Vec3 lo = b.empty() ? work_lo_ : b.lo, hi = b.empty() ? work_hi_ : b.hi;
		create_axis_ = ortho_axis_, create_sign_ = 1;
		create_plane_ = comp(lo, create_axis_);
		create_height_ = std::max(comp(hi, create_axis_) - create_plane_, std::max(grid, 8.0));
		at = o;
	} else if (pick(mx_, my_, h, false)) {
		create_axis_ = main_axis(h.normal);
		create_sign_ = comp(h.normal, create_axis_) < 0 ? -1 : 1;
		create_plane_ = comp(h.point, create_axis_);
		if (std::fabs(comp(h.normal, create_axis_)) > 0.999) {
			create_plane_ = std::round(create_plane_ * 1024) / 1024;
		}
		at = h.point;
	} else {
		create_axis_ = 2, create_sign_ = 1, create_plane_ = 0;
		if (!plane_point(mx_, my_, Vec3(), Vec3(0, 0, 1), at)) {
			return;
		}
	}
	create_a_ = in_limit(snap_vec(at));
	create_plane_ = std::clamp(create_plane_, -MAP_LIMIT, MAP_LIMIT);
	set_comp(create_a_, create_axis_, create_plane_);
	create_b_ = create_a_;
	create_state_ = 1;
}

void Session::create_motion() {
	Vec3 n;
	set_comp(n, create_axis_, create_sign_);
	if (create_state_ == 1) {
		Vec3 p;
		if (plane_point(mx_, my_, create_a_, n, p)) {
			create_b_ = in_limit(snap_vec(p));
			set_comp(create_b_, create_axis_, create_plane_);
		}
		return;
	}
	if (create_state_ != 2) {
		return;
	}
	// the height: where the pointer's ray passes the line up from the footprint corner nearest the camera
	Vec3 best = create_a_;
	double bd = 1e300;
	const int o1 = (create_axis_ + 1) % 3, o2 = (create_axis_ + 2) % 3;
	for (int i = 0; i < 4; i++) {
		Vec3 c = create_a_;
		if (i & 1) {
			set_comp(c, o1, comp(create_b_, o1));
		}
		if (i & 2) {
			set_comp(c, o2, comp(create_b_, o2));
		}
		if ((c - eye_).length() < bd) {
			bd = (c - eye_).length();
			best = c;
		}
	}
	Vec3 e;
	set_comp(e, create_axis_, 1);
	Vec3 o, d;
	ray(mx_, my_, o, d);
	const Vec3 w = o - best;
	const double b = d.dot(e), dd = d.dot(d), denom = dd - b * b;
	if (std::fabs(denom) < 1e-6) {
		return;
	}
	const double t = (dd * e.dot(w) - b * d.dot(w)) / denom;
	double h = snap(t * create_sign_);
	// the top stays within the limit too
	const double top = std::clamp(create_plane_ + create_sign_ * h, -MAP_LIMIT, MAP_LIMIT);
	h = (top - create_plane_) * create_sign_;
	if (std::fabs(h) >= 1) {
		create_height_ = h;
	}
}

static void create_box(const Vec3 &a, const Vec3 &b, int axis, double plane, double sign, double height, Vec3 &lo, Vec3 &hi) {
	lo = Vec3(std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z));
	hi = Vec3(std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z));
	const double top = plane + sign * height;
	set_comp(lo, axis, std::min(plane, top));
	set_comp(hi, axis, std::max(plane, top));
}

bool Session::create_commit() {
	if (create_state_ != 2) {
		return false;
	}
	Vec3 lo, hi;
	create_box(create_a_, create_b_, create_axis_, create_plane_, create_sign_, create_height_, lo, hi);
	const Vec3 s = hi - lo;
	if (s.x <= 0 || s.y <= 0 || s.z <= 0) {
		say("A brush needs some size every way", true);
		return false;
	}
	doc.begin("Create brush");
	Id id = doc.add(box_brush(doc, lo, hi, shader_id(shader), doc.worldspawn()));
	doc.end();
	work_lo_ = lo, work_hi_ = hi;
	create_state_ = 0;
	corners.clear();
	select({id}, SelMode::Set);
	return true;
}

// ---------------------------------------------------------------- the clipper

void Session::clip_click(int m) {
	(void)m;
	if (press_handle_.kind == Handle::ClipPoint) {
		return;
	}
	Hit h;
	const bool hit = pick(mx_, my_, h, false);
	if (ortho_axis_ >= 0 && (hit || !sel.empty())) {
		// a 2D view: anywhere in it, the cut running along its axis through the selection (a click
		// selects only when nothing is: the ray meets whatever is nearest along the axis first)
		if (sel.empty()) {
			corners.clear();
			select(group_of(h.id), SelMode::Set);
		}
		if (clip_pts_.size() >= 2) {
			clip_pts_.clear();
			clip_normals_.clear();
		}
		Vec3 o, d;
		ray(mx_, my_, o, d);
		Vec3 p = snap_vec(o);
		set_comp(p, ortho_axis_, comp(selection_bounds().center(), ortho_axis_));
		clip_pts_.push_back(p);
		clip_normals_.push_back(-fwd_);
		return;
	}
	if (!hit) {
		if (clip_pts_.empty()) {
			say("Click on a brush to put the clipper's first point");
		}
		return;
	}
	if (sel.empty() || (!sel.count(h.id) && clip_pts_.empty())) {
		corners.clear();
		select(group_of(h.id), SelMode::Set);
	}
	if (clip_pts_.size() == 3) {
		clip_pts_.clear();
		clip_normals_.clear();
	}
	// on the grid in the face's plane
	const int axis = main_axis(h.normal);
	Vec3 p = snap_vec(h.point);
	set_comp(p, axis, comp(h.point, axis));
	clip_pts_.push_back(p);
	clip_normals_.push_back(h.normal);
}

bool Session::clip_plane(Plane &out) const {
	if (clip_pts_.size() < 2) {
		return false;
	}
	Vec3 n;
	if (clip_pts_.size() == 3) {
		n = (clip_pts_[1] - clip_pts_[0]).cross(clip_pts_[2] - clip_pts_[0]);
	}
	if (n.length() < 1e-6) {
		// two points: the cut runs through them straight into the face the first was put on
		n = (clip_pts_[1] - clip_pts_[0]).cross(clip_normals_[0]);
	}
	if (n.length() < 1e-6) {
		return false;
	}
	n = n.normalized();
	// facing the camera: "near" is in front
	if (n.dot(cam.origin - clip_pts_[0]) < 0) {
		n = -n;
	}
	out = q3map::clean_plane(Plane{n, n.dot(clip_pts_[0])});
	return true;
}

bool Session::clip_apply() {
	Plane p;
	if (!clip_plane(p)) {
		say("The clipper needs two points on a brush", true);
		return false;
	}
	std::string note;
	std::vector<Id> kept = clip(doc, selected(), p, clip_keep_, note);
	say(note, kept.empty() && note.compare(0, 7, "Clipped") != 0);
	clip_pts_.clear();
	clip_normals_.clear();
	corners.clear();
	select(kept, SelMode::Set);
	return true;
}

// ---------------------------------------------------------------- previews

void Session::build_preview() {
	static const unsigned char box_fill[4] = {255, 150, 70, 46}, box_edge[4] = {255, 150, 70, 255}, box_hidden[4] = {255, 150, 70, 70},
		gone[4] = {255, 40, 30, 120}, plane_fill[4] = {110, 170, 255, 40}, plane_edge[4] = {110, 170, 255, 220};
	preview_.begin();
	if (tool == Tool::Create && create_state_ != 0) {
		Vec3 lo, hi;
		create_box(create_a_, create_b_, create_axis_, create_plane_, create_sign_, create_state_ == 2 ? create_height_ : 0, lo, hi);
		preview_.box_faces(lo, hi, box_fill);
		preview_.box_edges(lo, hi, box_edge);
		preview_.box_edges(lo, hi, box_hidden, "$oz_ed_lines_xray");
	}
	Plane p;
	if (tool == Tool::Clip && clip_plane(p)) {
		// the cut, across the selection
		Bounds b = selection_bounds();
		if (!b.empty()) {
			b.add(b.lo - Vec3(16, 16, 16));
			b.add(b.hi + Vec3(16, 16, 16));
			Winding w = q3map::base_winding(p);
			for (const Plane &bp : q3map::box_planes(b.lo, b.hi)) {
				q3map::clip_back(w, bp);
			}
			preview_.polygon(w, plane_fill);
			preview_.outline(w, plane_edge);
			preview_.outline(w, plane_edge, "$oz_ed_lines_xray");
		}
		// what goes
		if (clip_keep_ != 2) {
			for (Id id : sel) {
				const Object *o = doc.get(id);
				if (!o || o->kind != Kind::Brush) {
					continue;
				}
				Pieces pc = clipped(doc, *o, p, Vec3());
				std::shared_ptr<Object> drop = clip_keep_ == 1 ? pc.back : pc.front;
				if (!drop) {
					continue;
				}
				std::vector<Plane> pl;
				for (const Face &f : drop->brush.faces) {
					pl.push_back(f.plane);
				}
				for (const Winding &w : q3map::brush_windings(pl)) {
					preview_.polygon(w, gone, "$oz_ed_tint");
				}
			}
		}
	}
	preview_.end();
}

// ---------------------------------------------------------------- hints

// What the brush being drawn, or the drag, makes, by the pointer: a footprint's two sides (a side
// without width yet the cell a let-go gives it; in a 2D view its depth too), then the whole box, a
// move's offset, a push's or a corner's distance with the selection's size.
bool Session::size_tag(std::string &text, double &sx, double &sy) const {
	if (looking_ || mx_ <= -10000) {
		return false;
	}
	char buf[160];
	if (tool == Tool::Create && create_state_ == 1) {
		const double cell = std::max(grid, 8.0);
		double side[3];
		for (int i = 0; i < 3; i++) {
			const double w = std::fabs(comp(create_b_, i) - comp(create_a_, i));
			side[i] = i == create_axis_ ? create_height_ : w > 0 ? w : cell;
		}
		if (ortho_axis_ >= 0) {
			std::snprintf(buf, sizeof buf, "%g x %g x %g", side[0], side[1], side[2]);
		} else {
			const int o1 = std::min((create_axis_ + 1) % 3, (create_axis_ + 2) % 3), o2 = 3 - create_axis_ - o1;
			std::snprintf(buf, sizeof buf, "%g x %g", side[o1], side[o2]);
		}
	} else if (tool == Tool::Create && create_state_ == 2) {
		Vec3 lo, hi;
		create_box(create_a_, create_b_, create_axis_, create_plane_, create_sign_, create_height_, lo, hi);
		const Vec3 s = hi - lo;
		std::snprintf(buf, sizeof buf, "%g x %g x %g", s.x, s.y, s.z);
	} else if (!drag_note_.empty()) {
		const Bounds b = selection_bounds();
		if ((drag_ == Drag::Push || drag_ == Drag::Corners) && !b.empty()) {
			const Vec3 s = b.hi - b.lo;
			std::snprintf(buf, sizeof buf, "%s Â· %g x %g x %g", drag_note_.c_str(), s.x, s.y, s.z);
		} else {
			std::snprintf(buf, sizeof buf, "%s", drag_note_.c_str());
		}
	} else {
		return false;
	}
	text = buf;
	sx = mx_ + 18, sy = my_ + 14;
	return true;
}

std::string Session::hint() const {
	if (looking_) {
		return "Looking: W A S D to fly, Q and E down and up, Shift faster, Ctrl slower";
	}
	if (!drag_note_.empty()) {
		return drag_note_ + (drag_ == Drag::Move ? " \xc2\xb7 Alt: up and down \xc2\xb7 Ctrl: off the grid \xc2\xb7 Esc: cancel" : " \xc2\xb7 Ctrl: off the grid \xc2\xb7 Esc: cancel");
	}
	if (drag_ == Drag::Box) {
		return "Selects what's wholly inside the box \xc2\xb7 Shift adds, Ctrl toggles";
	}
	if (tool == Tool::Create) {
		if (create_state_ == 1) {
			return "Drag the footprint \xc2\xb7 let go to set the height";
		}
		if (create_state_ == 2) {
			Vec3 lo, hi;
			create_box(create_a_, create_b_, create_axis_, create_plane_, create_sign_, create_height_, lo, hi);
			const Vec3 s = hi - lo;
			char buf[200];
			std::snprintf(buf, sizeof buf, "%g x %g x %g \xc2\xb7 move the mouse to set the height \xc2\xb7 click or Enter makes it \xc2\xb7 Esc cancels", s.x, s.y, s.z);
			return buf;
		}
		return "Create brush: drag on a surface or the grid to draw a footprint (" + shader + ") \xc2\xb7 Esc: back to select";
	}
	if (tool == Tool::Faces) {
		return faces_hint();
	}
	if (tool == Tool::Place) {
		return place_hint();
	}
	if (tool == Tool::Clip) {
		if (hover_handle_.kind == Handle::ClipPoint) {
			return "Drag to move this point";
		}
		if (clip_pts_.empty()) {
			return sel.empty() ? "Clipper: click on a brush to put the first point" : "Clipper: click on the selection to put the first point";
		}
		if (clip_pts_.size() == 1) {
			return "Click a second point: the cut runs through both, straight into the face";
		}
		static const char *sides[3] = {"the far side", "the near side", "both sides"};
		return std::string("Enter cuts, keeping ") + sides[clip_keep_] + " (Tab changes it) \xc2\xb7 a third point tilts the cut \xc2\xb7 Esc clears";
	}
	switch (hover_handle_.kind) {
	case Handle::Corner:
		return "Drag: move this corner (Alt up and down, Ctrl off the grid) \xc2\xb7 click: select corners (Shift adds, Ctrl toggles) \xc2\xb7 Del removes them";
	case Handle::Edge:
		return "Drag: move this edge (Alt up and down, Ctrl off the grid) \xc2\xb7 click: select its corners";
	case Handle::Face:
		return "Drag: push or pull this face (Ctrl off the grid)";
	default:
		break;
	}
	if (!corners.empty()) {
		return "Drag a corner to reshape \xc2\xb7 the arrows nudge the selected corners \xc2\xb7 Del removes them \xc2\xb7 Esc keeps the brush, drops the corners";
	}
	if (hover_ok_ && hover_.kind == Kind::Entity && doc.get(hover_.id)) {
		// what an entity is, from the catalog
		const std::string cn = doc.get(hover_.id)->entity.classname();
		const EntityDef *d = entity_def(cn);
		return cn + (d ? ": " + d->help : "") + (d && !d->mod.empty() ? " (" + d->mod + ")" : "") + " \xc2\xb7 click selects, drag moves";
	}
	if (ortho_axis_ >= 0) {
		const std::string view = std::string("The ") + pane_name(active_) + " view: ";
		if (!sel.empty()) {
			return view + "drag to move in its plane (Ctrl off the grid) \xc2\xb7 corners, edges and diamonds reshape \xc2\xb7 the right button drags the view, the wheel zooms";
		}
		return view + "click to select \xc2\xb7 drag on empty space to box select \xc2\xb7 the right or middle button drags the view, the wheel zooms \xc2\xb7 V: one view or four";
	}
	if (!sel.empty()) {
		return "Drag to move (Alt up and down, Ctrl off the grid) \xc2\xb7 corners, edges and diamonds reshape \xc2\xb7 Shift+drag a face to push it \xc2\xb7 R turns \xc2\xb7 Ctrl+D duplicates";
	}
	return "Click to select \xc2\xb7 drag to move \xc2\xb7 Ctrl+drag (or drag on empty space) to box select \xc2\xb7 B makes a brush \xc2\xb7 hold the right button to look and fly";
}

// ---------------------------------------------------------------- curves

void Session::curves_action(const std::string &what) {
	if (what == "more_columns" || what == "fewer_columns" || what == "more_rows" || what == "fewer_rows" || what == "caps" || what == "thicken") {
		std::vector<Id> curves;
		for (Id id : sel) {
			if (doc.get(id) && doc.get(id)->kind == Kind::Patch) {
				curves.push_back(id);
			}
		}
		if (curves.empty()) {
			say("Select curves first");
			return;
		}
		const bool rows = what.find("rows") != std::string::npos, more = what.compare(0, 4, "more") == 0;
		std::vector<Id> keep = curves;
		int changed = 0;
		doc.begin(what == "caps" ? "Cap curves" : what == "thicken" ? "Thicken curves" : more ? "More sections" : "Fewer sections");
		for (Id id : curves) {
			const Object &o = *doc.get(id);
			if (what == "caps" || what == "thicken") {
				// new pieces beside it, in its entity
				for (auto &piece : what == "caps" ? curve_caps(doc, o) : curve_thick(doc, o, grid)) {
					piece->owner = o.owner;
					keep.push_back(doc.add(piece));
					changed++;
				}
			} else if (auto n = more ? curve_split(o, rows) : curve_merge(o, rows)) {
				doc.put(id, n);
				changed++;
			}
		}
		doc.end();
		if (!changed) {
			say(what == "caps" ? "Caps fit a ring of 9 control points (a cylinder's ends) or a half ring of 5 (an arch's)"
				: more ? "A curve takes 31 control points a side at most" : "Fewer sections needs an even count of them (two or more)", true);
			return;
		}
		corners.clear();
		select(keep, SelMode::Set);
		if (what == "thicken") {
			say("Thickened " + std::to_string(curves.size()) + (curves.size() == 1 ? " curve " : " curves ") + number(grid) + " deep (the grid): its other skin and the seams");
		}
		return;
	}
	if (what == "invert") {
		std::vector<Id> curves;
		for (Id id : sel) {
			if (doc.get(id) && doc.get(id)->kind == Kind::Patch) {
				curves.push_back(id);
			}
		}
		if (curves.empty()) {
			say("Select curves to turn inside out");
			return;
		}
		doc.begin("Invert curve");
		for (Id id : curves) {
			doc.put(id, inverted(*doc.get(id)));
		}
		doc.end();
		return;
	}
	static const std::pair<const char *, CurveShape> shapes[] = {{"cylinder", CurveShape::Cylinder}, {"cone", CurveShape::Cone},
		{"arch", CurveShape::Arch}, {"bevel", CurveShape::Bevel}, {"plane", CurveShape::Plane}};
	const CurveShape *shape = nullptr;
	for (const auto &s : shapes) {
		if (what == s.first) {
			shape = &s.second;
		}
	}
	std::vector<Id> brushes;
	for (Id id : sel) {
		if (doc.get(id) && doc.get(id)->kind == Kind::Brush) {
			brushes.push_back(id);
		}
	}
	if (!shape || brushes.empty()) {
		say("Select a brush first: the curve fills its box");
		return;
	}
	// each brush gives its box and its entity; the curve takes the current material
	std::vector<Id> made;
	doc.begin("Make " + what);
	for (Id id : brushes) {
		const Id owner = doc.get(id)->owner;
		auto c = curve_in_box(doc, *shape, bounds_of(doc, id), ortho_axis_ >= 0 ? ortho_axis_ : 2, shader_id(shader), owner);
		if (c) {
			made.push_back(doc.add(c));
			doc.remove(id);
		}
	}
	doc.end();
	corners.clear();
	select(made, SelMode::Set);
	say("Made " + std::to_string(made.size()) + (made.size() == 1 ? " curve" : " curves") + ": drag its control points to shape it, Invert curve shows its other side");
}

}  // namespace oz::ed
