/*
Oz: the map editor's thumbnails, ready before the editor is used (ENGINE_PLAN.md P13, engine/EDITOR.md).

The Materials browser shows a tile of every material (10,166 in Ozmium's install). Made as a page came
into view, each tile read its whole image (a remastered .dds is 1.4 MB for its smallest mips; a map pack's
image opened its pak again, the file system keeping 250 open), three a frame: a page switch stalled ten
frames of 15 to 60 ms. Now every tile is read once into <fs_homepath>/oz/editor/thumbs.cache (the cvar
ed_thumbcache names another file: the tests' shared one) and held in memory while the game runs, so a
page's tiles are made from memory in a frame (a 128-texel tile is 8 or 16 KB; a texture from it takes
microseconds).

As the editor opens, Thumbs_Frame does the work a slice a frame, the editor's page showing a card with
its progress: the game's materials listed (ShaderLib::materials), each picture's files checked against
the cache (OzE_ImageStamp: the files the renderer or q3map2 may read for it, their sizes and paks), and
the new or changed ones read (the renderer's ReadThumbnail), a pak's images together. The first time
reads them all; after that, only a new map pack's.

Records: [u16 name length][name][u32 stamp][u8 format][u16 width][u16 height][u32 bytes][bytes], after
an 8-byte header ("OZTH", version). Appended as they're read, so a run stopped halfway keeps what it read;
the last record of a name wins, and the file is written again without the stale ones when they're a
quarter of it. A picture the renderer gives as RGBA (id's .tga and .jpg, every map pack's) is kept as
BC1 (DXT1), compressed here; the remastered .dds mips as they are.
*/
#include "oz_ui_internal.h"
#include "../editor/ed_engine.h"
#include "../editor/ed_shaders.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace OzUI {

