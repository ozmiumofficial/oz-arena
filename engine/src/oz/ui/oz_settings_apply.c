/*
===========================================================================
Oz: the Settings page's own cvars applied to the engine's (ENGINE_PLAN.md P4).

The page (engine/ui/settings.json) keeps every setting in an oz_<tab>_<name> cvar. Those that stand
for engine settings are applied here: the window, the renderer's quality, sound, the controller and
the network. A setting is applied when it changes, never at startup, so the engine's own defaults
(the tuned look) stay until the player picks something else; settings.json's defaults match them.
Settings read where they act (the HUD's, the cgame's, the renderer's own oz_ cvars) aren't here.

The renderer's latched settings (shadows, reflections, AO, MSAA) take effect at the next
vid_restart, which the Settings page runs when it closes (Settings_PendingRestart); the window's
settings restart it at once, and the page asks to keep them (Settings_ConfirmDisplay).
===========================================================================
*/
#include "../../../quake3e/code/client/client.h"

#ifdef USE_SDL
// sdl_glimp.c: the monitors, for Video > Monitor
int			GLimp_DisplayCount( void );
qboolean	GLimp_DisplayBounds( int index, int *x, int *y, int *w, int *h );
qboolean	GLimp_UsableBounds( int index, int *x, int *y, int *w, int *h );
#endif

typedef struct {
	const char	*name;
	void		(*apply)( const char *value );
	cvar_t		*cv;
	int			applied;	// the modificationCount applied
} ozSetting_t;

static qboolean	restartPending;		// a latched renderer cvar changed: vid_restart when Settings closes
static qboolean	vidRestart, sndRestart;	// the window or the sound device changed: restart at this frame's end

static void Set( const char *name, const char *value ) {
	if ( strcmp( Cvar_VariableString( name ), value ) ) {
		Cvar_Set( name, value );
	}
}

// a latched renderer cvar: its change waits for a vid_restart
static void SetLatched( const char *name, const char *value ) {
	if ( strcmp( Cvar_SavedString( name ), value ) ) {
		Cvar_Set2( name, value, qfalse );	// latched: the running renderer keeps the value it was made with
		restartPending = qtrue;
	}
}

static int Pick( const char *value, const char *const *names, int fallback ) {
	int i;
	for ( i = 0; names[i]; i++ ) {
		if ( !Q_stricmp( value, names[i] ) ) {
			return i;
		}
	}
	return fallback;
}

/*
================
Video: the window. Windowed is the Window size centered on the monitor, with its frame; Borderless is
that size without a frame. Both fit beside the taskbar: a size as big as the monitor (1920x1080 on a
1080p screen) covered it all, the title bar off its top, and looked full screen. Full screen covers
the monitor with a borderless window at its own resolution; Exclusive takes the monitor over at its
resolution. Changing any of them restarts the renderer at once.
================
*/
#define OZ_WINDOW_TITLE		32	// a window's title bar and edges, outside the size SDL is given
#define OZ_WINDOW_EDGE		8

// the window's size and place: w by h shrunk to fit the usable area (aspect kept), centered in it
static void FitWindow( int ux, int uy, int uw, int uh, qboolean frame, int *w, int *h, int *x, int *y ) {
	const int top = frame ? OZ_WINDOW_TITLE : 0, edge = frame ? OZ_WINDOW_EDGE : 0;
	const int aw = uw - 2 * edge, ah = uh - top - edge;

	if ( uw > 0 && uh > 0 && aw > 0 && ah > 0 && ( *w > aw || *h > ah ) ) {
		const float s = MIN( (float)aw / *w, (float)ah / *h );
		*w = (int)( *w * s ) & ~1;
		*h = (int)( *h * s ) & ~1;
	}
	*x = uw > 0 ? ux + ( uw - *w ) / 2 : ux;
	*y = uh > 0 ? uy + top + ( uh - top - edge - *h ) / 2 : uy;
}

