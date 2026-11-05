// The map editor's compile and play-test (ENGINE_PLAN.md P13, engine/EDITOR.md), after the Godot
// build's q3_map_compiler.gd: q3map2 (BSP, vis, light) and mbspc (the bots' area file) run as programs
// of their own in the background, a stage at a time, while the mapper goes on editing. The map is
// compiled as it is in the editor (nothing is saved) under the name ed_<name>, into maps/ in the
// player's own files, so a play-test never shadows a stock map. A map open to the void (a leak) still
// builds and plays, without vis (q3map2 makes no portals for it), and the line q3map2 traced out to
// the void is drawn in the view. F5 plays the map once it's built.

#include "ed_view.h"
#include "ed_engine.h"
#include "ed_mapfile.h"

#include <algorithm>
#include <array>
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

// The map as compiled sits in a sky box: six sky brushes round everything in it (Sky and sun's sky: the
// one picked, else the first painted on a brush, else id's grey-clouded hellsky2), and under the map a
// hurt trigger to fall into, as id's void: it fills the box's bottom up to KILL_UNDER below the lowest
// thing in the map, so falling off an edge kills within a moment (Ozmium 2026-11-04). So a map open to the void shows sky there and seals, and builds
// with vis and the bots' file; a sealed map's box is outside it, and q3map2 drops it. The editor's map is
// left as it is: the box is only in the copy written for q3map2.
static const double SKY_SIDE = 512, SKY_TOP = 1024, SKY_BELOW = 512, SKY_THICK = 16, KILL_UNDER = 64;

// Sky and sun: the map's worldspawn keys, each left out while it has its default. The sky's light is the
// sun (its color, brightness, height over the horizon and direction, degrees as a light's angle) and the
// sky's own glow (q3map_surfacelight), in the sun's color. A map with a sky painted on its brushes and no
// settings (a stock map imported) keeps the sky's own light, as its script has it; any other is lit by
// the settings, whose defaults are a white sun. The default sky is id's hellsky2, grey clouds: most of
// id's cloud skies are red or orange, and toxicsky lit play-tests piss yellow (Ozmium 2026-11-04).
static const char *const K_SKY = "_oz_sky", *const K_LIGHT_FROM = "_oz_sky_light_from", *const K_COLOR = "_oz_sun_color",
	*const K_BRIGHT = "_oz_sun_brightness", *const K_HEIGHT = "_oz_sun_height", *const K_DIR = "_oz_sun_direction",
	*const K_GLOW = "_oz_sky_light", *const K_GLOW_COLOR = "_oz_sky_light_color";

// "r g b", 0 to 1 each (0 to 255 read as such), into out; false when it isn't three numbers
static bool read_rgb(const std::string &v, double out[3]) {
	double c[3];
	if (sscanf(v.c_str(), "%lf %lf %lf", &c[0], &c[1], &c[2]) != 3) {
		return false;
	}
	const double m = std::max({c[0], c[1], c[2]}) > 1 ? 255 : 1;
	for (int i = 0; i < 3; i++) {
		out[i] = std::max(0.0, std::min(1.0, c[i] / m));
	}
	return true;
}

static const struct {
	const char *name, *key;
	double lo, hi;
} SKY_NUMBERS[] = {
	{"brightness", K_BRIGHT, 0, 1000},
	{"height", K_HEIGHT, 1, 90},
	{"direction", K_DIR, 0, 359},
	{"sky_light", K_GLOW, 0, 1000},
};

// every skies/ material on the map's brushes and curves, in the order met
static std::vector<std::string> painted_skies(const Doc &d) {
	std::vector<std::string> out;
	auto note = [&](ShaderId id) {
		const std::string &n = shader_name(id);
		if (n.compare(0, 6, "skies/") == 0 && std::find(out.begin(), out.end(), n) == out.end()) {
			out.push_back(n);
		}
	};
	for (Id id : d.ids_of(Kind::Brush)) {
		for (const Face &f : d.get(id)->brush.faces) {
			note(f.shader);
		}
	}
	for (Id id : d.ids_of(Kind::Patch)) {
		note(d.get(id)->patch.shader);
	}
	return out;
}

