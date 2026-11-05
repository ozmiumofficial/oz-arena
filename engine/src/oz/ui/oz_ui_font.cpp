/*
===========================================================================
Oz Arena: the menus' display font (P5)

id's proportional menu font, menu/art/font1_prop.tga in the player's pak0, traced into a TrueType
font in memory when the UI starts: traced from id's art, it can't ship, so it's made from the
player's own files. The trace is engine/tools/make_ui_font.py's (the Godot build's make_ui.py
trace_ttf), which made it at staging before: each row of the 256x256 atlas is a band of 30 px
cells; a glyph is its ink columns (touching glyphs split at the thinnest column), upscaled 8 times
as Pillow's Lanczos does, cut at half, its outlines traced and simplified by 1.2 px. Capitals only:
lower case maps to them, as in ui_atoms.c.

The outlines follow the pixels' edges (OpenCV's followed their centers, half a pixel of the 8x
image further in), wound as TrueType wants: outer contours clockwise, holes counter-clockwise.
===========================================================================
*/

#include "oz_ui_internal.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace OzUI {

namespace {

struct Row {
	int top;
	const char *chars;
};

// cell bands (top of each row in the atlas), cell height, baseline, gap and space as ui_atoms.c draws them
const char *const ATLAS = "menu/art/font1_prop.tga";
const double PI = 3.14159265358979323846;
const int CELL = 30, ASCENT = 27, GAP = 3, SPACE = 8, UP = 8, UPM = 1000;
const Row ROWS[] = {
	{ 0, "ABCDEFGHIJKLM" }, { 30, "NOPQRSTUVWXY" }, { 60, "Z0123456897" }, { 90, "`-=~" },
	{ 120, "!@#$%^&*()" }, { 150, "_+[]\\/{}<>" }, { 180, "|;:,.'\"?" }
};

typedef std::vector<unsigned char> Bytes;

struct Point {
	int x, y;
};
typedef std::vector<Point> Contour;

struct Glyph {
	int code;					// its character, 0 for .notdef
	int advance;
	std::vector<Contour> contours;	// font units, y up
};

/*
================
DecodeTga: the alpha of an uncompressed or RLE 24 or 32 bit TGA, top row first
================
*/
bool DecodeTga( const unsigned char *d, int len, int &w, int &h, Bytes &alpha ) {
	if ( len < 18 ) {
		return false;
	}
	const int type = d[2], bits = d[16], desc = d[17];
	w = d[12] | d[13] << 8;
	h = d[14] | d[15] << 8;
	if ( ( type != 2 && type != 10 ) || ( bits != 32 && bits != 24 ) || w <= 0 || h <= 0 ) {
		return false;
	}
	const int bpp = bits / 8;
	Bytes px( (size_t)w * h * bpp );
	int pos = 18 + d[0];
	if ( type == 2 ) {
		if ( pos + (int)px.size() > len ) {
			return false;
		}
		memcpy( px.data(), d + pos, px.size() );
	} else {
		size_t out = 0;
		while ( out < px.size() ) {
			if ( pos >= len ) {
				return false;
			}
			const int head = d[pos++], count = ( head & 127 ) + 1;
			if ( head & 128 ) {
				if ( pos + bpp > len ) {
					return false;
				}
				for ( int i = 0; i < count && out < px.size(); i++, out += bpp ) {
					memcpy( &px[out], d + pos, bpp );
				}
				pos += bpp;
			} else {
				for ( int i = 0; i < count && out < px.size(); i++, out += bpp, pos += bpp ) {
					if ( pos + bpp > len ) {
						return false;
					}
					memcpy( &px[out], d + pos, bpp );
				}
			}
		}
	}
	alpha.assign( (size_t)w * h, 255 );
	for ( int y = 0; y < h; y++ ) {
		const int src = ( desc & 32 ) ? y : h - 1 - y;	// bit 5: the first row is the top
		for ( int x = 0; x < w; x++ ) {
			alpha[(size_t)y * w + x] = bpp == 4 ? px[( (size_t)src * w + x ) * 4 + 3] : px[( (size_t)src * w + x ) * 3];
		}
	}
	return true;
}

/*
================
InkRuns: columns with ink -> exactly n [x0, x1] runs: merge the closest pairs or split the widest
================
*/
std::vector<Point> InkRuns( const std::vector<int> &col, int n ) {
	std::vector<Point> runs;	// x = x0, y = x1
	for ( int x = 0; x < (int)col.size(); x++ ) {
		if ( col[x] <= 20 ) {
			continue;
		}
		if ( !runs.empty() && x == runs.back().y + 1 ) {
			runs.back().y = x;
		} else {
			runs.push_back( { x, x } );
		}
	}
	while ( (int)runs.size() > n ) {
		int best = 0;
		for ( int i = 1; i + 1 < (int)runs.size(); i++ ) {
			if ( runs[i + 1].x - runs[i].y < runs[best + 1].x - runs[best].y ) {
				best = i;
			}
		}
		runs[best].y = runs[best + 1].y;
		runs.erase( runs.begin() + best + 1 );
	}
	while ( (int)runs.size() < n && !runs.empty() ) {
		int best = 0;
		for ( int i = 1; i < (int)runs.size(); i++ ) {
			if ( runs[i].y - runs[i].x > runs[best].y - runs[best].x ) {
				best = i;
			}
		}
		const int x0 = runs[best].x, x1 = runs[best].y, w = x1 - x0;
		const int lo = x0 + (int)( w * 0.2 ), hi = x1 - (int)( w * 0.2 );
		int cut = lo;
		for ( int x = lo; x <= hi; x++ ) {
			if ( col[x] < col[cut] ) {
				cut = x;
			}
		}
		runs[best] = { x0, cut - 1 };
		runs.insert( runs.begin() + best + 1, Point{ cut + 1, x1 } );
	}
	return runs;
}

/*
================
Resample: one axis of Pillow's Lanczos resize of an 8 bit image (Resample.c: its coefficients,
22 bit fixed point, rounded and clipped to 8 bits after each pass)
================
*/
double Lanczos( double x ) {
	if ( x < -3.0 || x >= 3.0 ) {
		return 0.0;
	}
	const double a = x * PI, b = x * PI / 3.0;
	return ( x == 0.0 ? 1.0 : sin( a ) / a ) * ( x == 0.0 ? 1.0 : sin( b ) / b );
}

// in: rows of inSize samples spaced by inStep; out: outSize samples
void Resample( const unsigned char *in, int inSize, int inStep, unsigned char *out, int outSize, int outStep ) {
	const int PRECISION = 22;
	const double scale = (double)inSize / outSize, filterscale = scale < 1.0 ? 1.0 : scale, support = 3.0 * filterscale;
	for ( int xx = 0; xx < outSize; xx++ ) {
		const double center = ( xx + 0.5 ) * scale;
		int xmin = (int)( center - support + 0.5 );
		if ( xmin < 0 ) {
			xmin = 0;
		}
		int xmax = (int)( center + support + 0.5 );
		if ( xmax > inSize ) {
			xmax = inSize;
		}
		xmax -= xmin;
		double k[32], ww = 0.0;
		for ( int x = 0; x < xmax && x < 32; x++ ) {
			k[x] = Lanczos( ( x + xmin - center + 0.5 ) / filterscale );
			ww += k[x];
		}
		long long ss = 1LL << ( PRECISION - 1 );
		for ( int x = 0; x < xmax && x < 32; x++ ) {
			const double c = ww != 0.0 ? k[x] / ww : k[x];
			const int kk = (int)( c < 0 ? -0.5 + c * ( 1 << PRECISION ) : 0.5 + c * ( 1 << PRECISION ) );
			ss += (long long)in[( xmin + x ) * inStep] * kk;
		}
		const long long v = ss >> PRECISION;
		out[xx * outStep] = (unsigned char)( v < 0 ? 0 : v > 255 ? 255 : v );
	}
}

/*
================
Trace: a binary image (no ink at its edges) -> its outlines along the pixels' edges, in the
image's units with y down, ink on the left of each step (so clockwise once y points up); where two
ink pixels touch only at a corner, the outline goes on through it (OpenCV's 8-connected ink)
================
*/
std::vector<Contour> Trace( const Bytes &ink, int w, int h ) {
	struct Edge {
		int from, to, dir;	// dir: 0 east, 1 south, 2 west, 3 north
		bool used;
	};
	const int vw = w + 1;
	std::vector<Edge> edges;
	std::vector<int> out( (size_t)vw * ( h + 1 ) * 2, -1 );	// up to two edges leave a corner
	auto at = [&]( int x, int y ) { return x >= 0 && y >= 0 && x < w && y < h && ink[(size_t)y * w + x]; };
	auto add = [&]( int x0, int y0, int x1, int y1, int dir ) {
		const int from = y0 * vw + x0;
		edges.push_back( { from, y1 * vw + x1, dir, false } );
		out[from * 2 + ( out[from * 2] < 0 ? 0 : 1 )] = (int)edges.size() - 1;
	};
	for ( int y = 0; y < h; y++ ) {
		for ( int x = 0; x < w; x++ ) {
			if ( !at( x, y ) ) {
				continue;
			}
			if ( !at( x, y - 1 ) ) {
				add( x + 1, y, x, y, 2 );
			}
			if ( !at( x, y + 1 ) ) {
				add( x, y + 1, x + 1, y + 1, 0 );
			}
			if ( !at( x - 1, y ) ) {
				add( x, y, x, y + 1, 1 );
			}
			if ( !at( x + 1, y ) ) {
				add( x + 1, y + 1, x + 1, y, 3 );
			}
		}
	}
	std::vector<Contour> contours;
	for ( size_t first = 0; first < edges.size(); first++ ) {
		if ( edges[first].used ) {
			continue;
		}
		Contour c;
		int e = (int)first;
		while ( e >= 0 && !edges[e].used ) {
			edges[e].used = true;
			c.push_back( { edges[e].from % vw, edges[e].from / vw } );
			const int a = out[edges[e].to * 2], b = out[edges[e].to * 2 + 1];
			if ( b < 0 ) {
				e = a;
			} else {
				// a corner where ink touches diagonally: turn right (y down), into the other pixel
				const int right = ( edges[e].dir + 1 ) & 3;
				e = edges[a].dir == right ? a : b;
				if ( edges[e].used ) {
					e = e == a ? b : a;
				}
			}
		}
		// corners only
		Contour k;
		for ( size_t i = 0; i < c.size(); i++ ) {
			const Point &p = c[( i + c.size() - 1 ) % c.size()], &q = c[i], &r = c[( i + 1 ) % c.size()];
			if ( ( q.x - p.x ) * ( r.y - q.y ) - ( q.y - p.y ) * ( r.x - q.x ) != 0 ) {
				k.push_back( q );
			}
		}
		if ( k.size() >= 3 ) {
			contours.push_back( k );
		}
	}
	return contours;
}

/*
================
Simplify: a closed outline by Douglas-Peucker, as OpenCV's approxPolyDP (the first point and the
one farthest from it split it in two)
================
*/
void SimplifyRange( const Contour &c, int a, int b, double eps, std::vector<bool> &keep ) {
	const int n = (int)c.size();
	const Point &p = c[a % n], &q = c[b % n];
	const double dx = q.x - p.x, dy = q.y - p.y, len = sqrt( dx * dx + dy * dy );
	double far = -1.0;
	int at = -1;
	for ( int i = a + 1; i < b; i++ ) {
		const Point &r = c[i % n];
		const double d = len > 0 ? fabs( dx * ( r.y - p.y ) - dy * ( r.x - p.x ) ) / len : sqrt( (double)( r.x - p.x ) * ( r.x - p.x ) + (double)( r.y - p.y ) * ( r.y - p.y ) );
		if ( d > far ) {
			far = d;
			at = i;
		}
	}
	if ( at >= 0 && far > eps ) {
		keep[at % n] = true;
		SimplifyRange( c, a, at, eps, keep );
		SimplifyRange( c, at, b, eps, keep );
	}
}

Contour Simplify( const Contour &c, double eps ) {
	const int n = (int)c.size();
	int far = 0;
	double best = -1;
	for ( int i = 1; i < n; i++ ) {
		const double d = (double)( c[i].x - c[0].x ) * ( c[i].x - c[0].x ) + (double)( c[i].y - c[0].y ) * ( c[i].y - c[0].y );
		if ( d > best ) {
			best = d;
			far = i;
		}
	}
	std::vector<bool> keep( n, false );
	keep[0] = keep[far] = true;
	SimplifyRange( c, 0, far, eps, keep );
	SimplifyRange( c, far, n, eps, keep );
	Contour s;
	for ( int i = 0; i < n; i++ ) {
		if ( keep[i] ) {
			s.push_back( c[i] );
		}
	}
	return s;
}

/*
================
The TrueType file
================
*/
void U16( Bytes &b, int v ) {
	b.push_back( (unsigned char)( v >> 8 ) );
	b.push_back( (unsigned char)v );
}

void U32( Bytes &b, unsigned v ) {
	U16( b, (int)( v >> 16 ) );
	U16( b, (int)( v & 0xffff ) );
}

unsigned CheckSum( const Bytes &b ) {
	unsigned sum = 0;
	for ( size_t i = 0; i < b.size(); i += 4 ) {
		unsigned word = 0;
		for ( size_t j = 0; j < 4; j++ ) {
			word = word << 8 | ( i + j < b.size() ? b[i + j] : 0 );
		}
		sum += word;
	}
	return sum;
}

void Name( Bytes &strings, Bytes &records, int id, const char *text ) {
	U16( records, 3 );		// Windows
	U16( records, 1 );		// Unicode BMP
	U16( records, 0x409 );	// English (US)
	U16( records, id );
	U16( records, (int)strlen( text ) * 2 );
	U16( records, (int)strings.size() );
	for ( const char *c = text; *c; c++ ) {
		U16( strings, (unsigned char)*c );
	}
}

Bytes BuildFont( const std::vector<Glyph> &glyphs ) {
	const int ascent = (int)( ASCENT * UPM / (double)CELL ), descent = (int)( ( CELL - ASCENT ) * UPM / (double)CELL );
	Bytes glyf, loca, hmtx;
	int maxPoints = 0, maxContours = 0, maxAdvance = 0, minLsb = 0x7fff, minRsb = 0x7fff, maxExtent = 0;
	int bx0 = 0x7fff, by0 = 0x7fff, bx1 = -0x7fff, by1 = -0x7fff;
	long long advances = 0;
	for ( const Glyph &g : glyphs ) {
		U32( loca, (unsigned)glyf.size() );
		int x0 = 0, y0 = 0, x1 = 0, y1 = 0, points = 0;
		if ( !g.contours.empty() ) {
			x0 = y0 = 0x7fff;
			x1 = y1 = -0x7fff;
			for ( const Contour &c : g.contours ) {
				for ( const Point &p : c ) {
					x0 = p.x < x0 ? p.x : x0;
					y0 = p.y < y0 ? p.y : y0;
					x1 = p.x > x1 ? p.x : x1;
					y1 = p.y > y1 ? p.y : y1;
				}
				points += (int)c.size();
			}
			U16( glyf, (int)g.contours.size() );
			U16( glyf, x0 );
			U16( glyf, y0 );
			U16( glyf, x1 );
			U16( glyf, y1 );
			int end = -1;
			for ( const Contour &c : g.contours ) {
				end += (int)c.size();
				U16( glyf, end );
			}
			U16( glyf, 0 );	// no instructions
			for ( int i = 0; i < points; i++ ) {
				glyf.push_back( 1 );	// on the curve, 16 bit coordinates
			}
			for ( int axis = 0; axis < 2; axis++ ) {
				int last = 0;
				for ( const Contour &c : g.contours ) {
					for ( const Point &p : c ) {
						const int v = axis ? p.y : p.x;
						U16( glyf, v - last );
						last = v;
					}
				}
			}
			while ( glyf.size() & 3 ) {
				glyf.push_back( 0 );
			}
			bx0 = x0 < bx0 ? x0 : bx0;
			by0 = y0 < by0 ? y0 : by0;
			bx1 = x1 > bx1 ? x1 : bx1;
			by1 = y1 > by1 ? y1 : by1;
			minLsb = x0 < minLsb ? x0 : minLsb;
			minRsb = g.advance - x1 < minRsb ? g.advance - x1 : minRsb;
			maxExtent = x1 > maxExtent ? x1 : maxExtent;
		}
		maxPoints = points > maxPoints ? points : maxPoints;
		maxContours = (int)g.contours.size() > maxContours ? (int)g.contours.size() : maxContours;
		maxAdvance = g.advance > maxAdvance ? g.advance : maxAdvance;
		advances += g.advance;
		U16( hmtx, g.advance );
		U16( hmtx, 0 );			// 0, as the web menu's font: FreeType draws the ink from the origin, the gap after it
	}
	U32( loca, (unsigned)glyf.size() );
	const int numGlyphs = (int)glyphs.size();

	Bytes head;
	U32( head, 0x10000 );
	U32( head, 0x10000 );
	U32( head, 0 );				// checkSumAdjustment, set last
	U32( head, 0x5F0F3CF5 );
	U16( head, 1 );				// the baseline at y = 0
	U16( head, UPM );
	for ( int i = 0; i < 4; i++ ) {
		U32( head, 0 );			// created, modified
	}
	U16( head, bx0 );
	U16( head, by0 );
	U16( head, bx1 );
	U16( head, by1 );
	U16( head, 0 );				// macStyle
	U16( head, 8 );				// lowestRecPPEM
	U16( head, 2 );				// fontDirectionHint
	U16( head, 1 );				// long loca
	U16( head, 0 );

	Bytes hhea;
	U32( hhea, 0x10000 );
	U16( hhea, ascent );
	U16( hhea, -descent );
	U16( hhea, 0 );
	U16( hhea, maxAdvance );
	U16( hhea, minLsb == 0x7fff ? 0 : minLsb );
	U16( hhea, minRsb == 0x7fff ? 0 : minRsb );
	U16( hhea, maxExtent );
	U16( hhea, 1 );				// caret slope rise
	for ( int i = 0; i < 7; i++ ) {
		U16( hhea, 0 );			// caret slope run and offset, reserved, metricDataFormat
	}
	U16( hhea, numGlyphs );

	Bytes maxp;
	U32( maxp, 0x10000 );
	U16( maxp, numGlyphs );
	U16( maxp, maxPoints );
	U16( maxp, maxContours );
	U16( maxp, 0 );
	U16( maxp, 0 );
	U16( maxp, 2 );				// maxZones
	for ( int i = 0; i < 8; i++ ) {
		U16( maxp, 0 );
	}

	// one cmap segment per character (format 4), lower case on the capitals
	std::vector<Point> map;		// x = code, y = glyph
	for ( int g = 0; g < numGlyphs; g++ ) {
		const int code = glyphs[g].code;
		if ( code > 0 ) {
			map.push_back( { code, g } );
			if ( code >= 'A' && code <= 'Z' ) {
				map.push_back( { code - 'A' + 'a', g } );
			}
		}
	}
	for ( size_t i = 1; i < map.size(); i++ ) {
		for ( size_t j = i; j > 0 && map[j - 1].x > map[j].x; j-- ) {
			std::swap( map[j - 1], map[j] );
		}
	}
	const int segs = (int)map.size() + 1;
	int range = 1, selector = 0;
	while ( range * 2 <= segs ) {
		range *= 2;
		selector++;
	}
	Bytes sub;
	U16( sub, 4 );
	U16( sub, 16 + segs * 8 );
	U16( sub, 0 );
	U16( sub, segs * 2 );
	U16( sub, range * 2 );
	U16( sub, selector );
	U16( sub, segs * 2 - range * 2 );
	for ( const Point &m : map ) {
		U16( sub, m.x );
	}
	U16( sub, 0xffff );
	U16( sub, 0 );
	for ( const Point &m : map ) {
		U16( sub, m.x );
	}
	U16( sub, 0xffff );
	for ( const Point &m : map ) {
		U16( sub, ( m.y - m.x ) & 0xffff );
	}
	U16( sub, 1 );
	for ( int i = 0; i < segs; i++ ) {
		U16( sub, 0 );
	}
	Bytes cmap;
	U16( cmap, 0 );
	U16( cmap, 1 );
	U16( cmap, 3 );
	U16( cmap, 1 );
	U32( cmap, 12 );
	cmap.insert( cmap.end(), sub.begin(), sub.end() );

	Bytes strings, records, name;
	Name( strings, records, 1, "OzDisplay" );
	Name( strings, records, 2, "Regular" );
	Name( strings, records, 4, "OzDisplay Regular" );
	Name( strings, records, 6, "OzDisplay-Regular" );
	U16( name, 0 );
	U16( name, 4 );
	U16( name, 6 + (int)records.size() );
	name.insert( name.end(), records.begin(), records.end() );
	name.insert( name.end(), strings.begin(), strings.end() );

	Bytes os2;
	U16( os2, 4 );
	U16( os2, (int)( advances / numGlyphs ) );
	U16( os2, 400 );			// regular
	U16( os2, 5 );				// medium width
	U16( os2, 0 );				// installable
	for ( int i = 0; i < 11; i++ ) {
		U16( os2, 0 );			// sub- and superscript, strikeout, family class
	}
	for ( int i = 0; i < 10; i++ ) {
		os2.push_back( 0 );		// panose
	}
	U32( os2, 1 );				// Basic Latin
	U32( os2, 0 );
	U32( os2, 0 );
	U32( os2, 0 );
	os2.insert( os2.end(), { 'O', 'Z', ' ', ' ' } );
	U16( os2, 0x40 );			// regular
	U16( os2, map.empty() ? 32 : map.front().x );
	U16( os2, map.empty() ? 32 : map.back().x );
	U16( os2, ascent );
	U16( os2, -descent );
	U16( os2, 0 );
	U16( os2, ascent );
	U16( os2, descent );
	U32( os2, 1 );				// Latin 1
	U32( os2, 0 );
	U16( os2, ascent );			// x height: capitals only
	U16( os2, ascent );
	U16( os2, 0 );
	U16( os2, 32 );
	U16( os2, 1 );

	Bytes post;
	U32( post, 0x30000 );
	U32( post, 0 );
	U16( post, -75 );
	U16( post, 50 );
	for ( int i = 0; i < 5; i++ ) {
		U32( post, 0 );
	}

	struct Table {
		const char *tag;
		Bytes *data;
	} tables[] = { { "OS/2", &os2 }, { "cmap", &cmap }, { "glyf", &glyf }, { "head", &head }, { "hhea", &hhea },
		{ "hmtx", &hmtx }, { "loca", &loca }, { "maxp", &maxp }, { "name", &name }, { "post", &post } };
	const int count = (int)( sizeof( tables ) / sizeof( tables[0] ) );
	Bytes font;
	U32( font, 0x10000 );
	U16( font, count );
	U16( font, 128 );			// searchRange: 8 tables * 16
	U16( font, 3 );
	U16( font, count * 16 - 128 );
	unsigned offset = 12 + count * 16;
	size_t headAt = 0;
	for ( const Table &t : tables ) {
		font.insert( font.end(), t.tag, t.tag + 4 );
		U32( font, CheckSum( *t.data ) );
		U32( font, offset );
		U32( font, (unsigned)t.data->size() );
		if ( t.data == &head ) {
			headAt = offset;
		}
		offset += ( (unsigned)t.data->size() + 3 ) & ~3u;
	}
	for ( const Table &t : tables ) {
		font.insert( font.end(), t.data->begin(), t.data->end() );
		while ( font.size() & 3 ) {
			font.push_back( 0 );
		}
	}
	const unsigned adjust = 0xB1B0AFBAu - CheckSum( font );
	for ( int i = 0; i < 4; i++ ) {
		font[headAt + 8 + i] = (unsigned char)( adjust >> ( 24 - 8 * i ) );
	}
	return font;
}

Bytes displayFont;	// RmlUi reads it until it shuts down

}	// namespace

