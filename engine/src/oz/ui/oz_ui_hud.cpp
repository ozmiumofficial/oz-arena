/*
Oz: the HUD canvas (ENGINE_PLAN.md P6). The HUD (oz_hud.c) draws through it: triangles with an
optional image, and text in the UI's own fonts (Barlow and the display font) with Quake 3's color
codes, tabular figures and a soft outline. What the HUD sends during a frame is drawn under the
pages at that frame's OzUI_Draw, in the order sent, then dropped. Every renderer has the UI's calls
(oz, opengl and vulkan); one that can't start them (OpenGL before 3.2) keeps the game code's HUD.

Text is laid out once and kept: most of the HUD's text is the same from frame to frame, and
shaping it and making its geometry anew every frame cost more than the rest of the HUD together.
A text is cached by everything but its place, laid out from its pen start, and drawn moved there.
*/
#include "oz_ui_internal.h"
#include "oz_ui.h"

#include <cmath>
#include <cstring>
#include <deque>
#include <map>
#include <tuple>
#include <unordered_map>

namespace OzUI {

namespace {

// the fonts, as cgame numbers them (OZ_HUD_FONT_* in the game code's cg_oz_hud.h)
struct FontDef {
	const char *family;
	Rml::Style::FontWeight weight;
	bool tabular;		// tabular figures: every digit as wide as the widest (Godot's "tnum")
};
const FontDef fontDefs[] = {
	{ "barlow", Rml::Style::FontWeight( 600 ), true },	// 0 numbers (SemiBold)
	{ "barlow", Rml::Style::FontWeight( 500 ), true },	// 1 medium text
	{ "ozdisplay", Rml::Style::FontWeight::Normal, false },	// 2 labels (Quake 3's own font, traced)
	{ "barlow", Rml::Style::FontWeight::Normal, true },	// 3 regular
	{ "barlow", Rml::Style::FontWeight::Bold, true },		// 4 bold
};
const int numFonts = sizeof( fontDefs ) / sizeof( fontDefs[0] );

// id 1.32's eight, black and blue lifted as the Godot build (and the web menu) lift them
const Rml::Colourb colorTable[8] = {
	{ 0x5a, 0x5a, 0x5a, 255 }, { 0xff, 0x00, 0x00, 255 }, { 0x00, 0xff, 0x00, 255 }, { 0xff, 0xff, 0x00, 255 },
	{ 0x4a, 0x6b, 0xff, 255 }, { 0x00, 0xff, 0xff, 255 }, { 0xff, 0x00, 0xff, 255 }, { 0xff, 0xff, 0xff, 255 },
};

struct Face {
	Rml::FontFaceHandle handle = 0;
	float digit = 0.0f;			// the widest digit's advance
};

struct Run {
	Rml::String text;
	Rml::Colourb color;
};

struct TextPiece {
	Rml::Geometry geometry;
	Rml::Texture texture;
};

// a text as one call laid it out, from its pen start
struct CachedText {
	const Face *face = nullptr;
	Rml::FontEffectsHandle fx = 0;
	std::vector<Run> runs;			// cut to fit
	float width = 0.0f;
	std::vector<TextPiece> pieces;
	bool laidOut = false;			// pieces made (a measure alone makes none)
	int version = -1;				// the face's when laid out: RmlUi's atlas changes as glyphs come
	int used = 0;					// last used (OzE_Milliseconds)
};

// what a frame draws, in order: a shape of the frame's, or a piece of a cached text moved to its place
struct Item {
	const Rml::Geometry *shape = nullptr;
	Rml::Texture texture;			// the shape's
	CachedText *text = nullptr;
	size_t piece = 0;
	Rml::Vector2f at;
};

std::vector<Rml::String> imageNames;			// handle - 1
std::vector<Rml::Texture> images;
std::map<std::pair<int, int>, Face> faces;		// (font, size)
std::map<std::tuple<Rml::FontFaceHandle, int, int>, Rml::FontEffectsHandle> effects;	// (face, width, alpha)
std::unordered_map<Rml::String, CachedText> texts;
std::vector<Item> items;
std::deque<Rml::Geometry> frameShapes;			// this frame's shape geometry (items point into it)
Rml::Mesh shapes;					// triangles waiting to become an item
int shapesImage = -1;
int lastSweep;						// when unused texts were last let go
const Rml::String language = "en";
const int textKeep = 250;			// ms an unused text stays cached (30 frames at 125 fps; counted in frames,
									// a text shown every other 30 ms was laid out again each time at 1000)
const size_t textMax = 4096;		// more than that is text changing every frame: start over

Rml::RenderManager *Manager() {
	Rml::Context *c = GetContext();
	return c ? &c->GetRenderManager() : nullptr;
}

void FlushShapes() {
	Rml::RenderManager *m = Manager();
	if ( !shapes.vertices.empty() && m ) {
		frameShapes.push_back( m->MakeGeometry( std::move( shapes ) ) );
		Item it;
		it.shape = &frameShapes.back();
		it.texture = shapesImage > 0 && shapesImage <= (int)images.size() ? images[shapesImage - 1] : Rml::Texture();
		items.push_back( it );
	}
	shapes = Rml::Mesh();
	shapesImage = -1;
}

const Face *GetFace( int font, int size ) {
	if ( font < 0 || font >= numFonts ) {
		font = 0;
	}
	if ( size < 4 ) {
		size = 4;
	} else if ( size > 256 ) {
		size = 256;
	}
	Face &f = faces[{ font, size }];
	if ( !f.handle ) {
		const long long prof = OzE_ProfStart();
		struct Done { long long p; ~Done() { OzE_ProfDetail( "hud new face", p ); } } done{ prof };
		Rml::FontEngineInterface *fe = Rml::GetFontEngineInterface();
		f.handle = fe->GetFontFaceHandle( fontDefs[font].family, Rml::Style::FontStyle::Normal, fontDefs[font].weight, size );
		if ( f.handle && fontDefs[font].tabular ) {
			const Rml::TextShapingContext ctx{ language };
			for ( char d = '0'; d <= '9'; d++ ) {
				const float w = (float)fe->GetStringWidth( f.handle, Rml::StringView( &d, &d + 1 ), ctx );
				if ( w > f.digit ) {
					f.digit = w;
				}
			}
		}
	}
	return f.handle ? &f : nullptr;
}

Rml::FontEffectsHandle GetOutline( Rml::FontFaceHandle face, int width, int alpha ) {
	if ( width <= 0 || alpha <= 0 ) {
		return 0;
	}
	auto key = std::make_tuple( face, width, alpha );
	auto found = effects.find( key );
	if ( found != effects.end() ) {
		return found->second;
	}
	const long long prof = OzE_ProfStart();
	struct Done { long long p; ~Done() { OzE_ProfDetail( "hud new outline", p ); } } done{ prof };
	Rml::FontEffectsHandle h = 0;
	Rml::PropertyDictionary props;
	if ( Rml::StyleSheetSpecification::ParsePropertyDeclaration( props, "font-effect",
			Rml::CreateString( "outline(%dpx #000000%02x)", width, alpha > 255 ? 255 : alpha ) ) ) {
		if ( const Rml::Property *p = props.GetProperty( Rml::PropertyId::FontEffect ) ) {
			if ( Rml::FontEffectsPtr fx = p->Get<Rml::FontEffectsPtr>() ) {
				h = Rml::GetFontEngineInterface()->PrepareFontEffects( face, fx->list );
			}
		}
	}
	effects[key] = h;
	return h;
}

// the text cut at its color codes (id 1.32's Q_IsColorString: "^" and anything but "^")
void Runs( const char *s, Rml::Colourb base, bool codes, std::vector<Run> &out ) {
	Run cur{ "", base };
	for ( ; *s; s++ ) {
		if ( codes && s[0] == '^' && s[1] && s[1] != '^' ) {
			if ( !cur.text.empty() ) {
				out.push_back( cur );
			}
			Rml::Colourb c = colorTable[( s[1] - '0' ) & 7];
			c.alpha = base.alpha;
			cur = Run{ "", c };
			s++;
			continue;
		}
		cur.text += *s;
	}
	if ( !cur.text.empty() ) {
		out.push_back( cur );
	}
}

// a run's width, digits in tabular cells on fonts that have them
float RunWidth( const Face *f, const Rml::String &s ) {
	Rml::FontEngineInterface *fe = Rml::GetFontEngineInterface();
	const Rml::TextShapingContext ctx{ language };
	if ( !f->digit ) {
		return (float)fe->GetStringWidth( f->handle, s, ctx );
	}
	float w = 0.0f;
	size_t start = 0;
	for ( size_t i = 0; i <= s.size(); i++ ) {
		if ( i == s.size() || ( s[i] >= '0' && s[i] <= '9' ) ) {
			if ( i > start ) {
				w += (float)fe->GetStringWidth( f->handle, Rml::StringView( s.data() + start, s.data() + i ), ctx );
			}
			if ( i < s.size() ) {
				w += f->digit;
			}
			start = i + 1;
		}
	}
	return w;
}

void Emit( const Face *f, Rml::FontEffectsHandle fx, const Rml::String &s, float x, float y, Rml::Colourb color,
		Rml::TexturedMeshList &out ) {
	Rml::RenderManager *m = Manager();
	if ( !m || s.empty() ) {
		return;
	}
	Rml::FontEngineInterface *fe = Rml::GetFontEngineInterface();
	const Rml::TextShapingContext ctx{ language };
	const float opacity = color.alpha / 255.0f;
	auto one = [&]( Rml::StringView piece, float px ) {
		fe->GenerateString( *m, f->handle, fx, piece, Rml::Vector2f( px, y ), color.ToPremultiplied(), opacity, ctx, out );
	};
	if ( !f->digit ) {
		one( s, x );
		return;
	}
	size_t start = 0;
	for ( size_t i = 0; i <= s.size(); i++ ) {
		if ( i == s.size() || ( s[i] >= '0' && s[i] <= '9' ) ) {
			if ( i > start ) {
				const Rml::StringView piece( s.data() + start, s.data() + i );
				one( piece, x );
				x += (float)fe->GetStringWidth( f->handle, piece, ctx );
			}
			if ( i < s.size() ) {
				// the digit centered in its cell
				const float w = (float)fe->GetStringWidth( f->handle, Rml::StringView( s.data() + i, s.data() + i + 1 ), ctx );
				one( Rml::StringView( s.data() + i, s.data() + i + 1 ), x + ( f->digit - w ) * 0.5f );
				x += f->digit;
			}
			start = i + 1;
		}
	}
}

// the text's pieces from its pen start, again whenever the face's atlas changed: one per texture, in
// the order they come (the outline's layer before the letters'), so a number of tabular digits is two
// draws, not two a digit
void LayOut( CachedText &t ) {
	Rml::RenderManager *m = Manager();
	Rml::TexturedMeshList meshes;
	float x = 0.0f;
	for ( const Run &r : t.runs ) {
		Emit( t.face, t.fx, r.text, x, 0.0f, r.color, meshes );
		x += RunWidth( t.face, r.text );
	}
	std::vector<Rml::TexturedMesh> merged;
	for ( Rml::TexturedMesh &tm : meshes ) {
		Rml::TexturedMesh *into = nullptr;
		for ( Rml::TexturedMesh &g : merged ) {
			if ( g.texture == tm.texture ) {
				into = &g;
				break;
			}
		}
		if ( !into ) {
			merged.push_back( Rml::TexturedMesh{ Rml::Mesh(), tm.texture } );
			into = &merged.back();
		}
		const int base = (int)into->mesh.vertices.size();
		into->mesh.vertices.insert( into->mesh.vertices.end(), tm.mesh.vertices.begin(), tm.mesh.vertices.end() );
		for ( int i : tm.mesh.indices ) {
			into->mesh.indices.push_back( base + i );
		}
	}
	t.pieces.clear();
	if ( m ) {
		for ( Rml::TexturedMesh &g : merged ) {
			TextPiece p;
			p.texture = g.texture;
			p.geometry = m->MakeGeometry( std::move( g.mesh ) );
			t.pieces.push_back( std::move( p ) );
		}
	}
	t.laidOut = true;
	t.version = Rml::GetFontEngineInterface()->GetVersion( t.face->handle );
}

} // namespace

bool Hud_Pending() {
	FlushShapes();
	return !items.empty();
}

// inside the UI's frame, before the pages
void Hud_Render( int width, int height ) {
	Rml::RenderManager *m = Manager();
	FlushShapes();
	if ( m && !items.empty() ) {
		Rml::FontEngineInterface *fe = Rml::GetFontEngineInterface();
		m->PrepareRender( Rml::Vector2i( width, height ) );
		for ( const Item &it : items ) {
			if ( it.shape ) {
				it.shape->Render( it.at, it.texture );
				continue;
			}
			CachedText &t = *it.text;
			if ( t.version != fe->GetVersion( t.face->handle ) ) {
				const long long prof = OzE_ProfStart();
				LayOut( t );	// glyphs a later text brought changed the atlas
				OzE_ProfDetail( "hud relayout", prof );
			}
			if ( it.piece < t.pieces.size() ) {
				const long long prof = OzE_ProfStart();
				t.pieces[it.piece].geometry.Render( it.at, t.pieces[it.piece].texture );
				OzE_ProfDetail( "hud text draw", prof );
			}
		}
		m->ResetState();
	}
}

// after every frame, drawn or not: a frame's HUD is that frame's; texts unused for a while go
void Hud_Clear() {
	shapes = Rml::Mesh();
	shapesImage = -1;
	items.clear();
	frameShapes.clear();
	const int now = OzE_Milliseconds();
	if ( texts.size() > textMax ) {
		texts.clear();
	} else if ( now - lastSweep >= 125 ) {
		lastSweep = now;
		for ( auto it = texts.begin(); it != texts.end(); ) {
			it = now - it->second.used > textKeep ? texts.erase( it ) : std::next( it );
		}
	}
}

// a HUD image's texture, to draw it outside the HUD (the crosshair previews)
Rml::Texture Hud_ImageTexture( int image ) {
	return image > 0 && image <= (int)images.size() ? images[image - 1] : Rml::Texture();
}

// the RmlUi handles go with the renderer; textures reload by name
void Hud_Shutdown() {
	Hud_Clear();
	texts.clear();
	images.clear();
	imageNames.clear();
	faces.clear();
	effects.clear();
}

} // namespace OzUI

