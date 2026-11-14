#include "gles_backend.h"

#include <android/log.h>

#include <algorithm>
#include <cstring>

#include "roskos_abi.h"

#define LOG_TAG "roskos-core"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// Формат командного потока ядра, см. crates/roskos-core/src/frame.rs
static constexpr uint32_t kFrameStride = 65;
static constexpr uint32_t kLightStride = 7;
static constexpr uint32_t kDrawStride = 39;
static constexpr uint32_t kCommandFrame = 1;
static constexpr uint32_t kCommandLight = 2;
static constexpr uint32_t kCommandDraw = 3;

static constexpr size_t kInstanceStride = kRoskosInstanceFloats * sizeof(float);

RoskosBackend::~RoskosBackend() {
    destroyGlResources();
    destroyEglState(egl_);
}

void RoskosBackend::destroyGlResources() {
    coarseTarget_.destroy();
    if (vertexLit_.program) glDeleteProgram(vertexLit_.program);
    if (pixelLit_.program) glDeleteProgram(pixelLit_.program);
    if (vertexBuffer_) glDeleteBuffers(1, &vertexBuffer_);
    if (indexBuffer_) glDeleteBuffers(1, &indexBuffer_);
    if (instanceBuffer_) glDeleteBuffers(1, &instanceBuffer_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    vertexLit_ = ProgramState{};
    pixelLit_ = ProgramState{};
    vao_ = vertexBuffer_ = indexBuffer_ = instanceBuffer_ = 0;
}

bool RoskosBackend::buildPrograms(std::string& error) {
    if (!glesLinkState(pixelLit_, kRoskosPixelLitVertexShader, kRoskosPixelLitFragmentShader,
                       error)) {
        return false;
    }
    std::string vertexError;
    if (vertexLighting_ &&
        !glesLinkState(vertexLit_, kRoskosVertexShader, kRoskosVertexLitFragmentShader,
                       vertexError)) {
        // вершинный вариант не собрался — честно откатываемся на попиксельный
        LOGE("вершинное освещение не собралось, откат на попиксельное: %s",
             vertexError.c_str());
        vertexLighting_ = false;
    }
    return true;
}

/**
 * Per-instance атрибуты: 4 колонки модели + 3 колонки нормальной матрицы.
 * Буфер обновляется один раз в кадр через glBufferData (orphaning): старый
 * блок памяти отдаётся драйверу и не ждёт, пока GPU дочитает его, поэтому
 * CPU не встаёт в синхронизацию с кадром, который ещё рисуется.
 */
bool RoskosBackend::createInstanceBuffer(std::string& error) {
    glGenBuffers(1, &instanceBuffer_);
    if (instanceBuffer_ == 0) {
        error = "не удалось создать буфер инстансов";
        return false;
    }

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, instanceBuffer_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(kRoskosMaxInstances * kInstanceStride),
                 nullptr,
                 GL_DYNAMIC_DRAW);

    const GLsizei stride = static_cast<GLsizei>(kInstanceStride);
    for (int i = 0; i < 4; ++i) {
        const GLuint location = static_cast<GLuint>(3 + i);
        glEnableVertexAttribArray(location);
        glVertexAttribPointer(location, 4, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<const void*>(static_cast<uintptr_t>(i * 16)));
        glVertexAttribDivisor(location, 1);
    }
    for (int i = 0; i < 3; ++i) {
        const GLuint location = static_cast<GLuint>(7 + i);
        glEnableVertexAttribArray(location);
        glVertexAttribPointer(location, 3, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<const void*>(static_cast<uintptr_t>(64 + i * 12)));
        glVertexAttribDivisor(location, 1);
    }

    glBindVertexArray(0);
    return true;
}

bool RoskosBackend::init(ANativeWindow* window, int width, int height, bool vsync,
                         int coarseScale, bool vertexLighting, std::string& error) {
    width_ = width > 0 ? width : 1;
    height_ = height > 0 ? height : 1;
    coarseScale_ = coarseScale > 1 ? coarseScale : 1;
    vertexLighting_ = vertexLighting;

    if (!createEglState(window, egl_, vsync, error)) return false;
    if (!buildPrograms(error)) return false;

    // та же тесселяция, что у базового GLES-пути: сравнение честное
    const int majorSegments = lodMajorSegments(coarseScale_);
    const int minorSegments = lodMinorSegments(coarseScale_);
    const DonutMesh mesh = buildDonut(1.0f, 0.38f, majorSegments, minorSegments);
    glesUploadMesh(mesh, vao_, vertexBuffer_, indexBuffer_);
    indexCount_ = mesh.indexCount();

    if (!createInstanceBuffer(error)) return false;

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    // Сцена создаётся в Rust-ядре: четыре узла с общим мешем и три источника света.
    roskos_reset();
    roskos_set_viewport(static_cast<uint32_t>(width_), static_cast<uint32_t>(height_));
    for (int i = 0; i < DONUT_COUNT; ++i) {
        nodes_[i] = roskos_add_node(0);
    }
    for (int i = 0; i < LIGHT_COUNT; ++i) {
        lights_[i] = roskos_add_light(kLightColors[i][0], kLightColors[i][1], kLightColors[i][2]);
    }

    sceneReady_ = true;
    lastTime_ = 0.0f;

    displayName_ = "roSkos · сетка " + std::to_string(majorSegments) + "×" +
                   std::to_string(minorSegments) + " · " + std::to_string(mesh.vertexCount()) +
                   " верш.";

    LOGE("roSkos batched pipeline: %s (light=%s coarse=%dx mesh=%dx%d verts=%u)",
         glGetString(GL_RENDERER), vertexLighting_ ? "vertex" : "pixel", coarseScale_,
         majorSegments, minorSegments, mesh.vertexCount());
    return true;
}