static Session::SkySun sky_sun_of(const Doc &d) {
	Session::SkySun s;
	const ObjectPtr w = d.ptr(d.worldspawn());
	const Entity empty;
	const Entity &e = w ? w->entity : empty;
	const std::vector<std::string> painted = painted_skies(d);
	s.painted = painted.empty() ? "" : painted.front();
	const std::string chosen = e.get(K_SKY);
	s.chosen = !chosen.empty();
	s.sky = s.chosen ? chosen : !s.painted.empty() ? s.painted : Session::DEFAULT_SKY;
	const std::string from = e.get(K_LIGHT_FROM);
	bool any = s.chosen || !from.empty();
	double c[3];
	if (sscanf(e.get(K_COLOR).c_str(), "%lf %lf %lf", &c[0], &c[1], &c[2]) == 3) {
		any = true;
		const double m = std::max({c[0], c[1], c[2]}) > 1 ? 255 : 1;  // 0 to 255 read as such
		for (int i = 0; i < 3; i++) {
			s.color[i] = std::max(0.0, std::min(1.0, c[i] / m));
		}
	}
	double *num[4] = {&s.brightness, &s.height, &s.direction, &s.sky_light};
	for (int i = 0; i < 4; i++) {
		const std::string v = e.get(SKY_NUMBERS[i].key);
		if (!v.empty()) {
			any = true;
			*num[i] = std::max(SKY_NUMBERS[i].lo, std::min(SKY_NUMBERS[i].hi, std::atof(v.c_str())));
		}
	}
	s.own = from == "own" || (!any && !s.painted.empty());
	for (int i = 0; i < 3; i++) {
		s.glow[i] = s.color[i];
	}
	s.glow_set = read_rgb(e.get(K_GLOW_COLOR), s.glow);
	s.ambient = std::max(0.0, std::atof(e.get("ambient", "0").c_str()));
	read_rgb(e.get("_color"), s.ambient_color);
	return s;
}

Session::SkySun Session::sky_sun() const {
	return sky_sun_of(doc);
}

// The sky's light as q3map2 reads it for this compile: each sky the map uses, the box's among them, as
// its script but lit by the settings. Only q3map2 reads these (the compile's own scripts come before
// id's paks); the game draws the skies as their own scripts have them.
static std::string sky_light_shaders(const Doc &d) {
	const Session::SkySun s = sky_sun_of(d);
	if (s.own) {
		return "";
	}
	std::vector<std::string> skies = painted_skies(d);
	if (std::find(skies.begin(), skies.end(), s.sky) == skies.end()) {
		skies.push_back(s.sky);
	}
	char light[200];
	std::snprintf(light, sizeof light, "\tq3map_surfacelight %g\n\tq3map_lightRGB %g %g %g\n\tq3map_sun %g %g %g %g %g %g\n", s.sky_light,
		s.glow[0], s.glow[1], s.glow[2], s.color[0], s.color[1], s.color[2], s.brightness, s.direction, s.height);
	std::string out = "// Sky and sun's light for q3map2\n";
	for (const std::string &n : skies) {
		// an image q3map2 finds (id's), or it warns "Couldn't find image" for each
		out += "textures/" + n + "\n{\n\tqer_editorimage textures/skies/dimclouds.tga\n\tsurfaceparm noimpact\n\tsurfaceparm nolightmap\n\tsurfaceparm sky\n" +
			light + "\tskyparms - 512 -\n}\n";
	}
	return out;
}

