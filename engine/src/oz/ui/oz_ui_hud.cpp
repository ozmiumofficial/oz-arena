/*
Oz: the HUD canvas (ENGINE_PLAN.md P6). The HUD (oz_hud.c) draws through it: triangles with an
optional image, and text in the UI's own fonts (Barlow and the display font) with Quake 3's color
codes, tabular figures and a soft outline. What the HUD sends during a frame is drawn under the
pages at that frame's OzUI_Draw, in the order sent, then dropped. Every renderer has the UI's calls
(oz, opengl and vulkan); one that can't start them (OpenGL before 3.2) keeps the game code's HUD.

Text is laid out once and kept: most of the HUD's text is the same from frame to frame, and
shaping it and making its geometry anew every frame cost more than the rest of the HUD together.
A text is cached by everything but its place, laid out from its pen start, and drawn moved there.

A face is made for each whole-pixel size, and its glyphs rendered into an atlas at its first draw: a few
milliseconds each. A text whose size changes from frame to frame (a pop, a damage number's growth) is
laid out at a size that stays and drawn scaled (OzUI_HudText's scale), or a kill's center print and
damage numbers made a face every frame they grew or shrank.
*/
#include "oz_ui_internal.h"
#include "oz_ui.h"

#include <cmath>
#include <cstring>
#include <deque>
#include <map>
#include <set>
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
	float scale = 1.0f;				// the text's, about at
};

std::vector<Rml::String> imageNames;			// handle - 1
std::unordered_map<Rml::String, int> imageIndex;	// a name's place in them (the board asks a few for every row, every frame)
std::vector<Rml::Texture> images;
std::vector<int> imagesAsked;		// the renderer start each was last asked for in
std::deque<int> readAhead;			// images asked for since the renderer started, to read (Hud_Render)
int rendererStart;
// each face and outline drawn since the UI started, and those to draw again after the renderer restarts: a
// face's glyph atlas and its outline's are made at their first draw (1 to 8 ms each), and they go with the
// renderer at every map load
std::set<std::pair<Rml::FontFaceHandle, Rml::FontEffectsHandle>> layersDrawn;
std::deque<std::pair<Rml::FontFaceHandle, Rml::FontEffectsHandle>> layersAhead;
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

