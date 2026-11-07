#include "ed_ops.h"
#include "ed_entdefs.h"
#include "ed_mapfile.h"
#include "ed_texmap.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <unordered_map>

namespace oz::ed {

static const double PI = 3.14159265358979323846;

// ---------------------------------------------------------------- bounds

void Bounds::add(const Vec3 &p) {
	lo = Vec3(std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z));
	hi = Vec3(std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z));
}

void Bounds::add(const Bounds &b) {
	if (!b.empty()) {
		add(b.lo);
		add(b.hi);
	}
}

bool Bounds::overlaps(const Bounds &b, double eps) const {
	return !empty() && !b.empty() && lo.x <= b.hi.x + eps && hi.x >= b.lo.x - eps && lo.y <= b.hi.y + eps &&
		hi.y >= b.lo.y - eps && lo.z <= b.hi.z + eps && hi.z >= b.lo.z - eps;
}

// ---------------------------------------------------------------- transforms

Affine translation(const Vec3 &t) {
	Affine a;
	a.t = t;
	return a;
}

static void exact_turn(double degrees, double &c, double &s) {
	double r = std::fmod(degrees, 360.0);
	if (r < 0) {
		r += 360;
	}
	if (r == 0) {
		c = 1, s = 0;
	} else if (r == 90) {
		c = 0, s = 1;
	} else if (r == 180) {
		c = -1, s = 0;
	} else if (r == 270) {
		c = 0, s = -1;
	} else {
		c = std::cos(r * PI / 180), s = std::sin(r * PI / 180);
	}
}

static void finish_about(Affine &a, const Vec3 &pivot) {
	// x' = M (x - p) + p
	Vec3 mp(a.m[0][0] * pivot.x + a.m[0][1] * pivot.y + a.m[0][2] * pivot.z,
		a.m[1][0] * pivot.x + a.m[1][1] * pivot.y + a.m[1][2] * pivot.z,
		a.m[2][0] * pivot.x + a.m[2][1] * pivot.y + a.m[2][2] * pivot.z);
	a.t = pivot - mp;
}

Affine rotation(int axis, double degrees, const Vec3 &pivot) {
	double c, s;
	exact_turn(degrees, c, s);
	Affine a;
	const int i = (axis + 1) % 3, j = (axis + 2) % 3;  // counter-clockwise looking down the axis
	a.m[i][i] = c, a.m[i][j] = -s;
	a.m[j][i] = s, a.m[j][j] = c;
	finish_about(a, pivot);
	return a;
}

Affine mirror(int axis, const Vec3 &pivot) {
	Affine a;
	a.m[axis][axis] = -1;
	finish_about(a, pivot);
	return a;
}

Affine scaling(const Vec3 &scale, const Vec3 &pivot) {
	Affine a;
	a.m[0][0] = scale.x, a.m[1][1] = scale.y, a.m[2][2] = scale.z;
	finish_about(a, pivot);
	return a;
}

double determinant(const Affine &xf) {
	const auto &m = xf.m;
	return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
		m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
}

bool is_translation(const Affine &xf) {
	for (int r = 0; r < 3; r++) {
		for (int c = 0; c < 3; c++) {
			if (xf.m[r][c] != (r == c ? 1.0 : 0.0)) {
				return false;
			}
		}
	}
	return true;
}

// ---------------------------------------------------------------- entities

bool parse_vec(const std::string &s, Vec3 &v) {
	return std::sscanf(s.c_str(), "%lf %lf %lf", &v.x, &v.y, &v.z) == 3;
}

static double tidy(double v) {
	// a turned origin or angle to a thousandth, as editors write them; whole numbers stay whole
	double r = std::round(v * 1000) / 1000;
	return std::fabs(r - std::round(r)) < 1e-9 ? std::round(r) : r;
}

std::string vec_key(const Vec3 &v) {
	return number(tidy(v.x)) + " " + number(tidy(v.y)) + " " + number(tidy(v.z));
}

bool merges_into_world(const std::string &cn) {
	return cn == "worldspawn" || cn == "func_group" || cn == "group_info";
}

bool is_brush_class(const std::string &cn) {
	if (const EntityDef *d = entity_def(cn)) {
		return d->brush || cn == "worldspawn";
	}
	if (cn == "func_timer" || cn == "trigger_always") {
		return false;  // g_spawn.c: these have no model
	}
	return cn.compare(0, 5, "func_") == 0 || cn.compare(0, 8, "trigger_") == 0 || cn == "worldspawn" || cn == "group_info";
}

static bool starts(const std::string &s, const char *p) {
	return s.compare(0, std::char_traits<char>::length(p), p) == 0;
}

static bool is_spawn(const std::string &cn) {
	return starts(cn, "info_player_deathmatch") || cn == "info_player_start" || starts(cn, "team_CTF_redspawn") ||
		starts(cn, "team_CTF_bluespawn") || starts(cn, "team_CTF_redplayer") || starts(cn, "team_CTF_blueplayer");
}

static bool is_pickup(const std::string &cn) {
	return starts(cn, "item_") || starts(cn, "weapon_") || starts(cn, "ammo_") || starts(cn, "holdable_") ||
		cn == "team_CTF_redflag" || cn == "team_CTF_blueflag" || cn == "team_CTF_neutralflag";
}

bool point_box(const Object &o, Vec3 &lo, Vec3 &hi) {
	if (o.kind != Kind::Entity) {
		return false;
	}
	const std::string cn = o.entity.classname();
	Vec3 at;
	if (is_brush_class(cn) || !parse_vec(o.entity.get("origin"), at)) {
		return false;
	}
	// the catalog's box (the game code's QUAKED sizes); else Radiant's: players 32 x 32 x 56 from 24
	// below the origin, items 32 cubed, the rest 16
	if (const EntityDef *d = entity_def(cn)) {
		lo = at + d->lo, hi = at + d->hi;
	} else if (is_spawn(cn) || cn == "misc_teleporter_dest") {
		lo = at + Vec3(-16, -16, -24), hi = at + Vec3(16, 16, 32);
	} else if (is_pickup(cn) || cn == "misc_model" || cn == "info_player_intermission") {
		lo = at + Vec3(-16, -16, -16), hi = at + Vec3(16, 16, 16);
	} else {
		lo = at + Vec3(-8, -8, -8), hi = at + Vec3(8, 8, 8);
	}
	return true;
}

// q3map2's pointScale (7500) and linearScale (1 / 8000)
static const double LIGHT_LINEAR = 7500.0 / 8000.0;

LightNumbers light_numbers(const Entity &e) {
	LightNumbers n;
	n.key = e.find("_light") ? "_light" : "light";
	n.intensity = std::atof(e.get(n.key).c_str());
	if (n.intensity == 0) {
		n.intensity = 300;
	}
	const double s = std::atof(e.get("scale").c_str());
	n.scale = s != 0 ? s : 1;
	n.linear = (std::atoi(e.get("spawnflags", "0").c_str()) & 1) != 0;
	const double f = std::atof(e.get("fade").c_str());
	n.fade = n.linear && f != 0 ? f : 1;
	const double i = std::fabs(n.intensity * n.scale);
	n.range = n.linear ? i * LIGHT_LINEAR / std::fabs(n.fade) : 0;
	n.reach = n.linear ? n.range : std::sqrt(i * 7500 / 16);
	return n;
}

double light_fade_for(double intensity, double scale, double range) {
	return std::fabs(intensity * scale) * LIGHT_LINEAR / std::max(1.0, range);
}

void entity_color(const std::string &cn, unsigned char rgba[4]) {
	struct C {
		unsigned char r, g, b;
	} c = {242, 217, 77};  // logic: triggers' targets, relays
	if (const EntityDef *d = entity_def(cn)) {
		auto byte = [](double v) { return (unsigned char)std::lround(std::clamp(v, 0.0, 1.0) * 255); };
		c = {byte(d->color.x), byte(d->color.y), byte(d->color.z)};
	} else if (is_spawn(cn)) {
		c = {255, 77, 242};
	} else if (cn == "misc_teleporter_dest" || cn == "target_teleporter") {
		c = {255, 77, 64};
	} else if (cn == "target_push" || cn == "target_position" || cn == "info_notnull" || cn == "info_null") {
		c = {90, 200, 110};
	} else if (cn == "light") {
		c = {110, 255, 80};
	} else if (cn == "target_speaker") {
		c = {77, 217, 217};
	} else if (is_pickup(cn)) {
		c = {90, 128, 255};
	} else if (cn == "misc_model") {
		c = {204, 179, 140};
	} else if (starts(cn, "misc_portal")) {
		c = {179, 128, 255};
	} else if (cn == "info_player_intermission") {
		c = {255, 160, 230};
	}
	rgba[0] = c.r, rgba[1] = c.g, rgba[2] = c.b, rgba[3] = 255;
}

