/*
Oz: the main menu's showcase (ENGINE_PLAN.md P5), the Godot build's showcase_stage.gd on the engine:
one subject alone in a dark smoky void, turning slowly and bobbing, while the camera eases between
a few framings (three-quarter, low hero, close, wide) and drifts. A warm key from above, the accent
color's rim from behind, faint embers rising, big soft smoke puffs drifting through. Every 15-25 s the subject changes
(weapons/change.wav): mostly one of the characters standing idle, sometimes a weapon or a powerup.

Drawn by the native UI before its page layer (OzUI_Draw), as q3_ui drew its player model: a refdef
with no world, after a full-screen fill. id's md3 fighters for now; P2's rigged ones replace them.
Compiled with the client, like oz_ui_engine.c.
*/
#include "../../../quake3e/code/client/client.h"
#include "oz_ui_engine.h"

#define SWAP_MIN		15000
#define SWAP_MAX		25000
#define FADE_MS			1200
#define MAX_CHARACTERS	64
#define NUM_PUFFS		10
#define NUM_EMBERS		90
#define EMBER_LIFE		7.0f		// seconds

typedef struct {
	const char	*name, *kind, *model, *extra;	// extra: a second model at the same place (the mega health's sphere)
	float		spin;							// powerups spin by themselves (cg_ents.c), degrees a second
} showItem_t;

static const showItem_t items[] = {
	{ "Rocket Launcher", "Weapon", "models/weapons2/rocketl/rocketl.md3", NULL, 0 },
	{ "Railgun", "Weapon", "models/weapons2/railgun/railgun.md3", NULL, 0 },
	{ "Lightning Gun", "Weapon", "models/weapons2/lightning/lightning.md3", NULL, 0 },
	{ "Plasma Gun", "Weapon", "models/weapons2/plasma/plasma.md3", NULL, 0 },
	{ "Shotgun", "Weapon", "models/weapons2/shotgun/shotgun.md3", NULL, 0 },
	{ "Quad Damage", "Powerup", "models/powerups/instant/quad.md3", "models/powerups/instant/quad_ring.md3", 0 },
	{ "Regeneration", "Powerup", "models/powerups/instant/regen.md3", "models/powerups/instant/regen_ring.md3", 0 },
	{ "Mega Health", "Health", "models/powerups/health/mega_cross.md3", "models/powerups/health/mega_sphere.md3", 0 },
	{ "Heavy Armor", "Armor", "models/powerups/armor/armor_red.md3", NULL, 0 },
};

// framings: yaw and elevation in degrees around the subject, distance in subject heights, aim
// (0 feet, 1 top), as showcase_stage.gd's FRAMINGS
static const float framings[4][4] = {
	{ 32.0f, 7.0f, 1.75f, 0.56f },		// three-quarter
	{ -24.0f, -11.0f, 1.45f, 0.62f },	// low hero
	{ 14.0f, 4.0f, 0.78f, 0.84f },		// close
	{ -38.0f, 13.0f, 2.5f, 0.5f },		// wide
};

typedef struct {
	int		first, num, fps;
} anim_t;

static struct {
	int			generation;			// renderer handles are good while this matches
	char		characters[MAX_CHARACTERS][MAX_QPATH];
	int			numCharacters;
	qboolean	listed;

	// the subject
	char		name[64], kind[32];
	qboolean	isCharacter;
	qhandle_t	model[3], skin[3], weapon;	// legs, torso, head (a character), or model[0] and model[1] (an item)
	anim_t		legs, torso;
	float		height, center[3];		// in game units, the subject's middle
	const showItem_t *item;

	int			start;					// when the subject came in
	int			next;					// when it changes
	int			lastTime;
	float		t;						// drifting time, seconds
	int			frame, fromFrame;
	float		frameT, frameIn;
	qhandle_t	smoke, flare, white;
	unsigned	seed;
} sc;

static int Rand( void ) {
	sc.seed = sc.seed * 1103515245u + 12345u;
	return (int)( ( sc.seed >> 16 ) & 0x7fff );
}

// 0..1 from an index and a salt, the same every frame (an ember's place and speed for one life)
static float Hash( int i, int salt ) {
	unsigned h = (unsigned)i * 2654435761u ^ (unsigned)salt * 40503u;

	h ^= h >> 15;
	h *= 2246822519u;
	h ^= h >> 13;
	return ( h & 0xffff ) / 65535.0f;
}

