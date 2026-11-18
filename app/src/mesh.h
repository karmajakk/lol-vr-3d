// lol-vr — construction de maillages (triangles non indexés) pour le décor.
#pragma once

#include <stdint.h>
#include <vector>

#include "proctex.h"
#include "vecmath.h"

namespace lolvr {

// Sommet du décor éclairé (44 octets).
struct Vertex {
	float pos[3];
	float normal[3];
	float uvl[3];    // u, v, couche de texture
	uint8_t tint[4]; // couleur sRGB ; alpha inutilisé
	uint8_t ao;      // occlusion ambiante (255 = aucune)
	uint8_t emissive; // 255 = entièrement auto-éclairé
	uint8_t pad[2];
};

// Sommet d'effet lumineux (halo, flamme, ombre portée, faisceau).
struct FxVertex {
	float center[3];
	float corner[2]; // décalage en mètres dans le plan de l'écran (mode affiche)
	float uv[2];
	float color[4];  // linéaire
	float type;      // voir FxType
	float mode;      // 0 = quad libre, 1 = face caméra, 2 = face caméra autour de l'axe vertical
	float phase;
};

enum FxType { kFxGlow = 0, kFxFlame = 1, kFxShadow = 2, kFxBeam = 3, kFxSoftRect = 4 };

struct Material {
	int layer = kTexWhite;
	Vec3 tint = {1, 1, 1}; // sRGB
	float emissive = 0.0f;
	float uvScale = 1.0f;  // répétitions par mètre (plaquage planaire)
};

inline Material mat(int layer, Vec3 tint = {1, 1, 1}, float uvScale = 1.0f, float emissive = 0.0f) {
	Material m;
	m.layer = layer;
	m.tint = tint;
	m.uvScale = uvScale;
	m.emissive = emissive;
	return m;
}

struct ProfilePoint {
	float r, y;
};

class MeshBuilder {
public:
	std::vector<Vertex> verts;
	Mat4 xf = identity();
	float ao = 1.0f; // occlusion appliquée aux primitives suivantes

	void setTransform(const Mat4 &m) { xf = m; }
	void resetTransform() { xf = identity(); }

	void vertex(Vec3 p, Vec3 n, float u, float v, const Material &m, float aoMul = 1.0f);
	void tri(Vec3 a, Vec3 b, Vec3 c, Vec3 n, const float uv[6], const Material &m);
	// Quad a-b-c-d (sens trigonométrique vu de face), uv donnés aux quatre coins.
	void quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d, const Material &m, float u0 = 0, float v0 = 0, float u1 = 1, float v1 = 1);
	// Quad subdivisé (pour l'éclairage par sommet) : origine + deux arêtes, uv planaires en mètres * uvScale.
	void grid(Vec3 origin, Vec3 eu, Vec3 ev, const Material &m, float cell = 0.3f, float uOff = 0, float vOff = 0);
	// Boîte alignée sur les axes (dans le repère courant). faces : bits 0..5 = -x,+x,-y,+y,-z,+z.
	void box(Vec3 center, Vec3 half, const Material &m, int faces = 63, float aoBottom = 1.0f);
	// Tronc de cône d'axe y.
	void cylinder(Vec3 base, float r0, float r1, float height, int segs, const Material &m, bool capTop = true, bool capBottom = false);
	// Surface de révolution autour de l'axe y, à partir d'un profil (rayon, hauteur) du bas vers le haut.
	void lathe(Vec3 base, const ProfilePoint *profile, int count, int segs, const Material &m);
	// Moulure rectangulaire (cadre) dans le plan xy, ouverture w x h centrée ; le profil donne
	// (décalage vers l'extérieur, relief en z).
	void frame(float w, float h, const ProfilePoint *profile, int count, const Material &m, float uvPerUnit);
	// Cylindre entre deux points quelconques.
	void rod(Vec3 a, Vec3 b, float r0, float r1, int segs, const Material &m, bool caps = true);
};

class FxBuilder {
public:
	std::vector<FxVertex> verts;
	void sprite(Vec3 center, float w, float h, const float color[4], int type, int mode, float phase = 0.0f);
	void quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d, const float color[4], int type, float phase = 0.0f);
};

} // namespace lolvr
