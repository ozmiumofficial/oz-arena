/*
===========================================================================
Oz Arena: the online hub's connection (ENGINE_PLAN.md P7). GPL-2.0.

One WebSocket to the hub over libcurl's WebSocket API (CURLOPT_CONNECT_ONLY 2, curl_ws_send and
curl_ws_recv), on a thread of its own so a slow lookup or TLS handshake never holds a frame. The
thread signs each handshake (identity.cpp), sends {"type":"ping"} every 10 s and times the pong,
and after a lost connection tries again in 1, 2, 4 ... 30 s. A hub that refuses the identity (close
code 4001) or the game's version (4002) isn't retried. Messages go both ways through queues the
client's frame empties (OzOnline_Receive) and fills (OzOnline_Send).
===========================================================================
*/
#define NOMINMAX
#include "oz_online.h"

#include <curl/curl.h>

#include <algorithm>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#endif

namespace {

const int		PING_EVERY_MS = 10000;
const int		CLOSE_IDENTITY = 4001, CLOSE_OUTDATED = 4002;
const size_t	MAX_QUEUE = 512;		// messages kept for the frame; past it the oldest go

struct Hub {
	std::mutex				lock;
	std::condition_variable	wake;
	std::thread				thread;
	std::atomic<bool>		stop{ false };
	std::deque<std::string>	inbox, outbox;
	int						status = OZ_ONLINE_OFFLINE;
	std::string				reason;
	int						closeCode = 0;
	std::atomic<int>		rtt{ 100 };
	std::string				url, name, model, color;
} hub;

void SetStatus( int status, const std::string &reason ) {
	std::lock_guard<std::mutex> g( hub.lock );
	hub.status = status;
	hub.reason = reason;
}

// Godot's String.uri_encode: everything but unreserved characters, percent-encoded
std::string UriEncode( const std::string &s ) {
	static const char *hex = "0123456789ABCDEF";
	std::string out;
	for ( unsigned char c : s ) {
		if ( isalnum( c ) || c == '-' || c == '_' || c == '.' || c == '~' ) {
			out += (char)c;
		} else {
			out += '%';
			out += hex[c >> 4];
			out += hex[c & 15];
		}
	}
	return out;
}

long long NowMs() {
	using namespace std::chrono;
	return duration_cast<milliseconds>( system_clock::now().time_since_epoch() ).count();
}

long long TickMs() {
	using namespace std::chrono;
	return duration_cast<milliseconds>( steady_clock::now().time_since_epoch() ).count();
}

bool Waited( int ms ) {
	std::unique_lock<std::mutex> g( hub.lock );
	hub.wake.wait_for( g, std::chrono::milliseconds( ms ), [] { return hub.stop.load(); } );
	return !hub.stop;
}

void Deliver( std::string &&msg ) {
	if ( msg.find( "\"type\":\"pong\"" ) != std::string::npos ) {
		return;		// timed below, not the game's
	}
	std::lock_guard<std::mutex> g( hub.lock );
	if ( hub.inbox.size() >= MAX_QUEUE ) {
		hub.inbox.pop_front();
	}
	hub.inbox.push_back( std::move( msg ) );
}

// One connection, from the handshake to its end. Returns the close code (0: lost or never made).
int Session( std::string &why, bool &wasOnline ) {
	CURL *c = curl_easy_init();
	if ( !c ) {
		why = "Can't reach the online service.";
		return 0;
	}
	char sig[1024];
	long long ts = NowMs();
	if ( !OzIdentity_Sign( ( "hub|" + std::to_string( ts ) ).c_str(), sig, sizeof( sig ) ) ) {
		curl_easy_cleanup( c );
		why = "Couldn't sign in with this PC's online identity.";
		return CLOSE_IDENTITY;
	}
	std::string url, name, model, color;
	{
		std::lock_guard<std::mutex> g( hub.lock );
		url = hub.url; name = hub.name; model = hub.model; color = hub.color;
	}
	struct curl_slist *h = nullptr;
	h = curl_slist_append( h, "x-proto: 1" );
	h = curl_slist_append( h, ( std::string( "x-key: " ) + OzIdentity_PublicKey() ).c_str() );
	h = curl_slist_append( h, ( "x-ts: " + std::to_string( ts ) ).c_str() );
	h = curl_slist_append( h, ( std::string( "x-sig: " ) + sig ).c_str() );
	h = curl_slist_append( h, ( "x-name: " + UriEncode( name ) ).c_str() );
	h = curl_slist_append( h, ( "x-model: " + model ).c_str() );
	h = curl_slist_append( h, ( "x-color: " + color ).c_str() );
	// the Oz engine build: the hub keeps its listings, joins and queues apart from the Godot build's (P8)
	h = curl_slist_append( h, "x-engine: oz" );
	curl_easy_setopt( c, CURLOPT_URL, url.c_str() );
	curl_easy_setopt( c, CURLOPT_CONNECT_ONLY, 2L );
	curl_easy_setopt( c, CURLOPT_HTTPHEADER, h );
	curl_easy_setopt( c, CURLOPT_CONNECTTIMEOUT, 10L );
	curl_easy_setopt( c, CURLOPT_SSL_OPTIONS, (long)CURLSSLOPT_REVOKE_BEST_EFFORT );
	curl_easy_setopt( c, CURLOPT_USERAGENT, "OzArena" );

	int code = 0;
	CURLcode r = curl_easy_perform( c );
	if ( r != CURLE_OK ) {
		why = std::string( "Can't reach the online service (" ) + curl_easy_strerror( r ) + ").";
		curl_slist_free_all( h );
		curl_easy_cleanup( c );
		return 0;
	}
	SetStatus( OZ_ONLINE_ONLINE, "" );
	wasOnline = true;

	curl_socket_t sock = CURL_SOCKET_BAD;
	curl_easy_getinfo( c, CURLINFO_ACTIVESOCKET, &sock );
	std::string frame;
	long long nextPing = TickMs() + PING_EVERY_MS, pingSent = 0;
	char buf[16384];
	bool open = true;
	while ( open && !hub.stop ) {
		// everything the hub sent
		for ( ;; ) {
			size_t got = 0;
			const struct curl_ws_frame *meta = nullptr;
			r = curl_ws_recv( c, buf, sizeof( buf ), &got, &meta );
			if ( r == CURLE_AGAIN ) {
				break;
			}
			if ( r != CURLE_OK || !meta ) {
				open = false;
				break;
			}
			if ( meta->flags & CURLWS_CLOSE ) {
				if ( got >= 2 ) {
					code = ( (unsigned char)buf[0] << 8 ) | (unsigned char)buf[1];
					why.assign( buf + 2, got - 2 );
				}
				open = false;
				break;
			}
			if ( meta->flags & ( CURLWS_TEXT | CURLWS_BINARY ) ) {
				frame.append( buf, got );
				if ( meta->bytesleft == 0 && !( meta->flags & CURLWS_CONT ) ) {
					if ( frame.find( "\"type\":\"pong\"" ) != std::string::npos && pingSent ) {
						hub.rtt = (int)std::max( 1LL, std::min( 2000LL, TickMs() - pingSent ) );
						pingSent = 0;
					}
					Deliver( std::move( frame ) );
					frame.clear();
				}
			}
		}
		if ( !open ) {
			break;
		}
		// everything the game queued, and the ping
		std::deque<std::string> out;
		{
			std::lock_guard<std::mutex> g( hub.lock );
			out.swap( hub.outbox );
		}
		if ( TickMs() >= nextPing ) {
			out.push_back( "{\"type\":\"ping\"}" );
			nextPing = TickMs() + PING_EVERY_MS;
			pingSent = TickMs();
		}
		for ( const std::string &m : out ) {
			size_t off = 0;
			while ( off < m.size() ) {
				size_t sent = 0;
				r = curl_ws_send( c, m.data() + off, m.size() - off, &sent, 0, CURLWS_TEXT );
				if ( r == CURLE_AGAIN ) {
					std::this_thread::sleep_for( std::chrono::milliseconds( 2 ) );
					continue;
				}
				if ( r != CURLE_OK ) {
					open = false;
					break;
				}
				off += sent;
			}
			if ( !open ) {
				break;
			}
		}
		if ( !open ) {
			break;
		}
		// sleep until the socket has something, a message is queued, or 50 ms pass
		if ( sock != CURL_SOCKET_BAD ) {
			fd_set rd;
			FD_ZERO( &rd );
			FD_SET( sock, &rd );
			timeval tv = { 0, 20000 };
			select( (int)sock + 1, &rd, nullptr, nullptr, &tv );
		} else {
			std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
		}
	}
	if ( hub.stop && open ) {
		size_t sent = 0;
		curl_ws_send( c, "", 0, &sent, 0, CURLWS_CLOSE );
	}
	curl_slist_free_all( h );
	curl_easy_cleanup( c );
	if ( why.empty() ) {
		why = "Lost the connection to the online service.";
	}
	return code;
}

void Run() {
	int backoff = 1;
	while ( !hub.stop ) {
		SetStatus( OZ_ONLINE_CONNECTING, "" );
		std::string why;
		bool wasOnline = false;
		long long began = TickMs();
		int code = Session( why, wasOnline );
		{
			std::lock_guard<std::mutex> g( hub.lock );
			hub.closeCode = code;
			hub.status = OZ_ONLINE_OFFLINE;
			hub.reason = why;
			hub.outbox.clear();		// meant for the connection that's gone (the frame sends the state again)
		}
		if ( hub.stop || code == CLOSE_IDENTITY || code == CLOSE_OUTDATED ) {
			break;
		}
		if ( wasOnline && TickMs() - began > 10000 ) {
			backoff = 1;
		}
		if ( !Waited( backoff * 1000 ) ) {
			break;
		}
		backoff = std::min( backoff * 2, 30 );
	}
}

}	// namespace

