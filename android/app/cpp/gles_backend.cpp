#include "gles_backend.h"

#include <android/log.h>

#include <algorithm>
#include <vector>

#include "gles_shaders.h"

#define LOG_TAG "roskos-gles"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// --- «грубые пиксели»: рендер в уменьшенный буфер + апскейл -----------------

bool CoarseTarget::ensure(int targetWidth, int targetHeight, std::string& error) {
    targetWidth = std::max(1, targetWidth);
    targetHeight = std::max(1, targetHeight);

    if (framebuffer != 0 && width == targetWidth && height == targetHeight) return true;
    destroy();

    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

    glGenTextures(1, &colorTexture);
    glBindTexture(GL_TEXTURE_2D, colorTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, targetWidth, targetHeight, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTexture, 0);

    glGenRenderbuffers(1, &depthRenderbuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, depthRenderbuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, targetWidth, targetHeight);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER,
                              depthRenderbuffer);

    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (status != GL_FRAMEBUFFER_COMPLETE) {
        LOGE("FBO грубого рендера неполный: 0x%x", status);
        destroy();
        error = "framebuffer грубого рендера неполный";
        return false;
    }

    width = targetWidth;
    height = targetHeight;
    return true;
}

void CoarseTarget::destroy() {
    if (framebuffer) glDeleteFramebuffers(1, &framebuffer);
    if (colorTexture) glDeleteTextures(1, &colorTexture);
    if (depthRenderbuffer) glDeleteRenderbuffers(1, &depthRenderbuffer);
    framebuffer = colorTexture = depthRenderbuffer = 0;
    width = height = 0;
}

void CoarseTarget::begin() const {
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glViewport(0, 0, width, height);
}

void CoarseTarget::blitToScreen(int screenWidth, int screenHeight) const {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glDisable(GL_DEPTH_TEST);
    glBlitFramebuffer(0, 0, width, height, 0, 0, screenWidth, screenHeight,
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glEnable(GL_DEPTH_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, screenWidth, screenHeight);
}

// --- EGL -------------------------------------------------------------------

bool createEglState(ANativeWindow* window, EglState& state, bool vsync, std::string& error) {
    state.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (state.display == EGL_NO_DISPLAY) {
        error = "eglGetDisplay вернул EGL_NO_DISPLAY";
        return false;
    }

    EGLint major = 0;
    EGLint minor = 0;
    if (!eglInitialize(state.display, &major, &minor)) {
        error = "eglInitialize не удался";
        return false;
    }

    const EGLint configAttributes[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 24,
        EGL_NONE,
    };

    EGLConfig config = nullptr;
    EGLint configCount = 0;
    if (!eglChooseConfig(state.display, configAttributes, &config, 1, &configCount) || configCount < 1) {
        error = "eglChooseConfig не нашёл конфигурацию ES3";
        return false;
    }

    state.surface = eglCreateWindowSurface(state.display, config, window, nullptr);
    if (state.surface == EGL_NO_SURFACE) {
        error = "eglCreateWindowSurface не удался";
        return false;
    }

    const EGLint contextAttributes[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    state.context = eglCreateContext(state.display, config, EGL_NO_CONTEXT, contextAttributes);
    if (state.context == EGL_NO_CONTEXT) {
        error = "eglCreateContext (ES3) не удался";
        return false;
    }

    if (!eglMakeCurrent(state.display, state.surface, state.surface, state.context)) {
        error = "eglMakeCurrent не удался";
        return false;
    }

    // vsync = false -> интервал 0, кадры не ждут вертикальной синхронизации
    eglSwapInterval(state.display, vsync ? 1 : 0);
    return true;
}

void destroyEglState(EglState& state) {
    if (state.display == EGL_NO_DISPLAY) return;
    eglMakeCurrent(state.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (state.context != EGL_NO_CONTEXT) eglDestroyContext(state.display, state.context);
    if (state.surface != EGL_NO_SURFACE) eglDestroySurface(state.display, state.surface);
    eglTerminate(state.display);
    state = EglState{};
}

static GLuint compileShader(GLenum type, const char* source, std::string& error) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLint length = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
        std::vector<char> log(static_cast<size_t>(length > 1 ? length : 1));
        glGetShaderInfoLog(shader, length, nullptr, log.data());
        error = std::string("ошибка компиляции шейдера: ") + log.data();
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint glesLinkProgram(const char* vertexSource, const char* fragmentSource, std::string& error) {
    const GLuint vertex = compileShader(GL_VERTEX_SHADER, vertexSource, error);
    if (!vertex) return 0;
    const GLuint fragment = compileShader(GL_FRAGMENT_SHADER, fragmentSource, error);
    if (!fragment) {
        glDeleteShader(vertex);
        return 0;
    }

    const GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);

    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        GLint length = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
        std::vector<char> log(static_cast<size_t>(length > 1 ? length : 1));
        glGetProgramInfoLog(program, length, nullptr, log.data());
        error = std::string("ошибка линковки программы: ") + log.data();
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

void glesUploadMesh(const DonutMesh& mesh, GLuint& vao, GLuint& vbo, GLuint& ibo) {
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(mesh.vertices.size() * sizeof(float)),
                 mesh.vertices.data(), GL_STATIC_DRAW);

    glGenBuffers(1, &ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(uint32_t)),
                 mesh.indices.data(), GL_STATIC_DRAW);

    const GLsizei stride = static_cast<GLsizei>(mesh.vertexStride());
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<const void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<const void*>(6 * sizeof(float)));

    glBindVertexArray(0);
}