namespace {

const char MAGIC[4] = { 'O', 'Z', 'T', 'H' };
const uint32_t VERSION = 1;
const int SIZE = 128;			// texels across at most: a tile is 5.9 rem, about 120 px at 1440p
const uint8_t LIVE = 255;		// a picture the cache can't hold (a .dds without small mips): made when shown

struct Thumb {
	uint32_t stamp = 0;
	uint8_t format = OZ_UI_THUMB_NONE;	// NONE: no picture there
	uint16_t w = 0, h = 0;
	size_t offset = 0;		// in held
	uint32_t size = 0;
};

enum class Stage { Idle, List, Check, Read, Ready };

Stage stage = Stage::Idle;
std::unordered_map<std::string, Thumb> thumbs;	// by image path, lowercase
std::vector<uint8_t> held;			// every picture's bytes (the cache file as read, then the new ones)
size_t fileBytes, staleBytes;		// the cache file's size; the bytes in it a later record replaced
bool cutShort;						// it ended in a record cut short (the game closed mid-write): written again before more go on
std::vector<std::string> images;	// the browser's pictures, each once
size_t checkAt;
struct Todo {
	std::string image;
	uint32_t stamp;
	int pak;
};
std::vector<Todo> todo;
size_t readAt;
std::vector<Todo> bgTodo;			// kept ones whose files changed, read again in the background
size_t bgCheckAt, bgReadAt;
std::string pending;				// the picture being read: still set next frame when its read dropped the game
FILE *out;							// the cache file, appended to while reading
std::string cachePath;
int startedMs, listMs, checkMs, readMs;
bool hadCache;						// the cache file had pictures as the editor opened
int readCount, liveCount, missingCount;

std::string Lower( std::string s ) {
	std::transform( s.begin(), s.end(), s.begin(), []( unsigned char c ) { return (char)tolower( c ); } );
	return s;
}

std::string CachePath() {
	const char *c = OzE_CvarString( "ed_thumbcache" );
	if ( c && *c ) {
		return c;
	}
	return std::string( OzEd_HomePath() ) + "/oz/editor/thumbs.cache";
}

template <typename T> void Put( std::vector<uint8_t> &b, T v ) {
	for ( size_t i = 0; i < sizeof( T ); i++ ) {
		b.push_back( uint8_t( uint64_t( v ) >> ( 8 * i ) ) );
	}
}

template <typename T> bool Get( const uint8_t *&p, const uint8_t *end, T &v ) {
	if ( size_t( end - p ) < sizeof( T ) ) {
		return false;
	}
	uint64_t x = 0;
	for ( size_t i = 0; i < sizeof( T ); i++ ) {
		x |= uint64_t( p[i] ) << ( 8 * i );
	}
	v = T( x );
	p += sizeof( T );
	return true;
}

// the cache file into held; its records into thumbs
void LoadCache() {
	thumbs.clear();
	held.clear();
	fileBytes = staleBytes = 0;
	cutShort = false;
	cachePath = CachePath();
	hadCache = false;
	FILE *f = (FILE *)OzE_FOpenOS( cachePath.c_str(), "rb" );
	if ( !f ) {
		return;
	}
	fseek( f, 0, SEEK_END );
	const long len = ftell( f );
	fseek( f, 0, SEEK_SET );
	if ( len > 8 ) {
		held.resize( size_t( len ) );
		if ( fread( held.data(), 1, held.size(), f ) != held.size() ) {
			held.clear();
		}
	}
	fclose( f );
	if ( held.size() < 8 || memcmp( held.data(), MAGIC, 4 ) || held[4] != VERSION ) {
		held.clear();	// another version's or broken: read again
		return;
	}
	fileBytes = held.size();
	const uint8_t *p = held.data() + 8, *end = held.data() + held.size();
	while ( p < end ) {
		uint16_t n;
		Thumb t;
		if ( !Get( p, end, n ) || size_t( end - p ) < n ) {
			break;
		}
		std::string name( (const char *)p, n );
		p += n;
		if ( !Get( p, end, t.stamp ) || !Get( p, end, t.format ) || !Get( p, end, t.w ) || !Get( p, end, t.h ) || !Get( p, end, t.size ) ||
				size_t( end - p ) < t.size ) {
			break;	// cut short (the game closed mid-write): what's before it stays
		}
		t.offset = size_t( p - held.data() );
		p += t.size;
		auto it = thumbs.find( name );
		if ( it != thumbs.end() ) {
			staleBytes += 2 + name.size() + 15 + it->second.size;
		}
		thumbs[name] = t;
	}
	cutShort = p != end;
	fileBytes = size_t( p - held.data() );
	hadCache = !thumbs.empty();
}

void Record( std::vector<uint8_t> &b, const std::string &name, const Thumb &t, const uint8_t *bytes ) {
	Put<uint16_t>( b, uint16_t( name.size() ) );
	b.insert( b.end(), name.begin(), name.end() );
	Put<uint32_t>( b, t.stamp );
	Put<uint8_t>( b, t.format );
	Put<uint16_t>( b, t.w );
	Put<uint16_t>( b, t.h );
	Put<uint32_t>( b, t.size );
	b.insert( b.end(), bytes, bytes + t.size );
}

void Compact();

// a picture read: held, and appended to the cache file
void Keep( const std::string &name, Thumb t, const uint8_t *bytes ) {
	auto it = thumbs.find( name );
	if ( it != thumbs.end() ) {
		staleBytes += 2 + name.size() + 15 + it->second.size;
	}
	t.offset = held.size();
	held.insert( held.end(), bytes, bytes + t.size );
	thumbs[name] = t;
	if ( !out && cutShort ) {
		Compact();	// what was read whole, so what's appended next is read back
		cutShort = false;
	}
	if ( !out ) {
		OzEd_MakeDirs( cachePath.c_str() );
		out = (FILE *)OzE_FOpenOS( cachePath.c_str(), fileBytes ? "ab" : "wb" );
		if ( out && !fileBytes ) {
			fwrite( MAGIC, 1, 4, out );
			const uint8_t v[4] = { uint8_t( VERSION ), 0, 0, 0 };
			fwrite( v, 1, 4, out );
			fileBytes = 8;
		}
	}
	if ( out ) {
		std::vector<uint8_t> rec;
		Record( rec, name, t, bytes );
		fwrite( rec.data(), 1, rec.size(), out );
		fileBytes += rec.size();
	}
}

// the cache file again, with the current record of each picture the browser has (stale and gone ones left out)
void Compact() {
	std::vector<uint8_t> b( MAGIC, MAGIC + 4 );
	Put<uint32_t>( b, VERSION );
	std::vector<std::string> names( images );
	std::sort( names.begin(), names.end() );
	for ( const std::string &n : names ) {
		auto it = thumbs.find( n );
		if ( it != thumbs.end() ) {
			Record( b, n, it->second, held.data() + it->second.offset );
		}
	}
	if ( OzEd_WriteOSFile( cachePath.c_str(), b.data(), int( b.size() ) ) ) {
		fileBytes = b.size();
		staleBytes = 0;
	}
}

// ---- BC1 (DXT1), opaque: each 4x4 block's colors along their main axis (its covariance's), the ends
// rounded to 565 and each texel the nearest of the four colors between them
uint16_t To565( const float c[3] ) {
	auto q = []( float v, int bits ) { return int( std::lround( std::clamp( v, 0.f, 255.f ) * ( ( 1 << bits ) - 1 ) / 255.f ) ); };
	return uint16_t( ( q( c[0], 5 ) << 11 ) | ( q( c[1], 6 ) << 5 ) | q( c[2], 5 ) );
}

void From565( uint16_t v, int c[3] ) {
	const int r = v >> 11, g = ( v >> 5 ) & 63, b = v & 31;
	c[0] = ( r << 3 ) | ( r >> 2 ), c[1] = ( g << 2 ) | ( g >> 4 ), c[2] = ( b << 3 ) | ( b >> 2 );
}

void Block( const uint8_t px[16][3], uint8_t outb[8] ) {
	float mean[3] = {};
	for ( int i = 0; i < 16; i++ ) {
		for ( int c = 0; c < 3; c++ ) {
			mean[c] += px[i][c] / 16.f;
		}
	}
	float cov[6] = {};	// xx xy xz yy yz zz
	for ( int i = 0; i < 16; i++ ) {
		const float d[3] = { px[i][0] - mean[0], px[i][1] - mean[1], px[i][2] - mean[2] };
		cov[0] += d[0] * d[0], cov[1] += d[0] * d[1], cov[2] += d[0] * d[2];
		cov[3] += d[1] * d[1], cov[4] += d[1] * d[2], cov[5] += d[2] * d[2];
	}
	float axis[3] = { 0.577f, 0.577f, 0.577f };
	for ( int it = 0; it < 6; it++ ) {
		const float x = cov[0] * axis[0] + cov[1] * axis[1] + cov[2] * axis[2];
		const float y = cov[1] * axis[0] + cov[3] * axis[1] + cov[4] * axis[2];
		const float z = cov[2] * axis[0] + cov[4] * axis[1] + cov[5] * axis[2];
		const float l = std::sqrt( x * x + y * y + z * z );
		if ( l < 1e-6f ) {
			break;
		}
		axis[0] = x / l, axis[1] = y / l, axis[2] = z / l;
	}
	float lo = 1e9f, hi = -1e9f;
	for ( int i = 0; i < 16; i++ ) {
		const float t = ( px[i][0] - mean[0] ) * axis[0] + ( px[i][1] - mean[1] ) * axis[1] + ( px[i][2] - mean[2] ) * axis[2];
		lo = std::min( lo, t ), hi = std::max( hi, t );
	}
	const float a[3] = { mean[0] + axis[0] * hi, mean[1] + axis[1] * hi, mean[2] + axis[2] * hi };
	const float b[3] = { mean[0] + axis[0] * lo, mean[1] + axis[1] * lo, mean[2] + axis[2] * lo };
	uint16_t c0 = To565( a ), c1 = To565( b );
	if ( c0 < c1 ) {
		std::swap( c0, c1 );
	}
	uint32_t idx = 0;
	if ( c0 != c1 ) {	// c0 > c1: the four-color mode
		int p[4][3];
		From565( c0, p[0] );
		From565( c1, p[1] );
		for ( int c = 0; c < 3; c++ ) {
			p[2][c] = ( 2 * p[0][c] + p[1][c] ) / 3;
			p[3][c] = ( p[0][c] + 2 * p[1][c] ) / 3;
		}
		for ( int i = 0; i < 16; i++ ) {
			int best = 0, bestD = 1 << 30;
			for ( int k = 0; k < 4; k++ ) {
				const int dr = px[i][0] - p[k][0], dg = px[i][1] - p[k][1], db = px[i][2] - p[k][2];
				const int d = dr * dr + dg * dg + db * db;
				if ( d < bestD ) {
					bestD = d, best = k;
				}
			}
			idx |= uint32_t( best ) << ( 2 * i );
		}
	}
	outb[0] = uint8_t( c0 ), outb[1] = uint8_t( c0 >> 8 );
	outb[2] = uint8_t( c1 ), outb[3] = uint8_t( c1 >> 8 );
	for ( int i = 0; i < 4; i++ ) {
		outb[4 + i] = uint8_t( idx >> ( 8 * i ) );
	}
}

std::vector<uint8_t> ToBC1( const uint8_t *rgba, int w, int h ) {
	const int bw = ( w + 3 ) / 4, bh = ( h + 3 ) / 4;
	std::vector<uint8_t> o( size_t( bw ) * bh * 8 );
	for ( int by = 0; by < bh; by++ ) {
		for ( int bx = 0; bx < bw; bx++ ) {
			uint8_t px[16][3];
			for ( int y = 0; y < 4; y++ ) {
				for ( int x = 0; x < 4; x++ ) {
					const int sx = std::min( bx * 4 + x, w - 1 ), sy = std::min( by * 4 + y, h - 1 );	// a partial block repeats its edge
					const uint8_t *s = rgba + ( size_t( sy ) * w + sx ) * 4;
					px[y * 4 + x][0] = s[0], px[y * 4 + x][1] = s[1], px[y * 4 + x][2] = s[2];
				}
			}
			Block( px, &o[( size_t( by ) * bw + bx ) * 8] );
		}
	}
	return o;
}

int Since( std::chrono::steady_clock::time_point t ) {
	return int( std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::steady_clock::now() - t ).count() );
}