// ---------------------------------------------------------------- shape

static std::vector<Plane> planes_of(const Brush &b) {
	std::vector<Plane> p;
	p.reserve(b.faces.size());
	for (const Face &f : b.faces) {
		p.push_back(f.plane);
	}
	return p;
}

Bounds bounds_of(const Doc &doc, Id id) {
	Bounds b;
	const Object *o = doc.get(id);
	if (!o) {
		return b;
	}
	if (o->kind == Kind::Brush) {
		for (const Winding &w : doc.windings(id)) {
			for (const Vec3 &p : w) {
				b.add(p);
			}
		}
	} else if (o->kind == Kind::Patch) {
		for (size_t i = 0; i + 2 < o->patch.ctrl.size(); i += 5) {
			b.add(Vec3(o->patch.ctrl[i], o->patch.ctrl[i + 1], o->patch.ctrl[i + 2]));
		}
	} else {
		Vec3 lo, hi;
		if (point_box(*o, lo, hi)) {
			b.add(lo);
			b.add(hi);
		}
	}
	return b;
}

Bounds bounds_of(const Doc &doc, const std::vector<Id> &ids) {
	Bounds b;
	for (Id id : ids) {
		b.add(bounds_of(doc, id));
	}
	return b;
}

std::vector<Vec3> object_points(const Doc &doc, Id id) {
	const Object *o = doc.get(id);
	if (!o) {
		return {};
	}
	if (o->kind == Kind::Brush) {
		return q3map::brush_vertices(planes_of(o->brush));
	}
	std::vector<Vec3> out;
	if (o->kind == Kind::Patch) {
		for (size_t i = 0; i + 4 < o->patch.ctrl.size(); i += 5) {
			out.push_back(Vec3(o->patch.ctrl[i], o->patch.ctrl[i + 1], o->patch.ctrl[i + 2]));
		}
	} else {
		Vec3 at;
		if (parse_vec(o->entity.get("origin"), at) && !is_brush_class(o->entity.classname())) {
			out.push_back(at);
		}
	}
	return out;
}

q3map::PatchMesh patch_mesh(const Patch &pt, double tolerance) {
	if (pt.width < 3 || pt.height < 3 || (pt.width & 1) == 0 || (pt.height & 1) == 0) {
		return {};
	}
	std::vector<q3map::PatchVertex> ctrl(size_t(pt.width) * pt.height);
	for (size_t k = 0; k < ctrl.size() && k * 5 + 4 < pt.ctrl.size(); k++) {
		const double *c = &pt.ctrl[k * 5];
		ctrl[k].p = Vec3(c[0], c[1], c[2]);
		ctrl[k].s = c[3], ctrl[k].t = c[4];
	}
	return q3map::tessellate_patch(ctrl, pt.width, pt.height, tolerance);
}

// ---------------------------------------------------------------- rays

bool ray_faces(const Brush &b, const Vec3 &o, const Vec3 &d, double &t, int &face) {
	double enter = -1e300, leave = 1e300;
	face = -1;
	for (size_t i = 0; i < b.faces.size(); i++) {
		const Plane &p = b.faces[i].plane;
		double denom = p.n.dot(d);
		double dist = p.dist(o);
		if (std::fabs(denom) < 1e-12) {
			if (dist > 0) {
				return false;
			}
			continue;
		}
		double ti = -dist / denom;
		if (denom < 0) {
			if (ti > enter) {
				enter = ti;
				face = int(i);
			}
		} else if (ti < leave) {
			leave = ti;
		}
		if (enter > leave) {
			return false;
		}
	}
	if (face < 0 || enter > leave || enter < 0) {
		return false;
	}
	t = enter;
	return true;
}

bool ray_box(const Vec3 &o, const Vec3 &d, const Vec3 &lo, const Vec3 &hi, double &t, Vec3 &normal) {
	double t0 = -1e300, t1 = 1e300;
	const double oo[3] = {o.x, o.y, o.z}, dd[3] = {d.x, d.y, d.z}, l[3] = {lo.x, lo.y, lo.z}, h[3] = {hi.x, hi.y, hi.z};
	int axis = -1;
	for (int i = 0; i < 3; i++) {
		if (std::fabs(dd[i]) < 1e-12) {
			if (oo[i] < l[i] || oo[i] > h[i]) {
				return false;
			}
			continue;
		}
		double a = (l[i] - oo[i]) / dd[i], b = (h[i] - oo[i]) / dd[i];
		if (std::min(a, b) > t0) {
			t0 = std::min(a, b);
			axis = i;
		}
		t1 = std::min(t1, std::max(a, b));
	}
	if (t0 > t1 || t0 < 0 || axis < 0) {
		return false;
	}
	t = t0;
	double n[3] = {0, 0, 0};
	n[axis] = dd[axis] > 0 ? -1 : 1;
	normal = Vec3(n[0], n[1], n[2]);
	return true;
}

bool ray_triangle(const Vec3 &o, const Vec3 &d, const Vec3 &a, const Vec3 &b, const Vec3 &c, double &t) {
	// Moller-Trumbore, either side
	Vec3 e1 = b - a, e2 = c - a;
	Vec3 p = d.cross(e2);
	double det = e1.dot(p);
	if (std::fabs(det) < 1e-12) {
		return false;
	}
	double inv = 1 / det;
	Vec3 s = o - a;
	double u = s.dot(p) * inv;
	if (u < 0 || u > 1) {
		return false;
	}
	Vec3 q = s.cross(e1);
	double v = d.dot(q) * inv;
	if (v < 0 || u + v > 1) {
		return false;
	}
	t = e2.dot(q) * inv;
	return t > 0;
}

// ---------------------------------------------------------------- faces and brushes

Face new_face(const Doc &doc, const Plane &plane, ShaderId shader) {
	Face f;
	f.plane = plane;
	f.shader = shader;
	f.has_flags = true;
	f.raw_valid = true;
	if (doc.plain_form == TexForm::Valve) {
		Vec3 u, v;
		q3map::default_texture_axes(plane.n, u, v);
		const double raw[11] = {u.x, u.y, u.z, 0, v.x, v.y, v.z, 0, 0, 0.5, 0.5};
		std::copy(raw, raw + 11, f.raw);
		f.form = TexForm::Valve;
	} else {
		const double raw[5] = {0, 0, 0, 0.5, 0.5};
		std::copy(raw, raw + 5, f.raw);
		f.form = TexForm::Quake;
	}
	doc.derive_st(f, Vec3());
	return f;
}

std::shared_ptr<Object> box_brush(const Doc &doc, const Vec3 &lo, const Vec3 &hi, ShaderId shader, Id owner) {
	auto o = std::make_shared<Object>();
	o->kind = Kind::Brush;
	o->owner = owner;
	for (const Plane &p : q3map::box_planes(lo, hi)) {
		o->brush.faces.push_back(new_face(doc, p, shader));
	}
	return o;
}

// A Valve 220 face's own axes, which a turned plane can leave edge-on: its default axes then, as
// TrenchBroom's "paraxial" reset, keeping the scale and offsets.
bool world_grid(const Face &f) {
	const std::string &n = shader_name(f.shader);
	const size_t s = n.size() >= 9 && std::equal(n.begin(), n.begin() + 9, "textures/", [](char a, char b) { return std::tolower((unsigned char)a) == b; }) ? 9 : 0;
	static const char dev[] = "oz_dev/";
	if (n.size() < s + 7) {
		return false;
	}
	for (size_t i = 0; i < 7; i++) {
		if (std::tolower((unsigned char)n[s + i]) != dev[i]) {
			return false;
		}
	}
	return true;
}

