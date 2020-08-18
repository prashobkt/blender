#pragma BLENDER_REQUIRE(common_globals_lib.glsl)
#pragma BLENDER_REQUIRE(common_view_lib.glsl)

uniform float pointSize;
uniform float outlineWidth;

in vec2 u;
in int flag;

out vec4 fillColor;
out vec4 outlineColor;
out vec4 radii;

/* TODO Theme? */
const vec4 pinned_col = vec4(1.0, 0.0, 0.0, 1.0);

void main()
{
  vec3 world_pos = point_object_to_world(vec3(u, 0.0));
  gl_Position = point_world_to_ndc(world_pos);

  gl_PointSize = pointSize;

  bool is_selected = (flag & (VERT_UV_SELECT | FACE_UV_SELECT)) != 0;
  bool is_pinned = (flag & VERT_UV_PINNED) != 0;
  vec4 deselect_col = (is_pinned) ? pinned_col : vec4(colorWire.rgb, 1.0);
  fillColor = (is_selected) ? colorVertexSelect : deselect_col;
  outlineColor = (is_pinned) ? pinned_col : vec4(fillColor.rgb, 0.0);

  // calculate concentric radii in pixels
  float radius = 0.5 * pointSize;

  // start at the outside and progress toward the center
  radii[0] = radius;
  radii[1] = radius - 1.0;
  radii[2] = radius - outlineWidth;
  radii[3] = radius - outlineWidth - 1.0;

  // convert to PointCoord units
  radii /= pointSize;
}
