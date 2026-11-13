#pragma once

#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include <cstdint>

#include "backend.h"
#include "common.h"
#include "roskos_shaders.h"

/** Общие помощники GLES: программа и загрузка меша в VAO. */
GLuint glesLinkProgram(const char* vertexSource, const char* fragmentSource, std::string& error);
void glesUploadMesh(const DonutMesh& mesh, GLuint& vao, GLuint& vbo, GLuint& ibo);

struct EglState {
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
};

bool createEglState(ANativeWindow* window, EglState& state, bool vsync, std::string& error);
void destroyEglState(EglState& state);

/** Прямой OpenGL ES 3.0: свой EGL-контекст, свой шейдер, свои матрицы. */
class GlesBackend final : public Backend {
public:
    ~GlesBackend() override;

    bool init(ANativeWindow* window, int width, int height, bool vsync,
              std::string& error) override;
    void resize(int width, int height) override;
    void frame(float time) override;
    const char* name() const override { return "OpenGL ES 3.0"; }

private:
    void destroyGlResources();

    EglState egl_;
    int width_ = 1;
    int height_ = 1;
    GLuint program_ = 0;
    GLuint vao_ = 0;
    GLuint vertexBuffer_ = 0;
    GLuint indexBuffer_ = 0;
    uint32_t indexCount_ = 0;

    GLint uViewProjection_ = -1;
    GLint uModel_ = -1;
    GLint uNormalMatrix_ = -1;
    GLint uCameraPosition_ = -1;
    GLint uTime_ = -1;
};

/** Сколько инстансов и сколько float на инстанс в общем буфере. */
static constexpr int kRoskosMaxInstances = 16;
static constexpr int kRoskosInstanceFloats = 28;  // mat4 (16) + mat3 (9) + 3 float выравнивания

/**
 * «Наш рендер»: ядро на Rust строит командный поток, GLES его исполняет.
 *
 * Конвейер batched-first: вся сцена уходит в GPU одним инстансным draw-вызовом,
 * матрицы инстансов лежат в одном динамическом VBO, свет считается на CPU.
 */
class RoskosBackend final : public Backend {
public:
    ~RoskosBackend() override;

    bool init(ANativeWindow* window, int width, int height, bool vsync,
              std::string& error) override;
    void resize(int width, int height) override;
    void frame(float time) override;
    const char* name() const override { return "roSkos (Rust core)"; }

private:
    void destroyGlResources();
    bool createInstanceBuffer(std::string& error);
    void updateLights(float time);

    EglState egl_;
    int width_ = 1;
    int height_ = 1;
    GLuint program_ = 0;
    GLuint vao_ = 0;
    GLuint vertexBuffer_ = 0;
    GLuint indexBuffer_ = 0;
    GLuint instanceBuffer_ = 0;
    uint32_t indexCount_ = 0;

    GLint uViewProjection_ = -1;
    GLint uCameraPosition_ = -1;
    GLint uLightPosition_ = -1;
    GLint uLightColor_ = -1;

    uint32_t nodes_[DONUT_COUNT] = {};
    uint32_t lights_[kRoskosLightCount] = {};
    float instanceData_[kRoskosMaxInstances * kRoskosInstanceFloats] = {};
    float lastTime_ = 0.0f;
    bool sceneReady_ = false;
};
