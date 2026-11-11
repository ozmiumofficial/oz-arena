#pragma once
// The map editor's document (ENGINE_PLAN.md P13): a Quake III .map held losslessly, with undo.
//
// The file is the truth. Every entity keeps its keys in the file's order (repeats included), every
// brush its faces in order (faces that make no polygon too), and an unedited face its three points
// and texture numbers exactly as read, so a map opened and saved unchanged writes the same values.
// func_group entities are the groups, as in Radiant and TrenchBroom.
//
// Objects (entities, brushes, curves) share one id pool, and an object is never changed in place:
// an edit stores a new one under its id. An undo step keeps the before and after of each object it
// touched, so a step costs what it changed, not the size of the map.
//
// Space is id Tech 3's (+X forward, +Y left, +Z up, game units) in doubles; the geometry is
// brush_kernel's.

#include "brush_kernel.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace oz::ed {

using q3map::Plane;
using q3map::Vec3;

// Shader names ("base_wall/concrete", as a .map writes them: no "textures/") interned to small ids.
using ShaderId = int32_t;
ShaderId shader_id(const std::string &name);
const std::string &shader_name(ShaderId id);

// The form a face's texture numbers were written in.
enum class TexForm : uint8_t {
	Quake,       // shift s, shift t, rotate, scale s, scale t on q3map's default axes (Radiant's "AP")
	Primitives,  // brushDef: a 2x3 matrix on q3map's ComputeAxisBase axes, in texture repeats
	Valve,       // Valve 220: [ U offset ] [ V offset ] rotate scale s scale t, in texels
};

// A face's mapping as the renderer needs it: texture coordinates s = S . p + s0, t = T . p + t0, in
// repeats of the texture (what q3map writes into the BSP), for points p on the face's plane.
struct StAxes {
	Vec3 s, t;
	double s0 = 0, t0 = 0;
};

struct Face {
	Plane plane;
	ShaderId shader = 0;
	StAxes st;               // derived from raw (or set by an edit)
	TexForm form = TexForm::Quake;
	// The numbers as read or last set, in form: Quake 5, Primitives 6, Valve 11
	// (Ux Uy Uz offset Vx Vy Vz offset rotate scale_s scale_t). raw_valid false: st is the truth.
	double raw[11] = {0, 0, 0, 0.5, 0.5};
	bool raw_valid = true;
	// The file's three plane points, kept while the face isn't moved, so it writes back exactly.
	Vec3 pts[3];
	bool pts_valid = false;
	bool lock = false;       // texture lock: the mapping moves with the brush
	bool has_flags = false;  // the line carried contents, surface flags and value
	int32_t contents = 0, flags = 0, value = 0;
};

struct Brush {
	std::vector<Face> faces;
};

struct Patch {
	ShaderId shader = 0;
	bool def3 = false;           // patchDef3 (explicit subdivisions)
	double info[7] = {};         // the size row as read: width height, then 3 (patchDef2) or 5 more
	int width = 0, height = 0;   // control points, rows of width
	std::vector<double> ctrl;    // x y z s t per control point, row by row
};

using Keys = std::vector<std::pair<std::string, std::string>>;

struct Entity {
	Keys keys;  // in the file's order, classname included
	const std::string *find(const std::string &key) const;
	std::string get(const std::string &key, const std::string &fallback = "") const;
	void set(const std::string &key, const std::string &value);  // the first match, else appended
	bool erase(const std::string &key);
	std::string classname() const { return get("classname"); }
};

enum class Kind : uint8_t { Entity, Brush, Patch };

using Id = int32_t;

// A stored object. Immutable once stored: edits copy (Doc::edit) and put the copy back.
struct Object {
	Kind kind = Kind::Brush;
	Id owner = 0;  // a brush or patch: its entity (worldspawn, a func_group, a door ...)
	Entity entity;
	Brush brush;
	Patch patch;
};
using ObjectPtr = std::shared_ptr<const Object>;

class Doc {
public:
	// Texture size in texels as q3map sees it (its editor image, else the first stage's), for the
	// forms that count in texels. Unknown: 64 x 64, as q3map's default image.
	using TexSize = std::function<void(ShaderId, int &w, int &h)>;
	TexSize tex_size;

