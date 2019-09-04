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
 * Copyright 2019, Blender Foundation.
 *
 */

/** \file
 * \ingroup draw
 */

#include "BKE_customdata.h"
#include "BKE_object.h"

#include "BLI_linklist.h"
#include "BLI_listbase.h"
#include "BLI_math.h"

#include "DEG_depsgraph_query.h"

#include "DNA_camera_types.h"
#include "DNA_lanpr_types.h"
#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"

#include "DRW_engine.h"
#include "DRW_render.h"


#include "GPU_batch.h"
#include "GPU_draw.h"
#include "GPU_framebuffer.h"
#include "GPU_immediate.h"
#include "GPU_immediate_util.h"
#include "GPU_shader.h"
#include "GPU_uniformbuffer.h"
#include "GPU_viewport.h"

#include "lanpr_all.h"
#include "bmesh.h"

#include <math.h>

extern LANPR_SharedResource lanpr_share;
extern char datatoc_lanpr_dpix_project_passthrough_vert_glsl[];
extern char datatoc_lanpr_dpix_project_clip_frag_glsl[];
extern char datatoc_lanpr_dpix_preview_geom_glsl[];
extern char datatoc_lanpr_dpix_preview_frag_glsl[];

int lanpr_dpix_texture_size(SceneLANPR *lanpr)
{
  switch (lanpr->gpu_cache_size) {
    case LANPR_GPU_CACHE_SIZE_512:
      return 512;
    case LANPR_GPU_CACHE_SIZE_1K:
      return 1024;
    case LANPR_GPU_CACHE_SIZE_2K:
      return 2048;
    case LANPR_GPU_CACHE_SIZE_4K:
      return 4096;
  }
  return 512;
}

void lanpr_init_atlas_inputs(void *ved)
{
  lanpr_share.ved_viewport = ved;
  LANPR_Data *vedata = (LANPR_Data *)ved;
  LANPR_TextureList *txl = vedata->txl;
  LANPR_FramebufferList *fbl = vedata->fbl;

  const DRWContextState *draw_ctx = DRW_context_state_get();
  SceneLANPR *lanpr = &draw_ctx->scene->lanpr;

  int texture_size = lanpr_dpix_texture_size(lanpr);
  lanpr_share.texture_size = texture_size;

  if (txl->dpix_in_pl && GPU_texture_width(txl->dpix_in_pl) != texture_size) {
    DRW_texture_free(txl->dpix_in_pl);
    txl->dpix_in_pl = NULL;
    DRW_texture_free(txl->dpix_in_pr);
    txl->dpix_in_pr = NULL;
    DRW_texture_free(txl->dpix_in_nl);
    txl->dpix_in_nl = NULL;
    DRW_texture_free(txl->dpix_in_nr);
    txl->dpix_in_nr = NULL;
    DRW_texture_free(txl->dpix_out_pl);
    txl->dpix_out_pl = NULL;
    DRW_texture_free(txl->dpix_out_pr);
    txl->dpix_out_pr = NULL;
    DRW_texture_free(txl->dpix_out_length);
    txl->dpix_out_length = NULL;
  }

  if (lanpr_share.dpix_reloaded || !txl->dpix_in_pl) {
    DRW_texture_ensure_2d(&txl->dpix_in_pl, texture_size, texture_size, GPU_RGBA32F, 0);
    DRW_texture_ensure_2d(&txl->dpix_in_pr, texture_size, texture_size, GPU_RGBA32F, 0);
    DRW_texture_ensure_2d(&txl->dpix_in_nl, texture_size, texture_size, GPU_RGBA32F, 0);
    DRW_texture_ensure_2d(&txl->dpix_in_nr, texture_size, texture_size, GPU_RGBA32F, 0);
    DRW_texture_ensure_2d(&txl->dpix_in_edge_mask, texture_size, texture_size, GPU_RGBA8, 0);
    DRW_texture_ensure_2d(&txl->dpix_out_pl, texture_size, texture_size, GPU_RGBA32F, 0);
    DRW_texture_ensure_2d(&txl->dpix_out_pr, texture_size, texture_size, GPU_RGBA32F, 0);
    DRW_texture_ensure_2d(&txl->dpix_out_length, texture_size, texture_size, GPU_RGBA32F, 0);
  }

  GPU_framebuffer_ensure_config(&fbl->dpix_transform,
                                {GPU_ATTACHMENT_LEAVE,
                                 GPU_ATTACHMENT_TEXTURE(txl->dpix_out_pl),
                                 GPU_ATTACHMENT_TEXTURE(txl->dpix_out_pr),
                                 GPU_ATTACHMENT_TEXTURE(txl->dpix_out_length),
                                 GPU_ATTACHMENT_LEAVE,
                                 GPU_ATTACHMENT_LEAVE,
                                 GPU_ATTACHMENT_LEAVE});

  GPU_framebuffer_ensure_config(&fbl->dpix_preview,
                                {GPU_ATTACHMENT_TEXTURE(txl->depth),
                                 GPU_ATTACHMENT_TEXTURE(txl->color),
                                 GPU_ATTACHMENT_LEAVE,
                                 GPU_ATTACHMENT_LEAVE,
                                 GPU_ATTACHMENT_LEAVE,
                                 GPU_ATTACHMENT_LEAVE,
                                 GPU_ATTACHMENT_LEAVE});

  if (!lanpr_share.dpix_transform_shader) {
    lanpr_share.dpix_transform_shader = DRW_shader_create(
        datatoc_lanpr_dpix_project_passthrough_vert_glsl,
        NULL,
        datatoc_lanpr_dpix_project_clip_frag_glsl,
        NULL);
    if (!lanpr_share.dpix_transform_shader) {
      lanpr_share.dpix_shader_error = 1;
      printf("LANPR: DPIX transform shader compile error.");
    }
  }
  if (!lanpr_share.dpix_preview_shader) {
    lanpr_share.dpix_preview_shader = DRW_shader_create(
        datatoc_lanpr_dpix_project_passthrough_vert_glsl,
        datatoc_lanpr_dpix_preview_geom_glsl,
        datatoc_lanpr_dpix_preview_frag_glsl,
        NULL);
    if (!lanpr_share.dpix_transform_shader) {
      lanpr_share.dpix_shader_error = 1;
      printf("LANPR: DPIX transform shader compile error.");
    }
  }
}
void lanpr_destroy_atlas(void *UNUSED(ved))
{
  /*  no need to free things, no custom data. */
}

