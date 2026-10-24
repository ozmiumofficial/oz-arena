shader_type spatial;
// Quake 3 model shader stages, additive (blendFunc GL_ONE GL_ONE), unlit
render_mode blend_add, unshaded, depth_draw_never, cull_back, shadows_disabled, fog_disabled;
#define Q3_ADDITIVE
#include "res://shaders/q3_stages.gdshaderinc"