	// ---- objects
	const Object *get(Id id) const;
	ObjectPtr ptr(Id id) const;
	// A private copy to change and put() back.
	std::shared_ptr<Object> edit(Id id) const;
	Id add(std::shared_ptr<Object> obj);  // a new id at the end of the file order (in a session, the lease's next)
	void put(Id id, std::shared_ptr<Object> obj);
	void remove(Id id);
	// ids in file order (the ids are handed out in it)
	std::vector<Id> ids() const;
	std::vector<Id> ids_of(Kind kind) const;
	std::vector<Id> owned(Id entity) const;  // its brushes and curves, in file order
	Id worldspawn() const { return world_; }
	size_t size() const { return objects_.size(); }

	// ---- edit steps (undo). Changes outside a step are allowed only while loading.
	void begin(const std::string &label);
	void end();  // an empty step leaves no undo entry
	bool in_step() const { return depth_ > 0; }
	bool can_undo() const { return !undo_.empty(); }
	bool can_redo() const { return !redo_.empty(); }
	size_t undo_size() const { return undo_.size(); }
	size_t redo_size() const { return redo_.size(); }
	std::string undo_label() const;
	std::string redo_label() const;
	// The steps' labels, oldest first (undo) and next first (redo), for the history panel.
	std::vector<std::string> undo_labels() const;
	std::vector<std::string> redo_labels() const;
	bool undo();
	bool redo();
	void clear_history();
	size_t history_limit = 200;
	// and by size: the oldest steps go once the steps' objects come to more than this (an estimate: each step's
	// before and after objects, counted as if none were shared). Two hundred moves of a whole large map were
	// gigabytes. The last step always stays.
	size_t history_bytes = size_t(256) << 20;
	// The objects alone, as a document of its own: no undo history, no hooks, no lease (what the compile
	// copies to add its sky box to: copying the history too was most of that copy)
	Doc copy_objects() const;

	// Every change (an edit, undo or redo) is announced with the ids it touched; empty: everything.
	std::function<void(const std::vector<Id> &)> on_change;
	// The editor's selection, kept with each step: undo puts back the one the step began with, redo
	// the one it had when it was undone.
	std::function<std::vector<Id>()> get_selection;
	std::function<void(const std::vector<Id> &)> set_selection;

	bool dirty = false;
	uint64_t revision = 0;  // bumps on every change
	// The form plain brushes are written in: Valve when the file is "mapversion" "220", else Quake.
	// A brush whose faces were all read as brushDef is written as brushDef.
	TexForm plain_form = TexForm::Valve;

	// ---- faces
	// Where q3map evaluates a brush entity's mappings from (AdjustBrushesForOrigin): the middle of its
	// origin brush in whole units, cut toward 0 as q3map writes it, else its "origin" key. Zero for
	// worldspawn and func_group, which q3map merges into the world.
	Vec3 model_origin(Id entity) const;
	// Recomputes st from raw (after loading, or when a texture's size becomes known). origin: its
	// entity's model_origin.
	void derive_st(Face &f, const Vec3 &origin) const;
	// Sets raw in form from st (after an edit that changed the mapping or moved the face).
	void derive_raw(Face &f, TexForm form, const Vec3 &origin, const q3map::Winding &w) const;
	// The texture's size in texels as tex_size gives it (64 x 64 when unknown).
	void tex_dims(ShaderId shader, int &w, int &h) const;
	// The face's three points for writing: the file's while valid, else from its polygon.
	void plane_points(const Face &f, const q3map::Winding &w, Vec3 out[3]) const;

	// Face polygons of a brush, cached until it changes.
	const std::vector<q3map::Winding> &windings(Id id) const;

