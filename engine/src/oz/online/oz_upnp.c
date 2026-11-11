/*
===========================================================================
Oz Arena: an open UDP port for a host (ENGINE_PLAN.md P8 step 3). GPL-2.0.

When the player hosts (oz_host) with oz_host_upnp 1, the router is asked through UPnP (Windows' own
NATUPnP, no library) to forward the game's UDP port (net_port) to this PC. If it does, the listing
carries the router's public ip:port, so any client can join it as a normal server, and with
oz_host_masters 1 the listen server heartbeats to the master servers (sv_master1..5) as a dedicated
one would. The forward is taken down when hosting ends. Everything runs on a thread of its own:
UPnP discovery takes seconds.

  oz_host_upnp		1: ask the router for the port when hosting (0 in tests: it changes the router)
  oz_host_masters	1: an open host also registers with the master servers
  oz_upnp			prints what the router said
===========================================================================
*/
#ifdef _WIN32
#define COBJMACROS
#include "../../../quake3e/code/client/client.h"
#include <winsock2.h>

#include <windows.h>
#include <natupnp.h>
#include <oleauto.h>

static cvar_t	*oz_host_upnp, *oz_host_masters;
static HANDLE	thread;				// Open, while it asks the router
static HANDLE	closer;				// Close, while it takes a forward down
// 0 idle, 1 asking, 2 open, 3 refused, 4 hosting ended while asking (Open takes its forward down).
// 1 -> 2, 3 or 4 only by InterlockedCompareExchange, so Open and OzUpnp_Close never both miss a forward.
static volatile LONG	state;
static char		address[64];		// "ip:port" once open
static char		why[128];
static int		mappedPort;


// the LAN address this PC reaches the internet from (no packet is sent)
static qboolean LocalAddress( char *out, int size ) {
	SOCKET s = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
	struct sockaddr_in to, me;
	int len = sizeof( me );
	qboolean ok = qfalse;

	if ( s == INVALID_SOCKET ) {
		return qfalse;
	}
	memset( &to, 0, sizeof( to ) );
	to.sin_family = AF_INET;
	to.sin_port = htons( 53 );
	to.sin_addr.s_addr = htonl( 0xc0000201 );	// 192.0.2.1 (TEST-NET-1): only picks the route
	if ( connect( s, (struct sockaddr *)&to, sizeof( to ) ) == 0 && getsockname( s, (struct sockaddr *)&me, &len ) == 0 ) {
		const byte *b = (const byte *)&me.sin_addr.s_addr;
		Com_sprintf( out, size, "%i.%i.%i.%i", b[0], b[1], b[2], b[3] );
		ok = qtrue;
	}
	closesocket( s );
	return ok;
}


// an IPv4 address nobody on the internet can reach: private (10/8, 172.16/12, 192.168/16), the providers'
// shared range (100.64/10, carrier-grade NAT), this PC's own or none at all
static qboolean NotPublic( const char *ip ) {
	const char *dot = strchr( ip, '.' );
	const int a = atoi( ip ), b = dot ? atoi( dot + 1 ) : 0;
	return a == 10 || ( a == 172 && b >= 16 && b <= 31 ) || ( a == 192 && b == 168 ) || ( a == 100 && b >= 64 && b <= 127 )
		|| a == 127 || ( a == 169 && b == 254 ) || a == 0;
}


static IStaticPortMappingCollection *Mappings( IUPnPNAT **nat ) {
	IStaticPortMappingCollection *coll = NULL;
	if ( FAILED( CoCreateInstance( &CLSID_UPnPNAT, NULL, CLSCTX_ALL, &IID_IUPnPNAT, (void **)nat ) ) ) {
		return NULL;
	}
	if ( FAILED( IUPnPNAT_get_StaticPortMappingCollection( *nat, &coll ) ) ) {
		coll = NULL;
	}
	return coll;
}


// Open's answer: refused, unless hosting ended meanwhile (then idle)
static void Refused( const char *reason ) {
	Q_strncpyz( why, reason, sizeof( why ) );
	if ( InterlockedCompareExchange( &state, 3, 1 ) != 1 ) {
		InterlockedExchange( &state, 0 );
	}
}


static DWORD WINAPI Open( LPVOID arg ) {
	IUPnPNAT *nat = NULL;
	IStaticPortMappingCollection *coll;
	IStaticPortMapping *map = NULL;
	char lan[64];
	WCHAR wlan[64];
	BSTR proto, client, desc, ext = NULL;
	int port = (int)(intptr_t)arg;

	CoInitializeEx( NULL, COINIT_MULTITHREADED );
	coll = Mappings( &nat );
	if ( !coll ) {
		Refused( "no UPnP router answered" );
	} else if ( !LocalAddress( lan, sizeof( lan ) ) ) {
		Refused( "no LAN address" );
	} else {
		MultiByteToWideChar( CP_ACP, 0, lan, -1, wlan, ARRAY_LEN( wlan ) );
		proto = SysAllocString( L"UDP" );
		client = SysAllocString( wlan );
		desc = SysAllocString( L"Oz Arena" );
		if ( SUCCEEDED( IStaticPortMappingCollection_Add( coll, port, proto, port, client, VARIANT_TRUE, desc, &map ) ) && map
			&& SUCCEEDED( IStaticPortMapping_get_ExternalIPAddress( map, &ext ) ) && ext && ext[0] ) {
			char ip[48];
			ip[0] = '\0';
			WideCharToMultiByte( CP_ACP, 0, ext, -1, ip, sizeof( ip ), NULL, NULL );
			ip[sizeof( ip ) - 1] = '\0';
			if ( NotPublic( ip ) ) {
				// The router sits behind another one (or the provider's): its "external" address is a private one
				// nobody outside can reach. It was listed as "open at 192.168..." and every join there timed out.
				IStaticPortMappingCollection_Remove( coll, port, proto );
				Refused( "the router's own address isn't a public one" );
			} else {
				Com_sprintf( address, sizeof( address ), "%s:%i", ip, port );
				mappedPort = port;
				if ( InterlockedCompareExchange( &state, 2, 1 ) != 1 ) {
					// hosting ended while the router was asked: the forward comes down here, or it stays
					// on the router (NATUPnP's static forwards don't expire)
					IStaticPortMappingCollection_Remove( coll, port, proto );
					address[0] = '\0';
					mappedPort = 0;
					InterlockedExchange( &state, 0 );
				}
			}
		} else {
			Refused( "the router refused the port" );
		}
		if ( ext ) {
			SysFreeString( ext );
		}
		if ( map ) {
			IStaticPortMapping_Release( map );
		}
		SysFreeString( proto );
		SysFreeString( client );
		SysFreeString( desc );
	}
	if ( coll ) {
		IStaticPortMappingCollection_Release( coll );
	}
	if ( nat ) {
		IUPnPNAT_Release( nat );
	}
	CoUninitialize();
	return 0;
}


