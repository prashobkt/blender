
uniform sampler2D colorBuf;
uniform sampler2D revealBuf;

in vec4 uvcoordsvar;

/* Reminder: Blending func is fragRevealage * DST + fragColor .*/
layout(location = 0, index = 0) out vec4 fragColor;
layout(location = 0, index = 1) out vec4 fragRevealage;

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
}
