/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

#include "../qcommon/q_shared.h"
#include "../renderercommon/tr_public.h"

// Oz: while set, a JPEG loads smaller by its DCT, at least this many texels across (R_OzReducedImage)
int r_jpgAtLeast;

void R_LoadJPG( const char *filename, unsigned char **pic, int *width, int *height )
{
	if ( r_jpgAtLeast > 0 && ri.CL_LoadJPGScaled ) {
		ri.CL_LoadJPGScaled( filename, pic, width, height, r_jpgAtLeast );
		return;
	}
	ri.CL_LoadJPG( filename, pic, width, height );
}
