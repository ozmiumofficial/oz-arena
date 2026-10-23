#pragma once
// Q3MapGeo: the map editor's geometry kernel (src/map/brush_kernel.*) for GDScript.
// Everything is in id space and double precision: planes are PackedFloat64Array [nx, ny, nz, d, ...]
// (n . p = d, n out of the solid), points [x, y, z, ...], transforms 12 numbers (3 rows of M, then t).
// Face texture mapping is 8 numbers [Ux, Uy, Uz, Uoff, Vx, Vy, Vz, Voff] in texels: u = U . p + Uoff.
// The mesh builders return Godot surface arrays in Godot space (x = Y, y = Z, z = X, 32 units a meter).

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_float64_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>

namespace godot {

class Q3MapGeo : public RefCounted {
	GDCLASS(Q3MapGeo, RefCounted)

protected:
	static void _bind_methods();

public:
	static Array brush_windings(const PackedFloat64Array &planes);
	static bool brush_valid(const PackedFloat64Array &planes);
	static PackedFloat64Array brush_vertices(const PackedFloat64Array &planes);
	static PackedFloat64Array brush_bounds(const PackedFloat64Array &planes);
	static PackedInt32Array brush_face_planes(const PackedFloat64Array &planes);
	static PackedFloat64Array box(const PackedFloat64Array &mins, const PackedFloat64Array &maxs);
	static PackedFloat64Array hull(const PackedFloat64Array &points);
	static PackedFloat64Array transform(const PackedFloat64Array &planes, const PackedFloat64Array &xform);
	static PackedFloat64Array clean(const PackedFloat64Array &planes);
	static Dictionary split(const PackedFloat64Array &planes, const PackedFloat64Array &plane);
	static Dictionary subtract(const PackedFloat64Array &target, const PackedFloat64Array &cutter);
	static PackedFloat64Array ray(const PackedFloat64Array &planes, const PackedFloat64Array &origin, const PackedFloat64Array &dir);
	static PackedFloat64Array texture_axes(const PackedFloat64Array &normal);
	static PackedFloat64Array transform_uv(const PackedFloat64Array &uv, const PackedFloat64Array &xform);
	static Dictionary mesh_faces(const Array &windings, const PackedFloat64Array &planes, const PackedFloat64Array &uvs,
			const PackedFloat64Array &tex_sizes, const PackedInt32Array &slots, const PackedColorArray &colors);
	static Dictionary patch_mesh(const PackedFloat64Array &ctrl, int width, int height, double tolerance, const Color &color);
};

} // namespace godot
