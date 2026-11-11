// The live light (ed_light.h): the editor's own lightmaps, baked on worker threads as q3map2 bakes Play's.
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "ed_light.h"
#include "ed_engine.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <thread>
#include <unordered_set>

extern "C" {
#include "../ui/oz_ui_engine.h"
}

namespace oz::ed {

namespace {

const double PI = 3.14159265358979323846;
// q3map2's (q3map2.h): its sample size, light scales and the -fast threshold
const double SAMPLE = 16;
const double POINT_SCALE = 7500, SPOT_SCALE = 7500, AREA_SCALE = 0.25, LINEAR_SCALE = 1.0 / 8000;
const double FORM_FACTOR_SCALE = 3, FALLOFF = 1;
const double BACKSPLASH_FRACTION = 0.05, BACKSPLASH_DISTANCE = 23;
const int MAX_SIDE = 250;  // texels across a face at most: a bigger face's texels are bigger
const int PAGE = 1024;     // a page's side
const double SHRINK = 0.05;  // brushes as traced: this much inside their faces (touching faces let light by)
const double LIFT = 0.25;    // a texel's point off its face
const int SKY_RINGS = 4, SKY_SPOKES = 6;      // the sky's light: rays over the half sphere, by cosine
const int FLOOD_RINGS = 3, FLOOD_SPOKES = 8;  // the fill light's

struct Occ {  // a brush light stops at
	float lo[3], hi[3];
	std::vector<Plane> planes;
	std::vector<uint8_t> sky;  // per plane: its face is the sky's
};
struct Node {
	float lo[3], hi[3];
	int a = -1, b = -1;  // children
	int first = 0, count = 0;  // a leaf's occluders in order
};
struct PointL {
	Id owner = 0;
	Vec3 o, n;  // a spot's: n toward its target
	double photons = 0, fade = 1, env = 0, rbd = 0;
	double color[3] = {1, 1, 1};
	bool linear = false, angle = true, spot = false;
	std::string target;
};
struct AreaL {
	Id owner = 0;
	Vec3 o, n;
	double dist = 0, add = 0, env = 0, area = 0;
	double color[3] = {1, 1, 1};
	Winding w;
};
struct Sun {
	Vec3 dir;  // toward it
	double photons = 0;
	double color[3] = {1, 1, 1};
};

double luma(const double c[3]) { return c[0] * 0.3 + c[1] * 0.59 + c[2] * 0.11; }

void normalize_color(double c[3]) {
	const double m = std::max({c[0], c[1], c[2]});
	if (m <= 0) {
		c[0] = c[1] = c[2] = 1;
		return;
	}
	for (int i = 0; i < 3; i++) {
		c[i] /= m;
	}
}

// q3map2's PointToPolygonFormFactor: the part of a point's hemisphere (cosine weighted) the polygon covers
double form_factor(const Vec3 &p, const Vec3 &n, const Winding &w) {
	Vec3 dirs[64];
	const size_t k = std::min(w.size(), size_t(63));
	for (size_t i = 0; i < k; i++) {
		dirs[i] = (w[i] - p).normalized();
	}
	dirs[k] = dirs[0];
	double total = 0;
	for (size_t i = 0; i < k; i++) {
		Vec3 tri = dirs[i].cross(dirs[i + 1]);
		const double len = tri.length();
		if (len < 0.0001) {
			continue;
		}
		tri = tri * (1 / len);
		const double angle = std::acos(std::clamp(dirs[i].dot(dirs[i + 1]), -1.0, 1.0));
		total += n.dot(tri) * angle;
		if (total > 6.3 || total < -6.3) {
			return 0;
		}
	}
	return total / (2 * PI);
}

double area_of(const Winding &w) {
	Vec3 sum;
	for (size_t i = 2; i < w.size(); i++) {
		sum = sum + (w[i - 1] - w[0]).cross(w[i] - w[0]);
	}
	return sum.length() * 0.5;
}

// the winding cut by the plane x[axis] = at: the part below, the part above
void split(const Winding &w, int axis, double at, Winding &lo, Winding &hi) {
	auto c = [&](const Vec3 &p) { return axis == 0 ? p.x : axis == 1 ? p.y : p.z; };
	for (size_t i = 0; i < w.size(); i++) {
		const Vec3 &a = w[i], &b = w[(i + 1) % w.size()];
		const double da = c(a) - at, db = c(b) - at;
		if (da <= 0) lo.push_back(a);
		if (da >= 0) hi.push_back(a);
		if ((da < 0 && db > 0) || (da > 0 && db < 0)) {
			const Vec3 m = a + (b - a) * (da / (da - db));
			lo.push_back(m);
			hi.push_back(m);
		}
	}
}

// q3map2's RadSubdivideDiffuseLight: pieces no wider than subdivide on any axis
void subdivide(const Winding &w, double size, std::vector<Winding> &out, int depth = 0) {
	if (w.size() < 3) {
		return;
	}
	Bounds b;
	for (const Vec3 &p : w) {
		b.add(p);
	}
	const Vec3 s = b.size();
	const double ext[3] = {s.x, s.y, s.z};
	for (int i = 0; i < 3 && depth < 24; i++) {
		if (ext[i] > size) {
			const double at = i == 0 ? (b.lo.x + b.hi.x) / 2 : i == 1 ? (b.lo.y + b.hi.y) / 2 : (b.lo.z + b.hi.z) / 2;
			Winding lo, hi;
			split(w, i, at, lo, hi);
			subdivide(lo, size, out, depth + 1);
			subdivide(hi, size, out, depth + 1);
			return;
		}
	}
	out.push_back(w);
}

}  // namespace

namespace {
std::vector<std::string> words_of(const std::string &script) {
	std::vector<std::string> out;
	size_t i = 0;
	while (i < script.size()) {
		if (script.compare(i, 2, "//") == 0) {
			i = script.find('\n', i);
			if (i == std::string::npos) {
				break;
			}
			continue;
		}
		if (std::isspace((unsigned char)script[i])) {
			i++;
			continue;
		}
		size_t j = i;
		while (j < script.size() && !std::isspace((unsigned char)script[j])) {
			j++;
		}
		out.push_back(script.substr(i, j - i));
		i = j;
	}
	return out;
}
}  // namespace

bool script_numbers(const std::string &script, const char *key, double *out, int n) {
	const std::vector<std::string> w = words_of(script);
	for (size_t i = 0; i < w.size(); i++) {
		if (w[i] != key) {
			continue;
		}
		if (i + size_t(n) >= w.size()) {
			return false;
		}
		for (int k = 0; k < n; k++) {
			char *end = nullptr;
			out[k] = std::strtod(w[i + 1 + size_t(k)].c_str(), &end);
			if (end == w[i + 1 + size_t(k)].c_str()) {
				return false;
			}
		}
		return true;
	}
	return false;
}

std::string script_word(const std::string &script, const char *key) {
	const std::vector<std::string> w = words_of(script);
	for (size_t i = 0; i + 1 < w.size(); i++) {
		if (w[i] == key) {
			return w[i + 1];
		}
	}
	return "";
}

bool script_has(const std::string &script, const char *key, const char *word) {
	const std::vector<std::string> w = words_of(script);
	for (size_t i = 0; i + 1 < w.size(); i++) {
		if (w[i] == key && w[i + 1] == word) {
			return true;
		}
	}
	return false;
}

bool SkyLight::same(const SkyLight &o) const {
	auto eq3 = [](const double *a, const double *b) { return a[0] == b[0] && a[1] == b[1] && a[2] == b[2]; };
	return sun == o.sun && eq3(sun_color, o.sun_color) && sun_dir.x == o.sun_dir.x && sun_dir.y == o.sun_dir.y && sun_dir.z == o.sun_dir.z &&
		sky == o.sky && eq3(sky_color, o.sky_color) && eq3(ambient, o.ambient) && flood == o.flood && eq3(flood_color, o.flood_color) &&
		flood_dist == o.flood_dist && flood_amount == o.flood_amount;
}

// ---------------------------------------------------------------- the scene the workers light from

struct LiveLight::Scene {
	std::vector<Occ> occ;
	std::vector<int> order;
	std::vector<Node> nodes;
	std::vector<PointL> points;
	std::vector<AreaL> areas;
	std::vector<Sun> suns;
	SkyLight sky;

