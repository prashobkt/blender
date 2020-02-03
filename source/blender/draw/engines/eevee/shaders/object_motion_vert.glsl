
uniform mat4 currViewProjectionMatrix;
uniform mat4 prevViewProjectionMatrix;
uniform mat4 currModelMatrix;
uniform mat4 prevModelMatrix;

in vec3 pos;
//  in vec3 pos_prev; /* TODO */

out vec3 currWorldPos;
out vec3 prevWorldPos;

void main()
{
  prevWorldPos = (prevModelMatrix * vec4(pos, 1.0)).xyz;
  currWorldPos = (currModelMatrix * vec4(pos, 1.0)).xyz;
  gl_Position = currViewProjectionMatrix * vec4(currWorldPos, 1.0);
}
