/** This describe the entire interface of the shader.  */

/* Samplers */
uniform sampler2D colorBuffer;
uniform sampler2D depthBuffer;

/* Uniforms */
uniform float refractionDepth;

IN_OUT ShaderStageInterface
{
  vec3 worldPosition;
  vec3 viewPosition;
  vec3 worldNormal;
  vec3 viewNormal;

#ifdef HAIR_SHADER
  vec3 hairTangent; /* world space */
  float hairThickTime;
  float hairThickness;
  float hairTime;
  flat int hairStrandID;
#endif
};
