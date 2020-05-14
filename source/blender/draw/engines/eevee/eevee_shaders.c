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
 * Copyright 2016, Blender Foundation.
 */

/** \file
 * \ingroup draw_engine
 */

#include "DRW_render.h"

#include "BKE_lib_id.h"
#include "BKE_node.h"

#include "BLI_string_utils.h"

#include "MEM_guardedalloc.h"

#include "GPU_material.h"
#include "GPU_shader.h"

#include "NOD_shader.h"

#include "eevee_private.h"

static const char *filter_defines = "#define HAMMERSLEY_SIZE " STRINGIFY(HAMMERSLEY_SIZE) "\n"
#if defined(IRRADIANCE_SH_L2)
                               "#define IRRADIANCE_SH_L2\n"
#elif defined(IRRADIANCE_CUBEMAP)
                               "#define IRRADIANCE_CUBEMAP\n"
#elif defined(IRRADIANCE_HL2)
                               "#define IRRADIANCE_HL2\n"
#endif
                               "#define NOISE_SIZE 64\n";

static struct {
  /* Probes */
  struct GPUShader *probe_default_sh;
  struct GPUShader *probe_default_studiolight_sh;
  struct GPUShader *probe_background_studiolight_sh;
  struct GPUShader *probe_grid_display_sh;
  struct GPUShader *probe_cube_display_sh;
  struct GPUShader *probe_planar_display_sh;
  struct GPUShader *probe_filter_glossy_sh;
  struct GPUShader *probe_filter_diffuse_sh;
  struct GPUShader *probe_filter_visibility_sh;
  struct GPUShader *probe_grid_fill_sh;
  struct GPUShader *probe_planar_downsample_sh;

  /* Velocity Resolve */
  struct GPUShader *velocity_resolve_sh;

  /* Temporal Anti Aliasing */
  struct GPUShader *taa_resolve_sh;
  struct GPUShader *taa_resolve_reproject_sh;

  /* LookDev Materials */
  Material *glossy_mat;
  Material *diffuse_mat;

  Material *error_mat;

  /* Default Material */
  struct {
    bNodeTree *ntree;
    bNodeSocketValueRGBA *color_socket;
    bNodeSocketValueFloat *metallic_socket;
    bNodeSocketValueFloat *roughness_socket;
    bNodeSocketValueFloat *specular_socket;
  } surface;

  struct {
    bNodeTree *ntree;
    bNodeSocketValueRGBA *color_socket;
  } world;
} e_data = {NULL}; /* Engine data */

extern char datatoc_bsdf_common_lib_glsl[];
extern char datatoc_bsdf_sampling_lib_glsl[];
extern char datatoc_common_uniforms_lib_glsl[];
extern char datatoc_common_view_lib_glsl[];

extern char datatoc_background_vert_glsl[];
extern char datatoc_default_world_frag_glsl[];
extern char datatoc_lightprobe_geom_glsl[];
extern char datatoc_lightprobe_vert_glsl[];
extern char datatoc_lightprobe_cube_display_frag_glsl[];
extern char datatoc_lightprobe_cube_display_vert_glsl[];
extern char datatoc_lightprobe_filter_diffuse_frag_glsl[];
extern char datatoc_lightprobe_filter_glossy_frag_glsl[];
extern char datatoc_lightprobe_filter_visibility_frag_glsl[];
extern char datatoc_lightprobe_grid_display_frag_glsl[];
extern char datatoc_lightprobe_grid_display_vert_glsl[];
extern char datatoc_lightprobe_grid_fill_frag_glsl[];
extern char datatoc_lightprobe_planar_display_frag_glsl[];
extern char datatoc_lightprobe_planar_display_vert_glsl[];
extern char datatoc_lightprobe_planar_downsample_frag_glsl[];
extern char datatoc_lightprobe_planar_downsample_geom_glsl[];
extern char datatoc_lightprobe_planar_downsample_vert_glsl[];
extern char datatoc_irradiance_lib_glsl[];
extern char datatoc_lightprobe_lib_glsl[];
extern char datatoc_octahedron_lib_glsl[];
extern char datatoc_cubemap_lib_glsl[];

/* Velocity Resolve */
extern char datatoc_effect_velocity_resolve_frag_glsl[];

/* Temporal Sampling */
extern char datatoc_effect_temporal_aa_glsl[];

/* *********** FUNCTIONS *********** */

