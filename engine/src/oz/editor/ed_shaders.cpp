#include "ed_shaders.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

extern "C" {
#include "../ui/oz_ui_engine.h"
}
#include "ed_engine.h"


namespace oz::ed {

enum { PARM_SKY = 1, PARM_NODRAW = 2, PARM_FOG = 4, PARM_TRIGGER = 8, PARM_HINT = 16, PARM_ORIGIN = 32, PARM_PORTAL = 64,
	PARM_NONSOLID = 128, PARM_PLAYERCLIP = 256, PARM_LIQUID = 512, PARM_TRANS = 1024, PARM_GLOW = 2048, PARM_ANIM = 4096 };

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
	const char *start = nullptr;  // where the last token began (its quote, for a quoted one)
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
		start = p;
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
	stages_.clear();
	blocks_.clear();
	packs_.clear();
	oz_blocks_.clear();
	exists_.clear();
	scripts_.clear();
	parms_.clear();
	lights_.clear();
	sizes_.clear();
	materials_.clear();
	loaded_ = true;
	load_sizes();
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
		const bool id = OzE_InIdPak((std::string("scripts/") + files[i]).c_str()) != 0;
		while (lx.next()) {
			std::string name = lower(lx.tok);
			const char *block = lx.start;
			if (!lx.next() || lx.tok != "{") {
				break;  // a broken script: what was read stays
			}
			std::string editor, first;
			std::vector<std::string> stages;
			int parms = 0;
			double light = 0;
			int depth = 1;
			bool stage_add = false;      // the stage open: added over the rest
			bool stage_env = false;      // its picture a reflection (tcGen environment): a shine, not a glow
			std::string stage_map;
			while (depth > 0 && lx.next()) {
				const std::string t = lower(lx.tok);
				if (t == "{") {
					depth++;
					if (depth == 2) {
						stage_add = stage_env = false;
						stage_map.clear();
					}
				} else if (t == "}") {
					if (depth == 2 && stage_add && !stage_env && !stage_map.empty() && lower(stage_map) != "$lightmap") {
						parms |= PARM_GLOW;  // a lit part added over the surface (a light fixture's glow)
					}
					depth--;
				} else if (depth == 1 && t == "q3map_surfacelight" && lx.next()) {
					light = std::atof(lx.tok.c_str());
				} else if (depth == 1 && t == "deformvertexes") {
					parms |= PARM_ANIM;
				} else if (depth == 2 && t == "tcgen" && lx.next()) {
					stage_env = stage_env || lower(lx.tok) == "environment";
				} else if (depth == 2 && t == "blendfunc" && lx.next()) {
					const std::string b = lower(lx.tok);
					if (b == "add") {
						stage_add = true;
					} else if (b == "gl_one" && lx.next() && lower(lx.tok) == "gl_one") {
						stage_add = true;
					}
				} else if (depth == 2 && (t == "tcmod" || t == "animmap") && lx.tok.size()) {
					parms |= PARM_ANIM;
					if (t == "animmap" && lx.next() && lx.next()) {
						if (first.empty()) {
							first = lx.tok;  // past the frequency
						}
						stages.push_back(lx.tok);
						stage_map = lx.tok;
					}
				} else if (depth == 1 && t == "qer_editorimage" && lx.next()) {
					editor = lx.tok;
				} else if (depth == 1 && t == "skyparms") {
					parms |= PARM_SKY;
					if (lx.next() && lx.tok != "-") {
						for (const char *side : {"_ft", "_rt", "_up"}) {
							stages.push_back(lx.tok + side);  // the far box: a side shows the sky
						}
					}
				} else if (depth == 1 && t == "fogparms") {
					parms |= PARM_FOG;
				} else if (depth == 1 && t == "surfaceparm" && lx.next()) {
					const std::string p = lower(lx.tok);
					parms |= p == "sky" ? PARM_SKY : p == "nodraw" ? PARM_NODRAW : p == "fog" ? PARM_FOG : p == "trigger" ? PARM_TRIGGER
						: p == "hint" || p == "skip" ? PARM_HINT : p == "origin" ? PARM_ORIGIN : p == "areaportal" || p == "clusterportal" ? PARM_PORTAL
						: p == "nonsolid" ? PARM_NONSOLID : p == "playerclip" || p == "monsterclip" ? PARM_PLAYERCLIP
						: p == "water" || p == "lava" || p == "slime" ? PARM_LIQUID : p == "trans" ? PARM_TRANS : 0;
				} else if (depth == 2 && (t == "map" || t == "clampmap") && lx.next()) {
					stage_map = lx.tok;
					if (lx.tok[0] == '$' && lower(lx.tok) != "$lightmap") {
						stages.push_back(lx.tok);  // $whiteimage: drawn without a file (drawable() takes it)
					} else if (lx.tok[0] != '$') {
						if (first.empty()) {
							first = lx.tok;
						}
						stages.push_back(lx.tok);
					}
				}
			}
			// q3map2 keeps a shader's first definition
			if (!images_.count(name)) {
				images_[name] = !editor.empty() ? editor : first;
				firsts_[name] = first;
				stages_[name] = std::move(stages);
				scripts_[name] = files[i];
				parms_[name] = parms;
				if (light > 0) {
					lights_[name] = light;
				}
				if (depth == 0) {
					blocks_[name] = std::string(block, lx.p);
				}
				if (!id) {
					packs_[name] = true;
				}
			}
		}
		OzE_FreeFile(buf);
	}
	OzE_FreeFileList(files);
	// the oz renderer's own versions (the remastered images): only their text, for copies made of them
	files = OzE_ListFiles("scripts", ".ozmtr", &count);
	for (int i = 0; i < count; i++) {
		void *buf = nullptr;
		int len = OzE_ReadFile((std::string("scripts/") + files[i]).c_str(), &buf);
		if (len <= 0) {
			continue;
		}
		Lexer lx{(const char *)buf, (const char *)buf + len};
		while (lx.next()) {
			const std::string name = lower(lx.tok);
			const char *block = lx.start;
			if (!lx.next() || lx.tok != "{") {
				break;
			}
			int depth = 1;
			while (depth > 0 && lx.next()) {
				depth += lx.tok == "{" ? 1 : lx.tok == "}" ? -1 : 0;
			}
			if (depth == 0 && !oz_blocks_.count(name)) {
				oz_blocks_[name] = std::string(block, lx.p);
			}
		}
		OzE_FreeFile(buf);
	}
	OzE_FreeFileList(files);
}

