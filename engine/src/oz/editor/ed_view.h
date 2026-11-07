#pragma once
// The map editor's session (ENGINE_PLAN.md P13, engine/EDITOR.md): the open map, its 3D view, the
// camera, the selection and the tools. The native UI's page (oz_ui_editor.cpp) hands it the view's
// rectangle, the pointer, the buttons and the keys.
//   ed_view.cpp    the camera, the meshes, the grid, files
//   ed_select.cpp  picking, the selection, the select tool and its handles
//   ed_tools.cpp   the create and clip tools, the actions (keys, buttons, ed <action>)
//   ed_faces.cpp   the faces tool, texture mapping, the eyedropper and painting, entity keys, history
//   ed_entities.cpp  entities drawn (models, links, jump pad arcs), the place tool, jump pads, teleporters
//   ed_compile.cpp   compiling (q3map2, mbspc) in the background, leaks, play-tests

#include "ed_doc.h"
#include "ed_engine.h"
#include "ed_entdefs.h"
#include "ed_mapping.h"
#include "ed_ops.h"
#include "ed_shaders.h"

#include <functional>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace oz::ed {

struct Camera {
	Vec3 origin{-640, 0, 320};
	double yaw = 0, pitch = 20;  // degrees, as id's angles (pitch down is positive)
	double fov = 90;             // across
	void axes(Vec3 &forward, Vec3 &left, Vec3 &up) const;
};

// The panes (ed_panes.cpp): the 3D view, and the 2D views straight down each axis, as in Radiant: top
// (X across, Y up), front (X across, Z up), side (Y across, Z up).
enum class Pane { Cam, Top, Front, Side };
struct View2D {
	Vec3 center;       // the map point in the pane's middle
	double scale = 2;  // map units a pixel
};
const char *pane_name(Pane p);  // "3D", "top", "front", "side"
bool pane_by_name(const std::string &name, Pane &p);

// The document's brushes and curves as renderer meshes: one per 1024-unit cell of the map and shader
// set, rebuilt when something in the cell changes. include picks the objects this one draws (the
// world's mesher everything but the selection, the selection's the selection); with overlay, faces
// also get the selection's tint and edges; with wire, there are only lines: each brush's edges, each
// curve's rows (the 2D views').
class Mesher {
public:
	std::function<bool(Id)> include;
	bool overlay = false;
	bool wire = false;
	const unsigned char *wire_color = nullptr;  // wire's lines all in one color (else by owner)
	// unset, or true for an object: its faces in their materials; false: its tint and edges alone (the lit
	// preview's selected objects the compile already has)
	std::function<bool(Id)> materials;

	void changed(const Doc &doc, const std::vector<Id> &ids);  // empty: everything
	void flush(const Doc &doc);                                 // rebuild what changed
	void draw(const float origin[3], const float axis[3][3]) const;
	void reset();                                               // the renderer restarted: every mesh again
	int meshes() const;
	int triangles() const;

private:
	using Key = std::tuple<int, int, int>;
	struct Cell {
		std::vector<int> meshes;  // model handles
		int lines = 0;            // the overlay's edges
		std::vector<Id> ids;
		int triangles = 0;
		bool dirty = true;
	};
	struct Material {
		int shader = 0;  // a renderer handle: the face's shader, or one of the editor's looks
		unsigned char color[4] = {255, 255, 255, 255};
	};
	Key key_of(const Doc &doc, Id id) const;
	void build(const Doc &doc, Cell &cell);
	void build_wire(const Doc &doc, Cell &cell);
	const Material &material(ShaderId s);

	std::map<Key, Cell> cells_;
	std::unordered_map<Id, Key> where_;
	std::unordered_map<ShaderId, Material> materials_;
	std::vector<int> spare_;  // freed meshes' handles, for the next build
};

// Lines and triangles the editor draws over the map (entity boxes, outlines, handles, previews), in
// the editor's looks; built anew with begin() ... end().
class Overlay {
public:
	void begin();
	void line(const Vec3 &a, const Vec3 &b, const unsigned char c[4], const char *look = "$oz_ed_lines");
	void tri(const Vec3 &a, const Vec3 &b, const Vec3 &c, const unsigned char col[4], const char *look = "$oz_ed_ghost");
	void quad(const Vec3 &a, const Vec3 &b, const Vec3 &c, const Vec3 &d, const unsigned char col[4], const char *look = "$oz_ed_ghost");
	void box_edges(const Vec3 &lo, const Vec3 &hi, const unsigned char c[4], const char *look = "$oz_ed_lines");
	void box_faces(const Vec3 &lo, const Vec3 &hi, const unsigned char c[4], const char *look = "$oz_ed_ghost");
	void polygon(const Winding &w, const unsigned char c[4], const char *look = "$oz_ed_ghost");
	void outline(const Winding &w, const unsigned char c[4], const char *look = "$oz_ed_lines");
	void end();
	void draw(const float origin[3], const float axis[3][3]) const;
	void reset();
	bool empty() const { return !tri_used_ && !line_used_; }

private:
	struct Part {
		std::vector<ozEdVertex_t> v;
		std::vector<unsigned short> i;
	};
	unsigned short vert(Part &p, const Vec3 &at, const unsigned char c[4]);
	std::map<std::string, Part> tris_, lines_;
	int tri_mesh_ = 0, line_mesh_ = 0;
	bool tri_used_ = false, line_used_ = false;
	uint64_t tri_hash_ = 0, line_hash_ = 0;  // what's uploaded: an unchanged overlay isn't sent again
};

