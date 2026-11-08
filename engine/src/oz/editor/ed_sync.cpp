#include "ed_sync.h"

#include <algorithm>
#include <cstring>

#ifdef OZ_ED_MINIZ
#include "miniz.h"
#endif

namespace oz::ed::sync {

// ---------------------------------------------------------------- bytes

void Writer::var(uint64_t v) {
	while (v >= 0x80) {
		out.push_back(char(uint8_t(v) | 0x80));
		v >>= 7;
	}
	out.push_back(char(uint8_t(v)));
}

void Writer::i32(int32_t v) {
	var((uint32_t(v) << 1) ^ uint32_t(v >> 31));
}

void Writer::f64(double v) {
	uint64_t b;
	std::memcpy(&b, &v, 8);
	for (int i = 0; i < 8; i++) {
		out.push_back(char(uint8_t(b >> (8 * i))));
	}
}

void Writer::str(const std::string &s) {
	var(s.size());
	out += s;
}

uint8_t Reader::u8() {
	if (!ok || p >= end) {
		ok = false;
		return 0;
	}
	return *p++;
}

uint64_t Reader::var() {
	uint64_t v = 0;
	for (int shift = 0; shift < 64; shift += 7) {
		const uint8_t b = u8();
		if (!ok) {
			return 0;
		}
		v |= uint64_t(b & 0x7f) << shift;
		if (!(b & 0x80)) {
			return v;
		}
	}
	ok = false;
	return 0;
}

int32_t Reader::i32() {
	const uint32_t z = uint32_t(var());
	return int32_t((z >> 1) ^ (~(z & 1) + 1));
}

double Reader::f64() {
	if (!ok || end - p < 8) {
		ok = false;
		return 0;
	}
	uint64_t b = 0;
	for (int i = 0; i < 8; i++) {
		b |= uint64_t(p[i]) << (8 * i);
	}
	p += 8;
	double v;
	std::memcpy(&v, &b, 8);
	return v;
}

std::string Reader::str() {
	const uint64_t n = var();
	if (!ok || uint64_t(end - p) < n) {
		ok = false;
		return {};
	}
	std::string s(reinterpret_cast<const char *>(p), size_t(n));
	p += n;
	return s;
}

Bytes Reader::bytes() {
	return str();
}

// ---------------------------------------------------------------- objects

namespace {

void put_vec(Writer &w, const Vec3 &v) {
	w.f64(v.x), w.f64(v.y), w.f64(v.z);
}

Vec3 get_vec(Reader &r) {
	Vec3 v;
	v.x = r.f64(), v.y = r.f64(), v.z = r.f64();
	return v;
}

enum : uint8_t { F_RAW = 1, F_PTS = 2, F_LOCK = 4, F_FLAGS = 8 };

void put_face(Writer &w, const Face &f) {
	put_vec(w, f.plane.n);
	w.f64(f.plane.d);
	w.str(shader_name(f.shader));
	put_vec(w, f.st.s);
	put_vec(w, f.st.t);
	w.f64(f.st.s0), w.f64(f.st.t0);
	w.u8(uint8_t(f.form));
	const uint8_t flags = (f.raw_valid ? F_RAW : 0) | (f.pts_valid ? F_PTS : 0) | (f.lock ? F_LOCK : 0) | (f.has_flags ? F_FLAGS : 0);
	w.u8(flags);
	for (double v : f.raw) {
		w.f64(v);  // all eleven, whatever the form: the copy is the same object
	}
	if (f.pts_valid) {
		for (const Vec3 &p : f.pts) {
			put_vec(w, p);
		}
	}
	w.i32(f.contents), w.i32(f.flags), w.i32(f.value);  // kept when the line had none too (a copy is the same)
}

bool get_face(Reader &r, Face &f) {
	f.plane.n = get_vec(r);
	f.plane.d = r.f64();
	f.shader = shader_id(r.str());
	f.st.s = get_vec(r);
	f.st.t = get_vec(r);
	f.st.s0 = r.f64(), f.st.t0 = r.f64();
	const uint8_t form = r.u8();
	if (form > uint8_t(TexForm::Valve)) {
		r.ok = false;
	}
	f.form = TexForm(form);
	const uint8_t flags = r.u8();
	f.raw_valid = (flags & F_RAW) != 0;
	f.pts_valid = (flags & F_PTS) != 0;
	f.lock = (flags & F_LOCK) != 0;
	f.has_flags = (flags & F_FLAGS) != 0;
	for (double &v : f.raw) {
		v = r.f64();
	}
	if (f.pts_valid) {
		for (Vec3 &p : f.pts) {
			p = get_vec(r);
		}
	}
	f.contents = r.i32(), f.flags = r.i32(), f.value = r.i32();
	return r.ok;
}

bool same_vec(const Vec3 &a, const Vec3 &b) {
	return std::memcmp(&a.x, &b.x, 8) == 0 && std::memcmp(&a.y, &b.y, 8) == 0 && std::memcmp(&a.z, &b.z, 8) == 0;
}

bool same_num(double a, double b) {
	return std::memcmp(&a, &b, 8) == 0;  // bit for bit: -0 and NaNs too
}

bool same_face(const Face &a, const Face &b) {
	if (!same_vec(a.plane.n, b.plane.n) || !same_num(a.plane.d, b.plane.d) || a.shader != b.shader) {
		return false;
	}
	if (!same_vec(a.st.s, b.st.s) || !same_vec(a.st.t, b.st.t) || !same_num(a.st.s0, b.st.s0) || !same_num(a.st.t0, b.st.t0)) {
		return false;
	}
	if (a.form != b.form || a.raw_valid != b.raw_valid || a.pts_valid != b.pts_valid || a.lock != b.lock || a.has_flags != b.has_flags) {
		return false;
	}
	for (int i = 0; i < 11; i++) {
		if (!same_num(a.raw[i], b.raw[i])) {
			return false;
		}
	}
	if (a.pts_valid) {
		for (int i = 0; i < 3; i++) {
			if (!same_vec(a.pts[i], b.pts[i])) {
				return false;
			}
		}
	}
	return a.contents == b.contents && a.flags == b.flags && a.value == b.value;
}

}  // namespace

void put_object(Writer &w, const Object &o) {
	w.u8(uint8_t(o.kind));
	w.i32(o.owner);
	switch (o.kind) {
	case Kind::Entity:
		w.var(o.entity.keys.size());
		for (const auto &kv : o.entity.keys) {
			w.str(kv.first);
			w.str(kv.second);
		}
		break;
	case Kind::Brush:
		w.var(o.brush.faces.size());
		for (const Face &f : o.brush.faces) {
			put_face(w, f);
		}
		break;
	case Kind::Patch:
		w.str(shader_name(o.patch.shader));
		w.u8(o.patch.def3 ? 1 : 0);
		for (double v : o.patch.info) {
			w.f64(v);
		}
		w.i32(o.patch.width), w.i32(o.patch.height);
		w.var(o.patch.ctrl.size());
		for (double v : o.patch.ctrl) {
			w.f64(v);
		}
		break;
	}
}

ObjectPtr get_object(Reader &r) {
	auto o = std::make_shared<Object>();
	const uint8_t kind = r.u8();
	if (kind > uint8_t(Kind::Patch)) {
		r.ok = false;
		return nullptr;
	}
	o->kind = Kind(kind);
	o->owner = r.i32();
	switch (o->kind) {
	case Kind::Entity: {
		const uint64_t n = r.var();
		if (n > r.left()) {
			r.ok = false;
			return nullptr;
		}
		for (uint64_t i = 0; i < n && r.ok; i++) {
			std::string k = r.str();
			std::string v = r.str();
			o->entity.keys.emplace_back(std::move(k), std::move(v));
		}
		break;
	}
	case Kind::Brush: {
		const uint64_t n = r.var();
		if (n > r.left()) {
			r.ok = false;
			return nullptr;
		}
		o->brush.faces.resize(size_t(n));
		for (Face &f : o->brush.faces) {
			if (!get_face(r, f)) {
				return nullptr;
			}
		}
		break;
	}
	case Kind::Patch: {
		o->patch.shader = shader_id(r.str());
		o->patch.def3 = r.u8() != 0;
		for (double &v : o->patch.info) {
			v = r.f64();
		}
		o->patch.width = r.i32(), o->patch.height = r.i32();
		const uint64_t n = r.var();
		if (n > r.left() / 8) {
			r.ok = false;
			return nullptr;
		}
		o->patch.ctrl.resize(size_t(n));
		for (double &v : o->patch.ctrl) {
			v = r.f64();
		}
		break;
	}
	}
	return r.ok ? ObjectPtr(o) : nullptr;
}

bool same_object(const Object &a, const Object &b) {
	if (a.kind != b.kind || a.owner != b.owner) {
		return false;
	}
	switch (a.kind) {
	case Kind::Entity:
		return a.entity.keys == b.entity.keys;
	case Kind::Brush:
		if (a.brush.faces.size() != b.brush.faces.size()) {
			return false;
		}
		for (size_t i = 0; i < a.brush.faces.size(); i++) {
			if (!same_face(a.brush.faces[i], b.brush.faces[i])) {
				return false;
			}
		}
		return true;
	case Kind::Patch:
		if (a.patch.shader != b.patch.shader || a.patch.def3 != b.patch.def3 || a.patch.width != b.patch.width ||
			a.patch.height != b.patch.height || a.patch.ctrl.size() != b.patch.ctrl.size()) {
			return false;
		}
		for (int i = 0; i < 7; i++) {
			if (!same_num(a.patch.info[i], b.patch.info[i])) {
				return false;
			}
		}
		for (size_t i = 0; i < a.patch.ctrl.size(); i++) {
			if (!same_num(a.patch.ctrl[i], b.patch.ctrl[i])) {
				return false;
			}
		}
		return true;
	}
	return false;
}

// ---------------------------------------------------------------- deflate

Bytes pack(const Bytes &raw) {
#ifdef OZ_ED_MINIZ
	if (raw.size() > 256) {
		mz_ulong bound = mz_compressBound(mz_ulong(raw.size()));
		Bytes out(size_t(bound) + 9, '\0');
		mz_ulong len = bound;
		if (mz_compress2(reinterpret_cast<unsigned char *>(&out[9]), &len, reinterpret_cast<const unsigned char *>(raw.data()),
				mz_ulong(raw.size()), 6) == MZ_OK && len + 9 < raw.size()) {
			out[0] = 1;
			const uint64_t n = raw.size();
			for (int i = 0; i < 8; i++) {
				out[1 + i] = char(uint8_t(n >> (8 * i)));
			}
			out.resize(size_t(len) + 9);
			return out;
		}
	}
#endif
	return std::string(1, '\0') + raw;
}

bool unpack(const Bytes &packed, Bytes &raw) {
	if (packed.empty()) {
		return false;
	}
	if (packed[0] == 0) {
		raw.assign(packed, 1, std::string::npos);
		return true;
	}
#ifdef OZ_ED_MINIZ
	if (packed[0] == 1 && packed.size() >= 9) {
		uint64_t n = 0;
		for (int i = 0; i < 8; i++) {
			n |= uint64_t(uint8_t(packed[1 + i])) << (8 * i);
		}
		if (n > (uint64_t(1) << 31)) {
			return false;
		}
		raw.assign(size_t(n), '\0');
		mz_ulong len = mz_ulong(n);
		return mz_uncompress(reinterpret_cast<unsigned char *>(&raw[0]), &len, reinterpret_cast<const unsigned char *>(packed.data() + 9),
				   mz_ulong(packed.size() - 9)) == MZ_OK && len == n;
	}
#endif
	return false;
}

// ---------------------------------------------------------------- edits

void put_edit(Writer &w, const Edit &e, bool to_host) {
	w.var(e.seq);
	w.var(e.player);
	w.var(e.local);
	w.u8(e.refused ? 1 : 0);
	w.str(e.label);
	w.var(e.objects.size());
	for (const ObjEdit &o : e.objects) {
		w.i32(o.id);
		const bool before = to_host && o.before;
		w.u8((o.after ? 1 : 0) | (before ? 2 : 0) | (o.changed ? 4 : 0));
		if (o.after) {
			put_object(w, *o.after);
		}
		if (before) {
			put_object(w, *o.before);
		}
	}
}

bool get_edit(Reader &r, Edit &e) {
	e = Edit{};
	e.seq = uint32_t(r.var());
	e.player = uint32_t(r.var());
	e.local = uint32_t(r.var());
	e.refused = r.u8() != 0;
	e.label = r.str();
	const uint64_t n = r.var();
	if (!r.ok || n > r.left()) {
		return false;
	}
	e.objects.resize(size_t(n));
	for (ObjEdit &o : e.objects) {
		o.id = r.i32();
		const uint8_t has = r.u8();
		o.changed = (has & 4) != 0;
		if (has & 1) {
			o.after = get_object(r);
			if (!o.after) {
				return false;
			}
		}
		if (has & 2) {
			o.before = get_object(r);
			if (!o.before) {
				return false;
			}
		}
	}
	return r.ok;
}

// ---------------------------------------------------------------- the replica

void Replica::start(uint32_t me, uint32_t seq) {
	me_ = me;
	seq_ = seq;
	local_ = 0;
	state_.clear();
	for (Id id : doc_.ids()) {
		state_[id] = doc_.ptr(id);
	}
	pending_.clear();
	sent_.clear();
	held_.clear();
	log.clear();
	doc_.shared = true;
	doc_.on_local = [this](const std::string &label, const std::vector<Doc::Change> &changes) { local(label, changes); };
	running_ = true;
}

void Replica::stop() {
	if (!running_) {
		return;
	}
	running_ = false;
	doc_.on_local = nullptr;
	doc_.shared = false;
	doc_.lease(0, 0);
	pending_.clear();
	sent_.clear();
	held_.clear();
}

ObjectPtr Replica::state_of(Id id) const {
	auto it = state_.find(id);
	return it == state_.end() ? nullptr : it->second;
}

void Replica::local(const std::string &label, const std::vector<Doc::Change> &changes) {
	Edit e;
	e.player = me_;
	e.local = ++local_;
	e.label = label;
	for (const Doc::Change &c : changes) {
		ObjEdit o;
		o.id = c.id;
		o.after = c.after;
		if (c.after && c.after->kind == Kind::Entity && c.before && c.before->kind == Kind::Entity) {
			o.before = c.before;  // the host merges this player's keys over anyone else's
		}
		e.objects.push_back(std::move(o));
		pending_[c.id]++;
	}
	sent_[e.local] = e;
	if (send) {
		send(e);
	}
}

void Replica::show(const std::vector<Id> &ids) {
	std::vector<std::pair<Id, ObjectPtr>> put;
	for (Id id : ids) {
		if (pending_.count(id) || doc_.stepping(id)) {
			held_.insert(id);  // this player's own change is on its way, or still being made: it comes after
			continue;
		}
		held_.erase(id);
		ObjectPtr want = state_of(id);
		if (doc_.ptr(id) != want) {
			put.emplace_back(id, want);
			took_.push_back(id);
		}
	}
	if (!put.empty()) {
		doc_.apply_remote(put);
	}
}

void Replica::receive(const Edit &e) {
	if (!e.refused) {
		if (e.seq <= seq_) {
			return;  // had it (a new host's replay overlapping)
		}
		seq_ = e.seq;
	}
	took_.clear();
	std::vector<Id> ids;
	auto mine = e.player == me_ ? sent_.find(e.local) : sent_.end();
	if (mine != sent_.end()) {
		// this player's own edit, back in its place: the objects as sent, or as the host changed them
		std::unordered_map<Id, ObjectPtr> changed;
		for (const ObjEdit &o : e.objects) {
			if (o.changed) {
				changed[o.id] = o.after;
			}
		}
		for (const ObjEdit &o : mine->second.objects) {
			if (!e.refused) {
				auto c = changed.find(o.id);
				ObjectPtr v = c != changed.end() ? c->second : o.after;
				if (v) {
					state_[o.id] = v;
				} else {
					state_.erase(o.id);
				}
				if (c != changed.end()) {
					changed.erase(c);
				}
			}
			auto p = pending_.find(o.id);
			if (p != pending_.end() && --p->second <= 0) {
				pending_.erase(p);
			}
			ids.push_back(o.id);
		}
		// objects the host added to it (a removed entity's brushes it didn't know of, given to the world)
		for (auto &kv : changed) {
			if (kv.second) {
				state_[kv.first] = kv.second;
			} else {
				state_.erase(kv.first);
			}
			ids.push_back(kv.first);
		}
		sent_.erase(mine);
	} else {
		for (const ObjEdit &o : e.objects) {
			if (o.after) {
				state_[o.id] = o.after;
			} else {
				state_.erase(o.id);
			}
			ids.push_back(o.id);
		}
	}
	if (!e.refused) {
		log.push_back(Entry{e.seq, e.player, e.label});
		if (log.size() > log_limit) {
			log.erase(log.begin(), log.begin() + long(log.size() - log_limit));
		}
	}
	show(ids);
	if (on_remote && !took_.empty()) {
		on_remote(e, took_);
	}
}

void Replica::tick() {
	if (held_.empty() || doc_.in_step()) {
		return;
	}
	took_.clear();
	std::vector<Id> ids(held_.begin(), held_.end());
	show(ids);
	if (on_remote && !took_.empty()) {
		Edit none;
		on_remote(none, took_);
	}
}

void Replica::snapshot(Writer &w) const {
	w.var(seq_);
	w.i32(doc_.worldspawn());
	std::vector<Id> ids;
	ids.reserve(state_.size());
	for (const auto &kv : state_) {
		ids.push_back(kv.first);
	}
	std::sort(ids.begin(), ids.end());
	w.var(ids.size());
	for (Id id : ids) {
		w.i32(id);
		put_object(w, *state_.at(id));
	}
}

bool Replica::read_snapshot(Reader &r, uint32_t &seq, Id &world, std::vector<std::pair<Id, ObjectPtr>> &objects) {
	seq = uint32_t(r.var());
	world = r.i32();
	const uint64_t n = r.var();
	if (!r.ok || n > r.left()) {
		return false;
	}
	objects.clear();
	objects.reserve(size_t(n));
	for (uint64_t i = 0; i < n && r.ok; i++) {
		const Id id = r.i32();
		ObjectPtr o = get_object(r);
		if (!o) {
			return false;
		}
		objects.emplace_back(id, std::move(o));
	}
	return r.ok;
}

void Replica::resend(Edit e) {
	e.local = ++local_;
	e.seq = 0;
	e.player = me_;
	for (const ObjEdit &o : e.objects) {
		pending_[o.id]++;
	}
	sent_[e.local] = e;
	if (send) {
		send(e);
	}
}

std::vector<Edit> Replica::unanswered() const {
	std::vector<Edit> out;
	for (const auto &kv : sent_) {
		out.push_back(kv.second);
	}
	return out;
}

// ---------------------------------------------------------------- the host

Object merge_keys(const Object &cur, const Object &before, const Object &after) {
	Object out = cur;
	for (const auto &kv : after.entity.keys) {
		const std::string *was = before.entity.find(kv.first);
		if (!was || *was != kv.second) {
			out.entity.set(kv.first, kv.second);
		}
	}
	for (const auto &kv : before.entity.keys) {
		if (!after.entity.find(kv.first)) {
			out.entity.erase(kv.first);
		}
	}
	return out;
}

void Host::take(const Edit &in) {
	Edit e = in;
	if (viewers_.count(e.player)) {
		e.seq = 0;  // not in the order: only its maker hears of it
		e.refused = true;
		for (ObjEdit &o : e.objects) {
			o.after = mine_.state_of(o.id);  // what the session has: the viewer shows it again
			o.before = nullptr;
			o.changed = true;
		}
		if (deliver) {
			deliver(e);
		}
		return;
	}
	e.seq = ++seq_;
	const auto &state = mine_.state();
	auto now = [&](Id id) -> ObjectPtr {
		auto it = state.find(id);
		return it == state.end() ? nullptr : it->second;
	};
	// what this edit leaves, by id, for the owners' check
	std::unordered_map<Id, ObjectPtr> leaves;
	for (const ObjEdit &o : e.objects) {
		leaves[o.id] = o.after;
	}
	auto entity_there = [&](Id id) {
		auto it = leaves.find(id);
		const ObjectPtr o = it != leaves.end() ? it->second : now(id);
		return o && o->kind == Kind::Entity;
	};
	const Id world = mine_.world();
	for (ObjEdit &o : e.objects) {
		const ObjectPtr cur = now(o.id);
		if (o.after && o.after->kind == Kind::Entity && o.before && cur && cur->kind == Kind::Entity && !same_object(*cur, *o.before)) {
			// someone else changed this entity since its maker saw it: their keys stay, this one's go on top
			auto merged = std::make_shared<Object>(merge_keys(*cur, *o.before, *o.after));
			if (!same_object(*merged, *o.after)) {
				o.after = merged;
				o.changed = true;
			}
		}
		if (o.after && o.after->kind != Kind::Entity && o.after->owner && !entity_there(o.after->owner)) {
			// its entity is gone (someone removed it meanwhile): the world takes it
			auto moved = std::make_shared<Object>(*o.after);
			moved->owner = world;
			o.after = moved;
			o.changed = true;
		}
		o.before = nullptr;
	}
	// an entity removed: its brushes and curves its maker didn't know of (someone else's, made meanwhile) go to the world
	std::vector<ObjEdit> extra;
	for (const ObjEdit &o : e.objects) {
		const ObjectPtr cur = now(o.id);
		if (o.after || !cur || cur->kind != Kind::Entity) {
			continue;
		}
		for (const auto &kv : state) {
			if (kv.second->kind != Kind::Entity && kv.second->owner == o.id && !leaves.count(kv.first)) {
				auto moved = std::make_shared<Object>(*kv.second);
				moved->owner = world;
				ObjEdit x;
				x.id = kv.first;
				x.after = moved;
				x.changed = true;
				extra.push_back(std::move(x));
			}
		}
	}
	for (ObjEdit &x : extra) {
		e.objects.push_back(std::move(x));
	}
	if (deliver) {
		deliver(e);
	}
}

void Host::grant(Id &from, Id &end) {
	from = next_block_;
	end = next_block_ + BLOCK;
	next_block_ = end;
}

void Host::set_viewer(uint32_t player, bool viewer) {
	if (viewer) {
		viewers_.insert(player);
	} else {
		viewers_.erase(player);
	}
}

Edit Host::ack(const Edit &e) {
	Edit a = e;
	a.objects.clear();
	for (const ObjEdit &o : e.objects) {
		if (o.changed) {
			a.objects.push_back(o);
		}
	}
	return a;
}

bool Host::claim(uint32_t player, const std::vector<Id> &ids, uint32_t &holder) {
	for (Id id : ids) {
		auto it = claims_.find(id);
		if (it != claims_.end() && it->second != player) {
			holder = it->second;
			return false;
		}
	}
	release(player);
	for (Id id : ids) {
		claims_[id] = player;
	}
	return true;
}

void Host::release(uint32_t player) {
	for (auto it = claims_.begin(); it != claims_.end();) {
		it = it->second == player ? claims_.erase(it) : std::next(it);
	}
}

uint32_t Host::claimed_by(Id id) const {
	auto it = claims_.find(id);
	return it == claims_.end() ? 0 : it->second;
}

}  // namespace oz::ed::sync
