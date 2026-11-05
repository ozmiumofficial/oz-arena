// The map editor's movers: doors, lifts, buttons, trains, rotating, bobbing and swinging brushes, made and
// set up as id's maps have them, and where each moves, drawn (and played) in the view.
//
// What id's 35 maps do (2026-11-05 survey of their entities), which the tools follow:
// - 113 func_door: 108 in pairs sharing a "team", their halves sliding apart (angles 90/270 or 0/180), lip
//   16; most open when a player comes near (the game's own trigger, no targetname and no health); 7 are
//   crushers (spawnflags 4, dmg 1000, speed 1000, angle -2) fired by func_buttons.
// - 4 func_button: 3 shot to press (health), wait 6; they target a door's targetname.
// - func_bobbing (platforms, q3dm19), func_rotating and func_pendulum (an origin), 50 func_static.
// - No func_plat nor func_train: the lift and the train path follow the game code (g_mover.c) alone.
//
// The game's rules (g_mover.c) the preview draws: a door or button moves along its angle (-1 up, -2 down,
// else a yaw) by its size that way less its lip (8, a button's 4); a door with neither a targetname nor
// health gets a trigger 120 units out on its thinnest side and opens when shot too; with health it opens
// only when shot; with a targetname only when fired. A plat is built at its top and rests its travel
// ("height", else its height less its lip) lower; a bob moves its height each way on Z (X, Y by flag);
// a rotating brush turns about its origin on Z (X, Y by flag); a pendulum swings its speed in degrees
// each way about its origin; a train carries its origin from path_corner to path_corner.

#include "ed_view.h"
#include "ed_engine.h"
#include "ed_mapfile.h"
#include "ed_shaders.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>

extern "C" {
#include "../ui/oz_ui_engine.h"
}