enum class Tool { Select, Create, Clip, Faces, Place };
enum class SelMode { Set, Add, Toggle, Remove };

// What a ray from the view meets first.
struct Hit {
	Id id = 0;
	Kind kind = Kind::Brush;
	int face = -1;  // a brush's face
	double t = 0;
	Vec3 point, normal;
};

// A handle on the selection, drawn over the view: a brush's corner, an edge's middle, a face's diamond
// on its stem, or one of the clipper's points.
struct Handle {
	enum Kind { None, Corner, Edge, Face, ClipPoint, GroupFace } kind = None;  // GroupFace: a side of a group's box
	Id id = 0;
	int a = -1, b = -1;  // the corner, the edge's two corners, the face, the clip point
	Vec3 at;             // where it's drawn (a face's: the diamond's tip)
	Vec3 base, normal;   // a face's middle and its normal
	bool same(const Handle &o) const { return kind == o.kind && id == o.id && a == o.a && b == o.b; }
};

// One thing the editor does: its keys and buttons, `ed <name>`, and later the command search.
struct Action {
	const char *name;   // "rotate_left"
	const char *label;  // "Turn left"
	const char *keys;   // "R"
	const char *help;   // what it does, for the status line
};
const std::vector<Action> &actions();
const Action *find_action(const std::string &name);
std::string combo(int key, int mods);  // "Ctrl+Shift+R"; "" for a key the editor doesn't name (ed_keys.cpp)
bool runs_alone(const Action &a);      // listed by the search and the Keys dialog: needs nothing typed

class Session {
public:
	Doc doc;
	std::string path;   // the .map file ("" for a new map)
	std::string name;   // as shown ("untitled")
	Camera cam;
	double grid = 16;                             // any power of two, 1/8 to 4096
	double fly_scale = 1;                         // the fly keys' speed, times 640 a second (the wheel while looking)
	static constexpr double MAP_LIMIT = 8192;     // a map stays within this of the middle every way
	Tool tool = Tool::Select;
	bool texture_lock = true;                     // moves, turns and mirrors carry the textures along
	std::string shader = "base_wall/concrete";    // what new brushes are made of
	std::set<Id> sel;                             // the selected objects
	uint64_t sel_rev = 0;                         // bumps when the selection changes
	std::set<std::pair<Id, int>> corners;         // selected corners of selected brushes
	std::set<FaceRef> faces;                      // the faces tool's faces (the objects' selection is empty meanwhile)
	std::string place_class;                      // what the place tool puts down: a class, "jump_pad" or "teleporter"
	std::function<void(const std::string &, bool error)> notify;

	Session();
	// The view's rectangle in window pixels; moves the camera, rebuilds what changed, draws.
	void frame(int x, int y, int w, int h);
	// Keys and buttons (true: taken). over: the pointer is over the view.
	bool key(int key, bool down, bool over);
	void pointer(int x, int y);  // window pixels
	void look(int dx, int dy);   // the mouse while looking
	bool looking() const { return looking_; }
	bool dragging() const { return drag_ != Drag::None; }

	// ed <action> [args]: false when there's no such action
	bool run(const std::string &action, const std::vector<std::string> &args);

	bool open(const std::string &file, std::string &error);
	bool save(const std::string &file, std::string &error);
	// A map in the player's paks, decompiled by q3map2 into the editor's import folder, then opened. In
	// the background (the window keeps drawing, the status line counts): true when it started; wait
	// (the console's, the tests'): done before it returns.
	bool import_bsp(const std::string &map, std::string &error, bool wait = false);
	bool importing() const { return import_.proc != 0; }
	void new_map();
	void frame_all();       // the camera onto the whole map
	void frame_selection();
	Bounds map_bounds() const;  // where players are (spawns, pickups), else everything

