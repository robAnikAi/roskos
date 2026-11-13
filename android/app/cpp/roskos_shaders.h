#pragma once

/**
 * GLSL ES 3.00 для «нашего рендера» (roSkos).
 *
 * Общая часть: одна инстансная отрисовка на всю сцену, матрица модели и
 * нормальная матрица едут как per-instance атрибуты, позиции источников
 * света считаются раз в кадр на CPU (ядро отдаёт их в командном потоке).
 *
 * Два варианта освещения:
 *  - per-vertex (вершинный): весь свет считается в вершинном шейдере, пиксели
 *    только тонируются. На плотном торе (512x128 = 131k треугольников на
 *    пончик) интерполяция выглядит гладко, а работа падает в разы: вершин
 *    в 8 раз меньше, чем пикселей, и каждая вершина обслуживает много пикселей;
 *  - per-pixel (попиксельный): освещение на каждый фрагмент, как в GLES-пути.
 *
 * В обоих вариантах pow(x, 64) и pow(x, 3) заменены цепочками умножений.
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
uniform vec3 uCameraPosition;
uniform vec3 uLightPosition[3];
uniform vec3 uLightColor[3];

out vec3 vColor;

void main() {
    mat4 model = mat4(aModel0, aModel1, aModel2, aModel3);
    mat3 normalMatrix = mat3(aNormalRow0, aNormalRow1, aNormalRow2);

    vec4 world = model * vec4(aPosition, 1.0);
    vec3 normal = normalize(normalMatrix * aNormal);
    vec3 viewDir = normalize(uCameraPosition - world.xyz);

    // глазурь с шахматным рисунком и лёгкими полосами
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

/** Вершинный вариант: пиксель только тонируется и переводится в sRGB. */
static const char* kRoskosVertexLitFragmentShader = R"GLSL(#version 300 es
precision highp float;

in vec3 vColor;
out vec4 outColor;

void main() {
    vec3 color = vColor / (vColor + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));
    outColor = vec4(color, 1.0);
}
)GLSL";

static const char* kRoskosPixelLitVertexShader = R"GLSL(#version 300 es
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

static const char* kRoskosPixelLitFragmentShader = R"GLSL(#version 300 es
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
    float fresnel = oneMinusNdv * oneMinusNdv * oneMinusNdv;
    color += vec3(0.24, 0.34, 0.55) * fresnel * 0.3;

    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));
    outColor = vec4(color, 1.0);
}
)GLSL";
