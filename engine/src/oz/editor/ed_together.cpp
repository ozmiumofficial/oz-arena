// The multiplayer map editor's session (ed_together.h, ENGINE_PLAN.md PE).
#include "ed_together.h"
#include "ed_net.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

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
}  // namespace

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
		if (action == "play_together") {
			std::string why;
			if (!play_together(why)) {
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
		sync::Writer w;
		w.u8(M_EDIT);
		sync::put_edit(w, e, true);
		to_host(w);
	};
	rep_->on_remote = [this](const sync::Edit &e, const std::vector<Id> &ids) {
		Session &s2 = together_session();
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
	start_sharing(1, 0);
	host_ = std::make_unique<sync::Host>(*rep_);
	host_->start(0);
	host_->deliver = [this](const sync::Edit &e) { deliver(e); };
	Id from, end;
	host_->grant(from, end);
	s.doc.lease(from, end);
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

void Together::welcome(Player &p) {
	Session &s = together_session();
	sync::Writer snap;
	rep_->snapshot(snap);
	Id from, end;
	host_->grant(from, end);
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
	p.linked = true;
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
		host_->set_next_block(known_block_ + 64 * sync::BLOCK);  // past any block given since the last roster heard
		host_->deliver = [this](const sync::Edit &e) { deliver(e); };
		next_num_ = 2;
		for (auto &kv : players) {
			kv.second.linked = kv.first == me;
			next_num_ = std::max(next_num_, kv.first + 1);
		}
		players[me].role = Role::Owner;
		for (const sync::Edit &e : rep_->unanswered()) {
			host_->take(e);  // this player's own edits the old host never answered, first in the new order
		}
		Id from, end;
		host_->grant(from, end);
		s.doc.lease(0, 0);
		s.doc.lease(from, end);
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
		if (hosting && !num_of(pid)) {
			return;
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
			p->role = Role::Editor;
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
		case M_LEASE_ASK: {
			if (!p) {
				return;
			}
			Id f, en;
			host_->grant(f, en);
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
		const uint32_t num = uint32_t(r.var());
		const Id from = r.i32(), end = r.i32();
		session_name = r.str();
		map_name = r.str();
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
		// taken over: the edits the old host never answered go to the new one, on its copy of the session
		const std::vector<sync::Edit> again = rejoined && rep_ ? rep_->unanswered() : std::vector<sync::Edit>();
		s.select_none();
		s.doc.plain_form = form <= uint8_t(TexForm::Valve) ? TexForm(form) : TexForm::Valve;
		s.doc.reset(world, objects);
		if (!rejoined) {
			s.path.clear();  // the host's file isn't this PC's: Save asks for a name of this player's own
		}
		s.name = map_name;
		start_sharing(num, seq);
		s.doc.lease(from, end);
		for (const sync::Edit &e : again) {
			rep_->resend(e);
		}
		migrating_ = false;
		host_heard_ = OzEdNet_Milliseconds();
		state = State::Live;
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
			return;
		}
		rep_->receive(e);
		return;
	}
	case M_LEASE: {
		const Id from = r.i32(), end = r.i32();
		if (r.ok) {
			s.doc.lease(from, end);
		}
		asked_lease_ = false;
		return;
	}
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
	const std::string ext = f.substr(f.size() - 4);
	if (ext != ".bsp" && ext != ".aas") {
		return false;
	}
	const std::string stem = f.substr(0, f.size() - 4);
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

std::string random_password() {
	char buf[16];
	std::snprintf(buf, sizeof buf, "%08x", unsigned(OzEdNet_Milliseconds()) * 2654435761u ^ 0x5bd1e995u);
	return buf;
}

}  // namespace

bool Together::play_together(std::string &why) {
	Session &s = together_session();
	if (!live()) {
		why = "Play together is a session's: host or join one";
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
	if (!s.mode_problem().empty()) {
		why = s.mode_problem();  // Capture the flag without its flags
		return false;
	}
	play = Play{};
	play.stage = 1;
	play.starter = me;
	play.rev = seq();
	s.compiled = [this](bool ok, const std::string &dir, const std::string &out) { play_compiled(ok, dir, out, false); };
	s.next_without_bots = true;  // no bots in a play-test together: their file only slows it (and a rough map can fail it)
	if (!s.compile(false, false, false)) {
		s.compiled = nullptr;
		play.stage = 0;
		why = "The map didn't start compiling";
		return false;
	}
	note("Compiling the map on your PC to play it together");
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
	char rev[32];
	std::snprintf(rev, sizeof rev, "_r%u", seq());
	const std::string map = out + rev;
	sync::Bytes bsp, aas;
	if (!read_file(dir + "/maps/" + out + ".bsp", bsp)) {
		play.stage = reloading ? play.stage : 0;
		note("Play together: the compiled map isn't there");
		return;
	}
	const bool bots = read_file(dir + "/maps/" + out + ".aas", aas);
	OzEd_MakeDirs((maps_dir() + "/x").c_str());
	OzEd_WriteOSFile((maps_dir() + "/" + map + ".bsp").c_str(), bsp.data(), int(bsp.size()));
	if (bots) {
		OzEd_WriteOSFile((maps_dir() + "/" + map + ".aas").c_str(), aas.data(), int(aas.size()));
	}
	// the map to everyone, then (once the match is up) where to join it
	auto send_file = [&](const std::string &name, const sync::Bytes &data) {
		sync::Writer w;
		w.u8(M_PAK);
		w.str(name);
		w.bytes(sync::pack(data));
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
	play.stage = 4;
	play.was_active = false;
	OzEdNet_SetCvar("password", play.password.c_str());
	// linked to the match's host already (they host the session, or we do and they're our joiner): over that link;
	// else the online service links us, as any join
	const bool linked = (!hosting && play.host_pid == host_pid) || (hosting && num_of(play.host_pid));
	if (linked) {
		OzEdNet_Command(("connect ice:" + play.host_pid + "\n").c_str());
	} else {
		OzEdNet_Command(("oz_join " + play.host_pid + " " + play.password + "\n").c_str());
	}
	note("Joining " + name_of(play.starter) + "'s play-test");
}

void Together::on_play(const std::string &from, uint8_t type, sync::Reader &r, const sync::Bytes &b) {
	// the host passes a joiner's on to the others first
	if (hosting) {
		const uint32_t n = num_of(from);
		sync::Writer w;
		w.out = b;
		to_all(w, n);
	}
	switch (type) {
	case M_PAK: {
		const std::string name = r.str();
		const sync::Bytes packed = r.bytes();
		sync::Bytes raw;
		if (!r.ok || !safe_pak_file(name) || !sync::unpack(packed, raw)) {
			log("together: a play-test file didn't arrive whole (%s)\n", name.c_str());
			return;
		}
		OzEd_MakeDirs((maps_dir() + "/x").c_str());
		OzEd_WriteOSFile((maps_dir() + "/" + name).c_str(), raw.data(), int(raw.size()));
		log("together: play-test file %s, %zu KB\n", name.c_str(), raw.size() / 1024);
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
		if (!r.ok || starter == me) {
			return;
		}
		play = p;
		join_play();
		return;
	}
	case M_PLAYEND: {
		const uint32_t starter = uint32_t(r.var());
		if (play.stage == 4 && play.starter == starter) {
			play.stage = 0;
			OzEdNet_Command("disconnect\neditor_together\n");
			OzEdNet_ClearError();
			note(name_of(starter) + " ended the play-test: back to the map");
		}
		return;
	}
	case M_RELOAD_ASK: {
		if (play.stage == 3 && play.starter == me) {
			reload();
		}
		return;
	}
	case M_RELOAD: {
		const uint32_t starter = uint32_t(r.var());
		const std::string map = r.str();
		const uint32_t rev = uint32_t(r.var());
		if (!r.ok || starter == me || play.stage != 4) {
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
	return (play.stage == 3 || play.stage == 4) && !play.reloading && seq() > play.rev;
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
			}
			if (play.listed) {
				OzEdNet_Command("oz_unhost\n");
			}
			play.stage = 0;
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
	if (!hosting && !migrating_ && host_heard_ && OzEdNet_Milliseconds() - host_heard_ > HOST_SILENT_MS) {
		migrate("The host stopped answering");
	}
	if (!live()) {
		return;
	}
	rep_->tick();
	if (s.compiling()) {
		s.compile_poll();  // the editor's page polls it only while it draws: a Reload's compile runs during the match
	}
	send_presence();
	send_selection();
	claims_tick();
	play_tick();
	// a new block of ids well before this one runs out
	if (!s.doc.lease_spare() && s.doc.lease_left() < sync::BLOCK / 2) {
		if (hosting) {
			Id f, e;
			host_->grant(f, e);
			s.doc.lease(f, e);
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
			// their selection, outlined in their color; moved with their drag
			const Vec3 d = p.pres.moving ? p.pres.delta : Vec3();
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
				if (shown++ < 96) {
					others_.box_edges(b.lo + d, b.hi + d, c);
				}
			}
			if (shown > 96 && !all.empty()) {
				others_.box_edges(all.lo + d, all.hi + d, c);
			}
			if (p.pres.box) {
				others_.box_edges(p.pres.b.lo, p.pres.b.hi, c);
			}
		}
		// objects someone else is dragging: their outline in that player's color
		for (const auto &kv : claims_) {
			if (kv.second == me) {
				continue;
			}
			const Player *p = player(kv.second);
			if (!p || !s.doc.get(kv.first)) {
				continue;
			}
			const Bounds b = bounds_of(s.doc, kv.first);
			if (!b.empty()) {
				others_.box_edges(b.lo - Vec3(2, 2, 2), b.hi + Vec3(2, 2, 2), color_of(p->color));
			}
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
	std::snprintf(buf, sizeof buf, "together: %s%s, me %u, seq %u, waiting %zu, objects %zu, players %zu, claims %zu, lease %d left%s%s",
		states[int(state)], hosting ? " hosting" : "", me, seq(), waiting(), s.doc.size(), players.size(), claims_.size(),
		int(s.doc.lease_left()), why.empty() ? "" : ", why: ", why.c_str());
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