	// ---- what shows (ed_show.cpp): objects hidden (out of every view) or locked (drawn, never picked),
	// and the filters that leave whole kinds out; the view's alone (the map compiles and saves whole)
	enum Filter : unsigned { HIDE_TOOLS = 1, HIDE_TRIGGERS = 2, HIDE_SKY = 4, HIDE_ENTITIES = 8, HIDE_CURVES = 16 };
	unsigned filters = 0;
	std::set<Id> hidden, locked;
	uint64_t show_rev = 0;          // bumps when any of them changes
	bool visible(Id id) const;      // drawn: not hidden (nor its entity), not filtered out
	bool pickable(Id id) const;     // visible and not locked: a click, a box, Select all take it
	void hide_selection(bool others);  // the selection hidden, or everything else
	void show_all();
	void lock_selection();
	void unlock_all();
	void toggle_filter(unsigned f);
	// Prefabs: the selection saved as a .map of its own (<fs_homepath>/oz/editor/prefabs/<name>.map),
	// put back in any map where the pointer is, as Paste puts the clipboard
	bool prefab_save(const std::string &name);
	bool prefab_insert(const std::string &name);
	std::vector<std::string> prefab_names() const;
	bool prefab_delete(const std::string &name);  // its .map and its thumbnails
	// A prefab's 3D thumbnail, as the UI names it ("/editor/prefabs/<name>.<stamp>.jpg"), "" while it has
	// none; each save draws a new one (prefab_thumb_tick), and asking for a prefab without one queues it.
	std::string prefab_thumb(const std::string &name);
	int prefab_rev = 0;             // bumps when one is saved, deleted or gets its thumbnail
	void group_selection();         // the selected brushes and curves made a func_group
	void ungroup_selection();       // the selected groups' brushes back in the world

	// ---- the panes (ed_panes.cpp): the 3D view alone or four (3D, top, front, side). The tools work in
	// the pane the pointer is over, a drag in the one it began in.
	int layout = 1;          // 1 or 4
	Pane solo = Pane::Cam;   // the pane the one-pane layout shows
	View2D views2d[3];       // top, front, side
	Pane active_pane() const { return active_; }
	void frame_2d(const Bounds &b);  // the 2D views onto b

	// ---- the selection
	void select(const std::vector<Id> &ids, SelMode mode);
	void select_none();
	std::vector<Id> selected() const { return std::vector<Id>(sel.begin(), sel.end()); }
	// What a click takes: a brush of a group or a brush entity takes the whole entity, the rest itself.
	std::vector<Id> group_of(Id id) const;
	Bounds selection_bounds() const;
	// What the mapping, painting and the inspector act on: the faces tool's faces, else every face of
	// the selected brushes (and the selected curves, for painting).
	std::vector<FaceRef> target_faces() const;
	std::vector<Id> target_patches() const;
	// The entities the inspector's keys are for: the selected entities and the selected brushes' and
	// faces' entities; with nothing selected, the world (the map's own keys).
	std::vector<Id> key_entities() const;

	// ---- the view
	void ray(double sx, double sy, Vec3 &o, Vec3 &d) const;           // window pixels
	bool project(const Vec3 &p, double &sx, double &sy) const;         // false: behind the camera
	double units_per_pixel(const Vec3 &at) const;
	bool pick(double sx, double sy, Hit &hit, bool entities = true) const;
	std::vector<Hit> pick_all(double sx, double sy, bool entities = true) const;  // nearest first
	// What a placement lands on: the nearest face a player could stand on, passing through triggers, tool
	// volumes (hint, fog ...) and non-solid materials; entities too unless asked for.
	bool pick_floor(double sx, double sy, Hit &hit, bool entities) const;

	std::string status() const;        // the status line's right side
	std::string hint() const;          // its left side: what the pointer and the tool can do now
	// the size tag by the pointer while a brush is drawn or a drag runs (window pixels): false when none
	bool size_tag(std::string &text, double &sx, double &sy) const;
	std::string render_stats() const;  // the view's meshes and triangles (ed_stats)
	bool renderer_ok() const;          // the renderer can draw the view (only the oz renderer can)
	void load_defs();                  // the mods' .def files into the entity catalog, once

	// ---- compiling (ed_compile.cpp): the map as it is, into maps/ed_<name>.bsp in the player's files.
	// full: BSP, vis, full light and the bots' file, else BSP and quick light; play: then a devmap of
	// it, here: with the player's eye where the camera is. Polled each frame; a leak stops it.
	bool compile(bool full, bool play, bool here, bool defrag = false);  // defrag: played under Defrag
	void compile_poll();
	void compile_stop();
	void compile_wait();                      // until it's done (the tests)
	bool compiling() const { return job_.running; }
	std::string compile_status() const;       // "Compiling ed_room: light, 3 s"; "" when idle
	const std::string &compile_report() const { return compile_report_; }  // the last: "ok ...", "leak N", "failed: ..."
	std::string compile_name() const;         // ed_<name>
	bool leaking() const { return !leak_.empty(); }
	void frame_leak();
	// The last compile's warnings, each stage's, alike ones counted once; ids: what each is about (the
	// brush q3map2 names, the brushes with a material it couldn't find), framed by ed warning <i>.
	struct CompileWarning {
		std::string text;
		std::vector<Id> ids;
		int count = 1;
	};
	const std::vector<CompileWarning> &compile_warnings() const { return warnings_; }
	bool want_warnings = false;  // the page opens their list