void EEVEE_shaders_lightprobe_shaders_init(void)
{
  BLI_assert(e_data.probe_filter_glossy_sh == NULL);
  char *shader_str = NULL;

  shader_str = BLI_string_joinN(datatoc_common_view_lib_glsl,
                                datatoc_common_uniforms_lib_glsl,
                                datatoc_bsdf_common_lib_glsl,
                                datatoc_bsdf_sampling_lib_glsl,
                                datatoc_lightprobe_filter_glossy_frag_glsl);

  e_data.probe_filter_glossy_sh = DRW_shader_create(
      datatoc_lightprobe_vert_glsl, datatoc_lightprobe_geom_glsl, shader_str, filter_defines);

  e_data.probe_default_sh = DRW_shader_create_with_lib(datatoc_background_vert_glsl,
                                                       NULL,
                                                       datatoc_default_world_frag_glsl,
                                                       datatoc_common_view_lib_glsl,
                                                       NULL);

  MEM_freeN(shader_str);

  shader_str = BLI_string_joinN(datatoc_common_view_lib_glsl,
                                datatoc_common_uniforms_lib_glsl,
                                datatoc_bsdf_common_lib_glsl,
                                datatoc_bsdf_sampling_lib_glsl,
                                datatoc_lightprobe_filter_diffuse_frag_glsl);

  e_data.probe_filter_diffuse_sh = DRW_shader_create_fullscreen(shader_str, filter_defines);

  MEM_freeN(shader_str);

  shader_str = BLI_string_joinN(datatoc_common_view_lib_glsl,
                                datatoc_common_uniforms_lib_glsl,
                                datatoc_bsdf_common_lib_glsl,
                                datatoc_bsdf_sampling_lib_glsl,
                                datatoc_lightprobe_filter_visibility_frag_glsl);

  e_data.probe_filter_visibility_sh = DRW_shader_create_fullscreen(shader_str, filter_defines);

  MEM_freeN(shader_str);

  e_data.probe_grid_fill_sh = DRW_shader_create_fullscreen(datatoc_lightprobe_grid_fill_frag_glsl,
                                                           filter_defines);

  e_data.probe_planar_downsample_sh = DRW_shader_create(
      datatoc_lightprobe_planar_downsample_vert_glsl,
      datatoc_lightprobe_planar_downsample_geom_glsl,
      datatoc_lightprobe_planar_downsample_frag_glsl,
      NULL);
}

GPUShader *EEVEE_shaders_probe_filter_glossy_sh_get(void)
{
  return e_data.probe_filter_glossy_sh;
}

GPUShader *EEVEE_shaders_probe_default_sh_get(void)
{
  return e_data.probe_default_sh;
}

GPUShader *EEVEE_shaders_probe_filter_diffuse_sh_get(void)
{
  return e_data.probe_filter_diffuse_sh;
}

GPUShader *EEVEE_shaders_probe_filter_visibility_sh_get(void)
{
  return e_data.probe_filter_visibility_sh;
}

GPUShader *EEVEE_shaders_probe_grid_fill_sh_get(void)
{
  return e_data.probe_grid_fill_sh;
}

GPUShader *EEVEE_shaders_probe_planar_downsample_sh_get(void)
{
  return e_data.probe_planar_downsample_sh;
}

GPUShader *EEVEE_shaders_default_studiolight_sh_get(void)
{
  if (e_data.probe_default_studiolight_sh == NULL) {
    e_data.probe_default_studiolight_sh = DRW_shader_create_with_lib(
        datatoc_background_vert_glsl,
        NULL,
        datatoc_default_world_frag_glsl,
        datatoc_common_view_lib_glsl,
        "#define LOOKDEV\n");
  }
  return e_data.probe_default_studiolight_sh;
}

GPUShader *EEVEE_shaders_background_studiolight_sh_get(void)
{
  if (e_data.probe_background_studiolight_sh == NULL) {
    char *frag_str = BLI_string_joinN(datatoc_octahedron_lib_glsl,
                                      datatoc_cubemap_lib_glsl,
                                      datatoc_common_uniforms_lib_glsl,
                                      datatoc_bsdf_common_lib_glsl,
                                      datatoc_lightprobe_lib_glsl,
                                      datatoc_default_world_frag_glsl);

    e_data.probe_background_studiolight_sh = DRW_shader_create_with_lib(
        datatoc_background_vert_glsl,
        NULL,
        frag_str,
        datatoc_common_view_lib_glsl,
        "#define LOOKDEV_BG\n" SHADER_DEFINES);

    MEM_freeN(frag_str);
  }
  return e_data.probe_background_studiolight_sh;
}

