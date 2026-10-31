#include "ed_shaders.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <vector>

extern "C" {
#include "../ui/oz_ui_engine.h"
}

namespace oz::ed {

enum { PARM_SKY = 1, PARM_NODRAW = 2, PARM_FOG = 4, PARM_TRIGGER = 8, PARM_HINT = 16, PARM_ORIGIN = 32, PARM_PORTAL = 64,
	PARM_NONSOLID = 128, PARM_PLAYERCLIP = 256 };

ShaderLib &shaders_lib() {
	static ShaderLib lib;
	return lib;
}

static std::string lower(std::string s) {
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	return s;
}

static std::string strip_ext(const std::string &s) {
	size_t dot = s.find_last_of('.');
	size_t slash = s.find_last_of('/');
	return dot != std::string::npos && (slash == std::string::npos || dot > slash) ? s.substr(0, dot) : s;
}

namespace {
// COM_Parse's tokens: // and /* */ comments skipped, quoted strings whole.
struct Lexer {
	const char *p, *end;
	std::string tok;
	bool newline = false;
	bool next() {
		newline = false;
		for (;;) {
			while (p < end && (unsigned char)*p <= ' ') {
				newline |= *p == '\n';
				p++;
			}
			if (p + 1 < end && p[0] == '/' && p[1] == '/') {
				while (p < end && *p != '\n') {
					p++;
				}
				continue;
			}
			if (p + 1 < end && p[0] == '/' && p[1] == '*') {
				p += 2;
				while (p + 1 < end && !(p[0] == '*' && p[1] == '/')) {
					newline |= *p == '\n';
					p++;
				}
				p = std::min(p + 2, end);
				continue;
			}
			break;
		}
		if (p >= end) {
			return false;
		}
		const char *s = p;
		if (*p == '"') {
			s = ++p;
			while (p < end && *p != '"') {
				p++;
			}
			tok.assign(s, p);
			p = std::min(p + 1, end);
			return true;
		}
		while (p < end && (unsigned char)*p > ' ') {
			p++;
		}
		tok.assign(s, p);
		return true;
	}
};
}  // namespace

void ShaderLib::load() {
	images_.clear();
	firsts_.clear();
	scripts_.clear();
	parms_.clear();
	sizes_.clear();
	materials_.clear();
	loaded_ = true;
	listed_ = false;
	int count = 0;
	char **files = OzE_ListFiles("scripts", ".shader", &count);
	for (int i = 0; i < count; i++) {
		void *buf = nullptr;
		int len = OzE_ReadFile((std::string("scripts/") + files[i]).c_str(), &buf);
		if (len <= 0) {
			continue;
		}
		Lexer lx{(const char *)buf, (const char *)buf + len};
		while (lx.next()) {
			std::string name = lower(lx.tok);
			if (!lx.next() || lx.tok != "{") {
				break;  // a broken script: what was read stays
			}
			std::string editor, first;
			int parms = 0;
			int depth = 1;
			while (depth > 0 && lx.next()) {
				const std::string t = lower(lx.tok);
				if (t == "{") {
					depth++;
				} else if (t == "}") {
					depth--;
				} else if (depth == 1 && t == "qer_editorimage" && lx.next()) {
					editor = lx.tok;
				} else if (depth == 1 && t == "skyparms") {
					parms |= PARM_SKY;
				} else if (depth == 1 && t == "fogparms") {
					parms |= PARM_FOG;
				} else if (depth == 1 && t == "surfaceparm" && lx.next()) {
					const std::string p = lower(lx.tok);
					parms |= p == "sky" ? PARM_SKY : p == "nodraw" ? PARM_NODRAW : p == "fog" ? PARM_FOG : p == "trigger" ? PARM_TRIGGER
						: p == "hint" || p == "skip" ? PARM_HINT : p == "origin" ? PARM_ORIGIN : p == "areaportal" || p == "clusterportal" ? PARM_PORTAL
						: p == "nonsolid" ? PARM_NONSOLID : p == "playerclip" || p == "monsterclip" ? PARM_PLAYERCLIP : 0;
				} else if (depth == 2 && first.empty() && (t == "map" || t == "clampmap") && lx.next()) {
					if (lx.tok[0] != '$') {
						first = lx.tok;
					}
				} else if (depth == 2 && first.empty() && t == "animmap" && lx.next() && lx.next()) {
					first = lx.tok;  // past the frequency
				}
			}
			// q3map2 keeps a shader's first definition
			if (!images_.count(name)) {
				images_[name] = !editor.empty() ? editor : first;
				firsts_[name] = first;
				scripts_[name] = files[i];
				parms_[name] = parms;
			}
		}
		OzE_FreeFile(buf);
	}
	OzE_FreeFileList(files);
}

// The size in an image file's header: TGA, JPEG or PNG.
static bool header_size(const unsigned char *b, int len, int &w, int &h) {
	if (len >= 24 && b[0] == 0x89 && b[1] == 'P' && b[2] == 'N' && b[3] == 'G') {
		w = (b[16] << 24) | (b[17] << 16) | (b[18] << 8) | b[19];
		h = (b[20] << 24) | (b[21] << 16) | (b[22] << 8) | b[23];
		return true;
	}
	if (len >= 4 && b[0] == 0xFF && b[1] == 0xD8) {
		int i = 2;
		while (i + 9 < len) {
			if (b[i] != 0xFF) {
				return false;
			}
			int marker = b[i + 1];
			int seg = (b[i + 2] << 8) | b[i + 3];
			if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC) {
				h = (b[i + 5] << 8) | b[i + 6];
				w = (b[i + 7] << 8) | b[i + 8];
				return true;
			}
			i += 2 + seg;
		}
		return false;
	}
	if (len >= 18) {  // TGA has no magic: its header's size fields
		w = b[12] | (b[13] << 8);
		h = b[14] | (b[15] << 8);
		return w > 0 && h > 0;
	}
	return false;
}