void RoskosBackend::resize(int width, int height) {
    width_ = width > 0 ? width : 1;
    height_ = height > 0 ? height : 1;
    if (sceneReady_) {
        roskos_set_viewport(static_cast<uint32_t>(width_), static_cast<uint32_t>(height_));
    }
}

void RoskosBackend::frame(float time) {
    if (!sceneReady_ || egl_.display == EGL_NO_DISPLAY) return;

    const float dt = std::min(std::max(time - lastTime_, 0.0001f), 0.1f);
    lastTime_ = time;

    DonutInstance instances[DONUT_COUNT];
    updateInstances(time, instances);

    for (int i = 0; i < DONUT_COUNT; ++i) {
        roskos_set_transform(nodes_[i],
                             instances[i].position[0], instances[i].position[1], instances[i].position[2],
                             instances[i].rotation[0], instances[i].rotation[1], instances[i].rotation[2],
                             instances[i].scale, instances[i].scale, instances[i].scale);
    }

    // позиции света считаем на CPU (те же формулы, что у GLES-пути) и отдаём ядру
    float lightPositions[LIGHT_COUNT * 3];
    computeLightPositions(time, lightPositions);
    for (int i = 0; i < LIGHT_COUNT; ++i) {
        roskos_set_light(lights_[i], lightPositions[i * 3 + 0], lightPositions[i * 3 + 1],
                         lightPositions[i * 3 + 2],
                         kLightColors[i][0], kLightColors[i][1], kLightColors[i][2]);
    }

    // камера живёт в ядре; медленно облетаем сцену
    roskos_camera_orbit(dt * 0.08f, 0.0f);
    roskos_camera_update(dt);
    roskos_build_frame(static_cast<double>(time));

    const float* commands = roskos_frame_ptr();
    const uint32_t count = roskos_command_count();
    if (commands == nullptr || count < kFrameStride) return;
    if (static_cast<uint32_t>(commands[0]) != kCommandFrame) return;

    float lightColors[LIGHT_COUNT * 3] = {};
    uint32_t lightCount = 0;
    uint32_t instanceCount = 0;

    uint32_t offset = kFrameStride;
    while (offset + 1 < count) {
        const uint32_t type = static_cast<uint32_t>(commands[offset]);
        if (type == kCommandLight) {
            if (lightCount < LIGHT_COUNT) {
                std::memcpy(&lightPositions[lightCount * 3], commands + offset + 1, 3 * sizeof(float));
                std::memcpy(&lightColors[lightCount * 3], commands + offset + 4, 3 * sizeof(float));
                ++lightCount;
            }
            offset += kLightStride;
            continue;
        }
        if (type != kCommandDraw) break;
        if (instanceCount >= kRoskosMaxInstances) break;

        float* dst = instanceData_ + instanceCount * kRoskosInstanceFloats;
        std::memcpy(dst, commands + offset + 2, 16 * sizeof(float));
        std::memcpy(dst + 16, commands + offset + 18, 9 * sizeof(float));
        dst[25] = 0.0f;
        dst[26] = 0.0f;
        dst[27] = 0.0f;

        ++instanceCount;
        offset += kDrawStride;
    }
    if (instanceCount == 0) return;

    // грубые пиксели: рисуем в уменьшенный буфер, потом растягиваем
    bool coarse = false;
    if (coarseScale_ > 1) {
        std::string fboError;
        coarse = coarseTarget_.ensure(width_ / coarseScale_, height_ / coarseScale_, fboError);
    }
    if (coarse) {
        coarseTarget_.begin();
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, width_, height_);
    }

    glClearColor(0.03f, 0.04f, 0.06f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const ProgramState& active = vertexLighting_ ? vertexLit_ : pixelLit_;
    glUseProgram(active.program);
    glUniformMatrix4fv(active.viewProjection, 1, GL_FALSE, commands + 1);
    glUniform3fv(active.cameraPosition, 1, commands + 33);
    glUniform3fv(active.lightPosition, static_cast<GLsizei>(lightCount), lightPositions);
    glUniform3fv(active.lightColor, static_cast<GLsizei>(lightCount), lightColors);

    // orphaning: новая память под кадр вместо ожидания предыдущего кадра
    glBindBuffer(GL_ARRAY_BUFFER, instanceBuffer_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(kRoskosMaxInstances * kInstanceStride),
                 nullptr, GL_DYNAMIC_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    static_cast<GLsizeiptr>(instanceCount * kInstanceStride), instanceData_);

    glBindVertexArray(vao_);
    glDrawElementsInstanced(GL_TRIANGLES, static_cast<GLsizei>(indexCount_),
                            GL_UNSIGNED_INT, nullptr, static_cast<GLsizei>(instanceCount));
    glBindVertexArray(0);

    if (coarse) {
        coarseTarget_.blitToScreen(width_, height_);
    }

    eglSwapBuffers(egl_.display, egl_.surface);
}
