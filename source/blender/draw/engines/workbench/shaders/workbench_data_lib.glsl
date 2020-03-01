struct LightData {
  vec4 direction;
  vec4 specular_color;
  vec4 diffuse_color_wrap; /* rgb: diffuse col a: wrapped lighting factor */
};

struct WorldData {
  vec4 viewvecs[3];
  vec4 viewport_size;
  vec4 object_outline_color;
  vec4 shadow_direction_vs;
  LightData lights[4];
  vec4 ambient_color;
  int matcap_orientation;
  float curvature_ridge;
  float curvature_valley;
  int _pad0;
};

#define viewport_size_inv viewport_size.zw

layout(std140) uniform world_block
{
  WorldData world_data;
};
