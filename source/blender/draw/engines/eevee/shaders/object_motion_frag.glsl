
uniform mat4 currViewProjectionMatrix;
uniform mat4 prevViewProjectionMatrix;
uniform float deltaTimeInv;

in vec3 currWorldPos;
in vec3 prevWorldPos;

out vec2 outData;

void main()
{
  vec4 prev_wpos = prevViewProjectionMatrix * vec4(prevWorldPos, 1.0);
  vec4 curr_wpos = currViewProjectionMatrix * vec4(currWorldPos, 1.0);

  vec2 prev_uv = (prev_wpos.xy / prev_wpos.w) * 0.5;
  vec2 curr_uv = (curr_wpos.xy / curr_wpos.w) * 0.5;

  outData = curr_uv - prev_uv;
  outData *= deltaTimeInv;

  /* Encode to unsigned normalized 16bit texture. */
  outData = outData * 0.5 + 0.5;
}