int lanpr_feed_atlas_data_obj(void *UNUSED(vedata),
                              float *AtlasPointsL,
                              float *AtlasPointsR,
                              float *AtlasFaceNormalL,
                              float *AtlasFaceNormalR,
                              float *AtlasEdgeMask,
                              Object *ob,
                              int begin_index)
{
  if (!DRW_object_is_renderable(ob)) {
    return begin_index;
  }
  const DRWContextState *draw_ctx = DRW_context_state_get();
  if (ob == draw_ctx->object_edit) {
    return begin_index;
  }
  if (ob->type != OB_MESH) {
    return begin_index;
  }

  Mesh *me = ob->data;
  BMesh *bm;
  struct BMFace *f1, *f2;
  struct BMVert *v1, *v2;
  struct BMEdge *e;
  struct BMLoop *l1, *l2;
  LanprEdge *fe;
  int CanFindFreestyle = 0;
  int edge_count = me->totedge;
  int i, idx;

  int cache_total = lanpr_share.texture_size * lanpr_share.texture_size;

  /* Don't overflow the cache. */
  if ((edge_count + begin_index) > (cache_total - 1)) {
    return begin_index;
  }

  const BMAllocTemplate allocsize = BMALLOC_TEMPLATE_FROM_ME(me);
  bm = BM_mesh_create(&allocsize,
                      &((struct BMeshCreateParams){
                          .use_toolflags = true,
                      }));
  BM_mesh_bm_from_me(bm,
                     me,
                     &((struct BMeshFromMeshParams){
                         .calc_face_normal = true,
                     }));
  BM_mesh_elem_table_ensure(bm, BM_VERT | BM_EDGE | BM_FACE);

  if (CustomData_has_layer(&bm->edata, CD_LANPR_EDGE)) {
    CanFindFreestyle = 1;
  }

  for (i = 0; i < edge_count; i++) {
    f1 = 0;
    f2 = 0;
    e = BM_edge_at_index(bm, i);
    v1 = e->v1;
    v2 = e->v2;
    l1 = e->l;
    l2 = e->l ? e->l->radial_next : 0;
    if (l1) {
      f1 = l1->f;
    }
    if (l2) {
      f2 = l2->f;
    }

    idx = (begin_index + i) * 4;

    AtlasPointsL[idx + 0] = v1->co[0];
    AtlasPointsL[idx + 1] = v1->co[1];
    AtlasPointsL[idx + 2] = v1->co[2];
    AtlasPointsL[idx + 3] = 1;

    AtlasPointsR[idx + 0] = v2->co[0];
    AtlasPointsR[idx + 1] = v2->co[1];
    AtlasPointsR[idx + 2] = v2->co[2];
    AtlasPointsR[idx + 3] = 1;

    if (CanFindFreestyle) {
      fe = CustomData_bmesh_get(&bm->edata, e->head.data, CD_LANPR_EDGE);
      if (fe->flag & LANPR_EDGE_MARK) {
        AtlasEdgeMask[idx + 1] = 1; /*  channel G */
      }
    }

    if (f1) {
      AtlasFaceNormalL[idx + 0] = f1->no[0];
      AtlasFaceNormalL[idx + 1] = f1->no[1];
      AtlasFaceNormalL[idx + 2] = f1->no[2];
      AtlasFaceNormalL[idx + 3] = 1;
    }
    else {
      AtlasFaceNormalL[idx + 0] = 0;
      AtlasFaceNormalL[idx + 1] = 0;
      AtlasFaceNormalL[idx + 2] = 0;
      AtlasFaceNormalL[idx + 3] = 0;
    }

    if (f2 && f2 != f1) { /*  this is for edge condition */
      AtlasFaceNormalR[idx + 0] = f2->no[0];
      AtlasFaceNormalR[idx + 1] = f2->no[1];
      AtlasFaceNormalR[idx + 2] = f2->no[2];
      AtlasFaceNormalR[idx + 3] = 1;

      if (f2->mat_nr != f1->mat_nr) {
        AtlasEdgeMask[idx] = 1; /*  channel r */
      }
    }
    else {
      AtlasFaceNormalR[idx + 0] = 0;
      AtlasFaceNormalR[idx + 1] = 0;
      AtlasFaceNormalR[idx + 2] = 0;
      AtlasFaceNormalR[idx + 3] = 0;
    }
  }

  BM_mesh_free(bm);

  return begin_index + edge_count;
}

