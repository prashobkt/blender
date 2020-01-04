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
 */

/** \file
 * \ingroup draw
 */
#include "DRW_render.h"

#include "gpencil_engine.h"

extern char datatoc_gpencil_common_lib_glsl[];
extern char datatoc_gpencil_frag_glsl[];
extern char datatoc_gpencil_vert_glsl[];
extern char datatoc_gpencil_antialiasing_frag_glsl[];
extern char datatoc_gpencil_antialiasing_vert_glsl[];
extern char datatoc_gpencil_composite_frag_glsl[];
extern char datatoc_gpencil_layer_blend_frag_glsl[];
extern char datatoc_gpencil_layer_mask_frag_glsl[];
extern char datatoc_gpencil_depth_merge_frag_glsl[];
extern char datatoc_gpencil_depth_merge_vert_glsl[];
extern char datatoc_gpencil_vfx_frag_glsl[];

extern char datatoc_common_colormanagement_lib_glsl[];
extern char datatoc_common_fullscreen_vert_glsl[];
extern char datatoc_common_smaa_lib_glsl[];
extern char datatoc_common_view_lib_glsl[];

struct GPUShader *GPENCIL_shader_antialiasing(GPENCIL_e_data *e_data, int stage)
{
  BLI_assert(stage < 3);

  if (!e_data->antialiasing_sh[stage]) {
    char stage_define[32];
    BLI_snprintf(stage_define, sizeof(stage_define), "#define SMAA_STAGE %d\n", stage);

    e_data->antialiasing_sh[stage] = GPU_shader_create_from_arrays({
        .vert =
            (const char *[]){
                "#define SMAA_INCLUDE_VS 1\n",
                "#define SMAA_INCLUDE_PS 0\n",
                "uniform vec4 viewportMetrics;\n",
                datatoc_common_smaa_lib_glsl,
                datatoc_gpencil_antialiasing_vert_glsl,
                NULL,
            },
        .frag =
            (const char *[]){
                "#define SMAA_INCLUDE_VS 0\n",
                "#define SMAA_INCLUDE_PS 1\n",
                "uniform vec4 viewportMetrics;\n",
                datatoc_common_smaa_lib_glsl,
                datatoc_gpencil_antialiasing_frag_glsl,
                NULL,
            },
        .defs =
            (const char *[]){
                "#define SMAA_GLSL_3\n",
                "#define SMAA_RT_METRICS viewportMetrics\n",
                "#define SMAA_PRESET_HIGH\n",
                "#define SMAA_LUMA_WEIGHT float4(1.0, 1.0, 1.0, 0.0)\n",
                "#define SMAA_NO_DISCARD\n",
                stage_define,
                NULL,
            },
    });
  }
  return e_data->antialiasing_sh[stage];
}

struct GPUShader *GPENCIL_shader_geometry_get(GPENCIL_e_data *e_data)
{
  if (!e_data->gpencil_sh) {
    e_data->gpencil_sh = GPU_shader_create_from_arrays({
        .vert =
            (const char *[]){
                datatoc_common_view_lib_glsl,
                datatoc_gpencil_common_lib_glsl,
                datatoc_gpencil_vert_glsl,
                NULL,
            },
        .frag =
            (const char *[]){
                datatoc_common_colormanagement_lib_glsl,
                datatoc_gpencil_common_lib_glsl,
                datatoc_gpencil_frag_glsl,
                NULL,
            },
        .defs =
            (const char *[]){
                "#define GPENCIL_MATERIAL_BUFFER_LEN " STRINGIFY(GPENCIL_MATERIAL_BUFFER_LEN) "\n",
                "#define GPENCIL_LIGHT_BUFFER_LEN " STRINGIFY(GPENCIL_LIGHT_BUFFER_LEN) "\n",
                NULL,
            },
    });
  }
  return e_data->gpencil_sh;
}

struct GPUShader *GPENCIL_shader_composite_get(GPENCIL_e_data *e_data)
{
  if (!e_data->composite_sh) {
    e_data->composite_sh = DRW_shader_create_fullscreen(datatoc_gpencil_composite_frag_glsl, NULL);
  }
  return e_data->composite_sh;
}

struct GPUShader *GPENCIL_shader_layer_blend_get(GPENCIL_e_data *e_data)
{
  if (!e_data->layer_blend_sh) {
    e_data->layer_blend_sh = GPU_shader_create_from_arrays({
        .vert =
            (const char *[]){
                datatoc_common_fullscreen_vert_glsl,
                NULL,
            },
        .frag =
            (const char *[]){
                datatoc_gpencil_common_lib_glsl,
                datatoc_gpencil_layer_blend_frag_glsl,
                NULL,
            },
    });
  }
  return e_data->layer_blend_sh;
}

