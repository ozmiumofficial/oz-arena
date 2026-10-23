#pragma once
// The map editor's geometry kernel (MAP_EDITOR_PLAN.md E0), after OZMIUM's Brush/BrushGeometry/BrushCsg
// and q3map's polylib. Plain C++ in double precision, no Godot and no id headers.
//
// Space is id Tech 3's: +X forward, +Y left, +Z up, in game units. A brush is a list of planes, and
// the planes are the truth: n . p = d, n pointing out of the solid. Face polygons (windings) are
// derived by clipping each plane's huge base winding against all the others.

#include <cstdint>
#include <vector>

namespace q3map {

struct Vec3 {
	double x = 0, y = 0, z = 0;
	Vec3() = default;
	Vec3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}
	Vec3 operator+(const Vec3 &o) const { return {x + o.x, y + o.y, z + o.z}; }
	Vec3 operator-(const Vec3 &o) const { return {x - o.x, y - o.y, z - o.z}; }
	Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
	Vec3 operator-() const { return {-x, -y, -z}; }
	double dot(const Vec3 &o) const { return x * o.x + y * o.y + z * o.z; }
	Vec3 cross(const Vec3 &o) const { return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x}; }
	double length() const;
	Vec3 normalized() const;
};

struct Plane {
	Vec3 n;
	double d = 0;
	double dist(const Vec3 &p) const { return n.dot(p) - d; }
	Plane flipped() const { return {-n, -d}; }
};

using Winding = std::vector<Vec3>;

// Side tolerance for clipping and classification, in game units (q3map uses 0.1 in float; this is
// double precision, so far tighter).
constexpr double EPSILON = 1e-5;
// Base windings reach well past the largest id map (q3map's MAX_WORLD_COORD is 65536).
constexpr double BASE_SIZE = 1 << 20;

// Plane cleanup: unit normal, near-axial components snapped to exactly 0/1, distance near an integer
// snapped to it. Keeps axial brushes on integer coordinates exact through every operation.
Plane clean_plane(Plane p);
bool same_plane(const Plane &a, const Plane &b);

// A square on the plane, BASE_SIZE across, counter-clockwise seen from the front (around +n).
Winding base_winding(const Plane &p);
// Keeps the part of w behind plane (dist <= EPSILON). Points on axial planes land exactly on them.
void clip_back(Winding &w, const Plane &plane);
double winding_area(const Winding &w);
Vec3 winding_center(const Winding &w);

// One winding per plane, empty where the plane makes no face (outside, duplicate or degenerate).
std::vector<Winding> brush_windings(const std::vector<Plane> &planes);
// At least four faces with area and an enclosed volume.
bool brush_valid(const std::vector<Plane> &planes);
// Unique corners of the solid.
std::vector<Vec3> brush_vertices(const std::vector<Plane> &planes);
// Drops planes that make no face. Returns the kept indices into the input.
std::vector<int> brush_face_planes(const std::vector<Plane> &planes);

// The six planes of an axis-aligned box.
std::vector<Plane> box_planes(const Vec3 &mins, const Vec3 &maxs);
// Planes of the convex hull of points (the vertex tool's rebuild). Empty if the points are flat.
std::vector<Plane> hull_planes(const std::vector<Vec3> &points);

// Affine transform x' = M x + t, M as rows. Planes map by M^-T (so mirrors stay outward).
struct Affine {
	double m[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
	Vec3 t;
	Vec3 apply(const Vec3 &p) const;
	Vec3 apply_dir(const Vec3 &v) const;
	bool inverse_transpose(double out[3][3]) const;
};
std::vector<Plane> transform_planes(const std::vector<Plane> &planes, const Affine &xf);

// Splits a brush by a plane. front is the part in front of the plane (n . p > d).
// Either side is empty when the plane doesn't cut through the solid.
struct Split {
	std::vector<Plane> front, back;
	std::vector<int> front_src, back_src; // source index per plane; -1 for the splitting plane
};
Split split_brush(const std::vector<Plane> &planes, const Plane &by);

// CSG subtract: the target minus the cutter, as convex pieces (id Tech's classic method: split
// the target by each cutter plane, keep the outside pieces, drop what's left inside).
// Each piece plane carries its source: >= 0 a target plane, < 0 cutter plane -(src + 1), flipped.
// attr gives the target face each new face takes its attributes from: itself, or for a cut face
// the target face most nearly parallel to it.
struct Piece {
	std::vector<Plane> planes;
	std::vector<int> source;
	std::vector<int> attr;
};
struct SubtractResult {
	bool touched = false; // false: the solids don't overlap; the target is unchanged
	std::vector<Piece> pieces;
};
SubtractResult subtract(const std::vector<Plane> &target, const std::vector<Plane> &cutter);

// Ray against a convex brush. Returns false on a miss or when the ray starts inside.
bool ray_brush(const std::vector<Plane> &planes, const Vec3 &origin, const Vec3 &dir, double &t, int &face);

// Quake 3's default texture axes (q3map's TextureAxisFromPlane: floor, ceiling and the four walls).
void default_texture_axes(const Vec3 &normal, Vec3 &u, Vec3 &v);
// In-plane tangent (dP/du) and bitangent (dP/dv) for texture axes u, v on a face with normal n.
void face_tangents(const Vec3 &n, const Vec3 &u, const Vec3 &v, Vec3 &tangent, Vec3 &bitangent);

// Biquadratic Bezier patch (id Tech 3 patchDef2): a w x h grid of control points, w and h odd.
struct PatchVertex {
	Vec3 p;
	double s = 0, t = 0;
};
struct PatchMesh {
	int width = 0, height = 0; // tessellated grid
	std::vector<PatchVertex> verts;
	std::vector<Vec3> normals;
};
// Each span is cut into enough steps that the curve strays at most tolerance units from the chords.
PatchMesh tessellate_patch(const std::vector<PatchVertex> &ctrl, int w, int h, double tolerance, int max_steps = 16);

} // namespace q3map
