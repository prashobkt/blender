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
#include "BKE_object.h"
#include "BKE_report.h"
#include "BKE_screen.h"

#include "BLI_math_geom.h"
#include "BLI_math_matrix.h"

#include "DNA_object_types.h"
#include "DNA_scene_types.h"
#include "DNA_view3d_types.h"
#include "DNA_xr_types.h"

#include "DRW_engine.h"

#include "ED_view3d.h"

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

void wm_xr_runtime_session_state_free(struct bXrRuntimeSessionState **state);
void wm_xr_draw_view(const GHOST_XrDrawViewInfo *, void *);
void *wm_xr_session_gpu_binding_context_create(GHOST_TXrGraphicsBinding);
void wm_xr_session_gpu_binding_context_destroy(GHOST_TXrGraphicsBinding, void *);
wmSurface *wm_xr_session_surface_create(wmWindowManager *, unsigned int);

/* -------------------------------------------------------------------- */

typedef struct bXrRuntimeSessionState {
  /** The pose (location + rotation) that acts as the basis for view transforms (world space). */
  GHOST_XrPose reference_pose;
  /** The pose (location + rotation) to which eye deltas will be applied to when drawing (world
   * space). With positional tracking enabled, it should be the same as `base_pose`, when disabled
   * it also contains a location delta from the moment the option was toggled. */
  GHOST_XrPose final_reference_pose;

  /** Last known viewer location (centroid of eyes, in world space) stored for queries. */
  GHOST_XrPose viewer_pose;

  /** Copy of bXrSessionSettings.flag created on the last draw call,  */
  int prev_settings_flag;
} bXrRuntimeSessionState;

typedef struct {
  GHOST_TXrGraphicsBinding gpu_binding_type;
  GPUOffScreen *offscreen;
  GPUViewport *viewport;
  GPUFrameBuffer *fbo;
  GPUTexture *fbo_tex;
  bool viewport_bound;

  GHOST_ContextHandle secondary_ghost_ctx;
} wmXrSurfaceData;

typedef struct {
  wmWindowManager *wm;
  bContext *evil_C;
} wmXrErrorHandlerData;

/* -------------------------------------------------------------------- */

static wmSurface *g_xr_surface = NULL;

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
  UI_popup_menu_reports(handler_data->evil_C, &wm->reports);

  if (wm->xr.context) {
    /* Just play safe and destroy the entire context. */
    GHOST_XrContextDestroy(wm->xr.context);
    wm->xr.context = NULL;
  }
}

