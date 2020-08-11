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
#include "DNA_object_force_types.h" // Enums
#include "BKE_mesh_remesh_voxel.h" // TetGen
#include "BKE_mesh.h" // BKE_mesh_free
#include "BKE_softbody.h" // BodyPoint
#include "MEM_guardedalloc.h"

#include <iostream>
#include <memory>
#include <algorithm>

#define ADMMPD_API_DEBUG

// Collision obstacles are cached until
// solve(...) is called. If we are substepping,
// the obstacle is interpolated from start to end.
struct CollisionObstacle
{
  Eigen::VectorXf x0, x1;
  std::vector<unsigned int> F;
};

struct ADMMPDInternalData
{
  // Created in admmpd_update_mesh
  std::shared_ptr<admmpd::Mesh> mesh;
  std::shared_ptr<admmpd::Collision> collision;
  // Created in admmpd_update_solver
  std::shared_ptr<admmpd::Options> options;
  std::shared_ptr<admmpd::SolverData> data;
  // Created in set_obstacles
  CollisionObstacle obs;
};


static inline void strcpy_error(ADMMPDInterfaceData *iface, const std::string &str)
{
  int len = std::min(256, (int)str.size()+1);
  memset(iface->last_error, 0, sizeof(iface->last_error));
  str.copy(iface->last_error, len);
}

static inline void options_from_object(
  ADMMPDInterfaceData *iface,
  Scene *scene,
  Object *ob,
  admmpd::Options *op,
  bool skip_require_reset)
{
  (void)(iface);

  SoftBody *sb = ob->soft;
  if (sb==NULL)
    return;

  // Set options that don't require a re-initialization
  op->max_admm_iters = std::max(1,sb->admmpd_max_admm_iters);
  op->min_res = std::max(0.f,sb->admmpd_converge_eps);
  op->mult_pk = std::max(0.f,std::min(1.f,sb->admmpd_goalstiff));
  op->mult_ck = std::max(0.f,std::min(1.f,sb->admmpd_collisionstiff));
  op->floor = sb->admmpd_floor_z;
  op->self_collision = sb->admmpd_self_collision;
  op->log_level = std::max(0, std::min(LOGLEVEL_NUM-1, sb->admmpd_loglevel));
  op->grav = Eigen::Vector3d(0,0,sb->admmpd_gravity);
  op->max_threads = sb->admmpd_maxthreads;
  op->linsolver = std::max(0, std::min(LINSOLVER_NUM-1, sb->admmpd_linsolver));

  if (!skip_require_reset)
  {
    if (scene)
    {
      float framerate = scene->r.frs_sec / scene->r.frs_sec_base;
      float fps = std::min(1000.f,std::max(1.f,framerate));
      op->timestep_s = (1.0/fps) / float(std::max(1,sb->admmpd_substeps));
    }
    op->density_kgm3 = std::max(1.f,sb->admmpd_density_kgm3);
    op->youngs = std::pow(10.f, std::max(0.f,sb->admmpd_youngs_exp));
    op->poisson = std::max(0.f,std::min(0.499f,sb->admmpd_poisson));
    op->elastic_material = std::max(0, std::min(ELASTIC_NUM-1, sb->admmpd_material));
    op->substeps = std::max(1,sb->admmpd_substeps);
  }
}

static inline void vecs_from_object(
  Object *ob,
  float (*vertexCos)[3],
  std::vector<float> &v,
  std::vector<unsigned int> &f)
{
  if(ob->type != OB_MESH)
    return;

  Mesh *me = (Mesh*)ob->data;

  // Initialize input vertices
  v.resize(me->totvert*3, 0);
  for (int i=0; i<me->totvert; ++i)
  {
    // Local to global coordinates
    float vi[3];
    vi[0] = vertexCos[i][0];
    vi[1] = vertexCos[i][1];
    vi[2] = vertexCos[i][2];
    mul_m4_v3(ob->obmat, vi);
    for (int j=0; j<3; ++j) {
      v[i*3+j] = vi[j];
    }
  } // end loop input surface verts

  // Initialize input faces
  int totfaces = poly_to_tri_count(me->totpoly, me->totloop);
  f.resize(totfaces*3, 0);
  MLoopTri *looptri, *lt;
  looptri = lt = (MLoopTri *)MEM_callocN(sizeof(*looptri)*totfaces, __func__);
  BKE_mesh_recalc_looptri(me->mloop, me->mpoly, me->mvert, me->totloop, me->totpoly, looptri);
  for (int i=0; i<totfaces; ++i, ++lt)
  {
      f[i*3+0] = me->mloop[lt->tri[0]].v;
      f[i*3+1] = me->mloop[lt->tri[1]].v;
      f[i*3+2] = me->mloop[lt->tri[2]].v;
  }
  MEM_freeN(looptri);
  looptri = NULL;
}

