// The map editor's picking, selection and select tool (ENGINE_PLAN.md P13, engine/EDITOR.md), after
// the Godot build's q3_select_tool.gd and q3_vertex_tool.gd:
//   Click selects (a brush of a group or a brush entity takes the whole entity; a double-click or
//   Ctrl reaches inside), Shift adds, Ctrl toggles; a click again on the same spot takes the next
//   object behind. Dragging a brush moves the selection on the grid (Alt up and down, Ctrl held
//   during the drag: off the grid); dragging on empty space, or Ctrl+dragging anywhere, draws a box
//   that selects what's wholly inside it (Shift adds, Ctrl toggles).
//   Selected brushes are editable in place: a square on every corner, a dot on every edge and a
//   diamond on a stem out of every face turned to the camera. Dragging a corner or an edge moves it
//   (the brush is rebuilt as the hull of its corners), dragging a diamond pushes or pulls its face,
//   and Shift+drag on any face pushes or pulls it directly.

#include "ed_view.h"
#include "ed_engine.h"
#include "ed_entdefs.h"
#include "ed_shaders.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>

extern "C" {
#include "../ui/oz_ui_engine.h"
#include "../../../quake3e/code/client/keycodes.h"
}

namespace oz::ed {

static const int MAX_CORNERS = 512;          // handles show for selections up to this many corners
static const double PICK_CORNER = 10;        // pixels
static const double PICK_EDGE = 7;
static const double PICK_FACE = 10;
static const double STEM_PICK = 8;
static const double DIAMOND_OUT = 28;        // how far a diamond floats out of its face, on screen
static const double DRAG_START = 4;          // pixels a press moves before it's a drag
static const int DOUBLE_CLICK_MS = 400;

static const unsigned char C_CORNER[4] = {50, 230, 255, 255}, C_CORNER_SEL[4] = {255, 90, 50, 255},
	C_HOVER[4] = {255, 255, 255, 255}, C_EDGE[4] = {50, 230, 255, 200}, C_FACE[4] = {80, 255, 130, 255},
	C_FACE_HOVER[4] = {255, 210, 64, 255}, C_BLACK[4] = {0, 0, 0, 230}, C_OUTLINE[4] = {255, 240, 220, 200},
	C_OUTLINE_HIDDEN[4] = {255, 240, 220, 40};
static const unsigned char C_CLIP[3][4] = {{90, 255, 120, 255}, {90, 170, 255, 255}, {255, 200, 60, 255}};

int Session::mods() const {
	if (forced_mods_ >= 0) {
		return forced_mods_;
	}
	return (OzE_KeyDown(K_SHIFT) ? SHIFT : 0) | (OzE_KeyDown(K_CTRL) ? CTRL : 0) | (OzE_KeyDown(K_ALT) ? ALT : 0);
}

// ---------------------------------------------------------------- picking

std::vector<Hit> Session::pick_all(double sx, double sy, bool entities) const {
	Vec3 o, d;
	ray(sx, sy, o, d);
	std::vector<Hit> hits;
	for (Id id : doc.ids()) {
		if (!pickable(id)) {
			continue;  // hidden, filtered out or locked: clicks pass through
		}
		const Object *ob = doc.get(id);
		Hit h;
		h.id = id;
		h.kind = ob->kind;
		if (ob->kind == Kind::Brush) {
			if (!ray_faces(ob->brush, o, d, h.t, h.face)) {
				continue;
			}
			h.normal = ob->brush.faces[size_t(h.face)].plane.n;
		} else if (ob->kind == Kind::Patch) {
			auto &c = patch_cache_[id];
			if (c.first.get() != ob) {
				c.first = doc.ptr(id);
				c.second = patch_mesh(ob->patch, 4);
			}
			const q3map::PatchMesh &m = c.second;
			double best = 1e300;
			for (int r = 0; r + 1 < m.height; r++) {
				for (int k = 0; k + 1 < m.width; k++) {
					const Vec3 &a = m.verts[size_t(r * m.width + k)].p, &b = m.verts[size_t(r * m.width + k + 1)].p;
					const Vec3 &e = m.verts[size_t((r + 1) * m.width + k)].p, &f = m.verts[size_t((r + 1) * m.width + k + 1)].p;
					double t;
					if (ray_triangle(o, d, a, e, b, t) && t < best) {
						best = t, h.normal = (e - a).cross(b - a).normalized();
					}
					if (ray_triangle(o, d, b, e, f, t) && t < best) {
						best = t, h.normal = (e - b).cross(f - b).normalized();
					}
				}
			}
			if (best == 1e300) {
				continue;
			}
			h.t = best;
			if (h.normal.dot(d) > 0) {
				h.normal = -h.normal;  // the side the ray met
			}
		} else {
			Vec3 lo, hi;
			if (!entities || !point_box(*ob, lo, hi) || !ray_box(o, d, lo, hi, h.t, h.normal)) {
				continue;
			}
		}
		h.point = o + d * h.t;
		hits.push_back(h);
	}
	std::sort(hits.begin(), hits.end(), [](const Hit &a, const Hit &b) { return a.t < b.t; });
	return hits;
}

bool Session::pick(double sx, double sy, Hit &hit, bool entities) const {
	std::vector<Hit> hits = pick_all(sx, sy, entities);
	if (hits.empty()) {
		return false;
	}
	hit = hits[0];
	return true;
}

bool Session::pick_floor(double sx, double sy, Hit &hit, bool entities) const {
	for (const Hit &h : pick_all(sx, sy, entities)) {
		const Object *o = doc.get(h.id);
		if (!o) {
			continue;
		}
		if (o->kind == Kind::Brush) {
			const Object *owner = doc.get(o->owner);
			if (owner && owner->entity.classname().compare(0, 8, "trigger_") == 0) {
				continue;
			}
			if (h.face >= 0 && h.face < int(o->brush.faces.size())) {
				const std::string sh = shader_name(o->brush.faces[size_t(h.face)].shader);
				const Look l = shaders_lib().look(sh);
				if (l == Look::Trigger || l == Look::Hint || l == Look::Fog || l == Look::Origin || l == Look::Portal || is_nocollide(*o)) {
					continue;
				}
			}
		}
		hit = h;
		return true;
	}
	return false;
}

// ---------------------------------------------------------------- the selection

std::vector<Id> Session::group_of(Id id) const {
	const Object *o = doc.get(id);
	if (!o) {
		return {};
	}
	const Id ent = o->kind == Kind::Entity ? id : o->owner;
	if (ent == doc.worldspawn() || !doc.get(ent)) {
		return {id};
	}
	std::vector<Id> out = doc.owned(ent);
	out.push_back(ent);
	// a door's other halves (its team), and its own zone trigger, go with it
	const Entity &me = doc.get(ent)->entity;
	if (me.classname() == "func_door") {
		const std::string team = me.get("team"), zone = me.get("_oz_door");
		for (Id e : doc.ids_of(Kind::Entity)) {
			const Entity &en = doc.get(e)->entity;
			if (e != ent && ((!team.empty() && en.classname() == "func_door" && en.get("team") == team) ||
					(!zone.empty() && en.classname() == "trigger_multiple" && en.get("_oz_door") == zone))) {
				for (Id b : doc.owned(e)) {
					out.push_back(b);
				}
				out.push_back(e);
			}
		}
	}
	// a jump pad's pieces (its trigger, its target, an older pad's block group) go together
	const std::string tag = doc.get(ent)->entity.get(JUMPPAD_KEY);
	if (!tag.empty()) {
		for (Id e : doc.ids_of(Kind::Entity)) {
			if (e != ent && doc.get(e)->entity.get(JUMPPAD_KEY) == tag) {
				for (Id b : doc.owned(e)) {
					out.push_back(b);
				}
				out.push_back(e);
			}
		}
	}
	return out;
}

void Session::select(const std::vector<Id> &ids, SelMode mode) {
	std::set<Id> next = mode == SelMode::Set ? std::set<Id>() : sel;
	for (Id id : ids) {
		if (!doc.get(id) || id == doc.worldspawn() || (mode != SelMode::Remove && !pickable(id))) {
			continue;  // what's hidden or locked isn't selected
		}
		if (mode == SelMode::Remove || (mode == SelMode::Toggle && sel.count(id))) {
			next.erase(id);
		} else {
			next.insert(id);
		}
	}
	if (next == sel) {
		return;
	}
	std::vector<Id> diff;
	std::set_symmetric_difference(sel.begin(), sel.end(), next.begin(), next.end(), std::back_inserter(diff));
	sel = std::move(next);
	sel_rev++;
	for (auto it = corners.begin(); it != corners.end();) {
		it = sel.count(it->first) ? std::next(it) : corners.erase(it);
	}
	changed(diff);
}

void Session::select_none() {
	select({}, SelMode::Set);
}

Bounds Session::selection_bounds() const {
	return bounds_of(doc, selected());
}

// ---------------------------------------------------------------- handles

const Session::Shape &Session::shape(Id id) {
	ObjectPtr p = doc.ptr(id);
	Shape &s = shapes_[id];
	if (s.of == p && p) {
		return s;
	}
	s = Shape();
	s.of = p;
	if (!p || p->kind != Kind::Brush) {
		return s;
	}
	s.corners = object_points(doc, id);
	const auto &ws = doc.windings(id);
	auto index_of = [&](const Vec3 &v) {
		for (size_t i = 0; i < s.corners.size(); i++) {
			if ((s.corners[i] - v).length() < 1e-3) {
				return int(i);
			}
		}
		return -1;
	};
	std::set<std::pair<int, int>> edges;
	s.centers.assign(p->brush.faces.size(), Vec3());
	for (size_t k = 0; k < ws.size(); k++) {
		const Winding &w = ws[k];
		if (w.size() < 3) {
			continue;
		}
		s.centers[k] = q3map::winding_center(w);
		s.faces.push_back(int(k));
		for (size_t i = 0; i < w.size(); i++) {
			int a = index_of(w[i]), b = index_of(w[(i + 1) % w.size()]);
			if (a >= 0 && b >= 0 && a != b) {
				edges.insert({std::min(a, b), std::max(a, b)});
			}
		}
	}
	s.edges.assign(edges.begin(), edges.end());
	return s;
}

std::vector<Handle> Session::handles() {
	std::vector<Handle> out;
	if (tool == Tool::Clip) {
		for (size_t i = 0; i < clip_pts_.size(); i++) {
			Handle h;
			h.kind = Handle::ClipPoint;
			h.a = int(i);
			h.at = clip_pts_[i];
			out.push_back(h);
		}
		return out;
	}
	if (tool != Tool::Select || sel.empty()) {
		return out;
	}
	// a selected jump pad's landing: a handle where its throw comes down, dragged to re-aim it
	{
		std::set<Id> pads;
		for (Id id : sel) {
			const Object *o = doc.get(id);
			if (!o || o->kind != Kind::Entity) {
				continue;
			}
			if (o->entity.classname() == "trigger_push") {
				pads.insert(id);
			} else if (const std::string *n = o->entity.find("targetname")) {
				for (Id p : pushers_at(*n)) {
					if (doc.get(p)->entity.classname() == "trigger_push") {
						pads.insert(p);
					}
				}
			}
		}
		for (Id p : pads) {
			const std::vector<Vec3> arc = push_arc(p);
			if (arc.size() >= 2 && push_lands(p)) {
				Handle h;
				h.kind = Handle::Land;
				h.id = p;
				h.at = arc.back();
				out.push_back(h);
			}
		}
	}
	if (group_handles()) {
		// a diamond on each side of the group's box that faces the camera, as a brush's face has
		const Bounds b = selection_bounds();
		const Vec3 c = b.center();
		const double lo3[3] = {b.lo.x, b.lo.y, b.lo.z}, hi3[3] = {b.hi.x, b.hi.y, b.hi.z};
		for (int axis = 0; axis < 3; axis++) {
			for (int hi = 0; hi < 2; hi++) {
				Vec3 n, base = c;
				double *nv[3] = {&n.x, &n.y, &n.z}, *bv[3] = {&base.x, &base.y, &base.z};
				*nv[axis] = hi ? 1 : -1;
				*bv[axis] = hi ? hi3[axis] : lo3[axis];
				if (ortho_axis_ >= 0 ? std::fabs(n.dot(fwd_)) > 0.95 : n.dot(base - eye_) >= 0) {
					continue;  // turned away (a 2D view: facing it, it can't push)
				}
				Handle h;
				h.kind = Handle::GroupFace;
				h.a = axis * 2 + hi;
				h.base = base;
				h.normal = n;
				h.at = base + n * (units_per_pixel(base) * DIAMOND_OUT);
				out.push_back(h);
			}
		}
		return out;
	}
	size_t count = 0;
	for (Id id : sel) {
		const Object *o = doc.get(id);
		if (o && (o->kind == Kind::Brush || o->kind == Kind::Patch)) {
			count += o->kind == Kind::Brush ? shape(id).corners.size() : o->patch.ctrl.size() / 5;
			if (count > size_t(MAX_CORNERS)) {
				return out;  // a big selection would be all squares (a pad's landing stays)
			}
		}
	}
	for (Id id : sel) {
		const Object *o = doc.get(id);
		if (o && o->kind == Kind::Patch) {
			// a curve's control points: dragged, the curve follows them
			for (size_t i = 0; i + 4 < o->patch.ctrl.size(); i += 5) {
				Handle h;
				h.kind = Handle::Corner;
				h.id = id;
				h.a = int(i / 5);
				h.at = Vec3(o->patch.ctrl[i], o->patch.ctrl[i + 1], o->patch.ctrl[i + 2]);
				out.push_back(h);
			}
			continue;
		}
		if (!o || o->kind != Kind::Brush) {
			continue;
		}
		const Shape &s = shape(id);
		for (size_t i = 0; i < s.corners.size(); i++) {
			Handle h;
			h.kind = Handle::Corner;
			h.id = id;
			h.a = int(i);
			h.at = s.corners[i];
			out.push_back(h);
		}
		for (const auto &e : s.edges) {
			Handle h;
			h.kind = Handle::Edge;
			h.id = id;
			h.a = e.first, h.b = e.second;
			h.at = (s.corners[size_t(e.first)] + s.corners[size_t(e.second)]) * 0.5;
			out.push_back(h);
		}
		for (int f : s.faces) {
			const Vec3 &c = s.centers[size_t(f)];
			const Vec3 &n = o->brush.faces[size_t(f)].plane.n;
			if (ortho_axis_ >= 0 ? std::fabs(n.dot(fwd_)) > 0.95 : n.dot(c - eye_) >= 0) {
				continue;  // turned away: its diamond would sit behind the brush (a 2D view: facing it, it can't push)
			}
			Handle h;
			h.kind = Handle::Face;
			h.id = id;
			h.a = f;
			h.base = c;
			h.normal = n;
			h.at = c + n * (units_per_pixel(c) * DIAMOND_OUT);
			out.push_back(h);
		}
	}
	return out;
}

bool Session::group_handles() const {
	int n = 0;
	for (Id id : sel) {
		const Object *o = doc.get(id);
		n += o && o->kind == Kind::Brush;
	}
	return n >= 2;
}

static double seg_dist(double px, double py, double ax, double ay, double bx, double by) {
	double dx = bx - ax, dy = by - ay;
	double l = dx * dx + dy * dy;
	double t = l > 0 ? std::clamp(((px - ax) * dx + (py - ay) * dy) / l, 0.0, 1.0) : 0;
	return std::hypot(px - (ax + dx * t), py - (ay + dy * t));
}

Handle Session::handle_at(double sx, double sy) {
	Handle best;
	double best_d = 1e300, best_depth = 1e300;
	auto consider = [&](const Handle &h, double d, double reach) {
		if (d > reach) {
			return;
		}
		double depth = (h.at - eye_).length();
		if (d < best_d - 0.5 || (std::fabs(d - best_d) <= 0.5 && depth < best_depth)) {
			best = h, best_d = d, best_depth = depth;
		}
	};
	std::vector<Handle> hs = handles();
	if (hs.empty()) {
		return best;
	}
	// a corner or an edge behind the face under the pointer isn't grabbed through it
	Hit front;
	const double reach = pick(sx, sy, front) && ortho_axis_ < 0 ? (front.point - eye_).length() : 1e300;  // a 2D view sees every corner
	// corners and clip points win over edges, edges over diamonds
	for (int pass = 0; pass < 3 && best.kind == Handle::None; pass++) {
		for (const Handle &h : hs) {
			const bool mine = pass == 0 ? (h.kind == Handle::Corner || h.kind == Handle::ClipPoint || h.kind == Handle::Land)
				: pass == 1 ? h.kind == Handle::Edge : h.kind == Handle::Face || h.kind == Handle::GroupFace;
			double x, y;
			if (!mine || !project(h.at, x, y)) {
				continue;
			}
			if ((h.kind == Handle::Corner || h.kind == Handle::Edge) && (h.at - eye_).length() > reach + units_per_pixel(h.at) * 12) {
				continue;
			}
			double d = std::hypot(x - sx, y - sy);
			if (h.kind == Handle::Face || h.kind == Handle::GroupFace) {
				double bx, by;
				if (project(h.base, bx, by)) {
					// the stem's outer half grabs too; its inner half is the face's, which moves the brush
					double s = seg_dist(sx, sy, (bx + x) * 0.5, (by + y) * 0.5, x, y);
					if (s <= STEM_PICK) {
						d = std::min(d, s);
					}
				}
			}
			consider(h, d, h.kind == Handle::Land ? PICK_CORNER + 4 : pass == 0 ? PICK_CORNER : pass == 1 ? PICK_EDGE : PICK_FACE);
		}
	}
	return best;
}

// ---------------------------------------------------------------- the pointer

void Session::pointer(int x, int y) {
	mx_ = x, my_ = y;
	// the pane under the pointer is the one the tools work in; a drag stays in the one it began in
	Pane p;
	if (drag_ == Drag::None && !looking_ && create_state_ == 0 && pane_at(x, y, p) && p != active_) {
		active_ = p;
		use_pane(p);
	}
	over_ = x >= vx_ && y >= vy_ && x < vx_ + vw_ && y < vy_ + vh_;
	if (drag_ != Drag::None) {
		motion(mods());
	} else if (tool == Tool::Create && create_state_ == 2) {
		create_motion();
	}
}

void Session::button(int b, bool down) {
	const int m = mods();
	if (b == 3) {
		if (down && drag_ == Drag::None) {
			start_pan(3);
		} else if (!down && drag_ == Drag::Pan) {
			drag_ = Drag::None;
			press_button_ = 0;
		}
		return;
	}
	if (down) {
		if (drag_ == Drag::None) {
			press_button_ = 1;
			press(m);
		}
	} else if (press_button_ == 1) {
		release(m);
		press_button_ = 0;
	}
}

void Session::press(int m) {
	press_x_ = mx_, press_y_ = my_, press_mods_ = m;
	press_handle_ = handle_at(mx_, my_);
	press_hit_ok_ = pick(mx_, my_, press_hit_);
	drag_ = Drag::Pending;
	if (tool == Tool::Create && create_state_ == 0) {
		create_press(m);
	}
	if (tool == Tool::Clip) {
		clip_drag_ = press_handle_.kind == Handle::ClipPoint ? press_handle_.a : -1;
	}
}

void Session::motion(int m) {
	if (drag_ == Drag::Pending) {
		if (std::hypot(mx_ - press_x_, my_ - press_y_) < DRAG_START) {
			return;
		}
		// the press becomes a drag: what it does depends on what it grabbed
		const Handle &h = press_handle_;
		if (tool == Tool::Faces) {
			if (!faces_drag()) {
				return;  // nothing to push: a click when let go
			}
		} else if (tool == Tool::Place) {
			return;  // a click when let go
		} else if (tool == Tool::Create) {
			drag_ = create_state_ == 1 ? Drag::Footprint : Drag::None;
		} else if (tool == Tool::Clip && clip_drag_ >= 0) {
			drag_ = Drag::ClipPoint;
		} else if (tool == Tool::Select && h.kind == Handle::Land) {
			start_land_drag(h.id);
		} else if (tool == Tool::Select && (h.kind == Handle::Corner || h.kind == Handle::Edge)) {
			start_corners(h, press_mods_);
		} else if (tool == Tool::Select && h.kind == Handle::GroupFace) {
			start_group_push(h);
		} else if (tool == Tool::Select && h.kind == Handle::Face) {
			// grab where the press ray passes the face's axis, so the face doesn't jump to the cursor
			start_push({{h.id, h.a}}, h.base, h.normal, "Push face");
		} else if (press_hit_ok_ && (press_mods_ & SHIFT) && press_hit_.kind == Kind::Brush && tool == Tool::Select) {
			if (!sel.count(press_hit_.id)) {
				select({press_hit_.id}, SelMode::Add);
			}
			start_push({{press_hit_.id, press_hit_.face}}, press_hit_.point, press_hit_.normal, "Push face");
		} else if (press_hit_ok_ && tool == Tool::Select && (press_mods_ & CTRL) && sel.count(press_hit_.id)) {
			// Ctrl drag from the selection: copies of it, dragged off the originals
			std::vector<Id> made = oz::ed::paste(doc, copy_of(doc, selected()), Vec3(), "Duplicate");
			corners.clear();
			select(made, SelMode::Set);
			// start_move would select the brush under the pointer again: the original
			drag_ = Drag::Move;
			base_ = delta_ = Vec3();
			min_ = selection_bounds().lo;
			anchor_ = press_hit_.point;
			move_plane(false);
			copy_move_ = true;
		} else if (press_hit_ok_ && tool == Tool::Select && !(press_mods_ & CTRL)) {
			start_move(press_hit_, press_mods_);
		} else {
			drag_ = Drag::Box;
		}
	}
	switch (drag_) {
	case Drag::Move: {
		const bool alt = (m & ALT) != 0;
		if (ortho_axis_ < 0 && alt != vertical_) {
			// switch planes through where the grab point is now, so the switch itself moves nothing
			Vec3 q = anchor_ + (delta_ - base_);
			base_ = delta_;
			move_plane(alt);
			Vec3 here;
			anchor_ = plane_point(mx_, my_, q, plane_n_, here) ? here : q;
		}
		Vec3 p;
		if (!plane_point(mx_, my_, anchor_, plane_n_, p)) {
			return;
		}
		Vec3 raw = p - anchor_;
		const bool on_grid = copy_move_ || !(m & CTRL);
		auto along = [&](double lo, double move) { return on_grid ? snap(lo + move) - lo : move; };
		Vec3 d = base_;
		if (ortho_axis_ >= 0) {
			// a 2D view: across and up it, its own axis left as it is
			if (ortho_axis_ != 0) {
				d.x = along(min_.x, base_.x + raw.x);
			}
			if (ortho_axis_ != 1) {
				d.y = along(min_.y, base_.y + raw.y);
			}
			if (ortho_axis_ != 2) {
				d.z = along(min_.z, base_.z + raw.z);
			}
		} else if (vertical_) {
			d.z = along(min_.z, base_.z + raw.z);
		} else {
			d.x = along(min_.x, base_.x + raw.x);
			d.y = along(min_.y, base_.y + raw.y);
		}
		delta_ = d;
		char buf[96];
		std::snprintf(buf, sizeof buf, "Move %g %g %g", d.x, d.y, d.z);
		drag_note_ = buf;
		break;
	}
	case Drag::Push: {
		Vec3 o, d;
		ray(mx_, my_, o, d);
		const Vec3 w = o - push_grab_;
		const double b = d.dot(push_n_), dd = d.dot(d), denom = dd - b * b;
		if (std::fabs(denom) < 1e-6) {
			return;
		}
		double s = (dd * push_n_.dot(w) - b * d.dot(w)) / denom;
		if (!(m & CTRL)) {
			s = push_grid_ ? snap(push_d0_ + s) - push_d0_ : snap(s);
		}
		if (s == push_dist_) {
			return;
		}
		std::map<Id, std::vector<int>> by;
		for (const auto &f : push_faces_) {
			by[f.first].push_back(f.second);
		}
		std::vector<std::pair<Id, std::shared_ptr<Object>>> out;
		for (auto &kv : by) {
			auto p = pushed(doc, *start_[kv.first], kv.second, s);
			if (!p) {
				return;  // too far: a face would pass through the brush
			}
			out.emplace_back(kv.first, p);
		}
		std::vector<Id> ids;
		for (auto &kv : out) {
			doc.put(kv.first, kv.second);
			ids.push_back(kv.first);
		}
		changed(ids);
		push_dist_ = s;
		char buf[64];
		std::snprintf(buf, sizeof buf, "Push %g", s);
		drag_note_ = buf;
		break;
	}
	case Drag::Corners: {
		const bool alt = (m & ALT) != 0 && ortho_axis_ < 0;  // a 2D view's drag stays in its plane
		if (alt != vertical_) {
			vertical_ = alt;
			Vec3 f = fwd_;
			if (vertical_) {
				f.z = 0;
			}
			const double ax[3] = {std::fabs(f.x), std::fabs(f.y), std::fabs(f.z)};
			const int axis = ax[0] >= ax[1] && ax[0] >= ax[2] ? 0 : ax[1] >= ax[2] ? 1 : 2;
			plane_n_ = Vec3(axis == 0, axis == 1, axis == 2);
		}
		Vec3 p;
		if (!plane_point(mx_, my_, corner_grab_, plane_n_, p)) {
			return;
		}
		Vec3 target = (m & CTRL) ? p : snap_vec(p);
		Vec3 d = target - corner_grab_;
		d = d - plane_n_ * d.dot(plane_n_);  // stay in the drag plane
		if (vertical_) {
			d = Vec3(0, 0, d.z);
		}
		if (d.x == delta_.x && d.y == delta_.y && d.z == delta_.z) {
			return;
		}
		drag_corners(d);
		break;
	}
	case Drag::Pan:
		pan_to(mx_, my_);
		break;
	case Drag::Footprint:
		create_motion();
		break;
	case Drag::Land:
		land_drag(m);
		break;
	case Drag::ClipPoint: {
		Hit h;
		if (clip_drag_ >= 0 && clip_drag_ < int(clip_pts_.size()) && pick(mx_, my_, h, false)) {
			const double n[3] = {h.normal.x, h.normal.y, h.normal.z};
			const int axis = std::fabs(n[0]) >= std::fabs(n[1]) && std::fabs(n[0]) >= std::fabs(n[2]) ? 0 : std::fabs(n[1]) >= std::fabs(n[2]) ? 1 : 2;
			Vec3 p = snap_vec(h.point);
			double *pc[3] = {&p.x, &p.y, &p.z};
			const double hp[3] = {h.point.x, h.point.y, h.point.z};
			*pc[axis] = hp[axis];
			clip_pts_[size_t(clip_drag_)] = p;
			clip_normals_[size_t(clip_drag_)] = h.normal;
		}
		break;
	}
	default:
		break;
	}
}

void Session::release(int m) {
	const Drag was = drag_;
	drag_ = Drag::None;
	drag_note_.clear();
	switch (was) {
	case Drag::Pending: {
		const int now = OzE_Milliseconds();
		const bool twice = now - last_click_ms_ < DOUBLE_CLICK_MS && std::hypot(mx_ - last_click_x_, my_ - last_click_y_) < DRAG_START;
		click(press_mods_, twice);
		last_click_ms_ = twice ? 0 : now;
		last_click_x_ = mx_, last_click_y_ = my_;
		break;
	}
	case Drag::Move:
		if (delta_.x != 0 || delta_.y != 0 || delta_.z != 0) {
			transform_selection(translation(delta_), "Move", texture_lock);
		} else if (copy_move_) {
			remove_selection();  // let go where it began: no copies lying inside the originals
		}
		copy_move_ = false;
		delta_ = Vec3();
		break;
	case Drag::Box:
		box_select(press_x_, press_y_, mx_, my_, press_mods_);
		break;
	case Drag::Land:
		end_land_drag(false);
		break;
	case Drag::Push:
		doc.end();
		start_.clear();
		corners.clear();  // the brush numbers its corners anew
		break;
	case Drag::Corners:
		doc.end();
		reselect_corners(delta_);
		start_.clear();
		delta_ = Vec3();
		break;
	case Drag::Footprint:
		create_motion();
		create_state_ = 2;
		{
			// a footprint without width along an axis takes one grid cell there
			const double cell = std::max(grid, 8.0);
			double *a[3] = {&create_a_.x, &create_a_.y, &create_a_.z}, *b[3] = {&create_b_.x, &create_b_.y, &create_b_.z};
			for (int i = 0; i < 3; i++) {
				if (i != create_axis_ && *a[i] == *b[i]) {
					*b[i] += cell;
				}
			}
		}
		if (ortho_axis_ >= 0) {
			create_commit();  // a 2D view's footprint has its depth already
		} else {
			create_motion();
		}
		break;
	default:
		break;
	}
	(void)m;
}

void Session::click(int m, bool twice) {
	if (tool == Tool::Create) {
		if (create_state_ == 2) {
			create_commit();
		} else if (create_state_ == 1) {
			// a click without a drag: one cell, then the height (a 2D view's: made)
			const double cell = std::max(grid, 8.0);
			double *b[3] = {&create_b_.x, &create_b_.y, &create_b_.z};
			for (int i = 0; i < 3; i++) {
				if (i != create_axis_) {
					*b[i] += cell;
				}
			}
			create_state_ = 2;
			if (ortho_axis_ >= 0) {
				create_commit();
			} else {
				create_motion();
			}
		}
		return;
	}
	if (tool == Tool::Clip) {
		clip_click(m);
		return;
	}
	if (tool == Tool::Faces) {
		faces_click(m, twice);
		return;
	}
	if (tool == Tool::Place) {
		place_click(m);
		return;
	}
	const Handle &h = press_handle_;
	if (h.kind == Handle::Corner || h.kind == Handle::Edge) {
		std::vector<std::pair<Id, int>> keys = {{h.id, h.a}};
		if (h.kind == Handle::Edge) {
			keys.push_back({h.id, h.b});
		}
		if (m & CTRL) {
			for (auto &k : keys) {
				if (corners.count(k)) {
					corners.erase(k);
				} else {
					corners.insert(k);
				}
			}
		} else {
			if (!(m & SHIFT)) {
				corners.clear();
			}
			corners.insert(keys.begin(), keys.end());
		}
		return;
	}
	if (h.kind == Handle::Face) {
		return;  // a diamond only drags
	}
	std::vector<Hit> hits = pick_all(mx_, my_);
	if (hits.empty()) {
		if (!m) {
			corners.clear();
			select_none();
		}
		cycle_ = 0;
		return;
	}
	// a plain click on the same spot again takes the next object behind
	const bool again = !m && !twice && std::hypot(mx_ - last_click_x_, my_ - last_click_y_) < 3 && cycle_ < int(hits.size()) &&
		sel.count(hits[size_t(cycle_)].id);
	cycle_ = again ? (cycle_ + 1) % int(hits.size()) : 0;
	const Hit &hit = hits[size_t(cycle_)];
	std::vector<Id> ids = twice || (m & ALT) ? std::vector<Id>{hit.id} : group_of(hit.id);
	if (!m) {
		corners.clear();
	}
	select(ids, (m & CTRL) ? SelMode::Toggle : (m & SHIFT) ? SelMode::Add : SelMode::Set);
}

void Session::cancel_drag() {
	switch (drag_) {
	case Drag::Land:
		drag_ = Drag::None;
		drag_note_.clear();
		end_land_drag(true);
		break;
	case Drag::Push:
	case Drag::Corners: {
		std::vector<Id> ids;
		for (auto &kv : start_) {
			doc.put(kv.first, std::const_pointer_cast<Object>(kv.second));
			ids.push_back(kv.first);
		}
		doc.end();  // back where it began: no undo entry
		changed(ids);
		start_.clear();
		break;
	}
	case Drag::Pan:
		pan_to(press_x_, press_y_);
		break;
	case Drag::Move:
		if (copy_move_) {
			drag_ = Drag::None;
			remove_selection();  // a Ctrl drag called off: its copies go too
		}
		break;
	default:
		break;
	}
	copy_move_ = false;
	drag_ = Drag::None;
	press_button_ = 0;
	delta_ = Vec3();
	drag_note_.clear();
}

// ---------------------------------------------------------------- the select tool's drags

void Session::move_plane(bool vertical) {
	vertical_ = vertical;
	if (ortho_axis_ >= 0) {
		vertical_ = false;
		plane_n_ = Vec3(ortho_axis_ == 0, ortho_axis_ == 1, ortho_axis_ == 2);  // the 2D view's plane
	} else if (vertical) {
		Vec3 f(fwd_.x, fwd_.y, 0);
		plane_n_ = f.length() > 1e-4 ? f.normalized() : Vec3(1, 0, 0);
	} else {
		plane_n_ = Vec3(0, 0, 1);
	}
}

void Session::start_move(const Hit &hit, int m) {
	if (!sel.count(hit.id)) {
		select(group_of(hit.id), (m & SHIFT) ? SelMode::Add : SelMode::Set);
	}
	drag_ = Drag::Move;
	base_ = Vec3();
	delta_ = Vec3();
	min_ = selection_bounds().lo;
	anchor_ = hit.point;
	move_plane((m & ALT) != 0);
}

void Session::start_push(const std::vector<std::pair<Id, int>> &faces, const Vec3 &grab, const Vec3 &n, const char *label) {
	push_faces_ = faces;
	push_grab_ = grab;
	push_n_ = n;
	start_.clear();
	for (const auto &f : faces) {
		start_[f.first] = doc.ptr(f.first);
	}
	push_grid_ = false;
	if (faces.size() == 1) {
		const double a = std::max({std::fabs(n.x), std::fabs(n.y), std::fabs(n.z)});
		push_grid_ = a == 1;  // one axial face: its plane lands on the grid, not the push
		push_d0_ = start_[faces[0].first]->brush.faces[size_t(faces[0].second)].plane.d;
	}
	push_dist_ = 0;
	doc.begin(label);
	drag_ = Drag::Push;
}

void Session::start_group_push(const Handle &h) {
	// each selected brush's face turned that way and on the outside of the group (the tops at the group's
	// top, for its top): all pushed as one. A face inside the group (a stack's lower tops) stays put
	const double outer = h.normal.dot(h.base);
	std::vector<std::pair<Id, int>> faces;
	double d0 = 0;
	bool one_plane = true;
	for (Id id : sel) {
		const Object *o = doc.get(id);
		if (!o || o->kind != Kind::Brush) {
			continue;
		}
		for (int f : shape(id).faces) {
			const Plane &p = o->brush.faces[size_t(f)].plane;
			if (p.n.dot(h.normal) > 0.999 && std::fabs(p.d - outer) < 0.5) {
				one_plane &= faces.empty() || std::fabs(p.d - d0) < 1e-6;
				d0 = p.d;
				faces.push_back({id, f});
			}
		}
	}
	if (faces.empty()) {
		drag_ = Drag::None;
		say("None of the selected brushes has a face that way to push");
		return;
	}
	start_push(faces, h.base, h.normal, faces.size() > 1 ? "Push faces" : "Push face");
	// one shared plane on the grid's axis: it lands on the grid, as one face's does
	push_grid_ = one_plane;
	push_d0_ = d0;
}

void Session::start_corners(const Handle &h, int m) {
	std::vector<std::pair<Id, int>> keys = {{h.id, h.a}};
	if (h.kind == Handle::Edge) {
		keys.push_back({h.id, h.b});
	}
	bool all = true;
	for (auto &k : keys) {
		all &= corners.count(k) != 0;
	}
	if (!all) {
		if (!(m & SHIFT)) {
			corners.clear();
		}
		corners.insert(keys.begin(), keys.end());
	}
	corner_pts_.clear();
	corner_moving_.clear();
	start_.clear();
	for (const auto &k : corners) {
		if (!corner_pts_.count(k.first)) {
			corner_pts_[k.first] = object_points(doc, k.first);
			start_[k.first] = doc.ptr(k.first);
		}
		corner_moving_[k.first].push_back(k.second);
	}
	corner_grab_ = h.at;
	vertical_ = !((m & ALT) != 0);  // motion() sets the plane on the first move
	delta_ = Vec3();
	doc.begin(h.kind == Handle::Edge ? "Move edge" : "Move corners");
	drag_ = Drag::Corners;
	char buf[64];
	std::snprintf(buf, sizeof buf, "Move 0 0 0");
	drag_note_ = buf;
}

void Session::drag_corners(const Vec3 &d) {
	std::vector<std::pair<Id, std::shared_ptr<Object>>> out;
	for (auto &kv : corner_pts_) {
		std::vector<Vec3> pts = kv.second;
		for (int i : corner_moving_[kv.first]) {
			if (i >= 0 && i < int(pts.size())) {
				pts[size_t(i)] = pts[size_t(i)] + d;
			}
		}
		const Object &o = *start_[kv.first];
		auto b = from_points(doc, o, pts, doc.model_origin(o.owner));
		if (!b) {
			return;  // the brush would flatten
		}
		out.emplace_back(kv.first, b);
	}
	std::vector<Id> ids;
	for (auto &kv : out) {
		doc.put(kv.first, kv.second);
		ids.push_back(kv.first);
	}
	changed(ids);
	delta_ = d;
	char buf[96];
	std::snprintf(buf, sizeof buf, "Move %g %g %g", d.x, d.y, d.z);
	drag_note_ = buf;
}

void Session::reselect_corners(const Vec3 &d) {
	// a rebuilt brush numbers its corners anew: find the moved ones again by where they went
	std::map<Id, std::vector<Vec3>> wanted;
	for (auto &kv : corner_moving_) {
		for (int i : kv.second) {
			if (i >= 0 && i < int(corner_pts_[kv.first].size())) {
				wanted[kv.first].push_back(corner_pts_[kv.first][size_t(i)] + d);
			}
		}
	}
	corners.clear();
	for (auto &kv : wanted) {
		const std::vector<Vec3> now = object_points(doc, kv.first);
		for (size_t i = 0; i < now.size(); i++) {
			for (const Vec3 &w : kv.second) {
				if ((now[i] - w).length() < 0.01) {
					corners.insert({kv.first, int(i)});
				}
			}
		}
	}
}

void Session::delete_corners() {
	std::map<Id, std::set<int>> by;
	for (const auto &k : corners) {
		by[k.first].insert(k.second);
	}
	std::vector<std::pair<Id, std::shared_ptr<Object>>> out;
	for (auto &kv : by) {
		const Object *o = doc.get(kv.first);
		if (!o || o->kind != Kind::Brush) {
			continue;
		}
		std::vector<Vec3> pts = object_points(doc, kv.first), keep;
		for (size_t i = 0; i < pts.size(); i++) {
			if (!kv.second.count(int(i))) {
				keep.push_back(pts[i]);
			}
		}
		auto b = keep.size() >= 4 ? from_points(doc, *o, keep, doc.model_origin(o->owner)) : nullptr;
		if (!b) {
			say("That would leave a flat brush", true);
			return;
		}
		out.emplace_back(kv.first, b);
	}
	if (out.empty()) {
		return;
	}
	doc.begin("Delete corners");
	for (auto &kv : out) {
		doc.put(kv.first, kv.second);
	}
	doc.end();
	corners.clear();
}

void Session::nudge(const Vec3 &d) {
	if (!corners.empty()) {
		// the selected corners, not the brushes
		Handle h;
		h.kind = Handle::Corner;
		h.id = corners.begin()->first;
		h.a = corners.begin()->second;
		const std::vector<Vec3> pts = object_points(doc, h.id);
		h.at = h.a < int(pts.size()) ? pts[size_t(h.a)] : Vec3();
		start_corners(h, SHIFT);
		drag_corners(d);
		const bool moved = delta_.x == d.x && delta_.y == d.y && delta_.z == d.z;
		drag_ = Drag::None;
		doc.end();
		if (moved) {
			reselect_corners(d);
		} else {
			say("That would flatten the brush", true);
		}
		start_.clear();
		delta_ = Vec3();
		drag_note_.clear();
		return;
	}
	transform_selection(translation(d), "Nudge", texture_lock);
}

void Session::box_select(int x0, int y0, int x1, int y1, int m) {
	const double lx = std::min(x0, x1), hx = std::max(x0, x1), ly = std::min(y0, y1), hy = std::max(y0, y1);
	// an object counts when at least half of what it covers on screen is inside the box: one sticking
	// out a little past the box's edge comes along, a floor the box only covers a corner of doesn't. A
	// corner behind the camera (the 3D view) doesn't count against it
	auto in_box = [&](const std::vector<Vec3> &pts) {
		double ax = 1e300, ay = 1e300, bx = -1e300, by = -1e300;
		int seen = 0;
		for (const Vec3 &p : pts) {
			double x, y;
			if (project(p, x, y)) {
				ax = std::min(ax, x), ay = std::min(ay, y), bx = std::max(bx, x), by = std::max(by, y);
				seen++;
			}
		}
		if (!seen) {
			return false;
		}
		ax -= 0.5, ay -= 0.5, bx += 0.5, by += 0.5;  // a brush seen edge on still has an area
		const double ox = std::min(bx, hx) - std::max(ax, lx), oy = std::min(by, hy) - std::max(ay, ly);
		if (ox <= 0 || oy <= 0) {
			return false;
		}
		return ox * oy >= 0.5 * (bx - ax) * (by - ay);
	};
	std::vector<Id> ids;
	std::set<Id> got;
	for (Id id : doc.ids()) {
		const Object *o = doc.get(id);
		if (id == doc.worldspawn() || !pickable(id)) {
			continue;
		}
		std::vector<Vec3> pts;
		if (o->kind == Kind::Brush) {
			for (const Winding &w : doc.windings(id)) {
				pts.insert(pts.end(), w.begin(), w.end());
			}
		} else if (o->kind == Kind::Patch) {
			pts = object_points(doc, id);
		} else {
			Vec3 lo, hi;
			if (point_box(*o, lo, hi)) {
				for (int c = 0; c < 8; c++) {
					pts.push_back(Vec3(c & 1 ? hi.x : lo.x, c & 2 ? hi.y : lo.y, c & 4 ? hi.z : lo.z));
				}
			}
		}
		if (!pts.empty() && in_box(pts)) {
			ids.push_back(id);
			got.insert(id);
		}
	}
	// a group or a brush entity all of whose pieces count comes along as itself
	std::set<Id> owners;
	for (Id id : ids) {
		const Object *o = doc.get(id);
		if (o->kind != Kind::Entity && o->owner != doc.worldspawn()) {
			owners.insert(o->owner);
		}
	}
	for (Id e : owners) {
		std::vector<Id> own = doc.owned(e);
		if (std::all_of(own.begin(), own.end(), [&](Id b) { return got.count(b) != 0; })) {
			ids.push_back(e);
		}
	}
	select(ids, (m & CTRL) ? SelMode::Toggle : (m & SHIFT) ? SelMode::Add : SelMode::Set);
}

// ---------------------------------------------------------------- drawing

void Session::build_entities() {
	collect_models();
	std::set<Id> modeled;
	for (const EntModel &m : models_) {
		modeled.insert(m.id);
	}
	ents_.begin();
	ents_sel_.begin();
	for (Id id : doc.ids_of(Kind::Entity)) {
		const Object *o = doc.get(id);
		Vec3 lo, hi;
		if (!visible(id) || !point_box(*o, lo, hi)) {
			continue;
		}
		unsigned char c[4];
		entity_color(o->entity.classname(), c);
		Vec3 lc;
		if (o->entity.classname() == "light" && parse_vec(o->entity.get("_color", "1 1 1"), lc)) {
			// a light in its own color
			const double m = std::max({lc.x, lc.y, lc.z, 1e-6});
			c[0] = (unsigned char)(255 * std::max(0.0, lc.x) / m), c[1] = (unsigned char)(255 * std::max(0.0, lc.y) / m),
			c[2] = (unsigned char)(255 * std::max(0.0, lc.z) / m);
		}
		const bool s = sel.count(id) != 0;
		Overlay &ov = s ? ents_sel_ : ents_;
		unsigned char fill[4] = {c[0], c[1], c[2], (unsigned char)(s ? 110 : 64)};
		static const unsigned char sel_edge[4] = {255, 120, 50, 255}, sel_hidden[4] = {255, 120, 50, 64};
		const bool model = modeled.count(id) != 0;
		if (!model || s) {
			// one-sided: a camera inside one (at a spawn) sees through it; a model shows itself, its box only when selected
			const unsigned char thin[4] = {c[0], c[1], c[2], 40};
			ov.box_faces(lo, hi, model ? thin : fill, "$oz_ed_tool");
		}
		const unsigned char faint[4] = {c[0], c[1], c[2], 90};
		ov.box_edges(lo, hi, s ? sel_edge : model ? faint : c);
		if (s) {
			ov.box_edges(lo, hi, sel_hidden, "$oz_ed_lines_xray");
		}
		// which way it faces: a line out of its middle
		const std::string *angle = o->entity.find("angle");
		Vec3 ang;
		double yaw = angle ? std::atof(angle->c_str()) : (parse_vec(o->entity.get("angles"), ang) ? ang.y : 1e9);
		if (yaw != 1e9 && !(angle && (*angle == "-1" || *angle == "-2"))) {
			const double r = yaw * 3.14159265358979323846 / 180;
			Vec3 mid = (lo + hi) * 0.5, dir(std::cos(r), std::sin(r), 0);
			double len = (hi.x - lo.x) * 0.5 + 16;
			Vec3 tip = mid + dir * len, side(-dir.y, dir.x, 0);
			ov.line(mid, tip, s ? sel_edge : c);
			ov.line(tip, tip - dir * 8 + side * 6, s ? sel_edge : c);
			ov.line(tip, tip - dir * 8 - side * 6, s ? sel_edge : c);
		}
	}
	ents_.end();
	ents_sel_.end();
}

void Session::build_hover() {
	const bool face = tool == Tool::Faces && hover_ok_ && hover_.kind == Kind::Brush && drag_ == Drag::None;
	const int64_t target = face ? -(int64_t(hover_.id) * 4096 + hover_.face + 2)  // the face under the pointer alone
		: hover_ok_ && drag_ == Drag::None && !sel.count(hover_.id) && (tool == Tool::Select || tool == Tool::Clip) ? hover_.id : 0;
	if (target == hover_built_ && doc.revision == hover_doc_rev_ && sel_rev == hover_sel_rev_ && int(tool) == hover_tool_) {
		return;
	}
	hover_built_ = target;
	hover_doc_rev_ = doc.revision;
	hover_sel_rev_ = sel_rev;
	hover_tool_ = int(tool);
	hover_lines_.begin();
	if (face) {
		const std::vector<Winding> &ws = doc.windings(hover_.id);
		if (size_t(hover_.face) < ws.size() && !faces.count({hover_.id, hover_.face})) {
			hover_lines_.outline(ws[size_t(hover_.face)], C_OUTLINE);
			hover_lines_.outline(ws[size_t(hover_.face)], C_OUTLINE_HIDDEN, "$oz_ed_lines_xray");
		}
	} else if (target) {
		for (Id id : group_of(Id(target))) {
			const Object *o = doc.get(id);
			if (!o) {
				continue;
			}
			if (o->kind == Kind::Brush) {
				for (const Winding &w : doc.windings(id)) {
					hover_lines_.outline(w, C_OUTLINE);
					hover_lines_.outline(w, C_OUTLINE_HIDDEN, "$oz_ed_lines_xray");
				}
			} else if (o->kind == Kind::Patch) {
				q3map::PatchMesh m = patch_mesh(o->patch, 4);
				for (int c = 0; c + 1 < m.width; c++) {
					hover_lines_.line(m.verts[size_t(c)].p, m.verts[size_t(c + 1)].p, C_OUTLINE);
					hover_lines_.line(m.verts[size_t((m.height - 1) * m.width + c)].p, m.verts[size_t((m.height - 1) * m.width + c + 1)].p, C_OUTLINE);
				}
				for (int r = 0; r + 1 < m.height; r++) {
					hover_lines_.line(m.verts[size_t(r * m.width)].p, m.verts[size_t((r + 1) * m.width)].p, C_OUTLINE);
					hover_lines_.line(m.verts[size_t(r * m.width + m.width - 1)].p, m.verts[size_t((r + 1) * m.width + m.width - 1)].p, C_OUTLINE);
				}
			} else {
				Vec3 lo, hi;
				if (point_box(*o, lo, hi)) {
					hover_lines_.box_edges(lo - Vec3(1, 1, 1), hi + Vec3(1, 1, 1), C_OUTLINE);
				}
			}
		}
	}
	hover_lines_.end();
}

void Session::build_handles(Overlay &out) {
	out.begin();
	if (!looking_ && drag_ != Drag::Move && drag_ != Drag::Box && drag_ != Drag::Pan) {
		const Vec3 right = -left_;
		auto square = [&](const Vec3 &p, double px, const unsigned char c[4]) {
			const double s = units_per_pixel(p) * px;
			const Vec3 r = right * s, u = up_ * s;
			out.quad(p - r - u, p + r - u, p + r + u, p - r + u, c, "$oz_ed_handles");
		};
		auto diamond = [&](const Vec3 &p, double px, const unsigned char c[4]) {
			const double s = units_per_pixel(p) * px;
			const Vec3 r = right * s, u = up_ * s;
			out.quad(p - u, p + r, p + u, p - r, c, "$oz_ed_handles");
		};
		auto stem = [&](const Vec3 &a, const Vec3 &b, double px, const unsigned char c[4]) {
			Vec3 side = (b - a).cross(fwd_);
			if (side.length() < 1e-9) {
				return;
			}
			side = side.normalized();
			const Vec3 sa = side * (units_per_pixel(a) * px * 0.5), sb = side * (units_per_pixel(b) * px * 0.5);
			out.quad(a - sa, b - sb, b + sb, a + sa, c, "$oz_ed_handles");
		};
		const Handle &hot = drag_ == Drag::None ? hover_handle_ : press_handle_;
		for (const Handle &h : handles()) {
			const bool on = hot.same(h);
			switch (h.kind) {
			case Handle::Corner: {
				const bool picked = corners.count({h.id, h.a}) != 0;
				const double r = on ? 6 : picked ? 5 : 4;
				square(h.at, r + 1, C_BLACK);
				square(h.at, r, picked ? C_CORNER_SEL : on ? C_HOVER : C_CORNER);
				break;
			}
			case Handle::Edge: {
				const bool picked = corners.count({h.id, h.a}) && corners.count({h.id, h.b});
				const double r = on ? 4.5 : 2.5;
				square(h.at, r + 1, C_BLACK);
				square(h.at, r, picked ? C_CORNER_SEL : on ? C_HOVER : C_EDGE);
				break;
			}
			case Handle::Face:
			case Handle::GroupFace: {
				const unsigned char *c = on ? C_FACE_HOVER : C_FACE;
				stem(h.base, h.at, on ? 3 : 2, c);
				const double r = on ? 10 : 8;
				diamond(h.at, r + 1.5, C_BLACK);
				diamond(h.at, r, c);
				break;
			}
			case Handle::Land: {
				// a ring on the floor where the throw comes down, a square in it to grab
				static const unsigned char C_LAND[4] = {110, 255, 150, 255};
				const double r = on ? 8 : 6;
				const double ring = units_per_pixel(h.at) * (on ? 22 : 16);
				Vec3 last = h.at + Vec3(ring, 0, 1);
				for (int i = 1; i <= 24; i++) {
					const double a = i * (2 * 3.14159265358979 / 24);
					const Vec3 p = h.at + Vec3(std::cos(a) * ring, std::sin(a) * ring, 1);
					stem(last, p, on ? 3 : 2, C_LAND);
					last = p;
				}
				square(h.at, r + 1.5, C_BLACK);
				square(h.at, r, on ? C_HOVER : C_LAND);
				break;
			}
			case Handle::ClipPoint: {
				const double r = on ? 7 : 6;
				square(h.at, r + 1.5, C_BLACK);
				square(h.at, r, C_CLIP[h.a % 3]);
				break;
			}
			default:
				break;
			}
		}
	}
	out.end();
}

}  // namespace oz::ed