// OZ_HUD_TEXT_STEPPED: a size snapped up to one of a few, each 12.5% above the last, so a size that drifts
// (a damage number's, with the damage) comes from a handful of faces
int SteppedSize( float size ) {
	float s = 8.0f;
	while ( s < size ) {
		s *= 1.125f;
	}
	return (int)std::ceil( s );
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

int Hex( char c ) {
	return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

// the text cut at its color codes (id 1.32's Q_IsColorString: "^" and anything but "^"). OSP's too, under
// every mod (oz_hud.c's OspCode is the same rule): ^xRRGGBB (^X) is a color of its own, only with its six
// hex digits, and ^B ^b ^F ^f ^N ^n, its blink switches, change nothing. Read as id's codes each picked a
// color by 3 bits, and the hex digits showed as text
void Runs( const char *s, Rml::Colourb base, bool codes, std::vector<Run> &out ) {
	Run cur{ "", base };
	for ( ; *s; s++ ) {
		if ( codes && s[0] == '^' && ( s[1] == 'x' || s[1] == 'X' ) ) {
			int digits = 0;
			while ( digits < 6 && Hex( s[2 + digits] ) >= 0 ) {
				digits++;
			}
			if ( digits == 6 ) {
				if ( !cur.text.empty() ) {
					out.push_back( cur );
				}
				const Rml::Colourb c( (Rml::byte)( Hex( s[2] ) * 16 + Hex( s[3] ) ), (Rml::byte)( Hex( s[4] ) * 16 + Hex( s[5] ) ),
					(Rml::byte)( Hex( s[6] ) * 16 + Hex( s[7] ) ), base.alpha );
				cur = Run{ "", c };
				s += 7;
				continue;
			}
		}
		if ( codes && s[0] == '^' && ( s[1] == 'B' || s[1] == 'b' || s[1] == 'F' || s[1] == 'f' || s[1] == 'N' || s[1] == 'n' ) ) {
			s++;
			continue;
		}
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
	layersDrawn.insert( { t.face->handle, t.fx } );
}

} // namespace

bool Hud_Pending() {
	FlushShapes();
	return !items.empty();
}

// inside the UI's frame, before the pages
void Hud_Render( int width, int height ) {
	Rml::RenderManager *m = Manager();
	// one image a frame read ahead of its first draw: RmlUi reads a file texture as it's first drawn, and
	// a 32-pixel icon took up to 9 ms to find among the install's paks: a kill's weapon in the feed, a
	// pickup, the killer's card art. Here, inside the UI's frame, the renderer is the UI's to load into.
	if ( !readAhead.empty() ) {
		const long long prof = OzE_ProfStart();
		images[readAhead.front()].GetDimensions();
		readAhead.pop_front();
		OzE_ProfDetail( "hud read ahead", prof );
	} else if ( !layersAhead.empty() && m ) {
		// a face and outline the last map drew, drawn off the screen to make its atlases again
		const long long prof = OzE_ProfStart();
		Rml::TexturedMeshList meshes;
		Rml::GetFontEngineInterface()->GenerateString( *m, layersAhead.front().first, layersAhead.front().second, "0",
			Rml::Vector2f( -1000.0f, -1000.0f ), Rml::ColourbPremultiplied( 255, 255 ), 1.0f, Rml::TextShapingContext{ language }, meshes );
		m->PrepareRender( Rml::Vector2i( width, height ) );
		for ( Rml::TexturedMesh &tm : meshes ) {
			m->MakeGeometry( std::move( tm.mesh ) ).Render( Rml::Vector2f( 0.0f, 0.0f ), tm.texture );
		}
		m->ResetState();
		layersAhead.pop_front();
		OzE_ProfDetail( "hud layers ahead", prof );
	}
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
				if ( it.scale != 1.0f ) {
					const Rml::Matrix4f scaled = Rml::Matrix4f::Translate( it.at.x, it.at.y, 0.0f ) *
						Rml::Matrix4f::Scale( it.scale, it.scale, 1.0f );
					m->SetTransform( &scaled );
					t.pieces[it.piece].geometry.Render( Rml::Vector2f( 0.0f, 0.0f ), t.pieces[it.piece].texture );
					m->SetTransform( nullptr );
				} else {
					t.pieces[it.piece].geometry.Render( it.at, t.pieces[it.piece].texture );
				}
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

void Hud_RendererShutdown() {
	Hud_Clear();
	readAhead.clear();
	rendererStart++;
	layersAhead.assign( layersDrawn.begin(), layersDrawn.end() );
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
	imageIndex.clear();
	imagesAsked.clear();
	readAhead.clear();
	layersDrawn.clear();
	layersAhead.clear();
	faces.clear();
	effects.clear();
}

} // namespace OzUI

using namespace OzUI;

/*
The HUD's own icons (weapons, items, medals, flags, crosshairs) as id's paks have them, under every mod:
CPMA's paks replace nearly all of them, and a map pack can in Oz's own game (files.c FS_IdPakFile). An
icon id's paks don't have (a mod's own item) is read as it is.
*/
static Rml::String HudImageSource( const char *name ) {
	static const char *const ids[] = { "icons/", "menu/medals/", "gfx/2d/crosshair" };
	for ( const char *prefix : ids ) {
		if ( !strncmp( name, prefix, strlen( prefix ) ) ) {
			const Rml::String n = name;
			if ( OzE_InIdPak( ( n + ".tga" ).c_str() ) || OzE_InIdPak( ( n + ".jpg" ).c_str() ) ) {
				return "$q3/" + n;	// qcommon.h OZ_ID_PREFIX
			}
			break;
		}
	}
	return name;
}

extern "C" {

int OzUI_HudAvailable( void ) {
	return GetContext() && OzE_Renderer() ? 1 : 0;
}

int OzUI_HudImage( const char *name ) {
	Rml::RenderManager *m = Manager();
	if ( !m || !name || !*name ) {
		return 0;
	}
	const auto found = imageIndex.find( name );
	if ( found != imageIndex.end() ) {
		const int i = found->second;
		if ( imagesAsked[i] != rendererStart ) {
			imagesAsked[i] = rendererStart;
			readAhead.push_back( i );
		}
		return i + 1;
	}
	imageIndex[name] = (int)imageNames.size();
	imageNames.push_back( name );
	images.push_back( m->LoadTexture( HudImageSource( name ) ) );
	imagesAsked.push_back( rendererStart );
	readAhead.push_back( (int)images.size() - 1 );
	return (int)images.size();
}

void OzUI_HudMesh( const ozHudVertex_t *verts, int numVerts, const int *indexes, int numIndexes, int image ) {
	if ( !verts || numVerts < 1 || numIndexes < 3 || !Manager() ) {
		return;
	}
	const long long prof = OzE_ProfStart();
	if ( image != shapesImage ) {
		FlushShapes();
		shapesImage = image;
	}
	numIndexes -= numIndexes % 3;
	// sized once and filled in place: a push_back a vertex took 0.15 ms of the HUD's 0.26. The strips (the speed
	// arc's most) share their corners, a third of the vertices they took as plain triangles
	const size_t base = shapes.vertices.size(), first = shapes.indices.size();
	shapes.vertices.resize( base + numVerts );
	shapes.indices.resize( first + numIndexes );
	Rml::Vertex *out = shapes.vertices.data() + base;
	int *index = shapes.indices.data() + first;
	for ( int i = 0; i < numIndexes; i++ ) {
		index[i] = (int)base + indexes[i];
	}
	for ( int i = 0; i < numVerts; i++ ) {
		const ozHudVertex_t &v = verts[i];
		const unsigned a = v.color[3];
		out[i].position = Rml::Vector2f( v.xy[0], v.xy[1] );
		out[i].tex_coord = Rml::Vector2f( v.st[0], v.st[1] );
		out[i].colour = Rml::ColourbPremultiplied( (Rml::byte)( v.color[0] * a / 255 ), (Rml::byte)( v.color[1] * a / 255 ),
			(Rml::byte)( v.color[2] * a / 255 ), (Rml::byte)a );
	}
	OzE_ProfDetail( "hud tris", prof );
}

float OzUI_HudText( const char *text, float x, float y, int font, float size, float scale, int flags, const float *rgba,
		float outline, float maxWidth ) {
	if ( flags & OZ_HUD_TEXT_STEPPED ) {
		const int stepped = SteppedSize( size );
		scale *= size / stepped;
		size = (float)stepped;
	}
	if ( scale <= 0.0f ) {
		return 0.0f;
	}
	maxWidth /= scale;
	const Face *f = GetFace( font, (int)( size + 0.5f ) );
	if ( !f || !text ) {
		return 0.0f;
	}
	auto byte = []( float v ) { return (Rml::byte)( v <= 0.0f ? 0 : v >= 1.0f ? 255 : (int)( v * 255.0f + 0.5f ) ); };
	const Rml::Colourb base( byte( rgba[0] ), byte( rgba[1] ), byte( rgba[2] ), byte( rgba[3] ) );
	const int outlineAlpha = outline > 0.0f ? (int)( outline * 255.0f + 0.5f ) : 0;
	// the outline: Godot's draw_string_outline at size * 0.12 (at least 2), half of it each side
	const int outlineWidth = (int)std::fmax( 1.0f, std::round( std::fmax( size * 0.12f, 2.0f ) * 0.5f ) );
	if ( flags & OZ_HUD_TEXT_WARM ) {
		// a face and outline a later text will want (oz_hud.c WarmTexts): its atlases made a frame ahead
		const Rml::FontEffectsHandle fx = outlineAlpha > 0 ? GetOutline( f->handle, outlineWidth, outlineAlpha ) : 0;
		if ( layersDrawn.insert( { f->handle, fx } ).second ) {
			layersAhead.push_back( { f->handle, fx } );
		}
		return 0.0f;
	}

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
		return t.width * scale;
	}
	if ( !t.laidOut || t.version != Rml::GetFontEngineInterface()->GetVersion( f->handle ) ) {
		const long long prof = OzE_ProfStart();
		LayOut( t );
		OzE_ProfDetail( "hud layout", prof );
	}
	const int align = flags & 3;
	if ( align == OZ_HUD_ALIGN_CENTER ) {
		x -= t.width * scale * 0.5f;
	} else if ( align == OZ_HUD_ALIGN_RIGHT ) {
		x -= t.width * scale;
	}
	FlushShapes();
	// whole pixels, as RmlUi places glyphs (a scaled text's glyphs fall between them anyway)
	const Rml::Vector2f at = scale != 1.0f ? Rml::Vector2f( x, y ) : Rml::Vector2f( std::round( x ), std::round( y ) );
	for ( size_t i = 0; i < t.pieces.size(); i++ ) {
		Item it;
		it.text = &t;
		it.piece = i;
		it.at = at;
		it.scale = scale;
		items.push_back( it );
	}
	return t.width * scale;
}

} // extern "C"
