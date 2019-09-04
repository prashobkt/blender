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

#ifndef __LANPR_ALL_H__
#define __LANPR_ALL_H__

#include "ED_lanpr.h"
#include "BLI_mempool.h"
#include "BLI_utildefines.h"
/* #include "GPU_framebuffer.h" */
#include "GPU_batch.h"
#include "GPU_framebuffer.h"
#include "GPU_shader.h"
#include "GPU_uniformbuffer.h"
#include "GPU_viewport.h"
#include "DNA_listBase.h"
#include "DRW_render.h"
#include "BKE_object.h"
#include "DNA_mesh_types.h"
#include "DNA_camera_types.h"
#include "GPU_immediate.h"
#include "GPU_immediate_util.h"
#include "GPU_framebuffer.h"
#include "DNA_lanpr_types.h"
#include "DNA_meshdata_types.h"
#include "BKE_customdata.h"
#include "DEG_depsgraph_query.h"
#include "GPU_draw.h"
#include "GPU_texture.h"

#include "BLI_threads.h"

#include "bmesh.h"

#include "WM_types.h"
#include "WM_api.h"

#define LANPR_ENGINE "BLENDER_LANPR"

#define deg(r) r / M_PI * 180.0
#define rad(d) d *M_PI / 180.0

#define tMatDist2v(p1, p2) \
  sqrt(((p1)[0] - (p2)[0]) * ((p1)[0] - (p2)[0]) + ((p1)[1] - (p2)[1]) * ((p1)[1] - (p2)[1]))

extern struct RenderEngineType DRW_engine_viewport_lanpr_type;
extern struct DrawEngineType draw_engine_lanpr_type;

typedef struct LANPR_RenderBuffer LANPR_RenderBuffer;

typedef struct LANPR_PassList {
  /* Image filtering */
  struct DRWPass *depth_pass;
  struct DRWPass *color_pass;
  struct DRWPass *normal_pass;
  struct DRWPass *edge_intermediate;
  struct DRWPass *edge_thinning;
  struct DRWPass *snake_pass;

  /* GPU */
  struct DRWPass *dpix_transform_pass;
  struct DRWPass *dpix_preview_pass;

  /* SOFTWARE */
  struct DRWPass *software_pass;

} LANPR_PassList;

typedef struct LANPR_FramebufferList {

  /* CPU */
  struct GPUFrameBuffer *passes;
  struct GPUFrameBuffer *edge_intermediate;
  struct GPUFrameBuffer *edge_thinning;

  /* GPU */
  struct GPUFrameBuffer *dpix_transform;
  struct GPUFrameBuffer *dpix_preview;

  /* Image filtering */
  struct GPUFrameBuffer *software_ms;

} LANPR_FramebufferList;

typedef struct LANPR_TextureList {

  struct GPUTexture *color;
  struct GPUTexture *normal;
  struct GPUTexture *depth;
  struct GPUTexture *edge_intermediate;

  struct GPUTexture *dpix_in_pl;
  struct GPUTexture *dpix_in_pr;
  struct GPUTexture *dpix_in_nl;
  struct GPUTexture *dpix_in_nr;

  /** RGBA texture format,
   * R:Material, G: Freestyle Edge Mark,
   * BA:Reserved for future usages */
  struct GPUTexture *dpix_in_edge_mask;

  struct GPUTexture *dpix_out_pl;
  struct GPUTexture *dpix_out_pr;
  struct GPUTexture *dpix_out_length;

  /** Multisample resolve */
  struct GPUTexture *ms_resolve_depth;
  struct GPUTexture *ms_resolve_color;

} LANPR_TextureList;

typedef struct LANPR_PrivateData {
  DRWShadingGroup *multipass_shgrp;
  DRWShadingGroup *edge_detect_shgrp;
  DRWShadingGroup *edge_thinning_shgrp;
  DRWShadingGroup *snake_shgrp;

  DRWShadingGroup *dpix_transform_shgrp;
  DRWShadingGroup *dpix_preview_shgrp;

  DRWShadingGroup *debug_shgrp;

  /* Image filtering */

  float normal_clamp;
  float normal_strength;
  float depth_clamp;
  float depth_strength;

  float zfar;
  float znear;

  /** Thinning stage */
  int stage;

  float *line_result;
  unsigned char *line_result_8bit;

  /** If not match then recreate buffer. */
  int width, height;
  void **sample_table;

  ListBase pending_samples;
  ListBase erased_samples;
  ListBase line_strips;

  /* dpix data */

  void *atlas_pl;
  void *atlas_pr;
  void *atlas_nl;
  void *atlas_nr;
  void *atlas_edge_mask;

  int begin_index;

  int dpix_sample_step;
  int dpix_is_perspective;
  float dpix_viewport[4];
  float output_viewport[4];
  int dpix_buffer_width;
  float dpix_depth_offset;

  float dpix_znear;
  float dpix_zfar;

  /* drawing */

  unsigned v_buf;
  unsigned i_buf;
  unsigned l_buf;

  GPUVertFormat snake_gwn_format;
  GPUBatch *snake_batch;
} LANPR_PrivateData;

typedef struct LANPR_StorageList {
  LANPR_PrivateData *g_data;
} LANPR_StorageList;

typedef struct LANPR_BatchItem {
  Link item;
  GPUBatch *dpix_transform_batch;
  GPUBatch *dpix_preview_batch;
  Object *ob;
} LANPR_BatchItem;

typedef struct LANPR_Data {
  void *engine_type;
  LANPR_FramebufferList *fbl;
  LANPR_TextureList *txl;
  LANPR_PassList *psl;
  LANPR_StorageList *stl;
} LANPR_Data;

/*  functions */

void lanpr_init_atlas_inputs(void *ved);
void lanpr_destroy_atlas(void *ved);
int lanpr_feed_atlas_data_obj(void *vedata,
                              float *AtlasPointsL,
                              float *AtlasPointsR,
                              float *AtlasFaceNormalL,
                              float *AtlasFaceNormalR,
                              float *AtlasEdgeMask,
                              Object *ob,
                              int begin_index);

int lanpr_feed_atlas_data_intersection_cache(void *vedata,
                                             float *AtlasPointsL,
                                             float *AtlasPointsR,
                                             float *AtlasFaceNormalL,
                                             float *AtlasFaceNormalR,
                                             float *AtlasEdgeMask,
                                             int begin_index);

int lanpr_feed_atlas_trigger_preview_obj(void *vedata, Object *ob, int begin_index);
void lanpr_create_atlas_intersection_preview(void *vedata, int begin_index);

void lanpr_dpix_draw_scene(LANPR_TextureList *txl,
                           LANPR_FramebufferList *fbl,
                           LANPR_PassList *psl,
                           LANPR_PrivateData *pd,
                           SceneLANPR *lanpr,
                           GPUFrameBuffer *DefaultFB,
                           int is_render);

void lanpr_snake_draw_scene(LANPR_TextureList *txl,
                            LANPR_FramebufferList *fbl,
                            LANPR_PassList *psl,
                            LANPR_PrivateData *pd,
                            SceneLANPR *lanpr,
                            GPUFrameBuffer *DefaultFB,
                            int is_render);

void lanpr_software_draw_scene(void *vedata, GPUFrameBuffer *dfb, int is_render);

int lanpr_dpix_texture_size(SceneLANPR *lanpr);

void lanpr_chain_generate_draw_command(struct LANPR_RenderBuffer *rb);

#endif