	void build_tree() {
		order.resize(occ.size());
		for (size_t i = 0; i < occ.size(); i++) {
			order[i] = int(i);
		}
		nodes.clear();
		nodes.reserve(occ.size() * 2 + 1);
		if (!occ.empty()) {
			build(0, int(occ.size()));
		}
	}
	int build(int first, int count) {
		const int at = int(nodes.size());
		nodes.emplace_back();
		Node n;
		for (int k = 0; k < 3; k++) {
			n.lo[k] = 1e30f, n.hi[k] = -1e30f;
		}
		float clo[3] = {1e30f, 1e30f, 1e30f}, chi[3] = {-1e30f, -1e30f, -1e30f};
		for (int i = first; i < first + count; i++) {
			const Occ &o = occ[size_t(order[size_t(i)])];
			for (int k = 0; k < 3; k++) {
				n.lo[k] = std::min(n.lo[k], o.lo[k]);
				n.hi[k] = std::max(n.hi[k], o.hi[k]);
				const float c = (o.lo[k] + o.hi[k]) * 0.5f;
				clo[k] = std::min(clo[k], c);
				chi[k] = std::max(chi[k], c);
			}
		}
		if (count <= 4) {
			n.first = first, n.count = count;
			nodes[size_t(at)] = n;
			return at;
		}
		int axis = 0;
		for (int k = 1; k < 3; k++) {
			if (chi[k] - clo[k] > chi[axis] - clo[axis]) {
				axis = k;
			}
		}
		const int half = count / 2;
		std::nth_element(order.begin() + first, order.begin() + first + half, order.begin() + first + count, [&](int x, int y) {
			return occ[size_t(x)].lo[axis] + occ[size_t(x)].hi[axis] < occ[size_t(y)].lo[axis] + occ[size_t(y)].hi[axis];
		});
		nodes[size_t(at)] = n;
		const int a = build(first, half);
		const int b = build(first + half, count - half);
		nodes[size_t(at)].a = a;
		nodes[size_t(at)].b = b;
		return at;
	}

	static bool box(const float lo[3], const float hi[3], const double o[3], const double inv[3], double tmax) {
		double t0 = 0, t1 = tmax;
		for (int k = 0; k < 3; k++) {
			double a = (lo[k] - o[k]) * inv[k], b = (hi[k] - o[k]) * inv[k];
			if (a > b) {
				std::swap(a, b);
			}
			t0 = std::max(t0, a);
			t1 = std::min(t1, b);
			if (t0 > t1) {
				return false;
			}
		}
		return true;
	}
	// the segment o + t d (t from 0 to tmax) against a brush: where it comes in and by which plane (-1:
	// it starts inside)
	static bool clip(const Occ &oc, const Vec3 &o, const Vec3 &d, double tmax, double &tin, int &plane) {
		double t0 = 0, t1 = tmax;
		plane = -1;
		for (size_t k = 0; k < oc.planes.size(); k++) {
			const Plane &p = oc.planes[k];
			const double dn = p.n.dot(d), dist = p.n.dot(o) - p.d + SHRINK;
			if (std::fabs(dn) < 1e-12) {
				if (dist > 0) {
					return false;
				}
				continue;
			}
			const double t = -dist / dn;
			if (dn < 0) {
				if (t > t0) {
					t0 = t;
					plane = int(k);
				}
			} else if (t < t1) {
				t1 = t;
			}
			if (t0 > t1) {
				return false;
			}
		}
		tin = t0;
		return true;
	}
	// Visits the occluders whose boxes the segment meets; f returns true to stop.
	template <typename F> void walk(const Vec3 &o, const Vec3 &d, double tmax, F f) const {
		if (nodes.empty()) {
			return;
		}
		const double od[3] = {o.x, o.y, o.z};
		const double dd[3] = {d.x, d.y, d.z};
		double inv[3];
		for (int k = 0; k < 3; k++) {
			inv[k] = std::fabs(dd[k]) > 1e-12 ? 1 / dd[k] : (dd[k] < 0 ? -1e30 : 1e30);
		}
		int stack[96];
		int sp = 0;
		stack[sp++] = 0;
		while (sp) {
			const Node &n = nodes[size_t(stack[--sp])];
			if (!box(n.lo, n.hi, od, inv, tmax)) {
				continue;
			}
			if (n.count) {
				for (int i = n.first; i < n.first + n.count; i++) {
					if (f(occ[size_t(order[size_t(i)])])) {
						return;
					}
				}
			} else if (sp + 2 <= 96) {
				stack[sp++] = n.a;
				stack[sp++] = n.b;
			}
		}
	}
	// something light stops at between a and b
	bool blocked(const Vec3 &a, const Vec3 &b) const {
		const Vec3 d = b - a;
		bool hit = false;
		walk(a, d, 1, [&](const Occ &oc) {
			double t;
			int pl;
			if (clip(oc, a, d, 1, t, pl) && t < 1 - 1e-6) {
				hit = true;
			}
			return hit;
		});
		return hit;
	}
	// the first thing along d (a unit direction) within len: false nothing; sky: it came in by a sky face
	bool first_hit(const Vec3 &a, const Vec3 &d, double len, bool &sky, double &t) const {
		bool hit = false;
		double best = len;
		walk(a, d, len, [&](const Occ &oc) {
			double tin;
			int pl;
			if (clip(oc, a, d, best, tin, pl) && tin < best) {
				best = tin;
				hit = true;
				sky = pl >= 0 && oc.sky[size_t(pl)];
			}
			return false;
		});
		t = best;
		return hit;
	}
	bool in_solid(const Vec3 &p) const {
		bool in = false;
		const Vec3 d(0, 0, 1e-6);  // a point: a segment too short to leave it
		walk(p, d, 1, [&](const Occ &oc) {
			for (const Plane &pl : oc.planes) {
				if (pl.dist(p) > -SHRINK) {
					return false;
				}
			}
			in = true;
			return true;
		});
		return in;
	}
	bool sees_sky(const Vec3 &p, const Vec3 &dir) const {
		bool sky = false;
		double t;
		return !first_hit(p, dir, 65536, sky, t) || sky;
	}
};

// ---------------------------------------------------------------- jobs and the workers

struct LiveLight::Result {
	uint64_t key = 0, job = 0, layout = 0;
	std::vector<uint8_t> rgb;
	bool probes = false;
	std::vector<LiveLight::Probe> probe_out;
};

struct LiveLight::Sink {
	std::mutex m;
	std::vector<Result> done;
	std::atomic<int> outstanding{0};
};

struct LiveLight::Job {
	std::atomic<bool> cancelled{false};
	std::shared_ptr<const Scene> scene;
	std::shared_ptr<Sink> sink;
	uint64_t key = 0, num = 0, layout = 0;
	bool probes = false;
	std::vector<Vec3> probe_points;
	// the surface as laid out
	bool patch = false;
	int axis = 2, w = 0, h = 0, pw = 0, ph = 0;
	double S = 16, s0 = 0, t0 = 0;
	Plane plane;
	Winding poly;
	std::vector<Vec3> pts, normals;
	Vec3 center;
	double radius = 0;
	void run();
};

namespace {

// The light one point gets (q3map2's LightContributionToSample, each light's kind), into c.
struct Shader {
	const LiveLight::Scene &sc;
	std::vector<const PointL *> points;
	std::vector<const AreaL *> areas;
	Vec3 sky_dirs[SKY_RINGS * SKY_SPOKES];
	Vec3 flood_dirs[FLOOD_RINGS * FLOOD_SPOKES];

