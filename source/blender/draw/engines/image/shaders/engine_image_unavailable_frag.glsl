#pragma BLENDER_REQUIRE(common_colormanagement_lib.glsl)
#pragma BLENDER_REQUIRE(common_globals_lib.glsl)

uniform float zoomLevel;
uniform float zoomScale;
uniform ivec2 imageSize;

in vec2 uvs;
out vec4 fragColor;

void main()
{
  ivec2 d = ivec2(imageSize * zoomScale);
  ivec2 tex_coord = ivec2(uvs * d);
  ivec2 tex_coord_prev = tex_coord - ivec2(1);

  int zoom_level_1 = int(zoomLevel);
  int zoom_level_2 = zoom_level_1 + 1;
  float line_1_alpha = 1.0 - fract(zoomLevel);
  float line_2_alpha = fract(zoomLevel);

  int num_lines_in_level2 = (1 << zoom_level_2) * (1 << zoom_level_2);
  float spacing_between_lines = max(d.x, d.y) / num_lines_in_level2;

  ivec2 line_index = ivec2(tex_coord * num_lines_in_level2 / d);
  ivec2 line_index_prev = ivec2(tex_coord_prev * num_lines_in_level2 / d);
  bvec2 is_line_2 = notEqual(line_index, line_index_prev);
  bvec2 is_line_1 = notEqual(line_index / 4, line_index_prev / 4);

  float line_alpha = max(any(is_line_2) ? line_2_alpha : 0.0, any(is_line_1) ? line_1_alpha : 0.0);

  float color_offset = 20.0 / 256.0;
  vec3 line_color = clamp(colorBackground.rgb - color_offset, 0.0, 1.0);
  vec3 bg_color = clamp(colorBackground.rgb + color_offset, 0.0, 1.0);
  vec3 final_color = mix(bg_color, line_color, line_alpha);
  fragColor = vec4(final_color, 1.0);
}
