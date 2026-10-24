// cg_q3cam.c -- q3cam: camera paths for recording trailer footage from retail Quake 3
//
// Q3Remaster's capture mod (tools/trailer/q3cam, GPL like id's code it extends). A path is a
// text file, cams/<name>.cam, with one key per line:
//
//     time  x y z  pitch yaw roll  fov
//
// in seconds, id units and degrees (fov is horizontal, as cg_fov). The camera follows a cubic
// Hermite spline through the keys in time, with Catmull-Rom tangents inside and flat ones at
// both ends, so a path eases in and out. One key holds still. Q3Remaster reads the same files,
// so both games fly exactly the same camera.
//
// Path time counts rendered frames (cam_fps a second), not cg.time: cl_avidemo steps the game
// clock by 1000 / fps whole milliseconds (16 ms at 60), which would run the path 4% fast.
//
//   cam_play <name> [record]   fly a path; with "record", capture it through cl_avidemo
//   cam_stop
//   cam_auto <name>            cvar: on the first frame join the spectators, wait cam_wait
//                              frames, then fly and record <name>; with cam_quit 1, quit after

#include "cg_local.h"

#define	CAM_MAX_KEYS	256
#define	CAM_CHANNELS	7		// x y z pitch yaw roll fov
#define	CAM_TAIL		2		// frames held after the last key, so the capture's last frame lands

typedef struct {
	float	time;
	float	v[CAM_CHANNELS];
} camKey_t;

static camKey_t	camKeys[CAM_MAX_KEYS];
static int		camNumKeys;
static char		camName[MAX_QPATH];
static qboolean	camPlaying;
static qboolean	camRecording;
static int		camFrame;			// frames flown, 0 on the first
static int		camFrames;			// frames in the path, both ends included
static float	camFps;
static int		camAutoStart;		// cg.clientFrame to start cam_auto at, -1 once done
static char		camFile[16384];		// static: the QVM stack is small


// cgame has no trap_Cvar_VariableIntegerValue
static int CG_CamCvarInt( const char *name ) {
	char	buf[16];

	trap_Cvar_VariableStringBuffer( name, buf, sizeof( buf ) );
	return atoi( buf );
}


static qboolean CG_CamLoad( const char *name ) {
	fileHandle_t	f;
	int				len, i;
	char			*p, *tok;
	camKey_t		*k;

	len = trap_FS_FOpenFile( va( "cams/%s.cam", name ), &f, FS_READ );
	if ( len <= 0 ) {
		CG_Printf( "q3cam: cams/%s.cam not found\n", name );
		return qfalse;
	}
	if ( len >= sizeof( camFile ) ) {
		trap_FS_FCloseFile( f );
		CG_Printf( "q3cam: cams/%s.cam is too big\n", name );
		return qfalse;
	}
	trap_FS_Read( camFile, len, f );
	trap_FS_FCloseFile( f );
	camFile[len] = 0;

	camNumKeys = 0;
	p = camFile;
	while ( camNumKeys < CAM_MAX_KEYS ) {
		tok = COM_Parse( &p );
		if ( !tok[0] ) {
			break;
		}
		k = &camKeys[camNumKeys];
		k->time = atof( tok );
		for ( i = 0 ; i < CAM_CHANNELS ; i++ ) {
			k->v[i] = atof( COM_Parse( &p ) );
		}
		if ( camNumKeys && k->time <= camKeys[camNumKeys - 1].time ) {
			CG_Printf( "q3cam: cams/%s.cam: key %i is not later than the one before\n", name, camNumKeys );
			return qfalse;
		}
		camNumKeys++;
	}
	if ( !camNumKeys ) {
		CG_Printf( "q3cam: cams/%s.cam has no keys\n", name );
		return qfalse;
	}
	Q_strncpyz( camName, name, sizeof( camName ) );
	return qtrue;
}


// Catmull-Rom tangent of channel c at key i, flat at both ends
static float CG_CamTangent( int i, int c ) {
	if ( i <= 0 || i >= camNumKeys - 1 ) {
		return 0;
	}
	return ( camKeys[i + 1].v[c] - camKeys[i - 1].v[c] ) / ( camKeys[i + 1].time - camKeys[i - 1].time );
}


