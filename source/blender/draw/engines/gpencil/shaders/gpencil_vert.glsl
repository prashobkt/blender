
/* TODO UBO */
uniform vec2 sizeViewport;
uniform vec2 sizeViewportInv;

/* Per Object */
uniform vec4 gpModelMatrix[4];
uniform bool strokeOrder3d;
uniform float thicknessScale;
uniform float thicknessWorldScale;
#define thicknessIsScreenSpace (thicknessWorldScale < 0.0)

/* Per Layer */
uniform float thicknessOffset;
uniform float vertexColorOpacity;
uniform vec4 layerTint;

in vec4 ma;
in vec4 ma1;
in vec4 ma2;
in vec4 ma3;
#define strength1 ma1.y
#define strength2 ma2.y
#define stroke_id1 ma1.z
#define point_id1 ma1.w
/* Position contains thickness in 4th component. */
in vec4 pos;  /* Prev adj vert */
in vec4 pos1; /* Current edge */
in vec4 pos2; /* Current edge */
in vec4 pos3; /* Next adj vert */
#define thickness1 pos1.w
#define thickness2 pos2.w
/* xy is UV for fills, z is U of stroke, w is cosine of UV angle with sign of sine.  */
in vec4 uv1;
in vec4 uv2;

in vec4 col1;
in vec4 col2;

out vec4 finalColorMul;
out vec4 finalColorAdd;
out vec3 finalPos;
out vec2 finalUvs;
noperspective out float strokeThickness;
flat out vec2 strokePt1;
flat out vec2 strokePt2;
flat out int matFlag;
flat out float depth;

void discard_vert()
{
  /* We set the vertex at the camera origin to generate 0 fragments. */
  gl_Position = vec4(0.0, 0.0, -3e36, 0.0);
}

vec2 project_to_screenspace(vec4 v)
{
  return ((v.xy / v.w) * 0.5 + 0.5) * sizeViewport;
}

vec2 rotate_90deg(vec2 v)
{
  /* Counter Clock-Wise. */
  return vec2(-v.y, v.x);
}

mat4 model_matrix_get()
{
  return mat4(gpModelMatrix[0], gpModelMatrix[1], gpModelMatrix[2], gpModelMatrix[3]);
}

vec3 transform_point(mat4 m, vec3 v)
{
  return (m * vec4(v, 1.0)).xyz;
}

vec2 safe_normalize(vec2 v)
{
  float len_sqr = dot(v, v);
  if (len_sqr > 0.0) {
    return v / sqrt(len_sqr);
  }
  else {
    return vec2(0.0);
  }
}

vec2 safe_normalize_len(vec2 v, out float len)
{
  len = sqrt(dot(v, v));
  if (len > 0.0) {
    return v / len;
  }
  else {
    return vec2(0.0);
  }
}

float stroke_thickness_modulate(float thickness)
{
  /* Modify stroke thickness by object and layer factors.-*/
  thickness *= thicknessScale;
  thickness += thicknessOffset;
  thickness = max(1.0, thickness);

  if (thicknessIsScreenSpace) {
    /* Multiply offset by view Z so that offset is constant in screenspace.
     * (e.i: does not change with the distance to camera) */
    thickness *= gl_Position.w;
  }
  else {
    /* World space point size. */
    thickness *= thicknessWorldScale * ProjectionMatrix[1][1] * sizeViewport.y;
  }
  return thickness;
}

void color_output(vec4 stroke_col, vec4 vert_col, float vert_strength, float mix_tex)
{
  /* Mix stroke with other colors. */
  vec4 mixed_col = stroke_col;
  mixed_col.rgb = mix(mixed_col.rgb, vert_col.rgb, vert_col.a * vertexColorOpacity);
  mixed_col.rgb = mix(mixed_col.rgb, layerTint.rgb, layerTint.a);
  mixed_col.a *= vert_strength;
  /**
   * This is what the fragment shader looks like.
   * out = col * finalColorMul + col.a * finalColorAdd.
   * finalColorMul is how much of the texture color to keep.
   * finalColorAdd is how much of the mixed color to add.
   * Note that we never add alpha. This is to keep the texture act as a stencil.
   * We do however, modulate the alpha (reduce it).
   **/
  /* We add the mixed color. This is 100% mix (no texture visible). */
  finalColorMul = vec4(mixed_col.aaa, mixed_col.a);
  finalColorAdd = vec4(mixed_col.rgb * mixed_col.a, 0.0);
  /* Then we blend according to the texture mix factor.
   * Note that we keep the alpha modulation. */
  finalColorMul.rgb *= mix_tex;
  finalColorAdd.rgb *= 1.0 - mix_tex;
}