static void add_sky_box(Doc &d, const Bounds &b) {
	const ShaderId s = shader_id(sky_sun_of(d).sky);
	const Vec3 lo = b.lo - Vec3(SKY_SIDE, SKY_SIDE, SKY_BELOW), hi = b.hi + Vec3(SKY_SIDE, SKY_SIDE, SKY_TOP);
	const Vec3 t(SKY_THICK, SKY_THICK, SKY_THICK);
	const Id w = d.worldspawn();
	d.add(box_brush(d, Vec3(lo.x - t.x, lo.y - t.y, lo.z - t.z), Vec3(hi.x + t.x, hi.y + t.y, lo.z), s, w));  // floor
	d.add(box_brush(d, Vec3(lo.x - t.x, lo.y - t.y, hi.z), Vec3(hi.x + t.x, hi.y + t.y, hi.z + t.z), s, w));  // top
	d.add(box_brush(d, Vec3(lo.x - t.x, lo.y - t.y, lo.z), Vec3(lo.x, hi.y + t.y, hi.z), s, w));
	d.add(box_brush(d, Vec3(hi.x, lo.y - t.y, lo.z), Vec3(hi.x + t.x, hi.y + t.y, hi.z), s, w));
	d.add(box_brush(d, Vec3(lo.x, lo.y - t.y, lo.z), Vec3(hi.x, lo.y, hi.z), s, w));
	d.add(box_brush(d, Vec3(lo.x, hi.y, lo.z), Vec3(hi.x, hi.y + t.y, hi.z), s, w));
	auto hurt = std::make_shared<Object>();
	hurt->kind = Kind::Entity;
	hurt->entity.set("classname", "trigger_hurt");
	hurt->entity.set("dmg", "9999");
	const Id h = d.add(hurt);
	// from the box's floor to just under the lowest thing in the map: off the edge, a fall of a moment kills
	d.add(box_brush(d, lo, Vec3(hi.x, hi.y, b.lo.z - KILL_UNDER), shader_id("common/trigger"), h));
}

// ed sky_sun opens the dialog; ed sky <field> <value> sets one (sky <material> | light_from own|editor |
// color <r g b> | brightness | height | direction | sky_light <n>; an empty value or "default" puts the
// default back), ed sky reset puts them all back. Each is one undo step on the map's own keys.
bool Session::sky_action(const std::string &action, const std::vector<std::string> &a) {
	if (action == "sky_sun") {
		want_sky = true;
		return true;
	}
	const std::vector<Id> world = {doc.worldspawn()};
	if (a.empty()) {
		const SkySun s = sky_sun();
		char buf[300];
		std::snprintf(buf, sizeof buf, "Sky %s%s; light %s: sun %g %g %g, brightness %g, height %g, direction %g, sky light %g", s.sky.c_str(),
			s.chosen ? "" : s.painted.empty() ? " (default)" : " (painted)", s.own ? "the sky's own" : "the editor's", s.color[0], s.color[1],
			s.color[2], s.brightness, s.height, s.direction, s.sky_light);
		say(buf);
		return true;
	}
	if (a[0] == "reset") {
		doc.begin("Sky and sun: defaults");
		for (const char *k : {K_SKY, K_LIGHT_FROM, K_COLOR, K_BRIGHT, K_HEIGHT, K_DIR, K_GLOW, K_GLOW_COLOR, "ambient", "_color"}) {
			remove_key(doc, world, k, "");
		}
		doc.end();
		say("Sky and sun: back to the defaults");
		return true;
	}
	std::string v;
	for (size_t i = 1; i < a.size(); i++) {
		v += (i > 1 ? " " : "") + a[i];
	}
	v.erase(std::remove(v.begin(), v.end(), '"'), v.end());
	const bool clear = v.empty() || v == "default";
	auto put = [&](const char *key, const std::string &value, const std::string &label) {
		if (clear) {
			remove_key(doc, world, key, label);
		} else {
			set_key(doc, world, key, value, label);
		}
	};
	if (a[0] == "sky") {
		std::string n = v.compare(0, 9, "textures/") == 0 ? v.substr(9) : v;
		if (!clear && (n.compare(0, 6, "skies/") != 0 || shaders_lib().look(n) != Look::Sky)) {
			say(n + " isn't a sky: pick one of the skies/ materials whose script makes it one", true);
			return true;
		}
		put(K_SKY, n, "Sky");
		say("Sky: " + sky_sun().sky);
	} else if (a[0] == "light_from") {
		if (!clear && v != "own" && v != "editor") {
			say("ed sky light_from own|editor", true);
			return true;
		}
		put(K_LIGHT_FROM, v, "Sky light from");
		say(sky_sun().own ? "The sky's own script lights the map" : "Sky and sun's settings light the map");
	} else if (a[0] == "color" || a[0] == "glow_color" || a[0] == "ambient_color") {
		double c[3];
		if (!clear && !read_rgb(v, c)) {
			say("ed sky " + a[0] + " <r g b>: 0 to 1 each", true);
			return true;
		}
		char norm[64] = "";
		if (!clear) {
			std::snprintf(norm, sizeof norm, "%g %g %g", c[0], c[1], c[2]);
		}
		put(a[0] == "color" ? K_COLOR : a[0] == "glow_color" ? K_GLOW_COLOR : "_color", clear ? v : std::string(norm),
			a[0] == "color" ? "Sun color" : a[0] == "glow_color" ? "Sky glow color" : "Ambient color");
	} else if (a[0] == "ambient") {
		char buf[32];
		std::snprintf(buf, sizeof buf, "%g", std::max(0.0, std::min(500.0, std::atof(v.c_str()))));
		if (clear || std::atof(v.c_str()) <= 0) {
			remove_key(doc, world, "ambient", "Ambient light");
		} else {
			set_key(doc, world, "ambient", buf, "Ambient light");
		}
	} else {
		int i = 0;
		while (i < 4 && a[0] != SKY_NUMBERS[i].name) {
			i++;
		}
		if (i == 4) {
			say("ed sky <sky | light_from | color | brightness | height | direction | sky_light | glow_color | ambient | ambient_color> <value>, or ed sky reset", true);
			return true;
		}
		char buf[32];
		double n = std::atof(v.c_str());
		if (a[0] == "direction") {
			n = std::fmod(std::fmod(n, 360.0) + 360.0, 360.0);
		}
		n = std::max(SKY_NUMBERS[i].lo, std::min(SKY_NUMBERS[i].hi, n));
		std::snprintf(buf, sizeof buf, "%g", n);
		put(SKY_NUMBERS[i].key, buf, std::string("Sun ") + SKY_NUMBERS[i].name);
	}
	return true;
}

