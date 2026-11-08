#pragma once
// The multiplayer map editor's session (ENGINE_PLAN.md PE, engine/EDITOR.md "Editing together"): up to 8
// players edit one map live, see each other work, and play-test it together, peer-to-peer over P8's links.
//
// The multiplayer map editor is its own: its own entry in the main menu, its own session (its map, camera,
// autosave), apart from the map editor alone's, which never runs any of this (Ozmium, 2026-11-07).
//
// One player hosts: the host orders the edits (ed_sync.h's Host) and hands out id blocks; every player keeps
// a Replica of the session. The links are a star: each joiner is linked to the host alone, which passes the
// others' edits, selections and presence on. Messages go on the editing channel (ed_net.h): reliable ones
// (a type byte, then its fields, in ed_sync.h's codec) and presence, fast and unacknowledged, 20 a second.
//
// Each player sees the others as their fighters flying where their cameras are, with their names over
// them, their pointers, selections and drags in their colors. An object another player is dragging can't
// be grabbed. Saving and compiling are each player's own, on their own PC (decision 27).

#include "ed_sync.h"
#include "ed_view.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace oz::ed {

Session &solo_session();      // the map editor alone's
Session &together_session();  // the multiplayer map editor's
void use_session(bool together);  // which one session() is: the page shows it
bool using_together();

class Together {
public:
	enum class State { Off, Joining, Syncing, Live, Ended };
	enum class Role : uint8_t { Owner, Editor, Viewer };
	struct Presence {
		Vec3 eye;
		float yaw = 0, pitch = 0;
		bool pointer = false, moving = false, box = false, testing = false;
		Vec3 at, delta;
		Bounds b;
		int heard = 0;  // when it came (ms)
	};
	struct Player {
		uint32_t num = 0;
		std::string pid, name, model;
		Role role = Role::Editor;
		int color = 0;
		Presence pres;
		std::vector<Id> sel;
		bool linked = false;  // the host: a joiner whose link is up and who has the session
	};

	State state = State::Off;
	bool hosting = false;
	std::string session_name;   // what the session is listed as
	std::string map_name;       // the map's name, as the host has it
	std::string host_pid, host_name;
	uint32_t me = 0;
	uint32_t follow = 0;        // the player whose camera this one's follows (0: none)
	std::map<uint32_t, Player> players;
	std::string why;            // why joining failed or the session ended
	int max_players = sync::MAX_PLAYERS;
	int changed_rev = 0;        // bumps when the players or the state change (the page)

	static const unsigned char *color_of(int c);  // RGBA
	static const char *color_hex(int c);

	// Hosts a session on the multiplayer map editor's map as it is now: listed on the hub as name.
	bool host(const std::string &name, const std::string &password, int max, std::string &err);
	// Asks a host for their session; the map comes when the link is up.
	void join(const std::string &host_pid, const std::string &password, const std::string &host_name = "");
	// Leaves (a host ends it for everyone); the map stays as it was, to save.
	void leave(const std::string &why = "");
	bool active() const { return state == State::Joining || state == State::Syncing || state == State::Live; }
	bool live() const { return state == State::Live; }
	const Player *player(uint32_t num) const;
	const Player *mine() const { return player(me); }
	Role my_role() const;
	uint32_t holder(Id id) const;   // the player dragging it (0: none, or this player)
	// Owner's: a player out, or another role
	void kick(uint32_t num);
	void set_role(uint32_t num, Role role);

	// Every client frame, whatever is on the screen (the editor's page, a play-test): the links' messages,
	// this player's presence and selection, the others' changes into the map.
	void frame();
	// The others into a pane's scene (Session::draw_extra).
	void draw(bool is3d);
	std::string status() const;   // the session bar's line
	std::string stats() const;    // edt_stats
	size_t waiting() const { return rep_ ? rep_->waiting() : 0; }
	uint32_t seq() const { return rep_ ? rep_->seq() : 0; }
	const std::vector<sync::Replica::Entry> *history() const { return rep_ ? &rep_->log : nullptr; }
	std::string name_of(uint32_t num) const;