void world_mapping(const Doc &doc, Face &f, const Vec3 &origin, const Winding &w) {
	Face q = f;
	const double quake[5] = {0, 0, 0, 0.5, 0.5};
	std::copy(quake, quake + 5, q.raw);
	q.form = TexForm::Quake;
	if (f.form == TexForm::Valve) {
		Vec3 u, v;
		q3map::default_texture_axes(f.plane.n, u, v);
		const double valve[11] = {u.x, u.y, u.z, 0, v.x, v.y, v.z, 0, 0, 0.5, 0.5};
		std::copy(valve, valve + 11, q.raw);
		q.form = TexForm::Valve;
	}
	doc.derive_st(q, origin);
	if (f.form == TexForm::Primitives) {
		doc.derive_raw(q, TexForm::Primitives, origin, w);
	}
	f = q;
}

void fix_valve_axes(Face &f) {
	if (f.form != TexForm::Valve) {
		return;
	}
	Vec3 u(f.raw[0], f.raw[1], f.raw[2]), v(f.raw[4], f.raw[5], f.raw[6]);
	const double lu = u.length(), lv = v.length();
	if (lu > 1e-9 && lv > 1e-9 && std::fabs(u.dot(f.plane.n)) / lu < 0.7072 && std::fabs(v.dot(f.plane.n)) / lv < 0.7072) {
		return;
	}
	q3map::default_texture_axes(f.plane.n, u, v);
	f.raw[0] = u.x, f.raw[1] = u.y, f.raw[2] = u.z;
	f.raw[4] = v.x, f.raw[5] = v.y, f.raw[6] = v.z;
	f.raw[8] = 0;
}

Face retarget(const Doc &doc, const Face &src, const Plane &plane, const Vec3 &origin) {
	Face f = src;
	if (q3map::same_plane(src.plane, plane)) {
		return f;
	}
	f.plane = plane;
	f.pts_valid = false;
	fix_valve_axes(f);
	doc.derive_st(f, origin);
	return f;
}

std::shared_ptr<Object> brush_like(const Doc &doc, const std::vector<const Face *> &sources, Id owner,
		const std::vector<Plane> &planes, const Vec3 &origin) {
	if (sources.empty() || !q3map::brush_valid(planes)) {
		return nullptr;
	}
	auto o = std::make_shared<Object>();
	o->kind = Kind::Brush;
	o->owner = owner;
	for (const Plane &p : planes) {
		const Face *best = sources[0];
		double best_dot = -2, best_gap = 1e300;
		for (const Face *s : sources) {
			double d = s->plane.n.dot(p.n);
			double gap = std::fabs(s->plane.d - p.d);
			if (d > best_dot + 1e-9 || (std::fabs(d - best_dot) <= 1e-9 && gap < best_gap)) {
				best = s, best_dot = d, best_gap = gap;
			}
		}
		o->brush.faces.push_back(retarget(doc, *best, p, origin));
	}
	return o;
}

// ---------------------------------------------------------------- pure edits

std::shared_ptr<Object> transformed(const Doc &doc, const Object &o, const Affine &xf, bool lock, const Vec3 &origin) {
	auto out = std::make_shared<Object>(o);
	const bool flips = determinant(xf) < 0;
	const bool move = is_translation(xf);
	if (o.kind == Kind::Brush) {
		std::vector<Plane> planes = q3map::transform_planes(planes_of(o.brush), xf);
		for (size_t i = 0; i < out->brush.faces.size(); i++) {
			Face &f = out->brush.faces[i];
			f.plane = planes[i];
			if (f.pts_valid) {
				// the file's points carried along write back exactly (whole units stay whole)
				for (Vec3 &p : f.pts) {
					p = xf.apply(p);
				}
				if (flips) {
					std::swap(f.pts[0], f.pts[2]);
				}
				Plane p = plane_from_points(f.pts[0], f.pts[1], f.pts[2]);
				if (p.n.length() > 0.5) {
					f.plane = p;
				} else {
					f.pts_valid = false;
				}
			}
			if (lock) {
				f.st = transform_st(o.brush.faces[i].st, xf);
			} else if (!move) {
				fix_valve_axes(f);
				doc.derive_st(f, origin);
			}
		}
		if (lock) {
			// the numbers made again from the moved texture, in the face's form if it can hold them
			const std::vector<Winding> ws = q3map::brush_windings(planes_of(out->brush));
			for (size_t i = 0; i < out->brush.faces.size(); i++) {
				Face &f = out->brush.faces[i];
				doc.derive_raw(f, f.form, origin, i < ws.size() ? ws[i] : Winding{});
			}
		}
		// dev textures stay on the world's grid wherever the brush goes, texture lock or not
		bool dev = false;
		for (const Face &f : out->brush.faces) {
			dev = dev || world_grid(f);
		}
		if (dev) {
			const std::vector<Winding> ws = q3map::brush_windings(planes_of(out->brush));
			for (size_t i = 0; i < out->brush.faces.size(); i++) {
				if (world_grid(out->brush.faces[i])) {
					world_mapping(doc, out->brush.faces[i], origin, i < ws.size() ? ws[i] : Winding{});
				}
			}
		}
	} else if (o.kind == Kind::Patch) {
		Patch &pt = out->patch;
		for (size_t i = 0; i + 4 < pt.ctrl.size(); i += 5) {
			Vec3 p = xf.apply(Vec3(pt.ctrl[i], pt.ctrl[i + 1], pt.ctrl[i + 2]));
			pt.ctrl[i] = p.x, pt.ctrl[i + 1] = p.y, pt.ctrl[i + 2] = p.z;
		}
		if (flips) {
			// a mirror turns the curve inside out: its columns run the other way to face out again
			for (int r = 0; r < pt.height; r++) {
				for (int c = 0; c < pt.width / 2; c++) {
					for (int k = 0; k < 5; k++) {
						std::swap(pt.ctrl[(size_t(r) * pt.width + c) * 5 + k], pt.ctrl[(size_t(r) * pt.width + pt.width - 1 - c) * 5 + k]);
					}
				}
			}
		}
	} else {
		Entity &e = out->entity;
		Vec3 at;
		if (parse_vec(e.get("origin"), at)) {
			e.set("origin", vec_key(xf.apply(at)));
		}
		// a turn or a mirror turns the facing; G_SetMovedir's -1 (up) and -2 (down) aren't headings
		const std::string *angle = e.find("angle");
		const std::string *angles = e.find("angles");
		if (!move && (angle || angles) && !(angle && (*angle == "-1" || *angle == "-2"))) {
			Vec3 ang;
			double yaw = angle ? std::atof(angle->c_str()) : (parse_vec(*angles, ang) ? ang.y : 0);
			Vec3 dir = xf.apply_dir(Vec3(std::cos(yaw * PI / 180), std::sin(yaw * PI / 180), 0));
			if (std::sqrt(dir.x * dir.x + dir.y * dir.y) > 1e-6) {
				double y = std::atan2(dir.y, dir.x) * 180 / PI;
				y = tidy(y < 0 ? y + 360 : y);
				if (y >= 360) {
					y -= 360;
				}
				if (angles && parse_vec(*angles, ang)) {
					e.set("angles", vec_key(Vec3(ang.x, y, ang.z)));
				} else {
					e.set("angle", number(y));
				}
			}
		}
	}
	return out;
}

std::shared_ptr<Object> pushed(const Doc &doc, const Object &brush, const std::vector<int> &faces, double dist) {
	(void)doc;
	if (brush.kind != Kind::Brush) {
		return nullptr;
	}
	auto out = std::make_shared<Object>(brush);
	for (int i : faces) {
		if (i < 0 || i >= int(out->brush.faces.size())) {
			continue;
		}
		Face &f = out->brush.faces[i];
		// the texture stays where it is in the world: no form's numbers depend on the plane's distance
		const Vec3 step = f.plane.n * dist;
		f.plane.d += dist;
		if (f.pts_valid) {
			for (Vec3 &p : f.pts) {
				p = p + step;
			}
			Plane p = plane_from_points(f.pts[0], f.pts[1], f.pts[2]);
			if (p.n.length() > 0.5) {
				f.plane = p;
			} else {
				f.pts_valid = false;
			}
		}
	}
	if (!q3map::brush_valid(planes_of(out->brush))) {
		return nullptr;
	}
	// a push that swallows a face (pushed past another, or another past it) is refused: the face would
	// stay in the brush with no shape, written as three zero points
	const std::vector<q3map::Winding> before = q3map::brush_windings(planes_of(brush.brush)), after = q3map::brush_windings(planes_of(out->brush));
	for (size_t i = 0; i < before.size() && i < after.size(); i++) {
		if (!before[i].empty() && after[i].empty()) {
			return nullptr;
		}
	}
	return out;
}

