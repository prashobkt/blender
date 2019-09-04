#include "DRW_engine.h"
#include "DRW_render.h"
#include "BLI_listbase.h"
#include "BLI_linklist.h"
#include "BLI_math_matrix.h"
#include "BLI_task.h"
#include "BLI_utildefines.h"
#include "lanpr_all.h"
#include "ED_lanpr.h"
#include "DRW_render.h"
#include "BKE_object.h"
#include "DNA_mesh_types.h"
#include "DNA_camera_types.h"
#include "DNA_modifier_types.h"
#include "DNA_text_types.h"
#include "GPU_immediate.h"
#include "GPU_immediate_util.h"
#include "GPU_framebuffer.h"
#include "DNA_lanpr_types.h"
#include "DNA_meshdata_types.h"
#include "BKE_customdata.h"
#include "DEG_depsgraph_query.h"
#include "BKE_camera.h"
#include "BKE_collection.h"
#include "BKE_report.h"
#include "BKE_screen.h"
#include "BKE_text.h"
#include "GPU_draw.h"

#include "GPU_batch.h"
#include "GPU_framebuffer.h"
#include "GPU_shader.h"
#include "GPU_uniformbuffer.h"
#include "GPU_viewport.h"
#include "bmesh.h"
#include "bmesh_class.h"
#include "bmesh_tools.h"

#include "WM_types.h"
#include "WM_api.h"

#include <math.h>
#include "RNA_access.h"
#include "RNA_define.h"

#include "BKE_modifier.h"
#include "BKE_gpencil.h"
#include "BKE_gpencil_modifier.h"

extern LANPR_SharedResource lanpr_share;
extern const char *RE_engine_id_BLENDER_LANPR;

static void lanpr_rebuild_render_draw_command(LANPR_RenderBuffer *rb, LANPR_LineLayer *ll)
{
  int Count = 0;
  float *v, *tv, *N, *tn;
  int i;
  int vertCount;

  static GPUVertFormat format = {0};
  static struct {
    uint pos, normal;
  } attr_id;
  if (format.attr_len == 0) {
    attr_id.pos = GPU_vertformat_attr_add(&format, "pos", GPU_COMP_F32, 3, GPU_FETCH_FLOAT);
    attr_id.normal = GPU_vertformat_attr_add(&format, "normal", GPU_COMP_F32, 3, GPU_FETCH_FLOAT);
  }

  GPUVertBuf *vbo = GPU_vertbuf_create_with_format(&format);

  if (ll->contour.enabled) {
    Count += lanpr_count_leveled_edge_segment_count(&rb->contours, ll);
  }
  if (ll->crease.enabled) {
    Count += lanpr_count_leveled_edge_segment_count(&rb->crease_lines, ll);
  }
  if (ll->intersection.enabled) {
    Count += lanpr_count_leveled_edge_segment_count(&rb->intersection_lines, ll);
  }
  if (ll->edge_mark.enabled) {
    Count += lanpr_count_leveled_edge_segment_count(&rb->edge_marks, ll);
  }
  if (ll->material_separate.enabled) {
    Count += lanpr_count_leveled_edge_segment_count(&rb->material_lines, ll);
  }

  vertCount = Count * 2;

  if (!vertCount) {
    return;
  }

  GPU_vertbuf_data_alloc(vbo, vertCount);

  tv = v = MEM_callocN(sizeof(float) * 6 * Count, "temp v data");
  tn = N = MEM_callocN(sizeof(float) * 6 * Count, "temp n data");

  if (ll->contour.enabled) {
    tv = lanpr_make_leveled_edge_vertex_array(rb, &rb->contours, tv, tn, &tn, ll, 1.0f);
  }
  if (ll->crease.enabled) {
    tv = lanpr_make_leveled_edge_vertex_array(rb, &rb->crease_lines, tv, tn, &tn, ll, 2.0f);
  }
  if (ll->material_separate.enabled) {
    tv = lanpr_make_leveled_edge_vertex_array(rb, &rb->material_lines, tv, tn, &tn, ll, 3.0f);
  }
  if (ll->edge_mark.enabled) {
    tv = lanpr_make_leveled_edge_vertex_array(rb, &rb->edge_marks, tv, tn, &tn, ll, 4.0f);
  }
  if (ll->intersection.enabled) {
    tv = lanpr_make_leveled_edge_vertex_array(rb, &rb->intersection_lines, tv, tn, &tn, ll, 5.0f);
  }

  for (i = 0; i < vertCount; i++) {
    GPU_vertbuf_attr_set(vbo, attr_id.pos, i, &v[i * 3]);
    GPU_vertbuf_attr_set(vbo, attr_id.normal, i, &N[i * 3]);
  }

  MEM_freeN(v);
  MEM_freeN(N);

  ll->batch = GPU_batch_create_ex(GPU_PRIM_LINES, vbo, 0, GPU_USAGE_DYNAMIC | GPU_BATCH_OWNS_VBO);
}
void ED_lanpr_rebuild_all_command(SceneLANPR *lanpr)
{
  LANPR_LineLayer *ll;
  if (!lanpr || !lanpr_share.render_buffer_shared) {
    return;
  }

  if (lanpr->flags & LANPR_USE_CHAINING) {
    lanpr_chain_generate_draw_command(lanpr_share.render_buffer_shared);
  }
  else {
    for (ll = lanpr->line_layers.first; ll; ll = ll->next) {
      if (ll->batch) {
        GPU_batch_discard(ll->batch);
      }
      lanpr_rebuild_render_draw_command(lanpr_share.render_buffer_shared, ll);
    }
  }

  DEG_id_tag_update(&lanpr_share.render_buffer_shared->scene->id, ID_RECALC_COPY_ON_WRITE);
}

