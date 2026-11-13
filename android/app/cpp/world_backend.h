#pragma once

#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include <cstdint>
#include <string>
#include <vector>

#include "backend.h"
#include "gles_backend.h"

/** Один блок мира: позиция и тайл в атласе (u, v — левый нижний угол тайла). */
struct WorldBlock {
    float x;
    float y;
    float z;
    float tileU;
    float tileV;
};

/**
 * Мир из блоков — нагрузка, похожая на игровую: тысячи одинаковых кубов с
 * текстурами, туман, отсечение и батчинг.
 *
 * optimized = false — наивный путь, как рисует большинство простых движков:
 *   каждый блок отдельным draw-вызовом со своей матрицей в uniform;
 *   отсечения нет, рисуется весь мир;
 * optimized = true — путь roSkos:
 *   отсечение по фрустуму на CPU, потом ОДИН инстансный draw-вызов на весь
 *   видимый мир, а данные блоков едут в динамическом VBO;
 *   при уменьшении разрешения (грубые пиксели) дальность мира режется, чтобы
 *   вершинная нагрузка падала вместе с пиксельной.
 */
class WorldBackend final : public Backend {
public:
    explicit WorldBackend(bool optimized) : optimized_(optimized) {}

    ~WorldBackend() override;

    bool init(ANativeWindow* window, int width, int height, bool vsync, int coarseScale,
              bool vertexLighting, std::string& error) override;
    void resize(int width, int height) override;
    void frame(float time) override;
    const char* name() const override { return displayName_.c_str(); }

private:
    void destroyGlResources();
    bool buildProgram(std::string& error);
    void buildWorld();
    void uploadAtlas();
    void uploadStaticGeometry();
    void updateCamera(float time);
    int cullBlocks(float maxDistance);

    bool optimized_ = true;
    std::string displayName_ = "мир";

    EglState egl_;
    CoarseTarget coarseTarget_;
    int width_ = 1;
    int height_ = 1;
    int coarseScale_ = 1;
    bool ready_ = false;

    GLuint program_ = 0;
    GLuint vao_ = 0;
    GLuint vertexBuffer_ = 0;
    GLuint indexBuffer_ = 0;
    GLuint instanceBuffer_ = 0;
    GLuint atlasTexture_ = 0;
    uint32_t indexCount_ = 0;

    GLint uViewProjection_ = -1;
    GLint uModel_ = -1;
    GLint uTileOffset_ = -1;
    GLint uCameraPosition_ = -1;
    GLint uFogColor_ = -1;
    GLint uFogRange_ = -1;
    GLint uAtlas_ = -1;
    GLint uInstanced_ = -1;

    std::vector<WorldBlock> blocks_;
    std::vector<float> instanceData_;

    float cameraPosition_[3] = {0.0f, 14.0f, 26.0f};
    float viewProjection_[16] = {};
    float frustumPlanes_[6][4] = {};
};