static float Smooth( float x ) {
	x = x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x;
	return x * x * ( 3.0f - 2.0f * x );
}

// every models/players/<name> with the three parts
static void ListCharacters( void ) {
	char **dirs;
	int n, i;

	sc.listed = qtrue;
	sc.numCharacters = 0;
	dirs = FS_ListFiles( "models/players", "/", &n );
	for ( i = 0; i < n && sc.numCharacters < MAX_CHARACTERS; i++ ) {
		const size_t len = strlen( dirs[i] );
		if ( len && dirs[i][len - 1] == '/' ) {	// directories come with their slash
			dirs[i][len - 1] = '\0';
		}
		if ( !dirs[i][0] || dirs[i][0] == '.' ) {
			continue;
		}
		if ( FS_ReadFile( va( "models/players/%s/lower.md3", dirs[i] ), NULL ) > 0 &&
			FS_ReadFile( va( "models/players/%s/head.md3", dirs[i] ), NULL ) > 0 ) {
			Q_strncpyz( sc.characters[sc.numCharacters++], dirs[i], MAX_QPATH );
		}
	}
	FS_FreeFileList( dirs );
}

/*
animation.cfg as cg_players.c reads it: header keywords, then first, count, loop and fps per
animation in animNumber_t order; the legs' frames follow the torso's in the file.
*/
static void ParseAnimations( const char *model ) {
	char *text;
	const char *p, *tok;
	int nums[25][4], n = 0, i, skip;

	sc.legs.first = sc.torso.first = 0;
	sc.legs.num = sc.torso.num = 1;
	sc.legs.fps = sc.torso.fps = 15;
	if ( FS_ReadFile( va( "models/players/%s/animation.cfg", model ), (void **)&text ) <= 0 ) {
		return;
	}
	p = text;
	while ( n < 25 ) {
		tok = COM_Parse( &p );
		if ( !tok[0] ) {
			break;
		}
		if ( !( tok[0] >= '0' && tok[0] <= '9' ) ) {
			if ( !Q_stricmp( tok, "headoffset" ) ) {
				COM_Parse( &p ); COM_Parse( &p ); COM_Parse( &p );
			} else if ( !Q_stricmp( tok, "sex" ) || !Q_stricmp( tok, "footsteps" ) ) {
				COM_Parse( &p );
			}
			continue;
		}
		nums[n][0] = atoi( tok );
		for ( i = 1; i < 4; i++ ) {
			nums[n][i] = atoi( COM_Parse( &p ) );
		}
		n++;
	}
	FS_FreeFile( text );
	if ( n < 25 ) {
		return;
	}
	skip = nums[13][0] - nums[6][0];	// LEGS_WALKCR - TORSO_GESTURE
	sc.torso.first = nums[11][0];		// TORSO_STAND
	sc.torso.num = nums[11][1] > 0 ? nums[11][1] : 1;
	sc.torso.fps = nums[11][3] > 0 ? nums[11][3] : 15;
	sc.legs.first = nums[22][0] - skip;	// LEGS_IDLE
	sc.legs.num = nums[22][1] > 0 ? nums[22][1] : 1;
	sc.legs.fps = nums[22][3] > 0 ? nums[22][3] : 15;
}

static void Bounds( qhandle_t model, vec3_t mins, vec3_t maxs ) {
	re.ModelBounds( model, mins, maxs );
	if ( mins[0] >= maxs[0] ) {
		VectorSet( mins, -16, -16, -16 );
		VectorSet( maxs, 16, 16, 16 );
	}
}

