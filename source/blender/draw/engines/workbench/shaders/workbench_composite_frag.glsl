
#pragma BLENDER_REQUIRE(common_view_lib.glsl)
#pragma BLENDER_REQUIRE(workbench_common_lib.glsl)
#pragma BLENDER_REQUIRE(workbench_world_light_lib.glsl)

uniform sampler2D materialBuffer;
uniform sampler2D normalBuffer;

in vec4 uvcoordsvar;

out vec4 fragColor;

void main()
{
  vec3 normal = workbench_normal_decode(texture(normalBuffer, uvcoordsvar.st));
  vec4 mat_data = texture(materialBuffer, uvcoordsvar.st);

  vec3 I_vs = view_vector_from_screen_uv(uvcoordsvar.st, world_data.viewvecs, ProjectionMatrix);

  vec3 base_color = mat_data.rgb;

  float roughness, metallic;
  workbench_float_pair_decode(mat_data.a, roughness, metallic);

  vec3 specular_color = mix(vec3(0.05), base_color.rgb, metallic);
  vec3 diffuse_color = mix(base_color.rgb, vec3(0.0), metallic);

#ifdef V3D_LIGHTING_MATCAP
  fragColor.rgb = vec3(1.0);
#endif

#ifdef V3D_LIGHTING_STUDIO
  fragColor.rgb = get_world_lighting(diffuse_color, specular_color, roughness, normal, I_vs);
#endif

#ifdef V3D_LIGHTING_FLAT
  fragColor.rgb = base_color.rgb;
#endif

  fragColor.a = 1.0;
}