// --- прямой GLES-бэкенд ----------------------------------------------------

GlesBackend::~GlesBackend() {
    destroyGlResources();
    destroyEglState(egl_);
}

void GlesBackend::destroyGlResources() {
    coarseTarget_.destroy();
    if (program_) glDeleteProgram(program_);
    if (vertexBuffer_) glDeleteBuffers(1, &vertexBuffer_);
    if (indexBuffer_) glDeleteBuffers(1, &indexBuffer_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    program_ = vao_ = vertexBuffer_ = indexBuffer_ = 0;
}

bool GlesBackend::init(ANativeWindow* window, int width, int height, bool vsync,
                       int coarseScale, bool vertexLighting, std::string& error) {
    width_ = width > 0 ? width : 1;
    height_ = height > 0 ? height : 1;
    coarseScale_ = coarseScale > 1 ? coarseScale : 1;
    (void)vertexLighting;  // у прямого GLES-пути свет попиксельный, как в оригинале

    if (!createEglState(window, egl_, vsync, error)) return false;

    program_ = glesLinkProgram(kGlesVertexShader, kGlesFragmentShader, error);
    if (!program_) return false;

    // 512 x 128 = 131 072 треугольника на пончик, четыре пончика в кадре.
    const DonutMesh mesh = buildDonut(1.0f, 0.38f, 512, 128);
    glesUploadMesh(mesh, vao_, vertexBuffer_, indexBuffer_);
    indexCount_ = mesh.indexCount();

    uViewProjection_ = glGetUniformLocation(program_, "uViewProjection");
    uModel_ = glGetUniformLocation(program_, "uModel");
    uNormalMatrix_ = glGetUniformLocation(program_, "uNormalMatrix");
    uCameraPosition_ = glGetUniformLocation(program_, "uCameraPosition");
    uTime_ = glGetUniformLocation(program_, "uTime");

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    LOGE("GLES init ok: %s (coarse=%dx)", glGetString(GL_RENDERER), coarseScale_);
    return true;
}

void GlesBackend::resize(int width, int height) {
    width_ = width > 0 ? width : 1;
    height_ = height > 0 ? height : 1;
}

void GlesBackend::frame(float time) {
    if (egl_.display == EGL_NO_DISPLAY) return;

    bool coarse = false;
    if (coarseScale_ > 1) {
        std::string error;
        coarse = coarseTarget_.ensure(width_ / coarseScale_, height_ / coarseScale_, error);
    }

    if (coarse) {
        coarseTarget_.begin();
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, width_, height_);
    }

    glClearColor(0.03f, 0.04f, 0.06f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    FrameData frameData;
    buildCamera(false, static_cast<float>(width_) / static_cast<float>(height_), time, &frameData);

    glUseProgram(program_);
    glUniformMatrix4fv(uViewProjection_, 1, GL_FALSE, frameData.viewProjection);
    glUniform3fv(uCameraPosition_, 1, frameData.cameraPosition);
    glUniform1f(uTime_, time);

    DonutInstance instances[DONUT_COUNT];
    updateInstances(time, instances);

    glBindVertexArray(vao_);
    for (int i = 0; i < DONUT_COUNT; ++i) {
        float model[16];
        float normal[9];
        mat4Compose(model, instances[i].position, instances[i].rotation, instances[i].scale);
        mat3NormalFromMat4(normal, model);
        glUniformMatrix4fv(uModel_, 1, GL_FALSE, model);
        glUniformMatrix3fv(uNormalMatrix_, 1, GL_FALSE, normal);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indexCount_), GL_UNSIGNED_INT, nullptr);
    }
    glBindVertexArray(0);

    if (coarse) {
        coarseTarget_.blitToScreen(width_, height_);
    }

    eglSwapBuffers(egl_.display, egl_.surface);
}