void admmpd_dealloc(ADMMPDInterfaceData *iface)
{
  if (!iface) { return; }
  memset(iface->last_error, 0, sizeof(iface->last_error));

  if (!iface->idata) { return; }
  iface->idata->options.reset();
  iface->idata->data.reset();
  iface->idata->collision.reset();
  iface->idata->mesh.reset();
  MEM_freeN((void*)iface->idata);
  iface->idata = nullptr;
}

static inline int admmpd_init_with_tetgen(ADMMPDInterfaceData *iface, Object *ob, float (*vertexCos)[3])
{
  std::vector<float> v;
  std::vector<unsigned int> f;
  vecs_from_object(ob,vertexCos,v,f);

  TetGenRemeshData tg;
  init_tetgenremeshdata(&tg);
  tg.in_verts = v.data();
  tg.in_totverts = v.size()/3;
  tg.in_faces = f.data();
  tg.in_totfaces = f.size()/3;
  bool success = tetgen_resmesh(&tg);
  if (!success || tg.out_tottets==0)
  {
    strcpy_error(iface, "TetGen failed to generate");
    return 0;
  }

  // Double check assumption, the first
  // mesh_totverts vertices remain the same
  // for input and output mesh.
  #ifdef ADMMPD_API_DEBUG
    for (int i=0; i<tg.in_totverts; ++i)
    {
      for (int j=0; j<3; ++j)
      {
        float diff = std::abs(v[i*3+j]-tg.out_verts[i*3+j]);
        if (diff > 1e-10)
        {
          strcpy_error(iface, "Bad TetGen assumption: change in surface verts");
          return 0;
        }
      }
    }
  #endif

  iface->idata->mesh = std::make_shared<admmpd::TetMesh>();
  success = iface->idata->mesh->create(
    tg.out_verts,
    tg.out_totverts,
    tg.out_facets,
    tg.out_totfacets,
    tg.out_tets,
    tg.out_tottets);

  if (!success)
  {
    strcpy_error(iface, "TetMesh failed on creation");
    return 0;
  }

  // Clean up tetgen output data
  MEM_freeN(tg.out_tets);
  MEM_freeN(tg.out_facets);
  MEM_freeN(tg.out_verts);

  return 1;
}

static inline int admmpd_init_with_lattice(ADMMPDInterfaceData *iface, Object *ob, float (*vertexCos)[3])
{
  std::vector<float> v;
  std::vector<unsigned int> f;
  vecs_from_object(ob,vertexCos,v,f);
  iface->idata->mesh = std::make_shared<admmpd::EmbeddedMesh>();

  admmpd::EmbeddedMesh* emb_msh = static_cast<admmpd::EmbeddedMesh*>(iface->idata->mesh.get());
  emb_msh->options.max_subdiv_levels = ob->soft->admmpd_embed_res;
  bool success = iface->idata->mesh->create(
    v.data(),
    v.size()/3,
    f.data(),
    f.size()/3,
    nullptr,
    0);

  if (!success)
  {
    strcpy_error(iface, "EmbeddedMesh failed on creation");
    return 0;
  }

  iface->idata->collision = std::make_shared<admmpd::EmbeddedMeshCollision>();
  return 1;
}

static inline int admmpd_init_as_cloth(ADMMPDInterfaceData *iface, Object *ob, float (*vertexCos)[3])
{
  std::vector<float> v;
  std::vector<unsigned int> f;
  vecs_from_object(ob,vertexCos,v,f);

  iface->idata->mesh = std::make_shared<admmpd::TriangleMesh>();
  bool success = iface->idata->mesh->create(
    v.data(),
    v.size()/3,
    f.data(),
    f.size()/3,
    nullptr,
    0);

  if (!success)
  {
    strcpy_error(iface, "TriangleMesh failed on creation");
    return 0;
  }

  iface->idata->collision = nullptr; // TODO, triangle mesh collision
  return 1;
}