GPUShader *EEVEE_shaders_probe_cube_display_sh_get(void)
{
  if (e_data.probe_cube_display_sh == NULL) {
    char *shader_str = BLI_string_joinN(datatoc_octahedron_lib_glsl,
                                        datatoc_cubemap_lib_glsl,
                                        datatoc_common_view_lib_glsl,
                                        datatoc_common_uniforms_lib_glsl,
                                        datatoc_bsdf_common_lib_glsl,
                                        datatoc_lightprobe_lib_glsl,
                                        datatoc_lightprobe_cube_display_frag_glsl);

    char *vert_str = BLI_string_joinN(datatoc_common_view_lib_glsl,
                                      datatoc_lightprobe_cube_display_vert_glsl);

    e_data.probe_cube_display_sh = DRW_shader_create(vert_str, NULL, shader_str, SHADER_DEFINES);

    MEM_freeN(vert_str);
    MEM_freeN(shader_str);
  }
  return e_data.probe_cube_display_sh;
}

GPUShader *EEVEE_shaders_probe_grid_display_sh_get(void)
{
  if (e_data.probe_grid_display_sh == NULL) {
    char *shader_str = BLI_string_joinN(datatoc_octahedron_lib_glsl,
                                        datatoc_cubemap_lib_glsl,
                                        datatoc_common_view_lib_glsl,
                                        datatoc_common_uniforms_lib_glsl,
                                        datatoc_bsdf_common_lib_glsl,
                                        datatoc_irradiance_lib_glsl,
                                        datatoc_lightprobe_lib_glsl,
                                        datatoc_lightprobe_grid_display_frag_glsl);

    char *vert_str = BLI_string_joinN(datatoc_common_view_lib_glsl,
                                      datatoc_lightprobe_grid_display_vert_glsl);

    e_data.probe_grid_display_sh = DRW_shader_create(vert_str, NULL, shader_str, filter_defines);

    MEM_freeN(vert_str);
    MEM_freeN(shader_str);
  }
  return e_data.probe_grid_display_sh;
}

GPUShader *EEVEE_shaders_probe_planar_display_sh_get(void)
{
  if (e_data.probe_planar_display_sh == NULL) {
    char *vert_str = BLI_string_joinN(datatoc_common_view_lib_glsl,
                                      datatoc_lightprobe_planar_display_vert_glsl);

    char *shader_str = BLI_string_joinN(datatoc_common_view_lib_glsl,
                                        datatoc_lightprobe_planar_display_frag_glsl);

    e_data.probe_planar_display_sh = DRW_shader_create(vert_str, NULL, shader_str, NULL);

    MEM_freeN(vert_str);
    MEM_freeN(shader_str);
  }
  return e_data.probe_planar_display_sh;
}

GPUShader *EEVEE_shaders_velocity_resolve_sh_get(void)
{
  if (e_data.velocity_resolve_sh == NULL) {
    char *frag_str = BLI_string_joinN(datatoc_common_uniforms_lib_glsl,
                                      datatoc_common_view_lib_glsl,
                                      datatoc_bsdf_common_lib_glsl,
                                      datatoc_effect_velocity_resolve_frag_glsl);

    e_data.velocity_resolve_sh = DRW_shader_create_fullscreen(frag_str, NULL);

    MEM_freeN(frag_str);
  }
  return e_data.velocity_resolve_sh;
}

GPUShader *EEVEE_shaders_taa_resolve_sh_get(EEVEE_EffectsFlag enabled_effects)
{
  GPUShader **sh;
  const char *define = NULL;
  if (enabled_effects & EFFECT_TAA_REPROJECT) {
    sh = &e_data.taa_resolve_reproject_sh;
    define = "#define USE_REPROJECTION\n";
  }
  else {
    sh = &e_data.taa_resolve_sh;
  }
  if (*sh == NULL) {
    char *frag_str = BLI_string_joinN(datatoc_common_uniforms_lib_glsl,
                                      datatoc_common_view_lib_glsl,
                                      datatoc_bsdf_common_lib_glsl,
                                      datatoc_effect_temporal_aa_glsl);

    *sh = DRW_shader_create_fullscreen(frag_str, define);
    MEM_freeN(frag_str);
  }

  return *sh;
}

