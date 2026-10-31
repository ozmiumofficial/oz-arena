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

static const unsigned char C_LINK[4] = {255, 214, 90, 130}, C_LINK_HIDDEN[4] = {255, 214, 90, 34}, C_LINK_ON[4] = {255, 250, 210, 255},
	C_ARC[4] = {110, 255, 150, 230}, C_ARC_HIDDEN[4] = {110, 255, 150, 60}, C_GHOST_EDGE[4] = {255, 170, 90, 255};

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

void Session::draw_models(const float moved[3], int now) {
	for (const EntModel &m : models_) {
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
static bool solid_hit(const Doc &doc, const Vec3 &a, const Vec3 &b, double &frac) {
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
	for (Id id : doc.ids_of(Kind::Brush)) {
		const Object *o = doc.get(id);
		const Object *owner = doc.get(o->owner);
		if (owner && owner->entity.classname().compare(0, 8, "trigger_") == 0) {
			continue;
		}
		const Bounds bb = bounds_of(doc, id);
		if (bb.empty() || !bb.overlaps(seg, 1)) {
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

const std::vector<Vec3> &Session::push_arc(Id id) {
	auto &cached = arcs_[id];
	if (cached.first == doc.revision + 1) {
		return cached.second;
	}
	cached.first = doc.revision + 1;
	cached.second.clear();
	const Object *o = doc.get(id);
	Vec3 from, to;
	if (!o || !entity_center(doc, id, from)) {
		return cached.second;
	}
	const std::string target = o->entity.get("target");
	bool found = false;
	for (Id e : doc.ids_of(Kind::Entity)) {
		const std::string *n = doc.get(e)->entity.find("targetname");
		if (n && *n == target && entity_center(doc, e, to)) {
			found = true;
			break;
		}
	}
	Vec3 v;
	const double g = map_gravity(doc);
	if (!found || !push_velocity(from, to, g, v)) {
		return cached.second;
	}
	// the throw, falling on until it meets a brush
	cached.second.push_back(from);
	Vec3 last = from;
	for (int i = 1; i <= 160; i++) {
		const double t = i * 0.04;
		const Vec3 p = from + v * t - Vec3(0, 0, 0.5 * g * t * t);
		double frac;
		if (solid_hit(doc, last, p, frac)) {
			cached.second.push_back(last + (p - last) * frac);
			break;
		}
		cached.second.push_back(p);
		last = p;
		if (p.z < from.z - 4096) {
			break;
		}
	}
	return cached.second;
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

void Session::push_height(double dz) {
	// the selected pushers' targets (target_position, info_notnull): up or down by dz
	std::set<std::string> names;
	for (Id id : key_entities()) {
		const Entity &e = doc.get(id)->entity;
		if ((e.classname() == "trigger_push" || e.classname() == "target_push") && !e.get("target").empty()) {
			names.insert(e.get("target"));
		}
	}
	std::vector<Id> targets;
	for (Id e : doc.ids_of(Kind::Entity)) {
		const std::string *n = doc.get(e)->entity.find("targetname");
		Vec3 o;
		if (n && names.count(*n) && parse_vec(doc.get(e)->entity.get("origin"), o)) {
			targets.push_back(e);
		}
	}
	if (targets.empty()) {
		say("Select a jump pad (its trigger_push) with a target to throw at");
		return;
	}
	doc.begin(dz > 0 ? "Jump higher" : "Jump lower");
	for (Id e : targets) {
		auto o = doc.edit(e);
		Vec3 at;
		parse_vec(o->entity.get("origin"), at);
		o->entity.set("origin", number(at.x) + " " + number(at.y) + " " + number(at.z + dz));
		doc.put(e, o);
	}
	doc.end();
}

void Session::build_links() {
	if (doc.revision == links_doc_ && sel_rev == links_sel_) {
		return;
	}
	links_doc_ = doc.revision;
	links_sel_ = sel_rev;
	links_.begin();
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
		if (cn == "trigger_push" || cn == "target_push") {
			// a pusher shows the throw it makes, not a line to the top of it
			const std::vector<Vec3> &arc = push_arc(e);
			for (size_t i = 1; i < arc.size(); i++) {
				links_.line(arc[i - 1], arc[i], C_ARC);
				links_.line(arc[i - 1], arc[i], C_ARC_HIDDEN, "$oz_ed_lines_xray");
			}
			if (arc.size() > 1) {
				continue;
			}
		}
		for (Id to : named[*t]) {
			Vec3 at;
			if (!entity_center(doc, to, at)) {
				continue;
			}
			const bool on = sel.count(e) || sel.count(to);
			links_.line(from, at, on ? C_LINK_ON : C_LINK);
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
	// a selected light's reach: where it adds about a sixteenth of full brightness (q3map's
	// inverse square: intensity times 7500 over the distance squared)
	for (Id e : sel) {
		const Object *o = doc.get(e);
		Vec3 at;
		if (!o || o->kind != Kind::Entity || o->entity.classname() != "light" || !parse_vec(o->entity.get("origin"), at)) {
			continue;
		}
		const double l = std::fabs(std::atof(o->entity.get("light", "300").c_str()));
		const double r = std::sqrt(l * 7500 / 16);
		Vec3 c;
		if (!parse_vec(o->entity.get("_color", "1 1 1"), c)) {
			c = Vec3(1, 1, 1);
		}
		const double m = std::max({c.x, c.y, c.z, 1e-6});
		const unsigned char col[4] = {(unsigned char)(255 * c.x / m), (unsigned char)(255 * c.y / m), (unsigned char)(255 * c.z / m), 150};
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

void Session::place_click(int m) {
	Hit h;
	auto pick_fn = [&](Hit &out) { return pick(mx_, my_, out); };
	auto ground_fn = [&](Vec3 &out) { return plane_point(mx_, my_, Vec3(0, 0, 0), Vec3(0, 0, 1), out); };
	if (!place_hit(*this, pick_fn, ground_fn, h)) {
		return;
	}
	if (h.id && doc.get(h.id) && doc.get(h.id)->kind == Kind::Entity) {
		select(group_of(h.id), (m & SHIFT) ? SelMode::Add : SelMode::Set);  // an entity clicked: taken, not built on
		return;
	}
	const std::string cls = place_class;
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
			// a 4-unit pad under a 36-unit trigger, its target the top of the arc down to the second click
			// (the player rides 24 above the floor; AimAtTarget throws from the trigger's middle)
			doc.add(box_brush(doc, a + Vec3(-PAD / 2, -PAD / 2, 0), a + Vec3(PAD / 2, PAD / 2, 4), shader_id(shader), doc.worldspawn()));
			const Id trig = place_brush_entity("trigger_push", a + Vec3(-PAD / 2, -PAD / 2, 0), a + Vec3(PAD / 2, PAD / 2, 36), "common/trigger");
			auto t = doc.edit(trig);
			t->entity.set("target", name);
			doc.put(trig, t);
			const Vec3 top = apex_for(a + Vec3(0, 0, 18), h.point + Vec3(0, 0, 14), map_gravity(doc), ceiling_over(a, h.point));
			auto p = std::make_shared<Object>();
			p->kind = Kind::Entity;
			p->entity.keys = {{"classname", "target_position"}, {"targetname", name}, {"origin", vec_text(Vec3(std::round(top.x), std::round(top.y), std::round(top.z)))}};
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
		select({made}, SelMode::Set);
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
		auto pick_fn = [&](Hit &out) { return pick(mx_, my_, out); };
		auto ground_fn = [&](Vec3 &out) { return plane_point(mx_, my_, Vec3(0, 0, 0), Vec3(0, 0, 1), out); };
		if (place_hit(*this, pick_fn, ground_fn, h)) {
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
				const double tall = place_class == "jump_pad" ? 36 : PAD;
				place_preview_.box_faces(a + Vec3(-PAD / 2, -PAD / 2, 0), a + Vec3(PAD / 2, PAD / 2, tall), fill, "$oz_ed_tool");
				place_preview_.box_edges(a + Vec3(-PAD / 2, -PAD / 2, 0), a + Vec3(PAD / 2, PAD / 2, tall), C_GHOST_EDGE);
				if (place_step_ && place_class == "jump_pad") {
					// the throw the pad will make, down to the pointer
					const double g = map_gravity(doc);
					const Vec3 from = a + Vec3(0, 0, 18), top = apex_for(from, h.point + Vec3(0, 0, 14), g, ceiling_over(a, h.point));
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
			: "Jump pad: click where the pad goes, then where the player lands \xc2\xb7 Esc: back to select";
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
