//! Сцена roSkos: узлы (меш + материал + трансформация), источники света, окружение.

use crate::math::Vec3;

pub const MAX_LIGHTS: usize = 8;

#[derive(Clone, Copy)]
pub struct Node {
    pub mesh_id: u32,
    pub position: Vec3,
    pub rotation: Vec3,
    pub scale: Vec3,
    pub base_color: Vec3,
    pub emissive: Vec3,
    pub roughness: f32,
    pub metallic: f32,
    pub detail: bool,
    pub detail_mix: f32,
    pub uv_scale: [f32; 2],
    pub visible: bool,
}

impl Node {
    pub fn new(mesh_id: u32) -> Self {
        Self {
            mesh_id,
            position: [0.0, 0.0, 0.0],
            rotation: [0.0, 0.0, 0.0],
            scale: [1.0, 1.0, 1.0],
            base_color: [0.8, 0.8, 0.8],
            emissive: [0.0, 0.0, 0.0],
            roughness: 0.5,
            metallic: 0.0,
            detail: false,
            detail_mix: 0.0,
            uv_scale: [1.0, 1.0],
            visible: true,
        }
    }
}

pub struct Light {
    pub position: Vec3,
    pub color: Vec3,
}

pub struct Environment {
    pub ambient_sky: Vec3,
    pub ambient_ground: Vec3,
    pub fog_color: Vec3,
    pub fog_density: f32,
    pub sun_direction: Vec3,
    pub sun_color: Vec3,
    pub sky_zenith: Vec3,
    pub sky_horizon: Vec3,
    pub sky_ground: Vec3,
    pub sun_intensity: f32,
}

impl Environment {
    pub fn new() -> Self {
        Self {
            ambient_sky: [0.1, 0.13, 0.2],
            ambient_ground: [0.02, 0.02, 0.03],
            fog_color: [0.05, 0.06, 0.09],
            fog_density: 0.02,
            sun_direction: [0.4, 0.8, 0.3],
            sun_color: [0.55, 0.6, 0.8],
            sky_zenith: [0.02, 0.035, 0.09],
            sky_horizon: [0.11, 0.14, 0.21],
            sky_ground: [0.01, 0.012, 0.02],
            sun_intensity: 8.0,
        }
    }
}

pub struct Scene {
    pub nodes: Vec<Node>,
    pub lights: Vec<Light>,
    pub environment: Environment,
}

impl Scene {
    pub fn new() -> Self {
        Self {
            nodes: Vec::new(),
            lights: Vec::new(),
            environment: Environment::new(),
        }
    }
}