bool wm_xr_context_ensure(bContext *C, wmWindowManager *wm)
{
  if (wm->xr.context) {
    return true;
  }
  static wmXrErrorHandlerData error_customdata;

  /* Set up error handling */
  error_customdata.wm = wm;
  error_customdata.evil_C = C;
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

void wm_xr_data_destroy(wmWindowManager *wm)
{
  if (wm->xr.context != NULL) {
    GHOST_XrContextDestroy(wm->xr.context);
  }
  if (wm->xr.session_state != NULL) {
    wm_xr_runtime_session_state_free(&wm->xr.session_state);
  }
}

/** \} */ /* XR-Context */

/* -------------------------------------------------------------------- */
/** \name XR Runtime Session State
 *
 * \{ */

static bXrRuntimeSessionState *wm_xr_runtime_session_state_create(const Scene *scene)
{
  bXrRuntimeSessionState *state = MEM_callocN(sizeof(*state), __func__);

  if (scene->camera) {
    copy_v3_v3(state->reference_pose.position, scene->camera->loc);
    add_v3_v3(state->reference_pose.position, scene->camera->dloc);
    BKE_object_rot_to_quat(scene->camera, state->reference_pose.orientation_quat);
  }
  else {
    copy_v3_fl(state->reference_pose.position, 0.0f);
    unit_qt(state->reference_pose.orientation_quat);
  }

  /* Make sure the final reference pose is set, even if positional tracking is disabled on session
   * start. */
  copy_v3_v3(state->final_reference_pose.position, state->reference_pose.position);

  return state;
}

void wm_xr_runtime_session_state_free(bXrRuntimeSessionState **state)
{
  MEM_SAFE_FREE(*state);
}

static void wm_xr_runtime_session_state_update(bXrRuntimeSessionState *state,
                                               const GHOST_XrDrawViewInfo *draw_view,
                                               const bXrSessionSettings *settings)
{
  const bool position_tracking_toggled = (state->prev_settings_flag &
                                          XR_SESSION_USE_POSITION_TRACKING) !=
                                         (settings->flag & XR_SESSION_USE_POSITION_TRACKING);
  const bool use_position_tracking = settings->flag & XR_SESSION_USE_POSITION_TRACKING;

  copy_v4_v4(state->final_reference_pose.orientation_quat, state->reference_pose.orientation_quat);

  if (position_tracking_toggled) {
    copy_v3_v3(state->final_reference_pose.position, state->reference_pose.position);

    /* Update reference pose to the current position. */
    if (use_position_tracking == false) {
      /* OpenXR/Ghost-XR returns the local pose in local space, we need it in world space. */
      state->final_reference_pose.position[0] += draw_view->local_pose.position[0];
      state->final_reference_pose.position[1] -= draw_view->local_pose.position[2];
      state->final_reference_pose.position[2] += draw_view->local_pose.position[1];
    }
  }

  mul_qt_qtqt(state->viewer_pose.orientation_quat,
              state->final_reference_pose.orientation_quat,
              draw_view->local_pose.orientation_quat);

  copy_v3_v3(state->viewer_pose.position, state->final_reference_pose.position);
  if (use_position_tracking) {
    state->viewer_pose.position[0] += draw_view->local_pose.position[0];
    state->viewer_pose.position[1] -= draw_view->local_pose.position[2];
    state->viewer_pose.position[2] += draw_view->local_pose.position[1];
  }

  state->prev_settings_flag = settings->flag;
}

void WM_xr_session_state_viewer_location_get(const wmXrData *xr, float location[3])
{
  if (!WM_xr_is_session_running(xr)) {
    return;
  }

  copy_v3_v3(location, xr->session_state->viewer_pose.position);
}

void WM_xr_session_state_viewer_rotation_get(const wmXrData *xr, float rotation[4])
{
  if (!WM_xr_is_session_running(xr)) {
    return;
  }

  copy_v4_v4(rotation, xr->session_state->viewer_pose.orientation_quat);
}

/** \} */ /* XR Runtime Session State */

/* -------------------------------------------------------------------- */
/** \name XR-Session
 *
 * \{ */

void *wm_xr_session_gpu_binding_context_create(GHOST_TXrGraphicsBinding graphics_binding)
{
  wmSurface *surface = wm_xr_session_surface_create(G_MAIN->wm.first, graphics_binding);
  wmXrSurfaceData *data = surface->customdata;

  wm_surface_add(surface);

  /* Some regions may need to redraw with updated session state after the session is entirely up
   * and running. */
  WM_main_add_notifier(NC_WM | ND_XR_DATA_CHANGED, NULL);

  return data->secondary_ghost_ctx ? data->secondary_ghost_ctx : surface->ghost_ctx;
}

void wm_xr_session_gpu_binding_context_destroy(GHOST_TXrGraphicsBinding UNUSED(graphics_lib),
                                               void *UNUSED(context))
{
  if (g_xr_surface) { /* Might have been freed already */
    wm_surface_remove(g_xr_surface);
  }

  wm_window_reset_drawable();

  /* Some regions may need to redraw with updated session state after the session is entirely
   * stopped. */
  WM_main_add_notifier(NC_WM | ND_XR_DATA_CHANGED, NULL);
}

static void wm_xr_session_begin_info_create(const bXrRuntimeSessionState *state,
                                            GHOST_XrSessionBeginInfo *r_begin_info)
{
  copy_v3_v3(r_begin_info->base_pose.position, state->reference_pose.position);
  copy_v4_v4(r_begin_info->base_pose.orientation_quat, state->reference_pose.orientation_quat);
}

void wm_xr_session_toggle(bContext *C, void *xr_context_ptr)
{
  GHOST_XrContextHandle xr_context = xr_context_ptr;
  wmWindowManager *wm = CTX_wm_manager(C);

  if (WM_xr_is_session_running(&wm->xr)) {
    GHOST_XrSessionEnd(xr_context);
    wm_xr_runtime_session_state_free(&wm->xr.session_state);
  }
  else {
    GHOST_XrSessionBeginInfo begin_info;

    wm->xr.session_state = wm_xr_runtime_session_state_create(CTX_data_scene(C));
    wm_xr_session_begin_info_create(wm->xr.session_state, &begin_info);

    GHOST_XrSessionStart(xr_context, &begin_info);
  }
}

bool WM_xr_is_session_running(const wmXrData *xr)
{
  /* wmXrData.session_state will be NULL if session end was requested. In that case, pretend like
   * it's already  */
  return xr->context && GHOST_XrSessionIsRunning(xr->context);
}

/** \} */ /* XR-Session */

/* -------------------------------------------------------------------- */
/** \name XR-Session Surface
 *
 * A wmSurface is used to manage drawing of the VR viewport. It's created and destroyed with the
 * session.
 *
 * \{ */

static void wm_xr_surface_viewport_bind(wmXrSurfaceData *surface_data, const rcti *rect)
{
  if (surface_data->viewport_bound == false) {
    GPU_viewport_bind(surface_data->viewport, rect);
  }
  surface_data->viewport_bound = true;
}
static void wm_xr_surface_viewport_unbind(wmXrSurfaceData *surface_data)
{
  if (surface_data->viewport_bound) {
    GPU_viewport_unbind(surface_data->viewport);
  }
  surface_data->viewport_bound = false;
}

/**
 * \brief Call Ghost-XR to draw a frame
 *
 * Draw callback for the XR-session surface. It's expected to be called on each main loop iteration
 * and tells Ghost-XR to submit a new frame by drawing its views. Note that for drawing each view,
 * #wm_xr_draw_view() will be called through Ghost-XR (see GHOST_XrDrawViewFunc()).
 */
static void wm_xr_session_surface_draw(bContext *C)
{
  wmWindowManager *wm = CTX_wm_manager(C);
  wmXrSurfaceData *surface_data = g_xr_surface->customdata;

  if (!GHOST_XrSessionIsRunning(wm->xr.context)) {
    return;
  }
  GHOST_XrSessionDrawViews(wm->xr.context, C);
  if (surface_data->viewport) {
    /* Still bound from view drawing. */
    wm_xr_surface_viewport_unbind(surface_data);
  }
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
  GPU_context_active_set(surface->gpu_ctx);
  DRW_opengl_context_enable_ex(false);
  if (data->viewport) {
    GPU_viewport_clear_from_offscreen(data->viewport);
    GPU_viewport_free(data->viewport);
  }
  if (data->offscreen) {
    GPU_offscreen_free(data->offscreen);
  }
  if (data->fbo) {
    GPU_framebuffer_free(data->fbo);
  }
  if (data->fbo_tex) {
    GPU_texture_free(data->fbo_tex);
  }
  DRW_opengl_context_disable_ex(false);

  MEM_freeN(surface->customdata);

  g_xr_surface = NULL;
}

static bool wm_xr_session_surface_offscreen_ensure(const GHOST_XrDrawViewInfo *draw_view)
{
  wmXrSurfaceData *surface_data = g_xr_surface->customdata;
  char err_out[256] = "unknown";
  bool failure = false;

  if (surface_data->offscreen &&
      (GPU_offscreen_width(surface_data->offscreen) == draw_view->width) &&
      (GPU_offscreen_height(surface_data->offscreen) == draw_view->height)) {
    BLI_assert(surface_data->viewport);
    return true;
  }

  DRW_opengl_context_enable();
  if (surface_data->offscreen) {
    GPU_viewport_clear_from_offscreen(surface_data->viewport);
    GPU_viewport_free(surface_data->viewport);
    GPU_offscreen_free(surface_data->offscreen);
  }

  if (!(surface_data->offscreen = GPU_offscreen_create(
            draw_view->width, draw_view->height, 0, true, false, err_out))) {
    failure = true;
  }
  if ((failure == false) &&
      !(surface_data->viewport = GPU_viewport_create_from_offscreen(surface_data->offscreen))) {
    GPU_offscreen_free(surface_data->offscreen);
    failure = true;
  }

  surface_data->fbo = GPU_framebuffer_create();
  surface_data->fbo_tex = GPU_texture_create_2d(
      draw_view->width, draw_view->height, GPU_RGBA8, NULL, NULL);
  GPU_framebuffer_ensure_config(
      &surface_data->fbo, {GPU_ATTACHMENT_NONE, GPU_ATTACHMENT_TEXTURE(surface_data->fbo_tex)});

  DRW_opengl_context_disable();

  if (failure) {
    fprintf(stderr, "%s: failed to get buffer, %s\n", __func__, err_out);
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

  surface->ghost_ctx = DRW_opengl_context_get();
  DRW_opengl_context_enable();

  switch (gpu_binding_type) {
    case GHOST_kXrGraphicsOpenGL:
      break;
#ifdef WIN32
    case GHOST_kXrGraphicsD3D11:
      data->secondary_ghost_ctx = WM_directx_context_create();
      break;
#endif
  }

  surface->gpu_ctx = DRW_gpu_context_get();

  DRW_opengl_context_disable();

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
static void wm_xr_draw_matrices_create(const GHOST_XrDrawViewInfo *draw_view,
                                       const bXrSessionSettings *session_settings,
                                       const bXrRuntimeSessionState *session_state,
                                       float r_view_mat[4][4],
                                       float r_proj_mat[4][4])
{
  perspective_m4_fov(r_proj_mat,
                     draw_view->fov.angle_left,
                     draw_view->fov.angle_right,
                     draw_view->fov.angle_up,
                     draw_view->fov.angle_down,
                     session_settings->clip_start,
                     session_settings->clip_end);

  float eye_mat[4][4];
  float quat[4];
  invert_qt_qt_normalized(quat, draw_view->eye_pose.orientation_quat);
  quat_to_mat4(eye_mat, quat);
  if (session_settings->flag & XR_SESSION_USE_POSITION_TRACKING) {
    translate_m4(eye_mat,
                 -draw_view->eye_pose.position[0],
                 -draw_view->eye_pose.position[1],
                 -draw_view->eye_pose.position[2]);
  }

  float base_mat[4][4];
  invert_qt_qt_normalized(quat, session_state->final_reference_pose.orientation_quat);
  quat_to_mat4(base_mat, quat);
  translate_m4(base_mat,
               -session_state->final_reference_pose.position[0],
               -session_state->final_reference_pose.position[1],
               -session_state->final_reference_pose.position[2]);

  mul_m4_m4m4(r_view_mat, eye_mat, base_mat);
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
  bXrSessionSettings *settings = &wm->xr.session_settings;
  Scene *scene = CTX_data_scene(C);

  const float display_flags = V3D_OFSDRAW_OVERRIDE_SCENE_SETTINGS | settings->draw_flags;
  const rcti rect = {
      .xmin = 0, .ymin = 0, .xmax = draw_view->width - 1, .ymax = draw_view->height - 1};

  GPUOffScreen *offscreen;
  GPUViewport *viewport;
  View3DShading shading;
  float viewmat[4][4], winmat[4][4];

  /* The runtime may still trigger drawing while a session-end request is pending. */
  if (!wm->xr.session_state || !wm->xr.context) {
    return;
  }

  wm_xr_runtime_session_state_update(wm->xr.session_state, draw_view, settings);
  wm_xr_draw_matrices_create(draw_view, settings, wm->xr.session_state, viewmat, winmat);

  if (!wm_xr_session_surface_offscreen_ensure(draw_view)) {
    return;
  }

  offscreen = surface_data->offscreen;
  viewport = surface_data->viewport;
  wm_xr_surface_viewport_bind(surface_data, &rect);
  glClear(GL_DEPTH_BUFFER_BIT);

  BKE_screen_view3d_shading_init(&shading);
  shading.flag |= V3D_SHADING_WORLD_ORIENTATION;
  shading.flag &= ~V3D_SHADING_SPECULAR_HIGHLIGHT;
  shading.background_type = V3D_SHADING_BACKGROUND_WORLD;
  ED_view3d_draw_offscreen_simple(CTX_data_ensure_evaluated_depsgraph(C),
                                  scene,
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
                                  offscreen,
                                  viewport);

  GPU_framebuffer_bind(surface_data->fbo);

  wm_draw_offscreen_texture_parameters(offscreen);

  wmViewport(&rect);
  const bool is_upside_down = surface_data->secondary_ghost_ctx &&
                              GHOST_isUpsideDownContext(surface_data->secondary_ghost_ctx);
  const int ymin = is_upside_down ? draw_view->height : 0;
  const int ymax = is_upside_down ? 0 : draw_view->height;
  GPU_viewport_draw_to_screen_ex(
      viewport, 0, draw_view->width, ymin, ymax, draw_view->expects_srgb_buffer);

  /* Leave viewport bound so GHOST_Xr can use its context/framebuffer, its unbound in
   * wm_xr_session_surface_draw(). */
  // GPU_viewport_unbind(viewport);
}

/** \} */ /* XR Drawing */
