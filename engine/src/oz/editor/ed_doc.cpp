#include "ed_doc.h"
#include "ed_texmap.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace oz::ed {

// ---------------------------------------------------------------- shader names

namespace {
struct ShaderTable {
	std::vector<std::string> names{""};
	std::unordered_map<std::string, ShaderId> ids{{"", 0}};
};
ShaderTable &shaders() {
	static ShaderTable t;
	return t;
}
}  // namespace

ShaderId shader_id(const std::string &name) {
	ShaderTable &t = shaders();
	auto it = t.ids.find(name);
	if (it != t.ids.end()) {
		return it->second;
	}
	ShaderId id = ShaderId(t.names.size());
	t.names.push_back(name);
	t.ids.emplace(name, id);
	return id;
}

const std::string &shader_name(ShaderId id) {
	ShaderTable &t = shaders();
	return id >= 0 && size_t(id) < t.names.size() ? t.names[id] : t.names[0];
}

// ---------------------------------------------------------------- entities

const std::string *Entity::find(const std::string &key) const {
	for (const auto &kv : keys) {
		if (kv.first == key) {
			return &kv.second;
		}
	}
	return nullptr;
}

std::string Entity::get(const std::string &key, const std::string &fallback) const {
	const std::string *v = find(key);
	return v ? *v : fallback;
}

void Entity::set(const std::string &key, const std::string &value) {
	for (auto &kv : keys) {
		if (kv.first == key) {
			kv.second = value;
			return;
		}
	}
	keys.emplace_back(key, value);
}

bool Entity::erase(const std::string &key) {
	auto it = std::find_if(keys.begin(), keys.end(), [&](const auto &kv) { return kv.first == key; });
	if (it == keys.end()) {
		return false;
	}
	keys.erase(it);
	return true;
}

// ---------------------------------------------------------------- objects

const Object *Doc::get(Id id) const {
	auto it = objects_.find(id);
	return it == objects_.end() ? nullptr : it->second.get();
}

ObjectPtr Doc::ptr(Id id) const {
	auto it = objects_.find(id);
	return it == objects_.end() ? nullptr : it->second;
}

std::shared_ptr<Object> Doc::edit(Id id) const {
	const Object *o = get(id);
	return o ? std::make_shared<Object>(*o) : nullptr;
}

Id Doc::add(std::shared_ptr<Object> obj) {
	Id id = next_++;
	if (obj->kind == Kind::Entity && world_ == 0) {
		world_ = id;  // q3map takes the first entity as the world, whatever its classname
	}
	put(id, std::move(obj));
	return id;
}

void Doc::put(Id id, std::shared_ptr<Object> obj) {
	ObjectPtr after = std::move(obj);
	auto it = objects_.find(id);
	ObjectPtr before = it == objects_.end() ? nullptr : it->second;
	objects_[id] = after;
	next_ = std::max(next_, id + 1);
	record(id, before, after);
}

void Doc::remove(Id id) {
	auto it = objects_.find(id);
	if (it == objects_.end()) {
		return;
	}
	ObjectPtr before = it->second;
	objects_.erase(it);
	windings_.erase(id);
	record(id, before, nullptr);
}

std::vector<Id> Doc::ids() const {
	std::vector<Id> out;
	out.reserve(objects_.size());
	for (const auto &kv : objects_) {
		out.push_back(kv.first);
	}
	std::sort(out.begin(), out.end());
	return out;
}

std::vector<Id> Doc::ids_of(Kind kind) const {
	std::vector<Id> out;
	for (const auto &kv : objects_) {
		if (kv.second->kind == kind) {
			out.push_back(kv.first);
		}
	}
	std::sort(out.begin(), out.end());
	return out;
}

std::vector<Id> Doc::owned(Id entity) const {
	std::vector<Id> out;
	for (const auto &kv : objects_) {
		if (kv.second->kind != Kind::Entity && kv.second->owner == entity) {
			out.push_back(kv.first);
		}
	}
	std::sort(out.begin(), out.end());
	return out;
}

// ---------------------------------------------------------------- undo

void Doc::begin(const std::string &label) {
	if (depth_++ == 0) {
		open_ = Step{label, {}, {}, {}};
		open_index_.clear();
		if (get_selection) {
			open_.sel_before = get_selection();
		}
	}
}

