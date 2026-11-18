// lol-vr — petites maths 3D (vecteurs, quaternions, matrices 4x4 en colonnes, convention OpenGL).
#pragma once

#include <math.h>
#include <string.h>

namespace lolvr {

struct Vec3 {
	float x, y, z;
};

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline Vec3 mul(Vec3 a, Vec3 b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
inline Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float length(Vec3 a) { return sqrtf(dot(a, a)); }
inline Vec3 normalize(Vec3 a) {
	float l = sqrtf(dot(a, a));
	return l > 1e-6f ? a * (1.0f / l) : Vec3{0, 0, 0};
}
inline Vec3 lerp(Vec3 a, Vec3 b, float t) { return a + (b - a) * t; }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float mixf(float a, float b, float t) { return a + (b - a) * t; }
inline float smoothstepf(float e0, float e1, float x) {
	float t = clampf((x - e0) / (e1 - e0), 0.0f, 1.0f);
	return t * t * (3.0f - 2.0f * t);
}

struct Quat {
	float x, y, z, w;
};

inline Quat conjugate(Quat q) { return {-q.x, -q.y, -q.z, q.w}; }
inline Quat multiply(Quat a, Quat b) {
	return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
	        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
	        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
	        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
inline Vec3 rotate(Quat q, Vec3 v) {
	Vec3 u{q.x, q.y, q.z};
	Vec3 t = cross(u, v) * 2.0f;
	return v + t * q.w + cross(u, t);
}
inline Quat quatAxisAngle(Vec3 axis, float angle) {
	Vec3 a = normalize(axis);
	float s = sinf(angle * 0.5f);
	return {a.x * s, a.y * s, a.z * s, cosf(angle * 0.5f)};
}
inline Quat quatYaw(float yaw) { return {0, sinf(yaw * 0.5f), 0, cosf(yaw * 0.5f)}; }

struct Mat4 {
	float m[16];
};

inline Mat4 identity() {
	Mat4 r;
	memset(&r, 0, sizeof(r));
	r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
	return r;
}

inline Mat4 multiply(const Mat4 &a, const Mat4 &b) {
	Mat4 r;
	for (int c = 0; c < 4; c++)
		for (int row = 0; row < 4; row++) {
			float s = 0;
			for (int k = 0; k < 4; k++)
				s += a.m[k * 4 + row] * b.m[c * 4 + k];
			r.m[c * 4 + row] = s;
		}
	return r;
}

inline Mat4 translation(Vec3 t) {
	Mat4 r = identity();
	r.m[12] = t.x;
	r.m[13] = t.y;
	r.m[14] = t.z;
	return r;
}

inline Mat4 scaling(Vec3 s) {
	Mat4 r = identity();
	r.m[0] = s.x;
	r.m[5] = s.y;
	r.m[10] = s.z;
	return r;
}

inline Mat4 rotation(Quat q) {
	Vec3 x = rotate(q, {1, 0, 0}), y = rotate(q, {0, 1, 0}), z = rotate(q, {0, 0, 1});
	Mat4 r = identity();
	r.m[0] = x.x; r.m[1] = x.y; r.m[2] = x.z;
	r.m[4] = y.x; r.m[5] = y.y; r.m[6] = y.z;
	r.m[8] = z.x; r.m[9] = z.y; r.m[10] = z.z;
	return r;
}

// Pose (rotation puis translation) avec une échelle uniforme.
inline Mat4 poseMatrix(Vec3 pos, Quat rot, float scale = 1.0f) {
	Mat4 r = rotation(rot);
	for (int i = 0; i < 12; i++)
		r.m[i] *= scale;
	r.m[12] = pos.x;
	r.m[13] = pos.y;
	r.m[14] = pos.z;
	return r;
}

inline Vec3 transformPoint(const Mat4 &m, Vec3 p) {
	return {m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12],
	        m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13],
	        m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14]};
}

inline Vec3 transformDir(const Mat4 &m, Vec3 p) {
	return {m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z,
	        m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z,
	        m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z};
}

// Projection à partir des tangentes des demi-angles (gauche et bas négatifs).
inline Mat4 projectionFromTangents(float l, float r, float d, float u, float nearZ, float farZ) {
	Mat4 p;
	memset(&p, 0, sizeof(p));
	p.m[0] = 2.0f / (r - l);
	p.m[5] = 2.0f / (u - d);
	p.m[8] = (r + l) / (r - l);
	p.m[9] = (u + d) / (u - d);
	p.m[10] = -(farZ + nearZ) / (farZ - nearZ);
	p.m[11] = -1.0f;
	p.m[14] = -(2.0f * farZ * nearZ) / (farZ - nearZ);
	return p;
}

// Matrice de vue = inverse de la pose de l'œil.
inline Mat4 viewFromPose(Vec3 pos, Quat rot) {
	Quat inv = conjugate(rot);
	Vec3 x = rotate(inv, {1, 0, 0});
	Vec3 y = rotate(inv, {0, 1, 0});
	Vec3 z = rotate(inv, {0, 0, 1});
	Vec3 t = rotate(inv, pos * -1.0f);
	Mat4 v;
	v.m[0] = x.x; v.m[1] = x.y; v.m[2] = x.z; v.m[3] = 0;
	v.m[4] = y.x; v.m[5] = y.y; v.m[6] = y.z; v.m[7] = 0;
	v.m[8] = z.x; v.m[9] = z.y; v.m[10] = z.z; v.m[11] = 0;
	v.m[12] = t.x; v.m[13] = t.y; v.m[14] = t.z; v.m[15] = 1;
	return v;
}

} // namespace lolvr