static void Window( const char *unused ) {
	char	mode[32], res[32];
	int		monitor = Cvar_VariableIntegerValue( "oz_video_monitor" );
	int		mx = 0, my = 0, mw = 0, mh = 0, ux, uy, uw, uh, w = 1600, h = 900, x, y;
	qboolean changed = qfalse;

	Cvar_VariableStringBuffer( "oz_video_display_mode", mode, sizeof( mode ) );
	Cvar_VariableStringBuffer( "oz_video_resolution", res, sizeof( res ) );
	if ( sscanf( res, "%dx%d", &w, &h ) != 2 || w < 320 || h < 240 ) {
		w = 1600;
		h = 900;
	}
#ifdef USE_SDL
	if ( !GLimp_DisplayBounds( monitor, &mx, &my, &mw, &mh ) && !GLimp_DisplayBounds( monitor = 0, &mx, &my, &mw, &mh ) ) {
		mw = mh = 0;
	}
#endif
	ux = mx; uy = my; uw = mw; uh = mh;
#ifdef USE_SDL
	if ( mw && !GLimp_UsableBounds( monitor, &ux, &uy, &uw, &uh ) ) {
		ux = mx; uy = my; uw = mw; uh = mh;
	}
#endif
#define SET( n, v ) do { if ( strcmp( Cvar_SavedString( n ), v ) ) { Cvar_Set( n, v ); changed = qtrue; } } while ( 0 )
	if ( !Q_stricmp( mode, "exclusive" ) ) {
		SET( "r_fullscreen", "1" );
		SET( "r_modeFullscreen", "-2" );
		SET( "vid_xpos", va( "%d", mx ) );
		SET( "vid_ypos", va( "%d", my ) );
	} else if ( !Q_stricmp( mode, "fullscreen" ) ) {
		SET( "r_fullscreen", "0" );
		SET( "r_noborder", "1" );
		SET( "r_mode", "-2" );
		SET( "vid_xpos", va( "%d", mx ) );
		SET( "vid_ypos", va( "%d", my ) );
	} else {
		const qboolean borderless = !Q_stricmp( mode, "borderless" );
		FitWindow( ux, uy, uw, uh, !borderless, &w, &h, &x, &y );
		SET( "r_fullscreen", "0" );
		SET( "r_noborder", borderless ? "1" : "0" );
		SET( "r_mode", "-1" );
		SET( "r_customwidth", va( "%d", w ) );
		SET( "r_customheight", va( "%d", h ) );
		SET( "vid_xpos", va( "%d", x ) );
		SET( "vid_ypos", va( "%d", y ) );
	}
#undef SET
	if ( changed ) {
		vidRestart = qtrue;
	}
}

// Recording-friendly display: OpenGL frames through DXGI (sdl_dxgi.c), at the renderer's restart.
// The Video tab's Apply restarts it at once, as it does the window.
static void Capture( const char *v ) {
	const char *value = ( atoi( v ) || !Q_stricmp( v, "true" ) ) ? "1" : "0";
	if ( strcmp( Cvar_SavedString( "r_dxgi" ), value ) ) {
		Cvar_Set2( "r_dxgi", value, qfalse );
		vidRestart = qtrue;
	}
}

static void RenderScale( const char *v ) { Set( "r_ozRenderScale", v ); }
static void Upscaler( const char *v ) { Set( "r_ozUpscaler", !Q_stricmp( v, "fsr" ) ? "1" : "0" ); }
static void Contrast( const char *v ) { Set( "r_ozContrast", v ); }

/*
================
Graphics. High is the engine's own look: id's sun shadows at 1024, the cubemaps, the remastered
materials' AO, bloom, no anti-aliasing, full model detail.
================
*/
static void Shadows( const char *v ) {
	static const char *const names[] = { "off", "low", "medium", "high", "ultra", NULL };
	static const char *const size[] = { "512", "512", "512", "1024", "2048" };
	static const char *const filter[] = { "0", "0", "1", "1", "2" };
	const int i = Pick( v, names, 3 );
	SetLatched( "r_sunShadows", i ? "1" : "0" );
	SetLatched( "r_shadowMapSize", size[i] );
	SetLatched( "r_shadowFilter", filter[i] );
}

static void Reflections( const char *v ) { SetLatched( "r_cubeMapping", atoi( v ) ? "1" : "0" ); }

static void AmbientOcclusion( const char *v ) {
	static const char *const names[] = { "off", "low", "medium", "high", NULL };
	static const char *const strength[] = { "0", "0.5", "1", "1" };
	const int i = Pick( v, names, 2 );
	Set( "r_ozAO", strength[i] );					// the materials' own AO, at once
	SetLatched( "r_ssao", i == 3 ? "1" : "0" );		// rend2's screen-space AO on top, at High
}

