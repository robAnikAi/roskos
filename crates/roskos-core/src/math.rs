//! Математика ядра: column-major матрицы, как их ожидает GPU.

pub type Vec3 = [f32; 3];
pub type Mat4 = [f32; 16];
pub type Mat3 = [f32; 9];

pub fn clamp(value: f32, min: f32, max: f32) -> f32 {
    if value < min {
        min
    } else if value > max {
        max
    } else {
        value
    }
}

pub fn mat4_identity() -> Mat4 {
    let mut m = [0.0; 16];
    m[0] = 1.0;
    m[5] = 1.0;
    m[10] = 1.0;
    m[15] = 1.0;
    m
}

/// out = a * b
pub fn multiply(out: &mut Mat4, a: &Mat4, b: &Mat4) {
    let mut result = [0.0f32; 16];
    for column in 0..4 {
        for row in 0..4 {
            let mut sum = 0.0;
            for k in 0..4 {
                sum += a[k * 4 + row] * b[column * 4 + k];
            }
            result[column * 4 + row] = sum;
        }
    }
    *out = result;
}

pub fn perspective(out: &mut Mat4, fov_y: f32, aspect: f32, near: f32, far: f32) {
    let f = 1.0 / (fov_y / 2.0).tan();
    let nf = 1.0 / (near - far);
    let mut m = [0.0; 16];
    m[0] = f / aspect;
    m[5] = f;
    m[10] = (far + near) * nf;
    m[11] = -1.0;
    m[14] = 2.0 * far * near * nf;
    *out = m;
}

pub fn look_at(out: &mut Mat4, eye: Vec3, center: Vec3, up: Vec3) {
    let mut z0 = eye[0] - center[0];
    let mut z1 = eye[1] - center[1];
    let mut z2 = eye[2] - center[2];
    let length = (z0 * z0 + z1 * z1 + z2 * z2).sqrt();
    let inv = if length > 0.0 { 1.0 / length } else { 1.0 };
    z0 *= inv;
    z1 *= inv;
    z2 *= inv;

    let mut x0 = up[1] * z2 - up[2] * z1;
    let mut x1 = up[2] * z0 - up[0] * z2;
    let mut x2 = up[0] * z1 - up[1] * z0;
    let length = (x0 * x0 + x1 * x1 + x2 * x2).sqrt();
    if length == 0.0 {
        x0 = 0.0;
        x1 = 0.0;
        x2 = 0.0;
    } else {
        let inv = 1.0 / length;
        x0 *= inv;
        x1 *= inv;
        x2 *= inv;
    }

    let y0 = z1 * x2 - z2 * x1;
    let y1 = z2 * x0 - z0 * x2;
    let y2 = z0 * x1 - z1 * x0;

    *out = [
        x0,
        y0,
        z0,
        0.0,
        x1,
        y1,
        z1,
        0.0,
        x2,
        y2,
        z2,
        0.0,
        -(x0 * eye[0] + x1 * eye[1] + x2 * eye[2]),
        -(y0 * eye[0] + y1 * eye[1] + y2 * eye[2]),
        -(z0 * eye[0] + z1 * eye[1] + z2 * eye[2]),
        1.0,
    ];
}

pub fn invert(out: &mut Mat4, a: &Mat4) {
    let a00 = a[0]; let a01 = a[1]; let a02 = a[2]; let a03 = a[3];
    let a10 = a[4]; let a11 = a[5]; let a12 = a[6]; let a13 = a[7];
    let a20 = a[8]; let a21 = a[9]; let a22 = a[10]; let a23 = a[11];
    let a30 = a[12]; let a31 = a[13]; let a32 = a[14]; let a33 = a[15];

    let b00 = a00 * a11 - a01 * a10;
    let b01 = a00 * a12 - a02 * a10;
    let b02 = a00 * a13 - a03 * a10;
    let b03 = a01 * a12 - a02 * a11;
    let b04 = a01 * a13 - a03 * a11;
    let b05 = a02 * a13 - a03 * a12;
    let b06 = a20 * a31 - a21 * a30;
    let b07 = a20 * a32 - a22 * a30;
    let b08 = a20 * a33 - a23 * a30;
    let b09 = a21 * a32 - a22 * a31;
    let b10 = a21 * a33 - a23 * a31;
    let b11 = a22 * a33 - a23 * a32;

    let mut det = b00 * b11 - b01 * b10 + b02 * b09 + b03 * b08 - b04 * b07 + b05 * b06;
    if det.abs() < f32::EPSILON {
        *out = mat4_identity();
        return;
    }
    det = 1.0 / det;

    *out = [
        (a11 * b11 - a12 * b10 + a13 * b09) * det,
        (a02 * b10 - a01 * b11 - a03 * b09) * det,
        (a31 * b05 - a32 * b04 + a33 * b03) * det,
        (a22 * b04 - a21 * b05 - a23 * b03) * det,
        (a12 * b08 - a10 * b11 - a13 * b07) * det,
        (a00 * b11 - a02 * b08 + a03 * b07) * det,
        (a32 * b02 - a30 * b05 - a33 * b01) * det,
        (a20 * b05 - a22 * b02 + a23 * b01) * det,
        (a10 * b10 - a11 * b08 + a13 * b06) * det,
        (a01 * b08 - a00 * b10 - a03 * b06) * det,
        (a30 * b04 - a31 * b02 + a33 * b00) * det,
        (a21 * b02 - a20 * b04 - a23 * b00) * det,
        (a11 * b07 - a10 * b09 - a12 * b06) * det,
        (a00 * b09 - a01 * b07 + a02 * b06) * det,
        (a31 * b01 - a30 * b03 - a32 * b00) * det,
        (a20 * b03 - a21 * b01 + a22 * b00) * det,
    ];
}

