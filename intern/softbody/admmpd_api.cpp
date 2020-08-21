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
#ifdef WITH_TETGEN
  #include "tetgen.h"
#endif
#include "DNA_mesh_types.h" // Mesh
#include "DNA_meshdata_types.h" // MVert
#include "DNA_modifier_types.h" // CollisionModifierData
#include "DNA_object_force_types.h" // Enums
#include "BKE_mesh.h" // BKE_mesh_free
#include "BKE_softbody.h" // BodyPoint
#include "BKE_deform.h" // BKE_defvert_find_index
#include "BKE_modifier.h" // BKE_modifiers_findby_type
#include "MEM_guardedalloc.h"

#include <iostream>
#include <memory>
#include <algorithm>

struct ADMMPDInternalData
{
  // Created in admmpd_update_mesh
  std::shared_ptr<admmpd::Mesh> mesh;
  std::shared_ptr<admmpd::Collision> collision;
  // Created in admmpd_update_solver
  std::shared_ptr<admmpd::Options> options;
  std::shared_ptr<admmpd::SolverData> data;
  // Created in set_obstacles
  std::vector<Eigen::MatrixXd> obs_x0, obs_x1;
  std::vector<Eigen::MatrixXi> obs_F;
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
  if (sb==NULL) {
    return;
  }

  // Set options that don't require a re-initialization
  op->max_admm_iters = std::max(1,sb->admmpd_max_admm_iters);
  op->min_res = std::max(0.f,sb->admmpd_converge_eps);
  op->pk = std::pow(10.f, sb->admmpd_pk_exp);
  op->ck = std::pow(10.f, sb->admmpd_ck_exp);
  op->floor = sb->admmpd_floor_z;
  op->self_collision = sb->admmpd_self_collision;
  op->log_level = std::max(0, std::min(LOGLEVEL_NUM-1, sb->admmpd_loglevel));
  op->grav = Eigen::Vector3d(0,0,sb->admmpd_gravity);
  op->max_threads = sb->admmpd_maxthreads;
  op->linsolver = std::max(0, std::min(LINSOLVER_NUM-1, sb->admmpd_linsolver));
  op->strain_limit[0] = std::min(1.f, sb->admmpd_strainlimit_min);
  op->strain_limit[1] = std::max(1.f, sb->admmpd_strainlimit_max);
  op->lattice_subdiv = std::max(1,sb->admmpd_embed_res);

