#pragma once
// Editing together (ENGINE_PLAN.md PE step 1, engine/EDITOR.md "Editing together"): the core's side, plain
// C++ without the engine, so editor_core_test.py checks it alone.
//
// Up to 8 players edit one map. One of them hosts the session, as a listen server does: the host gives
// every edit its place in one order and hands out blocks of ids. Every player makes their own edits at
// once and sends them to the host, which numbers them and passes them to everyone; the last edit in the
// host's order wins, per object (a whole brush, curve or entity), but for entity keys, which merge key by
// key. No player runs another's geometry again: an edit is the objects it left, with exact doubles.
//
//   the codec    objects in a binary form with exact doubles; a face carries its file numbers (raw) and
//                its texture axes (st) both, so no player works a mapping out from texture sizes of their own
//   Edit         what travels: a player's changes as whole objects under their ids
//   Replica      a player's copy: the session as the host ordered it (state), the document showing it with
//                that player's own edits not yet back from the host laid over it
//   Host         the order: numbers each edit (merging entity keys, giving brushes whose entity is gone to the
//                world), hands out id blocks, refuses a viewer's edits

#include "ed_doc.h"

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace oz::ed::sync {

// The session's version: a player on another refuses to join (beside the game's own version).
inline constexpr const char *PROTOCOL = "oz-ed1";
inline constexpr Id BLOCK = 1 << 20;  // ids a lease holds; a map with more objects than this isn't one
inline constexpr int MAX_PLAYERS = 8;

using Bytes = std::string;  // binary

struct Writer {
	Bytes out;
	void u8(uint8_t v) { out.push_back(char(v)); }
	void var(uint64_t v);   // 7 bits a byte
	void i32(int32_t v);    // zigzag, then var
	void f64(double v);     // the 8 bytes as they are: exact
	void str(const std::string &s);
	void bytes(const Bytes &b) { var(b.size()); out += b; }
};

struct Reader {
	const uint8_t *p = nullptr, *end = nullptr;
	bool ok = true;
	Reader() = default;
	explicit Reader(const Bytes &b) : p(reinterpret_cast<const uint8_t *>(b.data())), end(p + b.size()) {}
	Reader(const void *data, size_t size) : p(static_cast<const uint8_t *>(data)), end(p + size) {}
	uint8_t u8();
	uint64_t var();
	int32_t i32();
	double f64();
	std::string str();
	Bytes bytes();
	bool done() const { return p >= end; }
	size_t left() const { return ok && end > p ? size_t(end - p) : 0; }
};

void put_object(Writer &w, const Object &o);
ObjectPtr get_object(Reader &r);  // null when it doesn't read
bool same_object(const Object &a, const Object &b);  // what the codec carries, equal

// Deflate (miniz) for what's big: a joiner's snapshot, a play-test's pak. A leading byte says which.
Bytes pack(const Bytes &raw);
bool unpack(const Bytes &packed, Bytes &raw);

struct ObjEdit {
	Id id = 0;
	ObjectPtr after;        // null: removed
	ObjectPtr before;       // sent to the host for an entity: the keys as its maker saw them (the host merges)
	bool changed = false;   // the host's: not what its maker sent (keys merged, its entity gone)
};

struct Edit {
	uint32_t seq = 0;      // the host's order, 1 up; 0 until numbered
	uint32_t player = 0;   // its maker
	uint32_t local = 0;    // its maker's own count, 1 up: how the maker knows it coming back
	bool refused = false;  // the host's: not taken (a viewer's); its maker shows the session's again
	std::string label;
	std::vector<ObjEdit> objects;
};
// to_host: an edit going to the host (its entities' befores along); else a numbered one
void put_edit(Writer &w, const Edit &e, bool to_host);
bool get_edit(Reader &r, Edit &e);

// A player's copy of the session.
class Replica {
public:
	explicit Replica(Doc &doc) : doc_(doc) {}
	~Replica() { stop(); }
	Replica(const Replica &) = delete;
	Replica &operator=(const Replica &) = delete;

	// Starts sharing the document, as it is, at that place in the host's order (a host's own map at 0, a
	// joiner's snapshot at the snapshot's): the document's own changes go to send from now on.
	void start(uint32_t me, uint32_t seq);
	void stop();  // the document's on its own again
	bool running() const { return running_; }
	uint32_t me() const { return me_; }

	std::function<void(const Edit &)> send;  // an edit of this player's, for the host
	// The others' changes the document took (ids), after each receive: the session redraws, drops them
	// from what's being dragged, says who changed what.
	std::function<void(const Edit &, const std::vector<Id> &)> on_remote;

	// A numbered edit from the host, in its order (the host's own go through it too).
	void receive(const Edit &e);
	// Each frame: objects held back while this player's step was open, shown once it ends.
	void tick();

	uint32_t seq() const { return seq_; }
	Id world() const { return doc_.worldspawn(); }
	size_t waiting() const { return sent_.size(); }  // own edits not back yet
	bool own_pending(Id id) const { return pending_.count(id) != 0; }
	const std::unordered_map<Id, ObjectPtr> &state() const { return state_; }
	ObjectPtr state_of(Id id) const;
	// The document as the session has it (what a snapshot carries)
	void snapshot(Writer &w) const;
	static bool read_snapshot(Reader &r, uint32_t &seq, Id &world, std::vector<std::pair<Id, ObjectPtr>> &objects);
	// The edits sent and not back (a new host takes them again)
	std::vector<Edit> unanswered() const;
	// One of those sent again (to a new host, after its snapshot): numbered anew, waited on again
	void resend(Edit e);

	// Everyone's steps, oldest first, for the History panel: who, what
	struct Entry {
		uint32_t seq, player;
		std::string label;
	};
	std::vector<Entry> log;
	size_t log_limit = 400;

private:
	void local(const std::string &label, const std::vector<Doc::Change> &changes);
	void show(const std::vector<Id> &ids);  // the document takes the state's objects where nothing of ours is on its way

	Doc &doc_;
	bool running_ = false;
	uint32_t me_ = 0, seq_ = 0, local_ = 0;
	std::unordered_map<Id, ObjectPtr> state_;
	std::unordered_map<Id, int> pending_;        // own edits on their way, by object
	std::map<uint32_t, Edit> sent_;              // by local number: what went to the host
	std::set<Id> held_;                          // the others' objects not shown while this player's step was open
	std::vector<Id> took_;
};

// The session's order, on the host's PC. It numbers each edit against the host's own replica's state
// (the session as ordered so far) and hands the numbered edit to deliver: the host's replica and the
// other players take it from there.
class Host {
public:
	explicit Host(Replica &mine) : mine_(mine) {}
	// The numbered edit goes to everyone (its maker's copy may be trimmed: see ack()).
	std::function<void(const Edit &)> deliver;
	// An edit from a player (the host's own replica's too); viewers' are refused.
	void take(const Edit &e);
	void start(uint32_t seq) { seq_ = seq; }
	uint32_t seq() const { return seq_; }
	// The next block of ids for a player; the host's own first.
	void grant(Id &from, Id &end);
	void set_viewer(uint32_t player, bool viewer);
	bool viewer(uint32_t player) const { return viewers_.count(player) != 0; }
	// What a numbered edit's maker gets back: the numbers and only the objects the host changed.
	static Edit ack(const Edit &e);
	// Objects claimed by a drag: false (and who has it) when another player has one of them
	bool claim(uint32_t player, const std::vector<Id> &ids, uint32_t &holder);
	void release(uint32_t player);
	uint32_t claimed_by(Id id) const;
	const std::unordered_map<Id, uint32_t> &claims() const { return claims_; }
	Id next_block() const { return next_block_; }
	void set_next_block(Id b) { next_block_ = b; }

private:
	Replica &mine_;
	uint32_t seq_ = 0;
	Id next_block_ = BLOCK;  // the map's own ids are under the first block
	std::set<uint32_t> viewers_;
	std::unordered_map<Id, uint32_t> claims_;
};

// An entity's keys with another player's change on top: cur, with every key their edit (before -> after)
// set or took away set or taken away the same.
Object merge_keys(const Object &cur, const Object &before, const Object &after);

}  // namespace oz::ed::sync
