#include <jni.h>

#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "backend.h"
#include "gles_backend.h"
#include "vulkan_backend.h"
#include "world_backend.h"

#define LOG_TAG "roskos-bench"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

namespace {

std::thread g_thread;
std::atomic<bool> g_running{false};
std::atomic<int> g_width{1};
std::atomic<int> g_height{1};
std::atomic<float> g_fps{0.0f};
std::atomic<float> g_fpsAverage{0.0f};
std::atomic<float> g_frameMs{0.0f};
// true — кадры ждут вертикальную синхронизацию (потолок = частота экрана),
// false — без ограничения, видно реальный потолок GPU.
std::atomic<bool> g_vsync{true};
// 1 — обычные пиксели, 2/4 — «грубые пиксели» (рендер в уменьшенный буфер + апскейл).
std::atomic<int> g_coarse{1};
// true — свет в вершинном шейдере (roSkos), false — в пиксельном.
std::atomic<bool> g_vertexLit{true};

std::mutex g_statusMutex;
std::string g_status = "ожидание";
std::string g_error;

void setStatus(const std::string& status) {
    std::lock_guard<std::mutex> lock(g_statusMutex);
    g_status = status;
}

void setError(const std::string& error) {
    std::lock_guard<std::mutex> lock(g_statusMutex);
    g_error = error;
}

std::string statusText() {
    std::lock_guard<std::mutex> lock(g_statusMutex);
    if (!g_error.empty()) return g_error;
    return g_status;
}

void stopThread() {
    g_running.store(false);
    if (g_thread.joinable()) g_thread.join();
}

std::unique_ptr<Backend> createBackend(int backendId) {
    if (backendId == 0) return std::unique_ptr<Backend>(new VulkanBackend());
    if (backendId == 2) return std::unique_ptr<Backend>(new RoskosBackend());
    if (backendId == 3) return std::unique_ptr<Backend>(new WorldBackend(false));
    if (backendId == 4) return std::unique_ptr<Backend>(new WorldBackend(true));
    return std::unique_ptr<Backend>(new GlesBackend());
}

void renderLoop(ANativeWindow* window, int backendId, bool vsync, int coarseScale,
                bool vertexLighting, int width, int height) {
    g_fps.store(0.0f);
    g_fpsAverage.store(0.0f);
    g_frameMs.store(0.0f);

    std::unique_ptr<Backend> backend = createBackend(backendId);
    std::string error;
    if (!backend->init(window, width, height, vsync, coarseScale, vertexLighting, error)) {
        setError(std::string(backend->name()) + ": " + error);
        backend.reset();
        ANativeWindow_release(window);
        g_running.store(false);
        return;
    }
    setStatus(backend->name());

    using clock = std::chrono::steady_clock;
    const auto start = clock::now();
    auto sampleStart = start;
    int framesInSample = 0;
    int totalFrames = 0;

    while (g_running.load(std::memory_order_relaxed)) {
        const float time = std::chrono::duration<float>(clock::now() - start).count();
        backend->resize(g_width.load(std::memory_order_relaxed),
                        g_height.load(std::memory_order_relaxed));
        backend->frame(time);

        ++framesInSample;
        ++totalFrames;

        const auto now = clock::now();
        const float elapsed = std::chrono::duration<float>(now - sampleStart).count();
        if (elapsed >= 0.5f) {
            g_fps.store(static_cast<float>(framesInSample) / elapsed);
            g_frameMs.store(elapsed * 1000.0f / static_cast<float>(framesInSample));
            g_fpsAverage.store(static_cast<float>(totalFrames) /
                               std::chrono::duration<float>(now - start).count());
            framesInSample = 0;
            sampleStart = now;
        }
    }

    // Контекст GL и Vulkan разрушаем на том же потоке, где создавали.
    backend.reset();
    ANativeWindow_release(window);

    g_fps.store(0.0f);
    g_fpsAverage.store(0.0f);
    g_frameMs.store(0.0f);
}

}  // namespace

extern "C" JNIEXPORT jboolean JNICALL
Java_com_roskos_bench_MainActivity_nativeStart(JNIEnv* env, jclass, jobject surface, jint backendId,
                                               jint width, jint height) {
    stopThread();

    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (window == nullptr) {
        setError("ANativeWindow_fromSurface вернул null");
        return JNI_FALSE;
    }

    g_width.store(width > 0 ? width : 1);
    g_height.store(height > 0 ? height : 1);
    g_running.store(true);

    g_thread = std::thread(renderLoop, window, static_cast<int>(backendId), g_vsync.load(),
                           g_coarse.load(), g_vertexLit.load(),
                           width > 0 ? width : 1, height > 0 ? height : 1);
    return JNI_TRUE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_roskos_bench_MainActivity_nativeStop(JNIEnv*, jclass) {
    stopThread();
}

extern "C" JNIEXPORT void JNICALL
Java_com_roskos_bench_MainActivity_nativeResize(JNIEnv*, jclass, jint width, jint height) {
    if (width > 0 && height > 0) {
        g_width.store(width);
        g_height.store(height);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_roskos_bench_MainActivity_nativeSetVsync(JNIEnv*, jclass, jboolean on) {
    g_vsync.store(on == JNI_TRUE);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_roskos_bench_MainActivity_nativeVsync(JNIEnv*, jclass) {
    return g_vsync.load() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_roskos_bench_MainActivity_nativeSetCoarse(JNIEnv*, jclass, jint scale) {
    const int value = static_cast<int>(scale);
    g_coarse.store(value >= 4 ? 4 : (value >= 2 ? 2 : 1));
}

extern "C" JNIEXPORT jint JNICALL
Java_com_roskos_bench_MainActivity_nativeCoarse(JNIEnv*, jclass) {
    return static_cast<jint>(g_coarse.load());
}

extern "C" JNIEXPORT void JNICALL
Java_com_roskos_bench_MainActivity_nativeSetVertexLit(JNIEnv*, jclass, jboolean on) {
    g_vertexLit.store(on == JNI_TRUE);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_roskos_bench_MainActivity_nativeVertexLit(JNIEnv*, jclass) {
    return g_vertexLit.load() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jfloat JNICALL
Java_com_roskos_bench_MainActivity_nativeFps(JNIEnv*, jclass) {
    return g_fps.load();
}

extern "C" JNIEXPORT jfloat JNICALL
Java_com_roskos_bench_MainActivity_nativeFpsAverage(JNIEnv*, jclass) {
    return g_fpsAverage.load();
}

extern "C" JNIEXPORT jfloat JNICALL
Java_com_roskos_bench_MainActivity_nativeFrameMs(JNIEnv*, jclass) {
    return g_frameMs.load();
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_roskos_bench_MainActivity_nativeStatus(JNIEnv* env, jclass) {
    return env->NewStringUTF(statusText().c_str());
}