void ED_lanpr_calculate_normal_object_vector(LANPR_LineLayer *ll, float *normal_object_direction)
{
  Object *ob;
  if (!(ll->flags & LANPR_LINE_LAYER_NORMAL_ENABLED)) {
    return;
  }
  switch (ll->normal_mode) {
    case LANPR_NORMAL_DIRECTIONAL:
      if (!(ob = ll->normal_control_object)) {
        normal_object_direction[0] = 0;
        normal_object_direction[1] = 0;
        normal_object_direction[2] = 1; /*  default z up direction */
      }
      else {
        float dir[3] = {0, 0, 1};
        float mat[3][3];
        copy_m3_m4(mat, ob->obmat);
        mul_v3_m3v3(normal_object_direction, mat, dir);
        normalize_v3(normal_object_direction);
      }
      return;
    case LANPR_NORMAL_POINT:
      if (!(ob = ll->normal_control_object)) {
        normal_object_direction[0] = 0;
        normal_object_direction[1] = 0;
        normal_object_direction[2] = 0; /*  default origin position */
      }
      else {
        normal_object_direction[0] = ob->obmat[3][0];
        normal_object_direction[1] = ob->obmat[3][1];
        normal_object_direction[2] = ob->obmat[3][2];
      }
      return;
  }
}

