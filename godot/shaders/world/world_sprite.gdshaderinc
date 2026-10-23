// id Tech 3's deformVertexes autosprite / autosprite2 (tr_shade_calc.c): flares, flames and beams
// that turn to the camera. tools/build_maps.py writes each such quad as its own node, centered on
// the node, flat in local XY: autosprite quads are squares (uv 0,0 at local -x +y), autosprite2
// quads have their long axis along local Y.
//   sprite_mode 1: face the camera (Quake 3's AutoSprite)
//   sprite_mode 2: swivel around the long axis toward the camera (AutoSprite2)
uniform int sprite_mode = 1;

void vertex() {
	vec3 center = MODEL_MATRIX[3].xyz;
	vec3 cam = INV_VIEW_MATRIX[3].xyz;
	vec3 world;
	if (sprite_mode == 2) {
		vec3 axis = normalize(MODEL_MATRIX[1].xyz);
		float len = length(MODEL_MATRIX[1].xyz);
		vec3 to_cam = cam - center;
		vec3 side = cross(axis, to_cam);
		side = length(side) > 1e-5 ? normalize(side) : normalize(MODEL_MATRIX[0].xyz);
		world = center + side * VERTEX.x * length(MODEL_MATRIX[0].xyz) + axis * VERTEX.y * len;
	} else {
		vec3 right = INV_VIEW_MATRIX[0].xyz;
		vec3 up = INV_VIEW_MATRIX[1].xyz;
		float scale = length(MODEL_MATRIX[0].xyz);
		world = center + (right * VERTEX.x + up * VERTEX.y) * scale;
	}
	VERTEX = (inverse(MODEL_MATRIX) * vec4(world, 1.0)).xyz;
	NORMAL = (inverse(MODEL_MATRIX) * vec4(normalize(cam - world), 0.0)).xyz;
}
