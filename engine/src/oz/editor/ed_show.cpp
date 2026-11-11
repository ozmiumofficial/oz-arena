// What the map editor shows (ENGINE_PLAN.md P13, engine/EDITOR.md): objects hidden (out of every
// view) or locked (drawn, but no click, box or Select all takes them), and the view filters that leave
// whole kinds out: tool brushes, triggers, sky, point entities, curves. They're the view's alone: the
// map compiles and saves whole. An entity's brushes go with it; a selection keeps only what can be
// picked. Groups (func_group): brushes made one entity, which a click takes whole and q3map2 merges
// back into the world.

#include "ed_view.h"
#include "ed_engine.h"
#include "ed_mapfile.h"
#include "ed_shaders.h"
#include "../ui/oz_ui_engine.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <memory>
#include <set>

namespace oz::ed {

Look Session::look_of(ShaderId s) const {
	auto it = looks_.find(s);
	if (it != looks_.end()) {
		return it->second;
	}
	std::string name = shader_name(s);
	if (name.compare(0, 9, "textures/") != 0) {
		name = "textures/" + name;
	}
	return looks_[s] = shaders_lib().look(name);
}

bool Session::filtered(Id id, const Object &o) const {
	if (o.kind == Kind::Entity) {
		return (filters & HIDE_ENTITIES) && id != doc.worldspawn() && doc.owned(id).empty();  // point entities
	}
	if (o.kind == Kind::Patch) {
		return (filters & HIDE_CURVES) != 0;
	}
	const Object *owner = o.owner && o.owner != doc.worldspawn() ? doc.get(o.owner) : nullptr;
	if ((filters & HIDE_TRIGGERS) && owner && owner->entity.classname().compare(0, 8, "trigger_") == 0) {
		return true;
	}
	if (!(filters & (HIDE_TOOLS | HIDE_TRIGGERS | HIDE_SKY))) {
		return false;
	}
	// what its faces are: a tool brush has nothing drawn (clip, hint, nodraw, origin ...), a sky brush sky
	// and the rest caulk, a trigger brush only trigger faces
	bool normal = false, sky = false, trigger = true, tool = true;
	for (const Face &f : o.brush.faces) {
		const Look l = look_of(f.shader);
		normal |= l == Look::Normal;
		sky |= l == Look::Sky;
		trigger &= l == Look::Trigger;
		tool &= l != Look::Normal && l != Look::Caulk && l != Look::Sky;
	}
	return ((filters & HIDE_SKY) && sky && !normal) || ((filters & HIDE_TRIGGERS) && trigger) || ((filters & HIDE_TOOLS) && tool && !trigger);
}

bool Session::visible(Id id) const {
	const Object *o = doc.get(id);
	if (!o || hidden.count(id)) {
		return false;
	}
	if (o->kind != Kind::Entity && o->owner && o->owner != doc.worldspawn() && hidden.count(o->owner)) {
		return false;  // its entity is hidden
	}
	return !filters || !filtered(id, *o);
}

bool Session::pickable(Id id) const {
	if (!visible(id) || locked.count(id)) {
		return false;
	}
	const Object *o = doc.get(id);
	return o->kind == Kind::Entity || !o->owner || !locked.count(o->owner);
}

void Session::shown_changed() {
	show_rev++;
	pending_all_ = true;
	ents_dirty_ = true;
	links_doc_ = ~0ull;
	hover_built_ = -1;
	std::vector<Id> keep;
	for (Id id : sel) {
		if (pickable(id)) {
			keep.push_back(id);
		}
	}
	if (keep.size() != sel.size()) {
		corners.clear();
		select(keep, SelMode::Set);
	}
}

// the selection, whole groups and entities with their brushes
std::vector<Id> Session::selection_groups() const {
	std::set<Id> all;
	for (Id id : sel) {
		for (Id g : group_of(id)) {
			all.insert(g);
		}
	}
	return std::vector<Id>(all.begin(), all.end());
}

void Session::hide_selection(bool others) {
	if (sel.empty()) {
		say("Select what to hide first");
		return;
	}
	int n = 0;
	if (others) {
		// everything but the selection (and the entities its brushes belong to)
		std::set<Id> keep;
		for (Id id : selection_groups()) {
			keep.insert(id);
			const Object *o = doc.get(id);
			if (o && o->kind != Kind::Entity) {
				keep.insert(o->owner);
			}
		}
		for (Id id : doc.ids()) {
			if (!keep.count(id) && id != doc.worldspawn() && hidden.insert(id).second) {
				n++;
			}
		}
	} else {
		for (Id id : selection_groups()) {
			n += hidden.insert(id).second ? 1 : 0;
		}
		select_none();
	}
	shown_changed();
	say("Hidden: " + std::to_string(n) + " \xc2\xb7 " + std::to_string(hidden.size()) + " in all (Show all brings them back)");
}

void Session::show_all() {
	const size_t n = hidden.size();
	hidden.clear();
	shown_changed();
	say(n ? "Shown again: " + std::to_string(n) : std::string("Nothing was hidden"));
}

void Session::lock_selection() {
	if (sel.empty()) {
		say("Select what to lock first");
		return;
	}
	int n = 0;
	for (Id id : selection_groups()) {
		n += locked.insert(id).second ? 1 : 0;
	}
	select_none();
	shown_changed();
	say("Locked: " + std::to_string(n) + " (drawn, but clicks pass through them; Unlock all frees them)");
}

void Session::unlock_all() {
	const size_t n = locked.size();
	locked.clear();
	shown_changed();
	say(n ? "Unlocked: " + std::to_string(n) : std::string("Nothing was locked"));
}

void Session::toggle_filter(unsigned f) {
	filters ^= f;
	shown_changed();
	static const char *names[] = {"tool brushes", "triggers", "sky", "point entities", "curves"};
	for (int i = 0; i < 5; i++) {
		if (f == (1u << i)) {
			say(std::string((filters & f) ? "Hiding " : "Showing ") + names[i]);
		}
	}
}

void Session::group_selection() {
	bool any = false;
	for (Id id : sel) {
		any |= doc.get(id) && doc.get(id)->kind != Kind::Entity;
	}
	if (!any) {
		say("Select the brushes and curves to group first");
		return;
	}
	make_brush_entity("func_group");
	say("Grouped: a click takes the group, a double-click one brush of it (Ungroup takes it apart)");
}

void Session::ungroup_selection() {
	std::set<Id> groups;
	for (Id id : sel) {
		const Object *o = doc.get(id);
		const Id g = !o ? 0 : o->kind == Kind::Entity ? id : o->owner;
		if (g && g != doc.worldspawn() && doc.get(g) && doc.get(g)->entity.classname() == "func_group") {
			groups.insert(g);
		}
	}
	if (groups.empty()) {
		say("Select a group to take apart");
		return;
	}
	std::vector<Id> freed;
	doc.begin("Ungroup");
	for (Id g : groups) {
		for (Id id : doc.owned(g)) {
			auto n = doc.edit(id);
			n->owner = doc.worldspawn();
			doc.put(id, n);
			freed.push_back(id);
		}
		doc.remove(g);
	}
	doc.end();
	corners.clear();
	select(freed, SelMode::Set);
	say("Ungrouped: " + std::to_string(freed.size()) + (freed.size() == 1 ? " brush" : " brushes") + " back in the world");
}

// ---------------------------------------------------------------- prefabs

static std::string prefab_dir() {
	return std::string(OzEd_HomePath()) + "/oz/editor/prefabs";
}

static std::string prefab_file_name(std::string name) {
	// letters, digits, - and _ (a file name on any system)
	for (char &c : name) {
		if (!std::isalnum((unsigned char)c) && c != '-' && c != '_') {
			c = '_';
		}
	}
	return name;
}

std::vector<std::string> Session::prefab_names() const {
	std::vector<std::string> out;
	int count = 0;
	char **files = OzEd_ListOSFiles(prefab_dir().c_str(), "map", &count);
	for (int i = 0; i < count; i++) {
		std::string n = files[i];
		if (n.size() > 4) {
			out.push_back(n.substr(0, n.size() - 4));
		}
	}
	OzEd_FreeList(files);
	std::sort(out.begin(), out.end());
	return out;
}

bool Session::prefab_save(const std::string &name) {
	const std::string file = prefab_file_name(name);
	if (file.empty()) {
		want_page = "panel prefabs";  // the panel's name field
		say("Name the prefab in the Prefabs panel, then Save selection");
		return false;
	}
	if (sel.empty()) {
		say("Select what the prefab is made of first");
		return false;
	}
	const std::vector<Id> ids = selection_groups();
	const std::set<Id> keep(ids.begin(), ids.end());
	WriteOptions wo;
	wo.title = "a prefab of the Oz Arena map editor: " + file;
	wo.only = [&](Id id) { return keep.count(id) != 0; };
	const std::string text = write_map(doc, wo);
	const std::string path = prefab_dir() + "/" + file + ".map";
	OzEd_MakeDirs(path.c_str());
	if (!OzEd_WriteOSFile(path.c_str(), text.data(), int(text.size()))) {
		say("Can't write " + path, true);
		return false;
	}
	prefab_rev++;
	// its thumbnail drawn again, first in line
	thumb_tried_.erase(file);
	thumb_queue_.erase(std::remove(thumb_queue_.begin(), thumb_queue_.end(), file), thumb_queue_.end());
	thumb_queue_.insert(thumb_queue_.begin(), file);
	say("Saved the prefab " + file + " (" + std::to_string(ids.size()) + " objects): the Prefabs panel puts it in any map");
	return true;
}

// The prefab's thumbnails on disk: "<name>.<stamp>.jpg", the newest last.
static std::vector<std::string> thumb_files(const std::string &file) {
	std::vector<std::pair<unsigned long long, std::string>> found;
	int count = 0;
	char **files = OzEd_ListOSFiles(prefab_dir().c_str(), "jpg", &count);
	for (int i = 0; i < count; i++) {
		const std::string n = files[i];
		if (n.size() > file.size() + 5 && n.compare(0, file.size() + 1, file + ".") == 0) {
			const std::string stamp = n.substr(file.size() + 1, n.size() - file.size() - 5);
			if (!stamp.empty() && std::all_of(stamp.begin(), stamp.end(), [](char c) { return std::isdigit((unsigned char)c); })) {
				found.push_back({std::stoull(stamp), n});
			}
		}
	}
	OzEd_FreeList(files);
	std::sort(found.begin(), found.end());
	std::vector<std::string> out;
	for (auto &f : found) {
		out.push_back(f.second);
	}
	return out;
}

bool Session::prefab_delete(const std::string &name) {
	const std::string file = prefab_file_name(name);
	const std::string path = prefab_dir() + "/" + file + ".map";
	if (file.empty() || !OzEd_FileExists(path.c_str())) {
		say("No prefab " + file, true);
		return false;
	}
	OzEd_RemoveOSFile(path.c_str());
	for (const std::string &t : thumb_files(file)) {
		OzEd_RemoveOSFile((prefab_dir() + "/" + t).c_str());
	}
	thumb_queue_.erase(std::remove(thumb_queue_.begin(), thumb_queue_.end(), file), thumb_queue_.end());
	prefab_rev++;
	say("Deleted the prefab " + file);
	return true;
}

std::string Session::prefab_thumb(const std::string &name) {
	const std::string file = prefab_file_name(name);
	const std::vector<std::string> t = thumb_files(file);
	if (t.empty()) {
		if (!thumb_tried_.count(file) && std::find(thumb_queue_.begin(), thumb_queue_.end(), file) == thumb_queue_.end()) {
			thumb_queue_.push_back(file);  // a prefab from before thumbnails, or one whose drawing failed last run
		}
		return "";
	}
	// the game's path to it: <fs_homepath>/oz is the game's folder
	return "/editor/prefabs/" + t.back();
}

// The screenshot the thumbnail is cut from (the renderer's "screenshot <name>": screenshots/<name>.tga)
static const char *const THUMB_SHOT = "_oz_prefab_thumb";
static std::string thumb_shot_path() {
	return std::string(OzEd_HomePath()) + "/oz/screenshots/" + THUMB_SHOT + ".tga";
}

void Session::prefab_thumb_tick(int x, int y, int w, int h, int now) {
	const ozEdRender_t *r = OzEd_Renderer();
	if (!r) {
		return;
	}
	if (thumb_state_ == 0) {
		if (thumb_queue_.empty() || shot_.state != SHOT_IDLE) {
			return;  // nothing to draw, or the map's own thumbnail is on its way (ed_thumb.cpp)
		}
		if (page_over) {
			return;  // a menu, a list or a dialog of the page is over the view: the screenshot would catch it
		}
		thumb_name_ = thumb_queue_.front();
		thumb_queue_.erase(thumb_queue_.begin());
		thumb_tried_.insert(thumb_name_);
		const std::string path = prefab_dir() + "/" + thumb_name_ + ".map";
		char *text = nullptr;
		const int len = OzEd_ReadOSFile(path.c_str(), &text);
		if (len < 0) {
			return;
		}
		auto d = std::make_shared<Doc>();
		d->tex_size = doc.tex_size;
		ReadReport rep;
		const bool ok = read_map(std::string_view(text, size_t(len)), *d, rep);
		OzEd_Free(text);
		std::vector<Id> ids;
		for (Id id : ok ? d->ids() : std::vector<Id>{}) {
			if (id != d->worldspawn()) {
				ids.push_back(id);
			}
		}
		const Bounds b = bounds_of(*d, ids);
		const int side = std::min({w, h, 320}) - 16;
		if (b.empty() || side < 64) {
			return;
		}
		thumb_doc_ = d;
		thumb_mesh_.include = nullptr;
		thumb_mesh_.changed(*thumb_doc_, {});
		thumb_mesh_.flush(*thumb_doc_);
		// point entities as boxes in the Entities panel's orange
		static const unsigned char ent[4] = {255, 150, 70, 255};
		thumb_ents_.begin();
		for (Id id : thumb_doc_->ids_of(Kind::Entity)) {
			const Object *o = thumb_doc_->get(id);
			Vec3 lo, hi;
			if (id != thumb_doc_->worldspawn() && o && point_box(*o, lo, hi)) {
				thumb_ents_.box_edges(lo, hi, ent);
			}
		}
		thumb_ents_.end();
		// seen from above at three quarters, framed whole
		thumb_cam_ = Camera();
		thumb_cam_.yaw = 45, thumb_cam_.pitch = 30, thumb_cam_.fov = 50;
		Vec3 f, l, u;
		thumb_cam_.axes(f, l, u);
		const double radius = std::max(b.size().length() * 0.5, 32.0);
		thumb_cam_.origin = b.center() - f * (radius / std::sin(thumb_cam_.fov * 0.5 * 3.14159265358979323846 / 180) * 1.05);
		thumb_rect_[0] = x + (w - side) / 2, thumb_rect_[1] = y + (h - side) / 2;
		thumb_rect_[2] = thumb_rect_[3] = side;
		OzEd_RemoveOSFile(thumb_shot_path().c_str());
		OzE_InsertText((std::string("screenshot ") + THUMB_SHOT + "\n").c_str());
		thumb_state_ = 1, thumb_frames_ = 0;
	}

	// drawing: the prefab alone, every frame until the screenshot has it
	const int *rc = thumb_rect_;
	static const float back[4] = {0.07f, 0.065f, 0.06f, 1.0f};
	OzEd_Fill(rc[0], rc[1], rc[2], rc[3], back);
	OzEd_ClearScene();
	static const float zero[3] = {0, 0, 0};
	static const float ident[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
	thumb_mesh_.draw(zero, ident);
	thumb_ents_.draw(zero, ident);
	Vec3 f, l, u;
	thumb_cam_.axes(f, l, u);
	float org[3] = {float(thumb_cam_.origin.x), float(thumb_cam_.origin.y), float(thumb_cam_.origin.z)};
	float axis[3][3] = {{float(f.x), float(f.y), float(f.z)}, {float(l.x), float(l.y), float(l.z)}, {float(u.x), float(u.y), float(u.z)}};
	OzEd_RenderScene(rc[0], rc[1], rc[2], rc[3], org, axis, float(thumb_cam_.fov), float(thumb_cam_.fov), now, 0);
	thumb_frames_++;

	// the screenshot landed: cut the square out of it
	char *shot = nullptr;
	const int len = thumb_frames_ > 1 ? OzEd_ReadOSFile(thumb_shot_path().c_str(), &shot) : -1;
	if (len < 18) {
		if (shot) {
			OzEd_Free(shot);
		}
		if (thumb_frames_ > 240) {
			thumb_state_ = 0;  // never came: tried once this run
			say("The prefab " + thumb_name_ + "'s thumbnail didn't draw", true);
		}
		return;
	}
	const unsigned char *t = (const unsigned char *)shot;
	const int sw = t[12] | t[13] << 8, sh = t[14] | t[15] << 8, idlen = t[0];
	const unsigned char *px = t + 18 + idlen;
	if (t[2] != 2 || t[16] != 24 || len < 18 + idlen + sw * sh * 3) {
		OzEd_Free(shot);  // still being written
		return;
	}
	// the TGA's rows go bottom-up, as the JPEG wants them; top-left screen coordinates turned to its rows
	const int cw = std::min(rc[2], sw - rc[0]), ch = std::min(rc[3], sh - rc[1]);
	if (cw > 0 && ch > 0 && rc[0] >= 0 && rc[1] >= 0) {
		std::vector<unsigned char> rgb(size_t(cw) * ch * 3);
		for (int row = 0; row < ch; row++) {
			const int src_row = sh - 1 - (rc[1] + ch - 1 - row);  // output row 0 is the square's bottom
			const unsigned char *s = px + (size_t(src_row) * sw + rc[0]) * 3;
			unsigned char *o = &rgb[size_t(row) * cw * 3];
			for (int i = 0; i < cw; i++) {
				o[i * 3] = s[i * 3 + 2], o[i * 3 + 1] = s[i * 3 + 1], o[i * 3 + 2] = s[i * 3];
			}
		}
		const std::vector<std::string> old = thumb_files(thumb_name_);
		const std::string out = prefab_dir() + "/" + thumb_name_ + "." + std::to_string((unsigned long long)std::time(nullptr) * 1000ull + (unsigned long long)(OzE_Milliseconds() % 1000)) + ".jpg";
		if (OzEd_SaveJPG(out.c_str(), cw, ch, rgb.data(), 90)) {
			for (const std::string &o : old) {
				OzEd_RemoveOSFile((prefab_dir() + "/" + o).c_str());
			}
			prefab_rev++;
		}
	}
	OzEd_Free(shot);
	OzEd_RemoveOSFile(thumb_shot_path().c_str());
	thumb_state_ = 0;
}

bool Session::prefab_insert(const std::string &name) {
	const std::string file = prefab_file_name(name), path = prefab_dir() + "/" + file + ".map";
	char *text = nullptr;
	const int len = OzEd_ReadOSFile(path.c_str(), &text);
	if (len < 0) {
		say("No prefab " + file, true);
		return false;
	}
	Doc d;
	d.tex_size = doc.tex_size;
	ReadReport rep;
	const bool ok = read_map(std::string_view(text, size_t(len)), d, rep);
	OzEd_Free(text);
	if (!ok) {
		say("The prefab " + file + " doesn't read: " + rep.error, true);
		return false;
	}
	// its world's brushes go into this map's world; its entities come with their own
	std::vector<Id> ids;
	for (Id id : d.ids()) {
		if (id != d.worldspawn()) {
			ids.push_back(id);
		}
	}
	Clip clip = copy_of(d, ids);
	for (auto &c : clip) {
		if (c.second->kind != Kind::Entity && c.second->owner == d.worldspawn()) {
			auto o = std::make_shared<Object>(*c.second);
			o->owner = doc.worldspawn();
			c.second = o;
		}
	}
	if (clip.empty()) {
		say("The prefab " + file + " is empty", true);
		return false;
	}
	paste_clip(clip, "Insert " + file, true);
	return true;
}

}  // namespace oz::ed
