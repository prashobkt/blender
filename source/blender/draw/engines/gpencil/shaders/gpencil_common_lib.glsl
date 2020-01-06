
/* Must match C declaration. */
struct gpMaterial {
  vec4 stroke_color;
  vec4 fill_color;
  vec4 fill_mix_color;
  vec4 fill_uv_rot_scale;
  vec4 fill_uv_offset;
  /* Put float/int at the end to avoid padding error */
  float stroke_texture_mix;
  float stroke_u_scale;
  float fill_texture_mix;
  int flag;
  /* Please ensure 16 byte alignment (multiple of vec4). */
};

/* flag */
#define GP_STROKE_ALIGNMENT_STROKE 1
#define GP_STROKE_ALIGNMENT_OBJECT 2
#define GP_STROKE_ALIGNMENT_FIXED 3
#define GP_STROKE_ALIGNMENT 0x3
#define GP_STROKE_OVERLAP (1 << 2)
#define GP_STROKE_TEXTURE_USE (1 << 3)
#define GP_STROKE_TEXTURE_STENCIL (1 << 4)
#define GP_STROKE_TEXTURE_PREMUL (1 << 5)
#define GP_STROKE_DOTS (1 << 6)
#define GP_FILL_TEXTURE_USE (1 << 10)
#define GP_FILL_TEXTURE_PREMUL (1 << 11)
#define GP_FILL_TEXTURE_CLIP (1 << 12)
#define GP_FILL_GRADIENT_USE (1 << 13)
#define GP_FILL_GRADIENT_RADIAL (1 << 14)
/* High bits are used to pass material ID to fragment shader. */
#define GP_MATID_SHIFT 16

/* Multiline defines can crash blender with certain GPU drivers. */
/* clang-format off */
#define GP_FILL_FLAGS (GP_FILL_TEXTURE_USE | GP_FILL_TEXTURE_PREMUL | GP_FILL_TEXTURE_CLIP | GP_FILL_GRADIENT_USE | GP_FILL_GRADIENT_RADIAL)
/* clang-format on */

#define GP_FLAG_TEST(flag, val) (((flag) & (val)) != 0)

/* Must match C declaration. */
struct gpLight {
  vec4 color_type;
  vec4 right;
  vec4 up;
  vec4 forward;
  vec4 position;
  /* Please ensure 16 byte alignment (multiple of vec4). */
};

#define spot_size right.w
#define spot_blend up.w

#define GP_LIGHT_TYPE_POINT 0.0
#define GP_LIGHT_TYPE_SPOT 1.0
#define GP_LIGHT_TYPE_SUN 2.0
#define GP_LIGHT_TYPE_AMBIENT 3.0

#ifdef GP_MATERIAL_BUFFER_LEN

layout(std140) uniform gpMaterialBlock
{
  gpMaterial materials[GP_MATERIAL_BUFFER_LEN];
};

#endif

#ifdef GPENCIL_LIGHT_BUFFER_LEN

layout(std140) uniform gpLightBlock
{
  gpLight lights[GPENCIL_LIGHT_BUFFER_LEN];
};

#endif

/* Must match eGPLayerBlendModes */
#define MODE_REGULAR 0
#define MODE_OVERLAY 1
#define MODE_ADD 2
#define MODE_SUB 3
#define MODE_MULTIPLY 4
#define MODE_DIVIDE 5
#define MODE_OVERLAY_SECOND_PASS 999

void blend_mode_output(
    int blend_mode, vec4 color, float opacity, out vec4 frag_color, out vec4 frag_revealage)
{
  switch (blend_mode) {
    case MODE_REGULAR:
      /* Reminder: Blending func is premult alpha blend (dst.rgba * (1 - src.a) + src.rgb).*/
      color *= opacity;
      frag_color = color;
      frag_revealage = vec4(0.0, 0.0, 0.0, color.a);
      break;
    case MODE_MULTIPLY:
      /* Reminder: Blending func is multiply blend (dst.rgba * src.rgba).*/
      color.a *= opacity;
      frag_revealage = frag_color = (1.0 - color.a) + color.a * color;
      break;
    case MODE_DIVIDE:
      /* Reminder: Blending func is multiply blend (dst.rgba * src.rgba).*/
      color.a *= opacity;
      frag_revealage = frag_color = clamp(1.0 / (1.0 - color * color.a), 0.0, 1e18);
      break;
    case MODE_OVERLAY:
      /* Reminder: Blending func is multiply blend (dst.rgba * src.rgba).*/
      /**
       * We need to separate the overlay equation into 2 term (one mul and one add).
       * This is the standard overlay equation (per channel):
       * rtn = (src < 0.5) ? (2.0 * src * dst) : (1.0 - 2.0 * (1.0 - src) * (1.0 - dst));
       * We rewrite the second branch like this:
       * rtn = 1 - 2 * (1 - src) * (1 - dst);
       * rtn = 1 - 2 (1 - dst + src * dst - src);
       * rtn = 1 - 2 (1 - dst * (1 - src) - src);
       * rtn = 1 - 2 + dst * (2 - 2 * src) + 2 * src;
       * rtn = (- 1 + 2 * src) + dst * (2 - 2 * src);
       **/
      color = mix(vec4(0.5), color, color.a * opacity);
      vec4 s = step(-0.5, -color);
      frag_revealage = frag_color = 2.0 * s + 2.0 * color * (1.0 - s * 2.0);
      break;
    case MODE_OVERLAY_SECOND_PASS:
      /* Reminder: Blending func is additive blend (dst.rgba + src.rgba).*/
      color = mix(vec4(0.5), color, color.a * opacity);
      frag_revealage = frag_color = (-1.0 + 2.0 * color) * step(-0.5, -color);
      break;
    case MODE_SUB:
    case MODE_ADD:
      /* Reminder: Blending func is additive / subtractive blend (dst.rgba +/- src.rgba).*/
      frag_color = color * color.a * opacity;
      frag_revealage = vec4(0.0);
      break;
  }
}