Material *EEVEE_material_default_diffuse_get(void)
{
  if (!e_data.diffuse_mat) {
    Material *ma = BKE_id_new_nomain(ID_MA, "EEVEEE default diffuse");

    bNodeTree *ntree = ntreeAddTree(NULL, "Shader Nodetree", ntreeType_Shader->idname);
    ma->nodetree = ntree;
    ma->use_nodes = true;

    bNode *bsdf = nodeAddStaticNode(NULL, ntree, SH_NODE_BSDF_DIFFUSE);
    bNodeSocket *base_color = nodeFindSocket(bsdf, SOCK_IN, "Color");
    copy_v3_fl(((bNodeSocketValueRGBA *)base_color->default_value)->value, 0.8f);

    bNode *output = nodeAddStaticNode(NULL, ntree, SH_NODE_OUTPUT_MATERIAL);

    nodeAddLink(ntree,
                bsdf,
                nodeFindSocket(bsdf, SOCK_OUT, "BSDF"),
                output,
                nodeFindSocket(output, SOCK_IN, "Surface"));

    nodeSetActive(ntree, output);
    e_data.diffuse_mat = ma;
  }
  return e_data.diffuse_mat;
}

Material *EEVEE_material_default_glossy_get(void)
{
  if (!e_data.glossy_mat) {
    Material *ma = BKE_id_new_nomain(ID_MA, "EEVEEE default metal");

    bNodeTree *ntree = ntreeAddTree(NULL, "Shader Nodetree", ntreeType_Shader->idname);
    ma->nodetree = ntree;
    ma->use_nodes = true;

    bNode *bsdf = nodeAddStaticNode(NULL, ntree, SH_NODE_BSDF_GLOSSY);
    bNodeSocket *base_color = nodeFindSocket(bsdf, SOCK_IN, "Color");
    copy_v3_fl(((bNodeSocketValueRGBA *)base_color->default_value)->value, 1.0f);
    bNodeSocket *roughness = nodeFindSocket(bsdf, SOCK_IN, "Roughness");
    ((bNodeSocketValueFloat *)roughness->default_value)->value = 0.0f;

    bNode *output = nodeAddStaticNode(NULL, ntree, SH_NODE_OUTPUT_MATERIAL);

    nodeAddLink(ntree,
                bsdf,
                nodeFindSocket(bsdf, SOCK_OUT, "BSDF"),
                output,
                nodeFindSocket(output, SOCK_IN, "Surface"));

    nodeSetActive(ntree, output);
    e_data.glossy_mat = ma;
  }
  return e_data.glossy_mat;
}

Material *EEVEE_material_default_error_get(void)
{
  if (!e_data.error_mat) {
    Material *ma = BKE_id_new_nomain(ID_MA, "EEVEEE default metal");

    bNodeTree *ntree = ntreeAddTree(NULL, "Shader Nodetree", ntreeType_Shader->idname);
    ma->nodetree = ntree;
    ma->use_nodes = true;

    /* Use emission and output material to be compatible with both World and Material. */
    bNode *bsdf = nodeAddStaticNode(NULL, ntree, SH_NODE_EMISSION);
    bNodeSocket *color = nodeFindSocket(bsdf, SOCK_IN, "Color");
    copy_v3_fl3(((bNodeSocketValueRGBA *)color->default_value)->value, 1.0f, 0.0f, 1.0f);

    bNode *output = nodeAddStaticNode(NULL, ntree, SH_NODE_OUTPUT_MATERIAL);

    nodeAddLink(ntree,
                bsdf,
                nodeFindSocket(bsdf, SOCK_OUT, "Emission"),
                output,
                nodeFindSocket(output, SOCK_IN, "Surface"));

    nodeSetActive(ntree, output);
    e_data.error_mat = ma;
  }
  return e_data.error_mat;
}