void Finish() {
	if ( out ) {
		fclose( out );
		out = nullptr;
	}
	if ( staleBytes * 4 > fileBytes ) {
		Compact();
	}
	for ( const std::string &n : images ) {
		auto it = thumbs.find( n );
		if ( it != thumbs.end() && it->second.format == OZ_UI_THUMB_NONE ) {
			ThumbMarkFailed( n );	// the browser leaves it out
		}
	}
	stage = Stage::Ready;
	Printf( "map editor: %d material pictures ready in %.1f s (listed %.1f s, checked %.1f s, %d read in %.1f s, %d made when shown, %d missing); %s, %.1f MB\n",
		int( images.size() ), ( OzE_Milliseconds() - startedMs ) / 1000.0, listMs / 1000.0, checkMs / 1000.0, readCount, readMs / 1000.0,
		liveCount, missingCount, cachePath.c_str(), fileBytes / 1048576.0 );
}

}  // namespace

// Reads list[at...] for up to budget ms (false: the renderer is down, on with it next frame).
static bool ReadSome( std::vector<Todo> &list, size_t &at, int budget ) {
	const auto start = std::chrono::steady_clock::now();
	const ozUiRender_t *r = OzE_Renderer();
	if ( !r ) {
		return false;	// the renderer starting again
	}
	if ( !r->ReadThumbnail ) {
		at = list.size();	// a renderer without it: the tiles load as they show
		return true;
	}
	static std::vector<uint8_t> buf( SIZE * SIZE * 4 );
	if ( !pending.empty() ) {
		// its read never came back (an image loader's error dropped the game): kept as no picture, so
		// it isn't asked for again
		Thumb th;
		th.stamp = at > 0 ? list[at - 1].stamp : 0;
		Keep( pending, th, buf.data() );
		missingCount++;
		Printf( "^3map editor: the picture %s couldn't be read\n", pending.c_str() );
		pending.clear();
	}
	while ( at < list.size() && Since( start ) < budget ) {
		const Todo &t = list[at++];
		int w = 0, h = 0, format = 0;
		pending = t.image;
		const int n = r->ReadThumbnail( t.image.c_str(), SIZE, &w, &h, &format, buf.data(), int( buf.size() ) );
		pending.clear();
		Thumb th;
		th.stamp = t.stamp;
		if ( n > 0 && format == OZ_UI_THUMB_RGBA8 ) {
			const std::vector<uint8_t> bc = ToBC1( buf.data(), w, h );
			th.format = OZ_UI_THUMB_BC1, th.w = uint16_t( w ), th.h = uint16_t( h ), th.size = uint32_t( bc.size() );
			Keep( t.image, th, bc.data() );
		} else if ( n > 0 ) {
			th.format = uint8_t( format ), th.w = uint16_t( w ), th.h = uint16_t( h ), th.size = uint32_t( n );
			Keep( t.image, th, buf.data() );
		} else {
			th.format = n < 0 ? LIVE : OZ_UI_THUMB_NONE;
			( n < 0 ? liveCount : missingCount )++;
			Keep( t.image, th, buf.data() );
		}
		readCount++;
	}
	readMs += Since( start );
	return true;
}

