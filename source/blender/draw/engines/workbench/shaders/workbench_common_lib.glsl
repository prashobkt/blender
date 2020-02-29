#define NO_OBJECT_ID uint(0)
#define EPSILON 0.00001
#define M_PI 3.14159265358979323846

#ifdef GPU_VERTEX_SHADER
#  define IN_OUT out
#else
#  define IN_OUT in
#endif

#define CAVITY_BUFFER_RANGE 4.0

#ifdef WORKBENCH_ENCODE_NORMALS

#  define WB_Normal vec2

/* From http://aras-p.info/texts/CompactNormalStorage.html
 * Using Method #4: Spheremap Transform */
vec3 workbench_normal_decode(vec4 enc)
{
  vec2 fenc = enc.xy * 4.0 - 2.0;
  float f = dot(fenc, fenc);
  float g = sqrt(1.0 - f / 4.0);
  vec3 n;
  n.xy = fenc * g;
  n.z = 1 - f / 2;
  return n;
}

/* From http://aras-p.info/texts/CompactNormalStorage.html
 * Using Method #4: Spheremap Transform */
WB_Normal workbench_normal_encode(bool front_face, vec3 n)
{
  n = normalize(front_face ? n : -n);
  float p = sqrt(n.z * 8.0 + 8.0);
  n.xy = clamp(n.xy / p + 0.5, 0.0, 1.0);
  return n.xy;
}

#else
#  define WB_Normal vec3
/* Well just do nothing... */
#  define workbench_normal_encode(f, a) (a)
#  define workbench_normal_decode(a) (a.xyz)
#endif /* WORKBENCH_ENCODE_NORMALS */

/* Encoding into the alpha of a RGBA8 UNORM texture. */
#define TARGET_BITCOUNT 8u
#define METALLIC_BITS 3u /* Metallic channel is less important. */
#define ROUGHNESS_BITS (TARGET_BITCOUNT - METALLIC_BITS)
#define TOTAL_BITS (METALLIC_BITS + ROUGHNESS_BITS)

/* Encode 2 float into 1 with the desired precision. */
float workbench_float_pair_encode(float v1, float v2)
{
  // const uint total_mask = ~(0xFFFFFFFFu << TOTAL_BITS);
  // const uint v1_mask = ~(0xFFFFFFFFu << ROUGHNESS_BITS);
  // const uint v2_mask = ~(0xFFFFFFFFu << METALLIC_BITS);
  /* Same as above because some compiler are dumb af. and think we use mediump int.  */
  const int total_mask = 0xFF;
  const int v1_mask = 0x1F;
  const int v2_mask = 0x7;
  int iv1 = int(v1 * float(v1_mask));
  int iv2 = int(v2 * float(v2_mask)) << int(ROUGHNESS_BITS);
  return float(iv1 | iv2) * (1.0 / float(total_mask));
}

void workbench_float_pair_decode(float data, out float v1, out float v2)
{
  // const uint total_mask = ~(0xFFFFFFFFu << TOTAL_BITS);
  // const uint v1_mask = ~(0xFFFFFFFFu << ROUGHNESS_BITS);
  // const uint v2_mask = ~(0xFFFFFFFFu << METALLIC_BITS);
  /* Same as above because some compiler are dumb af. and think we use mediump int.  */
  const int total_mask = 0xFF;
  const int v1_mask = 0x1F;
  const int v2_mask = 0x7;
  int idata = int(data * float(total_mask));
  v1 = float(idata & v1_mask) * (1.0 / float(v1_mask));
  v2 = float(idata >> int(ROUGHNESS_BITS)) * (1.0 / float(v2_mask));
}

float calculate_transparent_weight(float z, float alpha)
{
#if 0
  /* Eq 10 : Good for surfaces with varying opacity (like particles) */
  float a = min(1.0, alpha * 10.0) + 0.01;
  float b = -gl_FragCoord.z * 0.95 + 1.0;
  float w = a * a * a * 3e2 * b * b * b;
#else
  /* Eq 7 put more emphasis on surfaces closer to the view. */
  // float w = 10.0 / (1e-5 + pow(abs(z) / 5.0, 2.0) + pow(abs(z) / 200.0, 6.0)); /* Eq 7 */
  // float w = 10.0 / (1e-5 + pow(abs(z) / 10.0, 3.0) + pow(abs(z) / 200.0, 6.0)); /* Eq 8 */
  // float w = 10.0 / (1e-5 + pow(abs(z) / 200.0, 4.0)); /* Eq 9 */
  /* Same as eq 7, but optimized. */
  float a = abs(z) / 5.0;
  float b = abs(z) / 200.0;
  b *= b;
  float w = 10.0 / ((1e-5 + a * a) + b * (b * b)); /* Eq 7 */
#endif
  return alpha * clamp(w, 1e-2, 3e2);
}

/* Special function only to be used with calculate_transparent_weight(). */
float linear_zdepth(float depth, vec4 viewvecs[3], mat4 proj_mat)
{
  if (proj_mat[3][3] == 0.0) {
    float d = 2.0 * depth - 1.0;
    return -proj_mat[3][2] / (d + proj_mat[2][2]);
  }
  else {
    /* Return depth from near plane. */
    return depth * viewvecs[1].z;
  }
}

vec3 view_vector_from_screen_uv(vec2 uv, vec4 viewvecs[3], mat4 proj_mat)
{
  if (proj_mat[3][3] == 0.0) {
    return normalize(viewvecs[0].xyz + vec3(uv, 0.0) * viewvecs[1].xyz);
  }
  else {
    return vec3(0.0, 0.0, 1.0);
  }
}
