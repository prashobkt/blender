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
 * Copyright 2020, Blender Foundation.
 */

/** \file
 * \ingroup draw_engine
 */

#include "DNA_space_types.h"

typedef struct OVERLAY2D_PassList {
  DRWPass *background;
  DRWPass *image_tiling_borders;
  DRWPass *uv_faces;
  DRWPass *uv_verts;
  DRWPass *uv_stretching;
  DRWPass *uv_edges;
  DRWPass *uv_shadow_edges;
} OVERLAY2D_PassList;

typedef enum OVERLAY2D_LineStyle {
  OVERLAY2D_LINE_STYLE_OUTLINE = 0,
  OVERLAY2D_LINE_STYLE_DASH = 1,
  OVERLAY2D_LINE_STYLE_BLACK = 2,
  OVERLAY2D_LINE_STYLE_WHITE = 3,
  OVERLAY2D_LINE_STYLE_SHADOW = 4,
} OVERLAY2D_LineStyle;

typedef struct OVERLAY2D_UvStretchingAreaTotals {
  void *next, *prev;
  float *total_area;
  float *total_area_uv;
} OVERLAY2D_UvStretchingAreaTotals;

typedef struct OVERLAY2D_PrivateData {
  float unit_mat[4][4];

  bool do_uv_overlay;
  bool do_uv_shadow_overlay;
  bool do_uv_stretching_overlay;
  bool do_image_tiling_overlay;

  float uv_opacity;

  /* uv overlay */
  DRWShadingGroup *uv_edges_grp;
  DRWShadingGroup *uv_faces_grp;
  DRWShadingGroup *uv_stretching_grp;
  DRWShadingGroup *uv_face_dots_grp;
  DRWShadingGroup *uv_verts_grp;

  /* uv shadow overlay */
  DRWShadingGroup *uv_shadow_edges_grp;

  struct {
    bool do_transparency_checkerboard;
  } background;
  struct {
    bool do_faces;
    bool do_face_dots;
  } uv;
  struct {
    eSpaceImage_UVDT_Stretch draw_type;
    ListBase totals;
    float total_area_ratio;
    float total_area_ratio_inv;
  } uv_stretching;
  struct {
    OVERLAY2D_LineStyle line_style;
    float dash_length;
    int do_smooth_wire;
  } wireframe;

} OVERLAY2D_PrivateData;

typedef struct OVERLAY2D_StorageList {
  struct OVERLAY2D_PrivateData *pd;
} OVERLAY2D_StorageList;

typedef struct OVERLAY2D_Data {
  void *engine_type;
  DRWViewportEmptyList *fbl;
  DRWViewportEmptyList *txl;
  OVERLAY2D_PassList *psl;
  OVERLAY2D_StorageList *stl;
} OVERLAY2D_Data;

/* overlay2d_background.c */
void OVERLAY2D_background_engine_init(OVERLAY2D_Data *vedata);
void OVERLAY2D_background_cache_init(OVERLAY2D_Data *vedata);
void OVERLAY2D_background_draw_scene(OVERLAY2D_Data *vedata);

/* overlay2d_uv.c */
void OVERLAY2D_uv_engine_init(OVERLAY2D_Data *vedata);
void OVERLAY2D_uv_cache_init(OVERLAY2D_Data *vedata);
void OVERLAY2D_uv_cache_populate(OVERLAY2D_Data *vedata, Object *ob);
void OVERLAY2D_uv_draw_scene_faces(OVERLAY2D_Data *vedata);
void OVERLAY2D_uv_draw_scene_edges_and_verts(OVERLAY2D_Data *vedata);

/* overlay2d_uv_shadow.c */
void OVERLAY2D_uv_shadow_cache_init(OVERLAY2D_Data *vedata);
void OVERLAY2D_uv_shadow_cache_populate(OVERLAY2D_Data *vedata, Object *ob);
void OVERLAY2D_uv_shadow_draw_scene(OVERLAY2D_Data *vedata);

/* overlay2d_uv_stretching.c */
void OVERLAY2D_uv_stretching_engine_init(OVERLAY2D_Data *vedata);
void OVERLAY2D_uv_stretching_cache_init(OVERLAY2D_Data *vedata);
void OVERLAY2D_uv_stretching_cache_populate(OVERLAY2D_Data *vedata, Object *ob);
void OVERLAY2D_uv_stretching_draw_scene_faces(OVERLAY2D_Data *vedata);

/* overlay2d_shaders.c */
GPUShader *OVERLAY2D_shaders_uv_face_get(void);
GPUShader *OVERLAY2D_shaders_uv_face_dots_get(void);
GPUShader *OVERLAY2D_shaders_uv_verts_get(void);
GPUShader *OVERLAY2D_shaders_uv_stretching_area_get(void);
GPUShader *OVERLAY2D_shaders_uv_stretching_angle_get(void);
GPUShader *OVERLAY2D_shaders_background_get(void);
GPUShader *OVERLAY2D_shaders_image_tiling_border_get(void);
GPUShader *OVERLAY2D_shaders_wireframe_resolve_get(void);
GPUShader *OVERLAY2D_shaders_wireframe_get(void);
void OVERLAY2D_shader_library_ensure(void);
void OVERLAY2D_shaders_free(void);

/* overlay2d_image_tiling.c */
void OVERLAY2D_image_tiling_cache_init(OVERLAY2D_Data *vedata);
void OVERLAY2D_image_tiling_draw_scene(OVERLAY2D_Data *vedata);
