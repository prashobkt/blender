
/*
 * Based on:
 * A Fast and Stable Feature-Aware Motion Blur Filter
 * by Jean-Philippe Guertin, Morgan McGuire, Derek Nowrouzezahrai
 *
 * With modification from the presentation:
 * Next Generation Post Processing in Call of Duty Advanced Warfare
 * by Jorge Jimenez
 */
uniform sampler2D colorBuffer;
uniform sampler2D depthBuffer;
uniform sampler2D velocityBuffer;

uniform int samples;
uniform float sampleOffset;
uniform vec2 viewportSize;
uniform vec2 viewportSizeInv;
/* TODO plug scene value */
uniform vec2 nearFar = vec2(0.1, 100.0); /* Near & far view depths values */
/* TODO make sure ortho works */
#define linear_depth(z) \
  ((true) ? (nearFar.x * nearFar.y) / (z * (nearFar.x - nearFar.y) + nearFar.y) : \
            z * (nearFar.y - nearFar.x) + nearFar.x) /* Only true for camera view! */

in vec4 uvcoordsvar;

out vec4 FragColor;

#define saturate(a) clamp(a, 0.0, 1.0)

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

vec2 spread_compare(float center_motion_length, float sample_motion_length, float offset_length)
{
  return saturate(vec2(center_motion_length, sample_motion_length) - offset_length + 1.0);
}

/* TODO expose to user */
#define DEPTH_SCALE 100.0

vec2 depth_compare(float center_depth, float sample_depth)
{
  return saturate(0.5 + vec2(DEPTH_SCALE, -DEPTH_SCALE) * (sample_depth - center_depth));
}

/* Kill contribution if not going the same direction. */
float dir_compare(vec2 offset, vec2 sample_motion, float sample_motion_length)
{
  if (sample_motion_length < 0.5) {
    return 1.0;
  }
  return (dot(offset, sample_motion) > 0.0) ? 1.0 : 0.0;
}

/* Return background (x) and foreground (y) weights. */
vec2 sample_weights(float center_depth,
                    float sample_depth,
                    float center_motion_length,
                    float sample_motion_length,
                    float offset_length)
{
  /* Clasify foreground/background. */
  vec2 depth_weight = depth_compare(center_depth, sample_depth);
  /* Weight if sample is overlapping or under the center pixel. */
  vec2 spread_weight = spread_compare(center_motion_length, sample_motion_length, offset_length);
  return depth_weight * spread_weight;
}

vec4 sample_velocity(vec2 uv)
{
  vec4 data = texture(velocityBuffer, uv);
  data = data * 2.0 - 1.0;
  /* Needed to match cycles. Can't find why... (fclem) */
  data *= 0.5;
  /* Transpose to pixelspace. */
  data *= viewportSize.xyxy;
  return data;
}

vec2 sample_velocity(vec2 uv, const bool next)
{
  vec4 data = sample_velocity(uv);
  data.xy = (next ? data.zw : data.xy);
  return data.xy;
}

#define SEARCH_KERNEL 8.0
#define KERNEL 8

void gather_sample(vec2 screen_uv,
                   float center_depth,
                   float center_motion_len,
                   vec2 offset,
                   float offset_len,
                   const bool next,
                   inout vec4 accum,
                   inout vec4 accum_bg,
                   inout vec3 w_accum)
{
  /* TODO snap uv to pixel center. Will avoid halo at object edges */
  vec2 sample_uv = screen_uv - offset * viewportSizeInv;
  vec2 sample_motion = sample_velocity(sample_uv, next);
  float sample_motion_len = length(sample_motion);
  float sample_depth = linear_depth(texture(depthBuffer, sample_uv).r);
  vec4 col = textureLod(colorBuffer, sample_uv, 0.0);

  vec3 weights;
  weights.xy = sample_weights(
      center_depth, sample_depth, center_motion_len, sample_motion_len, offset_len);
  weights.z = dir_compare(offset, sample_motion, sample_motion_len);
  weights.xy *= weights.z;

  accum += col * weights.y;
  accum_bg += col * weights.x;
  w_accum += weights;
}

