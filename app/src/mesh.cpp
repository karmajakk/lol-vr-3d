// lol-vr — construction de maillages pour le décor.

#include "mesh.h"

#include <algorithm>

namespace lolvr {

namespace {

const float kPi = 3.14159265f;

uint8_t toByte(float v) {
	v = v < 0 ? 0 : (v > 1 ? 1 : v);
	return (uint8_t)(v * 255.0f + 0.5f);
}

} // namespace

void MeshBuilder::vertex(Vec3 p, Vec3 n, float u, float v, const Material &m, float aoMul) {
	Vertex vx;
	Vec3 wp = transformPoint(xf, p);
	Vec3 wn = normalize(transformDir(xf, n));
	vx.pos[0] = wp.x; vx.pos[1] = wp.y; vx.pos[2] = wp.z;
	vx.normal[0] = wn.x; vx.normal[1] = wn.y; vx.normal[2] = wn.z;
	vx.uvl[0] = u; vx.uvl[1] = v; vx.uvl[2] = (float)m.layer;
	vx.tint[0] = toByte(m.tint.x); vx.tint[1] = toByte(m.tint.y); vx.tint[2] = toByte(m.tint.z); vx.tint[3] = 255;
	vx.ao = toByte(ao * aoMul);
	vx.emissive = toByte(m.emissive);
	vx.pad[0] = vx.pad[1] = 0;
	verts.push_back(vx);
}

void MeshBuilder::tri(Vec3 a, Vec3 b, Vec3 c, Vec3 n, const float uv[6], const Material &m) {
	vertex(a, n, uv[0], uv[1], m);
	vertex(b, n, uv[2], uv[3], m);
	vertex(c, n, uv[4], uv[5], m);
}

void MeshBuilder::quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d, const Material &m, float u0, float v0, float u1, float v1) {
	Vec3 n = normalize(cross(b - a, d - a));
	vertex(a, n, u0, v0, m);
	vertex(b, n, u1, v0, m);
	vertex(c, n, u1, v1, m);
	vertex(a, n, u0, v0, m);
	vertex(c, n, u1, v1, m);
	vertex(d, n, u0, v1, m);
}

void MeshBuilder::grid(Vec3 origin, Vec3 eu, Vec3 ev, const Material &m, float cell, float uOff, float vOff) {
	float lu = length(eu), lv = length(ev);
	int nu = std::max(1, (int)ceilf(lu / cell)), nv = std::max(1, (int)ceilf(lv / cell));
	Vec3 n = normalize(cross(eu, ev));
	for (int j = 0; j < nv; j++)
		for (int i = 0; i < nu; i++) {
			float s0 = (float)i / nu, s1 = (float)(i + 1) / nu, t0 = (float)j / nv, t1 = (float)(j + 1) / nv;
			Vec3 a = origin + eu * s0 + ev * t0, b = origin + eu * s1 + ev * t0;
			Vec3 c = origin + eu * s1 + ev * t1, d = origin + eu * s0 + ev * t1;
			float u0 = (uOff + s0 * lu) * m.uvScale, u1 = (uOff + s1 * lu) * m.uvScale;
			float v0 = (vOff + t0 * lv) * m.uvScale, v1 = (vOff + t1 * lv) * m.uvScale;
			vertex(a, n, u0, v0, m);
			vertex(b, n, u1, v0, m);
			vertex(c, n, u1, v1, m);
			vertex(a, n, u0, v0, m);
			vertex(c, n, u1, v1, m);
			vertex(d, n, u0, v1, m);
		}
}

void MeshBuilder::box(Vec3 c, Vec3 h, const Material &m, int faces, float aoBottom) {
	struct Face {
		Vec3 a, b, cc, d;
	};
	const Face f[6] = {
		{{-h.x, -h.y, -h.z}, {-h.x, -h.y, h.z}, {-h.x, h.y, h.z}, {-h.x, h.y, -h.z}},   // -x
		{{h.x, -h.y, h.z}, {h.x, -h.y, -h.z}, {h.x, h.y, -h.z}, {h.x, h.y, h.z}},       // +x
		{{-h.x, -h.y, -h.z}, {h.x, -h.y, -h.z}, {h.x, -h.y, h.z}, {-h.x, -h.y, h.z}},   // -y
		{{-h.x, h.y, h.z}, {h.x, h.y, h.z}, {h.x, h.y, -h.z}, {-h.x, h.y, -h.z}},       // +y
		{{h.x, -h.y, -h.z}, {-h.x, -h.y, -h.z}, {-h.x, h.y, -h.z}, {h.x, h.y, -h.z}},   // -z
		{{-h.x, -h.y, h.z}, {h.x, -h.y, h.z}, {h.x, h.y, h.z}, {-h.x, h.y, h.z}},       // +z
	};
	for (int i = 0; i < 6; i++) {
		if (!(faces & (1 << i)))
			continue;
		const Face &q = f[i];
		Vec3 n = normalize(cross(q.b - q.a, q.d - q.a));
		float lu = length(q.b - q.a) * m.uvScale, lv = length(q.d - q.a) * m.uvScale;
		const Vec3 p[6] = {q.a, q.b, q.cc, q.a, q.cc, q.d};
		const float uv[6][2] = {{0, 0}, {lu, 0}, {lu, lv}, {0, 0}, {lu, lv}, {0, lv}};
		for (int k = 0; k < 6; k++) {
			float aoMul = (p[k].y < 0.0f) ? aoBottom : 1.0f;
			vertex(c + p[k], n, uv[k][0], uv[k][1], m, aoMul);
		}
	}
}