/// transpose(inverse(upper-left 3x3)) — универсальный, но дорогой путь.
/// Оставлен как эталон для тестов: в кадре используется `compose_with_normal`.
pub fn normal_matrix(out: &mut Mat3, model: &Mat4) {
    let mut inverse = mat4_identity();
    invert(&mut inverse, model);
    *out = [
        inverse[0], inverse[4], inverse[8],
        inverse[1], inverse[5], inverse[9],
        inverse[2], inverse[6], inverse[10],
    ];
}

/// Матрица поворота R = Rx * Ry * Rz в column-major виде.
#[inline]
fn rotation_matrix(rotation: Vec3) -> Mat3 {
    let (cx, sx) = (rotation[0].cos(), rotation[0].sin());
    let (cy, sy) = (rotation[1].cos(), rotation[1].sin());
    let (cz, sz) = (rotation[2].cos(), rotation[2].sin());

    [
        cy * cz,
        cx * sz + sx * sy * cz,
        sx * sz - cx * sy * cz,
        -cy * sz,
        cx * cz - sx * sy * sz,
        sx * cz + cx * sy * sz,
        sy,
        -sx * cy,
        cx * cy,
    ]
}

#[inline]
fn safe_inverse_scale(scale: Vec3) -> Vec3 {
    let mut out = [0.0f32; 3];
    for i in 0..3 {
        if scale[i].abs() > 1e-6 {
            out[i] = 1.0 / scale[i];
        }
    }
    out
}

/// model = translate * rotate(XYZ) * scale
pub fn compose(out: &mut Mat4, position: Vec3, rotation: Vec3, scale: Vec3) {
    let r = rotation_matrix(rotation);
    *out = [
        r[0] * scale[0],
        r[1] * scale[0],
        r[2] * scale[0],
        0.0,
        r[3] * scale[1],
        r[4] * scale[1],
        r[5] * scale[1],
        0.0,
        r[6] * scale[2],
        r[7] * scale[2],
        r[8] * scale[2],
        0.0,
        position[0],
        position[1],
        position[2],
        1.0,
    ];
}

/// Сразу модель и нормальная матрица, без обратной 4x4.
///
/// Для model = T * R * S (R ортонормальна, S — диагональ масштабов)
/// нормальная матрица (A^-1)^T = R * S^-1, то есть те же столбцы R,
/// поделённые на соответствующий масштаб. Раньше на каждый узел кадра
/// считалась полная обратная матрица 4x4 (~130 умножений) и её транспонирование.
pub fn compose_with_normal(
    model: &mut Mat4,
    normal: &mut Mat3,
    position: Vec3,
    rotation: Vec3,
    scale: Vec3,
) {
    let r = rotation_matrix(rotation);
    let inv = safe_inverse_scale(scale);

    *model = [
        r[0] * scale[0],
        r[1] * scale[0],
        r[2] * scale[0],
        0.0,
        r[3] * scale[1],
        r[4] * scale[1],
        r[5] * scale[1],
        0.0,
        r[6] * scale[2],
        r[7] * scale[2],
        r[8] * scale[2],
        0.0,
        position[0],
        position[1],
        position[2],
        1.0,
    ];

    *normal = [
        r[0] * inv[0],
        r[1] * inv[0],
        r[2] * inv[0],
        r[3] * inv[1],
        r[4] * inv[1],
        r[5] * inv[1],
        r[6] * inv[2],
        r[7] * inv[2],
        r[8] * inv[2],
    ];
}

