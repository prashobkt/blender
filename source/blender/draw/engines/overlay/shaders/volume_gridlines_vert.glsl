uniform float slicePosition;
uniform int sliceAxis; /* -1 is no slice, 0 is X, 1 is Y, 2 is Z. */

/* FluidDomainSettings.res */
uniform ivec3 volumeSize;
/* FluidDomainSettings.cell_size */
uniform vec3 cellSize;
/* FluidDomainSettings.p0 */
uniform vec3 domainOriginOffset;
/* FluidDomainSettings.res_min */
uniform ivec3 adaptiveCellOffset;

flat out vec4 finalColor;

const vec3 corners[4] = vec3[4](vec3(-0.5, 0.5, 0.0),
                                vec3(0.5, 0.5, 0.0),
                                vec3(0.5, -0.5, 0.0),
                                vec3(-0.5, -0.5, 0.0));

const int indices[8] = int[8](0, 1, 1, 2, 2, 3, 3, 0);

void main()
{
  int cell = gl_VertexID / 8;
  mat3 rot_mat = mat3(0.0);

  vec3 cell_offset = vec3(0.5);
  ivec3 cell_div = volumeSize;
  if (sliceAxis == 0) {
    cell_offset.x = slicePosition * float(volumeSize.x);
    cell_div.x = 1;
    rot_mat[2].x = 1.0;
    rot_mat[0].y = 1.0;
    rot_mat[1].z = 1.0;
  }
  else if (sliceAxis == 1) {
    cell_offset.y = slicePosition * float(volumeSize.y);
    cell_div.y = 1;
    rot_mat[1].x = 1.0;
    rot_mat[2].y = 1.0;
    rot_mat[0].z = 1.0;
  }
  else if (sliceAxis == 2) {
    cell_offset.z = slicePosition * float(volumeSize.z);
    cell_div.z = 1;
    rot_mat[0].x = 1.0;
    rot_mat[1].y = 1.0;
    rot_mat[2].z = 1.0;
  }

  vec3 cell_co;
  cell_co.x = float(cell % cell_div.x);
  cell_co.y = float((cell / cell_div.x) % cell_div.y);
  cell_co.z = float(cell / (cell_div.x * cell_div.y));
  cell_co += cell_offset;

  vec3 pos = domainOriginOffset + cellSize * (cell_co + vec3(adaptiveCellOffset));
  vec3 rotated_pos = rot_mat * corners[indices[gl_VertexID % 8]];
  pos += rotated_pos * cellSize;

  finalColor = vec4(0.0, 0.0, 0.0, 1.0);

  vec3 world_pos = point_object_to_world(pos);
  gl_Position = point_world_to_ndc(world_pos);
}