void lanpr_software_draw_scene(void *vedata, GPUFrameBuffer *dfb, int is_render)
{
  LANPR_LineLayer *ll;
  LANPR_PassList *psl = ((LANPR_Data *)vedata)->psl;
  LANPR_TextureList *txl = ((LANPR_Data *)vedata)->txl;
  LANPR_StorageList *stl = ((LANPR_Data *)vedata)->stl;
  LANPR_FramebufferList *fbl = ((LANPR_Data *)vedata)->fbl;
  LANPR_PrivateData *pd = stl->g_data;
  const DRWContextState *draw_ctx = DRW_context_state_get();
  Scene *scene = DEG_get_evaluated_scene(draw_ctx->depsgraph);
  SceneLANPR *lanpr = &scene->lanpr;
  View3D *v3d = draw_ctx->v3d;
  float indentity_mat[4][4];
  static float normal_object_direction[3] = {0, 0, 1};
  float use_background_color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  static float camdx, camdy, camzoom;

  if (is_render) {
    ED_lanpr_rebuild_all_command(lanpr);
  }

  float clear_depth = 1.0f;
  uint clear_stencil = 0xFF;
  eGPUFrameBufferBits clear_bits = GPU_DEPTH_BIT | GPU_COLOR_BIT;
  static int zero_value = 0;

  copy_v3_v3(use_background_color, &scene->world->horr);
  use_background_color[3] = scene->r.alphamode ? 0.0f : 1.0f;

  GPU_framebuffer_bind(fbl->software_ms);
  GPU_framebuffer_clear(
      fbl->software_ms, clear_bits, use_background_color, clear_depth, clear_stencil);

  if (lanpr_share.render_buffer_shared) {

    int texw = GPU_texture_width(txl->ms_resolve_color),
        texh = GPU_texture_height(txl->ms_resolve_color);
    ;
    pd->output_viewport[2] = scene->r.xsch;
    pd->output_viewport[3] = scene->r.ysch;
    pd->dpix_viewport[2] = texw;
    pd->dpix_viewport[3] = texh;

    unit_m4(indentity_mat);

    DRWView *view = DRW_view_create(indentity_mat, indentity_mat, NULL, NULL, NULL);
    if (is_render) {
      DRW_view_default_set(view);
    }
    else {
      DRW_view_set_active(view);
    }

    RegionView3D *rv3d = v3d ? draw_ctx->rv3d : NULL;
    if (rv3d) {
      camdx = rv3d->camdx;
      camdy = rv3d->camdy;
      camzoom = BKE_screen_view3d_zoom_to_fac(rv3d->camzoom);
    }
    else {
      camdx = camdy = 0.0f;
      camzoom = 1.0f;
    }

    if ((lanpr->flags & LANPR_USE_CHAINING) &&
        lanpr_share.render_buffer_shared->chain_draw_batch) {
      for (ll = lanpr->line_layers.last; ll; ll = ll->prev) {
        LANPR_RenderBuffer *rb;
        psl->software_pass = DRW_pass_create("Software Render Preview",
                                             DRW_STATE_WRITE_COLOR | DRW_STATE_WRITE_DEPTH |
                                                 DRW_STATE_DEPTH_LESS_EQUAL);
        rb = lanpr_share.render_buffer_shared;
        rb->ChainShgrp = DRW_shgroup_create(lanpr_share.software_chaining_shader,
                                            psl->software_pass);

        ED_lanpr_calculate_normal_object_vector(ll, normal_object_direction);

        DRW_shgroup_uniform_float(rb->ChainShgrp, "camdx", &camdx, 1);
        DRW_shgroup_uniform_float(rb->ChainShgrp, "camdy", &camdy, 1);
        DRW_shgroup_uniform_float(rb->ChainShgrp, "camzoom", &camzoom, 1);

        DRW_shgroup_uniform_vec4(rb->ChainShgrp,
                                 "contour_color",
                                 (ll->flags & LANPR_LINE_LAYER_USE_SAME_STYLE) ? ll->color :
                                                                                 ll->contour.color,
                                 1);
        DRW_shgroup_uniform_vec4(rb->ChainShgrp,
                                 "crease_color",
                                 (ll->flags & LANPR_LINE_LAYER_USE_SAME_STYLE) ? ll->color :
                                                                                 ll->crease.color,
                                 1);
        DRW_shgroup_uniform_vec4(rb->ChainShgrp,
                                 "material_color",
                                 (ll->flags & LANPR_LINE_LAYER_USE_SAME_STYLE) ?
                                     ll->color :
                                     ll->material_separate.color,
                                 1);
        DRW_shgroup_uniform_vec4(
            rb->ChainShgrp,
            "edge_mark_color",
            (ll->flags & LANPR_LINE_LAYER_USE_SAME_STYLE) ? ll->color : ll->edge_mark.color,
            1);
        DRW_shgroup_uniform_vec4(
            rb->ChainShgrp,
            "intersection_color",
            (ll->flags & LANPR_LINE_LAYER_USE_SAME_STYLE) ? ll->color : ll->intersection.color,
            1);
        static float unit_thickness = 1.0f;
        DRW_shgroup_uniform_float(rb->ChainShgrp, "thickness", &ll->thickness, 1);
        DRW_shgroup_uniform_float(rb->ChainShgrp,
                                  "thickness_contour",
                                  (ll->flags & LANPR_LINE_LAYER_USE_SAME_STYLE) ?
                                      &unit_thickness :
                                      &ll->contour.thickness,
                                  1);
        DRW_shgroup_uniform_float(rb->ChainShgrp,
                                  "thickness_crease",
                                  (ll->flags & LANPR_LINE_LAYER_USE_SAME_STYLE) ?
                                      &unit_thickness :
                                      &ll->crease.thickness,
                                  1);
        DRW_shgroup_uniform_float(rb->ChainShgrp,
                                  "thickness_material",
                                  (ll->flags & LANPR_LINE_LAYER_USE_SAME_STYLE) ?
                                      &unit_thickness :
                                      &ll->material_separate.thickness,
                                  1);
        DRW_shgroup_uniform_float(rb->ChainShgrp,
                                  "thickness_edge_mark",
                                  (ll->flags & LANPR_LINE_LAYER_USE_SAME_STYLE) ?
                                      &unit_thickness :
                                      &ll->edge_mark.thickness,
                                  1);
        DRW_shgroup_uniform_float(rb->ChainShgrp,
                                  "thickness_intersection",
                                  (ll->flags & LANPR_LINE_LAYER_USE_SAME_STYLE) ?
                                      &unit_thickness :
                                      &ll->intersection.thickness,
                                  1);
        DRW_shgroup_uniform_int(rb->ChainShgrp, "enable_contour", &ll->contour.enabled, 1);
        DRW_shgroup_uniform_int(rb->ChainShgrp, "enable_crease", &ll->crease.enabled, 1);
        DRW_shgroup_uniform_int(
            rb->ChainShgrp, "enable_material", &ll->material_separate.enabled, 1);
        DRW_shgroup_uniform_int(rb->ChainShgrp, "enable_edge_mark", &ll->edge_mark.enabled, 1);
        DRW_shgroup_uniform_int(
            rb->ChainShgrp, "enable_intersection", &ll->intersection.enabled, 1);

        static int normal_effect_inverse;
        normal_effect_inverse = (ll->flags & LANPR_LINE_LAYER_NORMAL_INVERSE) ? 1 : 0;
        DRW_shgroup_uniform_int(rb->ChainShgrp,
                                "normal_mode",
                                (ll->flags & LANPR_LINE_LAYER_NORMAL_ENABLED) ? &ll->normal_mode :
                                                                                &zero_value,
                                1);
        DRW_shgroup_uniform_int(
            rb->ChainShgrp, "normal_effect_inverse", &normal_effect_inverse, 1);
        DRW_shgroup_uniform_float(rb->ChainShgrp, "normal_ramp_begin", &ll->normal_ramp_begin, 1);
        DRW_shgroup_uniform_float(rb->ChainShgrp, "normal_ramp_end", &ll->normal_ramp_end, 1);
        DRW_shgroup_uniform_float(
            rb->ChainShgrp, "normal_thickness_start", &ll->normal_thickness_start, 1);
        DRW_shgroup_uniform_float(
            rb->ChainShgrp, "normal_thickness_end", &ll->normal_thickness_end, 1);
        DRW_shgroup_uniform_vec3(rb->ChainShgrp, "normal_direction", normal_object_direction, 1);

        DRW_shgroup_uniform_int(rb->ChainShgrp, "occlusion_level_start", &ll->qi_begin, 1);
        DRW_shgroup_uniform_int(rb->ChainShgrp,
                                "occlusion_level_end",
                                (ll->flags & LANPR_LINE_LAYER_USE_MULTIPLE_LEVELS) ? &ll->qi_end :
                                                                                     &ll->qi_begin,
                                1);

        DRW_shgroup_uniform_vec4(
            rb->ChainShgrp, "preview_viewport", stl->g_data->dpix_viewport, 1);
        DRW_shgroup_uniform_vec4(
            rb->ChainShgrp, "output_viewport", stl->g_data->output_viewport, 1);

        float *tld = &lanpr->taper_left_distance, *tls = &lanpr->taper_left_strength,
              *trd = &lanpr->taper_right_distance, *trs = &lanpr->taper_right_strength;

        DRW_shgroup_uniform_float(rb->ChainShgrp, "taper_l_dist", tld, 1);
        DRW_shgroup_uniform_float(rb->ChainShgrp, "taper_l_strength", tls, 1);
        DRW_shgroup_uniform_float(
            rb->ChainShgrp, "taper_r_dist", (lanpr->flags & LANPR_SAME_TAPER) ? tld : trd, 1);
        DRW_shgroup_uniform_float(
            rb->ChainShgrp, "taper_r_strength", (lanpr->flags & LANPR_SAME_TAPER) ? tls : trs, 1);

        /*  need to add component enable/disable option. */
        DRW_shgroup_call(rb->ChainShgrp, lanpr_share.render_buffer_shared->chain_draw_batch, NULL);
        /*  debug purpose */
        /*  DRW_draw_pass(psl->color_pass); */
        /*  DRW_draw_pass(psl->color_pass); */
        DRW_draw_pass(psl->software_pass);
      }
    }
    else if (!(lanpr->flags & LANPR_USE_CHAINING)) {
      for (ll = lanpr->line_layers.last; ll; ll = ll->prev) {
        if (ll->batch) {
          psl->software_pass = DRW_pass_create("Software Render Preview",
                                               DRW_STATE_WRITE_COLOR | DRW_STATE_WRITE_DEPTH |
                                                   DRW_STATE_DEPTH_LESS_EQUAL);
          ll->shgrp = DRW_shgroup_create(lanpr_share.software_shader, psl->software_pass);

          ED_lanpr_calculate_normal_object_vector(ll, normal_object_direction);

          DRW_shgroup_uniform_float(ll->shgrp, "camdx", &camdx, 1);
          DRW_shgroup_uniform_float(ll->shgrp, "camdy", &camdy, 1);
          DRW_shgroup_uniform_float(ll->shgrp, "camzoom", &camzoom, 1);

          DRW_shgroup_uniform_vec4(
              ll->shgrp,
              "contour_color",
              (ll->flags & LANPR_LINE_LAYER_USE_SAME_STYLE) ? ll->color : ll->contour.color,
              1);
          DRW_shgroup_uniform_vec4(
              ll->shgrp,
              "crease_color",
              (ll->flags & LANPR_LINE_LAYER_USE_SAME_STYLE) ? ll->color : ll->crease.color,
              1);
          DRW_shgroup_uniform_vec4(ll->shgrp,
                                   "material_color",
                                   (ll->flags & LANPR_LINE_LAYER_USE_SAME_STYLE) ?
                                       ll->color :
                                       ll->material_separate.color,
                                   1);
          DRW_shgroup_uniform_vec4(
              ll->shgrp,
              "edge_mark_color",
              (ll->flags & LANPR_LINE_LAYER_USE_SAME_STYLE) ? ll->color : ll->edge_mark.color,
              1);
          DRW_shgroup_uniform_vec4(
              ll->shgrp,
              "intersection_color",
              (ll->flags & LANPR_LINE_LAYER_USE_SAME_STYLE) ? ll->color : ll->intersection.color,
              1);
          static float uniform_thickness = 1.0f;
          DRW_shgroup_uniform_float(ll->shgrp, "thickness", &ll->thickness, 1);
          DRW_shgroup_uniform_float(ll->shgrp,
                                    "thickness_contour",
                                    (ll->flags & LANPR_LINE_LAYER_USE_SAME_STYLE) ?
                                        &uniform_thickness :
                                        &ll->contour.thickness,
                                    1);
          DRW_shgroup_uniform_float(ll->shgrp,
                                    "thickness_crease",
                                    (ll->flags & LANPR_LINE_LAYER_USE_SAME_STYLE) ?
                                        &uniform_thickness :
                                        &ll->crease.thickness,
                                    1);
          DRW_shgroup_uniform_float(ll->shgrp,
                                    "thickness_material",
                                    (ll->flags & LANPR_LINE_LAYER_USE_SAME_STYLE) ?
                                        &uniform_thickness :
                                        &ll->material_separate.thickness,
                                    1);
          DRW_shgroup_uniform_float(ll->shgrp,
                                    "thickness_edge_mark",
                                    (ll->flags & LANPR_LINE_LAYER_USE_SAME_STYLE) ?
                                        &uniform_thickness :
                                        &ll->edge_mark.thickness,
                                    1);
          DRW_shgroup_uniform_float(ll->shgrp,
                                    "thickness_intersection",
                                    (ll->flags & LANPR_LINE_LAYER_USE_SAME_STYLE) ?
                                        &uniform_thickness :
                                        &ll->intersection.thickness,
                                    1);
          DRW_shgroup_uniform_vec4(ll->shgrp, "preview_viewport", stl->g_data->dpix_viewport, 1);
          DRW_shgroup_uniform_vec4(ll->shgrp, "output_viewport", stl->g_data->output_viewport, 1);

          static int normal_effect_inverse;
          normal_effect_inverse = (ll->flags & LANPR_LINE_LAYER_NORMAL_INVERSE) ? 1 : 0;
          DRW_shgroup_uniform_int(
              ll->shgrp,
              "normal_mode",
              (ll->flags & LANPR_LINE_LAYER_NORMAL_ENABLED) ? &ll->normal_mode : &zero_value,
              1);
          DRW_shgroup_uniform_int(ll->shgrp, "normal_effect_inverse", &normal_effect_inverse, 1);
          DRW_shgroup_uniform_float(ll->shgrp, "normal_ramp_begin", &ll->normal_ramp_begin, 1);
          DRW_shgroup_uniform_float(ll->shgrp, "normal_ramp_end", &ll->normal_ramp_end, 1);
          DRW_shgroup_uniform_float(
              ll->shgrp, "normal_thickness_start", &ll->normal_thickness_start, 1);
          DRW_shgroup_uniform_float(
              ll->shgrp, "normal_thickness_end", &ll->normal_thickness_end, 1);
          DRW_shgroup_uniform_vec3(ll->shgrp, "normal_direction", normal_object_direction, 1);

          DRW_shgroup_call(ll->shgrp, ll->batch, NULL);
          DRW_draw_pass(psl->software_pass);
        }
      }
    }
  }

  GPU_framebuffer_blit(fbl->software_ms, 0, dfb, 0, GPU_COLOR_BIT);

  if (!is_render) {
    DRW_view_set_active(NULL);
  }
}

void ED_lanpr_update_render_progress(const char *text)
{
  if (lanpr_share.re_render) {
    RE_engine_update_stats(lanpr_share.re_render, NULL, text);
  }
}
