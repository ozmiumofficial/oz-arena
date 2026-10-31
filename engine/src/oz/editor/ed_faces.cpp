// The map editor's faces tool and texture mapping (ENGINE_PLAN.md P13, engine/EDITOR.md), after the
// Godot build's q3_face_tool.gd: picking faces, pushing them, moving, turning and scaling their
// textures from the keys, the inspector's mapping buttons and numbers, the eyedropper and painting;
// the inspector's entity keys and the history panel's jumps.

#include "ed_view.h"
#include "ed_engine.h"

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
	if (tool == Tool::Faces) {
		return std::vector<FaceRef>(faces.begin(), faces.end());
	}
	std::vector<FaceRef> out;
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
	if (tool != Tool::Faces) {
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
	if (tool == Tool::Faces) {
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
	if (m & ALT) {
		if (m & SHIFT) {
			paint_face(h);
		} else {
			eyedrop(h);
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
		shader = shader_name(picked_.shader);
	} else if (o && o->kind == Kind::Patch) {
		picked_ok_ = false;
		shader = shader_name(o->patch.shader);
	} else {
		say("Point at a face to take its material");
		return;
	}
	say("Material: " + shader);
}

void Session::paint_face(const Hit &h) {
	const Object *o = doc.get(h.id);
	if (o && o->kind == Kind::Brush && h.face >= 0) {
		if (picked_ok_ && shader_name(picked_.shader) == shader) {
			copy_mapping(doc, {{h.id, h.face}}, picked_, picked_origin_, "Paint face");  // with the picked face's mapping
		} else {
			paint(doc, {{h.id, h.face}}, {}, shader_id(shader), "Paint face");
		}
	} else if (o && o->kind == Kind::Patch) {
		paint(doc, {}, {h.id}, shader_id(shader), "Paint curve");
	}
}

void Session::map_targets(const MapEdit &e, const std::string &label) {
	const std::vector<FaceRef> t = target_faces();
	const std::vector<Id> curves = tool == Tool::Faces ? std::vector<Id>{} : target_patches();
	if (t.empty() && curves.empty()) {
		say(tool == Tool::Faces ? "Pick faces first: click them" : "Select brushes or curves, or pick faces with the faces tool (T)");
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
		say(tool == Tool::Faces ? "Pick faces first: click them" : "Select brushes, or pick faces with the faces tool (T)");
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
	} else if (action == "pick_material") {
		Hit h;
		if (over_ && pick(mx_, my_, h, false)) {
			eyedrop(h);
		} else {
			say("Point at a face to take its material");
		}
	} else if (action == "paint") {
		if (!a.empty()) {
			shader = bare(a[0]);
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
		if (tool == Tool::Faces) {
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
