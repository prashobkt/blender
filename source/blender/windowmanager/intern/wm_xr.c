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
 */

/** \file
 * \ingroup wm
 *
 * \name Window-Manager XR API
 *
 * Implements Blender specific functionality for the GHOST_Xr API.
 */

#include "BKE_context.h"
#include "BKE_global.h"
#include "BKE_main.h"
#include "BKE_report.h"
#include "BKE_screen.h"

#include "BLI_math_geom.h"
#include "BLI_math_matrix.h"

#include "CLG_log.h"

#include "DNA_object_types.h"
#include "DNA_scene_types.h"
#include "DNA_view3d_types.h"
#include "DNA_xr_types.h"

#include "DRW_engine.h"

#include "ED_view3d.h"
#include "ED_view3d_offscreen.h"

#include "GHOST_C-api.h"

#include "GPU_context.h"
#include "GPU_draw.h"
#include "GPU_matrix.h"
#include "GPU_viewport.h"

#include "MEM_guardedalloc.h"

#include "UI_interface.h"

#include "WM_types.h"
#include "WM_api.h"

#include "wm.h"
#include "wm_surface.h"
#include "wm_window.h"

static wmSurface *g_xr_surface = NULL;
static CLG_LogRef LOG = {"wm.xr"};

typedef struct {
  GHOST_TXrGraphicsBinding gpu_binding_type;
  GPUOffScreen *offscreen;
  GPUViewport *viewport;

  GHOST_ContextHandle secondary_ghost_ctx;
} wmXrSurfaceData;

typedef struct {
  wmWindowManager *wm;
} wmXrErrorHandlerData;

void wm_xr_draw_view(const GHOST_XrDrawViewInfo *, void *);
void *wm_xr_session_gpu_binding_context_create(GHOST_TXrGraphicsBinding);
void wm_xr_session_gpu_binding_context_destroy(GHOST_TXrGraphicsBinding, void *);
wmSurface *wm_xr_session_surface_create(wmWindowManager *, unsigned int);

/* -------------------------------------------------------------------- */
/** \name XR-Context
 *
 * All XR functionality is accessed through a #GHOST_XrContext handle.
 * The lifetime of this context also determines the lifetime of the OpenXR instance, which is the
 * representation of the OpenXR runtime connection within the application.
 *
 * \{ */

static void wm_xr_error_handler(const GHOST_XrError *error)
{
  wmXrErrorHandlerData *handler_data = error->customdata;
  wmWindowManager *wm = handler_data->wm;

  BKE_reports_clear(&wm->reports);
  WM_report(RPT_ERROR, error->user_message);
  WM_report_banner_show();

  if (wm->xr.context) {
    /* Just play safe and destroy the entire context. */
    GHOST_XrContextDestroy(wm->xr.context);
    wm->xr.context = NULL;
  }
}

bool wm_xr_context_ensure(wmWindowManager *wm)
{
  if (wm->xr.context) {
    return true;
  }
  static wmXrErrorHandlerData error_customdata;

  /* Set up error handling */
  error_customdata.wm = wm;
  GHOST_XrErrorHandler(wm_xr_error_handler, &error_customdata);

  {
    const GHOST_TXrGraphicsBinding gpu_bindings_candidates[] = {
        GHOST_kXrGraphicsOpenGL,
#ifdef WIN32
        GHOST_kXrGraphicsD3D11,
#endif
    };
    GHOST_XrContextCreateInfo create_info = {
        .gpu_binding_candidates = gpu_bindings_candidates,
        .gpu_binding_candidates_count = ARRAY_SIZE(gpu_bindings_candidates)};

    if (G.debug & G_DEBUG_XR) {
      create_info.context_flag |= GHOST_kXrContextDebug;
    }
    if (G.debug & G_DEBUG_XR_TIME) {
      create_info.context_flag |= GHOST_kXrContextDebugTime;
    }

    if (!(wm->xr.context = GHOST_XrContextCreate(&create_info))) {
      return false;
    }

    /* Set up context callbacks */
    GHOST_XrGraphicsContextBindFuncs(wm->xr.context,
                                     wm_xr_session_gpu_binding_context_create,
                                     wm_xr_session_gpu_binding_context_destroy);
    GHOST_XrDrawViewFunc(wm->xr.context, wm_xr_draw_view);
  }
  BLI_assert(wm->xr.context != NULL);

  return true;
}

