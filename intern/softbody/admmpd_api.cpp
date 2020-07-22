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
 * The Original Code is Copyright (C) 2013 Blender Foundation
 * All rights reserved.
 */

/** \file
 * \ingroup admmpd
 */

#include "admmpd_api.h"
#include "admmpd_types.h"
#include "admmpd_solver.h"
#include "admmpd_mesh.h"
#include "admmpd_collision.h"

#include "tetgen_api.h"
#include "DNA_mesh_types.h" // Mesh
#include "DNA_meshdata_types.h" // MVert
#include "BKE_mesh_remesh_voxel.h" // TetGen
#include "BKE_mesh.h" // BKE_mesh_free
#include "BKE_softbody.h" // BodyPoint
#include "MEM_guardedalloc.h" // 

#include <iostream>
#include <memory>

struct ADMMPDInternalData {
  std::shared_ptr<admmpd::Options> options;
  std::shared_ptr<admmpd::SolverData> data;
  std::shared_ptr<admmpd::Collision> collision;
  std::shared_ptr<admmpd::Mesh> mesh;
  int in_totverts; // number of input verts
};

void admmpd_dealloc(ADMMPDInterfaceData *iface)
{
  if (iface==NULL)
    return;

  iface->totverts = 0; // output vertices
  if (iface->idata)
  {
    iface->idata->options.reset();
    iface->idata->data.reset();
    iface->idata->collision.reset();
    iface->idata->mesh.reset();
  }

  iface->idata = NULL;
}

static int admmpd_init_with_tetgen(ADMMPDInterfaceData *iface, float *in_verts, unsigned int *in_faces)
{
  TetGenRemeshData tg;
  init_tetgenremeshdata(&tg);
  tg.in_verts = in_verts;
  tg.in_totverts = iface->mesh_totverts;
  tg.in_faces = in_faces;
  tg.in_totfaces = iface->mesh_totfaces;
  bool success = tetgen_resmesh(&tg);
  if (!success || tg.out_tottets==0)
  {
    printf("TetGen failed to generate\n");
    return 0;
  }

  // Double check assumption, the first
  // mesh_totverts vertices remain the same
  // for input and output mesh.
  for (int i=0; i<tg.in_totverts; ++i)
  {
    for (int j=0; j<3; ++j)
    {
      float diff = std::abs(in_verts[i*3+j]-tg.out_verts[i*3+j]);
      if (diff > 1e-10)
      {
        printf("TetGen assumption error: changed surface verts\n");
      }
    }
  }

  iface->totverts = tg.out_totverts;
  iface->idata->mesh = std::make_shared<admmpd::TetMesh>();
  success = iface->idata->mesh->create(
    tg.out_verts,
    tg.out_totverts,
    tg.out_facets,
    tg.out_totfacets,
    tg.out_tets,
    tg.out_tottets);

  if (!success || iface->totverts==0)
  {
    printf("TetMesh failed to create\n");
    return 0;
  }

  // Clean up tetgen output data
  MEM_freeN(tg.out_tets);
  MEM_freeN(tg.out_facets);
  MEM_freeN(tg.out_verts);
  return 1;
}

static int admmpd_init_with_lattice(ADMMPDInterfaceData *iface, float *in_verts, unsigned int *in_faces)
{
  iface->totverts = 0;
  iface->idata->mesh = std::make_shared<admmpd::EmbeddedMesh>();
  bool success = iface->idata->mesh->create(
    in_verts,
    iface->mesh_totverts,
    in_faces,
    iface->mesh_totfaces,
    nullptr,
    0);

  iface->totverts = iface->idata->mesh->rest_prim_verts().rows();
  if (!success)
  {
    printf("EmbeddedMesh failed to generate\n");
    return 0;
  }

  std::shared_ptr<admmpd::EmbeddedMesh> emb_msh =
    std::dynamic_pointer_cast<admmpd::EmbeddedMesh>(iface->idata->mesh);
  iface->idata->collision = std::make_shared<admmpd::EmbeddedMeshCollision>(emb_msh);

  return 1;
}

