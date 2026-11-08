/*
===========================================================================
Oz Arena: editing together's channel (ENGINE_PLAN.md PE step 2, oz_edlink.h). GPL-2.0.

One KCP control block per editing link, in stream mode (a snapshot is larger than a KCP message may
be), its segments sent as ICE packets with the 'K' prefix. The stream carries messages, each a 4-byte
length then its bytes. Fast messages go as one packet with the 'F' prefix, unacknowledged.

KCP runs in its fast mode (no delay, 10 ms clock, a resend after two skipped acks, no congestion
window): the links are few and the traffic light but for a joiner's snapshot and a play-test's pak.
===========================================================================
*/
#include "../../../quake3e/code/qcommon/q_shared.h"
#include "../../../quake3e/code/qcommon/qcommon.h"
#include "oz_edlink.h"
#include "ikcp.h"
#include <stdlib.h>

#define ED_LINKS		32			// as oz_ice.c's ICE_LINKS: the slot is the link's
#define ED_MTU			1196		// a packet's room for KCP, less the prefix (1200 in all: relays and tunnels take it)
#define ED_CHUNK		(32 * 1024)	// KCP takes a send of fewer than 128 segments
#define ED_EVENTS		1024
#define ED_MESSAGE_MAX	(64 * 1024 * 1024)	// a stream message larger than this is a broken stream
#define ED_FAST_MAX		1100
#define ED_CONV			0x0ED1

typedef struct {
	qboolean	used;
	int			slot, gen;
	char		pid[24];
	ikcpcb		*kcp;
	byte		*rbuf;		// the stream's bytes not yet a whole message
	int			rlen, rsize;
	qboolean	broken;
} edLink_t;

typedef struct {
	int		kind;
	char	pid[24];
	void	*data;
	int		len, fast;
} edEvent_t;

static edLink_t		ed[ED_LINKS];
static edEvent_t	events[ED_EVENTS];
static int			evHead, evTail;
static unsigned		clockBase;
static qboolean		clockSet;

static IUINT32 Now( void ) {
	if ( !clockSet ) {
		clockBase = (unsigned)Sys_Milliseconds();
		clockSet = qtrue;
	}
	return (IUINT32)( (unsigned)Sys_Milliseconds() - clockBase );
}


static void Push( int kind, const char *pid, void *data, int len, int fast ) {
	int next = ( evHead + 1 ) % ED_EVENTS;
	edEvent_t *e;
	if ( next == evTail ) {
		Com_Printf( S_COLOR_YELLOW "edlink: events full, one dropped\n" );
		if ( data ) {
			free( data );
		}
		return;
	}
	e = &events[evHead];
	e->kind = kind;
	Q_strncpyz( e->pid, pid, sizeof( e->pid ) );
	e->data = data;
	e->len = len;
	e->fast = fast;
	evHead = next;
}


int OzEdLink_Is( const unsigned char *data, int len ) {
	return len >= OZ_EDLINK_PREFIX_LEN && data[0] == 0xFE && data[1] == 'O' && data[2] == 'z' && ( data[3] == 'K' || data[3] == 'F' );
}


static int Output( const char *buf, int len, ikcpcb *kcp, void *user ) {
	edLink_t *l = (edLink_t *)user;
	byte pkt[OZ_EDLINK_PREFIX_LEN + 1500];
	if ( len > (int)sizeof( pkt ) - OZ_EDLINK_PREFIX_LEN ) {
		return -1;
	}
	pkt[0] = 0xFE, pkt[1] = 'O', pkt[2] = 'z', pkt[3] = 'K';
	memcpy( pkt + OZ_EDLINK_PREFIX_LEN, buf, len );
	OzIce_SendSlot( l->slot, l->gen, pkt, len + OZ_EDLINK_PREFIX_LEN );
	return 0;
}


static void Free( edLink_t *l ) {
	if ( l->kcp ) {
		ikcp_release( l->kcp );
	}
	if ( l->rbuf ) {
		free( l->rbuf );
	}
	Com_Memset( l, 0, sizeof( *l ) );
}


static edLink_t *ByPid( const char *pid ) {
	int i;
	for ( i = 0; i < ED_LINKS; i++ ) {
		if ( ed[i].used && !strcmp( ed[i].pid, pid ) ) {
			return &ed[i];
		}
	}
	return NULL;
}


void OzEdLink_Up( int slot, int gen, const char *pid, int relayed ) {
	edLink_t *l;
	if ( slot < 0 || slot >= ED_LINKS ) {
		return;
	}
	l = &ed[slot];
	Free( l );
	l->used = qtrue;
	l->slot = slot;
	l->gen = gen;
	Q_strncpyz( l->pid, pid, sizeof( l->pid ) );
	l->kcp = ikcp_create( ED_CONV, l );
	l->kcp->stream = 1;
	ikcp_setoutput( l->kcp, Output );
	ikcp_setmtu( l->kcp, ED_MTU );
	ikcp_nodelay( l->kcp, 1, 10, 2, 1 );
	ikcp_wndsize( l->kcp, 512, 512 );
	Com_Printf( "edlink: %s up%s\n", pid, relayed ? ", relayed" : "" );
	Push( OZ_EDEV_UP, pid, NULL, 0, relayed );
}


void OzEdLink_Down( int slot, int gen, const char *pid, const char *why ) {
	edLink_t *l;
	if ( slot < 0 || slot >= ED_LINKS ) {
		return;
	}
	l = &ed[slot];
	if ( !l->used || l->gen != gen ) {
		return;
	}
	Free( l );
	Com_Printf( "edlink: %s down (%s)\n", pid, why ? why : "closed" );
	Push( OZ_EDEV_DOWN, pid, NULL, 0, 0 );
}