int lanpr_feed_atlas_data_intersection_cache(void *UNUSED(vedata),
                                             float *AtlasPointsL,
                                             float *AtlasPointsR,
                                             float *AtlasFaceNormalL,
                                             float *AtlasFaceNormalR,
                                             float *AtlasEdgeMask,
                                             int begin_index)
{
  LANPR_RenderBuffer *rb = lanpr_share.render_buffer_shared;
  LinkData *lip;
  LANPR_RenderLine *rl;
  int i, idx;

  i = 0;

  if (!rb) {
    return 0;
  }

  int cache_total = lanpr_share.texture_size * lanpr_share.texture_size;

  /* Don't overflow the cache. */
  if ((rb->intersection_count + begin_index) > (cache_total - 1)) {
    return 0;
  }

  for (lip = rb->intersection_lines.first; lip; lip = lip->next) {
    rl = lip->data;

    idx = (begin_index + i) * 4;
    AtlasEdgeMask[idx + 2] = 1; /*  channel B */

    AtlasPointsL[idx + 0] = rl->l->gloc[0];
    AtlasPointsL[idx + 1] = rl->l->gloc[1];
    AtlasPointsL[idx + 2] = rl->l->gloc[2];
    AtlasPointsL[idx + 3] = 1;

    AtlasPointsR[idx + 0] = rl->r->gloc[0];
    AtlasPointsR[idx + 1] = rl->r->gloc[1];
    AtlasPointsR[idx + 2] = rl->r->gloc[2];
    AtlasPointsR[idx + 3] = 1;

    AtlasFaceNormalL[idx + 0] = 0;
    AtlasFaceNormalL[idx + 1] = 0;
    AtlasFaceNormalL[idx + 2] = 1;
    AtlasFaceNormalL[idx + 3] = 0;

    AtlasFaceNormalR[idx + 0] = 0;
    AtlasFaceNormalR[idx + 1] = 0;
    AtlasFaceNormalR[idx + 2] = 1;
    AtlasFaceNormalR[idx + 3] = 0;

    i++;
  }

  return begin_index + i;
}

static void lanpr_dpix_index_to_coord(int index, float *x, float *y)
{
  int texture_size = lanpr_share.texture_size;
  (*x) = interpf(1, -1, (float)(index % texture_size + 0.5) / (float)texture_size);
  (*y) = interpf(1, -1, (float)(index / texture_size + 0.5) / (float)texture_size);
}

