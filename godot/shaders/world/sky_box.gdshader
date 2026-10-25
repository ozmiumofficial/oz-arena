shader_type sky;
// id Tech 3 skybox (skyParms farbox env/<name>_rt/bk/lf/ft/up/dn) drawn from its six faces at run
// time, for maps the pipeline hasn't remastered (Q3ClassicMap). Same face table as
// tools/make_godot_world.py's panorama (tr_sky.c st_to_vec + sky_texorder): per face the depth
// axis, then the s and t axes, as (index, sign) in id axes; Godot -> id is x_q = Z, y_q = X, z_q = Y.
uniform sampler2D rt : source_color, filter_linear, repeat_disable;
uniform sampler2D lf : source_color, filter_linear, repeat_disable;
uniform sampler2D bk : source_color, filter_linear, repeat_disable;
uniform sampler2D ft : source_color, filter_linear, repeat_disable;
uniform sampler2D up : source_color, filter_linear, repeat_disable;
uniform sampler2D dn : source_color, filter_linear, repeat_disable;
uniform float energy = 1.0;

vec2 face_uv(float s, float t) {
	return vec2((s + 1.0) * 0.5, 1.0 - (t + 1.0) * 0.5);
}

void sky() {
	vec3 q = vec3(EYEDIR.z, EYEDIR.x, EYEDIR.y);
	vec3 a = abs(q);
	vec3 c;
	if (a.x >= a.y && a.x >= a.z) {
		if (q.x > 0.0) { c = texture(rt, face_uv(-q.y / a.x, q.z / a.x)).rgb; }    // rt: s = -y, t = z
		else { c = texture(lf, face_uv(q.y / a.x, q.z / a.x)).rgb; }               // lf: s = y, t = z
	} else if (a.y >= a.z) {
		if (q.y > 0.0) { c = texture(bk, face_uv(q.x / a.y, q.z / a.y)).rgb; }      // bk: s = x, t = z
		else { c = texture(ft, face_uv(-q.x / a.y, q.z / a.y)).rgb; }              // ft: s = -x, t = z
	} else {
		if (q.z > 0.0) { c = texture(up, face_uv(-q.y / a.z, -q.x / a.z)).rgb; }    // up: s = -y, t = -x
		else { c = texture(dn, face_uv(-q.y / a.z, q.x / a.z)).rgb; }              // dn: s = -y, t = x
	}
	COLOR = c * energy;
}