	// ---- Defrag (ed_defrag.cpp): its course, checked; its map info file; play-tests under it
	void defrag_check();  // the course's problems in the warnings' list, each framed when picked
	// "starts N checkpoints N finishes N problems N", or "no course"; problems: each, as a warning
	std::string course_summary(std::vector<CompileWarning> *problems = nullptr) const;
	std::string defrag_defi(const std::string &map) const;  // scripts/<map>.defi, Defrag's map info
	void write_defi_beside(const std::string &mapfile) const;  // a saved course's .defi (a maps folder's: scripts/ beside it)
	bool defrag_ready(std::string &why) const;  // Defrag's game code where a switch to it finds it
	// ---- keys (ed_keys.cpp): the actions' keys, the player's own over the table's (keymap.cfg)
	std::string keys_of(const Action &a);
	void bind(const std::string &action, const std::string &keys);  // its keys; each combo taken from any other
	void keymap_preset(const std::string &name);                    // "oz" (the table's), "radiant"
	bool bind_key(int key, int mods);  // while binding: the keys pressed (true: taken)
	std::string binding;               // the action waiting for its new keys, "" none
	int keymap_rev = 0;                // counts the keymap's changes (the page shows keys anew)
	bool want_search = false, want_keymap = false;  // the page opens them
	// Sky and sun (the map's worldspawn keys, _oz_sky ...; ed_compile.cpp): the sky the compile boxes the
	// map in, and the light q3map2 gives it. The page's dialog shows them while sky_open.
	struct SkySun {
		std::string sky;      // the sky box's: chosen, else the first sky painted on a brush, else DEFAULT_SKY
		bool chosen = false;  // the map's _oz_sky
		std::string painted;  // the first skies/ material on a brush, "" none
		bool own = false;     // lit as the sky's own script lights it, not by the settings below
		double color[3] = {1, 1, 1};
		double brightness = 100, height = 60, direction = 30, sky_light = 80;
		double glow[3] = {1, 1, 1};     // the sky's glow: the sun's color unless set apart
		bool glow_set = false;
		double ambient = 0;             // light everywhere before the lights (worldspawn "ambient")
		double ambient_color[3] = {1, 1, 1};  // its color (worldspawn "_color")
		// Fill light (worldspawn _oz_fill: auto, on, off): q3map2's floodlight, a soft even light everywhere,
		// darker in corners. Auto turns it on for a map without lamps (a map pack's: q3map2 drops its light
		// entities from the .bsp, so an imported one comes back with none and compiled pitch dark).
		std::string fill = "auto";
		bool lamps = false;    // the map has light entities
		bool fill_on = false;  // the compile adds it
	};
	static constexpr const char *DEFAULT_SKY = "skies/hellsky2";
	SkySun sky_sun() const;
	bool want_sky = false, sky_open = false;
	bool want_uv = false;                           // the UV editor (the page's)
	// What the page does for an action of its own: "open" and "import" (their lists), "save_as" (the
	// name), "leave" (the main menu), "guide", "menu file" (Escape with nothing to cancel), "panel <name>"
	std::string want_page;

	// ---- files (ed_files.cpp): the map written aside a while after it changes, offered back into a
	// session that hasn't touched a map yet; a question before New throws unsaved changes away
	void autosave_tick();
	bool recovery_offer(std::string &what);  // what was kept: its name and how long ago
	bool recover();
	void discard_recovery();
	bool ask_first(const std::string &action);  // true: unsaved changes, asking (Save, Discard or Cancel)
	void answer(const std::string &choice);     // "save", "discard", "cancel"
	std::string asking;                         // the action waiting on the answer ("new", "open\t<file>", "import\t<map>"), "" none
	std::string maps_dir() const;               // <fs_homepath>/oz/editor/maps, where Save puts a new map
	void load(const std::string &asked);        // "open\t<file>" or "import\t<map>": opened, or why not said
	bool saved_as(const std::string &name);     // Save As under that name (the maps folder); then what waited on it
	void save_as_cancelled() { after_save_.clear(); }

