#include "q3mapgeo.h"

#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>

#include <cmath>

#include "map/brush_kernel.h"

using namespace godot;
using Q3Plane = q3map::Plane;
using q3map::Vec3;

static const double UNIT = 1.0 / 32.0;

static std::vector<Q3Plane> to_planes(const PackedFloat64Array &a) {
	std::vector<Q3Plane> out;
	for (int64_t i = 0; i + 3 < a.size(); i += 4) {
		out.push_back(q3map::clean_plane({{a[i], a[i + 1], a[i + 2]}, a[i + 3]}));
	}
	return out;
}

static PackedFloat64Array from_planes(const std::vector<Q3Plane> &planes) {
	PackedFloat64Array a;
	a.resize(planes.size() * 4);
	for (size_t i = 0; i < planes.size(); i++) {
		a[i * 4] = planes[i].n.x;
		a[i * 4 + 1] = planes[i].n.y;
		a[i * 4 + 2] = planes[i].n.z;
		a[i * 4 + 3] = planes[i].d;
	}
	return a;
}

static Vec3 to_vec(const PackedFloat64Array &a, int64_t at = 0) {
	return a.size() >= at + 3 ? Vec3(a[at], a[at + 1], a[at + 2]) : Vec3();
}

static PackedFloat64Array from_points(const std::vector<Vec3> &pts) {
	PackedFloat64Array a;
	a.resize(pts.size() * 3);
	for (size_t i = 0; i < pts.size(); i++) {
		a[i * 3] = pts[i].x;
		a[i * 3 + 1] = pts[i].y;
		a[i * 3 + 2] = pts[i].z;
	}
	return a;
}

static PackedInt32Array from_ints(const std::vector<int> &v) {
	PackedInt32Array a;
	for (int i : v) {
		a.push_back(i);
	}
	return a;
}

static bool to_affine(const PackedFloat64Array &x, q3map::Affine &out) {
	if (x.size() < 12) {
		return false;
	}
	for (int r = 0; r < 3; r++) {
		for (int c = 0; c < 3; c++) {
			out.m[r][c] = x[r * 3 + c];
		}
	}
	out.t = Vec3(x[9], x[10], x[11]);
	return true;
}

// id space -> Godot space
static Vector3 gd_dir(const Vec3 &v) {
	return Vector3(real_t(v.y), real_t(v.z), real_t(v.x));
}

static Vector3 gd_pos(const Vec3 &p) {
	return gd_dir(p * UNIT);
}

static Array surface_arrays() {
	Array a;
	a.resize(Mesh::ARRAY_MAX);
	return a;
}

void Q3MapGeo::_bind_methods() {
	ClassDB::bind_static_method("Q3MapGeo", D_METHOD("brush_windings", "planes"), &Q3MapGeo::brush_windings);
	ClassDB::bind_static_method("Q3MapGeo", D_METHOD("brush_valid", "planes"), &Q3MapGeo::brush_valid);
	ClassDB::bind_static_method("Q3MapGeo", D_METHOD("brush_vertices", "planes"), &Q3MapGeo::brush_vertices);
	ClassDB::bind_static_method("Q3MapGeo", D_METHOD("brush_bounds", "planes"), &Q3MapGeo::brush_bounds);
	ClassDB::bind_static_method("Q3MapGeo", D_METHOD("brush_face_planes", "planes"), &Q3MapGeo::brush_face_planes);
	ClassDB::bind_static_method("Q3MapGeo", D_METHOD("box", "mins", "maxs"), &Q3MapGeo::box);
	ClassDB::bind_static_method("Q3MapGeo", D_METHOD("hull", "points"), &Q3MapGeo::hull);
	ClassDB::bind_static_method("Q3MapGeo", D_METHOD("transform", "planes", "xform"), &Q3MapGeo::transform);
	ClassDB::bind_static_method("Q3MapGeo", D_METHOD("clean", "planes"), &Q3MapGeo::clean);
	ClassDB::bind_static_method("Q3MapGeo", D_METHOD("split", "planes", "plane"), &Q3MapGeo::split);
	ClassDB::bind_static_method("Q3MapGeo", D_METHOD("subtract", "target", "cutter"), &Q3MapGeo::subtract);
	ClassDB::bind_static_method("Q3MapGeo", D_METHOD("ray", "planes", "origin", "dir"), &Q3MapGeo::ray);
	ClassDB::bind_static_method("Q3MapGeo", D_METHOD("texture_axes", "normal"), &Q3MapGeo::texture_axes);
	ClassDB::bind_static_method("Q3MapGeo", D_METHOD("transform_uv", "uv", "xform"), &Q3MapGeo::transform_uv);
	ClassDB::bind_static_method("Q3MapGeo", D_METHOD("mesh_faces", "windings", "planes", "uvs", "tex_sizes", "slots", "colors"), &Q3MapGeo::mesh_faces);
	ClassDB::bind_static_method("Q3MapGeo", D_METHOD("patch_mesh", "ctrl", "width", "height", "tolerance", "color"), &Q3MapGeo::patch_mesh);
}