std::shared_ptr<Object> from_points(const Doc &doc, const Object &brush, const std::vector<Vec3> &points, const Vec3 &origin) {
	if (brush.kind == Kind::Patch) {
		// a curve's control points moved: the same curve through them, its texture as it was
		if (points.size() * 5 != brush.patch.ctrl.size()) {
			return nullptr;
		}
		auto o = std::make_shared<Object>(brush);
		for (size_t i = 0; i < points.size(); i++) {
			o->patch.ctrl[i * 5] = points[i].x, o->patch.ctrl[i * 5 + 1] = points[i].y, o->patch.ctrl[i * 5 + 2] = points[i].z;
		}
		return o;
	}
	if (brush.kind != Kind::Brush || points.size() < 4) {
		return nullptr;
	}
	std::vector<Plane> planes = q3map::hull_planes(points);
	if (planes.empty()) {
		return nullptr;
	}
	std::vector<const Face *> src;
	for (const Face &f : brush.brush.faces) {
		src.push_back(&f);
	}
	return brush_like(doc, src, brush.owner, planes, origin);
}

static std::shared_ptr<Object> piece_of(const Doc &doc, const Object &brush, const std::vector<Plane> &planes,
		const std::vector<int> &src, const Vec3 &origin) {
	if (planes.empty()) {
		return nullptr;
	}
	auto o = std::make_shared<Object>();
	o->kind = Kind::Brush;
	o->owner = brush.owner;
	for (size_t k = 0; k < planes.size(); k++) {
		int s = src[k];
		if (s < 0) {
			// the cut face: the attributes of the face pointing most nearly its way
			double best = -2;
			for (size_t i = 0; i < brush.brush.faces.size(); i++) {
				double d = brush.brush.faces[i].plane.n.dot(planes[k].n);
				if (d > best) {
					best = d;
					s = int(i);
				}
			}
		}
		o->brush.faces.push_back(retarget(doc, brush.brush.faces[size_t(s)], planes[k], origin));
	}
	return o;
}

Pieces clipped(const Doc &doc, const Object &brush, const Plane &plane, const Vec3 &origin) {
	Pieces out;
	if (brush.kind != Kind::Brush) {
		return out;
	}
	const std::vector<Plane> planes = planes_of(brush.brush);
	q3map::Split s = q3map::split_brush(planes, plane);
	// a side that took the whole brush keeps it as it was (every face, those that make no polygon too)
	if (s.back.empty() || s.front.empty()) {
		(s.back.empty() ? out.front : out.back) = std::make_shared<Object>(brush);
		out.whole = true;
		return out;
	}
	out.front = piece_of(doc, brush, s.front, s.front_src, origin);
	out.back = piece_of(doc, brush, s.back, s.back_src, origin);
	return out;
}

// ---------------------------------------------------------------- steps

Vec3 origin_for(const Doc &doc, const Object &o) {
	return o.kind == Kind::Entity ? Vec3() : doc.model_origin(o.owner);
}

static bool same_vec(const Vec3 &a, const Vec3 &b) {
	return a.x == b.x && a.y == b.y && a.z == b.z;
}

// ids and, for entities other than the world, their brushes and curves
std::vector<Id> with_owned(const Doc &doc, const std::vector<Id> &ids) {
	std::set<Id> set(ids.begin(), ids.end());
	for (Id id : ids) {
		const Object *o = doc.get(id);
		if (o && o->kind == Kind::Entity && id != doc.worldspawn()) {
			for (Id b : doc.owned(id)) {
				set.insert(b);
			}
		}
	}
	set.erase(doc.worldspawn());
	return std::vector<Id>(set.begin(), set.end());
}

bool transform(Doc &doc, const std::vector<Id> &ids_in, const Affine &xf, bool lock, const std::string &label) {
	const std::vector<Id> ids = with_owned(doc, ids_in);
	if (ids.empty() || (is_translation(xf) && xf.t.x == 0 && xf.t.y == 0 && xf.t.z == 0)) {
		return false;
	}
	const std::set<Id> set(ids.begin(), ids.end());
	std::map<Id, Vec3> origins;  // brush entities (not merged into the world) whose brushes move
	std::unordered_map<Id, ObjectPtr> before;
	for (Id id : ids) {
		before[id] = doc.ptr(id);
		const Object *o = doc.get(id);
		if (o && o->kind != Kind::Entity && o->owner != doc.worldspawn()) {
			const Object *e = doc.get(o->owner);
			if (e && !merges_into_world(e->entity.classname())) {
				origins.emplace(o->owner, doc.model_origin(o->owner));
			}
		}
	}
	doc.begin(label);
	for (Id id : ids) {
		const Object *o = doc.get(id);
		if (o) {
			doc.put(id, transformed(doc, *o, xf, lock, origin_for(doc, *o)));
		}
	}
	for (auto &kv : origins) {
		const Id ent = kv.first;
		const std::vector<Id> own = doc.owned(ent);
		const bool all = std::all_of(own.begin(), own.end(), [&](Id b) { return set.count(b) != 0; });
		// an entity with an origin key whose brushes all moved: the key moves with them
		const Object *e = doc.get(ent);
		if (all && !set.count(ent) && e->entity.find("origin")) {
			auto ne = std::make_shared<Object>(*e);
			Vec3 at;
			if (parse_vec(ne->entity.get("origin"), at)) {
				ne->entity.set("origin", vec_key(xf.apply(at)));
			}
			doc.put(ent, ne);
		}
		// q3map2 maps the entity's brushes from its origin: where that moved, the texture goes along
		const Vec3 now = doc.model_origin(ent);
		if (!same_vec(now, kv.second)) {
			for (Id b : own) {
				if (set.count(b) && before[b]) {
					auto moved = transformed(doc, *before[b], xf, true, kv.second);
					if (moved->kind == Kind::Brush) {
						const std::vector<Winding> ws = q3map::brush_windings(planes_of(moved->brush));
						for (size_t i = 0; i < moved->brush.faces.size(); i++) {
							Face &f = moved->brush.faces[i];
							doc.derive_raw(f, f.form, now, i < ws.size() ? ws[i] : Winding{});
						}
					}
					doc.put(b, moved);
				}
			}
		}
	}
	doc.end();
	return true;
}

bool push_faces(Doc &doc, const std::vector<std::pair<Id, int>> &faces, double dist, const std::string &label) {
	std::map<Id, std::vector<int>> by;
	for (const auto &f : faces) {
		by[f.first].push_back(f.second);
	}
	std::vector<std::pair<Id, std::shared_ptr<Object>>> out;
	for (auto &kv : by) {
		const Object *o = doc.get(kv.first);
		auto p = o ? pushed(doc, *o, kv.second, dist) : nullptr;
		if (!p) {
			return false;
		}
		out.emplace_back(kv.first, p);
	}
	if (out.empty() || dist == 0) {
		return false;
	}
	doc.begin(label);
	for (auto &kv : out) {
		doc.put(kv.first, kv.second);
	}
	doc.end();
	return true;
}

static std::vector<Id> brushes_in(const Doc &doc, const std::vector<Id> &ids) {
	std::vector<Id> out;
	for (Id id : with_owned(doc, ids)) {
		const Object *o = doc.get(id);
		if (o && o->kind == Kind::Brush) {
			out.push_back(id);
		}
	}
	return out;
}