void wm_xr_context_destroy(wmWindowManager *wm)
{
  if (wm->xr.context != NULL) {
    GHOST_XrContextDestroy(wm->xr.context);
  }
}

/** \} */ /* XR-Context */

/* -------------------------------------------------------------------- */
/** \name XR-Session
 *
 * \{ */

void *wm_xr_session_gpu_binding_context_create(GHOST_TXrGraphicsBinding graphics_binding)
{
  wmSurface *surface = wm_xr_session_surface_create(G_MAIN->wm.first, graphics_binding);
  wmXrSurfaceData *data = surface->customdata;

  wm_surface_add(surface);

  return data->secondary_ghost_ctx ? data->secondary_ghost_ctx : surface->ghost_ctx;
}

void wm_xr_session_gpu_binding_context_destroy(GHOST_TXrGraphicsBinding UNUSED(graphics_lib),
                                               void *UNUSED(context))
{
  if (g_xr_surface) { /* Might have been freed already */
    wm_surface_remove(g_xr_surface);
  }

  wm_window_reset_drawable();
}

static void wm_xr_session_begin_info_create(const Scene *scene,
                                            GHOST_XrSessionBeginInfo *begin_info)
{
  if (scene->camera) {
    copy_v3_v3(begin_info->base_pose.position, scene->camera->loc);
    if (ELEM(scene->camera->rotmode, ROT_MODE_AXISANGLE, ROT_MODE_QUAT)) {
      axis_angle_to_quat(
          begin_info->base_pose.orientation_quat, scene->camera->rotAxis, scene->camera->rotAngle);
    }
    else if (scene->camera->rotmode == ROT_MODE_QUAT) {
      copy_v4_v4(begin_info->base_pose.orientation_quat, scene->camera->quat);
    }
    else {
      eul_to_quat(begin_info->base_pose.orientation_quat, scene->camera->rot);
    }
  }
  else {
    copy_v3_fl(begin_info->base_pose.position, 0.0f);
    unit_qt(begin_info->base_pose.orientation_quat);
  }
}

void wm_xr_session_toggle(bContext *C, void *xr_context_ptr)
{
  GHOST_XrContextHandle xr_context = xr_context_ptr;

  if (xr_context && GHOST_XrSessionIsRunning(xr_context)) {
    GHOST_XrSessionEnd(xr_context);
  }
  else {
    GHOST_XrSessionBeginInfo begin_info;

    wm_xr_session_begin_info_create(CTX_data_scene(C), &begin_info);

    GHOST_XrSessionStart(xr_context, &begin_info);
  }
}

/** \} */ /* XR-Session */

/* -------------------------------------------------------------------- */
/** \name XR-Session Surface
 *
 * A wmSurface is used to manage drawing of the VR viewport. It's created and destroyed with the
 * session.
 *
 * \{ */

/**
 * \brief Call Ghost-XR to draw a frame
 *
 * Draw callback for the XR-session surface. It's expected to be called on each main loop iteration
 * and tells Ghost-XR to submit a new frame by drawing its views. Note that for drawing each view,
 * #wm_xr_draw_view() will be called through Ghost-XR (see GHOST_XrDrawViewFunc()).
 */
static void wm_xr_session_surface_draw(bContext *C)
{
  wmXrSurfaceData *surface_data = g_xr_surface->customdata;
  wmWindowManager *wm = CTX_wm_manager(C);

  if (!GHOST_XrSessionIsRunning(wm->xr.context)) {
    return;
  }
  GHOST_XrSessionDrawViews(wm->xr.context, C);

  GPU_offscreen_unbind(surface_data->offscreen, false);
}

