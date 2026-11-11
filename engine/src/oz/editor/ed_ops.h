#pragma once
// The map editor's edits on the document (ENGINE_PLAN.md P13, engine/EDITOR.md): moves, turns,
// mirrors and resizes (with texture lock), pushing faces, vertex edits, the clipper, CSG subtract,
// hollow and merge, copies and deletes. Plain C++ like the document.
//
// Textures. Every face keeps its file numbers (raw) valid. Texture lock off, a face's texture stays
// where it is in the world: a move or a push keeps both its numbers and its texture coordinates (no
// form's numbers depend on where the plane is, only on its normal), and a turned face is projected
// again from its numbers, as Radiant does. Texture lock on, the texture coordinates move with the
// brush (transform_st) and the numbers are made again from them, in the face's form if it can hold
// them (else brushDef). A new face (a cut, a hull) takes the numbers of the old face pointing most
// nearly its way. A brush entity whose origin moves (a door moved whole) keeps its texture on it,
// lock or not, as q3map2 maps it from that origin.
//
// The functions named for a result (transformed, pushed, from_points, clipped) are pure: they make
// the new object, for a tool's preview; the verbs make one undo step on the document.

#include "ed_doc.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace oz::ed {

using q3map::Affine;
using q3map::Winding;

struct Bounds {
	Vec3 lo{1e30, 1e30, 1e30}, hi{-1e30, -1e30, -1e30};
	bool empty() const { return lo.x > hi.x; }
	void add(const Vec3 &p);
	void add(const Bounds &b);
	Vec3 center() const { return (lo + hi) * 0.5; }
	Vec3 size() const { return hi - lo; }
	bool overlaps(const Bounds &b, double eps = 0) const;
};

// ---- transforms (x' = M x + t); turns by multiples of 90 degrees are exact
Affine translation(const Vec3 &t);
Affine rotation(int axis, double degrees, const Vec3 &pivot);
Affine mirror(int axis, const Vec3 &pivot);
Affine scaling(const Vec3 &scale, const Vec3 &pivot);
double determinant(const Affine &xf);
bool is_translation(const Affine &xf);

// ---- entities
bool parse_vec(const std::string &s, Vec3 &v);
std::string vec_key(const Vec3 &v);         // "x y z", as a .map writes numbers
bool merges_into_world(const std::string &classname);  // worldspawn, func_group: q3map2 merges their brushes
bool is_brush_class(const std::string &classname);     // func_*, trigger_*: their brushes are their shape
// A point entity's box in the world (Radiant's sizes); false for brush entities and worldspawn.
bool point_box(const Object &o, Vec3 &lo, Vec3 &hi);
void entity_color(const std::string &classname, unsigned char rgba[4]);

// A light's numbers as q3map2 reads them (NetRadiant-custom light.cpp, CreateEntityLights): its
// intensity ("_light" or "light", 0 or none is 300) times "scale"; spawnflag 1 its linear falloff,
// faded by "fade" (none is 1). Linear, it adds intensity * scale * 7500 / 8000 - distance * fade, so it
// ends at range; else it falls off as the inverse square, reach then where it's about a sixteenth bright.
struct LightNumbers {
	std::string key = "light";  // the key its intensity is in
	double intensity = 300, scale = 1, fade = 1;
	bool linear = false;
	double range = 0;  // where a linear light ends (0: inverse square)
	double reach = 0;  // where it ends or nearly (the view's circle)
};
LightNumbers light_numbers(const Entity &e);
// The fade a linear light of this intensity and scale needs to end at range.
double light_fade_for(double intensity, double scale, double range);

// Where q3map2 maps an object's faces from: its entity's model_origin (none for an entity itself).
Vec3 origin_for(const Doc &doc, const Object &o);
// ids and, for entities other than the world, their brushes and curves; never the world itself.
std::vector<Id> with_owned(const Doc &doc, const std::vector<Id> &ids);