static void CG_CamEval( float t, float *out ) {
	int		i, c;
	float	dt, u, u2, u3;
	camKey_t	*a, *b;

	if ( camNumKeys == 1 || t <= camKeys[0].time ) {
		memcpy( out, camKeys[0].v, sizeof( camKeys[0].v ) );
		return;
	}
	if ( t >= camKeys[camNumKeys - 1].time ) {
		memcpy( out, camKeys[camNumKeys - 1].v, sizeof( camKeys[0].v ) );
		return;
	}
	for ( i = 0 ; i < camNumKeys - 2 && t >= camKeys[i + 1].time ; i++ ) {
	}
	a = &camKeys[i];
	b = &camKeys[i + 1];
	dt = b->time - a->time;
	u = ( t - a->time ) / dt;
	u2 = u * u;
	u3 = u2 * u;
	for ( c = 0 ; c < CAM_CHANNELS ; c++ ) {
		out[c] = ( 2 * u3 - 3 * u2 + 1 ) * a->v[c]
			+ ( u3 - 2 * u2 + u ) * dt * CG_CamTangent( i, c )
			+ ( -2 * u3 + 3 * u2 ) * b->v[c]
			+ ( u3 - u2 ) * dt * CG_CamTangent( i + 1, c );
	}
}


static void CG_CamStart( const char *name, qboolean record ) {
	char	buf[16];

	if ( !CG_CamLoad( name ) ) {
		return;
	}
	trap_Cvar_VariableStringBuffer( "cam_fps", buf, sizeof( buf ) );
	camFps = atof( buf );
	if ( camFps <= 0 ) {
		camFps = 60;
	}
	camFrames = (int)( ( camKeys[camNumKeys - 1].time - camKeys[0].time ) * camFps + 0.5f ) + 1;
	camFrame = -1;		// CG_CamFrame makes it 0 on the first frame flown
	camPlaying = qtrue;
	camRecording = record;
	if ( record ) {
		trap_Cvar_Set( "cl_avidemo", va( "%i", (int)camFps ) );
	}
	CG_Printf( "q3cam: start %s %i\n", camName, camFrames );
}


static void CG_CamStop( void ) {
	if ( !camPlaying ) {
		return;
	}
	camPlaying = qfalse;
	if ( camRecording ) {
		trap_Cvar_Set( "cl_avidemo", "0" );
	}
	CG_Printf( "q3cam: done %s %i\n", camName, camFrames );
	if ( camRecording && CG_CamCvarInt( "cam_quit" ) ) {
		trap_SendConsoleCommand( "quit\n" );
	}
	camRecording = qfalse;
}


void CG_CamPlay_f( void ) {
	if ( trap_Argc() < 2 ) {
		CG_Printf( "usage: cam_play <name> [record]\n" );
		return;
	}
	CG_CamStop();
	CG_CamStart( CG_Argv( 1 ), !Q_stricmp( CG_Argv( 2 ), "record" ) );
}


void CG_CamStop_f( void ) {
	CG_CamStop();
}


// once per rendered frame, before the view is built
void CG_CamFrame( void ) {
	char	name[MAX_QPATH];

	if ( camAutoStart == 0 ) {
		camAutoStart = -1;
		trap_Cvar_VariableStringBuffer( "cam_auto", name, sizeof( name ) );
		if ( name[0] ) {
			if ( cg.snap->ps.persistant[PERS_TEAM] != TEAM_SPECTATOR ) {
				trap_SendClientCommand( "team spectator" );
			}
			camAutoStart = cg.clientFrame + CG_CamCvarInt( "cam_wait" );
		}
	}
	if ( camAutoStart > 0 && cg.clientFrame >= camAutoStart
		&& cg.snap->ps.persistant[PERS_TEAM] == TEAM_SPECTATOR ) {
		camAutoStart = -1;
		trap_Cvar_VariableStringBuffer( "cam_auto", name, sizeof( name ) );
		CG_CamStart( name, qtrue );
	}

	if ( !camPlaying ) {
		return;
	}
	camFrame++;
	if ( camFrame >= camFrames + CAM_TAIL ) {
		CG_CamStop();
	}
}


// replaces the view CG_CalcViewValues built; returns the new inwater
int CG_CamView( int inwater ) {
	float	v[CAM_CHANNELS];
	float	x;

	if ( !camPlaying ) {
		return inwater;
	}
	CG_CamEval( camKeys[0].time + camFrame / camFps, v );
	VectorCopy( v, cg.refdef.vieworg );
	VectorCopy( &v[3], cg.refdefViewAngles );	// lcc's VectorCopy is a struct cast: no "v + 3"
	AnglesToAxis( cg.refdefViewAngles, cg.refdef.viewaxis );

	// CG_CalcFov's projection, without zoom or the underwater warp
	cg.refdef.fov_x = v[6];
	x = cg.refdef.width / tan( v[6] / 360 * M_PI );
	cg.refdef.fov_y = atan2( cg.refdef.height, x ) * 360 / M_PI;
	return qfalse;
}


// after the snapshot's area mask is copied into the refdef: every area open, so the world
// draws wherever the camera is, not only where the spectator's area connects
void CG_CamAreamask( void ) {
	if ( camPlaying ) {
		memset( cg.refdef.areamask, 0, sizeof( cg.refdef.areamask ) );
	}
}
