#pragma BLENDER_REQUIRE(common_colormanagement_lib.glsl)
#pragma BLENDER_REQUIRE(common_globals_lib.glsl)

flat in vec4 origin;
in vec2 uvs;

out vec4 fragColor;

/* XXX(jbakker): this is incomplete code. */
void main()
{
  ivec2 view_coord = ivec2(gl_FragCoord.xy - origin.xy);
  vec3 final_color = vec3(1.0, 0.0, 1.0);

  if (view_coord.x % 100 == 0) {
    final_color = vec3(0.0);
  }
  else {
    final_color = colorBackground.rgb;
  }

  fragColor = vec4(final_color, 1.0);
}