// ---- shape
Bounds bounds_of(const Doc &doc, Id id);
Bounds bounds_of(const Doc &doc, const std::vector<Id> &ids);
// A brush's corners, a curve's control points, a point entity's origin.
std::vector<Vec3> object_points(const Doc &doc, Id id);
q3map::PatchMesh patch_mesh(const Patch &p, double tolerance);

// ---- curves
// A curve filling a box, as Radiant's patch shapes: a cylinder or a cone round axis (its rings counter-
// clockwise seen from the axis's end, facing out); an arch (half round, standing on z across the box's
// longer side, facing in) or a bevel (a quarter round, facing in); a flat sheet on the box's top
// (facing up). Textured naturally.
enum class CurveShape { Cylinder, Cone, Arch, Bevel, Plane };
std::shared_ptr<Object> curve_in_box(const Doc &doc, CurveShape shape, const Bounds &box, int axis, ShaderId shader, Id owner);
// The curve turned inside out (its rows the other way round): its other side shows.
std::shared_ptr<Object> inverted(const Object &patch);
// The texture along the curve as brushes have it: half scale, by the length of its rows and columns.
void natural_texture(const Doc &doc, Patch &p);
// Which way the curve faces at its middle control point (du x dv, the kernel's facing), not normalized.
Vec3 curve_facing(const Patch &p);
// Each section across (columns) or down (rows) in two, the shape exactly as it was.
std::shared_ptr<Object> curve_split(const Object &patch, bool rows);
// Every two sections in one, through the middle point between them; nullptr for an odd count.
std::shared_ptr<Object> curve_merge(const Object &patch, bool rows);
// Caps for the curve's open ends (its first and last rows): a closed ring of 9 (a cylinder's) a 3 x 3
// disc, a half ring of 5 (an arch's) a 3 x 3 half disc down to its base; each facing the curve's way.
std::vector<std::shared_ptr<Object>> curve_caps(const Doc &doc, const Object &patch);
// The curve made thick: its other skin `depth` behind it facing the other way, and a seam along each
// border that isn't closed (a cylinder's ends, not its join).
std::vector<std::shared_ptr<Object>> curve_thick(const Doc &doc, const Object &patch, double depth);

// ---- rays (t along d from o)
bool ray_faces(const Brush &b, const Vec3 &o, const Vec3 &d, double &t, int &face);
bool ray_box(const Vec3 &o, const Vec3 &d, const Vec3 &lo, const Vec3 &hi, double &t, Vec3 &normal);
bool ray_triangle(const Vec3 &o, const Vec3 &d, const Vec3 &a, const Vec3 &b, const Vec3 &c, double &t);

// ---- faces and brushes
// A face on plane in the document's plain form, the shader at half scale on q3map2's default axes.
Face new_face(const Doc &doc, const Plane &plane, ShaderId shader);
std::shared_ptr<Object> box_brush(const Doc &doc, const Vec3 &lo, const Vec3 &hi, ShaderId shader, Id owner);
// A Valve 220 face's own axes when its plane leaves them edge-on: its default axes then, as
// TrenchBroom's "paraxial" reset, keeping the scale and offsets.
void fix_valve_axes(Face &f);
// Oz's prototyping textures (textures/oz_dev): their grid measures the world, so they always sit on
// the world's default projection at half scale, whatever texture lock says.
bool world_grid(const Face &f);
// The face's texture on the default projection at half scale, in its own form (Reset mapping).
void world_mapping(const Doc &doc, Face &f, const Vec3 &origin, const Winding &w);
// src's attributes (shader, numbers, flags) on another plane, its texture projected there again.
Face retarget(const Doc &doc, const Face &src, const Plane &plane, const Vec3 &origin);
// A brush of these planes, each face taking the attributes of the source face pointing most nearly
// its way (sources: the faces of one or more brushes).
std::shared_ptr<Object> brush_like(const Doc &doc, const std::vector<const Face *> &sources, Id owner,
		const std::vector<Plane> &planes, const Vec3 &origin);

