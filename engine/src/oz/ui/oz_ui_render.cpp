/*
Oz: RmlUi's render interface on the renderer's UI calls (renderercommon/tr_oz_ui.h).

Geometry and textures are the renderer's, so their handles carry a generation: after a renderer
restart (vid_restart, a map load, a mod switch) RmlUi has been told to drop them all and builds
them again, and anything older is ignored. Filters and gradients stay here as plain parameters,
handed over whole when drawn, so they outlive a restart.
*/
#include "oz_ui_internal.h"

#include <chrono>
#include <cmath>

namespace OzUI {

static RenderInterface renderInterface;

RenderInterface &Renderer() {
	return renderInterface;
}

static_assert( sizeof( Rml::Vertex ) == sizeof( ozUiVertex_t ), "RmlUi's vertex layout must match ozUiVertex_t" );

uintptr_t RenderInterface::Wrap( int handle ) const {
	return handle ? ( (uintptr_t)generation << 32 ) | (uint32_t)handle : 0;
}

int RenderInterface::Unwrap( uintptr_t handle ) const {
	if ( !r || ( handle >> 32 ) != generation ) {
		return 0;
	}
	return (int)( handle & 0xffffffffu );
}

void RenderInterface::Acquire() {
	r = OzE_Renderer();
	if ( r && r->version != OZ_UI_RENDER_VERSION ) {
		r = nullptr;
	}
}

bool RenderInterface::BeginFrame( int width, int height ) {
	r = OzE_Renderer();
	if ( !r || r->version != OZ_UI_RENDER_VERSION ) {
		r = nullptr;
		return false;
	}
	scissorOn = false;
	r->BeginFrame( width, height );
	return true;
}

void RenderInterface::EndFrame() {
	if ( r ) {
		r->EndFrame();
	}
}

void RenderInterface::RendererShutdown() {
	r = OzE_Renderer();
	if ( r ) {
		Rml::ReleaseTextures( this );
		Rml::ReleaseCompiledGeometry( this );
	}
	generation++;
	tiles.clear();
	r = nullptr;
}

Rml::CompiledGeometryHandle RenderInterface::CompileGeometry( Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices ) {
	if ( !r ) {
		return 0;
	}
	return Wrap( r->CompileGeometry( (const ozUiVertex_t *)vertices.data(), (int)vertices.size(), indices.data(), (int)indices.size() ) );
}

void RenderInterface::RenderGeometry( Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture ) {
	if ( const int g = Unwrap( geometry ) ) {
		r->RenderGeometry( g, translation.x, translation.y, Unwrap( texture ) );
	}
}

void RenderInterface::ReleaseGeometry( Rml::CompiledGeometryHandle geometry ) {
	if ( const int g = Unwrap( geometry ) ) {
		r->ReleaseGeometry( g );
	}
}

// a page's src="/textures/..." is a path in the pk3s; relative ones were joined to the page's folder
Rml::TextureHandle RenderInterface::LoadTexture( Rml::Vector2i &dimensions, const Rml::String &source ) {
	int w = 0, h = 0;

	if ( !r ) {
		return 0;
	}
	const char *path = source.c_str();
	while ( *path == '/' ) {
		path++;
	}
	// "/@thumb/<image>": a small copy, for the map editor's texture browser
	const bool thumb = !strncmp( path, "@thumb/", 7 );
	if ( thumb ) {
		path += 7;
	}
	// "/@tile/<image>": an image shown small, kept at least 640 across (a level shot's tile: about 600 px
	// at 4K), and loaded after the page shows: Create Match's 200 level shots took 1.3 s in its first
	// frame, 0.8 s at that size. The tile shows a clear stand-in until LoadTiles puts the image in it;
	// a tile fills its box (a card's shot, a banner, an img sized by its box), so the size given here is
	// never read.
	const bool tile = !thumb && !strncmp( path, "@tile/", 6 );
	if ( tile ) {
		path += 6;
	}
	if ( tile && r->LoadReduced && !tilesNow ) {
		static const Rml::byte clear[4] = {};
		const int t = r->CreateTexture( clear, 1, 1 );
		if ( !t ) {
			return 0;
		}
		tiles.push_back( { t, path } );
		dimensions = Rml::Vector2i( 640, 480 );
		return Wrap( t );
	}
	const int t = thumb && r->LoadThumbnail ? r->LoadThumbnail( path, 128, &w, &h )
		: tile && r->LoadReduced ? r->LoadReduced( path, 640, &w, &h, 0 ) : r->LoadTexture( path, &w, &h );
	if ( !t ) {
		if ( !thumb ) {	// a tool material's tile has no image: its color shows
			Printf( "^3UI: no image %s\n", path );
		}
		return 0;
	}
	dimensions = Rml::Vector2i( w, h );
	return Wrap( t );
}

Rml::TextureHandle RenderInterface::GenerateTexture( Rml::Span<const Rml::byte> source, Rml::Vector2i dimensions ) {
	if ( !r ) {
		return 0;
	}
	return Wrap( r->CreateTexture( source.data(), dimensions.x, dimensions.y ) );
}

void RenderInterface::ReleaseTexture( Rml::TextureHandle texture ) {
	if ( const int t = Unwrap( texture ) ) {
		for ( size_t i = 0; i < tiles.size(); i++ ) {
			if ( tiles[i].handle == t ) {
				tiles.erase( tiles.begin() + i );
				break;
			}
		}
		r->ReleaseTexture( t );
	}
}

// in the frame, before the pages draw: tiles in the order the pages asked for them (a list's from its
// top), until 3 ms went by (a 1280x960 level shot takes about 4)
void RenderInterface::LoadTiles( bool all ) {
	const auto start = std::chrono::steady_clock::now();

	tilesNow = all;
	while ( r && !tiles.empty() ) {
		const Tile tile = tiles.front();
		int w, h;
		tiles.erase( tiles.begin() );
		r->LoadReduced( tile.path.c_str(), 640, &w, &h, tile.handle );
		if ( !all && std::chrono::steady_clock::now() - start > std::chrono::milliseconds( 3 ) ) {
			break;
		}
	}
}

void RenderInterface::EnableScissorRegion( bool enable ) {
	scissorOn = enable;
	if ( r && !enable ) {
		r->SetScissor( 0, 0, 0, 0, 0 );
	}
}

void RenderInterface::SetScissorRegion( Rml::Rectanglei region ) {
	scissor[0] = region.Left();
	scissor[1] = region.Top();
	scissor[2] = region.Width();
	scissor[3] = region.Height();
	if ( r && scissorOn ) {
		r->SetScissor( 1, scissor[0], scissor[1], scissor[2], scissor[3] );
	}
}

void RenderInterface::EnableClipMask( bool enable ) {
	if ( r ) {
		r->EnableClipMask( enable );
	}
}

void RenderInterface::RenderToClipMask( Rml::ClipMaskOperation operation, Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation ) {
	if ( const int g = Unwrap( geometry ) ) {
		const int op = operation == Rml::ClipMaskOperation::Set ? OZ_UI_CLIP_SET
			: operation == Rml::ClipMaskOperation::SetInverse ? OZ_UI_CLIP_SET_INVERSE : OZ_UI_CLIP_INTERSECT;
		r->RenderToClipMask( op, g, translation.x, translation.y );
	}
}

void RenderInterface::SetTransform( const Rml::Matrix4f *transform ) {
	if ( r ) {
		static_assert( std::is_same_v<Rml::Matrix4f, Rml::ColumnMajorMatrix4f>, "the renderer takes column-major matrices" );
		r->SetTransform( transform ? transform->data() : nullptr );
	}
}

Rml::LayerHandle RenderInterface::PushLayer() {
	return r ? (Rml::LayerHandle)r->PushLayer() : 0;
}

void RenderInterface::CompositeLayers( Rml::LayerHandle source, Rml::LayerHandle destination, Rml::BlendMode blend_mode,
	Rml::Span<const Rml::CompiledFilterHandle> filters ) {
	ozUiFilter_t list[16];
	int n = 0;

	if ( !r ) {
		return;
	}
	for ( const Rml::CompiledFilterHandle f : filters ) {
		if ( f && n < 16 ) {
			list[n++] = *(const ozUiFilter_t *)f;
		}
	}
	r->CompositeLayers( (int)source, (int)destination, blend_mode == Rml::BlendMode::Replace, list, n );
}

void RenderInterface::PopLayer() {
	if ( r ) {
		r->PopLayer();
	}
}

Rml::TextureHandle RenderInterface::SaveLayerAsTexture() {
	return r ? Wrap( r->SaveLayerAsTexture() ) : 0;
}

Rml::CompiledFilterHandle RenderInterface::SaveLayerAsMaskImage() {
	if ( !r ) {
		return 0;
	}
	r->SaveLayerAsMask();
	ozUiFilter_t *f = new ozUiFilter_t{};
	f->type = OZ_UI_FILTER_MASK_IMAGE;
	return (Rml::CompiledFilterHandle)f;
}

static void ColorMatrix( ozUiFilter_t *f, const Rml::Matrix4f &m ) {
	f->type = OZ_UI_FILTER_COLOR_MATRIX;
	memcpy( f->matrix, m.data(), sizeof( f->matrix ) );
}

// RmlUi's filters as its GL3 backend reads them
Rml::CompiledFilterHandle RenderInterface::CompileFilter( const Rml::String &name, const Rml::Dictionary &parameters ) {
	ozUiFilter_t f = {};
	const float value = Rml::Get( parameters, "value", 1.0f );

	if ( name == "opacity" ) {
		f.type = OZ_UI_FILTER_OPACITY;
		f.value = value;
	} else if ( name == "blur" ) {
		f.type = OZ_UI_FILTER_BLUR;
		f.value = Rml::Get( parameters, "sigma", 1.0f );
	} else if ( name == "drop-shadow" ) {
		const Rml::ColourbPremultiplied c = Rml::Get( parameters, "color", Rml::Colourb() ).ToPremultiplied();
		const Rml::Vector2f offset = Rml::Get( parameters, "offset", Rml::Vector2f( 0.f ) );
		f.type = OZ_UI_FILTER_DROP_SHADOW;
		f.value = Rml::Get( parameters, "sigma", 0.0f );
		for ( int i = 0; i < 4; i++ ) {
			f.color[i] = c[i] / 255.0f;
		}
		f.offset[0] = offset.x;
		f.offset[1] = offset.y;
	} else if ( name == "brightness" ) {
		ColorMatrix( &f, Rml::Matrix4f::Diag( value, value, value, 1.f ) );
	} else if ( name == "contrast" ) {
		const float grayness = 0.5f - 0.5f * value;
		Rml::Matrix4f m = Rml::Matrix4f::Diag( value, value, value, 1.f );
		m.SetColumn( 3, Rml::Vector4f( grayness, grayness, grayness, 1.f ) );
		ColorMatrix( &f, m );
	} else if ( name == "invert" ) {
		const float v = Rml::Math::Clamp( value, 0.f, 1.f ), inverted = 1.f - 2.f * v;
		Rml::Matrix4f m = Rml::Matrix4f::Diag( inverted, inverted, inverted, 1.f );
		m.SetColumn( 3, Rml::Vector4f( v, v, v, 1.f ) );
		ColorMatrix( &f, m );
	} else if ( name == "grayscale" ) {
		const float rev = 1.f - value;
		const Rml::Vector3f g = value * Rml::Vector3f( 0.2126f, 0.7152f, 0.0722f );
		ColorMatrix( &f, Rml::Matrix4f::FromRows( { g.x + rev, g.y, g.z, 0.f }, { g.x, g.y + rev, g.z, 0.f },
			{ g.x, g.y, g.z + rev, 0.f }, { 0.f, 0.f, 0.f, 1.f } ) );
	} else if ( name == "sepia" ) {
		const float rev = 1.f - value;
		const Rml::Vector3f rm = value * Rml::Vector3f( 0.393f, 0.769f, 0.189f );
		const Rml::Vector3f gm = value * Rml::Vector3f( 0.349f, 0.686f, 0.168f );
		const Rml::Vector3f bm = value * Rml::Vector3f( 0.272f, 0.534f, 0.131f );
		ColorMatrix( &f, Rml::Matrix4f::FromRows( { rm.x + rev, rm.y, rm.z, 0.f }, { gm.x, gm.y + rev, gm.z, 0.f },
			{ bm.x, bm.y, bm.z + rev, 0.f }, { 0.f, 0.f, 0.f, 1.f } ) );
	} else if ( name == "hue-rotate" ) {
		const float s = sinf( value ), c = cosf( value );
		ColorMatrix( &f, Rml::Matrix4f::FromRows(
			{ 0.213f + 0.787f * c - 0.213f * s, 0.715f - 0.715f * c - 0.715f * s, 0.072f - 0.072f * c + 0.928f * s, 0.f },
			{ 0.213f - 0.213f * c + 0.143f * s, 0.715f + 0.285f * c + 0.140f * s, 0.072f - 0.072f * c - 0.283f * s, 0.f },
			{ 0.213f - 0.213f * c - 0.787f * s, 0.715f - 0.715f * c + 0.715f * s, 0.072f + 0.928f * c + 0.072f * s, 0.f },
			{ 0.f, 0.f, 0.f, 1.f } ) );
	} else if ( name == "saturate" ) {
		ColorMatrix( &f, Rml::Matrix4f::FromRows(
			{ 0.213f + 0.787f * value, 0.715f - 0.715f * value, 0.072f - 0.072f * value, 0.f },
			{ 0.213f - 0.213f * value, 0.715f + 0.285f * value, 0.072f - 0.072f * value, 0.f },
			{ 0.213f - 0.213f * value, 0.715f - 0.715f * value, 0.072f + 0.928f * value, 0.f },
			{ 0.f, 0.f, 0.f, 1.f } ) );
	} else {
		Printf( "^3UI: no filter %s\n", name.c_str() );
		return 0;
	}
	return (Rml::CompiledFilterHandle) new ozUiFilter_t( f );
}

void RenderInterface::ReleaseFilter( Rml::CompiledFilterHandle filter ) {
	delete (ozUiFilter_t *)filter;
}

// the gradients; RmlUi's "shader" decorator values beyond them aren't drawn
Rml::CompiledShaderHandle RenderInterface::CompileShader( const Rml::String &name, const Rml::Dictionary &parameters ) {
	ozUiGradient_t g = {};
	const bool repeating = Rml::Get( parameters, "repeating", false );

	if ( name == "linear-gradient" ) {
		const Rml::Vector2f p0 = Rml::Get( parameters, "p0", Rml::Vector2f( 0.f ) ), p1 = Rml::Get( parameters, "p1", Rml::Vector2f( 0.f ) );
		g.func = repeating ? OZ_UI_GRADIENT_REPEATING_LINEAR : OZ_UI_GRADIENT_LINEAR;
		g.p[0] = p0.x; g.p[1] = p0.y;
		g.v[0] = p1.x - p0.x; g.v[1] = p1.y - p0.y;
	} else if ( name == "radial-gradient" ) {
		const Rml::Vector2f c = Rml::Get( parameters, "center", Rml::Vector2f( 0.f ) ), radius = Rml::Get( parameters, "radius", Rml::Vector2f( 1.f ) );
		g.func = repeating ? OZ_UI_GRADIENT_REPEATING_RADIAL : OZ_UI_GRADIENT_RADIAL;
		g.p[0] = c.x; g.p[1] = c.y;
		g.v[0] = 1.f / radius.x; g.v[1] = 1.f / radius.y;
	} else if ( name == "conic-gradient" ) {
		const Rml::Vector2f c = Rml::Get( parameters, "center", Rml::Vector2f( 0.f ) );
		const float angle = Rml::Get( parameters, "angle", 0.f );
		g.func = repeating ? OZ_UI_GRADIENT_REPEATING_CONIC : OZ_UI_GRADIENT_CONIC;
		g.p[0] = c.x; g.p[1] = c.y;
		g.v[0] = cosf( angle ); g.v[1] = sinf( angle );
	} else {
		Printf( "^3UI: no shader %s\n", name.c_str() );
		return 0;
	}

	auto it = parameters.find( "color_stop_list" );
	if ( it == parameters.end() || it->second.GetType() != Rml::Variant::COLORSTOPLIST ) {
		return 0;
	}
	const Rml::ColorStopList &stops = it->second.GetReference<Rml::ColorStopList>();
	g.numStops = Rml::Math::Min( (int)stops.size(), OZ_UI_MAX_STOPS );
	for ( int i = 0; i < g.numStops; i++ ) {
		g.stopPositions[i] = stops[i].position.number;
		for ( int c = 0; c < 4; c++ ) {
			g.stopColors[i][c] = stops[i].color[c] / 255.0f;
		}
	}
	return (Rml::CompiledShaderHandle) new ozUiGradient_t( g );
}

void RenderInterface::RenderShader( Rml::CompiledShaderHandle shader, Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation,
	Rml::TextureHandle /*texture*/ ) {
	if ( const int g = Unwrap( geometry ) ) {
		r->RenderGradient( (const ozUiGradient_t *)shader, g, translation.x, translation.y );
	}
}

void RenderInterface::ReleaseShader( Rml::CompiledShaderHandle shader ) {
	delete (ozUiGradient_t *)shader;
}

} // namespace OzUI
