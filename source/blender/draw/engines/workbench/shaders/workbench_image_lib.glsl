
bool node_tex_tile_lookup(inout vec3 co, sampler2DArray ima, sampler1DArray map)
{
  vec2 tile_pos = floor(co.xy);

  if (tile_pos.x < 0 || tile_pos.y < 0 || tile_pos.x >= 10)
    return false;

  float tile = 10.0 * tile_pos.y + tile_pos.x;
  if (tile >= textureSize(map, 0).x)
    return false;

  /* Fetch tile information. */
  float tile_layer = texelFetch(map, ivec2(tile, 0), 0).x;
  if (tile_layer < 0.0)
    return false;

  vec4 tile_info = texelFetch(map, ivec2(tile, 1), 0);

  co = vec3(((co.xy - tile_pos) * tile_info.zw) + tile_info.xy, tile_layer);
  return true;
}

vec4 workbench_sample_texture(sampler2D image,
                              vec2 coord,
                              bool nearest_sampling,
                              bool premultiplied)
{
  vec2 tex_size = vec2(textureSize(image, 0).xy);
  /* TODO(fclem) We could do the same with sampler objects.
   * But this is a quick workaround instead of messing with the GPUTexture itself. */
  vec2 uv = nearest_sampling ? (floor(coord * tex_size) + 0.5) / tex_size : coord;
  vec4 color = texture(image, uv);

  /* Unpremultiply if stored multiplied, since straight alpha is expected by shaders. */
  if (premultiplied && !(color.a == 0.0 || color.a == 1.0)) {
    color.rgb = color.rgb / color.a;
  }

  return color;
}

vec4 workbench_sample_texture_array(sampler2DArray tile_array,
                                    sampler1DArray tile_data,
                                    vec2 coord,
                                    bool nearest_sampling,
                                    bool premultiplied)
{
  vec2 tex_size = vec2(textureSize(tile_array, 0).xy);

  vec3 uv = vec3(coord, 0);
  if (!node_tex_tile_lookup(uv, tile_array, tile_data))
    return vec4(1.0, 0.0, 1.0, 1.0);

  /* TODO(fclem) We could do the same with sampler objects.
   * But this is a quick workaround instead of messing with the GPUTexture itself. */
  uv.xy = nearest_sampling ? (floor(uv.xy * tex_size) + 0.5) / tex_size : uv.xy;
  vec4 color = texture(tile_array, uv);

  /* Unpremultiply if stored multiplied, since straight alpha is expected by shaders. */
  if (premultiplied && !(color.a == 0.0 || color.a == 1.0)) {
    color.rgb = color.rgb / color.a;
  }

  return color;
}

uniform sampler2DArray image_tile_array;
uniform sampler1DArray image_tile_data;
uniform sampler2D image;

uniform float imageTransparencyCutoff = 0.1;
uniform bool imageNearest;
uniform bool imagePremultiplied;

vec3 workbench_image_color(vec2 uvs)
{
#if defined(V3D_SHADING_TEXTURE_COLOR)
#  ifdef TEXTURE_IMAGE_ARRAY
  vec4 color = workbench_sample_texture_array(
      image_tile_array, image_tile_data, uvs, imageNearest, imagePremultiplied);
#  else
  vec4 color = workbench_sample_texture(image, uvs, imageNearest, imagePremultiplied);
#  endif

  if (color.a < ImageTransparencyCutoff) {
    discard;
  }

  return color.rgb;
#else
  return vec3(1.0);
#endif
}
