#pragma BLENDER_REQUIRE(common_view_lib.glsl)

in vec3 pos;

out vec2 uvs;
flat out vec4 origin;

void main()
{
  vec3 world_pos = point_object_to_world(pos);
  vec4 position = point_world_to_ndc(world_pos);
  origin = point_world_to_ndc(vec3(0.0));
  origin.z = 0.0;

  /* Move drawn pixels to the front. In the overlay engine the depth is used
   * to detect if a transparency texture or the background color should be drawn. */
  position.z = 0.0;
  gl_Position = position;

  uvs = pos.xy;
}