std::vector<Id> clip(Doc &doc, const std::vector<Id> &ids, const Plane &plane, int keep, std::string &note) {
	std::vector<Id> brushes = brushes_in(doc, ids), kept;
	if (brushes.empty()) {
		note = "Select the brushes to clip";
		return {};
	}
	doc.begin("Clip");
	int cut = 0;
	for (Id id : brushes) {
		const Object *o = doc.get(id);
		Pieces p = clipped(doc, *o, plane, origin_for(doc, *o));
		if (p.whole) {
			// the plane misses it: it stays whole, whichever side it's on (a cut deletes nothing it doesn't cut)
			kept.push_back(id);
			continue;
		}
		cut++;
		std::shared_ptr<Object> first = keep != 0 ? p.front : p.back;
		std::shared_ptr<Object> second = keep == 2 ? p.back : nullptr;
		if (!first) {
			std::swap(first, second);
		}
		if (!first) {
			doc.remove(id);
			continue;
		}
		doc.put(id, first);
		kept.push_back(id);
		if (second) {
			kept.push_back(doc.add(second));
		}
	}
	doc.end();
	char buf[96];
	std::snprintf(buf, sizeof buf, "Clipped %d brush%s", cut, cut == 1 ? "" : "es");
	note = buf;
	return kept;
}

std::vector<Id> subtract(Doc &doc, const std::vector<Id> &ids, std::string &note) {
	const std::vector<Id> cutters = brushes_in(doc, ids);
	if (cutters.empty()) {
		note = "Select the brushes to cut with";
		return {};
	}
	const std::set<Id> cutting(cutters.begin(), cutters.end());
	Bounds reach;
	std::vector<Bounds> cb;
	for (Id c : cutters) {
		cb.push_back(bounds_of(doc, c));
		reach.add(cb.back());
	}
	std::map<Id, std::vector<std::shared_ptr<Object>>> results;
	for (Id tid : doc.ids_of(Kind::Brush)) {
		if (cutting.count(tid)) {
			continue;
		}
		const Bounds tb = bounds_of(doc, tid);
		if (!tb.overlaps(reach, -q3map::EPSILON)) {
			continue;
		}
		const Object *t = doc.get(tid);
		const Vec3 origin = origin_for(doc, *t);
		std::vector<std::shared_ptr<Object>> pieces{std::make_shared<Object>(*t)};
		bool touched = false;
		for (size_t k = 0; k < cutters.size(); k++) {
			if (!tb.overlaps(cb[k], -q3map::EPSILON)) {
				continue;
			}
			const std::vector<Plane> cut = planes_of(doc.get(cutters[k])->brush);
			std::vector<std::shared_ptr<Object>> next;
			for (auto &p : pieces) {
				q3map::SubtractResult r = q3map::subtract(planes_of(p->brush), cut);
				if (!r.touched) {
					next.push_back(p);
					continue;
				}
				touched = true;
				for (const q3map::Piece &np : r.pieces) {
					auto b = std::make_shared<Object>();
					b->kind = Kind::Brush;
					b->owner = p->owner;
					for (size_t i = 0; i < np.planes.size(); i++) {
						b->brush.faces.push_back(retarget(doc, p->brush.faces[size_t(np.attr[i])], np.planes[i], origin));
					}
					next.push_back(b);
				}
			}
			pieces = std::move(next);
		}
		if (touched) {
			results[tid] = std::move(pieces);
		}
	}
	if (results.empty()) {
		note = "The selection doesn't overlap any brush";
		return {};
	}
	doc.begin("CSG subtract");
	std::vector<Id> made;
	for (auto &kv : results) {
		doc.remove(kv.first);
		for (auto &p : kv.second) {
			made.push_back(doc.add(p));
		}
	}
	for (Id c : cutters) {
		doc.remove(c);
	}
	doc.end();
	char buf[96];
	std::snprintf(buf, sizeof buf, "Cut %d brush%s into %d", int(results.size()), results.size() == 1 ? "" : "es", int(made.size()));
	note = buf;
	return made;
}

std::vector<Id> hollow(Doc &doc, const std::vector<Id> &ids, double thickness, std::string &note) {
	const std::vector<Id> brushes = brushes_in(doc, ids);
	if (brushes.empty() || thickness <= 0) {
		note = "Select the brushes to hollow";
		return {};
	}
	std::vector<std::pair<Id, std::vector<std::shared_ptr<Object>>>> walls;
	for (Id id : brushes) {
		const Object *o = doc.get(id);
		const std::vector<Plane> planes = planes_of(o->brush);
		// room for the inside: the brush shrunk by the thickness on every face
		std::vector<Plane> inner = planes;
		for (Plane &p : inner) {
			p.d -= thickness;
		}
		if (!q3map::brush_valid(inner)) {
			char buf[128];
			std::snprintf(buf, sizeof buf, "A brush is too thin to hollow with %s-unit walls", number(thickness).c_str());
			note = buf;
			return {};
		}
		const Vec3 origin = origin_for(doc, *o);
		const std::vector<Winding> &ws = doc.windings(id);
		std::vector<std::shared_ptr<Object>> mine;
		for (size_t i = 0; i < planes.size(); i++) {
			if (i >= ws.size() || ws[i].size() < 3) {
				continue;
			}
			q3map::Split s = q3map::split_brush(planes, Plane{planes[i].n, planes[i].d - thickness});
			if (s.front.empty()) {
				continue;
			}
			std::vector<int> src = s.front_src;
			for (int &k : src) {
				if (k < 0) {
					k = int(i);  // the inside face: this wall's own face's attributes
				}
			}
			auto w = std::make_shared<Object>();
			w->kind = Kind::Brush;
			w->owner = o->owner;
			for (size_t k = 0; k < s.front.size(); k++) {
				w->brush.faces.push_back(retarget(doc, o->brush.faces[size_t(src[k])], s.front[k], origin));
			}
			mine.push_back(w);
		}
		walls.emplace_back(id, std::move(mine));
	}
	doc.begin("Hollow");
	std::vector<Id> made;
	for (auto &kv : walls) {
		doc.remove(kv.first);
		for (auto &w : kv.second) {
			made.push_back(doc.add(w));
		}
	}
	doc.end();
	char buf[96];
	std::snprintf(buf, sizeof buf, "Hollowed %d brush%s into %d walls", int(walls.size()), walls.size() == 1 ? "" : "es", int(made.size()));
	note = buf;
	return made;
}

std::vector<Id> merge(Doc &doc, const std::vector<Id> &ids, std::string &note) {
	const std::vector<Id> brushes = brushes_in(doc, ids);
	if (brushes.size() < 2) {
		note = "Select two or more brushes to merge";
		return {};
	}
	std::vector<Vec3> points;
	std::vector<const Face *> src;
	for (Id id : brushes) {
		const Object *o = doc.get(id);
		for (const Vec3 &p : q3map::brush_vertices(planes_of(o->brush))) {
			points.push_back(p);
		}
		for (const Face &f : o->brush.faces) {
			src.push_back(&f);
		}
	}
	const Object *first = doc.get(brushes[0]);
	std::vector<Plane> planes = q3map::hull_planes(points);
	auto merged = planes.empty() ? nullptr : brush_like(doc, src, first->owner, planes, origin_for(doc, *first));
	if (!merged) {
		note = "Those brushes make no solid together";
		return {};
	}
	doc.begin("Merge");
	doc.put(brushes[0], merged);
	for (size_t i = 1; i < brushes.size(); i++) {
		doc.remove(brushes[i]);
	}
	doc.end();
	char buf[96];
	std::snprintf(buf, sizeof buf, "Merged %d brushes", int(brushes.size()));
	note = buf;
	return {brushes[0]};
}

Clip copy_of(const Doc &doc, const std::vector<Id> &ids) {
	Clip out;
	for (Id id : with_owned(doc, ids)) {
		if (ObjectPtr p = doc.ptr(id)) {
			out.emplace_back(id, p);
		}
	}
	return out;
}

