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
 * \name Window-Manager XR Drawing
 *
 * Implements Blender specific drawing functionality for use with the Ghost-XR API.
 */

#include <string.h>

#include "BLI_math.h"

#include "ED_view3d_offscreen.h"

#include "GHOST_C-api.h"

#include "GPU_immediate.h"
#include "GPU_immediate_util.h"
#include "GPU_matrix.h"
#include "GPU_viewport.h"

#include "WM_api.h"

#include "wm_surface.h"
#include "wm_xr_intern.h"

void wm_xr_pose_to_viewmat(float r_viewmat[4][4], const GHOST_XrPose *pose)
{
  float iquat[4];
  invert_qt_qt_normalized(iquat, pose->orientation_quat);
  quat_to_mat4(r_viewmat, iquat);
  translate_m4(r_viewmat, -pose->position[0], -pose->position[1], -pose->position[2]);
}

static void wm_xr_draw_matrices_create(const wmXrDrawData *draw_data,
                                       const GHOST_XrDrawViewInfo *draw_view,
                                       const XrSessionSettings *session_settings,
                                       float scale,
                                       float r_view_mat[4][4],
                                       float r_proj_mat[4][4])
{
  GHOST_XrPose eye_pose;

  copy_qt_qt(eye_pose.orientation_quat, draw_view->eye_pose.orientation_quat);
  copy_v3_v3(eye_pose.position, draw_view->eye_pose.position);
  add_v3_v3(eye_pose.position, draw_data->eye_position_ofs);
  if ((session_settings->flag & XR_SESSION_USE_POSITION_TRACKING) == 0) {
    sub_v3_v3(eye_pose.position, draw_view->local_pose.position);
  }

  perspective_m4_fov(r_proj_mat,
                     draw_view->fov.angle_left,
                     draw_view->fov.angle_right,
                     draw_view->fov.angle_up,
                     draw_view->fov.angle_down,
                     session_settings->clip_start * scale,
                     session_settings->clip_end * scale);

  float eye_mat[4][4];
  float base_mat[4][4];

  wm_xr_pose_to_viewmat(eye_mat, &eye_pose);
  /* Calculate the base pose matrix (in world space!). */
  wm_xr_pose_to_viewmat(base_mat, &draw_data->base_pose);

  mul_m4_m4m4(r_view_mat, eye_mat, base_mat);
}

static void wm_xr_draw_viewport_buffers_to_active_framebuffer(
    const wmXrRuntimeData *runtime_data,
    const wmXrSurfaceData *surface_data,
    const GHOST_XrDrawViewInfo *draw_view)
{
  const bool is_upside_down = GHOST_XrSessionNeedsUpsideDownDrawing(runtime_data->context);
  rcti rect = {.xmin = 0, .ymin = 0, .xmax = draw_view->width - 1, .ymax = draw_view->height - 1};

  wmViewport(&rect);

  /* For upside down contexts, draw with inverted y-values. */
  if (is_upside_down) {
    SWAP(int, rect.ymin, rect.ymax);
  }
  GPU_viewport_draw_to_screen_ex(surface_data->viewport, 0, &rect, draw_view->expects_srgb_buffer);
}

void apply_world_transform(float viewmat[4][4], GHOST_XrPose world_pose, float scale)
{
  float world[4][4];
  float scalev[3] = {scale, scale, scale};

  loc_quat_size_to_mat4(world, world_pose.position, world_pose.orientation_quat, scalev);

  mul_m4_m4m4(viewmat, viewmat, world);
}

/**
 * \brief Draw a viewport for a single eye.
 *
 * This is the main viewport drawing function for VR sessions. It's assigned to Ghost-XR as a
 * callback (see GHOST_XrDrawViewFunc()) and executed for each view (read: eye).
 */

void wm_xr_session_controller_transform_update(GHOST_XrPose *dst_pose,
                                               const GHOST_XrPose *base_pose,
                                               const GHOST_XrPose *pose)
{
  copy_v3_v3(dst_pose->position, base_pose->position);
  dst_pose->position[0] = base_pose->position[0] + pose->position[0];
  dst_pose->position[1] = base_pose->position[1] - pose->position[2];
  dst_pose->position[2] = base_pose->position[2] + pose->position[1];

  mul_qt_qtqt(dst_pose->orientation_quat, base_pose->orientation_quat, pose->orientation_quat);

  float invBaseRotation[4];
  copy_qt_qt(invBaseRotation, base_pose->orientation_quat);
  invert_qt(invBaseRotation);

  mul_qt_qtqt(dst_pose->orientation_quat, dst_pose->orientation_quat, invBaseRotation);
}

