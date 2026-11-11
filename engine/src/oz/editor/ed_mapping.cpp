#include "ed_mapping.h"
#include "ed_ops.h"
#include "ed_texmap.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace oz::ed {

static const double PI = 3.14159265358979323846;

static double posmod(double x, double m) {
	double r = std::fmod(x, m);
	if (r < 0) {
		r += m;
	}
	return r > m - 1e-9 ? 0 : r;  // a whole repeat short of m is 0
}

// An angle in [0, 360), whole multiples of a thousandth kept tidy.
static double angle(double a) {
	a = std::fmod(a, 360);
	if (a < 0) {
		a += 360;
	}
	const double r = std::round(a * 1000) / 1000;
	a = std::fabs(r - a) < 1e-9 ? r : a;
	return a >= 360 ? a - 360 : a;
}

// v turned about the unit axis k by degrees (Rodrigues); quarter turns exact.
static Vec3 turned(const Vec3 &v, const Vec3 &k, double degrees) {
	double c, s;
	const double q = degrees / 90;
	if (std::fabs(q - std::round(q)) < 1e-12) {
		static const double cs[4][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
		const int i = int(((long long)std::llround(q) % 4 + 4) % 4);
		c = cs[i][0], s = cs[i][1];
	} else {
		c = std::cos(degrees * PI / 180), s = std::sin(degrees * PI / 180);
	}
	return v * c + k.cross(v) * s + k * (k.dot(v) * (1 - c));
}

// The face's texels at its corners: [u min, u max, v min, v max].
static void ranges(const Doc &doc, const Face &f, const Winding &w, double r[4]) {
	int tw, th;
	doc.tex_dims(f.shader, tw, th);
	r[0] = r[2] = 1e300;
	r[1] = r[3] = -1e300;
	for (const Vec3 &p : w) {
		const double u = (f.st.s.dot(p) + f.st.s0) * tw, v = (f.st.t.dot(p) + f.st.t0) * th;
		r[0] = std::min(r[0], u), r[1] = std::max(r[1], u);
		r[2] = std::min(r[2], v), r[3] = std::max(r[3], v);
	}
}

// u' = a[0] u + b[0], v' = a[1] v + b[1] in texels, in the face's own numbers.
static void tex_affine(const Doc &doc, Face &f, const Vec3 &origin, const Winding &w, const double a[2], const double b[2]) {
	if (f.form == TexForm::Quake) {
		// texels = p . axis / scale + shift
		for (int k = 0; k < 2; k++) {
			f.raw[3 + k] /= a[k];
			f.raw[k] = a[k] * f.raw[k] + b[k];
		}
		doc.derive_st(f, origin);
	} else if (f.form == TexForm::Valve) {
		// texels = p . axis / scale + offset
		for (int k = 0; k < 2; k++) {
			f.raw[9 + k] /= a[k];
			f.raw[3 + 4 * k] = a[k] * f.raw[3 + 4 * k] + b[k];
		}
		doc.derive_st(f, origin);
	} else {
		int tw, th;
		doc.tex_dims(f.shader, tw, th);
		f.st.s = f.st.s * a[0];
		f.st.s0 = a[0] * f.st.s0 + b[0] / tw;
		f.st.t = f.st.t * a[1];
		f.st.t0 = a[1] * f.st.t0 + b[1] / th;
		doc.derive_raw(f, TexForm::Primitives, origin, w);
	}
}

// The axis q3map's rotate turns a mapping about: T x S (for its default axes +Z, +X or -Y).
static Vec3 turn_axis(const Vec3 &s, const Vec3 &t, const Vec3 &normal) {
	Vec3 k = t.cross(s);
	const double l = k.length();
	if (l < 1e-12) {
		return normal;
	}
	return k * (1 / l);
}

bool tex_fields(const Doc &doc, const Face &f, const Winding &w, const Vec3 &origin, TexFields &out) {
	if (f.form == TexForm::Quake) {
		for (int i = 0; i < 5; i++) {
			out.v[i] = f.raw[i];
		}
		return true;
	}
	if (f.form == TexForm::Valve) {
		const double v[5] = {f.raw[3], f.raw[7], f.raw[8], f.raw[9], f.raw[10]};
		std::copy(v, v + 5, out.v);
		return true;
	}
	Face q = f;
	doc.derive_raw(q, TexForm::Quake, origin, w);
	if (q.form != TexForm::Quake) {
		return false;
	}
	for (int i = 0; i < 5; i++) {
		out.v[i] = q.raw[i];
	}
	return true;
}

// The face's mapping turned by degrees, in its own numbers.
static void tex_rotate(const Doc &doc, Face &f, const Vec3 &origin, const Winding &w, double degrees) {
	if (f.form == TexForm::Quake) {
		f.raw[2] = angle(f.raw[2] + degrees);
		doc.derive_st(f, origin);
	} else if (f.form == TexForm::Valve) {
		const Vec3 u(f.raw[0], f.raw[1], f.raw[2]), v(f.raw[4], f.raw[5], f.raw[6]);
		const Vec3 k = turn_axis(u, v, f.plane.n);
		const Vec3 u2 = turned(u, k, degrees), v2 = turned(v, k, degrees);
		f.raw[0] = u2.x, f.raw[1] = u2.y, f.raw[2] = u2.z;
		f.raw[4] = v2.x, f.raw[5] = v2.y, f.raw[6] = v2.z;
		f.raw[8] = angle(f.raw[8] + degrees);
		doc.derive_st(f, origin);
	} else {
		const Vec3 k = turn_axis(f.st.s, f.st.t, f.plane.n);
		f.st.s = turned(f.st.s, k, degrees);
		f.st.t = turned(f.st.t, k, degrees);
		doc.derive_raw(f, TexForm::Primitives, origin, w);
	}
}

bool remapped(const Doc &doc, Face &f, const Winding &w, const Vec3 &origin, const MapEdit &e) {
	int tw, th;
	doc.tex_dims(f.shader, tw, th);
	double a[2] = {1, 1}, b[2] = {0, 0};
	switch (e.op) {
	case MapOp::Shift:
		b[0] = e.a, b[1] = e.b;
		break;
	case MapOp::Rotate:
		tex_rotate(doc, f, origin, w, e.a);
		return true;
	case MapOp::Scale:
		if (e.a == 0 || e.b == 0) {
			return false;
		}
		// the scale numbers times the factor: texels per unit over it
		a[0] = 1 / e.a, a[1] = 1 / e.b;
		break;
	case MapOp::Fit: {
		if (w.size() < 3) {
			return false;
		}
		double r[4];
		ranges(doc, f, w, r);
		const double n[2] = {e.a, e.b}, size[2] = {double(tw), double(th)};
		for (int k = 0; k < 2; k++) {
			const double span = r[k * 2 + 1] - r[k * 2];
			if (n[k] > 0 && span > 1e-9) {
				a[k] = n[k] * size[k] / span;
				b[k] = -a[k] * r[k * 2];
			}
		}
		break;
	}
	case MapOp::Align: {
		if (w.size() < 3) {
			return false;
		}
		double r[4];
		ranges(doc, f, w, r);
		switch (e.which) {
		case 0: b[0] = -posmod(r[0], tw); break;
		case 1: b[0] = -posmod(r[1], tw); break;
		case 2: b[1] = -posmod(r[2], th); break;
		case 3: b[1] = -posmod(r[3], th); break;
		default:
			b[0] = -posmod((r[0] + r[1]) * 0.5 - tw * 0.5, tw);
			b[1] = -posmod((r[2] + r[3]) * 0.5 - th * 0.5, th);
			break;
		}
		break;
	}
	case MapOp::Flip: {
		if (w.size() < 3) {
			return false;
		}
		double r[4];
		ranges(doc, f, w, r);
		const int k = e.which ? 1 : 0;
		a[k] = -1;
		b[k] = r[k * 2] + r[k * 2 + 1];
		break;
	}
	case MapOp::Reset:
		world_mapping(doc, f, origin, w);
		return true;
	case MapOp::Natural: {
		// texels a unit along the face, each way (the parts of the axes along the normal draw nothing)
		const Vec3 &n = f.plane.n;
		const double ls = (f.st.s - n * f.st.s.dot(n)).length() * tw, lt = (f.st.t - n * f.st.t.dot(n)).length() * th;
		if (ls < 1e-12 || lt < 1e-12) {
			return false;
		}
		a[0] = 2 / ls, a[1] = 2 / lt;  // about texel 0, so the texture stays on the world where it was
		break;
	}
	case MapOp::Field: {
		const int i = e.which;
		if (i < 0 || i > 4 || (i >= 3 && e.a == 0)) {
			return false;
		}
		if (f.form == TexForm::Quake) {
			f.raw[i] = i == 2 ? angle(e.a) : e.a;
			doc.derive_st(f, origin);
		} else if (f.form == TexForm::Valve) {
			if (i == 2) {
				tex_rotate(doc, f, origin, w, e.a - f.raw[8]);
				f.raw[8] = angle(e.a);
			} else {
				static const int at[5] = {3, 7, 8, 9, 10};
				f.raw[at[i]] = e.a;
				doc.derive_st(f, origin);
			}
		} else {
			TexFields q;
			if (!tex_fields(doc, f, w, origin, q)) {
				return false;  // a mapping the Quake numbers can't show
			}
			q.v[i] = i == 2 ? angle(e.a) : e.a;
			Face g = f;
			g.form = TexForm::Quake;
			std::copy(q.v, q.v + 5, g.raw);
			doc.derive_st(g, origin);
			f.st = g.st;
			doc.derive_raw(f, TexForm::Primitives, origin, w);
		}
		return true;
	}
	}
	tex_affine(doc, f, origin, w, a, b);
	return true;
}

static bool same_face(const Face &a, const Face &b) {
	if (a.shader != b.shader || a.form != b.form) {
		return false;
	}
	for (int i = 0; i < 11; i++) {
		if (a.raw[i] != b.raw[i]) {
			return false;
		}
	}
	return true;
}

// Each brush's faces in refs, once.
static std::map<Id, std::vector<int>> by_brush(const Doc &doc, const std::vector<FaceRef> &faces) {
	std::map<Id, std::vector<int>> by;
	for (const FaceRef &r : faces) {
		const Object *o = doc.get(r.first);
		if (o && o->kind == Kind::Brush && r.second >= 0 && r.second < int(o->brush.faces.size())) {
			std::vector<int> &v = by[r.first];
			if (std::find(v.begin(), v.end(), r.second) == v.end()) {
				v.push_back(r.second);
			}
		}
	}
	return by;
}

// One step doing fn to each face in faces; fn returns false to leave a face as it was.
static bool each_face(Doc &doc, const std::vector<FaceRef> &faces, const std::string &label,
		const std::function<bool(Face &, const Winding &, const Vec3 &)> &fn) {
	const auto by = by_brush(doc, faces);
	bool any = false;
	doc.begin(label);
	for (const auto &kv : by) {
		const Object *o = doc.get(kv.first);
		const std::vector<Winding> &ws = doc.windings(kv.first);
		const Vec3 origin = origin_for(doc, *o);
		std::shared_ptr<Object> nb;
		for (int i : kv.second) {
			Face g = o->brush.faces[size_t(i)];
			if (!fn(g, size_t(i) < ws.size() ? ws[size_t(i)] : Winding{}, origin) || same_face(g, o->brush.faces[size_t(i)])) {
				continue;
			}
			if (!nb) {
				nb = std::make_shared<Object>(*o);
			}
			nb->brush.faces[size_t(i)] = g;
		}
		if (nb) {
			doc.put(kv.first, nb);
			any = true;
		}
	}
	doc.end();
	return any;
}

bool map_faces(Doc &doc, const std::vector<FaceRef> &faces, const MapEdit &e, const std::string &label) {
	return each_face(doc, faces, label, [&](Face &f, const Winding &w, const Vec3 &origin) { return remapped(doc, f, w, origin, e); });
}

bool map_patches(Doc &doc, const std::vector<Id> &patches, const MapEdit &e, const std::string &label) {
	bool any = false;
	doc.begin(label);
	for (Id id : patches) {
		const Object *o = doc.get(id);
		if (!o || o->kind != Kind::Patch || o->patch.ctrl.empty()) {
			continue;
		}
		auto n = std::make_shared<Object>(*o);
		Patch &p = n->patch;
		int tw = 64, th = 64;
		doc.tex_dims(p.shader, tw, th);
		const size_t count = p.ctrl.size() / 5;
		double ms = 0, mt = 0;
		for (size_t i = 0; i < count; i++) {
			ms += p.ctrl[i * 5 + 3], mt += p.ctrl[i * 5 + 4];
		}
		ms /= double(count), mt /= double(count);
		const double r = e.a * 3.14159265358979323846 / 180, cs = std::cos(r), sn = std::sin(r);
		bool done = true;
		for (size_t i = 0; i < count; i++) {
			double &s = p.ctrl[i * 5 + 3], &t = p.ctrl[i * 5 + 4];
			const int row = int(i) / std::max(1, p.width), col = int(i) % std::max(1, p.width);
			switch (e.op) {
			case MapOp::Shift: s += e.a / tw, t += e.b / th; break;
			case MapOp::Rotate: {
				const double ds = s - ms, dt = t - mt;
				s = ms + ds * cs - dt * sn, t = mt + ds * sn + dt * cs;
				break;
			}
			case MapOp::Scale:
				if (e.a == 0 || e.b == 0) {
					done = false;
				} else {
					s = ms + (s - ms) / e.a, t = mt + (t - mt) / e.b;
				}
				break;
			case MapOp::Fit:
				if (e.a != 0) {
					s = p.width > 1 ? double(col) / (p.width - 1) * e.a : 0;
				}
				if (e.b != 0) {
					t = p.height > 1 ? double(row) / (p.height - 1) * e.b : 0;
				}
				break;
			case MapOp::Flip: (e.which ? t : s) = 2 * (e.which ? mt : ms) - (e.which ? t : s); break;
			default: done = e.op == MapOp::Reset || e.op == MapOp::Natural; break;
			}
		}
		if (e.op == MapOp::Reset || e.op == MapOp::Natural) {  // a curve's own size is its natural texture's
			natural_texture(doc, p);
		}
		if (done) {
			doc.put(id, n);
			any = true;
		}
	}
	doc.end();
	return any;
}

void painted(const Doc &doc, Face &f, const Winding &w, const Vec3 &origin, ShaderId shader) {
	int nw, nh, ow, oh;
	doc.tex_dims(shader, nw, nh);
	doc.tex_dims(f.shader, ow, oh);
	f.shader = shader;
	if (world_grid(f)) {
		world_mapping(doc, f, origin, w);  // a dev texture lines up with the world's grid
	} else if (f.form == TexForm::Primitives || !f.raw_valid) {
		// counted in repeats: the old texture's texels kept in the world, in the new one's repeats
		const double ks = double(ow) / nw, kt = double(oh) / nh;
		f.st.s = f.st.s * ks, f.st.s0 *= ks;
		f.st.t = f.st.t * kt, f.st.t0 *= kt;
		doc.derive_raw(f, f.raw_valid ? f.form : doc.plain_form, origin, w);
	} else {
		doc.derive_st(f, origin);  // Quake and Valve 220 count in texels already
	}
}

bool paint(Doc &doc, const std::vector<FaceRef> &faces, const std::vector<Id> &patches, ShaderId shader, const std::string &label) {
	int nw, nh;
	doc.tex_dims(shader, nw, nh);
	doc.begin(label);
	bool any = each_face(doc, faces, label, [&](Face &f, const Winding &w, const Vec3 &origin) {
		painted(doc, f, w, origin, shader);
		return true;
	});
	for (Id id : patches) {
		const Object *o = doc.get(id);
		if (o && o->kind == Kind::Patch && o->patch.shader != shader) {
			auto np = std::make_shared<Object>(*o);
			int ow, oh;
			doc.tex_dims(np->patch.shader, ow, oh);
			np->patch.shader = shader;
			// a curve's coordinates are repeats: the texels keep their size
			for (size_t i = 0; i + 4 < np->patch.ctrl.size(); i += 5) {
				np->patch.ctrl[i + 3] *= double(ow) / nw;
				np->patch.ctrl[i + 4] *= double(oh) / nh;
			}
			doc.put(id, np);
			any = true;
		}
	}
	doc.end();
	return any;
}

static Vec3 centroid(const Winding &w) {
	Vec3 c;
	for (const Vec3 &q : w) {
		c = c + q;
	}
	return w.empty() ? c : c * (1.0 / double(w.size()));
}

// How far along the edges of wa a stretch lies on wb (on wb's plane and inside its polygon): the
// longest, its middle and its direction. An edge of one polygon running across the other's middle
// counts too: a chamfer's wedge set against a wall whose face runs on behind it.
static void edge_on(const Winding &wa, const Winding &wb, const Plane &pb, double &best, Vec3 &p, Vec3 &d) {
	const Vec3 cb = centroid(wb);
	for (size_t i = 0; i < wa.size(); i++) {
		const Vec3 &a0 = wa[i], &a1 = wa[(i + 1) % wa.size()];
		const double la = (a1 - a0).length();
		if (la < 0.5 || std::fabs(pb.n.dot(a0) - pb.d) > 0.05 || std::fabs(pb.n.dot(a1) - pb.d) > 0.05) {
			continue;
		}
		const Vec3 da = (a1 - a0) * (1 / la);
		double lo = 0, hi = la;
		for (size_t j = 0; j < wb.size() && hi - lo > 0; j++) {
			const Vec3 &e0 = wb[j], &e1 = wb[(j + 1) % wb.size()];
			Vec3 m = pb.n.cross(e1 - e0);
			const double ml = m.length();
			if (ml < 1e-9) {
				continue;
			}
			m = m * (1 / ml);
			if (m.dot(cb - e0) < 0) {
				m = m * -1;  // toward the polygon's inside
			}
			// m . (a0 + t da - e0) >= -0.05
			const double c0 = m.dot(a0 - e0) + 0.05, c1 = m.dot(da);
			if (std::fabs(c1) < 1e-12) {
				if (c0 < 0) {
					hi = lo;
				}
			} else if (c1 > 0) {
				lo = std::max(lo, -c0 / c1);
			} else {
				hi = std::min(hi, -c0 / c1);
			}
		}
		if (hi - lo > best) {
			best = hi - lo;
			p = a0 + da * ((lo + hi) * 0.5);
			d = da;
		}
	}
}

// The line two faces' polygons meet along (more than half a unit of it): a point on it and its direction.
static bool shared_line(const Face &a, const Winding &wa, const Face &b, const Winding &wb, Vec3 &p, Vec3 &d) {
	double best = 0.5;
	const double start = best;
	edge_on(wa, wb, b.plane, best, p, d);
	edge_on(wb, wa, a.plane, best, p, d);
	return best > start;
}

// Across the line (p, d) in a face's plane n: the way into the part of its polygon w a player sees.
// A polygon on both sides of the line (a face running on behind the other face) shows the part in
// front of the other face's plane.
static Vec3 into(const Winding &w, const Vec3 &n, const Vec3 &p, const Vec3 &d, const Vec3 &other_n) {
	Vec3 u = n.cross(d);
	double lo = 0, hi = 0;
	for (const Vec3 &q : w) {
		const double x = u.dot(q - p);
		lo = std::min(lo, x), hi = std::max(hi, x);
	}
	if (hi > 0.05 && lo < -0.05) {
		return other_n.dot(u) >= 0 ? u : u * -1;
	}
	return hi >= -lo ? u : u * -1;
}

bool unfold_st(const Face &a, const Winding &wa, int tw_a, int th_a, const Face &b, const Winding &wb, int tw_b, int th_b, StAxes &out) {
	Vec3 p, d;
	const Vec3 &na = a.plane.n, &nb = b.plane.n;
	if (wa.size() < 3 || wb.size() < 3 || na.dot(nb) < -0.999 || !shared_line(a, wa, b, wb, p, d)) {
		return false;
	}
	// across the line in each plane: ua leaving a's polygon, ub going into b's
	const Vec3 ua = into(wa, na, p, d, nb) * -1, ub = into(wb, nb, p, d, na);
	const Vec3 ka = d.cross(ua), kb = d.cross(ub);
	// the turn about the line laying a's plane onto b's: d stays, ua goes to ub
	auto turn = [&](const Vec3 &x) { return d * d.dot(x) + ub * ua.dot(x) + kb * ka.dot(x); };
	auto carry = [&](const Vec3 &axis, double base, double k, Vec3 &axis_out, double &base_out) {
		const Vec3 in = axis - na * axis.dot(na);  // the same on a's plane: its part along the normal goes to the base
		const double in0 = base + axis.dot(na) * a.plane.d;
		const Vec3 r = turn(in);
		axis_out = r * k;
		base_out = (in0 + in.dot(p) - r.dot(p)) * k;  // the same texel on the line on both faces
	};
	carry(a.st.s, a.st.s0, double(tw_a) / tw_b, out.s, out.s0);
	carry(a.st.t, a.st.t0, double(th_a) / th_b, out.t, out.t0);
	return true;
}

bool wrap_faces(Doc &doc, const std::vector<FaceRef> &faces, const std::string &label) {
	struct Item {
		FaceRef ref;
		Face face;
		Winding w;
		bool reached = false;
	};
	std::vector<Item> items;
	for (const FaceRef &r : faces) {
		const Object *o = doc.get(r.first);
		if (!o || o->kind != Kind::Brush || r.second < 0 || r.second >= int(o->brush.faces.size())) {
			continue;
		}
		const std::vector<Winding> &ws = doc.windings(r.first);
		const Face &f = o->brush.faces[size_t(r.second)];
		if (size_t(r.second) >= ws.size() || ws[size_t(r.second)].size() < 3 || world_grid(f) ||
				std::any_of(items.begin(), items.end(), [&](const Item &it) { return it.ref == r; })) {
			continue;
		}
		items.push_back({r, f, ws[size_t(r.second)]});
	}
	if (items.size() < 2) {
		return false;
	}
	// face to face through the edges they share, the first face's neighbors first
	items[0].reached = true;
	std::vector<size_t> order = {0};
	for (size_t at = 0; at < order.size(); at++) {
		const Item a = items[order[at]];
		int aw, ah;
		doc.tex_dims(a.face.shader, aw, ah);
		for (size_t j = 0; j < items.size(); j++) {
			Item &b = items[j];
			if (b.reached) {
				continue;
			}
			int bw, bh;
			doc.tex_dims(b.face.shader, bw, bh);
			StAxes st;
			if (unfold_st(a.face, a.w, aw, ah, b.face, b.w, bw, bh, st)) {
				b.face.st = st;
				b.reached = true;
				order.push_back(j);
			}
		}
	}
	// each reached face's numbers made again from its new coordinates, a brush at a time
	std::map<Id, std::vector<size_t>> by;
	for (size_t k = 1; k < order.size(); k++) {
		by[items[order[k]].ref.first].push_back(order[k]);
	}
	bool any = false;
	doc.begin(label);
	for (const auto &kv : by) {
		const Object *o = doc.get(kv.first);
		const Vec3 origin = origin_for(doc, *o);
		auto nb = std::make_shared<Object>(*o);
		bool changed = false;
		for (size_t k : kv.second) {
			Face &f = nb->brush.faces[size_t(items[k].ref.second)];
			Face g = f;
			g.st = items[k].face.st;
			doc.derive_raw(g, g.raw_valid ? g.form : doc.plain_form, origin, items[k].w);
			if (!same_face(g, f)) {
				f = g;
				changed = true;
			}
		}
		if (changed) {
			doc.put(kv.first, nb);
			any = true;
		}
	}
	doc.end();
	return any;
}

bool copy_mapping(Doc &doc, const std::vector<FaceRef> &faces, const Face &src, const Vec3 &src_origin, const std::string &label,
		const Winding *src_w) {
	int sw, sh;
	doc.tex_dims(src.shader, sw, sh);
	return each_face(doc, faces, label, [&](Face &f, const Winding &w, const Vec3 &origin) {
		Face g = src;
		g.plane = f.plane;
		std::copy(f.pts, f.pts + 3, g.pts);
		g.pts_valid = f.pts_valid;
		g.lock = f.lock;
		g.has_flags = f.has_flags;
		g.contents = f.contents, g.flags = f.flags, g.value = f.value;
		const bool same_frame = origin.x == src_origin.x && origin.y == src_origin.y && origin.z == src_origin.z;
		const double along = src.plane.n.dot(f.plane.n);
		StAxes wrapped;
		if (along > 1 - 1e-6) {
			if (same_frame && along >= 1 - 1e-15) {
				doc.derive_st(g, origin);  // the same numbers give the same texture coordinates
			} else {
				g.st = src.st;  // the texture runs on across
				doc.derive_raw(g, src.form, origin, w);
			}
		} else if (src_w && !world_grid(g) && unfold_st(src, *src_w, sw, sh, g, w, sw, sh, wrapped)) {
			// a face beside it: the texture wrapped round the edge they share
			g.st = wrapped;
			doc.derive_raw(g, src.raw_valid ? src.form : doc.plain_form, origin, w);
		} else {
			// its numbers on this plane, from where the source is mapped
			fix_valve_axes(g);
			doc.derive_st(g, src_origin);
			if (!same_frame) {
				doc.derive_raw(g, g.form, origin, w);
			}
		}
		if (world_grid(g)) {
			world_mapping(doc, g, origin, w);  // a dev texture lines up with the world's grid
		}
		f = g;
		return true;
	});
}

}  // namespace oz::ed