	explicit Shader(const LiveLight::Scene &s) : sc(s) {
		// cosine weighted rings over the half sphere (z up), for the sky; the fill light's as q3map2's,
		// its cone 88 degrees
		for (int r = 0; r < SKY_RINGS; r++) {
			const double u = (r + 0.5) / SKY_RINGS, st = std::sqrt(u), ct = std::sqrt(1 - u);
			for (int k = 0; k < SKY_SPOKES; k++) {
				const double a = 2 * PI * (k + 0.5 * (r & 1)) / SKY_SPOKES;
				sky_dirs[r * SKY_SPOKES + k] = Vec3(st * std::cos(a), st * std::sin(a), ct);
			}
		}
		for (int r = 0; r < FLOOD_RINGS; r++) {
			const double e = (88.0 / FLOOD_RINGS) * (r + 0.5) * PI / 180;
			for (int k = 0; k < FLOOD_SPOKES; k++) {
				const double a = 2 * PI * (k + 0.5 * (r & 1)) / FLOOD_SPOKES;
				flood_dirs[r * FLOOD_SPOKES + k] = Vec3(std::sin(e) * std::cos(a), std::sin(e) * std::sin(a), std::cos(e));
			}
		}
	}
	// the lights that may reach a surface in this sphere (and, for a flat one, in front of its plane)
	void gather(const Vec3 &c, double r, const Plane *plane) {
		points.clear();
		areas.clear();
		for (const PointL &l : sc.points) {
			if ((l.o - c).length() > l.env + r) {
				continue;
			}
			if (plane && plane->dist(l.o) < -1) {
				continue;  // behind it
			}
			points.push_back(&l);
		}
		for (const AreaL &l : sc.areas) {
			if ((l.o - c).length() > l.env + r) {
				continue;
			}
			if (plane && plane->dist(l.o) < -r - 1) {
				continue;
			}
			areas.push_back(&l);
		}
	}
	static void frame_of(const Vec3 &n, Vec3 &t, Vec3 &b) {
		const Vec3 up = std::fabs(n.z) < 0.9 ? Vec3(0, 0, 1) : Vec3(1, 0, 0);
		t = up.cross(n).normalized();
		b = n.cross(t);
	}
	// spin: turns the rays round the normal, a little differently each texel (filtered after). omni: a
	// point with no surface (a model's), every light as if faced
	void light(const Vec3 &p, const Vec3 &n, double spin, double c[3], Vec3 *dir, bool omni = false) const {
		for (int i = 0; i < 3; i++) {
			c[i] = sc.sky.ambient[i];
		}
		Vec3 dsum;
		auto add = [&](const double col[3], double a, const Vec3 &toward) {
			for (int i = 0; i < 3; i++) {
				c[i] += col[i] * a;
			}
			dsum = dsum + toward * (a * luma(col));
		};
		for (const PointL *l : points) {
			Vec3 d = l->o - p;
			double dist = d.length();
			if (dist >= l->env || dist < 1e-6) {
				continue;
			}
			d = d * (1 / dist);
			if (!omni && n.dot(l->o) - n.dot(p) < 0) {
				continue;  // behind the surface
			}
			double angle = 1;
			if (l->angle && !omni) {
				angle = n.dot(d);
				if (angle <= 0) {
					continue;
				}
			}
			dist = std::max(16.0, dist);
			double a = l->linear ? std::max(0.0, angle * l->photons * LINEAR_SCALE - dist * l->fade) : l->photons / (dist * dist) * angle;
			if (l->spot) {
				const double by_normal = (p - l->o).dot(l->n);
				if (by_normal < 0) {
					continue;
				}
				const double radius = l->rbd * by_normal;
				const double sr = (p - (l->o + l->n * by_normal)).length();
				if (sr >= radius) {
					continue;
				}
				if (sr > radius - 32) {
					a *= (radius - sr) / 32;
				}
			}
			if (a <= FALLOFF || sc.blocked(p, l->o)) {
				continue;
			}
			add(l->color, a, d);
		}
		for (const AreaL *l : areas) {
			if (!omni && n.dot(l->o) - n.dot(p) < 0) {
				continue;  // behind the surface
			}
			const double dd = p.dot(l->n) - l->dist;
			if (dd < 3 && !omni) {
				if (dd < -1) {
					continue;  // behind its plane
				}
				if (dd > -3 && n.dot(l->n) > 0.9) {
					continue;  // beside it, in its plane
				}
			}
			const Vec3 pushed = dd > -8 && dd < 8 ? p + l->n * (8 - dd) : p;
			const Vec3 d = l->o - p;
			const double dist = d.length();
			if (dist >= l->env) {
				continue;
			}
			const double factor = form_factor(pushed, omni && dist > 1e-6 ? d * (1 / dist) : n, l->w);
			if (factor <= 0) {
				continue;
			}
			const double a = factor * l->add;
			if (a <= FALLOFF || sc.blocked(p, l->o)) {
				continue;
			}
			add(l->color, a, dist > 0 ? d * (1 / dist) : n);
		}
		for (const Sun &s : sc.suns) {
			const double angle = omni ? 1 : n.dot(s.dir);
			if (angle <= 0 || !sc.sees_sky(p, s.dir)) {
				continue;
			}
			add(s.color, s.photons * angle, s.dir);
		}
		if (sc.sky.sky > 0 || sc.sky.flood) {
			Vec3 t, b;
			frame_of(n, t, b);
			const double cs = std::cos(spin), sn = std::sin(spin);
			const Vec3 t2 = t * cs + b * sn, b2 = b * cs - t * sn;
			if (sc.sky.sky > 0) {
				int seen = 0;
				Vec3 toward;
				for (const Vec3 &v : sky_dirs) {
					const Vec3 d = t2 * v.x + b2 * v.y + n * v.z;
					if (sc.sees_sky(p, d)) {
						seen++;
						toward = toward + d;
					}
				}
				if (seen) {
					// q3map2's sky faces are area lights of value * 3 * 0.25 times the part of the half sphere they fill
					const double a = sc.sky.sky * FORM_FACTOR_SCALE * AREA_SCALE * seen / double(SKY_RINGS * SKY_SPOKES);
					add(sc.sky.sky_color, a, toward.normalized());
				}
			}
			if (sc.sky.flood) {
				double gather = 0;
				for (const Vec3 &v : flood_dirs) {
					const Vec3 d = t2 * v.x + b2 * v.y + n * v.z;
					bool sky = false;
					double at;
					if (!sc.first_hit(p, d, sc.sky.flood_dist, sky, at) || sky) {
						gather += 1;
					} else {
						gather += std::min(1.0, at / sc.sky.flood_dist);
					}
				}
				gather /= FLOOD_RINGS * FLOOD_SPOKES;
				const double a = sc.sky.flood_amount * std::min(1.0, gather);
				for (int i = 0; i < 3; i++) {
					c[i] += sc.sky.flood_color[i] * a;
				}
			}
		}
		if (dir) {
			*dir = dsum;
		}
	}
};

// q3map2's ColorToBytes: clamped by color at 255
void to_bytes(const double c[3], uint8_t out[3]) {
	double s[3] = {std::max(0.0, c[0]), std::max(0.0, c[1]), std::max(0.0, c[2])};
	const double m = std::max({s[0], s[1], s[2]});
	if (m > 255) {
		for (double &v : s) {
			v *= 255 / m;
		}
	}
	for (int i = 0; i < 3; i++) {
		out[i] = uint8_t(std::min(255.0, s[i] + 0.5));
	}
}

class Pool {
public:
	static Pool &get() {
		static Pool *p = new Pool;  // never destroyed: its threads wait on it until the game exits
		return *p;
	}
	void push_front(std::vector<std::shared_ptr<LiveLight::Job>> &batch) {
		{
			std::lock_guard<std::mutex> l(m_);
			q_.insert(q_.begin(), batch.begin(), batch.end());
		}
		cv_.notify_all();
	}
	int threads() const { return n_; }

private:
	Pool() {
		const unsigned hw = std::thread::hardware_concurrency();
		n_ = std::clamp(int(hw ? hw / 2 : 2), 1, 8);
		for (int i = 0; i < n_; i++) {
			std::thread(&Pool::run, this).detach();
		}
	}
	void run() {
#ifdef _WIN32
		SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);  // the game's frames first
#endif
		for (;;) {
			std::shared_ptr<LiveLight::Job> j;
			{
				std::unique_lock<std::mutex> l(m_);
				cv_.wait(l, [&] { return !q_.empty(); });
				j = q_.front();
				q_.pop_front();
			}
			if (!j->cancelled.load()) {
				j->run();
			}
			j->sink->outstanding--;
		}
	}
	std::mutex m_;
	std::condition_variable cv_;
	std::deque<std::shared_ptr<LiveLight::Job>> q_;
	int n_ = 1;
};

}  // namespace

