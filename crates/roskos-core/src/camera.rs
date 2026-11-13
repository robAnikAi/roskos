//! Орбитальная камера с инерционным сглаживанием.

use crate::math::{clamp, invert, look_at, mat4_identity, multiply, perspective, Mat4, Vec3};

const UP: Vec3 = [0.0, 1.0, 0.0];

pub struct OrbitCamera {
    pub fov: f32,
    pub near: f32,
    pub far: f32,
    pub focus: Vec3,
    pub position: Vec3,
    pub view: Mat4,
    pub projection: Mat4,
    pub view_projection: Mat4,
    pub inverse_view_projection: Mat4,

    yaw: f32,
    pitch: f32,
    distance: f32,
    target_yaw: f32,
    target_pitch: f32,
    target_distance: f32,
    aspect: f32,
}

impl OrbitCamera {
    pub fn new() -> Self {
        Self {
            fov: 46.0f32.to_radians(),
            near: 0.1,
            far: 220.0,
            focus: [0.0, 0.35, 0.0],
            position: [0.0, 2.0, 9.0],
            view: mat4_identity(),
            projection: mat4_identity(),
            view_projection: mat4_identity(),
            inverse_view_projection: mat4_identity(),
            yaw: 0.62,
            pitch: 0.18,
            distance: 9.6,
            target_yaw: 0.62,
            target_pitch: 0.18,
            target_distance: 9.6,
            aspect: 1.0,
        }
    }

    pub fn set_aspect(&mut self, aspect: f32) {
        self.aspect = if aspect > 0.0 { aspect } else { 1.0 };
    }

    pub fn orbit(&mut self, delta_yaw: f32, delta_pitch: f32) {
        self.target_yaw += delta_yaw;
        self.target_pitch = clamp(self.target_pitch + delta_pitch, -0.05, 1.2);
    }

    pub fn zoom(&mut self, delta: f32) {
        self.target_distance = clamp(self.target_distance * (delta * 0.0012).exp(), 4.2, 26.0);
    }

    pub fn update(&mut self, dt: f32) {
        let smoothing = 1.0 - (-dt * 14.0).exp();
        self.yaw += (self.target_yaw - self.yaw) * smoothing;
        self.pitch += (self.target_pitch - self.pitch) * smoothing;
        self.distance += (self.target_distance - self.distance) * smoothing;

        let cos_pitch = self.pitch.cos();
        self.position = [
            self.focus[0] + self.yaw.sin() * cos_pitch * self.distance,
            self.focus[1] + self.pitch.sin() * self.distance,
            self.focus[2] + self.yaw.cos() * cos_pitch * self.distance,
        ];

        look_at(&mut self.view, self.position, self.focus, UP);
        perspective(&mut self.projection, self.fov, self.aspect, self.near, self.far);
        multiply(&mut self.view_projection, &self.projection, &self.view);
        invert(&mut self.inverse_view_projection, &self.view_projection);
    }
}
