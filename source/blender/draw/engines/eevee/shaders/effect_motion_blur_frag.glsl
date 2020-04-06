
uniform sampler2D colorBuffer;
uniform sampler2D velocityBuffer;

uniform int samples;
uniform float sampleOffset;

in vec4 uvcoordsvar;

out vec4 FragColor;

#define MAX_SAMPLE 64

float wang_hash_noise(uint s)
{
  uint seed = (uint(gl_FragCoord.x) * 1664525u + uint(gl_FragCoord.y)) + s;

  seed = (seed ^ 61u) ^ (seed >> 16u);
  seed *= 9u;
  seed = seed ^ (seed >> 4u);
  seed *= 0x27d4eb2du;
  seed = seed ^ (seed >> 15u);

  float value = float(seed);
  value *= 1.0 / 4294967296.0;
  return fract(value + sampleOffset);
}

void main()
{
  float inv_samples = 1.0 / float(samples);
  float noise = wang_hash_noise(0u);

  vec2 uv = uvcoordsvar.xy;
  vec4 motion = texture(velocityBuffer, uv) * 2.0 - 1.0;

  /* Needed to match cycles. Can't find why... (fclem) */
  motion *= -0.25;

  FragColor = vec4(0.0);
  for (float j = noise; j < 8.0; j++) {
    FragColor += textureLod(colorBuffer, uv + motion.xy * (0.125 * j), 0.0);
  }
  for (float j = noise; j < 8.0; j++) {
    FragColor += textureLod(colorBuffer, uv + motion.zw * (0.125 * j), 0.0);
  }
  FragColor *= 1.0 / 16.0;
}
