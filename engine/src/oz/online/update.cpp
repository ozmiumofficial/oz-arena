/*
===========================================================================
Oz Arena: the update check. GPL-2.0.

At launch the game asks GitHub for the latest release (api.github.com's releases/latest) on a thread
of its own, and compares its tag with the build's OZ_VERSION. "Update now" downloads the release's
OzArenaSetup.exe, which the client (oz_online_engine.c) runs in its update mode as the game quits.
The download is checked against what the release's JSON says of the file before anything runs it: its
size, and its SHA-256 when GitHub gives one (the asset's "digest"). Nothing here blocks a frame: the
client polls.
===========================================================================
*/
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "oz_online.h"

#include <curl/curl.h>
#include "cJSON.h"
#include <mbedtls/sha256.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

namespace {

std::mutex			lock;
std::thread			checker, downloader;
std::atomic<int>	checkState{ OZ_UPDATE_IDLE };
std::atomic<int>	downloadState{ OZ_UPDATE_IDLE };
std::atomic<long long>	downloadDone{ 0 }, downloadTotal{ 0 };
std::atomic<bool>	stopping{ false };
ozRelease_t			release;
std::string			downloadError;

void Copy( char *dst, size_t size, const char *src ) {
	snprintf( dst, size, "%s", src ? src : "" );
}

size_t ToString( char *data, size_t size, size_t n, void *user ) {
	std::string *s = static_cast<std::string *>( user );
	if ( s->size() > 4 * 1024 * 1024 ) {
		return 0;	// a release's JSON is a few KB: anything this big isn't one
	}
	s->append( data, size * n );
	return size * n;
}

// a download's file, and its SHA-256 as it's written
struct Sink {
	FILE					*f;
	mbedtls_sha256_context	sha;
};

size_t ToFile( char *data, size_t size, size_t n, void *user ) {
	if ( stopping ) {
		return 0;
	}
	Sink *s = static_cast<Sink *>( user );
	const size_t w = fwrite( data, size, n, s->f );
	mbedtls_sha256_update( &s->sha, (const unsigned char *)data, w * size );
	downloadDone += (long long)( w * size );
	return w * size;
}

#ifdef _WIN32
// The path is UTF-8 (oz_update.c makes it from Windows' wide one). fopen and remove read it in the system's code
// page, so the download failed for a player whose user name has a letter outside it.
std::wstring Wide( const std::string &utf8 ) {
	std::wstring w( utf8.size() + 1, L'\0' );
	const int n = MultiByteToWideChar( CP_UTF8, 0, utf8.c_str(), -1, &w[0], (int)w.size() );
	w.resize( n > 0 ? n - 1 : 0 );
	return w;
}
#endif

FILE *OpenWrite( const std::string &path ) {
#ifdef _WIN32
	const std::wstring w = Wide( path );
	return w.empty() ? nullptr : _wfopen( w.c_str(), L"wb" );
#else
	return fopen( path.c_str(), "wb" );
#endif
}

void Remove( const std::string &path ) {
#ifdef _WIN32
	_wremove( Wide( path ).c_str() );
#else
	remove( path.c_str() );
#endif
}

int Progress( void *, curl_off_t total, curl_off_t, curl_off_t, curl_off_t ) {
	if ( total > 0 ) {
		downloadTotal = (long long)total;
	}
	return stopping ? 1 : 0;
}

int Abort( void *, curl_off_t, curl_off_t, curl_off_t, curl_off_t ) {
	return stopping ? 1 : 0;	// the game is quitting: OzUpdate_Shutdown waits for this thread
}

CURL *Easy( const std::string &url ) {
	CURL *c = curl_easy_init();
	if ( !c ) {
		return nullptr;
	}
	curl_easy_setopt( c, CURLOPT_URL, url.c_str() );
	curl_easy_setopt( c, CURLOPT_FOLLOWLOCATION, 1L );
	curl_easy_setopt( c, CURLOPT_MAXREDIRS, 8L );
	if ( !url.compare( 0, 8, "https://" ) ) {
		curl_easy_setopt( c, CURLOPT_REDIR_PROTOCOLS_STR, "https" );	// GitHub's redirects never leave https
	}
	curl_easy_setopt( c, CURLOPT_CONNECTTIMEOUT, 10L );
	curl_easy_setopt( c, CURLOPT_SSL_OPTIONS, (long)CURLSSLOPT_REVOKE_BEST_EFFORT );
	curl_easy_setopt( c, CURLOPT_USERAGENT, "OzArena-update" );	// GitHub's API refuses a request without one
	curl_easy_setopt( c, CURLOPT_NOSIGNAL, 1L );
	return c;
}

// "- **The thing** (detail)" -> "The thing (detail)": a release body's bullets as the menu's notes
std::string Plain( std::string s ) {
	std::string out;
	for ( size_t i = 0; i < s.size(); i++ ) {
		const char ch = s[i];
		if ( ch == '*' || ch == '`' || ( ch == '_' && ( i == 0 || s[i - 1] == ' ' || i + 1 == s.size() || s[i + 1] == ' ' ) ) ) {
			continue;
		}
		if ( ch == '[' ) {	// [text](link) -> text
			const size_t close = s.find( "](", i );
			const size_t end = close == std::string::npos ? close : s.find( ')', close );
			if ( end != std::string::npos ) {
				out += s.substr( i + 1, close - i - 1 );
				i = end;
				continue;
			}
		}
		out += ch;
	}
	while ( !out.empty() && isspace( (unsigned char)out.back() ) ) {
		out.pop_back();
	}
	return out;
}

void Notes( const char *body ) {
	release.numNotes = 0;
	if ( !body ) {
		return;
	}
	const char *p = body;
	while ( *p && release.numNotes < OZ_UPDATE_NOTES ) {
		const char *eol = strchr( p, '\n' );
		std::string line( p, eol ? eol - p : strlen( p ) );
		p = eol ? eol + 1 : p + line.size();
		size_t i = line.find_first_not_of( " \t" );
		if ( i == std::string::npos || i > 1 ) {
			continue;	// only top-level bullets
		}
		if ( ( line[i] == '-' || line[i] == '*' ) && i + 1 < line.size() && line[i + 1] == ' ' ) {
			const std::string note = Plain( line.substr( i + 2 ) );
			if ( !note.empty() ) {
				Copy( release.notes[release.numNotes++], sizeof( release.notes[0] ), note.c_str() );
			}
		}
	}
}

void Check( std::string url ) {
	std::string body;
	long code = 0;
	CURLcode r = CURLE_FAILED_INIT;
	if ( CURL *c = Easy( url ) ) {
		struct curl_slist *h = curl_slist_append( nullptr, "Accept: application/vnd.github+json" );
		curl_easy_setopt( c, CURLOPT_HTTPHEADER, h );
		curl_easy_setopt( c, CURLOPT_TIMEOUT, 20L );
		curl_easy_setopt( c, CURLOPT_WRITEFUNCTION, ToString );
		curl_easy_setopt( c, CURLOPT_WRITEDATA, &body );
		curl_easy_setopt( c, CURLOPT_NOPROGRESS, 0L );
		curl_easy_setopt( c, CURLOPT_XFERINFOFUNCTION, Abort );
		r = curl_easy_perform( c );
		curl_easy_getinfo( c, CURLINFO_RESPONSE_CODE, &code );
		curl_easy_cleanup( c );
		curl_slist_free_all( h );
	}
	std::lock_guard<std::mutex> g( lock );
	memset( &release, 0, sizeof( release ) );
	if ( r != CURLE_OK || code != 200 ) {
		if ( r != CURLE_OK ) {
			Copy( release.error, sizeof( release.error ), curl_easy_strerror( r ) );
		} else {
			snprintf( release.error, sizeof( release.error ), "HTTP %ld", code );
		}
		checkState = OZ_UPDATE_FAILED;
		return;
	}
	cJSON *doc = cJSON_Parse( body.c_str() );
	const cJSON *tag = cJSON_GetObjectItemCaseSensitive( doc, "tag_name" );
	if ( !cJSON_IsString( tag ) ) {
		Copy( release.error, sizeof( release.error ), "no tag_name in the answer" );
		cJSON_Delete( doc );
		checkState = OZ_UPDATE_FAILED;
		return;
	}
	Copy( release.tag, sizeof( release.tag ), tag->valuestring );
	const cJSON *name = cJSON_GetObjectItemCaseSensitive( doc, "name" );
	Copy( release.name, sizeof( release.name ), cJSON_IsString( name ) ? name->valuestring : tag->valuestring );
	const cJSON *page = cJSON_GetObjectItemCaseSensitive( doc, "html_url" );
	Copy( release.page, sizeof( release.page ), cJSON_IsString( page ) ? page->valuestring : "" );
	const cJSON *when = cJSON_GetObjectItemCaseSensitive( doc, "published_at" );
	if ( cJSON_IsString( when ) ) {
		snprintf( release.published, sizeof( release.published ), "%.10s", when->valuestring );	// 2026-11-05T00:27:36Z -> the date
	}
	const cJSON *body1 = cJSON_GetObjectItemCaseSensitive( doc, "body" );
	Notes( cJSON_IsString( body1 ) ? body1->valuestring : nullptr );
	const cJSON *assets = cJSON_GetObjectItemCaseSensitive( doc, "assets" );
	const cJSON *a;
	cJSON_ArrayForEach( a, assets ) {
		const cJSON *an = cJSON_GetObjectItemCaseSensitive( a, "name" );
		const cJSON *au = cJSON_GetObjectItemCaseSensitive( a, "browser_download_url" );
		const cJSON *as = cJSON_GetObjectItemCaseSensitive( a, "size" );
		const cJSON *ad = cJSON_GetObjectItemCaseSensitive( a, "digest" );
		if ( !cJSON_IsString( an ) || !cJSON_IsString( au ) ) {
			continue;
		}
		if ( !strcmp( an->valuestring, "OzArenaSetup.exe" ) ) {
			Copy( release.setup, sizeof( release.setup ), au->valuestring );
			release.setupSize = cJSON_IsNumber( as ) ? (long long)as->valuedouble : 0;
			// "sha256:<64 hex digits>", on releases GitHub made since it began giving one
			release.setupSha256[0] = '\0';
			if ( cJSON_IsString( ad ) && !strncmp( ad->valuestring, "sha256:", 7 ) && strlen( ad->valuestring + 7 ) == 64 ) {
				bool hex = true;
				for ( int i = 0; i < 64; i++ ) {
					const unsigned char ch = (unsigned char)ad->valuestring[7 + i];
					hex = hex && isxdigit( ch );
					release.setupSha256[i] = (char)tolower( ch );
				}
				release.setupSha256[hex ? 64 : 0] = '\0';
			}
		}
		if ( cJSON_IsNumber( as ) ) {
			release.size += (long long)as->valuedouble;
		}
	}
	cJSON_Delete( doc );
	checkState = OZ_UPDATE_DONE;
}

// `size` and `sha256` are what the release says of the file (0 and "" where it says nothing): a download that
// isn't that is removed and fails. The installer runs with the player's rights the moment it's down, so it is
// only ever the file the release lists.
void Download( std::string url, std::string path, long long size, std::string sha256 ) {
	Sink sink;
	sink.f = OpenWrite( path );
	if ( !sink.f ) {
		std::lock_guard<std::mutex> g( lock );
		downloadError = "couldn't write " + path;
		downloadState = OZ_UPDATE_FAILED;
		return;
	}
	FILE *f = sink.f;
	mbedtls_sha256_init( &sink.sha );
	mbedtls_sha256_starts( &sink.sha, 0 );
	long code = 0;
	CURLcode r = CURLE_FAILED_INIT;
	if ( CURL *c = Easy( url ) ) {
		curl_easy_setopt( c, CURLOPT_WRITEFUNCTION, ToFile );
		curl_easy_setopt( c, CURLOPT_WRITEDATA, &sink );
		curl_easy_setopt( c, CURLOPT_NOPROGRESS, 0L );
		curl_easy_setopt( c, CURLOPT_XFERINFOFUNCTION, Progress );
		curl_easy_setopt( c, CURLOPT_LOW_SPEED_LIMIT, 1L );	// stalled for a minute: give up
		curl_easy_setopt( c, CURLOPT_LOW_SPEED_TIME, 60L );
		r = curl_easy_perform( c );
		curl_easy_getinfo( c, CURLINFO_RESPONSE_CODE, &code );
		curl_easy_cleanup( c );
	}
	const bool closed = fclose( f ) == 0;
	unsigned char hash[32];
	char hex[65];
	const bool hashed = mbedtls_sha256_finish( &sink.sha, hash ) == 0;
	mbedtls_sha256_free( &sink.sha );
	for ( int i = 0; i < 32; i++ ) {
		snprintf( hex + i * 2, 3, "%02x", hash[i] );
	}
	const long long got = downloadDone;
	std::lock_guard<std::mutex> g( lock );
	if ( r != CURLE_OK || code != 200 || !closed || got <= 0 ) {
		downloadError = r != CURLE_OK ? curl_easy_strerror( r ) : code != 200 ? "HTTP " + std::to_string( code ) : "the file came empty";
	} else if ( size > 0 && got != size ) {
		downloadError = "it isn't the release's file: " + std::to_string( got ) + " bytes, not " + std::to_string( size );
	} else if ( !sha256.empty() && ( !hashed || sha256 != hex ) ) {
		downloadError = "it isn't the release's file: its SHA-256 differs";
	} else {
		downloadState = OZ_UPDATE_DONE;
		return;
	}
	Remove( path );
	downloadState = OZ_UPDATE_FAILED;
}

// 0.7.5-beta -> {0, 7, 5}, "beta"
void Parse( const char *v, int num[3], std::string &suffix ) {
	num[0] = num[1] = num[2] = 0;
	if ( *v == 'v' || *v == 'V' ) {
		v++;
	}
	for ( int i = 0; i < 3 && isdigit( (unsigned char)*v ); i++ ) {
		num[i] = atoi( v );
		while ( isdigit( (unsigned char)*v ) ) {
			v++;
		}
		if ( *v == '.' ) {
			v++;
		}
	}
	if ( *v == '-' || *v == '+' ) {
		v++;
	}
	suffix = v;
}

void Join( std::thread &t ) {
	if ( t.joinable() ) {
		t.join();
	}
}

}	// namespace

