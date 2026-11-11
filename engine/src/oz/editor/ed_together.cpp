// The multiplayer map editor's session (ed_together.h, ENGINE_PLAN.md PE).
#include "ed_together.h"
#include "ed_mapfile.h"
#include "ed_net.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>

extern "C" {
#include "../ui/oz_ui_engine.h"
}

namespace oz::ed {

// ---------------------------------------------------------------- the two sessions

namespace {
bool g_together = false;

void log(const char *fmt, ...) {
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	std::vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	OzE_Print(buf);
}

const int PRESENCE_MS = 50;        // 20 a second while something moves
const int PRESENCE_BEAT_MS = 500;  // and twice a second while nothing does: the link stays heard
const int PRESENCE_GONE_MS = 6000; // a player silent this long is drawn faded
const int SELECT_MS = 200;         // a selection sent at most five times a second
const int HOST_SILENT_MS = 8000;   // a host heard nothing from this long is gone: someone takes over
const int PLAY_SILENT_MS = 30000;  // in a play-test together and for PLAY_AFTER_MS after it (its switches back)
const int PLAY_AFTER_MS = 15000;
const int GHOST_MS = 100;          // a reshaping drag's objects sent at most ten times a second
const int RECENT_MS = 1500;        // what another player changed stays outlined this long, fading
const size_t GHOST_MAX = 128;      // a drag's objects sent as ghosts at most (a big group's push: the first ones)
// id blocks a player gets in a session, the first with the welcome: eight million ids. Asked for without
// end (each ask gave a block, as did each hello said again), a member ran the host out of ids. Eight players'
// seven more each is under the 64 blocks a player taking over skips past the last roster's (migrate)
const int MAX_BLOCKS = 8;
const int WELCOME_AGAIN_MS = 2000; // the session's map goes to one player no more often than this
const int RESYNC_ASK_MS = 3000;    // a joiner waiting for the map again asks again this often

// The date and time for a kept file's name: 20261110_142530
std::string file_stamp() {
	char buf[32];
	const std::time_t t = std::time(nullptr);
	const std::tm *tm = std::localtime(&t);
	if (!tm || !std::strftime(buf, sizeof buf, "%Y%m%d_%H%M%S", tm)) {
		std::snprintf(buf, sizeof buf, "%lld", (long long)t);
	}
	return buf;
}

// A session's map's name as its host sent it, made a name the editor's own Save as would give (letters,
// digits, _ - and ., ed_files.cpp's saved_as) with blanks let through, as an opened file's name may have
// them: it becomes this player's map's name, a file's title and part of the paths their Save writes
std::string safe_map_name(const std::string &from) {
	std::string n;
	for (unsigned char c : from) {
		if (std::isalnum(c) || c == '_' || c == '-' || c == '.' || c == ' ') {
			n += char(c);
		}
	}
	n = n.substr(0, 64);
	return n.find_first_not_of(". ") == std::string::npos ? "untitled" : n;
}
}  // namespace

std::string keep_unsaved_map(Session &s) {
	if (!s.doc.dirty) {
		return "";
	}
	std::string n;
	for (unsigned char c : s.name) {
		if (std::isalnum(c) || c == '_' || c == '-') {
			n += char(c);
		}
	}
	n = n.empty() ? "untitled" : n.substr(0, 48);
	const std::string file = n + "_kept_" + file_stamp() + ".map", path = s.maps_dir() + "/" + file;
	WriteOptions wo;
	wo.title = n + ", kept by the Oz Arena map editor (it was unsaved when a session's map took its place)";
	const std::string text = write_map(s.doc, wo);
	OzEd_MakeDirs(path.c_str());
	if (!OzEd_WriteOSFile(path.c_str(), text.data(), int(text.size()))) {
		log("^3map editor: the unsaved map couldn't be kept (%s)\n", path.c_str());
		return "";
	}
	log("map editor: the unsaved map was kept as %s\n", path.c_str());
	return file;
}

std::string recover_together_autosave() {
	Session &s = together_session();
	const std::string from = std::string(OzEd_HomePath()) + "/oz/editor/autosave/" + s.autosave_name;
	if (!OzEd_FileExists((from + ".map").c_str())) {
		return "";
	}
	const std::string file = "together_recovered_" + file_stamp() + ".map", path = s.maps_dir() + "/" + file;
	OzEd_MakeDirs(path.c_str());
	if (std::rename((from + ".map").c_str(), path.c_str()) != 0) {
		log("^3map editor: the map an earlier session left couldn't be moved to %s\n", path.c_str());
		return "";
	}
	std::remove((from + ".txt").c_str());
	log("map editor: the map an earlier session left was kept as %s\n", path.c_str());
	return file;
}

Session &solo_session() {
	static Session s;
	return s;
}

Session &together_session() {
	static Session *s = [] {
		static Session t;
		t.autosave_name = "together";  // its autosave apart from the map editor alone's
		t.no_recovery = true;          // a session's map is its host's: nothing of an earlier run is offered over it
		return &t;
	}();
	return *s;
}

void use_session(bool together) {
	g_together = together;
}

bool using_together() {
	return g_together;
}

Session &session() {
	return g_together ? together_session() : solo_session();
}

Together &together() {
	static Together t;
	return t;
}

// ---------------------------------------------------------------- the session's hooks into Session

void Session::drop_drag(const std::string &why) {
	if (drag_ == Drag::None) {
		return;
	}
	cancel_drag();
	if (!why.empty()) {
		say(why, true);
	}
}

std::vector<Id> Session::held_ids() const {
	std::vector<Id> out;
	switch (drag_) {
	case Drag::Move:
		out = with_owned(doc, selected());
		break;
	case Drag::Push:
		for (const auto &f : push_faces_) {
			out.push_back(f.first);
		}
		break;
	case Drag::Corners:
		for (const auto &kv : corner_pts_) {
			out.push_back(kv.first);
		}
		break;
	case Drag::Land:
		out.push_back(land_drag_pad_);
		break;
	case Drag::Apex:
		out.push_back(apex_drag_pad_);
		break;
	default:
		break;
	}
	std::sort(out.begin(), out.end());
	out.erase(std::unique(out.begin(), out.end()), out.end());
	return out;
}

bool Session::pointer_at(Vec3 &at) const {
	if (!over_ || !hover_ok_) {
		return false;
	}
	at = hover_.point;
	return true;
}

bool Session::move_ghost(Vec3 &delta) const {
	if (drag_ != Drag::Move) {
		return false;
	}
	delta = delta_;
	return true;
}

// ---------------------------------------------------------------- colors

static const unsigned char COLORS[8][4] = {
	{245, 158, 11, 255},   // amber
	{56, 189, 248, 255},   // sky
	{74, 222, 128, 255},   // green
	{167, 139, 250, 255},  // violet
	{244, 114, 182, 255},  // pink
	{250, 204, 21, 255},   // yellow
	{45, 212, 191, 255},   // teal
	{229, 231, 235, 255},  // white
};
static const char *HEX[8] = {"#f59e0b", "#38bdf8", "#4ade80", "#a78bfa", "#f472b6", "#facc15", "#2dd4bf", "#e5e7eb"};

const unsigned char *Together::color_of(int c) {
	return COLORS[(c % 8 + 8) % 8];
}

const char *Together::color_hex(int c) {
	return HEX[(c % 8 + 8) % 8];
}

// ---------------------------------------------------------------- players

const Together::Player *Together::player(uint32_t num) const {
	auto it = players.find(num);
	return it == players.end() ? nullptr : &it->second;
}

Together::Role Together::my_role() const {
	const Player *p = mine();
	return p ? p->role : (hosting ? Role::Owner : Role::Editor);
}

std::string Together::name_of(uint32_t num) const {
	const Player *p = player(num);
	if (!p) {
		return "Someone";
	}
	// the name without its color codes
	std::string out;
	for (size_t i = 0; i < p->name.size(); i++) {
		if (p->name[i] == '^' && i + 1 < p->name.size() && p->name[i + 1] != '^') {
			i++;
			continue;
		}
		out += p->name[i];
	}
	return out.empty() ? "Player" : out;
}

uint32_t Together::num_of(const std::string &pid) const {
	for (const auto &kv : players) {
		if (kv.second.pid == pid) {
			return kv.first;
		}
	}
	return 0;
}

uint32_t Together::holder(Id id) const {
	auto it = claims_.find(id);
	return it == claims_.end() || it->second == me ? 0 : it->second;
}

void Together::note(const std::string &text) {
	Session &s = together_session();
	if (s.notify) {
		s.notify(text, false);
	}
	log("map editor: %s\n", text.c_str());
}

// ---------------------------------------------------------------- the wire

void Together::send_to(const std::string &pid, const sync::Writer &w, bool fast) {
	OzEdNet_Send(pid.c_str(), w.out.data(), int(w.out.size()), fast ? 1 : 0);
}

void Together::to_all(const sync::Writer &w, uint32_t except, bool fast) {
	for (const auto &kv : players) {
		if (kv.first != me && kv.first != except && kv.second.linked) {
			send_to(kv.second.pid, w, fast);
		}
	}
}

void Together::to_host(const sync::Writer &w, bool fast) {
	if (!hosting && !host_pid.empty()) {
		send_to(host_pid, w, fast);
	}
}

void Together::put_roster(sync::Writer &w) const {
	w.var(players.size());
	for (const auto &kv : players) {
		const Player &p = kv.second;
		w.var(p.num);
		w.str(p.pid);
		w.str(p.name);
		w.str(p.model);
		w.u8(uint8_t(p.role));
		w.u8(uint8_t(p.color));
	}
	// what a player taking over from the host needs: the session's password, and the next id block it may give
	w.str(password_);
	w.i32(host_ ? host_->next_block() : known_block_);
}

bool Together::get_roster(sync::Reader &r) {
	std::map<uint32_t, Player> next;
	const uint64_t n = r.var();
	if (!r.ok || n > 64) {
		return false;
	}
	for (uint64_t i = 0; i < n && r.ok; i++) {
		Player p;
		p.num = uint32_t(r.var());
		p.pid = r.str();
		p.name = r.str();
		p.model = r.str();
		p.role = Role(r.u8() % 3);
		p.color = r.u8() % 8;
		auto old = players.find(p.num);
		if (old != players.end()) {
			p.pres = old->second.pres;
			p.sel = old->second.sel;
		}
		p.linked = true;
		next[p.num] = p;
	}
	const std::string password = r.str();
	const Id block = r.i32();
	if (!r.ok) {
		return false;
	}
	password_ = password;
	known_block_ = std::max(known_block_, block);
	players = std::move(next);
	changed_rev++;
	return true;
}

void Together::roster() {
	if (!hosting) {
		return;
	}
	sync::Writer w;
	w.u8(M_ROSTER);
	put_roster(w);
	to_all(w);
	relist();
	changed_rev++;
}

void Together::relist() {
	int in = 0;
	for (const auto &kv : players) {
		in += kv.second.linked || kv.first == me;
	}
	// the hub keeps a map's name as a word of a-z, 0-9 and _
	std::string map;
	for (char c : map_name) {
		const char l = char(std::tolower((unsigned char)c));
		map += (l >= 'a' && l <= 'z') || (l >= '0' && l <= '9') ? l : '_';
	}
	map = map.substr(0, 32);
	OzEdNet_Host(session_name.c_str(), map.empty() ? "untitled" : map.c_str(), in, max_players, password_.c_str());
}

// ---------------------------------------------------------------- starting and ending

void Together::attach() {
	Session &s = together_session();
	s.intercept = [this](const std::string &action, const std::vector<std::string> &) {
		if (!active()) {
			return false;
		}
		if (action == "new" || action == "open" || action == "import" || action == "recover") {
			note("The session's map stays the session's: leave the session to open another (File, Leave session)");
			return true;
		}
		if (action == "play_together" || action == "play_defrag_together") {
			std::string why;
			if (!play_together(why, action == "play_defrag_together")) {
				together_session().notify(why, true);
			}
			return true;
		}
		if (action == "leave_session") {
			leave();
			return true;
		}
		return false;
	};
	s.grab = [this](const std::vector<Id> &ids, std::string &why) {
		if (!live()) {
			return true;
		}
		if (my_role() == Role::Viewer) {
			why = "You're watching this session: the host can make you an editor";
			return false;
		}
		for (Id id : ids) {
			if (const uint32_t h = holder(id)) {
				why = name_of(h) + " is moving this";
				return false;
			}
		}
		claimed_ = ids;
		sync::Writer w;
		w.u8(M_CLAIM);
		w.var(ids.size());
		for (Id id : ids) {
			w.i32(id);
		}
		if (hosting) {
			uint32_t h = 0;
			if (!host_->claim(me, ids, h)) {
				why = name_of(h) + " is moving this";
				claimed_.clear();
				return false;
			}
			sync::Writer c;
			c.u8(M_CLAIMS);
			c.var(me);
			c.var(ids.size());
			for (Id id : ids) {
				c.i32(id);
			}
			to_all(c);
		} else {
			to_host(w);
		}
		return true;
	};
	s.draw_extra = [this](bool is3d) { draw(is3d); };
}

void Together::detach() {
	Session &s = together_session();
	s.intercept = nullptr;
	s.grab = nullptr;
	s.draw_extra = nullptr;
}

void Together::start_sharing(uint32_t num, uint32_t seq) {
	Session &s = together_session();
	rep_ = std::make_unique<sync::Replica>(s.doc);
	rep_->start(num, seq);
	rep_->send = [this](const sync::Edit &e) {
		if (hosting) {
			host_->take(e);
			return;
		}
		if (resync_) {
			return;  // waiting for the session's map again: it goes to the host after that (the welcome sends what's unanswered)
		}
		sync::Writer w;
		w.u8(M_EDIT);
		sync::put_edit(w, e, true);
		to_host(w);
	};
	rep_->on_remote = [this](const sync::Edit &e, const std::vector<Id> &ids) {
		Session &s2 = together_session();
		// outlined a moment in their color, so a change never just appears; their drag's ghost is done with
		if (e.player && e.player != me) {
			const int at = OzEdNet_Milliseconds();
			for (Id id : ids) {
				recent_[id] = {e.player, at};
			}
			ghosts_.erase(e.player);
		}
		// what someone else changed leaves this player's selection when it's gone
		bool gone = false;
		for (Id id : ids) {
			gone |= s2.sel.count(id) && !s2.doc.get(id);
		}
		if (gone) {
			std::vector<Id> keep;
			for (Id id : s2.sel) {
				if (s2.doc.get(id)) {
					keep.push_back(id);
				}
			}
			s2.select(keep, SelMode::Set);
		}
		const int now = OzEdNet_Milliseconds();
		if (e.player && e.player != me && !e.label.empty() && now - last_note_ > 1500) {
			last_note_ = now;
			if (s2.notify) {
				s2.notify(name_of(e.player) + ": " + e.label, false);
			}
		}
	};
	me = num;
	attach();
}

bool Together::host(const std::string &name, const std::string &password, int max, std::string &err) {
	if (!OzEdNet_Available()) {
		err = "This build has no peer-to-peer play";
		return false;
	}
	if (!OzEdNet_Online()) {
		err = "The online service isn't connected: Settings, Network, Online";
		return false;
	}
	if (active()) {
		leave();
	}
	Session &s = together_session();
	hosting = true;
	host_pid = OzEdNet_Me();
	host_name = OzEdNet_Name();
	session_name = name.empty() ? std::string(OzEdNet_Name()) + "'s map" : name;
	map_name = s.name;
	password_ = password;
	max_players = std::max(2, std::min(sync::MAX_PLAYERS, max));
	players.clear();
	claims_.clear();
	why.clear();
	next_num_ = 2;
	Player p;
	p.num = 1;
	p.pid = host_pid;
	p.name = host_name;
	p.model = OzEdNet_Model();
	p.role = Role::Owner;
	p.color = 0;
	p.linked = true;
	players[1] = p;
	resync_ = false;
	answered_.clear();
	play_starter_ = 0;
	start_sharing(1, 0);
	host_ = std::make_unique<sync::Host>(*rep_);
	host_->start(0);
	host_->deliver = [this](const sync::Edit &e) { deliver(e); };
	Id from = 0, end = 0;
	host_->grant(from, end);
	if (end > from) {
		s.doc.lease(from, end);
	}
	state = State::Live;
	relist();
	OzEdNet_Presence((std::string("edit:") + host_pid + (password_.empty() ? "" : " locked")).c_str(), ("Editing " + map_name).c_str());
	changed_rev++;
	log("together: hosting \"%s\" (%s), up to %i players\n", session_name.c_str(), map_name.c_str(), max_players);
	return true;
}

void Together::join(const std::string &pid, const std::string &password, const std::string &name) {
	if (active()) {
		leave();
	}
	why.clear();
	if (!OzEdNet_Available() || !OzEdNet_Online()) {
		state = State::Ended;
		why = "The online service isn't connected: Settings, Network, Online";
		changed_rev++;
		return;
	}
	if (pid == OzEdNet_Me()) {
		state = State::Ended;
		why = "That's your own session";
		changed_rev++;
		return;
	}
	hosting = false;
	host_pid = pid;
	host_name = name;
	players.clear();
	claims_.clear();
	resync_ = false;
	answered_.clear();
	play_starter_ = 0;
	state = State::Joining;
	joined_at_ = OzEdNet_Milliseconds();
	OzEdNet_Join(pid.c_str(), password.c_str());
	changed_rev++;
	log("together: asking %s to join their session\n", pid.c_str());
}

void Together::ended(const std::string &reason) {
	why = reason;
	if (rep_) {
		rep_->stop();
	}
	rep_.reset();
	host_.reset();
	detach();
	together_session().doc.lease(0, 0);
	claims_.clear();
	claimed_.clear();
	follow = 0;
	resync_ = false;
	answered_.clear();
	play_starter_ = 0;
	drop_play_files();  // the session's play-test maps aren't left in the player's maps folder
	const bool was_host = hosting;
	hosting = false;
	state = State::Ended;
	OzEdNet_Presence("", "");
	if (was_host) {
		OzEdNet_Unhost();
	}
	changed_rev++;
	log("together: the session ended%s%s\n", reason.empty() ? "" : ": ", reason.c_str());
}

void Together::leave(const std::string &reason) {
	if (!active()) {
		state = State::Off;
		changed_rev++;
		return;
	}
	sync::Writer w;
	w.u8(M_BYE);
	w.str(reason.empty() ? (hosting ? "The host ended the session" : name_of(me) + " left") : reason);
	if (hosting) {
		to_all(w);
	} else if (!host_pid.empty()) {
		to_host(w);
		OzEdNet_Leave(host_pid.c_str());
	}
	ended(reason);
	state = State::Off;
}

void Together::kick(uint32_t num) {
	if (!hosting || num == me) {
		return;
	}
	auto it = players.find(num);
	if (it == players.end()) {
		return;
	}
	sync::Writer w;
	w.u8(M_KICK);
	w.str("The host took you out of the session");
	send_to(it->second.pid, w);
	const std::string pid = it->second.pid;
	note(name_of(num) + " is out of the session");
	players.erase(it);
	host_->release(num);
	host_->set_viewer(num, false);
	if (play_starter_ == num) {
		play_starter_ = 0;
	}
	OzEdNet_Leave(pid.c_str());
	roster();
}

void Together::set_role(uint32_t num, Role role) {
	if (!hosting || num == me || role == Role::Owner) {
		return;
	}
	auto it = players.find(num);
	if (it == players.end()) {
		return;
	}
	it->second.role = role;
	host_->set_viewer(num, role == Role::Viewer);
	roster();
}

// ---------------------------------------------------------------- the host's order

void Together::deliver(const sync::Edit &e) {
	// everyone's copy; its maker's only the numbers and what the host changed
	sync::Writer full, ack;
	full.u8(M_SEQ);
	sync::put_edit(full, e, false);
	ack.u8(M_SEQ);
	sync::put_edit(ack, sync::Host::ack(e), false);
	for (auto &kv : players) {
		Player &p = kv.second;
		if (kv.first == me) {
			continue;
		}
		if (!p.linked || (e.refused && kv.first != e.player)) {
			continue;
		}
		send_to(p.pid, kv.first == e.player ? ack : full);
	}
	if (!e.refused || e.player == me) {
		rep_->receive(e);
	}
}

bool Together::lease_for(Player &p, Id &from, Id &end) {
	from = end = 0;
	if (p.blocks >= MAX_BLOCKS) {
		return false;
	}
	host_->grant(from, end);
	if (end <= from) {
		from = end = 0;
		return false;
	}
	p.blocks++;
	return true;
}

void Together::welcome(Player &p, bool again) {
	Session &s = together_session();
	sync::Writer snap;
	rep_->snapshot(snap);
	Id from = 0, end = 0;  // 0, 0: no block with this one (the map sent again, or they've had their share)
	if (!again) {
		lease_for(p, from, end);
	}
	sync::Writer w;
	w.u8(M_WELCOME);
	w.var(p.num);
	w.i32(from);
	w.i32(end);
	w.str(session_name);
	w.str(map_name);
	w.u8(uint8_t(s.doc.plain_form));
	put_roster(w);
	w.bytes(sync::pack(snap.out));
	// the claims standing now
	w.var(host_->claims().size());
	for (const auto &kv : host_->claims()) {
		w.i32(kv.first);
		w.var(kv.second);
	}
	send_to(p.pid, w);
	p.welcomed_at = OzEdNet_Milliseconds();
	if (again) {
		log("together: the session's map sent to %s again (a snapshot of %zu objects at %u, %zu KB)\n", name_of(p.num).c_str(),
			rep_->state().size(), rep_->seq(), w.out.size() / 1024);
		return;
	}
	p.linked = true;
	host_->set_viewer(p.num, p.role == Role::Viewer);  // a viewer coming back to a player who took the session over stays one
	log("together: %s joined as player %u (a snapshot of %zu objects at %u, %zu KB)\n", name_of(p.num).c_str(), p.num,
		rep_->state().size(), rep_->seq(), w.out.size() / 1024);
	note(name_of(p.num) + " joined the session");
	// the others' selections, so the joiner sees them
	for (const auto &kv : players) {
		if (kv.first == p.num || kv.second.sel.empty()) {
			continue;
		}
		sync::Writer sw;
		sw.u8(M_SELECT);
		sw.var(kv.first);
		sw.var(kv.second.sel.size());
		for (Id id : kv.second.sel) {
			sw.i32(id);
		}
		send_to(p.pid, sw);
	}
	roster();
}

// ---------------------------------------------------------------- messages

void Together::on_up(const std::string &pid) {
	if (hosting) {
		return;  // a joiner: they say hello
	}
	if (pid == host_pid && migrating_) {
		// linked to the player who took over: hello again, as the player we were
		sync::Writer w;
		w.u8(M_HELLO);
		w.str(OzEdNet_Version());
		w.str(OzEdNet_Name());
		w.str(OzEdNet_Model());
		send_to(pid, w);
		host_heard_ = OzEdNet_Milliseconds();
		log("together: linked to the new host, waiting for the session\n");
		return;
	}
	if (pid == host_pid && state == State::Joining) {
		state = State::Syncing;
		sync::Writer w;
		w.u8(M_HELLO);
		w.str(OzEdNet_Version());
		w.str(OzEdNet_Name());
		w.str(OzEdNet_Model());
		send_to(pid, w);
		changed_rev++;
		log("together: linked to the host, waiting for the map\n");
	}
}

void Together::on_down(const std::string &pid, bool failed, const std::string &reason) {
	if (hosting) {
		const uint32_t n = num_of(pid);
		if (n && n != me) {
			note(name_of(n) + " left the session");
			players.erase(n);
			host_->release(n);
			host_->set_viewer(n, false);
			if (play_starter_ == n) {
				play_starter_ = 0;
			}
			sync::Writer c;
			c.u8(M_CLAIMS);
			c.var(n);
			c.var(0);
			to_all(c);
			roster();
		}
		return;
	}
	if (pid != host_pid || !active()) {
		return;
	}
	if (migrating_) {
		// the new host not reached yet (it may not have listed the session again): ask again a moment later
		retry_at_ = OzEdNet_Milliseconds() + 2000;
		return;
	}
	if (state == State::Live && !failed) {
		migrate("The link to the host was lost");  // without a goodbye: someone takes the session over
		return;
	}
	ended(reason.empty() ? "The host couldn't be reached" : reason);
}

// ---------------------------------------------------------------- the host gone: someone takes over (PE step 7)

// The host went without ending the session (its game closed, its link lost). Everyone left agrees on who takes
// over without asking: the player with the lowest number (the earliest in). That player hosts from where their copy
// of the session is, lists it again with its name and password, gives ids from past every block given before, and
// takes their own unanswered edits in order; the others link to them, get the session as it is there, and send
// their unanswered edits again.
void Together::migrate(const std::string &why) {
	Session &s = together_session();
	const uint32_t gone = num_of(host_pid);
	const std::string gone_name = name_of(gone);
	players.erase(gone);
	claims_.clear();
	claimed_.clear();
	follow = 0;
	uint32_t next = 0;
	for (const auto &kv : players) {
		next = next ? std::min(next, kv.first) : kv.first;
	}
	if (!next) {
		ended(why);
		return;
	}
	log("together: the host (%s) went: %s; player %u takes over\n", gone_name.c_str(), why.c_str(), next);
	OzEdNet_Leave(host_pid.c_str());
	if (next == me) {
		hosting = true;
		host_pid = OzEdNet_Me();
		host_name = OzEdNet_Name();
		migrating_ = false;
		host_ = std::make_unique<sync::Host>(*rep_);
		host_->start(rep_->seq());
		// past any block given since the last roster heard (none once that would pass the last id: grant gives no more)
		host_->set_next_block(known_block_ > 0 && known_block_ < (1 << 30) ? known_block_ + 64 * sync::BLOCK : 0);
		host_->deliver = [this](const sync::Edit &e) { deliver(e); };
		next_num_ = 2;
		for (auto &kv : players) {
			kv.second.linked = kv.first == me;
			kv.second.blocks = 0;
			kv.second.welcomed_at = 0;
			next_num_ = std::max(next_num_, kv.first + 1);
			// the viewers stay viewers under the new host (its order refuses their edits as the old one's did)
			host_->set_viewer(kv.first, kv.first != me && kv.second.role == Role::Viewer);
		}
		players[me].role = Role::Owner;
		// in a play-test another player hosts: theirs is the one that's on
		play_starter_ = play.stage == 4 && play.starter != me ? play.starter : 0;
		for (const sync::Edit &e : rep_->unanswered()) {
			if (resync_ && answered_.count(e.local)) {
				continue;  // answered by the old host while this copy waited for the map again
			}
			host_->take(e);  // this player's own edits the old host never answered, first in the new order
		}
		resync_ = false;
		answered_.clear();
		Id from = 0, end = 0;
		host_->grant(from, end);
		s.doc.lease(0, 0);
		if (end > from) {
			s.doc.lease(from, end);
		}
		relist();
		OzEdNet_Presence((std::string("edit:") + host_pid + (password_.empty() ? "" : " locked")).c_str(), ("Editing " + map_name).c_str());
		note(gone_name + " left: you host the session now");
	} else {
		const Player &p = players[next];
		host_pid = p.pid;
		host_name = p.name;
		migrating_ = true;
		migrate_until_ = OzEdNet_Milliseconds() + 45000;
		retry_at_ = OzEdNet_Milliseconds() + 1500;  // a moment for them to list it again
		for (auto &kv : players) {
			kv.second.linked = false;
		}
		note(gone_name + " left: " + name_of(next) + " takes the session over");
	}
	changed_rev++;
}

void Together::migrate_tick() {
	if (!migrating_) {
		return;
	}
	const int now = OzEdNet_Milliseconds();
	if (now > migrate_until_) {
		migrating_ = false;
		ended("The host left, and the session couldn't be taken over");
		return;
	}
	if (retry_at_ && now >= retry_at_) {
		retry_at_ = 0;
		OzEdNet_Join(host_pid.c_str(), password_.c_str());
	}
}

void Together::on_message(const std::string &pid, const sync::Bytes &b) {
	sync::Reader r(b);
	const uint8_t type = r.u8();
	Session &s = together_session();
	if (type >= M_PAK && type <= M_RELOAD) {
		if (!hosting && pid != host_pid) {
			return;
		}
		if (hosting) {
			// a play-test is an editor's to start, feed and reload: a viewer's messages for one went to
			// everyone as any member's did (a map file written on every PC, everyone sent into a match)
			const Player *sender = player(num_of(pid));
			if (!sender || !sender->linked) {
				return;
			}
			if (sender->role == Role::Viewer) {
				log("together: a play-test message from %s, who is watching, refused\n", name_of(sender->num).c_str());
				return;
			}
		}
		on_play(pid, type, r, b);
		return;
	}
	if (hosting) {
		uint32_t from = num_of(pid);
		Player *p = from ? &players[from] : nullptr;
		switch (type) {
		case M_HELLO: {
			const std::string version = r.str(), name = r.str(), model = r.str();
			if (version != OzEdNet_Version()) {
				sync::Writer w;
				w.u8(M_BYE);
				w.str("The session is on " + std::string(OzEdNet_Version()) + "; you have " + version);
				send_to(pid, w);
				return;
			}
			int in = 0;
			for (const auto &kv : players) {
				in += kv.second.linked || kv.first == me;
			}
			if (!p && in >= max_players) {
				sync::Writer w;
				w.u8(M_BYE);
				w.str("The session is full");
				send_to(pid, w);
				return;
			}
			if (p && p->linked && OzEdNet_Milliseconds() - p->welcomed_at < WELCOME_AGAIN_MS) {
				return;  // said again by a player just welcomed: a hello a frame was a snapshot and an id block a frame
			}
			if (!p) {
				Player np;
				np.num = next_num_++;
				np.pid = pid;
				// the first color nobody has
				for (int c = 0; c < 8; c++) {
					bool used = false;
					for (const auto &kv : players) {
						used |= kv.second.color == c;
					}
					if (!used) {
						np.color = c;
						break;
					}
				}
				players[np.num] = np;
				p = &players[np.num];
			}
			p->name = name.empty() ? "Player" : name.substr(0, 40);
			p->model = model.empty() ? "sarge/default" : model.substr(0, 63);
			// a new player is an editor (Player's own start); one already known keeps their role: a viewer
			// became an editor by saying hello again
			welcome(*p);
			return;
		}
		case M_EDIT: {
			if (!p || !p->linked) {
				return;
			}
			sync::Edit e;
			if (!sync::get_edit(r, e)) {
				log("together: %s's edit didn't read\n", pid.c_str());
				return;
			}
			e.player = from;  // whoever the link is, not what it says
			host_->take(e);
			return;
		}
		case M_RESYNC: {
			// a joiner whose copy missed an edit: the session's map again, no more often than they ask
			if (!p || !p->linked || OzEdNet_Milliseconds() - p->welcomed_at < WELCOME_AGAIN_MS) {
				return;
			}
			welcome(*p, true);
			return;
		}
		case M_LEASE_ASK: {
			if (!p || !p->linked) {
				return;
			}
			Id f, en;
			if (!lease_for(*p, f, en)) {
				return;  // they've had their share (or the ids ran out): no answer, and they don't ask again
			}
			sync::Writer w;
			w.u8(M_LEASE);
			w.i32(f);
			w.i32(en);
			send_to(pid, w);
			return;
		}
		case M_CLAIM: {
			if (!p) {
				return;
			}
			const uint64_t n = r.var();
			std::vector<Id> ids;
			for (uint64_t i = 0; i < n && r.ok && i < 100000; i++) {
				ids.push_back(r.i32());
			}
			uint32_t h = 0;
			if (!host_->claim(from, ids, h)) {
				sync::Writer w;
				w.u8(M_CLAIM_NO);
				w.var(h);
				send_to(pid, w);
				return;
			}
			sync::Writer c;
			c.u8(M_CLAIMS);
			c.var(from);
			c.var(ids.size());
			for (Id id : ids) {
				c.i32(id);
			}
			to_all(c, from);
			for (Id id : ids) {
				claims_[id] = from;
			}
			return;
		}
		case M_CLAIMS: {  // a joiner's drag ended (an empty claim)
			if (!p) {
				return;
			}
			host_->release(from);
			for (auto it = claims_.begin(); it != claims_.end();) {
				it = it->second == from ? claims_.erase(it) : std::next(it);
			}
			sync::Writer c;
			c.u8(M_CLAIMS);
			c.var(from);
			c.var(0);
			to_all(c, from);
			return;
		}
		case M_SELECT: {
			if (!p) {
				return;
			}
			r.var();  // their own number: the link says who
			const uint64_t n = r.var();
			p->sel.clear();
			for (uint64_t i = 0; i < n && r.ok && i < 100000; i++) {
				p->sel.push_back(r.i32());
			}
			sync::Writer w;
			w.u8(M_SELECT);
			w.var(from);
			w.var(p->sel.size());
			for (Id id : p->sel) {
				w.i32(id);
			}
			to_all(w, from);
			others_built_ = -1;
			return;
		}
		case M_GHOST: {
			if (!p) {
				return;
			}
			r.var();  // their own number: the link says who
			std::vector<std::pair<Id, ObjectPtr>> objs;
			const uint64_t n = r.var();
			for (uint64_t i = 0; i < n && r.ok && i < GHOST_MAX; i++) {
				const Id id = r.i32();
				ObjectPtr o = sync::get_object(r);
				if (!o) {
					return;
				}
				objs.emplace_back(id, o);
			}
			if (objs.empty()) {
				ghosts_.erase(from);
			} else {
				ghosts_[from] = std::move(objs);
			}
			// passed on to the others with the player the link says
			sync::Writer g;
			g.u8(M_GHOST);
			g.var(from);
			const auto it = ghosts_.find(from);
			const size_t count = it == ghosts_.end() ? 0 : it->second.size();
			g.var(count);
			for (size_t i = 0; i < count; i++) {
				g.i32(it->second[i].first);
				sync::put_object(g, *it->second[i].second);
			}
			to_all(g, from);
			return;
		}
		case M_BYE:
			if (p) {
				OzEdNet_Leave(pid.c_str());
				on_down(pid, false, "");
			}
			return;
		default:
			return;
		}
	}

	// a joiner: from the host only
	if (pid != host_pid) {
		return;
	}
	switch (type) {
	case M_WELCOME: {
		if (state != State::Syncing && !migrating_ && !resync_) {
			return;  // not asked for (a join's hello, a takeover's, a resync): this player's map isn't replaced unasked
		}
		const uint32_t num = uint32_t(r.var());
		const Id from = r.i32(), end = r.i32();
		session_name = r.str().substr(0, 64);
		// the host's word, and it becomes this player's map's name (part of the paths their Save writes): a
		// name only, as Save as would make it
		map_name = safe_map_name(r.str());
		const uint8_t form = r.u8();
		if (!get_roster(r)) {
			ended("The session's welcome didn't read");
			return;
		}
		const sync::Bytes packed = r.bytes();
		sync::Bytes raw;
		uint32_t seq = 0;
		Id world = 0;
		std::vector<std::pair<Id, ObjectPtr>> objects;
		sync::Reader sr;
		if (!r.ok || !sync::unpack(packed, raw)) {
			ended("The session's map didn't arrive whole");
			return;
		}
		sr = sync::Reader(raw);
		if (!sync::Replica::read_snapshot(sr, seq, world, objects)) {
			ended("The session's map didn't read");
			return;
		}
		claims_.clear();
		const uint64_t nc = r.var();
		for (uint64_t i = 0; i < nc && r.ok && i < 100000; i++) {
			const Id id = r.i32();
			claims_[id] = uint32_t(r.var());
		}
		if (s.dragging()) {
			s.drop_drag("");
		}
		const bool rejoined = migrating_;
		// the map asked for again after a missed edit (ask_resync): this player stays who they were, as after a takeover
		const bool resynced = resync_ && !migrating_ && rep_ && state == State::Live;
		// taken over: the edits the old host never answered go to the new one, on its copy of the session
		// (after a resync: those the host didn't answer while this copy waited)
		std::vector<sync::Edit> again;
		if ((rejoined || resynced) && rep_) {
			for (const sync::Edit &e : rep_->unanswered()) {
				if (!answered_.count(e.local)) {
					again.push_back(e);
				}
			}
		}
		// a map of this player's own with unsaved changes, about to be replaced by the session's: written aside first
		const std::string kept = rejoined || resynced ? std::string() : keep_unsaved_map(s);
		s.select_none();
		s.doc.plain_form = form <= uint8_t(TexForm::Valve) ? TexForm(form) : TexForm::Valve;
		s.doc.reset(world, objects);
		s.doc_replaced();
		if (!rejoined && !resynced) {
			s.path.clear();  // the host's file isn't this PC's: Save asks for a name of this player's own
		}
		s.name = map_name;
		resync_ = false;
		answered_.clear();
		const Doc::Lease lease = s.doc.lease_now();
		start_sharing(num, seq);
		if (resynced) {
			s.doc.lease_put(lease);  // the id blocks this player holds stay theirs (the old copy's end let them go)
		}
		if (end > from) {
			s.doc.lease(from, end);  // 0, 0: no block came with this one (Doc::lease would take that as "no more blocks")
		}
		for (const sync::Edit &e : again) {
			rep_->resend(e);
		}
		migrating_ = false;
		host_heard_ = OzEdNet_Milliseconds();
		state = State::Live;
		if (resynced) {
			log("together: the session's map taken from the host again: %zu objects at %u (%zu edits sent again)\n", objects.size(), seq,
				again.size());
			note("The session's map was taken from the host again (an edit had gone missing)");
			changed_rev++;
			return;
		}
		if (rejoined) {
			log("together: back in the session, hosted by %s now (%zu edits sent again)\n", name_of(num_of(host_pid)).c_str(), again.size());
			note(name_of(num_of(host_pid)) + " hosts the session now");
			changed_rev++;
			return;
		}
		s.frame_all();
		const Player *h = player(1);
		host_name = h ? h->name : host_name;
		OzEdNet_Presence((std::string("edit:") + host_pid).c_str(), ("Editing " + map_name).c_str());
		changed_rev++;
		log("together: joined \"%s\" as player %u: %zu objects at %u, %zu KB\n", session_name.c_str(), num, objects.size(), seq,
			b.size() / 1024);
		note("Joined " + name_of(1) + "'s session: " + map_name);
		if (!kept.empty()) {
			note("Your unsaved map was kept as " + kept + " (Open, your maps)");
		}
		return;
	}
	case M_ROSTER:
		get_roster(r);
		return;
	case M_SEQ: {
		if (!rep_) {
			return;
		}
		sync::Edit e;
		if (!sync::get_edit(r, e)) {
			log("together: an edit from the host didn't read\n");
			ask_resync("an edit from the host didn't read");  // the ones after it would land on a map without it
			return;
		}
		if (resync_) {
			// the map is coming again and has this edit in it; an answer to one of this player's own: not to send again
			if (e.player == me) {
				answered_.insert(e.local);
			}
			return;
		}
		rep_->receive(e);
		if (rep_->lost) {
			ask_resync("an edit from the host never came");
		}
		return;
	}
	case M_LEASE: {
		const Id from = r.i32(), end = r.i32();
		if (r.ok && end > from) {
			s.doc.lease(from, end);
		}
		asked_lease_ = false;
		return;
	}
	case M_BUSY:
		// the host's game stands still for a moment (its own play-test's map loading): its silence is allowed
		// as a play-test together's is
		played_at_ = OzEdNet_Milliseconds();
		return;
	case M_CLAIMS: {
		const uint32_t who = uint32_t(r.var());
		const uint64_t n = r.var();
		for (auto it = claims_.begin(); it != claims_.end();) {
			it = it->second == who ? claims_.erase(it) : std::next(it);
		}
		for (uint64_t i = 0; i < n && r.ok && i < 100000; i++) {
			claims_[r.i32()] = who;
		}
		others_built_ = -1;
		return;
	}
	case M_CLAIM_NO: {
		const uint32_t who = uint32_t(r.var());
		claimed_.clear();
		if (s.dragging()) {
			s.drop_drag(name_of(who) + " is moving this");
		}
		return;
	}
	case M_SELECT: {
		const uint32_t who = uint32_t(r.var());
		const uint64_t n = r.var();
		auto it = players.find(who);
		if (it == players.end()) {
			return;
		}
		it->second.sel.clear();
		for (uint64_t i = 0; i < n && r.ok && i < 100000; i++) {
			it->second.sel.push_back(r.i32());
		}
		others_built_ = -1;
		return;
	}
	case M_GHOST: {
		const uint32_t who = uint32_t(r.var());
		std::vector<std::pair<Id, ObjectPtr>> objs;
		const uint64_t n = r.var();
		for (uint64_t i = 0; i < n && r.ok && i < GHOST_MAX; i++) {
			const Id id = r.i32();
			ObjectPtr o = sync::get_object(r);
			if (!o) {
				return;
			}
			objs.emplace_back(id, o);
		}
		if (objs.empty() || who == me) {
			ghosts_.erase(who);
		} else {
			ghosts_[who] = std::move(objs);
		}
		return;
	}
	case M_BYE:
	case M_KICK: {
		const std::string reason = r.str();
		OzEdNet_Leave(host_pid.c_str());
		ended(reason.empty() ? "The host ended the session" : reason);
		return;
	}
	default:
		return;
	}
}

// presence: 'P', the player, then the camera, the pointer, a move's offset, a brush being drawn
void Together::on_presence(const std::string &pid, const sync::Bytes &b) {
	sync::Reader r(b);
	if (r.u8() != 'P') {
		return;
	}
	uint32_t who = uint32_t(r.var());
	if (hosting) {
		who = num_of(pid);  // whoever the link is
		if (!who) {
			return;
		}
		// passed on to the others, with the player as the link says
		sync::Writer w;
		w.u8('P');
		w.var(who);
		w.out.append(b, size_t(r.p - reinterpret_cast<const uint8_t *>(b.data())), std::string::npos);
		to_all(w, who, true);
	}
	auto it = players.find(who);
	if (it == players.end() || who == me) {
		return;
	}
	Presence &p = it->second.pres;
	auto f = [&] { return double(float(r.f64())); };
	p.eye.x = f(), p.eye.y = f(), p.eye.z = f();
	p.yaw = float(r.f64());
	p.pitch = float(r.f64());
	const uint8_t flags = r.u8();
	p.pointer = flags & 1;
	p.moving = flags & 2;
	p.box = flags & 4;
	p.testing = flags & 8;
	if (p.pointer) {
		p.at.x = f(), p.at.y = f(), p.at.z = f();
	}
	if (p.moving) {
		p.delta.x = f(), p.delta.y = f(), p.delta.z = f();
	}
	if (p.box) {
		p.b.lo.x = f(), p.b.lo.y = f(), p.b.lo.z = f();
		p.b.hi.x = f(), p.b.hi.y = f(), p.b.hi.z = f();
	}
	if (r.ok) {
		p.heard = OzEdNet_Milliseconds();
	}
}

void Together::send_presence() {
	Session &s = together_session();
	const int now = OzEdNet_Milliseconds();
	if (now - presence_at_ < PRESENCE_MS) {
		return;
	}
	sync::Writer w;
	w.u8('P');
	w.var(me);
	Camera c = s.cam;
	w.f64(c.origin.x), w.f64(c.origin.y), w.f64(c.origin.z);
	w.f64(c.yaw), w.f64(c.pitch);
	Vec3 at, delta;
	Bounds box;
	const bool pointer = s.pointer_at(at), moving = s.move_ghost(delta), drawing = s.drawing_box(box);
	const bool testing = OzEdNet_ClientState() != 0;
	w.u8(uint8_t((pointer ? 1 : 0) | (moving ? 2 : 0) | (drawing ? 4 : 0) | (testing ? 8 : 0)));
	if (pointer) {
		w.f64(at.x), w.f64(at.y), w.f64(at.z);
	}
	if (moving) {
		w.f64(delta.x), w.f64(delta.y), w.f64(delta.z);
	}
	if (drawing) {
		w.f64(box.lo.x), w.f64(box.lo.y), w.f64(box.lo.z);
		w.f64(box.hi.x), w.f64(box.hi.y), w.f64(box.hi.z);
	}
	if (w.out == presence_last_ && now - presence_beat_ < PRESENCE_BEAT_MS) {
		return;
	}
	presence_last_ = w.out;
	presence_at_ = now;
	presence_beat_ = now;
	if (hosting) {
		to_all(w, 0, true);
	} else {
		to_host(w, true);
	}
}

void Together::send_selection() {
	Session &s = together_session();
	const int now = OzEdNet_Milliseconds();
	if (s.sel_rev != sel_rev_) {
		sel_rev_ = s.sel_rev;
		sel_due_ = true;
	}
	if (!sel_due_ || now - sel_at_ < SELECT_MS) {
		return;
	}
	sel_due_ = false;
	sel_at_ = now;
	std::vector<Id> ids = s.selected();
	if (ids.size() > 4096) {
		ids.resize(4096);  // the outline shows the rest's place well enough
	}
	if (Player *p = players.count(me) ? &players[me] : nullptr) {
		p->sel = ids;
	}
	sync::Writer w;
	w.u8(M_SELECT);
	w.var(me);
	w.var(ids.size());
	for (Id id : ids) {
		w.i32(id);
	}
	if (hosting) {
		to_all(w);
	} else {
		to_host(w);
	}
}

// A drag that reshapes objects as it goes (a face pushed, corners dragged, a jump pad's landing or arc) changes
// them in this player's document only until it's let go: the others see them as they are, ten times a second
// while they change, and nothing once it ends (the edit itself then comes, outlined).
void Together::ghost_tick() {
	Session &s = together_session();
	const int now = OzEdNet_Milliseconds();
	Vec3 delta;
	const bool reshaping = s.dragging() && !s.move_ghost(delta) && !s.held_ids().empty();
	if (!reshaping) {
		if (ghost_on_) {
			ghost_on_ = false;
			ghost_rev_ = ~0ull;
			sync::Writer w;
			w.u8(M_GHOST);
			w.var(me);
			w.var(0);
			if (hosting) {
				to_all(w);
			} else {
				to_host(w);
			}
		}
		return;
	}
	if (s.doc.revision == ghost_rev_ || now - ghost_at_ < GHOST_MS) {
		return;
	}
	ghost_rev_ = s.doc.revision;
	ghost_at_ = now;
	ghost_on_ = true;
	std::vector<Id> ids = s.held_ids();
	if (ids.size() > GHOST_MAX) {
		ids.resize(GHOST_MAX);
	}
	sync::Writer w;
	w.u8(M_GHOST);
	w.var(me);
	size_t count = 0;
	for (Id id : ids) {
		count += s.doc.get(id) != nullptr;
	}
	w.var(count);
	for (Id id : ids) {
		if (const Object *o = s.doc.get(id)) {
			w.i32(id);
			sync::put_object(w, *o);
		}
	}
	if (hosting) {
		to_all(w);
	} else {
		to_host(w);
	}
}

void Together::claims_tick() {
	Session &s = together_session();
	if (claimed_.empty() || s.dragging()) {
		return;
	}
	// the drag ended: let go of what it held
	claimed_.clear();
	sync::Writer c;
	c.u8(M_CLAIMS);
	c.var(me);
	c.var(0);
	if (hosting) {
		host_->release(me);
		to_all(c);
	} else {
		to_host(c);
	}
}

// ---------------------------------------------------------------- playing together

namespace {

std::string maps_dir() {
	return std::string(OzEd_HomePath()) + "/oz/maps";
}

// a play-test map's name as it travels: ed_<a-z 0-9 _>_r<digits>, with .bsp or .aas
bool safe_pak_file(const std::string &f) {
	if (f.size() < 10 || f.compare(0, 3, "ed_") != 0) {
		return false;
	}
	const bool defi = f.size() > 5 && f.compare(f.size() - 5, 5, ".defi") == 0;
	const std::string ext = defi ? ".defi" : f.substr(f.size() - 4);
	if (ext != ".bsp" && ext != ".aas" && ext != ".defi") {
		return false;
	}
	const std::string stem = f.substr(0, f.size() - ext.size());
	const size_t r = stem.rfind("_r");
	if (r == std::string::npos || r + 2 >= stem.size()) {
		return false;
	}
	for (size_t i = r + 2; i < stem.size(); i++) {
		if (!std::isdigit((unsigned char)stem[i])) {
			return false;
		}
	}
	for (char c : stem) {
		if (!(std::islower((unsigned char)c) || std::isdigit((unsigned char)c) || c == '_')) {
			return false;
		}
	}
	return true;
}

bool read_file(const std::string &path, std::string &out) {
	char *data = nullptr;
	const int len = OzEd_ReadOSFile(path.c_str(), &data);
	if (len < 0) {
		return false;
	}
	out.assign(data, size_t(len));
	OzEd_Free(data);
	return true;
}

// where a play-test's file goes: the player's own maps folder; under Defrag its folder (maps, and scripts for
// the map's info), where the switch to Defrag finds them
std::string play_file_path(const std::string &name, bool defrag) {
	if (!defrag) {
		return maps_dir() + "/" + name;
	}
	const bool defi = name.size() > 5 && name.compare(name.size() - 5, 5, ".defi") == 0;
	return std::string(OzEd_HomePath()) + "/defrag/" + (defi ? "scripts/" : "maps/") + name;
}

// what a peer's play-test message may name (M_PLAY, M_RELOAD), since it ends up in console commands
bool hex_word(const std::string &t) {
	if (t.empty() || t.size() > 64) {
		return false;
	}
	for (char c : t) {
		if (!std::isxdigit((unsigned char)c)) {
			return false;
		}
	}
	return true;
}

bool map_word(const std::string &t) {
	if (t.empty() || t.size() > 64) {
		return false;
	}
	for (char c : t) {
		if (!std::isalnum((unsigned char)c) && c != '_' && c != '-') {
			return false;
		}
	}
	return true;
}

std::string random_password() {
	char buf[16];
	std::snprintf(buf, sizeof buf, "%08x", unsigned(OzEdNet_Milliseconds()) * 2654435761u ^ 0x5bd1e995u);
	return buf;
}

}  // namespace

// A play-test's file onto this PC, and remembered. Every Play together and Reload left its
// ed_<name>_r<revision> files in each member's maps folder for good, each one a map in Skirmish's list: the
// revisions before the last two go as a new one comes (the one before may still be the map a tester is
// loading), and the rest when the play-test or the session ends (drop_play_files).
void Together::play_file(const std::string &name, const sync::Bytes &data, bool defrag) {
	const std::string path = play_file_path(name, defrag);
	const std::string stem = name.substr(0, name.rfind('.'));
	std::string before;  // the newest revision already here
	for (const auto &f : play_files_) {
		if (f.second != stem) {
			before = f.second;
		}
	}
	for (auto it = play_files_.begin(); it != play_files_.end();) {
		if (it->second != stem && it->second != before) {
			OzEd_RemoveOSFile(it->first.c_str());
			it = play_files_.erase(it);
		} else {
			++it;
		}
	}
	OzEd_MakeDirs(path.c_str());
	OzEd_WriteOSFile(path.c_str(), data.data(), int(data.size()));
	for (const auto &f : play_files_) {
		if (f.first == path) {
			return;
		}
	}
	play_files_.emplace_back(path, stem);
}

void Together::drop_play_files() {
	for (const auto &f : play_files_) {
		OzEd_RemoveOSFile(f.first.c_str());
	}
	play_files_.clear();
}

void Together::busy() {
	if (!live() || !hosting) {
		return;
	}
	sync::Writer w;
	w.u8(M_BUSY);
	to_all(w);
}

void Together::ask_resync(const char *why) {
	if (hosting || migrating_ || !rep_ || state != State::Live) {
		return;
	}
	const int now = OzEdNet_Milliseconds();
	if (!resync_) {
		resync_ = true;
		answered_.clear();
		log("together: %s: asking the host for the session's map again\n", why);
	} else if (now - resync_at_ < RESYNC_ASK_MS) {
		return;
	}
	resync_at_ = now;
	sync::Writer w;
	w.u8(M_RESYNC);
	to_host(w);
}

bool Together::play_together(std::string &why, bool defrag) {
	Session &s = together_session();
	if (defrag && !s.defrag_ready(why)) {
		return false;  // Oz never installs it (decision 24): the player brings their own
	}
	if (!live()) {
		why = "Play together is a session's: host or join one";
		return false;
	}
	if (my_role() == Role::Viewer) {
		why = "You're watching this session: the host can make you an editor";  // the host refuses a viewer's play-test anyway
		return false;
	}
	if (play.stage != 0) {
		why = play.stage == 1 ? "The map is compiling for the play-test" : "A play-test together is under way";
		return false;
	}
	if (s.compiling()) {
		why = "A compile is under way: wait for it, or stop it";
		return false;
	}
	if (OzEdNet_ClientState() != 0) {
		why = "Leave the match first";
		return false;
	}
	if (!defrag && !s.mode_problem().empty()) {
		why = s.mode_problem();  // Capture the flag without its flags
		return false;
	}
	play = Play{};
	play.stage = 1;
	play.starter = me;
	play.rev = seq();
	play.defrag = defrag;
	s.compiled = [this](bool ok, const std::string &dir, const std::string &out) { play_compiled(ok, dir, out, false); };
	s.next_without_bots = true;  // no bots in a play-test together: their file only slows it (and a rough map can fail it)
	if (!s.compile(false, false, false)) {
		s.compiled = nullptr;
		play.stage = 0;
		why = "The map didn't start compiling";
		return false;
	}
	note(defrag ? "Compiling the map on your PC to play it together under Defrag" : "Compiling the map on your PC to play it together");
	return true;
}

// the starter's compile is done: the map under its revision's name, sent to everyone, then the match
void Together::play_compiled(bool ok, const std::string &dir, const std::string &out, bool reloading) {
	Session &s = together_session();
	if (!ok) {
		if (!reloading) {
			play.stage = 0;
		}
		note("Play together: the compile failed: " + out);
		return;
	}
	if (play.starter != me || play.stage != (reloading ? 3 : 1)) {
		// while it compiled, the play-test it was for ended, or another player's began and this one joined it
		log("together: the compile's play-test is no longer this player's to start: nothing sent\n");
		return;
	}
	char rev[32];
	std::snprintf(rev, sizeof rev, "_r%u", seq());
	const std::string map = out + rev;
	sync::Bytes bsp, aas;
	if (!read_file(dir + "/maps/" + out + ".bsp", bsp)) {
		play.stage = reloading ? play.stage : 0;
		note("Play together: the compiled map isn't there");
		return;
	}
	const bool defrag = play.defrag;
	const bool bots = !defrag && read_file(dir + "/maps/" + out + ".aas", aas);  // Defrag runs without bots
	const sync::Bytes defi = defrag ? s.defrag_defi(map) : sync::Bytes();
	auto keep = [&](const std::string &name, const sync::Bytes &data) { play_file(name, data, defrag); };
	keep(map + ".bsp", bsp);
	if (bots) {
		keep(map + ".aas", aas);
	}
	if (defrag) {
		keep(map + ".defi", defi);
	}
	// the map to everyone, then (once the match is up) where to join it
	auto send_file = [&](const std::string &name, const sync::Bytes &data) {
		sync::Writer w;
		w.u8(M_PAK);
		w.str(name);
		w.bytes(sync::pack(data));
		w.u8(defrag ? 1 : 0);
		if (hosting) {
			to_all(w);
		} else {
			to_host(w);
		}
	};
	send_file(map + ".bsp", bsp);
	if (bots) {
		send_file(map + ".aas", aas);
	}
	if (defrag) {
		send_file(map + ".defi", defi);
	}
	play.map = map;
	play.rev = seq();
	if (reloading) {
		// everyone saves where they stand, the match changes map, everyone is put back there
		sync::Writer w;
		w.u8(M_RELOAD);
		w.var(me);
		w.str(map);
		w.var(play.rev);
		if (hosting) {
			to_all(w);
		} else {
			to_host(w);
		}
		// where this player stood is what put_back kept while the old map ran
		play.reloading = true;
		OzEdNet_Command((s.mode_before_map() + "devmap " + map + "\n").c_str());  // the session map's mode as it is now
		note("Reloading the play-test with the map as it is now (r" + std::to_string(play.rev) + ")");
		return;
	}
	play.password = random_password();
	play.host_pid = OzEdNet_Me();
	play.stage = 2;
	// a listen server in the map's game mode: the files are loose in the players' maps folders, so it isn't pure;
	// cheats for edpos. A team mode's players go on a team as they come in (team_tick).
	if (defrag) {
		// Defrag's game on this PC (oz_playmod: back to Oz when the match ends), then the map; the lines after
		// the switch wait for it
		// (Defrag's game asks its host's own player for the password too: Quake III's lets the local player by).
		// A listen server under Defrag takes remote players only in a multiplayer game type, set by hand
		// (df_gametype 5, multiplayer defrag: its auto-config picks one only on a dedicated server)
		OzEdNet_Command(("oz_playmod defrag\nset sv_pure 0\nset g_password " + play.password + "\nset password " + play.password +
			"\nset sv_hostname \"" + name_of(me) + "'s Defrag play-test\"\nset sv_maxclients " + std::to_string(sync::MAX_PLAYERS) +
			"\nset df_gametype 5\nmap " + map + "\n").c_str());
		return;
	}
	OzEdNet_Command(("set sv_pure 0\nset g_password " + play.password + "\nset sv_hostname \"" + name_of(me) + "'s play-test\"\n" +
		s.mode_before_map() + "devmap " + map + "\n").c_str());
}

// In a play-test together of a team mode: a player still a spectator (just in, or after a Reload that changed
// the mode) goes on the team with fewer players, as Custom Match puts them
void Together::team_tick(int cs) {
	const int now = OzEdNet_Milliseconds();
	if (cs != 2 || play.reloading || OzEdNet_GameType() < 3 || !OzEdNet_Spectating()) {
		play.team_since = 0;
		return;
	}
	if (!play.team_since) {
		play.team_since = now;  // a moment in the match first: its first snapshots
		return;
	}
	if (now - play.team_since > 800 && now - play.team_asked > 3000) {
		play.team_asked = now;
		OzEdNet_Command("team auto\n");
		log("together: onto a team (%s)\n", OzEdNet_GameType() == 4 ? "capture the flag" : "team deathmatch");
	}
}

void Together::join_play() {
	if (OzEdNet_ClientState() != 0) {
		note(name_of(play.starter) + " started playing together: leave your match to join them");
		play.stage = 0;
		return;
	}
	std::string why;
	if (play.defrag && !together_session().defrag_ready(why)) {
		// Oz never installs Defrag (decision 24): this player keeps editing
		note(name_of(play.starter) + " is playing the map under Defrag: install Defrag (Mod Library) to join. You stay in the session, editing.");
		log("together: not joining the Defrag play-test: %s\n", why.c_str());
		play.stage = 0;
		return;
	}
	play.stage = 4;
	play.was_active = false;
	// linked to the match's host already (they host the session, or we do and they're our joiner): over that link;
	// else the online service links us, as any join. Under Defrag: Defrag's game first (oz_playmod: back to Oz
	// after), the password set after the switch (a switch empties it)
	const bool linked = (!hosting && play.host_pid == host_pid) || (hosting && num_of(play.host_pid));
	const std::string join = linked ? "connect ice:" + play.host_pid + "\n" : "oz_join " + play.host_pid + " " + play.password + "\n";
	if (play.defrag) {
		OzEdNet_Command(("oz_playmod defrag\nset password " + play.password + "\n" + join).c_str());
	} else {
		OzEdNet_SetCvar("password", play.password.c_str());
		OzEdNet_Command(join.c_str());
	}
	note("Joining " + name_of(play.starter) + (play.defrag ? "'s Defrag play-test" : "'s play-test"));
}

void Together::on_play(const std::string &from, uint8_t type, sync::Reader &r, const sync::Bytes &b) {
	// The host passes a joiner's on to the others, but only once it has read it and found it that player's to
	// send, and with the player the link says in it: it used to pass the bytes on unread, whatever they were.
	// sender: on the host, the joiner it came from. running: on the host, whose play-test is on (0: none, or
	// not known after taking the session over).
	const uint32_t sender = hosting ? num_of(from) : 0;
	const uint32_t running = !hosting ? 0 : (play.starter == me && (play.stage == 2 || play.stage == 3)) ? me : play_starter_;
	switch (type) {
	case M_PAK: {
		const std::string name = r.str();
		const sync::Bytes packed = r.bytes();
		sync::Bytes raw;
		const bool defrag = r.left() ? r.u8() != 0 : false;
		if (!r.ok || !safe_pak_file(name) || !sync::unpack(packed, raw)) {
			log("together: a play-test file didn't arrive whole (%s)\n", name.c_str());
			return;
		}
		if (hosting) {
			if (running && running != sender) {
				// a play-test's files come from the player who started it, not from any member while it's on
				log("together: a play-test file from %s refused: %s's play-test is on\n", name_of(sender).c_str(), name_of(running).c_str());
				return;
			}
			sync::Writer w;
			w.out = b;
			to_all(w, sender);
		}
		play_file(name, raw, defrag);
		log("together: play-test file %s, %zu KB\n", name.c_str(), raw.size() / 1024);
		if (name.size() > 4 && name.compare(name.size() - 4, 4, ".bsp") == 0) {
			// the faces' glow copies the map draws with, made here from the session's map (this PC's own
			// materials): the game reads them from oz/scripts, Defrag's from its own folder
			const Doc &d = together_session().doc;
			for (Id id : d.ids_of(Kind::Brush)) {
				for (const Face &f : d.get(id)->brush.faces) {
					int brightness, light;
					if (face_glow(f, brightness, light)) {
						shaders_lib().glow_variant(shader_name(f.shader), brightness, light);
					}
				}
			}
			if (defrag) {
				shaders_lib().copy_glow_scripts(std::string(OzEd_HomePath()) + "/defrag/scripts");
			}
		}
		return;
	}
	case M_PLAY: {
		const uint32_t starter = uint32_t(r.var());
		Play p;
		p.starter = starter;
		p.host_pid = r.str();
		p.map = r.str();
		p.password = r.str();
		p.rev = uint32_t(r.var());
		p.defrag = r.left() ? r.u8() != 0 : false;
		if (!r.ok || (!hosting && starter == me)) {
			return;
		}
		// These go into console commands (join_play: connect, oz_join, set password), and the console splits a
		// line on ; and newlines: a member's "x; unbindall" as the password ran on every other member's game.
		// A player id and a play-test's password are hex digits, a map's name letters, digits and _
		if (!hex_word(p.host_pid) || !hex_word(p.password) || !map_word(p.map)) {
			log("together: a play-test's message wasn't one (refused)\n");
			return;
		}
		if (hosting) {
			// the player who starts a play-test hosts its match on their own PC: the link's player, at the link's id
			if (p.host_pid != from) {
				log("together: a play-test's message wasn't one (refused)\n");
				return;
			}
			if (running && running != sender) {
				log("together: %s's play-test refused: %s's is on\n", name_of(sender).c_str(), name_of(running).c_str());
				return;
			}
			p.starter = sender;
			play_starter_ = sender;
			sync::Writer w;
			w.u8(M_PLAY);
			w.var(sender);
			w.str(p.host_pid);
			w.str(p.map);
			w.str(p.password);
			w.var(p.rev);
			w.u8(p.defrag ? 1 : 0);
			to_all(w, sender);
		}
		play = p;
		join_play();
		return;
	}
	case M_PLAYEND: {
		uint32_t starter = uint32_t(r.var());
		if (hosting) {
			if (running && running != sender) {
				return;  // only the player whose play-test it is ends it
			}
			starter = sender;
			if (play_starter_ == sender) {
				play_starter_ = 0;
			}
			sync::Writer w;
			w.u8(M_PLAYEND);
			w.var(sender);
			to_all(w, sender);
		}
		if (play.stage == 4 && play.starter == starter) {
			play.stage = 0;
			OzEdNet_Command("disconnect\neditor_together\n");
			OzEdNet_ClearError();
			note(name_of(starter) + " ended the play-test: back to the map");
		}
		if (play.stage == 0) {
			drop_play_files();  // the play-test is over: its maps go (a game still in it has them loaded)
		}
		return;
	}
	case M_RELOAD_ASK: {
		if (hosting && running != me) {
			// for the player whose play-test it is; not known (the session was taken over): everyone, as before
			sync::Writer w;
			w.u8(M_RELOAD_ASK);
			const Player *to = running ? player(running) : nullptr;
			if (to) {
				send_to(to->pid, w);
			} else {
				to_all(w, sender);
			}
			return;
		}
		if (play.stage == 3 && play.starter == me) {
			reload();
		}
		return;
	}
	case M_RELOAD: {
		uint32_t starter = uint32_t(r.var());
		const std::string map = r.str();
		const uint32_t rev = uint32_t(r.var());
		if (!r.ok || !map_word(map)) {
			return;
		}
		if (hosting) {
			if (running && running != sender) {
				return;  // only the player whose play-test it is loads another map in it
			}
			starter = sender;
			sync::Writer w;
			w.u8(M_RELOAD);
			w.var(sender);
			w.str(map);
			w.var(rev);
			to_all(w, sender);
		}
		if (starter == me || play.stage != 4 || play.starter != starter) {
			return;
		}
		// where this player stood is what put_back kept while the old map ran
		play.map = map;
		play.rev = rev;
		play.reloading = true;
		note(name_of(starter) + " reloaded the play-test with the map as it is now (r" + std::to_string(rev) + ")");
		return;
	}
	default:
		return;
	}
}

void Together::back_to_editor() {
	if (play.stage == 3 && play.starter == me && !play.ending_at) {
		sync::Writer w;
		w.u8(M_PLAYEND);
		w.var(me);
		if (hosting) {
			to_all(w);
		} else {
			to_host(w);
		}
		play.told_end = true;
		play.ending_at = OzEdNet_Milliseconds();
		return;
	}
	if (play.stage == 4) {
		play.stage = 0;  // a tester leaving alone: the match goes on for the others
	}
	OzEdNet_Command("disconnect\neditor_together\n");
}

bool Together::hold_disconnect() {
	if (!live() || play.stage != 3 || play.starter != me || play.told_end || play.reloading || !OzEdNet_ServerRunning()) {
		return false;
	}
	back_to_editor();
	log("together: leaving the play-test: the testers are told first, the match ends in a moment\n");
	return play.told_end;
}

void Together::reload() {
	if (play.stage == 3 && play.starter == me) {
		Session &s = together_session();
		if (s.compiling()) {
			return;
		}
		s.compiled = [this](bool ok, const std::string &dir, const std::string &out) { play_compiled(ok, dir, out, true); };
		s.next_without_bots = true;
		if (!s.compile(false, false, false)) {
			s.compiled = nullptr;
		}
		return;
	}
	if (play.stage == 4) {
		if (my_role() == Role::Viewer) {
			note("You're watching this session: an editor reloads the play-test");
			return;
		}
		sync::Writer w;
		w.u8(M_RELOAD_ASK);
		if (hosting) {
			const Player *p = player(play.starter);
			if (p) {
				send_to(p->pid, w);
			}
		} else {
			to_host(w);  // the host passes it on to the match's host
		}
	}
}

bool Together::reload_offered() const {
	return (play.stage == 3 || play.stage == 4) && !play.reloading && !play.defrag && seq() > play.rev;
}

// In a play-test together: where this player stands is kept while the match runs on its map; after a Reload's
// map change (once the player is in it a moment: edpos respawns, the first snapshots first), they're put back there.
void Together::put_back(int cs) {
	const std::string map = OzEdNet_MapName();
	if (cs != 2) {
		play.arrived_at = 0;
		return;
	}
	if (!play.reloading) {
		if (map == play.map) {
			float a[3];
			if (OzEdNet_PlayerPos(play.pos, a)) {
				std::memcpy(play.angles, a, sizeof a);
				play.saved = true;
			}
		}
		return;
	}
	if (map != play.map) {
		return;  // the new map not loaded yet
	}
	const int now = OzEdNet_Milliseconds();
	if (!play.arrived_at) {
		play.arrived_at = now;
		return;
	}
	if (now - play.arrived_at < 600) {
		return;
	}
	play.reloading = false;
	play.arrived_at = 0;
	if (play.saved) {
		char buf[160];
		std::snprintf(buf, sizeof buf, "edpos %.0f %.0f %.0f %.1f %.1f\n", play.pos[0], play.pos[1], play.pos[2], play.angles[0], play.angles[1]);
		OzEdNet_Command(buf);
		log("together: back where you stood: %.0f %.0f %.0f\n", play.pos[0], play.pos[1], play.pos[2]);
	}
}

void Together::play_tick() {
	const int cs = OzEdNet_ClientState();
	if (play.stage == 3 || play.stage == 4) {
		team_tick(cs);
	}
	switch (play.stage) {
	case 2:  // the starter's match loading: once in it, the others are told where it is
		if (cs == 2 && OzEdNet_ServerRunning() && play.map == OzEdNet_MapName()) {
			if (!hosting && !play.listed) {
				OzEdNet_Command("oz_host\n");  // listed (with its password) so the others are linked to it
				play.listed = true;
				play.listed_at = OzEdNet_Milliseconds();
			}
			if (play.listed && OzEdNet_Milliseconds() - play.listed_at < 1500) {
				break;  // the listing on the hub before anyone asks for it
			}
			sync::Writer w;
			w.u8(M_PLAY);
			w.var(me);
			w.str(play.host_pid);
			w.str(play.map);
			w.str(play.password);
			w.var(play.rev);
			w.u8(play.defrag ? 1 : 0);
			if (hosting) {
				to_all(w);
			} else {
				to_host(w);
			}
			play.stage = 3;
			log("together: playing %s together, hosted here\n", play.map.c_str());
		}
		break;
	case 3:  // hosting the match: when it ends, everyone back to the map
		if (play.ending_at && OzEdNet_Milliseconds() - play.ending_at > 800) {
			play.ending_at = 0;
			OzEdNet_Command("disconnect\neditor_together\n");
		}
		put_back(cs);
		if (!OzEdNet_ServerRunning() && !play.reloading && !play.ending_at) {
			if (!play.told_end) {
				sync::Writer w;
				w.u8(M_PLAYEND);
				w.var(me);
				if (hosting) {
					to_all(w);
				} else {
					to_host(w);
				}
				// left some other way than Back to the map editor (Defrag's own menu has no such item): back
				// to the session's editor all the same, once any switch back to Oz is done
				OzEdNet_ClearError();
				OzEdNet_Command("editor_together\n");
			}
			if (play.listed) {
				OzEdNet_Command("oz_unhost\n");
			}
			play.stage = 0;
			drop_play_files();  // its match is down: the maps it ran on go
			log("together: the play-test together ended\n");
		}
		break;
	case 4:  // a tester: back to the map when the match ends for them
		if (cs == 2) {
			play.was_active = true;
			put_back(cs);
		} else if (cs == 0 && play.was_active && !play.reloading) {
			play.stage = 0;
			OzEdNet_ClearError();
			OzEdNet_Command("editor_together\n");
		}
		break;
	default:
		break;
	}
}

// ---------------------------------------------------------------- each frame

void Together::frame() {
	// the channel's events, whatever the state (a link's last words after the session ended are dropped)
	for (int n = 0; n < 4096; n++) {
		char pid[32];
		void *data = nullptr;
		int len = 0, fast = 0;
		const int ev = OzEdNet_Poll(pid, sizeof pid, &data, &len, &fast);
		if (ev == 0) {
			break;
		}
		if (active()) {
			if (!hosting && host_pid == pid && (ev == 1 || ev == 2)) {
				host_heard_ = OzEdNet_Milliseconds();
			}
			if (ev == 1 && data) {
				const sync::Bytes b(static_cast<const char *>(data), size_t(len));
				if (fast) {
					on_presence(pid, b);
				} else {
					on_message(pid, b);
				}
			} else if (ev == 2) {
				on_up(pid);
			} else if (ev == 3) {
				on_down(pid, false, "");
			} else if (ev == 4) {
				on_down(pid, true, data ? static_cast<const char *>(data) : "");
			}
		}
		OzEdNet_Free(data);
	}
	if (state == State::Joining && OzEdNet_Milliseconds() - joined_at_ > 45000) {
		OzEdNet_Leave(host_pid.c_str());
		ended("The host didn't answer");
	}
	if (!live()) {
		return;
	}
	Session &s = together_session();
	migrate_tick();
	// silence is counted only while this game runs: a stall of its own (a map load, a mod switch) left the host's
	// messages waiting, not unsent. A play-test's loads and switches (Defrag's: two, with the renderer and the
	// menus started again) stall the host too, so the host gets longer then
	const int now = OzEdNet_Milliseconds();
	if (last_frame_ && now - last_frame_ > 1000 && host_heard_) {
		host_heard_ += now - last_frame_;
	}
	last_frame_ = now;
	if (play.stage) {
		played_at_ = now;
	}
	// the host in a play-test of its own (F5, not Play together) stalls the same way: its presence said it was
	// in a match (testing) when last heard, or it said its map was about to load (M_BUSY sets played_at_ here).
	// It was taken for gone after 8 s and the session taken over under it
	const Player *hp = hosting ? nullptr : player(num_of(host_pid));
	const bool host_testing = hp && hp->pres.heard && hp->pres.testing;
	const int silent = (played_at_ && now - played_at_ < PLAY_AFTER_MS) || host_testing ? PLAY_SILENT_MS : HOST_SILENT_MS;
	if (!hosting && !migrating_ && host_heard_ && now - host_heard_ > silent) {
		migrate("The host stopped answering");
	}
	if (!live()) {
		return;
	}
	if (resync_) {
		ask_resync("");  // again, until the map comes (the host answers one a couple of seconds at most)
	}
	rep_->tick();
	if (s.compiling()) {
		s.compile_poll();  // the editor's page polls it only while it draws: a Reload's compile runs during the match
	}
	send_presence();
	send_selection();
	claims_tick();
	ghost_tick();
	play_tick();
	// a new block of ids well before this one runs out
	if (!s.doc.lease_spare() && s.doc.lease_left() < sync::BLOCK / 2) {
		if (hosting) {
			Id f = 0, e = 0;
			host_->grant(f, e);
			if (e > f) {
				s.doc.lease(f, e);  // none left: Doc::lease would take 0, 0 as "no more blocks" and forget the one in use
			}
		} else if (!asked_lease_) {
			asked_lease_ = true;
			sync::Writer w;
			w.u8(M_LEASE_ASK);
			to_host(w);
		}
	}
	// following someone: this camera is theirs
	if (follow) {
		const Player *p = player(follow);
		if (!p || follow == me) {
			follow = 0;
		} else if (p->pres.heard) {
			s.cam.origin = p->pres.eye;
			s.cam.yaw = p->pres.yaw;
			s.cam.pitch = p->pres.pitch;
		}
	}
	if (map_name != s.name && hosting) {
		map_name = s.name;  // a Save as renamed it
		relist();
		OzEdNet_Presence((std::string("edit:") + host_pid + (password_.empty() ? "" : " locked")).c_str(), ("Editing " + map_name).c_str());
	}
}

// ---------------------------------------------------------------- drawing the others

// An object's edges as the editor's 2D views draw them: a brush's faces, a curve's net of control points, a
// point entity's box (a brush entity's brushes are objects of their own). in_doc: id is the document's object
// (its faces' polygons cached there), else they're worked out from its planes.
void Together::draw_shape(Overlay &out, const Object &o, Id id, bool in_doc, const Vec3 &off, const unsigned char c[4]) {
	if (o.kind == Kind::Brush) {
		std::vector<q3map::Winding> made;
		if (!in_doc) {
			std::vector<Plane> planes;
			for (const Face &f : o.brush.faces) {
				planes.push_back(f.plane);
			}
			made = q3map::brush_windings(planes);
		}
		const std::vector<q3map::Winding> &ws = in_doc ? together_session().doc.windings(id) : made;
		for (q3map::Winding w : ws) {
			for (Vec3 &v : w) {
				v = v + off;
			}
			out.outline(w, c);
		}
	} else if (o.kind == Kind::Patch) {
		const Patch &pt = o.patch;
		auto at = [&](int col, int row) {
			const size_t i = size_t(row * pt.width + col) * 5;
			return i + 2 < pt.ctrl.size() ? Vec3(pt.ctrl[i], pt.ctrl[i + 1], pt.ctrl[i + 2]) + off : off;
		};
		for (int row = 0; row < pt.height; row++) {
			for (int col = 0; col < pt.width; col++) {
				if (col + 1 < pt.width) {
					out.line(at(col, row), at(col + 1, row), c);
				}
				if (row + 1 < pt.height) {
					out.line(at(col, row), at(col, row + 1), c);
				}
			}
		}
	} else {
		Vec3 lo, hi;
		if (point_box(o, lo, hi)) {
			out.box_edges(lo + off, hi + off, c);
		}
	}
}

void Together::draw(bool is3d) {
	if (!live()) {
		return;
	}
	Session &s = together_session();
	const int now = OzEdNet_Milliseconds();
	// the lines once a frame (each pane draws the same)
	if (others_built_ != now) {
		others_built_ = now;
		others_.begin();
		eyes_.begin();
		for (const auto &kv : players) {
			const Player &p = kv.second;
			if (kv.first == me || !p.pres.heard) {
				continue;
			}
			unsigned char c[4];
			std::memcpy(c, color_of(p.color), 4);
			if (now - p.pres.heard > PRESENCE_GONE_MS || p.pres.testing) {
				c[3] = 90;  // gone quiet, or play-testing: faded
			}
			// where they look: a short line from the eye
			Camera cam;
			cam.yaw = p.pres.yaw, cam.pitch = p.pres.pitch;
			Vec3 f, l, u;
			cam.axes(f, l, u);
			const Vec3 eye = p.pres.eye;
			others_.line(eye, eye + f * 48, c);
			// a small frame round the eye: the 2D views' mark (the 3D view has the fighter)
			const double r = 10;
			eyes_.box_edges(eye - Vec3(r, r, r), eye + Vec3(r, r, r), c);
			if (p.pres.pointer) {
				const Vec3 a = p.pres.at;
				const double k = 8;
				others_.line(a - Vec3(k, 0, 0), a + Vec3(k, 0, 0), c);
				others_.line(a - Vec3(0, k, 0), a + Vec3(0, k, 0), c);
				others_.line(a - Vec3(0, 0, k), a + Vec3(0, 0, k), c);
			}
			// their selection: a box round each, faint while they aren't dragging it
			unsigned char faint[4] = {c[0], c[1], c[2], uint8_t(c[3] / 3)};
			const bool moving = p.pres.moving;
			size_t shown = 0;
			Bounds all;
			for (Id id : p.sel) {
				if (!s.doc.get(id)) {
					continue;
				}
				const Bounds b = bounds_of(s.doc, id);
				if (b.empty()) {
					continue;
				}
				all.add(b.lo), all.add(b.hi);
				if (!moving && shown++ < 96) {
					others_.box_edges(b.lo, b.hi, faint);
				}
			}
			if (!moving && shown > 96 && !all.empty()) {
				others_.box_edges(all.lo, all.hi, faint);
			}
			// a move they're making: what they hold drawn as it is, where it's going (their offset so far),
			// and where it was, faint
			if (moving) {
				size_t n = 0;
				for (const auto &cl : claims_) {
					const Object *o = cl.second == kv.first ? s.doc.get(cl.first) : nullptr;
					if (o && n++ < 160) {
						draw_shape(others_, *o, cl.first, true, p.pres.delta, c);
						draw_shape(others_, *o, cl.first, true, Vec3(), faint);
					}
				}
			}
			// a reshape they're making (a face pushed, corners dragged, a pad's landing or arc): the shapes as
			// they last sent them, the old ones faint under them
			auto g = ghosts_.find(kv.first);
			if (g != ghosts_.end()) {
				for (const auto &obj : g->second) {
					if (obj.second) {
						draw_shape(others_, *obj.second, obj.first, false, Vec3(), c);
					}
					if (const Object *o = s.doc.get(obj.first)) {
						draw_shape(others_, *o, obj.first, true, Vec3(), faint);
					}
				}
			}
			if (p.pres.box) {
				others_.box_edges(p.pres.b.lo, p.pres.b.hi, c);  // the brush they're drawing
			}
		}
		// what the others just changed: outlined in their color, fading over a second and a half
		for (auto it = recent_.begin(); it != recent_.end();) {
			const int age = now - it->second.second;
			const Player *p = player(it->second.first);
			const Object *o = s.doc.get(it->first);
			if (age > RECENT_MS || !p || !o) {
				it = recent_.erase(it);
				continue;
			}
			unsigned char c[4];
			std::memcpy(c, color_of(p->color), 4);
			c[3] = uint8_t(255 * (RECENT_MS - age) / RECENT_MS);
			draw_shape(others_, *o, it->first, true, Vec3(), c);
			++it;
		}
		others_.end();
		eyes_.end();
	}
	static const float zero[3] = {0, 0, 0};
	static const float ident[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
	others_.draw(zero, ident);
	if (!is3d) {
		eyes_.draw(zero, ident);
		return;
	}
	// each of them as their fighter, flying where their camera is
	for (const auto &kv : players) {
		const Player &p = kv.second;
		if (kv.first == me || !p.pres.heard || p.pres.testing || kv.first == follow) {
			continue;
		}
		if ((p.pres.eye - s.cam.origin).length() < 40) {
			continue;  // right where this camera is: the fighter would fill the view from inside
		}
		std::string model = p.model.empty() ? "sarge/default" : p.model, skin = "default";
		const size_t slash = model.find('/');
		if (slash != std::string::npos) {
			skin = model.substr(slash + 1);
			model = model.substr(0, slash);
		}
		const float eye[3] = {float(p.pres.eye.x), float(p.pres.eye.y), float(p.pres.eye.z)};
		if (!OzEd_AddFlyer(model.c_str(), skin.c_str(), eye, p.pres.yaw, p.pres.pitch)) {
			OzEd_AddFlyer("sarge", "default", eye, p.pres.yaw, p.pres.pitch);
		}
	}
}

// ---------------------------------------------------------------- what the page shows

std::string Together::status() const {
	switch (state) {
	case State::Off:
		return "";
	case State::Joining:
		return "Asking " + (host_name.empty() ? std::string("the host") : host_name) + " to join";
	case State::Syncing:
		return "Linked: the map is on its way";
	case State::Ended:
		return why.empty() ? "The session ended" : why;
	case State::Live:
		break;
	}
	int in = 0;
	for (const auto &kv : players) {
		in += kv.second.linked || kv.first == me;
	}
	std::string line = (hosting ? "Hosting " : "In ") + session_name + ", " + std::to_string(in) + " of " + std::to_string(max_players);
	if (!hosting && OzEdNet_Relayed(host_pid.c_str())) {
		line += ", relayed";
	}
	return line;
}

std::string Together::stats() const {
	static const char *states[] = {"off", "joining", "syncing", "live", "ended"};
	static const char *roles[] = {"owner", "editor", "viewer"};
	const Session &s = together_session();
	char buf[512];
	size_t ghost_objects = 0;
	for (const auto &g : ghosts_) {
		ghost_objects += g.second.size();
	}
	std::snprintf(buf, sizeof buf, "together: %s%s, me %u, seq %u, waiting %zu, objects %zu, players %zu, claims %zu, ghosts %zu, outlined %zu, lease %d left%s%s",
		states[int(state)], hosting ? " hosting" : "", me, seq(), waiting(), s.doc.size(), players.size(), claims_.size(), ghost_objects,
		recent_.size(), int(s.doc.lease_left()), why.empty() ? "" : ", why: ", why.c_str());
	std::string out = buf;
	for (const auto &kv : players) {
		const Player &p = kv.second;
		const int ago = p.pres.heard ? OzEdNet_Milliseconds() - p.pres.heard : -1;
		std::snprintf(buf, sizeof buf, "\ntogether: player %u %s %s (%s) %s color %d, sel %zu, heard %d ms ago, eye %.0f %.0f %.0f%s", p.num,
			p.pid.c_str(), name_of(p.num).c_str(), p.model.c_str(), roles[int(p.role) % 3], p.color, p.sel.size(), ago, p.pres.eye.x,
			p.pres.eye.y, p.pres.eye.z, kv.first == me ? " (me)" : "");
		out += buf;
	}
	return out;
}

}  // namespace oz::ed

// every client frame (cl_main.c), whatever is on the screen
extern "C" void OzEd_TogetherFrame(void) {
	oz::ed::together().frame();
}

// the disconnect command (cl_main.c): a play-test's host leaving it some way other than Back to the map editor
// (Defrag's own menu, the console) tells the testers first, as Back to the map editor does, and the match ends
// 0.8 s later. Without it the server went down under them: an error drop in each tester's game
extern "C" int OzEd_TogetherDisconnect(void) {
	return oz::ed::together().hold_disconnect() ? 1 : 0;
}
