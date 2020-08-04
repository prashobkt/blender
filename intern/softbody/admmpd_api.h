/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2013 Blender Foundation,
 * All rights reserved.
 */

/** \file
 * \ingroup admmpd
 */

#ifndef ADMMPD_API_H
#define ADMMPD_API_H

#ifdef __cplusplus
extern "C" {
#endif

#include "DNA_object_types.h"

typedef struct ADMMPDInterfaceData {
    // totverts is usually different than mesh_totverts.
    // This is due to the lattice/tetmesh that is generated
    // in init. You can use them as input if reading from cache,
    // as they will be copied to internal solver data before admmpd_solve.
    int out_totverts; // number of deformable verts (output)
    float in_framerate; // frames per second (input)
    char last_error[256]; // last error message
    struct ADMMPDInternalData *idata; // internal data
} ADMMPDInterfaceData;

// SoftBody bodypoint (contains pos,vec)
typedef struct BodyPoint BodyPoint;

// Clears all solver data and ADMMPDInterfaceData
void admmpd_dealloc(ADMMPDInterfaceData*);

// Initializes solver and allocates internal data
// Mode:
//      ADMMPD_INIT_MODE_EMBEDDED
//      ADMMPD_INIT_MODE_TETGEN
//      ADMMPD_INIT_MODE_TRIANGLE (cloth)
// Returns 1 on success, 0 on failure
int admmpd_init(ADMMPDInterfaceData*, Object*, float (*vertexCos)[3], int mode);

// Copies BodyPoint data (from SoftBody)
// to internal vertex position and velocity
void admmpd_copy_from_bodypoint(ADMMPDInterfaceData*, const BodyPoint *pts);

// Sets the obstacle data for collisions
void admmpd_update_obstacles(
    ADMMPDInterfaceData*,
    float *in_verts_0,
    float *in_verts_1,
    int nv,
    unsigned int *in_faces,
    int nf);

// Updates goal positions
void admmpd_update_goals(
    ADMMPDInterfaceData*,
    float *goal_k, // goal stiffness, nv
    float *goal_pos, // goal position, nv x 3
    int nv);

// Copies internal vertex position and velocity data
// to BodyPoints (from SoftBody) AND surface mesh vertices.
// If pts or vertexCos is null, its skipped
void admmpd_copy_to_bodypoint_and_object(
    ADMMPDInterfaceData*,
    BodyPoint *pts,
    float (*vertexCos)[3]);

// Performs a time step. Object is passed
// only to update settings if they have changed.
// Returns 1 on success, 0 on error
int admmpd_solve(ADMMPDInterfaceData*, Object*);

#ifdef __cplusplus
}
#endif

#endif // ADMMPD_API_H
