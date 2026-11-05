/*
===========================================================================
Oz Arena: the update check. GPL-2.0.

At launch the game asks GitHub for the latest release (api.github.com's releases/latest) on a thread
of its own, and compares its tag with the build's OZ_VERSION. "Update now" downloads the release's
OzArenaSetup.exe, which the client (oz_online_engine.c) runs in its update mode as the game quits.
Nothing here blocks a frame: the client polls.
===========================================================================
*/
#include "oz_online.h"

#include <curl/curl.h>
#include "cJSON.h"

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

size_t ToFile( char *data, size_t size, size_t n, void *user ) {
	if ( stopping ) {
		return 0;
	}
	const size_t w = fwrite( data, size, n, static_cast<FILE *>( user ) );
	downloadDone += (long long)( w * size );
	return w * size;
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
		if ( !cJSON_IsString( an ) || !cJSON_IsString( au ) ) {
			continue;
		}
		if ( !strcmp( an->valuestring, "OzArenaSetup.exe" ) ) {
			Copy( release.setup, sizeof( release.setup ), au->valuestring );
			release.setupSize = cJSON_IsNumber( as ) ? (long long)as->valuedouble : 0;
		}
		if ( cJSON_IsNumber( as ) ) {
			release.size += (long long)as->valuedouble;
		}
	}
	cJSON_Delete( doc );
	checkState = OZ_UPDATE_DONE;
}

void Download( std::string url, std::string path ) {
	FILE *f = fopen( path.c_str(), "wb" );
	if ( !f ) {
		std::lock_guard<std::mutex> g( lock );
		downloadError = "couldn't write " + path;
		downloadState = OZ_UPDATE_FAILED;
		return;
	}
	long code = 0;
	CURLcode r = CURLE_FAILED_INIT;
	if ( CURL *c = Easy( url ) ) {
		curl_easy_setopt( c, CURLOPT_WRITEFUNCTION, ToFile );
		curl_easy_setopt( c, CURLOPT_WRITEDATA, f );
		curl_easy_setopt( c, CURLOPT_NOPROGRESS, 0L );
		curl_easy_setopt( c, CURLOPT_XFERINFOFUNCTION, Progress );
		curl_easy_setopt( c, CURLOPT_LOW_SPEED_LIMIT, 1L );	// stalled for a minute: give up
		curl_easy_setopt( c, CURLOPT_LOW_SPEED_TIME, 60L );
		r = curl_easy_perform( c );
		curl_easy_getinfo( c, CURLINFO_RESPONSE_CODE, &code );
		curl_easy_cleanup( c );
	}
	const bool closed = fclose( f ) == 0;
	std::lock_guard<std::mutex> g( lock );
	if ( r != CURLE_OK || code != 200 || !closed || downloadDone <= 0 ) {
		downloadError = r != CURLE_OK ? curl_easy_strerror( r ) : code != 200 ? "HTTP " + std::to_string( code ) : "the file came empty";
		remove( path.c_str() );
		downloadState = OZ_UPDATE_FAILED;
		return;
	}
	downloadState = OZ_UPDATE_DONE;
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

void OzUpdate_Download( const char *url, const char *path ) {
	if ( downloadState == OZ_UPDATE_BUSY ) {
		return;
	}
	Join( downloader );
	stopping = false;
	downloadDone = downloadTotal = 0;
	downloadError.clear();
	downloadState = OZ_UPDATE_BUSY;
	downloader = std::thread( Download, std::string( url ), std::string( path ) );
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
