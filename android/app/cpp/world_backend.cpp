#include "world_backend.h"

#include <android/log.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#define LOG_TAG "roskos-world"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {

constexpr int kWorldSize = 48;         // блоков по X и Z
constexpr int kMaxHeights = 4;         // высота рельефа
constexpr float kBlockScale = 0.9f;    // размер куба в мировых единицах
constexpr float kFogStart = 26.0f;
constexpr float kFogEnd = 62.0f;
constexpr int kMaxInstances = 30000;

struct CubeVertex {
    float x, y, z;
    float u, v;
    float shade;
};

// 6 граней по 4 вершины; затенение по граням, как в Minecraft (без источников света).
const CubeVertex kCubeVertices[24] = {
    // +X
    {0.5f, -0.5f, -0.5f, 0.0f, 1.0f, 0.80f},
    {0.5f, -0.5f, 0.5f, 1.0f, 1.0f, 0.80f},
    {0.5f, 0.5f, 0.5f, 1.0f, 0.0f, 0.80f},
    {0.5f, 0.5f, -0.5f, 0.0f, 0.0f, 0.80f},
    // -X
    {-0.5f, -0.5f, 0.5f, 0.0f, 1.0f, 0.80f},
    {-0.5f, -0.5f, -0.5f, 1.0f, 1.0f, 0.80f},
    {-0.5f, 0.5f, -0.5f, 1.0f, 0.0f, 0.80f},
    {-0.5f, 0.5f, 0.5f, 0.0f, 0.0f, 0.80f},
    // +Y
    {-0.5f, 0.5f, -0.5f, 0.0f, 0.0f, 1.00f},
    {0.5f, 0.5f, -0.5f, 1.0f, 0.0f, 1.00f},
    {0.5f, 0.5f, 0.5f, 1.0f, 1.0f, 1.00f},
    {-0.5f, 0.5f, 0.5f, 0.0f, 1.0f, 1.00f},
    // -Y
    {-0.5f, -0.5f, 0.5f, 0.0f, 0.0f, 0.45f},
    {0.5f, -0.5f, 0.5f, 1.0f, 0.0f, 0.45f},
    {0.5f, -0.5f, -0.5f, 1.0f, 1.0f, 0.45f},
    {-0.5f, -0.5f, -0.5f, 0.0f, 1.0f, 0.45f},
    // +Z
    {-0.5f, -0.5f, 0.5f, 0.0f, 1.0f, 0.88f},
    {-0.5f, 0.5f, 0.5f, 0.0f, 0.0f, 0.88f},
    {0.5f, 0.5f, 0.5f, 1.0f, 0.0f, 0.88f},
    {0.5f, -0.5f, 0.5f, 1.0f, 1.0f, 0.88f},
    // -Z
    {0.5f, -0.5f, -0.5f, 0.0f, 1.0f, 0.88f},
    {0.5f, 0.5f, -0.5f, 0.0f, 0.0f, 0.88f},
    {-0.5f, 0.5f, -0.5f, 1.0f, 0.0f, 0.88f},
    {-0.5f, -0.5f, -0.5f, 1.0f, 1.0f, 0.88f},
};

const uint16_t kCubeIndices[36] = {
    0, 1, 2, 0, 2, 3,
    4, 5, 6, 4, 6, 7,
    8, 9, 10, 8, 10, 11,
    12, 13, 14, 12, 14, 15,
    16, 17, 18, 16, 18, 19,
    20, 21, 22, 20, 22, 23,
};

const char* kWorldVertexShader = R"GLSL(#version 300 es
precision highp float;

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aUv;
layout(location = 2) in float aShade;
layout(location = 3) in vec3 aBlockPosition;
layout(location = 4) in vec2 aBlockTile;

uniform mat4 uViewProjection;
uniform mat4 uModel;
uniform vec2 uTileOffset;
uniform float uInstanced;
uniform vec3 uCameraPosition;
uniform vec2 uFogRange;

out vec2 vUv;
out float vShade;
out float vFog;

void main() {
    bool instanced = uInstanced > 0.5;
    vec3 base = instanced ? aBlockPosition : vec3(0.0);
    vec2 tile = instanced ? aBlockTile : uTileOffset;

    vec4 world = uModel * vec4(aPosition * 0.9 + base, 1.0);
    vUv = tile + aUv * 0.5;
    vShade = aShade;

    float distanceToCamera = length(world.xyz - uCameraPosition);
    vFog = clamp((distanceToCamera - uFogRange.x) / max(uFogRange.y - uFogRange.x, 0.001),
                 0.0, 1.0);

    gl_Position = uViewProjection * world;
}
)GLSL";

