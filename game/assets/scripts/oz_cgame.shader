// Oz Arena: the cgame's own shaders (game/assets, staged into oz/pak_oz_code.pk3 by engine/build_engine.py).
// Every renderer reads these; they use id's images only. Images named without an extension get the
// remastered .dds beside them on the oz renderer (tools/make_pk3s.py, make_fx_pk3.py).

// The armor shell (CG_OzArmorShell, game/patches/0014): a glow pushed just outside the body for a moment
// when armor takes part of a hit. The cgame sets its color and fades it out (rgbGen entity).
oz/armorShell
{
	deformVertexes wave 100 sin 1.5 0 0 0
	{
		map textures/effects/envmapgold2
		tcGen environment
		tcMod scroll 0 1.5
		blendFunc GL_ONE GL_ONE
		rgbGen entity
	}
}

// The remade effects (cg_oz_fx.c, cg_fxStyle 1), colored and faded by the cgame through the vertex
// colors. oz/fx/glow: sparks, embers, impact flashes, glowing trails and beams (a beam draws the
// flare's middle column, its round falloff across the beam).
oz/fx/glow
{
	cull none
	entityMergable
	{
		map gfx/misc/flare
		blendFunc GL_ONE GL_ONE
		rgbGen vertex
	}
}

// oz/fx/beam: the glow for beams' hot cores (the lightning gun's "glow" and "straight"), brighter on the oz renderer
oz/fx/beam
{
	cull none
	entityMergable
	{
		map gfx/misc/flare
		blendFunc GL_ONE GL_ONE
		rgbGen vertex
	}
}

// Smoke left rising after blasts (Explosions "enhanced")
oz/fx/smoke
{
	cull none
	entityMergable
	{
		map gfx/misc/smokepuff3
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen vertex
		alphaGen vertex
	}
}

// The lightning gun's burn lines, marks on the map (CG_OzAddBurns): a scorch darkening the wall by the
// vertex alpha under a glow added in the vertex color, both set by the cgame as the line cools.
// gfx/oz/burn is made by tools/make_fx_pk3.py (no lines without it).
oz/fx/burn
{
	polygonOffset
	{
		clampmap gfx/oz/burn
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen const ( 0.035 0.028 0.022 )
		alphaGen vertex
	}
	{
		clampmap gfx/oz/burn
		blendFunc GL_ONE GL_ONE
		rgbGen vertex
	}
}

// Droplets of Blood "heavy"
oz/fx/blood
{
	cull none
	entityMergable
	{
		map gfx/damage/blood_stain
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen vertex
		alphaGen vertex
	}
}