/*
================
Font_MakeDisplay: the display font's TrueType bytes from the player's paks; empty without them
================
*/
const std::vector<unsigned char> &Font_MakeDisplay( Rml::String &how ) {
	if ( !displayFont.empty() ) {
		return displayFont;
	}
	void *buffer = nullptr;
	const int len = OzE_ReadFile( ATLAS, &buffer );
	if ( len < 0 ) {
		how = Rml::String( "no " ) + ATLAS + " (the display text falls back to Barlow)";
		return displayFont;
	}
	int aw = 0, ah = 0;
	Bytes alpha;
	const bool decoded = DecodeTga( (const unsigned char *)buffer, len, aw, ah, alpha );
	OzE_FreeFile( buffer );
	if ( !decoded || aw < 256 || ah < 210 ) {
		how = Rml::String( ATLAS ) + " isn't a 256x256 TGA (the display text falls back to Barlow)";
		return displayFont;
	}
	const double u = UPM / (double)CELL;	// font units per atlas pixel
	std::vector<Glyph> glyphs;
	glyphs.push_back( { 0, (int)( SPACE * u ), {} } );		// .notdef
	glyphs.push_back( { ' ', (int)( SPACE * u ), {} } );
	for ( const Row &row : ROWS ) {
		std::vector<int> col( aw, 0 );
		for ( int y = row.top; y < row.top + CELL; y++ ) {
			for ( int x = 0; x < aw; x++ ) {
				col[x] = alpha[(size_t)y * aw + x] > col[x] ? alpha[(size_t)y * aw + x] : col[x];
			}
		}
		const int n = (int)strlen( row.chars );
		const std::vector<Point> runs = InkRuns( col, n );
		for ( int c = 0; c < n && c < (int)runs.size(); c++ ) {
			const int x0 = runs[c].x, w = runs[c].y - runs[c].x + 1;
			const int cw = w + 2, ch = CELL + 2, bw = cw * UP, bh = ch * UP;
			// the glyph with a pixel of border, 8 times bigger: across, then down, as Pillow does
			Bytes canvas( (size_t)cw * ch, 0 ), across( (size_t)bw * ch ), big( (size_t)bw * bh );
			for ( int y = 0; y < CELL; y++ ) {
				for ( int x = 0; x < w; x++ ) {
					canvas[(size_t)( y + 1 ) * cw + x + 1] = alpha[(size_t)( row.top + y ) * aw + x0 + x];
				}
			}
			for ( int y = 0; y < ch; y++ ) {
				Resample( &canvas[(size_t)y * cw], cw, 1, &across[(size_t)y * bw], bw, 1 );
			}
			for ( int x = 0; x < bw; x++ ) {
				Resample( &across[x], ch, bw, &big[x], bh, bw );
			}
			for ( unsigned char &p : big ) {
				p = p >= 128;
			}
			Glyph g = { (unsigned char)row.chars[c], (int)lround( ( w + GAP ) * u ), {} };
			for ( const Contour &outline : Trace( big, bw, bh ) ) {
				const Contour s = Simplify( outline, 1.2 );
				if ( s.size() < 3 ) {
					continue;
				}
				// the 8x image's pixel corners (a pixel of border) -> font units, y up from the baseline;
				// a corner is half a pixel before the center OpenCV's outline went through
				Contour f;
				for ( const Point &p : s ) {
					f.push_back( { (int)lround( ( ( p.x - 0.5 ) / UP - 1 ) * u ), (int)lround( ( ASCENT - ( ( p.y - 0.5 ) / UP - 1 ) ) * u ) } );
				}
				g.contours.push_back( f );
			}
			glyphs.push_back( g );
		}
	}
	displayFont = BuildFont( glyphs );
	how = Rml::String( "traced " ) + ATLAS + ", " + std::to_string( glyphs.size() - 2 ) + " glyphs";
	return displayFont;
}