	// The lit preview (F6): the last compile drawn as the world, its light and all; what changed since
	// is drawn by the editor over it. Compiles first when the map changed since.
	void toggle_preview();
	bool previewing() const;
	bool changed_since(Id id) const;  // since the compile the preview shows
	int changes_since_compile() const;
	// While it shows: a light changed, added or taken out since compiles again on its own, a moment
	// after the last change (not mid-drag), so its color, intensity and range show as Play shows them.
	void preview_tick();
	bool lights_changed_since() const;
	// Test hooks: modifiers as if held (-1: the keyboard's), a button at a spot.
	void force_mods(int mods) { forced_mods_ = mods; }
	enum { SHIFT = 1, CTRL = 2, ALT = 4 };

private:
	enum class Drag { None, Pending, Move, Box, Push, Corners, Pan, Footprint, ClipPoint };
	struct Shape {  // a selected brush's corners, edges and faces, cached until it changes
		ObjectPtr of;
		std::vector<Vec3> corners;
		std::vector<std::pair<int, int>> edges;
		std::vector<Vec3> centers;  // per face, its polygon's middle (faces without one: none)
		std::vector<int> faces;     // the faces with a polygon
	};

	void attach();
	void changed(const std::vector<Id> &ids);  // the document changed (or a live drag changed it)
	int mods() const;
	void say(const std::string &text, bool error = false);
	void button(int b, bool down);
	void press(int mods);
	void release(int mods);
	void motion(int mods);
	void click(int mods, bool twice);
	void cancel_drag();
	bool cancel();  // Escape
	const Shape &shape(Id id);
	std::vector<Handle> handles();
	Handle handle_at(double sx, double sy);
	bool plane_point(double sx, double sy, const Vec3 &at, const Vec3 &n, Vec3 &out) const;
	double snap(double v) const { return std::round(v / grid) * grid; }
	Vec3 snap_vec(const Vec3 &v) const { return Vec3(snap(v.x), snap(v.y), snap(v.z)); }
	Vec3 camera_axis(bool right) const;