void LiveLight::Job::run() {
	Result r;
	r.key = key, r.job = num, r.layout = layout;
	Shader sh(*scene);
	if (probes) {
		// the models' light: as the light grid gives it, all light toward the point, part of it directed
		r.probes = true;
		for (const Vec3 &p : probe_points) {
			Probe pr;
			pr.at = p;
			sh.gather(p, 0, nullptr);
			double c[3];
			Vec3 dir;
			sh.light(p, Vec3(0, 0, 1), 0, c, &dir, true);
			double total[3];
			for (int i = 0; i < 3; i++) {
				total[i] = c[i] - scene->sky.ambient[i];
			}
			const double dl = dir.length();
			const Vec3 toward = dl > 1e-9 ? dir * (1 / dl) : Vec3(0.4, 0.25, 0.88).normalized();
			// as rend2 takes the grid: shifted by the map's overbright bit, ambient at 0.6
			for (int i = 0; i < 3; i++) {
				const double all = total[i] * 2;
				const double directed = all * 0.6, ambient = all * 0.4 * 0.6 + scene->sky.ambient[i] * 2 + 12;
				pr.directed[i] = float(std::min(255.0, directed));
				pr.ambient[i] = float(std::min(255.0, ambient));
			}
			pr.dir[0] = float(toward.x), pr.dir[1] = float(toward.y), pr.dir[2] = float(toward.z);
			r.probe_out.push_back(pr);
		}
		std::lock_guard<std::mutex> l(sink->m);
		sink->done.push_back(std::move(r));
		return;
	}
	const int W = patch ? pw + 2 : w, H = patch ? ph + 2 : h;
	std::vector<double> col(size_t(W) * H * 3, 0.0);
	std::vector<uint8_t> ok(size_t(W) * H, 0);
	sh.gather(center, radius + 2, patch ? nullptr : &plane);
	auto spin_of = [](int i, int j) { return ((i * 73856093u) ^ (j * 19349663u)) % 1024 * (2 * PI / 1024); };
	if (!patch) {
		// the face on its axial plane: texel (i, j)'s grid point, on the face (or its nearest point on it)
		const int u = axis == 0 ? 1 : 0, v = axis == 2 ? 1 : 2;
		auto c2 = [](const Vec3 &p, int k) { return k == 0 ? p.x : k == 1 ? p.y : p.z; };
		std::vector<double> px, py;
		double cx = 0, cy = 0;
		for (const Vec3 &p : poly) {
			px.push_back(c2(p, u));
			py.push_back(c2(p, v));
			cx += px.back(), cy += py.back();
		}
		const size_t np = poly.size();
		cx /= double(np), cy /= double(np);
		double area = 0;
		for (size_t k = 0; k < np; k++) {
			const size_t k2 = (k + 1) % np;
			area += px[k] * py[k2] - px[k2] * py[k];
		}
		const double orient = area >= 0 ? 1 : -1;
		const double na = c2(plane.n, axis);
		auto on_plane = [&](double a, double b) {
			const double rest = plane.d - c2(plane.n, u) * a - c2(plane.n, v) * b;
			const double z = std::fabs(na) > 1e-9 ? rest / na : 0;
			double xyz[3];
			xyz[u] = a, xyz[v] = b, xyz[axis] = z;
			return Vec3(xyz[0], xyz[1], xyz[2]);
		};
		for (int j = 0; j < H; j++) {
			for (int i = 0; i < W; i++) {
				double a = (s0 + i) * S, b = (t0 + j) * S;  // q3map2's luxels sit on the sample grid
				// inside the polygon? else its nearest point on the edge, pulled a unit in
				bool inside = true;
				for (size_t k = 0; k < np && inside; k++) {
					const size_t k2 = (k + 1) % np;
					const double e = (px[k2] - px[k]) * (b - py[k]) - (py[k2] - py[k]) * (a - px[k]);
					inside = e * orient >= -1e-9;
				}
				if (!inside) {
					double best = 1e30, ba = a, bb = b;
					for (size_t k = 0; k < np; k++) {
						const size_t k2 = (k + 1) % np;
						const double ex = px[k2] - px[k], ey = py[k2] - py[k];
						const double len2 = ex * ex + ey * ey;
						double t = len2 > 1e-12 ? ((a - px[k]) * ex + (b - py[k]) * ey) / len2 : 0;
						t = std::clamp(t, 0.0, 1.0);
						const double qx = px[k] + ex * t, qy = py[k] + ey * t;
						const double d2 = (qx - a) * (qx - a) + (qy - b) * (qy - b);
						if (d2 < best) {
							best = d2, ba = qx, bb = qy;
						}
					}
					const double dx = cx - ba, dy = cy - bb, dl = std::sqrt(dx * dx + dy * dy);
					a = ba + (dl > 1 ? dx / dl : dx);
					b = bb + (dl > 1 ? dy / dl : dy);
				}
				Vec3 p = on_plane(a, b) + plane.n * LIFT;
				if (scene->in_solid(p)) {
					// nudged toward the face's middle, as q3map2 nudges a texel out of solid
					bool found = false;
					for (double f : {0.25, 0.5, 0.75}) {
						const Vec3 q = on_plane(a + (cx - a) * f, b + (cy - b) * f) + plane.n * LIFT;
						if (!scene->in_solid(q)) {
							p = q;
							found = true;
							break;
						}
					}
					if (!found) {
						continue;  // in solid: filled from its neighbors
					}
				}
				double c[3];
				sh.light(p, plane.n, spin_of(i, j), c, nullptr);
				for (int k = 0; k < 3; k++) {
					col[(size_t(j) * W + i) * 3 + k] = c[k];
				}
				ok[size_t(j) * W + i] = 1;
			}
		}
	} else {
		// a curve: a texel a tessellated vertex, with a border
		for (int j = 0; j < ph; j++) {
			for (int i = 0; i < pw; i++) {
				const size_t k = size_t(j) * pw + i;
				const Vec3 n = k < normals.size() ? normals[k] : Vec3(0, 0, 1);
				Vec3 p = pts[k] + n * LIFT;
				if (scene->in_solid(p)) {
					p = pts[k] + n * 4;
					if (scene->in_solid(p)) {
						continue;
					}
				}
				double c[3];
				sh.light(p, n, spin_of(i, j), c, nullptr);
				for (int q = 0; q < 3; q++) {
					col[(size_t(j + 1) * W + i + 1) * 3 + q] = c[q];
				}
				ok[size_t(j + 1) * W + i + 1] = 1;
			}
		}
	}
	if (cancelled.load()) {
		return;
	}
	// texels in solid (and a curve's border) from their neighbors, ring by ring
	bool any = false;
	for (uint8_t o : ok) {
		any = any || o;
	}
	if (!any) {
		for (int k = 0; k < W * H; k++) {
			for (int q = 0; q < 3; q++) {
				col[size_t(k) * 3 + q] = scene->sky.ambient[q];
			}
		}
	} else {
		std::vector<uint8_t> next = ok;
		for (int pass = 0; pass < W + H; pass++) {
			bool left = false;
			for (int j = 0; j < H; j++) {
				for (int i = 0; i < W; i++) {
					if (ok[size_t(j) * W + i]) {
						continue;
					}
					double sum[3] = {0, 0, 0};
					int n = 0;
					for (int dj = -1; dj <= 1; dj++) {
						for (int di = -1; di <= 1; di++) {
							const int x = i + di, y = j + dj;
							if (x < 0 || y < 0 || x >= W || y >= H || !ok[size_t(y) * W + x]) {
								continue;
							}
							for (int q = 0; q < 3; q++) {
								sum[q] += col[(size_t(y) * W + x) * 3 + q];
							}
							n++;
						}
					}
					if (n) {
						for (int q = 0; q < 3; q++) {
							col[(size_t(j) * W + i) * 3 + q] = sum[q] / n;
						}
						next[size_t(j) * W + i] = 1;
					} else {
						left = true;
					}
				}
			}
			ok = next;
			if (!left) {
				break;
			}
		}
	}
	// q3map2's -filter: each texel with its neighbors (1 2 1)
	std::vector<double> f(col.size());
	for (int j = 0; j < H; j++) {
		for (int i = 0; i < W; i++) {
			double sum[3] = {0, 0, 0}, wsum = 0;
			for (int dj = -1; dj <= 1; dj++) {
				for (int di = -1; di <= 1; di++) {
					const int x = i + di, y = j + dj;
					if (x < 0 || y < 0 || x >= W || y >= H) {
						continue;
					}
					const double wt = (di ? 1 : 2) * (dj ? 1 : 2);
					for (int q = 0; q < 3; q++) {
						sum[q] += col[(size_t(y) * W + x) * 3 + q] * wt;
					}
					wsum += wt;
				}
			}
			for (int q = 0; q < 3; q++) {
				f[(size_t(j) * W + i) * 3 + q] = sum[q] / wsum;
			}
		}
	}
	r.rgb.resize(size_t(W) * H * 3);
	for (int k = 0; k < W * H; k++) {
		to_bytes(&f[size_t(k) * 3], &r.rgb[size_t(k) * 3]);
	}
	std::lock_guard<std::mutex> l(sink->m);
	sink->done.push_back(std::move(r));
}

// ---------------------------------------------------------------- LiveLight

LiveLight::LiveLight() : sink_(std::make_shared<Sink>()) {}

LiveLight::~LiveLight() {
	for (auto &kv : surfs_) {
		if (kv.second.queued) {
			kv.second.queued->cancelled = true;
		}
	}
}

int LiveLight::threads() const {
	return Pool::get().threads();
}

int LiveLight::pending() const {
	return std::max(0, sink_->outstanding.load());
}

int LiveLight::lights() const {
	return scene_ ? int(scene_->points.size() + scene_->areas.size() + scene_->suns.size()) : 0;
}

std::vector<Id> LiveLight::take_relaid() {
	std::vector<Id> out;
	out.swap(relaid_);
	if (all_relaid_) {
		all_relaid_ = false;
		out.push_back(0);  // 0: every object
	}
	return out;
}

void LiveLight::renderer_restarted() {
	generation_ = OzE_RendererGeneration();
	const ozEdRender_t *r = OzEd_Renderer();
	for (Page &p : pages_) {
		p.handle = r && r->CreateLightmap ? r->CreateLightmap(PAGE, PAGE) : -1;
		if (p.handle >= 0) {
			r->UploadLightmap(p.handle, 0, 0, PAGE, PAGE, p.rgb.data());
		}
	}
	all_relaid_ = true;
}

