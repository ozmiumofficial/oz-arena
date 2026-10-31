uniform sampler2D u_DiffuseMap;

uniform int       u_AlphaTest;

varying vec2      var_DiffuseTex;

varying vec4      var_Color;

// Oz: a fighter's corpse burning away, as in lightall_fp.glsl (its scripted surfaces draw here)
uniform float     u_OzDissolve;
varying vec3      var_OzPosition;

// Oz: ozGlow scales the color (above white, for bloom); ozMono turns the image that far to grey (its
// brightest channel, so a glow keeps its brightness) for the vertex color to color; ozSoft fades it
// out where it comes within u_OzSoft.x units of the scene behind it (the depth prepass's copy): its
// alpha, and its color too where u_OzSoft.y is 1 (not an alpha blend). u_ViewInfo: znear, zfar, 1/w, 1/h.
uniform float     u_OzGlow;
uniform float     u_OzMono;
uniform vec2      u_OzSoft;
uniform sampler2D u_ScreenDepthMap;
uniform vec4      u_ViewInfo;

// Oz: ozHeat bends the picture behind the sprite (u_ScreenImageMap, the scene drawn so far) with
// rising noise, strongest in its middle and fading out at its rim; u_OzHeat is the bend in screen
// heights at 48 units from the eye, times the vertex alpha (the effect's life).
uniform float     u_OzHeat;
uniform sampler2D u_ScreenImageMap;
uniform float     u_Time;


float OzLinearDepth(float d)
{
	float n = u_ViewInfo.x, f = u_ViewInfo.y;
	return n * f / (f - d * (f - n));
}

float OzHash(vec3 p)
{
	p = fract(p * 0.3183099 + vec3(0.1, 0.2, 0.3));
	p *= 17.0;
	return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float OzNoise(vec3 x)
{
	vec3 i = floor(x);
	vec3 f = fract(x);
	f = f * f * (3.0 - 2.0 * f);
	return mix(mix(mix(OzHash(i), OzHash(i + vec3(1.0, 0.0, 0.0)), f.x),
	               mix(OzHash(i + vec3(0.0, 1.0, 0.0)), OzHash(i + vec3(1.0, 1.0, 0.0)), f.x), f.y),
	           mix(mix(OzHash(i + vec3(0.0, 0.0, 1.0)), OzHash(i + vec3(1.0, 0.0, 1.0)), f.x),
	               mix(OzHash(i + vec3(0.0, 1.0, 1.0)), OzHash(i + vec3(1.0, 1.0, 1.0)), f.x), f.y), f.z);
}


void main()
{
	if (u_OzHeat > 0.0)
	{
		float rim = 1.0 - smoothstep(0.25, 1.0, length(var_DiffuseTex - 0.5) * 2.0);
		vec3 p = vec3(var_DiffuseTex * 5.0 + vec2(0.0, u_Time * 1.6), u_Time * 0.6);
		vec2 n = vec2(OzNoise(p), OzNoise(p + vec3(17.3, 5.1, 2.9))) - 0.5;
		float eye = OzLinearDepth(gl_FragCoord.z);
		float soft = 1.0;
		if (u_OzSoft.x > 0.0)
			soft = clamp((OzLinearDepth(texture2D(u_ScreenDepthMap, gl_FragCoord.xy * u_ViewInfo.zw).r) - eye) / u_OzSoft.x, 0.0, 1.0);
		float bend = u_OzHeat * var_Color.a * rim * soft * clamp(48.0 / eye, 0.0, 2.0);
		vec2 at = gl_FragCoord.xy * u_ViewInfo.zw + n * 2.0 * bend * vec2(u_ViewInfo.z / u_ViewInfo.w, 1.0);
		gl_FragColor = vec4(texture2D(u_ScreenImageMap, at).rgb, rim * soft);
		return;
	}

	float ozEdge = 0.0;
	if (u_OzDissolve > 0.0)
	{
		vec3 p = var_OzPosition * 0.22;
		float n = OzNoise(p) * 0.65 + OzNoise(p * 2.1 + vec3(7.3)) * 0.35;
		float cut = mix(0.2, 0.82, u_OzDissolve);
		if (n < cut)
			discard;
		ozEdge = 1.0 - smoothstep(cut, cut + 0.05, n);
	}

	vec4 color  = texture2D(u_DiffuseMap, var_DiffuseTex);
	color.rgb = mix(color.rgb, vec3(max(color.r, max(color.g, color.b))), u_OzMono);

	float alpha = color.a * var_Color.a;
	if (u_AlphaTest == 1)
	{
		if (alpha == 0.0)
			discard;
	}
	else if (u_AlphaTest == 2)
	{
		if (alpha >= 0.5)
			discard;
	}
	else if (u_AlphaTest == 3)
	{
		if (alpha < 0.5)
			discard;
	}
	
	gl_FragColor.rgb = color.rgb * var_Color.rgb + vec3(1.0, 0.42, 0.08) * (ozEdge * 3.0);
	gl_FragColor.a = alpha;

	gl_FragColor.rgb *= u_OzGlow;
	if (u_OzSoft.x > 0.0)
	{
		float scene = OzLinearDepth(texture2D(u_ScreenDepthMap, gl_FragCoord.xy * u_ViewInfo.zw).r);
		float soft = clamp((scene - OzLinearDepth(gl_FragCoord.z)) / u_OzSoft.x, 0.0, 1.0);
		gl_FragColor *= vec4(vec3(mix(1.0, soft, u_OzSoft.y)), soft);
	}
}