	// the select tool's drags
	void start_move(const Hit &hit, int mods);
	void move_plane(bool vertical);
	void start_push(const std::vector<std::pair<Id, int>> &faces, const Vec3 &grab, const Vec3 &n, const char *label);
	// A group's side dragged: every selected brush's face that way pushed out (or in) together, as one face's push
	void start_group_push(const Handle &h);
	bool group_handles() const;  // two brushes selected or more: one diamond per side of their box, not each brush's handles
	void start_corners(const Handle &h, int mods);
	void drag_corners(const Vec3 &d);
	void reselect_corners(const Vec3 &d);
	void box_select(int x0, int y0, int x1, int y1, int mods);
	void delete_corners();
	void nudge(const Vec3 &d);
	// the create and clip tools
	void create_press(int mods);
	void create_motion();
	bool create_commit();
	void clip_click(int mods);
	bool clip_plane(Plane &p) const;
	bool clip_apply();
	// the actions
	void transform_selection(const Affine &xf, const char *label, bool lock);
	void rotate(int axis, double degrees);
	void flip(int axis);
	void resize(const Vec3 &size);
	void duplicate();
	void copy();
	void paste();
	void paste_clip(const Clip &clip, const std::string &label);
	void remove_selection();
	void set_tool(Tool t);
	// the faces tool and the mapping (ed_faces.cpp)
	bool faces_action(const std::string &action, const std::vector<std::string> &args);
	void curves_action(const std::string &what);  // ed curve_<cylinder|cone|arch|bevel|plane|invert>
	void select_faces(const std::vector<FaceRef> &refs, SelMode mode);
	void faces_click(int mods, bool twice);
	bool faces_drag();
	bool faces_key(int key, int mods);
	void eyedrop(const Hit &hit);
	void paint_face(const Hit &hit);
	void map_targets(const MapEdit &e, const std::string &label);
	void tex_nudge(double dx, double dy, double step);
	void tex_turn(double degrees);
	std::string faces_hint() const;
	// entities (ed_entities.cpp)
	bool entities_action(const std::string &action, const std::vector<std::string> &args);
	int model_handle(const std::string &name);
	void collect_models();
	void draw_models(const float moved[3], int now_ms);
	void build_links();
public:
	// A pusher's throw (trigger_push, target_push): its launch speed, how high it goes over where it
	// starts, how far across it lands, and its time in the air; false without a target to throw at.
	bool push_flight(Id id, double &speed, double &height, double &across, double &seconds);
	bool push_lands(Id id);       // its throw comes down on a brush (not into the void, nor past 6 s)
	// The selected pushers' targets raised (lower: negative), the throw still landing where it did.
	void push_height(double dz);
	// Why a pusher does nothing (or harm) in the game, "" when it's fine: no target, a target that isn't
	// there, a target under it, a throw that never lands.
	std::string push_problem(Id id);
	std::vector<Id> pushers_at(const std::string &targetname) const;  // the pushers aiming at it
	void push_land();             // the place tool: the next click is where the selected pads' throws land
	// ---- movers (ed_movers.cpp): doors, lifts, buttons, trains, rotating, bobbing and swinging brushes as
	// id's maps have them; where each moves is drawn for the selected ones, and played with mover_play
	struct DoorState {
		bool any = false, near = false, shot = false, near_mixed = false, shot_mixed = false;
		std::string team;
		int buttons = 0;  // what fires them besides their own zone
	};
	DoorState door_state();               // the selected doors'
	void make_door(bool pair);            // the selected brushes a door, or cut in two a pair sliding apart
	void make_lift();                     // the selected brushes a func_plat
	void door_opens(int near, int shot);  // 1 on, 0 off, -1 as it is
	bool mover_play = false;
	// What would trouble the map in the game, found before it's compiled: brushes left without collision
	// by a non-solid face, jump pads that do nothing, targets that name nothing. Each with what it's about.
	std::vector<CompileWarning> map_problems();
	void check_map();             // map_problems into the warnings' list, said
private:
	std::vector<Id> land_pushers_;  // push_land's pads
	Id make_mover(const std::vector<Id> &brushes, const std::string &cls, const Keys &keys);
	void add_pivot(Id ent);
	bool movers_action(const std::string &action, const std::vector<std::string> &a);
	bool mover_click(const Hit &h);
	std::string mover_hint() const;
	void mover_problems(std::vector<CompileWarning> &out);
	void build_movers();
	Overlay movers_;
	uint64_t movers_doc_ = ~0ull, movers_sel_ = ~0ull;
	int mover_t0_ = 0;
	std::vector<Id> path_corners_;  // the train path tool's corners so far
	Id path_train_ = 0;
	std::string mover_target_;      // the button tool's door
	std::unordered_map<Id, bool> arc_lands_;
	const std::vector<Vec3> &push_arc(Id entity);
	bool place_point(const Hit &hit, const std::string &cls, Vec3 &origin) const;
	void place_click(int mods);
	void build_place_preview();
	Id place_entity(const std::string &cls, const Vec3 &origin, const char *label);
	Id place_defrag_line(const std::string &kind, const Vec3 &a, const Vec3 &b);  // defrag_start, _checkpoint, _finish
	Id place_defrag_speed(const Vec3 &at);                       // a speed pad
	Id place_defrag_shooter(const Vec3 &from, const Vec3 &zone);  // a shooter aimed at the player, and its zone
	Id place_brush_entity(const std::string &cls, const Vec3 &lo, const Vec3 &hi, const std::string &shader);
	void make_brush_entity(const std::string &cls);
	double ceiling_over(const Vec3 &a, const Vec3 &b) const;
	std::string place_hint() const;

	// the panes
	struct Rect {
		int x = 0, y = 0, w = 0, h = 0;
	};
	Rect panes_[4];
	Pane active_ = Pane::Cam;
	int ortho_axis_ = -1;             // the active pane's view axis (a 2D view's), -1 for the 3D view
	double half_w_ = 1, half_h_ = 1;  // a 2D pane's half size in map units
	Vec3 eye_;                        // where the active pane's rays start (a 2D view's: far back)
	int view_w_ = 1, view_h_ = 1;     // the whole viewport
	Bounds frame2d_later_;            // framed before the viewport had a size: done at the first frame
	Vec3 work_lo_{0, 0, 0}, work_hi_{128, 128, 128};  // the last brush made: a 2D create's depth
	void lay_out(int x, int y, int w, int h);
	bool pane_at(int x, int y, Pane &p) const;
	void use_pane(Pane p);
	View2D &view2d();  // the active 2D pane's
	void draw_pane(Pane p, int now);
	void start_pan(int button);
	void pan_to(int x, int y);
	void zoom_2d(int steps);
	void build_grid2d(Pane p);

	// drawing
	void build_entities();
	// what shows
	mutable std::unordered_map<ShaderId, Look> looks_;
	Look look_of(ShaderId s) const;
	bool filtered(Id id, const Object &o) const;
	void shown_changed();
	std::vector<Id> selection_groups() const;
	void build_hover();
	void build_handles(Overlay &out);
	void build_preview();
	void build_faces();