bool LiveLight::place(int w, int h, int &page, int &x, int &y) {
	// a freed place that fits, then a shelf, then a new shelf, then a new page
	for (size_t pi = 0; pi < pages_.size(); pi++) {
		Page &p = pages_[pi];
		for (size_t k = 0; k < p.free.size(); k++) {
			Page::Free &f = p.free[k];
			if (f.w >= w && f.h >= h && f.w * f.h <= w * h * 2) {
				page = int(pi), x = f.x, y = f.y;
				p.free.erase(p.free.begin() + long(k));
				return true;
			}
		}
	}
	for (size_t pi = 0; pi < pages_.size(); pi++) {
		Page &p = pages_[pi];
		Page::Shelf *best = nullptr;
		for (Page::Shelf &s : p.shelves) {
			if (s.h >= h && s.h <= h * 2 + 2 && s.x + w <= PAGE && (!best || s.h < best->h)) {
				best = &s;
			}
		}
		if (best) {
			page = int(pi), x = best->x, y = best->y;
			best->x += w;
			return true;
		}
		if (p.shelf_y + h <= PAGE) {
			p.shelves.push_back({p.shelf_y, h, w});
			page = int(pi), x = 0, y = p.shelf_y;
			p.shelf_y += h;
			return true;
		}
	}
	const ozEdRender_t *r = OzEd_Renderer();
	if (!r || !r->CreateLightmap || pages_.size() >= 24) {
		place_failed_ += r && r->CreateLightmap ? 1 : 0;  // the pages are full: the face draws unlit, and the editor says so
		return false;
	}
	Page p;
	p.handle = r->CreateLightmap(PAGE, PAGE);
	if (p.handle < 0) {
		place_failed_++;
		return false;
	}
	p.rgb.assign(size_t(PAGE) * PAGE * 3, 0);
	p.shelves.push_back({0, h, w});
	p.shelf_y = h;
	pages_.push_back(std::move(p));
	page = int(pages_.size()) - 1, x = 0, y = 0;
	return true;
}

bool LiveLight::lay_out(Surf &s) {
	s.page = -1;
	if (s.face >= 0) {
		const Vec3 n = s.plane.n;
		const double ax = std::fabs(n.x), ay = std::fabs(n.y), az = std::fabs(n.z);
		s.axis = ax >= ay && ax >= az ? 0 : ay >= az ? 1 : 2;
		const int u = s.axis == 0 ? 1 : 0, v = s.axis == 2 ? 1 : 2;
		auto c2 = [](const Vec3 &p, int k) { return k == 0 ? p.x : k == 1 ? p.y : p.z; };
		double lo[2] = {1e30, 1e30}, hi[2] = {-1e30, -1e30};
		for (const Vec3 &p : s.poly) {
			lo[0] = std::min(lo[0], c2(p, u)), hi[0] = std::max(hi[0], c2(p, u));
			lo[1] = std::min(lo[1], c2(p, v)), hi[1] = std::max(hi[1], c2(p, v));
		}
		s.S = SAMPLE;
		for (int tries = 0; tries < 8; tries++) {
			s.s0 = std::floor(lo[0] / s.S) - 1;
			s.t0 = std::floor(lo[1] / s.S) - 1;
			s.w = int(std::ceil(hi[0] / s.S) - s.s0) + 2;
			s.h = int(std::ceil(hi[1] / s.S) - s.t0) + 2;
			if (s.w <= MAX_SIDE && s.h <= MAX_SIDE) {
				break;
			}
			s.S *= std::ceil(double(std::max(s.w, s.h)) / MAX_SIDE);
		}
		if (s.w > MAX_SIDE || s.h > MAX_SIDE) {
			return false;
		}
	} else {
		s.w = s.pw + 2, s.h = s.ph + 2;
		if (s.w > PAGE || s.h > PAGE) {
			return false;
		}
	}
	if (!place(s.w, s.h, s.page, s.x, s.y)) {
		s.page = -1;
		return false;
	}
	pages_[size_t(s.page)].used += s.w * s.h;
	s.layout = next_layout_++;
	return true;
}

void LiveLight::free_place(Surf &s) {
	if (s.page < 0 || s.page >= int(pages_.size())) {
		return;
	}
	Page &p = pages_[size_t(s.page)];
	p.free.push_back({s.x, s.y, s.w, s.h});
	p.used -= s.w * s.h;
	s.page = -1;
}

void LiveLight::repack() {
	// every face placed again, its texels with it
	std::vector<std::pair<uint64_t, std::vector<uint8_t>>> kept;
	std::vector<uint64_t> keys;
	for (auto &kv : surfs_) {
		Surf &s = kv.second;
		if (s.page < 0) {
			continue;
		}
		std::vector<uint8_t> t(size_t(s.w) * s.h * 3);
		const Page &p = pages_[size_t(s.page)];
		for (int j = 0; j < s.h; j++) {
			std::memcpy(&t[size_t(j) * s.w * 3], &p.rgb[(size_t(s.y + j) * PAGE + s.x) * 3], size_t(s.w) * 3);
		}
		kept.emplace_back(kv.first, std::move(t));
	}
	for (Page &p : pages_) {
		p.shelves.clear();
		p.free.clear();
		p.shelf_y = 0;
		p.used = 0;
	}
	std::sort(kept.begin(), kept.end(), [&](const auto &a, const auto &b) { return surfs_[a.first].h > surfs_[b.first].h; });
	for (auto &kt : kept) {
		Surf &s = surfs_[kt.first];
		if (!place(s.w, s.h, s.page, s.x, s.y)) {
			s.page = -1;
			continue;
		}
		pages_[size_t(s.page)].used += s.w * s.h;
		s.layout = next_layout_++;
		Page &p = pages_[size_t(s.page)];
		for (int j = 0; j < s.h; j++) {
			std::memcpy(&p.rgb[(size_t(s.y + j) * PAGE + s.x) * 3], &kt.second[size_t(j) * s.w * 3], size_t(s.w) * 3);
		}
		if (s.queued) {
			s.queued->cancelled = true;  // its layout is gone: asked again below
			s.queued.reset();
			keys.push_back(kt.first);
		}
	}
	const ozEdRender_t *r = OzEd_Renderer();
	for (Page &p : pages_) {
		if (r && p.handle >= 0) {
			r->UploadLightmap(p.handle, 0, 0, PAGE, PAGE, p.rgb.data());
		}
	}
	all_relaid_ = true;
	if (!keys.empty()) {
		queue(keys, eye_);
	}
}

void LiveLight::fill_placeholder(Surf &s, const uint8_t rgb[3]) {
	if (s.page < 0) {
		return;
	}
	Page &p = pages_[size_t(s.page)];
	for (int j = 0; j < s.h; j++) {
		for (int i = 0; i < s.w; i++) {
			std::memcpy(&p.rgb[(size_t(s.y + j) * PAGE + s.x + i) * 3], rgb, 3);
		}
	}
	upload(s);
}

void LiveLight::upload(Surf &s) {
	const ozEdRender_t *r = OzEd_Renderer();
	if (!r || s.page < 0 || pages_[size_t(s.page)].handle < 0) {
		return;
	}
	const Page &p = pages_[size_t(s.page)];
	static std::vector<uint8_t> t;
	t.resize(size_t(s.w) * s.h * 3);
	for (int j = 0; j < s.h; j++) {
		std::memcpy(&t[size_t(j) * s.w * 3], &p.rgb[(size_t(s.y + j) * PAGE + s.x) * 3], size_t(s.w) * 3);
	}
	r->UploadLightmap(p.handle, s.x, s.y, s.w, s.h, t.data());
}

void LiveLight::add_object(const Doc &doc, Id id, const Object &o) {
	std::vector<uint64_t> &keys = by_id_[id];
	auto finish = [&](Surf &s) {
		s.center = s.b.center();
		s.radius = (s.b.hi - s.center).length();
	};
	if (o.kind == Kind::Brush) {
		const auto &ws = doc.windings(id);
		for (size_t i = 0; i < o.brush.faces.size() && i < ws.size(); i++) {
			if (ws[i].size() < 3) {
				continue;
			}
			const Face &f = o.brush.faces[i];
			Surf s;
			s.m = face_mat ? face_mat(f) : LightMat();
			if (!s.m.lightmapped) {
				continue;
			}
			s.id = id, s.face = int(i), s.obj = doc.ptr(id);
			s.plane = f.plane;
			s.poly = ws[i];
			for (const Vec3 &p : s.poly) {
				s.b.add(p);
			}
			finish(s);
			if (!lay_out(s)) {
				continue;
			}
			const uint64_t k = key(id, int(i));
			keys.push_back(k);
			surfs_[k] = std::move(s);
		}
	} else if (o.kind == Kind::Patch) {
		Surf s;
		s.m = mat ? mat(o.patch.shader) : LightMat();
		if (!s.m.lightmapped) {
			return;
		}
		const q3map::PatchMesh pm = patch_mesh(o.patch, PATCH_TOLERANCE);
		if (pm.verts.empty()) {
			return;
		}
		s.id = id, s.face = -1, s.obj = doc.ptr(id);
		s.pw = pm.width, s.ph = pm.height;
		for (size_t k = 0; k < pm.verts.size(); k++) {
			s.pts.push_back(pm.verts[k].p);
			s.normals.push_back(k < pm.normals.size() ? pm.normals[k] : Vec3(0, 0, 1));
			s.b.add(pm.verts[k].p);
		}
		finish(s);
		if (!lay_out(s)) {
			return;
		}
		const uint64_t k = key(id, -1);
		keys.push_back(k);
		surfs_[k] = std::move(s);
	}
}