void stroke_vertex()
{
  int m = int(ma1.x);
  bool is_dot = false;
  bool is_squares = false;

  if (m != -1.0) {
    is_dot = GP_FLAG_TEST(materials[m].flag, GP_STROKE_ALIGNMENT);
    is_squares = !GP_FLAG_TEST(materials[m].flag, GP_STROKE_DOTS);
  }

  /* Enpoints, we discard the vertices. */
  if (ma1.x == -1.0 || (!is_dot && ma2.x == -1.0)) {
    discard_vert();
    return;
  }

  mat4 model_mat = model_matrix_get();

  /* Avoid using a vertex attrib for quad positioning. */
  float x = float(gl_VertexID & 1) * 2.0 - 1.0; /* [-1..1] */
  float y = float(gl_VertexID & 2) - 1.0;       /* [-1..1] */

  bool use_curr = is_dot || (x == -1.0);

  vec3 wpos_adj = transform_point(model_mat, (use_curr) ? pos.xyz : pos3.xyz);
  vec3 wpos1 = transform_point(model_mat, pos1.xyz);
  vec3 wpos2 = transform_point(model_mat, pos2.xyz);

  vec4 ndc_adj = point_world_to_ndc(wpos_adj);
  vec4 ndc1 = point_world_to_ndc(wpos1);
  vec4 ndc2 = point_world_to_ndc(wpos2);

  gl_Position = (use_curr) ? ndc1 : ndc2;
  finalPos = (use_curr) ? wpos1 : wpos2;

  /* TODO case where ndc1 & ndc2 is behind camera */
  vec2 ss_adj = project_to_screenspace(ndc_adj);
  vec2 ss1 = project_to_screenspace(ndc1);
  vec2 ss2 = project_to_screenspace(ndc2);
  /* Screenspace Lines tangents. */
  float line_len;
  vec2 line = safe_normalize_len(ss2 - ss1, line_len);
  vec2 line_adj = safe_normalize((x == -1.0) ? (ss1 - ss_adj) : (ss_adj - ss2));

  float thickness = abs((use_curr) ? thickness1 : thickness2);
  thickness = stroke_thickness_modulate(thickness);

  finalUvs = vec2(x, y) * 0.5 + 0.5;

  if (is_dot) {
    vec2 x_axis;
    vec2 y_axis;
    int alignement = materials[m].flag & GP_STROKE_ALIGNMENT;
    if (alignement == GP_STROKE_ALIGNMENT_STROKE) {
      x_axis = line;
    }
    else if (alignement == GP_STROKE_ALIGNMENT_OBJECT) {
      vec4 ndc_x = point_world_to_ndc(wpos1 + model_mat[0].xyz);
      vec2 ss_x = project_to_screenspace(ndc_x);
      x_axis = safe_normalize(ss_x - ss1);
    }
    else /* GP_STROKE_ALIGNMENT_FIXED*/ {
      x_axis = vec2(1.0, 0.0);
    }

    /* Rotation: Encoded as Cos + Sin sign. */
    float rot_sin = sqrt(1.0 - uv1.w * uv1.w) * sign(uv1.w);
    float rot_cos = abs(uv1.w);
    x_axis = mat2(rot_cos, -rot_sin, rot_sin, rot_cos) * x_axis;

    y_axis = rotate_90deg(x_axis);

    gl_Position.xy += (x * x_axis + y * y_axis) * sizeViewportInv.xy * thickness;

    strokePt1 = ss1;
    strokePt2 = ss1 + vec2(0.5, 0.0);
    strokeThickness = (is_squares) ? 1e18 : (thickness / gl_Position.w);
  }
  else {
    /* Mitter tangent vector. */
    vec2 miter_tan = safe_normalize(line_adj + line);
    float miter_dot = dot(miter_tan, line_adj);
    /* Break corners after a certain angle to avoid really thick corners. */
    const float miter_limit = 0.5; /* cos(60°) */
    miter_tan = (miter_dot < miter_limit) ? line : (miter_tan / miter_dot);

    vec2 miter = rotate_90deg(miter_tan);

    strokePt1.xy = ss1;
    strokePt2.xy = ss2;
    strokeThickness = thickness / gl_Position.w;

    /* Reminder: we packed the cap flag into the sign of stength and thickness sign. */
    bool is_stroke_start = (ma.x == -1.0 && x == -1.0 && strength1 > 0.0);
    bool is_stroke_end = (ma3.x == -1.0 && x == 1.0 && thickness1 > 0.0);

    vec2 screen_ofs = miter * y;

    if (is_stroke_start || is_stroke_end) {
      screen_ofs += line * x * 2.0;
    }

    gl_Position.xy += screen_ofs * sizeViewportInv.xy * thickness;

    finalUvs.x = (use_curr) ? uv1.z : uv2.z;
  }

  vec4 vert_col = (use_curr) ? col1 : col2;
  float vert_strength = abs((use_curr) ? strength1 : strength2);
  vec4 stroke_col = materials[m].stroke_color;
  float mix_tex = materials[m].stroke_texture_mix;

  color_output(stroke_col, vert_col, vert_strength, mix_tex);

  matFlag = materials[m].flag & ~GP_FILL_FLAGS;

  if (strokeOrder3d) {
    /* Use the fragment depth (see fragment shader). */
    depth = -1.0;
    /* We still offset the fills a little to avoid overlaps */
    gl_Position.z -= (stroke_id1 + 1) * 0.000002;
  }
  else if (GP_FLAG_TEST(materials[m].flag, GP_STROKE_OVERLAP)) {
    /* Use the index of the point as depth.
     * This means the stroke can overlap itself. */
    depth = (point_id1 + 1.0) * 0.0000002;
  }
  else {
    /* Use the index of first point of the stroke as depth.
     * We render using a greater depth test this means the stroke
     * cannot overlap itself.
     * We offset by one so that the fill can be overlapped by its stroke.
     * The offset is ok since we pad the strokes data because of adjacency infos. */
    depth = (stroke_id1 + 1.0) * 0.0000002;
  }
}