	// ---- playing together (PE steps 5 and 6). The player who starts it compiles the session's map on their own
	// PC, sends it to the others, hosts the match from their PC (a listen server: one BSP for all, so collision
	// and prediction agree) and the others join it; Back to the map editor brings them all back, the session as
	// it went on. A newer revision offers Reload: the match's host compiles it and loads it for everyone, each
	// player put back where they stood.
	struct Play {
		int stage = 0;             // 0 none; the starter: 1 compiling, 2 loading the match, 3 hosting it; a tester: 4 joined
		uint32_t starter = 0;
		std::string host_pid, map, password;
		uint32_t rev = 0;          // the session's place in the order the map was compiled at
		bool was_active = false;   // a tester: the match was entered
		bool listed = false;       // the starter (not the session's host): the match listed for the others to join
		bool reloading = false;    // a reload under way: positions saved, the new map coming
		float pos[3] = {0, 0, 0}, angles[3] = {0, 0, 0};
		bool saved = false;
		int listed_at = 0;         // the starter: when its match was listed (the others told a moment after)
		int arrived_at = 0;        // a reload: when the new map was first in (put back a moment later)
		int ending_at = 0;         // the starter going back: the testers told, the match ends a moment after
		int team_since = 0, team_asked = 0;  // a team mode: when this player was first seen a spectator, and last put on a team
		bool told_end = false;
	} play;
	bool play_together(std::string &why);  // Ctrl+Shift+F5
	void reload();                         // Reload (the match's host does it; a tester asks)
	// Back to the map editor (the pause menu's): the match's host tells the testers first, so they leave it
	// themselves before its server goes (a server gone under a client is an error drop)
	void back_to_editor();
	bool reload_offered() const;           // a tester's match is older than the session's map
	bool playing_together() const { return play.stage != 0; }

private:
	enum Msg : uint8_t {
		M_HELLO = 1, M_WELCOME, M_ROSTER, M_EDIT, M_SEQ, M_LEASE_ASK, M_LEASE, M_CLAIM, M_CLAIMS, M_CLAIM_NO,
		M_SELECT, M_BYE, M_KICK, M_PAK, M_PLAY, M_PLAYEND, M_RELOAD_ASK, M_RELOAD,
	};
	void on_play(const std::string &from, uint8_t type, sync::Reader &r, const sync::Bytes &b);  // M_PAK ... M_RELOAD
	void play_compiled(bool ok, const std::string &dir, const std::string &out, bool reload);
	void play_tick();
	void join_play();
	void put_back(int client_state);
	void team_tick(int client_state);
	void attach();     // the session's hooks
	void detach();
	void start_sharing(uint32_t me, uint32_t seq);
	void on_message(const std::string &pid, const sync::Bytes &b);
	void on_presence(const std::string &pid, const sync::Bytes &b);
	void on_up(const std::string &pid);
	void on_down(const std::string &pid, bool failed, const std::string &why);
	void send_to(const std::string &pid, const sync::Writer &w, bool fast = false);
	void to_all(const sync::Writer &w, uint32_t except = 0, bool fast = false);  // the host's
	void to_host(const sync::Writer &w, bool fast = false);
	void deliver(const sync::Edit &e);     // the host: a numbered edit to everyone
	void roster();                         // the host: who's in, to everyone
	void put_roster(sync::Writer &w) const;
	bool get_roster(sync::Reader &r);
	void welcome(Player &p);
	void send_presence();
	void send_selection();
	void claims_tick();
	void ended(const std::string &why);
	void relist();
	uint32_t num_of(const std::string &pid) const;
	void note(const std::string &text);

	std::unique_ptr<sync::Replica> rep_;
	std::unique_ptr<sync::Host> host_;
	std::string password_;
	uint32_t next_num_ = 2;
	int presence_at_ = 0, presence_beat_ = 0;
	std::string presence_last_;
	uint64_t sel_rev_ = ~0ull;
	int sel_at_ = 0;
	bool sel_due_ = false;
	std::vector<Id> claimed_;              // what this player's drag holds, as told to the host
	std::map<Id, uint32_t> claims_;        // the others' drags' objects
	bool asked_lease_ = false;
	// the host gone (PE step 7): who takes over, and the others' way back in
	void migrate(const std::string &why);
	void migrate_tick();
	bool migrating_ = false;
	int migrate_until_ = 0, retry_at_ = 0, host_heard_ = 0;
	Id known_block_ = sync::BLOCK;   // the next id block the host may give, as its rosters say
	int joined_at_ = 0;
	int last_note_ = 0;
	Overlay others_, eyes_;   // the others' lines; the frames round their eyes (the 2D views' marks)
	int others_built_ = -1;
};

Together &together();

}  // namespace oz::ed