void LiveLight::drop_object(Id id) {
	auto it = by_id_.find(id);
	if (it == by_id_.end()) {
		return;
	}
	for (uint64_t k : it->second) {
		auto s = surfs_.find(k);
		if (s == surfs_.end()) {
			continue;
		}
		if (s->second.queued) {
			s->second.queued->cancelled = true;
		}
		free_place(s->second);
		surfs_.erase(s);
	}
	by_id_.erase(it);
}

void LiveLight::build_scene(const Doc &doc, const SkyLight &sky, const Moved &moved) {
	auto sc = std::make_shared<Scene>();
	sc->sky = sky;
	const Id world = doc.worldspawn();
	std::unordered_map<std::string, Vec3> targets;
	auto origin_of = [](const Entity &e, Vec3 &o) {
		double x, y, z;
		if (std::sscanf(e.get("origin").c_str(), "%lf %lf %lf", &x, &y, &z) != 3) {
			return false;
		}
		o = Vec3(x, y, z);
		return true;
	};
	for (Id id : doc.ids_of(Kind::Entity)) {
		const Entity &e = doc.get(id)->entity;
		const std::string tn = e.get("targetname");
		Vec3 o;
		if (!tn.empty() && origin_of(e, o)) {
			targets[tn] = o;
		}
	}
	auto area_from = [&](Id owner, const Winding &w, const Vec3 &n, const LightMat &m) {
		// q3map2's surface light: each piece an area light, and its backsplash 23 units out, facing back
		std::vector<Winding> pieces;
		subdivide(w, m.subdivide > 0 ? m.subdivide : 999, pieces);
		for (Winding &pw : pieces) {
			const double area = area_of(pw);
			if (area < 1) {
				continue;
			}
			Bounds b;
			for (const Vec3 &p : pw) {
				b.add(p);
			}
			for (int side = 0; side < (BACKSPLASH_FRACTION > 0 ? 2 : 1); side++) {
				AreaL l;
				l.owner = owner;
				const double scale = side ? 7 * BACKSPLASH_FRACTION : 1;
				l.add = m.light * FORM_FACTOR_SCALE * AREA_SCALE * scale;
				l.area = area;
				for (int i = 0; i < 3; i++) {
					l.color[i] = m.color[i];
				}
				if (!side) {
					l.n = n;
					l.o = b.center() + n;
					l.w = pw;
				} else {
					l.n = -n;
					l.o = b.center() + n + n * BACKSPLASH_DISTANCE;
					for (size_t i = pw.size(); i-- > 0;) {
						l.w.push_back(pw[i] + n * BACKSPLASH_DISTANCE);
					}
				}
				l.dist = l.o.dot(l.n);
				// the winding's way round, as q3map2's surfaces go: lit in front
				const Vec3 test = l.o + l.n * 64;
				if (form_factor(test, -l.n, l.w) < 0) {
					std::reverse(l.w.begin(), l.w.end());
				}
				// -fast's envelope: out along its normal until the light is under the threshold
				l.env = 1e9;
				for (double r = 100; r < 65536; r += std::max(10.0, r * 0.05)) {
					if (std::fabs(form_factor(l.o + l.n * r, -l.n, l.w)) * l.add <= FALLOFF) {
						l.env = r + (b.hi - b.lo).length() * 0.5;
						break;
					}
				}
				sc->areas.push_back(std::move(l));
			}
		}
	};
	for (Id id : doc.ids()) {
		const Object *o = doc.get(id);
		if (o->kind == Kind::Entity) {
			const Entity &e = o->entity;
			if (e.classname() != "light") {
				continue;
			}
			PointL l;
			l.owner = id;
			if (!origin_of(e, l.o)) {
				continue;
			}
			for (const auto &mv : moved) {
				if (mv.first == id) {
					l.o = mv.second;  // being dragged: where it is now
				}
			}
			const int flags = std::atoi(e.get("spawnflags", "0").c_str());
			l.linear = (flags & 1) != 0;
			l.angle = !(flags & 1) && !(flags & 2);
			double intensity = std::atof(e.get("_light").c_str());
			if (intensity == 0) {
				intensity = std::atof(e.get("light").c_str());
			}
			if (intensity == 0) {
				intensity = 300;
			}
			const double scale = std::atof(e.get("scale").c_str());
			if (scale != 0) {
				intensity *= scale;
			}
			if (std::sscanf(e.get("_color").c_str(), "%lf %lf %lf", &l.color[0], &l.color[1], &l.color[2]) == 3) {
				normalize_color(l.color);
			}
			if (l.linear) {
				l.fade = std::atof(e.get("fade").c_str());
				if (l.fade == 0) {
					l.fade = 1;
				}
			}
			const std::string target = e.get("target");
			auto t = target.empty() ? targets.end() : targets.find(target);
			l.photons = intensity * POINT_SCALE;
			if (t != targets.end()) {
				Vec3 d = t->second - l.o;
				double dist = d.length();
				l.n = dist > 0 ? d * (1 / dist) : Vec3(0, 0, -1);
				double radius = std::atof(e.get("radius").c_str());
				if (radius == 0) {
					radius = 64;
				}
				if (dist == 0) {
					dist = 64;
				}
				if (std::atoi(e.get("_sun", "0").c_str())) {
					Sun s;  // a sun aimed by its target
					s.dir = -l.n;
					s.photons = intensity;
					for (int i = 0; i < 3; i++) {
						s.color[i] = l.color[i];
					}
					sc->suns.push_back(s);
					continue;
				}
				l.spot = true;
				l.rbd = (radius + 16) / dist;
				l.linear = false;
				l.angle = true;
				l.fade = 1;
				l.photons = intensity * SPOT_SCALE;
				l.target = target;
			}
			l.env = l.linear ? (l.photons * LINEAR_SCALE - FALLOFF) / l.fade : std::sqrt(l.photons / FALLOFF);
			if (l.env > 0) {
				sc->points.push_back(std::move(l));
			}
		} else if (o->kind == Kind::Brush) {
			const auto &ws = doc.windings(id);
			const Object *owner = doc.get(o->owner);
			const bool in_world = o->owner == world || (owner && owner->entity.classname() == "func_group");
			Occ oc;
			bool blocks = in_world;
			Bounds b;
			for (size_t i = 0; i < o->brush.faces.size(); i++) {
				const Face &f = o->brush.faces[i];
				if (f.plane.n.length() < 0.5) {
					continue;
				}
				const LightMat m = face_mat ? face_mat(f) : LightMat();
				if (i < ws.size() && ws[i].size() >= 3) {
					for (const Vec3 &p : ws[i]) {
						b.add(p);
					}
					if (m.light > 0 && !m.sky) {
						area_from(id, ws[i], f.plane.n, m);
					}
				}
				blocks = blocks && (m.blocks || m.sky);
				oc.planes.push_back(f.plane);
				oc.sky.push_back(m.sky ? 1 : 0);
			}
			if (blocks && !b.empty() && oc.planes.size() >= 4) {
				oc.lo[0] = float(b.lo.x) - 1, oc.lo[1] = float(b.lo.y) - 1, oc.lo[2] = float(b.lo.z) - 1;
				oc.hi[0] = float(b.hi.x) + 1, oc.hi[1] = float(b.hi.y) + 1, oc.hi[2] = float(b.hi.z) + 1;
				sc->occ.push_back(std::move(oc));
			}
		} else if (o->kind == Kind::Patch) {
			const LightMat m = mat ? mat(o->patch.shader) : LightMat();
			if (m.light <= 0 || m.sky) {
				continue;
			}
			// a glowing curve: its coarse quads, each a light
			const q3map::PatchMesh pm = patch_mesh(o->patch, 32);
			for (int r = 0; r + 1 < pm.height; r++) {
				for (int c = 0; c + 1 < pm.width; c++) {
					const size_t a = size_t(r) * pm.width + c;
					const Winding w = {pm.verts[a].p, pm.verts[a + 1].p, pm.verts[a + pm.width + 1].p, pm.verts[a + pm.width].p};
					Vec3 n = (w[1] - w[0]).cross(w[3] - w[0]);
					if (n.length() < 1e-6 || a >= pm.normals.size()) {
						continue;
					}
					n = n.normalized();
					if (n.dot(pm.normals[a]) < 0) {
						n = -n;
					}
					area_from(id, w, n, m);
				}
			}
		}
	}
	if (sky.sun > 0) {
		Sun s;
		s.dir = sky.sun_dir.normalized();
		s.photons = sky.sun;
		for (int i = 0; i < 3; i++) {
			s.color[i] = sky.sun_color[i];
		}
		sc->suns.push_back(s);
	}
	sc->build_tree();
	scene_ = sc;
}

