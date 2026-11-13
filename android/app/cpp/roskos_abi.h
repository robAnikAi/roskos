#pragma once

#include <cstdint>

/**
 * Подмножество C-ABI ядра roSkos (crates/roskos-core), которое использует
 * Android-бэкенд «наш рендер». Ядро линкуется статически.
 */
extern "C" {
void roskos_reset();
uint32_t roskos_add_node(uint32_t meshId);
void roskos_set_transform(uint32_t node,
                          float px, float py, float pz,
                          float rx, float ry, float rz,
                          float sx, float sy, float sz);
void roskos_set_material(uint32_t node,
                         float r, float g, float b,
                         float roughness, float metallic,
                         float er, float eg, float eb,
                         uint32_t detail, float detailMix,
                         float uvX, float uvY);
uint32_t roskos_add_light(float r, float g, float b);
void roskos_set_light(uint32_t index, float x, float y, float z, float r, float g, float b);
void roskos_set_sky(float zr, float zg, float zb,
                    float hr, float hg, float hb,
                    float gr, float gg, float gb,
                    float sunIntensity);
void roskos_set_lighting(float ambientSkyR, float ambientSkyG, float ambientSkyB,
                         float ambientGroundR, float ambientGroundG, float ambientGroundB,
                         float fogR, float fogG, float fogB, float fogDensity,
                         float sunDirX, float sunDirY, float sunDirZ,
                         float sunR, float sunG, float sunB);
void roskos_camera_orbit(float deltaYaw, float deltaPitch);
void roskos_camera_zoom(float delta);
void roskos_camera_update(float dt);
void roskos_set_viewport(uint32_t width, uint32_t height);
uint32_t roskos_build_frame(double time);
const float* roskos_frame_ptr();
uint32_t roskos_command_count();
}
