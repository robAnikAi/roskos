#pragma once

/** GLSL ES 3.00: одинаковый пончик и одинаковое освещение для GLES и roSkos. */

static const char* kGlesVertexShader = R"GLSL(#version 300 es
precision highp float;

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;

uniform mat4 uViewProjection;
uniform mat4 uModel;
uniform mat3 uNormalMatrix;

out vec3 vWorld;
out vec3 vNormal;
out vec2 vUv;

void main() {
    vec4 world = uModel * vec4(aPosition, 1.0);
    vWorld = world.xyz;
    vNormal = uNormalMatrix * aNormal;
    vUv = aUv;
    gl_Position = uViewProjection * world;
}
)GLSL";

static const char* kGlesFragmentShader = R"GLSL(#version 300 es
precision highp float;

in vec3 vWorld;
in vec3 vNormal;
in vec2 vUv;
out vec4 outColor;

uniform vec3 uCameraPosition;
uniform float uTime;

vec3 lightPosition(int index) {
    float phase = float(index) * 2.0943951;
    return vec3(
        cos(uTime * 0.6 + phase) * 4.0,
        sin(uTime * 0.45 + phase) * 2.0 + 1.6,
        sin(uTime * 0.5 + phase) * 4.0
    );
}

vec3 lightColor(int index) {
    if (index == 0) return vec3(1.0, 0.62, 0.32);
    if (index == 1) return vec3(0.32, 0.72, 1.0);
    return vec3(0.78, 0.42, 1.0);
}

void main() {
    vec3 normal = normalize(vNormal);
    vec3 viewDir = normalize(uCameraPosition - vWorld);

    // глазурь с шахматным рисунком и лёгкими полосами
    vec2 uv = vUv;
    float checker = mod(floor(uv.x) + floor(uv.y), 2.0);
    vec3 albedo = mix(vec3(0.42, 0.14, 0.06), vec3(0.96, 0.87, 0.72), checker);
    albedo *= 0.86 + 0.14 * sin((uv.x + uv.y) * 6.2831853);

    vec3 color = albedo * 0.07;

    for (int i = 0; i < 3; ++i) {
        vec3 toLight = lightPosition(i) - vWorld;
        float distanceToLight = length(toLight);
        vec3 L = toLight / max(distanceToLight, 0.0001);
        vec3 H = normalize(L + viewDir);
        float ndl = max(dot(normal, L), 0.0);
        float spec = pow(max(dot(normal, H), 0.0), 64.0);
        float attenuation = 1.0 / (1.0 + distanceToLight * distanceToLight * 0.08);
        color += (albedo * ndl + vec3(0.55) * spec * ndl) * lightColor(i) * attenuation;
    }

    float fresnel = pow(1.0 - max(dot(normal, viewDir), 0.0), 3.0);
    color += vec3(0.24, 0.34, 0.55) * fresnel * 0.3;

    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));
    outColor = vec4(color, 1.0);
}
)GLSL";