// The sun's arrow while the dialog is open: from above the map's middle, along the light, to it
static const unsigned char C_SUN[4] = {255, 214, 120, 255}, C_SUN_HIDDEN[4] = {255, 214, 120, 120};

void Session::build_sun_lines() {
	if (!sky_open) {  // the map isn't looked through each frame while the dialog is closed
		if (!sun_built_.empty()) {
			sun_built_.clear();
			sun_lines_.begin();
			sun_lines_.end();
		}
		return;
	}
	const SkySun s = sky_sun();
	const Bounds b = bounds_of(doc, doc.ids());
	const Vec3 mid = b.empty() ? Vec3(0, 0, 0) : (b.lo + b.hi) * 0.5;
	char key[200];
	std::snprintf(key, sizeof key, "on %d %g %g %g %g %g %g", int(s.own), s.height, s.direction, mid.x, mid.y, mid.z,
		b.empty() ? 0.0 : (b.hi - b.lo).length());
	if (sun_built_ == key) {
		return;
	}
	sun_built_ = key;
	sun_lines_.begin();
	if (!s.own) {
		const double pi = 3.14159265358979323846, yaw = s.direction * pi / 180, pitch = s.height * pi / 180;
		const Vec3 to_sun(std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch));
		const double len = std::max(256.0, b.empty() ? 0.0 : (b.hi - b.lo).length() * 0.6);
		const Vec3 from = mid + to_sun * len;
		sun_lines_.line(from, mid, C_SUN);
		sun_lines_.line(from, mid, C_SUN_HIDDEN, "$oz_ed_lines_xray");
		// the head at the middle, and the sun as a box where the light comes from
		const Vec3 side = std::fabs(to_sun.z) > 0.99 ? Vec3(1, 0, 0) : Vec3(-to_sun.y, to_sun.x, 0).normalized();
		const Vec3 head = mid + to_sun * 32;
		sun_lines_.line(mid, head + side * 14, C_SUN, "$oz_ed_lines_xray");
		sun_lines_.line(mid, head - side * 14, C_SUN, "$oz_ed_lines_xray");
		sun_lines_.box_edges(from - Vec3(12, 12, 12), from + Vec3(12, 12, 12), C_SUN, "$oz_ed_lines_xray");
	}
	sun_lines_.end();
}

// A shader's script without its "surfaceparm nonsolid": every material collides unless its brush says not
static std::string without_nonsolid(const std::string &block) {
	std::string out;
	size_t at = 0;
	while (at < block.size()) {
		size_t eol = block.find('\n', at);
		const std::string line = block.substr(at, eol == std::string::npos ? std::string::npos : eol + 1 - at);
		at = eol == std::string::npos ? block.size() : eol + 1;
		std::string low = line;
		std::transform(low.begin(), low.end(), low.begin(), [](unsigned char c) { return char(std::tolower(c)); });
		const size_t p = low.find("surfaceparm");
		if (p != std::string::npos && low.find("nonsolid", p) != std::string::npos && low.find("//") > p) {
			continue;
		}
		out += line;
	}
	return out;
}