static void Pick( void ) {
	vec3_t mins, maxs;
	int character = sc.numCharacters > 0 && ( Rand() % 100 ) < 70;
	const char *forced = Cvar_VariableString( "ui_showcaseSubject" );	// captures: a character's model or an item's name
	int i;

	if ( forced[0] ) {
		for ( i = 0; i < sc.numCharacters; i++ ) {
			if ( !Q_stricmp( sc.characters[i], forced ) ) {
				break;
			}
		}
		character = i < sc.numCharacters;
	}
	static const char *held[] = { "railgun/railgun", "rocketl/rocketl", "plasma/plasma", "lightning/lightning", "shotgun/shotgun", "machinegun/machinegun" };

	sc.isCharacter = character;
	sc.item = NULL;
	Com_Memset( sc.model, 0, sizeof( sc.model ) );
	Com_Memset( sc.skin, 0, sizeof( sc.skin ) );
	sc.weapon = 0;
	if ( character ) {
		const char *m = forced[0] ? forced : sc.characters[Rand() % sc.numCharacters];
		sc.model[0] = re.RegisterModel( va( "models/players/%s/lower.md3", m ) );
		sc.model[1] = re.RegisterModel( va( "models/players/%s/upper.md3", m ) );
		sc.model[2] = re.RegisterModel( va( "models/players/%s/head.md3", m ) );
		sc.skin[0] = re.RegisterSkin( va( "models/players/%s/lower_default.skin", m ) );
		sc.skin[1] = re.RegisterSkin( va( "models/players/%s/upper_default.skin", m ) );
		sc.skin[2] = re.RegisterSkin( va( "models/players/%s/head_default.skin", m ) );
		sc.weapon = re.RegisterModel( va( "models/weapons2/%s.md3", held[Rand() % ARRAY_LEN( held )] ) );
		ParseAnimations( m );
		Q_strncpyz( sc.name, m, sizeof( sc.name ) );
		Q_strupr( sc.name );
		Q_strncpyz( sc.kind, "Character", sizeof( sc.kind ) );
		sc.height = 56.0f;			// a player's box, feet at -24
		VectorSet( sc.center, 0, 0, 4 );
	} else {
		sc.item = &items[Rand() % ARRAY_LEN( items )];
		for ( i = 0; forced[0] && i < (int)ARRAY_LEN( items ); i++ ) {
			if ( !Q_stricmp( items[i].name, forced ) ) {
				sc.item = &items[i];
			}
		}
		sc.model[0] = re.RegisterModel( sc.item->model );
		sc.model[1] = sc.item->extra ? re.RegisterModel( sc.item->extra ) : 0;
		Q_strncpyz( sc.name, sc.item->name, sizeof( sc.name ) );
		Q_strncpyz( sc.kind, sc.item->kind, sizeof( sc.kind ) );
		Bounds( sc.model[0], mins, maxs );
		if ( sc.model[1] ) {	// the ring or sphere around it counts too
			vec3_t m2, x2;
			Bounds( sc.model[1], m2, x2 );
			for ( i = 0; i < 3; i++ ) {
				mins[i] = mins[i] < m2[i] ? mins[i] : m2[i];
				maxs[i] = maxs[i] > x2[i] ? maxs[i] : x2[i];
			}
		}
		sc.height = maxs[2] - mins[2];
		for ( i = 0; i < 2; i++ ) {
			if ( maxs[i] - mins[i] > sc.height ) {
				sc.height = maxs[i] - mins[i];
			}
		}
		// an item floats smaller in the frame than a fighter (the Godot showcase's items sat in about half the height)
		sc.height = ( sc.height < 16.0f ? 16.0f : sc.height ) * 1.7f;
		VectorAdd( mins, maxs, sc.center );
		VectorScale( sc.center, 0.5f, sc.center );
	}
	sc.start = Sys_Milliseconds();
	sc.next = sc.start + SWAP_MIN + Rand() % ( SWAP_MAX - SWAP_MIN );
	sc.fromFrame = sc.frame;
	sc.frame = Rand() % 4;
	sc.frameT = 1.0f;
	sc.frameIn = 9.0f + ( Rand() % 400 ) * 0.01f;
	if ( forced[0] ) {	// a capture: it stays, three-quarter framed
		sc.next = sc.start + 0x3fffffff;
		sc.frame = sc.fromFrame = 0;
		sc.frameIn = 1e9f;
	}
}

static void Register( void ) {
	sc.generation = OzE_RendererGeneration();
	sc.smoke = re.RegisterShader( "smokePuff" );
	sc.flare = re.RegisterShader( "flareShader" );	// additive, vertex colored
	sc.white = re.RegisterShader( "white" );
	if ( !sc.listed ) {
		ListCharacters();
	}
	if ( !sc.seed ) {
		sc.seed = (unsigned)Sys_Milliseconds() | 1u;
	}
	Pick();
}

const char *OzE_ShowcaseSubject( const char **kind ) {
	*kind = sc.kind;
	return sc.name;
}

void OzE_ShowcaseNext( void ) {
	if ( sc.generation == OzE_RendererGeneration() ) {
		Pick();
	}
}

static void Lerp( anim_t *a, int ms, refEntity_t *e ) {
	const float f = ms * 0.001f * a->fps;
	const int i = (int)f;

	e->frame = a->first + ( i + 1 ) % a->num;
	e->oldframe = a->first + i % a->num;
	e->backlerp = 1.0f - ( f - i );
}