static void wm_xr_session_free_data(wmSurface *surface)
{
  wmXrSurfaceData *data = surface->customdata;

  if (data->secondary_ghost_ctx) {
#ifdef WIN32
    if (data->gpu_binding_type == GHOST_kXrGraphicsD3D11) {
      WM_directx_context_dispose(data->secondary_ghost_ctx);
    }
#endif
  }
  if (data->viewport) {
    GPU_viewport_free(data->viewport);
  }
  if (data->offscreen) {
    GPU_offscreen_free(data->offscreen);
  }

  MEM_freeN(surface->customdata);

  g_xr_surface = NULL;
}

static bool wm_xr_session_surface_offscreen_ensure(const GHOST_XrDrawViewInfo *draw_view)
{
  wmXrSurfaceData *surface_data = g_xr_surface->customdata;
  const bool size_changed = surface_data->offscreen &&
                            (GPU_offscreen_width(surface_data->offscreen) != draw_view->width) &&
                            (GPU_offscreen_height(surface_data->offscreen) != draw_view->height);
  char err_out[256] = "unknown";
  bool failure = false;

  if (surface_data->offscreen) {
    BLI_assert(surface_data->viewport);

    if (!size_changed) {
      return true;
    }
    GPU_viewport_free(surface_data->viewport);
    GPU_offscreen_free(surface_data->offscreen);
  }

  if (!(surface_data->offscreen = GPU_offscreen_create(
            draw_view->width, draw_view->height, 0, true, false, err_out))) {
    failure = true;
  }

  if (failure) {
    /* Pass. */
  }
  else if (!(surface_data->viewport = GPU_viewport_create())) {
    GPU_offscreen_free(surface_data->offscreen);
    failure = true;
  }

  if (failure) {
    CLOG_ERROR(&LOG, "Failed to get buffer, %s\n", err_out);
    return false;
  }

  return true;
}

wmSurface *wm_xr_session_surface_create(wmWindowManager *UNUSED(wm), unsigned int gpu_binding_type)
{
  if (g_xr_surface) {
    BLI_assert(false);
    return g_xr_surface;
  }

  wmSurface *surface = MEM_callocN(sizeof(*surface), __func__);
  wmXrSurfaceData *data = MEM_callocN(sizeof(*data), "XrSurfaceData");

#ifndef WIN32
  BLI_assert(gpu_binding_type == GHOST_kXrGraphicsOpenGL);
#endif

  surface->draw = wm_xr_session_surface_draw;
  surface->free_data = wm_xr_session_free_data;

  data->gpu_binding_type = gpu_binding_type;
  surface->customdata = data;

  surface->ghost_ctx = DRW_xr_opengl_context_get();

  switch (gpu_binding_type) {
    case GHOST_kXrGraphicsOpenGL:
      break;
#ifdef WIN32
    case GHOST_kXrGraphicsD3D11:
      data->secondary_ghost_ctx = WM_directx_context_create();
      break;
#endif
  }

  surface->gpu_ctx = DRW_xr_gpu_context_get();

  g_xr_surface = surface;

  return surface;
}

/** \} */ /* XR-Session Surface */

/* -------------------------------------------------------------------- */
/** \name XR Drawing
 *
 * \{ */

/**
 * Proper reference space set up is not supported yet. We simply hand OpenXR the global space as
 * reference space and apply its pose onto the active camera matrix to get a basic viewing
 * experience going. If there's no active camera with stick to the world origin.
 */
static void wm_xr_draw_matrices_create(const Scene *scene,
                                       const GHOST_XrDrawViewInfo *draw_view,
                                       const float clip_start,
                                       const float clip_end,
                                       float r_view_mat[4][4],
                                       float r_proj_mat[4][4])
{
  float scalemat[4][4], quat[4];
  float temp[4][4];

  perspective_m4_fov(r_proj_mat,
                     draw_view->fov.angle_left,
                     draw_view->fov.angle_right,
                     draw_view->fov.angle_up,
                     draw_view->fov.angle_down,
                     clip_start,
                     clip_end);

  scale_m4_fl(scalemat, 1.0f);
  invert_qt_qt_normalized(quat, draw_view->pose.orientation_quat);
  quat_to_mat4(temp, quat);
  translate_m4(temp,
               -draw_view->pose.position[0],
               -draw_view->pose.position[1],
               -draw_view->pose.position[2]);

  if (scene->camera) {
    invert_m4_m4(scene->camera->imat, scene->camera->obmat);
    mul_m4_m4m4(r_view_mat, temp, scene->camera->imat);
  }
  else {
    copy_m4_m4(r_view_mat, temp);
  }
}

