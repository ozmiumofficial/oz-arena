// Defrag's tools in the map editor (ENGINE_PLAN.md P13, engine/EDITOR.md): a course is Defrag's own
// entities, as its defrag.def documents them (the player's install): a trigger_multiple across the
// course fires target_startTimer (the start line), target_checkpoint (a checkpoint) or
// target_stopTimer (the finish). The line tools put a trigger across the course between two clicks
// with its timer entity; the course check lists what's missing or unlinked; the .defi file is
// Defrag's map info (scripts/<map>.defi: the map, its long name, its style, its author), which
// Defrag's menus read. Play-tests under Defrag are the compile's (ed_compile.cpp).

#include "ed_view.h"
#include "ed_engine.h"
#include "ed_mapfile.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <array>
#include <cstring>
#include <deque>
#include <map>
#include <unordered_map>

namespace oz::ed {

static const char *const START = "target_startTimer", *const STOP = "target_stopTimer", *const CHECKPOINT = "target_checkpoint";

static bool is_spawn(const std::string &cn) {
	return cn == "info_player_deathmatch" || cn == "info_player_start";
}

// ---------------------------------------------------------------- reachability

// What stops a player: brushes but those of triggers and movers (doors, platforms and the like move
// out of the way), and of nothing but hint, trigger, fog, origin or portal faces.
struct Solid {
	Bounds b;
	std::vector<Plane> planes;
};

static bool mover(const std::string &cn) {
	static const char *movers[] = {"func_door", "func_plat", "func_button", "func_train", "func_bobbing", "func_rotating", "func_pendulum"};
	for (const char *m : movers) {
		if (cn == m) {
			return true;
		}
	}
	return cn.compare(0, 8, "trigger_") == 0;
}

static std::vector<Solid> solids_of(const Doc &doc) {
	std::vector<Solid> out;
	for (Id id : doc.ids_of(Kind::Brush)) {
		const Object *o = doc.get(id);
		const Object *owner = doc.get(o->owner);
		if (owner && o->owner != doc.worldspawn() && mover(owner->entity.classname())) {
			continue;
		}
		bool solid = false;
		Solid s;
		for (const Face &f : o->brush.faces) {
			const Look l = shaders_lib().look(shader_name(f.shader));
			solid |= l != Look::Hint && l != Look::Trigger && l != Look::Fog && l != Look::Origin && l != Look::Portal;
			s.planes.push_back(f.plane);
		}
		s.b = bounds_of(doc, id);
		if (solid && !s.b.empty()) {
			out.push_back(std::move(s));
		}
	}
	return out;
}

static bool inside(const Solid &s, const Vec3 &p) {
	for (const Plane &pl : s.planes) {
		if (pl.dist(p) >= -0.01) {
			return false;
		}
	}
	return true;
}

// The segment passes through the brush's inside (Cyrus-Beck): touching a face doesn't count.
static bool crosses(const Solid &s, const Vec3 &a, const Vec3 &b) {
	double t0 = 0, t1 = 1;
	for (const Plane &pl : s.planes) {
		const double da = pl.dist(a), db = pl.dist(b);
		if (da >= -0.01 && db >= -0.01) {
			return false;
		}
		if (da >= -0.01) {
			t0 = std::max(t0, da / (da - db));
		} else if (db >= -0.01) {
			t1 = std::min(t1, da / (da - db));
		}
		if (t0 > t1 - 1e-9) {
			return false;
		}
	}
	return true;
}

// The open space a player could be in, as cells (32 units, more on a big map), flooded from the spawn
// points through every opening, a teleporter's trigger leading on to its destination. Which of
// targets (trigger entities) the flood touches. Not a physics test: a jump too high still counts as
// a way; a line walled in, or behind a sealed wall, doesn't.
static std::set<Id> reached_triggers(const Doc &doc, const std::vector<Id> &targets) {
	const std::vector<Solid> solids = solids_of(doc);
	Bounds all;
	for (Id id : doc.ids()) {
		all.add(bounds_of(doc, id));
	}
	std::set<Id> out;
	if (all.empty()) {
		return out;
	}
	double cell = 32;
	auto cells_along = [&](double lo, double hi) { return int(std::ceil((hi - lo) / cell)) + 2; };
	for (;;) {
		const double n = double(cells_along(all.lo.x, all.hi.x)) * cells_along(all.lo.y, all.hi.y) * cells_along(all.lo.z, all.hi.z);
		if (n <= 2.5e6) {
			break;
		}
		cell *= 2;
	}
	const Vec3 lo(std::floor(all.lo.x / cell) * cell - cell, std::floor(all.lo.y / cell) * cell - cell, std::floor(all.lo.z / cell) * cell - cell);
	const int nx = cells_along(lo.x, all.hi.x) + 1, ny = cells_along(lo.y, all.hi.y) + 1, nz = cells_along(lo.z, all.hi.z) + 1;
	auto center = [&](int x, int y, int z) { return Vec3(lo.x + (x + 0.5) * cell, lo.y + (y + 0.5) * cell, lo.z + (z + 0.5) * cell); };
	auto index = [&](int x, int y, int z) { return (size_t(z) * size_t(ny) + size_t(y)) * size_t(nx) + size_t(x); };
	// the solids by 256-unit buckets, so a cell asks only those near it
	const double B = 256;
	std::unordered_map<long long, std::vector<int>> buckets;
	auto key = [&](int bx, int by, int bz) { return ((long long)(bx + 1024) << 40) | ((long long)(by + 1024) << 20) | (long long)(bz + 1024); };
	for (size_t i = 0; i < solids.size(); i++) {
		const Bounds &b = solids[i].b;
		for (int bx = int(std::floor((b.lo.x - 1) / B)); bx <= int(std::floor((b.hi.x + 1) / B)); bx++) {
			for (int by = int(std::floor((b.lo.y - 1) / B)); by <= int(std::floor((b.hi.y + 1) / B)); by++) {
				for (int bz = int(std::floor((b.lo.z - 1) / B)); bz <= int(std::floor((b.hi.z + 1) / B)); bz++) {
					buckets[key(bx, by, bz)].push_back(int(i));
				}
			}
		}
	}
	static const std::vector<int> none;
	auto near = [&](const Vec3 &p) -> const std::vector<int> & {
		auto it = buckets.find(key(int(std::floor(p.x / B)), int(std::floor(p.y / B)), int(std::floor(p.z / B))));
		return it == buckets.end() ? none : it->second;
	};
	auto open = [&](const Vec3 &p) {
		for (int i : near(p)) {
			if (inside(solids[size_t(i)], p)) {
				return false;
			}
		}
		return true;
	};
	auto passes = [&](const Vec3 &a, const Vec3 &b) {
		for (const Vec3 *p : {&a, &b}) {
			for (int i : near(*p)) {
				if (crosses(solids[size_t(i)], a, b)) {
					return false;
				}
			}
		}
		return true;
	};
	// teleporters: their trigger's box, where they lead
	struct Tele {
		Bounds b;
		Vec3 to;
		bool used = false;
	};
	std::vector<Tele> teles;
	std::map<std::string, Vec3> names;
	for (Id e : doc.ids_of(Kind::Entity)) {
		const Entity &en = doc.get(e)->entity;
		Vec3 at;
		if (const std::string *n = en.find("targetname")) {
			if (parse_vec(en.get("origin"), at)) {
				names[*n] = at;
			}
		}
	}
	for (Id e : doc.ids_of(Kind::Entity)) {
		const Entity &en = doc.get(e)->entity;
		const std::string *t = en.find("target");
		if (en.classname() == "trigger_teleport" && t && names.count(*t)) {
			teles.push_back({bounds_of(doc, doc.owned(e)), names[*t]});
		}
	}
	std::vector<uint8_t> seen(size_t(nx) * size_t(ny) * size_t(nz), 0);
	std::deque<std::array<int, 3>> queue;
	auto cell_of = [&](const Vec3 &p, int c[3]) {
		c[0] = std::clamp(int(std::floor((p.x - lo.x) / cell)), 0, nx - 1);
		c[1] = std::clamp(int(std::floor((p.y - lo.y) / cell)), 0, ny - 1);
		c[2] = std::clamp(int(std::floor((p.z - lo.z) / cell)), 0, nz - 1);
	};
	auto seed = [&](const Vec3 &p) {
		int c[3];
		cell_of(p, c);
		// the cell it's in, or the open one nearest above (a spawn sits just over the floor)
		for (int up = 0; up < 3 && c[2] + up < nz; up++) {
			const size_t i = index(c[0], c[1], c[2] + up);
			if (!seen[i] && open(center(c[0], c[1], c[2] + up))) {
				seen[i] = 1;
				queue.push_back({c[0], c[1], c[2] + up});
				return;
			}
		}
	};
	for (Id e : doc.ids_of(Kind::Entity)) {
		const Entity &en = doc.get(e)->entity;
		Vec3 at;
		if (is_spawn(en.classname()) && parse_vec(en.get("origin"), at)) {
			seed(at);
		}
	}
	static const int step[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
	while (!queue.empty()) {
		const auto c = queue.front();
		queue.pop_front();
		const Vec3 here = center(c[0], c[1], c[2]);
		for (Tele &t : teles) {
			if (!t.used && here.x >= t.b.lo.x - cell / 2 && here.x <= t.b.hi.x + cell / 2 && here.y >= t.b.lo.y - cell / 2 && here.y <= t.b.hi.y + cell / 2 &&
					here.z >= t.b.lo.z - cell / 2 && here.z <= t.b.hi.z + cell / 2) {
				t.used = true;
				seed(t.to);
			}
		}
		for (const auto &s : step) {
			const int x = c[0] + s[0], y = c[1] + s[1], z = c[2] + s[2];
			if (x < 0 || y < 0 || z < 0 || x >= nx || y >= ny || z >= nz || seen[index(x, y, z)]) {
				continue;
			}
			const Vec3 there = center(x, y, z);
			if (open(there) && passes(here, there)) {
				seen[index(x, y, z)] = 1;
				queue.push_back({x, y, z});
			}
		}
	}
	// a target is reached when a flooded cell's middle is within half a cell of its box
	for (Id t : targets) {
		const Bounds b = bounds_of(doc, doc.owned(t));
		if (b.empty()) {
			continue;
		}
		int c0[3], c1[3];
		cell_of(b.lo - Vec3(cell / 2, cell / 2, cell / 2), c0);
		cell_of(b.hi + Vec3(cell / 2, cell / 2, cell / 2), c1);
		bool hit = false;
		for (int z = c0[2]; z <= c1[2] && !hit; z++) {
			for (int y = c0[1]; y <= c1[1] && !hit; y++) {
				for (int x = c0[0]; x <= c1[0] && !hit; x++) {
					hit = seen[index(x, y, z)] != 0;
				}
			}
		}
		if (hit) {
			out.insert(t);
		}
	}
	return out;
}

Id Session::place_defrag_line(const std::string &kind, const Vec3 &a, const Vec3 &b) {
	const bool start = kind == "defrag_start", finish = kind == "defrag_finish";
	const std::string cls = start ? START : finish ? STOP : CHECKPOINT, prefix = start ? "start" : finish ? "finish" : "checkpoint";
	Vec3 d = b - a;
	d.z = 0;
	if (d.length() < 8) {
		say("A line needs its two ends apart: click one side of the course, then the other", true);
		return 0;
	}
	// the trigger: 16 thick across the line, from the floor up to the ceiling (256 at most)
	const Vec3 along = d.normalized(), across(-along.y, along.x, 0);
	const double floor = std::min(a.z, b.z), roof = ceiling_over(a, b);
	const double top = std::max(floor + 64, std::min(roof < 1e29 ? roof : floor + 256, floor + 256));
	std::vector<Vec3> pts;
	Bounds box;
	for (const Vec3 &e : {a, b}) {
		for (double s : {-8.0, 8.0}) {
			for (double z : {floor, top}) {
				pts.push_back(Vec3(e.x, e.y, z) + across * s);
				box.add(pts.back());
			}
		}
	}
	const std::string name = unique_targetname(doc, prefix);
	doc.begin(start ? "Start line" : finish ? "Finish line" : "Checkpoint");
	auto trig = std::make_shared<Object>();
	trig->kind = Kind::Entity;
	trig->entity.keys = {{"classname", "trigger_multiple"}, {"target", name}};
	const Id t = doc.add(trig);
	auto plain = box_brush(doc, box.lo, box.hi, shader_id("common/trigger"), t);
	auto turned = from_points(doc, *plain, pts, doc.model_origin(t));  // across the line however it runs
	const Id vol = doc.add(turned ? turned : plain);
	// the timer entity it fires, at the line's middle
	const Vec3 mid = (a + b) * 0.5;
	auto timer = std::make_shared<Object>();
	timer->kind = Kind::Entity;
	timer->entity.keys = {{"classname", cls}, {"targetname", name},
		{"origin", number(std::round(mid.x)) + " " + number(std::round(mid.y)) + " " + number(std::round(floor + 32))}};
	const Id tm = doc.add(timer);
	doc.end();
	corners.clear();
	select({t, vol, tm}, SelMode::Set);
	say((start ? "Start line " : finish ? "Finish line " : "Checkpoint ") + name + ": crossing it fires Defrag's " + cls);
	return t;
}

Id Session::place_defrag_speed(const Vec3 &at) {
	// a pad: a trigger_multiple on the floor firing Defrag's target_speed, launching the player the way
	// the view faces (its nearest axis), 700 a second; the inspector sets the rest
	const Vec3 f = camera_axis(false);
	const int dir = std::fabs(f.x) >= std::fabs(f.y) ? (f.x >= 0 ? 4 : 8) : (f.y >= 0 ? 16 : 32);
	const std::string name = unique_targetname(doc, "speed");
	doc.begin("Speed pad");
	auto sp = std::make_shared<Object>();
	sp->kind = Kind::Entity;
	sp->entity.keys = {{"classname", "target_speed"}, {"targetname", name}, {"speed", "700"}, {"spawnflags", std::to_string(256 + dir)},
		{"origin", number(at.x) + " " + number(at.y) + " " + number(at.z + 48)}};
	const Id speed = doc.add(sp);
	auto trig = std::make_shared<Object>();
	trig->kind = Kind::Entity;
	trig->entity.keys = {{"classname", "trigger_multiple"}, {"target", name}};
	const Id t = doc.add(trig);
	const Id vol = doc.add(box_brush(doc, at - Vec3(32, 32, 0), at + Vec3(32, 32, 32), shader_id("common/trigger"), t));
	doc.end();
	corners.clear();
	select({speed, t, vol}, SelMode::Set);
	say("Speed pad " + name + ": stepping on it launches the player " + (dir == 4 ? "+X" : dir == 8 ? "-X" : dir == 16 ? "+Y" : "-Y") + " at 700 (Defrag's target_speed)");
	return speed;
}

Id Session::place_defrag_shooter(const Vec3 &from, const Vec3 &zone) {
	// a rocket shooter aimed at the player (leading them across), fired while they're in the zone
	const std::string name = unique_targetname(doc, "shooter");
	doc.begin("Shooter");
	auto sh = std::make_shared<Object>();
	sh->kind = Kind::Entity;
	sh->entity.keys = {{"classname", "shooter_rocket_targetplayer"}, {"targetname", name}, {"spawnflags", "3"},
		{"origin", number(std::round(from.x)) + " " + number(std::round(from.y)) + " " + number(std::round(from.z))}};
	const Id shooter = doc.add(sh);
	auto trig = std::make_shared<Object>();
	trig->kind = Kind::Entity;
	trig->entity.keys = {{"classname", "trigger_multiple"}, {"target", name}, {"wait", "1"}};
	const Id t = doc.add(trig);
	const Id vol = doc.add(box_brush(doc, zone - Vec3(64, 64, 0), zone + Vec3(64, 64, 64), shader_id("common/trigger"), t));
	doc.end();
	corners.clear();
	select({shooter, t, vol}, SelMode::Set);
	say("Shooter " + name + ": a rocket a second at a player in its zone, leading them (the inspector switches plasma or grenade, prediction, quad)");
	return shooter;
}

std::string Session::course_summary(std::vector<CompileWarning> *problems) const {
	std::vector<Id> starts, stops, cps, spawns;
	std::map<std::string, std::vector<Id>> named, firing;  // targetname -> entities; target -> the entities firing it
	for (Id id : doc.ids_of(Kind::Entity)) {
		const Entity &e = doc.get(id)->entity;
		const std::string cn = e.classname();
		if (cn == START) {
			starts.push_back(id);
		} else if (cn == STOP) {
			stops.push_back(id);
		} else if (cn == CHECKPOINT) {
			cps.push_back(id);
		} else if (is_spawn(cn)) {
			spawns.push_back(id);
		}
		if (const std::string *n = e.find("targetname")) {
			named[*n].push_back(id);
		}
		if (const std::string *t = e.find("target")) {
			firing[*t].push_back(id);
		}
	}
	auto add = [&](const std::string &text, std::vector<Id> ids) {
		if (problems) {
			CompileWarning w;
			w.text = "Course: " + text;
			for (Id id : ids) {
				for (Id g : group_of(id)) {
					w.ids.push_back(g);
				}
			}
			problems->push_back(w);
		}
	};
	int n = 0;
	auto count = [&](const std::string &text, std::vector<Id> ids) {
		n++;
		add(text, ids);
	};
	if (starts.empty() && stops.empty() && cps.empty()) {
		return "no course";
	}
	if (starts.empty()) {
		count("no start line (Start line puts one across the course)", {});
	}
	if (stops.empty()) {
		count("no finish line (Finish line puts one across the course)", {});
	}
	if (spawns.empty()) {
		count("no spawn point to start from (an info_player_deathmatch)", {});
	}
	// a timer nothing fires does nothing
	for (const auto *list : {&starts, &cps, &stops}) {
		for (Id id : *list) {
			const Entity &e = doc.get(id)->entity;
			const std::string *tn = e.find("targetname");
			if (!tn || tn->empty() || !firing.count(*tn)) {
				count(e.classname() + (tn && !tn->empty() ? " " + *tn : "") + ": no trigger fires it", {id});
			}
		}
	}
	// a line nothing open reaches from the spawn (only for the full check: it floods the map)
	if (problems && !spawns.empty()) {
		std::vector<Id> lines;
		for (const auto *list : {&starts, &cps, &stops}) {
			for (Id id : *list) {
				const std::string *tn = doc.get(id)->entity.find("targetname");
				if (tn && firing.count(*tn)) {
					for (Id trig : firing[*tn]) {
						lines.push_back(trig);
					}
				}
			}
		}
		const std::set<Id> reached = reached_triggers(doc, lines);
		for (const auto *list : {&starts, &cps, &stops}) {
			for (Id id : *list) {
				const Entity &e = doc.get(id)->entity;
				const std::string *tn = e.find("targetname");
				if (!tn || !firing.count(*tn)) {
					continue;
				}
				bool any = false;
				std::vector<Id> trigs = firing[*tn];
				for (Id trig : trigs) {
					any |= reached.count(trig) != 0;
				}
				if (!any) {
					trigs.push_back(id);
					count(e.classname() + " " + *tn + ": no open way to its line from the spawn point (walled off?)", trigs);
				}
			}
		}
	}
	// a trigger whose target names nothing fires nothing
	for (Id id : doc.ids_of(Kind::Entity)) {
		const Entity &e = doc.get(id)->entity;
		const std::string *t = e.find("target");
		if (t && !t->empty() && e.classname().compare(0, 8, "trigger_") == 0 && !named.count(*t)) {
			count(e.classname() + ": its target " + *t + " is nothing's name", {id});
		}
	}
	return "starts " + std::to_string(starts.size()) + " checkpoints " + std::to_string(cps.size()) + " finishes " + std::to_string(stops.size()) +
		" problems " + std::to_string(n);
}

void Session::defrag_check() {
	std::vector<CompileWarning> problems;
	const std::string sum = course_summary(&problems);
	if (sum == "no course") {
		say("No course here: Start line, Checkpoint and Finish line (the Entities panel) make one");
		return;
	}
	warnings_ = problems;
	if (problems.empty()) {
		int cps = 0;
		for (Id id : doc.ids_of(Kind::Entity)) {
			cps += doc.get(id)->entity.classname() == CHECKPOINT;
		}
		say("The course checks out: a start, " + std::to_string(cps) + (cps == 1 ? " checkpoint" : " checkpoints") + " and a finish, each fired by its trigger");
		return;
	}
	want_warnings = true;  // the list, each framed when picked
	say(std::to_string(problems.size()) + (problems.size() == 1 ? " thing to fix in the course" : " things to fix in the course"), true);
}

std::string Session::defrag_defi(const std::string &map) const {
	// Defrag's map info, as the .defi files in its own paks have it
	const Object *w = doc.get(doc.worldspawn());
	auto clean = [](std::string s) {
		std::replace(s.begin(), s.end(), '"', '\'');
		std::replace(s.begin(), s.end(), '\n', ' ');
		return s;
	};
	const std::string message = w ? clean(w->entity.get("message")) : "";
	std::string author = w ? clean(w->entity.get("author", w->entity.get("_author"))) : "";
	bool start = false, stop = false;
	for (Id id : doc.ids_of(Kind::Entity)) {
		const std::string cn = doc.get(id)->entity.classname();
		start |= cn == START;
		stop |= cn == STOP;
	}
	return "{\nmap\t\t\"" + map + "\"\nlongname\t\"" + (message.empty() ? map : message) + "\"\nstyle\t\t\"" + (start && stop ? "run" : "freestyle") +
		"\"\nauthor\t\t\"" + author + "\"\n}\n";
}

void Session::write_defi_beside(const std::string &mapfile) const {
	// a map saved into a maps folder: its .defi in the scripts folder beside (a pk3's layout), else beside it
	if (course_summary() == "no course") {
		return;
	}
	const size_t slash = mapfile.find_last_of("/\\");
	const std::string dir = slash == std::string::npos ? "." : mapfile.substr(0, slash);
	std::string stem = mapfile.substr(slash == std::string::npos ? 0 : slash + 1);
	stem = stem.substr(0, stem.find_last_of('.'));
	std::string folder = dir.substr(dir.find_last_of("/\\") + 1);
	std::transform(folder.begin(), folder.end(), folder.begin(), [](unsigned char c) { return char(std::tolower(c)); });
	const std::string to = folder == "maps" ? dir.substr(0, dir.find_last_of("/\\")) + "/scripts/" + stem + ".defi" : dir + "/" + stem + ".defi";
	const std::string text = defrag_defi(stem);
	OzEd_MakeDirs(to.c_str());
	OzEd_WriteOSFile(to.c_str(), text.data(), int(text.size()));
}

bool Session::defrag_ready(std::string &why) const {
	// Defrag's game code, where a mod switch to defrag finds it: the user folder's defrag, or the install's
	auto has = [](const std::string &dir) {
		int n = 0;
		bool found = false;
		char **files = OzEd_ListOSFiles(dir.c_str(), "pk3", &n);
		for (int i = 0; i < n; i++) {
			std::string f = files[i];
			std::transform(f.begin(), f.end(), f.begin(), [](unsigned char c) { return char(std::tolower(c)); });
			found |= f.find("defrag") != std::string::npos;
		}
		OzEd_FreeList(files);
		return found;
	};
	const std::string base = OzEd_BaseQ3(), install = base.substr(0, base.find_last_of("/\\"));
	if (has(std::string(OzEd_HomePath()) + "/defrag") || (!base.empty() && has(install + "/defrag"))) {
		return true;
	}
	why = !base.empty() && has(install + "/defrag/defrag")
		? "Defrag is in its bundle's own folder (" + install + "/defrag/defrag), which Oz doesn't play from yet: put its pk3s in " + install + "/defrag"
		: std::string("Defrag isn't in the game's folder: Oz plays it from your own install (<Quake 3>/defrag)");
	return false;
}

}  // namespace oz::ed
