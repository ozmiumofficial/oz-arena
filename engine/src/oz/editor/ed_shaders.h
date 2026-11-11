#pragma once
// The game's shaders as the map editor needs them (ENGINE_PLAN.md P13): which image q3map2 takes a
// shader's size from (its qer_editorimage, else its first stage's map, else the image of its name),
// and that image's size in texels, which the Quake and Valve 220 texture forms count in.

#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace oz::ed {

// What the editor draws a face as, from its shader's script (q3map2's surfaceparms) and name.
enum class Look { Normal, Caulk, Clip, Trigger, Hint, Nodraw, Origin, Portal, Sky, Fog };

// What a material is, for the browser's kinds: one or more of these (a lit trim is a Trim and a Light).
// id's folders say it (base_floor and gothic_floor are Floors); a map pack's material, its name's words.
enum MatKind : uint32_t {
	MK_WALL = 1 << 0, MK_FLOOR = 1 << 1, MK_CEILING = 1 << 2, MK_TRIM = 1 << 3, MK_LIGHT = 1 << 4,
	MK_DOOR = 1 << 5, MK_SUPPORT = 1 << 6, MK_NATURE = 1 << 7, MK_LIQUID = 1 << 8, MK_GLASS = 1 << 9,
	MK_SKY = 1 << 10, MK_EFFECT = 1 << 11, MK_TEAM = 1 << 12, MK_TOOL = 1 << 13, MK_OTHER = 1 << 14,
	MK_COUNT = 15
};
const char *mat_kind_name(int bit);   // "walls", for commands (bit: 0 to MK_COUNT - 1)
const char *mat_kind_label(int bit);  // "Walls", for the chips (short words: the panel is narrow)
// id's two looks: base_ (Tech) and gothic_ (Gothic); the rest, a map pack's too, Other.
enum class MatStyle : uint8_t { Other, Tech, Gothic };

// A material the texture browser offers.
struct Material {
	std::string name;    // as a .map names it: "base_wall/concrete"
	std::string folder;  // "base_wall"
	std::string image;   // what its tile shows ("textures/base_wall/concrete.tga"); empty: a tool, drawn in its color
	bool script = false; // a shader script's, not a bare image
	bool stock = false;  // id's own (its script, or its image, in pak0 to pak8) or a tool shader; else a map pack's
	uint32_t kinds = 0;  // MatKind bits; none for Oz's dev textures (their own view)
	MatStyle style = MatStyle::Other;
};

class ShaderLib {
public:
	// Reads every scripts/*.shader in the game's search path (again after a mod or pak change).
	void load();
	// Texels of the shader's image as q3map2 sees it ("base_wall/concrete" or "textures/..."); 64 x 64
	// when it has none, as q3map2's default image.
	void size(const std::string &shader, int &w, int &h);
	size_t count() const { return images_.size(); }
	Look look(const std::string &shader);
	// A material players pass through though it's drawn like a wall (surfaceparm nonsolid: id's
	// killtrim_trans, vines, decals): q3map2 gives a brush its non-solid face's contents, so one such face
	// takes the whole brush's collision away. Tool materials (their own looks) aren't counted.
	bool nonsolid(const std::string &shader);
	// Every material a map can use, id's own first, then the map packs', each by folder and name: the
	// scripts' shaders under textures/, the images there without one, and the editor's tool shaders
	// (caulk, clip, hint ...: id's common.shader was an editor file, never in the paks).
	const std::vector<Material> &materials();
	// The image q3map2 and the browser show a shader by, and whether there is one (a .tga, .jpg, .png or
	// .dds): its editor image, its first stage's, any later stage's, a sky's box or the name's own.
	std::string image(const std::string &shader);
	bool found(const std::string &shader);
	// A map pack's material as its script defines it ("" for id's, a bare image's or an unknown one), and
	// every image it names: what a compile hands q3map2, which reads only id's paks.
	std::string pack_script(const std::string &shader);
	std::string script(const std::string &shader);  // any material's script text, "" for a bare image
	std::string oz_script(const std::string &shader);  // its scripts/*.ozmtr text (the oz renderer's), "" none
	std::vector<std::string> stage_images(const std::string &shader);
	// Any of its images is there (by name, without reading it): false for a shader that draws nothing.
	bool drawable(const std::string &shader);
	// It glows: a stage added over the rest (blendFunc add, GL_ONE GL_ONE), a light fixture's lit part.
	bool glows(const std::string &shader);
	// The light it gives the map in a compile (q3map_surfacelight), 0 for none.
	double surface_light(const std::string &shader);

	// ---- glow (Inspect's GLOW: a face's flags, ed_view.h face_glow). The material with its glow's brightness
	// and the light it casts at these percents of its own: "oz_glow/<brightness>_<light>/<material>" (a hash
	// of the material past the engine's 63 characters), made once. Its scripts are the material's own, its
	// glow stages brightened or dimmed and its q3map_surfacelight scaled (the oz renderer's version with
	// ozGlow, past white for its bloom; id's renderers stop at white), kept here as any material's, so
	// every call takes the copy; and written for the game, with every copy made before, into
	// <fs_homepath>/oz/scripts/oz_glow.shader and .ozmtr. The material's own name when it can't be made.
	std::string glow_variant(const std::string &material, int brightness, int light);
	bool glow_adjustable(const std::string &material);  // it glows or lights the map
	void copy_glow_scripts(const std::string &dir);     // the game's copies into another scripts folder (Defrag's)

private:
	bool image_size(const std::string &image, int &w, int &h);
	// the sizes read from the images' headers, kept in <fs_homepath>/oz/editor/sizes.cache while the
	// game's paks stay the same (reading 7,000 headers took most of the 4.6 s the browser's list did)
	void load_sizes();
	void save_sizes();
	bool sizes_dirty_ = false;
	void write_glow_scripts();
	std::map<std::string, std::pair<std::string, std::string>> glow_scripts_;  // a glow copy's name -> its two scripts
	bool glow_read_ = false;
	std::unordered_map<std::string, std::string> images_;  // lowercase shader name -> image path (q3map2's)
	std::unordered_map<std::string, std::string> firsts_;  // its first stage's image, for a missing editor image
	std::unordered_map<std::string, std::vector<std::string>> stages_;  // every stage's image, a sky's box sides
	std::unordered_map<std::string, std::string> blocks_;  // every shader's script text
	std::unordered_map<std::string, bool> packs_;           // a map pack's shaders (not id's)
	std::unordered_map<std::string, std::string> oz_blocks_;  // the .ozmtr scripts' text
	std::unordered_map<std::string, bool> exists_;          // an image (without extension): a file has it
	std::unordered_map<std::string, std::string> scripts_; // the scripts/ file that defines it
	std::unordered_map<std::string, int> parms_;           // lowercase shader name -> PARM_* bits
	std::unordered_map<std::string, double> lights_;       // q3map_surfacelight, where given
	std::unordered_map<std::string, std::pair<int, int>> sizes_;
	std::vector<Material> materials_;
	bool loaded_ = false, listed_ = false;
};

ShaderLib &shaders_lib();

}  // namespace oz::ed
