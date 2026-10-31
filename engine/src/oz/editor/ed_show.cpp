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

#include <algorithm>
#include <cctype>

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
		say("ed prefab_save <name>: a name for it", true);
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
	say("Saved the prefab " + file + " (" + std::to_string(ids.size()) + " objects): the Entities panel puts it in any map");
	return true;
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
	paste_clip(clip, "Insert " + file);
	return true;
}

}  // namespace oz::ed