std::vector<Id> paste(Doc &doc, const Clip &clip, const Vec3 &offset, const std::string &label) {
	if (clip.empty()) {
		return {};
	}
	const Affine xf = translation(offset);
	std::map<Id, Id> ents;  // a copied entity: its copy
	std::vector<Id> made;
	// a copy's targetname the map already has gets a new one (its stem and the first free number),
	// and the copies that target it follow: the copies' links stay among themselves
	std::map<std::string, std::string> renamed;
	std::set<std::string> taken;
	for (const auto &c : clip) {
		const std::string *n = c.second->kind == Kind::Entity ? c.second->entity.find("targetname") : nullptr;
		if (!n || n->empty() || renamed.count(*n)) {
			continue;
		}
		bool there = false;
		for (Id e : doc.ids_of(Kind::Entity)) {
			const std::string *m = doc.get(e)->entity.find("targetname");
			there |= m && *m == *n;
		}
		if (there) {
			std::string stem = *n;
			while (!stem.empty() && std::isdigit((unsigned char)stem.back())) {
				stem.pop_back();
			}
			renamed[*n] = unique_targetname(doc, stem.empty() ? *n + "_" : stem, taken);
			taken.insert(renamed[*n]);
		}
	}
	// each jump pad tie in the clip the map already has: a new one (shared by that pad's copied pieces)
	std::map<std::string, std::string> ties;
	for (const auto &c : clip) {
		const std::string *t = c.second->kind == Kind::Entity ? c.second->entity.find(JUMPPAD_KEY) : nullptr;
		if (!t || t->empty() || ties.count(*t)) {
			continue;
		}
		bool there = false;
		for (Id e : doc.ids_of(Kind::Entity)) {
			there |= doc.get(e)->entity.get(JUMPPAD_KEY) == *t;
		}
		if (there) {
			ties[*t] = renamed.count(*t) ? renamed[*t] : unique_targetname(doc, "pad", taken);
			taken.insert(ties[*t]);
		}
	}
	doc.begin(label);
	for (const auto &c : clip) {
		if (c.second->kind == Kind::Entity && !(c.first == doc.worldspawn() && doc.get(c.first))) {
			auto copy = transformed(doc, *c.second, xf, true, Vec3());
			for (const char *k : {"targetname", "target"}) {
				const std::string *v = copy->entity.find(k);
				if (v && renamed.count(*v)) {
					copy->entity.set(k, renamed[*v]);
				}
			}
			if (const std::string *t = copy->entity.find(JUMPPAD_KEY); t && ties.count(*t)) {
				copy->entity.set(JUMPPAD_KEY, ties[*t]);
			}
			Id n = doc.add(copy);
			ents[c.first] = n;
			made.push_back(n);
		}
	}
	for (const auto &c : clip) {
		if (c.second->kind == Kind::Entity) {
			continue;
		}
		auto it = ents.find(c.second->owner);
		const Id owner = it != ents.end() ? it->second : doc.get(c.second->owner) ? c.second->owner : doc.worldspawn();
		const Vec3 origin = doc.get(c.second->owner) ? doc.model_origin(c.second->owner) : Vec3();
		auto o = transformed(doc, *c.second, xf, true, origin);
		o->owner = owner;
		made.push_back(doc.add(o));
	}
	// a copied brush entity maps its brushes from its own origin
	for (auto &kv : ents) {
		const Vec3 now = doc.model_origin(kv.second);
		for (Id b : doc.owned(kv.second)) {
			auto o = doc.edit(b);
			if (o->kind != Kind::Brush) {
				continue;
			}
			const std::vector<Winding> &ws = doc.windings(b);
			for (size_t i = 0; i < o->brush.faces.size(); i++) {
				doc.derive_raw(o->brush.faces[i], o->brush.faces[i].form, now, i < ws.size() ? ws[i] : Winding{});
			}
			doc.put(b, o);
		}
	}
	doc.end();
	std::sort(made.begin(), made.end());
	return made;
}

bool remove(Doc &doc, const std::vector<Id> &ids, const std::string &label) {
	const std::vector<Id> all = with_owned(doc, ids);
	if (all.empty()) {
		return false;
	}
	std::set<Id> owners;
	for (Id id : all) {
		const Object *o = doc.get(id);
		if (o && o->kind != Kind::Entity && o->owner != doc.worldspawn()) {
			owners.insert(o->owner);
		}
	}
	doc.begin(label);
	for (Id id : all) {
		doc.remove(id);
	}
	// a brush entity left without brushes would compile to nothing
	for (Id e : owners) {
		const Object *o = doc.get(e);
		if (o && doc.owned(e).empty()) {
			doc.remove(e);
		}
	}
	doc.end();
	return true;
}

// ---------------------------------------------------------------- entity keys

// One entity's keys changed by edit (false: unchanged). Its brushes keep their textures where they
// are when that moves the origin q3map2 maps them from.
static bool edit_keys(Doc &doc, Id ent, const std::function<bool(Entity &)> &edit) {
	const Object *o = doc.get(ent);
	if (!o || o->kind != Kind::Entity) {
		return false;
	}
	const Vec3 was = doc.model_origin(ent);
	auto ne = std::make_shared<Object>(*o);
	if (!edit(ne->entity) || ne->entity.keys == o->entity.keys) {
		return false;
	}
	doc.put(ent, ne);
	const Vec3 now = doc.model_origin(ent);
	if (same_vec(was, now)) {
		return true;
	}
	for (Id b : doc.owned(ent)) {
		const Object *bo = doc.get(b);
		if (!bo || bo->kind != Kind::Brush) {
			continue;
		}
		const std::vector<Winding> ws = q3map::brush_windings(planes_of(bo->brush));
		auto nb = std::make_shared<Object>(*bo);
		for (size_t i = 0; i < nb->brush.faces.size(); i++) {
			Face &f = nb->brush.faces[i];
			doc.derive_raw(f, f.form, now, i < ws.size() ? ws[i] : Winding{});
		}
		doc.put(b, nb);
	}
	return true;
}

bool set_key(Doc &doc, const std::vector<Id> &ents, const std::string &key, const std::string &value, const std::string &label) {
	if (key.empty()) {
		return false;
	}
	doc.begin(label);
	bool any = false;
	for (Id e : ents) {
		any |= edit_keys(doc, e, [&](Entity &en) {
			en.set(key, value);
			return true;
		});
	}
	doc.end();
	return any;
}

bool remove_key(Doc &doc, const std::vector<Id> &ents, const std::string &key, const std::string &label) {
	if (key == "classname") {
		return false;  // an entity without a class is no entity
	}
	doc.begin(label);
	bool any = false;
	for (Id e : ents) {
		any |= edit_keys(doc, e, [&](Entity &en) {
			bool gone = false;
			while (en.erase(key)) {
				gone = true;
			}
			return gone;
		});
	}
	doc.end();
	return any;
}

bool rename_key(Doc &doc, const std::vector<Id> &ents, const std::string &from, const std::string &to, std::string &note) {
	if (to.empty() || from == to || from == "classname" || to == "classname") {
		note = to.empty() ? "A key needs a name" : "The class key keeps its name";
		return false;
	}
	for (Id e : ents) {
		const Object *o = doc.get(e);
		if (o && o->entity.find(from) && o->entity.find(to)) {
			note = "There's already a key " + to;
			return false;
		}
	}
	doc.begin("Rename key " + from);
	bool any = false;
	for (Id e : ents) {
		any |= edit_keys(doc, e, [&](Entity &en) {
			for (auto &kv : en.keys) {
				if (kv.first == from) {
					kv.first = to;
					return true;
				}
			}
			return false;
		});
	}
	doc.end();
	return any;
}

// ---------------------------------------------------------------- curves

static Vec3 ctrl_point(const Patch &p, int row, int col) {
	const double *c = &p.ctrl[(size_t(row) * size_t(p.width) + size_t(col)) * 5];
	return Vec3(c[0], c[1], c[2]);
}

Vec3 curve_facing(const Patch &p) {
	if (p.width < 3 || p.height < 3 || p.ctrl.size() < size_t(p.width * p.height) * 5) {
		return Vec3();
	}
	const int r = p.height / 2, c = p.width / 2;
	const Vec3 du = ctrl_point(p, r, std::min(c + 1, p.width - 1)) - ctrl_point(p, r, std::max(c - 1, 0));
	const Vec3 dv = ctrl_point(p, std::min(r + 1, p.height - 1), c) - ctrl_point(p, std::max(r - 1, 0), c);
	return du.cross(dv);
}

