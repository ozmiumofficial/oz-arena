// The map editor's compile and play-test (ENGINE_PLAN.md P13, engine/EDITOR.md), after the Godot
// build's q3_map_compiler.gd: q3map2 (BSP, vis, light) and mbspc (the bots' area file) run as programs
// of their own in the background, a stage at a time, while the mapper goes on editing. The map is
// compiled as it is in the editor (nothing is saved) under the name ed_<name>, into maps/ in the
// player's own files, so a play-test never shadows a stock map. A leak stops the compile and the line
// q3map2 traced out to the void is drawn in the view. F5 plays the map once it's built.

#include "ed_view.h"
#include "ed_engine.h"
#include "ed_mapfile.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

extern "C" {
#include "../ui/oz_ui_engine.h"
}

namespace oz::ed {

// q3map2's tool shaders: the surfaceparms it acts on, under the names Radiant and TrenchBroom use.
// id's common.shader was an editor file and isn't in the paks, so the editor hands q3map2 its own.
static const char *const TOOL_SHADERS =
	"// The map editor's tool shaders for q3map2: what each tool brush is to the compiler.\n"
	"textures/common/caulk\n{\n\tsurfaceparm nodraw\n\tsurfaceparm nolightmap\n\tsurfaceparm nomarks\n}\n"
	"textures/common/nodraw\n{\n\tsurfaceparm nodraw\n\tsurfaceparm nolightmap\n\tsurfaceparm nomarks\n}\n"
	"textures/common/nodrawnonsolid\n{\n\tsurfaceparm nodraw\n\tsurfaceparm nolightmap\n\tsurfaceparm nonsolid\n\tsurfaceparm trans\n\tsurfaceparm nomarks\n}\n"
	"textures/common/clip\n{\n\tsurfaceparm nodraw\n\tsurfaceparm nolightmap\n\tsurfaceparm nonsolid\n\tsurfaceparm trans\n\tsurfaceparm nomarks\n\tsurfaceparm noimpact\n\tsurfaceparm playerclip\n}\n"
	"textures/common/weapclip\n{\n\tsurfaceparm nodraw\n\tsurfaceparm nolightmap\n\tsurfaceparm trans\n\tsurfaceparm nomarks\n}\n"
	"textures/common/fullclip\n{\n\tsurfaceparm nodraw\n\tsurfaceparm nolightmap\n\tsurfaceparm trans\n\tsurfaceparm nomarks\n\tsurfaceparm playerclip\n}\n"
	"textures/common/botclip\n{\n\tsurfaceparm nodraw\n\tsurfaceparm nolightmap\n\tsurfaceparm nonsolid\n\tsurfaceparm trans\n\tsurfaceparm nomarks\n\tsurfaceparm noimpact\n\tsurfaceparm botclip\n}\n"
	"textures/common/trigger\n{\n\tsurfaceparm nodraw\n\tsurfaceparm nolightmap\n\tsurfaceparm nonsolid\n\tsurfaceparm trans\n\tsurfaceparm nomarks\n\tsurfaceparm trigger\n}\n"
	"textures/common/hint\n{\n\tsurfaceparm nodraw\n\tsurfaceparm nolightmap\n\tsurfaceparm nonsolid\n\tsurfaceparm structural\n\tsurfaceparm trans\n\tsurfaceparm noimpact\n\tsurfaceparm hint\n}\n"
	"textures/common/skip\n{\n\tsurfaceparm nodraw\n\tsurfaceparm nolightmap\n\tsurfaceparm nonsolid\n\tsurfaceparm structural\n\tsurfaceparm trans\n\tsurfaceparm noimpact\n\tsurfaceparm skip\n}\n"
	"textures/common/origin\n{\n\tsurfaceparm nodraw\n\tsurfaceparm nolightmap\n\tsurfaceparm nonsolid\n\tsurfaceparm trans\n\tsurfaceparm origin\n}\n"
	"textures/common/areaportal\n{\n\tsurfaceparm nodraw\n\tsurfaceparm nolightmap\n\tsurfaceparm nonsolid\n\tsurfaceparm structural\n\tsurfaceparm trans\n\tsurfaceparm nomarks\n\tsurfaceparm areaportal\n}\n"
	"textures/common/clusterportal\n{\n\tsurfaceparm nodraw\n\tsurfaceparm nolightmap\n\tsurfaceparm nonsolid\n\tsurfaceparm detail\n\tsurfaceparm trans\n\tsurfaceparm nomarks\n\tsurfaceparm clusterportal\n}\n"
	"textures/common/donotenter\n{\n\tsurfaceparm nodraw\n\tsurfaceparm nolightmap\n\tsurfaceparm nonsolid\n\tsurfaceparm trans\n\tsurfaceparm nomarks\n\tsurfaceparm donotenter\n}\n"
	"textures/common/nodrop\n{\n\tsurfaceparm nodraw\n\tsurfaceparm nolightmap\n\tsurfaceparm nonsolid\n\tsurfaceparm trans\n\tsurfaceparm nomarks\n\tsurfaceparm nodrop\n}\n"
	"textures/common/slick\n{\n\tsurfaceparm nodraw\n\tsurfaceparm nolightmap\n\tsurfaceparm trans\n\tsurfaceparm nomarks\n\tsurfaceparm slick\n}\n"
	"textures/common/lightgrid\n{\n\tsurfaceparm nodraw\n\tsurfaceparm nolightmap\n\tsurfaceparm nonsolid\n\tsurfaceparm detail\n\tsurfaceparm trans\n\tsurfaceparm nomarks\n\tsurfaceparm lightgrid\n}\n";

static const unsigned char C_LEAK[4] = {255, 40, 40, 255}, C_LEAK_HIDDEN[4] = {255, 40, 40, 150};

static std::string tool_exe(const char *name) {
	return std::string(OzEd_EngineDir()) + "/tools/" + name;
}

// "ed_" and the map's name in letters, digits and underscores
std::string Session::compile_name() const {
	std::string n = "ed_";
	for (char c : name) {
		n += std::isalnum((unsigned char)c) ? (char)std::tolower((unsigned char)c) : '_';
	}
	return n;
}

static std::string read_text(const std::string &path) {
	char *text = nullptr;
	const int len = OzEd_ReadOSFile(path.c_str(), &text);
	std::string out = len > 0 ? std::string(text, size_t(len)) : std::string();
	OzEd_Free(text);
	return out;
}

static bool copy_file(const std::string &from, const std::string &to) {
	char *data = nullptr;
	const int len = OzEd_ReadOSFile(from.c_str(), &data);
	bool ok = len > 0 && (OzEd_MakeDirs(to.c_str()), OzEd_WriteOSFile(to.c_str(), data, len) != 0);
	OzEd_Free(data);
	return ok;
}

// The line after q3map2's or mbspc's ERROR banner.
static std::string error_of(const std::string &log) {
	// the banner of the error that stopped it; an "ERROR: Unable to open file" without one is a model
	// q3map2 couldn't find, and it goes on
	const size_t at = log.find("************ ERROR ************");
	if (at == std::string::npos) {
		return "";
	}
	size_t line = log.find('\n', at);
	while (line != std::string::npos && line + 1 < log.size()) {
		const size_t next = log.find('\n', line + 1);
		std::string l = log.substr(line + 1, next == std::string::npos ? std::string::npos : next - line - 1);
		l.erase(std::remove(l.begin(), l.end(), '\r'), l.end());
		if (l.find_first_not_of(" *\t") != std::string::npos) {
			return l;
		}
		line = next;
	}
	return "";
}

bool Session::compile(bool full, bool play, bool here, bool defrag) {
	if (job_.running) {
		say("A compile is under way: wait for it, or stop it");
		return false;
	}
	const std::string q3map2 = tool_exe("q3map2.exe"), mbspc = tool_exe("mbspc.exe");
	const std::string baseq3 = OzEd_BaseQ3();
	if (!OzEd_FileExists(q3map2.c_str()) || baseq3.empty()) {
		say(baseq3.empty() ? "The game's baseq3 folder wasn't found" : "q3map2 isn't there (" + q3map2 + ")", true);
		return false;
	}
	CompileJob j;
	j.out = compile_name();
	j.dir = std::string(OzEd_HomePath()) + "/oz/editor/compile";
	j.map = j.dir + "/maps/" + j.out + ".map";
	const std::string bsp = j.dir + "/maps/" + j.out + ".bsp";
	// the map as it is now, and q3map2's tool shaders
	WriteOptions wo;
	wo.title = "compiled by the Oz Arena map editor";
	const std::string text = write_map(doc, wo);
	const std::string shaders = j.dir + "/home/baseq3/scripts/oz_editor_tools.shader";
	OzEd_MakeDirs(j.map.c_str());
	OzEd_MakeDirs(shaders.c_str());
	if (!OzEd_WriteOSFile(j.map.c_str(), text.data(), int(text.size())) || !OzEd_WriteOSFile(shaders.c_str(), TOOL_SHADERS, int(std::strlen(TOOL_SHADERS)))) {
		say("Can't write " + j.map, true);
		return false;
	}
	for (const char *ext : {".bsp", ".lin", ".aas", ".prt", ".srf"}) {
		std::remove((j.dir + "/maps/" + j.out + ext).c_str());  // a stage that fails leaves nothing of the last run
	}
	// id's paks only: a map pack's broken shader script stops q3map2
	std::string common = "-game quake3 -fs_basepath \"" + baseq3.substr(0, baseq3.find_last_of("/\\")) + "\" -fs_homepath \"" + j.dir + "/home\"";
	int count = 0;
	char **paks = OzEd_ListOSFiles(baseq3.c_str(), "pk3", &count);
	for (int i = 0; i < count; i++) {
		const char *n = paks[i];
		const bool id = std::strlen(n) == 8 && (n[0] == 'p' || n[0] == 'P') && n[3] >= '0' && n[3] <= '8';
		if (!id) {
			common += " -fs_forbiddenpath \"" + std::string(n) + "\"";
		}
	}
	OzEd_FreeList(paks);
	const std::string m = " \"" + j.map + "\"";
	j.stages.push_back({"BSP", q3map2, common + " -meta" + m});
	if (full) {
		j.stages.push_back({"vis", q3map2, common + " -vis" + m});
		j.stages.push_back({"light", q3map2, common + " -light -fast -filter -super 2 -bounce 2 -patchshadows" + m});
	} else {
		j.stages.push_back({"light", q3map2, common + " -light -fast -filter" + m});
	}
	// the bots' area file: a play-test has bots too (without one the bot library refuses the map)
	if (OzEd_FileExists(mbspc.c_str())) {
		j.stages.push_back({"bots", mbspc, "-forcesidesvisible -bsp2aas \"" + bsp + "\""});
	}
	j.full = full;
	j.play = play;
	j.here = here;
	j.defrag = defrag;
	for (Id id : doc.ids()) {
		j.snapshot[id] = doc.ptr(id);
	}
	// the order write_map wrote them in: q3map2 names a brush by its entity's place and its own in it
	std::vector<Id> ents = doc.ids_of(Kind::Entity);
	const auto world = std::find(ents.begin(), ents.end(), doc.worldspawn());
	if (world != ents.end()) {
		std::rotate(ents.begin(), world, world + 1);
	}
	for (Id e : ents) {
		std::vector<Id> one = {e};
		for (Id pid : doc.owned(e)) {
			one.push_back(pid);
		}
		j.order.push_back(std::move(one));
	}
	warnings_.clear();
	j.cam = cam;
	j.t0 = OzE_Milliseconds();
	j.running = true;
	job_ = j;
	compile_report_ = "running";
	compile_stage();
	return true;
}

void Session::compile_stage() {
	CompileJob &j = job_;
	const CompileJob::Stage &s = j.stages[j.at];
	j.log = j.dir + "/" + j.out + "_" + s.name + ".log";
	j.stage_t0 = OzE_Milliseconds();
	j.proc = OzEd_Start(s.exe.c_str(), s.args.c_str(), j.dir.c_str(), j.log.c_str());
	if (!j.proc) {
		compile_done(false, s.name + " didn't start (" + s.exe + ")");
	}
}

void Session::compile_done(bool ok, const std::string &why) {
	CompileJob &j = job_;
	j.running = false;
	const double secs = (OzE_Milliseconds() - j.t0) / 1000.0;
	if (!ok) {
		compile_report_ = "failed: " + why;
		say(why, true);
		return;
	}
	// into maps/ in the player's own files, where the game finds it
	const std::string to = std::string(OzEd_HomePath()) + "/oz/maps/" + j.out;
	if (!copy_file(j.dir + "/maps/" + j.out + ".bsp", to + ".bsp")) {
		compile_report_ = "failed: can't copy the BSP to " + to + ".bsp";
		say("Can't copy the BSP to " + to + ".bsp", true);
		return;
	}
	if (!copy_file(j.dir + "/maps/" + j.out + ".aas", to + ".aas")) {
		std::remove((to + ".aas").c_str());  // an old area file wouldn't match the new map
	}
	compiled_ = j.snapshot;
	compiled_name_ = j.out;
	if (j.preview || previewing()) {
		// the lit preview shows the new compile (a command: the client's systems start again)
		OzE_InsertText(("ed_world maps/" + j.out + ".bsp\n").c_str());
	}
	char buf[160];
	std::snprintf(buf, sizeof buf, "ok %s %d stages %.1f s", j.out.c_str(), int(j.stages.size()), secs);
	compile_report_ = buf;
	std::snprintf(buf, sizeof buf, "Compiled maps/%s.bsp in %.1f s%s", j.out.c_str(), secs, j.play ? ": playing it" : "");
	// models q3map2 couldn't bake in (id's own misc_models never shipped: a stock map recompiled loses them)
	std::set<std::string> missing;
	for (Id id : doc.ids_of(Kind::Entity)) {
		const Entity &e = doc.get(id)->entity;
		const std::string model = e.get("model");
		if (e.classname() == "misc_model" && !model.empty() && !missing.count(model)) {
			void *buf2 = nullptr;
			if (OzE_ReadFile(model.c_str(), &buf2) <= 0) {
				missing.insert(model);
			}
			if (buf2) {
				OzE_FreeFile(buf2);
			}
		}
	}
	std::string note = buf;
	if (!warnings_.empty()) {
		note += " \xc2\xb7 " + std::to_string(warnings_.size()) + (warnings_.size() == 1 ? " warning" : " warnings") + " (Warnings lists them)";
		compile_report_ += " warnings " + std::to_string(warnings_.size());
	}
	if (!missing.empty()) {
		note += " (" + std::to_string(missing.size()) + (missing.size() == 1 ? " misc_model's file isn't" : " misc_models' files aren't") +
			" in the game's files, so they're left out: " + *missing.begin() + (missing.size() > 1 ? " ..." : "") + ")";
		compile_report_ += " missing models " + std::to_string(missing.size());
	}
	say(note);
	if (j.play && j.defrag) {
		// under Defrag: the map and its .defi into the user folder's defrag, where the switch finds them;
		// Defrag's game runs it, its own timer and all; a disconnect comes back to Oz
		const std::string df = std::string(OzEd_HomePath()) + "/defrag";
		const std::string defi = defrag_defi(j.out);
		OzEd_MakeDirs((df + "/scripts/x").c_str());
		if (!copy_file(j.dir + "/maps/" + j.out + ".bsp", df + "/maps/" + j.out + ".bsp") ||
				!OzEd_WriteOSFile((df + "/scripts/" + j.out + ".defi").c_str(), defi.data(), int(defi.size()))) {
			say("Can't put the map in " + df, true);
			return;
		}
		if (!copy_file(j.dir + "/maps/" + j.out + ".aas", df + "/maps/" + j.out + ".aas")) {
			std::remove((df + "/maps/" + j.out + ".aas").c_str());  // the bots' file, when there is one
		}
		say("Playing " + j.out + " under Defrag, its own timer and all: Disconnect comes back to Oz, where the Map Editor keeps the map");
		OzE_InsertText(("game_restart defrag\nmap " + j.out + "\n").c_str());  // a plain map: Defrag times runs without cheats
		return;
	}
	if (j.play) {
		// a devmap (cheats, so edpos works); from the camera: the player's eye where it was
		std::string cmd = "devmap " + j.out + "\n";
		if (j.here) {
			char pos[160];
			std::snprintf(pos, sizeof pos, "wait 40\nedpos %.0f %.0f %.0f %.1f %.1f\n", j.cam.origin.x, j.cam.origin.y, j.cam.origin.z - 26,
				j.cam.pitch, j.cam.yaw);
			cmd += pos;
		}
		OzE_InsertText(cmd.c_str());  // before anything queued after the compile (a test script)
	}
}

void Session::compile_poll() {
	CompileJob &j = job_;
	if (!j.running) {
		return;
	}
	int code = -1;
	if (OzEd_Poll(j.proc, &code)) {
		return;  // still under way
	}
	const CompileJob::Stage &s = j.stages[j.at];
	const std::string log = read_text(j.log);
	collect_warnings(log, s.name);
	if (s.name == "BSP") {
		// q3map2 writes the line it traced from an entity out to the void beside the map
		leak_.clear();
		if (log.find("leaked") != std::string::npos) {
			const std::string lin = read_text(j.dir + "/maps/" + j.out + ".lin");
			size_t at = 0;
			while (at < lin.size()) {
				const size_t eol = lin.find('\n', at);
				Vec3 p;
				if (std::sscanf(lin.c_str() + at, "%lf %lf %lf", &p.x, &p.y, &p.z) == 3) {
					leak_.push_back(p);
				}
				at = eol == std::string::npos ? lin.size() : eol + 1;
			}
			leak_rev_++;
			compile_report_ = "leak " + std::to_string(leak_.size());
			j.running = false;
			say("The map leaks: the red line runs from an entity out to the void through the gap (Show the leak frames it). Seal it, then compile again", true);
			return;
		}
		leak_rev_++;
	}
	const std::string err = error_of(log);
	const bool made = OzEd_FileExists((j.dir + "/maps/" + j.out + (s.name == "bots" ? ".aas" : ".bsp")).c_str()) != 0;
	if (!err.empty() || code != 0 || !made) {
		compile_done(false, s.name + ": " + (!err.empty() ? err : code != 0 ? "stopped (exit code " + std::to_string(code) + ")" : "made nothing") +
			" (" + j.log + ")");
		return;
	}
	if (++j.at < j.stages.size()) {
		compile_stage();
	} else {
		compile_done(true, "");
	}
}

// What a stage's log warns of: q3map2's "Entity 0, Brush 12: <what>" (that brush, by the order the map
// was written in) and its "WARNING: <what>" lines (a material it couldn't find: the brushes and curves
// painted with it); mbspc's warnings too. Alike ones are counted once.
void Session::collect_warnings(const std::string &log, const std::string &stage) {
	const CompileJob &j = job_;
	size_t at = 0;
	while (at < log.size() && warnings_.size() < 100) {
		size_t eol = log.find('\n', at);
		std::string line = log.substr(at, eol == std::string::npos ? std::string::npos : eol - at);
		at = eol == std::string::npos ? log.size() : eol + 1;
		while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
			line.pop_back();
		}
		CompileWarning w;
		int e = -1, b = -1, n = 0;
		if (std::sscanf(line.c_str(), "Entity %d, Brush %d: %n", &e, &b, &n) == 2 && n > 0) {
			w.text = line.substr(size_t(n)) + " (entity " + std::to_string(e) + ", brush " + std::to_string(b) + ")";
			if (e >= 0 && size_t(e) < j.order.size() && b >= 0 && size_t(b) + 1 < j.order[size_t(e)].size()) {
				w.ids.push_back(j.order[size_t(e)][size_t(b) + 1]);
			} else if (e > 0 && size_t(e) < j.order.size()) {
				w.ids.push_back(j.order[size_t(e)][0]);  // a point entity ("Entity leaked"): the entity itself
			}
		} else {
			std::string up = line;
			std::transform(up.begin(), up.end(), up.begin(), [](unsigned char c) { return char(std::toupper(c)); });
			const size_t warn = up.find("WARNING:");
			if (warn == std::string::npos) {
				continue;
			}
			size_t from = line.find_first_not_of(" ", warn + 8);
			w.text = from == std::string::npos ? line : line.substr(from);
			if (w.text.compare(0, 19, "Unknown surfaceparm") == 0) {
				continue;  // the game's own shader scripts (id's nomipmaps), never the map's doing
			}
			// a material q3map2 couldn't find: what it's on
			const size_t sh = w.text.find("shader ");
			if (sh != std::string::npos) {
				std::string name = w.text.substr(sh + 7);
				name = name.substr(0, name.find_first_of(" \t"));
				if (name.compare(0, 9, "textures/") == 0) {
					name = name.substr(9);
				}
				for (Id id : doc.ids()) {
					const Object *o = doc.get(id);
					bool uses = o->kind == Kind::Patch && shader_name(o->patch.shader) == name;
					for (size_t f = 0; o->kind == Kind::Brush && f < o->brush.faces.size() && !uses; f++) {
						uses = shader_name(o->brush.faces[f].shader) == name;
					}
					if (uses) {
						w.ids.push_back(id);
					}
				}
			}
		}
		if (w.text.empty()) {
			continue;
		}
		w.text = stage + ": " + w.text;
		bool alike = false;
		for (CompileWarning &o : warnings_) {
			// the same warning in a later stage (light repeats the BSP's missing images) counts once
			const std::string mine = w.text.substr(w.text.find(": ") + 2), theirs = o.text.substr(o.text.find(": ") + 2);
			if (mine == theirs) {
				o.count++;
				alike = true;
				break;
			}
		}
		if (!alike) {
			warnings_.push_back(std::move(w));
		}
	}
}