// Given the mesh, options, and data, initializes the solver
static inline int admmpd_reinit_solver(ADMMPDInterfaceData *iface)
{
  if (!iface) { return 0; }
  if (!iface->idata) { return 0; }
  if (!iface->idata->mesh) { return 0; }
  if (!iface->idata->options) { return 0; }
  if (!iface->idata->data) { return 0; }

  try
  {
    admmpd::Solver().init(
      iface->idata->mesh.get(),
      iface->idata->options.get(),
      iface->idata->data.get());
  }
  catch(const std::exception &e)
  {
    strcpy_error(iface, e.what());
    return 0;
  }
  return 1;
}

int admmpd_mesh_needs_update(ADMMPDInterfaceData *iface, Object *ob)
{
  if (!iface) { return 0; }
  if (!ob) { return 0; }
  if (!ob->soft) { return 0; }
  if (!iface->idata) { return 1; }
  if (!iface->idata->mesh) { return 1; }

  // Mode or topology change?
  int mode = ob->soft->admmpd_init_mode;
  int mesh_type = iface->idata->mesh->type();
  if (mode != mesh_type) { return 1; }
  switch (mode)
  {
    default:
    case MESHTYPE_EMBEDDED:
    case MESHTYPE_TET: {
      int nv = iface->idata->mesh->rest_prim_verts()->rows();
      if (nv != ob->soft->totpoint) { return 1; }
    } break;
    case MESHTYPE_TRIANGLE: {
      int nv = iface->idata->mesh->rest_facet_verts()->rows();
      if (nv != ob->soft->totpoint) { return 1; }
    } break;
  }

  return 0;
}

int admmpd_update_mesh(ADMMPDInterfaceData *iface, Object *ob, float (*vertexCos)[3])
{
  if (!iface) { return 0; }
  if (!ob) { return 0; }
  if (!ob->soft) { return 0; }

  if (!iface->idata)
    iface->idata = (ADMMPDInternalData*)MEM_callocN(sizeof(ADMMPDInternalData), "ADMMPD_idata");

  int mode = ob->soft->admmpd_init_mode;
  iface->idata->mesh.reset();

  // Try to initialize the mesh
  const Eigen::MatrixXd *x0 = nullptr;
  try
  {
    int gen_success = 0;
    switch (mode)
    {
      default:
      case MESHTYPE_EMBEDDED: {
        gen_success = admmpd_init_with_lattice(iface,ob,vertexCos);
        if (gen_success) {
          x0 = iface->idata->mesh->rest_prim_verts();
        }
      } break;
      case MESHTYPE_TET: {
        gen_success = admmpd_init_with_tetgen(iface,ob,vertexCos);
        if (gen_success) {
          x0 = iface->idata->mesh->rest_prim_verts();
        }
      } break;
      case MESHTYPE_TRIANGLE: {
        gen_success = admmpd_init_as_cloth(iface,ob,vertexCos);
        if (gen_success) {
          x0 = iface->idata->mesh->rest_facet_verts();
        }
      } break;
    }
    if (!gen_success || !iface->idata->mesh || x0==nullptr)
    {
      strcpy_error(iface, "failed to init mesh");
      return 0;
    }
  }
  catch(const std::exception &e)
  {
    strcpy_error(iface, e.what());
    return 0;
  }

  // Set up softbody to store defo verts
  int n_defo_verts = x0->rows();
  SoftBody *sb = ob->soft;
  if (sb->bpoint)
    MEM_freeN(sb->bpoint);

  sb->totpoint = n_defo_verts;
  sb->totspring = 0;
  sb->bpoint = (BodyPoint*)MEM_callocN(n_defo_verts*sizeof(BodyPoint), "ADMMPD_bpoint");

  // Copy init data to BodyPoint
  BodyPoint *pts = sb->bpoint;
  for (int i=0; i<n_defo_verts; ++i)
  {
    BodyPoint *pt = &pts[i];
    for(int j=0; j<3; ++j)
    {
      pt->pos[j] = x0->operator()(i,j);
      pt->vec[j] = 0;
    }
  }

  return 1;
}

