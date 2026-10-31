/*
===========================================================================
Oz Arena: Ogg Theora cinematics behind cl_cin.c's API (ENGINE_PLAN.md P3). GPL-2.0.

The upscaled cinematics (tools/make_fx_pk3.py: video/<name>.ogv in pak_oz_video.pk3) are Theora
video with Vorbis sound. cl_cin.c plays video/<name>.ogv in place of the RoQ asked for when the
.ogv is there, and an .ogv asked for by name; RoQ stays for everything else (mods' videos).

Frames are decoded as the clock reaches them and converted from Y'CbCr to RGBA once each. The
oz renderer draws them at their own size; id's power-of-two rule (RE_StretchRaw) holds for the
other renderers, which get each frame resampled to the nearest power of two on each side. The
sound goes to the mixer through S_RawSamples a little ahead of the picture.
===========================================================================
*/
#include "client.h"
#include "snd_local.h"
#include <ogg/ogg.h>
#include <vorbis/codec.h>
#include <theora/theoradec.h>

#define OGV_READ		8192
#define OGV_SOUND_LEAD	150		// ms of sound queued ahead of the picture

typedef struct ozOgv_s {
	fileHandle_t		file;
	ogg_sync_state		sync;
	ogg_page			page;

	ogg_stream_state	tStream, vStream;
	qboolean			hasTheora, hasVorbis;
	th_info				tInfo;
	th_comment			tComment;
	th_setup_info		*tSetup;
	th_dec_ctx			*tDec;
	int					tHeaders;

	vorbis_info			vInfo;
	vorbis_comment		vComment;
	vorbis_dsp_state	vDsp;
	vorbis_block		vBlock;
	int					vHeaders;
	qboolean			vReady;

	int					frames;			// video frames decoded
	double				frameMsec;
	long long			samples;		// sound samples sent to the mixer
	qboolean			eof;			// the file is read to its end

	int					outW, outH;		// the RGBA frame's size
	byte				*rgba;
	int					*colX;			// the source column of each output column (resampling)
	int					*rowY;
	short				*pcm;			// interleaved 16-bit sound for S_RawSamples
	int					pcmCap;
} ozOgv_t;


// one more page from the file into the sync state; qfalse at the end of the file
static qboolean OGV_ReadPage( ozOgv_t *v ) {
	while ( ogg_sync_pageout( &v->sync, &v->page ) != 1 ) {
		char *buf = ogg_sync_buffer( &v->sync, OGV_READ );
		int n = FS_Read( buf, OGV_READ, v->file );
		if ( n <= 0 ) {
			v->eof = qtrue;
			return qfalse;
		}
		ogg_sync_wrote( &v->sync, n );
	}
	return qtrue;
}


static void OGV_PageIn( ozOgv_t *v ) {
	if ( v->hasTheora ) {
		ogg_stream_pagein( &v->tStream, &v->page );
	}
	if ( v->hasVorbis ) {
		ogg_stream_pagein( &v->vStream, &v->page );
	}
}


/*
==================
CIN_OgvClose
==================
*/
void CIN_OgvClose( ozOgv_t *v ) {
	if ( !v ) {
		return;
	}
	if ( v->tDec ) {
		th_decode_free( v->tDec );
	}
	if ( v->tSetup ) {
		th_setup_free( v->tSetup );
	}
	th_info_clear( &v->tInfo );
	th_comment_clear( &v->tComment );
	if ( v->hasTheora ) {
		ogg_stream_clear( &v->tStream );
	}
	if ( v->vReady ) {
		vorbis_block_clear( &v->vBlock );
		vorbis_dsp_clear( &v->vDsp );
	}
	if ( v->hasVorbis ) {
		ogg_stream_clear( &v->vStream );
	}
	vorbis_comment_clear( &v->vComment );
	vorbis_info_clear( &v->vInfo );
	ogg_sync_clear( &v->sync );
	if ( v->rgba ) Z_Free( v->rgba );
	if ( v->colX ) Z_Free( v->colX );
	if ( v->rowY ) Z_Free( v->rowY );
	if ( v->pcm ) Z_Free( v->pcm );
	Z_Free( v );
}


static int NearestPow2( int n ) {
	int p = 1;
	while ( p * 2 <= n ) {
		p *= 2;
	}
	return ( n - p < p * 2 - n ) ? p : p * 2;
}