void MeshBuilder::cylinder(Vec3 base, float r0, float r1, float height, int segs, const Material &m, bool capTop, bool capBottom) {
	rod(base, base + Vec3{0, height, 0}, r0, r1, segs, m, false);
	Vec3 top = base + Vec3{0, height, 0};
	for (int i = 0; i < segs; i++) {
		float a0 = (float)i / segs * 2.0f * kPi, a1 = (float)(i + 1) / segs * 2.0f * kPi;
		Vec3 d0{cosf(a0), 0, sinf(a0)}, d1{cosf(a1), 0, sinf(a1)};
		if (capTop && r1 > 0.0f) {
			const float uv[6] = {0.5f, 0.5f, 0.5f + d1.x * 0.5f, 0.5f + d1.z * 0.5f, 0.5f + d0.x * 0.5f, 0.5f + d0.z * 0.5f};
			tri(top, top + d1 * r1, top + d0 * r1, {0, 1, 0}, uv, m);
		}
		if (capBottom && r0 > 0.0f) {
			const float uv[6] = {0.5f, 0.5f, 0.5f + d0.x * 0.5f, 0.5f + d0.z * 0.5f, 0.5f + d1.x * 0.5f, 0.5f + d1.z * 0.5f};
			tri(base, base + d0 * r0, base + d1 * r0, {0, -1, 0}, uv, m);
		}
	}
}

void MeshBuilder::rod(Vec3 a, Vec3 b, float r0, float r1, int segs, const Material &m, bool caps) {
	Vec3 axis = b - a;
	float h = length(axis);
	if (h < 1e-6f)
		return;
	Vec3 y = axis * (1.0f / h);
	Vec3 ref = fabsf(y.y) > 0.9f ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
	// Base directe (x, y, z) : x = y × z.
	Vec3 z = normalize(cross(ref, y));
	Vec3 x = cross(y, z);
	float slope = (r0 - r1) / h;
	for (int i = 0; i < segs; i++) {
		float a0 = (float)i / segs * 2.0f * kPi, a1 = (float)(i + 1) / segs * 2.0f * kPi;
		Vec3 d0 = x * cosf(a0) + z * sinf(a0), d1 = x * cosf(a1) + z * sinf(a1);
		Vec3 n0 = normalize(d0 + y * slope), n1 = normalize(d1 + y * slope);
		Vec3 p00 = a + d0 * r0, p10 = a + d1 * r0, p01 = b + d0 * r1, p11 = b + d1 * r1;
		float u0 = (float)i / segs, u1 = (float)(i + 1) / segs;
		float us = std::max(1.0f, roundf(2.0f * kPi * std::max(r0, r1) * m.uvScale));
		float v1 = h * m.uvScale;
		vertex(p10, n1, u1 * us, 0, m);
		vertex(p00, n0, u0 * us, 0, m);
		vertex(p01, n0, u0 * us, v1, m);
		vertex(p10, n1, u1 * us, 0, m);
		vertex(p01, n0, u0 * us, v1, m);
		vertex(p11, n1, u1 * us, v1, m);
		if (caps) {
			const float uv[6] = {0.5f, 0.5f, 0.6f, 0.5f, 0.5f, 0.6f};
			if (r1 > 0.0f)
				tri(b, b + d1 * r1, b + d0 * r1, y, uv, m);
			if (r0 > 0.0f)
				tri(a, a + d0 * r0, a + d1 * r0, y * -1.0f, uv, m);
		}
	}
}