// One PackedFloat64Array of points per plane, empty where the plane makes no face.
// Windings run counter-clockwise seen from outside.
Array Q3MapGeo::brush_windings(const PackedFloat64Array &planes) {
	Array out;
	for (const q3map::Winding &w : q3map::brush_windings(to_planes(planes))) {
		out.push_back(from_points(w));
	}
	return out;
}

bool Q3MapGeo::brush_valid(const PackedFloat64Array &planes) {
	return q3map::brush_valid(to_planes(planes));
}

PackedFloat64Array Q3MapGeo::brush_vertices(const PackedFloat64Array &planes) {
	return from_points(q3map::brush_vertices(to_planes(planes)));
}

// [minx, miny, minz, maxx, maxy, maxz], empty for an invalid brush.
PackedFloat64Array Q3MapGeo::brush_bounds(const PackedFloat64Array &planes) {
	std::vector<Vec3> v = q3map::brush_vertices(to_planes(planes));
	PackedFloat64Array a;
	if (v.empty()) {
		return a;
	}
	Vec3 lo = v[0], hi = v[0];
	for (const Vec3 &p : v) {
		lo = Vec3(std::fmin(lo.x, p.x), std::fmin(lo.y, p.y), std::fmin(lo.z, p.z));
		hi = Vec3(std::fmax(hi.x, p.x), std::fmax(hi.y, p.y), std::fmax(hi.z, p.z));
	}
	return from_points({lo, hi});
}

// Indices of the planes that make a face.
PackedInt32Array Q3MapGeo::brush_face_planes(const PackedFloat64Array &planes) {
	return from_ints(q3map::brush_face_planes(to_planes(planes)));
}

PackedFloat64Array Q3MapGeo::box(const PackedFloat64Array &mins, const PackedFloat64Array &maxs) {
	return from_planes(q3map::box_planes(to_vec(mins), to_vec(maxs)));
}

// Convex hull planes of the points, empty if they don't enclose a volume.
PackedFloat64Array Q3MapGeo::hull(const PackedFloat64Array &points) {
	std::vector<Vec3> pts;
	for (int64_t i = 0; i + 2 < points.size(); i += 3) {
		pts.push_back(to_vec(points, i));
	}
	return from_planes(q3map::hull_planes(pts));
}

PackedFloat64Array Q3MapGeo::transform(const PackedFloat64Array &planes, const PackedFloat64Array &xform) {
	q3map::Affine xf;
	if (!to_affine(xform, xf)) {
		return planes;
	}
	return from_planes(q3map::transform_planes(to_planes(planes), xf));
}

// Unit normals, snapped axial components and distances (what every other call does on input).
PackedFloat64Array Q3MapGeo::clean(const PackedFloat64Array &planes) {
	return from_planes(to_planes(planes));
}

// {front, back, front_src, back_src}: the pieces in front of and behind the plane; *_src maps each
// piece plane to its index in the input, -1 for the splitting plane. A side is empty if the plane
// doesn't cut the brush.
Dictionary Q3MapGeo::split(const PackedFloat64Array &planes, const PackedFloat64Array &plane) {
	q3map::Split s = q3map::split_brush(to_planes(planes), q3map::clean_plane({to_vec(plane), plane.size() > 3 ? plane[3] : 0}));
	Dictionary d;
	d["front"] = from_planes(s.front);
	d["back"] = from_planes(s.back);
	d["front_src"] = from_ints(s.front_src);
	d["back_src"] = from_ints(s.back_src);
	return d;
}

