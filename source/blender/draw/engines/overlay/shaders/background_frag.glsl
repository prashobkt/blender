
uniform sampler2D colorBuf;

in vec4 uvcoordsvar;

out vec4 fragColor;

void main()
{
  float alpha = texture(colorBuf, uvcoordsvar.st).a;
  /* Mimic alpha under behavior. Result is premultiplied. */
  fragColor = vec4(colorBackground.rgb, 0.0) * (1.0 - alpha);

  /* TODO Gradient Background. */
  /* TODO Alpha checker Background. */
}
