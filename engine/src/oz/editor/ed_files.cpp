// The map editor's safety net (ENGINE_PLAN.md P13, engine/EDITOR.md): the map written aside a while
// after it changes (autosave), offered back when the editor opens on a session that never saved it
// (the game closed or crashed), and a question before New throws unsaved changes away. Leaving for
// the main menu loses nothing: the session keeps the map until the editor opens again.

#include "ed_view.h"
#include "ed_engine.h"
#include "ed_mapfile.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>

extern "C" {
#include "../ui/oz_ui_engine.h"
}

namespace oz::ed {

static const int AUTOSAVE_MS = 20000;  // after a change, at most this long before it's written aside

static std::string autosave_dir() {
	return std::string(OzEd_HomePath()) + "/oz/editor/autosave";
}

void Session::autosave_tick() {
	if (!doc.dirty || doc.revision == autosave_rev_ || dragging()) {
		return;
	}
	const int now = OzE_Milliseconds();
	if (autosave_due_ == 0) {
		autosave_due_ = now + AUTOSAVE_MS;
		return;
	}
	if (now < autosave_due_) {
		return;
	}
	autosave_write();
}

void Session::autosave_write() {
	autosave_due_ = 0;
	if (!doc.dirty || doc.revision == autosave_rev_) {
		return;
	}
	WriteOptions wo;
	wo.title = name + ", kept by the Oz Arena map editor's autosave";
	const std::string text = write_map(doc, wo);
	const std::string map = autosave_dir() + "/" + autosave_name + ".map", info = autosave_dir() + "/" + autosave_name + ".txt";
	// what it was: its file, its name, when
	const std::string about = path + "\n" + name + "\n" + std::to_string((long long)std::time(nullptr)) + "\n";
	OzEd_MakeDirs(map.c_str());
	if (OzEd_WriteOSFile(map.c_str(), text.data(), int(text.size())) && OzEd_WriteOSFile(info.c_str(), about.data(), int(about.size()))) {
		autosave_rev_ = doc.revision;
	}
}

void Session::autosave_clear() {
	std::remove((autosave_dir() + "/" + autosave_name + ".map").c_str());
	std::remove((autosave_dir() + "/" + autosave_name + ".txt").c_str());
	autosave_rev_ = doc.revision;
	autosave_due_ = 0;
}

bool Session::recovery_offer(std::string &what) {
	if (no_recovery) {
		return false;
	}
	if (!recovery_looked_) {
		// the first look of a session: what an earlier one left (it closed or crashed unsaved) is moved aside,
		// so this session's autosaves, 20 s after its first change, can't write over it
		recovery_looked_ = true;
		const std::string from = autosave_dir() + "/autosave", to = autosave_dir() + "/recovered";
		if (!doc.dirty && !doc.can_undo() && OzEd_FileExists((from + ".map").c_str())) {
			std::remove((to + ".map").c_str());
			std::remove((to + ".txt").c_str());
			std::rename((from + ".map").c_str(), (to + ".map").c_str());
			std::rename((from + ".txt").c_str(), (to + ".txt").c_str());
		}
		recovery_kept_ = OzEd_FileExists((to + ".map").c_str()) != 0;
		recovery_what_.clear();
	}
	// offered into a session that hasn't touched a map yet (the file stays for the next one meanwhile)
	if (!recovery_kept_ || doc.dirty || doc.can_undo()) {
		return false;
	}
	if (!recovery_what_.empty()) {
		what = recovery_what_;
		return true;
	}
	char *text = nullptr;
	const int len = OzEd_ReadOSFile((autosave_dir() + "/recovered.txt").c_str(), &text);
	std::string about = len > 0 ? std::string(text, size_t(len)) : std::string();
	OzEd_Free(text);
	const size_t a = about.find('\n'), b = a == std::string::npos ? a : about.find('\n', a + 1);
	const std::string was = b == std::string::npos ? "a map" : about.substr(a + 1, b - a - 1);
	const long long at = b == std::string::npos ? 0 : std::atoll(about.c_str() + b + 1);
	const long long ago = at ? (long long)std::time(nullptr) - at : -1;
	char when[64] = "";
	if (ago >= 0 && ago < 120) {
		std::snprintf(when, sizeof when, ", %lld seconds ago", ago);
	} else if (ago >= 0 && ago < 7200) {
		std::snprintf(when, sizeof when, ", %lld minutes ago", ago / 60);
	} else if (ago >= 0) {
		std::snprintf(when, sizeof when, ", %lld hours ago", ago / 3600);
	}
	what = was + when;
	recovery_what_ = what;  // read once: the banner asks every frame
	return true;
}

bool Session::recover() {
	std::string err;
	char *text = nullptr;
	const int len = OzEd_ReadOSFile((autosave_dir() + "/recovered.txt").c_str(), &text);
	std::string about = len > 0 ? std::string(text, size_t(len)) : std::string();
	OzEd_Free(text);
	if (!open(autosave_dir() + "/recovered.map", err)) {
		say(err, true);
		return false;
	}
	// open now, it's this session's map: its own autosave keeps it from here
	std::remove((autosave_dir() + "/recovered.map").c_str());
	std::remove((autosave_dir() + "/recovered.txt").c_str());
	recovery_kept_ = false;
	// it goes back where it came from, unsaved
	const size_t a = about.find('\n'), b = a == std::string::npos ? a : about.find('\n', a + 1);
	path = a == std::string::npos ? "" : about.substr(0, a);
	name = b == std::string::npos ? "recovered" : about.substr(a + 1, b - a - 1);
	doc.dirty = true;
	autosave_rev_ = ~0ull;
	autosave_write();  // its file was the only copy: written aside again now
	say("Recovered " + name + ": save it to keep it");
	return true;
}

void Session::discard_recovery() {
	std::remove((autosave_dir() + "/recovered.map").c_str());
	std::remove((autosave_dir() + "/recovered.txt").c_str());
	recovery_kept_ = false;
	say("The kept map was thrown away");
}

// ---------------------------------------------------------------- asking before losing work

bool Session::ask_first(const std::string &action) {
	if (!doc.dirty) {
		return false;
	}
	asking = action;
	return true;
}

void Session::answer(const std::string &choice) {
	const std::string action = asking;
	asking.clear();
	if (action.empty() || choice == "cancel") {
		return;
	}
	if (choice == "save") {
		if (path.empty()) {
			// never saved: Save As asks its name, and what waited goes on once it's saved
			after_save_ = action;
			want_page = "save_as";
			return;
		}
		std::string err;
		if (!save(path, err)) {
			say(err, true);
			return;  // nothing lost: the question goes, the map stays
		}
		say("Saved " + name);
	} else {
		autosave_clear();  // thrown away as asked
	}
	if (action == "new") {
		new_map();
	} else {
		load(action);
	}
}

bool Session::saved_as(const std::string &typed) {
	std::string n = typed;
	if (n.size() > 4 && n.compare(n.size() - 4, 4, ".map") == 0) {
		n.resize(n.size() - 4);
	}
	n.erase(std::remove_if(n.begin(), n.end(), [](unsigned char c) { return !std::isalnum(c) && c != '_' && c != '-' && c != '.'; }), n.end());
	if (n.empty()) {
		say("A name of letters, digits, - and _", true);
		return false;
	}
	std::string err;
	const std::string file = maps_dir() + "/" + n + ".map", was = name;
	name = n;  // the file's title and the .defi take the new name
	if (!save(file, err)) {
		name = was;
		say(err, true);
		return false;
	}
	say(n == typed || n + ".map" == typed ? "Saved " + n : "Saved as " + n + " (a name of letters, digits, - and _)");
	const std::string next = after_save_;
	after_save_.clear();
	if (next == "new") {
		new_map();
	} else if (!next.empty()) {
		load(next);
	}
	return true;
}

std::string Session::maps_dir() const {
	return std::string(OzEd_HomePath()) + "/oz/editor/maps";
}

void Session::load(const std::string &asked) {
	const size_t tab = asked.find('\t');
	if (tab == std::string::npos) {
		return;
	}
	const std::string how = asked.substr(0, tab), what = asked.substr(tab + 1);
	std::string err;
	if (how == "import") {
		if (import_bsp(what, err)) {
			say("Opening " + what + ": q3map2 makes it editable, a few seconds");  // done in the background
		} else {
			say(err, true);
		}
	} else if (open(what, err)) {
		say("Opened " + name);
	} else if (!err.empty()) {
		say(err, true);
	}
}

}  // namespace oz::ed
