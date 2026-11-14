#pragma once

/**
 * GLES-путь для честного сравнения.
 *
 * Математика света здесь ТА ЖЕ, что в roSkos: те же формулы, те же константы,
 * те же фрагментные шейдеры (они общие, см. roskos_shaders.h). Отличается
 * только способ доставки данных на GPU:
 *   - здесь матрицы модели и нормали едут через uniform, а объект рисуется
 *     отдельным draw-вызовом;
 *   - в roSkos те же матрицы лежат per-instance атрибутами, и вся сцена
 *     уходит одним инстансным вызовом.
 * Поэтому разница в FPS — это разница путей отправки, а не шейдеров.
 */

static const char* kGlesVertexLitVertexShader = R"GLSL(#version 300 es
precision highp float;

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;

uniform mat4 uViewProjection;
uniform mat4 uModel;
uniform mat3 uNormalMatrix;
uniform vec3 uCameraPosition;
uniform vec3 uLightPosition[3];
uniform vec3 uLightColor[3];

out vec3 vColor;

void main() {
    vec4 world = uModel * vec4(aPosition, 1.0);
    vec3 normal = normalize(uNormalMatrix * aNormal);
    vec3 viewDir = normalize(uCameraPosition - world.xyz);

    float checker = mod(floor(aUv.x) + floor(aUv.y), 2.0);
    vec3 albedo = mix(vec3(0.42, 0.14, 0.06), vec3(0.96, 0.87, 0.72), checker);
    albedo *= 0.86 + 0.14 * sin((aUv.x + aUv.y) * 6.2831853);

    vec3 color = albedo * 0.07;

    for (int i = 0; i < 3; ++i) {
        vec3 toLight = uLightPosition[i] - world.xyz;
        float distanceToLight = length(toLight);
        vec3 L = toLight / max(distanceToLight, 0.0001);
        vec3 H = normalize(L + viewDir);
        float ndl = max(dot(normal, L), 0.0);
        float ndh = max(dot(normal, H), 0.0);

        float ndh2 = ndh * ndh;
        float ndh4 = ndh2 * ndh2;
        float ndh8 = ndh4 * ndh4;
        float ndh16 = ndh8 * ndh8;
        float ndh32 = ndh16 * ndh16;
        float spec = ndh32 * ndh32;

        float attenuation = 1.0 / (1.0 + distanceToLight * distanceToLight * 0.08);
        color += (albedo * ndl + vec3(0.55) * spec * ndl) * uLightColor[i] * attenuation;
    }

    float oneMinusNdv = 1.0 - max(dot(normal, viewDir), 0.0);
    color += vec3(0.24, 0.34, 0.55) * (oneMinusNdv * oneMinusNdv * oneMinusNdv) * 0.3;

    vColor = color;
    gl_Position = uViewProjection * world;
}
)GLSL";

static const char* kGlesPixelLitVertexShader = R"GLSL(#version 300 es
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
