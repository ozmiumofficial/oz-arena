// Oz: the Settings page's grade of the finished 3D view (RB_OzFinish): contrast around middle gray
// (Video > Contrast) and the colorblind filter (Accessibility > Colorblind), a daltonize: what the
// color deficiency loses is shifted into the colors it keeps.
uniform sampler2D u_DiffuseMap;
uniform vec4      u_Color;   // x: contrast, y: 0 off, 1 protanopia, 2 deuteranopia, 3 tritanopia

varying vec2      var_Tex1;

const mat3 RGB_TO_LMS = mat3(17.8824, 3.45565, 0.0299566,
                             43.5161, 27.1554, 0.184309,
                             4.11935, 3.86714, 1.46709);
const mat3 LMS_TO_RGB = mat3(0.0809444479, -0.0102485335, -0.000365296938,
                             -0.130504409, 0.0540193266, -0.00412161469,
                             0.116721066, -0.113614708, 0.693511405);

vec3 Daltonize(vec3 c, float type)
{
	vec3 lms = RGB_TO_LMS * c;
	vec3 sim;
	if (type < 1.5)        // protanopia: no long-wave cones
		sim = vec3(2.02344 * lms.y - 2.52581 * lms.z, lms.y, lms.z);
	else if (type < 2.5)   // deuteranopia: no medium-wave cones
		sim = vec3(lms.x, 0.494207 * lms.x + 1.24827 * lms.z, lms.z);
	else                   // tritanopia: no short-wave cones
		sim = vec3(lms.x, lms.y, -0.395913 * lms.x + 0.801109 * lms.y);
	vec3 err = c - LMS_TO_RGB * sim;
	return c + vec3(0.0, 0.7 * err.r + err.g, 0.7 * err.r + err.b);
}

void main()
{
	vec3 c = texture2D(u_DiffuseMap, var_Tex1).rgb;
	c = (c - vec3(0.5)) * u_Color.x + vec3(0.5);
	if (u_Color.y > 0.5)
		c = Daltonize(c, u_Color.y);
	gl_FragColor = vec4(clamp(c, 0.0, 1.0), 1.0);
}