// {touched, pieces: [{planes, source, attr}]}. touched is false when the solids don't overlap.
// source per plane: >= 0 a target plane, < 0 cutter plane -(source + 1), flipped. attr: the target
// face whose material and texture mapping the face takes.
Dictionary Q3MapGeo::subtract(const PackedFloat64Array &target, const PackedFloat64Array &cutter) {
	q3map::SubtractResult r = q3map::subtract(to_planes(target), to_planes(cutter));
	Dictionary d;
	d["touched"] = r.touched;
	Array pieces;
	for (const q3map::Piece &p : r.pieces) {
		Dictionary pd;
		pd["planes"] = from_planes(p.planes);
		pd["source"] = from_ints(p.source);
		pd["attr"] = from_ints(p.attr);
		pieces.push_back(pd);
	}
	d["pieces"] = pieces;
	return d;
}

// [t, face] for the first hit from outside, or empty.
PackedFloat64Array Q3MapGeo::ray(const PackedFloat64Array &planes, const PackedFloat64Array &origin, const PackedFloat64Array &dir) {
	double t;
	int face;
	PackedFloat64Array out;
	if (q3map::ray_brush(to_planes(planes), to_vec(origin), to_vec(dir), t, face)) {
		out.push_back(t);
		out.push_back(face);
	}
	return out;
}

// Quake 3's default axes for a face normal: [Ux, Uy, Uz, Vx, Vy, Vz].
PackedFloat64Array Q3MapGeo::texture_axes(const PackedFloat64Array &normal) {
	Vec3 u, v;
	q3map::default_texture_axes(to_vec(normal), u, v);
	return from_points({u, v});
}

// Texture lock: the mapping that keeps the same texels on the same surface after x' = M x + t.
PackedFloat64Array Q3MapGeo::transform_uv(const PackedFloat64Array &uv, const PackedFloat64Array &xform) {
	q3map::Affine xf;
	double it[3][3];
	if (uv.size() < 8 || !to_affine(xform, xf) || !xf.inverse_transpose(it)) {
		return uv;
	}
	PackedFloat64Array out;
	out.resize(8);
	for (int k = 0; k < 2; k++) {
		Vec3 a = to_vec(uv, k * 4);
		Vec3 b(it[0][0] * a.x + it[0][1] * a.y + it[0][2] * a.z,
				it[1][0] * a.x + it[1][1] * a.y + it[1][2] * a.z,
				it[2][0] * a.x + it[2][1] * a.y + it[2][2] * a.z);
		out[k * 4] = b.x;
		out[k * 4 + 1] = b.y;
		out[k * 4 + 2] = b.z;
		out[k * 4 + 3] = uv[k * 4 + 3] - b.dot(xf.t);
	}
	return out;
}

// Brush faces to Godot surfaces, one per material slot: {slot: surface arrays}.
// Per face: windings[i] (id points), planes 4, uvs 8, tex_sizes 2 (texels), slots 1, colors 1
// (colors may be empty: no vertex colors). Faces with an empty winding are skipped.
Dictionary Q3MapGeo::mesh_faces(const Array &windings, const PackedFloat64Array &planes, const PackedFloat64Array &uvs,
		const PackedFloat64Array &tex_sizes, const PackedInt32Array &slots, const PackedColorArray &colors) {
	struct Out {
		PackedVector3Array v, n;
		PackedFloat32Array t;
		PackedVector2Array uv;
		PackedColorArray c;
		PackedInt32Array idx;
	};
	std::vector<std::pair<int, Out>> outs;
	auto out_for = [&](int slot) -> Out & {
		for (auto &o : outs) {
			if (o.first == slot) {
				return o.second;
			}
		}
		outs.push_back({slot, Out()});
		return outs.back().second;
	};
	int64_t faces = windings.size();
	for (int64_t f = 0; f < faces; f++) {
		PackedFloat64Array w = windings[f];
		int64_t count = w.size() / 3;
		if (count < 3 || planes.size() < f * 4 + 4 || uvs.size() < f * 8 + 8 || slots.size() <= f) {
			continue;
		}
		Out &o = out_for(slots[f]);
		Vec3 n = to_vec(planes, f * 4).normalized();
		Vec3 U = to_vec(uvs, f * 8), V = to_vec(uvs, f * 8 + 4);
		double uoff = uvs[f * 8 + 3], voff = uvs[f * 8 + 7];
		double tw = tex_sizes.size() > f * 2 + 1 && tex_sizes[f * 2] > 0 ? tex_sizes[f * 2] : 128;
		double th = tex_sizes.size() > f * 2 + 1 && tex_sizes[f * 2 + 1] > 0 ? tex_sizes[f * 2 + 1] : 128;
		Vec3 tan, bit;
		q3map::face_tangents(n, U, V, tan, bit);
		// Godot's binormal points up the texture (OpenGL normal maps): -dP/dv
		Vec3 binormal = -bit;
		Vector3 gt = gd_dir(tan.normalized());
		float sign = n.cross(tan).dot(binormal) < 0 ? -1.0f : 1.0f;
		Vector3 gn = gd_dir(n);
		Color col = colors.size() > f ? colors[f] : Color(1, 1, 1);
		int base = int(o.v.size());
		for (int64_t i = 0; i < count; i++) {
			Vec3 p = to_vec(w, i * 3);
			o.v.push_back(gd_pos(p));
			o.n.push_back(gn);
			o.t.push_back(gt.x);
			o.t.push_back(gt.y);
			o.t.push_back(gt.z);
			o.t.push_back(sign);
			o.uv.push_back(Vector2(real_t((U.dot(p) + uoff) / tw), real_t((V.dot(p) + voff) / th)));
			if (!colors.is_empty()) {
				o.c.push_back(col);
			}
		}
		// counter-clockwise from outside in id space; Godot's front faces are clockwise
		for (int64_t i = 1; i + 1 < count; i++) {
			o.idx.push_back(base);
			o.idx.push_back(base + int(i) + 1);
			o.idx.push_back(base + int(i));
		}
	}
	Dictionary d;
	for (auto &o : outs) {
		Array a = surface_arrays();
		a[Mesh::ARRAY_VERTEX] = o.second.v;
		a[Mesh::ARRAY_NORMAL] = o.second.n;
		a[Mesh::ARRAY_TANGENT] = o.second.t;
		a[Mesh::ARRAY_TEX_UV] = o.second.uv;
		if (!o.second.c.is_empty()) {
			a[Mesh::ARRAY_COLOR] = o.second.c;
		}
		a[Mesh::ARRAY_INDEX] = o.second.idx;
		d[o.first] = a;
	}
	return d;
}