void gather_blur(vec2 screen_uv,
                 vec2 center_motion,
                 float center_depth,
                 vec2 max_motion,
                 const bool next,
                 inout vec4 accum,
                 inout vec4 accum_bg,
                 inout vec3 w_accum)
{
  float center_motion_len = length(center_motion);
  float max_motion_len = length(max_motion);

  if (max_motion_len < 0.5) {
    return;
  }

  float ofs = fract(wang_hash_noise(0u) + sampleOffset);

  int i;
  float t, inc = 1.0 / float(KERNEL);
  for (i = 0, t = ofs * inc; i < KERNEL; i++, t += inc) {
    gather_sample(screen_uv,
                  center_depth,
                  center_motion_len,
                  max_motion * t,
                  max_motion_len * t,
                  next,
                  accum,
                  accum_bg,
                  w_accum);
  }

  if (center_motion_len < 0.5) {
    return;
  }

  for (i = 0, t = ofs * inc; i < KERNEL; i++, t += inc) {
    /* Also sample in center motion direction.
     * Allow to recover motion where there is conflicting
     * motion between foreground and background. */
    gather_sample(screen_uv,
                  center_depth,
                  center_motion_len,
                  center_motion * t,
                  center_motion_len * t,
                  next,
                  accum,
                  accum_bg,
                  w_accum);
  }
}

void main()
{
  /* TODO use blue noise texture. */
  float noise1 = wang_hash_noise(0u);
  float noise2 = fract(wang_hash_noise(5u) * 97894.594987);

  vec2 uv = uvcoordsvar.xy;

  /* Data of the center pixel of the gather (target). */
  float center_depth = linear_depth(texture(depthBuffer, uv).r);
  vec4 center_motion = sample_velocity(uv);
  vec4 center_color = textureLod(colorBuffer, uv, 0.0);

  /* TODO replace by preprocessing steps. */
  vec4 max_motion = center_motion;
  for (float j = 0.0; j <= 20.0; j++) {
    for (float i = 0.0; i <= 20.0; i++) {
      vec2 offset = vec2(i, j) * 3.0 - 20.0;
      vec2 sample_uv = (floor(vec2(noise1, noise2) - 0.5 + uv * viewportSize / 20.0) * 20.0 +
                        offset) *
                       viewportSizeInv;
      vec4 motion = sample_velocity(sample_uv);

      if (length(motion.xy) > length(max_motion.xy)) {
        max_motion.xy = motion.xy;
      }
      if (length(motion.zw) > length(max_motion.zw)) {
        max_motion.zw = motion.zw;
      }
    }
  }

  /* First (center) sample: time = T */
  /* x: Background, y: Foreground, z: dir. */
  vec3 w_accum = vec3(0.0, 0.0, 1.0);
  vec4 accum_bg = vec4(0.0);
  vec4 accum = vec4(0.0);
  /* First linear gather. time = [T - delta, T] */
  gather_blur(uv, center_motion.xy, center_depth, max_motion.xy, false, accum, accum_bg, w_accum);
  /* Second linear gather. time = [T, T + delta] */
  gather_blur(uv, center_motion.zw, center_depth, max_motion.zw, true, accum, accum_bg, w_accum);

#if 1
  /* Avoid division by 0.0. */
  float w = 1.0 / (50.0 * float(KERNEL) * 4.0);
  accum_bg += center_color * w;
  w_accum.x += w;
  /* Note: In Jimenez's presentation, they used center sample.
   * We use background color as it contains more informations for foreground
   * elements that have not enough weights.
   * Yield beter blur in complex motion. */
  center_color = accum_bg / w_accum.x;
#endif
  /* Merge background. */
  accum += accum_bg;
  w_accum.y += w_accum.x;
  /* Balance accumulation for failled samples.
   * We replace the missing foreground by the background. */
  float blend_fac = saturate(1.0 - w_accum.y / w_accum.z);
  FragColor = (accum / w_accum.z) + center_color * blend_fac;
}
