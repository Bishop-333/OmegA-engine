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
// tr_light.c

#include "../core/tr_local.h"

#define	DLIGHT_AT_RADIUS		16
// at the edge of a dlight's influence, this amount of light will be added

#define	DLIGHT_MINIMUM_RADIUS	16
// never calculate a range less than this to prevent huge light numbers


/*
===============
R_TransformDlights

Transforms the origins of an array of dlights.
Used by both the front end (for DlightBmodel) and
the back end (before doing the lighting calculation)
===============
*/
void R_TransformDlights( int count, dlight_t *dl, orientationr_t *or) {
	int		i;
	vec3_t	temp, temp2;

	for ( i = 0 ; i < count ; i++, dl++ ) {
		VectorSubtract( dl->origin, or->origin, temp );
		dl->transformed[0] = DotProduct( temp, or->axis[0] );
		dl->transformed[1] = DotProduct( temp, or->axis[1] );
		dl->transformed[2] = DotProduct( temp, or->axis[2] );
		if ( dl->linear ) {
			VectorSubtract( dl->origin2, or->origin, temp2 );
			dl->transformed2[0] = DotProduct( temp2, or->axis[0] );
			dl->transformed2[1] = DotProduct( temp2, or->axis[1] );
			dl->transformed2[2] = DotProduct( temp2, or->axis[2] );
		}
	}
}


#ifdef USE_LEGACY_DLIGHTS
/*
=============
R_DlightBmodel

Determine which dynamic lights may effect this bmodel
=============
*/
void R_DlightBmodel( bmodel_t *bmodel ) {
	int			i, j;
	const dlight_t	*dl;
	int			mask;
	msurface_t	*surf;

	// transform all the lights
	R_TransformDlights( tr.refdef.num_dlights, tr.refdef.dlights, &tr.or );

	mask = 0;
	for ( i = 0; i < tr.refdef.num_dlights; i++ ) {
		dl = &tr.refdef.dlights[i];

		// see if the point is close enough to the bounds to matter
		for ( j = 0 ; j < 3 ; j++ ) {
			if ( dl->transformed[j] - bmodel->bounds[1][j] > dl->radius ) {
				break;
			}
			if ( bmodel->bounds[0][j] - dl->transformed[j] > dl->radius ) {
				break;
			}
		}
		if ( j < 3 ) {
			continue;
		}

		// we need to check this light
		mask |= 1 << i;
	}

	tr.currentEntity->needDlights = (mask != 0) ? 1 : 0;

	// set the dlight bits in all the surfaces
	for ( i = 0 ; i < bmodel->numSurfaces ; i++ ) {
		surf = bmodel->firstSurface + i;

		if ( *surf->data == SF_FACE ) {
			((srfSurfaceFace_t *)surf->data)->dlightBits = mask;
		} else if ( *surf->data == SF_GRID ) {
			((srfGridMesh_t *)surf->data)->dlightBits = mask;
		} else if ( *surf->data == SF_TRIANGLES ) {
			((srfTriangles_t *)surf->data)->dlightBits = mask;
		}
	}
}
#endif // USE_LEGACY_DLIGHTS


/*
=============================================================================

LIGHT SAMPLING

=============================================================================
*/

extern	cvar_t	*r_ambientScale;
extern	cvar_t	*r_directedScale;
extern	cvar_t	*r_debugLight;

/*
===============
LogLight
===============
*/
static void LogLight( const trRefEntity_t *ent ) {
	int	max1, max2;

	if ( !(ent->e.renderfx & RF_FIRST_PERSON ) ) {
		return;
	}

	max1 = ent->ambientLight[0];
	if ( ent->ambientLight[1] > max1 ) {
		max1 = ent->ambientLight[1];
	} else if ( ent->ambientLight[2] > max1 ) {
		max1 = ent->ambientLight[2];
	}

	max2 = ent->directedLight[0];
	if ( ent->directedLight[1] > max2 ) {
		max2 = ent->directedLight[1];
	} else if ( ent->directedLight[2] > max2 ) {
		max2 = ent->directedLight[2];
	}

	ri.Printf( PRINT_ALL, "amb:%i  dir:%i\n", max1, max2 );
}


