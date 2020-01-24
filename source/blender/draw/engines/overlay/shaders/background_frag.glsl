
uniform sampler2D colorBuffer;
uniform sampler2D depthBuffer;

in vec4 uvcoordsvar;

out vec4 fragColor;

void main()
{
  /* The blend equation is:
   * resutl.rgb = SRC.rgb * (1 - DST.a) + DST.rgb * (SRC.a)
   * result.a = SRC.a * 0 + DST.a * SRC.a
   * This removes the alpha channel and put the background behind reference images
   * while masking the reference images by the render alpha.
   */
  float alpha = texture(colorBuffer, uvcoordsvar.st).a;
  float depth = texture(depthBuffer, uvcoordsvar.st).r;

  /* Mimic alpha under behavior. Result is premultiplied. */
  fragColor = vec4(colorBackground.rgb, 1.0) * (1.0 - alpha);

  /* Special case: If the render is not transparent, do not clear alpha values. */
  if (depth == 1.0 && alpha == 1.0) {
    fragColor.a = 1.0;
  }

  /* TODO Gradient Background. */
  /* TODO Alpha checker Background. */
}
