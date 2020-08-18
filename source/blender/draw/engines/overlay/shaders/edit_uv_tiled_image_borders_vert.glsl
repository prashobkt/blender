#pragma BLENDER_REQUIRE(common_view_lib.glsl)
uniform vec3 offset;

in vec3 pos;

void main()
{
  vec4 position = point_object_to_ndc(pos + offset);
  gl_Position = position;
}