/*
=================
R_SetupEntityLighting

Calculates all the lighting values that will be used
by the Calc_* functions
=================
*/
void R_SetupEntityLighting( const trRefdef_t *refdef, trRefEntity_t *ent ) {
	int				i;
	const dlight_t		*dl;
	float			power;
	vec3_t			dir;
	float			d;
	vec3_t			lightDir;
	vec3_t			lightOrigin;
#ifdef USE_PMLIGHT
#endif

	// lighting calculations
	if ( ent->lightingCalculated ) {
		return;
	}
	ent->lightingCalculated = qtrue;

	//
	// trace a sample point down to find ambient light
	//
	if ( ent->e.renderfx & RF_LIGHTING_ORIGIN ) {
		// separate lightOrigins are needed so an object that is
		// sinking into the ground can still be lit, and so
		// multi-part models can be lit identically
		VectorCopy( ent->e.lightingOrigin, lightOrigin );
	} else {
		VectorCopy( ent->e.origin, lightOrigin );
	}

	// if NOWORLDMODEL, only use dynamic lights (menu system, etc)
	if ( !(refdef->rdflags & RDF_NOWORLDMODEL ) ) {
		R_ComputeSceneLighting( lightOrigin, ent->ambientLight, ent->directedLight, ent->lightDir );
		VectorScale( ent->ambientLight, r_ambientScale->value, ent->ambientLight );
		VectorScale( ent->directedLight, r_directedScale->value, ent->directedLight );
	} else {
		ent->ambientLight[0] = ent->ambientLight[1] =
			ent->ambientLight[2] = tr.identityLight * 150;
		ent->directedLight[0] = ent->directedLight[1] =
			ent->directedLight[2] = tr.identityLight * 150;
		VectorCopy( tr.sunDirection, ent->lightDir );
	}

	// bonus items and view weapons have a fixed minimum add
	if ( 1 /* ent->e.renderfx & RF_MINLIGHT */ ) {
		// give everything a minimum light add
		ent->ambientLight[0] += tr.identityLight * 32;
		ent->ambientLight[1] += tr.identityLight * 32;
		ent->ambientLight[2] += tr.identityLight * 32;
	}

	// Extra lighting boost for first-person weapon models to ensure visibility
	if ( ent->e.renderfx & (RF_FIRST_PERSON | RF_DEPTHHACK) ) {
		// Add significant ambient light to weapon models in HUD
		// This ensures they're always clearly visible regardless of environment lighting
		float weaponLightBoost = 100.0f * tr.identityLight;
		ent->ambientLight[0] += weaponLightBoost;
		ent->ambientLight[1] += weaponLightBoost;
		ent->ambientLight[2] += weaponLightBoost;
		
		// Also boost directed light slightly for better definition
		float directedBoost = 50.0f * tr.identityLight;
		ent->directedLight[0] += directedBoost;
		ent->directedLight[1] += directedBoost;
		ent->directedLight[2] += directedBoost;
	}

	//
	// modify the light by dynamic lights
	//
	d = VectorLength( ent->directedLight );
	VectorScale( ent->lightDir, d, lightDir );
	for ( i = 0 ; i < refdef->num_dlights ; i++ ) {
		dl = &refdef->dlights[i];
		VectorSubtract( dl->origin, lightOrigin, dir );
		d = VectorNormalize( dir );

		power = DLIGHT_AT_RADIUS * ( dl->radius * dl->radius );
		if ( d < DLIGHT_MINIMUM_RADIUS ) {
			d = DLIGHT_MINIMUM_RADIUS;
		}
		d = power / ( d * d );

		VectorMA( ent->directedLight, d, dl->color, ent->directedLight );
		VectorMA( lightDir, d, dir, lightDir );
	}

	// clamp ambient
	for ( i = 0 ; i < 3 ; i++ ) {
		if ( ent->ambientLight[i] > tr.identityLightByte ) {
			ent->ambientLight[i] = tr.identityLightByte;
		}
	}

	if ( r_debugLight->integer ) {
		LogLight( ent );
	}

	// save out the byte packet version
	((byte *)&ent->ambientLightInt)[0] = myftol( ent->ambientLight[0] ); // -EC-: don't use ri.ftol to avoid precision losses
	((byte *)&ent->ambientLightInt)[1] = myftol( ent->ambientLight[1] );
	((byte *)&ent->ambientLightInt)[2] = myftol( ent->ambientLight[2] );
	((byte *)&ent->ambientLightInt)[3] = 0xff;

	// transform the direction to local space
	VectorNormalize( lightDir );
	ent->lightDir[0] = DotProduct( lightDir, ent->e.axis[0] );
	ent->lightDir[1] = DotProduct( lightDir, ent->e.axis[1] );
	ent->lightDir[2] = DotProduct( lightDir, ent->e.axis[2] );

}


/*
=================
R_LightForPoint
=================
*/
int R_LightForPoint( vec3_t point, vec3_t ambientLight, vec3_t directedLight, vec3_t lightDir )
{
	R_ComputeSceneLighting( point, ambientLight, directedLight, lightDir );
	VectorScale( ambientLight, r_ambientScale->value, ambientLight );
	VectorScale( directedLight, r_directedScale->value, directedLight );

	return qtrue;
}