const char* kWorldFragmentShader = R"GLSL(#version 300 es
precision highp float;

in vec2 vUv;
in float vShade;
in float vFog;
out vec4 outColor;

uniform sampler2D uAtlas;
uniform vec3 uFogColor;

void main() {
    vec3 texel = texture(uAtlas, vUv).rgb;
    outColor = vec4(mix(texel * vShade, uFogColor, vFog), 1.0);
}
)GLSL";

uint32_t hashCoords(int x, int z) {
    uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(z) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

/** Атлас 64x64: четыре тайла 32x32 — трава, земля, камень, дерево. */
void fillAtlas(std::vector<uint8_t>& pixels) {
    constexpr int kAtlasSize = 64;
    constexpr int kTileSize = 32;
    pixels.assign(kAtlasSize * kAtlasSize * 4, 255);

    struct TileStyle {
        int r, g, b, vary;
        bool stripes;
    };
    const TileStyle styles[4] = {
        {96, 168, 78, 26, false},   // трава
        {124, 92, 58, 22, false},   // земля
        {128, 128, 132, 20, false}, // камень
        {150, 112, 66, 16, true},   // дерево
    };

    for (int tile = 0; tile < 4; ++tile) {
        const int originX = (tile % 2) * kTileSize;
        const int originY = (tile / 2) * kTileSize;
        const TileStyle& style = styles[tile];
        for (int y = 0; y < kTileSize; ++y) {
            for (int x = 0; x < kTileSize; ++x) {
                const uint32_t noise = hashCoords(x + tile * 97, y + tile * 131);
                int delta = static_cast<int>(noise % static_cast<uint32_t>(style.vary * 2 + 1)) -
                            style.vary;
                if (style.stripes) delta += ((x / 4) % 2 == 0) ? 10 : -10;
                const size_t index =
                    (static_cast<size_t>(originY + y) * kAtlasSize + (originX + x)) * 4;
                pixels[index + 0] = static_cast<uint8_t>(std::clamp(style.r + delta, 0, 255));
                pixels[index + 1] = static_cast<uint8_t>(std::clamp(style.g + delta, 0, 255));
                pixels[index + 2] = static_cast<uint8_t>(std::clamp(style.b + delta, 0, 255));
                pixels[index + 3] = 255;
            }
        }
    }
}

void extractFrustumPlanes(const float* m, float planes[6][4]) {
    float rows[4][4];
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) rows[r][c] = m[c * 4 + r];
    }
    for (int axis = 0; axis < 3; ++axis) {
        for (int side = 0; side < 2; ++side) {
            const float sign = side == 0 ? 1.0f : -1.0f;
            float* plane = planes[axis * 2 + side];
            for (int c = 0; c < 4; ++c) plane[c] = rows[3][c] + sign * rows[axis][c];

            const float length =
                std::sqrt(plane[0] * plane[0] + plane[1] * plane[1] + plane[2] * plane[2]);
            if (length > 1e-6f) {
                for (int c = 0; c < 4; ++c) plane[c] /= length;
            }
        }
    }
}

}  // namespace

WorldBackend::~WorldBackend() {
    destroyGlResources();
    destroyEglState(egl_);
}