	Mesher world_, chosen_, wire_, wire_sel_;
	Overlay ents_, ents_sel_, hover_lines_, pane_handles_[4], grid2d_[3], preview_, faces_lines_;
	// the World tab live in the 3D view: its sky's picture on a box around the camera, and the view's light
	// from its sun, sky light, ambient and fill (live_world, each frame; worked out again as the map changes)
	void live_world();
	int sky_mesh_ = 0;
	std::string sky_mesh_image_;
	uint64_t live_rev_ = ~0ull;
	float live_ambient_[3] = {90, 90, 96}, live_directed_[3] = {150, 146, 138}, live_dir_[3] = {0.4f, 0.25f, 0.88f};
	// prefab thumbnails (ed_show.cpp): the prefab read into a document of its own and drawn alone, framed,
	// in a square in the 3D view's middle for a few frames, while a screenshot takes it
	void prefab_thumb_tick(int x, int y, int w, int h, int now);
	std::vector<std::string> thumb_queue_;
	std::set<std::string> thumb_tried_;  // asked for this run: one try each, even if it fails
	std::string thumb_name_;
	std::shared_ptr<Doc> thumb_doc_;
	Mesher thumb_mesh_;
	Overlay thumb_ents_;
	Camera thumb_cam_;
	int thumb_state_ = 0, thumb_frames_ = 0;  // 0 idle, 1 drawing until the screenshot lands
	int thumb_rect_[4] = {0, 0, 0, 0};
	uint64_t faces_doc_rev_ = ~0ull, faces_sel_rev_ = ~0ull;
	int generation_ = -1;
	int grid_mesh_ = 0;
	double grid_mesh_step_ = 0;  // the grid the 3D view's ground lines were made for
	int last_ms_ = 0;
	bool looking_ = false;
	std::vector<Id> pending_;
	bool pending_all_ = true;
	uint64_t ents_rev_ = ~0ull, hover_rev_ = ~0ull;
	int64_t hover_built_ = -1;
	uint64_t hover_doc_rev_ = 0, hover_sel_rev_ = 0;
	int hover_tool_ = -1;
	bool ents_dirty_ = true;

	// the view as last drawn
	int vx_ = 0, vy_ = 0, vw_ = 1, vh_ = 1;
	double tan_x_ = 1, tan_y_ = 1;
	Vec3 fwd_{1, 0, 0}, left_{0, 1, 0}, up_{0, 0, 1};
	int mx_ = -10000, my_ = -10000;  // the pointer, window pixels
	bool over_ = false;              // it's over the view
	int forced_mods_ = -1;

	// a drag
	Drag drag_ = Drag::None;
	int press_x_ = 0, press_y_ = 0, press_mods_ = 0, press_button_ = 0;
	Hit press_hit_;
	bool press_hit_ok_ = false;
	Handle press_handle_;
	Vec3 anchor_, plane_n_, base_, min_, delta_;
	bool vertical_ = false;
	std::vector<std::pair<Id, int>> push_faces_;
	Vec3 push_grab_, push_n_;
	double push_d0_ = 0, push_dist_ = 0;
	bool push_grid_ = false;
	std::unordered_map<Id, ObjectPtr> start_;      // objects before a live drag changed them
	std::map<Id, std::vector<Vec3>> corner_pts_;   // their corners then
	std::map<Id, std::vector<int>> corner_moving_;
	Vec3 corner_grab_;
	double pan_depth_ = 512;
	Vec3 pan_origin_;

	// the pointer's last clicks: a double-click, a click again on the same spot (the next object back)
	int last_click_ms_ = 0, last_click_x_ = -10000, last_click_y_ = -10000, cycle_ = 0;
	Hit hover_;
	bool hover_ok_ = false;
	Handle hover_handle_;
	std::unordered_map<Id, Shape> shapes_;
	mutable std::unordered_map<Id, std::pair<ObjectPtr, q3map::PatchMesh>> patch_cache_;  // picking's

	// the create tool
	int create_state_ = 0;  // 0 idle, 1 footprint, 2 height
	int create_axis_ = 2;
	double create_sign_ = 1, create_plane_ = 0, create_height_ = 64;
	Vec3 create_a_, create_b_;
	// the clipper
	std::vector<Vec3> clip_pts_, clip_normals_;
	int clip_keep_ = 1;  // 0 behind, 1 in front, 2 both
	int clip_drag_ = -1;
	Plane last_clip_;