int admmpd_solver_needs_update(ADMMPDInterfaceData *iface, Scene *sc, Object *ob)
{
  (void)(sc);
  if (!iface) { return 0; }
  if (!ob) { return 0; }
  if (!ob->soft) { return 0; }
  if (!iface->idata) { return 1; }
  if (!iface->idata->options) { return 1; }
  if (!iface->idata->data) { return 1; }

  auto big_diff = [](const double &a, const double &b) {
    if (std::abs(a-b) > 1e-4) { return true; }
    return false;
  };

  admmpd::Options *opt = iface->idata->options.get();
  SoftBody *sb = ob->soft;
  if (sb->admmpd_material != opt->elastic_material) { return 1; }
  if (sb->admmpd_substeps != opt->substeps) { return 1; }
  double youngs = std::pow(10.f, std::max(0.f,sb->admmpd_youngs_exp));
  if (big_diff(youngs, opt->youngs)) { return 1; }
  if (big_diff(sb->admmpd_density_kgm3, opt->density_kgm3)) { return 1; }
  if (big_diff(sb->admmpd_poisson, opt->poisson)) { return 1; }

  return 0;
}

int admmpd_update_solver(ADMMPDInterfaceData *iface,  Scene *sc, Object *ob, float (*vertexCos)[3])
{
  if (!iface) { return 0; }
  if (!ob) { return 0; }
  if (!ob->soft) { return 0; }
  (void)(vertexCos);

  // idata is created in admmpd_update_mesh
  if (!iface->idata) { return 0; }
  if (!iface->idata->mesh) { return 0; }

  // Reset options and data
  iface->idata->options = std::make_shared<admmpd::Options>();
  iface->idata->data = std::make_shared<admmpd::SolverData>();

  admmpd::Options *op = iface->idata->options.get();
  options_from_object(iface,sc,ob,op,false);

  try
  {
    admmpd::Solver().init(
      iface->idata->mesh.get(),
      iface->idata->options.get(),
      iface->idata->data.get());
  }
  catch(const std::exception &e)
  {
    strcpy_error(iface, e.what());
    return 0;
  }

  return 1;
}

void admmpd_copy_from_object(ADMMPDInterfaceData *iface, Object *ob)
{
  if (!iface) { return; }
  if (!iface->idata) { return; }
  if (!iface->idata->data) { return; }
  if (!ob) { return; }
  if (!ob->soft) { return; }
  if (!ob->soft->bpoint) { return; }

  // Should be the same, but if not we'll just
  // copy over up to the amount that we can.
  int nx = iface->idata->data->x.rows();
  int nv = std::min(ob->soft->totpoint, nx);
  for (int i=0; i<nv; ++i)
  {
    const BodyPoint *pt = &ob->soft->bpoint[i];
    for(int j=0; j<3; ++j)
    {
      iface->idata->data->x(i,j)=pt->pos[j];
      iface->idata->data->v(i,j)=pt->vec[j];
    }
  }
}

