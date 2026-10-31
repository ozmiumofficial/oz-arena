#pragma once
// The game's shaders as the map editor needs them (ENGINE_PLAN.md P13): which image q3map2 takes a
// shader's size from (its qer_editorimage, else its first stage's map, else the image of its name),
// and that image's size in texels, which the Quake and Valve 220 texture forms count in.

#include <string>
#include <unordered_map>
#include <vector>

namespace oz::ed {

// What the editor draws a face as, from its shader's script (q3map2's surfaceparms) and name.
enum class Look { Normal, Caulk, Clip, Trigger, Hint, Nodraw, Origin, Portal, Sky, Fog };

// A material the texture browser offers.
struct Material {
	std::string name;    // as a .map names it: "base_wall/concrete"
	std::string folder;  // "base_wall"
	std::string image;   // what its tile shows ("textures/base_wall/concrete.tga"); empty: a tool, drawn in its color
	bool script = false; // a shader script's, not a bare image
	bool stock = false;  // id's own (its script, or its image, in pak0 to pak8) or a tool shader; else a map pack's
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
	// Every material a map can use, id's own first, then the map packs', each by folder and name: the
	// scripts' shaders under textures/, the images there without one, and the editor's tool shaders
	// (caulk, clip, hint ...: id's common.shader was an editor file, never in the paks).
	const std::vector<Material> &materials();
	// The image q3map2 and the browser show a shader by, and whether there is one (a .tga, .jpg or .png).
	std::string image(const std::string &shader);
	bool found(const std::string &shader);

private:
	bool image_size(const std::string &image, int &w, int &h);
	std::unordered_map<std::string, std::string> images_;  // lowercase shader name -> image path (q3map2's)
	std::unordered_map<std::string, std::string> firsts_;  // its first stage's image, for a missing editor image
	std::unordered_map<std::string, std::string> scripts_; // the scripts/ file that defines it
	std::unordered_map<std::string, int> parms_;           // lowercase shader name -> PARM_* bits
	std::unordered_map<std::string, std::pair<int, int>> sizes_;
	std::vector<Material> materials_;
	bool loaded_ = false, listed_ = false;
};

ShaderLib &shaders_lib();

}  // namespace oz::ed
