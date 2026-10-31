#include "ed_texmap.h"

#include <cmath>

namespace oz::ed {

static const double PI = 3.14159265358979323846;

Plane snap_plane(const Plane &p) {
	Plane out = p;
	const double c[3] = {p.n.x, p.n.y, p.n.z};
	for (int i = 0; i < 3; i++) {
		if (std::fabs(c[i] - 1) < 1e-5 || std::fabs(c[i] + 1) < 1e-5) {
			double s = c[i] > 0 ? 1 : -1;
			out.n = Vec3(i == 0 ? s : 0, i == 1 ? s : 0, i == 2 ? s : 0);
			break;
		}
	}
	if (std::fabs(p.d - std::round(p.d)) < 0.01) {
		out.d = std::round(p.d);
	}
	return out;
}

void axis_base(const Vec3 &normal, Vec3 &tex_x, Vec3 &tex_y) {
	double nx = std::fabs(normal.x) < 1e-6 ? 0 : normal.x;
	double ny = std::fabs(normal.y) < 1e-6 ? 0 : normal.y;
	double nz = std::fabs(normal.z) < 1e-6 ? 0 : normal.z;
	double rot_y = -std::atan2(nz, std::sqrt(ny * ny + nx * nx));
	double rot_z = std::atan2(ny, nx);
	tex_x = Vec3(-std::sin(rot_z), std::cos(rot_z), 0);
	tex_y = Vec3(-std::sin(rot_y) * std::cos(rot_z), -std::sin(rot_y) * std::sin(rot_z), -std::cos(rot_y));
}

static double comp(const Vec3 &v, int i) {
	return i == 0 ? v.x : i == 1 ? v.y : v.z;
}

static double &comp(Vec3 &v, int i) {
	return i == 0 ? v.x : i == 1 ? v.y : v.z;
}

// QuakeTextureVecs' texel axes: the default axes turned by the rotation (exact at quarter turns)
// and divided by the scales; the shifts are the offsets.
static void quake_axes(const double raw[5], const Plane &plane, Vec3 &u, Vec3 &v) {
	q3map::default_texture_axes(plane.n, u, v);
	double rot = std::fmod(raw[2], 360.0);
	if (rot < 0) {
		rot += 360;
	}
	double s = 0, c = 1;
	if (rot == 90) {
		s = 1, c = 0;
	} else if (rot == 180) {
		s = 0, c = -1;
	} else if (rot == 270) {
		s = -1, c = 0;
	} else if (rot != 0) {
		s = std::sin(rot * PI / 180), c = std::cos(rot * PI / 180);
	}
	int sv = u.x != 0 ? 0 : (u.y != 0 ? 1 : 2);
	int tv = v.x != 0 ? 0 : (v.y != 0 ? 1 : 2);
	for (Vec3 *a : {&u, &v}) {
		double ns = c * comp(*a, sv) - s * comp(*a, tv);
		double nt = s * comp(*a, sv) + c * comp(*a, tv);
		comp(*a, sv) = ns;
		comp(*a, tv) = nt;
	}
	double ss = raw[3] != 0 ? raw[3] : 1, st = raw[4] != 0 ? raw[4] : 1;
	u = u * (1 / ss);
	v = v * (1 / st);
}

StAxes st_from_quake(const double raw[5], const Plane &plane, int w, int h) {
	Vec3 u, v;
	quake_axes(raw, plane, u, v);
	StAxes st;
	st.s = u * (1.0 / w);
	st.t = v * (1.0 / h);
	st.s0 = raw[0] / w;
	st.t0 = raw[1] / h;
	return st;
}

StAxes st_from_primitives(const double raw[6], const Plane &plane) {
	Vec3 tx, ty;
	axis_base(snap_plane(plane).n, tx, ty);
	StAxes st;
	st.s = tx * raw[0] + ty * raw[1];
	st.s0 = raw[2];
	st.t = tx * raw[3] + ty * raw[4];
	st.t0 = raw[5];
	return st;
}

StAxes st_from_valve(const double raw[11], int w, int h) {
	double ss = raw[9] != 0 ? raw[9] : 1, sts = raw[10] != 0 ? raw[10] : 1;
	StAxes st;
	st.s = Vec3(raw[0], raw[1], raw[2]) * (1.0 / (ss * w));
	st.s0 = raw[3] / w;
	st.t = Vec3(raw[4], raw[5], raw[6]) * (1.0 / (sts * h));
	st.t0 = raw[7] / h;
	return st;
}

void primitives_from_st(const StAxes &st, const Plane &plane, double out[6]) {
	// q3map2 takes s = m0 x + m1 y + m2 with x = p . tex_x, y = p . tex_y, the axes of the snapped
	// plane. The face's own plane can lean off it by up to 1e-5, and ComputeAxisBase drops tiny normal
	// components, so the matrix is solved on three points of the face's own plane: exact there.
	Vec3 tx, ty;
	axis_base(snap_plane(plane).n, tx, ty);
	Vec3 e1, e2;
	q3map::default_texture_axes(plane.n, e1, e2);
	e1 = (e1 - plane.n * e1.dot(plane.n)).normalized();
	e2 = plane.n.cross(e1);
	const double L = 1024;
	Vec3 p[3] = {plane.n * plane.d, plane.n * plane.d + e1 * L, plane.n * plane.d + e2 * L};
	// rows [x y 1]: solved by Cramer's rule for each of s and t
	double a[3][3];
	for (int i = 0; i < 3; i++) {
		a[i][0] = p[i].dot(tx), a[i][1] = p[i].dot(ty), a[i][2] = 1;
	}
	auto det3 = [](double m[3][3]) {
		return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
				m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
	};
	double det = det3(a);
	for (int axis = 0; axis < 2; axis++) {
		const Vec3 &S = axis == 0 ? st.s : st.t;
		double s0 = axis == 0 ? st.s0 : st.t0;
		double rhs[3] = {S.dot(p[0]) + s0, S.dot(p[1]) + s0, S.dot(p[2]) + s0};
		for (int c = 0; c < 3; c++) {
			double m[3][3];
			for (int i = 0; i < 3; i++) {
				for (int j = 0; j < 3; j++) {
					m[i][j] = j == c ? rhs[i] : a[i][j];
				}
			}
			out[axis * 3 + c] = std::fabs(det) > 1e-12 ? det3(m) / det : 0;
		}
	}
}

void valve_from_st(const StAxes &st, int w, int h, double out[11]) {
	// texels = p . axis / scale + offset: each axis the texel axis made unit, its scale one over its length
	Vec3 u = st.s * w, v = st.t * h;
	double lu = u.length(), lv = v.length();
	double su = lu > 1e-12 ? 1 / lu : 1, sv = lv > 1e-12 ? 1 / lv : 1;
	Vec3 un = u * su, vn = v * sv;
	double vals[11] = {un.x, un.y, un.z, st.s0 * w, vn.x, vn.y, vn.z, st.t0 * h, 0, su, sv};
	for (int i = 0; i < 11; i++) {
		out[i] = vals[i];
	}
}

static double snapped(double x, double step) {
	return std::round(x / step) * step;
}

static double posmod(double x, double m) {
	double r = std::fmod(x, m);
	return r < 0 ? r + m : r;
}

// Whether a Quake mapping gives the polygon's corners the same texels as st, apart from whole repeats.
static bool quake_matches(const double tex[5], const StAxes &st, const Plane &plane, const q3map::Winding &w, int tw,
		int th, double tol) {
	StAxes back = st_from_quake(tex, plane, tw, th);
	double ds0 = 0, dt0 = 0;
	for (size_t i = 0; i < w.size(); i++) {
		double ds = (back.s.dot(w[i]) + back.s0 - st.s.dot(w[i]) - st.s0) * tw;
		double dt = (back.t.dot(w[i]) + back.t0 - st.t.dot(w[i]) - st.t0) * th;
		if (i == 0) {
			ds0 = ds, dt0 = dt;
			// whole repeats of the texture draw the same
			if (std::fabs(ds / tw - std::round(ds / tw)) * tw > tol || std::fabs(dt / th - std::round(dt / th)) * th > tol) {
				return false;
			}
		} else if (std::fabs(ds - ds0) > tol || std::fabs(dt - dt0) > tol) {
			return false;
		}
	}
	return !w.empty();
}

bool quake_from_st(const StAxes &st, const Plane &plane, const q3map::Winding &w, int tw, int th, double out[5],
		double tol) {
	Vec3 b0, b1;
	q3map::default_texture_axes(plane.n, b0, b1);
	// the axis the projection drops: the one neither default axis uses
	int drop = 0;
	for (int k = 0; k < 3; k++) {
		if (comp(b0, k) == 0 && comp(b1, k) == 0) {
			drop = k;
		}
	}
	double nd = comp(plane.n, drop);
	if (std::fabs(nd) < 1e-6) {
		return false;
	}
	// the same values on the plane with nothing along the dropped axis: U' = U - k n gives
	// U' . p = U . p - k d there, so the offset gains k d
	Vec3 u = st.s * tw, v = st.t * th;
	double ku = comp(u, drop) / nd, kv = comp(v, drop) / nd;
	u = u - plane.n * ku;
	v = v - plane.n * kv;
	double shift_s = st.s0 * tw + ku * plane.d;
	double shift_t = st.t0 * th + kv * plane.d;
	int sv = b0.x != 0 ? 0 : (b0.y != 0 ? 1 : 2);
	int tv = b1.x != 0 ? 0 : (b1.y != 0 ? 1 : 2);
	double b0x = comp(b0, sv), b0y = comp(b0, tv), b1x = comp(b1, sv), b1y = comp(b1, tv);
	double ux = comp(u, sv), uy = comp(u, tv), vx = comp(v, sv), vy = comp(v, tv);
	double lu = std::sqrt(ux * ux + uy * uy), lv = std::sqrt(vx * vx + vy * vy);
	if (lu < 1e-9 || lv < 1e-9) {
		return false;
	}
	for (double flip : {1.0, -1.0}) {
		double sx = flip / lu;
		double rot = std::atan2(uy * sx, ux * sx) - std::atan2(b0y, b0x);
		// b1 turned by rot
		double rbx = b1x * std::cos(rot) - b1y * std::sin(rot), rby = b1x * std::sin(rot) + b1y * std::cos(rot);
		double dot = vx * rbx + vy * rby;
		if (std::fabs(dot) < 1e-9) {
			continue;
		}
		double sy = 1 / dot;
		double deg = posmod(rot * 180 / PI, 360);
		for (bool snap : {true, false}) {
			double tex[5] = {shift_s, shift_t, deg, sx, sy};
			if (snap) {
				tex[0] = snapped(shift_s, 0.001), tex[1] = snapped(shift_t, 0.001), tex[2] = snapped(deg, 0.001);
				tex[3] = snapped(sx, 0.00001), tex[4] = snapped(sy, 0.00001);
			}
			tex[0] = posmod(tex[0], tw);
			tex[1] = posmod(tex[1], th);
			if (snap) {
				tex[0] = snapped(tex[0], 0.001), tex[1] = snapped(tex[1], 0.001);
				tex[2] = posmod(tex[2], 360);  // 359.9996 rounds to 360: that's 0
				if (tex[0] >= tw) {
					tex[0] -= tw;
				}
				if (tex[1] >= th) {
					tex[1] -= th;
				}
			}
			if (quake_matches(tex, st, plane, w, tw, th, tol)) {
				for (int i = 0; i < 5; i++) {
					out[i] = tex[i] + 0.0;
				}
				return true;
			}
		}
	}
	return false;
}

StAxes transform_st(const StAxes &st, const q3map::Affine &xf) {
	// S' . (M x + t) + s0' = S . x + s0 for every x: S' = M^-T S, s0' = s0 - S' . t
	double it[3][3];
	if (!xf.inverse_transpose(it)) {
		return st;
	}
	auto mul = [&](const Vec3 &a) {
		return Vec3(it[0][0] * a.x + it[0][1] * a.y + it[0][2] * a.z, it[1][0] * a.x + it[1][1] * a.y + it[1][2] * a.z,
				it[2][0] * a.x + it[2][1] * a.y + it[2][2] * a.z);
	};
	StAxes out;
	out.s = mul(st.s);
	out.t = mul(st.t);
	out.s0 = st.s0 - out.s.dot(xf.t);
	out.t0 = st.t0 - out.t.dot(xf.t);
	return out;
}

}  // namespace oz::ed