namespace oz::ed {

static const double PI = 3.14159265358979323846;
static const unsigned char C_GHOST[4] = {120, 200, 255, 230}, C_GHOST_HIDDEN[4] = {120, 200, 255, 70}, C_ZONE[4] = {255, 200, 90, 120},
	C_AXIS[4] = {255, 120, 220, 230}, C_PATH[4] = {120, 255, 160, 220};

static bool is_mover(const std::string &cn) {
	return cn == "func_door" || cn == "func_plat" || cn == "func_button" || cn == "func_train" || cn == "func_rotating" || cn == "func_bobbing" ||
		cn == "func_pendulum";
}

static bool is_origin_brush(const Object &o) {
	if (o.kind != Kind::Brush) {
		return false;
	}
	for (const Face &f : o.brush.faces) {
		const std::string &s = shader_name(f.shader);
		if (s == "common/origin" || s == "textures/common/origin") {
			return true;
		}
	}
	return false;
}

// a mover's own brushes and curves (its origin brush aside: q3map2 takes it out of the model)
static std::vector<Id> model_parts(const Doc &doc, Id ent) {
	std::vector<Id> out;
	for (Id id : doc.owned(ent)) {
		const Object *o = doc.get(id);
		if (o && !is_origin_brush(*o)) {
			out.push_back(id);
		}
	}
	return out;
}

static bool has_pivot(const Doc &doc, Id ent) {
	for (Id id : doc.owned(ent)) {
		const Object *o = doc.get(id);
		if (o && is_origin_brush(*o)) {
			return true;
		}
	}
	return !doc.get(ent)->entity.get("origin").empty();
}

// G_SetMovedir: -1 up, -2 down, else the yaw
static Vec3 move_dir(const Entity &e) {
	const std::string a = e.get("angle", "0");
	if (a == "-1") {
		return Vec3(0, 0, 1);
	}
	if (a == "-2") {
		return Vec3(0, 0, -1);
	}
	const double y = std::atof(a.c_str()) * PI / 180;
	return Vec3(std::cos(y), std::sin(y), 0);
}

static double num(const Entity &e, const char *key, double fallback) {
	const std::string v = e.get(key);
	return v.empty() ? fallback : std::atof(v.c_str());
}

// Where a door, button or plat ends up: its offset from where it's built (SP_func_door, SP_func_plat)
static Vec3 end_offset(const Doc &doc, Id ent) {
	const Entity &e = doc.get(ent)->entity;
	const std::string cn = e.classname();
	const Bounds b = bounds_of(doc, model_parts(doc, ent));
	if (b.empty()) {
		return Vec3();
	}
	const Vec3 size = b.hi - b.lo;
	if (cn == "func_plat") {
		const double lip = num(e, "lip", 8);
		return Vec3(0, 0, -num(e, "height", size.z - lip));
	}
	const Vec3 d = move_dir(e);
	const double lip = num(e, "lip", cn == "func_button" ? 4 : 8);
	const double dist = std::fabs(d.x) * size.x + std::fabs(d.y) * size.y + std::fabs(d.z) * size.z - lip;
	return d * dist;
}

// Think_SpawnNewDoorTrigger: the team's bounds, 120 units out each way on the thinnest axis
static Bounds door_zone(const Doc &doc, Id ent) {
	const std::string team = doc.get(ent)->entity.get("team");
	std::vector<Id> parts = model_parts(doc, ent);
	if (!team.empty()) {
		for (Id e : doc.ids_of(Kind::Entity)) {
			const Entity &en = doc.get(e)->entity;
			if (e != ent && en.classname() == "func_door" && en.get("team") == team) {
				for (Id p : model_parts(doc, e)) {
					parts.push_back(p);
				}
			}
		}
	}
	Bounds b = bounds_of(doc, parts);
	if (b.empty()) {
		return b;
	}
	const Vec3 s = b.hi - b.lo;
	const int axis = s.x <= s.y && s.x <= s.z ? 0 : s.y <= s.z ? 1 : 2;
	double *lo[3] = {&b.lo.x, &b.lo.y, &b.lo.z}, *hi[3] = {&b.hi.x, &b.hi.y, &b.hi.z};
	*lo[axis] -= 120;
	*hi[axis] += 120;
	return b;
}

// the zone trigger the editor made for a door that opens when a player comes near but can't be shot
static Id zone_of(const Doc &doc, Id door) {
	const std::string tag = doc.get(door)->entity.get("_oz_door");
	if (tag.empty()) {
		return 0;
	}
	for (Id e : doc.ids_of(Kind::Entity)) {
		const Entity &en = doc.get(e)->entity;
		if (e != door && en.classname() == "trigger_multiple" && en.get("_oz_door") == tag) {
			return e;
		}
	}
	return 0;
}

// what fires the door other than its own zone (buttons, triggers, timers)
static int firers_of(const Doc &doc, Id door) {
	const std::string name = doc.get(door)->entity.get("targetname");
	if (name.empty()) {
		return 0;
	}
	const Id zone = zone_of(doc, door);
	int n = 0;
	for (Id e : doc.ids_of(Kind::Entity)) {
		n += e != zone && doc.get(e)->entity.get("target") == name;
	}
	return n;
}

// the selected movers of a class (a selected brush counts for its entity)
static std::vector<Id> selected_of(Session &s, const std::string &cls) {
	std::set<Id> out;
	for (Id id : s.sel) {
		const Object *o = s.doc.get(id);
		if (!o) {
			continue;
		}
		const Id e = o->kind == Kind::Entity ? id : o->owner;
		const Object *eo = s.doc.get(e);
		if (eo && eo->kind == Kind::Entity && eo->entity.classname() == cls) {
			out.insert(e);
		}
	}
	return std::vector<Id>(out.begin(), out.end());
}

static std::vector<Id> selected_brushes(const Session &s) {
	std::vector<Id> out;
	for (Id id : s.sel) {
		const Object *o = s.doc.get(id);
		if (o && o->kind == Kind::Brush) {
			out.push_back(id);
		}
	}
	return out;
}

// ---------------------------------------------------------------- making them

Id Session::make_mover(const std::vector<Id> &brushes, const std::string &cls, const Keys &keys) {
	auto e = std::make_shared<Object>();
	e->kind = Kind::Entity;
	e->entity.set("classname", cls);
	for (const auto &kv : keys) {
		e->entity.set(kv.first, kv.second);
	}
	const Id ent = doc.add(e);
	std::set<Id> was;
	for (Id b : brushes) {
		auto o = doc.edit(b);
		if (o->owner != doc.worldspawn()) {
			was.insert(o->owner);
		}
		o->owner = ent;
		doc.put(b, o);
	}
	for (Id w : was) {
		if (doc.get(w) && doc.owned(w).empty()) {
			doc.remove(w);
		}
	}
	add_pivot(ent);
	return ent;
}

// rotating, swinging and trains turn about (or ride on) an origin brush: one in the middle (inside a step)
void Session::add_pivot(Id ent) {
	const Object *o = doc.get(ent);
	const std::string cls = o ? o->entity.classname() : "";
	if ((cls != "func_rotating" && cls != "func_pendulum" && cls != "func_train") || has_pivot(doc, ent)) {
		return;
	}
	const Bounds b = bounds_of(doc, model_parts(doc, ent));
	if (!b.empty()) {
		const Vec3 c(std::round(b.center().x / 8) * 8, std::round(b.center().y / 8) * 8, std::round(b.center().z / 8) * 8);
		doc.add(box_brush(doc, c - Vec3(8, 8, 8), c + Vec3(8, 8, 8), shader_id("common/origin"), ent));
	}
}

void Session::make_door(bool pair) {
	std::vector<Id> brushes = selected_brushes(*this);
	if (brushes.empty()) {
		say("Select the brush (or brushes) the door is made of first", true);
		return;
	}
	const Bounds b = bounds_of(doc, brushes);
	const Vec3 size = b.hi - b.lo;
	// it slides along its longer side across the doorway (id's doors), the way the camera looks right
	const bool along_x = size.x >= size.y;
	const std::string team = unique_targetname(doc, "door");
	doc.begin(pair ? "Double door" : "Door");
	if (!pair) {
		const Vec3 r = camera_axis(true);
		const double yaw = along_x ? (r.x >= 0 ? 0 : 180) : (r.y >= 0 ? 90 : 270);
		const Id d = make_mover(brushes, "func_door", {{"angle", number(yaw)}, {"lip", "16"}});
		doc.end();
		select(group_of(d), SelMode::Set);
		say("Door: it opens when a player comes near, or when shot (Inspect: which way, how it opens, a button)");
		return;
	}
	// a pair: the brushes cut down the middle, each half its own door, sliding apart, one team (id's way)
	const double mid = along_x ? (b.lo.x + b.hi.x) / 2 : (b.lo.y + b.hi.y) / 2;
	Plane cut;
	cut.n = along_x ? Vec3(1, 0, 0) : Vec3(0, 1, 0);
	cut.d = std::round(mid);
	std::vector<Id> lo_side, hi_side;
	for (Id id : brushes) {
		const Object *o = doc.get(id);
		Pieces p = clipped(doc, *o, cut, origin_for(doc, *o));
		if (p.whole) {
			(p.front ? hi_side : lo_side).push_back(id);
			continue;
		}
		if (p.front && p.back) {
			doc.put(id, p.back);
			lo_side.push_back(id);
			hi_side.push_back(doc.add(p.front));
		}
	}
	if (lo_side.empty() || hi_side.empty()) {
		doc.end();
		say("A double door needs a brush wide enough to cut in two", true);
		return;
	}
	const Id a = make_mover(lo_side, "func_door", {{"angle", along_x ? "180" : "270"}, {"lip", "16"}, {"team", team}});
	const Id c = make_mover(hi_side, "func_door", {{"angle", along_x ? "0" : "90"}, {"lip", "16"}, {"team", team}});
	doc.end();
	std::vector<Id> all = group_of(a);
	for (Id id : group_of(c)) {
		all.push_back(id);
	}
	select(all, SelMode::Set);
	say("Double door: two halves sliding apart, opening together when a player comes near or a shot hits them");
}

void Session::make_lift() {
	std::vector<Id> brushes = selected_brushes(*this);
	if (brushes.empty()) {
		say("Select the platform's brush (built at the top of its travel) first", true);
		return;
	}
	// its travel: down to the floor under it (the highest brush top below it, across its footprint)
	const Bounds b = bounds_of(doc, brushes);
	double floor = -1e30;
	const std::set<Id> mine(brushes.begin(), brushes.end());
	for (Id id : doc.ids_of(Kind::Brush)) {
		if (mine.count(id) || !visible(id)) {
			continue;
		}
		const Object *o = doc.get(id);
		const Object *own = doc.get(o->owner);
		if (own && own->entity.classname().compare(0, 8, "trigger_") == 0) {
			continue;
		}
		const Bounds ob = bounds_of(doc, id);
		if (!ob.empty() && ob.hi.z <= b.lo.z + 0.5 && ob.lo.x < b.hi.x && ob.hi.x > b.lo.x && ob.lo.y < b.hi.y && ob.hi.y > b.lo.y) {
			floor = std::max(floor, ob.hi.z);
		}
	}
	const double travel = floor > -1e29 ? b.lo.z - floor : (b.hi.z - b.lo.z) - 8;
	doc.begin("Lift");
	const Id p = make_mover(brushes, "func_plat", {});
	if (travel > 0) {
		set_key(doc, {p}, "height", number(std::round(travel)), "");
	}
	doc.end();
	select(group_of(p), SelMode::Set);
	say("Lift: build it at the top; the game starts it lowered by its travel and it rises when stepped on (Inspect: TRAVEL)");
}

// ---------------------------------------------------------------- how a door opens

Session::DoorState Session::door_state() {
	DoorState s;
	for (Id d : selected_of(*this, "func_door")) {
		const Entity &e = doc.get(d)->entity;
		const bool named = !e.get("targetname").empty();
		const bool health = num(e, "health", 0) > 0;
		const bool near = (!named && !health) || zone_of(doc, d);
		const bool shot = health || (!named && !health);
		if (!s.any) {
			s.near = near, s.shot = shot;
		} else {
			s.near_mixed |= s.near != near;
			s.shot_mixed |= s.shot != shot;
		}
		s.any = true;
		s.team = e.get("team");
		s.buttons += firers_of(doc, d);
	}
	return s;
}

void Session::door_opens(int near, int shot) {
	std::vector<Id> doors = selected_of(*this, "func_door");
	// a team opens as one: its other halves go along
	std::set<Id> all(doors.begin(), doors.end());
	for (Id d : doors) {
		const std::string team = doc.get(d)->entity.get("team");
		for (Id e : doc.ids_of(Kind::Entity)) {
			if (!team.empty() && doc.get(e)->entity.classname() == "func_door" && doc.get(e)->entity.get("team") == team) {
				all.insert(e);
			}
		}
	}
	if (all.empty()) {
		say("Select a door first");
		return;
	}
	const DoorState now = door_state();
	const bool want_near = near < 0 ? now.near : near != 0, want_shot = shot < 0 ? now.shot : shot != 0;
	doc.begin("How the door opens");
	std::string name;  // the team's targetname, shared
	for (Id d : all) {
		if (name.empty()) {
			name = doc.get(d)->entity.get("targetname");
		}
	}
	int fired = 0;
	for (Id d : all) {
		fired += firers_of(doc, d);
	}
	// the game's own door (near, and shot) unless a button or a trigger fires it: no name, no health
	const bool plain = want_near && want_shot && !fired;
	const std::string tag = !plain && want_near ? unique_targetname(doc, "doorzone") : "";
	if (!plain && name.empty() && (want_near || fired)) {
		name = unique_targetname(doc, "door");
	}
	for (Id d : all) {
		auto o = doc.edit(d);
		if (plain || (!want_near && !fired)) {
			o->entity.erase("targetname");
		} else {
			o->entity.set("targetname", name);
		}
		if (want_shot && !plain) {
			if (num(o->entity, "health", 0) <= 0) {
				o->entity.set("health", "1");
			}
		} else {
			o->entity.erase("health");
		}
		if (const Id z = zone_of(doc, d)) {
			for (Id b : doc.owned(z)) {
				doc.remove(b);
			}
			doc.remove(z);  // made again below when it's still wanted
		}
		o->entity.erase("_oz_door");
		doc.put(d, o);
	}
	if (!plain && want_near) {
		// the game's own trigger can't be had without the shot (it makes the door shootable): the same
		// volume as a trigger_multiple firing the door, resizable like any brush
		const Id first = *all.begin();
		const Bounds z = door_zone(doc, first);
		auto t = std::make_shared<Object>();
		t->kind = Kind::Entity;
		t->entity.keys = {{"classname", "trigger_multiple"}, {"target", name}, {"wait", "0.5"}, {"_oz_door", tag}};
		const Id trig = doc.add(t);
		doc.add(box_brush(doc, z.lo, z.hi, shader_id("common/trigger"), trig));
		for (Id d : all) {
			auto o = doc.edit(d);
			o->entity.set("_oz_door", tag);
			doc.put(d, o);
		}
	}
	doc.end();
	say(want_near && want_shot ? "The door opens when a player comes near, or when shot"
		: want_near ? "The door opens when a player comes near (its trigger shows: resize it like a brush); shots don't open it"
		: want_shot ? "The door opens only when shot" : fired ? "The door opens only when its button or trigger fires it"
		: "The door never opens on its own now: ADD BUTTON gives it one", !want_near && !want_shot && !fired);
}

// ---------------------------------------------------------------- the place tool's mover clicks

bool Session::mover_click(const Hit &h) {
	if (place_class == "door_button") {
		// a button on the wall clicked, pressed into it, firing the selected doors
		std::vector<Id> doors = selected_of(*this, "func_door");
		if (doors.empty() && !mover_target_.empty()) {
			for (Id e : doc.ids_of(Kind::Entity)) {
				if (doc.get(e)->entity.get("targetname") == mover_target_) {
					doors.push_back(e);
				}
			}
		}
		const Vec3 n = h.normal;
		if (std::fabs(n.z) > 0.7) {
			say("Click a wall for the button (it's pressed into the wall)", true);
			return true;
		}
		const DoorState was = door_state();
		doc.begin("Button");
		std::string name;
		for (Id d : doors) {
			if (name.empty()) {
				name = doc.get(d)->entity.get("targetname");
			}
		}
		if (name.empty()) {
			name = unique_targetname(doc, "door");
		}
		for (Id d : doors) {
			set_key(doc, {d}, "targetname", name, "");
		}
		// 32 across, 32 high, 8 out from the wall, its middle at the click (snapped)
		const bool along_x = std::fabs(n.x) > std::fabs(n.y);
		const Vec3 at(snap(h.point.x), snap(h.point.y), snap(h.point.z));
		Vec3 lo = at - Vec3(along_x ? 0 : 16, along_x ? 16 : 0, 16), hi = at + Vec3(along_x ? 0 : 16, along_x ? 16 : 0, 16);
		if (along_x) {
			(n.x > 0 ? hi.x : lo.x) = at.x + (n.x > 0 ? 8 : -8);
		} else {
			(n.y > 0 ? hi.y : lo.y) = at.y + (n.y > 0 ? 8 : -8);
		}
		const double into = std::atan2(-n.y, -n.x) * 180 / PI;
		auto e = std::make_shared<Object>();
		e->kind = Kind::Entity;
		e->entity.keys = {{"classname", "func_button"}, {"target", name}, {"angle", number(std::fmod(std::round(into) + 360, 360.0))}, {"lip", "4"}, {"wait", "1"}};
		const Id b = doc.add(e);
		doc.add(box_brush(doc, lo, hi, shader_id(shader), b));
		doc.end();
		// a door that only opened near or by shots keeps that (its zone trigger comes back with the name)
		if (was.any) {
			select(doors, SelMode::Set);
			door_opens(was.near ? 1 : 0, was.shot ? 1 : 0);
		}
		set_tool(Tool::Select);
		select(group_of(b), SelMode::Set);
		say("Button: pressed (or shot, Inspect: health) it opens the door");
		return true;
	}
	if (place_class == "train_path") {
		const Id train = path_train_;
		if (!doc.get(train)) {
			set_tool(Tool::Select);
			return true;
		}
		// the corner carries the train's origin: at the click, the origin's height over its floor up
		const Bounds tb = bounds_of(doc, model_parts(doc, train));
		const Vec3 pivot = doc.model_origin(train);
		const double up = tb.empty() ? 0 : pivot.z - tb.lo.z;
		const Vec3 at(snap(h.point.x), snap(h.point.y), std::round(h.point.z + up));
		// back on the first corner: the loop closed, done
		if (!path_corners_.empty()) {
			Vec3 first;
			if (doc.get(path_corners_[0]) && parse_vec(doc.get(path_corners_[0])->entity.get("origin"), first) && (first - at).length() < 32) {
				set_key(doc, {path_corners_.back()}, "target", doc.get(path_corners_[0])->entity.get("targetname"), "Close the path");
				path_corners_.clear();
				set_tool(Tool::Select);
				select(group_of(train), SelMode::Set);
				say("Path closed: the train runs round it");
				return true;
			}
		}
		doc.begin("Path corner");
		const std::string name = unique_targetname(doc, "path");
		auto c = std::make_shared<Object>();
		c->kind = Kind::Entity;
		c->entity.keys = {{"classname", "path_corner"}, {"targetname", name}, {"origin", vec_key(at)}};
		const Id id = doc.add(c);
		if (path_corners_.empty()) {
			set_key(doc, {train}, "target", name, "");
		} else if (doc.get(path_corners_.back())) {
			set_key(doc, {path_corners_.back()}, "target", name, "");
		}
		doc.end();
		path_corners_.push_back(id);
		return true;
	}
	return false;
}

std::string Session::mover_hint() const {
	if (place_class == "door_button") {
		return "Button: click the wall where it goes (it fires the door) \xc2\xb7 Esc cancels";
	}
	if (place_class == "train_path") {
		return std::to_string(path_corners_.size()) + (path_corners_.size() == 1 ? " corner" : " corners") +
			": click where the train goes next \xc2\xb7 the first corner again closes the loop \xc2\xb7 Esc ends the path";
	}
	return "";
}

// ---------------------------------------------------------------- the actions

bool Session::movers_action(const std::string &action, const std::vector<std::string> &a) {
	if (action == "door" || action == "door_pair") {
		make_door(action == "door_pair");
	} else if (action == "lift") {
		make_lift();
	} else if (action == "door_near" || action == "door_shot") {
		const DoorState s = door_state();
		if (!s.any) {
			say("Select a door first");
		} else if (action == "door_near") {
			door_opens(a.empty() ? !s.near : std::atoi(a[0].c_str()), -1);
		} else {
			door_opens(-1, a.empty() ? !s.shot : std::atoi(a[0].c_str()));
		}
	} else if (action == "door_button") {
		std::vector<Id> doors = selected_of(*this, "func_door");
		if (doors.empty()) {
			say("Select the door the button opens first");
			return true;
		}
		mover_target_ = doc.get(doors[0])->entity.get("targetname");
		set_tool(Tool::Place);
		place_class = "door_button";
		select(doors, SelMode::Set);
	} else if (action == "train_path") {
		std::vector<Id> trains = selected_of(*this, "func_train");
		if (trains.empty()) {
			say("Select a train (a func_train: brushes made one in the Entities panel) first");
			return true;
		}
		path_train_ = trains[0];
		path_corners_.clear();
		set_tool(Tool::Place);
		place_class = "train_path";
	} else if (action == "mover_play") {
		mover_play = !mover_play;
		movers_doc_ = ~0ull;  // drawn again, still or moving
		mover_t0_ = OzE_Milliseconds();
		say(mover_play ? "Movers play: the selected ones move as in the game (again to stop)" : "Movers stopped");
	} else if (action == "lift_travel") {
		// ed lift_travel <units> | +n | -n: how far the selected lifts go down
		for (Id p : selected_of(*this, "func_plat")) {
			const Entity &e = doc.get(p)->entity;
			const Bounds b = bounds_of(doc, model_parts(doc, p));
			const double now = num(e, "height", b.empty() ? 0 : (b.hi.z - b.lo.z) - num(e, "lip", 8));
			const std::string v = a.empty() ? "" : a[0];
			const double next = v.empty() ? now : (v[0] == '+' || v[0] == '-') ? now + std::atof(v.c_str()) : std::atof(v.c_str());
			set_key(doc, {p}, "height", number(std::max(0.0, std::round(next))), "Lift travel");
		}
	} else {
		return false;
	}
	return true;
}

// ---------------------------------------------------------------- what the map check says of them

void Session::mover_problems(std::vector<CompileWarning> &out) {
	for (Id e : doc.ids_of(Kind::Entity)) {
		const Entity &en = doc.get(e)->entity;
		const std::string cn = en.classname();
		if (!is_mover(cn)) {
			continue;
		}
		CompileWarning w;
		if (model_parts(doc, e).empty()) {
			w.text = "A " + cn + " has no brushes: it's nothing in the game";
		} else if ((cn == "func_rotating" || cn == "func_pendulum" || cn == "func_train") && !has_pivot(doc, e)) {
			w.text = "A " + cn + " has no origin brush, so it " + (cn == "func_train" ? "rides its path off to one side" : "turns about the map's middle") +
				": give it a small brush of common/origin where it should turn";
		} else if (cn == "func_train" && en.get("target").empty()) {
			w.text = "A func_train has no path: select it, then LAY PATH (Inspect)";
		} else if (cn == "func_door") {
			const bool named = !en.get("targetname").empty(), health = num(en, "health", 0) > 0;
			if (named && !health && !zone_of(doc, e) && !firers_of(doc, e)) {
				w.text = "A door is named \"" + en.get("targetname") + "\" but nothing fires it, so it never opens (Inspect: how it opens)";
			}
		}
		if (!w.text.empty()) {
			w.ids = group_of(e);
			out.push_back(std::move(w));
		}
	}
}

// ---------------------------------------------------------------- drawn: where they go

// The mover's brushes' outlines moved by xf
static void ghost(Overlay &o, const Doc &doc, Id ent, const Affine &xf, const unsigned char c[4], const unsigned char hidden[4]) {
	for (Id id : model_parts(doc, ent)) {
		const Object *ob = doc.get(id);
		if (!ob || ob->kind != Kind::Brush) {
			continue;
		}
		for (const Winding &w : doc.windings(id)) {
			Winding m;
			for (const Vec3 &p : w) {
				m.push_back(xf.apply(p));
			}
			o.outline(m, c);
			if (hidden) {
				o.outline(m, hidden, "$oz_ed_lines_xray");
			}
		}
	}
}

// the train's path: its corners in order from its target, as far as they go (a loop once round)
static std::vector<Vec3> train_path(const Doc &doc, Id train) {
	std::vector<Vec3> out;
	std::map<std::string, Id> named;
	for (Id e : doc.ids_of(Kind::Entity)) {
		const std::string n = doc.get(e)->entity.get("targetname");
		if (!n.empty() && doc.get(e)->entity.classname() == "path_corner") {
			named[n] = e;
		}
	}
	std::set<Id> seen;
	std::string next = doc.get(train)->entity.get("target");
	while (named.count(next) && !seen.count(named[next])) {
		const Id c = named[next];
		seen.insert(c);
		Vec3 p;
		if (parse_vec(doc.get(c)->entity.get("origin"), p)) {
			out.push_back(p);
		}
		next = doc.get(c)->entity.get("target");
	}
	if (named.count(next) && !out.empty()) {
		Vec3 p;
		if (parse_vec(doc.get(named[next])->entity.get("origin"), p)) {
			out.push_back(p);  // closed: back to where it began
		}
	}
	return out;
}

void Session::build_movers() {
	const int now = OzE_Milliseconds();
	if (!mover_play && doc.revision == movers_doc_ && sel_rev == movers_sel_) {
		return;
	}
	movers_doc_ = doc.revision;
	movers_sel_ = sel_rev;
	movers_.begin();
	std::set<Id> ents;
	for (Id id : sel) {
		const Object *o = doc.get(id);
		if (!o) {
			continue;
		}
		const Id e = o->kind == Kind::Entity ? id : o->owner;
		if (doc.get(e) && doc.get(e)->kind == Kind::Entity && is_mover(doc.get(e)->entity.classname())) {
			ents.insert(e);
		}
	}
	const double t = (now - mover_t0_) / 1000.0;
	for (Id e : ents) {
		const Entity &en = doc.get(e)->entity;
		const std::string cn = en.classname();
		const Vec3 pivot = doc.model_origin(e);
		if (cn == "func_door" || cn == "func_plat" || cn == "func_button") {
			const Vec3 off = end_offset(doc, e);
			const bool start_open = cn == "func_door" && (std::atoi(en.get("spawnflags", "0").c_str()) & 1);
			// the end it moves to; the plat's travel down from where it's built
			ghost(movers_, doc, e, translation(start_open ? Vec3() : off), C_GHOST, C_GHOST_HIDDEN);
			if (mover_play) {
				const double speed = num(en, "speed", cn == "func_door" ? 400 : cn == "func_plat" ? 200 : 40);
				const double len = std::max(1.0, off.length()), travel = len / std::max(1.0, speed), period = 2 * travel + 1.5;
				const double ph = std::fmod(t, period);
				const double k = ph < travel ? ph / travel : ph < travel + 0.75 ? 1 : ph < 2 * travel + 0.75 ? 1 - (ph - travel - 0.75) / travel : 0;
				ghost(movers_, doc, e, translation(off * k), C_PATH, nullptr);
			}
			if (cn == "func_door" && en.get("targetname").empty() && num(en, "health", 0) <= 0) {
				const Bounds z = door_zone(doc, e);  // the game's own trigger
				if (!z.empty()) {
					movers_.box_edges(z.lo, z.hi, C_ZONE);
				}
			}
		} else if (cn == "func_bobbing") {
			const int f = std::atoi(en.get("spawnflags", "0").c_str());
			const Vec3 axis = (f & 1) ? Vec3(1, 0, 0) : (f & 2) ? Vec3(0, 1, 0) : Vec3(0, 0, 1);
			const double h = num(en, "height", 32);
			ghost(movers_, doc, e, translation(axis * h), C_GHOST, C_GHOST_HIDDEN);
			ghost(movers_, doc, e, translation(axis * -h), C_GHOST, C_GHOST_HIDDEN);
			if (mover_play) {
				const double cycle = std::max(0.1, num(en, "speed", 4)), phase = num(en, "phase", 0);
				ghost(movers_, doc, e, translation(axis * (h * std::sin(2 * PI * (t / cycle + phase)))), C_PATH, nullptr);
			}
		} else if (cn == "func_rotating" || cn == "func_pendulum") {
			const int f = std::atoi(en.get("spawnflags", "0").c_str());
			int axis = 2;
			if (cn == "func_rotating") {
				axis = (f & 4) ? 0 : (f & 8) ? 1 : 2;
			} else {
				// a pendulum rolls (about X); its angle turns that about Z: to the nearer of X and Y
				const double yaw = std::fmod(std::fabs(num(en, "angle", 0)), 180.0);
				axis = yaw > 45 && yaw < 135 ? 1 : 0;
			}
			const Vec3 dir = axis == 0 ? Vec3(1, 0, 0) : axis == 1 ? Vec3(0, 1, 0) : Vec3(0, 0, 1);
			movers_.line(pivot - dir * 96, pivot + dir * 96, C_AXIS, "$oz_ed_lines_xray");
			movers_.box_edges(pivot - Vec3(6, 6, 6), pivot + Vec3(6, 6, 6), C_AXIS, "$oz_ed_lines_xray");
			if (cn == "func_pendulum") {
				const double swing = num(en, "speed", 30);
				ghost(movers_, doc, e, rotation(axis, swing, pivot), C_GHOST, C_GHOST_HIDDEN);
				ghost(movers_, doc, e, rotation(axis, -swing, pivot), C_GHOST, C_GHOST_HIDDEN);
				if (mover_play) {
					ghost(movers_, doc, e, rotation(axis, swing * std::sin(2 * PI * t / 2.0 + 2 * PI * num(en, "phase", 0)), pivot), C_PATH, nullptr);
				}
			} else if (mover_play) {
				ghost(movers_, doc, e, rotation(axis, std::fmod(num(en, "speed", 100) * t, 360.0), pivot), C_PATH, nullptr);
			}
		} else if (cn == "func_train") {
			const std::vector<Vec3> path = train_path(doc, e);
			for (size_t i = 1; i < path.size(); i++) {
				movers_.line(path[i - 1], path[i], C_PATH);
			}
			if (mover_play && path.size() > 1) {
				double total = 0;
				for (size_t i = 1; i < path.size(); i++) {
					total += (path[i] - path[i - 1]).length();
				}
				double d = std::fmod(num(en, "speed", 100) * t, std::max(1.0, total));
				Vec3 at = path[0];
				for (size_t i = 1; i < path.size(); i++) {
					const double l = (path[i] - path[i - 1]).length();
					if (d <= l) {
						at = path[i - 1] + (path[i] - path[i - 1]) * (l > 0 ? d / l : 0);
						break;
					}
					d -= l;
				}
				ghost(movers_, doc, e, translation(at - pivot), C_PATH, nullptr);
			}
		}
	}
	movers_.end();
}

}  // namespace oz::ed