extern "C" {

void OzUpdate_Check( const char *url ) {
	if ( checkState == OZ_UPDATE_BUSY ) {
		return;
	}
	Join( checker );
	stopping = false;
	checkState = OZ_UPDATE_BUSY;
	checker = std::thread( Check, std::string( url ) );
}

int OzUpdate_Checked( ozRelease_t *out ) {
	const int s = checkState;
	if ( out && s != OZ_UPDATE_IDLE && s != OZ_UPDATE_BUSY ) {
		std::lock_guard<std::mutex> g( lock );
		*out = release;
	}
	return s;
}

void OzUpdate_Download( const char *url, const char *path, long long size, const char *sha256 ) {
	if ( downloadState == OZ_UPDATE_BUSY ) {
		return;
	}
	Join( downloader );
	stopping = false;
	downloadDone = downloadTotal = 0;
	downloadError.clear();
	downloadState = OZ_UPDATE_BUSY;
	downloader = std::thread( Download, std::string( url ), std::string( path ), size, std::string( sha256 ? sha256 : "" ) );
}

int OzUpdate_Downloaded( long long *done, long long *total, char *error, int size ) {
	if ( done ) {
		*done = downloadDone;
	}
	if ( total ) {
		*total = downloadTotal;
	}
	const int s = downloadState;
	if ( error && size > 0 ) {
		std::lock_guard<std::mutex> g( lock );
		Copy( error, size, downloadError.c_str() );
	}
	return s;
}

int OzUpdate_Compare( const char *a, const char *b ) {
	int na[3], nb[3];
	std::string sa, sb;
	Parse( a, na, sa );
	Parse( b, nb, sb );
	for ( int i = 0; i < 3; i++ ) {
		if ( na[i] != nb[i] ) {
			return na[i] < nb[i] ? -1 : 1;
		}
	}
	if ( sa == sb ) {
		return 0;
	}
	if ( sa.empty() || sb.empty() ) {
		return sa.empty() ? 1 : -1;	// 0.8.0 is newer than 0.8.0-beta
	}
	return sa < sb ? -1 : 1;	// alpha < beta < rc
}

void OzUpdate_Shutdown( void ) {
	stopping = true;
	Join( checker );
	Join( downloader );
}

}	// extern "C"
