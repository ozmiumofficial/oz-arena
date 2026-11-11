// The map editor's entities (ENGINE_PLAN.md P13, engine/EDITOR.md), after the Godot build's
// q3_editor_entities.gd and q3_entity_tool.gd: the classes of the mods' .def files, the models drawn
// (pickups spinning and bobbing as in the game, misc_models), what targets what, the jump pads' arcs on
// id's throw, the place tool, the jump pad and teleporter tools, brush entities made from brushes, and
// the inspector's spawnflags and keys.

#include "ed_view.h"
#include "ed_engine.h"
#include "ed_mapfile.h"
#include "ed_shaders.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <map>

extern "C" {
#include "../ui/oz_ui_engine.h"
}

namespace oz::ed {

static const double PI = 3.14159265358979323846;
static const double PAD = 64;  // the jump pad and teleporter tools' footprint
// A jump pad's throw, as the arc shows it: from the trigger's middle (AimAtTarget), down to the floor
// clicked. The player stands 10 units above the trigger's middle (its origin 24 over the floor, the
// trigger 4 into the floor and 32 over it, so its middle 14 up), so the arc that brings the player's
// origin to 24 over the floor brings the middle to 14 over it. (The editor's older pads, a 4-unit
// block under a 36-unit trigger, put the player 10 over the middle too.)
static const double PAD_LOW = -4, PAD_HIGH = 32, PAD_MIDDLE = 14, LAND_MIDDLE = 14;

static const unsigned char C_LINK[4] = {255, 214, 90, 130}, C_LINK_HIDDEN[4] = {255, 214, 90, 34}, C_LINK_ON[4] = {255, 250, 210, 255},
	C_ARC[4] = {110, 255, 150, 230}, C_ARC_HIDDEN[4] = {110, 255, 150, 60}, C_GHOST_EDGE[4] = {255, 170, 90, 255},
	C_BAD[4] = {255, 60, 50, 255};  // a jump pad aiming at a target it can't throw to

static double arg(const std::vector<std::string> &a, size_t i, double fallback) {
	return i < a.size() && !a[i].empty() ? std::atof(a[i].c_str()) : fallback;
}

// ---------------------------------------------------------------- the mods' .def files

// "defrag" -> "Defrag", "cpma" -> "CPMA"
static std::string mod_name(std::string d) {
	if (d.size() <= 4) {
		for (char &c : d) {
			c = (char)std::toupper((unsigned char)c);
		}
	} else if (!d.empty()) {
		d[0] = (char)std::toupper((unsigned char)d[0]);
	}
	return d;
}

void Session::load_defs() {
	if (defs_loaded_) {
		return;
	}
	defs_loaded_ = true;
	int joined = 0;
	auto read_os = [&](const std::string &dir, const std::string &source) {
		int n = 0;
		char **files = OzEd_ListOSFiles(dir.c_str(), ".def", &n);
		for (int i = 0; i < n; i++) {
			char *text = nullptr;
			const int len = OzEd_ReadOSFile((dir + "/" + files[i]).c_str(), &text);
			if (len > 0) {
				joined += read_def_file(std::string_view(text, size_t(len)), source);
			}
			OzEd_Free(text);
		}
		OzEd_FreeList(files);
	};
	// the game's own files (a pk3's scripts/*.def)
	int n = 0;
	char **files = OzE_ListFiles("scripts", ".def", &n);
	for (int i = 0; i < n; i++) {
		void *buf = nullptr;
		const int len = OzE_ReadFile((std::string("scripts/") + files[i]).c_str(), &buf);
		if (len > 0) {
			joined += read_def_file(std::string_view((const char *)buf, size_t(len)), files[i]);
		}
		OzE_FreeFile(buf);
	}
	OzE_FreeFileList(files);
	// the install's mods: <mod>/scripts, and a bundle's <mod>/<mod>/scripts (Defrag's)
	std::string base = OzEd_BaseQ3();
	const size_t slash = base.find_last_of("/\\");
	if (slash != std::string::npos) {
		const std::string install = base.substr(0, slash);
		char **dirs = OzEd_ListOSFiles(install.c_str(), "/", &n);
		for (int i = 0; i < n; i++) {
			const std::string d = dirs[i];
			if (d == "." || d == "..") {
				continue;
			}
			read_os(install + "/" + d + "/scripts", mod_name(d));
			read_os(install + "/" + d + "/" + d + "/scripts", mod_name(d));
		}
		OzEd_FreeList(dirs);
	}
	if (joined) {
		OzE_Print(("map editor: " + std::to_string(joined) + " entity classes from the mods' .def files\n").c_str());
	}
}

// ---------------------------------------------------------------- models

int Session::model_handle(const std::string &name) {
	auto it = model_handles_.find(name);
	if (it != model_handles_.end()) {
		return it->second;
	}
	const int h = OzEd_RegisterModel(name.c_str());
	model_handles_[name] = h;
	return h;
}

static double yaw_of(const Entity &e) {
	Vec3 ang;
	if (const std::string *a = e.find("angles")) {
		if (parse_vec(*a, ang)) {
			return ang.y;
		}
	}
	return std::atof(e.get("angle", "0").c_str());
}

void Session::collect_models() {
	models_.clear();
	for (Id id : doc.ids_of(Kind::Entity)) {
		const Entity &e = doc.get(id)->entity;
		Vec3 at;
		if (!visible(id) || !parse_vec(e.get("origin"), at)) {
			continue;
		}
		const std::string cn = e.classname();
		const EntityDef *d = entity_def(cn);
		std::vector<std::string> names = d ? d->models : std::vector<std::string>{};
		double scale = d ? d->model_scale : 1;
		if (cn == "misc_model") {
			names = {e.get("model")};
			const double s = std::atof(e.get("modelscale", "1").c_str());
			scale = s > 0 ? s : 1;
		}
		// a spawn point stands as a fighter: the red or blue team's in its color
		if (d && d->section == "Spawns" && cn != "info_player_intermission") {
			const std::string skin = cn.find("_red") != std::string::npos ? "red" : cn.find("_blue") != std::string::npos ? "blue" : "default";
			models_.push_back({0, at, yaw_of(e), 1, false, id, "sarge/" + skin});
			continue;
		}
		for (const std::string &m : names) {
			if (m.empty()) {
				continue;
			}
			if (const int h = model_handle(m)) {
				models_.push_back({h, at, yaw_of(e), scale, d && d->item, id, ""});
			}
		}
	}
}

void Session::draw_models(const float moved[3], int now, bool spawns) {
	for (const EntModel &m : models_) {
		if (!spawns && !m.fighter.empty()) {
			continue;  // the thumbnail: no one stands on the spawn points
		}
		if (lit_ && !changed_since(m.id) && doc.get(m.id) && doc.get(m.id)->entity.classname() == "misc_model") {
			continue;  // the compile baked it into the world the lit preview draws
		}
		double yaw = m.yaw, z = 0;
		if (m.item) {
			// cg_ents.c CG_Item: a turn every 2.048 s (health every 1.024), bobbing 4 units
			const EntityDef *d = entity_def(doc.get(m.id) ? doc.get(m.id)->entity.classname() : "");
			const bool fast = d && d->section == "Health";
			yaw = fast ? (now & 1023) * 360.0 / 1024 : (now & 2047) * 360.0 / 2048;
			z = 4 + std::cos((now + 1000) * (0.005 + m.id * 0.00001)) * 4;
		}
		const double r = yaw * PI / 180, c = std::cos(r) * m.scale, s = std::sin(r) * m.scale;
		const float axis[3][3] = {{float(c), float(s), 0}, {float(-s), float(c), 0}, {0, 0, float(m.scale)}};
		float org[3] = {float(m.at.x), float(m.at.y), float(m.at.z + z)};
		if (sel.count(m.id)) {
			org[0] += moved[0], org[1] += moved[1], org[2] += moved[2];
		}
		if (!m.fighter.empty()) {
			const size_t slash = m.fighter.find('/');
			OzEd_AddFighter(m.fighter.substr(0, slash).c_str(), m.fighter.substr(slash + 1).c_str(), org, float(m.yaw));
			continue;
		}
		OzEd_AddModel(m.model, org, axis, 0, nullptr);
	}
}

// ---------------------------------------------------------------- links and arcs

// The first solid brush a segment meets (triggers, hint and the like let players through).
// The brushes a throw can meet, with their bounds: listed once for a whole arc (and once for all the arcs
// build_links traces), not at each of its 160 steps. Each step used to list and sort every brush of the map
// and work out its bounds again, for every pad, at every change of the document: a drag stalled on a big map.
struct ThrowBrush {
	const Object *o;
	Bounds bb;
};
static std::vector<ThrowBrush> throw_brushes(const Doc &doc) {
	std::vector<ThrowBrush> out;
	for (Id id : doc.ids_of(Kind::Brush)) {
		const Object *o = doc.get(id);
		const Object *owner = doc.get(o->owner);
		if ((owner && owner->entity.classname().compare(0, 8, "trigger_") == 0) || is_nocollide(*o)) {
			continue;  // a trigger, or a brush with its collision off: thrown players go through
		}
		const Bounds bb = bounds_of(doc, id);
		if (!bb.empty()) {
			out.push_back({o, bb});
		}
	}
	return out;
}
// build_links' list while it runs (the document doesn't change under it), else null
static const std::vector<ThrowBrush> *g_throw_brushes;

static bool solid_hit_in(const std::vector<ThrowBrush> &brushes, const Vec3 &a, const Vec3 &b, double &frac) {
	const Vec3 d = b - a;
	const double len = d.length();
	if (len < 1e-6) {
		return false;
	}
	const Vec3 dir = d * (1 / len);
	Bounds seg;
	seg.add(a);
	seg.add(b);
	double best = len;
	bool hit = false;
	for (const ThrowBrush &tb : brushes) {
		const Object *o = tb.o;
		if (!tb.bb.overlaps(seg, 1)) {
			continue;
		}
		bool solid = false;
		for (const Face &f : o->brush.faces) {
			const Look l = shaders_lib().look(shader_name(f.shader));
			solid |= l != Look::Hint && l != Look::Trigger && l != Look::Fog && l != Look::Origin && l != Look::Portal;
		}
		double t;
		int face;
		if (solid && ray_faces(o->brush, a, dir, t, face) && t >= 0 && t < best) {
			best = t;
			hit = true;
		}
	}
	frac = best / len;
	return hit;
}

bool solid_hit(const Doc &doc, const Vec3 &a, const Vec3 &b, double &frac) {
	if (g_throw_brushes) {
		return solid_hit_in(*g_throw_brushes, a, b, frac);
	}
	return solid_hit_in(throw_brushes(doc), a, b, frac);
}

// The middle of the first entity named `name` that has one (a pusher's target).
static bool target_center(const Doc &doc, const std::string &name, Vec3 &out) {
	for (Id e : doc.ids_of(Kind::Entity)) {
		const std::string *n = doc.get(e)->entity.find("targetname");
		if (n && *n == name && entity_center(doc, e, out)) {
			return true;
		}
	}
	return false;
}

// A throw from `from` at v, falling on until it meets a brush (or 6.4 s, or 4096 under where it began):
// its points 0.04 s apart, the last where it met the brush. Whether it met one.
static bool throw_path(const Doc &doc, const Vec3 &from, const Vec3 &v, double g, std::vector<Vec3> &out) {
	out.clear();
	out.push_back(from);
	Vec3 last = from;
	std::vector<ThrowBrush> own;
	if (!g_throw_brushes) {
		own = throw_brushes(doc);
	}
	const std::vector<ThrowBrush> &brushes = g_throw_brushes ? *g_throw_brushes : own;
	for (int i = 1; i <= 160; i++) {
		const double t = i * 0.04;
		const Vec3 p = from + v * t - Vec3(0, 0, 0.5 * g * t * t);
		double frac;
		if (solid_hit_in(brushes, last, p, frac)) {
			out.push_back(last + (p - last) * frac);
			return true;
		}
		out.push_back(p);
		last = p;
		if (p.z < from.z - 4096) {
			break;
		}
	}
	return false;
}

const std::vector<Vec3> &Session::push_arc(Id id) {
	auto &cached = arcs_[id];
	if (cached.first == doc.revision + 1) {
		return cached.second;
	}
	cached.first = doc.revision + 1;
	cached.second.clear();
	arc_lands_[id] = false;
	const Object *o = doc.get(id);
	Vec3 from, to, v;
	const double g = map_gravity(doc);
	if (!o || !entity_center(doc, id, from) || !target_center(doc, o->entity.get("target"), to) || !push_velocity(from, to, g, v)) {
		return cached.second;
	}
	arc_lands_[id] = throw_path(doc, from, v, g, cached.second);
	return cached.second;
}

bool Session::push_top(Id pad, Vec3 &top) {
	const Object *o = doc.get(pad);
	return o && push_arc(pad).size() >= 2 && target_center(doc, o->entity.get("target"), top);
}

bool Session::push_aim(Id pad, Vec3 &floor) {
	const std::vector<Vec3> &arc = push_arc(pad);
	if (arc.size() < 2 || !arc_lands_[pad]) {
		return false;
	}
	floor = arc.back();
	Vec3 from, top, v;
	const double g = map_gravity(doc);
	if (arc[arc.size() - 1].z >= arc[arc.size() - 2].z || !entity_center(doc, pad, from) || !push_top(pad, top) || !push_velocity(from, top, g, v)) {
		return true;  // it comes down on something on its way up: aimed where it lands
	}
	// aim_pad aims LAND_MIDDLE over the floor: where the throw passes that height on its way down
	const double disc = v.z * v.z - 2 * g * (floor.z + LAND_MIDDLE - from.z);
	if (disc >= 0) {
		const double t = (v.z + std::sqrt(disc)) / g;
		floor.x = from.x + v.x * t, floor.y = from.y + v.y * t;
	}
	return true;
}

bool Session::push_flight(Id id, double &speed, double &height, double &across, double &seconds) {
	const std::vector<Vec3> &arc = push_arc(id);
	if (arc.size() < 3) {
		return false;
	}
	// the arc's points are 0.04 s apart: the launch from the first step, gravity taken back out
	const double g = map_gravity(doc), t = 0.04;
	const Vec3 v = (arc[1] - arc[0]) * (1 / t) + Vec3(0, 0, 0.5 * g * t);
	speed = v.length();
	height = 0;
	for (const Vec3 &p : arc) {
		height = std::max(height, p.z - arc[0].z);
	}
	const Vec3 d = arc.back() - arc[0];
	across = std::sqrt(d.x * d.x + d.y * d.y);
	seconds = t * double(arc.size() - 1);
	return true;
}

bool Session::push_lands(Id id) {
	push_arc(id);
	return arc_lands_[id];
}

std::vector<Id> Session::pushers_at(const std::string &targetname) const {
	std::vector<Id> out;
	for (Id e : doc.ids_of(Kind::Entity)) {
		const Entity &en = doc.get(e)->entity;
		if ((en.classname() == "trigger_push" || en.classname() == "target_push") && !targetname.empty() && en.get("target") == targetname) {
			out.push_back(e);
		}
	}
	return out;
}

// The selected pushers, and those aiming at a selected target.
static std::vector<Id> selected_pushers(Session &s) {
	std::set<Id> out;
	for (Id id : s.key_entities()) {
		const Entity &e = s.doc.get(id)->entity;
		if (e.classname() == "trigger_push" || e.classname() == "target_push") {
			out.insert(id);
		} else if (const std::string *n = e.find("targetname")) {
			for (Id p : s.pushers_at(*n)) {
				out.insert(p);
			}
		}
	}
	return std::vector<Id>(out.begin(), out.end());
}

static Id entity_named(const Doc &doc, const std::string &name) {
	for (Id e : doc.ids_of(Kind::Entity)) {
		const std::string *n = doc.get(e)->entity.find("targetname");
		if (n && *n == name) {
			return e;
		}
	}
	return 0;
}

void Session::push_height(double dz) {
	// each selected pad's target up or down by dz, moved across so the throw still comes down where it did
	std::set<Id> targets;
	std::map<Id, Id> pusher_of;
	for (Id p : selected_pushers(*this)) {
		const Id t = entity_named(doc, doc.get(p)->entity.get("target"));
		Vec3 o;
		if (t && parse_vec(doc.get(t)->entity.get("origin"), o) && targets.insert(t).second) {
			pusher_of[t] = p;
		}
	}
	if (targets.empty()) {
		say("Select a jump pad with a target to throw at (Land here gives it one)");
		return;
	}
	const double g = map_gravity(doc);
	int refused = 0;
	doc.begin(dz > 0 ? "Jump higher" : "Jump lower");
	for (Id e : targets) {
		auto o = doc.edit(e);
		Vec3 at, from;
		parse_vec(o->entity.get("origin"), at);
		Vec3 next = at + Vec3(0, 0, dz);
		const Id p = pusher_of[e];
		const std::vector<Vec3> arc = push_arc(p);
		if (doc.get(p)->entity.classname() == "trigger_push" && arc.size() >= 2 && entity_center(doc, p, from)) {
			Vec3 land = arc.back();
			push_aim(p, land);  // where it was aimed, or each press would carry the landing on a little
			if (next.z < std::max(from.z, land.z + LAND_MIDDLE) + 16) {
				refused++;
				continue;  // a throw this low wouldn't clear where it lands
			}
			next = apex_at(from, land + Vec3(0, 0, LAND_MIDDLE), g, next.z);
		}
		o->entity.set("origin", vec_key(Vec3(std::round(next.x), std::round(next.y), std::round(next.z))));
		doc.put(e, o);
	}
	doc.end();
	if (refused) {
		say("The throw can't go any lower and still land there", true);
	}
}

Id Session::aim_pad(Id p, const Vec3 &floor) {
	Vec3 from;
	if (!doc.get(p) || !entity_center(doc, p, from)) {
		return 0;
	}
	return aim_pad_top(p, apex_for(from, floor + Vec3(0, 0, LAND_MIDDLE), map_gravity(doc), ceiling_over(from, floor)));
}

Id Session::aim_pad_top(Id p, const Vec3 &top) {
	if (!doc.get(p)) {
		return 0;
	}
	const std::string at = vec_key(Vec3(std::round(top.x), std::round(top.y), std::round(top.z)));
	const std::string name = doc.get(p)->entity.get("target");
	Id t = name.empty() ? 0 : entity_named(doc, name);
	if (t && pushers_at(name).size() > 1) {
		t = 0;  // shared with another pad: this one gets a target of its own
	}
	if (t) {
		auto o = doc.edit(t);
		o->entity.set("origin", at);
		doc.put(t, o);
		return t;
	}
	const std::string tag = doc.get(p)->entity.get(JUMPPAD_KEY);
	const std::string fresh = unique_targetname(doc, "pad");
	auto o = std::make_shared<Object>();
	o->kind = Kind::Entity;
	o->entity.keys = {{"classname", "target_position"}, {"targetname", fresh}, {"origin", at}};
	if (!tag.empty()) {
		o->entity.set(JUMPPAD_KEY, tag);
	}
	t = doc.add(o);
	auto pe = doc.edit(p);
	pe->entity.set("target", fresh);
	doc.put(p, pe);
	return t;
}

// A landing or top drag begins: the pad and its target as they are, to put back if the drag is called off.
void Session::keep_pad(Id pad) {
	start_.clear();
	pad_made_.clear();
	const Object *o = doc.get(pad);
	if (!o) {
		return;
	}
	start_[pad] = doc.ptr(pad);
	const std::string name = o->entity.get("target");
	const Id t = name.empty() ? 0 : entity_named(doc, name);
	if (t) {
		start_[t] = doc.ptr(t);
	}
}

// Called off: the pad and its target back as they were, a target the drag made for it gone, all inside the
// drag's open step, which then ends empty. (An undo of the ended step put them back too, but left a Redo.)
void Session::restore_pad() {
	for (Id id : pad_made_) {
		if (doc.get(id)) {
			doc.remove(id);
		}
	}
	for (auto &kv : start_) {
		doc.put(kv.first, std::const_pointer_cast<Object>(kv.second));
	}
}

void Session::start_land_drag(Id pad) {
	land_drag_pad_ = pad;
	land_drag_moved_ = false;
	land_drag_at_ = Vec3(1e30, 1e30, 1e30);
	doc.begin("Land here");
	keep_pad(pad);
	drag_ = Drag::Land;
}

void Session::land_drag(int m) {
	// the floor under the pointer, through triggers and entities, on the grid unless Ctrl is held
	Hit h;
	if (!doc.get(land_drag_pad_)) {
		return;
	}
	if (!pick_floor(mx_, my_, h, false)) {
		h = Hit();
		h.normal = Vec3(0, 0, 1);
		if (!plane_point(mx_, my_, Vec3(0, 0, 0), Vec3(0, 0, 1), h.point)) {
			return;
		}
	}
	Vec3 at = h.point;
	if (!(m & CTRL)) {
		at.x = snap(at.x), at.y = snap(at.y);
	}
	if (at.x == land_drag_at_.x && at.y == land_drag_at_.y && at.z == land_drag_at_.z) {
		return;
	}
	land_drag_at_ = at;
	const Id t = aim_pad(land_drag_pad_, at);
	if (t && !start_.count(t) && std::find(pad_made_.begin(), pad_made_.end(), t) == pad_made_.end()) {
		pad_made_.push_back(t);  // a target of its own, made by this drag
	}
	land_drag_moved_ = true;
	changed({land_drag_pad_, t});
	Vec3 from;
	const double across = entity_center(doc, land_drag_pad_, from) ? std::hypot(at.x - from.x, at.y - from.y) : 0;
	char buf[96];
	std::snprintf(buf, sizeof buf, "Lands at %g %g %g, %g across", std::round(at.x), std::round(at.y), std::round(at.z), std::round(across));
	drag_note_ = buf;
}

void Session::end_land_drag(bool cancel) {
	if (cancel && land_drag_moved_) {
		restore_pad();  // called off: the pad aimed as it was
	}
	doc.end();
	start_.clear();
	pad_made_.clear();
	changed({});
	if (doc.get(land_drag_pad_)) {
		select(group_of(land_drag_pad_), SelMode::Set);  // its target too, if it got one of its own
	}
	land_drag_pad_ = 0;
}

// The height where a ray passes nearest the vertical through `at`; false when it runs along it (a view
// straight down, which can't say how high).
static bool vertical_height(const Vec3 &o, const Vec3 &d, const Vec3 &at, double &z) {
	const Vec3 w = o - at;
	const double dd = d.dot(d), b = d.z, denom = dd - b * b;
	if (denom < 1e-4 * dd) {
		return false;
	}
	z = at.z + (dd * w.z - b * d.dot(w)) / denom;
	return true;
}

void Session::start_apex_drag(const Handle &h) {
	drag_ = Drag::None;
	Vec3 o, d, from;
	ray(press_x_, press_y_, o, d);
	if (!doc.get(h.id) || !entity_center(doc, h.id, from) || !push_aim(h.id, apex_drag_floor_) || !vertical_height(o, d, h.at, apex_drag_grab_)) {
		return;
	}
	apex_drag_pad_ = h.id;
	apex_drag_at_ = h.at;
	apex_drag_top_ = h.at.z;
	apex_drag_land_ = push_arc(h.id).back();
	apex_drag_moved_ = false;
	// no higher than 40 under the roof over its top, as a new pad's (or where it was, if that's higher)
	double frac;
	apex_drag_max_ = solid_hit(doc, h.at, h.at + Vec3(0, 0, 8192), frac) ? std::max(h.at.z + 8192 * frac - 40, h.at.z) : 1e30;
	doc.begin("Jump height");
	keep_pad(h.id);
	drag_ = Drag::Apex;
}

void Session::apex_drag(int m) {
	const Id p = apex_drag_pad_;
	Vec3 o, d, from;
	double z;
	ray(mx_, my_, o, d);
	if (!doc.get(p) || !entity_center(doc, p, from) || !vertical_height(o, d, apex_drag_at_, z)) {
		return;
	}
	// the top follows the pointer up and down, on the grid unless Ctrl is held
	double top = apex_drag_at_.z + (z - apex_drag_grab_);
	if (!(m & CTRL)) {
		top = snap(top);
	}
	const char *limit = nullptr;
	if (top > apex_drag_max_) {
		top = std::floor(apex_drag_max_);
		limit = "as high as it goes under the roof";
	}
	// 16 over the higher end at the least, as Lower keeps it
	const double low = std::ceil(std::max(from.z, apex_drag_floor_.z + LAND_MIDDLE) + 16);
	if (top < low) {
		top = low;
		limit = "as low as it goes and still lands there";
	}
	if (top != apex_drag_top_) {
		const double g = map_gravity(doc);
		const Vec3 a = apex_at(from, apex_drag_floor_ + Vec3(0, 0, LAND_MIDDLE), g, top);
		const Vec3 next(std::round(a.x), std::round(a.y), std::round(a.z));
		// tried first: a top whose throw comes down anywhere but where it should (something in the way,
		// a roof, a wall or a ledge) is refused, and the throw stays at the last one that landed
		Vec3 v;
		std::vector<Vec3> path;
		bool lands = push_velocity(from, next, g, v) && throw_path(doc, from, v, g, path);
		if (lands) {
			const double disc = v.z * v.z - 2 * g * (apex_drag_land_.z - from.z);
			const double t = disc >= 0 ? (v.z + std::sqrt(disc)) / g : -1;
			const Vec3 want = from + v * t - Vec3(0, 0, 0.5 * g * t * t);
			lands = t > 0 && (path.back() - want).length() < 8;
		}
		if (lands) {
			const Id t = aim_pad_top(p, next);
			if (t && !start_.count(t) && std::find(pad_made_.begin(), pad_made_.end(), t) == pad_made_.end()) {
				pad_made_.push_back(t);  // a target of its own, made by this drag
			}
			apex_drag_top_ = top;
			apex_drag_moved_ = true;
			changed({p, t});
		} else {
			limit = top > apex_drag_top_ ? "any higher and it meets something on the way" : "any lower and it meets something on the way";
		}
	}
	double speed, height, across, seconds;
	if (push_flight(p, speed, height, across, seconds)) {
		char buf[192];
		std::snprintf(buf, sizeof buf, "Throws %g up, %.1f s in the air, landing where it did%s%s", std::round(height), seconds,
			limit ? " \xc2\xb7 " : "", limit ? limit : "");
		drag_note_ = buf;
	}
}

void Session::end_apex_drag(bool cancel) {
	if (cancel && apex_drag_moved_) {
		restore_pad();  // called off: the throw as it was
	}
	doc.end();
	start_.clear();
	pad_made_.clear();
	changed({});
	if (doc.get(apex_drag_pad_)) {
		select(group_of(apex_drag_pad_), SelMode::Set);  // its target too, if it got one of its own
	}
	apex_drag_pad_ = 0;
}

void Session::push_land() {
	land_pushers_.clear();
	for (Id p : selected_pushers(*this)) {
		if (doc.get(p)->entity.classname() == "trigger_push") {
			land_pushers_.push_back(p);
		}
	}
	if (land_pushers_.empty()) {
		say("Select a jump pad first, then Land here and click where players should come down");
		return;
	}
	set_tool(Tool::Place);
	place_class = "jump_land";
	place_step_ = 0;
}

std::string Session::push_problem(Id id) {
	const Object *o = doc.get(id);
	if (!o || o->kind != Kind::Entity) {
		return "";
	}
	const std::string cn = o->entity.classname();
	if (cn != "trigger_push" && cn != "target_push") {
		return "";
	}
	const std::string target = o->entity.get("target");
	if (target.empty()) {
		return cn == "trigger_push" ? "It has no target, so it does nothing in the game: Land here gives it one" : "";
	}
	const Id t = entity_named(doc, target);
	if (!t) {
		return "Its target \"" + target + "\" isn't in the map, so it does nothing in the game: Land here gives it a new one";
	}
	if (cn != "trigger_push") {
		return "";
	}
	Vec3 from, to;
	if (entity_center(doc, id, from) && entity_center(doc, t, to) && to.z <= from.z) {
		return "Its target is no higher than the pad, so the game can't aim the throw: raise the target, or Land here";
	}
	if (push_arc(id).size() >= 2 && !arc_lands_[id]) {
		return "Its throw never comes down on anything: Land here aims it at a floor";
	}
	return "";
}

void Session::build_links() {
	if (doc.revision == links_doc_ && sel_rev == links_sel_) {
		return;
	}
	if ((drag_ == Drag::Push || drag_ == Drag::Corners) && links_doc_ != ~0ull) {
		return;  // a face or a corner being dragged changes the map at every step: the throws are traced again when it's let go
	}
	links_doc_ = doc.revision;
	links_sel_ = sel_rev;
	links_.begin();
	std::vector<ThrowBrush> solids;  // listed at the first throw to trace, for all of them
	std::map<std::string, std::vector<Id>> named;
	for (Id e : doc.ids_of(Kind::Entity)) {
		if (const std::string *n = doc.get(e)->entity.find("targetname")) {
			named[*n].push_back(e);
		}
	}
	for (Id e : doc.ids_of(Kind::Entity)) {
		const Entity &en = doc.get(e)->entity;
		const std::string *t = en.find("target");
		Vec3 from;
		if (!t || t->empty() || !named.count(*t) || !visible(e) || !entity_center(doc, e, from)) {
			continue;
		}
		const std::string cn = en.classname();
		bool bad = false;
		if (cn == "trigger_push" || cn == "target_push") {
			// a pusher shows the throw it makes, not a line to the top of it
			if (!g_throw_brushes) {
				const auto cached = arcs_.find(e);
				if (cached == arcs_.end() || cached->second.first != doc.revision + 1) {
					solids = throw_brushes(doc);
					g_throw_brushes = &solids;
				}
			}
			const std::vector<Vec3> &arc = push_arc(e);
			for (size_t i = 1; i < arc.size(); i++) {
				links_.line(arc[i - 1], arc[i], C_ARC);
				links_.line(arc[i - 1], arc[i], C_ARC_HIDDEN, "$oz_ed_lines_xray");
			}
			if (arc.size() > 1) {
				continue;
			}
			bad = cn == "trigger_push";  // a target it can't throw at: the line to it in red
		}
		for (Id to : named[*t]) {
			Vec3 at;
			if (!entity_center(doc, to, at)) {
				continue;
			}
			const bool on = sel.count(e) || sel.count(to);
			links_.line(from, at, bad ? C_BAD : on ? C_LINK_ON : C_LINK);
			links_.line(from, at, C_LINK_HIDDEN, "$oz_ed_lines_xray");
			// an arrowhead past the middle, the way the link goes
			const Vec3 d = at - from;
			const double len = d.length();
			if (len > 24) {
				const Vec3 dir = d * (1 / len), mid = from + d * 0.6;
				Vec3 side = dir.cross(Vec3(0, 0, 1));
				if (side.length() < 1e-3) {
					side = dir.cross(Vec3(1, 0, 0));
				}
				side = side.normalized() * 6;
				links_.line(mid, mid - dir * 10 + side, on ? C_LINK_ON : C_LINK);
				links_.line(mid, mid - dir * 10 - side, on ? C_LINK_ON : C_LINK);
			}
		}
	}
	g_throw_brushes = nullptr;
	// a selected light's reach: where it adds about a sixteenth of full brightness (q3map's
	// inverse square: intensity times 7500 over the distance squared), or where a linear one ends
	for (Id e : sel) {
		const Object *o = doc.get(e);
		Vec3 at;
		if (!o || o->kind != Kind::Entity || o->entity.classname() != "light" || !parse_vec(o->entity.get("origin"), at)) {
			continue;
		}
		const double r = light_numbers(o->entity).reach;
		Vec3 c;
		if (!parse_vec(o->entity.get("_color", "1 1 1"), c)) {
			c = Vec3(1, 1, 1);
		}
		const double m = std::max({c.x, c.y, c.z, 1e-6});
		const unsigned char col[4] = {(unsigned char)(255 * c.x / m), (unsigned char)(255 * c.y / m), (unsigned char)(255 * c.z / m), 150};
		// a spotlight (a light with a target): its cone, out to the radius it has where it meets the target
		Vec3 aim;
		const std::string *tn = o->entity.find("target");
		if (tn && !tn->empty() && named.count(*tn) && entity_center(doc, named[*tn][0], aim) && (aim - at).length() > 1) {
			const Vec3 dir = (aim - at).normalized();
			Vec3 u = dir.cross(Vec3(0, 0, 1));
			if (u.length() < 1e-3) {
				u = dir.cross(Vec3(1, 0, 0));
			}
			u = u.normalized();
			const Vec3 v = dir.cross(u);
			const double rad = std::max(1.0, std::atof(o->entity.get("radius", "64").c_str()));
			Vec3 prev;
			for (int i = 0; i <= 32; i++) {
				const double a = i * 2 * PI / 32;
				const Vec3 p = aim + u * (std::cos(a) * rad) + v * (std::sin(a) * rad);
				if (i) {
					links_.line(prev, p, col);
				}
				if (i % 8 == 0) {
					links_.line(at, p, col);
				}
				prev = p;
			}
			continue;  // a spotlight's reach is its cone
		}
		for (int ring = 0; ring < 3; ring++) {
			Vec3 prev;
			for (int i = 0; i <= 48; i++) {
				const double a = i * 2 * PI / 48, x = std::cos(a) * r, y = std::sin(a) * r;
				const Vec3 p = at + (ring == 0 ? Vec3(x, y, 0) : ring == 1 ? Vec3(x, 0, y) : Vec3(0, x, y));
				if (i) {
					links_.line(prev, p, col);
				}
				prev = p;
			}
		}
	}
	links_.end();
}

// ---------------------------------------------------------------- the place tool

bool Session::place_point(const Hit &h, const std::string &cls, Vec3 &origin) const {
	const EntityDef *d = entity_def(cls);
	Vec3 lo(-8, -8, -8), hi(8, 8, 8);
	if (d) {
		lo = d->lo, hi = d->hi;
	}
	if (d && d->brush) {
		lo = Vec3(-PAD / 2, -PAD / 2, -PAD / 2), hi = Vec3(PAD / 2, PAD / 2, PAD / 2);
	}
	// the box against the surface: as far out along the normal as it reaches back
	const Vec3 n = h.normal;
	double reach = 0;
	for (int i = 0; i < 8; i++) {
		const Vec3 c((i & 1) ? hi.x : lo.x, (i & 2) ? hi.y : lo.y, (i & 4) ? hi.z : lo.z);
		reach = std::max(reach, -c.dot(n));
	}
	origin = h.point + n * (reach + (cls == "light" ? std::max(grid, 16.0) : 0));
	// on the grid across the surface
	const double ax[3] = {std::fabs(n.x), std::fabs(n.y), std::fabs(n.z)};
	const int k = ax[0] >= ax[1] && ax[0] >= ax[2] ? 0 : ax[1] >= ax[2] ? 1 : 2;
	if (k != 0) origin.x = snap(origin.x);
	if (k != 1) origin.y = snap(origin.y);
	if (k != 2) origin.z = snap(origin.z);
	return true;
}

// What the pointer is on for placing: a surface, else the ground grid.
static bool place_hit(Session &s, const std::function<bool(Hit &)> &pick, const std::function<bool(Vec3 &)> &ground, Hit &h) {
	(void)s;
	if (pick(h)) {
		return true;
	}
	Vec3 p;
	if (!ground(p)) {
		return false;
	}
	h = Hit();
	h.point = p;
	h.normal = Vec3(0, 0, 1);
	return true;
}

static std::string vec_text(const Vec3 &v) {
	return vec_key(v);
}

Id Session::place_entity(const std::string &cls, const Vec3 &origin, const char *label) {
	auto o = std::make_shared<Object>();
	o->kind = Kind::Entity;
	o->entity.set("classname", cls);
	o->entity.set("origin", vec_text(origin));
	const EntityDef *d = entity_def(cls);
	if (d && d->faces) {
		// facing the way the camera faces, to 15 degrees
		double yaw = std::fmod(std::round(cam.yaw / 15) * 15, 360.0);
		if (yaw < 0) {
			yaw += 360;
		}
		o->entity.set("angle", number(yaw));
	}
	if (cls == "light") {
		o->entity.set("light", "300");
		o->entity.set("_color", "1 1 1");  // its color control shows at once (white, as q3map2's default)
	}
	doc.begin(label);
	const Id id = doc.add(o);
	doc.end();
	return id;
}

Id Session::place_brush_entity(const std::string &cls, const Vec3 &lo, const Vec3 &hi, const std::string &material) {
	auto e = std::make_shared<Object>();
	e->kind = Kind::Entity;
	e->entity.set("classname", cls);
	const Id id = doc.add(e);
	doc.add(box_brush(doc, lo, hi, shader_id(material), id));
	return id;
}

double Session::ceiling_over(const Vec3 &a, const Vec3 &b) const {
	double low = 1e30;
	for (int i = 0; i <= 4; i++) {
		const Vec3 p = a + (b - a) * (i / 4.0) + Vec3(0, 0, 8);
		double frac;
		if (solid_hit(doc, p, p + Vec3(0, 0, 8192), frac)) {
			low = std::min(low, p.z + 8192 * frac);
		}
	}
	return low;
}

// The tools that take two clicks on the floor (a jump pad, a teleporter, Defrag's lines) and LAND HERE: their
// clicks go through entities, triggers and tool volumes to the floor under them.
static bool floor_tool(const std::string &cls) {
	return cls == "jump_pad" || cls == "teleporter" || cls == "jump_land" || cls == "door_button" || cls == "train_path" ||
		cls.compare(0, 7, "defrag_") == 0;
}

// A jump pad's throw, as the arc shows it: from the trigger's middle (AimAtTarget), down to the floor
// clicked. The player stands 10 units above the trigger's middle (its origin 24 over the pad's 4-unit
// top, the trigger's middle 18 up), so the arc that brings the player's origin to 24 over the floor
// brings the middle to 14 over it.

void Session::place_click(int m) {
	Hit h;
	const std::string cls = place_class;
	const bool through = floor_tool(cls);
	auto pick_fn = [&](Hit &out) { return pick_floor(mx_, my_, out, !through); };
	auto ground_fn = [&](Vec3 &out) { return plane_point(mx_, my_, Vec3(0, 0, 0), Vec3(0, 0, 1), out); };
	if (!place_hit(*this, pick_fn, ground_fn, h)) {
		return;
	}
	if (h.id && doc.get(h.id) && doc.get(h.id)->kind == Kind::Entity) {
		select(group_of(h.id), (m & SHIFT) ? SelMode::Add : SelMode::Set);  // an entity clicked: taken, not built on
		return;
	}
	if (mover_click(h)) {
		return;
	}
	if (cls == "jump_land") {
		// the selected pads' targets moved (or made) so their throws come down here
		std::vector<Id> pads;
		doc.begin("Land here");
		for (Id p : land_pushers_) {
			if (aim_pad(p, h.point)) {
				pads.push_back(p);
			}
		}
		doc.end();
		land_pushers_.clear();
		set_tool(Tool::Select);
		std::vector<Id> again;
		for (Id p : pads) {
			for (Id id : group_of(p)) {
				again.push_back(id);
			}
		}
		select(again, SelMode::Set);
		return;
	}
	if (cls.compare(0, 7, "defrag_") == 0) {
		// Defrag's lines: one side of the course, then the other; a speed pad where clicked; a shooter
		// where it shoots from, then the zone that fires it
		Vec3 at = h.point;
		at.x = snap(at.x), at.y = snap(at.y);
		if (cls == "defrag_speed") {
			place_defrag_speed(at);
			return;
		}
		if (place_step_ == 0) {
			if (cls == "defrag_shooter") {
				place_point(h, "shooter_rocket_targetplayer", at);
			}
			place_first_ = at;
			place_step_ = 1;
			return;
		}
		place_step_ = 0;
		if (cls == "defrag_shooter") {
			place_defrag_shooter(place_first_, at);
		} else {
			place_defrag_line(cls, place_first_, at);
		}
		return;
	}
	if (cls == "jump_pad" || cls == "teleporter") {
		Vec3 at = h.point;
		at.x = snap(at.x), at.y = snap(at.y);
		if (place_step_ == 0) {
			place_first_ = at;
			place_step_ = 1;
			return;
		}
		place_step_ = 0;
		const Vec3 a = place_first_;
		const std::string name = unique_targetname(doc, cls == "jump_pad" ? "pad" : "tele");
		doc.begin(cls == "jump_pad" ? "Jump pad" : "Teleporter");
		Id made;
		if (cls == "jump_pad") {
			// no block, only what plays: an invisible trigger where the player steps, and its target, the
			// top of the arc down to the second click; the two tied by JUMPPAD_KEY
			const Id trig = place_brush_entity("trigger_push", a + Vec3(-PAD / 2, -PAD / 2, PAD_LOW), a + Vec3(PAD / 2, PAD / 2, PAD_HIGH), "common/trigger");
			auto t = doc.edit(trig);
			t->entity.set("target", name);
			t->entity.set(JUMPPAD_KEY, name);
			doc.put(trig, t);
			const Vec3 top = apex_for(a + Vec3(0, 0, PAD_MIDDLE), h.point + Vec3(0, 0, LAND_MIDDLE), map_gravity(doc), ceiling_over(a, h.point));
			auto p = std::make_shared<Object>();
			p->kind = Kind::Entity;
			p->entity.keys = {{"classname", "target_position"}, {"targetname", name}, {"origin", vec_text(Vec3(std::round(top.x), std::round(top.y), std::round(top.z)))},
				{JUMPPAD_KEY, name}};
			made = doc.add(p);
		} else {
			const Id trig = place_brush_entity("trigger_teleport", a + Vec3(-PAD / 2, -PAD / 2, 0), a + Vec3(PAD / 2, PAD / 2, PAD), "common/trigger");
			auto t = doc.edit(trig);
			t->entity.set("target", name);
			doc.put(trig, t);
			Vec3 at2;
			place_point(h, "misc_teleporter_dest", at2);
			double yaw = std::fmod(std::round(cam.yaw / 15) * 15, 360.0);
			auto dst = std::make_shared<Object>();
			dst->kind = Kind::Entity;
			dst->entity.keys = {{"classname", "misc_teleporter_dest"}, {"targetname", name}, {"origin", vec_text(at2)},
				{"angle", number(yaw < 0 ? yaw + 360 : yaw)}};
			made = doc.add(dst);
		}
		doc.end();
		if (cls == "jump_pad") {
			set_tool(Tool::Select);  // its landing's handle ready to drag
			select(group_of(made), SelMode::Set);
			say("Jump pad made: drag the green ring to change where it lands, the green diamond at the top of the arc to throw it higher or lower; drag the pad to move it all");
		} else {
			select({made}, SelMode::Set);
		}
		return;
	}
	const EntityDef *d = entity_def(cls);
	Vec3 origin;
	place_point(h, cls, origin);
	Id made;
	if (d && d->brush) {
		doc.begin("Place " + cls);
		const bool trigger = cls.compare(0, 8, "trigger_") == 0;
		made = place_brush_entity(cls, origin - Vec3(PAD / 2, PAD / 2, PAD / 2), origin + Vec3(PAD / 2, PAD / 2, PAD / 2),
			trigger ? "common/trigger" : shader);
		add_pivot(made);
		doc.end();
		select(group_of(made), SelMode::Set);
		return;
	}
	made = place_entity(cls, origin, ("Place " + cls).c_str());
	select({made}, SelMode::Set);
}

void Session::build_place_preview() {
	place_preview_.begin();
	if (tool == Tool::Place && over_ && !looking_) {
		Hit h;
		const bool through = floor_tool(place_class);
		auto pick_fn = [&](Hit &out) { return pick_floor(mx_, my_, out, !through); };
		auto ground_fn = [&](Vec3 &out) { return plane_point(mx_, my_, Vec3(0, 0, 0), Vec3(0, 0, 1), out); };
		if (place_class == "jump_land" && place_hit(*this, pick_fn, ground_fn, h)) {
			// each pad's new throw, down to the pointer
			const double g = map_gravity(doc);
			for (Id p : land_pushers_) {
				Vec3 from, v;
				if (!doc.get(p) || !entity_center(doc, p, from)) {
					continue;
				}
				const Vec3 top = apex_for(from, h.point + Vec3(0, 0, LAND_MIDDLE), g, ceiling_over(from, h.point));
				Vec3 last = from;
				if (push_velocity(from, top, g, v)) {
					for (int i = 1; i <= 160; i++) {
						const double t = i * 0.04;
						const Vec3 q = from + v * t - Vec3(0, 0, 0.5 * g * t * t);
						place_preview_.line(last, q, C_ARC);
						last = q;
						if (q.z < h.point.z && v.z - g * t < 0) {
							break;
						}
					}
				}
			}
			place_preview_.box_edges(h.point - Vec3(16, 16, 0), h.point + Vec3(16, 16, 2), C_GHOST_EDGE);
		} else if (place_hit(*this, pick_fn, ground_fn, h)) {
			const std::string cls = place_class == "jump_pad" ? "trigger_push" : place_class == "teleporter" ? "trigger_teleport"
				: place_class.compare(0, 7, "defrag_") == 0 ? "trigger_multiple" : place_class;
			unsigned char col[4];
			entity_color(cls, col);
			const unsigned char fill[4] = {col[0], col[1], col[2], 60};
			if (place_class.compare(0, 7, "defrag_") == 0) {
				// the line so far: from the first click to the pointer
				Vec3 at = h.point;
				at.x = snap(at.x), at.y = snap(at.y);
				place_preview_.box_edges(at - Vec3(8, 8, 0), at + Vec3(8, 8, 64), C_GHOST_EDGE);
				if (place_step_) {
					place_preview_.line(place_first_ + Vec3(0, 0, 32), at + Vec3(0, 0, 32), C_LINK_ON);
					place_preview_.line(place_first_, at, C_LINK_ON);
				}
			} else if (place_class == "jump_pad" || place_class == "teleporter") {
				Vec3 at = h.point;
				at.x = snap(at.x), at.y = snap(at.y);
				const Vec3 a = place_step_ ? place_first_ : at;
				const bool pad = place_class == "jump_pad";
				const Vec3 lo = a + Vec3(-PAD / 2, -PAD / 2, pad ? PAD_LOW : 0), hi = a + Vec3(PAD / 2, PAD / 2, pad ? PAD_HIGH : PAD);
				place_preview_.box_faces(lo, hi, fill, "$oz_ed_tool");
				place_preview_.box_edges(lo, hi, C_GHOST_EDGE);
				if (place_step_ && place_class == "jump_pad") {
					// the throw the pad will make, down to the pointer
					const double g = map_gravity(doc);
					const Vec3 from = a + Vec3(0, 0, PAD_MIDDLE), top = apex_for(from, h.point + Vec3(0, 0, LAND_MIDDLE), g, ceiling_over(a, h.point));
					Vec3 v, last = from;
					if (push_velocity(from, top, g, v)) {
						for (int i = 1; i <= 120; i++) {
							const double t = i * 0.04;
							const Vec3 p = from + v * t - Vec3(0, 0, 0.5 * g * t * t);
							place_preview_.line(last, p, C_ARC);
							last = p;
							if (p.z < h.point.z) {
								break;
							}
						}
					}
				} else if (place_step_) {
					place_preview_.line(a + Vec3(0, 0, PAD / 2), h.point + Vec3(0, 0, 24), C_LINK_ON);
				}
			} else {
				const EntityDef *d = entity_def(cls);
				Vec3 o;
				place_point(h, cls, o);
				Vec3 lo = o + (d ? d->lo : Vec3(-8, -8, -8)), hi = o + (d ? d->hi : Vec3(8, 8, 8));
				if (d && d->brush) {
					lo = o - Vec3(PAD / 2, PAD / 2, PAD / 2), hi = o + Vec3(PAD / 2, PAD / 2, PAD / 2);
				}
				place_preview_.box_faces(lo, hi, fill, "$oz_ed_tool");
				place_preview_.box_edges(lo, hi, C_GHOST_EDGE);
			}
		}
	}
	place_preview_.end();
}

void Session::make_brush_entity(const std::string &cls) {
	std::vector<Id> brushes;
	for (Id id : sel) {
		const Object *o = doc.get(id);
		if (o && o->kind != Kind::Entity) {
			brushes.push_back(id);
		}
	}
	if (brushes.empty()) {
		say("Select the brushes it's made of first");
		return;
	}
	const bool trigger = cls.compare(0, 8, "trigger_") == 0;
	std::set<Id> was;
	doc.begin("Make " + cls);
	auto e = std::make_shared<Object>();
	e->kind = Kind::Entity;
	e->entity.set("classname", cls);
	const Id ent = doc.add(e);
	for (Id b : brushes) {
		auto o = doc.edit(b);
		if (o->owner != doc.worldspawn()) {
			was.insert(o->owner);
		}
		o->owner = ent;
		if (trigger && o->kind == Kind::Brush) {
			for (Face &f : o->brush.faces) {
				f.shader = shader_id("common/trigger");  // triggers are drawn as triggers; their numbers stay
			}
		}
		doc.put(b, o);
	}
	for (Id w : was) {
		if (doc.get(w) && doc.owned(w).empty()) {
			doc.remove(w);  // an entity left without brushes would compile to nothing
		}
	}
	add_pivot(ent);
	doc.end();
	select(group_of(ent), SelMode::Set);
}

std::string Session::place_hint() const {
	if (place_class == "defrag_speed") {
		return "Speed pad: click the floor where it goes (it launches the way the view faces) \xc2\xb7 Esc: back to select";
	}
	if (place_class == "defrag_shooter") {
		return place_step_ ? "Shooter: click the floor where a player sets it off (a 128 square zone) \xc2\xb7 Esc cancels"
			: "Shooter: click where it shoots from (a wall, a ceiling), then the zone that sets it off \xc2\xb7 Esc: back to select";
	}
	if (place_class.compare(0, 7, "defrag_") == 0) {
		const std::string what = place_class == "defrag_start" ? "Start line" : place_class == "defrag_finish" ? "Finish line" : "Checkpoint";
		return what + (place_step_ ? ": click the other side of the course (a trigger across it, with Defrag's timer) \xc2\xb7 Esc cancels"
			: ": click one side of the course, then the other \xc2\xb7 Esc: back to select");
	}
	if (place_class == "jump_pad") {
		return place_step_ ? "Jump pad: click where the player lands (the arc shows the throw) \xc2\xb7 Esc cancels"
			: "Jump pad: click the floor where the pad goes, then where the player lands \xc2\xb7 Esc: back to select";
	}
	if (place_class == "jump_land") {
		return "Land here: click where players should come down (the arc shows the throw) \xc2\xb7 Esc cancels";
	}
	if (place_class == "door_button" || place_class == "train_path") {
		return mover_hint();
	}
	if (place_class == "teleporter") {
		return place_step_ ? "Teleporter: click where the player comes out (facing the way the camera faces) \xc2\xb7 Esc cancels"
			: "Teleporter: click the entrance, then the exit \xc2\xb7 Esc: back to select";
	}
	const EntityDef *d = entity_def(place_class);
	const std::string what = place_class + (d ? ": " + d->help : "");
	if (d && d->brush) {
		return what + " \xc2\xb7 click puts a 64-unit brush of it (or select brushes, then pick it again, to make them one) \xc2\xb7 Esc: back to select";
	}
	return what + " \xc2\xb7 click where it goes (a floor, a wall, the ceiling) \xc2\xb7 Esc: back to select";
}

// ---------------------------------------------------------------- the actions

bool Session::entities_action(const std::string &action, const std::vector<std::string> &a) {
	if (action == "place") {
		const std::string cls = a.empty() ? "" : a[0];
		const EntityDef *d = entity_def(cls);
		if (cls.empty() || (!d && cls != "jump_pad" && cls != "teleporter")) {
			say("ed place <class>: a class of the Entities panel, jump_pad or teleporter", true);
			return true;
		}
		bool brushes = false;
		for (Id id : sel) {
			brushes |= doc.get(id) && doc.get(id)->kind == Kind::Brush;
		}
		if (d && d->brush && brushes && tool != Tool::Place) {
			make_brush_entity(cls);
			return true;
		}
		set_tool(Tool::Place);
		place_class = cls;
		place_step_ = 0;
	} else if (action == "push_height") {
		// ed push_height <units>: the selected jump pads' targets up (or down)
		push_height(arg(a, 0, 32));
	} else if (action == "push_land") {
		push_land();
	} else if (action == "to_world") {
		// the selected brushes back into the world; an entity left without brushes goes
		std::set<Id> was;
		std::vector<Id> moved;
		doc.begin("To the world");
		for (Id id : sel) {
			const Object *o = doc.get(id);
			if (o && o->kind != Kind::Entity && o->owner != doc.worldspawn()) {
				was.insert(o->owner);
				auto n = doc.edit(id);
				n->owner = doc.worldspawn();
				doc.put(id, n);
				moved.push_back(id);
			}
		}
		for (Id w : was) {
			if (doc.get(w) && doc.owned(w).empty()) {
				doc.remove(w);
			}
		}
		doc.end();
		if (moved.empty()) {
			say("Select brushes of an entity (a door, a trigger) to put them back in the world");
		} else {
			select(moved, SelMode::Set);
		}
	} else if (action == "flag") {
		// ed flag <bit>: the spawnflag on the selected entities, on or off
		const int bit = int(arg(a, 0, 0));
		const std::vector<Id> ents = key_entities();
		if (bit <= 0 || ents.empty()) {
			say("ed flag <bit>", true);
			return true;
		}
		bool all = true;
		for (Id e : ents) {
			all &= (std::atoi(doc.get(e)->entity.get("spawnflags", "0").c_str()) & bit) != 0;
		}
		doc.begin(all ? "Flag off" : "Flag on");
		for (Id e : ents) {
			const int now = std::atoi(doc.get(e)->entity.get("spawnflags", "0").c_str());
			const int next = all ? now & ~bit : now | bit;
			if (next) {
				set_key(doc, {e}, "spawnflags", std::to_string(next), "Flag");
			} else {
				remove_key(doc, {e}, "spawnflags", "Flag");
			}
		}
		doc.end();
	} else if (action == "spotlight" || action == "point_light") {
		// the selected lights made spotlights: an aim point (info_null) where they shine down onto the
		// floor, linked by name; or point lights again, their aim point gone when nothing else uses it
		std::vector<Id> lights;
		for (Id e : key_entities()) {
			if (doc.get(e) && doc.get(e)->entity.classname() == "light") {
				lights.push_back(e);
			}
		}
		if (lights.empty()) {
			say("Select a light first", true);
			return true;
		}
		doc.begin(action == "spotlight" ? "Make spotlight" : "Back to a point light");
		for (Id e : lights) {
			const Entity &en = doc.get(e)->entity;
			const std::string name = en.get("target");
			Vec3 at;
			if (action == "spotlight") {
				if (!name.empty() || !parse_vec(en.get("origin"), at)) {
					continue;
				}
				double frac = 1;
				const Vec3 down = at - Vec3(0, 0, 1024);
				const Vec3 aim = solid_hit(doc, at, down, frac) ? at + (down - at) * frac : at - Vec3(0, 0, 128);
				const std::string fresh = unique_targetname(doc, "spot");
				auto o = std::make_shared<Object>();
				o->kind = Kind::Entity;
				o->entity.keys = {{"classname", "info_null"}, {"targetname", fresh},
					{"origin", vec_text(Vec3(std::round(aim.x), std::round(aim.y), std::round(aim.z)))}};
				doc.add(o);
				set_key(doc, {e}, "target", fresh, "");
			} else if (!name.empty()) {
				remove_key(doc, {e}, "target", "");
				remove_key(doc, {e}, "radius", "");
				const Id t = entity_named(doc, name);
				int users = 0;
				for (Id other : doc.ids_of(Kind::Entity)) {
					users += doc.get(other)->entity.get("target") == name;
				}
				if (t && users == 0 && doc.get(t)->entity.classname() == "info_null") {
					doc.remove(t);
				}
			}
		}
		doc.end();
	} else if (action == "light_intensity" || action == "light_range" || action == "light_cone") {
		// the selected lights' intensity (a linear one keeps its range), range (0: back to the inverse
		// square), or a spotlight's cone (its radius where it meets its target)
		std::vector<Id> lights;
		for (Id e : key_entities()) {
			if (doc.get(e) && doc.get(e)->entity.classname() == "light") {
				lights.push_back(e);
			}
		}
		if (a.empty() || lights.empty()) {
			say(lights.empty() ? "Select a light first" : "ed " + action + " <units>", true);
			return true;
		}
		const double v = std::max(0.0, std::atof(a[0].c_str()));
		char num[32];
		auto put = [&](Id e, const char *key, double x) {
			std::snprintf(num, sizeof num, "%.4g", x);
			set_key(doc, {e}, key, num, "");
		};
		doc.begin(action == "light_intensity" ? "Light intensity " + a[0] : action == "light_range" ? (v > 0 ? "Light range " + a[0] : "Light range auto")
			: "Spotlight cone " + a[0]);
		for (Id e : lights) {
			const LightNumbers n = light_numbers(doc.get(e)->entity);
			if (action == "light_intensity") {
				put(e, n.key.c_str(), std::max(1.0, v));
				if (n.linear) {
					put(e, "fade", light_fade_for(std::max(1.0, v), n.scale, n.range));
				}
			} else if (action == "light_cone") {
				put(e, "radius", std::max(1.0, v));
			} else {
				const int flags = std::atoi(doc.get(e)->entity.get("spawnflags", "0").c_str());
				const int next = v > 0 ? flags | 1 : flags & ~1;
				if (next) {
					set_key(doc, {e}, "spawnflags", std::to_string(next), "");
				} else {
					remove_key(doc, {e}, "spawnflags", "");
				}
				if (v > 0) {
					put(e, "fade", light_fade_for(n.intensity, n.scale, v));
				} else {
					remove_key(doc, {e}, "fade", "");
				}
			}
		}
		doc.end();
	} else if (action == "key_new") {
		// ed key_new <key>: a key the class takes, at its default
		if (a.empty()) {
			return true;
		}
		const std::vector<Id> ents = key_entities();
		const KeyDef *k = ents.empty() ? nullptr : key_def(doc.get(ents[0])->entity.classname(), a[0]);
		const std::string value = k ? (k->type == KeyType::Bool ? "1" : k->def) : "";
		set_key(doc, ents, a[0], value, "Set " + a[0]);
	} else {
		return false;
	}
	return true;
}

}  // namespace oz::ed