// A non-solid copy of a material for a brush whose collision is off: the material's own script with
// surfaceparm nonsolid, renamed oz_nonsolid/<name>, written to the BSP as the material itself
// (q3map_remapShader), so the game draws it as it is and lets players through.
static std::string nonsolid_copy(const std::string &name, const std::map<std::string, std::string> &made) {
	ShaderLib &lib = shaders_lib();
	auto own = made.find(name);
	const std::string block = own != made.end() ? own->second : lib.script(name);
	const size_t open = block.find('{');
	const std::string body = open != std::string::npos ? block.substr(open + 1) : "\n\tqer_editorimage " + lib.image(name) + "\n}\n";
	return "textures/oz_nonsolid/" + name + "\n{\n\tsurfaceparm nonsolid\n\tq3map_remapShader textures/" + name + body + "\n";
}

// A colored copy of a material (a face's Color): its script (the oz renderer's version, with the remastered
// images, when there is one), made solid as every material is, with a last stage multiplying it by the color;
// a bare image gets the script the renderer would make for it. The name is textures/oz_tint/<rrggbb>/<material>
// (or a hash of it, past the engine's 63 characters).
static std::string tint_name(const std::string &material, const unsigned char rgb[3]) {
	char hex[8];
	std::snprintf(hex, sizeof hex, "%02x%02x%02x", rgb[0], rgb[1], rgb[2]);
	std::string n = std::string("oz_tint/") + hex + "/" + material;
	if (n.size() + 9 > 63) {
		uint32_t h = 2166136261u;
		for (char c : material) {
			h = (h ^ (unsigned char)c) * 16777619u;
		}
		char tail[12];
		std::snprintf(tail, sizeof tail, "%08x", h);
		n = std::string("oz_tint/") + hex + "/" + tail;
	}
	return n;
}

static std::string tint_script(const std::string &material, const unsigned char rgb[3], const std::string &as) {
	ShaderLib &lib = shaders_lib();
	std::string block = lib.oz_script(material);
	if (block.empty()) {
		block = lib.script(material);
	}
	char stage[160];
	std::snprintf(stage, sizeof stage, "\t{\n\t\tmap $whiteimage\n\t\tblendFunc GL_DST_COLOR GL_ZERO\n\t\trgbGen const ( %g %g %g )\n\t}\n",
		rgb[0] / 255.0, rgb[1] / 255.0, rgb[2] / 255.0);
	std::string body;
	if (!block.empty()) {
		block = without_nonsolid(block);
		const size_t open = block.find('{'), close = block.rfind('}');
		if (open == std::string::npos || close == std::string::npos || close < open) {
			return "";
		}
		body = block.substr(open, close - open) + stage + "}\n";
	} else {
		std::string img = lib.image(material);
		const size_t dot = img.find_last_of('.'), slash = img.find_last_of('/');
		const std::string bare = dot != std::string::npos && (slash == std::string::npos || dot > slash) ? img.substr(0, dot) : img;
		body = "{\n\tqer_editorimage " + img + "\n\t{\n\t\tmap $lightmap\n\t\trgbGen identity\n\t}\n\t{\n\t\tmap " + bare +
			"\n\t\tblendFunc GL_DST_COLOR GL_ZERO\n\t\trgbGen identity\n\t}\n" + stage + "}\n";
	}
	return "textures/" + as + "\n" + body;
}