static void Attach( refEntity_t *e, const refEntity_t *parent, qhandle_t parentModel, const char *tag ) {
	orientation_t o;
	int i;

	re.LerpTag( &o, parentModel, parent->oldframe, parent->frame, 1.0f - parent->backlerp, tag );
	VectorCopy( parent->origin, e->origin );
	for ( i = 0; i < 3; i++ ) {
		VectorMA( e->origin, o.origin[i], parent->axis[i], e->origin );
	}
	MatrixMultiply( o.axis, ( (refEntity_t *)parent )->axis, e->axis );
	VectorCopy( e->origin, e->oldorigin );
}

/*
OzE_ShowcaseDraw: the void and the subject over the whole screen, before the page layer. fade
darkens it (pages open over the menu dim it), accent is the rim light's color.
*/
void OzE_ShowcaseDraw( int width, int height, const float *accent ) {
	refdef_t rd;
	refEntity_t ent, legs, torso, head, gun;
	vec3_t angles, target, dir, right, up;
	float yaw, elev, dist, aim, k, size, swapFade;
	const float *from, *to;
	float f[4];
	static const float voidColor[4] = { 0.012f, 0.010f, 0.011f, 1.0f };
	int now, ms, i;

	if ( !cls.rendererStarted ) {
		return;
	}
	if ( sc.generation != OzE_RendererGeneration() ) {
		Register();
	}
	now = Sys_Milliseconds();
	if ( sc.lastTime ) {
		sc.t += ( now - sc.lastTime ) * 0.001f;
		sc.frameT += ( now - sc.lastTime ) * 0.001f / 6.0f;
		sc.frameIn -= ( now - sc.lastTime ) * 0.001f;
	}
	sc.lastTime = now;
	if ( now >= sc.next ) {
		S_StartLocalSound( S_RegisterSound( "sound/weapons/change.wav", qfalse ), CHAN_LOCAL_SOUND );
		Pick();
	}
	if ( sc.frameIn <= 0.0f ) {
		sc.fromFrame = sc.frame;
		sc.frame = ( sc.frame + 1 + Rand() % 3 ) % 4;
		sc.frameT = 0.0f;
		sc.frameIn = 9.0f + ( Rand() % 400 ) * 0.01f;
	}

	re.SetColor( voidColor );
	re.DrawStretchPic( 0, 0, width, height, 0, 0, 1, 1, sc.white );
	re.SetColor( NULL );

	// the framing, eased from the last one, drifting
	from = framings[sc.fromFrame];
	to = framings[sc.frame];
	k = Smooth( sc.frameT );
	for ( i = 0; i < 4; i++ ) {
		f[i] = from[i] + ( to[i] - from[i] ) * k;
	}
	size = sc.height;
	yaw = DEG2RAD( f[0] + 6.0f * sinf( sc.t * 0.07f ) );
	elev = DEG2RAD( f[1] + 3.0f * sinf( sc.t * 0.05f + 1.0f ) );
	dist = f[2] * size * ( 1.0f + 0.05f * sinf( sc.t * 0.09f ) ) + 24.0f;
	aim = sc.isCharacter ? f[3] : 0.5f;
	VectorCopy( sc.center, target );
	target[2] += ( aim - 0.5f ) * size + 2.0f * sinf( sc.t * 0.8f );	// the bob
	// the game's axes: x forward, y left, z up; the camera sits in front of the subject (+x)
	dir[0] = cosf( yaw ) * cosf( elev );
	dir[1] = sinf( yaw ) * cosf( elev );
	dir[2] = sinf( elev );

	Com_Memset( &rd, 0, sizeof( rd ) );
	rd.x = 0;
	rd.y = 0;
	rd.width = width;
	rd.height = height;
	rd.fov_y = 34.0f;
	rd.fov_x = RAD2DEG( 2.0f * atanf( tanf( DEG2RAD( rd.fov_y ) * 0.5f ) * width / (float)height ) );
	VectorMA( target, dist, dir, rd.vieworg );
	VectorNegate( dir, angles );
	vectoangles( angles, angles );
	// the subject in the middle of the right two-thirds of the screen: turn the view left by the
	// angle that puts the target a third of the way from the center to the right edge
	angles[YAW] += RAD2DEG( atanf( tanf( DEG2RAD( rd.fov_x ) * 0.5f ) / 3.0f ) );
	AnglesToAxis( angles, rd.viewaxis );
	rd.time = now;
	rd.rdflags = RDF_NOWORLDMODEL;

	re.ClearScene();

	Com_Memset( &ent, 0, sizeof( ent ) );
	// facing the camera when it comes in, then turning once in 40 s
	VectorSet( angles, 0, f[0] - 25.0f + ( now - sc.start ) * 0.009f, 0 );
	AnglesToAxis( angles, ent.axis );
	ent.renderfx = RF_LIGHTING_ORIGIN | RF_NOSHADOW;
	ent.shader.rgba[0] = ent.shader.rgba[1] = ent.shader.rgba[2] = ent.shader.rgba[3] = 255;
	VectorSet( ent.origin, 0, 0, 2.0f * sinf( sc.t * 0.8f ) );
	VectorCopy( ent.origin, ent.lightingOrigin );
	VectorCopy( ent.origin, ent.oldorigin );
	ms = now - sc.start;
	swapFade = Smooth( ( now - sc.start ) / (float)FADE_MS );

	if ( sc.isCharacter ) {
		legs = ent;
		legs.hModel = sc.model[0];
		legs.customSkin = sc.skin[0];
		Lerp( &sc.legs, ms, &legs );
		re.AddRefEntityToScene( &legs, qfalse );

		torso = ent;
		torso.hModel = sc.model[1];
		torso.customSkin = sc.skin[1];
		Lerp( &sc.torso, ms, &torso );
		Attach( &torso, &legs, sc.model[0], "tag_torso" );
		re.AddRefEntityToScene( &torso, qfalse );

		head = ent;
		head.hModel = sc.model[2];
		head.customSkin = sc.skin[2];
		Attach( &head, &torso, sc.model[1], "tag_head" );
		re.AddRefEntityToScene( &head, qfalse );

		if ( sc.weapon ) {
			gun = ent;
			gun.hModel = sc.weapon;
			gun.frame = gun.oldframe = 0;
			gun.backlerp = 0;
			Attach( &gun, &torso, sc.model[1], "tag_weapon" );
			re.AddRefEntityToScene( &gun, qfalse );
		}
	} else {
		vec3_t p;
		ent.hModel = sc.model[0];
		// turn about the model's middle: its center stays where it is (plus the bob)
		VectorAdd( sc.center, ent.origin, p );
		for ( i = 0; i < 3; i++ ) {
			VectorMA( p, -sc.center[i], ent.axis[i], p );
		}
		VectorCopy( p, ent.origin );
		VectorCopy( p, ent.oldorigin );
		VectorCopy( p, ent.lightingOrigin );
		re.AddRefEntityToScene( &ent, qfalse );
		if ( sc.model[1] ) {
			ent.hModel = sc.model[1];
			re.AddRefEntityToScene( &ent, qfalse );
		}
	}

	// lights follow the camera: a warm key from above-front-left, the accent's rim from behind,
	// a faint cool fill
	VectorSet( up, 0, 0, 1 );
	CrossProduct( dir, up, right );
	VectorNormalize( right );
	{
		vec3_t p;
		VectorMA( target, size * 1.8f, up, p );
		VectorMA( p, size * 0.9f, dir, p );
		VectorMA( p, -size * 0.8f, right, p );
		re.AddLightToScene( p, size * 7.0f, 1.0f, 0.93f, 0.85f );
		VectorMA( target, -size * 1.1f, dir, p );
		VectorMA( p, size * 0.55f, up, p );
		VectorMA( p, size * 0.35f, right, p );
		re.AddLightToScene( p, size * 6.0f, accent[0], accent[1], accent[2] );
		VectorMA( target, size * 1.2f, dir, p );
		VectorMA( p, size * 1.2f, right, p );
		re.AddLightToScene( p, size * 3.0f, 0.55f * 0.35f, 0.62f * 0.35f, 0.8f * 0.35f );
	}

	// faint embers rising past the subject (showcase_stage.gd's _embers, a fighter being 1.8 of its
	// meters tall): from a flat box around the feet, up at 0.15-0.5 m/s within 25 degrees of
	// straight up, a little faster as they go, wandering; in by 15% of their 7 s, orange to red to nothing;
	// none with Settings > Accessibility > Reduced flashing
	const int embers = Cvar_VariableIntegerValue( "oz_accessibility_reduced_flashing" ) ? 0 : NUM_EMBERS;	// one cvar lookup a frame, not one an ember
	for ( i = 0; i < embers; i++ ) {
		refEntity_t ember;
		const float m = size / 1.8f;
		const float age = sc.t / EMBER_LIFE + Hash( i, 0 );
		const int life = (int)age;				// a new place and speed every life
		const float x = age - life, s = x * EMBER_LIFE;
		const float speed = ( 0.15f + 0.35f * Hash( i, life * 7 + 1 ) ) * m;
		const float tilt = DEG2RAD( 25.0f ) * Hash( i, life * 7 + 2 ), heading = 2.0f * M_PI * Hash( i, life * 7 + 3 );
		const float wander = 0.3f * m * x;
		float c[3], a;
		int j;

		Com_Memset( &ember, 0, sizeof( ember ) );
		ember.reType = RT_SPRITE;
		ember.customShader = sc.flare;
		ember.radius = 0.03f * m * ( 0.5f + 0.8f * Hash( i, life * 7 + 4 ) );
		ember.origin[0] = sc.center[0] + ( Hash( i, life * 7 + 5 ) * 2.0f - 1.0f ) * 3.5f * m + cosf( heading ) * sinf( tilt ) * speed * s
			+ wander * sinf( sc.t * 0.9f + i );
		ember.origin[1] = sc.center[1] + ( Hash( i, life * 7 + 6 ) * 2.0f - 1.0f ) * 3.0f * m + sinf( heading ) * sinf( tilt ) * speed * s
			+ wander * cosf( sc.t * 0.7f + i * 1.7f );
		ember.origin[2] = sc.center[2] - size * 0.5f + ( Hash( i, life * 7 + 7 ) * 2.0f - 1.0f ) * 0.3f * m
			+ cosf( tilt ) * speed * s + 0.06f * m * s * s;
		// the color ramp, brightened as the Godot material's (2.5, 2, 1.6) albedo, then scaled back
		// whole to fit (clamping each channel turned the orange yellow); additive, so the alpha
		// scales the color
		if ( x < 0.15f ) {
			a = x / 0.15f;
			c[0] = 1.0f; c[1] = 0.55f - 0.1f * a; c[2] = 0.15f - 0.05f * a;
		} else {
			const float k2 = ( x - 0.15f ) / 0.85f;
			a = 1.0f - k2;
			c[0] = 1.0f - 0.2f * k2; c[1] = 0.45f - 0.35f * k2; c[2] = 0.1f - 0.08f * k2;
		}
		c[0] *= 2.5f; c[1] *= 2.0f; c[2] *= 1.6f;
		for ( j = 0; j < 3; j++ ) {
			ember.shader.rgba[j] = (byte)( 255 * a * c[j] / c[0] );	// red is the brightest throughout
		}
		ember.shader.rgba[3] = 255;
		re.AddRefEntityToScene( &ember, qfalse );
	}

	// big soft smoke puffs drifting through, thicker while a subject comes in
	for ( i = 0; i < NUM_PUFFS; i++ ) {
		refEntity_t puff;
		const float ph = i * 2.399f;	// the golden angle spreads them
		const float life = fmodf( sc.t * 0.06f + i / (float)NUM_PUFFS, 1.0f );
		const float a = sinf( life * M_PI );
		const float shade = 0.5f + 0.5f * ( 1.0f - swapFade );

		Com_Memset( &puff, 0, sizeof( puff ) );
		puff.reType = RT_SPRITE;
		puff.customShader = sc.smoke;
		puff.radius = size * ( 0.9f + 0.5f * ( i % 3 ) );
		puff.rotation = ph * 40.0f + sc.t * 4.0f;
		puff.origin[0] = sc.center[0] + cosf( ph ) * size * 1.4f - life * size * 0.8f;
		puff.origin[1] = sc.center[1] + sinf( ph ) * size * 1.6f;
		puff.origin[2] = sc.center[2] + ( ( i * 37 ) % 100 / 100.0f - 0.5f ) * size * 1.2f + life * size * 0.5f;
		puff.shader.rgba[0] = (byte)( 84 * a * shade );
		puff.shader.rgba[1] = (byte)( 76 * a * shade );
		puff.shader.rgba[2] = (byte)( 72 * a * shade );
		puff.shader.rgba[3] = (byte)( 150 * a * shade );
		re.AddRefEntityToScene( &puff, qfalse );
	}

	re.RenderScene( &rd );
}