bool Session::previewing() const {
	return OzEd_World()[0] != 0;
}

bool Session::changed_since(Id id) const {
	auto it = compiled_.find(id);
	return it == compiled_.end() || it->second != doc.ptr(id);
}

int Session::changes_since_compile() const {
	int n = 0;
	for (Id id : doc.ids()) {
		n += changed_since(id);
	}
	for (const auto &kv : compiled_) {
		n += !doc.get(kv.first);  // deleted since
	}
	return n;
}

void Session::toggle_preview() {
	if (previewing()) {
		OzE_InsertText("ed_world\n");
		return;
	}
	if (compiled_name_ == compile_name() && changes_since_compile() == 0 &&
			OzEd_FileExists((std::string(OzEd_HomePath()) + "/oz/maps/" + compiled_name_ + ".bsp").c_str())) {
		OzE_InsertText(("ed_world maps/" + compiled_name_ + ".bsp\n").c_str());
		return;
	}
	// the map changed since: a quick compile first, then the preview
	if (compile(false, false, false)) {
		job_.preview = true;
		say("Compiling for the lit preview");
	}
}

void Session::compile_stop() {
	if (!job_.running) {
		return;
	}
	OzEd_Stop(job_.proc);
	job_.running = false;
	compile_report_ = "stopped";
	say("Compile stopped");
}