void fill_vertex()
{
  mat4 model_mat = model_matrix_get();

  vec3 wpos = transform_point(model_mat, pos1.xyz);
  gl_Position = point_world_to_ndc(wpos);
  finalPos = wpos;

  int m = int(ma1.x);

  vec4 fill_col = materials[m].fill_color;
  float mix_tex = materials[m].fill_texture_mix;

  color_output(fill_col, vec4(0.0), 1.0, mix_tex);

  matFlag = materials[m].flag & GP_FILL_FLAGS;

  vec2 loc = materials[m].fill_uv_offset.xy;
  mat2x2 rot_scale = mat2x2(materials[m].fill_uv_rot_scale.xy, materials[m].fill_uv_rot_scale.zw);
  finalUvs = rot_scale * uv1.xy + loc;

  strokeThickness = 1e18;
  strokePt1 = strokePt2 = vec2(0.0);

  if (strokeOrder3d) {
    /* Use the fragment depth (see fragment shader). */
    depth = -1.0;
    /* We still offset the fills a little to avoid overlaps */
    gl_Position.z -= stroke_id1 * 0.000002;
  }
  else {
    /* Use the index of first point of the stroke as depth. */
    depth = stroke_id1 * 0.0000002;
  }
}

void main()
{
  /* Trick to detect if a drawcall is stroke or fill.
   * This does mean that we need to draw an empty stroke segment before starting
   * to draw the real stroke segments. */
  bool is_fill = (gl_InstanceID == 0);

  if (!is_fill) {
    stroke_vertex();
  }
  else {
    fill_vertex();
  }
}
