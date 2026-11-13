/** GLSL ES 3.00 шейдеры движка. */

export const sceneVertexShader = /* glsl */ `#version 300 es
precision highp float;

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;

uniform mat4 uModel;
uniform mat4 uViewProjection;
uniform mat3 uNormalMatrix;

out vec3 vWorldPosition;
out vec3 vNormal;
out vec2 vUv;

void main() {
  vec4 worldPosition = uModel * vec4(aPosition, 1.0);
  vWorldPosition = worldPosition.xyz;
  vNormal = uNormalMatrix * aNormal;
  vUv = aUv;
  gl_Position = uViewProjection * worldPosition;
}
`

export const sceneFragmentShader = /* glsl */ `#version 300 es
precision highp float;

#define MAX_LIGHTS 8

in vec3 vWorldPosition;
in vec3 vNormal;
in vec2 vUv;
out vec4 outColor;

uniform vec3 uCameraPosition;
uniform vec3 uBaseColor;
uniform vec3 uEmissive;
uniform float uRoughness;
uniform float uMetallic;
uniform sampler2D uDetail;
uniform float uDetailMix;
uniform vec2 uUvScale;

uniform vec3 uAmbientSky;
uniform vec3 uAmbientGround;
uniform vec3 uFogColor;
uniform float uFogDensity;
uniform vec3 uSunDirection;
uniform vec3 uSunColor;

uniform int uLightCount;
uniform vec3 uLightPosition[MAX_LIGHTS];
uniform vec3 uLightColor[MAX_LIGHTS];

void main() {
  vec3 N = normalize(vNormal);
  vec3 V = normalize(uCameraPosition - vWorldPosition);

  vec3 albedo = uBaseColor;
  if (uDetailMix > 0.001) {
    vec3 detail = texture(uDetail, vUv * uUvScale).rgb;
    albedo *= mix(vec3(1.0), detail, uDetailMix);
  }

  float shininess = mix(220.0, 6.0, clamp(uRoughness, 0.0, 1.0));
  float specStrength = mix(0.25, 1.4, uMetallic) * (1.0 - 0.55 * uRoughness);
  float diffuseStrength = mix(1.0, 0.35, uMetallic);
  vec3 specColor = mix(vec3(1.0), albedo, uMetallic);

  vec3 ambient = mix(uAmbientGround, uAmbientSky, N.y * 0.5 + 0.5);
  vec3 color = albedo * ambient * diffuseStrength;

  // направленный свет (солнце)
  {
    vec3 L = normalize(uSunDirection);
    vec3 H = normalize(L + V);
    float ndl = max(dot(N, L), 0.0);
    float spec = pow(max(dot(N, H), 0.0), shininess) * specStrength;
    color += (albedo * diffuseStrength * ndl + specColor * spec * ndl) * uSunColor;
  }

  // точечные источники
  for (int i = 0; i < MAX_LIGHTS; i++) {
    if (i >= uLightCount) break;
    vec3 toLight = uLightPosition[i] - vWorldPosition;
    float distanceToLight = length(toLight);
    vec3 L = toLight / max(distanceToLight, 0.0001);
    float attenuation = 1.0 / (1.0 + distanceToLight * distanceToLight * 0.14);
    float ndl = max(dot(N, L), 0.0);
    vec3 H = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), shininess) * specStrength;
    color += (albedo * diffuseStrength * ndl + specColor * spec * ndl) * uLightColor[i] * attenuation;
  }

  // френелевский ободок
  float fresnel = pow(1.0 - max(dot(N, V), 0.0), 3.0);
  color += specColor * fresnel * (0.12 + 0.5 * uMetallic) * ambient * 2.0;
  color += uEmissive;

  float viewDistance = length(uCameraPosition - vWorldPosition);
  float fog = 1.0 - exp(-viewDistance * uFogDensity);
  color = mix(color, uFogColor, clamp(fog, 0.0, 1.0));

  outColor = vec4(color, 1.0);
}
`

export const fullscreenVertexShader = /* glsl */ `#version 300 es
out vec2 vUv;

void main() {
  vec2 position = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
  vUv = position;
  gl_Position = vec4(position * 2.0 - 1.0, 0.0, 1.0);
}
`

export const skyFragmentShader = /* glsl */ `#version 300 es
precision highp float;

out vec4 outColor;

uniform mat4 uInvViewProjection;
uniform vec3 uCameraPosition;
uniform vec2 uResolution;
uniform vec3 uZenithColor;
uniform vec3 uHorizonColor;
uniform vec3 uGroundColor;
uniform vec3 uSunDirection;
uniform float uSunIntensity;

void main() {
  vec2 uv = gl_FragCoord.xy / uResolution;
  vec4 farPoint = uInvViewProjection * vec4(uv * 2.0 - 1.0, 1.0, 1.0);
  vec3 direction = normalize(farPoint.xyz / farPoint.w - uCameraPosition);

  float height = direction.y;
  vec3 sky = mix(uHorizonColor, uZenithColor, pow(clamp(height, 0.0, 1.0), 0.45));
  sky = mix(uGroundColor, sky, smoothstep(-0.18, 0.02, height));

  vec3 sunDirection = normalize(uSunDirection);
  float alignment = max(dot(direction, sunDirection), 0.0);
  float disc = pow(alignment, 1800.0) * uSunIntensity;
  float halo = pow(alignment, 24.0) * 0.35 + pow(alignment, 4.0) * 0.06;

  sky += vec3(1.0, 0.82, 0.6) * disc + vec3(1.0, 0.7, 0.45) * halo;
  outColor = vec4(sky, 1.0);
}
`

export const postFragmentShader = /* glsl */ `#version 300 es
precision highp float;

in vec2 vUv;
out vec4 outColor;

uniform sampler2D uScene;
uniform float uExposure;
uniform float uVignette;
uniform float uGrain;
uniform float uTime;

vec3 acesFilm(vec3 color) {
  const float a = 2.51;
  const float b = 0.03;
  const float c = 2.43;
  const float d = 0.59;
  const float e = 0.14;
  return clamp((color * (a * color + b)) / (color * (c * color + d) + e), 0.0, 1.0);
}

float hash(vec2 point) {
  return fract(sin(dot(point, vec2(127.1, 311.7))) * 43758.5453);
}

void main() {
  vec3 color = texture(uScene, vUv).rgb * uExposure;
  color = acesFilm(color);

  float vignette = smoothstep(0.25, 0.9, distance(vUv, vec2(0.5)));
  color *= 1.0 - uVignette * vignette;

  color = pow(color, vec3(1.0 / 2.2));
  color += (hash(gl_FragCoord.xy + uTime) - 0.5) * uGrain;

  outColor = vec4(color, 1.0);
}
`
