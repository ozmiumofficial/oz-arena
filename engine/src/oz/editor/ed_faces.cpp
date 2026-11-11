// The map editor's faces tool and texture mapping (ENGINE_PLAN.md P13, engine/EDITOR.md), after the
// Godot build's q3_face_tool.gd: picking faces, pushing them, moving, turning and scaling their
// textures from the keys, the inspector's mapping buttons and numbers, the eyedropper and painting;
// the inspector's entity keys and the history panel's jumps.

#include "ed_view.h"
#include "ed_engine.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>

extern "C" {
#include "../ui/oz_ui_engine.h"
#include "../../../quake3e/code/client/keycodes.h"
}

namespace oz::ed {

static const unsigned char C_FACE_FILL[4] = {40, 200, 255, 40}, C_FACE_LINE[4] = {70, 225, 255, 255},
	C_FACE_HIDDEN[4] = {70, 225, 255, 70};

static double arg(const std::vector<std::string> &a, size_t i, double fallback) {
	return i < a.size() && !a[i].empty() ? std::atof(a[i].c_str()) : fallback;
}

static std::string bare(const std::string &shader) {
	return shader.compare(0, 9, "textures/") == 0 ? shader.substr(9) : shader;
}

// ---------------------------------------------------------------- what the panel acts on

std::vector<FaceRef> Session::target_faces() const {
	std::vector<FaceRef> out;
	if (face_mode()) {
		// only the faces still there: frame() prunes the picked faces once a frame, and an undo followed by
		// another input in the same frame read a brush that was gone, or a face past its last
		for (const FaceRef &r : faces) {
			const Object *o = doc.get(r.first);
			if (o && o->kind == Kind::Brush && r.second >= 0 && r.second < int(o->brush.faces.size())) {
				out.push_back(r);
			}
		}
		return out;
	}
	for (Id id : sel) {
		const Object *o = doc.get(id);
		if (o && o->kind == Kind::Brush) {
			for (size_t i = 0; i < o->brush.faces.size(); i++) {
				out.push_back({id, int(i)});
			}
		}
	}
	return out;
}

std::vector<Id> Session::target_patches() const {
	std::vector<Id> out;
	if (!face_mode()) {
		for (Id id : sel) {
			const Object *o = doc.get(id);
			if (o && o->kind == Kind::Patch) {
				out.push_back(id);
			}
		}
	}
	return out;
}

std::vector<Id> Session::key_entities() const {
	std::set<Id> ents;
	auto add = [&](Id id) {
		if (const Object *o = doc.get(id)) {
			ents.insert(o->kind == Kind::Entity ? id : o->owner);
		}
	};
	if (face_mode()) {
		for (const FaceRef &f : faces) {
			add(f.first);
		}
	} else {
		for (Id id : sel) {
			add(id);
		}
	}
	if (ents.empty()) {
		ents.insert(doc.worldspawn());
	}
	return std::vector<Id>(ents.begin(), ents.end());
}

// ---------------------------------------------------------------- the faces tool

void Session::select_faces(const std::vector<FaceRef> &refs, SelMode mode) {
	std::set<FaceRef> next = mode == SelMode::Set ? std::set<FaceRef>() : faces;
	for (const FaceRef &r : refs) {
		const Object *o = doc.get(r.first);
		if (!o || o->kind != Kind::Brush || r.second < 0 || r.second >= int(o->brush.faces.size())) {
			continue;
		}
		if (mode == SelMode::Remove || (mode == SelMode::Toggle && faces.count(r))) {
			next.erase(r);
		} else {
			next.insert(r);
		}
	}
	if (next != faces) {
		faces = std::move(next);
		sel_rev++;
	}
}

void Session::faces_click(int m, bool twice) {
	Hit h;
	if (!pick(mx_, my_, h, false)) {
		if (!m) {
			select_faces({}, SelMode::Set);
		}
		return;
	}
	if (h.kind != Kind::Brush) {
		return;  // curves are painted whole, from the select tool
	}
	std::vector<FaceRef> refs = {{h.id, h.face}};
	if (twice) {
		refs.clear();
		for (size_t i = 0; i < doc.get(h.id)->brush.faces.size(); i++) {
			refs.push_back({h.id, int(i)});
		}
	}
	select_faces(refs, (m & CTRL) && !twice ? SelMode::Toggle : (m & SHIFT) ? SelMode::Add : SelMode::Set);
}

bool Session::faces_drag() {
	if (!press_hit_ok_ || press_hit_.kind != Kind::Brush || (press_mods_ & ALT)) {
		return false;
	}
	const FaceRef r{press_hit_.id, press_hit_.face};
	std::vector<FaceRef> push = {r};
	if (faces.count(r)) {
		push.assign(faces.begin(), faces.end());
	} else {
		select_faces({r}, (press_mods_ & SHIFT) ? SelMode::Add : SelMode::Set);
	}
	start_push(push, press_hit_.point, press_hit_.normal, push.size() > 1 ? "Push faces" : "Push face");
	return true;
}

bool Session::faces_key(int key, int m) {
	if (m & (CTRL | ALT)) {
		return false;
	}
	const double step = (m & SHIFT) ? 1 : grid;
	switch (key) {
	case K_LEFTARROW: tex_nudge(-1, 0, step); return true;
	case K_RIGHTARROW: tex_nudge(1, 0, step); return true;
	case K_UPARROW: tex_nudge(0, -1, step); return true;
	case K_DOWNARROW: tex_nudge(0, 1, step); return true;
	default: return false;
	}
}

void Session::eyedrop(const Hit &h) {
	const Object *o = doc.get(h.id);
	if (o && o->kind == Kind::Brush && h.face >= 0) {
		picked_ = o->brush.faces[size_t(h.face)];
		picked_ok_ = true;
		picked_origin_ = origin_for(doc, *o);
		const std::vector<Winding> &ws = doc.windings(h.id);
		picked_w_ = size_t(h.face) < ws.size() ? ws[size_t(h.face)] : Winding{};
		shader = shader_name(picked_.shader);
	} else if (o && o->kind == Kind::Patch) {
		picked_ok_ = false;
		shader = shader_name(o->patch.shader);
	} else {
		say("Point at a face to take its material");
		return;
	}
	want_mat_show = true;
	say("Material: " + shader);
}

void Session::paint_face(const Hit &h) {
	const Object *o = doc.get(h.id);
	if (o && o->kind == Kind::Brush && h.face >= 0) {
		if (picked_ok_ && shader_name(picked_.shader) == shader) {
			// with the picked face's mapping, wrapped round the edge they share when they're side by side
			if (copy_mapping(doc, {{h.id, h.face}}, picked_, picked_origin_, "Paint face", picked_w_.size() >= 3 ? &picked_w_ : nullptr)) {
				const Object *p = doc.get(h.id);  // the next click carries on from this face
				const std::vector<Winding> &ws = doc.windings(h.id);
				if (p && size_t(h.face) < p->brush.faces.size() && size_t(h.face) < ws.size()) {
					picked_ = p->brush.faces[size_t(h.face)];
					picked_origin_ = origin_for(doc, *p);
					picked_w_ = ws[size_t(h.face)];
				}
			}
		} else {
			paint(doc, {{h.id, h.face}}, {}, shader_id(shader), "Paint face");
		}
	} else if (o && o->kind == Kind::Patch) {
		paint(doc, {}, {h.id}, shader_id(shader), "Paint curve");
	}
}

// ---------------------------------------------------------------- the paint tool

// A face no one sees in the game, a volume's (clip, a trigger, hint, fog ...): the tool looks through it.
static bool volume_face(const Doc &doc, const Object &o, int face) {
	const Object *owner = doc.get(o.owner);
	if (owner && owner->entity.classname().compare(0, 8, "trigger_") == 0) {
		return true;
	}
	const Look l = shaders_lib().look(shader_name(o.brush.faces[size_t(face)].shader));
	return l == Look::Clip || l == Look::Trigger || l == Look::Hint || l == Look::Origin || l == Look::Portal || l == Look::Fog;
}

static double axis_of(const Vec3 &v, int i) {
	return i == 0 ? v.x : i == 1 ? v.y : v.z;
}

static Vec3 axis_vec(int i, double s) {
	return Vec3(i == 0 ? s : 0, i == 1 ? s : 0, i == 2 ? s : 0);
}

// The most a stair step rises or drops for the paint area to follow it (the game's walkable step is 18 units).
static const double PAINT_STEP = 24;

static int main_axis(const Vec3 &n) {
	const double ax = std::fabs(n.x), ay = std::fabs(n.y), az = std::fabs(n.z);
	return ax >= ay && ax >= az ? 0 : ay >= az ? 1 : 2;
}

bool Session::paint_surface(double sx, double sy, Hit &hit) const {
	for (const Hit &h : pick_all(sx, sy, false)) {
		const Object *o = doc.get(h.id);
		if (!o || h.kind != Kind::Brush || h.face < 0 || h.face >= int(o->brush.faces.size())) {
			return false;  // a curve in front hides what's behind it: curves aren't painted
		}
		if (volume_face(doc, *o, h.face)) {
			continue;
		}
		hit = h;
		return true;
	}
	return false;
}

void Session::paint_rect(const Vec3 &a, const Vec3 &b) {
	// the grid cells the two points are in, and those between: a click's one cell, as the create tool's footprint
	const double g = grid;
	const int u = (area_.axis + 1) % 3, v = (area_.axis + 2) % 3;
	auto span = [&](double p, double q, double &lo, double &hi) {
		lo = std::floor(std::min(p, q) / g + 1e-9) * g;
		hi = std::ceil(std::max(p, q) / g - 1e-9) * g;
		if (hi <= lo) {
			hi = lo + g;
		}
	};
	span(axis_of(a, u), axis_of(b, u), area_.u0, area_.u1);
	span(axis_of(a, v), axis_of(b, v), area_.v0, area_.v1);
}

std::vector<Plane> Session::paint_sides() const {
	const int u = (area_.axis + 1) % 3, v = (area_.axis + 2) % 3;
	return {{axis_vec(u, 1), area_.u1}, {axis_vec(u, -1), -area_.u0}, {axis_vec(v, 1), area_.v1}, {axis_vec(v, -1), -area_.v0}};
}

std::vector<Session::PaintPart> Session::paint_parts() const {
	std::vector<PaintPart> out;
	if (!area_.on) {
		return out;
	}
	const std::vector<Plane> sides = paint_sides();
	const std::vector<Id> brushes = doc.ids_of(Kind::Brush);
	// a point inside another block (a solid one, not a clip or trigger volume)
	auto buried = [&](const Vec3 &p, Id self) {
		for (Id b : brushes) {
			const Object *ob = doc.get(b);
			if (b == self || ob->brush.faces.empty() || volume_face(doc, *ob, 0)) {
				continue;
			}
			bool in = true;
			for (const Face &f : ob->brush.faces) {
				if (f.plane.dist(p) > -0.01) {
					in = false;
					break;
				}
			}
			if (in) {
				return true;
			}
		}
		return false;
	};
	struct Cand {
		PaintPart part;
		double off;               // how far in front of the surface it began on (a step up: +, down: -)
		double lo[2], hi[2];      // its box along the area's two axes
		Vec3 c, n;                // its middle and its facing
		double off_lo, off_hi;    // a riser's: its bottom and top, as off
	};
	std::vector<Cand> cands, sides_of;  // faces facing the surface's way (floors, treads); facing across it (risers)
	const int ua = (area_.axis + 1) % 3, va = (area_.axis + 2) % 3;
	for (Id id : brushes) {
		if (!pickable(id)) {
			continue;
		}
		const Object *o = doc.get(id);
		const std::vector<Winding> &ws = doc.windings(id);
		for (size_t i = 0; i < o->brush.faces.size() && i < ws.size(); i++) {
			const double facing = o->brush.faces[i].plane.n.dot(area_.plane.n);
			const bool across = std::fabs(facing) < 0.02;
			if (ws[i].size() < 3 || (facing < 0.9995 && !across) || volume_face(doc, *o, int(i))) {
				continue;  // facing away, or slanted: the face under it, a ramp
			}
			Winding w = ws[i];
			for (const Plane &p : sides) {
				q3map::clip_back(w, p);
			}
			if (w.size() < 3 || q3map::winding_area(w) <= 0.01) {
				continue;
			}
			bool on_edge = false;  // a face lying on the area's side (a staircase's own side at its edge)
			for (const Plane &p : sides) {
				on_edge = on_edge || p.dist(q3map::winding_center(w)) > -0.5;
			}
			if (on_edge) {
				continue;
			}
			// a part wholly under other blocks (the floor under a staircase) is never seen: left alone
			const Vec3 c = q3map::winding_center(w), lift = (across ? o->brush.faces[i].plane.n : area_.plane.n) * 0.25;
			bool hidden = buried(c + lift, id);
			for (size_t k = 0; hidden && k < w.size(); k++) {
				hidden = buried(w[k] + (c - w[k]) * 0.1 + lift, id);
			}
			if (hidden) {
				continue;
			}
			Cand cd{{id, int(i), w}, area_.plane.dist(c), {1e30, 1e30}, {-1e30, -1e30}, c, o->brush.faces[i].plane.n, 1e30, -1e30};
			for (const Vec3 &q : w) {
				for (int k = 0; k < 2; k++) {
					const double x = axis_of(q, k == 0 ? ua : va);
					cd.lo[k] = std::min(cd.lo[k], x), cd.hi[k] = std::max(cd.hi[k], x);
				}
				cd.off_lo = std::min(cd.off_lo, area_.plane.dist(q)), cd.off_hi = std::max(cd.off_hi, area_.plane.dist(q));
			}
			(across ? sides_of : cands).push_back(cd);
		}
	}
	// the surface it began on, and from it each face a step up or down from one already in that touches it
	// (stairs, both ways), so the wall seen far behind a gap stays out; Ctrl: the faces level with it alone
	std::vector<char> in(cands.size(), 0);
	std::vector<size_t> todo;
	for (size_t i = 0; i < cands.size(); i++) {
		if (std::fabs(cands[i].off) <= 0.5) {
			in[i] = 1;
			todo.push_back(i);
		}
	}
	while (!area_.flat && !todo.empty()) {
		const Cand &c = cands[todo.back()];
		todo.pop_back();
		for (size_t j = 0; j < cands.size(); j++) {
			const Cand &d = cands[j];
			if (!in[j] && std::fabs(d.off - c.off) <= PAINT_STEP && d.lo[0] <= c.hi[0] + 0.5 && c.lo[0] <= d.hi[0] + 0.5 &&
					d.lo[1] <= c.hi[1] + 0.5 && c.lo[1] <= d.hi[1] + 0.5) {
				in[j] = 1;
				todo.push_back(j);
			}
		}
	}
	for (size_t i = 0; i < cands.size(); i++) {
		if (in[i]) {
			out.push_back(cands[i].part);
		}
	}
	// the risers between them: a face across the surface whose top meets a tread taken, with a lower tread taken
	// in front of it (the side it faces), so a step's wall goes with its floor; the room's walls and a box's sides don't
	auto touch = [](const Cand &a, const Cand &b) {
		return a.lo[0] <= b.hi[0] + 0.5 && b.lo[0] <= a.hi[0] + 0.5 && a.lo[1] <= b.hi[1] + 0.5 && b.lo[1] <= a.hi[1] + 0.5;
	};
	for (const Cand &r : area_.flat ? std::vector<Cand>{} : sides_of) {
		bool top = false, below = false;
		for (size_t i = 0; i < cands.size() && !(top && below); i++) {
			const Cand &t = cands[i];
			if (!in[i] || !touch(r, t)) {
				continue;
			}
			top = top || std::fabs(t.off - r.off_hi) <= 0.5;
			below = below || (t.off < r.off_hi - 0.5 && t.off >= r.off_lo - 0.5 && (t.c - r.c).dot(r.n) > 0);
		}
		if (top && below) {
			out.push_back(r.part);
		}
	}
	return out;
}

void Session::build_paint_area() {
	char key[256] = "";
	if (area_.on) {
		std::snprintf(key, sizeof key, "%d %g %g %g %g %g %g %g %g %d %llu %llu", area_.axis, area_.plane.n.x, area_.plane.n.y, area_.plane.n.z,
			area_.plane.d, area_.u0, area_.u1, area_.v0, area_.v1, int(area_.flat), (unsigned long long)doc.revision, (unsigned long long)show_rev);
	}
	if (paint_built_ == key) {
		return;
	}
	paint_built_ = key;
	static const unsigned char fill[4] = {255, 150, 70, 90}, edge[4] = {255, 170, 90, 255}, hidden[4] = {255, 150, 70, 80};
	paint_lines_.begin();
	paint_count_ = 0;
	if (area_.on) {
		// what will take the material, and the rectangle itself on the surface it began on
		for (const PaintPart &p : paint_parts()) {
			paint_lines_.polygon(p.inside, fill, "$oz_ed_tint");
			paint_lines_.outline(p.inside, edge);
			paint_count_++;
		}
		Winding r = q3map::base_winding(area_.plane);
		for (const Plane &p : paint_sides()) {
			q3map::clip_back(r, p);
		}
		for (Vec3 &q : r) {
			q = q + area_.plane.n * 0.25;  // just off the surface, so the lines aren't lost in it
		}
		paint_lines_.outline(r, edge);
		paint_lines_.outline(r, hidden, "$oz_ed_lines_xray");
	}
	paint_lines_.end();
}

void Session::build_paint_preview() {
	// what shows: a material being dragged from the browser, in any tool; in the paint tool the one held,
	// while nothing else is under way (a drag, a marked area, the eyedropper armed or Alt held)
	std::string mat = drop_material;
	if (mat.empty() && tool == Tool::Paint && drag_ == Drag::None && !area_.on && !eyedrop_armed_ && !(mods() & ALT)) {
		mat = shader;
	}
	Id id = 0;
	std::vector<int> fs;
	if (!mat.empty() && over_ && !looking_ && hover_ok_ && hover_.kind == Kind::Brush && hover_.face >= 0) {
		const Object *o = doc.get(hover_.id);
		if (o && o->kind == Kind::Brush && size_t(hover_.face) < o->brush.faces.size()) {
			id = hover_.id;
			for (int i = 0; i < int(o->brush.faces.size()); i++) {
				if (i == hover_.face || (mods() & SHIFT)) {
					fs.push_back(i);
				}
			}
		}
	}
	std::string key;
	if (id) {
		key = std::to_string(id) + " " + std::to_string(doc.revision) + " " + mat;
		for (int i : fs) {
			key += " " + std::to_string(i);
		}
	}
	if (key != preview_key_) {
		preview_key_ = key;
		std::vector<Id> ids;
		if (preview_id_) {
			ids.push_back(preview_id_);
		}
		preview_id_ = id;
		preview_mat_ = mat;
		preview_faces_.clear();
		if (id) {
			const Object *o = doc.get(id);
			const std::vector<Winding> &ws = doc.windings(id);
			const Vec3 origin = origin_for(doc, *o);
			const ShaderId to = shader_id(mat);
			for (int i : fs) {
				Face g = o->brush.faces[size_t(i)];
				if (g.shader == to || size_t(i) >= ws.size()) {
					continue;  // it has it already
				}
				painted(doc, g, ws[size_t(i)], origin, to);
				preview_faces_[{id, i}] = g;
			}
			if (ids.empty() || ids[0] != id) {
				ids.push_back(id);
			}
		}
		if (!ids.empty()) {
			paint_preview_.changed(doc, ids);
		}
	}
	paint_preview_.flush(doc);
}

std::string Session::paint_preview_text() const {
	if (!preview_id_ || preview_faces_.empty()) {
		return "";
	}
	std::string out = std::to_string(preview_id_);
	const char *sep = " ";
	for (const auto &kv : preview_faces_) {
		out += sep + std::to_string(kv.first.second);
		sep = ",";
	}
	return out + " " + preview_mat_;
}

void Session::drop_at_pointer(int m) {
	if (drop_material.empty()) {
		return;
	}
	shader = bare(drop_material);  // held, as a click in the browser holds it
	drop_material.clear();
	Hit h;
	const Object *o = over_ && pick(mx_, my_, h, false) ? doc.get(h.id) : nullptr;
	if (o && o->kind == Kind::Patch) {
		paint(doc, {}, {h.id}, shader_id(shader), "Paint curve");
		say("Painted the curve " + shader);
		return;
	}
	if (!o || o->kind != Kind::Brush || h.face < 0) {
		say("Material: " + shader + " \xc2\xb7 let go on a face to paint it");
		return;
	}
	std::vector<FaceRef> refs = {{h.id, h.face}};
	if (m & SHIFT) {
		refs.clear();
		for (size_t i = 0; i < o->brush.faces.size(); i++) {
			refs.push_back({h.id, int(i)});
		}
	}
	paint(doc, refs, {}, shader_id(shader), refs.size() == 1 ? "Paint face" : "Paint brush");
	picked_ok_ = false;
	if (face_mode()) {
		select_faces(refs, SelMode::Set);  // the faces and paint tools pick what they painted, for the panel
	}
	say("Painted " + std::string(refs.size() == 1 ? "a face " : "a brush ") + shader);
}

std::string Session::paint_area_text() const {
	char buf[96];
	std::snprintf(buf, sizeof buf, "%g x %g, %d %s", area_.u1 - area_.u0, area_.v1 - area_.v0, paint_count_, paint_count_ == 1 ? "face" : "faces");
	return buf;
}

void Session::paint_area_clear() {
	area_.on = false;
}

bool Session::paint_area_apply() {
	if (!area_.on) {
		say(tool == Tool::Paint ? "Drag an area on a block's surface first" : "The paint tool (Shift+P): drag an area, then paint it");
		return false;
	}
	const std::vector<PaintPart> parts = paint_parts();
	if (parts.empty()) {
		say("Nothing to paint there: the area covers no face facing the way of the one it began on");
		return false;
	}
	std::vector<Id> ids;
	std::map<Id, std::vector<Plane>> planes;  // each block's faces the area takes
	for (const PaintPart &p : parts) {
		if (ids.empty() || ids.back() != p.id) {
			ids.push_back(p.id);
		}
		planes[p.id].push_back(doc.get(p.id)->brush.faces[size_t(p.face)].plane);
	}
	const std::vector<Plane> sides = paint_sides();
	const std::string label = "Paint area " + shader;
	std::vector<FaceRef> painted;
	int made = 0;
	doc.begin(label);
	for (Id id : ids) {
		const Object *o = doc.get(id);
		const Vec3 origin = origin_for(doc, *o);
		// cut on each side the block reaches past: the part outside is a block of its own, as it was
		std::shared_ptr<Object> inside = std::make_shared<Object>(*o);
		std::vector<std::shared_ptr<Object>> outside;
		for (const Plane &p : sides) {
			Pieces pc = clipped(doc, *inside, p, origin);
			if (pc.whole || !pc.front || !pc.back) {
				continue;  // the side misses it (it has a face inside the area, so it's inside this side)
			}
			outside.push_back(pc.front);
			inside = pc.back;
		}
		if (!outside.empty()) {
			doc.put(id, inside);
			for (const auto &p : outside) {
				doc.add(p);
				made++;
			}
		}
		const Object *in = doc.get(id);
		const std::vector<Winding> &ws = doc.windings(id);
		for (size_t i = 0; i < in->brush.faces.size() && i < ws.size(); i++) {
			if (ws[i].size() < 3) {
				continue;
			}
			for (const Plane &pl : planes[id]) {
				if (in->brush.faces[i].plane.n.dot(pl.n) >= 0.9995 && std::fabs(pl.dist(q3map::winding_center(ws[i]))) <= 0.5) {
					painted.push_back({id, int(i)});
					break;
				}
			}
		}
	}
	paint(doc, painted, {}, shader_id(shader), label);
	doc.end();
	area_.on = false;
	faces.clear();
	sel_rev++;
	select_faces(painted, SelMode::Set);
	say("Painted " + std::to_string(painted.size()) + (painted.size() == 1 ? " face " : " faces ") + shader +
		(made ? ", the blocks cut on the area's edges (" + std::to_string(made) + (made == 1 ? " piece" : " pieces") + " beside it)" : ""));
	return true;
}

bool Session::paint_begin() {
	Hit h;
	if ((press_mods_ & ALT) || !paint_surface(press_x_, press_y_, h)) {
		return false;  // Alt: the eyedropper's click; off any block: a click when let go
	}
	area_before_ = area_;
	area_.on = true;
	area_.plane = doc.get(h.id)->brush.faces[size_t(h.face)].plane;
	area_.axis = main_axis(area_.plane.n);
	paint_grab_ = h.point;
	drag_ = Drag::Paint;
	paint_motion(mods());
	return true;
}

void Session::paint_motion(int m) {
	Vec3 p;
	if (plane_point(mx_, my_, paint_grab_, area_.plane.n, p)) {
		paint_rect(paint_grab_, p);
	}
	area_.flat = (m & CTRL) != 0;
	char buf[64];
	std::snprintf(buf, sizeof buf, "%g x %g", area_.u1 - area_.u0, area_.v1 - area_.v0);
	drag_note_ = buf;
}

void Session::paint_end(bool cancel) {
	if (cancel) {
		area_ = area_before_;
	}
	drag_note_.clear();
}

void Session::paint_click(int m) {
	// a click paints the face under the pointer, Shift+click every face of its brush, a curve whole
	// (Alt's eyedropper is click()'s, in every tool)
	Hit h;
	const Object *o = pick(mx_, my_, h, false) ? doc.get(h.id) : nullptr;
	if (o && o->kind == Kind::Patch) {
		paint(doc, {}, {h.id}, shader_id(shader), "Paint curve");
		say("Painted the curve " + shader);
		return;
	}
	if (!o || o->kind != Kind::Brush || h.face < 0) {
		say("Click a face to paint it " + shader + " (Shift: its whole brush), or drag an area on a block's surface");
		return;
	}
	std::vector<FaceRef> refs = {{h.id, h.face}};
	if (m & SHIFT) {
		refs.clear();
		for (size_t i = 0; i < o->brush.faces.size(); i++) {
			refs.push_back({h.id, int(i)});
		}
	}
	area_.on = false;
	paint(doc, refs, {}, shader_id(shader), refs.size() == 1 ? "Paint face" : "Paint brush");
	picked_ok_ = false;  // painted plainly: Alt+Shift+click paints plainly too
	select_faces(refs, SelMode::Set);  // picked for the panel, as an area's faces are
	say("Painted " + std::string(refs.size() == 1 ? "a face " : "a brush ") + shader);
}

std::string Session::paint_hint() const {
	if (drag_ == Drag::Paint) {
		return drag_note_ + " \xc2\xb7 let go: it stays marked, Enter paints it" +
			((mods() & CTRL) ? " \xc2\xb7 Ctrl: only the faces level with where it began" : " \xc2\xb7 it follows steps up and down (Ctrl: level only)") + " \xc2\xb7 Esc cancels";
	}
	if (area_.on) {
		return "Area " + paint_area_text() + ": Enter (or Paint area in Inspect) paints it " + shader +
			", the blocks cut on its edges \xc2\xb7 pick another material first if you like \xc2\xb7 drag again for another area \xc2\xb7 Esc clears it";
	}
	if (!faces.empty()) {
		return std::to_string(faces.size()) + (faces.size() == 1 ? " face" : " faces") +
			" painted: the panel maps their texture, P paints them again \xc2\xb7 drag a new area \xc2\xb7 Esc lets them go";
	}
	std::string on;
	if (hover_ok_ && hover_.kind == Kind::Brush && hover_.face >= 0) {
		if (const Object *o = doc.get(hover_.id)) {
			on = shader_name(o->brush.faces[size_t(hover_.face)].shader) + " \xc2\xb7 ";
		}
	}
	return on + "Paint " + shader + ": drag an area on a block's surface, on the grid, as when making a brush (Enter paints it), or click a face to paint it (Shift+click: its brush) \xc2\xb7 Alt+click takes a material \xc2\xb7 Esc: back to select";
}

void Session::map_targets(const MapEdit &e, const std::string &label) {
	const std::vector<FaceRef> t = target_faces();
	const std::vector<Id> curves = face_mode() ? std::vector<Id>{} : target_patches();
	if (t.empty() && curves.empty()) {
		say(tool == Tool::Paint ? "Paint faces first: drag across them" : tool == Tool::Faces ? "Pick faces first: click them"
			: "Select brushes or curves, or pick faces with the faces tool (T)");
		return;
	}
	if (!t.empty()) {
		map_faces(doc, t, e, label);
	}
	if (!curves.empty() && !map_patches(doc, curves, e, label) && t.empty()) {
		say("A curve's texture takes the buttons (move, turn, scale, fit, flip, reset), not align or the typed numbers");
	}
}

// A face's middle.
static Vec3 middle(const Winding &w) {
	Vec3 c;
	for (const Vec3 &p : w) {
		c = c + p;
	}
	return w.empty() ? c : c * (1.0 / double(w.size()));
}

void Session::tex_nudge(double dx, double dy, double step) {
	const std::vector<FaceRef> t = target_faces();
	if (t.empty()) {
		say("Pick faces first: click them");
		return;
	}
	// each face's texture moves the way asked on screen: along whichever of its axes runs most that way
	std::map<std::pair<int, int>, std::vector<FaceRef>> groups;  // (axis, sign)
	for (const FaceRef &r : t) {
		const Face &f = doc.get(r.first)->brush.faces[size_t(r.second)];
		const std::vector<Winding> &ws = doc.windings(r.first);
		int axis = dx != 0 ? 0 : 1, sign = dx + dy > 0 ? -1 : 1;
		if (size_t(r.second) < ws.size() && ws[size_t(r.second)].size() >= 3) {
			const Vec3 c = middle(ws[size_t(r.second)]), n = f.plane.n;
			const Vec3 su = f.st.s - n * f.st.s.dot(n), sv = f.st.t - n * f.st.t.dot(n);
			double x0, y0, x1, y1, x2, y2;
			if (su.length() > 1e-12 && sv.length() > 1e-12 && project(c, x0, y0) && project(c + su.normalized() * 8, x1, y1) &&
					project(c + sv.normalized() * 8, x2, y2)) {
				const double du = (x1 - x0) * dx + (y1 - y0) * dy, dv = (x2 - x0) * dx + (y2 - y0) * dy;
				axis = std::fabs(du) >= std::fabs(dv) ? 0 : 1;
				sign = (axis == 0 ? du : dv) > 0 ? -1 : 1;  // the picture moves toward +s as its shift goes down
			}
		}
		groups[{axis, sign}].push_back(r);
	}
	doc.begin("Move texture");
	for (const auto &g : groups) {
		MapEdit e;
		e.op = MapOp::Shift;
		(g.first.first == 0 ? e.a : e.b) = g.first.second * step;
		map_faces(doc, g.second, e, "Move texture");
	}
	doc.end();
}

void Session::tex_turn(double degrees) {
	const std::vector<FaceRef> t = target_faces();
	if (t.empty()) {
		say(tool == Tool::Paint ? "Paint faces first: drag across them" : tool == Tool::Faces ? "Pick faces first: click them"
			: "Select brushes, or pick faces with the faces tool (T)");
		return;
	}
	// counter-clockwise as the camera sees each face: the turn's axis toward the camera or away
	std::vector<FaceRef> toward, away;
	for (const FaceRef &r : t) {
		const Face &f = doc.get(r.first)->brush.faces[size_t(r.second)];
		Vec3 k;
		if (f.form == TexForm::Quake) {
			Vec3 s, tt;
			q3map::default_texture_axes(f.plane.n, s, tt);
			k = tt.cross(s);
		} else if (f.form == TexForm::Valve) {
			k = Vec3(f.raw[4], f.raw[5], f.raw[6]).cross(Vec3(f.raw[0], f.raw[1], f.raw[2]));
		} else {
			k = f.st.t.cross(f.st.s);
		}
		const std::vector<Winding> &ws = doc.windings(r.first);
		const Vec3 c = size_t(r.second) < ws.size() ? middle(ws[size_t(r.second)]) : Vec3();
		(k.dot(cam.origin - c) >= 0 ? toward : away).push_back(r);
	}
	doc.begin("Turn texture");
	MapEdit e;
	e.op = MapOp::Rotate;
	e.a = degrees;
	map_faces(doc, toward, e, "Turn texture");
	e.a = -degrees;
	map_faces(doc, away, e, "Turn texture");
	doc.end();
}

// A polygon's area.
static double area(const Winding &w) {
	Vec3 sum;
	for (size_t i = 1; i + 1 < w.size(); i++) {
		sum = sum + (w[i] - w[0]).cross(w[i + 1] - w[0]);
	}
	return sum.length() * 0.5;
}

void Session::tex_wrap() {
	std::vector<FaceRef> t = target_faces();
	if (!face_mode()) {
		// the selected brushes' walls: round them, not over their tops and bottoms
		t.erase(std::remove_if(t.begin(), t.end(), [&](const FaceRef &r) {
			return std::fabs(doc.get(r.first)->brush.faces[size_t(r.second)].plane.n.z) > 0.7;
		}), t.end());
	}
	if (t.size() < 2) {
		say(face_mode() ? "Pick the faces to wrap the texture round (Shift+click adds): the biggest keeps its texture"
			: "Select the brushes whose walls the texture wraps round, or pick faces with the faces tool (T)");
		return;
	}
	// the biggest face keeps its texture, and the wrap spreads from it
	size_t best = 0;
	double most = -1;
	for (size_t i = 0; i < t.size(); i++) {
		const std::vector<Winding> &ws = doc.windings(t[i].first);
		const double a = size_t(t[i].second) < ws.size() ? area(ws[size_t(t[i].second)]) : 0;
		if (a > most + 1e-6) {
			most = a;
			best = i;
		}
	}
	std::swap(t[0], t[best]);
	if (!wrap_faces(doc, t, "Wrap texture")) {
		say("Nothing to wrap: no face shares an edge with the biggest one, or the texture already runs round them");
	}
}

void Session::build_faces() {
	if (doc.revision == faces_doc_rev_ && sel_rev == faces_sel_rev_) {
		return;
	}
	faces_doc_rev_ = doc.revision;
	faces_sel_rev_ = sel_rev;
	faces_lines_.begin();
	for (const FaceRef &r : faces) {
		const std::vector<Winding> &ws = doc.windings(r.first);
		if (size_t(r.second) < ws.size() && ws[size_t(r.second)].size() >= 3) {
			faces_lines_.polygon(ws[size_t(r.second)], C_FACE_FILL, "$oz_ed_tint");
			faces_lines_.outline(ws[size_t(r.second)], C_FACE_LINE);
			faces_lines_.outline(ws[size_t(r.second)], C_FACE_HIDDEN, "$oz_ed_lines_xray");
		}
	}
	faces_lines_.end();
}

std::string Session::faces_hint() const {
	if (hover_ok_ && hover_.kind == Kind::Brush && hover_.face >= 0) {
		const Object *o = doc.get(hover_.id);
		const std::string on = o ? shader_name(o->brush.faces[size_t(hover_.face)].shader) : "";
		if (faces.empty()) {
			return on + " \xc2\xb7 click picks the face (Shift adds, Ctrl toggles, double-click: the whole brush) \xc2\xb7 Alt+click takes its material \xc2\xb7 Alt+Shift+click paints it";
		}
	}
	if (faces.empty()) {
		return "Faces: click a face to pick it \xc2\xb7 Alt+click takes a face's material, Alt+Shift+click paints one \xc2\xb7 Esc: back to select";
	}
	return std::to_string(faces.size()) + (faces.size() == 1 ? " face" : " faces") +
		": drag to push \xc2\xb7 the arrows move the texture (Shift: a texel) \xc2\xb7 Ctrl+arrows turn and scale it \xc2\xb7 P paints \xc2\xb7 the panel maps them";
}

// ---------------------------------------------------------------- the actions

bool Session::faces_action(const std::string &action, const std::vector<std::string> &a) {
	auto edit = [&](MapOp op, double x, double y, int which, const std::string &label) {
		MapEdit e;
		e.op = op, e.a = x, e.b = y, e.which = which;
		map_targets(e, label);
	};
	if (action == "faces") {
		set_tool(tool == Tool::Faces ? Tool::Select : Tool::Faces);
	} else if (action == "paint_tool") {
		set_tool(tool == Tool::Paint ? Tool::Select : Tool::Paint);
	} else if (action == "paint_area") {
		if (tool != Tool::Paint) {
			set_tool(Tool::Paint);
		}
		paint_area_apply();
	} else if (action == "paint_area_clear") {
		paint_area_clear();
	} else if (action == "pick_material") {
		Hit h;
		if (eyedrop_armed_) {
			eyedrop_armed_ = false;  // the button again: put away
			say("Eyedropper put away");
		} else if (over_ && pick(mx_, my_, h, false)) {
			eyedrop(h);  // the key, with the pointer on a face
		} else {
			// the button (the pointer is on it, not on a face): the next click in a view takes the material
			eyedrop_armed_ = true;
			say("Eyedropper: click a face to take its material \xc2\xb7 Esc puts it away");
		}
	} else if (action == "paint") {
		if (!a.empty()) {
			// the browser's click: the material is picked up, nothing painted (P, or a click in the paint tool, paints)
			shader = bare(a[0]);
			say("Material: " + shader + (tool == Tool::Paint ? (area_.on ? " \xc2\xb7 Enter paints the area with it" : " \xc2\xb7 click a face to paint it, or drag an area")
				: !target_faces().empty() || !target_patches().empty() ? " \xc2\xb7 P paints the selection with it" : " \xc2\xb7 the paint tool (Shift+P) paints it, as do new brushes"));
			return true;
		} else if (tool == Tool::Paint && area_.on) {
			paint_area_apply();  // P in the paint tool: the area marked
			return true;
		}
		const std::vector<FaceRef> t = target_faces();
		const std::vector<Id> p = target_patches();
		if (t.empty() && p.empty()) {
			say("Material: " + shader);
		} else if (paint(doc, t, p, shader_id(shader), "Paint " + shader)) {
			picked_ok_ = false;  // painted plainly: Alt+Shift+click paints plainly too
		}
	} else if (action == "tex_fit") {
		const double x = arg(a, 0, 1);
		edit(MapOp::Fit, x, arg(a, 1, x), 0, "Fit texture");
	} else if (action == "tex_left" || action == "tex_right" || action == "tex_top" || action == "tex_bottom" || action == "tex_center") {
		static const char *names[5] = {"tex_left", "tex_right", "tex_top", "tex_bottom", "tex_center"};
		int which = 0;
		while (which < 4 && action != names[which]) {
			which++;
		}
		edit(MapOp::Align, 0, 0, which, "Align texture");
	} else if (action == "tex_flip_s" || action == "tex_flip_t") {
		edit(MapOp::Flip, 0, 0, action == "tex_flip_t", "Flip texture");
	} else if (action == "tex_reset") {
		edit(MapOp::Reset, 0, 0, 0, "Reset texture");
	} else if (action == "tex_natural") {
		edit(MapOp::Natural, 0, 0, 0, "Natural texture size");
	} else if (action == "tex_wrap") {
		tex_wrap();
	} else if (action == "tex_rotate_left" || action == "tex_rotate_right") {
		tex_turn(action == "tex_rotate_left" ? 15 : -15);
	} else if (action == "tex_scale_up" || action == "tex_scale_down") {
		const double k = action == "tex_scale_up" ? 2 : 0.5;
		edit(MapOp::Scale, k, k, 0, "Scale texture");
	} else if (action == "tex_nudge") {
		// the faces tool's arrows: ed tex_nudge left|right|up|down [texels]
		const std::string d = a.empty() ? "" : a[0];
		const double step = arg(a, 1, grid);
		if (d == "left" || d == "right" || d == "up" || d == "down") {
			tex_nudge(d == "left" ? -1 : d == "right" ? 1 : 0, d == "up" ? -1 : d == "down" ? 1 : 0, step);
		} else {
			say("ed tex_nudge left|right|up|down [texels]", true);
		}
	} else if (action == "tex_shift") {
		edit(MapOp::Shift, arg(a, 0, 0), arg(a, 1, 0), 0, "Move texture");
	} else if (action == "tex_rotate") {
		edit(MapOp::Rotate, arg(a, 0, 0), 0, 0, "Turn texture");
	} else if (action == "tex_scale") {
		const double x = arg(a, 0, 1);
		edit(MapOp::Scale, x, arg(a, 1, x), 0, "Scale texture");
	} else if (action == "tex_set") {
		static const char *fields[5] = {"shift_s", "shift_t", "rotate", "scale_s", "scale_t"};
		int which = -1;
		for (int i = 0; i < 5; i++) {
			if (!a.empty() && a[0] == fields[i]) {
				which = i;
			}
		}
		if (which < 0 || a.size() < 2 || a[1].empty()) {
			say("ed tex_set shift_s|shift_t|rotate|scale_s|scale_t <value>", true);
		} else if (which >= 3 && std::atof(a[1].c_str()) == 0) {
			say("A scale can't be 0", true);
		} else {
			static const char *labels[5] = {"Texture shift across", "Texture shift down", "Texture turn", "Texture scale across", "Texture scale down"};
			edit(MapOp::Field, std::atof(a[1].c_str()), 0, which, std::string(labels[which]) + " " + a[1]);
		}
	} else if (action == "key_set" || action == "key_remove" || action == "key_rename") {
		if (a.empty() || a[0].empty()) {
			say("ed " + action + " <key> ...", true);
			return true;
		}
		const std::vector<Id> ents = key_entities();
		if (action == "key_set") {
			std::string value;
			for (size_t i = 1; i < a.size(); i++) {
				value += (i > 1 ? " " : "") + a[i];
			}
			if (a[0] == "classname" && value.empty()) {
				say("An entity needs a class", true);
			} else {
				set_key(doc, ents, a[0], value, "Set " + a[0]);
			}
		} else if (action == "key_remove") {
			if (!remove_key(doc, ents, a[0], "Remove " + a[0]) && a[0] == "classname") {
				say("An entity needs a class", true);
			}
		} else {
			std::string note;
			if (!rename_key(doc, ents, a[0], a.size() > 1 ? a[1] : "", note) && !note.empty()) {
				say(note, true);
			}
		}
	} else if (action == "history") {
		// back or forward to n steps done
		const size_t n = size_t(std::max(0.0, arg(a, 0, double(doc.undo_size()))));
		while (doc.undo_size() > n && doc.undo()) {
		}
		while (doc.undo_size() < n && doc.redo()) {
		}
		corners.clear();
	} else if (action == "row" || action == "frame_ids") {
		// the outliner: ed row <id> (Shift adds, Ctrl toggles); ed frame_ids <id>...: those, framed
		std::vector<Id> ids;
		for (const std::string &s : a) {
			const Id id = Id(std::atoi(s.c_str()));
			for (Id g : group_of(id)) {
				ids.push_back(g);
			}
			if (doc.get(id) && !pickable(id)) {
				say(visible(id) ? "That's locked: Unlock all frees it" : "That's hidden: Show all brings it back (or a filter leaves it out)");
			}
		}
		if (face_mode()) {
			set_tool(Tool::Select);
		}
		const int m = mods();
		corners.clear();
		select(ids, action == "frame_ids" ? SelMode::Set : (m & CTRL) ? SelMode::Toggle : (m & SHIFT) ? SelMode::Add : SelMode::Set);
		if (action == "frame_ids") {
			frame_selection();
		}
	} else {
		return false;
	}
	return true;
}

}  // namespace oz::ed
