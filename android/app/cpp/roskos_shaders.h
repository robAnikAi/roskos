#pragma once

/**
 * GLSL ES 3.00 для «нашего рендера» (roSkos).
 *
 * Отличия от наивного GLES-пути (gles_shaders.h):
 *  - одна инстансная отрисовка на всю сцену: матрица модели и нормальная
 *    матрица едут как per-instance атрибуты, а не как uniform на каждый draw;
 *  - позиции источников света считаются раз в кадр на CPU (ядро отдаёт их
 *    в командном потоке), а не 3 sin/cos на каждый фрагмент;
 *  - pow(x, 64) и pow(x, 3) заменены на цепочки умножений — те же значения,
 *    без exp2/log2 на фрагмент.
 */

static constexpr int kRoskosLightCount = 3;

static const char* kRoskosVertexShader = R"GLSL(#version 300 es
precision highp float;

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;

layout(location = 3) in vec4 aModel0;
layout(location = 4) in vec4 aModel1;
layout(location = 5) in vec4 aModel2;
layout(location = 6) in vec4 aModel3;
layout(location = 7) in vec3 aNormalRow0;
layout(location = 8) in vec3 aNormalRow1;
layout(location = 9) in vec3 aNormalRow2;

uniform mat4 uViewProjection;

out vec3 vWorld;
out vec3 vNormal;
out vec2 vUv;

void main() {
    mat4 model = mat4(aModel0, aModel1, aModel2, aModel3);
    mat3 normalMatrix = mat3(aNormalRow0, aNormalRow1, aNormalRow2);

    vec4 world = model * vec4(aPosition, 1.0);
    vWorld = world.xyz;
    vNormal = normalMatrix * aNormal;
    vUv = aUv;
    gl_Position = uViewProjection * world;
}
)GLSL";

static const char* kRoskosFragmentShader = R"GLSL(#version 300 es
precision highp float;

in vec3 vWorld;
in vec3 vNormal;
in vec2 vUv;
out vec4 outColor;

uniform vec3 uCameraPosition;
uniform vec3 uLightPosition[3];
uniform vec3 uLightColor[3];

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
        vec3 toLight = uLightPosition[i] - vWorld;
        float distanceToLight = length(toLight);
        vec3 L = toLight / max(distanceToLight, 0.0001);
        vec3 H = normalize(L + viewDir);
        float ndl = max(dot(normal, L), 0.0);
        float ndh = max(dot(normal, H), 0.0);

        // pow(ndh, 64.0) через возведение в квадрат
        float ndh2 = ndh * ndh;
        float ndh4 = ndh2 * ndh2;
        float ndh8 = ndh4 * ndh4;
        float ndh16 = ndh8 * ndh8;
        float ndh32 = ndh16 * ndh16;
        float spec = ndh32 * ndh32;

        float attenuation = 1.0 / (1.0 + distanceToLight * distanceToLight * 0.08);
        color += (albedo * ndl + vec3(0.55) * spec * ndl) * uLightColor[i] * attenuation;
    }

    // pow(1 - ndv, 3.0) через умножение
    float oneMinusNdv = 1.0 - max(dot(normal, viewDir), 0.0);
    float fresnel = oneMinusNdv * oneMinusNdv * oneMinusNdv;
    color += vec3(0.24, 0.34, 0.55) * fresnel * 0.3;

    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));
    outColor = vec4(color, 1.0);
}
)GLSL";