static void wm_xr_draw_viewport_buffers_to_active_framebuffer(
    const wmXrSurfaceData *surface_data, const GHOST_XrDrawViewInfo *draw_view)
{
  const bool is_upside_down = surface_data->secondary_ghost_ctx &&
                              GHOST_isUpsideDownContext(surface_data->secondary_ghost_ctx);
  rcti rect = {.xmin = 0, .ymin = 0, .xmax = draw_view->width - 1, .ymax = draw_view->height - 1};

  wmViewport(&rect);

  /* For upside down contexts, draw with inverted y-values. */
  if (is_upside_down) {
    SWAP(int, rect.ymin, rect.ymax);
  }
  GPU_viewport_draw_to_screen(surface_data->viewport, &rect);
}

/**
 * \brief Draw a viewport for a single eye.
 *
 * This is the main viewport drawing function for VR sessions. It's assigned to Ghost-XR as a
 * callback (see GHOST_XrDrawViewFunc()) and executed for each view (read: eye).
 */
void wm_xr_draw_view(const GHOST_XrDrawViewInfo *draw_view, void *customdata)
{
  bContext *C = customdata;
  wmWindowManager *wm = CTX_wm_manager(C);
  wmXrSurfaceData *surface_data = g_xr_surface->customdata;
  XrSessionSettings *settings = &wm->xr.session_settings;
  const float display_flags = V3D_OFSDRAW_OVERRIDE_SCENE_SETTINGS | settings->draw_flags;

  View3DShading shading;
  float viewmat[4][4], winmat[4][4];

  wm_xr_draw_matrices_create(
      CTX_data_scene(C), draw_view, settings->clip_start, settings->clip_end, viewmat, winmat);

  if (!wm_xr_session_surface_offscreen_ensure(draw_view)) {
    return;
  }

  /* In case a framebuffer is still bound from drawing the last eye. */
  GPU_framebuffer_restore();

  BKE_screen_view3d_shading_init(&shading);
  shading.flag |= V3D_SHADING_WORLD_ORIENTATION;
  shading.flag &= ~V3D_SHADING_SPECULAR_HIGHLIGHT;
  shading.background_type = V3D_SHADING_BACKGROUND_WORLD;

  /* Draws the view into the surface_data->viewport's framebuffers */
  ED_view3d_draw_offscreen_simple(CTX_data_ensure_evaluated_depsgraph(C),
                                  CTX_data_scene(C),
                                  &shading,
                                  wm->xr.session_settings.shading_type,
                                  draw_view->width,
                                  draw_view->height,
                                  display_flags,
                                  viewmat,
                                  winmat,
                                  settings->clip_start,
                                  settings->clip_end,
                                  true,
                                  true,
                                  NULL,
                                  false,
                                  surface_data->offscreen,
                                  surface_data->viewport);

  /* The draw-manager uses both GPUOffscreen and GPUViewport to manage frame and texture buffers. A
   * call to GPU_viewport_draw_to_screen() is still needed to get the final result from the
   * viewport buffers composited together and potentially color managed for display on screen.
   * It needs a bound framebuffer to draw into, for which we simply reuse the GPUOffscreen one.
   *
   * In a next step, Ghost-XR will use the the currently bound framebuffer to retrieve the image to
   * be submitted to the OpenXR swapchain. So do not un-bind the offscreen yet! */

  GPU_offscreen_bind(surface_data->offscreen, false);

  wm_xr_draw_viewport_buffers_to_active_framebuffer(surface_data, draw_view);
}

/** \} */ /* XR Drawing */