/* Configure a default nodetree with the given material.  */
struct bNodeTree *EEVEE_shader_default_surface_nodetree(Material *ma)
{
  /* WARNING: This function is not threadsafe. Which is not a problem for the moment. */
  if (!e_data.surface.ntree) {
    bNodeTree *ntree = ntreeAddTree(NULL, "Shader Nodetree", ntreeType_Shader->idname);
    bNode *bsdf = nodeAddStaticNode(NULL, ntree, SH_NODE_BSDF_PRINCIPLED);
    bNode *output = nodeAddStaticNode(NULL, ntree, SH_NODE_OUTPUT_MATERIAL);
    bNodeSocket *bsdf_out = nodeFindSocket(bsdf, SOCK_OUT, "BSDF");
    bNodeSocket *output_in = nodeFindSocket(output, SOCK_IN, "Surface");
    nodeAddLink(ntree, bsdf, bsdf_out, output, output_in);
    nodeSetActive(ntree, output);

    e_data.surface.color_socket = nodeFindSocket(bsdf, SOCK_IN, "Base Color")->default_value;
    e_data.surface.metallic_socket = nodeFindSocket(bsdf, SOCK_IN, "Metallic")->default_value;
    e_data.surface.roughness_socket = nodeFindSocket(bsdf, SOCK_IN, "Roughness")->default_value;
    e_data.surface.specular_socket = nodeFindSocket(bsdf, SOCK_IN, "Specular")->default_value;
    e_data.surface.ntree = ntree;
  }
  /* Update */
  copy_v3_fl3(e_data.surface.color_socket->value, ma->r, ma->g, ma->b);
  e_data.surface.metallic_socket->value = ma->metallic;
  e_data.surface.roughness_socket->value = ma->roughness;
  e_data.surface.specular_socket->value = ma->spec;

  return e_data.surface.ntree;
}

/* Configure a default nodetree with the given world.  */
struct bNodeTree *EEVEE_shader_default_world_nodetree(World *wo)
{
  /* WARNING: This function is not threadsafe. Which is not a problem for the moment. */
  if (!e_data.world.ntree) {
    bNodeTree *ntree = ntreeAddTree(NULL, "Shader Nodetree", ntreeType_Shader->idname);
    bNode *bg = nodeAddStaticNode(NULL, ntree, SH_NODE_BACKGROUND);
    bNode *output = nodeAddStaticNode(NULL, ntree, SH_NODE_OUTPUT_WORLD);
    bNodeSocket *bg_out = nodeFindSocket(bg, SOCK_OUT, "Background");
    bNodeSocket *output_in = nodeFindSocket(output, SOCK_IN, "Surface");
    nodeAddLink(ntree, bg, bg_out, output, output_in);
    nodeSetActive(ntree, output);

    e_data.world.color_socket = nodeFindSocket(bg, SOCK_IN, "Color")->default_value;
    e_data.world.ntree = ntree;
  }

  copy_v3_fl3(e_data.world.color_socket->value, wo->horr, wo->horg, wo->horb);

  return e_data.world.ntree;
}

void EEVEE_shaders_free(void)
{
  DRW_SHADER_FREE_SAFE(e_data.probe_default_sh);
  DRW_SHADER_FREE_SAFE(e_data.probe_filter_glossy_sh);
  DRW_SHADER_FREE_SAFE(e_data.probe_filter_diffuse_sh);
  DRW_SHADER_FREE_SAFE(e_data.probe_filter_visibility_sh);
  DRW_SHADER_FREE_SAFE(e_data.probe_grid_fill_sh);
  DRW_SHADER_FREE_SAFE(e_data.probe_planar_downsample_sh);
  DRW_SHADER_FREE_SAFE(e_data.probe_default_studiolight_sh);
  DRW_SHADER_FREE_SAFE(e_data.probe_background_studiolight_sh);
  DRW_SHADER_FREE_SAFE(e_data.probe_grid_display_sh);
  DRW_SHADER_FREE_SAFE(e_data.probe_cube_display_sh);
  DRW_SHADER_FREE_SAFE(e_data.probe_planar_display_sh);
  DRW_SHADER_FREE_SAFE(e_data.velocity_resolve_sh);
  DRW_SHADER_FREE_SAFE(e_data.taa_resolve_sh);
  DRW_SHADER_FREE_SAFE(e_data.taa_resolve_reproject_sh);

  if (e_data.glossy_mat) {
    BKE_id_free(NULL, e_data.glossy_mat);
    e_data.glossy_mat = NULL;
  }
  if (e_data.diffuse_mat) {
    BKE_id_free(NULL, e_data.diffuse_mat);
    e_data.diffuse_mat = NULL;
  }
  if (e_data.error_mat) {
    BKE_id_free(NULL, e_data.error_mat);
    e_data.error_mat = NULL;
  }
  if (e_data.surface.ntree) {
    ntreeFreeEmbeddedTree(e_data.surface.ntree);
    MEM_freeN(e_data.surface.ntree);
    e_data.surface.ntree = NULL;
  }
  if (e_data.world.ntree) {
    ntreeFreeEmbeddedTree(e_data.world.ntree);
    MEM_freeN(e_data.world.ntree);
    e_data.world.ntree = NULL;
  }
}