void WorldBackend::destroyGlResources() {
    coarseTarget_.destroy();
    if (program_) glDeleteProgram(program_);
    if (vertexBuffer_) glDeleteBuffers(1, &vertexBuffer_);
    if (indexBuffer_) glDeleteBuffers(1, &indexBuffer_);
    if (instanceBuffer_) glDeleteBuffers(1, &instanceBuffer_);
    if (atlasTexture_) glDeleteTextures(1, &atlasTexture_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    program_ = vao_ = vertexBuffer_ = indexBuffer_ = instanceBuffer_ = atlasTexture_ = 0;
}

bool WorldBackend::buildProgram(std::string& error) {
    program_ = glesLinkProgram(kWorldVertexShader, kWorldFragmentShader, error);
    if (!program_) return false;

    uViewProjection_ = glGetUniformLocation(program_, "uViewProjection");
    uModel_ = glGetUniformLocation(program_, "uModel");
    uTileOffset_ = glGetUniformLocation(program_, "uTileOffset");
    uInstanced_ = glGetUniformLocation(program_, "uInstanced");
    uCameraPosition_ = glGetUniformLocation(program_, "uCameraPosition");
    uFogRange_ = glGetUniformLocation(program_, "uFogRange");
    uFogColor_ = glGetUniformLocation(program_, "uFogColor");
    uAtlas_ = glGetUniformLocation(program_, "uAtlas");
    return true;
}

void WorldBackend::buildWorld() {
    blocks_.clear();
    blocks_.reserve(static_cast<size_t>(kWorldSize) * kWorldSize * kMaxHeights);
    const float half = static_cast<float>(kWorldSize) * 0.5f;

    for (int x = 0; x < kWorldSize; ++x) {
        for (int z = 0; z < kWorldSize; ++z) {
            const uint32_t noise = hashCoords(x, z);
            const int height = 1 + static_cast<int>(noise % static_cast<uint32_t>(kMaxHeights));
            for (int y = 0; y < height; ++y) {
                int tile = 2;                          // камень
                if (y == height - 1) tile = 0;         // трава сверху
                else if (y >= height - 2) tile = 1;    // земля
                WorldBlock block;
                block.x = static_cast<float>(x) - half + 0.5f;
                block.y = static_cast<float>(y) + 0.5f;
                block.z = static_cast<float>(z) - half + 0.5f;
                block.tileU = static_cast<float>(tile % 2) * 0.5f;
                block.tileV = static_cast<float>(tile / 2) * 0.5f;
                blocks_.push_back(block);
            }
        }
    }
    instanceData_.assign(blocks_.size() * 5, 0.0f);
}

void WorldBackend::uploadAtlas() {
    std::vector<uint8_t> pixels;
    fillAtlas(pixels);

    glGenTextures(1, &atlasTexture_);
    glBindTexture(GL_TEXTURE_2D, atlasTexture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void WorldBackend::uploadStaticGeometry() {
    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);

    glGenBuffers(1, &vertexBuffer_);
    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(sizeof(kCubeVertices)),
                 kCubeVertices, GL_STATIC_DRAW);

    glGenBuffers(1, &indexBuffer_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(sizeof(kCubeIndices)),
                 kCubeIndices, GL_STATIC_DRAW);

    const GLsizei stride = static_cast<GLsizei>(sizeof(CubeVertex));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<const void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<const void*>(5 * sizeof(float)));

    glGenBuffers(1, &instanceBuffer_);
    glBindBuffer(GL_ARRAY_BUFFER, instanceBuffer_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(kMaxInstances * 5 * sizeof(float)),
                 nullptr, GL_DYNAMIC_DRAW);

    const GLsizei instanceStride = static_cast<GLsizei>(5 * sizeof(float));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, instanceStride,
                          reinterpret_cast<const void*>(0));
    glVertexAttribDivisor(3, 1);
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, instanceStride,
                          reinterpret_cast<const void*>(3 * sizeof(float)));
    glVertexAttribDivisor(4, 1);

    glBindVertexArray(0);
    indexCount_ = 36;
}

bool WorldBackend::init(ANativeWindow* window, int width, int height, bool vsync, int coarseScale,
                        bool vertexLighting, std::string& error) {
    (void)vertexLighting;  // в мире свет запечён в грани, как в Minecraft

    width_ = width > 0 ? width : 1;
    height_ = height > 0 ? height : 1;
    coarseScale_ = coarseScale > 1 ? coarseScale : 1;

    if (!createEglState(window, egl_, vsync, error)) return false;
    if (!buildProgram(error)) return false;

    buildWorld();
    uploadAtlas();
    uploadStaticGeometry();

    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    ready_ = true;
    displayName_ = optimized_ ? "Мир · наш путь" : "Мир · обычный путь";
    displayName_ += " · " + std::to_string(blocks_.size()) + " блоков";

    LOGE("world init ok: %s (optimized=%d blocks=%zu)", glGetString(GL_RENDERER),
         optimized_ ? 1 : 0, blocks_.size());
    return true;
}

void WorldBackend::resize(int width, int height) {
    width_ = width > 0 ? width : 1;
    height_ = height > 0 ? height : 1;
}

void WorldBackend::updateCamera(float time) {
    const float angle = time * 0.06f;
    const float radius = 27.0f;
    const float eye[3] = {std::cos(angle) * radius, 15.0f, std::sin(angle) * radius};
    const float center[3] = {0.0f, 2.0f, 0.0f};
    const float up[3] = {0.0f, 1.0f, 0.0f};

    float view[16];
    float projection[16];
    mat4LookAt(view, eye, center, up);
    mat4PerspectiveGL(projection, 60.0f * PI / 180.0f,
                      static_cast<float>(width_) / static_cast<float>(height_), 0.3f, 120.0f);
    mat4Multiply(viewProjection_, projection, view);

    cameraPosition_[0] = eye[0];
    cameraPosition_[1] = eye[1];
    cameraPosition_[2] = eye[2];
    extractFrustumPlanes(viewProjection_, frustumPlanes_);
}