/*
==================
CIN_OgvOpen

The streams' headers from an open .ogv. NULL (the file left for the caller to close) when it holds
no Theora video. `nativeSize`: the renderer takes frames of any size.
==================
*/
ozOgv_t *CIN_OgvOpen( fileHandle_t file, qboolean nativeSize, int *width, int *height ) {
	ozOgv_t *v = Z_Malloc( sizeof( *v ) );
	ogg_stream_state test;
	ogg_packet op;
	int i;

	Com_Memset( v, 0, sizeof( *v ) );
	v->file = file;
	ogg_sync_init( &v->sync );
	th_info_init( &v->tInfo );
	th_comment_init( &v->tComment );
	vorbis_info_init( &v->vInfo );
	vorbis_comment_init( &v->vComment );

	// the beginning-of-stream pages: find a Theora and a Vorbis stream and their first headers
	while ( OGV_ReadPage( v ) ) {
		if ( !ogg_page_bos( &v->page ) ) {
			OGV_PageIn( v );
			break;
		}
		ogg_stream_init( &test, ogg_page_serialno( &v->page ) );
		ogg_stream_pagein( &test, &v->page );
		if ( ogg_stream_packetout( &test, &op ) != 1 ) {
			ogg_stream_clear( &test );
			continue;
		}
		if ( !v->hasTheora && th_decode_headerin( &v->tInfo, &v->tComment, &v->tSetup, &op ) > 0 ) {
			Com_Memcpy( &v->tStream, &test, sizeof( test ) );
			v->hasTheora = qtrue;
			v->tHeaders = 1;
		} else if ( !v->hasVorbis && vorbis_synthesis_headerin( &v->vInfo, &v->vComment, &op ) >= 0 ) {
			Com_Memcpy( &v->vStream, &test, sizeof( test ) );
			v->hasVorbis = qtrue;
			v->vHeaders = 1;
		} else {
			ogg_stream_clear( &test );
		}
	}
	// the rest of the headers (three each)
	while ( ( v->hasTheora && v->tHeaders < 3 ) || ( v->hasVorbis && v->vHeaders < 3 ) ) {
		qboolean progress = qfalse;
		while ( v->hasTheora && v->tHeaders < 3 && ogg_stream_packetout( &v->tStream, &op ) == 1 ) {
			if ( th_decode_headerin( &v->tInfo, &v->tComment, &v->tSetup, &op ) < 0 ) {
				Com_Printf( S_COLOR_YELLOW "cinematic: bad Theora header\n" );
				CIN_OgvClose( v );
				return NULL;
			}
			v->tHeaders++;
			progress = qtrue;
		}
		while ( v->hasVorbis && v->vHeaders < 3 && ogg_stream_packetout( &v->vStream, &op ) == 1 ) {
			if ( vorbis_synthesis_headerin( &v->vInfo, &v->vComment, &op ) < 0 ) {
				v->hasVorbis = qfalse;	// silent, the picture still plays
				ogg_stream_clear( &v->vStream );
				break;
			}
			v->vHeaders++;
			progress = qtrue;
		}
		if ( !progress && !OGV_ReadPage( v ) ) {
			break;
		}
		if ( !progress ) {
			OGV_PageIn( v );
		}
	}
	if ( !v->hasTheora || v->tHeaders < 3 ) {
		v->file = FS_INVALID_HANDLE;	// the caller's to close
		CIN_OgvClose( v );
		return NULL;
	}
	v->tDec = th_decode_alloc( &v->tInfo, v->tSetup );
	if ( v->hasVorbis && v->vHeaders >= 3 ) {
		vorbis_synthesis_init( &v->vDsp, &v->vInfo );
		vorbis_block_init( &v->vDsp, &v->vBlock );
		v->vReady = qtrue;
	}
	v->frameMsec = v->tInfo.fps_numerator ? 1000.0 * v->tInfo.fps_denominator / v->tInfo.fps_numerator : 1000.0 / 30.0;

	v->outW = nativeSize ? (int)v->tInfo.pic_width : NearestPow2( v->tInfo.pic_width );
	v->outH = nativeSize ? (int)v->tInfo.pic_height : NearestPow2( v->tInfo.pic_height );
	v->rgba = Z_Malloc( v->outW * v->outH * 4 );
	v->colX = Z_Malloc( v->outW * sizeof( int ) );
	v->rowY = Z_Malloc( v->outH * sizeof( int ) );
	for ( i = 0; i < v->outW; i++ ) {
		v->colX[i] = (int)( ( i + 0.5 ) * v->tInfo.pic_width / v->outW );
	}
	for ( i = 0; i < v->outH; i++ ) {
		v->rowY[i] = (int)( ( i + 0.5 ) * v->tInfo.pic_height / v->outH );
	}
	*width = v->outW;
	*height = v->outH;
	Com_Printf( "cinematic: Theora %ux%u at %.2f fps, %s, drawn %ix%i\n", v->tInfo.pic_width, v->tInfo.pic_height,
		1000.0 / v->frameMsec, v->vReady ? va( "Vorbis %i Hz %i ch", (int)v->vInfo.rate, v->vInfo.channels ) : "no sound",
		v->outW, v->outH );
	return v;
}