static void Bloom( const char *v ) { Set( "r_ozBloom", atoi( v ) ? "1" : "0" ); }

// Experimental: the upgraded fighters' models (tr_oz_rig.c loads them as their rigs register: at the next vid_restart)
static void ModernModels( const char *v ) { SetLatched( "r_ozModernFighters", atoi( v ) ? "1" : "0" ); }

static void Antialiasing( const char *v ) {
	static const char *const names[] = { "off", "fxaa", "msaa2", "msaa4", "msaa8", NULL };
	static const char *const samples[] = { "0", "0", "2", "4", "8" };
	const int i = Pick( v, names, 0 );
	Set( "r_ozFxaa", i == 1 ? "1" : "0" );
	SetLatched( "r_ext_multisample", samples[i] );
}

static void Detail( const char *v ) {
	static const char *const names[] = { "low", "medium", "high", NULL };
	static const char *const lodbias[] = { "1", "0", "-2" };
	Set( "r_lodbias", lodbias[Pick( v, names, 2 )] );
}

/*
================
Audio: the channels' volumes are read by the sound code (oz_audio_vol_*); here only what switches
the engine: 3D sound (OpenAL with HRTF) and the device.
================
*/
static void Spatial( const char *v ) {
	const qboolean on = atoi( v ) != 0;
	if ( strcmp( Cvar_VariableString( "s_useOpenAL" ), on ? "1" : "0" ) ) {
		Cvar_Set( "s_useOpenAL", on ? "1" : "0" );
		Cvar_Set( "s_alHRTF", on ? "2" : "0" );
		sndRestart = qtrue;
	}
}

// the list is SDL's (sdl_snd.c, SNDDMA_ListDevices); OpenAL Soft names the same endpoint "OpenAL Soft on <name>"
static void OutputDevice( const char *v ) {
	const char *device = !Q_stricmp( v, "Default" ) ? "" : v;
	const char *al = device[0] ? va( "OpenAL Soft on %s", device ) : "";
	if ( strcmp( Cvar_VariableString( "s_alDevice" ), al ) || strcmp( Cvar_VariableString( "s_sdlDevice" ), device ) ) {
		Cvar_Set( "s_alDevice", al );
		Cvar_Set( "s_sdlDevice", device );
		sndRestart = qtrue;
	}
}

// the categories' volumes (snd_main.c S_OzVolume): the page's 0-100, the engine's 0-1
static void Volume( const char *cvar, const char *v ) { Set( cvar, va( "%g", atoi( v ) / 100.0f ) ); }
static void VolEffects( const char *v ) { Volume( "s_volEffects", v ); }
static void VolAnnouncer( const char *v ) { Volume( "s_volAnnouncer", v ); }
static void VolMenu( const char *v ) { Volume( "s_volMenu", v ); }
static void Announcer( const char *v ) { Set( "s_announcer", atoi( v ) ? "1" : "0" ); }

/*
================
Controls, gameplay and the network
================
*/
static void PadDeadzone( const char *v ) { Set( "joy_threshold", v ); }
static void PadLook( const char *v ) { Set( "j_lookTurns", v ); }
static void ToggleCrouch( const char *v ) { Set( "cl_toggleCrouch", atoi( v ) ? "1" : "0" ); }

static void StickLayout( const char *v ) {
	// SDL's axes: 0, 1 the left stick, 2, 3 the right; southpaw looks with the left
	const qboolean southpaw = !Q_stricmp( v, "southpaw" );
	Set( "j_side_axis", southpaw ? "2" : "0" );
	Set( "j_forward_axis", southpaw ? "3" : "1" );
	Set( "j_yaw_axis", southpaw ? "0" : "2" );
	Set( "j_pitch_axis", southpaw ? "1" : "3" );
}

static void Autoswitch( const char *v ) { Set( "cg_autoswitch", Q_stricmp( v, "never" ) ? "1" : "0" ); }

// a delay before others are drawn: Godot's 16 ms is the client's own interpolation, more or less nudges it
static void Interp( const char *v ) { Set( "cl_timeNudge", va( "%d", atoi( v ) - 16 ) ); }