void Session::compile_wait() {
	while (job_.running) {
		OzEd_Wait(20);
		compile_poll();
	}
}

std::string Session::compile_status() const {
	if (!job_.running) {
		return "";
	}
	char buf[96];
	std::snprintf(buf, sizeof buf, "Compiling %s: %s, %d s", job_.out.c_str(), job_.stages[job_.at].name.c_str(), (OzE_Milliseconds() - job_.t0) / 1000);
	return buf;
}

void Session::build_leak() {
	if (leak_rev_ == leak_built_) {
		return;
	}
	leak_built_ = leak_rev_;
	leak_lines_.begin();
	for (size_t i = 1; i < leak_.size(); i++) {
		leak_lines_.line(leak_[i - 1], leak_[i], C_LEAK);
		leak_lines_.line(leak_[i - 1], leak_[i], C_LEAK_HIDDEN, "$oz_ed_lines_xray");
	}
	// the end out in the void, and the entity's end
	for (size_t i : {size_t(0), leak_.size() - 1}) {
		if (i < leak_.size()) {
			leak_lines_.box_edges(leak_[i] - Vec3(6, 6, 6), leak_[i] + Vec3(6, 6, 6), C_LEAK, "$oz_ed_lines_xray");
		}
	}
	leak_lines_.end();
}

void Session::frame_leak() {
	if (leak_.empty()) {
		say("No leak: the last compile found the map sealed (or there hasn't been one)");
		return;
	}
	Bounds b;
	for (const Vec3 &p : leak_) {
		b.add(p);
	}
	const Vec3 c = b.center();
	const double r = std::max(b.size().length() * 0.5, 96.0);
	Vec3 f, l, u;
	cam.axes(f, l, u);
	cam.origin = c - f * (r * 1.4);
}

}  // namespace oz::ed