bool ShaderLib::image_size(const std::string &image, int &w, int &h) {
	auto it = sizes_.find(image);
	if (it != sizes_.end()) {
		w = it->second.first, h = it->second.second;
		return w > 0;
	}
	w = h = 0;
	const std::string base = strip_ext(image);
	for (const char *ext : {".tga", ".jpg", ".png"}) {
		void *buf = nullptr;
		int len = OzE_ReadFile((base + ext).c_str(), &buf);
		if (len > 0) {
			bool ok = header_size((const unsigned char *)buf, len, w, h);
			OzE_FreeFile(buf);
			if (ok) {
				break;
			}
			w = h = 0;
		}
	}
	sizes_[image] = {w, h};
	return w > 0;
}

Look ShaderLib::look(const std::string &shader) {
	if (!loaded_) {
		load();
	}
	std::string name = lower(shader);
	if (name.compare(0, 9, "textures/") != 0) {
		name = "textures/" + name;
	}
	// id's tool shaders are editor files (common.shader never shipped in the paks): by name first
	if (name.compare(0, 16, "textures/common/") == 0) {
		const std::string t = name.substr(16);
		if (t == "caulk" || t == "caulkshadow") return Look::Caulk;
		if (t.find("clip") != std::string::npos) return Look::Clip;
		if (t == "trigger") return Look::Trigger;
		if (t == "hint" || t == "skip" || t == "hintlocal" || t == "hintskip") return Look::Hint;
		if (t == "origin") return Look::Origin;
		if (t.find("portal") != std::string::npos || t == "donotenter") return Look::Portal;
		if (t.compare(0, 6, "nodraw") == 0 || t == "invisible" || t == "lightgrid" || t == "terrain" || t == "nodrop" ||
				t == "slick" || t == "ladder") return Look::Nodraw;
	}
	auto it = parms_.find(name);
	int p = it != parms_.end() ? it->second : 0;
	if (p & PARM_SKY) return Look::Sky;
	if (p & PARM_FOG) return Look::Fog;
	if (p & PARM_TRIGGER) return Look::Trigger;
	if (p & PARM_HINT) return Look::Hint;
	if (p & PARM_ORIGIN) return Look::Origin;
	if (p & PARM_PORTAL) return Look::Portal;
	if (p & PARM_PLAYERCLIP) return Look::Clip;
	if (p & PARM_NODRAW) return Look::Nodraw;
	return Look::Normal;
}

void ShaderLib::size(const std::string &shader, int &w, int &h) {
	if (!loaded_) {
		load();
	}
	std::string name = lower(shader);
	if (name.compare(0, 9, "textures/") != 0) {
		name = "textures/" + name;
	}
	auto it = images_.find(name);
	std::string image = it != images_.end() && !it->second.empty() ? lower(it->second) : name;
	if (!image_size(image, w, h)) {
		w = h = 64;
	}
}

