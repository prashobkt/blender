
uniform sampler2D colorBuf;
uniform sampler2D revealBuf;
uniform bool doSrgb;

in vec4 uvcoordsvar;

/* Reminder: Blending func is fragRevealage * DST + fragColor .*/
layout(location = 0, index = 0) out vec4 fragColor;
layout(location = 0, index = 1) out vec4 fragRevealage;

/* TODO Remove. */
float linearrgb_to_srgb(float c)
{
  if (c < 0.0031308) {
    return (c < 0.0) ? 0.0 : c * 12.92;
  }
  else {
    return 1.055 * pow(c, 1.0 / 2.4) - 0.055;
  }
}

void main()
{
  /* Revealage, how much light passes through. */
  fragRevealage.rgb = textureLod(revealBuf, uvcoordsvar.xy, 0.0).rgb;
  /* Average for alpha channel. */
  fragRevealage.a = clamp(dot(fragRevealage.rgb, vec3(0.333334)), 0.0, 1.0);
  /* Color buf is already premultiplied. Just add it to the color. */
  fragColor.rgb = textureLod(colorBuf, uvcoordsvar.xy, 0.0).rgb;
  /* Add the alpha. */
  fragColor.a = 1.0 - fragRevealage.a;

  if (doSrgb) {
    /* Temporary srgb conversion.
     * TODO do color management / tonemapping here. */
    fragColor.r = linearrgb_to_srgb(fragColor.r);
    fragColor.g = linearrgb_to_srgb(fragColor.g);
    fragColor.b = linearrgb_to_srgb(fragColor.b);
  }
}
