#include "common.h"

const float kLightColors[LIGHT_COUNT][3] = {
    {1.0f, 0.62f, 0.32f},
    {0.32f, 0.72f, 1.0f},
    {0.78f, 0.42f, 1.0f},
};

void computeLightPositions(float time, float outPositions[LIGHT_COUNT * 3]) {
    for (int i = 0; i < LIGHT_COUNT; ++i) {
        const float phase = static_cast<float>(i) * (2.0f * PI / LIGHT_COUNT);
        outPositions[i * 3 + 0] = cosf(time * 0.6f + phase) * 4.0f;
        outPositions[i * 3 + 1] = sinf(time * 0.45f + phase) * 2.0f + 1.6f;
        outPositions[i * 3 + 2] = sinf(time * 0.5f + phase) * 4.0f;
    }
}

DonutMesh buildDonut(float radius, float tube, int majorSegments, int minorSegments) {
    DonutMesh mesh;
    mesh.vertices.reserve(static_cast<size_t>(majorSegments + 1) * (minorSegments + 1) * 8);
    mesh.indices.reserve(static_cast<size_t>(majorSegments) * minorSegments * 6);

    for (int i = 0; i <= majorSegments; ++i) {
        const float u = static_cast<float>(i) / static_cast<float>(majorSegments);
        const float theta = u * 2.0f * PI;
        const float ct = cosf(theta);
        const float st = sinf(theta);

        for (int j = 0; j <= minorSegments; ++j) {
            const float v = static_cast<float>(j) / static_cast<float>(minorSegments);
            const float phi = v * 2.0f * PI;
            const float cp = cosf(phi);
            const float sp = sinf(phi);

            // нормаль тора в точке (theta, phi)
            const float nx = ct * cp;
            const float ny = sp;
            const float nz = st * cp;

            mesh.vertices.push_back(ct * (radius + tube * cp));
            mesh.vertices.push_back(tube * sp);
            mesh.vertices.push_back(st * (radius + tube * cp));
            mesh.vertices.push_back(nx);
            mesh.vertices.push_back(ny);
            mesh.vertices.push_back(nz);
            mesh.vertices.push_back(u * 4.0f);
            mesh.vertices.push_back(v * 4.0f);
        }
    }

    const uint32_t stride = static_cast<uint32_t>(minorSegments + 1);
    for (int i = 0; i < majorSegments; ++i) {
        for (int j = 0; j < minorSegments; ++j) {
            const uint32_t a = static_cast<uint32_t>(i) * stride + static_cast<uint32_t>(j);
            const uint32_t b = a + stride;
            const uint32_t c = a + 1;
            const uint32_t d = b + 1;
            mesh.indices.push_back(a);
            mesh.indices.push_back(c);
            mesh.indices.push_back(b);
            mesh.indices.push_back(b);
            mesh.indices.push_back(c);
            mesh.indices.push_back(d);
        }
    }

    return mesh;
}

void mat4Identity(float* out) {
    memset(out, 0, sizeof(float) * 16);
    out[0] = out[5] = out[10] = out[15] = 1.0f;
}

void mat4Multiply(float* out, const float* a, const float* b) {
    float result[16];
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += a[k * 4 + row] * b[column * 4 + k];
            }
            result[column * 4 + row] = sum;
        }
    }
    memcpy(out, result, sizeof(result));
}

void mat4PerspectiveGL(float* out, float fovY, float aspect, float nearPlane, float farPlane) {
    const float f = 1.0f / tanf(fovY * 0.5f);
    const float nf = 1.0f / (nearPlane - farPlane);
    memset(out, 0, sizeof(float) * 16);
    out[0] = f / aspect;
    out[5] = f;
    out[10] = (farPlane + nearPlane) * nf;
    out[11] = -1.0f;
    out[14] = 2.0f * farPlane * nearPlane * nf;
}

/** Vulkan: глубина в [0, 1] и перевёрнутая ось Y (front face станет CW). */
void mat4PerspectiveVulkan(float* out, float fovY, float aspect, float nearPlane, float farPlane) {
    const float f = 1.0f / tanf(fovY * 0.5f);
    memset(out, 0, sizeof(float) * 16);
    out[0] = f / aspect;
    out[5] = -f;
    out[10] = farPlane / (nearPlane - farPlane);
    out[11] = -1.0f;
    out[14] = (farPlane * nearPlane) / (nearPlane - farPlane);
}

void mat4LookAt(float* out, const float* eye, const float* center, const float* up) {
    float z0 = eye[0] - center[0];
    float z1 = eye[1] - center[1];
    float z2 = eye[2] - center[2];
    float length = sqrtf(z0 * z0 + z1 * z1 + z2 * z2);
    const float inv = length > 0.0f ? 1.0f / length : 1.0f;
    z0 *= inv;
    z1 *= inv;
    z2 *= inv;

    float x0 = up[1] * z2 - up[2] * z1;
    float x1 = up[2] * z0 - up[0] * z2;
    float x2 = up[0] * z1 - up[1] * z0;
    length = sqrtf(x0 * x0 + x1 * x1 + x2 * x2);
    if (length == 0.0f) {
        x0 = 0.0f;
        x1 = 0.0f;
        x2 = 0.0f;
    } else {
        const float invX = 1.0f / length;
        x0 *= invX;
        x1 *= invX;
        x2 *= invX;
    }

    const float y0 = z1 * x2 - z2 * x1;
    const float y1 = z2 * x0 - z0 * x2;
    const float y2 = z0 * x1 - z1 * x0;

    out[0] = x0;
    out[1] = y0;
    out[2] = z0;
    out[3] = 0.0f;
    out[4] = x1;
    out[5] = y1;
    out[6] = z1;
    out[7] = 0.0f;
    out[8] = x2;
    out[9] = y2;
    out[10] = z2;
    out[11] = 0.0f;
    out[12] = -(x0 * eye[0] + x1 * eye[1] + x2 * eye[2]);
    out[13] = -(y0 * eye[0] + y1 * eye[1] + y2 * eye[2]);
    out[14] = -(z0 * eye[0] + z1 * eye[1] + z2 * eye[2]);
    out[15] = 1.0f;
}

