#pragma once
// Texture mapping edits on brush faces (ENGINE_PLAN.md P13, engine/EDITOR.md): the inspector's and
// the faces tool's shift, scale, rotate, fit, align, flip and reset, the typed numbers, painting a
// material and copying a face's mapping (the eyedropper). Plain C++ like the document.
//
// Every edit keeps the face in its form, as Radiant and TrenchBroom do:
//   Quake      its shift, rotate and scale numbers change, as typed;
//   Valve 220  its offsets and scales change, and a turn turns its axes (its rotate number follows);
//   brushDef   its matrix changes so the texels move the same way.
// A shift is in texels of the face's own texture, a turn in degrees the way q3map's rotate turns
// (about the texture axes' T x S), a scale multiplies the scale numbers (bigger: the texture larger).

#include "ed_ops.h"

#include <string>
#include <utility>
#include <vector>

namespace oz::ed {

// A face's numbers as the inspector shows them: shift s and t (texels), rotate (degrees), scale s
// and t. Quake: its own; Valve 220: its offsets, rotate and scales; brushDef: the Quake numbers that
// give the same texels, when it has that form (false otherwise: a sheared or skewed mapping).
struct TexFields {
	double v[5] = {0, 0, 0, 0.5, 0.5};  // shift s, shift t, rotate, scale s, scale t
};
bool tex_fields(const Doc &doc, const Face &f, const Winding &w, const Vec3 &origin, TexFields &out);

enum class MapOp {
	Shift,   // a, b: texels along s and t
	Rotate,  // a: degrees
	Scale,   // a, b: the scale numbers times these
	Fit,     // a, b: the texture this many times across the face along s and t (0: that axis as it is)
	Align,   // which: 0 left, 1 right, 2 top, 3 bottom, 4 center: that edge on a texture edge
	Flip,    // which: 0 s, 1 t: mirrored in place
	Reset,   // q3map's default projection at half scale
	Field,   // which: the TexFields index set to a
};
struct MapEdit {
	MapOp op = MapOp::Shift;
	double a = 0, b = 0;
	int which = 0;
};

// The face with the edit done (pure); false when it can't take it (a scale of 0, a field a brushDef
// mapping can't show).
bool remapped(const Doc &doc, Face &f, const Winding &w, const Vec3 &origin, const MapEdit &e);

using FaceRef = std::pair<Id, int>;  // a brush and the index of its face

// One undo step over the faces; false when nothing changed.
bool map_faces(Doc &doc, const std::vector<FaceRef> &faces, const MapEdit &e, const std::string &label);
// The same edits on curves' texture coordinates: shift (texels), rotate, scale, fit (this many times
// across its columns and down its rows), flip, reset (natural); align and the typed numbers are faces'.
bool map_patches(Doc &doc, const std::vector<Id> &patches, const MapEdit &e, const std::string &label);
// The shader onto the faces and curves, each keeping its numbers (Radiant's way: a Quake or Valve
// 220 face counts in texels, so a texture of another size keeps its texel scale).
bool paint(Doc &doc, const std::vector<FaceRef> &faces, const std::vector<Id> &patches, ShaderId shader, const std::string &label);
// The source face's shader and mapping onto the faces (the eyedropper's paint): a face parallel to
// it takes its texture coordinates, so the texture runs on across; the others its numbers on their
// own plane. Each keeps its plane and flags. src_origin: where q3map2 maps the source from.
bool copy_mapping(Doc &doc, const std::vector<FaceRef> &faces, const Face &src, const Vec3 &src_origin, const std::string &label);

}  // namespace oz::ed