std::string ShaderLib::image(const std::string &shader) {
	if (!loaded_) {
		load();
	}
	std::string name = lower(shader);
	if (name.compare(0, 9, "textures/") != 0) {
		name = "textures/" + name;
	}
	// q3map2's image, else the first stage's (some scripts name an editor image no pak has), else the name's
	auto it = images_.find(name);
	auto first = firsts_.find(name);
	int w, h;
	for (const std::string *c : {it != images_.end() ? &it->second : nullptr, first != firsts_.end() ? &first->second : nullptr, &name}) {
		if (c && !c->empty() && image_size(lower(*c), w, h)) {
			return lower(*c);
		}
	}
	return it != images_.end() && !it->second.empty() ? lower(it->second) : name;
}

bool ShaderLib::found(const std::string &shader) {
	int w, h;
	return image_size(image(shader), w, h);
}

// q3map2's tool shaders (its common.shader), which the browser offers though no pak has them
static const char *const TOOLS[] = {
	"caulk", "clip", "weapclip", "botclip", "fullclip", "nodraw", "nodrawnonsolid", "trigger", "hint", "skip",
	"origin", "areaportal", "clusterportal", "donotenter", "nodrop", "slick", "lightgrid",
};  // ed_compile.cpp hands q3map2 these

const std::vector<Material> &ShaderLib::materials() {
	if (!loaded_) {
		load();
	}
	if (listed_) {
		return materials_;
	}
	listed_ = true;
	std::unordered_map<std::string, size_t> at;
	auto add = [&](const std::string &full, const std::string &image, bool script) {
		// full: "textures/base_wall/concrete"
		if (full.compare(0, 9, "textures/") != 0 || full.size() <= 9 || at.count(full)) {
			return;
		}
		Material m;
		m.name = full.substr(9);
		const size_t slash = m.name.find('/');
		m.folder = slash == std::string::npos ? "" : m.name.substr(0, slash);
		m.image = image;
		m.script = script;
		at[full] = materials_.size();
		materials_.push_back(std::move(m));
	};
	for (const char *t : TOOLS) {
		add(std::string("textures/common/") + t, "", true);
		materials_.back().stock = true;
	}
	for (const auto &kv : images_) {
		add(kv.first, kv.second.empty() ? kv.first : lower(kv.second), true);
	}
	// the images under textures/ (one folder down) that no script names
	for (const char *ext : {".tga", ".jpg", ".png"}) {
		int count = 0;
		char **files = OzE_ListFiles("textures", ext, &count);
		for (int i = 0; i < count; i++) {
			const std::string file = lower(std::string("textures/") + files[i]);
			add(strip_ext(file), file, false);
		}
		OzE_FreeFileList(files);
	}
	// id's own: a shader whose script is in pak0 to pak8, an image there (a map pack may carry a copy)
	for (Material &m : materials_) {
		if (m.stock) {
			continue;
		}
		if (m.script) {
			auto s = scripts_.find("textures/" + m.name);
			m.stock = s != scripts_.end() && OzE_InIdPak(("scripts/" + s->second).c_str());
			continue;
		}
		const std::string base = strip_ext(m.image);
		for (const char *ext : {".tga", ".jpg", ".png"}) {
			if (OzE_InIdPak((base + ext).c_str())) {
				m.stock = true;
				break;
			}
		}
	}
	// id's first, then by folder; id's development leftovers in pak0 (most of them without a
	// picture) and the few outside any folder last
	auto later = [](const Material &m) {
		static const char *const leftovers[] = {"base", "bwhtest", "castle", "dont_use", "proto", "test", "tim"};
		if (m.folder.empty()) {
			return 2;
		}
		for (const char *l : leftovers) {
			if (m.stock && m.folder == l) {
				return 1;
			}
		}
		return 0;
	};
	std::sort(materials_.begin(), materials_.end(), [&](const Material &a, const Material &b) {
		if (a.stock != b.stock) {
			return a.stock;
		}
		if (later(a) != later(b)) {
			return later(a) < later(b);
		}
		return a.folder != b.folder ? a.folder < b.folder : a.name < b.name;
	});
	return materials_;
}

}  // namespace oz::ed