  if (!skip_require_reset)
  {
    if (scene)
    {
      float framerate = scene->r.frs_sec / scene->r.frs_sec_base;
      float fps = std::min(1000.f,std::max(1.f,framerate));
      op->timestep_s = (1.0/fps) / float(std::max(1,sb->admmpd_substeps));
    }
    op->density_kgm3 = std::max(1.f,sb->admmpd_density_kgm3);
    op->youngs = std::pow(10.f, sb->admmpd_youngs_exp);
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
  if(ob->type != OB_MESH) {
    return;
  }

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

static inline int admmpd_init_with_tetgen(ADMMPDInterfaceData *iface, Object *ob, float (*vertexCos)[3]);

static inline int admmpd_init_with_lattice(ADMMPDInterfaceData *iface, Object *ob, float (*vertexCos)[3])
{
  std::vector<float> v;
  std::vector<unsigned int> f;
  vecs_from_object(ob,vertexCos,v,f);
  iface->idata->mesh = std::make_shared<admmpd::EmbeddedMesh>();
  bool success = iface->idata->mesh->create(
    iface->idata->options.get(),
    v.data(),
    v.size()/3,
    f.data(),
    f.size()/3,
    nullptr,
    0);

  if (!success) { // soft unknown fail
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
    iface->idata->options.get(),
    v.data(),
    v.size()/3,
    f.data(),
    f.size()/3,
    nullptr,
    0);

  if (!success) {
    strcpy_error(iface, "TriangleMesh failed on creation");
    return 0;
  }

  iface->idata->collision = nullptr; // TODO, triangle mesh collision
  return 1;
}

void admmpd_compute_lattice(
    int subdiv,
    float *in_verts, int in_nv,
    unsigned int *in_faces, int in_nf,
    float **out_verts, int *out_nv,
    unsigned int **out_tets, int *out_nt)
{

  admmpd::EmbeddedMesh emesh;
  admmpd::Options opt;
  opt.lattice_subdiv = subdiv;
  bool success = emesh.create(
    &opt,
    in_verts, in_nv,
    in_faces, in_nf,
    nullptr,
    0);

  if (!success) {
    return;
  }

  const Eigen::MatrixXd &vt = *emesh.rest_prim_verts();
  const Eigen::MatrixXi &t = *emesh.prims();
  if (vt.rows()==0 || t.rows()==0) {
    return;
  }

  *out_nv = vt.rows();
  *out_verts = (float*)MEM_callocN(sizeof(float)*3*(vt.rows()), "ADMMPD_lattice_verts");
  *out_nt = t.rows();
  *out_tets = (unsigned int*)MEM_callocN(sizeof(unsigned int)*4*(t.rows()), "ADMMPD_lattice_tets");

  for (int i=0; i<vt.rows(); ++i) {
    (*out_verts)[i*3+0] = vt(i,0);
    (*out_verts)[i*3+1] = vt(i,1);
    (*out_verts)[i*3+2] = vt(i,2);
  }

  for (int i=0; i<t.rows(); ++i) {
    (*out_tets)[i*4+0] = t(i,0);
    (*out_tets)[i*4+1] = t(i,1);
    (*out_tets)[i*4+2] = t(i,2);
    (*out_tets)[i*4+3] = t(i,3);
  }
}

int admmpd_mesh_needs_update(ADMMPDInterfaceData *iface, Object *ob)
{
  if (!iface) { return 0; }
  if (!ob) { return 0; }
  if (!ob->soft) { return 0; }
  if (!ob->data) { return 0; }
  if (!iface->idata) { return 1; }
  if (!iface->idata->mesh) { return 1; }
  Mesh *mesh = (Mesh*)ob->data;
  if (!mesh) { return 0; }

  // Mode or topology change?
  int mode = ob->soft->admmpd_mesh_mode;
  int mesh_type = iface->idata->mesh->type();

  if (mode != mesh_type) { return 1; }
  if (!iface->idata->mesh->rest_facet_verts()) { return 1; }
  int nx = iface->idata->mesh->rest_facet_verts()->rows();
  if (nx != mesh->totvert) { return 1; }

  return 0;
}

int admmpd_update_mesh(ADMMPDInterfaceData *iface, Object *ob, float (*vertexCos)[3])
{
  if (!iface) { return 0; }
  if (!ob) { return 0; }
  if (!ob->soft) { return 0; }

  if (!iface->idata) {
    iface->idata = (ADMMPDInternalData*)MEM_callocN(sizeof(ADMMPDInternalData), "ADMMPD_idata");
  }

  if (!iface->idata->options) {
    iface->idata->options = std::make_shared<admmpd::Options>();
  }

  options_from_object(iface,NULL,ob,iface->idata->options.get(),false);
  int mode = ob->soft->admmpd_mesh_mode;
  iface->idata->mesh.reset();

  // Try to initialize the mesh
  const Eigen::MatrixXd *x0 = nullptr;
  try {
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
    if (!gen_success || !iface->idata->mesh || x0==nullptr) {
      return 0;
    }
  }
  catch(const std::exception &e) {
    strcpy_error(iface, e.what());
    return 0;
  }

  // Set up softbody to store defo verts
  int n_defo_verts = x0->rows();
  SoftBody *sb = ob->soft;
  if (sb->bpoint) {
    MEM_freeN(sb->bpoint);
  }

  sb->totpoint = n_defo_verts;
  sb->totspring = 0;
  sb->bpoint = (BodyPoint*)MEM_callocN(n_defo_verts*sizeof(BodyPoint), "ADMMPD_bpoint");

  // Copy init data to BodyPoint
  BodyPoint *pts = sb->bpoint;
  for (int i=0; i<n_defo_verts; ++i) {
    BodyPoint *pt = &pts[i];
    for(int j=0; j<3; ++j) {
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
  if (!iface->idata->options) {
    iface->idata->options = std::make_shared<admmpd::Options>();
  }
  iface->idata->data = std::make_shared<admmpd::SolverData>();
  iface->idata->obs_x0.clear();
  iface->idata->obs_x1.clear();
  iface->idata->obs_F.clear();

  admmpd::Options *op = iface->idata->options.get();
  options_from_object(iface,sc,ob,op,false);

  try {
    admmpd::Solver().init(
      iface->idata->mesh.get(),
      iface->idata->options.get(),
      iface->idata->data.get());
  }
  catch(const std::exception &e) {
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
  for (int i=0; i<nv; ++i) {
    const BodyPoint *pt = &ob->soft->bpoint[i];
    for(int j=0; j<3; ++j) {
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

  if (ob && ob->soft) {
    SoftBody *sb = ob->soft;

    if (!sb->bpoint) {
      if (!ob->soft->bpoint) {
        sb->bpoint = (BodyPoint*)MEM_callocN(nx*sizeof(BodyPoint), "ADMMPD_bpoint");
      }
      sb->totpoint = nx;
      sb->totspring = 0;
    }

    if (sb->totpoint != nx && sb->totpoint>0) {
      MEM_freeN(sb->bpoint);
      sb->bpoint = (BodyPoint*)MEM_callocN(nx*sizeof(BodyPoint), "ADMMPD_bpoint");
      sb->totpoint = nx;
      sb->totspring = 0;
    }

    // Copy internal data to BodyPoint
    int np = std::min(ob->soft->totpoint, nx);
    for (int i=0; i<np; ++i) {
      BodyPoint *pt = &ob->soft->bpoint[i];
      for(int j=0; j<3; ++j) {
        pt->pos[j] = iface->idata->data->x(i,j);
        pt->vec[j] = iface->idata->data->v(i,j);
      }
    }
  } // end has ob ptr

  // Copy to vertexCos
  int nfv = iface->idata->mesh->rest_facet_verts()->rows();
  if (vertexCos != NULL) {
    for (int i=0; i<nfv; ++i) {
      Eigen::Vector3d xi =
        iface->idata->mesh->get_mapped_facet_vertex(
        &iface->idata->data->x, i);
      vertexCos[i][0] = xi[0];
      vertexCos[i][1] = xi[1];
      vertexCos[i][2] = xi[2];
      if (ob && ob->soft && ob->soft->local==0) {
        mul_m4_v3(ob->imat, vertexCos[i]);
      }
    }
  }
}

static inline void admmpd_update_goals(ADMMPDInterfaceData *iface, Object *ob, float (*vertexCos)[3])
{
  if (!iface) { return; }
  if (!iface->idata) { return; }
  if (!iface->idata->mesh) { return; }
  if (!ob) { return; }
  if (!ob->soft) { return; }

  SoftBody *sb = ob->soft;
  Mesh *me = (Mesh*)ob->data;
  if (!me) { return; }

  // Goal positions turned off
  if (!(ob->softflag & OB_SB_GOAL)) {
    iface->idata->mesh->clear_pins();
    return;
  }

  int defgroup_index = me->dvert ? (sb->vertgroup - 1) : -1;
  int nv = me->totvert;

  for (int i=0; i<nv; i++) {
    double k = 0.1;
    if ((ob->softflag & OB_SB_GOAL) && (defgroup_index != -1)) {
      MDeformWeight *dw = BKE_defvert_find_index(&(me->dvert[i]), defgroup_index);
      k = dw ? dw->weight : 0.0f;
    }

    Eigen::Vector3d goal_pos(0,0,0);
    float vi[3];
    vi[0] = vertexCos[i][0];
    vi[1] = vertexCos[i][1];
    vi[2] = vertexCos[i][2];
    mul_m4_v3(ob->obmat, vi);
    for (int j=0; j<3; ++j) {
      goal_pos[j] = vi[j];
    }

    // We want to call set_pin for every vertex, even
    // if stiffness is zero. This allows us to animate pins on/off
    // without calling Mesh::clear_pins().
    iface->idata->mesh->set_pin(i,goal_pos,k);

  } // end loop verts
} // end update goals

static inline void update_selfcollision_group(ADMMPDInterfaceData *iface, Object *ob)
{
  if (!iface) { return; }
  if (!iface->idata) { return; }
  if (!iface->idata->options) { return; }
  if (!iface->idata->data) { return; }
  if (!iface->idata->options->self_collision) { return; }

  if (!ob) { return; }
  if (!ob->soft) { return; }
  Mesh *me = (Mesh*)ob->data;
  if (!me) { return; }
  SoftBody *sb = ob->soft;

  int defgroup_idx_selfcollide = me->dvert ?
    BKE_object_defgroup_name_index(ob, sb->admmpd_namedVG_selfcollision) : -1;

  // If we do not have a self collision vertex group, we want to
  // do self collision on all vertices. If the selfcollision_verts set
  // is empty, the collider will test all verts.
  iface->idata->data->col.selfcollision_verts.clear();
  if (defgroup_idx_selfcollide == -1) {
    return;
  }

  // Otherwise, we need to set which vertices are to be tested.
  int nv = me->totvert;
  for (int i=0; i<nv; i++) {
    MDeformWeight *dw = BKE_defvert_find_index(&(me->dvert[i]), defgroup_idx_selfcollide);
    float wi = dw ? dw->weight : 0.0f;
    if (wi > 1e-2f) { // I guess we can use the weight as a threshold...
      iface->idata->data->col.selfcollision_verts.emplace(i);
    }
  }
}

int admmpd_solve(ADMMPDInterfaceData *iface, Object *ob, float (*vertexCos)[3])
{
  if (!iface || !ob || !ob->soft) {
    strcpy_error(iface, "NULL input");
    return 0;
  }

  if (!iface->idata || !iface->idata->options ||
      !iface->idata->data || !iface->idata->mesh) {
    strcpy_error(iface, "NULL internal data");
    return 0;
  }

  std::string meshname(ob->id.name);

  // Set to true if certain conditions should
  // throw a warning flag.
  bool return_warning = false;

  // Change only options that do not cause a reset of the solver.
  bool skip_solver_reset = true;
  options_from_object(
    iface,
    nullptr, // scene, ignored if null
    ob,
    iface->idata->options.get(),
    skip_solver_reset);

  // Disable self collision flag if the mesh does not support it.
  if (iface->idata->options->self_collision &&
    !iface->idata->mesh->self_collision_allowed()) {
    // Special message if embedded, in which the mesh is not closed.
    std::string err = "Cannot do self collisions on object "+meshname+" for selected mesh type";
    if (iface->idata->mesh->type() == MESHTYPE_EMBEDDED) {
      err = "Cannot do self collisions on object "+meshname+", mesh is not closed.";
    }
    strcpy_error(iface, err.c_str());
    iface->idata->options->self_collision = false;
    return_warning = true;
  }

  // Goals and self collision group can change
  // between time steps. If the goal indices/weights change,
  // it will trigger a refactorization in the solver.
  admmpd_update_goals(iface,ob,vertexCos);
  update_selfcollision_group(iface,ob);

  // Obstacle collisions not yet implemented
  // for cloth or tet mesh.
  if ((ob->soft->admmpd_mesh_mode == MESHTYPE_TET ||
    ob->soft->admmpd_mesh_mode == MESHTYPE_TRIANGLE) &&
    iface->idata->obs_x0.size()>0)
  {
    return_warning = true;
    strcpy_error(iface, "Obstacle collision not yet available for selected mesh mode.");
  }

  // Changing the location of the obstacles requires a recompuation
  // of the SDF. So we'll only do that if:
  // a) we are substepping (need to lerp)
  // b) the obstacle positions have changed from the last frame
  bool has_obstacles = 
    iface->idata->collision &&
    iface->idata->obs_x0.size() > 0 &&
    iface->idata->obs_x1.size() > 0 &&
    iface->idata->obs_x0[0].size()==iface->idata->obs_x1[0].size();

  int substeps = std::max(1,iface->idata->options->substeps);
  int n_obs = iface->idata->obs_x0.size();
  if (has_obstacles && substeps == 1) { // no lerp necessary
    std::string set_obs_error = "";
    if (!iface->idata->collision->set_obstacles(
        iface->idata->obs_x0, iface->idata->obs_x1, iface->idata->obs_F,
        &set_obs_error)) {
      strcpy_error(iface, set_obs_error.c_str());
      return_warning = true;
    }
  }

  try
  {
    std::vector<Eigen::MatrixXd> obs_x1_t;
    for (int i=0; i<substeps; ++i) {

      // Interpolate obstacles
      if (has_obstacles && substeps>1) {
        float t = float(i)/float(substeps-1);
        obs_x1_t.resize(n_obs);
        for (int j=0; j<n_obs; ++j) {
          obs_x1_t[j] = (1.f-t)*iface->idata->obs_x0[j] + t*iface->idata->obs_x1[j];
        }
        std::string set_obs_error = "";
        if (!iface->idata->collision->set_obstacles(
            iface->idata->obs_x0, iface->idata->obs_x1, iface->idata->obs_F,
            &set_obs_error)) {
          strcpy_error(iface, set_obs_error.c_str());
          return_warning = true;
        }
      }

      admmpd::Solver().solve(
        iface->idata->mesh.get(),
        iface->idata->options.get(),
        iface->idata->data.get(),
        iface->idata->collision.get());
    }
  }
  catch(const std::exception &e) {
    // This is a more important error than set obstacle error,
    // so if we had an exception we'll report that instead.
    iface->idata->data->x = iface->idata->data->x_start;
    strcpy_error(iface, e.what());
    return 0;
  }

  if (return_warning) {
    // We've already copied the error message.
    return -1;
  }

  return 1;
}

void admmpd_update_obstacles(ADMMPDInterfaceData *iface, Object **obstacles, int numobjects)
{
  // Because substepping may occur, we'll buffer the start and end states
  // of the obstacles. They will not be copied over to the collision pointer
  // until solve(), depending on the number of substeps, in which case
  // they are LERP'd
  iface->idata->obs_x0.clear();
  iface->idata->obs_x1.clear();
  iface->idata->obs_F.clear();
  if (!iface) { return; }
  if (!iface->idata) { return; }
  if (!obstacles || numobjects==0) { return; }

  for (int i = 0; i < numobjects; ++i) {
    Object *ob = obstacles[i];
    if (!ob) {
      continue; // uh?
    }
    if (ob->type != OB_MESH) {
      continue; // is not a mesh type
    }
    if (!ob->pd || !ob->pd->deflect) {
      continue; // is a non-collider
    }
    if (strcmp(ob->id.name,iface->name)==0) {
      continue; // skip self
    }

    CollisionModifierData *cmd = (CollisionModifierData *)BKE_modifiers_findby_type(
        ob, eModifierType_Collision);
    if (!cmd) {
      continue;
    }

    int idx = iface->idata->obs_x0.size();
    iface->idata->obs_x0.emplace_back(Eigen::MatrixXd(cmd->mvert_num,3));
    iface->idata->obs_x1.emplace_back(Eigen::MatrixXd(cmd->mvert_num,3));
    iface->idata->obs_F.emplace_back(Eigen::MatrixXi(cmd->tri_num,3));

    for (int j=0; j<cmd->mvert_num; ++j) {
      for (int k=0; k<3; ++k) {
        iface->idata->obs_x0[idx](j,k) = cmd->x[j].co[k];
        iface->idata->obs_x1[idx](j,k) = cmd->xnew[j].co[k];
      }
    }

    for (int j=0; j<cmd->tri_num; ++j) {
      for (int k=0; k<3; ++k) {
        iface->idata->obs_F[idx](j,k) = cmd->tri[j].tri[k];
      }
    }
  }
}

#ifdef WITH_TETGEN

static void make_tetgenio(
  float *verts,
  unsigned int *faces,
  int numverts,
  int numfaces,
  tetgenio &tgio )
{
  tgio.initialize();
  tgio.firstnumber = 0;
  tgio.mesh_dim = 3;
  tgio.numberofpoints = numverts;
  tgio.pointlist = new REAL[tgio.numberofpoints * 3];
//	tgio.pointlist = (REAL *)MEM_malloc_arrayN(
//		tgio.numberofpoints, 3 * sizeof(REAL), "tetgen remesh out verts");
  for (int i=0; i < tgio.numberofpoints; ++i)
    {
    tgio.pointlist[i*3+0] = verts[i*3+0];
    tgio.pointlist[i*3+1] = verts[i*3+1];
    tgio.pointlist[i*3+2] = verts[i*3+2];
  }
  tgio.numberoffacets = numfaces;
  tgio.facetlist = new tetgenio::facet[tgio.numberoffacets];
//	tgio.facetlist = (tetgenio::facet *)MEM_malloc_arrayN(
//		tgio.numberoffacets, sizeof(tetgenio::facet), "tetgen remesh out facets");  
  tgio.facetmarkerlist = new int[tgio.numberoffacets];
//	tgio.facetmarkerlist = (int *)MEM_malloc_arrayN(
//		tgio.numberoffacets, sizeof(int), "tetgen remesh out marker list");
  for (int i=0; i<numfaces; ++i)
    {
    tgio.facetmarkerlist[i] = i;
    tetgenio::facet *f = &tgio.facetlist[i];
    f->numberofholes = 0;
    f->holelist = NULL;
    f->numberofpolygons = 1;
    f->polygonlist = new tetgenio::polygon[1];
    tetgenio::polygon *p = &f->polygonlist[0];
    p->numberofvertices = 3;
    p->vertexlist = new int[3];
    p->vertexlist[0] = faces[i*3+0];
    p->vertexlist[1] = faces[i*3+1];
    p->vertexlist[2] = faces[i*3+2];
  }
}

static inline int admmpd_init_with_tetgen(ADMMPDInterfaceData *iface, Object *ob, float (*vertexCos)[3])
{
  if (!iface) { return 0; }
  if (!iface->idata) { return 0; }
  if (!ob) { return 0; }

  iface->idata->mesh = std::make_shared<admmpd::TetMesh>();
  iface->idata->collision.reset(); // TODO

  std::vector<float> v;
  std::vector<unsigned int> f;
  vecs_from_object(ob,vertexCos,v,f);

	// Set up the switches
  //double quality = 1.4; // changes topology
	std::stringstream switches;
	switches << "Q"; // quiet
  //if (quality > 0) { switches << "q" << quality; }
  //if (maxvol > 0) { switches << "a" << maxvol; }

  tetgenio in;
  make_tetgenio(v.data(), f.data(), v.size()/3, f.size()/3, in);
  tetgenio out;
  out.initialize();
  char *c_switches = (char *)switches.str().c_str();
  tetrahedralize(c_switches, &in, &out);

	if( out.numberoftetrahedra == 0 || out.numberofpoints == 0 )
  {
    strcpy_error(iface, "TetGen failed to generate");
    return 0;
  }

  // We'll create our custom list of facets to render
  // with blender. These are all of the triangles that
  // make up the inner and outer faces, without duplicates.
  // To avoid duplicates, we'll hash them as a string.
  // While not super efficient, neither is tetrahedralization...
  struct face {
      int f0, f1, f2;
      face(int f0_, int f1_, int f2_) : f0(f0_), f1(f1_), f2(f2_) {}
  };
  auto face_hash = [](int f0, int f1, int f2){
      return std::to_string(f0)+" "+std::to_string(f1)+" "+std::to_string(f2);
  };
  std::unordered_map<std::string,face> faces_map;

  int nt = out.numberoftetrahedra;
  std::vector<unsigned int> tets(nt*4);
  for (int i=0; i<nt; ++i)
  {
    tets[i*4+0] = out.tetrahedronlist[i*4+0];
    tets[i*4+1] = out.tetrahedronlist[i*4+1];
    tets[i*4+2] = out.tetrahedronlist[i*4+2];
    tets[i*4+3] = out.tetrahedronlist[i*4+3];

    // Append faces
    for(int j=0; j<4; ++j)
    {
      int f0 = tets[i*4+j];
      int f1 = tets[i*4+(j+1)%4];
      int f2 = tets[i*4+(j+2)%4];
      std::string hash = face_hash(f0,f1,f2);
      if (faces_map.count(hash)!=0) {
        continue;
      }
      faces_map.emplace(hash, face(f0,f1,f2));
    }
  }

  int nf = faces_map.size();
  std::vector<unsigned int> faces(nf*3);
  int f_idx = 0;
  for (std::unordered_map<std::string,face>::iterator it = faces_map.begin();
    it != faces_map.end(); ++it, ++f_idx)
  {
    faces[f_idx*3+0] = it->second.f0;
    faces[f_idx*3+1] = it->second.f1;
    faces[f_idx*3+2] = it->second.f2;    
  }

  int nv = out.numberofpoints;
  std::vector<float> verts(nv*3);
  for (int i=0; i<out.numberofpoints; ++i)
  {
    verts[i*3+0] = out.pointlist[i*3+0];
    verts[i*3+1] = out.pointlist[i*3+1];
    verts[i*3+2] = out.pointlist[i*3+2];
  }

  // In the future we can compute a mapping if the tetrahedralization
  // changes the surface vertices. In fact, we'll want to if we want to use
  // other tetrahedralization code. For now report an error if the
  // surface changes.
  for (int i=0; i<nv; ++i)
  {
    for (int j=0; j<3; ++j)
    {
      float diff = std::abs(v[i*3+j]-verts[i*3+j]);
      if (diff > 1e-10)
      {
        strcpy_error(iface, "TetGen error: change in surface vertices");
        return 0;
      }
    }
  }

  iface->idata->mesh = std::make_shared<admmpd::TetMesh>();
  bool success = iface->idata->mesh->create(
    iface->idata->options.get(),
    verts.data(),
    nv,
    faces.data(),
    nf,
    tets.data(),
    nt);

  if (!success) {
    strcpy_error(iface, "Error on mesh creation");
    return 0;
  }

  return 1;
}

#else

static inline int admmpd_init_with_tetgen(ADMMPDInterfaceData *iface, Object *ob, float (*vertexCos)[3])
{
  if (!iface) { return 0; }
  (void)(iface); (void)(ob); (void)(vertexCos);
  strcpy_error(iface, "TetGen not enabled");
  return 0;
}

#endif // WITH_TETGEN