// The map editor's keys (ENGINE_PLAN.md P13, engine/EDITOR.md): each action's keys, the player's own
// over the table's (`<fs_homepath>/oz/editor/keymap.cfg`, a line "action = keys" each), set by
// pressing them in the Keys dialog; and the presets: Oz's (the table's) and Radiant's (what
// GtkRadiant and NetRadiant bind by default, where the editor has the same action; the rest stays
// Oz's).

#include "ed_view.h"
#include "ed_engine.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

extern "C" {
#include "../ui/oz_ui_engine.h"
#include "../../../quake3e/code/client/keycodes.h"
}

namespace oz::ed {

static std::string keymap_file() {
	return std::string(OzEd_HomePath()) + "/oz/editor/keymap.cfg";
}

// "Ctrl+Shift+R" for a key with the modifiers held; "" for a key the editor doesn't name
std::string combo(int key, int mods) {
	std::string k;
	if (key >= 'a' && key <= 'z') {
		k = char(key - 'a' + 'A');
	} else if ((key >= '0' && key <= '9') || key == '[' || key == ']' || key == '-' || key == '=' || key == ',' || key == '.' || key == '/' ||
			key == ';' || key == '\'') {
		k = char(key);
	} else if (key >= K_F1 && key <= K_F12) {
		k = "F" + std::to_string(key - K_F1 + 1);
	} else {
		switch (key) {
		case K_DEL: k = "Del"; break;
		case K_INS: k = "Ins"; break;
		case K_BACKSPACE: k = "Backspace"; break;
		case K_ESCAPE: k = "Esc"; break;
		case K_ENTER: case K_KP_ENTER: k = "Enter"; break;
		case K_TAB: k = "Tab"; break;
		case K_SPACE: k = "Space"; break;
		case K_LEFTARROW: k = "Left"; break;
		case K_RIGHTARROW: k = "Right"; break;
		case K_UPARROW: k = "Up"; break;
		case K_DOWNARROW: k = "Down"; break;
		case K_PGUP: k = "PgUp"; break;
		case K_PGDN: k = "PgDn"; break;
		case K_HOME: k = "Home"; break;
		case K_END: k = "End"; break;
		default: return "";
		}
	}
	return std::string(mods & Session::CTRL ? "Ctrl+" : "") + (mods & Session::SHIFT ? "Shift+" : "") + (mods & Session::ALT ? "Alt+" : "") + k;
}

// an action the search and the Keys dialog list: one that does its work with no more said (the rest
// take numbers or names typed, or answer the page's own controls)
bool runs_alone(const Action &a) {
	static const char *page[] = {"row", "frame_ids", "answer", "recover", "discard_recovery", "tex_nudge"};
	if (std::string(a.help).compare(0, 3, "ed ") == 0) {
		return false;
	}
	for (const char *n : page) {
		if (!std::strcmp(a.name, n)) {
			return false;
		}
	}
	return true;
}

// "Ctrl+Y, Ctrl+Shift+Z" -> its combos
static std::vector<std::string> split_keys(const std::string &keys) {
	std::vector<std::string> out;
	size_t at = 0;
	while (at < keys.size()) {
		const size_t comma = keys.find(", ", at);
		const std::string one = keys.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
		if (!one.empty()) {
			out.push_back(one);
		}
		if (comma == std::string::npos) {
			break;
		}
		at = comma + 2;
	}
	return out;
}

static std::string join_keys(const std::vector<std::string> &keys) {
	std::string out;
	for (const std::string &k : keys) {
		out += (out.empty() ? "" : ", ") + k;
	}
	return out;
}

// what the Radiant preset binds (GtkRadiant's and NetRadiant's defaults for the same actions)
static const std::vector<std::pair<const char *, const char *>> &radiant() {
	static const std::vector<std::pair<const char *, const char *>> keys = {
		{"duplicate", "Space, Ctrl+D"},       // CloneSelection
		{"clip", "X"},                        // ToggleClipper
		{"clip_side", "Ctrl+Enter"},          // FlipClip
		{"subtract", "Shift+U"},              // CSGSubtract
		{"merge", "Ctrl+U"},                  // CSGMerge
		{"texture_lock", "Shift+T"},          // TogTexLock
		{"tex_nudge_left", "Shift+Left"},     // TexShiftLeft ...
		{"tex_nudge_right", "Shift+Right"},
		{"tex_nudge_up", "Shift+Up"},
		{"tex_nudge_down", "Shift+Down"},
		{"tex_rotate_left", "Shift+PgUp"},    // TexRotateCounter
		{"tex_rotate_right", "Shift+PgDn"},   // TexRotateClock
	};
	return keys;
}

void Session::load_keymap() {
	if (keymap_loaded_) {
		return;
	}
	keymap_loaded_ = true;
	keymap_.clear();
	char *text = nullptr;
	const int len = OzEd_ReadOSFile(keymap_file().c_str(), &text);
	const std::string t = len > 0 ? std::string(text, size_t(len)) : std::string();
	OzEd_Free(text);
	size_t at = 0;
	while (at < t.size()) {
		size_t eol = t.find('\n', at);
		std::string line = t.substr(at, eol == std::string::npos ? std::string::npos : eol - at);
		at = eol == std::string::npos ? t.size() : eol + 1;
		line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());
		const size_t eq = line.find(" = ");
		if (line.empty() || line[0] == '/' || eq == std::string::npos || !find_action(line.substr(0, eq))) {
			continue;
		}
		keymap_[line.substr(0, eq)] = line.substr(eq + 3);
	}
	keymap_rev++;
}

