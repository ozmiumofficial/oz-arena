// Oz: one axis of the bloom's blur in one pass (RB_OzBloom): RB_BlurAxis's five taps, which it adds up in
// five blended blits
uniform sampler2D u_DiffuseMap;
uniform vec2      u_InvTexRes;   // from one tap to the next, in texture coordinates

varying vec2      var_Tex1;


void main()
{
	vec3 c = texture2D(u_DiffuseMap, var_Tex1).rgb * 0.227027027;
	c += (texture2D(u_DiffuseMap, var_Tex1 + u_InvTexRes * 1.3846153846).rgb +
		  texture2D(u_DiffuseMap, var_Tex1 - u_InvTexRes * 1.3846153846).rgb) * 0.316216216;
	c += (texture2D(u_DiffuseMap, var_Tex1 + u_InvTexRes * 3.2307692308).rgb +
		  texture2D(u_DiffuseMap, var_Tex1 - u_InvTexRes * 3.2307692308).rgb) * 0.070270270;
	gl_FragColor = vec4(c, 1.0);
}