void OzEdLink_Failed( const char *pid, const char *why ) {
	const char *w = why ? why : "";
	const int n = (int)strlen( w );
	char *text = (char *)malloc( n + 1 );
	memcpy( text, w, n + 1 );
	Push( OZ_EDEV_FAILED, pid, text, n, 0 );
}


// whole messages out of the stream's bytes
static void Drain( edLink_t *l ) {
	for ( ;; ) {
		int n = ikcp_peeksize( l->kcp );
		if ( n <= 0 ) {
			break;
		}
		if ( l->rlen + n > l->rsize ) {
			int size = MAX( l->rsize * 2, l->rlen + n + 65536 );
			byte *b = (byte *)malloc( size );
			if ( l->rbuf ) {
				memcpy( b, l->rbuf, l->rlen );
				free( l->rbuf );
			}
			l->rbuf = b;
			l->rsize = size;
		}
		n = ikcp_recv( l->kcp, (char *)l->rbuf + l->rlen, l->rsize - l->rlen );
		if ( n <= 0 ) {
			break;
		}
		l->rlen += n;
	}
	while ( l->rlen >= 4 ) {
		const int len = l->rbuf[0] | ( l->rbuf[1] << 8 ) | ( l->rbuf[2] << 16 ) | ( l->rbuf[3] << 24 );
		byte *msg;
		if ( len < 0 || len > ED_MESSAGE_MAX ) {
			Com_Printf( S_COLOR_YELLOW "edlink: %s's stream is broken (a message of %i bytes)\n", l->pid, len );
			l->broken = qtrue;
			l->rlen = 0;
			break;
		}
		if ( l->rlen < 4 + len ) {
			break;
		}
		msg = (byte *)malloc( len + 1 );
		memcpy( msg, l->rbuf + 4, len );
		msg[len] = 0;
		Push( OZ_EDEV_MESSAGE, l->pid, msg, len, 0 );
		memmove( l->rbuf, l->rbuf + 4 + len, l->rlen - 4 - len );
		l->rlen -= 4 + len;
	}
}


void OzEdLink_Input( int slot, int gen, const char *pid, const unsigned char *data, int len ) {
	edLink_t *l;
	if ( slot < 0 || slot >= ED_LINKS || !OzEdLink_Is( data, len ) ) {
		return;
	}
	l = &ed[slot];
	if ( !l->used || l->gen != gen ) {
		return;	// a link the session never took up
	}
	if ( data[3] == 'F' ) {
		const int n = len - OZ_EDLINK_PREFIX_LEN;
		byte *msg = (byte *)malloc( n + 1 );
		memcpy( msg, data + OZ_EDLINK_PREFIX_LEN, n );
		msg[n] = 0;
		Push( OZ_EDEV_MESSAGE, l->pid, msg, n, 1 );
		return;
	}
	ikcp_input( l->kcp, (const char *)data + OZ_EDLINK_PREFIX_LEN, len - OZ_EDLINK_PREFIX_LEN );
	Drain( l );
}


void OzEdLink_Frame( void ) {
	const IUINT32 now = Now();
	int i;
	for ( i = 0; i < ED_LINKS; i++ ) {
		if ( ed[i].used && ed[i].kcp ) {
			ikcp_update( ed[i].kcp, now );
		}
	}
}


void OzEdLink_Send( const char *pid, const void *data, int len, int fast ) {
	edLink_t *l = ByPid( pid );
	byte head[4];
	int at;
	if ( !l || len < 0 ) {
		return;
	}
	if ( fast ) {
		byte pkt[OZ_EDLINK_PREFIX_LEN + ED_FAST_MAX];
		if ( len > ED_FAST_MAX ) {
			return;
		}
		pkt[0] = 0xFE, pkt[1] = 'O', pkt[2] = 'z', pkt[3] = 'F';
		memcpy( pkt + OZ_EDLINK_PREFIX_LEN, data, len );
		OzIce_SendSlot( l->slot, l->gen, pkt, len + OZ_EDLINK_PREFIX_LEN );
		return;
	}
	head[0] = (byte)len, head[1] = (byte)( len >> 8 ), head[2] = (byte)( len >> 16 ), head[3] = (byte)( len >> 24 );
	ikcp_send( l->kcp, (const char *)head, 4 );
	for ( at = 0; at < len; at += ED_CHUNK ) {
		ikcp_send( l->kcp, (const char *)data + at, MIN( ED_CHUNK, len - at ) );
	}
	ikcp_flush( l->kcp );
}


int OzEdLink_Queued( const char *pid ) {
	edLink_t *l = ByPid( pid );
	return l && l->kcp ? ikcp_waitsnd( l->kcp ) * ED_MTU : 0;
}


int OzEdLink_Poll( char *pid, int pidSize, void **data, int *len, int *fast ) {
	edEvent_t e;
	if ( evTail == evHead ) {
		return OZ_EDEV_NONE;
	}
	e = events[evTail];
	evTail = ( evTail + 1 ) % ED_EVENTS;
	Q_strncpyz( pid, e.pid, pidSize );
	*data = e.data;
	*len = e.len;
	*fast = e.fast;
	return e.kind;
}


void OzEdLink_Clear( void ) {
	int i;
	for ( i = 0; i < ED_LINKS; i++ ) {
		if ( ed[i].used ) {
			Free( &ed[i] );
		}
	}
	while ( evTail != evHead ) {
		if ( events[evTail].data ) {
			free( events[evTail].data );
		}
		evTail = ( evTail + 1 ) % ED_EVENTS;
	}
}
