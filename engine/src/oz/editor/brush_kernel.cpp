#include "brush_kernel.h"

#include <algorithm>
#include <cmath>

namespace q3map {

double Vec3::length() const {
	return std::sqrt(dot(*this));
}

Vec3 Vec3::normalized() const {
	double l = length();
	return l > 0 ? *this * (1.0 / l) : Vec3();
}

// Near-integers become integers; + 0.0 turns -0 into 0 (-mins.x of a box at the origin).
static double snap(double v, double tol) {
	double r = std::round(v);
	return (std::fabs(v - r) < tol ? r : v) + 0.0;
}

static Vec3 snap_point(const Vec3 &p) {
	return {snap(p.x, 1e-7), snap(p.y, 1e-7), snap(p.z, 1e-7)};
}

Plane clean_plane(Plane p) {
	double l = p.n.length();
	if (l <= 0) {
		return p;
	}
	p.n = p.n * (1.0 / l);
	p.d /= l;
	double *c[3] = {&p.n.x, &p.n.y, &p.n.z};
	for (int i = 0; i < 3; i++) {
		*c[i] += 0.0;
		if (std::fabs(*c[i]) < 1e-12) {
			*c[i] = 0;
		} else if (std::fabs(*c[i] - 1) < 1e-12) {
			*c[i] = 1;
		} else if (std::fabs(*c[i] + 1) < 1e-12) {
			*c[i] = -1;
		}
	}
	p.d = snap(p.d, 1e-9);
	return p;
}

bool same_plane(const Plane &a, const Plane &b) {
	return a.n.dot(b.n) > 1 - 1e-10 && std::fabs(a.d - b.d) < EPSILON;
}

// Which axis a plane is aligned to (0..2), or -1. Used to put clipped points exactly on it.
static int axial(const Plane &p) {
	if (std::fabs(p.n.x) == 1) {
		return 0;
	}
	if (std::fabs(p.n.y) == 1) {
		return 1;
	}
	if (std::fabs(p.n.z) == 1) {
		return 2;
	}
	return -1;
}

Winding base_winding(const Plane &p) {
	// q3map's BaseWindingForPlane
	Vec3 up = std::fabs(p.n.z) >= std::fabs(p.n.x) && std::fabs(p.n.z) >= std::fabs(p.n.y) ? Vec3(1, 0, 0) : Vec3(0, 0, 1);
	up = (up - p.n * up.dot(p.n)).normalized();
	Vec3 right = up.cross(p.n);
	Vec3 org = p.n * p.d;
	up = up * BASE_SIZE;
	right = right * BASE_SIZE;
	Winding w = {org - right + up, org + right + up, org + right - up, org - right - up};
	if ((w[1] - w[0]).cross(w[2] - w[0]).dot(p.n) < 0) {
		std::reverse(w.begin(), w.end());
	}
	return w;
}

void clip_back(Winding &w, const Plane &plane) {
	enum { FRONT, BACK, ON };
	size_t n = w.size();
	std::vector<double> dists(n);
	std::vector<int> sides(n);
	bool front = false, back = false;
	for (size_t i = 0; i < n; i++) {
		dists[i] = plane.dist(w[i]);
		sides[i] = dists[i] > EPSILON ? FRONT : (dists[i] < -EPSILON ? BACK : ON);
		front |= sides[i] == FRONT;
		back |= sides[i] == BACK;
	}
	if (!front) {
		return;
	}
	if (!back) {
		w.clear();
		return;
	}
	int ax = axial(plane);
	Winding out;
	out.reserve(n + 4);
	for (size_t i = 0; i < n; i++) {
		size_t j = (i + 1) % n;
		if (sides[i] == ON) {
			out.push_back(w[i]);
			continue;
		}
		if (sides[i] == BACK) {
			out.push_back(w[i]);
		}
		if (sides[j] == ON || sides[j] == sides[i]) {
			continue;
		}
		double t = dists[i] / (dists[i] - dists[j]);
		Vec3 mid = w[i] + (w[j] - w[i]) * t;
		if (ax >= 0) {
			double v = plane.d * (ax == 0 ? plane.n.x : ax == 1 ? plane.n.y : plane.n.z);
			(ax == 0 ? mid.x : ax == 1 ? mid.y : mid.z) = v;
		}
		out.push_back(snap_point(mid));
	}
	w.swap(out);
}

double winding_area(const Winding &w) {
	Vec3 sum;
	for (size_t i = 2; i < w.size(); i++) {
		sum = sum + (w[i - 1] - w[0]).cross(w[i] - w[0]);
	}
	return sum.length() * 0.5;
}

Vec3 winding_center(const Winding &w) {
	Vec3 c;
	for (const Vec3 &p : w) {
		c = c + p;
	}
	return w.empty() ? c : c * (1.0 / w.size());
}

std::vector<Winding> brush_windings(const std::vector<Plane> &planes) {
	std::vector<Winding> out(planes.size());
	for (size_t i = 0; i < planes.size(); i++) {
		bool dup = false;
		for (size_t j = 0; j < i && !dup; j++) {
			dup = same_plane(planes[i], planes[j]);
		}
		if (dup || planes[i].n.length() < 0.5) {
			continue;
		}
		Winding w = base_winding(planes[i]);
		for (size_t j = 0; j < planes.size() && !w.empty(); j++) {
			if (j != i && !same_plane(planes[i], planes[j])) {
				clip_back(w, planes[j]);
			}
		}
		if (w.size() >= 3 && winding_area(w) > EPSILON) {
			out[i] = std::move(w);
		}
	}
	return out;
}

bool brush_valid(const std::vector<Plane> &planes) {
	std::vector<Winding> ws = brush_windings(planes);
	int faces = 0;
	double volume = 0;
	for (size_t i = 0; i < ws.size(); i++) {
		if (ws[i].empty()) {
			continue;
		}
		for (const Vec3 &p : ws[i]) {
			if (std::fabs(p.x) > BASE_SIZE * 0.5 || std::fabs(p.y) > BASE_SIZE * 0.5 || std::fabs(p.z) > BASE_SIZE * 0.5) {
				return false; // open: some side reaches the base winding
			}
		}
		faces++;
		volume += winding_area(ws[i]) * planes[i].d / 3.0; // divergence theorem, n . p = d on the face
	}
	return faces >= 4 && volume > EPSILON;
}

std::vector<Vec3> brush_vertices(const std::vector<Plane> &planes) {
	std::vector<Vec3> out;
	for (const Winding &w : brush_windings(planes)) {
		for (const Vec3 &p : w) {
			bool seen = false;
			for (const Vec3 &q : out) {
				if ((p - q).dot(p - q) < 1e-10) {
					seen = true;
					break;
				}
			}
			if (!seen) {
				out.push_back(p);
			}
		}
	}
	return out;
}

std::vector<int> brush_face_planes(const std::vector<Plane> &planes) {
	std::vector<int> keep;
	std::vector<Winding> ws = brush_windings(planes);
	for (size_t i = 0; i < ws.size(); i++) {
		if (!ws[i].empty()) {
			keep.push_back(int(i));
		}
	}
	return keep;
}

std::vector<Plane> box_planes(const Vec3 &mins, const Vec3 &maxs) {
	return {
		{{1, 0, 0}, maxs.x}, {{-1, 0, 0}, -mins.x},
		{{0, 1, 0}, maxs.y}, {{0, -1, 0}, -mins.y},
		{{0, 0, 1}, maxs.z}, {{0, 0, -1}, -mins.z},
	};
}

std::vector<Plane> hull_planes(const std::vector<Vec3> &pts) {
	std::vector<Plane> out;
	size_t n = pts.size();
	for (size_t i = 0; i < n; i++) {
		for (size_t j = i + 1; j < n; j++) {
			for (size_t k = j + 1; k < n; k++) {
				Vec3 nrm = (pts[j] - pts[i]).cross(pts[k] - pts[i]);
				if (nrm.length() < 1e-9) {
					continue;
				}
				Plane p = clean_plane({nrm, nrm.dot(pts[i])});
				p.d = p.n.dot(pts[i]);
				bool front = false, back = false;
				for (size_t m = 0; m < n && !(front && back); m++) {
					double d = p.dist(pts[m]);
					front |= d > EPSILON;
					back |= d < -EPSILON;
				}
				if (front && back) {
					continue;
				}
				if (front) {
					p = p.flipped();
				}
				p = clean_plane(p);
				bool dup = false;
				for (const Plane &q : out) {
					dup |= same_plane(p, q);
				}
				if (!dup) {
					out.push_back(p);
				}
			}
		}
	}
	if (!brush_valid(out)) {
		out.clear();
	}
	return out;
}

Vec3 Affine::apply(const Vec3 &p) const {
	return apply_dir(p) + t;
}

Vec3 Affine::apply_dir(const Vec3 &v) const {
	return {m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z,
			m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
			m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z};
}

bool Affine::inverse_transpose(double out[3][3]) const {
	// the inverse transpose is the cofactor matrix over the determinant
	double c[3][3];
	for (int r = 0; r < 3; r++) {
		for (int col = 0; col < 3; col++) {
			int r1 = (r + 1) % 3, r2 = (r + 2) % 3, c1 = (col + 1) % 3, c2 = (col + 2) % 3;
			c[r][col] = m[r1][c1] * m[r2][c2] - m[r1][c2] * m[r2][c1];
		}
	}
	double det = m[0][0] * c[0][0] + m[0][1] * c[0][1] + m[0][2] * c[0][2];
	if (std::fabs(det) < 1e-12) {
		return false;
	}
	for (int r = 0; r < 3; r++) {
		for (int col = 0; col < 3; col++) {
			out[r][col] = c[r][col] / det;
		}
	}
	return true;
}

std::vector<Plane> transform_planes(const std::vector<Plane> &planes, const Affine &xf) {
	double it[3][3];
	if (!xf.inverse_transpose(it)) {
		return {};
	}
	std::vector<Plane> out;
	out.reserve(planes.size());
	for (const Plane &p : planes) {
		Vec3 on = xf.apply(p.n * p.d);
		Vec3 n = {it[0][0] * p.n.x + it[0][1] * p.n.y + it[0][2] * p.n.z,
				it[1][0] * p.n.x + it[1][1] * p.n.y + it[1][2] * p.n.z,
				it[2][0] * p.n.x + it[2][1] * p.n.y + it[2][2] * p.n.z};
		n = n.normalized();
		out.push_back(clean_plane({n, n.dot(on)}));
	}
	return out;
}

static void prune(std::vector<Plane> &planes, std::vector<int> &src) {
	std::vector<int> keep = brush_face_planes(planes);
	std::vector<Plane> p;
	std::vector<int> s;
	for (int i : keep) {
		p.push_back(planes[i]);
		s.push_back(src[i]);
	}
	planes.swap(p);
	src.swap(s);
}

Split split_brush(const std::vector<Plane> &planes, const Plane &by) {
	Split r;
	std::vector<int> idx(planes.size());
	for (size_t i = 0; i < planes.size(); i++) {
		idx[i] = int(i);
	}
	double lo = 0, hi = 0;
	bool first = true;
	for (const Vec3 &p : brush_vertices(planes)) {
		double d = by.dist(p);
		lo = first ? d : std::min(lo, d);
		hi = first ? d : std::max(hi, d);
		first = false;
	}
	if (lo >= -EPSILON) {
		r.front = planes;
		r.front_src = idx;
		return r;
	}
	if (hi <= EPSILON) {
		r.back = planes;
		r.back_src = idx;
		return r;
	}
	r.front = planes;
	r.front.push_back(clean_plane(by.flipped()));
	r.front_src = idx;
	r.front_src.push_back(-1);
	r.back = planes;
	r.back.push_back(clean_plane(by));
	r.back_src = idx;
	r.back_src.push_back(-1);
	prune(r.front, r.front_src);
	prune(r.back, r.back_src);
	if (!brush_valid(r.front)) {
		r.front.clear();
		r.front_src.clear();
	}
	if (!brush_valid(r.back)) {
		r.back.clear();
		r.back_src.clear();
	}
	return r;
}

SubtractResult subtract(const std::vector<Plane> &target, const std::vector<Plane> &cutter) {
	SubtractResult r;
	std::vector<Plane> piece = target;
	std::vector<int> src(target.size());
	for (size_t i = 0; i < target.size(); i++) {
		src[i] = int(i);
	}
	std::vector<Piece> outside;
	for (size_t k = 0; k < cutter.size(); k++) {
		Split s = split_brush(piece, cutter[k]);
		if (s.back.empty()) {
			return r; // the rest of the target is all in front of this cutter plane: no overlap
		}
		if (!s.front.empty()) {
			Piece p;
			p.planes = s.front;
			for (int i : s.front_src) {
				p.source.push_back(i < 0 ? -int(k) - 1 : src[i]);
			}
			outside.push_back(std::move(p));
		}
		std::vector<int> next;
		for (int i : s.back_src) {
			next.push_back(i < 0 ? -int(k) - 1 : src[i]);
		}
		piece = s.back;
		src = next;
	}
	for (Piece &p : outside) {
		for (size_t i = 0; i < p.planes.size(); i++) {
			int a = p.source[i];
			if (a < 0) {
				double best = -2;
				for (size_t t = 0; t < target.size(); t++) {
					double d = target[t].n.dot(p.planes[i].n);
					if (d > best) {
						best = d;
						a = int(t);
					}
				}
			}
			p.attr.push_back(a);
		}
	}
	r.touched = true;
	r.pieces = std::move(outside);
	return r;
}

bool ray_brush(const std::vector<Plane> &planes, const Vec3 &o, const Vec3 &dir, double &t, int &face) {
	double enter = -1e300, leave = 1e300;
	face = -1;
	for (size_t i = 0; i < planes.size(); i++) {
		double denom = planes[i].n.dot(dir);
		double dist = planes[i].dist(o);
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
	}
	if (face < 0 || enter > leave || enter < 0) {
		return false;
	}
	t = enter;
	return true;
}

void default_texture_axes(const Vec3 &n, Vec3 &u, Vec3 &v) {
	static const Vec3 base[18] = {
		{0, 0, 1}, {1, 0, 0}, {0, -1, 0},   // floor
		{0, 0, -1}, {1, 0, 0}, {0, -1, 0},  // ceiling
		{1, 0, 0}, {0, 1, 0}, {0, 0, -1},   // west wall
		{-1, 0, 0}, {0, 1, 0}, {0, 0, -1},  // east wall
		{0, 1, 0}, {1, 0, 0}, {0, 0, -1},   // south wall
		{0, -1, 0}, {1, 0, 0}, {0, 0, -1},  // north wall
	};
	int best_axis = 0;
	double best = 0;
	for (int i = 0; i < 6; i++) {
		double d = n.dot(base[i * 3]);
		if (d > best + 0.0001) {
			best = d;
			best_axis = i;
		}
	}
	u = base[best_axis * 3 + 1];
	v = base[best_axis * 3 + 2];
}

void face_tangents(const Vec3 &n, const Vec3 &u, const Vec3 &v, Vec3 &tangent, Vec3 &bitangent) {
	// T and B lie in the face with u.T = 1, v.T = 0, u.B = 0, v.B = 1
	Vec3 vn = v.cross(n), nu = n.cross(u);
	double du = u.dot(vn), dv = v.dot(nu);
	tangent = std::fabs(du) > 1e-12 ? vn * (1.0 / du) : Vec3();
	bitangent = std::fabs(dv) > 1e-12 ? nu * (1.0 / dv) : Vec3();
}

static int span_steps(const Vec3 &a, const Vec3 &b, const Vec3 &c, double tolerance, int max_steps) {
	// a quadratic's chord error with n segments is |a - 2b + c| / (4 n^2)
	double bend = (a - b * 2 + c).length();
	int n = int(std::ceil(std::sqrt(bend / (4 * std::max(tolerance, 1e-3)))));
	return std::clamp(n, 1, max_steps);
}

static void bezier(double t, double b[3], double db[3]) {
	b[0] = (1 - t) * (1 - t);
	b[1] = 2 * t * (1 - t);
	b[2] = t * t;
	db[0] = -2 * (1 - t);
	db[1] = 2 - 4 * t;
	db[2] = 2 * t;
}

PatchMesh tessellate_patch(const std::vector<PatchVertex> &ctrl, int w, int h, double tolerance, int max_steps) {
	PatchMesh m;
	if (w < 3 || h < 3 || (w & 1) == 0 || (h & 1) == 0 || int(ctrl.size()) < w * h) {
		return m;
	}
	auto P = [&](int i, int j) -> const PatchVertex & { return ctrl[j * w + i]; };
	int su = (w - 1) / 2, sv = (h - 1) / 2;
	std::vector<int> nu(su, 1), nv(sv, 1);
	for (int s = 0; s < su; s++) {
		for (int j = 0; j < h; j++) {
			nu[s] = std::max(nu[s], span_steps(P(2 * s, j).p, P(2 * s + 1, j).p, P(2 * s + 2, j).p, tolerance, max_steps));
		}
	}
	for (int s = 0; s < sv; s++) {
		for (int i = 0; i < w; i++) {
			nv[s] = std::max(nv[s], span_steps(P(i, 2 * s).p, P(i, 2 * s + 1).p, P(i, 2 * s + 2).p, tolerance, max_steps));
		}
	}
	// parameter list per direction: (span, t)
	std::vector<std::pair<int, double>> us, vs;
	for (int s = 0; s < su; s++) {
		for (int k = (s == 0 ? 0 : 1); k <= nu[s]; k++) {
			us.push_back({s, double(k) / nu[s]});
		}
	}
	for (int s = 0; s < sv; s++) {
		for (int k = (s == 0 ? 0 : 1); k <= nv[s]; k++) {
			vs.push_back({s, double(k) / nv[s]});
		}
	}
	m.width = int(us.size());
	m.height = int(vs.size());
	m.verts.resize(size_t(m.width) * m.height);
	m.normals.resize(m.verts.size());
	for (int y = 0; y < m.height; y++) {
		for (int x = 0; x < m.width; x++) {
			int sx = us[x].first, sy = vs[y].first;
			double bu[3], dbu[3], bv[3], dbv[3];
			bezier(us[x].second, bu, dbu);
			bezier(vs[y].second, bv, dbv);
			PatchVertex out;
			Vec3 du, dv;
			for (int b = 0; b < 3; b++) {
				for (int a = 0; a < 3; a++) {
					const PatchVertex &c = P(2 * sx + a, 2 * sy + b);
					double wgt = bu[a] * bv[b];
					out.p = out.p + c.p * wgt;
					out.s += c.s * wgt;
					out.t += c.t * wgt;
					du = du + c.p * (dbu[a] * bv[b]);
					dv = dv + c.p * (bu[a] * dbv[b]);
				}
			}
			Vec3 n = du.cross(dv);
			if (n.length() < 1e-9) {
				// collapsed edge (cone tip, cylinder cap): take the normal just inside
				double tu = std::clamp(us[x].second, 1e-3, 1 - 1e-3), tv = std::clamp(vs[y].second, 1e-3, 1 - 1e-3);
				bezier(tu, bu, dbu);
				bezier(tv, bv, dbv);
				du = dv = Vec3();
				for (int b = 0; b < 3; b++) {
					for (int a = 0; a < 3; a++) {
						const Vec3 &c = P(2 * sx + a, 2 * sy + b).p;
						du = du + c * (dbu[a] * bv[b]);
						dv = dv + c * (bu[a] * dbv[b]);
					}
				}
				n = du.cross(dv);
			}
			m.verts[size_t(y) * m.width + x] = out;
			m.normals[size_t(y) * m.width + x] = n.normalized();
		}
	}
	return m;
}

} // namespace q3map
