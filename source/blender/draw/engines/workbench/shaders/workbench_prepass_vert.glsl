
#pragma BLENDER_REQUIRE(common_view_lib.glsl)
#pragma BLENDER_REQUIRE(workbench_common_lib.glsl)
#pragma BLENDER_REQUIRE(workbench_material_lib.glsl)
#pragma BLENDER_REQUIRE(workbench_image_lib.glsl)

IN_OUT ShaderStageInterface
{
  vec3 normal_interp;
  vec3 color_interp;
  vec2 uv_interp;
  flat float packed_rough_metal;
  flat int object_id;
};

#ifdef GPU_VERTEX_SHADER

in vec3 pos;
in vec3 nor;
in vec4 ac; /* active color */
in vec2 au; /* active texture layer */

void main()
{
  vec3 world_pos = point_object_to_world(pos);
  gl_Position = point_world_to_ndc(world_pos);

#  ifdef USE_WORLD_CLIP_PLANES
  world_clip_planes_calc_clip_distance(world_pos);
#  endif

  uv_interp = au;

  normal_interp = normalize(normal_object_to_view(nor));

  float alpha, metallic, roughness;
  workbench_material_data_get(resource_handle, color_interp, alpha, roughness, metallic);

  if (materialIndex == 0) {
    color_interp = ac.rgb;
  }

  packed_rough_metal = workbench_float_pair_encode(roughness, metallic);

  object_id = int((uint(resource_id) + 1u) & 0xFFu);
}

#else

layout(location = 0) out vec4 materialData;
layout(location = 1) out WB_Normal normalData;
layout(location = 2) out uint objectId;

uniform bool useMatcap = false;

void main()
{
  normalData = workbench_normal_encode(gl_FrontFacing, normal_interp);

  materialData = vec4(color_interp, packed_rough_metal);

  objectId = uint(object_id);

  if (useMatcap) {
    /* For matcaps, save front facing in alpha channel. */
    materialData.a = float(gl_FrontFacing);
  }

#  ifdef V3D_SHADING_TEXTURE_COLOR
  materialData.rgb = workbench_image_color(uv_interp);
#  endif
}

#endif