void mat4Compose(float* out, const float* position, const float* rotation, float scale) {
    const float cx = cosf(rotation[0]);
    const float sx = sinf(rotation[0]);
    const float cy = cosf(rotation[1]);
    const float sy = sinf(rotation[1]);
    const float cz = cosf(rotation[2]);
    const float sz = sinf(rotation[2]);

    const float r00 = cy * cz;
    const float r01 = -cy * sz;
    const float r02 = sy;
    const float r10 = cx * sz + sx * sy * cz;
    const float r11 = cx * cz - sx * sy * sz;
    const float r12 = -sx * cy;
    const float r20 = sx * sz - cx * sy * cz;
    const float r21 = sx * cz + cx * sy * sz;
    const float r22 = cx * cy;

    out[0] = r00 * scale;
    out[1] = r10 * scale;
    out[2] = r20 * scale;
    out[3] = 0.0f;
    out[4] = r01 * scale;
    out[5] = r11 * scale;
    out[6] = r21 * scale;
    out[7] = 0.0f;
    out[8] = r02 * scale;
    out[9] = r12 * scale;
    out[10] = r22 * scale;
    out[11] = 0.0f;
    out[12] = position[0];
    out[13] = position[1];
    out[14] = position[2];
    out[15] = 1.0f;
}

void mat3NormalFromMat4(float* out, const float* model) {
    // Для матрицы без сдвига/скоса достаточно inverse-transpose верхнего 3x3.
    const float a00 = model[0], a01 = model[1], a02 = model[2];
    const float a10 = model[4], a11 = model[5], a12 = model[6];
    const float a20 = model[8], a21 = model[9], a22 = model[10];

    float b01 = a22 * a11 - a12 * a21;
    float b11 = -a22 * a10 + a12 * a20;
    float b21 = a21 * a10 - a11 * a20;

    float det = a00 * b01 + a01 * b11 + a02 * b21;
    if (fabsf(det) < 1e-8f) {
        out[0] = 1.0f; out[1] = 0.0f; out[2] = 0.0f;
        out[3] = 0.0f; out[4] = 1.0f; out[5] = 0.0f;
        out[6] = 0.0f; out[7] = 0.0f; out[8] = 1.0f;
        return;
    }
    det = 1.0f / det;

    // обратная матрица, затем транспонирование
    const float i00 = b01 * det;
    const float i01 = (-a22 * a01 + a02 * a21) * det;
    const float i02 = (a12 * a01 - a02 * a11) * det;
    const float i10 = b11 * det;
    const float i11 = (a22 * a00 - a02 * a20) * det;
    const float i12 = (-a12 * a00 + a02 * a10) * det;
    const float i20 = b21 * det;
    const float i21 = (-a21 * a00 + a01 * a20) * det;
    const float i22 = (a11 * a00 - a01 * a10) * det;

    out[0] = i00; out[1] = i10; out[2] = i20;
    out[3] = i01; out[4] = i11; out[5] = i21;
    out[6] = i02; out[7] = i12; out[8] = i22;
}

void updateInstances(float time, DonutInstance* instances) {
    for (int i = 0; i < DONUT_COUNT; ++i) {
        const float phase = static_cast<float>(i) * (2.0f * PI / DONUT_COUNT);
        const float angle = time * 0.35f + phase;
        instances[i].position[0] = cosf(angle) * 2.5f;
        instances[i].position[1] = sinf(time * 0.9f + phase) * 0.6f;
        instances[i].position[2] = sinf(angle) * 2.5f;
        instances[i].rotation[0] = time * 0.5f + phase;
        instances[i].rotation[1] = angle + 0.6f;
        instances[i].rotation[2] = 0.2f * sinf(time * 0.7f + phase);
        instances[i].scale = 0.8f;
    }
}

void buildCamera(bool vulkanClip, float aspect, float time, FrameData* frame) {
    const float camAngle = time * 0.08f;
    const float eye[3] = {cosf(camAngle) * 7.4f, 2.5f, sinf(camAngle) * 7.4f};
    const float center[3] = {0.0f, 0.0f, 0.0f};
    const float up[3] = {0.0f, 1.0f, 0.0f};

    float view[16];
    float projection[16];
    mat4LookAt(view, eye, center, up);
    if (vulkanClip) {
        mat4PerspectiveVulkan(projection, 50.0f * PI / 180.0f, aspect, 0.1f, 100.0f);
    } else {
        mat4PerspectiveGL(projection, 50.0f * PI / 180.0f, aspect, 0.1f, 100.0f);
    }
    mat4Multiply(frame->viewProjection, projection, view);
    frame->cameraPosition[0] = eye[0];
    frame->cameraPosition[1] = eye[1];
    frame->cameraPosition[2] = eye[2];
    frame->time = time;
}