void LiveLight::mark_light_reach(const Vec3 &at, double reach, std::vector<uint64_t> &dirty) const {
	for (const auto &kv : surfs_) {
		if ((kv.second.center - at).length() <= reach + kv.second.radius) {
			dirty.push_back(kv.first);
		}
	}
}

void LiveLight::mark_shade(const Bounds &bb, std::vector<uint64_t> &dirty) const {
	if (bb.empty() || !scene_) {
		return;
	}
	const Vec3 bc = bb.center();
	const double br = (bb.hi - bc).length() + 2;
	const Scene &sc = *scene_;
	struct L {
		Vec3 o;
		double env;
	};
	std::vector<L> nearby;
	for (const PointL &l : sc.points) {
		if ((l.o - bc).length() <= l.env + br) {
			nearby.push_back({l.o, l.env});
		}
	}
	for (const AreaL &l : sc.areas) {
		if ((l.o - bc).length() <= l.env + br) {
			nearby.push_back({l.o, l.env});
		}
	}
	for (const auto &kv : surfs_) {
		const Surf &s = kv.second;
		const double sr = s.radius;
		const Vec3 sc2 = s.center;
		bool hit = (sc2 - bc).length() <= br + sr + 32;  // its own faces, what touches it, and the sky and fill light round it
		if (!hit && (sc.sky.sky > 0 || sc.sky.flood)) {
			hit = (sc2 - bc).length() <= br + sr + 512;
		}
		for (size_t i = 0; i < nearby.size() && !hit; i++) {
			const Vec3 to_b = bc - nearby[i].o, to_s = sc2 - nearby[i].o;
			const double db = to_b.length(), ds = to_s.length();
			if (ds > nearby[i].env + sr) {
				continue;
			}
			if (db <= br) {
				hit = true;  // the brush is round the light
				break;
			}
			if (ds + sr < db - br) {
				continue;  // all nearer the light than the brush
			}
			const double alpha = std::asin(std::min(1.0, br / db)) + (ds > 1e-6 ? std::asin(std::min(1.0, sr / ds)) : PI);
			const double ang = ds > 1e-6 ? std::acos(std::clamp(to_b.dot(to_s) / (db * ds), -1.0, 1.0)) : 0;
			hit = ang <= alpha;
		}
		for (size_t i = 0; i < sc.suns.size() && !hit; i++) {
			// in the brush's shadow from the sun: along the sun's way past it
			const Vec3 away = -sc.suns[i].dir;
			const Vec3 v = sc2 - bc;
			const double along = v.dot(away);
			if (along < -(br + sr)) {
				continue;
			}
			hit = (v - away * along).length() <= br + sr;
		}
		if (hit) {
			dirty.push_back(kv.first);
		}
	}
}

void LiveLight::queue(const std::vector<uint64_t> &keys_in, const Vec3 &eye) {
	if (!scene_) {
		return;
	}
	std::vector<uint64_t> keys = keys_in;
	std::sort(keys.begin(), keys.end());
	keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
	std::vector<std::pair<double, uint64_t>> order;
	for (uint64_t k : keys) {
		auto it = surfs_.find(k);
		if (it == surfs_.end() || it->second.page < 0) {
			continue;
		}
		order.emplace_back(std::max(0.0, (it->second.center - eye).length() - it->second.radius), k);
	}
	std::sort(order.begin(), order.end());
	std::vector<std::shared_ptr<Job>> batch;
	for (auto &ok : order) {
		Surf &s = surfs_[ok.second];
		if (s.queued) {
			s.queued->cancelled = true;
		}
		auto j = std::make_shared<Job>();
		j->scene = scene_;
		j->sink = sink_;
		j->key = ok.second;
		j->num = next_job_++;
		j->layout = s.layout;
		j->patch = s.face < 0;
		j->axis = s.axis, j->w = s.w, j->h = s.h, j->pw = s.pw, j->ph = s.ph;
		j->S = s.S, j->s0 = s.s0, j->t0 = s.t0;
		j->plane = s.plane;
		j->poly = s.poly;
		j->pts = s.pts;
		j->normals = s.normals;
		j->center = s.center;
		j->radius = s.radius;
		s.want = j->num;
		s.queued = j;
		batch.push_back(j);
	}
	if (batch.empty()) {
		return;
	}
	if (!sink_->outstanding.load()) {
		batch_t0_ = OzE_Milliseconds();
	}
	sink_->outstanding += int(batch.size());
	Pool::get().push_front(batch);
}

void LiveLight::queue_probes() {
	if (!scene_) {
		return;
	}
	probes_due_ = false;
	auto j = std::make_shared<Job>();
	j->scene = scene_;
	j->sink = sink_;
	j->probes = true;
	j->num = next_job_++;
	j->probe_points = probe_points_;
	probe_job_ = j->num;
	sink_->outstanding++;
	std::vector<std::shared_ptr<Job>> batch{j};
	Pool::get().push_front(batch);
}

void LiveLight::set_probe_points(const std::vector<Vec3> &points) {
	bool same = points.size() == probe_points_.size();
	for (size_t i = 0; same && i < points.size(); i++) {
		same = (points[i] - probe_points_[i]).length() < 0.01;
	}
	if (!same) {
		probe_points_ = points;
		probes_due_ = true;
	}
}

bool LiveLight::probes_changed() {
	const bool c = probes_new_;
	probes_new_ = false;
	return c;
}

void LiveLight::collect(int budget_ms) {
	std::vector<Result> done;
	{
		std::lock_guard<std::mutex> l(sink_->m);
		done.swap(sink_->done);
	}
	const int t0 = OzE_Milliseconds();
	size_t i = 0;
	for (; i < done.size(); i++) {
		Result &r = done[i];
		if (r.probes) {
			if (r.job == probe_job_) {
				probes_ = std::move(r.probe_out);
				probes_new_ = true;
			}
			continue;
		}
		auto it = surfs_.find(r.key);
		if (it == surfs_.end()) {
			continue;
		}
		Surf &s = it->second;
		if (r.layout != s.layout || r.job < s.have || s.page < 0 || r.rgb.size() != size_t(s.w) * s.h * 3) {
			continue;  // moved or relit since
		}
		s.have = r.job;
		if (s.queued && s.queued->num == r.job) {
			s.queued.reset();
		}
		Page &p = pages_[size_t(s.page)];
		for (int j = 0; j < s.h; j++) {
			std::memcpy(&p.rgb[(size_t(s.y + j) * PAGE + s.x) * 3], &r.rgb[size_t(j) * s.w * 3], size_t(s.w) * 3);
		}
		upload(s);
		if (budget_ms >= 0 && OzE_Milliseconds() - t0 > budget_ms) {
			i++;
			break;
		}
	}
	if (i < done.size()) {
		// the rest next frame, ahead of what comes in meanwhile
		std::lock_guard<std::mutex> l(sink_->m);
		sink_->done.insert(sink_->done.begin(), std::make_move_iterator(done.begin() + long(i)), std::make_move_iterator(done.end()));
	}
	if (!sink_->outstanding.load() && batch_t0_) {
		last_ms_ = OzE_Milliseconds() - batch_t0_;
		batch_t0_ = 0;
	}
}

void LiveLight::wait() {
	for (int n = 0; n < 60000 && (sink_->outstanding.load() > 0 || !sink_->done.empty()); n++) {
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
		collect(-1);
	}
	collect(-1);
}

