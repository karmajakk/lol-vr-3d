// lol-vr — construction de la bibliothèque médiévale.
// Repère : origine au sol sous le joueur, -z vers l'écran (mur du fond), +x à droite, y vers le haut.

#include "room.h"

#include <algorithm>

namespace lolvr {

namespace {

const float kPi = 3.14159265f;
const float X = kRoomHalfX, ZF = kRoomFrontZ, ZB = kRoomBackZ, H = kRoomHeight;
const float kPanelH = 0.9f;

struct Rng {
	uint32_t s;
	float f() {
		s = s * 1664525u + 1013904223u;
		return (float)((s >> 8) & 0xFFFFFF) / 16777216.0f;
	}
	float range(float a, float b) { return a + (b - a) * f(); }
	int i(int n) { return std::min(n - 1, (int)(f() * (float)n)); }
};

float rad(float deg) { return deg * kPi / 180.0f; }
Mat4 place(Vec3 pos, float yawDeg) { return poseMatrix(pos, quatYaw(rad(yawDeg))); }

const Vec3 kIron = {0.16f, 0.16f, 0.18f};
const Vec3 kBrass = {0.85f, 0.62f, 0.25f};
const Vec3 kWax = {0.93f, 0.88f, 0.72f};
const Vec3 kPaperTint = {0.80f, 0.74f, 0.60f};

const Vec3 kBookColors[] = {
	{0.46f, 0.10f, 0.08f}, {0.12f, 0.26f, 0.14f}, {0.10f, 0.17f, 0.35f}, {0.36f, 0.22f, 0.10f}, {0.56f, 0.43f, 0.22f},
	{0.17f, 0.11f, 0.09f}, {0.40f, 0.12f, 0.28f}, {0.62f, 0.52f, 0.40f}, {0.08f, 0.23f, 0.27f}, {0.52f, 0.29f, 0.08f},
	{0.30f, 0.06f, 0.06f}, {0.22f, 0.20f, 0.30f}};
const int kBookColorCount = (int)(sizeof(kBookColors) / sizeof(kBookColors[0]));

// Bougie posée en `p` (repère courant) ; enregistre la position de sa flamme.
void candle(MeshBuilder &mb, RoomInfo &info, Vec3 p, float height, float radius) {
	mb.cylinder(p, radius, radius * 0.92f, height, 8, mat(kTexWhite, kWax, 1.0f, 0.35f));
	mb.cylinder(p + Vec3{0, height, 0}, 0.0015f, 0.0015f, 0.012f, 4, mat(kTexWhite, {0.05f, 0.04f, 0.03f}), false);
	info.candleFlames.push_back(transformPoint(mb.xf, p + Vec3{0, height + 0.03f, 0}));
}

void candlestick(MeshBuilder &mb, RoomInfo &info, Vec3 p, float scale) {
	const ProfilePoint prof[] = {{0.050f, 0.0f}, {0.052f, 0.008f}, {0.030f, 0.018f}, {0.012f, 0.035f}, {0.010f, 0.09f},
	                             {0.020f, 0.105f}, {0.010f, 0.12f}, {0.010f, 0.17f}, {0.030f, 0.185f}, {0.032f, 0.195f}, {0.0f, 0.195f}};
	ProfilePoint sp[11];
	for (int i = 0; i < 11; i++)
		sp[i] = {prof[i].r * scale, prof[i].y * scale};
	mb.lathe(p, sp, 11, 10, mat(kTexMetal, kBrass, 4.0f, 0.04f));
	candle(mb, info, p + Vec3{0, 0.195f * scale, 0}, 0.13f * scale, 0.012f * scale);
}

void potion(MeshBuilder &mb, RoomInfo &info, Vec3 p, Vec3 color, float scale) {
	const ProfilePoint prof[] = {{0.0f, 0.0f}, {0.030f, 0.003f}, {0.044f, 0.025f}, {0.042f, 0.055f}, {0.016f, 0.085f},
	                             {0.012f, 0.115f}, {0.017f, 0.120f}, {0.017f, 0.128f}, {0.0f, 0.128f}};
	ProfilePoint sp[9];
	for (int i = 0; i < 9; i++)
		sp[i] = {prof[i].r * scale, prof[i].y * scale};
	mb.lathe(p, sp, 9, 10, mat(kTexWhite, color, 1.0f, 0.55f));
	mb.cylinder(p + Vec3{0, 0.128f * scale, 0}, 0.011f * scale, 0.013f * scale, 0.02f * scale, 6, mat(kTexWood, {0.9f, 0.75f, 0.55f}));
	Vec3 lin = {powf(color.x, 2.2f), powf(color.y, 2.2f), powf(color.z, 2.2f)};
	info.glows.push_back({transformPoint(mb.xf, p + Vec3{0, 0.05f * scale, 0}), lin * 0.5f, 0.22f * scale});
}

// Un livre debout : dos vers +z. (x0..x1, y0..y1, zb..zf)
void bookUpright(MeshBuilder &mb, Rng &rng, float x0, float x1, float y0, float y1, float zb, float zf) {
	Vec3 col = kBookColors[rng.i(kBookColorCount)] * rng.range(0.75f, 1.15f);
	int k = rng.i(4);
	float u0 = (float)k * 0.25f + 0.012f, u1 = (float)(k + 1) * 0.25f - 0.012f;
	Material spine = mat(kTexBooks, col);
	Material cover = mat(kTexLeather, col * 0.85f);
	Material pages = mat(kTexWhite, kPaperTint * rng.range(0.8f, 1.0f));
	mb.quad({x0, y0, zf}, {x1, y0, zf}, {x1, y1, zf}, {x0, y1, zf}, spine, u0, 0.0f, u1, 1.0f);
	mb.quad({x0, y1 - 0.004f, zf - 0.006f}, {x1, y1 - 0.004f, zf - 0.006f}, {x1, y1 - 0.004f, zb}, {x0, y1 - 0.004f, zb}, pages);
	mb.quad({x0, y0, zb}, {x0, y0, zf}, {x0, y1, zf}, {x0, y1, zb}, cover);
	mb.quad({x1, y0, zf}, {x1, y0, zb}, {x1, y1, zb}, {x1, y1, zf}, cover);
}

// Un livre posé à plat, centré en `c`, tourné de `yawDeg`.
void bookFlat(MeshBuilder &mb, Rng &rng, Vec3 c, float w, float d, float t, float yawDeg) {
	Mat4 saved = mb.xf;
	mb.setTransform(multiply(saved, place(c, yawDeg)));
	Vec3 col = kBookColors[rng.i(kBookColorCount)] * rng.range(0.8f, 1.1f);
	mb.box({0, t * 0.5f, 0}, {w * 0.5f, t * 0.5f, d * 0.5f}, mat(kTexLeather, col, 3.0f));
	mb.box({0.004f, t * 0.5f, 0}, {w * 0.5f, t * 0.5f - 0.004f, d * 0.5f - 0.004f}, mat(kTexWhite, kPaperTint), 2 | 16 | 32);
	mb.setTransform(saved);
}

void bookcase(MeshBuilder &mb, RoomInfo &info, Rng &rng, const Mat4 &xf, float W, float Hc, float D, int shelves) {
	mb.setTransform(xf);
	Material wood = mat(kTexWood, {1, 1, 1}, 1.3f);
	Material dark = mat(kTexWood, {0.45f, 0.42f, 0.40f}, 1.3f);
	const float side = 0.035f;
	mb.box({-(W * 0.5f - side * 0.5f), Hc * 0.5f, 0}, {side * 0.5f, Hc * 0.5f, D * 0.5f}, wood, 63, 0.7f);
	mb.box({(W * 0.5f - side * 0.5f), Hc * 0.5f, 0}, {side * 0.5f, Hc * 0.5f, D * 0.5f}, wood, 63, 0.7f);
	mb.box({0, 0.06f, 0.0f}, {W * 0.5f, 0.06f, D * 0.5f + 0.01f}, wood, 63, 0.6f);
	mb.box({0, Hc + 0.045f, 0.015f}, {W * 0.5f + 0.04f, 0.045f, D * 0.5f + 0.03f}, wood);
	mb.box({0, Hc - 0.02f, 0}, {W * 0.5f, 0.02f, D * 0.5f}, wood);
	mb.ao = 0.55f;
	mb.box({0, Hc * 0.5f, -D * 0.5f + 0.012f}, {W * 0.5f - side, Hc * 0.5f, 0.012f}, dark, 32);
	mb.ao = 1.0f;
	float y0 = 0.12f, span = (Hc - 0.04f - y0) / (float)shelves;
	for (int s = 0; s < shelves; s++) {
		float ys = y0 + span * (float)s; // dessus de la tablette
		if (s > 0)
			mb.box({0, ys - 0.015f, 0}, {W * 0.5f - side, 0.015f, D * 0.5f - 0.01f}, wood);
		float clear = span - 0.035f;
		float x = -W * 0.5f + side + 0.01f, xEnd = W * 0.5f - side - 0.01f;
		while (x < xEnd - 0.03f) {
			float r = rng.f();
			if (r < 0.07f) {
				x += rng.range(0.04f, 0.14f);
				continue;
			}
			if (r < 0.12f && xEnd - x > 0.26f) {
				// Pile de livres à plat
				float w = rng.range(0.20f, 0.25f);
				int n = 2 + rng.i(3);
				float yy = ys;
				for (int k = 0; k < n; k++) {
					float t = rng.range(0.03f, 0.05f);
					bookFlat(mb, rng, {x + w * 0.5f, yy, D * 0.5f - 0.13f}, 0.16f, w - 0.02f * (float)k, t, 90.0f + rng.range(-4.0f, 4.0f));
					yy += t;
				}
				x += w + 0.01f;
				continue;
			}
			if (r < 0.145f && xEnd - x > 0.14f && clear > 0.2f) {
				// Un bibelot : fiole ou bougeoir
				if (rng.f() < 0.6f) {
					const Vec3 cols[] = {{0.75f, 0.15f, 0.12f}, {0.15f, 0.65f, 0.30f}, {0.30f, 0.35f, 0.85f}, {0.70f, 0.30f, 0.80f}};
					potion(mb, info, {x + 0.06f, ys, D * 0.5f - 0.10f}, cols[rng.i(4)], 1.1f);
				} else {
					const ProfilePoint vase[] = {{0.0f, 0.0f}, {0.035f, 0.004f}, {0.055f, 0.06f}, {0.045f, 0.11f}, {0.022f, 0.14f}, {0.030f, 0.16f}, {0.0f, 0.16f}};
					mb.lathe({x + 0.07f, ys, D * 0.5f - 0.11f}, vase, 7, 10, mat(kTexMetal, kBrass * rng.range(0.6f, 1.0f), 4.0f));
				}
				x += 0.14f;
				continue;
			}
			float t = rng.range(0.024f, 0.062f);
			if (x + t > xEnd)
				break;
			float hb = clear * rng.range(0.62f, 0.96f);
			float db = rng.range(0.15f, 0.21f);
			float zf = D * 0.5f - 0.025f - rng.range(0.0f, 0.025f);
			bookUpright(mb, rng, x, x + t, ys, ys + hb, zf - db, zf);
			x += t + 0.002f;
		}
	}
	mb.resetTransform();
}

// Tableau encadré, face vers +z dans le repère courant.
void painting(MeshBuilder &mb, float w, float h, int layer) {
	const float s = 1.0f;
	const ProfilePoint prof[] = {{-0.012f * s, 0.006f}, {0.0f, 0.024f}, {0.020f, 0.034f}, {0.045f, 0.024f}, {0.062f, 0.032f}, {0.074f, 0.0f}};
	mb.quad({-w * 0.5f, -h * 0.5f, 0.008f}, {w * 0.5f, -h * 0.5f, 0.008f}, {w * 0.5f, h * 0.5f, 0.008f}, {-w * 0.5f, h * 0.5f, 0.008f},
	        mat(layer, {1, 1, 1}, 1.0f, 0.10f));
	mb.frame(w, h, prof, 6, mat(kTexGold, {1, 1, 1}), 3.5f);
}

// Image du joueur sur deux couches (moitié gauche, moitié droite), dans le rectangle a-b-c-d
// (bas gauche, bas droite, haut droite, haut gauche).
void imageQuad(MeshBuilder &mb, Vec3 a, Vec3 b, Vec3 c, Vec3 d, int layerA, float emissive) {
	const float m = kImgMargin;
	Vec3 mb0 = lerp(a, b, 0.5f), mt0 = lerp(d, c, 0.5f);
	mb.quad(a, mb0, mt0, d, mat(layerA, {1, 1, 1}, 1.0f, emissive), m, m, 1.0f - m, 1.0f - m);
	mb.quad(mb0, b, c, mt0, mat(layerA + 1, {1, 1, 1}, 1.0f, emissive), m, m, 1.0f - m, 1.0f - m);
}

// Tableau encadré dont la toile est une image du joueur.
void paintingImage(MeshBuilder &mb, float w, float h, int layerA) {
	const ProfilePoint prof[] = {{-0.012f, 0.006f}, {0.0f, 0.026f}, {0.022f, 0.040f}, {0.050f, 0.028f}, {0.072f, 0.040f}, {0.088f, 0.0f}};
	imageQuad(mb, {-w * 0.5f, -h * 0.5f, 0.008f}, {w * 0.5f, -h * 0.5f, 0.008f}, {w * 0.5f, h * 0.5f, 0.008f}, {-w * 0.5f, h * 0.5f, 0.008f}, layerA, 0.16f);
	mb.frame(w, h, prof, 6, mat(kTexGold, {1, 1, 1}), 3.5f);
	// Petite plaque en laiton sous le cadre
	mb.box({0, -h * 0.5f - 0.115f, 0.012f}, {0.13f, 0.022f, 0.006f}, mat(kTexMetal, kBrass, 6.0f, 0.05f));
}

// --- Objets inspirés de l'univers du jeu (dessinés ici, sans rien reprendre des images du jeu) ---

// Gros rubis taillé sur un support à griffes.
void rubyOnStand(MeshBuilder &mb, RoomInfo &info, Vec3 p) {
	const ProfilePoint stand[] = {{0.060f, 0.0f}, {0.062f, 0.010f}, {0.030f, 0.022f}, {0.014f, 0.050f}, {0.026f, 0.075f}, {0.0f, 0.075f}};
	mb.lathe(p, stand, 6, 10, mat(kTexMetal, kBrass, 6.0f, 0.04f));
	const ProfilePoint gem[] = {{0.0f, 0.0f}, {0.050f, 0.050f}, {0.058f, 0.062f}, {0.036f, 0.092f}, {0.0f, 0.096f}};
	Vec3 g = p + Vec3{0, 0.070f, 0};
	mb.lathe(g, gem, 5, 8, mat(kTexWhite, {0.92f, 0.06f, 0.10f}, 1.0f, 0.55f));
	for (int i = 0; i < 4; i++) {
		float a = (float)i / 4.0f * 2.0f * kPi + 0.4f;
		mb.rod(p + Vec3{cosf(a) * 0.020f, 0.066f, sinf(a) * 0.020f}, g + Vec3{cosf(a) * 0.056f, 0.060f, sinf(a) * 0.056f}, 0.004f, 0.003f, 4, mat(kTexMetal, kBrass, 6.0f));
	}
	info.glows.push_back({transformPoint(mb.xf, g + Vec3{0, 0.05f, 0}), Vec3{0.9f, 0.05f, 0.08f} * 0.55f, 0.34f});
}

// Lanterne à huile en laiton.
void lantern(MeshBuilder &mb, RoomInfo &info, Vec3 p) {
	Material brass = mat(kTexMetal, kBrass, 6.0f, 0.04f);
	const ProfilePoint base[] = {{0.058f, 0.0f}, {0.060f, 0.012f}, {0.050f, 0.030f}, {0.040f, 0.046f}, {0.0f, 0.046f}};
	mb.lathe(p, base, 5, 12, brass);
	mb.cylinder(p + Vec3{0, 0.046f, 0}, 0.036f, 0.040f, 0.105f, 10, mat(kTexWhite, {1.0f, 0.78f, 0.42f}, 1.0f, 0.60f), false, false);
	for (int i = 0; i < 4; i++) {
		float a = (float)i / 4.0f * 2.0f * kPi + 0.78f;
		mb.rod(p + Vec3{cosf(a) * 0.042f, 0.046f, sinf(a) * 0.042f}, p + Vec3{cosf(a) * 0.042f, 0.152f, sinf(a) * 0.042f}, 0.004f, 0.004f, 4, brass, false);
	}
	const ProfilePoint cap[] = {{0.050f, 0.0f}, {0.046f, 0.012f}, {0.020f, 0.036f}, {0.012f, 0.052f}, {0.0f, 0.052f}};
	mb.lathe(p + Vec3{0, 0.150f, 0}, cap, 5, 12, brass);
	const int n = 8;
	for (int i = 0; i < n; i++) {
		float a0 = kPi * (float)i / n, a1 = kPi * (float)(i + 1) / n;
		mb.rod(p + Vec3{cosf(a0) * 0.045f, 0.19f + sinf(a0) * 0.050f, 0}, p + Vec3{cosf(a1) * 0.045f, 0.19f + sinf(a1) * 0.050f, 0}, 0.003f, 0.003f, 4, brass, false);
	}
	info.candleFlames.push_back(transformPoint(mb.xf, p + Vec3{0, 0.085f, 0}));
}

// Petit crâne verdâtre (les clés-crânes des donjons).
void greenSkull(MeshBuilder &mb, RoomInfo &info, Vec3 p, float yawDeg) {
	Mat4 saved = mb.xf;
	mb.setTransform(multiply(saved, place(p, yawDeg)));
	Material bone = mat(kTexWhite, {0.62f, 0.74f, 0.50f}, 1.0f, 0.08f);
	ProfilePoint sph[8];
	for (int i = 0; i < 8; i++) {
		float a = -kPi * 0.35f + kPi * 0.85f * (float)i / 7.0f;
		sph[i] = {cosf(a) * 0.050f, 0.060f + sinf(a) * 0.052f};
	}
	sph[7].r = 0.0f;
	mb.lathe({0, 0, 0}, sph, 8, 10, bone);
	mb.box({0, 0.016f, 0.020f}, {0.030f, 0.016f, 0.026f}, bone);
	Material hole = mat(kTexWhite, {0.02f, 0.05f, 0.02f});
	for (float sx : {-1.0f, 1.0f})
		mb.box({sx * 0.019f, 0.060f, 0.045f}, {0.011f, 0.012f, 0.004f}, hole);
	mb.box({0, 0.036f, 0.047f}, {0.005f, 0.008f, 0.003f}, hole);
	mb.setTransform(saved);
	info.glows.push_back({transformPoint(mb.xf, p + Vec3{0, 0.06f, 0}), Vec3{0.15f, 0.6f, 0.15f} * 0.35f, 0.18f});
}

// Épée à lame d'émeraude sur un présentoir en bois (repère : lame le long de x).
void emeraldBlade(MeshBuilder &mb, RoomInfo &info, Vec3 p) {
	Material wood = mat(kTexWood, {0.9f, 0.85f, 0.8f}, 2.0f);
	mb.box(p + Vec3{0, 0.012f, 0}, {0.36f, 0.012f, 0.07f}, wood);
	for (float sx : {-0.24f, 0.20f}) {
		mb.box(p + Vec3{sx, 0.085f, 0}, {0.012f, 0.062f, 0.045f}, wood);
		mb.box(p + Vec3{sx, 0.150f, -0.030f}, {0.012f, 0.012f, 0.010f}, wood);
		mb.box(p + Vec3{sx, 0.150f, 0.030f}, {0.012f, 0.012f, 0.010f}, wood);
	}
	Vec3 c = p + Vec3{0, 0.155f, 0};
	Material blade = mat(kTexWhite, {0.10f, 0.85f, 0.38f}, 1.0f, 0.45f);
	// Lame en losange, pointe vers +x
	Vec3 b0 = c + Vec3{-0.26f, 0, 0}, b1 = c + Vec3{0.34f, 0, 0}, tip = c + Vec3{0.44f, 0, 0};
	const float hw = 0.022f, ht = 0.006f;
	for (int side = 0; side < 4; side++) {
		float sy = (side & 1) ? 1.0f : -1.0f, sz = (side & 2) ? 1.0f : -1.0f;
		Vec3 e0 = b0 + Vec3{0, 0, sz * hw}, e1 = b1 + Vec3{0, 0, sz * hw * 0.8f};
		Vec3 r0 = b0 + Vec3{0, sy * ht, 0}, r1 = b1 + Vec3{0, sy * ht, 0};
		Vec3 n = normalize(Vec3{0, sy * hw, sz * ht});
		const float uv[6] = {0, 0, 1, 0, 1, 1};
		if (sy * sz > 0) {
			mb.tri(e0, e1, r1, n, uv, blade); mb.tri(e0, r1, r0, n, uv, blade); mb.tri(e1, tip, r1, n, uv, blade);
		} else {
			mb.tri(e1, e0, r1, n, uv, blade); mb.tri(r1, e0, r0, n, uv, blade); mb.tri(tip, e1, r1, n, uv, blade);
		}
	}
	Material gold = mat(kTexMetal, kBrass, 6.0f, 0.05f);
	mb.box(c + Vec3{-0.27f, 0, 0}, {0.012f, 0.012f, 0.075f}, gold);
	mb.rod(c + Vec3{-0.40f, 0, 0}, c + Vec3{-0.28f, 0, 0}, 0.013f, 0.013f, 6, mat(kTexLeather, {0.20f, 0.10f, 0.07f}, 4.0f));
	mb.box(c + Vec3{-0.415f, 0, 0}, {0.018f, 0.018f, 0.018f}, gold);
	info.glows.push_back({transformPoint(mb.xf, c + Vec3{0.10f, 0.02f, 0}), Vec3{0.08f, 0.7f, 0.3f} * 0.35f, 0.5f});
}

// Socle en pierre (colonne tronquée).
void pedestal(MeshBuilder &mb, RoomInfo &info, float height) {
	const ProfilePoint prof[] = {{0.20f, 0.0f}, {0.20f, 0.06f}, {0.15f, 0.10f}, {0.13f, height - 0.12f}, {0.17f, height - 0.07f}, {0.19f, height - 0.05f}, {0.19f, height}, {0.0f, height}};
	mb.lathe({0, 0, 0}, prof, 8, 8, mat(kTexStone, {0.95f, 0.92f, 0.9f}, 1.2f));
	info.shadows.push_back({transformPoint(mb.xf, {0, 0.012f, 0}), 0.30f, 0.30f, 0.5f});
}

// Masque sombre aux yeux luisants, sur une tige.
void darkMask(MeshBuilder &mb, RoomInfo &info, Vec3 p) {
	mb.rod(p, p + Vec3{0, 0.16f, 0}, 0.010f, 0.008f, 6, mat(kTexMetal, kIron, 4.0f));
	mb.cylinder(p, 0.06f, 0.05f, 0.012f, 10, mat(kTexMetal, kIron, 4.0f), true, false);
	Mat4 saved = mb.xf;
	// Coque bombée : demi-ellipsoïde tourné vers +z
	mb.setTransform(multiply(saved, multiply(poseMatrix(p + Vec3{0, 0.30f, 0}, quatAxisAngle({1, 0, 0}, rad(90.0f))),
	                                         Mat4{{0.80f, 0, 0, 0, 0, 0.55f, 0, 0, 0, 0, 1.25f, 0, 0, 0, 0, 1}})));
	ProfilePoint dome[7];
	for (int i = 0; i < 7; i++) {
		float a = kPi * 0.5f * (float)i / 6.0f;
		dome[i] = {cosf(a) * 0.15f, sinf(a) * 0.15f};
	}
	mb.lathe({0, 0, 0}, dome, 7, 12, mat(kTexMetal, {0.20f, 0.12f, 0.26f}, 5.0f, 0.03f));
	mb.setTransform(saved);
	Vec3 c = p + Vec3{0, 0.30f, 0};
	Material eye = mat(kTexWhite, {0.80f, 0.25f, 1.0f}, 1.0f, 1.0f);
	for (float sx : {-1.0f, 1.0f}) {
		const float uv[6] = {0, 0, 1, 0, 0.5f, 1};
		Vec3 e = c + Vec3{sx * 0.050f, 0.030f, 0.082f};
		Vec3 a = e + Vec3{-sx * 0.030f, -0.012f, 0.004f}, b = e + Vec3{sx * 0.030f, 0.016f, -0.010f}, d = e + Vec3{sx * 0.022f, -0.012f, -0.004f};
		if (sx > 0)
			mb.tri(a, d, b, {0, 0, 1}, uv, eye);
		else
			mb.tri(a, b, d, {0, 0, 1}, uv, eye);
		// Cornes
		mb.rod(c + Vec3{sx * 0.085f, 0.13f, 0.0f}, c + Vec3{sx * 0.150f, 0.30f, -0.02f}, 0.022f, 0.004f, 6, mat(kTexMetal, {0.16f, 0.10f, 0.20f}, 5.0f));
	}
	info.glows.push_back({transformPoint(mb.xf, c + Vec3{0, 0.03f, 0.08f}), Vec3{0.5f, 0.12f, 0.75f} * 0.28f, 0.22f});
}

// Cube gravé de runes qui flotte au-dessus de son socle.
void runeCube(MeshBuilder &mb, RoomInfo &info, Vec3 p) {
	Mat4 saved = mb.xf;
	Quat q = multiply(quatYaw(rad(32.0f)), multiply(quatAxisAngle({1, 0, 0}, rad(35.3f)), quatAxisAngle({0, 0, 1}, rad(45.0f))));
	mb.setTransform(multiply(saved, poseMatrix(p, q)));
	const float h = 0.075f;
	mb.box({0, 0, 0}, {h, h, h}, mat(kTexMetal, {0.30f, 0.62f, 0.78f}, 5.0f, 0.30f));
	Material rune = mat(kTexWhite, {0.55f, 0.95f, 1.0f}, 1.0f, 1.0f);
	const float e = h + 0.001f, r = h * 0.55f, t = 0.006f;
	// Un motif en creux sur chaque face : carré ouvert + trait
	for (int f = 0; f < 6; f++) {
		Vec3 n{0, 0, 0}, u{0, 0, 0}, v{0, 0, 0};
		int ax = f / 2;
		float sgn = (f & 1) ? 1.0f : -1.0f;
		(&n.x)[ax] = sgn;
		(&u.x)[(ax + 1) % 3] = 1.0f;
		(&v.x)[(ax + 2) % 3] = 1.0f;
		Vec3 c = n * e;
		auto bar = [&](Vec3 a, Vec3 hu, Vec3 hv) {
			Vec3 he = hu + hv + Vec3{fabsf(n.x), fabsf(n.y), fabsf(n.z)} * 0.001f;
			mb.box(c + a, {fabsf(he.x), fabsf(he.y), fabsf(he.z)}, rune);
		};
		bar(v * r, u * r, v * t);
		bar(v * -r, u * r, v * t);
		bar(u * -r, u * t, v * r);
		bar(u * (r * 0.2f), u * t, v * (r * 0.55f));
	}
	mb.setTransform(saved);
	info.glows.push_back({transformPoint(mb.xf, p), Vec3{0.2f, 0.7f, 0.9f} * 0.32f, 0.28f});
}

void banner(MeshBuilder &mb, float w, float len, Vec3 tint) {
	// Tringle
	mb.rod({-w * 0.5f - 0.06f, 0, 0.03f}, {w * 0.5f + 0.06f, 0, 0.03f}, 0.014f, 0.014f, 8, mat(kTexMetal, kBrass, 4.0f));
	Material m = mat(kTexBanner, tint);
	const int n = 6;
	float body = len * 0.84f;
	for (int i = 0; i < n; i++) {
		float t0 = (float)i / n, t1 = (float)(i + 1) / n;
		float z0 = 0.03f + 0.012f * sinf(t0 * 7.0f), z1 = 0.03f + 0.012f * sinf(t1 * 7.0f);
		mb.quad({-w * 0.5f, -body * t1, z1}, {w * 0.5f, -body * t1, z1}, {w * 0.5f, -body * t0, z0}, {-w * 0.5f, -body * t0, z0}, m,
		        0.0f, 1.0f - t1 * 0.84f, 1.0f, 1.0f - t0 * 0.84f);
	}
	// Pointes en queue d'aronde
	float zb = 0.03f + 0.012f * sinf(7.0f);
	const float uvL[6] = {0.0f, 0.16f, 0.0f, 0.0f, 0.5f, 0.16f};
	const float uvR[6] = {0.5f, 0.16f, 1.0f, 0.0f, 1.0f, 0.16f};
	mb.tri({-w * 0.5f, -body, zb}, {-w * 0.5f, -len, zb}, {0, -body, zb}, {0, 0, 1}, uvL, m);
	mb.tri({0, -body, zb}, {w * 0.5f, -len, zb}, {w * 0.5f, -body, zb}, {0, 0, 1}, uvR, m);
}

void sconce(MeshBuilder &mb, RoomInfo &info) {
	Material iron = mat(kTexMetal, kIron, 4.0f);
	mb.box({0, 0, 0.01f}, {0.05f, 0.10f, 0.01f}, iron);
	mb.rod({0, -0.04f, 0.02f}, {0, -0.07f, 0.13f}, 0.010f, 0.010f, 6, iron);
	mb.cylinder({0, -0.075f, 0.13f}, 0.022f, 0.036f, 0.02f, 8, iron, true, true);
	candle(mb, info, {0, -0.055f, 0.13f}, 0.12f, 0.014f);
}

void chest(MeshBuilder &mb, RoomInfo &info) {
	Material wood = mat(kTexWood, {0.85f, 0.8f, 0.75f}, 1.6f);
	Material iron = mat(kTexMetal, kIron, 4.0f);
	const float w = 0.85f, d = 0.46f, h = 0.36f;
	mb.box({0, h * 0.5f, 0}, {w * 0.5f, h * 0.5f, d * 0.5f}, wood, 63, 0.55f);
	// Couvercle bombé
	const int n = 6;
	for (int i = 0; i < n; i++) {
		float a0 = (float)i / n * kPi, a1 = (float)(i + 1) / n * kPi;
		float z0 = cosf(a0) * d * 0.5f, y0 = h + sinf(a0) * 0.16f, z1 = cosf(a1) * d * 0.5f, y1 = h + sinf(a1) * 0.16f;
		mb.quad({-w * 0.5f, y0, z0}, {w * 0.5f, y0, z0}, {w * 0.5f, y1, z1}, {-w * 0.5f, y1, z1}, wood, 0.0f, (float)i * 0.15f, 1.3f, (float)(i + 1) * 0.15f);
		for (float bx : {-0.30f, 0.30f})
			mb.quad({bx - 0.03f, y0 + 0.004f, z0 * 1.01f}, {bx + 0.03f, y0 + 0.004f, z0 * 1.01f}, {bx + 0.03f, y1 + 0.004f, z1 * 1.01f}, {bx - 0.03f, y1 + 0.004f, z1 * 1.01f}, iron);
		const float uv[6] = {0.5f, 0.5f, 0.6f, 0.5f, 0.5f, 0.6f};
		mb.tri({w * 0.5f, h, 0}, {w * 0.5f, y1, z1}, {w * 0.5f, y0, z0}, {1, 0, 0}, uv, wood);
		mb.tri({-w * 0.5f, h, 0}, {-w * 0.5f, y0, z0}, {-w * 0.5f, y1, z1}, {-1, 0, 0}, uv, wood);
	}
	for (float bx : {-0.30f, 0.30f})
		mb.box({bx, h * 0.5f, 0}, {0.03f, h * 0.5f + 0.002f, d * 0.5f + 0.005f}, iron, 63 & ~4, 0.6f);
	mb.box({0, h - 0.02f, d * 0.5f + 0.012f}, {0.04f, 0.05f, 0.012f}, mat(kTexMetal, kBrass, 4.0f));
	info.shadows.push_back({transformPoint(mb.xf, {0, 0.012f, 0}), w * 0.62f, d * 0.8f, 0.6f});
}

void globe(MeshBuilder &mb, RoomInfo &info) {
	Material wood = mat(kTexWood, {1, 1, 1}, 2.0f);
	const ProfilePoint stand[] = {{0.20f, 0.0f}, {0.21f, 0.02f}, {0.06f, 0.05f}, {0.035f, 0.12f}, {0.05f, 0.30f}, {0.03f, 0.50f}, {0.045f, 0.66f}, {0.0f, 0.66f}};
	mb.lathe({0, 0, 0}, stand, 8, 12, wood);
	const float r = 0.23f, cy = 0.66f + r + 0.03f;
	ProfilePoint sph[13];
	for (int i = 0; i < 13; i++) {
		float a = -kPi * 0.5f + kPi * (float)i / 12.0f;
		sph[i] = {cosf(a) * r, sinf(a) * r};
	}
	Mat4 saved = mb.xf;
	mb.setTransform(multiply(saved, poseMatrix({0, cy, 0}, quatAxisAngle({0, 0, 1}, rad(20.0f)))));
	mb.lathe({0, 0, 0}, sph, 13, 20, mat(kTexPaintMap, {0.95f, 0.92f, 0.85f}, 1.0f / (kPi * r), 0.08f));
	mb.rod({0, -r - 0.035f, 0}, {0, r + 0.035f, 0}, 0.008f, 0.008f, 6, mat(kTexMetal, kBrass, 4.0f));
	// Méridien en laiton
	const int n = 20;
	for (int i = 0; i < n; i++) {
		float a0 = -kPi * 0.5f + kPi * (float)i / n, a1 = -kPi * 0.5f + kPi * (float)(i + 1) / n;
		float rr = r + 0.03f;
		mb.rod({cosf(a0) * rr, sinf(a0) * rr, 0}, {cosf(a1) * rr, sinf(a1) * rr, 0}, 0.008f, 0.008f, 5, mat(kTexMetal, kBrass, 4.0f), false);
	}
	mb.setTransform(saved);
	info.shadows.push_back({transformPoint(mb.xf, {0, 0.012f, 0}), 0.32f, 0.32f, 0.5f});
}

void ladder(MeshBuilder &mb) {
	Material wood = mat(kTexWood, {1.1f, 1.05f, 1.0f}, 2.0f);
	const float len = 2.55f, w = 0.42f;
	for (float sx : {-1.0f, 1.0f})
		mb.box({sx * w * 0.5f, len * 0.5f, 0}, {0.022f, len * 0.5f, 0.03f}, wood, 63, 0.6f);
	for (int i = 0; i < 8; i++)
		mb.rod({-w * 0.5f, 0.25f + 0.29f * (float)i, 0}, {w * 0.5f, 0.25f + 0.29f * (float)i, 0}, 0.016f, 0.016f, 6, wood, false);
}

void chandelier(MeshBuilder &mb, RoomInfo &info, Vec3 c) {
	Material iron = mat(kTexMetal, kIron, 4.0f);
	const float r = 0.48f;
	const int n = 18;
	for (int i = 0; i < n; i++) {
		float a0 = (float)i / n * 2.0f * kPi, a1 = (float)(i + 1) / n * 2.0f * kPi;
		mb.rod(c + Vec3{cosf(a0) * r, 0, sinf(a0) * r}, c + Vec3{cosf(a1) * r, 0, sinf(a1) * r}, 0.02f, 0.02f, 6, iron, false);
	}
	Vec3 hub = c + Vec3{0, 0.42f, 0};
	for (int i = 0; i < 3; i++) {
		float a = (float)i / 3.0f * 2.0f * kPi + 0.5f;
		mb.rod(c + Vec3{cosf(a) * r, 0, sinf(a) * r}, hub, 0.006f, 0.006f, 4, iron, false);
	}
	mb.rod(hub, {c.x, H - 0.2f, c.z}, 0.008f, 0.008f, 4, iron, false);
	for (int i = 0; i < 6; i++) {
		float a = (float)i / 6.0f * 2.0f * kPi;
		Vec3 p = c + Vec3{cosf(a) * r, 0.02f, sinf(a) * r};
		mb.cylinder(p, 0.025f, 0.04f, 0.02f, 8, iron, true, true);
		candle(mb, info, p + Vec3{0, 0.02f, 0}, 0.11f, 0.014f);
	}
}

void door(MeshBuilder &mb) {
	Material stone = mat(kTexStone, {1.05f, 1.02f, 1.0f}, 0.5f);
	Material iron = mat(kTexMetal, kIron, 4.0f);
	for (float sx : {-1.0f, 1.0f})
		mb.box({sx * 0.70f, 1.15f, 0.06f}, {0.11f, 1.15f, 0.08f}, stone, 63, 0.7f);
	mb.box({0, 2.42f, 0.06f}, {0.86f, 0.13f, 0.09f}, stone);
	mb.quad({-0.60f, 0.0f, 0.03f}, {0.60f, 0.0f, 0.03f}, {0.60f, 2.30f, 0.03f}, {-0.60f, 2.30f, 0.03f},
	        mat(kTexFloor, {0.62f, 0.52f, 0.46f}), 0.0f, 0.0f, 0.62f, 1.15f);
	for (float y : {0.42f, 1.15f, 1.88f}) {
		mb.box({0, y, 0.045f}, {0.58f, 0.045f, 0.012f}, iron);
		for (int i = 0; i < 6; i++)
			mb.box({-0.50f + 0.20f * (float)i, y, 0.062f}, {0.014f, 0.014f, 0.008f}, mat(kTexMetal, {0.3f, 0.3f, 0.33f}, 4.0f));
	}
	const int n = 10;
	for (int i = 0; i < n; i++) {
		float a0 = (float)i / n * 2.0f * kPi, a1 = (float)(i + 1) / n * 2.0f * kPi;
		mb.rod({0.42f + cosf(a0) * 0.06f, 1.02f + sinf(a0) * 0.06f, 0.075f}, {0.42f + cosf(a1) * 0.06f, 1.02f + sinf(a1) * 0.06f, 0.075f}, 0.008f, 0.008f, 5, iron, false);
	}
	mb.box({0.42f, 1.09f, 0.055f}, {0.025f, 0.025f, 0.015f}, iron);
}

void shieldAndSwords(MeshBuilder &mb, Vec3 tint) {
	Mat4 saved = mb.xf;
	// Épées croisées
	for (float sgn : {-1.0f, 1.0f}) {
		mb.setTransform(multiply(saved, poseMatrix({0, 0, 0.03f}, quatAxisAngle({0, 0, 1}, rad(sgn * 42.0f)))));
		mb.box({0, 0.10f, 0}, {0.024f, 0.52f, 0.005f}, mat(kTexMetal, {0.78f, 0.80f, 0.85f}, 4.0f, 0.04f));
		mb.box({0, -0.43f, 0}, {0.10f, 0.014f, 0.014f}, mat(kTexMetal, kBrass, 4.0f));
		mb.rod({0, -0.57f, 0}, {0, -0.44f, 0}, 0.014f, 0.014f, 6, mat(kTexLeather, {0.25f, 0.13f, 0.08f}, 4.0f));
		mb.box({0, -0.59f, 0}, {0.022f, 0.022f, 0.022f}, mat(kTexMetal, kBrass, 4.0f));
	}
	// Écu rond bombé
	mb.setTransform(multiply(saved, poseMatrix({0, 0, 0.05f}, quatAxisAngle({1, 0, 0}, rad(90.0f)))));
	const ProfilePoint dome[] = {{0.34f, 0.0f}, {0.34f, 0.02f}, {0.31f, 0.035f}, {0.20f, 0.06f}, {0.09f, 0.075f}, {0.0f, 0.08f}};
	mb.lathe({0, 0, 0}, dome, 2, 20, mat(kTexMetal, kBrass, 4.0f));
	mb.lathe({0, 0, 0}, dome + 1, 4, 20, mat(kTexLeather, tint, 3.0f));
	const ProfilePoint boss[] = {{0.09f, 0.073f}, {0.075f, 0.10f}, {0.04f, 0.12f}, {0.0f, 0.125f}};
	mb.lathe({0, 0, 0}, boss, 4, 12, mat(kTexMetal, kBrass, 4.0f, 0.05f));
	mb.setTransform(saved);
}

// Occlusion ambiante simple pour l'enveloppe de la pièce : plus sombre dans les angles.
void shellOcclusion(MeshBuilder &mb, size_t from, size_t to) {
	for (size_t i = from; i < to; i++) {
		Vertex &v = mb.verts[i];
		float x = v.pos[0], y = v.pos[1], z = v.pos[2];
		float dWallX = X - fabsf(x), dWallZ = std::min(z - ZF, ZB - z);
		float k = 1.0f;
		bool isFloor = v.normal[1] > 0.9f, isCeil = v.normal[1] < -0.9f;
		if (isFloor || isCeil) {
			k *= 0.62f + 0.38f * smoothstepf(0.0f, 0.7f, std::min(dWallX, dWallZ));
		} else {
			k *= 0.60f + 0.40f * smoothstepf(0.0f, 0.5f, y);
			k *= 0.72f + 0.28f * smoothstepf(0.0f, 0.6f, H - y);
			float dCorner = fabsf(v.normal[0]) > 0.5f ? dWallZ : dWallX;
			k *= 0.70f + 0.30f * smoothstepf(0.0f, 0.6f, dCorner);
		}
		v.ao = (uint8_t)((float)v.ao * k);
	}
}

} // namespace

void buildRoom(MeshBuilder &mb, MeshBuilder &alpha, RoomInfo &info, int theme, const RoomOptions &opt) {
	const ThemeInfo &th = themeInfo(theme);
	Rng rng{12345u};
	info = RoomInfo();
	mb.verts.clear();
	alpha.verts.clear();
	mb.resetTransform();
	alpha.resetTransform();

	Material stone = mat(kTexStone, {1, 1, 1}, 0.5f);
	Material panel = mat(kTexPanel, {1, 1, 1}, 1.0f / 0.9f);
	Material floorM = mat(kTexFloor, {1, 1, 1}, 0.5f);
	Material wood = mat(kTexWood, {1, 1, 1}, 1.0f);
	const float cell = 0.25f;
	const float LZ = ZB - ZF;

	// --- Fenêtre (mur de gauche) ---
	const float wzc = -0.6f, whw = 0.65f, wys = 1.08f, wyr = 2.25f, wya = 2.95f;
	const float wz0 = wzc - whw, wz1 = wzc + whw, reveal = 0.42f;
	info.windowCenter = {-X, (wys + wya) * 0.5f, wzc};
	info.windowWidth = whw * 2.0f;
	info.windowHeight = wya - wys;

	size_t shellStart = mb.verts.size();
	// Sol et plafond
	mb.grid({-X, 0, ZB}, {2 * X, 0, 0}, {0, 0, -LZ}, floorM, cell);
	mb.grid({-X, H, ZF}, {2 * X, 0, 0}, {0, 0, LZ}, mat(kTexFloor, {0.42f, 0.38f, 0.36f}, 0.5f), 0.4f);
	// Mur du fond de la pièce (derrière l'écran) et mur arrière
	mb.grid({-X, 0, ZF}, {2 * X, 0, 0}, {0, kPanelH, 0}, panel, cell);
	mb.grid({-X, kPanelH, ZF}, {2 * X, 0, 0}, {0, H - kPanelH, 0}, stone, cell, 0, kPanelH);
	mb.grid({X, 0, ZB}, {-2 * X, 0, 0}, {0, kPanelH, 0}, panel, cell);
	mb.grid({X, kPanelH, ZB}, {-2 * X, 0, 0}, {0, H - kPanelH, 0}, stone, cell, 0.37f, kPanelH);
	// Mur de gauche, percé de la fenêtre
	mb.grid({-X, 0, ZB}, {0, 0, -LZ}, {0, kPanelH, 0}, panel, cell);
	mb.grid({-X, kPanelH, ZB}, {0, 0, -(ZB - wz1)}, {0, H - kPanelH, 0}, stone, cell, 0, kPanelH);
	mb.grid({-X, kPanelH, wz0}, {0, 0, -(wz0 - ZF)}, {0, H - kPanelH, 0}, stone, cell, ZB - wz0, kPanelH);
	mb.grid({-X, kPanelH, wz1}, {0, 0, -(wz1 - wz0)}, {0, wys - kPanelH, 0}, stone, cell, ZB - wz1, kPanelH);
	mb.grid({-X, wya, wz1}, {0, 0, -(wz1 - wz0)}, {0, H - wya, 0}, stone, cell, ZB - wz1, wya);
	// Mur de droite, de part et d'autre de la cheminée
	const float fz0 = -1.65f, fz1 = 0.45f, fx = X - 0.45f;
	mb.grid({X, 0, ZF}, {0, 0, fz0 - ZF}, {0, kPanelH, 0}, panel, cell);
	mb.grid({X, kPanelH, ZF}, {0, 0, fz0 - ZF}, {0, H - kPanelH, 0}, stone, cell, 0.21f, kPanelH);
	mb.grid({X, 0, fz1}, {0, 0, ZB - fz1}, {0, kPanelH, 0}, panel, cell);
	mb.grid({X, kPanelH, fz1}, {0, 0, ZB - fz1}, {0, H - kPanelH, 0}, stone, cell, 0.63f, kPanelH);
	shellOcclusion(mb, shellStart, mb.verts.size());

	// Arc brisé de la fenêtre : remplissage du mur au-dessus de la courbe, et intrados
	{
		const int n = 10;
		const float ang = rad(64.0f);
		Material rev = mat(kTexStone, {0.80f, 0.78f, 0.76f}, 0.5f);
		auto wallVertex = [&](float y, float z) { mb.vertex({-X, y, z}, {1, 0, 0}, (ZB - z) * 0.5f, y * 0.5f, stone); };
		for (int side = 0; side < 2; side++) {
			for (int i = 0; i < n; i++) {
				float t0 = (float)i / n, t1 = (float)(i + 1) / n;
				float f0 = (1.0f - cosf(t0 * ang)) / (1.0f - cosf(ang)), f1 = (1.0f - cosf(t1 * ang)) / (1.0f - cosf(ang));
				float y0 = wyr + (wya - wyr) * sinf(t0 * ang) / sinf(ang), y1 = wyr + (wya - wyr) * sinf(t1 * ang) / sinf(ang);
				float za = side == 0 ? wz0 + whw * f0 : wz1 - whw * f0;
				float zb = side == 0 ? wz0 + whw * f1 : wz1 - whw * f1;
				if (side == 0) {
					wallVertex(y1, zb); wallVertex(y0, za); wallVertex(wya, za);
					wallVertex(y1, zb); wallVertex(wya, za); wallVertex(wya, zb);
					mb.quad({-X - reveal, y0, za}, {-X, y0, za}, {-X, y1, zb}, {-X - reveal, y1, zb}, rev);
				} else {
					wallVertex(y0, za); wallVertex(y1, zb); wallVertex(wya, zb);
					wallVertex(y0, za); wallVertex(wya, zb); wallVertex(wya, za);
					mb.quad({-X, y0, za}, {-X - reveal, y0, za}, {-X - reveal, y1, zb}, {-X, y1, zb}, rev);
				}
			}
		}
		// Appui et piédroits
		mb.quad({-X - reveal, wys, wz1}, {-X, wys, wz1}, {-X, wys, wz0}, {-X - reveal, wys, wz0}, rev);
		mb.quad({-X - reveal, wys, wz0}, {-X, wys, wz0}, {-X, wyr, wz0}, {-X - reveal, wyr, wz0}, rev);
		mb.quad({-X, wys, wz1}, {-X - reveal, wys, wz1}, {-X - reveal, wyr, wz1}, {-X, wyr, wz1}, rev);
		mb.box({-X + 0.07f, wys - 0.045f, wzc}, {0.11f, 0.045f, whw + 0.12f}, mat(kTexStone, {1.08f, 1.05f, 1.0f}, 0.5f));
		for (float z : {wz0 - 0.08f, wz1 + 0.08f})
			mb.box({-X + 0.02f, (wys + wyr) * 0.5f, z}, {0.035f, (wyr - wys) * 0.5f, 0.08f}, mat(kTexStone, {1.08f, 1.05f, 1.0f}, 0.5f));
		// Vue extérieure
		if (opt.windowImage) {
			// L'image du joueur, assez proche pour rester nette ; on la centre pour que son
			// point d'intérêt (aux deux tiers à droite, un peu au-dessus du milieu) soit visible du fauteuil.
			const float sx = -X - 0.9f, iw = 4.0f, ih = iw / opt.windowAspect;
			const float zc = 0.0f, yc = 2.25f - (0.5f - 0.38f) * ih;
			imageQuad(mb, {sx, yc - ih * 0.5f, zc + iw * 0.5f}, {sx, yc - ih * 0.5f, zc - iw * 0.5f}, {sx, yc + ih * 0.5f, zc - iw * 0.5f},
			          {sx, yc + ih * 0.5f, zc + iw * 0.5f}, kTexImgWindowA, 1.0f);
			// Au-delà des bords : ciel sombre en haut, terre sombre en bas
			mb.quad({sx - 0.02f, yc + ih * 0.5f - 0.02f, zc + 6}, {sx - 0.02f, yc + ih * 0.5f - 0.02f, zc - 6}, {sx - 0.02f, 9, zc - 6}, {sx - 0.02f, 9, zc + 6},
			        mat(kTexWhite, {0.24f, 0.20f, 0.36f}, 1.0f, 1.0f));
			mb.quad({sx - 0.04f, -6, zc + 6}, {sx - 0.04f, -6, zc - 6}, {sx - 0.04f, yc + ih * 0.5f, zc - 6}, {sx - 0.04f, yc + ih * 0.5f, zc + 6},
			        mat(kTexWhite, {0.05f, 0.09f, 0.05f}, 1.0f, 1.0f));
		} else {
			const float sx = -X - 1.9f, half = 2.6f, ymid = 2.0f;
			mb.quad({sx, ymid - half, wzc + half}, {sx, ymid - half, wzc - half}, {sx, ymid + half, wzc - half}, {sx, ymid + half, wzc + half},
			        mat(kTexSky, {1, 1, 1}, 1.0f, 1.0f));
			mb.quad({sx - 0.1f, -6, wzc + 8}, {sx - 0.1f, -6, wzc - 8}, {sx - 0.1f, 10, wzc - 8}, {sx - 0.1f, 10, wzc + 8},
			        mat(kTexWhite, {0.01f, 0.01f, 0.015f}, 1.0f, 1.0f));
		}
		// Vitrail
		const float gx = -X - reveal * 0.55f, s = 1.4f;
		Material glass = mat(kTexLattice, {0.55f, 0.60f, 0.66f}, 1.0f, 1.0f);
		alpha.quad({gx, wys, wz1}, {gx, wys, wz0}, {gx, wya, wz0}, {gx, wya, wz1}, glass, wz1 * s, wys * s, wz0 * s, wya * s);
		// Meneau central et traverse
		Material lead = mat(kTexMetal, {0.10f, 0.10f, 0.11f}, 4.0f);
		mb.box({gx, (wys + wya) * 0.5f, wzc}, {0.02f, (wya - wys) * 0.5f, 0.022f}, lead);
		mb.box({gx, wyr, wzc}, {0.02f, 0.022f, whw}, lead);
	}

	// --- Lambris : cimaise en relief le long des murs ---
	Material rail = mat(kTexWood, {1.05f, 1.0f, 0.95f}, 1.0f);
	mb.box({0, kPanelH, ZF + 0.02f}, {X, 0.03f, 0.025f}, rail);
	mb.box({0, kPanelH, ZB - 0.02f}, {X, 0.03f, 0.025f}, rail);
	mb.box({-X + 0.02f, kPanelH, (ZF + ZB) * 0.5f}, {0.025f, 0.03f, LZ * 0.5f}, rail);
	mb.box({X - 0.02f, kPanelH, (ZF + fz0) * 0.5f}, {0.025f, 0.03f, (fz0 - ZF) * 0.5f}, rail);
	mb.box({X - 0.02f, kPanelH, (fz1 + ZB) * 0.5f}, {0.025f, 0.03f, (ZB - fz1) * 0.5f}, rail);

	// --- Poutres du plafond ---
	for (float z : {-2.95f, -1.70f, -0.45f, 0.80f, 2.05f})
		mb.box({0, H - 0.11f, z}, {X, 0.11f, 0.085f}, mat(kTexWood, {0.75f, 0.72f, 0.70f}, 1.0f), 63 & ~8);
	for (float x : {-1.5f, 1.5f})
		mb.box({x, H - 0.26f, (ZF + ZB) * 0.5f}, {0.10f, 0.05f, LZ * 0.5f}, mat(kTexWood, {0.70f, 0.68f, 0.66f}, 1.0f));

	// --- Cheminée (mur de droite) ---
	{
		const float oz0 = -1.15f, oz1 = -0.05f, oy = 1.05f, depth = 0.42f;
		size_t start = mb.verts.size();
		mb.grid({fx, 0, fz0}, {0, 0, oz0 - fz0}, {0, H, 0}, stone, cell, 0.1f, 0);
		mb.grid({fx, 0, oz1}, {0, 0, fz1 - oz1}, {0, H, 0}, stone, cell, 0.4f, 0);
		mb.grid({fx, oy, oz0}, {0, 0, oz1 - oz0}, {0, H - oy, 0}, stone, cell, 0.6f, oy);
		mb.grid({X, 0, fz0}, {-(X - fx), 0, 0}, {0, H, 0}, stone, cell);
		mb.grid({fx, 0, fz1}, {X - fx, 0, 0}, {0, H, 0}, stone, cell);
		shellOcclusion(mb, start, mb.verts.size());
		Material soot = mat(kTexStone, {0.30f, 0.27f, 0.25f}, 0.8f);
		mb.grid({fx + depth, 0, oz0}, {0, 0, oz1 - oz0}, {0, oy, 0}, soot, 0.2f);
		mb.grid({fx, 0, oz0}, {depth, 0, 0}, {0, oy, 0}, soot, 0.2f);
		mb.grid({fx + depth, 0, oz1}, {-depth, 0, 0}, {0, oy, 0}, soot, 0.2f);
		mb.grid({fx, oy, oz0}, {depth, 0, 0}, {0, 0, oz1 - oz0}, soot, 0.2f);
		// Âtre, linteau, manteau
		mb.box({fx + 0.035f, 0.03f, -0.6f}, {0.385f, 0.03f, 0.78f}, mat(kTexStone, {0.85f, 0.82f, 0.8f}, 0.8f));
		mb.box({fx - 0.015f, oy + 0.09f, -0.6f}, {0.02f, 0.09f, 0.66f}, mat(kTexStone, {1.12f, 1.08f, 1.02f}, 0.8f));
		mb.box({fx - 0.10f, 1.32f, -0.6f}, {0.17f, 0.045f, 1.16f}, wood);
		for (float z : {fz0 + 0.22f, fz1 - 0.22f})
			mb.box({fx - 0.06f, 1.21f, z}, {0.07f, 0.07f, 0.06f}, wood);
		// Bûches et braises
		Material log = mat(kTexWood, {0.45f, 0.38f, 0.34f}, 3.0f);
		mb.rod({fx + 0.17f, 0.115f, -0.92f}, {fx + 0.19f, 0.115f, -0.30f}, 0.055f, 0.05f, 8, log);
		mb.rod({fx + 0.29f, 0.115f, -0.88f}, {fx + 0.27f, 0.115f, -0.26f}, 0.05f, 0.055f, 8, log);
		mb.rod({fx + 0.22f, 0.205f, -0.95f}, {fx + 0.25f, 0.215f, -0.36f}, 0.045f, 0.045f, 8, log);
		mb.box({fx + 0.23f, 0.066f, -0.6f}, {0.11f, 0.008f, 0.26f}, mat(kTexWhite, {0.95f, 0.30f, 0.05f}, 1.0f, 0.85f));
		info.firePos = {fx + 0.22f, 0.16f, -0.6f};
		// Sur le manteau
		candlestick(mb, info, {fx - 0.12f, 1.365f, fz0 + 0.20f}, 1.1f);
		candlestick(mb, info, {fx - 0.12f, 1.365f, fz1 - 0.20f}, 1.1f);
		mb.setTransform(place({fx - 0.10f, 1.365f, -1.0f}, -90.0f));
		float bx = -0.1f;
		for (int i = 0; i < 4; i++) {
			float t = rng.range(0.03f, 0.05f);
			bookUpright(mb, rng, bx, bx + t, 0.0f, rng.range(0.18f, 0.24f), -0.08f, 0.08f);
			bx += t + 0.002f;
		}
		mb.resetTransform();
		potion(mb, info, {fx - 0.12f, 1.365f, -0.15f}, {0.70f, 0.30f, 0.80f}, 1.2f);
		rubyOnStand(mb, info, {fx - 0.12f, 1.365f, -0.6f});
		// Au-dessus : le portrait fourni par le joueur, sinon un tableau du château
		mb.setTransform(place({fx - 0.012f, 2.27f, -0.6f}, -90.0f));
		if (opt.portraitImage) {
			float pw = 1.25f, ph = pw / opt.portraitAspect;
			if (ph > 1.05f) {
				ph = 1.05f;
				pw = ph * opt.portraitAspect;
			}
			paintingImage(mb, pw, ph, kTexImgPortraitA);
		} else {
			painting(mb, 1.15f, 0.86f, kTexPaintCastle);
		}
		mb.resetTransform();
	}

	// --- Bibliothèques ---
	bookcase(mb, info, rng, place({-X + 0.20f, 0, -2.42f}, 90.0f), 1.72f, 2.70f, 0.38f, 6);
	bookcase(mb, info, rng, place({X - 0.20f, 0, -2.60f}, -90.0f), 1.50f, 2.70f, 0.38f, 6);
	bookcase(mb, info, rng, place({X - 0.20f, 0, 1.60f}, -90.0f), 1.70f, 2.70f, 0.38f, 6);
	bookcase(mb, info, rng, place({1.72f, 0, ZB - 0.20f}, 180.0f), 1.55f, 2.70f, 0.38f, 6);
	// Échelle appuyée contre la bibliothèque de gauche
	mb.setTransform(multiply(place({-X + 0.42f, 0, -1.95f}, 90.0f), poseMatrix({0, 0, 0.46f}, quatAxisAngle({1, 0, 0}, rad(-11.0f)))));
	ladder(mb);
	mb.resetTransform();

	// --- Mur derrière l'écran : console, bannières, appliques, écu ---
	{
		Material body = mat(kTexPanel, {1, 1, 1}, 1.0f / 0.77f);
		const float cw = 2.3f, ch = 0.77f, cd = 0.42f;
		float cz = ZF + cd * 0.5f + 0.02f;
		mb.grid({-cw * 0.5f, 0, cz + cd * 0.5f}, {cw, 0, 0}, {0, ch, 0}, mat(kTexPanel, {1, 1, 1}, 1.0f / 0.77f), 0.4f);
		mb.box({0, ch * 0.5f, cz}, {cw * 0.5f, ch * 0.5f, cd * 0.5f}, body, 1 | 2, 0.6f);
		mb.box({0, ch + 0.02f, cz}, {cw * 0.5f + 0.04f, 0.02f, cd * 0.5f + 0.03f}, wood);
		info.shadows.push_back({{0, 0.012f, cz + 0.1f}, cw * 0.56f, cd * 0.9f, 0.6f});
		candlestick(mb, info, {-0.95f, ch + 0.04f, cz}, 1.3f);
		candlestick(mb, info, {0.95f, ch + 0.04f, cz}, 1.3f);
		float yy = ch + 0.04f;
		for (int k = 0; k < 3; k++) {
			float t = rng.range(0.04f, 0.06f);
			bookFlat(mb, rng, {0.45f, yy, cz}, 0.26f, 0.19f, t, rng.range(-20.0f, 20.0f));
			yy += t;
		}
		const ProfilePoint vase[] = {{0.0f, 0.0f}, {0.06f, 0.006f}, {0.10f, 0.10f}, {0.085f, 0.20f}, {0.04f, 0.27f}, {0.055f, 0.30f}, {0.0f, 0.30f}};
		mb.lathe({-0.40f, ch + 0.04f, cz}, vase, 7, 14, mat(kTexMetal, kBrass, 4.0f));
		for (float sx : {-1.0f, 1.0f}) {
			mb.setTransform(place({sx * 2.12f, 2.95f, ZF + 0.01f}, 0.0f));
			banner(mb, 0.58f, 1.75f, {th.banner[0], th.banner[1], th.banner[2]});
			mb.setTransform(place({sx * 1.62f, 2.05f, ZF}, 0.0f));
			sconce(mb, info);
		}
		mb.setTransform(place({0, 2.72f, ZF + 0.01f}, 0.0f));
		shieldAndSwords(mb, {th.banner[0], th.banner[1], th.banner[2]});
		// De part et d'autre de l'écran : deux reliques sur leur socle
		mb.setTransform(place({2.05f, 0, -2.62f}, -24.0f));
		pedestal(mb, info, 1.05f);
		darkMask(mb, info, {0, 1.05f, 0});
		mb.setTransform(place({-2.05f, 0, -2.62f}, 24.0f));
		pedestal(mb, info, 1.05f);
		runeCube(mb, info, {0, 1.05f + 0.20f, 0});
		mb.resetTransform();
	}

	// --- Mur de gauche, partie arrière : bannière, carte encadrée, globe ---
	mb.setTransform(place({-X + 0.012f, 2.02f, 0.78f}, 90.0f));
	if (opt.paintingImage[2]) {
		float pw = 0.90f, ph = pw / opt.paintingAspect[2];
		if (ph > 0.80f) { ph = 0.80f; pw = ph * opt.paintingAspect[2]; }
		paintingImage(mb, pw, ph, kTexImgPainting3A);
	} else {
		painting(mb, 0.84f, 0.63f, kTexPaintTheme);
	}
	mb.setTransform(place({-X + 0.012f, 1.85f, 2.07f}, 90.0f));
	if (opt.paintingImage[0]) {
		float pw = 0.95f, ph = pw / opt.paintingAspect[0];
		if (ph > 1.0f) { ph = 1.0f; pw = ph * opt.paintingAspect[0]; }
		paintingImage(mb, pw, ph, kTexImgPainting1A);
	} else {
		painting(mb, 0.92f, 0.92f, kTexPaintMap);
	}
	mb.setTransform(place({-1.95f, 0, 0.95f}, 30.0f));
	globe(mb, info);
	// Coffre sous la fenêtre
	mb.setTransform(place({-X + 0.34f, 0, wzc}, 90.0f));
	chest(mb, info);
	emeraldBlade(mb, info, {0, 0.52f, 0});
	mb.resetTransform();

	// --- Mur arrière : porte, tableau, appliques ---
	mb.setTransform(place({-0.15f, 0, ZB}, 180.0f));
	door(mb);
	mb.setTransform(place({-1.72f, 1.90f, ZB - 0.012f}, 180.0f));
	if (opt.paintingImage[1]) {
		float pw = 1.10f, ph = pw / opt.paintingAspect[1];
		if (ph > 1.0f) { ph = 1.0f; pw = ph * opt.paintingAspect[1]; }
		paintingImage(mb, pw, ph, kTexImgPainting2A);
	} else {
		painting(mb, 1.10f, 0.80f, kTexPaintCastle);
	}
	// Un cadre de plus à droite de la porte, seulement si le joueur fournit l'image
	if (opt.paintingImage[3]) {
		mb.setTransform(place({1.62f, 1.90f, ZB - 0.012f}, 180.0f));
		float pw = 1.10f, ph = pw / opt.paintingAspect[3];
		if (ph > 1.0f) { ph = 1.0f; pw = ph * opt.paintingAspect[3]; }
		paintingImage(mb, pw, ph, kTexImgPainting4A);
	}
	for (float x : {-0.98f, 0.68f}) {
		mb.setTransform(place({x, 2.05f, ZB}, 180.0f));
		sconce(mb, info);
	}
	mb.resetTransform();

	// --- Tapis, lustre (le milieu de la pièce reste libre) ---
	{
		const float rw = 3.3f, rd = 2.25f, rz = -0.35f;
		Material rug = mat(kTexRug, {1, 1, 1});
		const int nx = 8, nz = 6;
		for (int j = 0; j < nz; j++)
			for (int i = 0; i < nx; i++) {
				float u0 = (float)i / nx, u1 = (float)(i + 1) / nx, v0 = (float)j / nz, v1 = (float)(j + 1) / nz;
				mb.quad({-rw * 0.5f + rw * u0, 0.006f, rz + rd * 0.5f - rd * v0}, {-rw * 0.5f + rw * u1, 0.006f, rz + rd * 0.5f - rd * v0},
				        {-rw * 0.5f + rw * u1, 0.006f, rz + rd * 0.5f - rd * v1}, {-rw * 0.5f + rw * u0, 0.006f, rz + rd * 0.5f - rd * v1}, rug, u0, v0, u1, v1);
			}
	}
	// Le lustre est au-dessus et en arrière du fauteuil : jamais entre le regard et l'écran.
	chandelier(mb, info, {0, 2.62f, 0.75f});
	// Quelques livres empilés par terre
	{
		float yy = 0.0f;
		for (int k = 0; k < 4; k++) {
			float t = rng.range(0.04f, 0.06f);
			bookFlat(mb, rng, {-2.15f, yy, -1.45f}, 0.27f, 0.20f, t, rng.range(-30.0f, 30.0f));
			yy += t;
		}
		info.shadows.push_back({{-2.15f, 0.012f, -1.45f}, 0.24f, 0.22f, 0.45f});
		// Dessus, le crâne vert ; à côté, une lanterne posée par terre.
		greenSkull(mb, info, {-2.15f, yy, -1.45f}, 35.0f);
		lantern(mb, info, {-1.80f, 0.0f, -1.62f});
	}

	// --- Lumières ---
	Vec3 warm = {1.0f, 0.60f, 0.26f};
	Vec3 fire = {1.0f * th.fireTint[0], 0.46f * th.fireTint[1], 0.15f * th.fireTint[2]};
	info.lights.push_back({{fx - 0.55f, 0.60f, -0.6f}, fire * 2.4f, 1.7f, 1.0f, kLightFire});
	info.lights.push_back({{0, 2.45f, 0.75f}, warm * 0.85f, 1.8f, 0.10f, kLightWarm});
	info.lights.push_back({{-1.80f, 0.34f, -1.62f}, warm * 0.40f, 0.7f, 0.25f, kLightWarm});
	info.lights.push_back({{-X + 0.55f, 1.85f, wzc}, Vec3{th.windowLight[0], th.windowLight[1], th.windowLight[2]} * 2.6f, 1.9f, 0.0f, kLightWindow});
	info.lights.push_back({{-1.62f, 2.05f, ZF + 0.35f}, warm * 0.45f, 0.9f, 0.15f, kLightWarm});
	info.lights.push_back({{1.62f, 2.05f, ZF + 0.35f}, warm * 0.45f, 0.9f, 0.15f, kLightWarm});
	info.lights.push_back({{-0.15f, 2.05f, ZB - 0.45f}, warm * 0.60f, 1.1f, 0.5f, kLightWarm});
}

} // namespace lolvr
