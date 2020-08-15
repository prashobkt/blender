#pragma BLENDER_REQUIRE(common_view_lib.glsl)
uniform vec3 offset;

/* ---- Instantiated Attrs ---- */
in vec3 pos;

/* ---- Per instance Attrs ---- */
in vec3 local_pos;

void main()
{
  vec4 position = point_object_to_ndc(pos + local_pos + offset);
  gl_Position = position;
}