static void lanpr_dpix_index_to_coord_absolute(int index, float *x, float *y)
{
  int texture_size = lanpr_share.texture_size;
  (*x) = (float)(index % texture_size) + 0.5;
  (*y) = (float)(index / texture_size) + 0.5;
}

int lanpr_feed_atlas_trigger_preview_obj(void *UNUSED(vedata), Object *ob, int begin_index)
{
  Mesh *me = ob->data;
  if (ob->type != OB_MESH) {
    return begin_index;
  }
  int edge_count = me->totedge;
  int i;
  float co[2];

  int cache_total = lanpr_share.texture_size * lanpr_share.texture_size;

  /* Don't overflow the cache. */
  if ((edge_count + begin_index) > (cache_total - 1)) {
    return begin_index;
  }

  static GPUVertFormat format = {0};
  static struct {
    uint pos, uvs;
  } attr_id;
  if (format.attr_len == 0) {
    attr_id.pos = GPU_vertformat_attr_add(&format, "pos", GPU_COMP_F32, 2, GPU_FETCH_FLOAT);
  }

  static GPUVertFormat format2 = {0};
  static struct {
    uint pos, uvs;
  } attr_id2;
  if (format2.attr_len == 0) {
    attr_id2.pos = GPU_vertformat_attr_add(&format2, "pos", GPU_COMP_F32, 2, GPU_FETCH_FLOAT);
  }

  GPUVertBuf *vbo = GPU_vertbuf_create_with_format(&format);
  GPUVertBuf *vbo2 = GPU_vertbuf_create_with_format(&format2);
  GPU_vertbuf_data_alloc(vbo, edge_count);
  GPU_vertbuf_data_alloc(vbo2, edge_count);

  for (i = 0; i < edge_count; i++) {
    lanpr_dpix_index_to_coord(i + begin_index, &co[0], &co[1]);
    GPU_vertbuf_attr_set(vbo, attr_id.pos, i, co);
    lanpr_dpix_index_to_coord_absolute(i + begin_index, &co[0], &co[1]);
    GPU_vertbuf_attr_set(vbo2, attr_id2.pos, i, co);
  }

  GPUBatch *gb = GPU_batch_create_ex(
      GPU_PRIM_POINTS, vbo, 0, GPU_USAGE_DYNAMIC | GPU_BATCH_OWNS_VBO);
  GPUBatch *gb2 = GPU_batch_create_ex(
      GPU_PRIM_POINTS, vbo2, 0, GPU_USAGE_DYNAMIC | GPU_BATCH_OWNS_VBO);

  LANPR_BatchItem *bi = BLI_mempool_alloc(lanpr_share.mp_batch_list);
  BLI_addtail(&lanpr_share.dpix_batch_list, bi);
  bi->dpix_transform_batch = gb;
  bi->dpix_preview_batch = gb2;
  bi->ob = ob;

  return begin_index + edge_count;
}

void lanpr_create_atlas_intersection_preview(void *UNUSED(vedata), int begin_index)
{
  LANPR_RenderBuffer *rb = lanpr_share.render_buffer_shared;
  float co[2];
  int i;

  if (!rb) {
    return;
  }

  if (rb->DPIXIntersectionBatch) {
    GPU_BATCH_DISCARD_SAFE(rb->DPIXIntersectionBatch);
  }
  rb->DPIXIntersectionBatch = 0;

  if (!rb->intersection_count) {
    return;
  }

  int cache_total = lanpr_share.texture_size * lanpr_share.texture_size;

  /* Don't overflow the cache. */
  if ((rb->intersection_count + begin_index) > (cache_total - 1)) {
    return;
  }

  static GPUVertFormat format = {0};
  static struct {
    uint pos, uvs;
  } attr_id;
  if (format.attr_len == 0) {
    attr_id.pos = GPU_vertformat_attr_add(&format, "pos", GPU_COMP_F32, 2, GPU_FETCH_FLOAT);
  }
  static GPUVertFormat format2 = {0};
  static struct {
    uint pos, uvs;
  } attr_id2;
  if (format2.attr_len == 0) {
    attr_id2.pos = GPU_vertformat_attr_add(&format2, "pos", GPU_COMP_F32, 2, GPU_FETCH_FLOAT);
  }

  GPUVertBuf *vbo = GPU_vertbuf_create_with_format(&format);
  GPU_vertbuf_data_alloc(vbo, rb->intersection_count);

  GPUVertBuf *vbo2 = GPU_vertbuf_create_with_format(&format2);
  GPU_vertbuf_data_alloc(vbo2, rb->intersection_count);

  for (i = 0; i < rb->intersection_count; i++) {
    lanpr_dpix_index_to_coord(i + begin_index, &co[0], &co[1]);
    GPU_vertbuf_attr_set(vbo, attr_id.pos, i, co);
    lanpr_dpix_index_to_coord_absolute(i + begin_index, &co[0], &co[1]);
    GPU_vertbuf_attr_set(vbo2, attr_id2.pos, i, co);
  }
  rb->DPIXIntersectionTransformBatch = GPU_batch_create_ex(
      GPU_PRIM_POINTS, vbo, 0, GPU_USAGE_DYNAMIC | GPU_BATCH_OWNS_VBO);
  rb->DPIXIntersectionBatch = GPU_batch_create_ex(
      GPU_PRIM_POINTS, vbo2, 0, GPU_USAGE_DYNAMIC | GPU_BATCH_OWNS_VBO);
}