/*
================
Font_Emoji: Windows' own Segoe UI Emoji, for emoji in chat (oz_emoji.c); empty without it. Its color
glyphs are layers (COLR), which FreeType draws. Read once: RmlUi reads it until it shuts down.
On Linux, the first layered (COLRv0) emoji font a distro ships: Twemoji's. Noto Color Emoji is PNG
bitmaps (CBDT), which Oz's FreeType leaves out (FT_DISABLE_PNG), so it isn't one of them.
================
*/
const std::vector<unsigned char> &Font_Emoji( Rml::String &how ) {
	static Bytes emoji;
	static bool tried;
	static Rml::String name;
	if ( tried ) {
		how = emoji.empty() ? "none" : name;
		return emoji;
	}
	tried = true;
	name = "Segoe UI Emoji";
#ifdef _WIN32
	const char *windows = getenv( "WINDIR" );
	const std::string path = std::string( windows && *windows ? windows : "C:\\Windows" ) + "\\Fonts\\seguiemj.ttf";
	if ( FILE *f = fopen( path.c_str(), "rb" ) ) {
		fseek( f, 0, SEEK_END );
		const long len = ftell( f );
		fseek( f, 0, SEEK_SET );
		if ( len > 0 ) {
			emoji.resize( (size_t)len );
			if ( fread( emoji.data(), 1, emoji.size(), f ) != emoji.size() ) {
				emoji.clear();
			}
		}
		fclose( f );
	}
	how = emoji.empty() ? "no " + path + " (chat shows emoji codes)" : "Segoe UI Emoji";
#else
	static const char *paths[] = {
		"/usr/share/fonts/truetype/twemoji/TwemojiMozilla.ttf",		// Debian, Ubuntu: fonts-twemoji
		"/usr/share/fonts/twemoji/Twemoji.ttf",
		"/usr/share/fonts/TTF/TwemojiMozilla.ttf",
		"/usr/share/fonts/twitter-twemoji/Twemoji.ttf",				// Fedora
		"/usr/share/fonts/truetype/twemoji/Twemoji.ttf" };
	for ( const char *path : paths ) {
		if ( FILE *f = fopen( path, "rb" ) ) {
			fseek( f, 0, SEEK_END );
			const long len = ftell( f );
			fseek( f, 0, SEEK_SET );
			if ( len > 0 ) {
				emoji.resize( (size_t)len );
				if ( fread( emoji.data(), 1, emoji.size(), f ) != emoji.size() ) {
					emoji.clear();
				}
			}
			fclose( f );
			if ( !emoji.empty() ) {
				name = path;
				break;
			}
		}
	}
	how = emoji.empty() ? Rml::String( "no Twemoji font (chat shows emoji codes)" ) : name;
#endif
	return emoji;
}

} // namespace OzUI
