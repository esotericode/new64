/*
 * new64 -- fixed-point angle math implementation.
 */
#include "m64_math.h"

#include <math.h>

/*
 * Half a turn expressed in s16 angle units, as a float.  Multiplying a radian
 * value by (0x8000 / pi) converts it into the engine's angle space.
 */
#define M64_RAD_TO_ANGLE (32768.0f / 3.14159265358979323846f)

f32 m64_atan2f(f32 z, f32 x) {
    /*
     * Note the argument order: this is the yaw of the vector (x, z) measured
     * from +Z toward +X, so x is the "opposite" side and z the "adjacent" one.
     * That is the inverse of the usual atan2(y, x) spelling, and matching it is
     * what lets movement code read naturally as atan2s(dz, dx).
     */
    return atan2f(x, z);
}

s16 m64_atan2s(f32 z, f32 x) {
    /*
     * Truncating toward zero (rather than rounding) keeps the sign symmetric,
     * which matters because movement code subtracts two of these and relies on
     * the difference being exactly 0 when two directions agree.
     */
    return (s16) (s32) (m64_atan2f(z, x) * M64_RAD_TO_ANGLE);
}

s32 m64_approach_s32(s32 current, s32 target, s32 inc, s32 dec) {
    if (current < target) {
        current += inc;
        if (current > target) {
            current = target;
        }
    } else {
        current -= dec;
        if (current < target) {
            current = target;
        }
    }
    return current;
}

f32 m64_approach_f32(f32 current, f32 target, f32 inc, f32 dec) {
    if (current < target) {
        current += inc;
        if (current > target) {
            current = target;
        }
    } else {
        current -= dec;
        if (current < target) {
            current = target;
        }
    }
    return current;
}

f32 m64_vec3f_dot(const Vec3f a, const Vec3f b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

f32 m64_vec3f_length(const Vec3f v) {
    return sqrtf(m64_vec3f_dot(v, v));
}

void m64_vec3f_normalize(Vec3f v) {
    f32 len = m64_vec3f_length(v);

    if (len < 1e-8f) {
        v[0] = 0.0f;
        v[1] = 0.0f;
        v[2] = 0.0f;
        return;
    }
    len = 1.0f / len;
    v[0] *= len;
    v[1] *= len;
    v[2] *= len;
}

void m64_vec3f_cross(Vec3f dst, const Vec3f a, const Vec3f b) {
    /* Write through temporaries so dst may alias a or b. */
    f32 x = a[1] * b[2] - a[2] * b[1];
    f32 y = a[2] * b[0] - a[0] * b[2];
    f32 z = a[0] * b[1] - a[1] * b[0];

    dst[0] = x;
    dst[1] = y;
    dst[2] = z;
}

void m64_mtxf_identity(Mat4 dst) {
    s32 i, j;

    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            dst[i][j] = (i == j) ? 1.0f : 0.0f;
        }
    }
}

void m64_mtxf_mul(Mat4 dst, const Mat4 a, const Mat4 b) {
    Mat4 out;
    s32 i, j;

    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            out[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j]
                        + a[i][3] * b[3][j];
        }
    }
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            dst[i][j] = out[i][j];
        }
    }
}

void m64_mtxf_perspective(Mat4 dst, f32 fovDeg, f32 aspect, f32 near, f32 far) {
    f32 f = 1.0f / tanf(fovDeg * 0.5f * 3.14159265358979323846f / 180.0f);

    m64_mtxf_identity(dst);
    dst[0][0] = f / aspect;
    dst[1][1] = f;
    dst[2][2] = (far + near) / (near - far);
    dst[2][3] = -1.0f;
    dst[3][2] = (2.0f * far * near) / (near - far);
    dst[3][3] = 0.0f;
}