// the decoder's last frame to RGBA (BT.601, video range), sampled at colX and rowY
static void OGV_Convert( ozOgv_t *v ) {
	th_ycbcr_buffer yuv;
	int x, y, cx = 0, cy = 0, px, py;

	th_decode_ycbcr_out( v->tDec, yuv );
	if ( !( v->tInfo.pixel_fmt & 1 ) ) cx = 1;		// 4:2:0 and 4:2:2 halve the chroma columns
	if ( v->tInfo.pixel_fmt == TH_PF_420 ) cy = 1;
	for ( y = 0; y < v->outH; y++ ) {
		byte *out = v->rgba + y * v->outW * 4;
		py = v->rowY[y] + v->tInfo.pic_y;
		{
			const byte *yr = yuv[0].data + py * yuv[0].stride;
			const byte *ur = yuv[1].data + ( py >> cy ) * yuv[1].stride;
			const byte *vr = yuv[2].data + ( py >> cy ) * yuv[2].stride;
			for ( x = 0; x < v->outW; x++, out += 4 ) {
				int Y, U, V, r, g, b;
				px = v->colX[x] + v->tInfo.pic_x;
				Y = ( yr[px] - 16 ) * 298;
				U = ur[px >> cx] - 128;
				V = vr[px >> cx] - 128;
				r = ( Y + 409 * V + 128 ) >> 8;
				g = ( Y - 100 * U - 208 * V + 128 ) >> 8;
				b = ( Y + 516 * U + 128 ) >> 8;
				out[0] = r < 0 ? 0 : r > 255 ? 255 : r;
				out[1] = g < 0 ? 0 : g > 255 ? 255 : g;
				out[2] = b < 0 ? 0 : b > 255 ? 255 : b;
				out[3] = 255;
			}
		}
	}
}


// decoded sound up to `untilMsec` to the mixer
static void OGV_Sound( ozOgv_t *v, int untilMsec, qboolean silent ) {
	ogg_packet op;
	float **pcm;
	int n, i, c, ch = v->vInfo.channels;

	if ( !v->vReady ) {
		return;
	}
	while ( v->samples * 1000 / v->vInfo.rate < untilMsec ) {
		n = vorbis_synthesis_pcmout( &v->vDsp, &pcm );
		if ( n > 0 ) {
			if ( n * ch > v->pcmCap ) {
				if ( v->pcm ) Z_Free( v->pcm );
				v->pcmCap = n * ch;
				v->pcm = Z_Malloc( v->pcmCap * sizeof( short ) );
			}
			for ( i = 0; i < n; i++ ) {
				for ( c = 0; c < ch; c++ ) {
					int s = (int)( pcm[c][i] * 32767.0f );
					v->pcm[i * ch + c] = s < -32768 ? -32768 : s > 32767 ? 32767 : s;
				}
			}
			if ( !silent ) {
				S_RawSamples( n, v->vInfo.rate, 2, ch, (byte *)v->pcm, s_volume->value );
			}
			vorbis_synthesis_read( &v->vDsp, n );
			v->samples += n;
			continue;
		}
		if ( ogg_stream_packetout( &v->vStream, &op ) == 1 ) {
			if ( vorbis_synthesis( &v->vBlock, &op ) == 0 ) {
				vorbis_synthesis_blockin( &v->vDsp, &v->vBlock );
			}
			continue;
		}
		if ( !OGV_ReadPage( v ) ) {
			return;
		}
		OGV_PageIn( v );
	}
}


/*
==================
CIN_OgvRun

Decodes up to `msec` into the cinematic. Returns 1 with a new frame in *frame, 0 when the frame
shown stays, -1 at the end.
==================
*/
int CIN_OgvRun( ozOgv_t *v, int msec, qboolean silent, byte **frame ) {
	ogg_packet op;
	qboolean fresh = qfalse;

	OGV_Sound( v, msec + OGV_SOUND_LEAD, silent );
	// every frame due by now; only the last one is converted
	while ( v->frames == 0 || v->frames * v->frameMsec <= msec ) {
		if ( ogg_stream_packetout( &v->tStream, &op ) == 1 ) {
			if ( th_decode_packetin( v->tDec, &op, NULL ) >= 0 ) {
				v->frames++;
				fresh = qtrue;
			}
			continue;
		}
		if ( !OGV_ReadPage( v ) ) {
			break;
		}
		OGV_PageIn( v );
	}
	if ( fresh ) {
		OGV_Convert( v );
		*frame = v->rgba;
		return 1;
	}
	if ( v->eof && v->frames * v->frameMsec <= msec ) {
		Com_Printf( "cinematic: %i frames, %.2f s\n", v->frames, v->frames * v->frameMsec / 1000.0 );
		return -1;
	}
	*frame = v->rgba;
	return 0;
}