void MeshBuilder::lathe(Vec3 base, const ProfilePoint *p, int count, int segs, const Material &m) {
	if (count < 2)
		return;
	// Normales du profil (dans le plan rayon/hauteur), lissées entre segments voisins.
	std::vector<Vec3> pn((size_t)count);
	for (int k = 0; k < count; k++) {
		int k0 = std::max(0, k - 1), k1 = std::min(count - 1, k + 1);
		float dr = p[k1].r - p[k0].r, dy = p[k1].y - p[k0].y;
		float l = sqrtf(dr * dr + dy * dy);
		pn[(size_t)k] = l > 1e-6f ? Vec3{dy / l, -dr / l, 0} : Vec3{1, 0, 0};
	}
	float vAcc = 0.0f;
	float rMax = 0.0f;
	for (int k = 0; k < count; k++)
		rMax = std::max(rMax, p[k].r);
	float us = std::max(1.0f, roundf(2.0f * kPi * rMax * m.uvScale));
	for (int k = 0; k + 1 < count; k++) {
		float dr = p[k + 1].r - p[k].r, dy = p[k + 1].y - p[k].y;
		float vNext = vAcc + sqrtf(dr * dr + dy * dy) * m.uvScale;
		for (int i = 0; i < segs; i++) {
			float a0 = (float)i / segs * 2.0f * kPi, a1 = (float)(i + 1) / segs * 2.0f * kPi;
			Vec3 d0{cosf(a0), 0, sinf(a0)}, d1{cosf(a1), 0, sinf(a1)};
			auto pos = [&](int kk, Vec3 d) { return base + d * p[kk].r + Vec3{0, p[kk].y, 0}; };
			auto nrm = [&](int kk, Vec3 d) { return normalize(d * pn[(size_t)kk].x + Vec3{0, pn[(size_t)kk].y, 0}); };
			float u0 = (float)i / segs * us, u1 = (float)(i + 1) / segs * us;
			vertex(pos(k, d1), nrm(k, d1), u1, vAcc, m);
			vertex(pos(k, d0), nrm(k, d0), u0, vAcc, m);
			vertex(pos(k + 1, d0), nrm(k + 1, d0), u0, vNext, m);
			vertex(pos(k, d1), nrm(k, d1), u1, vAcc, m);
			vertex(pos(k + 1, d0), nrm(k + 1, d0), u0, vNext, m);
			vertex(pos(k + 1, d1), nrm(k + 1, d1), u1, vNext, m);
		}
		vAcc = vNext;
	}
}

void MeshBuilder::frame(float w, float h, const ProfilePoint *profile, int count, const Material &m, float uvPerUnit) {
	// Chaque côté est décrit par un axe « le long » A et un axe « vers l'extérieur » O.
	const Vec3 A[4] = {{1, 0, 0}, {0, 1, 0}, {-1, 0, 0}, {0, -1, 0}};
	const Vec3 O[4] = {{0, -1, 0}, {1, 0, 0}, {0, 1, 0}, {-1, 0, 0}};
	for (int s = 0; s < 4; s++) {
		float len = (s % 2 == 0) ? w : h, off = (s % 2 == 0) ? h : w;
		Vec3 S = A[s] * (-len * 0.5f) + O[s] * (off * 0.5f);
		Vec3 E = A[s] * (len * 0.5f) + O[s] * (off * 0.5f);
		for (int k = 0; k + 1 < count; k++) {
			float o0 = profile[k].r, z0 = profile[k].y, o1 = profile[k + 1].r, z1 = profile[k + 1].y;
			Vec3 s0 = S + O[s] * o0 - A[s] * o0 + Vec3{0, 0, z0}, e0 = E + O[s] * o0 + A[s] * o0 + Vec3{0, 0, z0};
			Vec3 s1 = S + O[s] * o1 - A[s] * o1 + Vec3{0, 0, z1}, e1 = E + O[s] * o1 + A[s] * o1 + Vec3{0, 0, z1};
			Vec3 n = normalize(cross(e1 - s1, s0 - s1));
			float v0 = (float)k / (float)(count - 1), v1 = (float)(k + 1) / (float)(count - 1);
			float ua1 = (-len * 0.5f - o1) * uvPerUnit, ub1 = (len * 0.5f + o1) * uvPerUnit;
			float ua0 = (-len * 0.5f - o0) * uvPerUnit, ub0 = (len * 0.5f + o0) * uvPerUnit;
			vertex(s1, n, ua1, v1, m);
			vertex(e1, n, ub1, v1, m);
			vertex(e0, n, ub0, v0, m);
			vertex(s1, n, ua1, v1, m);
			vertex(e0, n, ub0, v0, m);
			vertex(s0, n, ua0, v0, m);
		}
	}
}

void FxBuilder::sprite(Vec3 center, float w, float h, const float color[4], int type, int mode, float phase) {
	const float cx[6] = {-0.5f, 0.5f, 0.5f, -0.5f, 0.5f, -0.5f};
	const float cy[6] = {-0.5f, -0.5f, 0.5f, -0.5f, 0.5f, 0.5f};
	for (int i = 0; i < 6; i++) {
		FxVertex v;
		v.center[0] = center.x; v.center[1] = center.y; v.center[2] = center.z;
		v.corner[0] = cx[i] * w; v.corner[1] = cy[i] * h;
		v.uv[0] = cx[i] + 0.5f; v.uv[1] = cy[i] + 0.5f;
		memcpy(v.color, color, sizeof(v.color));
		v.type = (float)type;
		v.mode = (float)mode;
		v.phase = phase;
		verts.push_back(v);
	}
}

void FxBuilder::quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d, const float color[4], int type, float phase) {
	const Vec3 p[6] = {a, b, c, a, c, d};
	const float uv[6][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 0}, {1, 1}, {0, 1}};
	for (int i = 0; i < 6; i++) {
		FxVertex v;
		v.center[0] = p[i].x; v.center[1] = p[i].y; v.center[2] = p[i].z;
		v.corner[0] = v.corner[1] = 0.0f;
		v.uv[0] = uv[i][0]; v.uv[1] = uv[i][1];
		memcpy(v.color, color, sizeof(v.color));
		v.type = (float)type;
		v.mode = 0.0f;
		v.phase = phase;
		verts.push_back(v);
	}
}

} // namespace lolvr