bool Thumbs_Ready() {
	return stage == Stage::Ready;
}

void Thumbs_Frame() {
	const auto start = std::chrono::steady_clock::now();
	switch ( stage ) {
	case Stage::Idle:
		startedMs = OzE_Milliseconds();
		stage = Stage::List;	// the card shows this frame; the long listing is the next one's
		return;
	case Stage::List: {
		LoadCache();
		oz::ed::ShaderLib &lib = oz::ed::shaders_lib();
		std::unordered_map<std::string, bool> seen;
		images.clear();
		for ( const oz::ed::Material &m : lib.materials() ) {
			if ( m.image.empty() || !lib.found( m.name ) ) {
				continue;	// a tool's color, or NO IMAGE
			}
			const std::string img = Lower( lib.image( m.name ) );
			if ( seen.emplace( img, true ).second ) {
				images.push_back( img );
			}
		}
		listMs = Since( start );
		checkAt = 0;
		todo.clear();
		stage = Stage::Check;
		return;
	}
	case Stage::Check: {
		// the pictures the cache doesn't have (all of them the first time): read before the editor's used
		while ( checkAt < images.size() && Since( start ) < 20 ) {
			const std::string &img = images[checkAt++];
			if ( !thumbs.count( img ) ) {
				int pak = 0;
				const uint32_t stamp = OzE_ImageStamp( img.c_str(), &pak );
				todo.push_back( { img, stamp, pak } );
			}
		}
		checkMs += Since( start );
		if ( checkAt >= images.size() ) {
			// a pak's pictures together: the file system keeps 250 paks open, and opening one again reads its directory
			std::stable_sort( todo.begin(), todo.end(), []( const Todo &a, const Todo &b ) { return a.pak < b.pak; } );
			readAt = 0;
			stage = Stage::Read;
			if ( todo.empty() ) {
				Finish();
			}
		}
		return;
	}
	case Stage::Read:
		if ( ReadSome( todo, readAt, 25 ) && readAt >= todo.size() ) {
			Finish();
		}
		return;
	case Stage::Ready: {
		// the kept ones checked against the game's files in the background, a little a frame: one whose
		// files changed (a map pack replaced, a custom texture) is read again, and shows so from the next
		// time the game starts (the page keeps the texture it made of the old one)
		if ( bgCheckAt < images.size() ) {
			while ( bgCheckAt < images.size() && Since( start ) < 2 ) {
				const std::string &img = images[bgCheckAt++];
				auto it = thumbs.find( img );
				int pak = 0;
				const uint32_t stamp = OzE_ImageStamp( img.c_str(), &pak );
				if ( it != thumbs.end() && it->second.stamp != stamp ) {
					bgTodo.push_back( { img, stamp, pak } );
				}
			}
			if ( bgCheckAt >= images.size() && !bgTodo.empty() ) {
				Printf( "map editor: %d material pictures changed since they were kept: read again\n", int( bgTodo.size() ) );
			}
		} else if ( bgReadAt < bgTodo.size() ) {
			ReadSome( bgTodo, bgReadAt, 4 );
			if ( bgReadAt >= bgTodo.size() && out ) {
				fclose( out );
				out = nullptr;
			}
		}
		return;
	}
	default:
		return;
	}
}

