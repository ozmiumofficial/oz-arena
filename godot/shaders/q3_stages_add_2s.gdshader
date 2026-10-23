shader_type spatial;
// Quake 3 model shader stages, additive, two-sided
render_mode blend_add, unshaded, depth_draw_never, cull_disabled, shadows_disabled;
#define Q3_ADDITIVE
#include "res://shaders/q3_stages.gdshaderinc"
