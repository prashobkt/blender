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

#include "DRW_render.h"

#include "BLI_dynstr.h"

#include "GPU_batch.h"

#include "overlay2d_engine.h"
#include "overlay2d_private.h"

extern char datatoc_common_globals_lib_glsl[];
extern char datatoc_common_overlay2d_lib_glsl[];
extern char datatoc_common_view_lib_glsl[];
extern char datatoc_gpu_shader_colorspace_lib_glsl[];
extern char datatoc_gpu_shader_flat_color_frag_glsl[];
extern char datatoc_gpu_shader_uniform_color_frag_glsl[];
extern char datatoc_gpu_shader_3D_smooth_color_frag_glsl[];

extern char datatoc_uv_faces_vert_glsl[];
extern char datatoc_uv_face_dots_vert_glsl[];
extern char datatoc_uv_verts_vert_glsl[];
extern char datatoc_uv_verts_frag_glsl[];
extern char datatoc_overlay2d_background_frag_glsl[];
extern char datatoc_overlay2d_wireframe_frag_glsl[];
extern char datatoc_overlay2d_wireframe_geom_glsl[];
extern char datatoc_overlay2d_wireframe_vert_glsl[];
extern char datatoc_overlay2d_uv_stretching_vert_glsl[];
extern char datatoc_overlay2d_tiled_image_border_vert_glsl[];

typedef struct OVERLAY2D_Shaders {
  GPUShader *background_sh;
  GPUShader *uv_face_sh;
  GPUShader *uv_face_dots_sh;
  GPUShader *uv_verts_sh;
  GPUShader *uv_stretching_angle_sh;
  GPUShader *uv_stretching_area_sh;
  GPUShader *wireframe_sh;
  GPUShader *tiled_image_border_sh;
} OVERLAY2D_Shaders;

static struct {
  OVERLAY2D_Shaders shaders;
  DRWShaderLibrary *lib;
} e_data = {NULL}; /* Engine data */

void OVERLAY2D_shader_library_ensure(void)
{
  if (e_data.lib == NULL) {
    e_data.lib = DRW_shader_library_create();
    /* NOTE: Theses needs to be ordered by dependencies. */
    DRW_SHADER_LIB_ADD(e_data.lib, common_globals_lib);
    DRW_SHADER_LIB_ADD(e_data.lib, common_overlay2d_lib);
    DRW_SHADER_LIB_ADD(e_data.lib, common_view_lib);
    DRW_SHADER_LIB_ADD(e_data.lib, gpu_shader_colorspace_lib);
  }
}

GPUShader *OVERLAY2D_shaders_wireframe_get(void)
{
  OVERLAY2D_Shaders *sh_data = &e_data.shaders;
  if (!sh_data->wireframe_sh) {
    sh_data->wireframe_sh = DRW_shader_create_with_shaderlib(datatoc_overlay2d_wireframe_vert_glsl,
                                                             datatoc_overlay2d_wireframe_geom_glsl,
                                                             datatoc_overlay2d_wireframe_frag_glsl,
                                                             e_data.lib,
                                                             NULL);
  }
  return sh_data->wireframe_sh;
}

GPUShader *OVERLAY2D_shaders_uv_face_get(void)
{
  OVERLAY2D_Shaders *sh_data = &e_data.shaders;
  if (!sh_data->uv_face_sh) {
    sh_data->uv_face_sh = DRW_shader_create_with_shaderlib(
        datatoc_uv_faces_vert_glsl,
        NULL,
        datatoc_gpu_shader_flat_color_frag_glsl,
        e_data.lib,
        "#define blender_srgb_to_framebuffer_space(a) a\n");
  }
  return sh_data->uv_face_sh;
}

GPUShader *OVERLAY2D_shaders_uv_face_dots_get(void)
{
  OVERLAY2D_Shaders *sh_data = &e_data.shaders;
  if (!sh_data->uv_face_dots_sh) {
    sh_data->uv_face_dots_sh = DRW_shader_create_with_shaderlib(
        datatoc_uv_face_dots_vert_glsl,
        NULL,
        datatoc_gpu_shader_flat_color_frag_glsl,
        e_data.lib,
        "#define blender_srgb_to_framebuffer_space(a) a\n");
  }
  return sh_data->uv_face_dots_sh;
}

GPUShader *OVERLAY2D_shaders_uv_verts_get(void)
{
  OVERLAY2D_Shaders *sh_data = &e_data.shaders;
  if (!sh_data->uv_verts_sh) {
    sh_data->uv_verts_sh = DRW_shader_create_with_shaderlib(
        datatoc_uv_verts_vert_glsl, NULL, datatoc_uv_verts_frag_glsl, e_data.lib, NULL);
  }

  return sh_data->uv_verts_sh;
}

GPUShader *OVERLAY2D_shaders_uv_stretching_area_get(void)
{
  OVERLAY2D_Shaders *sh_data = &e_data.shaders;
  if (!sh_data->uv_stretching_area_sh) {
    sh_data->uv_stretching_area_sh = DRW_shader_create_with_shaderlib(
        datatoc_overlay2d_uv_stretching_vert_glsl,
        NULL,
        datatoc_gpu_shader_3D_smooth_color_frag_glsl,
        e_data.lib,
        "#define blender_srgb_to_framebuffer_space(a) a\n");
  }

  return sh_data->uv_stretching_area_sh;
}

GPUShader *OVERLAY2D_shaders_uv_stretching_angle_get(void)
{
  OVERLAY2D_Shaders *sh_data = &e_data.shaders;
  if (!sh_data->uv_stretching_angle_sh) {
    sh_data->uv_stretching_angle_sh = DRW_shader_create_with_shaderlib(
        datatoc_overlay2d_uv_stretching_vert_glsl,
        NULL,
        datatoc_gpu_shader_3D_smooth_color_frag_glsl,
        e_data.lib,
        "#define blender_srgb_to_framebuffer_space(a) a\n#define STRETCH_ANGLE\n");
  }

  return sh_data->uv_stretching_angle_sh;
}

GPUShader *OVERLAY2D_shaders_background_get(void)
{
  OVERLAY2D_Shaders *sh_data = &e_data.shaders;
  if (!sh_data->background_sh) {
    sh_data->background_sh = DRW_shader_create_fullscreen_with_shaderlib(
        datatoc_overlay2d_background_frag_glsl, e_data.lib, NULL);
  }
  return sh_data->background_sh;
}

GPUShader *OVERLAY2D_shaders_tiled_image_border_get(void)
{
  OVERLAY2D_Shaders *sh_data = &e_data.shaders;
  if (!sh_data->tiled_image_border_sh) {
    sh_data->tiled_image_border_sh = DRW_shader_create_with_shaderlib(
        datatoc_overlay2d_tiled_image_border_vert_glsl,
        NULL,
        datatoc_gpu_shader_uniform_color_frag_glsl,
        e_data.lib,
        "#define INSTANCED_ATTR\n#define blender_srgb_to_framebuffer_space(a) a\n");
  }
  return sh_data->tiled_image_border_sh;
}

void OVERLAY2D_shaders_free(void)
{
  GPUShader **sh_data_as_array = (GPUShader **)&e_data.shaders;
  for (int i = 0; i < (sizeof(OVERLAY2D_Shaders) / sizeof(GPUShader *)); i++) {
    DRW_SHADER_FREE_SAFE(sh_data_as_array[i]);
  }
  DRW_SHADER_LIB_FREE_SAFE(e_data.lib);
}