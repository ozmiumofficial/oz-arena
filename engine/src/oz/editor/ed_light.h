#pragma once
// The live light (EDITOR.md "Live light", Ozmium 2026-11-08): the map in the editor's view lit as Play's
// compile lights it (q3map2 -light -fast -filter), and lit again as it's edited. The editor bakes the
// lightmaps itself, on worker threads, with q3map2's own light (light.cpp, light_ydnar.cpp,
// light_bounce.cpp of NetRadiant-custom's q3map2, GPL):
//   point and spot lights (the light entities: light, _color, spawnflags, a target's cone), inverse
//     square or linear, their envelopes as -fast cuts them, shadows from the world's brushes;
//   surface lights (a material's q3map_surfacelight, a face's GLOW copy's): area lights by the exact
//     point to polygon form factor, subdivided at q3map_lightSubdivide, each with its backsplash;
//   the sun (Sky and sun's, a sky's own q3map_sun) where a ray toward it reaches the sky;
//   the sky's own light (its q3map_surfacelight) by the part of the sky each texel sees;
//   the ambient (worldspawn _color times ambient) and the fill light (_floodlight).
// Texels are q3map2's 16 units a side on each face's axial plane, filled where they fall in solid,
// filtered, and clamped by color as q3map2 writes bytes. The renderer draws them as a map's lightmaps
// (tr_oz_editor.h: CreateLightmap, RegisterLitShader).
//
// What a change relights: a brush's own faces; anything a changed light or glowing face reaches; what a
// changed brush may shade from a light, the sun, or the sky around it. The nearest go first.
//
// Plain C++ but for the renderer's table (ed_engine.h's OzEd_Renderer): the core test needn't build it.

#include "ed_doc.h"
#include "ed_ops.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace oz::ed {

// What the light needs of a material.
struct LightMat {
	bool lightmapped = false;  // drawn with a lightmap stage: its faces get texels
	bool sky = false;          // a sky: the sun and the sky's light come in through it
	bool blocks = true;        // light stops at it (a translucent or tool material lets it through)
	double light = 0;          // q3map_surfacelight
	double color[3] = {1, 1, 1};  // the light's color, its brightest channel 1
	double subdivide = 999;    // q3map_lightSubdivide
};

// The light the sky and worldspawn give the map, as the compile hands it to q3map2 (ed_compile.cpp).
struct SkyLight {
	double sun = 0;                   // q3map_sun's brightness (0: no sun)
	double sun_color[3] = {1, 1, 1};
	Vec3 sun_dir{0, 0, 1};            // toward the sun
	double sky = 0;                   // the sky's q3map_surfacelight
	double sky_color[3] = {1, 1, 1};
	double ambient[3] = {0, 0, 0};    // worldspawn _color times ambient
	bool flood = false;               // the fill light (_floodlight)
	double flood_color[3] = {0.94, 0.94, 1};
	double flood_dist = 1024, flood_amount = 128;
	bool same(const SkyLight &o) const;
};

// A shader script's words (its text lowercased): the numbers after a keyword (false when it isn't there
// or has fewer), the word after one, a keyword followed by a word ("surfaceparm" "trans").
bool script_numbers(const std::string &script, const char *key, double *out, int n);
std::string script_word(const std::string &script, const char *key);
bool script_has(const std::string &script, const char *key, const char *word);

class LiveLight {
public:
	LiveLight();
	~LiveLight();
	LiveLight(const LiveLight &) = delete;
	LiveLight &operator=(const LiveLight &) = delete;

	// a brush face's material as drawn (its GLOW copy's), and a curve's: the session's shader library
	std::function<LightMat(const Face &)> face_mat;
	std::function<LightMat(ShaderId)> mat;

	// Each frame, the view's: the changes since the last call handed to the workers, their results put on
	// the pages. eye: where the view looks from (the nearest relit first). moved: lights being dragged and
	// where they are now (relit as they go, as often as the workers keep up).
	using Moved = std::vector<std::pair<Id, Vec3>>;
	void frame(const Doc &doc, const SkyLight &sky, const Vec3 &eye, const Moved &moved = {});
	// The renderer started again: new pages, every texel uploaded again.
	void renderer_restarted();
	// Another document in the session's (New, Open, a session's snapshot): its revisions count anew, so
	// every object is looked at again at the next frame (what's unchanged keeps its texels).
	void forget() { doc_rev_ = ~0ull; }
	// Waits until every texel asked for is in (a test hook; uploads as they come).
	void wait();

