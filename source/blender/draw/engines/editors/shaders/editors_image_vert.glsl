#pragma BLENDER_REQUIRE(common_view_lib.glsl)

#define SIMA_DRAW_FLAG_TILED (1 << 4)
uniform int drawFlags;

/* ---- Instantiated Attrs ---- */
in vec3 pos;

/* ---- Per instance Attrs ---- */
in vec3 local_pos;

out vec2 uvs;

void main()
{
  vec3 world_pos = point_object_to_world(pos + local_pos);
  vec4 position = point_world_to_ndc(world_pos);

  /* Move drawn pixels to the front. In the overlay engine the depth is used
   * to detect if a transparency texture or the background color should be drawn. */
  position.z = 0.0;
  gl_Position = position;

  uvs = pos.xy + (local_pos.xy * vec2((drawFlags & SIMA_DRAW_FLAG_TILED) != 0));
}