void m64_mtxf_look_at(Mat4 dst, const Vec3f eye, const Vec3f target, f32 roll) {
    Vec3f fwd, right, up;
    Vec3f worldUp = { 0.0f, 1.0f, 0.0f };

    fwd[0] = target[0] - eye[0];
    fwd[1] = target[1] - eye[1];
    fwd[2] = target[2] - eye[2];
    m64_vec3f_normalize(fwd);

    /* Degenerate case: looking straight up or down. Pick any stable basis. */
    if (m64_absf(fwd[1]) > 0.9999f) {
        worldUp[1] = 0.0f;
        worldUp[2] = 1.0f;
    }

    m64_vec3f_cross(right, fwd, worldUp);
    m64_vec3f_normalize(right);
    m64_vec3f_cross(up, right, fwd);

    if (roll != 0.0f) {
        f32 c = cosf(roll), s = sinf(roll);
        Vec3f r2 = { right[0] * c + up[0] * s, right[1] * c + up[1] * s,
                     right[2] * c + up[2] * s };
        Vec3f u2 = { up[0] * c - right[0] * s, up[1] * c - right[1] * s,
                     up[2] * c - right[2] * s };
        m64_vec3f_copy(right, r2);
        m64_vec3f_copy(up, u2);
    }

    /* Row-vector convention: p' = p * M, so basis vectors go in columns. */
    dst[0][0] = right[0]; dst[0][1] = up[0]; dst[0][2] = -fwd[0]; dst[0][3] = 0.0f;
    dst[1][0] = right[1]; dst[1][1] = up[1]; dst[1][2] = -fwd[1]; dst[1][3] = 0.0f;
    dst[2][0] = right[2]; dst[2][1] = up[2]; dst[2][2] = -fwd[2]; dst[2][3] = 0.0f;
    dst[3][0] = -m64_vec3f_dot(right, eye);
    dst[3][1] = -m64_vec3f_dot(up, eye);
    dst[3][2] = m64_vec3f_dot(fwd, eye);
    dst[3][3] = 1.0f;
}

void m64_mtxf_rotate_translate(Mat4 dst, const Vec3f pos, const Vec3s rotAngles,
                               f32 scale) {
    /* ZXY order, which is the order the N64 titles compose object rotations. */
    f32 sx = m64_sins(rotAngles[0]), cx = m64_coss(rotAngles[0]);
    f32 sy = m64_sins(rotAngles[1]), cy = m64_coss(rotAngles[1]);
    f32 sz = m64_sins(rotAngles[2]), cz = m64_coss(rotAngles[2]);

    dst[0][0] = (cy * cz + sx * sy * sz) * scale;
    dst[0][1] = (cx * sz) * scale;
    dst[0][2] = (-sy * cz + sx * cy * sz) * scale;
    dst[0][3] = 0.0f;

    dst[1][0] = (-cy * sz + sx * sy * cz) * scale;
    dst[1][1] = (cx * cz) * scale;
    dst[1][2] = (sy * sz + sx * cy * cz) * scale;
    dst[1][3] = 0.0f;

    dst[2][0] = (cx * sy) * scale;
    dst[2][1] = (-sx) * scale;
    dst[2][2] = (cx * cy) * scale;
    dst[2][3] = 0.0f;

    dst[3][0] = pos[0];
    dst[3][1] = pos[1];
    dst[3][2] = pos[2];
    dst[3][3] = 1.0f;
}

void m64_mtxf_transform_point(Vec4f dst, const Mat4 m, const Vec3f p) {
    dst[0] = p[0] * m[0][0] + p[1] * m[1][0] + p[2] * m[2][0] + m[3][0];
    dst[1] = p[0] * m[0][1] + p[1] * m[1][1] + p[2] * m[2][1] + m[3][1];
    dst[2] = p[0] * m[0][2] + p[1] * m[1][2] + p[2] * m[2][2] + m[3][2];
    dst[3] = p[0] * m[0][3] + p[1] * m[1][3] + p[2] * m[2][3] + m[3][3];
}