void natural_texture(const Doc &doc, Patch &p) {
	if (p.width < 1 || p.height < 1 || p.ctrl.size() < size_t(p.width * p.height) * 5) {
		return;
	}
	int tw = 64, th = 64;
	doc.tex_dims(p.shader, tw, th);
	// lengths along the middle row and column, each quadratic piece by its two halves (through its
	// middle point on the curve), the control point between taking the piece's middle
	auto along = [&](int count, const std::function<Vec3(int)> &at) {
		std::vector<double> d(size_t(count), 0.0);
		for (int i = 0; i + 2 < count; i += 2) {
			const Vec3 a = at(i), b = at(i + 1), c = at(i + 2), m = a * 0.25 + b * 0.5 + c * 0.25;
			const double h1 = (m - a).length(), h2 = (c - m).length();
			d[size_t(i + 1)] = d[size_t(i)] + h1;
			d[size_t(i + 2)] = d[size_t(i)] + h1 + h2;
		}
		if (count == 2) {
			d[1] = (at(1) - at(0)).length();
		}
		return d;
	};
	const int mr = p.height / 2, mc = p.width / 2;
	const std::vector<double> s = along(p.width, [&](int i) { return ctrl_point(p, mr, i); });
	const std::vector<double> t = along(p.height, [&](int i) { return ctrl_point(p, i, mc); });
	for (int r = 0; r < p.height; r++) {
		for (int c = 0; c < p.width; c++) {
			double *x = &p.ctrl[(size_t(r) * size_t(p.width) + size_t(c)) * 5];
			x[3] = s[size_t(c)] / (tw * 0.5);
			x[4] = t[size_t(r)] / (th * 0.5);
		}
	}
}

std::shared_ptr<Object> inverted(const Object &patch) {
	auto o = std::make_shared<Object>(patch);
	Patch &p = o->patch;
	for (int r = 0; r < p.height / 2; r++) {
		for (size_t k = 0; k < size_t(p.width) * 5; k++) {
			std::swap(p.ctrl[size_t(r) * size_t(p.width) * 5 + k], p.ctrl[size_t(p.height - 1 - r) * size_t(p.width) * 5 + k]);
		}
	}
	return o;
}