void Session::save_keymap() {
	std::string out = "// the map editor's keys, the player's own over its defaults: action = keys (\"\" none)\n";
	for (const auto &kv : keymap_) {
		out += kv.first + " = " + kv.second + "\n";
	}
	const std::string file = keymap_file();
	OzEd_MakeDirs(file.c_str());
	if (keymap_.empty()) {
		std::remove(file.c_str());
	} else {
		OzEd_WriteOSFile(file.c_str(), out.data(), int(out.size()));
	}
}

std::string Session::keys_of(const Action &a) {
	load_keymap();
	auto it = keymap_.find(a.name);
	return it != keymap_.end() ? it->second : a.keys;
}

const Action *Session::action_for(const std::string &c) {
	if (c.empty()) {
		return nullptr;
	}
	for (const Action &a : actions()) {
		for (const std::string &one : split_keys(keys_of(a))) {
			if (one == c) {
				return &a;
			}
		}
	}
	return nullptr;
}

void Session::bind(const std::string &action, const std::string &keys) {
	const Action *target = find_action(action);
	if (!target) {
		return;
	}
	// each combo one action's: taken from any other that had it
	const std::vector<std::string> mine = split_keys(keys);
	for (const Action &a : actions()) {
		if (action == a.name) {
			continue;
		}
		std::vector<std::string> theirs = split_keys(keys_of(a)), kept;
		for (const std::string &k : theirs) {
			if (std::find(mine.begin(), mine.end(), k) == mine.end()) {
				kept.push_back(k);
			}
		}
		if (kept.size() != theirs.size()) {
			keymap_[a.name] = join_keys(kept);
		}
	}
	keymap_[action] = keys;
	// a binding back at the table's is no binding of the player's
	for (auto it = keymap_.begin(); it != keymap_.end();) {
		const Action *a = find_action(it->first);
		it = a && it->second == a->keys ? keymap_.erase(it) : std::next(it);
	}
	save_keymap();
	keymap_rev++;
	say(std::string(target->label) + ": " + (keys.empty() ? "no keys" : keys));
}

void Session::keymap_preset(const std::string &name) {
	load_keymap();
	keymap_.clear();
	if (name == "radiant") {
		for (const auto &kv : radiant()) {
			bind(kv.first, kv.second);
		}
	}
	save_keymap();
	keymap_rev++;
	say(name == "radiant" ? "Radiant's keys, where the editor has the same action; Oz's for the rest" : "Oz's keys");
}

bool Session::bind_key(int key, int mods) {
	if (binding.empty()) {
		return false;
	}
	if (key == K_SHIFT || key == K_CTRL || key == K_ALT || key == K_MOUSE1 || key == K_MOUSE2 || key == K_MOUSE3) {
		return key != K_MOUSE1;  // a modifier alone waits for its key; the left button can still press Cancel
	}
	const std::string action = binding;
	if ((key == K_ESCAPE && !mods) || (key >= K_PAD0_A && key <= K_PAD0_RIGHTTRIGGER)) {
		binding.clear();
		say("Keys left as they were");
		return true;
	}
	if (key == K_BACKSPACE && !mods) {
		binding.clear();
		bind(action, "");
		return true;
	}
	if (key >= '1' && key <= '9' && !mods) {
		say("1 to 9 set the grid: another key, or Esc", true);
		return true;
	}
	const std::string c = combo(key, mods);
	if (c.empty()) {
		say("The editor doesn't take that key: another, or Esc", true);
		return true;
	}
	binding.clear();
	bind(action, c);
	return true;
}

}  // namespace oz::ed
