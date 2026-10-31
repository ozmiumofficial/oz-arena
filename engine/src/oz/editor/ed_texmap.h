#pragma once
// Texture mappings on brush faces (ENGINE_PLAN.md P13): the three forms a .map writes, as q3map2
// reads them, to and from the texture coordinates they give (StAxes, in texture repeats).
//   Quake       q3map's QuakeTextureVecs: the default axes for the normal, turned, over the scale,
//               plus the shift, in texels over the texture's size.
//   Primitives  q3map's brush_primit.c: a 2x3 matrix on ComputeAxisBase's axes of the snapped plane.
//   Valve 220   texels = p . axis / scale + offset, over the texture's size.

#include "ed_doc.h"

namespace oz::ed {

// q3map's SnapPlane: a normal within 1e-5 of an axis becomes it; a distance within 0.01 of a whole
// number becomes it. The axis base of a nearly axial plane swings with the tiniest tilt, so q3map's
// frame is the snapped plane's.
Plane snap_plane(const Plane &p);
// brush_primit.c ComputeAxisBase.
void axis_base(const Vec3 &normal, Vec3 &tex_x, Vec3 &tex_y);

StAxes st_from_quake(const double raw[5], const Plane &plane, int w, int h);
StAxes st_from_primitives(const double raw[6], const Plane &plane);
StAxes st_from_valve(const double raw[11], int w, int h);

void primitives_from_st(const StAxes &st, const Plane &plane, double out[6]);
void valve_from_st(const StAxes &st, int w, int h, double out[11]);
// Shift, rotate and scale that give the same texels on the face's polygon, apart from whole repeats,
// when the mapping has that form (no shear, axes on the default projection). tol in texels.
bool quake_from_st(const StAxes &st, const Plane &plane, const q3map::Winding &w, int tw, int th, double out[5],
		double tol = 0.0001);

// The mapping carried through x' = M x + t (texture lock): the same texels stay on the same spots.
StAxes transform_st(const StAxes &st, const q3map::Affine &xf);

}  // namespace oz::ed