std::shared_ptr<Object> curve_in_box(const Doc &doc, CurveShape shape, const Bounds &box, int axis, ShaderId shader, Id owner) {
	if (box.empty()) {
		return nullptr;
	}
	const double lo[3] = {box.lo.x, box.lo.y, box.lo.z}, hi[3] = {box.hi.x, box.hi.y, box.hi.z};
	const double mid[3] = {(lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, (lo[2] + hi[2]) / 2};
	auto point = [](double x, double y, double z, int ax, int a, int b, double va, double vb, double vax) {
		double q[3] = {x, y, z};
		q[a] = va, q[b] = vb, q[ax] = vax;
		return Vec3(q[0], q[1], q[2]);
	};
	std::vector<std::vector<Vec3>> rows;
	bool out = true;  // facing out from the box's middle (else in)
	if (shape == CurveShape::Cylinder || shape == CurveShape::Cone) {
		// rings round axis, the other two (axis+1, axis+2) a right-handed pair with it: counter-clockwise
		const int ax = axis, a = (ax + 1) % 3, b = (ax + 2) % 3;
		static const double ring[9][2] = {{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}, {1, 0}};
		const double ra = (hi[a] - lo[a]) / 2, rb = (hi[b] - lo[b]) / 2;
		const double level[3] = {lo[ax], mid[ax], hi[ax]};
		for (int r = 0; r < 3; r++) {
			const double k = shape == CurveShape::Cone ? 1.0 - r * 0.5 : 1.0;  // a cone narrows to its tip
			std::vector<Vec3> row;
			for (const auto &e : ring) {
				row.push_back(point(0, 0, 0, ax, a, b, mid[a] + e[0] * ra * k, mid[b] + e[1] * rb * k, level[r]));
			}
			rows.push_back(row);
		}
	} else if (shape == CurveShape::Plane) {
		for (int r = 0; r < 3; r++) {
			std::vector<Vec3> row;
			for (int c = 0; c < 3; c++) {
				row.push_back(Vec3(c == 0 ? lo[0] : c == 1 ? mid[0] : hi[0], r == 0 ? lo[1] : r == 1 ? mid[1] : hi[1], hi[2]));
			}
			rows.push_back(row);
		}
	} else {
		// an arch or a bevel stands on z, round across the longer side, as long as the shorter
		const int l = (hi[0] - lo[0]) >= (hi[1] - lo[1]) ? 0 : 1, s = 1 - l;
		std::vector<std::pair<double, double>> across;  // (along l, z)
		if (shape == CurveShape::Arch) {
			across = {{lo[l], lo[2]}, {lo[l], hi[2]}, {mid[l], hi[2]}, {hi[l], hi[2]}, {hi[l], lo[2]}};
		} else {
			across = {{lo[l], lo[2]}, {hi[l], lo[2]}, {hi[l], hi[2]}};
		}
		for (double vs : {lo[s], mid[s], hi[s]}) {
			std::vector<Vec3> row;
			for (const auto &e : across) {
				double q[3];
				q[l] = e.first, q[s] = vs, q[2] = e.second;
				row.push_back(Vec3(q[0], q[1], q[2]));
			}
			rows.push_back(row);
		}
		out = false;
	}
	auto o = std::make_shared<Object>();
	o->kind = Kind::Patch;
	o->owner = owner;
	Patch &p = o->patch;
	p.shader = shader;
	p.height = int(rows.size());
	p.width = int(rows[0].size());
	p.info[0] = p.width, p.info[1] = p.height;
	for (const auto &row : rows) {
		for (const Vec3 &v : row) {
			p.ctrl.insert(p.ctrl.end(), {v.x, v.y, v.z, 0.0, 0.0});
		}
	}
	// facing the way the shape wants: its rows the other way round if not
	const Vec3 c = box.center(), m = ctrl_point(p, p.height / 2, p.width / 2);
	const double d = curve_facing(p).dot(m - c);
	if ((out && d < 0) || (!out && d > 0)) {
		o = inverted(*o);
	}
	natural_texture(doc, o->patch);
	return o;
}

// ---- curves: sections, caps, thickness

// a control point's five numbers (x y z s t)
using CtrlPoint = std::array<double, 5>;

static CtrlPoint ctrl_at(const Patch &p, int r, int c) {
	CtrlPoint out;
	std::copy_n(&p.ctrl[(size_t(r) * size_t(p.width) + size_t(c)) * 5], 5, out.begin());
	return out;
}

static CtrlPoint mix(const CtrlPoint &a, double wa, const CtrlPoint &b, double wb, const CtrlPoint &c = CtrlPoint{}, double wc = 0) {
	CtrlPoint out;
	for (int k = 0; k < 5; k++) {
		out[size_t(k)] = a[size_t(k)] * wa + b[size_t(k)] * wb + c[size_t(k)] * wc;
	}
	return out;
}

static Vec3 xyz(const CtrlPoint &c) {
	return Vec3(c[0], c[1], c[2]);
}

static std::shared_ptr<Object> with_grid(const Object &like, const std::vector<std::vector<CtrlPoint>> &rows) {
	auto o = std::make_shared<Object>(like);
	Patch &p = o->patch;
	p.height = int(rows.size());
	p.width = rows.empty() ? 0 : int(rows[0].size());
	p.def3 = false;
	std::fill(std::begin(p.info), std::end(p.info), 0.0);
	p.info[0] = p.width, p.info[1] = p.height;
	p.ctrl.clear();
	for (const auto &row : rows) {
		for (const CtrlPoint &c : row) {
			p.ctrl.insert(p.ctrl.end(), c.begin(), c.end());
		}
	}
	return o;
}

static std::vector<std::vector<CtrlPoint>> grid_of(const Patch &p, bool transposed) {
	std::vector<std::vector<CtrlPoint>> rows;
	const int h = transposed ? p.width : p.height, w = transposed ? p.height : p.width;
	for (int r = 0; r < h; r++) {
		std::vector<CtrlPoint> row;
		for (int c = 0; c < w; c++) {
			row.push_back(transposed ? ctrl_at(p, c, r) : ctrl_at(p, r, c));
		}
		rows.push_back(row);
	}
	return rows;
}

static std::vector<std::vector<CtrlPoint>> transposed(const std::vector<std::vector<CtrlPoint>> &g) {
	std::vector<std::vector<CtrlPoint>> out(g.empty() ? 0 : g[0].size());
	for (const auto &row : g) {
		for (size_t c = 0; c < row.size(); c++) {
			out[c].push_back(row[c]);
		}
	}
	return out;
}

std::shared_ptr<Object> curve_split(const Object &patch, bool rows) {
	const Patch &p = patch.patch;
	if (patch.kind != Kind::Patch || p.width < 3 || p.height < 3 || (rows ? p.height : p.width) * 2 - 1 > 31) {
		return nullptr;  // q3map2 takes curves up to 31 control points a side
	}
	// de Casteljau at the middle of each quadratic section: (A, P, C) becomes (A, AP, M) and (M, PC, C)
	auto g = grid_of(p, rows);
	for (auto &row : g) {
		std::vector<CtrlPoint> out = {row[0]};
		for (size_t i = 0; i + 2 < row.size(); i += 2) {
			const CtrlPoint &a = row[i], &q = row[i + 1], &c = row[i + 2];
			out.push_back(mix(a, 0.5, q, 0.5));
			out.push_back(mix(a, 0.25, q, 0.5, c, 0.25));
			out.push_back(mix(q, 0.5, c, 0.5));
			out.push_back(c);
		}
		row = out;
	}
	return with_grid(patch, rows ? transposed(g) : g);
}

std::shared_ptr<Object> curve_merge(const Object &patch, bool rows) {
	const Patch &p = patch.patch;
	const int n = rows ? p.height : p.width;
	if (patch.kind != Kind::Patch || n < 5 || (n - 1) % 4 != 0) {
		return nullptr;  // an even count of sections, two or more
	}
	// two sections (A .. B .. C) as one through B: its control point P = (4B - A - C) / 2
	auto g = grid_of(p, rows);
	for (auto &row : g) {
		std::vector<CtrlPoint> out = {row[0]};
		for (size_t i = 0; i + 4 < row.size(); i += 4) {
			const CtrlPoint &a = row[i], &b = row[i + 2], &c = row[i + 4];
			out.push_back(mix(b, 2.0, a, -0.5, c, -0.5));
			out.push_back(c);
		}
		row = out;
	}
	return with_grid(patch, rows ? transposed(g) : g);
}

static Vec3 middle_of(const Patch &p) {
	Vec3 m;
	const size_t n = p.ctrl.size() / 5;
	for (size_t i = 0; i < n; i++) {
		m = m + Vec3(p.ctrl[i * 5], p.ctrl[i * 5 + 1], p.ctrl[i * 5 + 2]);
	}
	return n ? m * (1.0 / double(n)) : m;
}

// The curve's side: +1 when it faces away from its own middle (a cylinder seen from outside), -1 when toward
static double side_of(const Patch &p) {
	const Vec3 m = xyz(ctrl_at(p, p.height / 2, p.width / 2));
	return curve_facing(p).dot(m - middle_of(p)) >= 0 ? 1 : -1;
}

// a new piece faces the curve's way: away from (or toward) the curve's middle as the curve does
static std::shared_ptr<Object> facing_like(const Doc &doc, std::shared_ptr<Object> piece, const Patch &curve, double side) {
	const Vec3 c = middle_of(piece->patch);
	if (curve_facing(piece->patch).dot(c - middle_of(curve)) * side < 0) {
		piece = inverted(*piece);
	}
	natural_texture(doc, piece->patch);
	return piece;
}

std::vector<std::shared_ptr<Object>> curve_caps(const Doc &doc, const Object &patch) {
	std::vector<std::shared_ptr<Object>> out;
	const Patch &p = patch.patch;
	if (patch.kind != Kind::Patch) {
		return out;
	}
	const double side = side_of(p);
	for (int r : {0, p.height - 1}) {
		std::vector<CtrlPoint> q;
		for (int c = 0; c < p.width; c++) {
			q.push_back(ctrl_at(p, r, c));
		}
		// the ring's eight points round the 3 x 3's border: corners on the curve, edges' middles its controls
		CtrlPoint c00, c01, c02, c12, c22, c21, c20, c10, mid;
		if (q.size() == 9 && (xyz(q[0]) - xyz(q[8])).length() < 0.01) {
			c00 = q[0], c01 = q[1], c02 = q[2], c12 = q[3], c22 = q[4], c21 = q[5], c20 = q[6], c10 = q[7];
			mid = mix(mix(q[0], 0.5, q[4], 0.5), 0.5, mix(q[2], 0.5, q[6], 0.5), 0.5);
		} else if (q.size() == 5) {
			// a half ring, closed by a straight base from its last point back to its first
			c00 = q[0], c01 = q[1], c02 = q[2], c12 = q[3], c22 = q[4];
			c20 = mix(q[0], 0.5, q[4], 0.5);
			c21 = mix(q[4], 0.5, c20, 0.5), c10 = mix(c20, 0.5, q[0], 0.5);
			mid = mix(c20, 0.5, q[2], 0.5);
		} else {
			continue;
		}
		auto cap = with_grid(patch, {{c00, c01, c02}, {c10, mid, c12}, {c20, c21, c22}});
		out.push_back(facing_like(doc, cap, p, side));
	}
	return out;
}

std::vector<std::shared_ptr<Object>> curve_thick(const Doc &doc, const Object &patch, double depth) {
	std::vector<std::shared_ptr<Object>> out;
	const Patch &p = patch.patch;
	if (patch.kind != Kind::Patch || p.width < 3 || p.height < 3 || depth <= 0) {
		return out;
	}
	const bool closed_u = (xyz(ctrl_at(p, 0, 0)) - xyz(ctrl_at(p, 0, p.width - 1))).length() < 0.01;
	const bool closed_v = (xyz(ctrl_at(p, 0, 0)) - xyz(ctrl_at(p, p.height - 1, 0))).length() < 0.01;
	const Vec3 facing = curve_facing(p);
	// each control point moved back along the net's normal there (du x dv; across a closed join both sides)
	auto at = [&](int r, int c) { return xyz(ctrl_at(p, r, c)); };
	std::vector<std::vector<CtrlPoint>> back = grid_of(p, false);
	for (int r = 0; r < p.height; r++) {
		for (int c = 0; c < p.width; c++) {
			const int c0 = c > 0 ? c - 1 : closed_u ? p.width - 2 : c, c1 = c + 1 < p.width ? c + 1 : closed_u ? 1 : c;
			const int r0 = r > 0 ? r - 1 : closed_v ? p.height - 2 : r, r1 = r + 1 < p.height ? r + 1 : closed_v ? 1 : r;
			Vec3 n = (at(r, c1) - at(r, c0)).cross(at(r1, c) - at(r0, c));
			n = n.length() > 1e-9 ? n.normalized() : facing.normalized();
			for (int k = 0; k < 3; k++) {
				const double d[3] = {n.x, n.y, n.z};
				back[size_t(r)][size_t(c)][size_t(k)] -= d[k] * depth;
			}
		}
	}
	// the other skin faces the other way
	auto skin = inverted(*with_grid(patch, back));
	natural_texture(doc, skin->patch);
	out.push_back(skin);
	// the seams: a strip from each open border to its other skin's, facing away from the curve's middle
	auto strip = [&](const std::vector<CtrlPoint> &front, const std::vector<CtrlPoint> &rear) {
		std::vector<CtrlPoint> midrow;
		for (size_t i = 0; i < front.size(); i++) {
			midrow.push_back(mix(front[i], 0.5, rear[i], 0.5));
		}
		auto seam = with_grid(patch, {front, midrow, rear});
		out.push_back(facing_like(doc, seam, p, 1));
	};
	const auto front = grid_of(p, false), frontT = grid_of(p, true), backT = transposed(back);
	if (!closed_v) {
		strip(front.front(), back.front());
		strip(front.back(), back.back());
	}
	if (!closed_u) {
		strip(frontT.front(), backT.front());
		strip(frontT.back(), backT.back());
	}
	return out;
}

}  // namespace oz::ed
