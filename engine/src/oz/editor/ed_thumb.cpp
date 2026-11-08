// The map editor's thumbnail camera (ENGINE_PLAN.md P13, engine/EDITOR.md): the map's picture,
// levelshots/<map>.jpg, which the menus, the loading screen and the scoreboard's header show. Quake III
// takes it from the map's intermission point (info_player_intermission) with the game's levelshot command
// (g_cmds.c Cmd_LevelShot_f: the player put there as at a match's end, cg_view.c's 90 degrees across,
// nothing 2D drawn), and so does the editor: the camera is that entity (the first, the one the game's
// FindIntermissionPoint takes), set from the 3D view, and its picture is drawn in the lit preview (the last
// compile's world, its lightmaps and light grid, the pickups) at 4:3, in a frame in the view's middle for
// the few frames a screenshot takes. Compile (Ctrl+F5) takes it when it's done; Make thumbnail takes it any
// time, a quick compile first when the map changed since the last one.

#include "ed_view.h"
#include "ed_engine.h"
#include "ed_mapfile.h"
#include "../ui/oz_ui_engine.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <memory>

namespace oz::ed {

static const double PI = 3.14159265358979323846;
static const char *const SHOT = "_oz_levelshot";  // the renderer's screenshot: screenshots/_oz_levelshot.tga
static const int SHOT_MAX_W = 1280;               // as wide as Oz's own shots of id's maps (tools/make_levelshots.py)
// 90 degrees across at 4:3: up and down, 2 atan(3/4)
static const double FOV_X = 90, FOV_Y = 2 * std::atan(0.75) * 180 / PI;

static std::string shot_tga() {
	return std::string(OzEd_HomePath()) + "/oz/screenshots/" + SHOT + ".tga";
}

static std::string thumbs_dir() {
	return std::string(OzEd_HomePath()) + "/oz/editor/thumbs";
}

bool camera_view(const Doc &doc, Id id, Vec3 &o, double &yaw, double &pitch) {
	const Object *c = id ? doc.get(id) : nullptr;
	if (!c || c->kind != Kind::Entity || !parse_vec(c->entity.get("origin"), o)) {
		return false;
	}
	// its angles as the game spawns them, in the file's order: a later "angle" or "angles" wins
	yaw = 0, pitch = 0;
	for (const auto &kv : c->entity.keys) {
		Vec3 a;
		if (kv.first == "angle") {
			pitch = 0, yaw = std::atof(kv.second.c_str());
		} else if (kv.first == "angles" && parse_vec(kv.second, a)) {
			pitch = a.x, yaw = a.y;
		}
	}
	// a target turns it toward it (g_main.c FindIntermissionPoint, vectoangles)
	const std::string target = c->entity.get("target");
	if (!target.empty()) {
		for (Id t : doc.ids_of(Kind::Entity)) {
			const Entity &e = doc.get(t)->entity;
			Vec3 at;
			if (t != id && e.get("targetname") == target && parse_vec(e.get("origin"), at)) {
				const Vec3 d = at - o;
				if (d.length() > 1e-6) {
					yaw = std::atan2(d.y, d.x) * 180 / PI;
					pitch = -std::atan2(d.z, std::sqrt(d.x * d.x + d.y * d.y)) * 180 / PI;
				}
				break;
			}
		}
	}
	return true;
}

Id Session::thumb_camera() const {
	for (Id id : doc.ids_of(Kind::Entity)) {
		const Object *o = doc.get(id);
		if (o && o->entity.classname() == "info_player_intermission") {
			return id;
		}
	}
	return 0;
}

bool Session::thumb_view(Vec3 &origin, double &yaw, double &pitch) const {
	return camera_view(doc, thumb_camera(), origin, yaw, pitch);
}

void Session::thumb_set() {
	// whole units and tenths of a degree, as a mapper would type them
	const Vec3 at(std::round(cam.origin.x), std::round(cam.origin.y), std::round(cam.origin.z));
	double yaw = std::fmod(cam.yaw, 360.0);
	yaw = std::round((yaw < 0 ? yaw + 360 : yaw) * 10) / 10;
	yaw = yaw >= 360 ? 0 : yaw;
	const double pitch = std::round(std::clamp(cam.pitch, -89.0, 89.0) * 10) / 10;
	const Id id = thumb_camera();
	std::shared_ptr<Object> o = id ? doc.edit(id) : std::make_shared<Object>();
	if (!id) {
		o->kind = Kind::Entity;
		o->entity.set("classname", "info_player_intermission");
	}
	const std::string target = o->entity.get("target");
	o->entity.set("origin", vec_key(at));
	while (o->entity.erase("angle")) {
	}
	while (o->entity.erase("target")) {
	}
	while (o->entity.erase("angles")) {
	}
	o->entity.set("angles", number(pitch) + " " + number(yaw) + " 0");
	doc.begin(id ? "Move thumbnail camera" : "Thumbnail camera");
	if (id) {
		doc.put(id, o);
	} else {
		doc.add(o);
	}
	doc.end();
	thumb_look();
	thumb_rev++;
	say(std::string(id ? "The thumbnail camera moved here" : "The thumbnail camera is here") +
		(target.empty() ? "" : " (it aimed at " + target + ": its angles aim it now)") +
		": the frame is its picture. Compile, or Make thumbnail, takes it");
}

void Session::thumb_look() {
	Vec3 o;
	double yaw, pitch;
	if (!thumb_view(o, yaw, pitch)) {
		say("No thumbnail camera yet: Set thumbnail camera puts one where you look from");
		return;
	}
	if (layout == 1 && solo != Pane::Cam) {
		solo = Pane::Cam;  // the one view: the 3D one
	}
	cam.origin = o;
	cam.yaw = yaw;
	cam.pitch = std::clamp(pitch, -89.0, 89.0);
	through_ = true;
	through_cam_ = cam;
}

bool Session::thumb_busy() const {
	return shot_.state != SHOT_IDLE || (job_.running && job_.thumb);
}

void Session::thumb_shoot() {
	Vec3 o;
	double yaw, pitch;
	if (!thumb_view(o, yaw, pitch)) {
		say("No thumbnail camera yet: Set thumbnail camera (Build menu) puts one where you look from", true);
		return;
	}
	if (thumb_busy()) {
		say("The thumbnail is on its way");
		return;
	}
	if (!renderer_ok()) {
		say("Only the oz renderer draws the editor's view, and with it the thumbnail", true);
		return;
	}
	shot_error_.clear();
	// the last compile, when it's this map as it is now
	const std::string bsp = std::string(OzEd_HomePath()) + "/oz/maps/" + compile_name() + ".bsp";
	if (compiled_name_ == compile_name() && changes_since_compile() == 0 && OzEd_FileExists(bsp.c_str())) {
		start_shot(false);
		return;
	}
	if (job_.running) {
		if (!job_.play && !job_.defrag) {
			job_.thumb = true;  // this compile's world, when it's done
			thumb_rev++;
			say("The thumbnail comes when this compile is done");
		} else {
			say("A play-test is compiling: Make thumbnail again once it's done");
		}
		return;
	}
	if (compile(false, false, false)) {
		job_.thumb = true;
		thumb_rev++;
		say("Compiling for the thumbnail (quick light: Compile lights it fully, and takes it too)");
	}
}

void Session::start_shot(bool loading) {
	shot_ = Shot();
	shot_.bsp = "maps/" + compile_name() + ".bsp";
	shot_.t0 = OzE_Milliseconds();
	shot_error_.clear();
	if (loading) {
		// the compile loads its world into the lit preview, which stays on after
		shot_.state = SHOT_WORLD;
		shot_.gen = OzE_RendererGeneration();
	} else if (previewing() && shot_.bsp == OzEd_World()) {
		shot_.state = SHOT_DRAW;
	} else {
		shot_.state = SHOT_STILL;  // the world loaded once nothing is being dragged
		shot_.leave = !previewing();
	}
	thumb_rev++;
}

void Session::shot_failed(const std::string &why) {
	if (shot_.leave && previewing()) {
		OzE_InsertText("ed_world\n");
	}
	shot_ = Shot();
	shot_error_ = why;
	OzEd_RemoveOSFile(shot_tga().c_str());
	thumb_rev++;
	say("No thumbnail: " + why, true);
}

void Session::thumb_reset() {
	if (shot_.state != SHOT_IDLE) {
		if (shot_.leave && previewing()) {
			OzE_InsertText("ed_world\n");
		}
		shot_ = Shot();
	}
	shot_file_.clear();
	shot_panel_.clear();
	shot_error_.clear();
	shot_w_ = shot_h_ = 0;
	through_ = false;
	thumb_rev++;
}

// The newest of a map's panel copies, "<map>.<stamp>.jpg"
static std::vector<std::pair<unsigned long long, std::string>> panel_copies(const std::string &map) {
	std::vector<std::pair<unsigned long long, std::string>> found;
	int count = 0;
	char **files = OzEd_ListOSFiles(thumbs_dir().c_str(), "jpg", &count);
	for (int i = 0; i < count; i++) {
		const std::string n = files[i];
		if (n.size() > map.size() + 5 && n.compare(0, map.size() + 1, map + ".") == 0) {
			const std::string stamp = n.substr(map.size() + 1, n.size() - map.size() - 5);
			if (!stamp.empty() && std::all_of(stamp.begin(), stamp.end(), [](char c) { return std::isdigit((unsigned char)c); })) {
				found.push_back({std::stoull(stamp), n});
			}
		}
	}
	OzEd_FreeList(files);
	std::sort(found.begin(), found.end());
	return found;
}

std::string Session::thumb_image() const {
	if (!shot_panel_.empty()) {
		return shot_panel_;
	}
	// one an earlier session made of this map
	const auto found = panel_copies(compile_name());
	return found.empty() ? "" : "/editor/thumbs/" + found.back().second;
}

std::string Session::thumb_note() const {
	Vec3 o;
	double yaw, pitch;
	if (!thumb_view(o, yaw, pitch)) {
		return "No thumbnail camera yet. Set from view puts one where you look from: the menus, the loading screen and the "
			   "scoreboard show the picture it takes (the camera at a match's end is there too)";
	}
	if (job_.running && job_.thumb) {
		return "Compiling for the thumbnail ...";
	}
	if (shot_.state != SHOT_IDLE) {
		return "Taking the thumbnail ...";
	}
	if (!shot_error_.empty()) {
		return "No thumbnail: " + shot_error_;
	}
	const std::string level = "levelshots/" + compile_name() + ".jpg";
	if (shot_file_.empty()) {
		return thumb_image().empty() ? "No picture yet: Make thumbnail takes it from the camera (Compile takes it too)"
			: level + ", from an earlier session: Make thumbnail takes it again";
	}
	char buf[64];
	std::snprintf(buf, sizeof buf, ", %d x %d", shot_w_, shot_h_);
	const bool moved = (o - shot_origin_).length() > 0.5 || std::fabs(yaw - shot_yaw_) > 0.05 || std::fabs(pitch - shot_pitch_) > 0.05;
	return level + buf + (moved ? ". The camera moved since: Make thumbnail takes it again" : ", from the camera as it is");
}

std::string Session::thumb_stats() const {
	Vec3 o;
	double yaw = 0, pitch = 0;
	char buf[400];
	if (!thumb_view(o, yaw, pitch)) {
		std::snprintf(buf, sizeof buf, "camera none; through %d; state %d", through_, int(shot_.state));
		return buf;
	}
	// game: the game's own files find it, as the menus and the loading screen ask for it
	const int game = OzE_FileExists(("levelshots/" + compile_name() + ".jpg").c_str());
	std::snprintf(buf, sizeof buf, "camera %d %.0f %.0f %.0f yaw %.1f pitch %.1f; through %d; state %d%s; shot %s %dx%d game %d panel %s%s%s",
		int(thumb_camera()), o.x, o.y, o.z, yaw, pitch, through_, int(shot_.state), job_.running && job_.thumb ? " compiling" : "",
		shot_file_.empty() ? "none" : shot_file_.c_str(), shot_w_, shot_h_, game, shot_panel_.empty() ? "none" : shot_panel_.c_str(),
		shot_error_.empty() ? "" : "; failed: ", shot_error_.c_str());
	return buf;
}

void Session::write_thumb_beside(const std::string &mapfile) const {
	// the picture this session took, else one of this map's from before
	std::string from = shot_file_;
	if (from.empty() || !OzEd_FileExists(from.c_str())) {
		from = std::string(OzEd_HomePath()) + "/oz/levelshots/" + compile_name() + ".jpg";
		if (!OzEd_FileExists(from.c_str())) {
			return;
		}
	}
	// a map saved into a maps folder: its picture in the levelshots folder beside (a pk3's layout), else beside it
	const size_t slash = mapfile.find_last_of("/\\");
	const std::string dir = slash == std::string::npos ? "." : mapfile.substr(0, slash);
	std::string stem = mapfile.substr(slash == std::string::npos ? 0 : slash + 1);
	stem = stem.substr(0, stem.find_last_of('.'));
	std::string folder = dir.substr(dir.find_last_of("/\\") + 1);
	std::transform(folder.begin(), folder.end(), folder.begin(), [](unsigned char c) { return char(std::tolower(c)); });
	const std::string to = folder == "maps" ? dir.substr(0, dir.find_last_of("/\\")) + "/levelshots/" + stem + ".jpg" : dir + "/" + stem + ".jpg";
	char *data = nullptr;
	const int len = OzEd_ReadOSFile(from.c_str(), &data);
	if (len > 0) {
		OzEd_MakeDirs(to.c_str());
		OzEd_WriteOSFile(to.c_str(), data, len);
	}
	OzEd_Free(data);
}

// An RGB picture made smaller by averaging the source pixels under each one.
static std::vector<unsigned char> shrink(const std::vector<unsigned char> &src, int sw, int sh, int dw, int dh) {
	std::vector<unsigned char> out(size_t(dw) * dh * 3);
	for (int y = 0; y < dh; y++) {
		const int y0 = y * sh / dh, y1 = std::max(y0 + 1, (y + 1) * sh / dh);
		for (int x = 0; x < dw; x++) {
			const int x0 = x * sw / dw, x1 = std::max(x0 + 1, (x + 1) * sw / dw);
			unsigned sum[3] = {0, 0, 0}, n = 0;
			for (int yy = y0; yy < y1; yy++) {
				const unsigned char *p = &src[(size_t(yy) * sw + x0) * 3];
				for (int xx = x0; xx < x1; xx++, p += 3) {
					sum[0] += p[0], sum[1] += p[1], sum[2] += p[2], n++;
				}
			}
			unsigned char *o = &out[(size_t(y) * dw + x) * 3];
			o[0] = (unsigned char)((sum[0] + n / 2) / n), o[1] = (unsigned char)((sum[1] + n / 2) / n), o[2] = (unsigned char)((sum[2] + n / 2) / n);
		}
	}
	return out;
}

void Session::thumb_tick(int x, int y, int w, int h, int now) {
	if (shot_.state == SHOT_IDLE) {
		return;
	}
	const int waited = OzE_Milliseconds() - shot_.t0;
	if (shot_.state == SHOT_STILL) {
		// the world loads between frames, the client's systems starting again: not under a drag or a look
		if (dragging() || looking_ || thumb_state_) {
			return;
		}
		shot_.gen = OzE_RendererGeneration();
		shot_.state = SHOT_WORLD;
		OzE_InsertText(("ed_world " + shot_.bsp + "\n").c_str());
		return;
	}
	if (shot_.state == SHOT_WORLD) {
		if (OzE_RendererGeneration() != shot_.gen && shot_.bsp == OzEd_World()) {
			shot_.state = SHOT_DRAW;
			shot_.frames = 0;
			shot_.t0 = OzE_Milliseconds();
		} else if (waited > 60000) {
			shot_failed("the compiled map didn't load as the lit preview");
		}
		return;
	}
	// drawing: the camera's view, every frame until the screenshot has it
	if (shot_.bsp != OzEd_World()) {
		shot_failed("the lit preview closed before it was taken");
		return;
	}
	if (thumb_state_) {
		return;  // a prefab's thumbnail is in the view's middle: after it
	}
	if (shot_.frames == 0) {
		Vec3 o;
		if (!thumb_view(o, shot_.yaw, shot_.pitch)) {
			shot_failed("the thumbnail camera is gone");
			return;
		}
		shot_.origin = o;
		// 4:3, as large as the view holds with a margin round it
		int fw = std::min(w - 24, int((h - 24) * 4.0 / 3.0));
		fw -= fw % 4;
		const int fh = fw * 3 / 4;
		if (fw < 160) {
			shot_failed("the view is too small to draw it");
			return;
		}
		shot_.rect[0] = x + (w - fw) / 2, shot_.rect[1] = y + (h - fh) / 2;
		shot_.rect[2] = fw, shot_.rect[3] = fh;
		OzEd_RemoveOSFile(shot_tga().c_str());
	}
	const int *rc = shot_.rect;
	static const float back[4] = {0, 0, 0, 1};
	OzEd_Fill(rc[0], rc[1], rc[2], rc[3], back);
	OzEd_ClearScene();
	static const float zero[3] = {0, 0, 0};
	draw_models(zero, now, false);  // the pickups (the world has the rest), no one standing on the spawns
	Camera c;
	c.origin = shot_.origin, c.yaw = shot_.yaw, c.pitch = shot_.pitch;
	Vec3 f, l, u;
	c.axes(f, l, u);
	const float org[3] = {float(c.origin.x), float(c.origin.y), float(c.origin.z)};
	const float axis[3][3] = {{float(f.x), float(f.y), float(f.z)}, {float(l.x), float(l.y), float(l.z)}, {float(u.x), float(u.y), float(u.z)}};
	OzEd_RenderScene(rc[0], rc[1], rc[2], rc[3], org, axis, float(FOV_X), float(FOV_Y), now, 0);
	shot_.frames++;
	if (shot_.frames == 3) {
		OzE_InsertText((std::string("screenshot ") + SHOT + "\n").c_str());  // a frame or two in: the world's images all up
	}
	char *data = nullptr;
	const int len = shot_.frames > 3 ? OzEd_ReadOSFile(shot_tga().c_str(), &data) : -1;
	if (len < 18) {
		OzEd_Free(data);
		// by the clock: a console script's wait holds the screenshot command back, not the frames
		if (waited > 20000 && shot_.frames > 3) {
			shot_failed("the screenshot never came");
		}
		return;
	}
	const unsigned char *t = (const unsigned char *)data;
	const int sw = t[12] | t[13] << 8, sh = t[14] | t[15] << 8, idlen = t[0];
	const unsigned char *px = t + 18 + idlen;
	if (t[2] != 2 || t[16] != 24 || len < 18 + idlen + sw * sh * 3) {
		OzEd_Free(data);  // still being written
		return;
	}
	// the TGA's rows go bottom-up, as the JPEG wants them: the frame's rows cut out of it, its bottom first
	const int cw = std::min(rc[2], sw - rc[0]), ch = std::min(rc[3], sh - rc[1]);
	if (cw < 64 || ch < 48 || rc[0] < 0 || rc[1] < 0) {
		OzEd_Free(data);
		shot_failed("the view wasn't in the screenshot");
		return;
	}
	std::vector<unsigned char> rgb(size_t(cw) * ch * 3);
	for (int row = 0; row < ch; row++) {
		const int src_row = sh - 1 - (rc[1] + ch - 1 - row);
		const unsigned char *s = px + (size_t(src_row) * sw + rc[0]) * 3;
		unsigned char *o = &rgb[size_t(row) * cw * 3];
		for (int i = 0; i < cw; i++) {
			o[i * 3] = s[i * 3 + 2], o[i * 3 + 1] = s[i * 3 + 1], o[i * 3 + 2] = s[i * 3];
		}
	}
	OzEd_Free(data);
	OzEd_RemoveOSFile(shot_tga().c_str());
	int ow = cw, oh = ch;
	if (ow > SHOT_MAX_W) {
		ow = SHOT_MAX_W, oh = SHOT_MAX_W * ch / cw;
		rgb = shrink(rgb, cw, ch, ow, oh);
	}

	// the play-test's level shot, where the game finds it (<fs_homepath>/oz is the game's folder)
	const std::string level = std::string(OzEd_HomePath()) + "/oz/levelshots/" + compile_name() + ".jpg";
	OzEd_MakeDirs(level.c_str());
	if (!OzEd_SaveJPG(level.c_str(), ow, oh, rgb.data(), 92)) {
		shot_failed("can't write " + level);
		return;
	}
	// the panel's copy, a new name each time (the UI keeps an image by its path)
	const auto old = panel_copies(compile_name());
	const std::string stamp = std::to_string((unsigned long long)std::time(nullptr) * 1000ull + (unsigned long long)(OzE_Milliseconds() % 1000));
	const std::string panel = thumbs_dir() + "/" + compile_name() + "." + stamp + ".jpg";
	OzEd_MakeDirs(panel.c_str());
	if (OzEd_SaveJPG(panel.c_str(), ow, oh, rgb.data(), 90)) {
		for (const auto &f : old) {
			OzEd_RemoveOSFile((thumbs_dir() + "/" + f.second).c_str());
		}
		shot_panel_ = "/editor/thumbs/" + compile_name() + "." + stamp + ".jpg";
	}
	shot_file_ = level;
	shot_w_ = ow, shot_h_ = oh;
	shot_origin_ = shot_.origin, shot_yaw_ = shot_.yaw, shot_pitch_ = shot_.pitch;
	if (!path.empty()) {
		write_thumb_beside(path);
	}
	const bool leave = shot_.leave;
	shot_ = Shot();
	thumb_rev++;
	OzE_Print(("map editor: thumbnail levelshots/" + compile_name() + ".jpg " + std::to_string(ow) + "x" + std::to_string(oh) + "\n").c_str());
	say("Thumbnail taken: levelshots/" + compile_name() + ".jpg, the picture the menus and the loading screen show");
	if (leave) {
		OzE_InsertText("ed_world\n");  // the lit preview was off before
	}
}

void Session::draw_through() {
	// looking through the camera: its picture's frame in the 3D view, what's outside it dimmed
	if (!through_) {
		return;
	}
	const Rect &r = panes_[int(Pane::Cam)];
	if (r.w <= 0 || r.h <= 0) {
		return;
	}
	const int fw = std::min(r.w, int(r.h * 4.0 / 3.0)), fh = std::min(r.h, fw * 3 / 4);
	const int fx = r.x + (r.w - fw) / 2, fy = r.y + (r.h - fh) / 2;
	static const float dim[4] = {0, 0, 0, 0.55f}, edge[4] = {1.0f, 0.93f, 0.86f, 0.85f};
	OzEd_Fill(r.x, r.y, fx - r.x, r.h, dim);
	OzEd_Fill(fx + fw, r.y, r.x + r.w - fx - fw, r.h, dim);
	OzEd_Fill(fx, r.y, fw, fy - r.y, dim);
	OzEd_Fill(fx, fy + fh, fw, r.y + r.h - fy - fh, dim);
	OzEd_Fill(fx, fy, fw, 1, edge);
	OzEd_Fill(fx, fy + fh - 1, fw, 1, edge);
	OzEd_Fill(fx, fy, 1, fh, edge);
	OzEd_Fill(fx + fw - 1, fy, 1, fh, edge);
}

}  // namespace oz::ed
