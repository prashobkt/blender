
/* NOTE: To be used with UNIFORM_RESOURCE_ID and INSTANCED_ATTR as define. */
#pragma BLENDER_REQUIRE(common_view_lib.glsl)

in vec4 pos; /* Position and radius. */

/* ---- Instanced attribs ---- */

in vec3 pos_inst;
in vec3 nor;

/* Return object position. */
vec3 pointcloud_get_pos(void)
{
  return pos.xyz + pos_inst * pos.w;
}

/* Return object Normal. */
vec3 pointcloud_get_nor(void)
{
  return nor;
}
