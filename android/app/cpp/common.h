#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

/** Общая «сцена» для всех трёх бэкендов: один и тот же пончик, одни и те же матрицы. */

constexpr int DONUT_COUNT = 4;
constexpr float PI = 3.14159265358979f;

struct DonutMesh {
    std::vector<float> vertices;   // pos(3) + normal(3) + uv(2)
    std::vector<uint32_t> indices;

    uint32_t indexCount() const { return static_cast<uint32_t>(indices.size()); }
    uint32_t vertexCount() const { return static_cast<uint32_t>(vertices.size() / 8); }
    uint32_t vertexStride() const { return 8 * sizeof(float); }
};

struct DonutInstance {
    float position[3];
    float rotation[3];
    float scale;
};

struct FrameData {
    float viewProjection[16];
    float cameraPosition[3];
    float time;
};

/** Тяжёлый тор: высокая тесселяция, чтобы нагрузить вершинный конвейер. */
DonutMesh buildDonut(float radius, float tube, int majorSegments, int minorSegments);

void mat4Identity(float* out);
void mat4Multiply(float* out, const float* a, const float* b);
void mat4PerspectiveGL(float* out, float fovY, float aspect, float nearPlane, float farPlane);
void mat4PerspectiveVulkan(float* out, float fovY, float aspect, float nearPlane, float farPlane);
void mat4LookAt(float* out, const float* eye, const float* center, const float* up);
void mat4Compose(float* out, const float* position, const float* rotation, float scale);
void mat3NormalFromMat4(float* out, const float* model);

void updateInstances(float time, DonutInstance* instances);
void buildCamera(bool vulkanClip, float aspect, float time, FrameData* frame);