	Clip clipboard_;
	std::string drag_note_;  // the drag's numbers, for the hint
	// entities: their models (refreshed with the boxes), links and arcs
	struct EntModel {
		int model;
		Vec3 at;
		double yaw, scale;
		bool item;
		Id id;
		std::string fighter;  // a spawn point: drawn as this fighter ("sarge/default"), model unused
	};
	std::vector<EntModel> models_;
	std::unordered_map<std::string, int> model_handles_;  // since the renderer started
	Overlay links_, place_preview_;
	uint64_t links_doc_ = ~0ull, links_sel_ = ~0ull;
	std::unordered_map<Id, std::pair<uint64_t, std::vector<Vec3>>> arcs_;  // a pusher's arc, by the document's revision
	bool defs_loaded_ = false;
	int place_step_ = 0;   // the jump pad and teleporter tools: 1 after the first click
	Vec3 place_first_;
	// a compile under way, its stages run one at a time
	struct CompileJob {
		struct Stage {
			std::string name, exe, args;
		};
		std::vector<Stage> stages;
		size_t at = 0;
		int proc = 0;
		std::string out, dir, map, log;
		int t0 = 0, stage_t0 = 0;
		bool running = false, full = false, play = false, here = false, preview = false, defrag = false;
		bool leaked = false;  // the BSP found a way out to the void: built and played without vis
		Camera cam;
		std::unordered_map<Id, ObjectPtr> snapshot;  // the document compiled
		std::vector<std::vector<Id>> order;          // as written: each entity, then its brushes and curves
	};
	std::unordered_map<Id, ObjectPtr> compiled_;  // the last good compile's document, and its name
	std::string compiled_name_;
	bool lit_ = false;
	uint64_t preview_rev_ = 0;  // the revision the lit preview last looked at
	int preview_due_ = 0;       // when it compiles again for a changed light (0: not waiting)
	std::map<std::string, std::string> keymap_;  // action -> keys, the player's own
	bool keymap_loaded_ = false;
	void load_keymap();
	void save_keymap();
	const Action *action_for(const std::string &combo);
	uint64_t autosave_rev_ = 0;  // the revision written aside
	int autosave_due_ = 0;
	// The map an earlier session left in the autosave, moved aside (recovered.map) the first time this
	// session looks, so this session's own autosaves never write over it; kept until answered.
	bool recovery_looked_ = false, recovery_kept_ = false;
	std::string recovery_what_;
	std::string after_save_;  // the action an unsaved map's question waits on while Save As asks the name
	void autosave_write();
	void autosave_clear();
	CompileJob job_;
	struct ImportJob {
		int proc = 0, t0 = 0;
		std::string map, out, log;
		uint64_t rev = 0;  // the map's revision when it started: changed since, it isn't opened over them
	} import_;
	void import_poll();
	bool import_finish(const std::string &map, const std::string &out, const std::string &log, int code, std::string &error);
	std::string compile_report_;
	std::vector<Vec3> leak_;  // q3map2's line from an entity out to the void
	uint64_t leak_rev_ = 0, leak_built_ = ~0ull;
	Overlay leak_lines_;
	Overlay sun_lines_;  // while the Sky and sun dialog is open: an arrow from the sun to the map's middle
	std::string sun_built_;
	void build_sun_lines();
	bool sky_action(const std::string &action, const std::vector<std::string> &a);
	void compile_stage();
	void compile_done(bool ok, const std::string &why);
	void collect_warnings(const std::string &log, const std::string &stage);
	std::vector<CompileWarning> warnings_;
	void build_leak();
	// the eyedropper's last face: Alt+Shift+click pastes its mapping with the material
	Face picked_;
	bool picked_ok_ = false;
	Vec3 picked_origin_;
};

Session &session();

// q3map2's detail flag, in a face's contents number: a detail brush doesn't split the BSP for visibility.
inline constexpr int32_t CONTENTS_DETAIL = 0x8000000;
// The editor's own: the brush has no collision (its switch in Inspect). Every material collides by default,
// liquids aside; the compile draws such a brush with its materials but makes it non-solid.
inline constexpr int32_t CONTENTS_OZ_NOCOLLIDE = 0x4;
// A face's color (Color in Inspect): its value number holds 0x01rrggbb; the texture is multiplied by it, in
// the view and in the game (the compile writes a copy of the material with a last stage of that color).
inline constexpr int32_t TINT_MARK = 0x01000000;
inline bool face_tint(const Face &f, unsigned char rgb[3]) {
	if (!f.has_flags || (f.value & 0x7f000000) != TINT_MARK) {
		return false;
	}
	rgb[0] = (unsigned char)((f.value >> 16) & 255), rgb[1] = (unsigned char)((f.value >> 8) & 255), rgb[2] = (unsigned char)(f.value & 255);
	return true;
}
inline bool is_nocollide(const Object &o) {
	if (o.kind != Kind::Brush || o.brush.faces.empty()) {
		return false;
	}
	for (const Face &f : o.brush.faces) {
		if (!(f.contents & CONTENTS_OZ_NOCOLLIDE)) {
			return false;
		}
	}
	return true;
}
inline bool is_detail(const Object &o) {
	if (o.kind != Kind::Brush || o.brush.faces.empty()) {
		return false;
	}
	for (const Face &f : o.brush.faces) {
		if (!(f.contents & CONTENTS_DETAIL)) {
			return false;
		}
	}
	return true;
}

}  // namespace oz::ed