void LiveLight::frame(const Doc &doc, const SkyLight &sky, const Vec3 &eye, const Moved &moved) {
	const ozEdRender_t *r = OzEd_Renderer();
	if (!r || !r->CreateLightmap) {
		return;
	}
	eye_ = eye;
	if (generation_ != OzE_RendererGeneration()) {
		renderer_restarted();
	}
	const bool sky_changed = !sky_known_ || !sky.same(sky_);
	bool drag_changed = moved.size() != moved_.size();
	for (size_t i = 0; !drag_changed && i < moved.size(); i++) {
		drag_changed = moved[i].first != moved_[i].first || (moved[i].second - moved_[i].second).length() > 0.5;
	}
	if (doc.revision == doc_rev_ && !sky_changed) {
		if (drag_changed && scene_ && pending() == 0) {
			// a light dragged: lit where it is now, once the last of its light is in (as fast as the workers go)
			std::vector<uint64_t> dirty;
			std::unordered_set<Id> ids;
			for (const auto &mv : moved) {
				ids.insert(mv.first);
			}
			for (const auto &mv : moved_) {
				ids.insert(mv.first);
			}
			for (const PointL &l : scene_->points) {
				if (ids.count(l.owner)) {
					mark_light_reach(l.o, l.env, dirty);
				}
			}
			moved_ = moved;
			build_scene(doc, sky_, moved_);
			for (const PointL &l : scene_->points) {
				if (ids.count(l.owner)) {
					mark_light_reach(l.o, l.env, dirty);
				}
			}
			queue(dirty, eye);
			probes_due_ = true;
		}
		if (probes_due_) {
			queue_probes();
		}
		collect(4);
		return;
	}
	moved_ = moved;
	doc_rev_ = doc.revision;
	// what changed since the last look
	struct Change {
		Id id;
		ObjectPtr before, after;
		Bounds was;
	};
	std::vector<Change> changes;
	std::unordered_set<Id> here;
	for (Id id : doc.ids()) {
		here.insert(id);
		const ObjectPtr p = doc.ptr(id);
		auto it = seen_.find(id);
		if (it == seen_.end()) {
			changes.push_back({id, nullptr, p, Bounds()});
		} else if (it->second.obj != p) {
			changes.push_back({id, it->second.obj, p, it->second.b});
		}
	}
	for (const auto &kv : seen_) {
		if (!here.count(kv.first)) {
			changes.push_back({kv.first, kv.second.obj, nullptr, kv.second.b});
		}
	}
	const bool first = !scene_;
	bool rebuild = first || sky_changed;
	std::vector<uint64_t> dirty;
	std::vector<Bounds> shade;
	std::vector<Id> lights_changed;
	std::unordered_map<uint64_t, std::array<uint8_t, 3>> was_avg;  // a changed object's faces' texels, for its new ones meanwhile
	for (const Change &c : changes) {
		const Object *before = c.before.get(), *after = c.after.get();
		const Kind kind = after ? after->kind : before->kind;
		if (kind == Kind::Entity) {
			const bool light = (before && before->entity.classname() == "light") || (after && after->entity.classname() == "light");
			if (light) {
				rebuild = true;
				lights_changed.push_back(c.id);
			} else if (scene_) {
				// a spot's target moved: its light
				for (const Object *o : {before, after}) {
					const std::string tn = o ? o->entity.get("targetname") : "";
					for (const PointL &l : scene_->points) {
						if (!tn.empty() && l.target == tn) {
							rebuild = true;
							lights_changed.push_back(l.owner);
						}
					}
				}
			}
			probes_due_ = true;
		} else {
			rebuild = true;
			auto old = by_id_.find(c.id);
			if (old != by_id_.end()) {
				for (uint64_t k : old->second) {
					auto s = surfs_.find(k);
					if (s == surfs_.end() || s->second.page < 0) {
						continue;
					}
					// the face's average, kept for the same face of the new object meanwhile
					const Surf &sf = s->second;
					const Page &p = pages_[size_t(sf.page)];
					uint64_t sum[3] = {0, 0, 0};
					for (int j = 1; j + 1 < sf.h; j++) {
						for (int i = 1; i + 1 < sf.w; i++) {
							for (int q = 0; q < 3; q++) {
								sum[q] += p.rgb[(size_t(sf.y + j) * PAGE + sf.x + i) * 3 + q];
							}
						}
					}
					const uint64_t n = uint64_t(std::max(1, (sf.w - 2) * (sf.h - 2)));
					was_avg[k] = {uint8_t(sum[0] / n), uint8_t(sum[1] / n), uint8_t(sum[2] / n)};
				}
			}
			drop_object(c.id);
			Bounds b = c.was;
			if (after) {
				add_object(doc, c.id, *after);
				b.add(bounds_of(doc, c.id));
				for (uint64_t k : by_id_[c.id]) {
					dirty.push_back(k);
				}
			}
			shade.push_back(b);
			relaid_.push_back(c.id);
		}
		if (c.after) {
			seen_[c.id] = {c.after, bounds_of(doc, c.id)};
		} else {
			seen_.erase(c.id);
		}
	}
	// new faces show their old face's light, or a dim grey, until theirs is in
	for (Id id : relaid_) {
		auto it = by_id_.find(id);
		if (it == by_id_.end()) {
			continue;
		}
		for (uint64_t k : it->second) {
			auto w = was_avg.find(k);
			static const uint8_t grey[3] = {48, 48, 48};
			const uint8_t *c = w != was_avg.end() ? w->second.data() : grey;
			fill_placeholder(surfs_[k], c);
		}
	}
	// lights and glowing faces as they were, then as they are
	std::vector<std::pair<Vec3, double>> reaches;
	auto note_lights = [&](const Scene *sc) {
		if (!sc) {
			return;
		}
		std::unordered_set<Id> ids(lights_changed.begin(), lights_changed.end());
		for (const Change &c : changes) {
			ids.insert(c.id);
		}
		for (const PointL &l : sc->points) {
			if (ids.count(l.owner)) {
				reaches.emplace_back(l.o, l.env);
			}
		}
		for (const AreaL &l : sc->areas) {
			if (ids.count(l.owner)) {
				reaches.emplace_back(l.o, l.env);
			}
		}
	};
	if (rebuild) {
		const std::shared_ptr<const Scene> before = scene_;
		note_lights(before.get());
		build_scene(doc, sky, moved_);
		note_lights(scene_.get());
		probes_due_ = true;
	}
	if (first || sky_changed) {
		sky_ = sky;
		sky_known_ = true;
		for (const auto &kv : surfs_) {
			dirty.push_back(kv.first);
		}
	} else {
		for (const auto &rc : reaches) {
			mark_light_reach(rc.first, rc.second, dirty);
		}
		for (const Bounds &b : shade) {
			mark_shade(b, dirty);
		}
	}
	// the pages full of freed places: packed again
	int used = 0, freed = 0;
	for (const Page &p : pages_) {
		used += p.used;
		for (const Page::Free &f : p.free) {
			freed += f.w * f.h;
		}
	}
	if (pages_.size() > 1 && freed > used) {
		repack();
	}
	queue(dirty, eye);
	if (probes_due_) {
		queue_probes();
	}
	collect(4);
}

bool LiveLight::face_lm(Id brush, int face, const Vec3 &p, int &page, float &u, float &v) const {
	auto it = surfs_.find(key(brush, face));
	if (it == surfs_.end() || it->second.page < 0) {
		return false;
	}
	const Surf &s = it->second;
	const Page &pg = pages_[size_t(s.page)];
	if (pg.handle < 0) {
		return false;
	}
	const int a = s.axis == 0 ? 1 : 0, b = s.axis == 2 ? 1 : 2;
	auto c2 = [](const Vec3 &q, int k) { return k == 0 ? q.x : k == 1 ? q.y : q.z; };
	page = pg.handle;
	u = float((s.x + (c2(p, a) / s.S - s.s0) + 0.5) / PAGE);  // texel i's middle reads its grid point
	v = float((s.y + (c2(p, b) / s.S - s.t0) + 0.5) / PAGE);
	return true;
}

bool LiveLight::sample(Id brush, int face, const Vec3 &p, uint8_t rgb[3]) const {
	auto it = surfs_.find(key(brush, face));
	if (it == surfs_.end() || it->second.page < 0) {
		return false;
	}
	const Surf &s = it->second;
	const int a = s.axis == 0 ? 1 : 0, b = s.axis == 2 ? 1 : 2;
	auto c2 = [](const Vec3 &q, int k) { return k == 0 ? q.x : k == 1 ? q.y : q.z; };
	const int i = std::clamp(int(std::floor(c2(p, a) / s.S - s.s0 + 0.5)), 0, s.w - 1);
	const int j = std::clamp(int(std::floor(c2(p, b) / s.S - s.t0 + 0.5)), 0, s.h - 1);
	std::memcpy(rgb, &pages_[size_t(s.page)].rgb[(size_t(s.y + j) * PAGE + s.x + i) * 3], 3);
	return true;
}

bool LiveLight::patch_lm(Id patch, int vertex, int &page, float &u, float &v) const {
	auto it = surfs_.find(key(patch, -1));
	if (it == surfs_.end() || it->second.page < 0 || it->second.pw <= 0) {
		return false;
	}
	const Surf &s = it->second;
	const Page &pg = pages_[size_t(s.page)];
	if (pg.handle < 0) {
		return false;
	}
	const int c = vertex % s.pw, r = vertex / s.pw;
	page = pg.handle;
	u = float((s.x + 1 + c + 0.5) / PAGE);
	v = float((s.y + 1 + r + 0.5) / PAGE);
	return true;
}

}  // namespace oz::ed