static DWORD WINAPI Close( LPVOID arg ) {
	IUPnPNAT *nat = NULL;
	IStaticPortMappingCollection *coll;
	CoInitializeEx( NULL, COINIT_MULTITHREADED );
	coll = Mappings( &nat );
	if ( coll ) {
		BSTR proto = SysAllocString( L"UDP" );
		IStaticPortMappingCollection_Remove( coll, (long)(intptr_t)arg, proto );
		SysFreeString( proto );
		IStaticPortMappingCollection_Release( coll );
	}
	if ( nat ) {
		IUPnPNAT_Release( nat );
	}
	CoUninitialize();
	return 0;
}


/*
==================
OzUpnp_Address

The host's public ip:port while the router forwards it, else "". Called each second while hosting
(oz_ice.c's listing): the first call asks the router, and a listen server that's gone closes it.
==================
*/
const char *OzUpnp_Address( void ) {
	if ( !oz_host_upnp ) {
		oz_host_upnp = Cvar_Get( "oz_host_upnp", "1", CVAR_ARCHIVE );
		Cvar_SetDescription( oz_host_upnp, "1: a host asks the router (UPnP) to open its UDP port, so any game can join it at ip:port." );
		oz_host_masters = Cvar_Get( "oz_host_masters", "0", CVAR_ARCHIVE );
		Cvar_SetDescription( oz_host_masters, "1: a host whose port is open also registers with the master servers (sv_master1..5)." );
	}
	if ( !oz_host_upnp->integer ) {
		return "";
	}
	if ( closer ) {
		// the last forward is still coming down: asking now could be undone by its Remove
		if ( WaitForSingleObject( closer, 0 ) != WAIT_OBJECT_0 ) {
			return "";
		}
		CloseHandle( closer );
		closer = NULL;
	}
	if ( state == 0 && !thread ) {
		InterlockedExchange( &state, 1 );
		thread = CreateThread( NULL, 0, Open, (LPVOID)(intptr_t)Cvar_VariableIntegerValue( "net_port" ), 0, NULL );
	}
	if ( thread && WaitForSingleObject( thread, 0 ) == WAIT_OBJECT_0 ) {
		CloseHandle( thread );
		thread = NULL;
		if ( state == 2 ) {
			Com_Printf( "upnp: the router forwards %s to this PC\n", address );
			if ( oz_host_masters->integer ) {
				Cvar_Set( "sv_ozMasters", "1" );
			}
		} else if ( state == 3 ) {
			Com_Printf( "upnp: %s; peer-to-peer only\n", why );
		}
		// state 0: hosting ended while asking, and Open took its forward down; a host still on asks again
	}
	return state == 2 ? address : "";
}


/*
==================
OzUpnp_Close

Hosting ended: the forward comes down, on a thread (it can take a while). Still asking, the router's
answer is Open's to undo. Quitting (OzUpnp_Shutdown) waits for it.
==================
*/
void OzUpnp_Close( void ) {
	LONG was = InterlockedCompareExchange( &state, 4, 1 );
	if ( was == 2 && mappedPort ) {
		if ( closer ) {
			WaitForSingleObject( closer, 3000 );	// the one before (an unhost, a host, an unhost in a row)
			CloseHandle( closer );
		}
		closer = CreateThread( NULL, 0, Close, (LPVOID)(intptr_t)mappedPort, 0, NULL );
	}
	if ( was != 1 ) {
		InterlockedExchange( &state, 0 );
		mappedPort = 0;
		address[0] = '\0';
	}
	Cvar_Set( "sv_ozMasters", "0" );
}


// the game quits: whatever forward is up or on its way comes down before the process ends
void OzUpnp_Shutdown( void ) {
	OzUpnp_Close();
	if ( thread ) {
		WaitForSingleObject( thread, 5000 );	// asking: Open removes what the router gives it
		CloseHandle( thread );
		thread = NULL;
	}
	if ( closer ) {
		WaitForSingleObject( closer, 3000 );
		CloseHandle( closer );
		closer = NULL;
	}
}


void OzUpnp_Print( void ) {
	static const char *names[] = { "not asked", "asking the router", "open", "refused", "taking the forward down" };
	LONG s = state;
	Com_Printf( "upnp: %s%s%s\n", names[s >= 0 && s <= 4 ? s : 0], s == 2 ? " at " : s == 3 ? ": " : "", s == 2 ? address : s == 3 ? why : "" );
}
#endif