// The size in an image file's header: TGA, JPEG, PNG or DDS.
static bool header_size(const unsigned char *b, int len, int &w, int &h) {
	if (len >= 20 && b[0] == 'D' && b[1] == 'D' && b[2] == 'S' && b[3] == ' ') {
		h = b[12] | (b[13] << 8) | (b[14] << 16) | (b[15] << 24);
		w = b[16] | (b[17] << 8) | (b[18] << 16) | (b[19] << 24);
		return w > 0 && h > 0;
	}
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
	for (const char *ext : {".tga", ".jpg", ".png", ".dds"}) {
		// the header alone (every material is checked once: whole files took 4.8 s for 9,500 of them);
		// a JPEG's size can sit past a big EXIF block, so one that ran out reads the whole file
		static unsigned char head[16384];
		const int got = OzE_ReadFileHead((base + ext).c_str(), head, int(sizeof head));
		if (got > 0 && header_size(head, got, w, h)) {
			break;
		}
		w = h = 0;
		void *buf = nullptr;
		int len = got == int(sizeof head) && ext[1] == 'j' ? OzE_ReadFile((base + ext).c_str(), &buf) : -1;
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
	sizes_dirty_ = true;
	return w > 0;
}

static std::string sizes_file() {
	return std::string(OzEd_HomePath()) + "/oz/editor/sizes.cache";
}

void ShaderLib::load_sizes() {
	sizes_dirty_ = false;
	char *text = nullptr;
	const int len = OzEd_ReadOSFile(sizes_file().c_str(), &text);
	if (len <= 0) {
		OzEd_Free(text);
		return;
	}
	// "OZSZ 1 <stamp>", then "<w> <h> <image>" a line
	const char *p = text, *end = text + len;
	unsigned stamp = 0;
	int version = 0;
	if (std::sscanf(p, "OZSZ %d %u", &version, &stamp) == 2 && version == 1 && stamp == OzE_SearchStamp()) {
		while (p < end && *p != '\n') {
			p++;
		}
		while (p < end) {
			p++;
			const char *eol = p;
			while (eol < end && *eol != '\n') {
				eol++;
			}
			if (eol - p > 4 && p[0] == 'E' && p[1] == ' ') {
				// "E <0|1> <image without its extension>": whether a file has it (drawable's)
				std::string key(p + 4, eol);
				if (!key.empty() && key.back() == '\r') {
					key.pop_back();
				}
				exists_[key] = p[2] == '1';
				p = eol;
				continue;
			}
			char *q = nullptr;
			const long w = std::strtol(p, &q, 10);
			const long h = q && q < eol ? std::strtol(q, &q, 10) : -1;
			if (q && q < eol && *q == ' ' && h >= 0) {
				std::string key(static_cast<const char *>(q) + 1, eol);
				if (!key.empty() && key.back() == '\r') {
					key.pop_back();
				}
				sizes_[key] = {int(w), int(h)};
			}
			p = eol;
		}
	}
	OzEd_Free(text);
}

void ShaderLib::save_sizes() {
	if (!sizes_dirty_) {
		return;
	}
	std::string out = "OZSZ 1 " + std::to_string(OzE_SearchStamp()) + "\n";
	for (const auto &kv : sizes_) {
		out += std::to_string(kv.second.first) + " " + std::to_string(kv.second.second) + " " + kv.first + "\n";
	}
	for (const auto &kv : exists_) {
		out += std::string("E ") + (kv.second ? "1 " : "0 ") + kv.first + "\n";
	}
	const std::string file = sizes_file();
	OzEd_MakeDirs(file.c_str());
	if (OzEd_WriteOSFile(file.c_str(), out.data(), int(out.size()))) {
		sizes_dirty_ = false;
	}
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

bool ShaderLib::nonsolid(const std::string &shader) {
	if (look(shader) != Look::Normal) {
		return false;
	}
	std::string name = lower(shader);
	if (name.compare(0, 9, "textures/") != 0) {
		name = "textures/" + name;
	}
	auto it = parms_.find(name);
	return it != parms_.end() && (it->second & PARM_NONSOLID) && !(it->second & PARM_LIQUID);
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
	// a first stage id never shipped (a $lightmap-first glow, a dev leftover): any stage that's there
	auto st = stages_.find(name);
	if (st != stages_.end()) {
		for (const std::string &c : st->second) {
			if (image_size(lower(c), w, h)) {
				return lower(c);
			}
		}
	}
	return it != images_.end() && !it->second.empty() ? lower(it->second) : name;
}

bool ShaderLib::drawable(const std::string &shader) {
	for (const std::string &img : stage_images(shader)) {
		if (!img.empty() && img[0] == '$') {
			return true;
		}
		const std::string base = strip_ext(img);
		auto it = exists_.find(base);
		if (it == exists_.end()) {
			bool there = false;
			for (const char *ext : {".tga", ".jpg", ".png", ".dds"}) {
				there = there || OzE_FileExists((base + ext).c_str());
			}
			it = exists_.emplace(base, there).first;
			sizes_dirty_ = true;  // kept with the sizes
		}
		if (it->second) {
			return true;
		}
	}
	return false;
}

std::string ShaderLib::pack_script(const std::string &shader) {
	if (!loaded_) {
		load();
	}
	std::string name = lower(shader);
	if (name.compare(0, 9, "textures/") != 0) {
		name = "textures/" + name;
	}
	auto it = blocks_.find(name);
	return it != blocks_.end() && packs_.count(name) ? it->second : "";
}

std::string ShaderLib::oz_script(const std::string &shader) {
	if (!loaded_) {
		load();
	}
	std::string name = lower(shader);
	if (name.compare(0, 9, "textures/") != 0) {
		name = "textures/" + name;
	}
	auto it = oz_blocks_.find(name);
	return it != oz_blocks_.end() ? it->second : "";
}

std::string ShaderLib::script(const std::string &shader) {
	if (!loaded_) {
		load();
	}
	std::string name = lower(shader);
	if (name.compare(0, 9, "textures/") != 0) {
		name = "textures/" + name;
	}
	auto it = blocks_.find(name);
	return it != blocks_.end() ? it->second : "";
}

std::vector<std::string> ShaderLib::stage_images(const std::string &shader) {
	if (!loaded_) {
		load();
	}
	std::string name = lower(shader);
	if (name.compare(0, 9, "textures/") != 0) {
		name = "textures/" + name;
	}
	std::vector<std::string> out;
	auto it = images_.find(name);
	if (it != images_.end() && !it->second.empty()) {
		out.push_back(lower(it->second));
	}
	auto st = stages_.find(name);
	if (st != stages_.end()) {
		for (const std::string &s : st->second) {
			out.push_back(lower(s));
		}
	}
	if (out.empty()) {
		out.push_back(name);  // a bare image
	}
	return out;
}

bool ShaderLib::found(const std::string &shader) {
	int w, h;
	return image_size(image(shader), w, h);
}

bool ShaderLib::glows(const std::string &shader) {
	if (!loaded_) {
		load();
	}
	std::string name = lower(shader);
	if (name.compare(0, 9, "textures/") != 0) {
		name = "textures/" + name;
	}
	auto it = parms_.find(name);
	return it != parms_.end() && (it->second & PARM_GLOW);
}

double ShaderLib::surface_light(const std::string &shader) {
	if (!loaded_) {
		load();
	}
	std::string name = lower(shader);
	if (name.compare(0, 9, "textures/") != 0) {
		name = "textures/" + name;
	}
	auto it = lights_.find(name);
	return it != lights_.end() ? it->second : 0;
}

// ---- glow copies

static std::string number(double v) {
	char b[32];
	std::snprintf(b, sizeof b, "%g", v);
	return b;
}

// A material's script block with another name and its glow at k of its own (a stage added over the
// rest that isn't the lightmap: dropped at 0; id's renderers: its rgbGen scaled, white at most; the oz
// renderer's: ozGlow k, past white for its bloom), its q3map_surfacelight times kl, and an editor image
// for q3map2 (which sizes a material's texture by it) when it has none.
static std::string rewrite_glow(const std::string &text, const std::string &name, double k, double kl, bool oz, const std::string &editor_image) {
	struct Edit {
		size_t a, b;
		std::string with;
	};
	std::vector<Edit> edits;
	Lexer lx{text.data(), text.data() + text.size()};
	auto at = [&](const char *ptr) { return size_t(ptr - text.data()); };
	if (!lx.next()) {
		return "";
	}
	edits.push_back({at(lx.start), at(lx.p), name});
	if (!lx.next() || lx.tok != "{") {
		return "";
	}
	const size_t open = at(lx.p);
	bool has_editor = false;
	int depth = 1;
	struct Stage {
		size_t open = 0;
		bool add = false, lightmap = false, rgb = false, glow = false;
		std::vector<Edit> edits;
	} st;
	while (depth > 0 && lx.next()) {
		const std::string t = lower(lx.tok);
		if (t == "{") {
			if (++depth == 2) {
				st = Stage();
				st.open = at(lx.start);
			}
		} else if (t == "}") {
			if (depth == 2 && st.add && !st.lightmap) {
				const size_t close = at(lx.start);
				if (k <= 0) {
					edits.push_back({st.open, at(lx.p), ""});  // no glow
				} else {
					edits.insert(edits.end(), st.edits.begin(), st.edits.end());
					if (oz && !st.glow) {
						edits.push_back({close, close, "\tozGlow " + number(k) + "\n\t"});
					} else if (!oz && !st.rgb && k < 1) {
						edits.push_back({close, close, "\trgbGen const ( " + number(k) + " " + number(k) + " " + number(k) + " )\n\t"});
					}
				}
			}
			depth--;
		} else if (depth == 1 && t == "qer_editorimage") {
			has_editor = true;
		} else if (depth == 1 && t == "q3map_surfacelight") {
			const size_t a = at(lx.start);
			if (lx.next()) {
				edits.push_back({a, at(lx.p), "q3map_surfacelight " + number(std::atof(lx.tok.c_str()) * kl)});
			}
		} else if (depth == 2 && t == "blendfunc" && lx.next()) {
			const std::string b = lower(lx.tok);
			if (b == "add") {
				st.add = true;
			} else if (b == "gl_one") {
				const char *keep = lx.p;
				if (lx.next() && lower(lx.tok) == "gl_one") {
					st.add = true;
				} else {
					lx.p = keep;
				}
			}
		} else if (depth == 2 && (t == "map" || t == "clampmap") && lx.next()) {
			st.lightmap = st.lightmap || lower(lx.tok) == "$lightmap";
		} else if (depth == 2 && t == "ozglow" && oz) {
			const size_t a = at(lx.start);
			if (lx.next()) {
				st.glow = true;
				st.edits.push_back({a, at(lx.p), "ozGlow " + number(std::atof(lx.tok.c_str()) * k)});
			}
		} else if (depth == 2 && t == "rgbgen" && !oz) {
			// its words, to the end of the line (or a brace)
			const size_t a = at(lx.start);
			const char *e = lx.p;
			while (e < lx.end && *e != '\n' && *e != '\r' && *e != '}' && *e != '{') {
				e++;
			}
			std::vector<std::string> w;
			std::string cur;
			for (const char *c = lx.p; c < e; c++) {
				if ((unsigned char)*c <= ' ' || *c == '(' || *c == ')') {
					if (!cur.empty()) {
						w.push_back(cur);
					}
					cur.clear();
				} else {
					cur += *c;
				}
			}
			if (!cur.empty()) {
				w.push_back(cur);
			}
			lx.p = e;
			st.rgb = true;
			const std::string kind = w.empty() ? "" : lower(w[0]);
			std::string with;
			if ((kind == "identity" || kind == "identitylighting") && k < 1) {
				with = "rgbGen const ( " + number(k) + " " + number(k) + " " + number(k) + " )";
			} else if (kind == "wave" && w.size() >= 6) {
				with = "rgbGen wave " + w[1] + " " + number(std::atof(w[2].c_str()) * k) + " " + number(std::atof(w[3].c_str()) * k) + " " + w[4] + " " + w[5];
			} else if (kind == "const" && w.size() >= 4) {
				with = "rgbGen const ( ";
				for (int i = 1; i <= 3; i++) {
					with += number(std::min(1.0, std::atof(w[size_t(i)].c_str()) * k)) + " ";
				}
				with += ")";
			}
			if (!with.empty()) {
				st.edits.push_back({a, at(e), with});
			}
		}
	}
	if (depth != 0) {
		return "";  // a broken block
	}
	if (!has_editor && !editor_image.empty()) {
		edits.push_back({open, open, "\n\tqer_editorimage " + editor_image});
	}
	std::sort(edits.begin(), edits.end(), [](const Edit &x, const Edit &y) { return x.a > y.a || (x.a == y.a && x.b > y.b); });
	std::string out = text.substr(0, at(lx.p));
	size_t last = std::string::npos;
	for (const Edit &e : edits) {
		if (e.b > last) {
			continue;  // inside a dropped stage
		}
		out.replace(e.a, e.b - e.a, e.with);
		last = e.a;
	}
	return out;
}

bool ShaderLib::glow_adjustable(const std::string &material) {
	return look(material) == Look::Normal && (glows(material) || surface_light(material) > 0);
}

std::string ShaderLib::glow_variant(const std::string &material, int brightness, int light) {
	if (!loaded_) {
		load();
	}
	std::string base = lower(material);
	if (base.compare(0, 9, "textures/") == 0) {
		base = base.substr(9);
	}
	if ((brightness == 100 && light == 100) || !glow_adjustable(base)) {
		return material;
	}
	brightness = std::max(0, std::min(brightness, 4095));
	light = std::max(0, std::min(light, 4095));
	std::string name = "oz_glow/" + std::to_string(brightness) + "_" + std::to_string(light) + "/" + base;
	if (name.size() + 9 > 63) {
		uint32_t h = 2166136261u;
		for (char c : base) {
			h = (h ^ (unsigned char)c) * 16777619u;
		}
		char tail[12];
		std::snprintf(tail, sizeof tail, "%08x", h);
		name = "oz_glow/" + std::to_string(brightness) + "_" + std::to_string(light) + "/" + tail;
	}
	const std::string vk = "textures/" + name, bk = "textures/" + base;
	if (blocks_.count(vk)) {
		return name;
	}
	const std::string own = script(base);
	std::string oz = oz_script(base);
	if (own.empty()) {
		return material;
	}
	const std::string img = image(base);
	const std::string shader_text = rewrite_glow(own, vk, brightness / 100.0, light / 100.0, false, img);
	const std::string oz_text = rewrite_glow(oz.empty() ? own : oz, vk, brightness / 100.0, light / 100.0, true, img);
	if (shader_text.empty() || oz_text.empty()) {
		return material;
	}
	// the copy as any material: the material's images, flags and light (scaled), these scripts
	images_[vk] = images_[bk];
	firsts_[vk] = firsts_[bk];
	stages_[vk] = stages_[bk];
	parms_[vk] = parms_[bk];
	if (lights_.count(bk)) {
		lights_[vk] = lights_[bk] * light / 100.0;
	}
	blocks_[vk] = shader_text;
	oz_blocks_[vk] = oz_text;
	packs_[vk] = true;  // the compile hands q3map2 its script, as a map pack's
	scripts_[vk] = "oz_glow.shader";
	if (!glow_read_) {
		// the copies made before, kept in the game's file: written again with this one
		glow_read_ = true;
		for (int which = 0; which < 2; which++) {
			char *text = nullptr;
			const std::string file = std::string(OzEd_HomePath()) + "/oz/scripts/oz_glow" + (which ? ".ozmtr" : ".shader");
			const int len = OzEd_ReadOSFile(file.c_str(), &text);
			if (len > 0) {
				Lexer lx{text, text + len};
				while (lx.next()) {
					const std::string n = lower(lx.tok);
					const char *block = lx.start;
					if (!lx.next() || lx.tok != "{") {
						break;
					}
					int depth = 1;
					while (depth > 0 && lx.next()) {
						depth += lx.tok == "{" ? 1 : lx.tok == "}" ? -1 : 0;
					}
					if (depth == 0 && n.compare(0, 17, "textures/oz_glow/") == 0) {
						(which ? glow_scripts_[n].second : glow_scripts_[n].first) = std::string(block, lx.p);
					}
				}
			}
			OzEd_Free(text);
		}
	}
	glow_scripts_[vk] = {shader_text, oz_text};
	write_glow_scripts();
	return name;
}

void ShaderLib::write_glow_scripts() {
	std::string a = "// the map editor's glow copies (a face's GLOW in Inspect): a material with its glow and its light changed\n", b = a;
	for (const auto &kv : glow_scripts_) {
		if (!kv.second.first.empty() && !kv.second.second.empty()) {
			a += kv.second.first + "\n\n";
			b += kv.second.second + "\n\n";
		}
	}
	const std::string dir = std::string(OzEd_HomePath()) + "/oz/scripts/";
	OzEd_MakeDirs((dir + "x").c_str());
	OzEd_WriteOSFile((dir + "oz_glow.shader").c_str(), a.data(), int(a.size()));
	OzEd_WriteOSFile((dir + "oz_glow.ozmtr").c_str(), b.data(), int(b.size()));
}

void ShaderLib::copy_glow_scripts(const std::string &to) {
	const std::string from = std::string(OzEd_HomePath()) + "/oz/scripts/";
	for (const char *f : {"oz_glow.shader", "oz_glow.ozmtr"}) {
		char *text = nullptr;
		const int len = OzEd_ReadOSFile((from + f).c_str(), &text);
		if (len > 0) {
			OzEd_MakeDirs((to + "/" + f).c_str());
			OzEd_WriteOSFile((to + "/" + f).c_str(), text, len);
		}
		OzEd_Free(text);
	}
}

static const char *const KIND_NAMES[MK_COUNT] = {"walls", "floors", "ceilings", "trims", "lights", "doors", "supports", "nature",
	"liquids", "glass", "skies", "effects", "team", "tools", "other"};
static const char *const KIND_LABELS[MK_COUNT] = {"Walls", "Floors", "Ceilings", "Trims", "Lights", "Doors", "Pipes", "Rock",
	"Liquids", "Glass", "Skies", "Effects", "Team", "Tools", "Other"};

const char *mat_kind_name(int bit) {
	return bit >= 0 && bit < MK_COUNT ? KIND_NAMES[bit] : "";
}

const char *mat_kind_label(int bit) {
	return bit >= 0 && bit < MK_COUNT ? KIND_LABELS[bit] : "";
}

// What a material is: id's folders by name; anything else by the words in its name (a map pack's are
// joined words: "clangfloor2", "rustpipe_lrg"), the first kind that fits; and a Light too when it glows
// or lights the map.
static uint32_t kinds_of(const Material &m, int parms, double light, Look look) {
	const std::string leaf = m.name.substr(m.name.find('/') == std::string::npos ? 0 : m.name.find('/') + 1);
	uint32_t k = 0;
	if (m.folder == "oz_dev") {
		return 0;  // Dev textures, their own view
	}
	if (m.folder == "common") {
		k = MK_TOOL;
	} else if (look == Look::Sky) {
		return MK_SKY;
	} else if (look == Look::Fog) {
		k = MK_EFFECT;
	} else if (parms & PARM_LIQUID) {
		k = MK_LIQUID;
	} else {
		static const struct {
			const char *folder;
			uint32_t kind;
		} ids[] = {
			{"base_button", MK_DOOR}, {"base_ceiling", MK_CEILING}, {"base_door", MK_DOOR}, {"base_floor", MK_FLOOR},
			{"base_light", MK_LIGHT}, {"base_object", MK_OTHER}, {"base_support", MK_SUPPORT}, {"base_trim", MK_TRIM},
			{"base_wall", MK_WALL}, {"ctf", MK_TEAM}, {"effects", MK_EFFECT}, {"gothic_block", MK_WALL}, {"gothic_button", MK_DOOR},
			{"gothic_cath", MK_WALL}, {"gothic_ceiling", MK_CEILING}, {"gothic_door", MK_DOOR}, {"gothic_floor", MK_FLOOR},
			{"gothic_light", MK_LIGHT}, {"gothic_trim", MK_TRIM}, {"gothic_wall", MK_WALL}, {"liquids", MK_LIQUID},
			{"organics", MK_NATURE}, {"stone", MK_NATURE}, {"skin", MK_NATURE}, {"sfx", MK_EFFECT}, {"skies", MK_SKY},
		};
		if (m.stock) {
			for (const auto &e : ids) {
				if (m.folder == e.folder) {
					k = e.kind;
					break;
				}
			}
		}
		if (!k) {
			// the words, in this order: the first that's in the name
			static const struct {
				uint32_t kind;
				const char *words[22];
			} words[] = {
				{MK_LIGHT, {"light", "lamp", "lite", "glow", "neon", "bulb"}},
				{MK_SKY, {"sky", "cloud"}},
				{MK_LIQUID, {"water", "lava", "slime", "acid", "liquid"}},
				{MK_EFFECT, {"jumppad", "jumpad", "jump", "bounce", "telep", "portal", "flame", "fire", "beam", "spark", "laser",
					"energy", "forcefield", "shield", "fx"}},
				{MK_TEAM, {"ctf", "redteam", "blueteam", "teamred", "teamblue", "flag"}},
				{MK_GLASS, {"glass", "window", "grate", "grill", "fence", "mesh", "chain"}},
				{MK_DOOR, {"door", "button", "switch", "gate", "hatch"}},
				{MK_SUPPORT, {"pipe", "pillar", "column", "support", "girder", "strut", "pole", "post", "cable", "rail", "truss"}},
				{MK_TRIM, {"trim", "border", "edge", "baseboard", "band", "rim", "molding", "moulding", "stripe", "frame", "lip"}},
				{MK_CEILING, {"ceil", "roof"}},
				{MK_FLOOR, {"floor", "ground", "tile", "stair", "step", "path", "road", "grid"}},
				{MK_NATURE, {"rock", "stone", "dirt", "grass", "sand", "mud", "cliff", "moss", "wood", "tree", "bark", "leaf", "leaves",
					"ivy", "vine", "plant", "flesh", "skin", "terrain", "earth", "snow", "gravel"}},
				{MK_WALL, {"wall", "brick", "block", "panel", "concrete", "plaster", "stucco", "marble", "tech", "metal", "cement",
					"slab", "plate"}},
			};
			for (const auto &w : words) {
				for (const char *word : w.words) {
					if (word && (leaf.find(word) != std::string::npos || (!m.stock && m.folder.find(word) != std::string::npos))) {
						k = w.kind;
						break;
					}
				}
				if (k) {
					break;
				}
			}
		}
		if (!k && (parms & PARM_TRANS)) {
			k = MK_GLASS;
		}
		if (!k && (parms & PARM_ANIM)) {
			k = MK_EFFECT;
		}
	}
	if ((light > 0 || (parms & PARM_GLOW)) && !(k & (MK_SKY | MK_TOOL))) {
		k |= MK_LIGHT;  // it lights the map, or glows
	}
	return k ? k : MK_OTHER;
}

static MatStyle style_of(const Material &m) {
	if (m.folder.compare(0, 5, "base_") == 0 || m.folder == "ctf") {
		return MatStyle::Tech;
	}
	if (m.folder.compare(0, 7, "gothic_") == 0) {
		return MatStyle::Gothic;
	}
	return MatStyle::Other;
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
		if (kv.first.compare(0, 17, "textures/oz_glow/") != 0) {  // the glow copies of a face's GLOW
			add(kv.first, kv.second.empty() ? kv.first : lower(kv.second), true);
		}
	}
	// the images under textures/ that no script names (loose files on disk too, and .dds)
	// (OzE_ListTree takes the extension without its dot and names files from the game's root: with
	// ".png" the filter was "textures/*..png", so none of these were ever listed, oz_dev's included)
	auto layer = [](const std::string &stem) {  // Oz's remastered layers beside id's images, not materials
		for (const char *s : {"_n", "_s", "__glow", "__add", "__sky"}) {
			const size_t n = strlen(s);
			if (stem.size() > n && stem.compare(stem.size() - n, n, s) == 0) {
				return true;
			}
		}
		return false;
	};
	for (const char *ext : {"tga", "jpg", "png", "dds"}) {
		int count = 0;
		char **files = OzE_ListTree("textures", ext, &count);
		for (int i = 0; i < count; i++) {
			std::string file = lower(files[i]);
			if (file.compare(0, 9, "textures/") != 0) {
				file = "textures/" + file;
			}
			if (ext[0] == 'd' && layer(strip_ext(file))) {
				continue;
			}
			add(strip_ext(file), file, false);
		}
		OzE_FreeFileList(files);
	}
	// id's own: a shader whose script is in pak0 to pak8, an image there (a map pack may carry a copy)
	for (Material &m : materials_) {
		if (m.folder == "oz_dev") {
			m.stock = true;  // Oz's prototyping textures (game/assets): with id's, first
		}
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
	// shaders that draw nothing because none of their images exists (about 350 of id's development
	// leftovers in pak0, never shipped with their pictures): they'd be a blank tile and a missing texture
	// and any (a sky too) whose picture can't be read: a "NO IMAGE" tile that paints nothing you can see.
	// The tools and fog volumes stay: they draw in their color, as they're meant to.
	materials_.erase(std::remove_if(materials_.begin(), materials_.end(), [&](const Material &m) {
		if (m.name.compare(0, 7, "common/") == 0) {
			return false;
		}
		const Look l = look(m.name);
		if (l == Look::Normal && !drawable(m.name)) {
			return true;
		}
		return l != Look::Fog && !found(m.name);
	}), materials_.end());
	// id's first, then by folder; id's development leftovers in pak0 (most of them without a
	// picture) and the few outside any folder last
	auto later = [](const Material &m) {
		static const char *const leftovers[] = {"base", "bwhtest", "castle", "dont_use", "proto", "test", "tim"};
		if (m.folder == "oz_dev") {
			return -1;
		}
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
	// what each one is, for the browser's kinds and styles
	for (Material &m : materials_) {
		const std::string full = "textures/" + lower(m.name);
		auto pa = parms_.find(full);
		auto li = lights_.find(full);
		m.kinds = kinds_of(m, pa != parms_.end() ? pa->second : 0, li != lights_.end() ? li->second : 0, look(m.name));
		m.style = style_of(m);
	}
	save_sizes();  // the headers read for the list, for the next time the game starts
	return materials_;
}

}  // namespace oz::ed
