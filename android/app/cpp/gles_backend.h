#pragma once

#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include <cstdint>
#include <string>

#include "backend.h"
#include "common.h"
#include "roskos_shaders.h"

/** Общие помощники GLES: программа и загрузка меша в VAO. */
GLuint glesLinkProgram(const char* vertexSource, const char* fragmentSource, std::string& error);
void glesUploadMesh(const DonutMesh& mesh, GLuint& vao, GLuint& vbo, GLuint& ibo);

/** Программа и её uniform-локации (у двух вариантов освещения они разные). */
struct ProgramState {
    GLuint program = 0;
    GLint viewProjection = -1;
    GLint model = -1;
    GLint normalMatrix = -1;
    GLint cameraPosition = -1;
    GLint lightPosition = -1;
    GLint lightColor = -1;
};

bool glesLinkState(ProgramState& state, const char* vertexSource, const char* fragmentSource,
                   std::string& error);

/** Тесселяция тора под масштаб рендера: пикселей в 16 раз меньше — вершин тоже. */
int lodMajorSegments(int coarseScale);
int lodMinorSegments(int coarseScale);

struct EglState {
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
};

bool createEglState(ANativeWindow* window, EglState& state, bool vsync, std::string& error);
void destroyEglState(EglState& state);

/**
 * «Грубые пиксели» (coarse shading): сцена рисуется в буфер в N раз меньший
 * по каждой оси, затем растягивается на экран.
 */
struct CoarseTarget {
    GLuint framebuffer = 0;
    GLuint colorTexture = 0;
    GLuint depthRenderbuffer = 0;
    int width = 0;
    int height = 0;

    bool ensure(int targetWidth, int targetHeight, std::string& error);
    void destroy();
    void begin() const;
    void blitToScreen(int screenWidth, int screenHeight) const;
};

/**
 * Прямой OpenGL ES 3.0: те же шейдеры и та же геометрия, что у roSkos,
 * но объекты рисуются по одному, а матрицы едут через uniform.
 * Это базовая линия для сравнения путей отправки.
 */
class GlesBackend final : public Backend {
public:
    ~GlesBackend() override;

    bool init(ANativeWindow* window, int width, int height, bool vsync, int coarseScale,
              bool vertexLighting, std::string& error) override;
    void resize(int width, int height) override;
    void frame(float time) override;
    const char* name() const override { return displayName_.c_str(); }

private:
    void destroyGlResources();
    bool buildPrograms(std::string& error);

    EglState egl_;
    CoarseTarget coarseTarget_;
    int width_ = 1;
    int height_ = 1;
    int coarseScale_ = 1;
    bool vertexLighting_ = true;
    std::string displayName_ = "OpenGL ES 3.0";

    ProgramState vertexLit_;
    ProgramState pixelLit_;

    GLuint vao_ = 0;
    GLuint vertexBuffer_ = 0;
    GLuint indexBuffer_ = 0;
    uint32_t indexCount_ = 0;
};

/** Сколько инстансов и сколько float на инстанс в общем буфере roSkos. */
static constexpr int kRoskosMaxInstances = 16;
static constexpr int kRoskosInstanceFloats = 28;  // mat4 (16) + mat3 (9) + 3 float выравнивания

/**
 * «Наш рендер»: та же сцена и те же шейдеры, что у GlesBackend, но вся сцена
 * уходит одним инстансным draw-вызовом, а матрицы лежат per-instance.
 * Плюс режим грубых пикселей (coarse shading) с апскейлом.
 */
class RoskosBackend final : public Backend {
public:
    ~RoskosBackend() override;

    bool init(ANativeWindow* window, int width, int height, bool vsync, int coarseScale,
              bool vertexLighting, std::string& error) override;
    void resize(int width, int height) override;
    void frame(float time) override;
    const char* name() const override { return displayName_.c_str(); }

private:
    void destroyGlResources();
    bool createInstanceBuffer(std::string& error);
    bool buildPrograms(std::string& error);

    EglState egl_;
    CoarseTarget coarseTarget_;
    int width_ = 1;
    int height_ = 1;
    int coarseScale_ = 1;
    bool vertexLighting_ = true;
    GLuint vao_ = 0;
    GLuint vertexBuffer_ = 0;
    GLuint indexBuffer_ = 0;
    GLuint instanceBuffer_ = 0;
    uint32_t indexCount_ = 0;

    ProgramState vertexLit_;
    ProgramState pixelLit_;
    std::string displayName_ = "roSkos (Rust core)";

    uint32_t nodes_[DONUT_COUNT] = {};
    uint32_t lights_[LIGHT_COUNT] = {};
    float instanceData_[kRoskosMaxInstances * kRoskosInstanceFloats] = {};
    float lastTime_ = 0.0f;
    bool sceneReady_ = false;
};
