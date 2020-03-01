
#ifdef GPU_VERTEX_SHADER
#  define IN_OUT out
#else
#  define IN_OUT in
#endif

IN_OUT ShaderStageInterface
{
  vec3 normal_interp;
  vec3 color_interp;
  float alpha_interp;
  vec2 uv_interp;
  flat float packed_rough_metal;
  flat int object_id;
};