bool Thumbs_Progress( std::string &what, int &done, int &total ) {
	switch ( stage ) {
	case Stage::Idle:
	case Stage::List:
		what = "Listing the game's materials";
		done = 0, total = 0;
		return true;
	case Stage::Check:
		what = "Finding the pictures not kept yet";
		done = int( checkAt ), total = int( images.size() );
		return true;
	case Stage::Read:
		what = hadCache ? "Reading the new material pictures" : "Reading every material's picture";
		done = int( readAt ), total = int( todo.size() );
		return true;
	default:
		return false;
	}
}

int Thumbs_Texture( const std::string &image, int &w, int &h, bool &missing ) {
	missing = false;
	auto it = thumbs.find( Lower( image ) );
	if ( it == thumbs.end() || it->second.format == LIVE ) {
		return 0;	// not held: the renderer makes it whole
	}
	const Thumb &t = it->second;
	if ( t.format == OZ_UI_THUMB_NONE ) {
		missing = true;
		return 0;
	}
	const ozUiRender_t *r = OzE_Renderer();
	if ( !r || !r->CreateThumbnail ) {
		return 0;
	}
	const int handle = r->CreateThumbnail( t.format, t.w, t.h, held.data() + t.offset, int( t.size ) );
	if ( handle ) {
		w = t.w, h = t.h;
	}
	return handle;
}

bool Thumbs_Held( const std::string &image ) {
	auto it = thumbs.find( Lower( image ) );
	return it != thumbs.end() && it->second.format != LIVE;
}

std::string Thumbs_Stats() {
	char line[512];
	static const char *const names[] = { "idle", "listing", "checking", "reading", "ready" };
	snprintf( line, sizeof line, "thumbs: %s, %d pictures, %d held, %d read this run (%d made when shown, %d missing), %s %.1f MB",
		names[int( stage )], int( images.size() ), int( thumbs.size() ), readCount, liveCount, missingCount, cachePath.c_str(), fileBytes / 1048576.0 );
	return line;
}

}  // namespace OzUI