int WorldBackend::cullBlocks(float maxDistance) {
    const float maxDistanceSq = maxDistance * maxDistance;
    const float camX = cameraPosition_[0];
    const float camY = cameraPosition_[1];
    const float camZ = cameraPosition_[2];
    int visible = 0;

    for (const WorldBlock& block : blocks_) {
        const float dx = block.x - camX;
        const float dy = block.y - camY;
        const float dz = block.z - camZ;
        if (maxDistanceSq < 1.0e9f && dx * dx + dy * dy + dz * dz > maxDistanceSq) continue;

        bool outside = false;
        for (int i = 0; i < 6; ++i) {
            const float* plane = frustumPlanes_[i];
            if (plane[0] * block.x + plane[1] * block.y + plane[2] * block.z + plane[3] < -1.0f) {
                outside = true;
                break;
            }
        }
        if (outside) continue;
        if (visible >= kMaxInstances) break;

        float* target = instanceData_.data() + static_cast<size_t>(visible) * 5;
        target[0] = block.x;
        target[1] = block.y;
        target[2] = block.z;
        target[3] = block.tileU;
        target[4] = block.tileV;
        ++visible;
    }
    return visible;
}

void WorldBackend::frame(float time) {
    if (!ready_ || egl_.display == EGL_NO_DISPLAY) return;

    updateCamera(time);

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

    glClearColor(0.55f, 0.68f, 0.86f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glUseProgram(program_);
    glUniformMatrix4fv(uViewProjection_, 1, GL_FALSE, viewProjection_);
    glUniform3fv(uCameraPosition_, 1, cameraPosition_);
    const float fogColor[3] = {0.55f, 0.68f, 0.86f};
    glUniform3fv(uFogColor_, 1, fogColor);
    const float fogRange[2] = {kFogStart, kFogEnd};
    glUniform2fv(uFogRange_, 1, fogRange);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlasTexture_);
    glUniform1i(uAtlas_, 0);

    glBindVertexArray(vao_);

    if (optimized_) {
        const float maxDistance =
            coarseScale_ >= 4 ? 34.0f : (coarseScale_ == 2 ? 46.0f : 1.0e9f);
        const int visibleCount = cullBlocks(maxDistance);

        glEnableVertexAttribArray(3);
        glEnableVertexAttribArray(4);
        glUniform1f(uInstanced_, 1.0f);
        const float identity[16] = {1.0f, 0.0f, 0.0f, 0.0f,
                                    0.0f, 1.0f, 0.0f, 0.0f,
                                    0.0f, 0.0f, 1.0f, 0.0f,
                                    0.0f, 0.0f, 0.0f, 1.0f};
        glUniformMatrix4fv(uModel_, 1, GL_FALSE, identity);

        glBindBuffer(GL_ARRAY_BUFFER, instanceBuffer_);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(kMaxInstances * 5 * sizeof(float)),
                     nullptr, GL_DYNAMIC_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0,
                        static_cast<GLsizeiptr>(visibleCount * 5 * sizeof(float)),
                        instanceData_.data());

        glDrawElementsInstanced(GL_TRIANGLES, static_cast<GLsizei>(indexCount_),
                                GL_UNSIGNED_SHORT, nullptr, visibleCount);
    } else {
        // наивный путь: блоками, со сменой состояния на каждый блок
        glDisableVertexAttribArray(3);
        glDisableVertexAttribArray(4);
        glUniform1f(uInstanced_, 0.0f);

        for (const WorldBlock& block : blocks_) {
            const float model[16] = {1.0f, 0.0f, 0.0f, 0.0f,
                                     0.0f, 1.0f, 0.0f, 0.0f,
                                     0.0f, 0.0f, 1.0f, 0.0f,
                                     block.x, block.y, block.z, 1.0f};
            glUniformMatrix4fv(uModel_, 1, GL_FALSE, model);
            glUniform2f(uTileOffset_, block.tileU, block.tileV);
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indexCount_),
                           GL_UNSIGNED_SHORT, nullptr);
        }
    }

    glBindVertexArray(0);

    if (coarse) {
        coarseTarget_.blitToScreen(width_, height_);
    }

    eglSwapBuffers(egl_.display, egl_.surface);
}