	// ---- editing together (ed_sync.h, ENGINE_PLAN.md PE). None of it is used while editing alone.
	struct Change {
		Id id;
		ObjectPtr before, after;  // null: not there (made, or removed)
	};
	// Every change made here, as it lands: a step's end, an undo or redo, a change outside a step;
	// label is the step's ("Undo Move" for an undo of Move). A session sends it to the others. A copy of
	// the document (the compile's, with its sky box) never has it: its changes are its own.
	struct LocalHook {
		std::function<void(const std::string &label, const std::vector<Change> &)> f;
		LocalHook() = default;
		LocalHook(const LocalHook &) {}
		LocalHook &operator=(const LocalHook &) { return *this; }  // assigned a document, this one's hook stays
		LocalHook &operator=(std::function<void(const std::string &, const std::vector<Change> &)> fn) {
			f = std::move(fn);
			return *this;
		}
		LocalHook &operator=(std::nullptr_t) {
			f = nullptr;
			return *this;
		}
		explicit operator bool() const { return bool(f); }
		void operator()(const std::string &label, const std::vector<Change> &c) const { f(label, c); }
	} on_local;
	// Another player's objects (null: removed), as the session's host ordered them: no undo step, the
	// history kept; announced through on_change.
	void apply_remote(const std::vector<std::pair<Id, ObjectPtr>> &objects);
	// The document replaced by a session's snapshot: these objects, the first entity the world. No history.
	void reset(Id world, const std::vector<std::pair<Id, ObjectPtr>> &objects);
	// New ids from a block the host handed out (lease_next .. lease_end), so no two players make the same
	// id; without one, the next past every id there is (as alone). lease_left: how many are left.
	void lease(Id from, Id end);
	Id lease_left() const {
		return (lease_end_ > lease_next_ ? lease_end_ - lease_next_ : 0) + (spare_end_ > spare_next_ ? spare_end_ - spare_next_ : 0);
	}
	bool lease_spare() const { return spare_end_ > spare_next_; }
	// The blocks held, to put back as they were (a session's map taken from its host again: the ids used stay used)
	struct Lease {
		Id next = 0, end = 0, spare_next = 0, spare_end = 0;
	};
	Lease lease_now() const {
		Lease l;
		l.next = lease_next_, l.end = lease_end_, l.spare_next = spare_next_, l.spare_end = spare_end_;
		return l;
	}
	void lease_put(const Lease &l) {
		lease_next_ = l.next, lease_end_ = l.end, spare_next_ = l.spare_next, spare_end_ = l.spare_end;
	}
	bool leasing() const { return lease_end_ != 0; }
	// In a session undo and redo are each player's own: they put back only the objects still as the step
	// left them (someone else changed the rest since); skipped counts the last one's left alone.
	bool shared = false;
	size_t skipped = 0;
	Id next_id() const { return next_; }
	bool stepping(Id id) const { return depth_ > 0 && open_index_.count(id) != 0; }  // changed in the open step

private:
	struct Step {
		std::string label;
		std::vector<Change> changes;
		std::vector<Id> sel_before, sel_after;
		size_t bytes = 0;  // its objects' size, about (history_bytes)
	};
	void record(Id id, ObjectPtr before, ObjectPtr after);
	void apply(const Step &s, bool forward);
	// shared: only what's still as the step left it (forward: as before it), returned as the changes made
	std::vector<Change> apply_shared(const Step &s, bool forward);
	void announce(std::vector<Id> ids);
	void local(const std::string &label, const std::vector<Change> &changes);

	std::unordered_map<Id, ObjectPtr> objects_;
	Id next_ = 1;
	Id lease_next_ = 0, lease_end_ = 0, spare_next_ = 0, spare_end_ = 0;
	Id world_ = 0;
	int depth_ = 0;
	Step open_;
	std::unordered_map<Id, size_t> open_index_;
	std::vector<Step> undo_, redo_;
	size_t undo_bytes_ = 0;  // the undo steps' bytes together
	mutable std::unordered_map<Id, std::pair<ObjectPtr, std::vector<q3map::Winding>>> windings_;
	// owned()'s answers: every entity's brushes and curves, made once per revision (every change to objects_
	// bumps revision: record, apply, apply_shared, apply_remote, reset). owned() scanned and sorted every
	// object at each call, inside loops over the entities (a save, a delete, a paste).
	mutable std::unordered_map<Id, std::vector<Id>> owned_index_;
	mutable uint64_t owned_rev_ = ~0ull;

	friend struct MapReader;
};

// The plane through three points as q3map's MapPlaneFromPoints: normal (p0 - p1) x (p2 - p1).
// Points in a line give a zero normal (a plane that cuts nothing).
Plane plane_from_points(const Vec3 &p0, const Vec3 &p1, const Vec3 &p2);

}  // namespace oz::ed