int admmpd_init(ADMMPDInterfaceData *iface, ADMMPDInitData *in_mesh)
{
  if (iface==NULL)
    return 0;
  if (in_mesh->verts==NULL || in_mesh->faces==NULL)
    return 0;
  if (iface->mesh_totverts<=0 || iface->mesh_totfaces<=0)
    return 0;

//iface->init_mode = 0;

  // Delete any existing data
  admmpd_dealloc(iface);

  // Generate solver data
  iface->idata = new ADMMPDInternalData();
  iface->idata->options = std::make_shared<admmpd::Options>();
  iface->idata->data = std::make_shared<admmpd::SolverData>();

  int gen_success = 0;
  switch (iface->init_mode)
  {
    default:
    case 0: {
      gen_success = admmpd_init_with_tetgen(iface,in_mesh->verts,in_mesh->faces);
    } break;
    case 1: {
      gen_success = admmpd_init_with_lattice(iface,in_mesh->verts,in_mesh->faces);
    } break;
  }
  if (!gen_success || iface->totverts==0)
  {
    printf("**ADMMPD Failed to generate tets\n");
    return 0;
  }

  // Initialize
  bool init_success = false;
  try
  {
    init_success = admmpd::Solver().init(
      iface->idata->mesh.get(),
      iface->idata->options.get(),
      iface->idata->data.get());
  }
  catch(const std::exception &e)
  {
    printf("**ADMMPD Error on init: %s\n", e.what());
  }

  if (!init_success)
  {
    printf("**ADMMPD Failed to initialize\n");
    return 0;
  }

  return 1;
}

void admmpd_copy_from_bodypoint(ADMMPDInterfaceData *iface, const BodyPoint *pts)
{
  if (iface == NULL || pts == NULL)
    return;

  for (int i=0; i<iface->totverts; ++i)
  {
    const BodyPoint *pt = &pts[i];
    for(int j=0; j<3; ++j)
    {
      iface->idata->data->x(i,j)=pt->pos[j];
      iface->idata->data->v(i,j)=pt->vec[j];
    }
  }
}

void admmpd_update_obstacles(
    ADMMPDInterfaceData *iface,
    float *in_verts_0,
    float *in_verts_1,
    int nv,
    unsigned int *in_faces,
    int nf)
{
    if (iface==NULL || in_verts_0==NULL || in_verts_1==NULL || in_faces==NULL)
      return;
    if (iface->idata==NULL)
      return;
    if (iface->idata->collision==NULL)
      return;

    iface->idata->collision->set_obstacles(
      in_verts_0, in_verts_1, nv, in_faces, nf);
}

void admmpd_update_goals(
    ADMMPDInterfaceData *iface,
    float *goal_k, // goal stiffness, nv
    float *goal_pos, // goal position, nv x 3
    int nv)
{
    if (iface==NULL || goal_k==NULL || goal_pos==NULL)
      return;
    if (iface->idata==NULL)
      return;
    if (!iface->idata->mesh)
      return;

    for (int i=0; i<nv; ++i)
    {
      if (goal_k[i] <= 0.f)
        continue;

      Eigen::Vector3d ki = Eigen::Vector3d::Ones() * goal_k[i];
      Eigen::Vector3d qi(goal_pos[i*3+0], goal_pos[i*3+1], goal_pos[i*3+2]);
      iface->idata->mesh->set_pin(i,qi,ki);
    }
}

void admmpd_copy_to_bodypoint_and_object(ADMMPDInterfaceData *iface, BodyPoint *pts, float (*vertexCos)[3])
{

  if (iface == NULL)
    return;

  // Map the deforming vertices to BodyPoint
  for (int i=0; i<iface->totverts; ++i)
  {
    if (pts != NULL)
    {
      BodyPoint *pt = &pts[i];
      for(int j=0; j<3; ++j)
      {
        pt->pos[j] = iface->idata->data->x(i,j);
        pt->vec[j] = iface->idata->data->v(i,j);
      }
    }
  }

  // Map the facet vertices 
  if (vertexCos != NULL)
  {
      for (int i=0; i<iface->mesh_totverts; ++i)
      {
        Eigen::Vector3d xi =
          iface->idata->mesh->get_mapped_facet_vertex(
          iface->idata->data->x, i);
        vertexCos[i][0] = xi[0];
        vertexCos[i][1] = xi[1];
        vertexCos[i][2] = xi[2];
      }
  }

} // end map ADMMPD to bodypoint and object

void admmpd_solve(ADMMPDInterfaceData *iface)
{
  
  if (iface == NULL)
    return;

  if (iface->idata == NULL || !iface->idata->options || !iface->idata->data)
    return;

  try
  {
    admmpd::Solver().solve(
        iface->idata->mesh.get(),
        iface->idata->options.get(),
        iface->idata->data.get(),
        iface->idata->collision.get());
  }
  catch(const std::exception &e)
  {
    iface->idata->data->x = iface->idata->data->x_start;
    printf("**ADMMPD Error on solve: %s\n", e.what());
  }
}