void OzOnline_Start( const char *base, const char *name, const char *model, const char *color ) {
	static bool curlReady = false;
	OzOnline_Stop();
	if ( !curlReady ) {
		curl_global_init( CURL_GLOBAL_DEFAULT );
		curlReady = true;
	}
	std::string url = base;
	while ( !url.empty() && ( url.back() == '/' || url.back() == ' ' ) ) {
		url.pop_back();
	}
	{
		std::lock_guard<std::mutex> g( hub.lock );
		hub.url = url + "/hub";
		hub.name = name;
		hub.model = model;
		hub.color = color;
		hub.inbox.clear();
		hub.outbox.clear();
		hub.closeCode = 0;
		hub.status = OZ_ONLINE_CONNECTING;
		hub.reason.clear();
	}
	hub.stop = false;
	hub.thread = std::thread( Run );
}

void OzOnline_Stop( void ) {
	if ( hub.thread.joinable() ) {
		{
			std::lock_guard<std::mutex> g( hub.lock );
			hub.stop = true;
		}
		hub.wake.notify_all();
		hub.thread.join();
	}
	std::lock_guard<std::mutex> g( hub.lock );
	hub.status = OZ_ONLINE_OFFLINE;
	hub.inbox.clear();
	hub.outbox.clear();
}

int OzOnline_Status( char *reason, int size ) {
	std::lock_guard<std::mutex> g( hub.lock );
	if ( reason && size > 0 ) {
		snprintf( reason, size, "%s", hub.reason.c_str() );
	}
	return hub.status;
}

int OzOnline_CloseCode( void ) {
	std::lock_guard<std::mutex> g( hub.lock );
	return hub.closeCode;
}

int OzOnline_Rtt( void ) {
	return hub.rtt;
}

int OzOnline_Send( const char *json ) {
	std::lock_guard<std::mutex> g( hub.lock );
	if ( hub.status != OZ_ONLINE_ONLINE ) {
		return 0;
	}
	hub.outbox.push_back( json );
	return 1;
}

int OzOnline_Receive( char *buf, int size ) {
	std::lock_guard<std::mutex> g( hub.lock );
	if ( hub.inbox.empty() ) {
		return 0;
	}
	snprintf( buf, size, "%s", hub.inbox.front().c_str() );
	hub.inbox.pop_front();
	return 1;
}