// A biquadratic patch: ctrl is width x height points of [x, y, z, s, t] (row by row, s/t already
// divided by the texture size). Returns {arrays, width, height, points} where points is the
// tessellated grid in id space (for picking).
Dictionary Q3MapGeo::patch_mesh(const PackedFloat64Array &ctrl, int width, int height, double tolerance, const Color &color) {
	std::vector<q3map::PatchVertex> cv;
	for (int64_t i = 0; i + 4 < ctrl.size(); i += 5) {
		q3map::PatchVertex v;
		v.p = to_vec(ctrl, i);
		v.s = ctrl[i + 3];
		v.t = ctrl[i + 4];
		cv.push_back(v);
	}
	q3map::PatchMesh m = q3map::tessellate_patch(cv, width, height, tolerance);
	Dictionary d;
	d["width"] = m.width;
	d["height"] = m.height;
	PackedVector3Array v, n;
	PackedVector2Array uv;
	PackedColorArray c;
	PackedFloat64Array pts;
	PackedInt32Array idx;
	for (size_t i = 0; i < m.verts.size(); i++) {
		v.push_back(gd_pos(m.verts[i].p));
		n.push_back(gd_dir(m.normals[i]));
		uv.push_back(Vector2(real_t(m.verts[i].s), real_t(m.verts[i].t)));
		c.push_back(color);
		pts.push_back(m.verts[i].p.x);
		pts.push_back(m.verts[i].p.y);
		pts.push_back(m.verts[i].p.z);
	}
	for (int y = 0; y + 1 < m.height; y++) {
		for (int x = 0; x + 1 < m.width; x++) {
			int a = y * m.width + x, b = a + 1, cc = a + m.width, dd = cc + 1;
			// counter-clockwise around du x dv is (a, b, dd), (a, dd, cc); Godot wants them reversed
			idx.push_back(a);
			idx.push_back(dd);
			idx.push_back(b);
			idx.push_back(a);
			idx.push_back(cc);
			idx.push_back(dd);
		}
	}
	Array a = surface_arrays();
	if (!v.is_empty()) {
		a[Mesh::ARRAY_VERTEX] = v;
		a[Mesh::ARRAY_NORMAL] = n;
		a[Mesh::ARRAY_TEX_UV] = uv;
		a[Mesh::ARRAY_COLOR] = c;
		a[Mesh::ARRAY_INDEX] = idx;
	}
	d["arrays"] = a;
	d["points"] = pts;
	return d;
}