	// Where a face's point reads its page, and the page; false: the face has no texels (its plain shader).
	bool face_lm(Id brush, int face, const Vec3 &p, int &page, float &u, float &v) const;
	// A curve's tessellated vertex (row * width + column of patch_mesh(curve, PATCH_TOLERANCE)).
	bool patch_lm(Id patch, int vertex, int &page, float &u, float &v) const;
	static constexpr double PATCH_TOLERANCE = 4;  // the meshers' (ed_view.cpp)
	// The texel a face's point reads, as q3map2's bytes (a test hook): false when the face has none.
	bool sample(Id brush, int face, const Vec3 &p, uint8_t rgb[3]) const;
	// Objects whose texels moved since the last call: their meshes need building again.
	std::vector<Id> take_relaid();

	// The models' light: points (an item's, a spawn's) the probes are worked out at, by the workers.
	void set_probe_points(const std::vector<Vec3> &points);
	bool probes_changed();  // new probes since the last call (then probes() is current)
	struct Probe {
		Vec3 at;
		float ambient[3], directed[3], dir[3];
	};
	const std::vector<Probe> &probes() const { return probes_; }

	// ---- what it's doing, for ed_stats and the status line
	int pending() const;     // faces waiting for their texels
	int surfaces() const { return int(surfs_.size()); }
	int pages() const { return int(pages_.size()); }
	// How often a face or curve found no room on the pages (they're full) since the game started: it draws unlit.
	int place_failures() const { return place_failed_; }
	int lights() const;      // point, spot and area lights in the last scene
	int threads() const;
	double last_ms() const { return last_ms_; }  // the last batch, asked to done

	struct Scene;  // ed_light.cpp
	struct Job;
	struct Result;
	struct Sink;

private:
	struct Surf {
		Id id = 0;
		int face = -1;            // a brush's face, -1 a curve
		ObjectPtr obj;            // the object as laid out
		LightMat m;
		Bounds b;
		Vec3 center;
		double radius = 0;
		// the texels' place
		int page = -1, x = 0, y = 0, w = 0, h = 0;
		// a face: its axial plane (axis), texel size, the rect's first texel's corner in texels
		int axis = 2;
		double S = 16, s0 = 0, t0 = 0;
		Plane plane;
		Winding poly;
		// a curve: its tessellated grid
		int pw = 0, ph = 0;
		std::vector<Vec3> pts, normals;
		uint64_t layout = 0;       // its place's number (a new place: a new number)
		uint64_t want = 0, have = 0;  // the newest job asked for it, the job its texels came from
		std::shared_ptr<Job> queued;
	};
	struct Page {
		int handle = -1;  // the renderer's
		std::vector<uint8_t> rgb;
		int shelf_y = 0;
		struct Shelf {
			int y, h, x;
		};
		std::vector<Shelf> shelves;
		struct Free {
			int x, y, w, h;
		};
		std::vector<Free> free;
		int used = 0;  // texels in use
	};
	struct Seen {
		ObjectPtr obj;
		Bounds b;
	};
	static uint64_t key(Id id, int face) { return (uint64_t(uint32_t(id)) << 16) | uint64_t(face + 1); }

	void add_object(const Doc &doc, Id id, const Object &o);
	void drop_object(Id id);
	bool lay_out(Surf &s);      // a place on a page; false: none
	void free_place(Surf &s);
	bool place(int w, int h, int &page, int &x, int &y);
	void repack();
	void fill_placeholder(Surf &s, const uint8_t rgb[3]);
	void build_scene(const Doc &doc, const SkyLight &sky, const Moved &moved);
	void mark_light_reach(const Vec3 &at, double reach, std::vector<uint64_t> &dirty) const;
	void mark_shade(const Bounds &b, std::vector<uint64_t> &dirty) const;
	void queue(const std::vector<uint64_t> &keys, const Vec3 &eye);
	void collect(int budget_ms);
	void upload(Surf &s);
	void queue_probes();

	std::unordered_map<uint64_t, Surf> surfs_;
	std::unordered_map<Id, std::vector<uint64_t>> by_id_;
	std::unordered_map<Id, Seen> seen_;
	std::vector<Page> pages_;
	std::shared_ptr<const Scene> scene_;
	std::shared_ptr<Sink> sink_;
	SkyLight sky_;
	bool sky_known_ = false;
	uint64_t doc_rev_ = ~0ull;
	uint64_t next_job_ = 1, next_layout_ = 1;
	int place_failed_ = 0;
	std::vector<Id> relaid_;
	bool all_relaid_ = false;
	int generation_ = -1;
	double last_ms_ = 0;
	int batch_t0_ = 0;
	std::vector<Vec3> probe_points_;
	std::vector<Probe> probes_;
	bool probes_new_ = false, probes_due_ = false;
	uint64_t probe_job_ = 0;
	Vec3 eye_;
	Moved moved_;  // the dragged lights the scene was built with
};

}  // namespace oz::ed
