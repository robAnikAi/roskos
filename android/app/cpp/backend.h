#pragma once

#include <android/native_window.h>

#include <string>

/**
 * Бэкенд рендеринга пончика. Все три реализации получают одно и то же окно
 * и рисуют одну и ту же сцену, чтобы FPS был сопоставимым.
 *
 * vsync = false отключает ограничение частотой обновления: только в этом
 * режиме FPS показывает реальный потолок GPU, а не потолок экрана.
 */
class Backend {
public:
    virtual ~Backend() = default;

    virtual bool init(ANativeWindow* window, int width, int height, bool vsync,
                      std::string& error) = 0;
    virtual void resize(int width, int height) = 0;
    virtual void frame(float time) = 0;
    virtual const char* name() const = 0;
};
