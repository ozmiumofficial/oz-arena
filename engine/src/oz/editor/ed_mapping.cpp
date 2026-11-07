#include "ed_mapping.h"
#include "ed_ops.h"
#include "ed_texmap.h"

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
			default: done = e.op == MapOp::Reset; break;
			}
		}
		if (e.op == MapOp::Reset) {
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

bool paint(Doc &doc, const std::vector<FaceRef> &faces, const std::vector<Id> &patches, ShaderId shader, const std::string &label) {
	doc.begin(label);
	bool any = each_face(doc, faces, label, [&](Face &f, const Winding &w, const Vec3 &origin) {
		f.shader = shader;
		if (world_grid(f)) {
			world_mapping(doc, f, origin, w);  // a dev texture lines up with the world's grid
		} else {
			doc.derive_st(f, origin);
		}
		return true;
	});
	for (Id id : patches) {
		const Object *o = doc.get(id);
		if (o && o->kind == Kind::Patch && o->patch.shader != shader) {
			auto np = std::make_shared<Object>(*o);
			np->patch.shader = shader;
			doc.put(id, np);
			any = true;
		}
	}
	doc.end();
	return any;
}

bool copy_mapping(Doc &doc, const std::vector<FaceRef> &faces, const Face &src, const Vec3 &src_origin, const std::string &label) {
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
		if (along > 1 - 1e-6) {
			if (same_frame && along >= 1 - 1e-15) {
				doc.derive_st(g, origin);  // the same numbers give the same texture coordinates
			} else {
				g.st = src.st;  // the texture runs on across
				doc.derive_raw(g, src.form, origin, w);
			}
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
