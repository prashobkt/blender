
uniform sampler2D colorBuffer;
uniform sampler2D velocityBuffer;

uniform int samples;
uniform float shutter;
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
  float noise = 2.0 * wang_hash_noise(0u) * inv_samples;

  vec2 motion = texture(velocityBuffer, uvcoordsvar.xy).xy - 0.5;
  motion *= shutter;

  float inc = 2.0 * inv_samples;
  float i = -1.0 + noise;

  FragColor = vec4(0.0);
  for (int j = 0; j < samples && j < MAX_SAMPLE; j++) {
    FragColor += textureLod(colorBuffer, uvcoordsvar.xy + motion * i, 0.0) * inv_samples;
    i += inc;
  }
}