void Doc::end() {
	if (depth_ == 0 || --depth_ > 0) {
		return;
	}
	// changes that ended where they began cancel out
	std::vector<Change> kept;
	std::vector<Id> touched;
	for (Change &c : open_.changes) {
		if (c.before != c.after) {
			touched.push_back(c.id);
			kept.push_back(std::move(c));
		}
	}
	open_.changes = std::move(kept);
	if (!open_.changes.empty()) {
		undo_.push_back(std::move(open_));
		if (undo_.size() > history_limit) {
			undo_.erase(undo_.begin());
		}
		redo_.clear();
		announce(std::move(touched));
	}
	open_ = Step{};
	open_index_.clear();
}

void Doc::record(Id id, ObjectPtr before, ObjectPtr after) {
	dirty = true;
	revision++;
	if (depth_ == 0) {
		// loading, or a change outside any step: it can't be undone, so the history before it is void
		if (!undo_.empty() || !redo_.empty()) {
			clear_history();
		}
		announce({id});
		return;
	}
	auto it = open_index_.find(id);
	if (it != open_index_.end()) {
		open_.changes[it->second].after = std::move(after);
	} else {
		open_index_[id] = open_.changes.size();
		open_.changes.push_back(Change{id, std::move(before), std::move(after)});
	}
}

void Doc::apply(const Step &s, bool forward) {
	std::vector<Id> touched;
	for (const Change &c : s.changes) {
		const ObjectPtr &o = forward ? c.after : c.before;
		if (o) {
			objects_[c.id] = o;
		} else {
			objects_.erase(c.id);
		}
		windings_.erase(c.id);
		touched.push_back(c.id);
	}
	dirty = true;
	revision++;
	announce(std::move(touched));
}

bool Doc::undo() {
	if (undo_.empty() || depth_ > 0) {
		return false;
	}
	Step s = std::move(undo_.back());
	undo_.pop_back();
	if (get_selection) {
		s.sel_after = get_selection();
	}
	apply(s, false);
	if (set_selection) {
		set_selection(s.sel_before);
	}
	redo_.push_back(std::move(s));
	return true;
}

bool Doc::redo() {
	if (redo_.empty() || depth_ > 0) {
		return false;
	}
	Step s = std::move(redo_.back());
	redo_.pop_back();
	apply(s, true);
	if (set_selection) {
		set_selection(s.sel_after);
	}
	undo_.push_back(std::move(s));
	return true;
}

std::string Doc::undo_label() const {
	return undo_.empty() ? "" : undo_.back().label;
}

std::string Doc::redo_label() const {
	return redo_.empty() ? "" : redo_.back().label;
}

std::vector<std::string> Doc::undo_labels() const {
	std::vector<std::string> out;
	for (const Step &s : undo_) {
		out.push_back(s.label);
	}
	return out;
}

std::vector<std::string> Doc::redo_labels() const {
	std::vector<std::string> out;
	for (auto it = redo_.rbegin(); it != redo_.rend(); ++it) {
		out.push_back(it->label);
	}
	return out;
}

void Doc::clear_history() {
	undo_.clear();
	redo_.clear();
}

void Doc::announce(std::vector<Id> ids) {
	if (on_change) {
		on_change(ids);
	}
}

// ---------------------------------------------------------------- faces

static bool is_merged_class(const std::string &cn) {
	return cn == "worldspawn" || cn == "func_group" || cn == "group_info";
}

Vec3 Doc::model_origin(Id entity) const {
	const Object *e = get(entity);
	if (!e || e->kind != Kind::Entity || is_merged_class(e->entity.classname())) {
		return Vec3();
	}
	for (Id id : owned(entity)) {
		const Object *o = get(id);
		if (o->kind != Kind::Brush) {
			continue;
		}
		for (const Face &f : o->brush.faces) {
			if (shader_name(f.shader) == "common/origin" || shader_name(f.shader) == "textures/common/origin") {
				Vec3 lo(1e30, 1e30, 1e30), hi(-1e30, -1e30, -1e30);
				for (const q3map::Winding &w : windings(id)) {
					for (const Vec3 &p : w) {
						lo = Vec3(std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z));
						hi = Vec3(std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z));
					}
				}
				if (lo.x > hi.x) {
					break;
				}
				return Vec3(std::trunc((lo.x + hi.x) * 0.5), std::trunc((lo.y + hi.y) * 0.5), std::trunc((lo.z + hi.z) * 0.5));
			}
		}
	}
	const std::string *org = e->entity.find("origin");
	Vec3 v;
	if (org && std::sscanf(org->c_str(), "%lf %lf %lf", &v.x, &v.y, &v.z) == 3) {
		return v;
	}
	return Vec3();
}