struct GPUShader *GPENCIL_shader_layer_mask_get(GPENCIL_e_data *e_data)
{
  if (!e_data->layer_mask_sh) {
    e_data->layer_mask_sh = DRW_shader_create_fullscreen(datatoc_gpencil_layer_mask_frag_glsl,
                                                         NULL);
  }
  return e_data->layer_mask_sh;
}

struct GPUShader *GPENCIL_shader_depth_merge_get(GPENCIL_e_data *e_data)
{
  if (!e_data->depth_merge_sh) {
    e_data->depth_merge_sh = GPU_shader_create_from_arrays({
        .vert =
            (const char *[]){
                datatoc_common_view_lib_glsl,
                datatoc_gpencil_depth_merge_vert_glsl,
                NULL,
            },
        .frag =
            (const char *[]){
                datatoc_gpencil_depth_merge_frag_glsl,
                NULL,
            },
    });
  }
  return e_data->depth_merge_sh;
}

/* ------- FX Shaders --------- */

struct GPUShader *GPENCIL_shader_fx_blur_get(GPENCIL_e_data *e_data)
{
  if (!e_data->fx_blur_sh) {
    e_data->fx_blur_sh = DRW_shader_create_fullscreen(datatoc_gpencil_vfx_frag_glsl,
                                                      "#define BLUR\n");
  }
  return e_data->fx_blur_sh;
}

struct GPUShader *GPENCIL_shader_fx_colorize_get(GPENCIL_e_data *e_data)
{
  if (!e_data->fx_colorize_sh) {
    e_data->fx_colorize_sh = DRW_shader_create_fullscreen(datatoc_gpencil_vfx_frag_glsl,
                                                          "#define COLORIZE\n");
  }
  return e_data->fx_colorize_sh;
}

struct GPUShader *GPENCIL_shader_fx_composite_get(GPENCIL_e_data *e_data)
{
  if (!e_data->fx_composite_sh) {
    e_data->fx_composite_sh = DRW_shader_create_fullscreen(datatoc_gpencil_vfx_frag_glsl,
                                                           "#define COMPOSITE\n");
  }
  return e_data->fx_composite_sh;
}

struct GPUShader *GPENCIL_shader_fx_glow_get(GPENCIL_e_data *e_data)
{
  if (!e_data->fx_glow_sh) {
    e_data->fx_glow_sh = DRW_shader_create_fullscreen(datatoc_gpencil_vfx_frag_glsl,
                                                      "#define GLOW\n");
  }
  return e_data->fx_glow_sh;
}

struct GPUShader *GPENCIL_shader_fx_pixelize_get(GPENCIL_e_data *e_data)
{
  if (!e_data->fx_pixel_sh) {
    e_data->fx_pixel_sh = DRW_shader_create_fullscreen(datatoc_gpencil_vfx_frag_glsl,
                                                       "#define PIXELIZE\n");
  }
  return e_data->fx_pixel_sh;
}

struct GPUShader *GPENCIL_shader_fx_rim_get(GPENCIL_e_data *e_data)
{
  if (!e_data->fx_rim_sh) {
    e_data->fx_rim_sh = GPU_shader_create_from_arrays({
        .vert =
            (const char *[]){
                datatoc_common_fullscreen_vert_glsl,
                NULL,
            },
        .frag =
            (const char *[]){
                datatoc_gpencil_common_lib_glsl,
                datatoc_gpencil_vfx_frag_glsl,
                NULL,
            },
        .defs =
            (const char *[]){
                "#define RIM\n",
                NULL,
            },
    });
  }
  return e_data->fx_rim_sh;
}

struct GPUShader *GPENCIL_shader_fx_shadow_get(GPENCIL_e_data *e_data)
{
  if (!e_data->fx_shadow_sh) {
    e_data->fx_shadow_sh = DRW_shader_create_fullscreen(datatoc_gpencil_vfx_frag_glsl,
                                                        "#define SHADOW\n");
  }
  return e_data->fx_shadow_sh;
}

struct GPUShader *GPENCIL_shader_fx_transform_get(GPENCIL_e_data *e_data)
{
  if (!e_data->fx_transform_sh) {
    e_data->fx_transform_sh = DRW_shader_create_fullscreen(datatoc_gpencil_vfx_frag_glsl,
                                                           "#define TRANSFORM\n");
  }
  return e_data->fx_transform_sh;
}