void lanpr_dpix_draw_scene(LANPR_TextureList *txl,
                           LANPR_FramebufferList *fbl,
                           LANPR_PassList *psl,
                           LANPR_PrivateData *pd,
                           SceneLANPR *lanpr,
                           GPUFrameBuffer *DefaultFB,
                           int is_render)
{
  float clear_col[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  float clear_depth = 1.0f;
  uint clear_stencil = 0xFF;
  int is_persp = 1;
  float use_background_color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  ;

  if (!lanpr->active_layer) {
    return; /* return early in case we don't have line layers. DPIX only use the first layer. */
  }
  int texw = GPU_texture_width(txl->edge_intermediate),
      texh = GPU_texture_height(txl->edge_intermediate);

  const DRWContextState *draw_ctx = DRW_context_state_get();
  Scene *scene = DEG_get_evaluated_scene(draw_ctx->depsgraph);
  View3D *v3d = draw_ctx->v3d;
  Object *camera = 0;
  if (v3d) {
    RegionView3D *rv3d = draw_ctx->rv3d;
    camera = (rv3d && rv3d->persp == RV3D_CAMOB) ? v3d->camera : NULL;
    is_persp = rv3d->is_persp;
  }
  if (!camera) {
    camera = scene->camera;
    if (!v3d) {
      is_persp = ((Camera *)camera->data)->type == CAM_PERSP ? 1 : 0;
    }
  }
  if (is_render && !camera) {
    return;
  }
  /*  XXX: should implement view angle functions for ortho camera. */

  int texture_size = lanpr_share.texture_size;

  pd->dpix_viewport[2] = texw;
  pd->dpix_viewport[3] = texh;
  pd->dpix_is_perspective = is_persp;
  pd->dpix_sample_step = 1;
  pd->dpix_buffer_width = texture_size;
  pd->dpix_depth_offset = 0.0001;
  pd->dpix_znear = camera ? ((Camera *)camera->data)->clip_start : v3d->clip_start;
  pd->dpix_zfar = camera ? ((Camera *)camera->data)->clip_end : v3d->clip_end;

  copy_v3_v3(use_background_color, &scene->world->horr);
  use_background_color[3] = scene->r.alphamode ? 0.0f : 1.0f;

  GPU_point_size(1);
  /*  GPU_line_width(2); */
  GPU_framebuffer_bind(fbl->dpix_transform);
  DRW_draw_pass(psl->dpix_transform_pass);

  GPU_framebuffer_bind(fbl->dpix_preview);
  eGPUFrameBufferBits clear_bits = GPU_COLOR_BIT;
  GPU_framebuffer_clear(fbl->dpix_preview, clear_bits, clear_col, clear_depth, clear_stencil);
  DRW_draw_pass(psl->dpix_preview_pass);

  if (is_render) {
    mul_v3_v3fl(clear_col, use_background_color, use_background_color[3]);
    clear_col[3] = use_background_color[3];
  }
  else {
    copy_v4_v4(clear_col, use_background_color);
  }

  GPU_framebuffer_bind(DefaultFB);
  GPU_framebuffer_clear(DefaultFB, clear_bits, clear_col, clear_depth, clear_stencil);
  DRW_multisamples_resolve(txl->depth, txl->color, 0);
}
