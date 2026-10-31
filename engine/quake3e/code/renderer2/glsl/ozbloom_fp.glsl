// Oz: bloom's bright pass (RB_OzBloom): what's over the threshold, scaled
uniform sampler2D u_DiffuseMap;
uniform vec4      u_Color;   // rgb: scale, a: threshold

varying vec2      var_Tex1;


void main()
{
	vec3 c = texture2D(u_DiffuseMap, var_Tex1).rgb;
	gl_FragColor = vec4(max(c - vec3(u_Color.a), vec3(0.0)) * u_Color.rgb, 1.0);
}
