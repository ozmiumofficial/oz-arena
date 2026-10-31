#include "ed_entdefs.h"
#include "ed_ops.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <unordered_map>

namespace oz::ed {

static EntityDef D(const char *name, const char *section, const char *mod, const char *help, bool brush, bool faces, Vec3 lo, Vec3 hi,
		Vec3 color, std::vector<std::string> models, const char *icon, double scale, bool item, std::vector<KeyDef> keys,
		std::vector<FlagDef> flags) {
	EntityDef d;
	d.name = name, d.section = section, d.mod = mod, d.help = help;
	d.brush = brush, d.faces = faces;
	d.lo = lo, d.hi = hi, d.color = color;
	d.models = std::move(models);
	d.icon = icon;
	d.model_scale = scale;
	d.item = item;
	d.keys = std::move(keys);
	d.flags = std::move(flags);
	return d;
}

static std::vector<EntityDef> &table() {
	static std::vector<EntityDef> t = {
#include "ed_entdefs_table.inc"
	};
	return t;
}

static std::unordered_map<std::string, size_t> &index() {
	static std::unordered_map<std::string, size_t> at;
	if (at.size() != table().size()) {
		at.clear();
		for (size_t i = 0; i < table().size(); i++) {
			at[table()[i].name] = i;
		}
	}
	return at;
}

const std::vector<EntityDef> &entity_defs() {
	return table();
}

const EntityDef *entity_def(const std::string &classname) {
	auto it = index().find(classname);
	return it == index().end() ? nullptr : &table()[it->second];
}

const std::vector<KeyDef> &common_keys() {
	static const std::vector<KeyDef> keys = {
		{"targetname", KeyType::Name, "", "The name other entities' target uses for this one"},
		{"target", KeyType::Target, "", "The targetname of what this fires or aims at"},
		{"gametype", KeyType::Text, "", "Only in these game types: ffa tournament single team ctf oneflag obelisk harvester (CPMA: also its modes)"},
		{"not_gametype", KeyType::Text, "", "CPMA: left out of these game types"},
		{"notfree", KeyType::Bool, "", "Leave it out of free for all and tournament"},
		{"notteam", KeyType::Bool, "", "Leave it out of team games"},
		{"notsingle", KeyType::Bool, "", "Leave it out of single player"},
	};
	return keys;
}

const KeyDef *key_def(const std::string &classname, const std::string &key) {
	if (const EntityDef *d = entity_def(classname)) {
		for (const KeyDef &k : d->keys) {
			if (k.name == key) {
				return &k;
			}
		}
	}
	for (const KeyDef &k : common_keys()) {
		if (k.name == key) {
			return &k;
		}
	}
	return nullptr;
}

// ---------------------------------------------------------------- .def files

static std::string trim(std::string_view s) {
	size_t a = 0, b = s.size();
	while (a < b && std::isspace((unsigned char)s[a])) {
		a++;
	}
	while (b > a && std::isspace((unsigned char)s[b - 1])) {
		b--;
	}
	return std::string(s.substr(a, b - a));
}

// "PLAYERDIR_XY" -> "Playerdir xy"
static std::string pretty(const std::string &flag) {
	std::string s = flag;
	for (char &c : s) {
		c = c == '_' ? ' ' : (char)std::tolower((unsigned char)c);
	}
	if (!s.empty()) {
		s[0] = (char)std::toupper((unsigned char)s[0]);
	}
	return s;
}

int read_def_file(std::string_view text, const std::string &source) {
	int joined = 0;
	size_t at = 0;
	while ((at = text.find("/*QUAKED", at)) != std::string_view::npos) {
		const size_t end = std::min(text.find("*/", at), text.size());
		const size_t eol = std::min(text.find('\n', at), end);
		const std::string_view head = text.substr(at + 8, eol - at - 8), body = text.substr(eol, end - eol);
		at = end;
		// name (r g b) ? | (mins) (maxs), then the flags
		std::vector<std::string> tok;
		std::string cur;
		int paren = 0;
		for (char c : head) {
			if (c == '(') {
				paren++;
			} else if (c == ')') {
				paren--;
			}
			if (std::isspace((unsigned char)c) && paren == 0) {
				if (!cur.empty()) {
					tok.push_back(cur);
				}
				cur.clear();
			} else {
				cur += c;
			}
		}
		if (!cur.empty()) {
			tok.push_back(cur);
		}
		if (tok.size() < 3) {
			continue;
		}
		EntityDef d;
		d.name = tok[0];
		auto vec = [](const std::string &s) {
			Vec3 v;
			std::sscanf(s.c_str(), "(%lf %lf %lf)", &v.x, &v.y, &v.z);
			return v;
		};
		d.color = vec(tok[1]);
		size_t next = 2;
		if (tok[2] == "?") {
			d.brush = true;
			d.lo = Vec3(-8, -8, -8), d.hi = Vec3(8, 8, 8);
			next = 3;
		} else if (tok.size() >= 4) {
			d.lo = vec(tok[2]), d.hi = vec(tok[3]);
			next = 4;
		}
		for (size_t i = next; i < tok.size() && i - next < 24; i++) {
			if (tok[i] != "-" && tok[i] != "x" && tok[i] != "NULL") {
				d.flags.push_back({1 << int(i - next), pretty(tok[i])});
			}
		}
		// what it is: the text's first paragraph
		const std::string t = trim(body);
		const size_t para = t.find("\n\n");
		std::string help = trim(t.substr(0, para == std::string::npos ? std::string::npos : para));
		std::replace(help.begin(), help.end(), '\n', ' ');
		std::replace(help.begin(), help.end(), '\r', ' ');
		if (help.size() > 240) {
			help = help.substr(0, 237) + "...";
		}
		if (EntityDef *have = const_cast<EntityDef *>(entity_def(d.name))) {
			for (const FlagDef &f : d.flags) {
				if (std::none_of(have->flags.begin(), have->flags.end(), [&](const FlagDef &h) { return h.bit == f.bit; })) {
					have->flags.push_back({f.bit, f.label + " (" + source + ")"});
				}
			}
			continue;
		}
		d.section = source;
		d.mod = source;
		d.help = help.empty() ? "From " + source : help;
		d.faces = false;
		table().push_back(std::move(d));
		joined++;
	}
	return joined;
}

// ---------------------------------------------------------------- links and names

bool entity_center(const Doc &doc, Id entity, Vec3 &out) {
	const Object *o = doc.get(entity);
	if (!o || o->kind != Kind::Entity || entity == doc.worldspawn()) {
		return false;
	}
	const std::vector<Id> own = doc.owned(entity);
	if (!own.empty()) {
		const Bounds b = bounds_of(doc, own);
		if (!b.empty()) {
			out = b.center();
			return true;
		}
	}
	return parse_vec(o->entity.get("origin"), out);
}

std::string unique_targetname(const Doc &doc, const std::string &prefix, const std::set<std::string> &taken) {
	std::set<std::string> used = taken;
	for (Id id : doc.ids_of(Kind::Entity)) {
		if (const std::string *n = doc.get(id)->entity.find("targetname")) {
			used.insert(*n);
		}
	}
	for (int i = 1;; i++) {
		const std::string name = prefix + std::to_string(i);
		if (!used.count(name)) {
			return name;
		}
	}
}

// ---------------------------------------------------------------- id's numbers

double map_gravity(const Doc &doc) {
	const Object *w = doc.get(doc.worldspawn());
	const double g = w ? std::atof(w->entity.get("gravity", "800").c_str()) : 800;
	return g > 0 ? g : 800;
}

bool push_velocity(const Vec3 &from, const Vec3 &target, double gravity, Vec3 &v) {
	const double height = target.z - from.z;
	if (height <= 0 || gravity <= 0) {
		return false;
	}
	const double time = std::sqrt(height / (0.5 * gravity));
	Vec3 flat(target.x - from.x, target.y - from.y, 0);
	const double dist = flat.length();
	v = dist > 0 ? flat * (1 / dist) * (dist / time) : Vec3();
	v.z = time * gravity;
	return true;
}

Vec3 apex_for(const Vec3 &from, const Vec3 &landing, double gravity, double ceiling) {
	const double fx = landing.x - from.x, fy = landing.y - from.y;
	const double flat = std::sqrt(fx * fx + fy * fy);
	const double high = std::max(from.z, landing.z);
	double top = high + 64 + flat / 3;
	top = std::max(std::min(top, ceiling - 40), high + 16);
	const double up = std::sqrt(2 * (top - from.z) / gravity), down = std::sqrt(2 * (top - landing.z) / gravity);
	const double f = up / (up + down);
	return Vec3(from.x + fx * f, from.y + fy * f, top);
}

}  // namespace oz::ed