#[cfg(test)]
mod tests {
    use super::*;

    const TRANSFORMS: [([f32; 3], [f32; 3]); 4] = [
        ([0.3, 1.2, -0.7], [1.0, 1.0, 1.0]),
        ([1.1, -0.4, 2.0], [0.8, 0.8, 0.8]),
        ([2.4, 0.5, -1.3], [2.0, 0.5, 1.5]),
        ([-0.9, 2.2, 0.1], [1.2, 0.9, 0.4]),
    ];

    #[test]
    fn fast_normal_matrix_equals_inverse_transpose() {
        for (rotation, scale) in TRANSFORMS {
            let mut model_fast = [0.0f32; 16];
            let mut normal_fast = [0.0f32; 9];
            compose_with_normal(&mut model_fast, &mut normal_fast, [1.0, 2.0, 3.0], rotation, scale);

            let mut model_ref = [0.0f32; 16];
            compose(&mut model_ref, [1.0, 2.0, 3.0], rotation, scale);
            let mut normal_ref = [0.0f32; 9];
            normal_matrix(&mut normal_ref, &model_ref);

            for i in 0..16 {
                assert!(
                    (model_fast[i] - model_ref[i]).abs() < 1e-5,
                    "model[{i}] {} != {}",
                    model_fast[i],
                    model_ref[i]
                );
            }
            for i in 0..9 {
                assert!(
                    (normal_fast[i] - normal_ref[i]).abs() < 1e-4,
                    "normal[{i}] {} != {}",
                    normal_fast[i],
                    normal_ref[i]
                );
            }
        }
    }

    #[test]
    fn fast_normal_matrix_keeps_normal_direction() {
        // нормаль к поверхности должна остаться перпендикулярной касательным
        let mut model = [0.0f32; 16];
        let mut normal = [0.0f32; 9];
        compose_with_normal(&mut model, &mut normal, [0.0, 0.0, 0.0], [0.4, 1.9, -0.2], [1.0, 1.0, 1.0]);

        // при равномерном масштабе нормальная матрица = R: длина нормали сохраняется
        let n = [0.0f32, 0.6, 0.8];
        let mut out = [0.0f32; 3];
        for row in 0..3 {
            out[row] = normal[row] * n[0] + normal[3 + row] * n[1] + normal[6 + row] * n[2];
        }
        let length = (out[0] * out[0] + out[1] * out[1] + out[2] * out[2]).sqrt();
        assert!((length - 1.0).abs() < 1e-4, "длина нормали {length}");
    }

    #[test]
    fn reports_per_frame_transform_cost() {
        const NODES: usize = 4;
        const FRAMES: usize = 200_000;

        let mut model = [0.0f32; 16];
        let mut normal = [0.0f32; 9];
        let mut sink = 0.0f32;

        let start = std::time::Instant::now();
        for frame in 0..FRAMES {
            for node in 0..NODES {
                let t = (frame + node) as f32 * 0.001;
                compose_with_normal(
                    &mut model,
                    &mut normal,
                    [t.cos() * 2.5, 0.2, t.sin() * 2.5],
                    [t, t * 1.3, t * 0.7],
                    [0.8, 0.8, 0.8],
                );
                sink += model[0] + normal[0];
            }
        }
        let fast = start.elapsed();

        let start = std::time::Instant::now();
        for frame in 0..FRAMES {
            for node in 0..NODES {
                let t = (frame + node) as f32 * 0.001;
                compose(
                    &mut model,
                    [t.cos() * 2.5, 0.2, t.sin() * 2.5],
                    [t, t * 1.3, t * 0.7],
                    [0.8, 0.8, 0.8],
                );
                normal_matrix(&mut normal, &model);
                sink += model[0] + normal[0];
            }
        }
        let slow = start.elapsed();

        let per_frame_fast = fast.as_secs_f64() * 1e9 / FRAMES as f64;
        let per_frame_slow = slow.as_secs_f64() * 1e9 / FRAMES as f64;
        println!(
            "transform math: inverse-based {per_frame_slow:.0} нс/кадр -> closed-form {per_frame_fast:.0} нс/кадр ({} узлов, sink={sink})",
            NODES
        );
        assert!(per_frame_fast < per_frame_slow);
    }
}
