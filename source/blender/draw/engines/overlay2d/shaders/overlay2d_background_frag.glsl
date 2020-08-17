
#pragma BLENDER_REQUIRE(common_globals_lib.glsl)

uniform sampler2D colorBuffer;
uniform sampler2D depthBuffer;

in vec4 uvcoordsvar;

out vec4 fragColor;

/* 4x4 bayer matrix prepared for 8bit UNORM precision error. */
#define P(x) (((x + 0.5) * (1.0 / 16.0) - 0.5) * (1.0 / 255.0))
const vec4 dither_mat4x4[4] = vec4[4](vec4(P(0.0), P(8.0), P(2.0), P(10.0)),
                                      vec4(P(12.0), P(4.0), P(14.0), P(6.0)),
                                      vec4(P(3.0), P(11.0), P(1.0), P(9.0)),
                                      vec4(P(15.0), P(7.0), P(13.0), P(5.0)));

float dither(void)
{
  ivec2 co = ivec2(gl_FragCoord.xy) % 4;
  return dither_mat4x4[co.x][co.y];
}

void main()
{
  /* The blend equation is:
   * resutl.rgb = SRC.rgb * (1 - DST.a) + DST.rgb * (SRC.a)
   * result.a = SRC.a * 0 + DST.a * SRC.a
   * This removes the alpha channel and put the background behind reference images
   * while masking the reference images by the render alpha.
   */
  vec4 color = texture(colorBuffer, uvcoordsvar.st);
  float alpha = color.a;
  /* color is premultiplied. extract alpha from emission when alpha is 0. */
  if (alpha == 0.0) {
    alpha = min((color.r + color.g + color.b) / 3.0, 1.0);
  }
  float depth = texture(depthBuffer, uvcoordsvar.st).r;

  vec3 bg_col;

  if (depth == 1.0) {
    bg_col = colorBackground.rgb;
  }
  else {
    float size = sizeChecker * sizePixel;
    ivec2 p = ivec2(floor(gl_FragCoord.xy / size));
    bool check = mod(p.x, 2) == mod(p.y, 2);
    bg_col = (check) ? colorCheckerPrimary.rgb : colorCheckerSecondary.rgb;
  }

  /* Mimic alpha under behavior. Result is premultiplied. */
  fragColor = vec4(bg_col, 1.0) * (1.0 - alpha);

  /* Special case: If the render is not transparent, do not clear alpha values. */
  if (depth == 1.0 && alpha == 1.0) {
    fragColor.a = 1.0;
  }
}
