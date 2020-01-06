
uniform float normalSize;
uniform bool doMultiframe;

in vec3 pos;
in float ma;
in uint vflag;

out vec4 finalColor;

void discard_vert()
{
  /* We set the vertex at the camera origin to generate 0 fragments. */
  gl_Position = vec4(0.0, 0.0, -3e36, 0.0);
}

#define GP_EDIT_POINT_SELECTED (1u << 0u)
#define GP_EDIT_STROKE_SELECTED (1u << 1u)
#define GP_EDIT_MULTIFRAME (1u << 2u)

void main()
{
  GPU_INTEL_VERTEX_SHADER_WORKAROUND

  vec3 world_pos = point_object_to_world(pos);
  gl_Position = point_world_to_ndc(world_pos);

  bool is_multiframe = (vflag & GP_EDIT_MULTIFRAME) != 0u;
  bool is_stroke_sel = (vflag & GP_EDIT_STROKE_SELECTED) != 0u;
  bool is_point_sel = (vflag & GP_EDIT_POINT_SELECTED) != 0u;
  finalColor = ((vflag & GP_EDIT_POINT_SELECTED) != 0u) ? colorGpencilVertexSelect :
                                                          colorGpencilVertex;

#ifdef USE_POINTS
  if ((!doMultiframe || !is_stroke_sel) && is_multiframe) {
    discard_vert();
  }
#endif

  if (ma == -1.0 || (is_multiframe && !doMultiframe)) {
    discard_vert();
  }

  gl_PointSize = sizeVertex * 2.0;

#ifdef USE_WORLD_CLIP_PLANES
  world_clip_planes_calc_clip_distance(world_pos);
#endif
}