using namespace OzUI;

extern "C" {

int OzUI_HudAvailable( void ) {
	return GetContext() && OzE_Renderer() ? 1 : 0;
}

int OzUI_HudImage( const char *name ) {
	Rml::RenderManager *m = Manager();
	if ( !m || !name || !*name ) {
		return 0;
	}
	for ( size_t i = 0; i < imageNames.size(); i++ ) {
		if ( imageNames[i] == name ) {
			return (int)i + 1;
		}
	}
	imageNames.push_back( name );
	images.push_back( m->LoadTexture( name ) );
	return (int)images.size();
}

void OzUI_HudTris( const ozHudVertex_t *verts, int numVerts, int image ) {
	if ( !verts || numVerts < 3 || !Manager() ) {
		return;
	}
	if ( image != shapesImage ) {
		FlushShapes();
		shapesImage = image;
	}
	numVerts -= numVerts % 3;
	const int base = (int)shapes.vertices.size();
	for ( int i = 0; i < numVerts; i++ ) {
		const ozHudVertex_t &v = verts[i];
		Rml::Vertex rv;
		rv.position = Rml::Vector2f( v.xy[0], v.xy[1] );
		rv.tex_coord = Rml::Vector2f( v.st[0], v.st[1] );
		rv.colour = Rml::Colourb( v.color[0], v.color[1], v.color[2], v.color[3] ).ToPremultiplied();
		shapes.vertices.push_back( rv );
		shapes.indices.push_back( base + i );
	}
}

float OzUI_HudText( const char *text, float x, float y, int font, float size, int flags, const float *rgba, float outline, float maxWidth ) {
	const Face *f = GetFace( font, (int)( size + 0.5f ) );
	if ( !f || !text ) {
		return 0.0f;
	}
	auto byte = []( float v ) { return (Rml::byte)( v <= 0.0f ? 0 : v >= 1.0f ? 255 : (int)( v * 255.0f + 0.5f ) ); };
	const Rml::Colourb base( byte( rgba[0] ), byte( rgba[1] ), byte( rgba[2] ), byte( rgba[3] ) );
	const int outlineAlpha = outline > 0.0f ? (int)( outline * 255.0f + 0.5f ) : 0;
	// the outline: Godot's draw_string_outline at size * 0.12 (at least 2), half of it each side
	const int outlineWidth = (int)std::fmax( 1.0f, std::round( std::fmax( size * 0.12f, 2.0f ) * 0.5f ) );

	// everything but the place and the alignment
	struct {
		const Face *face;
		int codes, outline, outlineWidth;
		Rml::byte color[4];
		float maxWidth;
	} k;
	memset( &k, 0, sizeof( k ) );
	k.face = f;
	k.codes = ( flags & OZ_HUD_TEXT_NOCODES ) == 0;
	k.outline = outlineAlpha;
	k.outlineWidth = outlineAlpha > 0 ? outlineWidth : 0;
	k.color[0] = base.red; k.color[1] = base.green; k.color[2] = base.blue; k.color[3] = base.alpha;
	k.maxWidth = maxWidth > 0.0f ? maxWidth : 0.0f;
	Rml::String key( (const char *)&k, sizeof( k ) );
	key += text;
	CachedText &t = texts[key];
	t.used = OzE_Milliseconds();
	if ( !t.face ) {
		t.face = f;
		Runs( text, base, k.codes != 0, t.runs );
		float total = 0.0f;
		for ( const Run &r : t.runs ) {
			total += RunWidth( f, r.text );
		}
		if ( maxWidth > 0.0f && total > maxWidth ) {
			// cut to fit, ending in an ellipsis
			const float dots = RunWidth( f, "..." );
			float w = 0.0f;
			std::vector<Run> cut;
			for ( const Run &r : t.runs ) {
				Run c{ "", r.color };
				for ( size_t i = 0; i < r.text.size(); ) {
					// a whole character (UTF-8: an emoji is up to 4 bytes)
					size_t n = 1;
					while ( i + n < r.text.size() && ( (unsigned char)r.text[i + n] & 0xc0 ) == 0x80 ) {
						n++;
					}
					const Rml::String ch = r.text.substr( i, n );
					const float cw = RunWidth( f, ch );
					if ( w + cw + dots > maxWidth ) {
						break;
					}
					c.text += ch;
					w += cw;
					i += n;
				}
				if ( !c.text.empty() ) {
					cut.push_back( c );
				}
				if ( c.text.size() < r.text.size() ) {
					break;
				}
			}
			cut.push_back( Run{ "...", cut.empty() ? base : cut.back().color } );
			t.runs.swap( cut );
			total = w + dots;
		}
		t.width = total;
		if ( outlineAlpha > 0 ) {
			t.fx = GetOutline( f->handle, outlineWidth, outlineAlpha );
		}
	}
	if ( flags & OZ_HUD_TEXT_MEASURE ) {
		return t.width;
	}
	if ( !t.laidOut || t.version != Rml::GetFontEngineInterface()->GetVersion( f->handle ) ) {
		const long long prof = OzE_ProfStart();
		LayOut( t );
		OzE_ProfDetail( "hud layout", prof );
	}
	const int align = flags & 3;
	if ( align == OZ_HUD_ALIGN_CENTER ) {
		x -= t.width * 0.5f;
	} else if ( align == OZ_HUD_ALIGN_RIGHT ) {
		x -= t.width;
	}
	FlushShapes();
	// whole pixels, as RmlUi places glyphs
	const Rml::Vector2f at( std::round( x ), std::round( y ) );
	for ( size_t i = 0; i < t.pieces.size(); i++ ) {
		Item it;
		it.text = &t;
		it.piece = i;
		it.at = at;
		items.push_back( it );
	}
	return t.width;
}

} // extern "C"