void wm_xr_draw_view(const GHOST_XrDrawViewInfo *draw_view, void *customdata)
{
  wmXrDrawData *draw_data = customdata;
  wmXrData *xr_data = draw_data->xr_data;
  wmXrSurfaceData *surface_data = draw_data->surface_data;
  wmXrSessionState *session_state = &xr_data->runtime->session_state;
  XrSessionSettings *settings = &xr_data->session_settings;

  const int display_flags = V3D_OFSDRAW_OVERRIDE_SCENE_SETTINGS | settings->draw_flags;

  float viewmat[4][4], winmat[4][4];

  BLI_assert(WM_xr_session_is_ready(xr_data));

  wm_xr_session_draw_data_update(session_state, settings, draw_view, draw_data);
  wm_xr_draw_matrices_create(
      draw_data, draw_view, settings, session_state->world_scale, viewmat, winmat);

  apply_world_transform(viewmat, session_state->world_pose, session_state->world_scale);

  wm_xr_session_state_update(settings, draw_data, draw_view, session_state, viewmat);

  if (!wm_xr_session_surface_offscreen_ensure(surface_data, draw_view)) {
    return;
  }

  /* In case a framebuffer is still bound from drawing the last eye. */
  GPU_framebuffer_restore();
  /* Some systems have drawing glitches without this. */
  GPU_clear(GPU_DEPTH_BIT);

  /* Draws the view into the surface_data->viewport's framebuffers */
  ED_view3d_draw_offscreen_simple(draw_data->depsgraph,
                                  draw_data->scene,
                                  &settings->shading,
                                  settings->shading.type,
                                  draw_view->width,
                                  draw_view->height,
                                  display_flags,
                                  viewmat,
                                  winmat,
                                  settings->clip_start * session_state->world_scale,
                                  settings->clip_end * session_state->world_scale,
                                  false,
                                  true,
                                  true,
                                  NULL,
                                  false,
                                  surface_data->offscreen,
                                  surface_data->viewport);

  GHOST_XrPose leftPoseStatic = GHOST_XrGetSpacePose(xr_data->runtime->context, GHOST_SPACE_LEFT_HAND);
  GHOST_XrPose rightPoseStatic = GHOST_XrGetSpacePose(xr_data->runtime->context, GHOST_SPACE_RIGHT_HAND);

  GHOST_XrPose leftPose;
  GHOST_XrPose rightPose;

  wm_xr_session_controller_transform_update(&leftPose,
                                            &draw_data->base_pose,
                                            &leftPoseStatic);

  wm_xr_session_controller_transform_update(&rightPose,
                                            &draw_data->base_pose,
                                            &rightPoseStatic);

  /* The draw-manager uses both GPUOffscreen and GPUViewport to manage frame and texture buffers. A
   * call to GPU_viewport_draw_to_screen() is still needed to get the final result from the
   * viewport buffers composited together and potentially color managed for display on screen.
   * It needs a bound frame-buffer to draw into, for which we simply reuse the GPUOffscreen one.
   *
   * In a next step, Ghost-XR will use the currently bound frame-buffer to retrieve the image
   * to be submitted to the OpenXR swap-chain. So do not un-bind the off-screen yet! */

  GPU_offscreen_bind(surface_data->offscreen, false);
  wm_xr_draw_viewport_buffers_to_active_framebuffer(xr_data->runtime, surface_data, draw_view);

  GPU_matrix_push_projection();
  GPU_matrix_push();
  GPU_matrix_projection_set(winmat);
  GPU_matrix_set(viewmat);

  GPU_blend(true);

  uint pos = GPU_vertformat_attr_add(immVertexFormat(), "pos", GPU_COMP_F32, 3, GPU_FETCH_FLOAT);
  immBindBuiltinProgram(GPU_SHADER_3D_UNIFORM_COLOR);
  immUniformColor4fv((const float[4]){1, 1, 1, 0.5f});

  immBegin(GPU_PRIM_LINES, 2);

  immVertex3f(pos, leftPose.position[0], leftPose.position[1], leftPose.position[2]);
  immVertex3f(
      pos, leftPose.position[0] + 5.0f, leftPose.position[1] + 5.0f, leftPose.position[2] + 5.0f);

  immEnd();

  immBegin(GPU_PRIM_LINES, 2);

  immVertex3f(pos, rightPose.position[0], rightPose.position[1], rightPose.position[2]);
  immVertex3f(pos,
              rightPose.position[0] + 5.0f,
              rightPose.position[1] + 5.0f,
              rightPose.position[2] + 5.0f);

  immEnd();

  immUnbindProgram();
  GPU_blend(false);

  GPU_matrix_pop();
  GPU_matrix_pop_projection();
}