void Doc::tex_dims(ShaderId shader, int &w, int &h) const {
	w = 64, h = 64;
	if (tex_size) {
		tex_size(shader, w, h);
	}
	w = w > 0 ? w : 64;
	h = h > 0 ? h : 64;
}

// The face's plane where q3map maps it: moved back by the entity's origin.
static Plane model_plane(const Plane &p, const Vec3 &origin) {
	return Plane{p.n, p.d - p.n.dot(origin)};
}

void Doc::derive_st(Face &f, const Vec3 &origin) const {
	if (!f.raw_valid) {
		return;
	}
	int w, h;
	tex_dims(f.shader, w, h);
	Plane mp = model_plane(f.plane, origin);
	switch (f.form) {
	case TexForm::Quake:
		f.st = st_from_quake(f.raw, mp, w, h);
		break;
	case TexForm::Primitives:
		f.st = st_from_primitives(f.raw, mp);
		break;
	case TexForm::Valve:
		f.st = st_from_valve(f.raw, w, h);
		break;
	}
	// texels at p - origin are the editor's at p
	f.st.s0 -= f.st.s.dot(origin);
	f.st.t0 -= f.st.t.dot(origin);
}

void Doc::derive_raw(Face &f, TexForm form, const Vec3 &origin, const q3map::Winding &w) const {
	int tw, th;
	tex_dims(f.shader, tw, th);
	StAxes ms = f.st;
	ms.s0 += ms.s.dot(origin);
	ms.t0 += ms.t.dot(origin);
	Plane mp = model_plane(f.plane, origin);
	f.raw_valid = true;
	if (form == TexForm::Quake) {
		q3map::Winding mw = w;
		for (Vec3 &p : mw) {
			p = p - origin;
		}
		if (quake_from_st(ms, mp, mw, tw, th, f.raw)) {
			f.form = TexForm::Quake;
			return;
		}
		form = TexForm::Primitives;  // a mapping the Quake form can't hold
	}
	f.form = form;
	if (form == TexForm::Primitives) {
		primitives_from_st(ms, mp, f.raw);
	} else {
		valve_from_st(ms, tw, th, f.raw);
	}
}

void Doc::plane_points(const Face &f, const q3map::Winding &w, Vec3 out[3]) const {
	if (f.pts_valid || w.size() < 3) {
		for (int i = 0; i < 3; i++) {
			out[i] = f.pts[i];
		}
		return;
	}
	// the best-conditioned triple (the largest triangle), ordered so (p0 - p1) x (p2 - p1) points out
	size_t best[3] = {0, 1, 2};
	double best_area = -1;
	for (size_t i = 0; i < w.size(); i++) {
		for (size_t j = i + 1; j < w.size(); j++) {
			for (size_t k = j + 1; k < w.size(); k++) {
				Vec3 c = (w[i] - w[j]).cross(w[k] - w[j]);
				double a = c.dot(c);
				if (a > best_area) {
					best_area = a;
					best[0] = i, best[1] = j, best[2] = k;
				}
			}
		}
	}
	Vec3 c = (w[best[0]] - w[best[1]]).cross(w[best[2]] - w[best[1]]);
	if (c.dot(f.plane.n) < 0) {
		std::swap(best[0], best[2]);
	}
	for (int i = 0; i < 3; i++) {
		out[i] = w[best[i]];
	}
}

const std::vector<q3map::Winding> &Doc::windings(Id id) const {
	ObjectPtr o = ptr(id);
	auto it = windings_.find(id);
	if (it != windings_.end() && it->second.first == o) {
		return it->second.second;
	}
	std::vector<Plane> planes;
	if (o && o->kind == Kind::Brush) {
		planes.reserve(o->brush.faces.size());
		for (const Face &f : o->brush.faces) {
			planes.push_back(f.plane);
		}
	}
	auto &slot = windings_[id];
	slot.first = o;
	slot.second = q3map::brush_windings(planes);
	return slot.second;
}

Plane plane_from_points(const Vec3 &p0, const Vec3 &p1, const Vec3 &p2) {
	Vec3 n = (p0 - p1).cross(p2 - p1);
	double l = n.length();
	if (l <= 0) {
		return Plane{};
	}
	n = n * (1 / l);
	return q3map::clean_plane(Plane{n, p0.dot(n)});
}

}  // namespace oz::ed
