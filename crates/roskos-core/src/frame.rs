//! Сборка командного потока кадра.
//!
//! Ядро не знает ничего о WebGL: оно выдаёт плоский поток `f32`, который
//! бэкенд исполняет. Формат (все значения — f32):
//!
//! ```text
//! FRAME : 1, viewProj[16], invViewProj[16], cameraPos[3],
//!         ambientSky[3], ambientGround[3], fogColor[3], fogDensity,
//!         sunDirection[3], sunColor[3],
//!         skyZenith[3], skyHorizon[3], skyGround[3], sunIntensity,
//!         viewportW, viewportH, lightCount
//! LIGHT : 2, position[3], color[3]
//! DRAW  : 3, meshId, model[16], normal[9], baseColor[3],
//!         roughness, metallic, emissive[3], detailMix, uvScale[2], hasDetail
//! ```

use crate::camera::OrbitCamera;
use crate::math::{compose_with_normal, Mat3, Mat4};
use crate::scene::{Scene, MAX_LIGHTS};

pub const CMD_FRAME: f32 = 1.0;
pub const CMD_LIGHT: f32 = 2.0;
pub const CMD_DRAW: f32 = 3.0;

pub const FRAME_STRIDE: usize = 65;
pub const LIGHT_STRIDE: usize = 7;
pub const DRAW_STRIDE: usize = 39;

pub fn build(scene: &Scene, camera: &OrbitCamera, viewport: (u32, u32), out: &mut Vec<f32>) {
    let environment = &scene.environment;
    let light_count = scene.lights.len().min(MAX_LIGHTS);

    out.clear();
    out.reserve(FRAME_STRIDE + scene.lights.len() * LIGHT_STRIDE + scene.nodes.len() * DRAW_STRIDE);

    out.push(CMD_FRAME);
    out.extend_from_slice(&camera.view_projection);
    out.extend_from_slice(&camera.inverse_view_projection);
    out.extend_from_slice(&camera.position);
    out.extend_from_slice(&environment.ambient_sky);
    out.extend_from_slice(&environment.ambient_ground);
    out.extend_from_slice(&environment.fog_color);
    out.push(environment.fog_density);
    out.extend_from_slice(&environment.sun_direction);
    out.extend_from_slice(&environment.sun_color);
    out.extend_from_slice(&environment.sky_zenith);
    out.extend_from_slice(&environment.sky_horizon);
    out.extend_from_slice(&environment.sky_ground);
    out.push(environment.sun_intensity);
    out.push(viewport.0 as f32);
    out.push(viewport.1 as f32);
    out.push(light_count as f32);

    for light in scene.lights.iter().take(MAX_LIGHTS) {
        out.push(CMD_LIGHT);
        out.extend_from_slice(&light.position);
        out.extend_from_slice(&light.color);
    }

    let mut model: Mat4 = [0.0; 16];
    let mut normal: Mat3 = [0.0; 9];

    for node in &scene.nodes {
        if !node.visible {
            continue;
        }
        // сразу модель и нормальная матрица: без обратной 4x4 на узел
        compose_with_normal(&mut model, &mut normal, node.position, node.rotation, node.scale);

        out.push(CMD_DRAW);
        out.push(node.mesh_id as f32);
        out.extend_from_slice(&model);
        out.extend_from_slice(&normal);
        out.extend_from_slice(&node.base_color);
        out.push(node.roughness);
        out.push(node.metallic);
        out.extend_from_slice(&node.emissive);
        out.push(node.detail_mix);
        out.extend_from_slice(&node.uv_scale);
        out.push(if node.detail { 1.0 } else { 0.0 });
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::camera::OrbitCamera;
    use crate::scene::Scene;

    #[test]
    fn command_stream_has_expected_layout() {
        let mut scene = Scene::new();
        scene.nodes.push(crate::scene::Node::new(7));
        scene.lights.push(crate::scene::Light {
            position: [1.0, 2.0, 3.0],
            color: [0.5, 0.5, 0.5],
        });

        let mut camera = OrbitCamera::new();
        camera.update(0.016);

        let mut out = Vec::new();
        build(&scene, &camera, (800, 600), &mut out);

        assert_eq!(out.len(), FRAME_STRIDE + LIGHT_STRIDE + DRAW_STRIDE);
        assert_eq!(out[0], CMD_FRAME);
        assert_eq!(out[FRAME_STRIDE], CMD_LIGHT);
        assert_eq!(out[FRAME_STRIDE + LIGHT_STRIDE], CMD_DRAW);
        // meshId идёт сразу после маркера DRAW
        assert_eq!(out[FRAME_STRIDE + LIGHT_STRIDE + 1], 7.0);
        // вьюпорт в конце FRAME-блока
        assert_eq!(out[FRAME_STRIDE - 3], 800.0);
        assert_eq!(out[FRAME_STRIDE - 2], 600.0);
        assert_eq!(out[FRAME_STRIDE - 1], 1.0);
    }
}
