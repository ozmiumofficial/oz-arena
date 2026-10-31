uniform sampler2D u_TextureMap;
uniform sampler2D u_LevelsMap;

uniform vec4      u_Color;


uniform vec2      u_AutoExposureMinMax;
uniform vec3      u_ToneMinAvgMaxLinear;

varying vec2      var_TexCoords;
varying float     var_InvWhite;

const vec3  LUMINANCE_VECTOR =   vec3(0.2125, 0.7154, 0.0721); //vec3(0.299, 0.587, 0.114);

float FilmicTonemap(float x)
{
	const float SS  = 0.22; // Shoulder Strength
	const float LS  = 0.30; // Linear Strength
	const float LA  = 0.10; // Linear Angle
	const float TS  = 0.20; // Toe Strength
	const float TAN = 0.01; // Toe Angle Numerator
	const float TAD = 0.30; // Toe Angle Denominator

	return ((x*(SS*x+LA*LS)+TS*TAN)/(x*(SS*x+LS)+TS*TAD)) - TAN/TAD;
}

#if defined(USE_OZ_TONEMAP)
// Oz (ENGINE_PLAN.md P1): the scene is already in display values (lightall writes the square
// root of linear light), and they stay as they are up to the knee: no toe and no black point,
// which crushed the maps' shadows. Above it, a soft shoulder takes the overbright range to white
// where id's renderer clipped it.
vec3 OzShoulder(vec3 c)
{
	const float knee = 0.8;
	vec3 over = max(c - vec3(knee), vec3(0.0));
	return min(c, vec3(knee)) + (1.0 - knee) * (vec3(1.0) - exp(-over / (1.0 - knee)));
}
#endif

void main()
{
	vec4 color = texture2D(u_TextureMap, var_TexCoords) * u_Color;

#if defined(USE_OZ_TONEMAP)
	// exposure as a plain scale: 1 with the fixed levels (u_Color 2, invAvgLum 0.5)
	vec3 ozLevels = texture2D(u_LevelsMap, var_TexCoords).rgb;
	float ozLogAvg = clamp(ozLevels.y * 20.0 - 10.0, -u_AutoExposureMinMax.y, -u_AutoExposureMinMax.x);
	color.rgb = OzShoulder(max(color.rgb * u_ToneMinAvgMaxLinear.y * exp2(-ozLogAvg), vec3(0.0)));
	color.rgb += vec3(1.0/510.0 * mod(gl_FragCoord.x + gl_FragCoord.y, 2.0) - 1.0/1020.0);
	gl_FragColor = color;
	return;
#endif

#if defined(USE_PBR)
	color.rgb *= color.rgb;
#endif

	vec3 minAvgMax = texture2D(u_LevelsMap, var_TexCoords).rgb;
	vec3 logMinAvgMaxLum = clamp(minAvgMax * 20.0 - 10.0, -u_AutoExposureMinMax.y, -u_AutoExposureMinMax.x);

	float invAvgLum = u_ToneMinAvgMaxLinear.y * exp2(-logMinAvgMaxLum.y);

	color.rgb = color.rgb * invAvgLum - u_ToneMinAvgMaxLinear.xxx;
	color.rgb = max(vec3(0.0), color.rgb);

	color.r = FilmicTonemap(color.r);
	color.g = FilmicTonemap(color.g);
	color.b = FilmicTonemap(color.b);

	color.rgb = clamp(color.rgb * var_InvWhite, 0.0, 1.0);

#if defined(USE_PBR)
	color.rgb = sqrt(color.rgb);
#endif

	// add a bit of dither to reduce banding
	color.rgb += vec3(1.0/510.0 * mod(gl_FragCoord.x + gl_FragCoord.y, 2.0) - 1.0/1020.0);

	gl_FragColor = color;
}