void admmpd_copy_to_object(ADMMPDInterfaceData *iface, Object *ob, float (*vertexCos)[3])
{
  if (!iface) { return; }
  if (!iface->idata) { return; }
  if (!iface->idata->data) { return; }
  if (!iface->idata->mesh) { return; }
  if (!iface->idata->mesh->rest_facet_verts()) { return; }

  int nx = iface->idata->data->x.rows();

  if (ob && ob->soft)
  {
    int np = std::min(ob->soft->totpoint, nx);
    SoftBody *sb = ob->soft;
    if (!sb->bpoint || nx != np)
    {
      // If we have a bpoint but it's the wrong size
      if (ob->soft->bpoint && nx != np)
        MEM_freeN(sb->bpoint);

      // Create bpoint if we don't have one
      if (!ob->soft->bpoint)
        sb->bpoint = (BodyPoint*)MEM_callocN(nx*sizeof(BodyPoint), "ADMMPD_bpoint");

      sb->totpoint = nx;
      sb->totspring = 0;
    }

    // Copy internal data to BodyPoint
    for (int i=0; i<nx; ++i)
    {
      BodyPoint *pt = &ob->soft->bpoint[i];
      for(int j=0; j<3; ++j)
      {
        pt->pos[j] = iface->idata->data->x(i,j);
        pt->vec[j] = iface->idata->data->v(i,j);
      }
    }
  } // end has ob ptr

  // Copy to vertexCos
  int nfv = iface->idata->mesh->rest_facet_verts()->rows();
  if (vertexCos != NULL)
  {
    for (int i=0; i<nfv; ++i)
    {
      Eigen::Vector3d xi =
        iface->idata->mesh->get_mapped_facet_vertex(
        &iface->idata->data->x, i);
      vertexCos[i][0] = xi[0];
      vertexCos[i][1] = xi[1];
      vertexCos[i][2] = xi[2];
      if (ob && ob->soft && ob->soft->local==0)
      {
        mul_m4_v3(ob->imat, vertexCos[i]);
      }
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
  if (!iface->idata) { return; }
  if (!iface->idata->collision) { return; }
  if (nf==0 || nv==0) { return; }

  int nv3 = nv*3;
  iface->idata->obs.x0.resize(nv3);
  iface->idata->obs.x1.resize(nv3);
  int nf3 = nf*3;
  iface->idata->obs.F.resize(nf3);

  for (int i=0; i<nv3; ++i)
  {
    iface->idata->obs.x0[i] = in_verts_0[i];
    iface->idata->obs.x1[i] = in_verts_1[i];
  }
  for (int i=0; i<nf3; ++i)
    iface->idata->obs.F[i] = in_faces[i];

}

void admmpd_update_goals(
    ADMMPDInterfaceData *iface,
    float *goal_k, // goal stiffness, nv
    float *goal_pos, // goal position, nv x 3
    int nv)
{
    if (iface==NULL || goal_k==NULL || goal_pos==NULL)
      return;
    if (!iface->idata)
      return;
    if (!iface->idata->mesh)
      return;

    for (int i=0; i<nv; ++i)
    {
      // We want to call set_pin for every vertex, even
      // if stiffness is zero. This allows us to animate pins on/off
      // without calling Mesh::clear_pins().
      Eigen::Vector3d qi(goal_pos[i*3+0], goal_pos[i*3+1], goal_pos[i*3+2]);
      iface->idata->mesh->set_pin(i,qi,goal_k[i]);
    }
}

int admmpd_solve(ADMMPDInterfaceData *iface, Object *ob)
{
  if (!iface || !ob || !ob->soft)
  {
    strcpy_error(iface, "NULL input");
    return 0;
  }

  if (!iface->idata || !iface->idata->options || !iface->idata->data)
  {
    strcpy_error(iface, "NULL internal data");
    return 0;
  }

  // Change only options that do not cause a reset of the solver.
  options_from_object(
    iface,
    nullptr, // scene, ignored if null
    ob,
    iface->idata->options.get(),
    true);

  // Changing the location of the obstacles requires a recompuation
  // of the SDF. So we'll only do that if we need to:
  // a) we are substepping (need to lerp)
  // b) the obstacle is actually moving.
  bool has_obstacles = 
    iface->idata->collision &&
    iface->idata->obs.x0.size() > 0 &&
    iface->idata->obs.F.size() > 0 &&
    iface->idata->obs.x0.size()==iface->idata->obs.x1.size();
  bool lerp_obstacles =
    has_obstacles &&
    iface->idata->options->substeps>1 &&
    (iface->idata->obs.x0-iface->idata->obs.x1).lpNorm<Eigen::Infinity>()>1e-6;

  if (has_obstacles && !lerp_obstacles)
  {
    iface->idata->collision->set_obstacles(
      iface->idata->obs.x0.data(),
      iface->idata->obs.x1.data(),
      iface->idata->obs.x0.size()/3,
      iface->idata->obs.F.data(),
      iface->idata->obs.F.size()/3);
  }

  try
  {
    Eigen::VectorXf obs_x1; // used if substeps > 1
    int substeps = std::max(1,iface->idata->options->substeps);
    for (int i=0; i<substeps; ++i)
    {
      if (lerp_obstacles)
      {
          float t = float(i)/float(substeps-1);
          obs_x1 = (1.f-t)*iface->idata->obs.x0 + t*iface->idata->obs.x1;
          iface->idata->collision->set_obstacles(
            iface->idata->obs.x0.data(),
            obs_x1.data(),
            iface->idata->obs.x0.size()/3,
            iface->idata->obs.F.data(),
            iface->idata->obs.F.size()/3);
      }
      admmpd::Solver().solve(
        iface->idata->mesh.get(),
        iface->idata->options.get(),
        iface->idata->data.get(),
        iface->idata->collision.get());
    }
  }
  catch(const std::exception &e)
  {
    iface->idata->data->x = iface->idata->data->x_start;
    strcpy_error(iface, e.what());
    return 0;
  }

  return 1;
}