bool Session::compile(bool full, bool play, bool here, bool defrag) {
	if (job_.running) {
		say("A compile is under way: wait for it, or stop it");
		return false;
	}
	const std::string q3map2 = tool_exe("q3map2" OZ_ED_EXE), mbspc = tool_exe("mbspc" OZ_ED_EXE);
	const std::string baseq3 = OzEd_BaseQ3();
	if (!OzEd_FileExists(q3map2.c_str()) || baseq3.empty()) {
		say(baseq3.empty() ? "The game's baseq3 folder wasn't found" : "q3map2 isn't there (" + q3map2 + ")", true);
		return false;
	}
	// the editor's limit: everything within MAP_LIMIT of the middle every way
	const Bounds all = bounds_of(doc, doc.ids());
	if (!all.empty() && (std::max({-all.lo.x, -all.lo.y, -all.lo.z, all.hi.x, all.hi.y, all.hi.z}) > MAP_LIMIT)) {
		char buf[200];
		std::snprintf(buf, sizeof buf, "The map reaches past the editor's limit, %g units from the middle every way (its box: %g %g %g to %g %g %g): move it in",
			MAP_LIMIT, all.lo.x, all.lo.y, all.lo.z, all.hi.x, all.hi.y, all.hi.z);
		say(buf, true);
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
	Doc boxed = doc;
	boxed.on_change = nullptr;
	boxed.get_selection = nullptr;
	boxed.set_selection = nullptr;
	boxed.clear_history();
	add_sky_box(boxed, all.empty() ? Bounds{Vec3(-256, -256, -64), Vec3(256, 256, 256)} : all);
	// faces with a color: a colored copy of their material each (written below with the scripts, and for the
	// game beside the map), then brushes with their collision off: their materials' non-solid copies
	std::map<std::string, std::string> tint_scripts;
	boxed.begin("tint");
	for (Id id : boxed.ids_of(Kind::Brush)) {
		const Object *o = boxed.get(id);
		bool any = false;
		unsigned char rgb[3];
		for (const Face &f : o->brush.faces) {
			any |= face_tint(f, rgb);
		}
		if (!any) {
			continue;
		}
		auto e = boxed.edit(id);
		for (Face &f : e->brush.faces) {
			if (!face_tint(f, rgb)) {
				continue;
			}
			f.value = 0;
			const std::string sh = shader_name(f.shader);
			if (shaders_lib().look(sh) != Look::Normal) {
				continue;  // a tool material, a sky: never drawn as a wall
			}
			const std::string as = tint_name(sh, rgb);
			if (!tint_scripts.count(as)) {
				const std::string text = tint_script(sh, rgb, as);
				if (text.empty()) {
					continue;
				}
				tint_scripts[as] = text;
			}
			f.shader = shader_id(as);
		}
		boxed.put(id, e);
	}
	boxed.end();
	std::set<std::string> nonsolid_copies;
	boxed.begin("collision");
	for (Id id : boxed.ids_of(Kind::Brush)) {
		const Object *o = boxed.get(id);
		if (!is_nocollide(*o)) {
			continue;
		}
		auto e = boxed.edit(id);
		for (Face &f : e->brush.faces) {
			const std::string sh = shader_name(f.shader);
			if (shaders_lib().look(sh) == Look::Normal) {  // tool materials (clip, hint ...) keep their own
				nonsolid_copies.insert(sh);
				f.shader = shader_id("oz_nonsolid/" + sh);
			}
		}
		boxed.put(id, e);
	}
	boxed.end();
	const std::string text = write_map(boxed, wo);
	const std::string shaders = j.dir + "/home/baseq3/scripts/oz_editor_tools.shader";
	const std::string tool_shaders = std::string(TOOL_SHADERS) + sky_light_shaders(doc);
	OzEd_MakeDirs(j.map.c_str());
	OzEd_MakeDirs(shaders.c_str());
	if (!OzEd_WriteOSFile(j.map.c_str(), text.data(), int(text.size())) || !OzEd_WriteOSFile(shaders.c_str(), tool_shaders.data(), int(tool_shaders.size()))) {
		say("Can't write " + j.map, true);
		return false;
	}
	for (const char *ext : {".bsp", ".lin", ".aas", ".prt", ".srf"}) {
		std::remove((j.dir + "/maps/" + j.out + ext).c_str());  // a stage that fails leaves nothing of the last run
	}
	// the materials the map uses that q3map2 reads id's paks only for (below): a map pack's script blocks and
	// images go into the compile's own folder, where it finds them; without them a pack's water, lava or
	// glass compiled as a plain solid wall
	{
		std::set<std::string> used;
		const bool lit_here = !sky_sun().own;  // the settings light the skies: sky_light_shaders' scripts must win
		std::vector<const Object *> objects;  // what's written (sky box, copies) and what they were made from
		for (Id id : boxed.ids()) {
			objects.push_back(boxed.get(id));
		}
		for (Id id : doc.ids()) {
			objects.push_back(doc.get(id));
		}
		for (const Object *o : objects) {
			if (o->kind == Kind::Brush) {
				for (const Face &f : o->brush.faces) {
					used.insert(shader_name(f.shader));
				}
			} else if (o->kind == Kind::Patch) {
				used.insert(shader_name(o->patch.shader));
			}
		}
		std::string scripts = "// this map's materials as q3map2 is to read them: a map pack's, the non-solid ones made solid,\n"
			"// the non-solid copies of brushes with their collision off (the Oz Arena map editor)\n";
		const std::string home = j.dir + "/home/baseq3/";
		for (const std::string &u : used) {
			// every material collides: a non-solid one (id's killtrim_trans, vines, decals) goes in solid
			// (q3map2 takes the first script that names it: these come before the paks')
			std::string block = shaders_lib().nonsolid(u) ? without_nonsolid(shaders_lib().script(u)) : shaders_lib().pack_script(u);
			if (lit_here && shaders_lib().look(u) == Look::Sky) {
				block.clear();  // its images still go along, its script is the editor's lit one
			}
			scripts += block.empty() ? "" : block + "\n";
			for (const std::string &img : shaders_lib().stage_images(u)) {
				std::string base = img;
				const size_t dot = base.find_last_of('.'), slash = base.find_last_of('/');
				if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
					base.resize(dot);
				}
				for (const char *ext : {".tga", ".jpg", ".png", ".dds"}) {
					const std::string file = base + ext;
					void *buf = nullptr;
					if (OzE_InIdPak(file.c_str()) || !OzE_FileExists(file.c_str())) {
						continue;
					}
					const int len = OzE_ReadFile(file.c_str(), &buf);
					if (len > 0) {
						OzEd_MakeDirs((home + file).c_str());
						OzEd_WriteOSFile((home + file).c_str(), buf, len);
						OzE_FreeFile(buf);
					}
				}
			}
		}
		std::string tints;
		std::map<std::string, std::string> made;
		for (const auto &kv : tint_scripts) {
			tints += kv.second + "\n";
			const size_t nl = kv.second.find('\n');
			made[kv.first] = nl == std::string::npos ? kv.second : kv.second.substr(nl + 1);
		}
		scripts += tints;
		for (const std::string &n : nonsolid_copies) {
			scripts += nonsolid_copy(n, made);
		}
		// the colored copies for the game too: a script of the player's own beside the map, read when it loads
		const std::string game_scripts = std::string(OzEd_HomePath()) + "/oz/scripts/" + j.out + "_colors.shader";
		if (tints.empty()) {
			std::remove(game_scripts.c_str());
		} else {
			const std::string text = "// " + j.out + "'s face colors (the Oz Arena map editor)\n" + tints;
			OzEd_MakeDirs(game_scripts.c_str());
			OzEd_WriteOSFile(game_scripts.c_str(), text.data(), int(text.size()));
		}
		OzEd_WriteOSFile((home + "scripts/oz_editor_packs.shader").c_str(), scripts.data(), int(scripts.size()));
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
	warnings_ = map_problems();  // what the compile won't say: brushes without collision, pads that do nothing
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
		// kept at the top of the warnings' list, which opens: a status note alone was gone in 5 seconds
		CompileWarning w;
		w.text = "The compile failed: " + why;
		warnings_.insert(warnings_.begin(), w);
		want_warnings = true;
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
	if (j.leaked) {
		compile_report_ += " leak " + std::to_string(leak_.size());
	}
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
	if (j.leaked) {
		note += " \xc2\xb7 open to the void, so it's drawn whole without vis and has no bots (the red line shows one way out: seal it for both)";
	}
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
			// it plays anyway: q3map2 wrote the BSP, but with no inside to it there are no portals, so no vis,
			// and mbspc refuses a map that leaks, so no bots' file (it plays without bots)
			j.leaked = true;
			j.stages.erase(std::remove_if(j.stages.begin() + long(j.at) + 1, j.stages.end(),
				[](const CompileJob::Stage &st) { return st.name == "vis" || st.name == "bots"; }), j.stages.end());
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

std::vector<Session::CompileWarning> Session::map_problems() {
	std::vector<CompileWarning> out;
	// brushes stacked exactly on another (a duplicate never moved: q3map2 cuts both)
	std::map<std::vector<std::array<long long, 4>>, Id> seen;
	std::vector<Id> doubled;
	for (Id id : doc.ids_of(Kind::Brush)) {
		const Object *o = doc.get(id);
		const Object *owner = doc.get(o->owner);
		const std::string cn = owner ? owner->entity.classname() : "";
		if (o->owner != doc.worldspawn() && cn.compare(0, 5, "func_") != 0) {
			continue;  // triggers and the like aren't meant to be solid
		}
		std::vector<std::array<long long, 4>> key;  // its planes, each whole (a corner's two walls share numbers, not planes)
		for (const Face &f : o->brush.faces) {
			key.push_back({std::llround(f.plane.n.x * 1e4), std::llround(f.plane.n.y * 1e4), std::llround(f.plane.n.z * 1e4), std::llround(f.plane.d * 16)});
		}
		std::sort(key.begin(), key.end());
		auto it = seen.find(key);
		if (it != seen.end() && doc.get(it->second)) {
			doubled.push_back(id);
		} else {
			seen[key] = id;
		}
	}
	if (!doubled.empty()) {
		CompileWarning w;
		w.text = std::to_string(doubled.size()) + (doubled.size() == 1 ? " brush sits" : " brushes sit") +
			" exactly on another (a copy never moved): delete " + (doubled.size() == 1 ? "it" : "them");
		w.ids = doubled;
		out.push_back(std::move(w));
	}
	mover_problems(out);
	// jump pads, and targets that name nothing
	std::set<std::string> names;
	for (Id e : doc.ids_of(Kind::Entity)) {
		if (const std::string *n = doc.get(e)->entity.find("targetname")) {
			names.insert(*n);
		}
	}
	for (Id e : doc.ids_of(Kind::Entity)) {
		const Entity &en = doc.get(e)->entity;
		const std::string cn = en.classname();
		std::string why = push_problem(e);
		if (!why.empty()) {
			why = "A jump pad (" + cn + "): " + why;
		} else if (cn != "trigger_push" && cn != "target_push" && !en.get("target").empty() && !names.count(en.get("target"))) {
			why = "A " + cn + "'s target \"" + en.get("target") + "\" names nothing in the map";
		}
		if (!why.empty()) {
			CompileWarning w;
			w.text = why;
			w.ids = group_of(e);
			out.push_back(std::move(w));
		}
	}
	return out;
}

void Session::check_map() {
	warnings_ = map_problems();
	if (warnings_.empty()) {
		say("Nothing found: every jump pad throws, every target is there, no brush is doubled");
		return;
	}
	want_warnings = true;
	say(std::to_string(warnings_.size()) + (warnings_.size() == 1 ? " thing to look at" : " things to look at") + " (each framed when picked)", true);
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

bool Session::lights_changed_since() const {
	for (Id id : doc.ids_of(Kind::Entity)) {
		if (changed_since(id) && doc.get(id)->entity.classname() == "light") {
			return true;
		}
	}
	for (const auto &kv : compiled_) {
		const Object *was = kv.second.get();
		if (!doc.get(kv.first) && was && was->kind == Kind::Entity && was->entity.classname() == "light") {
			return true;  // taken out since
		}
	}
	return false;
}

void Session::preview_tick() {
	if (!previewing()) {
		preview_due_ = 0;
		preview_rev_ = doc.revision;
		return;
	}
	const int now = OzE_Milliseconds();
	if (doc.revision != preview_rev_) {
		preview_rev_ = doc.revision;
		preview_due_ = lights_changed_since() ? now + 400 : 0;
	}
	// a moment after the last change, not mid-drag, one compile at a time (a change during one waits for it)
	if (!preview_due_ || now < preview_due_ || dragging() || job_.running || import_.proc) {
		return;
	}
	preview_due_ = 0;
	if (lights_changed_since() && compile(false, false, false)) {
		job_.preview = true;
		say("The lights changed: compiling the lit preview again");
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
	if (import_.proc) {
		return "Opening " + import_.map + " (q3map2 makes it editable), " + std::to_string((OzE_Milliseconds() - import_.t0) / 1000) + " s";
	}
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
