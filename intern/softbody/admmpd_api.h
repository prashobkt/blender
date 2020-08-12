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
#include "DNA_scene_types.h"

typedef struct ADMMPDInterfaceData {
    char last_error[256]; // last error message
    struct ADMMPDInternalData *idata; // internal data
} ADMMPDInterfaceData;

// Frees ADMMPDInternalData
void admmpd_dealloc(ADMMPDInterfaceData*);

// Test if the mesh topology has changed in a way that requires re-initialization.
int admmpd_mesh_needs_update(ADMMPDInterfaceData*, Object*);

// Initialize the mesh.
// The SoftBody object's (ob->soft) bpoint array is also updated.
// Returns 1 on success, 0 on failure
int admmpd_update_mesh(ADMMPDInterfaceData*, Object*, float (*vertexCos)[3]);

// Test if certain parameter changes require re-initialization.
int admmpd_solver_needs_update(ADMMPDInterfaceData*, Scene*, Object*);

// Initialize solver variables.
// Returns 1 on success, 0 on failure.
int admmpd_update_solver(ADMMPDInterfaceData*, Scene*, Object*, float (*vertexCos)[3]);

// Copies BodyPoint data (from SoftBody)
// to internal vertex position and velocity
void admmpd_copy_from_object(ADMMPDInterfaceData*, Object*);

// Copies ADMM-PD data to SoftBody::bpoint and vertexCos
void admmpd_copy_to_object(ADMMPDInterfaceData*, Object*, float (*vertexCos)[3]);

// Sets the obstacle data for collisions.
// Update obstacles has a different interface because of the
// complexity of grabbing obstacle mesh data. We'll just do
// that in softbody.c
void admmpd_update_obstacles(
    ADMMPDInterfaceData*,
    float *in_verts_0,
    float *in_verts_1,
    int nv,
    unsigned int *in_faces,
    int nf);

// Performs a time step. Object and vertexCos are not changed.
// Returns 1 on success, 0 on error
int admmpd_solve(ADMMPDInterfaceData*, Object*, float (*vertexCos)[3]);

#ifdef __cplusplus
}
#endif

#endif // ADMMPD_API_H
