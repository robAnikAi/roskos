//! roSkos core.
//!
//! Низкоуровневое ядро движка: владеет сценой, камерой и собирает командный
//! поток кадра. Экспортируется в WebAssembly через голый C-ABI — без
//! wasm-bindgen и без сторонних крейтов.

mod camera;
mod frame;
mod math;
mod scene;

use camera::OrbitCamera;
use scene::{Light, Node, Scene};

pub struct Engine {
    scene: Scene,
    camera: OrbitCamera,
    commands: Vec<f32>,
    viewport: (u32, u32),
}

impl Engine {
    fn new() -> Self {
        Self {
            scene: Scene::new(),
            camera: OrbitCamera::new(),
            commands: Vec::new(),
            viewport: (1, 1),
        }
    }
}

static mut ENGINE: Option<Engine> = None;

#[allow(static_mut_refs)]
fn engine() -> &'static mut Engine {
    unsafe {
        if ENGINE.is_none() {
            ENGINE = Some(Engine::new());
        }
        match ENGINE.as_mut() {
            Some(engine) => engine,
            None => unreachable!(),
        }
    }
}

fn node_mut(index: u32) -> Option<&'static mut Node> {
    engine().scene.nodes.get_mut(index as usize)
}

// --- сцена ---------------------------------------------------------------

/// Очищает сцену (нужно при перезапуске бэкенда без перезагрузки процесса).
#[no_mangle]
pub extern "C" fn roskos_reset() {
    let engine = engine();
    engine.scene.nodes.clear();
    engine.scene.lights.clear();
    engine.commands.clear();
}

#[no_mangle]
pub extern "C" fn roskos_add_node(mesh_id: u32) -> u32 {
    let engine = engine();
    engine.scene.nodes.push(Node::new(mesh_id));
    (engine.scene.nodes.len() - 1) as u32
}

#[no_mangle]
#[allow(clippy::too_many_arguments)]
pub extern "C" fn roskos_set_transform(
    node: u32,
    px: f32,
    py: f32,
    pz: f32,
    rx: f32,
    ry: f32,
    rz: f32,
    sx: f32,
    sy: f32,
    sz: f32,
) {
    if let Some(node) = node_mut(node) {
        node.position = [px, py, pz];
        node.rotation = [rx, ry, rz];
        node.scale = [sx, sy, sz];
    }
}

#[no_mangle]
#[allow(clippy::too_many_arguments)]
pub extern "C" fn roskos_set_material(
    node: u32,
    r: f32,
    g: f32,
    b: f32,
    roughness: f32,
    metallic: f32,
    er: f32,
    eg: f32,
    eb: f32,
    detail: u32,
    detail_mix: f32,
    uv_x: f32,
    uv_y: f32,
) {
    if let Some(node) = node_mut(node) {
        node.base_color = [r, g, b];
        node.roughness = roughness;
        node.metallic = metallic;
        node.emissive = [er, eg, eb];
        node.detail = detail != 0;
        node.detail_mix = detail_mix;
        node.uv_scale = [uv_x, uv_y];
    }
}

#[no_mangle]
pub extern "C" fn roskos_set_visible(node: u32, visible: u32) {
    if let Some(node) = node_mut(node) {
        node.visible = visible != 0;
    }
}

// --- свет ----------------------------------------------------------------

#[no_mangle]
pub extern "C" fn roskos_add_light(r: f32, g: f32, b: f32) -> u32 {
    let engine = engine();
    engine.scene.lights.push(Light {
        position: [0.0, 0.0, 0.0],
        color: [r, g, b],
    });
    (engine.scene.lights.len() - 1) as u32
}

#[no_mangle]
pub extern "C" fn roskos_set_light(index: u32, x: f32, y: f32, z: f32, r: f32, g: f32, b: f32) {
    let engine = engine();
    if let Some(light) = engine.scene.lights.get_mut(index as usize) {
        light.position = [x, y, z];
        light.color = [r, g, b];
    }
}

#[no_mangle]
pub extern "C" fn roskos_set_sky(
    zr: f32,
    zg: f32,
    zb: f32,
    hr: f32,
    hg: f32,
    hb: f32,
    gr: f32,
    gg: f32,
    gb: f32,
    sun_intensity: f32,
) {
    let environment = &mut engine().scene.environment;
    environment.sky_zenith = [zr, zg, zb];
    environment.sky_horizon = [hr, hg, hb];
    environment.sky_ground = [gr, gg, gb];
    environment.sun_intensity = sun_intensity;
}

#[no_mangle]
#[allow(clippy::too_many_arguments)]
pub extern "C" fn roskos_set_lighting(
    ambient_sky_r: f32,
    ambient_sky_g: f32,
    ambient_sky_b: f32,
    ambient_ground_r: f32,
    ambient_ground_g: f32,
    ambient_ground_b: f32,
    fog_r: f32,
    fog_g: f32,
    fog_b: f32,
    fog_density: f32,
    sun_dir_x: f32,
    sun_dir_y: f32,
    sun_dir_z: f32,
    sun_r: f32,
    sun_g: f32,
    sun_b: f32,
) {
    let environment = &mut engine().scene.environment;
    environment.ambient_sky = [ambient_sky_r, ambient_sky_g, ambient_sky_b];
    environment.ambient_ground = [ambient_ground_r, ambient_ground_g, ambient_ground_b];
    environment.fog_color = [fog_r, fog_g, fog_b];
    environment.fog_density = fog_density;
    environment.sun_direction = [sun_dir_x, sun_dir_y, sun_dir_z];
    environment.sun_color = [sun_r, sun_g, sun_b];
}

// --- камера и кадр -------------------------------------------------------

#[no_mangle]
pub extern "C" fn roskos_camera_orbit(delta_yaw: f32, delta_pitch: f32) {
    engine().camera.orbit(delta_yaw, delta_pitch);
}

#[no_mangle]
pub extern "C" fn roskos_camera_zoom(delta: f32) {
    engine().camera.zoom(delta);
}

#[no_mangle]
pub extern "C" fn roskos_camera_update(dt: f32) {
    engine().camera.update(dt);
}

#[no_mangle]
pub extern "C" fn roskos_set_viewport(width: u32, height: u32) {
    let engine = engine();
    engine.viewport = (width.max(1), height.max(1));
    engine
        .camera
        .set_aspect(engine.viewport.0 as f32 / engine.viewport.1 as f32);
}

/// Пересобирает командный поток кадра и возвращает количество элементов f32.
#[no_mangle]
pub extern "C" fn roskos_build_frame(_time: f64) -> u32 {
    let engine = engine();
    let viewport = engine.viewport;
    frame::build(
        &engine.scene,
        &engine.camera,
        viewport,
        &mut engine.commands,
    );
    engine.commands.len() as u32
}

#[no_mangle]
pub extern "C" fn roskos_frame_ptr() -> *const f32 {
    engine().commands.as_ptr()
}

#[no_mangle]
pub extern "C" fn roskos_command_count() -> u32 {
    engine().commands.len() as u32
}