// ---- pure edits: the new object, or nullptr when the edit would leave no valid brush
std::shared_ptr<Object> transformed(const Doc &doc, const Object &o, const Affine &xf, bool lock, const Vec3 &origin);
std::shared_ptr<Object> pushed(const Doc &doc, const Object &brush, const std::vector<int> &faces, double dist);
std::shared_ptr<Object> from_points(const Doc &doc, const Object &brush, const std::vector<Vec3> &points, const Vec3 &origin);
struct Pieces {
	std::shared_ptr<Object> front, back;  // in front of the plane (n . p > d), behind it
	bool whole = false;                   // the plane misses the brush: one side has it as it was
};
Pieces clipped(const Doc &doc, const Object &brush, const Plane &plane, const Vec3 &origin);

// ---- steps on the document. Each is one undo step named label; false, or nothing returned, when
// nothing changed. note: what to tell the mapper (why nothing happened, or what did).
bool transform(Doc &doc, const std::vector<Id> &ids, const Affine &xf, bool lock, const std::string &label);
bool push_faces(Doc &doc, const std::vector<std::pair<Id, int>> &faces, double dist, const std::string &label);
// keep: 0 the side behind the plane, 1 the side in front, 2 both. The brushes made (the kept ones).
std::vector<Id> clip(Doc &doc, const std::vector<Id> &ids, const Plane &plane, int keep, std::string &note);
// The cutters (the brushes among ids) carved out of every other brush they overlap, then deleted.
std::vector<Id> subtract(Doc &doc, const std::vector<Id> &ids, std::string &note);
// Each brush made into walls of thickness around its inside (Radiant's hollow, the walls overlapping).
std::vector<Id> hollow(Doc &doc, const std::vector<Id> &ids, double thickness, std::string &note);
// Each wedge (a brush with a sloped top) cut into solid steps climbing its slope: count steps, or
// with count 0 as many as make each about rise units high (2 at least).
std::vector<Id> stairs(Doc &doc, const std::vector<Id> &ids, int count, double rise, std::string &note);
// The brushes joined where together they make one convex brush (their hull fills nothing more), within
// each entity; the rest stay as they were. The brushes left: the joined ones and those kept apart.
std::vector<Id> merge(Doc &doc, const std::vector<Id> &ids, std::string &note);
// Copies with new ids, moved by offset. A copied entity takes copies of its brushes; a brush whose
// entity isn't copied stays in it. The copies' ids, objects and brushes, in file order.
// The objects with the ids they had. owner_class: for a brush or curve copied without its entity, that
// entity's classname as it was (by its id): a paste keeps the brush in the entity with that id only while
// it's still an entity of that class. The clipboard outlives New and Open, where the same id is another
// object: a pasted brush became a light's, or a brush's, and was left out of the saved file.
struct Clip : std::vector<std::pair<Id, ObjectPtr>> {
	std::map<Id, std::string> owner_class;
};
Clip copy_of(const Doc &doc, const std::vector<Id> &ids);
std::vector<Id> paste(Doc &doc, const Clip &clip, const Vec3 &offset, const std::string &label);
// Deletes ids, an entity with its brushes; a brush entity left without brushes goes too. The world stays.
bool remove(Doc &doc, const std::vector<Id> &ids, const std::string &label);
// Entity keys on every entity in ents, one step: set (the first match, else appended), remove (every
// match), rename (in place; refused when the new key is already there). A brush entity whose origin
// moves keeps its textures where they are (its faces' numbers made again from the new origin).
bool set_key(Doc &doc, const std::vector<Id> &ents, const std::string &key, const std::string &value, const std::string &label);
bool remove_key(Doc &doc, const std::vector<Id> &ents, const std::string &key, const std::string &label);
bool rename_key(Doc &doc, const std::vector<Id> &ents, const std::string &from, const std::string &to, std::string &note);

}  // namespace oz::ed
