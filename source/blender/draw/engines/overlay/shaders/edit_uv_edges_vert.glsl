#pragma BLENDER_REQUIRE(common_globals_lib.glsl)
#pragma BLENDER_REQUIRE(common_view_lib.glsl)

in vec3 pos;
in vec2 u;
in int flag;

out float selectionFac;
noperspective out vec2 stipplePos;
flat out vec2 stippleStart;

void main()
{
  vec3 world_pos = point_object_to_world(vec3(u, 0.0));
  gl_Position = point_world_to_ndc(world_pos);

  bool is_select = (flag & VERT_UV_SELECT) != 0;
  if (is_select) {
    selectionFac = 1.0;
  }
  else {
    selectionFac = 0.0;
  }

  /* Avoid precision loss. */
  stippleStart = stipplePos = 500.0 + 500.0 * (gl_Position.xy / gl_Position.w);
}