static void Rate( const char *v ) {
	static const char *const names[] = { "slow", "medium", "fast", "lan", NULL };
	static const char *const rates[] = { "25000", "50000", "100000", "125000" };
	Set( "rate", rates[Pick( v, names, 2 )] );
}

static void Colorblind( const char *v ) {
	static const char *const names[] = { "off", "protanopia", "deuteranopia", "tritanopia", NULL };
	Set( "r_ozColorblind", va( "%d", Pick( v, names, 0 ) ) );
}

static void Intro( const char *v ) {
	// first: id's com_introplayed (once); always: shown again next launch; never: never
	if ( !Q_stricmp( v, "always" ) ) {
		Set( "com_introplayed", "0" );
	} else if ( !Q_stricmp( v, "never" ) ) {
		Set( "com_introplayed", "1" );
	}
}

static ozSetting_t settings[] = {
	{ "oz_video_display_mode", Window },
	{ "oz_video_resolution", Window },
	{ "oz_video_monitor", Window },
	{ "oz_video_capture", Capture },
	{ "oz_video_render_scale", RenderScale },
	{ "oz_video_upscaler", Upscaler },
	{ "oz_video_contrast", Contrast },
	{ "oz_graphics_shadows", Shadows },
	{ "oz_graphics_reflections", Reflections },
	{ "oz_graphics_ao", AmbientOcclusion },
	{ "oz_graphics_bloom", Bloom },
	{ "oz_graphics_antialiasing", Antialiasing },
	{ "oz_graphics_detail", Detail },
	{ "oz_audio_spatial", Spatial },
	{ "oz_audio_output_device", OutputDevice },
	{ "oz_audio_vol_effects", VolEffects },
	{ "oz_audio_vol_announcer", VolAnnouncer },
	{ "oz_audio_vol_ui", VolMenu },
	{ "oz_audio_announcer", Announcer },
	{ "oz_controls_pad_deadzone", PadDeadzone },
	{ "oz_controls_pad_look", PadLook },
	{ "oz_controls_toggle_crouch", ToggleCrouch },
	{ "oz_controls_stick_layout", StickLayout },
	{ "oz_gameplay_autoswitch", Autoswitch },
	{ "oz_network_interp", Interp },
	{ "oz_network_rate", Rate },
	{ "oz_gameplay_intro", Intro },
	{ "oz_accessibility_colorblind", Colorblind },
	{ "oz_experimental_modern_models", ModernModels },
};

/*
================
OzSettings_Frame

Every frame (CL_Frame): a setting whose cvar changed since it was last seen is applied. The first
look only notes where each stands.
================
*/
void OzSettings_Frame( void ) {
	static qboolean started;
	ozSetting_t *s;

	for ( s = settings; s < settings + ARRAY_LEN( settings ); s++ ) {
		if ( !s->cv ) {
			// the Settings page makes them (oz_ui_settings.cpp); an existing cvar's Cvar_Get changes nothing
			if ( Cvar_Flags( s->name ) == CVAR_NONEXISTENT ) {
				continue;
			}
			s->cv = Cvar_Get( s->name, "", 0 );
			s->applied = s->cv->modificationCount;
			continue;
		}
		if ( s->cv->modificationCount != s->applied ) {
			s->applied = s->cv->modificationCount;
			if ( started ) {
				s->apply( s->cv->string );
			}
		}
	}
	started = qtrue;
	// inserted, so they run at the next frame's commands, not after whatever a script queued
	if ( sndRestart && !vidRestart ) {
		Cbuf_InsertText( "snd_restart\n" );
	}
	if ( vidRestart ) {
		Cbuf_InsertText( "vid_restart\n" );
	}
	vidRestart = sndRestart = qfalse;
}

// the Settings page, closing: a latched renderer change applies now
int OzSettings_TakeRestart( void ) {
	const qboolean r = restartPending;
	restartPending = qfalse;
	return r;
}

#ifdef USE_SDL
// the Settings page's Monitor list: how many there are
int OzSettings_Monitors( void ) {
	return GLimp_DisplayCount();
}
#else
int OzSettings_Monitors( void ) {
	return 1;
}
#endif
