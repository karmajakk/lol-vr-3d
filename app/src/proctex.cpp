// lol-vr — textures procédurales du décor.
// Convention : la ligne 0 de l'image correspond à v = 0 (le bas une fois plaquée sur un mur).

#include "proctex.h"

#include <math.h>
#include <string.h>
#include <strings.h>

#include <algorithm>
#include <vector>

namespace lolvr {

namespace {

const int N = kTexSize;
const float kPi = 3.14159265f;

struct Col {
	float r, g, b;
};
Col operator+(Col a, Col b) { return {a.r + b.r, a.g + b.g, a.b + b.b}; }
Col operator*(Col a, float s) { return {a.r * s, a.g * s, a.b * s}; }
Col mixc(Col a, Col b, float t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }

struct Px {
	float r, g, b, a;
};

float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
float sstep(float e0, float e1, float x) {
	float t = clamp01((x - e0) / (e1 - e0));
	return t * t * (3.0f - 2.0f * t);
}
float fracf(float x) { return x - floorf(x); }

uint32_t hashU(uint32_t x) {
	x ^= x >> 16;
	x *= 0x7feb352dU;
	x ^= x >> 15;
	x *= 0x846ca68bU;
	x ^= x >> 16;
	return x;
}
float hash2(int x, int y, uint32_t seed) {
	uint32_t h = hashU((uint32_t)x * 0x9E3779B1U + hashU((uint32_t)y * 0x85EBCA77U + seed * 0xC2B2AE3DU + 0x165667B1U));
	return (float)(h & 0xFFFFFF) / 16777216.0f;
}
float hash1(int x, uint32_t seed) { return hash2(x, 7919, seed); }

int wrapi(int a, int p) {
	a %= p;
	return a < 0 ? a + p : a;
}

// Bruit de valeur périodique (période px, py en cellules).
float vnoise(float x, float y, int px, int py, uint32_t seed) {
	float fx = floorf(x), fy = floorf(y);
	int ix = (int)fx, iy = (int)fy;
	float tx = x - fx, ty = y - fy;
	tx = tx * tx * (3.0f - 2.0f * tx);
	ty = ty * ty * (3.0f - 2.0f * ty);
	int x0 = wrapi(ix, px), x1 = wrapi(ix + 1, px), y0 = wrapi(iy, py), y1 = wrapi(iy + 1, py);
	float a = hash2(x0, y0, seed), b = hash2(x1, y0, seed), c = hash2(x0, y1, seed), d = hash2(x1, y1, seed);
	return a + (b - a) * tx + (c - a) * ty + (a - b - c + d) * tx * ty;
}

// Bruit fractal périodique ; u, v dans [0,1], fx/fy = cellules par tuile à la première octave.
float fbm(float u, float v, int fx, int fy, int octaves, uint32_t seed) {
	float s = 0, amp = 0.5f, norm = 0;
	for (int o = 0; o < octaves; o++) {
		s += amp * vnoise(u * (float)fx, v * (float)fy, fx, fy, seed + (uint32_t)o * 131U);
		norm += amp;
		fx *= 2;
		fy *= 2;
		amp *= 0.5f;
	}
	return s / norm;
}

void blendPx(Px *px, int x, int y, Col c, float t) {
	if (x < 0 || y < 0 || x >= N || y >= N || t <= 0)
		return;
	Px &p = px[y * N + x];
	t = clamp01(t);
	p.r += (c.r - p.r) * t;
	p.g += (c.g - p.g) * t;
	p.b += (c.b - p.b) * t;
}

void addPx(Px *px, int x, int y, Col c, float t) {
	if (x < 0 || y < 0 || x >= N || y >= N)
		return;
	Px &p = px[y * N + x];
	p.r += c.r * t;
	p.g += c.g * t;
	p.b += c.b * t;
}

// Trait à bords doux (coordonnées en pixels).
void drawLine(Px *px, float x0, float y0, float x1, float y1, float width, Col c, float opacity = 1.0f) {
	float minx = std::min(x0, x1) - width - 1, maxx = std::max(x0, x1) + width + 1;
	float miny = std::min(y0, y1) - width - 1, maxy = std::max(y0, y1) + width + 1;
	float dx = x1 - x0, dy = y1 - y0;
	float len2 = dx * dx + dy * dy;
	for (int y = (int)miny; y <= (int)maxy; y++)
		for (int x = (int)minx; x <= (int)maxx; x++) {
			float t = len2 > 1e-6f ? (((float)x - x0) * dx + ((float)y - y0) * dy) / len2 : 0.0f;
			t = clamp01(t);
			float ex = (float)x - (x0 + dx * t), ey = (float)y - (y0 + dy * t);
			float d = sqrtf(ex * ex + ey * ey);
			blendPx(px, x, y, c, opacity * (1.0f - sstep(width * 0.5f - 0.6f, width * 0.5f + 0.6f, d)));
		}
}

void fillRect(Px *px, float x0, float y0, float x1, float y1, Col c, float opacity = 1.0f) {
	for (int y = (int)floorf(y0); y < (int)ceilf(y1); y++)
		for (int x = (int)floorf(x0); x < (int)ceilf(x1); x++)
			blendPx(px, x, y, c, opacity);
}

void glowDisc(Px *px, float cx, float cy, float radius, Col c, float amount) {
	int r = (int)(radius * 3.0f) + 1;
	for (int y = (int)cy - r; y <= (int)cy + r; y++)
		for (int x = (int)cx - r; x <= (int)cx + r; x++) {
			float d = sqrtf(((float)x - cx) * ((float)x - cx) + ((float)y - cy) * ((float)y - cy));
			addPx(px, x, y, c, amount * expf(-d * d / (radius * radius)));
		}
}

// ---------------------------------------------------------------------------
// Matériaux

void genStone(Px *px) {
	const int rows = 6, cols = 3;
	const Col base{0.50f, 0.47f, 0.42f}, mortar{0.17f, 0.155f, 0.14f};
	for (int y = 0; y < N; y++)
		for (int x = 0; x < N; x++) {
			float u = ((float)x + 0.5f) / N, v = ((float)y + 0.5f) / N;
			int row = (int)(v * rows);
			float fv = v * rows - (float)row;
			float su = u * cols + ((row & 1) ? 0.5f : 0.0f) + 0.13f * (float)row;
			int col = wrapi((int)floorf(su), cols);
			float fu = fracf(su);
			float eu = std::min(fu, 1.0f - fu) * ((float)N / cols);
			float ev = std::min(fv, 1.0f - fv) * ((float)N / rows);
			// Bords légèrement irréguliers
			float wob = (fbm(u, v, 24, 24, 2, 11) - 0.5f) * 5.0f;
			float e = std::min(eu, ev) + wob;
			float m = sstep(2.0f, 6.5f, e);
			float tone = 0.80f + 0.34f * hash2(col, row, 3);
			float warm = hash2(col, row, 4) - 0.5f;
			float grain = fbm(u, v, 48, 48, 4, 5) - 0.5f;
			float stain = fbm(u, v, 5, 5, 3, 6) - 0.5f;
			Col c = base * (tone + grain * 0.30f + stain * 0.22f);
			c.r *= 1.0f + warm * 0.10f;
			c.b *= 1.0f - warm * 0.12f;
			// Petits trous
			float pit = fbm(u, v, 96, 96, 2, 7);
			if (pit > 0.72f)
				c = c * (1.0f - (pit - 0.72f) * 1.8f);
			// Chanfrein : arête haute éclairée, arête basse dans l'ombre
			float top = sstep(12.0f, 3.0f, (1.0f - fv) * ((float)N / rows));
			float bot = sstep(12.0f, 3.0f, fv * ((float)N / rows));
			c = c * (1.0f + 0.22f * top - 0.25f * bot);
			c = mixc(mortar * (0.85f + 0.3f * (grain + 0.5f)), c, m);
			px[y * N + x] = {c.r, c.g, c.b, 1.0f};
		}
}

void genFloor(Px *px) {
	const int planks = 8;
	const Col base{0.42f, 0.28f, 0.165f};
	for (int y = 0; y < N; y++)
		for (int x = 0; x < N; x++) {
			float u = ((float)x + 0.5f) / N, v = ((float)y + 0.5f) / N;
			int p = (int)(u * planks);
			float fu = u * planks - (float)p;
			float off = hash1(p, 21);
			float sv = (v + off) * 2.0f;
			int board = wrapi((int)floorf(sv), 2);
			float fv = fracf(sv);
			float tone = 0.74f + 0.42f * hash2(p, board, 22);
			float grain = fbm(u, v, 96, 3, 4, 23 + (uint32_t)p) - 0.5f;
			float rings = sinf((fbm(u, v, 6, 2, 3, 24) * 22.0f + fu * 3.0f)) * 0.5f;
			Col c = base * (tone + grain * 0.45f + rings * 0.07f);
			c.r *= 1.0f + 0.10f * (hash2(p, board, 25) - 0.5f);
			float gap = std::min(fu, 1.0f - fu) * ((float)N / planks);
			float joint = std::min(fv, 1.0f - fv) * ((float)N / 2.0f);
			float line = sstep(0.4f, 2.2f, std::min(gap, joint));
			c = c * (0.18f + 0.82f * line);
			// Usure douce au centre des planches
			c = c * (0.93f + 0.07f * sstep(0.0f, 10.0f, gap));
			// Clous près des joints
			float nx = fabsf(fu - 0.5f) * ((float)N / planks);
			if (joint > 5.0f && joint < 9.0f && fabsf(nx - 14.0f) < 1.8f)
				c = c * 0.35f;
			px[y * N + x] = {c.r, c.g, c.b, 1.0f};
		}
}

void genWood(Px *px) {
	const Col base{0.33f, 0.205f, 0.115f};
	for (int y = 0; y < N; y++)
		for (int x = 0; x < N; x++) {
			float u = ((float)x + 0.5f) / N, v = ((float)y + 0.5f) / N;
			float grain = fbm(u, v, 4, 96, 4, 31) - 0.5f;
			float wave = fbm(u, v, 3, 6, 3, 32);
			float rings = sinf(wave * 40.0f) * 0.5f;
			Col c = base * (0.95f + grain * 0.55f + rings * 0.10f);
			float knot = fbm(u, v, 3, 3, 2, 33);
			if (knot > 0.74f)
				c = c * (1.0f - (knot - 0.74f) * 2.0f);
			px[y * N + x] = {c.r, c.g, c.b, 1.0f};
		}
}

void genPanel(Px *px) {
	const Col base{0.30f, 0.185f, 0.105f};
	for (int y = 0; y < N; y++)
		for (int x = 0; x < N; x++) {
			float u = ((float)x + 0.5f) / N, v = ((float)y + 0.5f) / N;
			float grain = fbm(u, v, 96, 4, 4, 41) - 0.5f;
			Col c = base * (0.95f + grain * 0.5f);
			// Caisson en retrait avec chanfrein (lumière venant d'en haut à gauche)
			float du = std::min(u, 1.0f - u), dv = std::min(v - 0.10f, 0.90f - v);
			float iu = (du - 0.10f) * (float)N, iv = (dv - 0.08f) * (float)N;
			float inset = std::min(iu, iv);
			if (inset > 0.0f && inset < 9.0f) {
				bool facesLight = (iu < iv) ? (u > 0.5f) : (v < 0.5f);
				c = c * (facesLight ? 1.35f : 0.55f);
			} else if (inset >= 9.0f) {
				c = c * 0.86f;
				float in2 = inset - 9.0f;
				if (in2 > 14.0f && in2 < 18.0f)
					c = c * 0.72f;
			}
			// Plinthe et cimaise
			if (v < 0.07f)
				c = c * (0.62f + 2.0f * v);
			if (v > 0.93f)
				c = c * (1.0f + 0.5f * sinf((v - 0.93f) / 0.07f * kPi));
			if (du * (float)N < 1.5f)
				c = c * 0.5f;
			px[y * N + x] = {c.r, c.g, c.b, 1.0f};
		}
}

// Quatre dos de livre côte à côte (un par quart de largeur), cuir neutre teinté par sommet.
void genBooks(Px *px) {
	const Col gold{0.86f, 0.66f, 0.26f};
	for (int y = 0; y < N; y++)
		for (int x = 0; x < N; x++) {
			float u = ((float)x + 0.5f) / N, v = ((float)y + 0.5f) / N;
			int k = (int)(u * 4.0f);
			float fu = u * 4.0f - (float)k;
			float round = 0.50f + 0.50f * sinf(kPi * (0.08f + 0.84f * fu));
			float grain = fbm(u, v, 64, 64, 3, 51) - 0.5f;
			float lum = (0.80f + grain * 0.25f) * round;
			Col c{lum, lum, lum};
			float alpha = 1.0f;
			// Nerfs (bandes en relief) et filets dorés
			static const float bands[4][4] = {
				{0.10f, 0.22f, 0.78f, 0.90f}, {0.08f, 0.30f, 0.52f, 0.92f}, {0.14f, 0.20f, 0.80f, 0.86f}, {0.06f, 0.50f, 0.56f, 0.94f}};
			for (int b = 0; b < 4; b++) {
				float d = fabsf(v - bands[k][b]) * (float)N;
				if (d < 3.5f) {
					c = mixc(c, gold * (0.6f + 0.6f * round), 1.0f);
					alpha = 0.0f;
				} else if (d < 6.0f) {
					c = c * 0.72f;
				}
			}
			// Étiquette de titre
			float l0 = (k == 1) ? 0.62f : 0.56f, l1 = l0 + ((k == 3) ? 0.26f : 0.16f);
			if (k != 2 && v > l0 && v < l1 && fu > 0.14f && fu < 0.86f) {
				c = Col{0.10f, 0.07f, 0.05f} * (0.7f + 0.6f * round);
				alpha = 0.25f;
				int line = (int)((v - l0) / (l1 - l0) * 5.0f);
				float lf = fracf((v - l0) / (l1 - l0) * 5.0f);
				float wlen = 0.25f + 0.5f * hash2(line, k, 52);
				if (lf > 0.35f && lf < 0.65f && fabsf(fu - 0.5f) < wlen * 0.5f) {
					c = gold * (0.7f + 0.5f * round);
					alpha = 0.0f;
				}
			}
			// Motif central du dos sans étiquette
			if (k == 2) {
				float dx = (fu - 0.5f) * 2.2f, dy = (v - 0.5f) * 9.0f;
				float d = fabsf(dx) + fabsf(dy);
				if (d < 0.55f && d > 0.36f) {
					c = gold * (0.7f + 0.5f * round);
					alpha = 0.0f;
				}
			}
			px[y * N + x] = {c.r, c.g, c.b, alpha};
		}
}

void genRug(Px *px) {
	const Col red{0.46f, 0.07f, 0.06f}, navy{0.07f, 0.10f, 0.25f}, gold{0.78f, 0.58f, 0.22f}, cream{0.80f, 0.72f, 0.55f};
	for (int y = 0; y < N; y++)
		for (int x = 0; x < N; x++) {
			float u = ((float)x + 0.5f) / N, v = ((float)y + 0.5f) / N;
			float a = fabsf(u - 0.5f) * 2.0f, b = fabsf(v - 0.5f) * 2.0f;
			float e = std::max(a, b);
			Col c = red;
			// Champ central : treillis de losanges
			float la = a * 9.0f, lb = b * 6.0f;
			float d = fabsf(fracf(la) - 0.5f) + fabsf(fracf(lb) - 0.5f);
			if (fabsf(d - 0.5f) < 0.045f)
				c = gold;
			else if (d < 0.14f)
				c = navy;
			else if (d < 0.20f)
				c = cream;
			// Médaillon central
			float rx = a * 1.5f, ry = b;
			float r = sqrtf(rx * rx + ry * ry);
			float ang = atan2f(ry, rx);
			float petal = 0.36f + 0.05f * cosf(ang * 8.0f);
			if (r < petal) {
				c = navy;
				if (r > petal - 0.03f)
					c = gold;
				else if (r < 0.20f + 0.03f * cosf(ang * 16.0f))
					c = (r < 0.07f) ? gold : red;
				else if (fabsf(r - 0.27f) < 0.012f)
					c = cream;
			}
			// Bordures
			if (e > 0.80f) {
				c = navy;
				float t = (e - 0.80f) / 0.16f;
				float along = (a > b) ? b : a;
				float zig = fabsf(fracf(along * 14.0f) - 0.5f) * 2.0f;
				if (fabsf(t - 0.5f - (zig - 0.5f) * 0.5f) < 0.10f)
					c = gold;
				if (t < 0.10f || (t > 0.90f && t < 1.0f))
					c = cream;
				if (e > 0.96f)
					c = red * 0.8f;
			}
			// Franges
			if (b > 0.985f)
				c = (fracf(u * 90.0f) < 0.6f) ? cream : cream * 0.5f;
			// Trame du tissage
			float weave = 0.90f + 0.10f * hash2(x / 2, y / 2, 61) + 0.04f * sinf((float)y * kPi * 0.5f);
			float wear = 0.88f + 0.24f * fbm(u, v, 6, 6, 3, 62);
			c = c * (weave * wear);
			px[y * N + x] = {c.r, c.g, c.b, 1.0f};
		}
}

void parchment(Px *px, uint32_t seed) {
	const Col base{0.80f, 0.71f, 0.52f};
	for (int y = 0; y < N; y++)
		for (int x = 0; x < N; x++) {
			float u = ((float)x + 0.5f) / N, v = ((float)y + 0.5f) / N;
			float stain = fbm(u, v, 4, 4, 4, seed) - 0.5f;
			float fiber = fbm(u, v, 96, 96, 2, seed + 1) - 0.5f;
			float edge = std::min(std::min(u, 1.0f - u), std::min(v, 1.0f - v));
			Col c = base * (1.0f + stain * 0.30f + fiber * 0.10f);
			c = c * (0.72f + 0.28f * sstep(0.0f, 0.10f, edge));
			px[y * N + x] = {c.r, c.g, c.b, 1.0f};
		}
}

// Double page d'un grimoire ouvert.
void genPaper(Px *px) {
	parchment(px, 71);
	const Col ink{0.13f, 0.09f, 0.06f}, redInk{0.50f, 0.08f, 0.05f}, gold{0.80f, 0.60f, 0.22f};
	for (int y = 0; y < N; y++)
		for (int x = 0; x < N; x++) {
			float u = ((float)x + 0.5f) / N, v = ((float)y + 0.5f) / N;
			Px &p = px[y * N + x];
			// Pliure centrale
			float g = expf(-fabsf(u - 0.5f) * 38.0f);
			p.r *= 1.0f - 0.55f * g;
			p.g *= 1.0f - 0.57f * g;
			p.b *= 1.0f - 0.60f * g;
			int page = u < 0.5f ? 0 : 1;
			float pu = page == 0 ? (u - 0.07f) / 0.36f : (u - 0.57f) / 0.36f;
			if (pu < 0.0f || pu > 1.0f || v < 0.10f || v > 0.90f)
				continue;
			float row = (0.90f - v) / 0.80f * 22.0f;
			int line = (int)row;
			float lf = row - (float)line;
			bool capZone = page == 0 && line < 5 && pu < 0.24f;
			bool figZone = page == 1 && line >= 11;
			if (capZone) {
				// Lettrine enluminée
				float cu = pu / 0.24f, cv = (row / 5.0f);
				float border = std::min(std::min(cu, 1.0f - cu), std::min(cv, 1.0f - cv));
				Col c = border < 0.10f ? gold : redInk;
				float dx = cu - 0.5f, dy = cv - 0.5f;
				if (border >= 0.10f && fabsf(sqrtf(dx * dx + dy * dy) - 0.22f) < 0.05f)
					c = gold;
				p.r = c.r;
				p.g = c.g;
				p.b = c.b;
				continue;
			}
			if (figZone)
				continue;
			if (lf > 0.30f && lf < 0.62f) {
				int word = (int)(pu * 46.0f);
				float lineEnd = (line % 6 == 5) ? 0.55f : 0.97f;
				bool gapWord = hash2(word, line + page * 100, 72) < 0.16f;
				if (!gapWord && pu < lineEnd) {
					float stroke = hash2(x, line + page * 100, 73);
					float k = 0.55f + 0.45f * stroke;
					bool rubric = (line == 0 && page == 1) || (line == 6 && page == 0);
					Col c = rubric ? redInk : ink;
					p.r += (c.r - p.r) * k;
					p.g += (c.g - p.g) * k;
					p.b += (c.b - p.b) * k;
				}
			}
		}
	// Figure : cercle et étoile sur la page de droite
	float cx = 0.75f * N, cy = 0.29f * N, r = 0.115f * N;
	for (int i = 0; i < 48; i++) {
		float a0 = (float)i / 48.0f * 2.0f * kPi, a1 = (float)(i + 1) / 48.0f * 2.0f * kPi;
		drawLine(px, cx + cosf(a0) * r, cy + sinf(a0) * r, cx + cosf(a1) * r, cy + sinf(a1) * r, 1.8f, redInk, 0.9f);
		drawLine(px, cx + cosf(a0) * r * 0.86f, cy + sinf(a0) * r * 0.86f, cx + cosf(a1) * r * 0.86f, cy + sinf(a1) * r * 0.86f, 1.2f, ink, 0.8f);
	}
	for (int i = 0; i < 7; i++) {
		float a0 = (float)i / 7.0f * 2.0f * kPi + kPi * 0.5f, a1 = (float)((i * 3) % 7 + 0) / 7.0f * 2.0f * kPi + kPi * 0.5f;
		a1 = (float)(i + 3) / 7.0f * 2.0f * kPi + kPi * 0.5f;
		drawLine(px, cx + cosf(a0) * r * 0.86f, cy + sinf(a0) * r * 0.86f, cx + cosf(a1) * r * 0.86f, cy + sinf(a1) * r * 0.86f, 1.3f, ink, 0.85f);
	}
}

void genMetal(Px *px) {
	for (int y = 0; y < N; y++)
		for (int x = 0; x < N; x++) {
			float u = ((float)x + 0.5f) / N, v = ((float)y + 0.5f) / N;
			float brush = fbm(u, v, 3, 128, 3, 81) - 0.5f;
			float blot = fbm(u, v, 8, 8, 3, 82) - 0.5f;
			float pit = fbm(u, v, 64, 64, 2, 83);
			float lum = 0.78f + brush * 0.30f + blot * 0.25f;
			if (pit > 0.70f)
				lum *= 1.0f - (pit - 0.70f) * 1.5f;
			px[y * N + x] = {lum, lum, lum * 1.02f, 1.0f};
		}
}

void genLeather(Px *px) {
	for (int y = 0; y < N; y++)
		for (int x = 0; x < N; x++) {
			float u = ((float)x + 0.5f) / N, v = ((float)y + 0.5f) / N;
			float pebble = fbm(u, v, 72, 72, 3, 91);
			float fold = fbm(u, v, 6, 6, 3, 92) - 0.5f;
			float lum = 0.76f + 0.12f * sstep(0.30f, 0.62f, pebble) + fold * 0.14f;
			px[y * N + x] = {lum, lum * 0.985f, lum * 0.96f, 1.0f};
		}
}

float goldHeight(float u, float v) {
	float h = 0.0f;
	// Rang d'oves au centre
	float fu = fracf(u * 8.0f);
	float e = 1.0f - powf((fu - 0.5f) / 0.40f, 2.0f) - powf((v - 0.5f) / 0.20f, 2.0f);
	if (e > 0.0f)
		h = std::max(h, sqrtf(e));
	// Dards entre les oves
	float fd = fracf(u * 8.0f + 0.5f);
	float dd = 1.0f - fabsf(fd - 0.5f) / 0.07f - fabsf(v - 0.5f) / 0.26f;
	if (dd > 0.0f)
		h = std::max(h, dd * 0.7f);
	// Rangs de perles en haut et en bas
	for (int s = 0; s < 2; s++) {
		float cv = s ? 0.875f : 0.125f;
		float fb = fracf(u * 24.0f);
		float b = 1.0f - powf((fb - 0.5f) / 0.42f, 2.0f) - powf((v - cv) / 0.062f, 2.0f);
		if (b > 0.0f)
			h = std::max(h, sqrtf(b) * 0.55f);
	}
	// Filets
	float rim = std::min(fabsf(v - 0.25f), fabsf(v - 0.75f));
	if (rim < 0.02f)
		h = std::max(h, 0.35f * (1.0f - rim / 0.02f));
	return h;
}

void genGold(Px *px) {
	const Col gold{0.82f, 0.60f, 0.20f}, dark{0.30f, 0.17f, 0.04f}, bright{1.0f, 0.90f, 0.60f};
	const float d = 1.5f / N;
	for (int y = 0; y < N; y++)
		for (int x = 0; x < N; x++) {
			float u = ((float)x + 0.5f) / N, v = ((float)y + 0.5f) / N;
			float h = goldHeight(u, v);
			float slope = (goldHeight(u - d, v + d) - goldHeight(u + d, v - d));
			float lit = clamp01(0.62f + slope * 1.6f + h * 0.22f);
			float grain = fbm(u, v, 64, 64, 2, 101) - 0.5f;
			Col c = mixc(dark, gold, clamp01(lit + grain * 0.15f));
			float spec = sstep(0.80f, 1.0f, lit);
			c = mixc(c, bright, spec * 0.6f);
			px[y * N + x] = {c.r, c.g, c.b, 1.0f};
		}
}

void genWhite(Px *px) {
	for (int i = 0; i < N * N; i++)
		px[i] = {1, 1, 1, 1};
}

// Vitrail : résille de plomb en losanges, alpha = opacité.
void genLattice(Px *px) {
	for (int y = 0; y < N; y++)
		for (int x = 0; x < N; x++) {
			float u = ((float)x + 0.5f) / N, v = ((float)y + 0.5f) / N;
			float p = u * 6.0f, q = v * 6.0f;
			float s1 = p + q, s2 = p - q;
			float d1 = std::min(fracf(s1), 1.0f - fracf(s1)), d2 = std::min(fracf(s2), 1.0f - fracf(s2));
			float d = std::min(d1, d2);
			int pane1 = wrapi((int)floorf(s1), 12), pane2 = wrapi((int)floorf(s2), 12);
			float tintv = hash2(pane1, pane2, 111);
			Col glass = mixc(Col{0.55f, 0.75f, 0.70f}, Col{0.75f, 0.70f, 0.45f}, tintv);
			float lead = 1.0f - sstep(0.035f, 0.060f, d);
			float alpha = 0.05f + 0.07f * tintv + 0.04f * fbm(u, v, 24, 24, 2, 112);
			Col c = mixc(glass, Col{0.07f, 0.07f, 0.08f}, lead);
			px[y * N + x] = {c.r, c.g, c.b, alpha + (1.0f - alpha) * lead};
		}
}

void genBanner(Px *px) {
	const Col gold{0.86f, 0.66f, 0.26f};
	for (int y = 0; y < N; y++)
		for (int x = 0; x < N; x++) {
			float u = ((float)x + 0.5f) / N, v = ((float)y + 0.5f) / N;
			float weave = 0.86f + 0.10f * hash2(x / 2, y / 2, 121) + 0.04f * sinf((float)x * kPi * 0.5f);
			float fold = 0.90f + 0.16f * sinf(u * kPi * 5.0f + sinf(v * 5.0f) * 0.6f);
			float lum = weave * fold;
			Col c{lum, lum, lum};
			float alpha = 1.0f;
			bool g = false;
			if ((u > 0.05f && u < 0.09f) || (u > 0.91f && u < 0.95f))
				g = true;
			if (v > 0.93f && v < 0.96f)
				g = true;
			// Emblème : soleil rayonnant au-dessus d'un chevron
			float dx = u - 0.5f, dy = (v - 0.62f) * 1.6f;
			float r = sqrtf(dx * dx + dy * dy), ang = atan2f(dy, dx);
			if (r < 0.10f || (r > 0.125f && r < 0.20f + 0.05f * cosf(ang * 12.0f) && cosf(ang * 12.0f) > 0.2f))
				g = true;
			if (r > 0.06f && r < 0.075f)
				g = false;
			float chev = fabsf(u - 0.5f) * 0.9f + (v - 0.22f);
			if (chev > 0.10f && chev < 0.145f && v < 0.36f)
				g = true;
			if (chev > 0.19f && chev < 0.22f && v < 0.44f)
				g = true;
			if (g) {
				c = gold * (0.75f + 0.35f * weave * fold);
				alpha = 0.0f;
			}
			px[y * N + x] = {c.r, c.g, c.b, alpha};
		}
}

// ---------------------------------------------------------------------------
// Paysages (vue par la fenêtre et tableaux)

enum Feature { kFeatNone = 0, kFeatConifers, kFeatCastle, kFeatDeadTrees, kFeatCity, kFeatColumns, kFeatTower, kFeatFortress, kFeatReeds, kFeatStalagmites };

struct LandLayer {
	float base, rough;
	Col col;
	int feature;
	float scale;
	Col lights; // fenêtres éclairées
};

struct Land {
	Col skyTop, skyBot;
	bool body;
	float bx, by, br;
	Col bodyCol;
	float glow;
	float stars;
	float clouds;
	Col cloudCol;
	int nLayers;
	LandLayer layers[3];
	Col fog;
	float fogAmt;
	bool cave;      // voûte rocheuse avec stalactites
	Col caveCol;
	Col floorGlow;  // lueur montant du sol (cristaux, lave, lanternes)
	float floorGlowAmt;
	bool timbers;   // étais de mine
};

float ridge(float u, const LandLayer &L, uint32_t seed) {
	float n = 0.6f * vnoise(u * 3.0f, 0.5f, 1024, 8, seed) + 0.3f * vnoise(u * 9.0f, 1.5f, 1024, 8, seed) +
	          0.1f * vnoise(u * 31.0f, 2.5f, 1024, 8, seed);
	return L.base + L.rough * (n - 0.5f) * 2.0f;
}

void branch(Px *px, float x, float y, float ang, float len, float width, Col c, uint32_t seed, int depth) {
	float x1 = x + cosf(ang) * len, y1 = y + sinf(ang) * len;
	drawLine(px, x, y, x1, y1, width, c);
	if (depth <= 0)
		return;
	int n = 2 + (int)(hash1(depth, seed) * 1.99f);
	for (int i = 0; i < n; i++) {
		float t = 0.45f + 0.5f * hash1(i * 7 + depth, seed + 1);
		float da = (hash1(i * 13 + depth, seed + 2) - 0.5f) * 1.9f;
		branch(px, x + (x1 - x) * t, y + (y1 - y) * t, ang + da, len * (0.45f + 0.25f * hash1(i, seed + 3)),
		       std::max(1.0f, width * 0.6f), c, seed * 31 + (uint32_t)i + 5, depth - 1);
	}
}

void drawLand(Px *px, const Land &L, uint32_t seed) {
	// Ciel
	for (int y = 0; y < N; y++)
		for (int x = 0; x < N; x++) {
			float u = ((float)x + 0.5f) / N, v = ((float)y + 0.5f) / N;
			Col c = mixc(L.skyBot, L.skyTop, powf(v, 0.75f));
			if (L.stars > 0.0f && v > 0.25f) {
				float h = hash2(x, y, seed + 1);
				if (h > 1.0f - 0.0030f * L.stars) {
					float br = 0.5f + 0.5f * hash2(x, y, seed + 2);
					c = c + Col{0.9f, 0.9f, 1.0f} * (br * sstep(0.25f, 0.5f, v));
				}
			}
			if (L.clouds > 0.0f) {
				float n = fbm(u, v, 3, 9, 5, seed + 3);
				float band = sstep(0.15f, 0.55f, v);
				float k = sstep(0.52f - 0.25f * L.clouds, 0.80f, n) * band;
				c = mixc(c, L.cloudCol, k * 0.85f);
			}
			px[y * N + x] = {c.r, c.g, c.b, 1.0f};
		}
	if (L.body) {
		float cx = L.bx * N, cy = L.by * N, r = L.br * N;
		if (L.glow > 0.0f)
			glowDisc(px, cx, cy, r * 2.6f, L.bodyCol, 0.35f * L.glow);
		for (int y = (int)(cy - r - 2); y <= (int)(cy + r + 2); y++)
			for (int x = (int)(cx - r - 2); x <= (int)(cx + r + 2); x++) {
				float d = sqrtf(((float)x - cx) * ((float)x - cx) + ((float)y - cy) * ((float)y - cy));
				float k = 1.0f - sstep(r - 1.0f, r + 1.0f, d);
				float crater = fbm((float)x / N, (float)y / N, 20, 20, 3, seed + 4);
				blendPx(px, x, y, L.bodyCol * (0.80f + 0.25f * crater), k);
			}
	}
	// Voûte de caverne
	if (L.cave) {
		for (int x = 0; x < N; x++) {
			float u = ((float)x + 0.5f) / N;
			float top = 0.80f + 0.10f * (vnoise(u * 5.0f, 0.5f, 1024, 8, seed + 20) - 0.5f);
			// Stalactites
			for (int i = -1; i < 14; i++) {
				float cxs = ((float)i + hash1(i, seed + 21)) / 13.0f;
				float len = 0.06f + 0.26f * hash1(i, seed + 22) * hash1(i, seed + 23);
				float hw = 0.012f + 0.03f * hash1(i, seed + 24);
				float d = fabsf(u - cxs) / hw;
				if (d < 1.0f)
					top = std::min(top, 0.80f - len * powf(1.0f - d, 1.6f));
			}
			for (int y = (int)(top * N); y < N; y++) {
				float v = (float)y / N;
				float shade = 0.7f + 0.5f * fbm(u, v, 24, 24, 3, seed + 25);
				blendPx(px, x, y, L.caveCol * shade, 1.0f);
			}
		}
	}
	// Plans successifs, du plus lointain au plus proche
	for (int li = 0; li < L.nLayers; li++) {
		const LandLayer &ly = L.layers[li];
		uint32_t ls = seed + 100U * (uint32_t)(li + 1);
		std::vector<float> h(N);
		for (int x = 0; x < N; x++)
			h[x] = ridge(((float)x + 0.5f) / N, ly, ls);
		float s = ly.scale;
		struct Light {
			float x, y;
		};
		std::vector<Light> lights;
		struct Rect {
			float x0, y0, x1, y1;
		};
		std::vector<Rect> rects;
		switch (ly.feature) {
		case kFeatStalagmites: {
			int count = (int)(16.0f / s);
			for (int i = 0; i < count; i++) {
				float cxs = ((float)i + hash1(i, ls + 1)) / (float)count;
				float th = s * 0.13f * (0.3f + 1.2f * hash1(i, ls + 2) * hash1(i, ls + 3));
				float hw = s * (0.015f + 0.02f * hash1(i, ls + 4));
				float gy = ridge(cxs, ly, ls);
				for (int x = std::max(0, (int)((cxs - hw) * N) - 1); x <= std::min(N - 1, (int)((cxs + hw) * N) + 1); x++) {
					float d = fabsf(((float)x + 0.5f) / N - cxs) / hw;
					if (d < 1.0f)
						h[x] = std::max(h[x], gy + th * powf(1.0f - d, 1.5f));
				}
			}
			break;
		}
		case kFeatConifers:
		case kFeatReeds: {
			bool reeds = ly.feature == kFeatReeds;
			int count = (int)((reeds ? 60.0f : 26.0f) / s);
			for (int i = 0; i < count; i++) {
				float cxs = ((float)i + hash1(i, ls + 1)) / (float)count;
				float th = s * (reeds ? 0.10f : 0.16f) * (0.55f + 0.9f * hash1(i, ls + 2));
				float hw = reeds ? 0.004f : th * 0.26f;
				float gy = ridge(cxs, ly, ls);
				int x0 = (int)((cxs - hw) * N) - 1, x1 = (int)((cxs + hw) * N) + 1;
				for (int x = std::max(0, x0); x <= std::min(N - 1, x1); x++) {
					float d = fabsf(((float)x + 0.5f) / N - cxs) / hw;
					if (d >= 1.0f)
						continue;
					float t = 1.0f - d;
					float tiers = reeds ? 0.0f : 0.07f * th * fracf(t * 4.0f);
					h[x] = std::max(h[x], gy + th * t - tiers);
				}
			}
			break;
		}
		case kFeatCastle: {
			float cxs = 0.34f + 0.2f * hash1(1, ls);
			float gy = ridge(cxs, ly, ls) - 0.01f;
			float w = 0.20f * s, wallH = 0.085f * s;
			struct T {
				float off, w, h;
				bool roof;
			};
			const T towers[] = {{-0.5f, 0.20f, 1.9f, true}, {0.5f, 0.20f, 1.6f, true}, {0.08f, 0.30f, 2.5f, false}, {-0.15f, 0.12f, 3.3f, true}};
			for (int x = 0; x < N; x++) {
				float u = ((float)x + 0.5f) / N;
				float rel = (u - cxs) / w;
				if (fabsf(rel) < 0.5f) {
					float cren = fracf(u * 90.0f / s) < 0.5f ? 0.012f * s : 0.0f;
					h[x] = std::max(h[x], gy + wallH + cren);
				}
				for (const T &t : towers) {
					float tr = (rel - t.off) / t.w;
					if (fabsf(tr) < 0.5f) {
						float top = gy + wallH * t.h;
						if (t.roof)
							top += wallH * 0.9f * (1.0f - fabsf(tr) * 2.0f);
						else
							top += fracf(u * 120.0f / s) < 0.5f ? 0.010f * s : 0.0f;
						h[x] = std::max(h[x], top);
					}
				}
			}
			for (int i = 0; i < 9; i++)
				lights.push_back({cxs + (hash1(i, ls + 5) - 0.5f) * w * 0.9f, gy + wallH * (0.4f + 1.6f * hash1(i, ls + 6))});
			break;
		}
		case kFeatCity: {
			float x = -0.02f;
			int i = 0;
			while (x < 1.02f) {
				float bw = s * (0.03f + 0.06f * hash1(i, ls + 1));
				float bh = s * (0.05f + 0.16f * hash1(i, ls + 2) * hash1(i, ls + 3));
				int style = (int)(hash1(i, ls + 4) * 4.0f);
				float gy = ridge(x + bw * 0.5f, ly, ls);
				for (int px0 = std::max(0, (int)(x * N)); px0 < std::min(N, (int)((x + bw) * N)); px0++) {
					float t = (((float)px0 + 0.5f) / N - x) / bw;
					float top = gy + bh;
					if (style == 1)
						top += bw * 0.6f * (1.0f - fabsf(t - 0.5f) * 2.0f);
					else if (style == 2 && fabsf(t - 0.5f) < 0.18f)
						top += bh * 0.9f * (1.0f - fabsf(t - 0.5f) / 0.18f);
					else if (style == 3)
						top += fracf(t * 4.0f) < 0.5f ? 0.008f * s : 0.0f;
					h[px0] = std::max(h[px0], top);
				}
				int nl = (int)(hash1(i, ls + 7) * 4.0f);
				for (int k = 0; k < nl; k++)
					lights.push_back({x + bw * (0.2f + 0.6f * hash1(i * 9 + k, ls + 8)), gy + bh * (0.25f + 0.6f * hash1(i * 9 + k, ls + 9))});
				x += bw * (0.9f + 0.5f * hash1(i, ls + 10));
				i++;
			}
			break;
		}
		case kFeatColumns: {
			for (int i = 0; i < 7; i++) {
				float cxs = 0.08f + 0.86f * ((float)i + 0.6f * hash1(i, ls + 1)) / 7.0f;
				float hw = 0.016f * s;
				float ch = s * (0.08f + 0.26f * hash1(i, ls + 2));
				float gy = ridge(cxs, ly, ls) - 0.01f;
				bool lintel = hash1(i, ls + 3) > 0.55f && i < 6;
				for (int x = std::max(0, (int)((cxs - hw * 1.5f) * N)); x < std::min(N, (int)((cxs + hw * 1.5f) * N)); x++) {
					float d = fabsf(((float)x + 0.5f) / N - cxs) / hw;
					float broken = 0.02f * s * hash2(x / 3, i, ls + 4);
					if (d < 1.0f)
						h[x] = std::max(h[x], gy + ch - broken);
					else if (d < 1.5f)
						h[x] = std::max(h[x], gy + 0.02f * s);
				}
				if (lintel) {
					float nx = 0.08f + 0.86f * ((float)(i + 1) + 0.6f * hash1(i + 1, ls + 1)) / 7.0f;
					float nh = s * (0.08f + 0.26f * hash1(i + 1, ls + 2));
					float top = gy + std::min(ch, nh);
					rects.push_back({cxs - hw * 1.3f, top - 0.022f * s, nx + hw * 1.3f, top + 0.012f * s});
				}
			}
			break;
		}
		case kFeatTower: {
			float cxs = 0.60f;
			float gy = ridge(cxs, ly, ls);
			float th = 0.50f * s;
			for (int x = 0; x < N; x++) {
				float u = ((float)x + 0.5f) / N;
				float d = fabsf(u - cxs);
				// Tour effilée, couronnée d'une flèche
				for (float t = 0.0f; t <= 1.0f; t += 0.02f) {
					float w = 0.045f * s * (1.0f - 0.45f * t);
					if (d < w)
						h[x] = std::max(h[x], gy + th * t);
				}
				float wtop = 0.045f * s * 0.55f;
				if (d < wtop * 1.5f)
					h[x] = std::max(h[x], gy + th + 0.16f * s * (1.0f - d / (wtop * 1.5f)));
				// Balcon
				if (d < 0.062f * s)
					h[x] = std::max(h[x], gy + th * 0.72f);
			}
			for (int i = 0; i < 6; i++)
				lights.push_back({cxs + (hash1(i, ls + 5) - 0.5f) * 0.03f * s, gy + th * (0.15f + 0.13f * (float)i)});
			break;
		}
		case kFeatFortress: {
			float cxs = 0.42f;
			float gy = ridge(cxs, ly, ls);
			for (int i = 0; i < 11; i++) {
				float off = ((float)i - 5.0f) / 5.0f;
				float sx = cxs + off * 0.20f * s + (hash1(i, ls + 1) - 0.5f) * 0.02f;
				float sh = s * (0.42f - 0.26f * fabsf(off) + 0.10f * hash1(i, ls + 2));
				float hw = s * (0.012f + 0.018f * hash1(i, ls + 3));
				for (int x = std::max(0, (int)((sx - hw * 1.6f) * N)); x < std::min(N, (int)((sx + hw * 1.6f) * N)); x++) {
					float d = fabsf(((float)x + 0.5f) / N - sx) / hw;
					if (d < 1.0f)
						h[x] = std::max(h[x], gy + sh * (0.72f + 0.28f * (1.0f - d) * (1.0f - d)));
					else if (d < 1.6f)
						h[x] = std::max(h[x], gy + sh * 0.45f);
				}
				if (i % 2 == 0)
					lights.push_back({sx, gy + sh * (0.3f + 0.3f * hash1(i, ls + 6))});
			}
			break;
		}
		default:
			break;
		}
		float depth = L.nLayers > 1 ? (float)li / (float)(L.nLayers - 1) : 1.0f;
		Col col = mixc(ly.col, L.fog, L.fogAmt * (1.0f - depth));
		for (int x = 0; x < N; x++) {
			int top = (int)(h[x] * N);
			for (int y = 0; y < std::min(N, top + 1); y++) {
				float k = (y == top) ? fracf(h[x] * N) : 1.0f;
				float shade = 0.72f + 0.28f * ((float)y / (float)std::max(1, top));
				float tex = 0.92f + 0.16f * hash2(x / 2, y / 2, ls + 30);
				blendPx(px, x, y, col * (shade * tex), k);
			}
		}
		if (ly.feature == kFeatDeadTrees) {
			for (int i = 0; i < 5; i++) {
				float cxs = (0.08f + 0.84f * ((float)i + 0.7f * hash1(i, ls + 1)) / 5.0f);
				float gy = ridge(cxs, ly, ls);
				branch(px, cxs * N, gy * N - 2.0f, kPi * 0.5f + (hash1(i, ls + 2) - 0.5f) * 0.4f,
				       s * N * (0.10f + 0.10f * hash1(i, ls + 3)), 4.0f * s + 1.0f, col * 0.8f, ls + (uint32_t)i * 17, 3);
			}
		}
		for (const Rect &rc : rects)
			fillRect(px, rc.x0 * N, rc.y0 * N, rc.x1 * N, rc.y1 * N, col * 0.9f);
		for (const Light &lt : lights) {
			float lx = lt.x * N, lyy = lt.y * N;
			fillRect(px, lx - 1.0f, lyy - 1.5f, lx + 1.5f, lyy + 2.0f, ly.lights, 1.0f);
			glowDisc(px, lx, lyy, 4.0f, ly.lights, 0.35f);
		}
	}
	if (L.timbers) {
		const Col wood{0.10f, 0.06f, 0.03f};
		for (int i = 0; i < 3; i++) {
			float cx = (0.2f + 0.3f * (float)i) * N, w = (0.16f - 0.03f * (float)i) * N, hgt = (0.62f - 0.08f * (float)i) * N;
			float bw = 9.0f - 2.0f * (float)i;
			fillRect(px, cx - w, 0, cx - w + bw, hgt, wood);
			fillRect(px, cx + w - bw, 0, cx + w, hgt, wood);
			fillRect(px, cx - w - bw, hgt - bw, cx + w + bw, hgt + bw * 0.4f, wood);
			glowDisc(px, cx + w * 0.55f, hgt * 0.78f, 9.0f, L.floorGlow, 0.9f);
			fillRect(px, cx + w * 0.55f - 2.0f, hgt * 0.78f - 3.0f, cx + w * 0.55f + 2.0f, hgt * 0.78f + 3.0f, Col{1.0f, 0.9f, 0.6f});
		}
	}
	if (L.floorGlowAmt > 0.0f) {
		for (int y = 0; y < N; y++)
			for (int x = 0; x < N; x++) {
				float u = ((float)x + 0.5f) / N, v = ((float)y + 0.5f) / N;
				float n = fbm(u, v, 5, 3, 3, seed + 40);
				float k = L.floorGlowAmt * expf(-v * 5.5f) * (0.5f + n);
				addPx(px, x, y, L.floorGlow, k);
			}
	}
}

Land landFor(int theme) {
	Land L;
	memset(&L, 0, sizeof(L));
	const Col noLight{0, 0, 0};
	switch (theme) {
	case kThemeForest:
		L.skyTop = {0.22f, 0.42f, 0.72f};
		L.skyBot = {0.96f, 0.78f, 0.48f};
		L.body = true; L.bx = 0.30f; L.by = 0.44f; L.br = 0.050f; L.bodyCol = {1.0f, 0.96f, 0.75f}; L.glow = 1.0f;
		L.clouds = 0.5f; L.cloudCol = {1.0f, 0.88f, 0.72f};
		L.nLayers = 3;
		L.layers[0] = {0.40f, 0.10f, {0.36f, 0.50f, 0.42f}, kFeatNone, 1.0f, noLight};
		L.layers[1] = {0.30f, 0.05f, {0.14f, 0.32f, 0.17f}, kFeatConifers, 0.8f, noLight};
		L.layers[2] = {0.10f, 0.04f, {0.04f, 0.13f, 0.06f}, kFeatConifers, 1.9f, noLight};
		L.fog = {0.85f, 0.75f, 0.50f}; L.fogAmt = 0.45f;
		break;
	case kThemeCave:
	case kThemeMine:
	case kThemeLava: {
		bool lava = theme == kThemeLava, mine = theme == kThemeMine;
		L.skyTop = lava ? Col{0.22f, 0.06f, 0.02f} : (mine ? Col{0.10f, 0.07f, 0.05f} : Col{0.05f, 0.09f, 0.16f});
		L.skyBot = lava ? Col{0.60f, 0.18f, 0.04f} : (mine ? Col{0.20f, 0.13f, 0.07f} : Col{0.10f, 0.22f, 0.38f});
		L.cave = true;
		L.caveCol = lava ? Col{0.05f, 0.02f, 0.015f} : (mine ? Col{0.045f, 0.035f, 0.03f} : Col{0.025f, 0.03f, 0.05f});
		L.nLayers = 2;
		L.layers[0] = {0.20f, 0.08f, lava ? Col{0.14f, 0.04f, 0.02f} : (mine ? Col{0.09f, 0.065f, 0.05f} : Col{0.05f, 0.08f, 0.13f}), kFeatStalagmites, 1.2f, noLight};
		L.layers[1] = {0.08f, 0.05f, lava ? Col{0.03f, 0.012f, 0.008f} : Col{0.015f, 0.018f, 0.03f}, kFeatStalagmites, 2.2f, noLight};
		L.fog = L.skyBot; L.fogAmt = 0.35f;
		L.floorGlow = lava ? Col{1.0f, 0.35f, 0.05f} : (mine ? Col{0.95f, 0.60f, 0.22f} : Col{0.20f, 0.55f, 0.95f});
		L.floorGlowAmt = lava ? 1.1f : (mine ? 0.25f : 0.6f);
		L.timbers = mine;
		break;
	}
	case kThemeSwamp:
		L.skyTop = {0.07f, 0.11f, 0.08f};
		L.skyBot = {0.34f, 0.42f, 0.25f};
		L.body = true; L.bx = 0.66f; L.by = 0.70f; L.br = 0.060f; L.bodyCol = {0.80f, 0.90f, 0.60f}; L.glow = 1.3f;
		L.clouds = 0.8f; L.cloudCol = {0.25f, 0.33f, 0.22f};
		L.nLayers = 3;
		L.layers[0] = {0.34f, 0.05f, {0.20f, 0.27f, 0.17f}, kFeatConifers, 0.6f, noLight};
		L.layers[1] = {0.22f, 0.03f, {0.07f, 0.10f, 0.06f}, kFeatDeadTrees, 1.5f, noLight};
		L.layers[2] = {0.08f, 0.02f, {0.02f, 0.04f, 0.02f}, kFeatReeds, 1.2f, noLight};
		L.fog = {0.36f, 0.45f, 0.28f}; L.fogAmt = 0.7f;
		L.floorGlow = {0.4f, 0.6f, 0.3f}; L.floorGlowAmt = 0.25f;
		break;
	case kThemeTower:
		L.skyTop = {0.02f, 0.04f, 0.14f};
		L.skyBot = {0.30f, 0.50f, 0.72f};
		L.body = true; L.bx = 0.24f; L.by = 0.82f; L.br = 0.035f; L.bodyCol = {0.95f, 0.97f, 1.0f}; L.glow = 0.8f;
		L.stars = 1.3f;
		L.clouds = 0.55f; L.cloudCol = {0.30f, 0.85f, 0.70f}; // aurore
		L.nLayers = 3;
		L.layers[0] = {0.36f, 0.20f, {0.55f, 0.66f, 0.82f}, kFeatNone, 1.0f, noLight};
		L.layers[1] = {0.22f, 0.05f, {0.86f, 0.90f, 0.97f}, kFeatTower, 1.25f, {1.0f, 0.85f, 0.5f}};
		L.layers[2] = {0.09f, 0.05f, {0.42f, 0.50f, 0.66f}, kFeatNone, 1.0f, noLight};
		L.fog = {0.5f, 0.65f, 0.85f}; L.fogAmt = 0.3f;
		break;
	case kThemeCity:
		L.skyTop = {0.13f, 0.11f, 0.33f};
		L.skyBot = {0.97f, 0.52f, 0.28f};
		L.body = true; L.bx = 0.70f; L.by = 0.30f; L.br = 0.070f; L.bodyCol = {1.0f, 0.75f, 0.40f}; L.glow = 1.2f;
		L.stars = 0.4f;
		L.clouds = 0.5f; L.cloudCol = {0.85f, 0.40f, 0.40f};
		L.nLayers = 3;
		L.layers[0] = {0.30f, 0.08f, {0.32f, 0.20f, 0.36f}, kFeatNone, 1.0f, noLight};
		L.layers[1] = {0.20f, 0.03f, {0.12f, 0.08f, 0.17f}, kFeatCity, 1.0f, {1.0f, 0.80f, 0.35f}};
		L.layers[2] = {0.05f, 0.02f, {0.04f, 0.03f, 0.07f}, kFeatCity, 1.6f, {1.0f, 0.75f, 0.30f}};
		L.fog = {0.85f, 0.45f, 0.30f}; L.fogAmt = 0.45f;
		break;
	case kThemeRuin:
		L.skyTop = {0.10f, 0.09f, 0.16f};
		L.skyBot = {0.48f, 0.38f, 0.46f};
		L.clouds = 1.0f; L.cloudCol = {0.24f, 0.21f, 0.30f};
		L.body = true; L.bx = 0.5f; L.by = 0.58f; L.br = 0.045f; L.bodyCol = {0.85f, 0.80f, 0.85f}; L.glow = 1.0f;
		L.nLayers = 3;
		L.layers[0] = {0.34f, 0.12f, {0.28f, 0.24f, 0.32f}, kFeatNone, 1.0f, noLight};
		L.layers[1] = {0.20f, 0.03f, {0.18f, 0.16f, 0.21f}, kFeatColumns, 1.0f, noLight};
		L.layers[2] = {0.07f, 0.04f, {0.05f, 0.04f, 0.07f}, kFeatDeadTrees, 1.3f, noLight};
		L.fog = {0.45f, 0.36f, 0.45f}; L.fogAmt = 0.5f;
		break;
	case kThemeCimmeria:
		L.skyTop = {0.09f, 0.01f, 0.02f};
		L.skyBot = {0.68f, 0.16f, 0.05f};
		L.body = true; L.bx = 0.70f; L.by = 0.68f; L.br = 0.085f; L.bodyCol = {0.95f, 0.30f, 0.15f}; L.glow = 1.2f;
		L.clouds = 0.8f; L.cloudCol = {0.12f, 0.02f, 0.03f};
		L.nLayers = 3;
		L.layers[0] = {0.36f, 0.22f, {0.22f, 0.05f, 0.04f}, kFeatNone, 1.0f, noLight};
		L.layers[1] = {0.20f, 0.04f, {0.05f, 0.012f, 0.02f}, kFeatFortress, 1.15f, {1.0f, 0.25f, 0.08f}};
		L.layers[2] = {0.07f, 0.05f, {0.02f, 0.005f, 0.01f}, kFeatReeds, 2.5f, noLight};
		L.fog = {0.6f, 0.15f, 0.06f}; L.fogAmt = 0.45f;
		break;
	case kThemeKeep:
	default:
		L.skyTop = {0.015f, 0.025f, 0.09f};
		L.skyBot = {0.10f, 0.15f, 0.32f};
		L.body = true; L.bx = 0.70f; L.by = 0.76f; L.br = 0.065f; L.bodyCol = {0.96f, 0.95f, 0.86f}; L.glow = 1.0f;
		L.stars = 1.0f;
		L.clouds = 0.35f; L.cloudCol = {0.20f, 0.24f, 0.40f};
		L.nLayers = 3;
		L.layers[0] = {0.34f, 0.16f, {0.11f, 0.15f, 0.30f}, kFeatNone, 1.0f, noLight};
		L.layers[1] = {0.25f, 0.05f, {0.035f, 0.05f, 0.11f}, kFeatCastle, 1.7f, {1.0f, 0.78f, 0.35f}};
		L.layers[2] = {0.09f, 0.04f, {0.015f, 0.025f, 0.05f}, kFeatConifers, 1.5f, noLight};
		L.fog = {0.12f, 0.17f, 0.34f}; L.fogAmt = 0.5f;
		break;
	}
	return L;
}

// Donne à l'image un aspect de peinture à l'huile vieillie.
void painterly(Px *px, uint32_t seed, float warmth) {
	for (int y = 0; y < N; y++)
		for (int x = 0; x < N; x++) {
			float u = ((float)x + 0.5f) / N, v = ((float)y + 0.5f) / N;
			Px &p = px[y * N + x];
			float canvas = 0.92f + 0.10f * hash2(x / 2, y / 2, seed) + 0.03f * sinf((float)x * 1.6f) * sinf((float)y * 1.6f);
			float dx = u - 0.5f, dy = v - 0.5f;
			float vig = 1.0f - 0.9f * (dx * dx + dy * dy);
			float crack = fbm(u, v, 20, 20, 2, seed + 1);
			float cr = fabsf(crack - 0.5f) < 0.006f ? 0.75f : 1.0f;
			float k = canvas * vig * cr;
			p.r = (p.r * (1.0f + 0.10f * warmth) + 0.02f * warmth) * k;
			p.g = p.g * k;
			p.b = p.b * (1.0f - 0.15f * warmth) * k;
		}
}

void genMap(Px *px) {
	parchment(px, 141);
	const Col ink{0.20f, 0.13f, 0.07f}, sea{0.55f, 0.58f, 0.50f}, land{0.74f, 0.62f, 0.40f}, redInk{0.55f, 0.10f, 0.06f};
	auto height = [](float u, float v) {
		float n = fbm(u * 0.999f, v * 0.999f, 3, 3, 5, 142);
		float dx = u - 0.5f, dy = v - 0.5f;
		return n - 0.9f * (dx * dx + dy * dy) + 0.08f;
	};
	for (int y = 0; y < N; y++)
		for (int x = 0; x < N; x++) {
			float u = ((float)x + 0.5f) / N, v = ((float)y + 0.5f) / N;
			float edge = std::min(std::min(u, 1.0f - u), std::min(v, 1.0f - v));
			if (edge < 0.05f)
				continue;
			float h = height(u, v);
			Px &p = px[y * N + x];
			if (h < 0.47f) {
				float wave = sinf((v * 70.0f + sinf(u * 30.0f) * 1.2f));
				float k = 0.35f + (wave > 0.93f ? 0.2f : 0.0f);
				// Hachures le long des côtes
				float coast = fracf((0.47f - h) * 60.0f);
				if (h > 0.41f && coast < 0.25f)
					k += 0.25f;
				p.r += (sea.r - p.r) * k;
				p.g += (sea.g - p.g) * k;
				p.b += (sea.b - p.b) * k;
			} else {
				float k = 0.45f;
				p.r += (land.r - p.r) * k;
				p.g += (land.g - p.g) * k;
				p.b += (land.b - p.b) * k;
			}
			if (fabsf(h - 0.47f) < 0.006f) {
				p.r = ink.r;
				p.g = ink.g;
				p.b = ink.b;
			}
		}
	// Montagnes et forêts
	for (int gy = 0; gy < 16; gy++)
		for (int gx = 0; gx < 16; gx++) {
			float u = ((float)gx + 0.2f + 0.6f * hash2(gx, gy, 143)) / 16.0f, v = ((float)gy + 0.2f + 0.6f * hash2(gx, gy, 144)) / 16.0f;
			if (u < 0.09f || u > 0.91f || v < 0.09f || v > 0.91f)
				continue;
			float h = height(u, v);
			float cx = u * N, cy = v * N;
			if (h > 0.60f) {
				float s = 7.0f + 5.0f * hash2(gx, gy, 145);
				drawLine(px, cx - s, cy - s * 0.6f, cx, cy + s * 0.7f, 1.6f, ink);
				drawLine(px, cx, cy + s * 0.7f, cx + s, cy - s * 0.6f, 1.6f, ink);
				drawLine(px, cx, cy + s * 0.7f, cx + s * 0.25f, cy - s * 0.2f, 1.0f, ink, 0.7f);
			} else if (h > 0.52f && hash2(gx, gy, 146) > 0.45f) {
				drawLine(px, cx, cy - 4.0f, cx, cy, 1.4f, ink);
				for (int k = 0; k < 6; k++) {
					float a = (float)k / 6.0f * 2.0f * kPi;
					drawLine(px, cx + cosf(a) * 3.0f, cy + 4.0f + sinf(a) * 3.0f, cx + cosf(a + 1.0f) * 3.0f, cy + 4.0f + sinf(a + 1.0f) * 3.0f, 1.3f, Col{0.22f, 0.30f, 0.14f});
				}
			}
		}
	// Route en pointillés et croix rouge
	float px0 = 0.22f * N, py0 = 0.30f * N;
	for (int i = 0; i < 40; i++) {
		float t = (float)(i + 1) / 40.0f;
		float nx = (0.22f + 0.50f * t + 0.05f * sinf(t * 9.0f)) * N, ny = (0.30f + 0.38f * t + 0.06f * sinf(t * 6.0f + 1.0f)) * N;
		if (i % 2 == 0)
			drawLine(px, px0, py0, nx, ny, 1.8f, redInk, 0.9f);
		px0 = nx;
		py0 = ny;
	}
	drawLine(px, px0 - 8, py0 - 8, px0 + 8, py0 + 8, 3.0f, redInk);
	drawLine(px, px0 - 8, py0 + 8, px0 + 8, py0 - 8, 3.0f, redInk);
	// Rose des vents
	float rx = 0.82f * N, ry = 0.20f * N;
	for (int i = 0; i < 8; i++) {
		float a = (float)i / 8.0f * 2.0f * kPi;
		float len = (i % 2 == 0) ? 34.0f : 18.0f;
		drawLine(px, rx, ry, rx + cosf(a) * len, ry + sinf(a) * len, (i % 2 == 0) ? 3.0f : 2.0f, i == 2 ? redInk : ink);
	}
	for (int i = 0; i < 40; i++) {
		float a0 = (float)i / 40.0f * 2.0f * kPi, a1 = (float)(i + 1) / 40.0f * 2.0f * kPi;
		drawLine(px, rx + cosf(a0) * 24.0f, ry + sinf(a0) * 24.0f, rx + cosf(a1) * 24.0f, ry + sinf(a1) * 24.0f, 1.2f, ink);
	}
	// Double filet d'encadrement
	const float b0 = 0.045f * N, b1 = 0.060f * N;
	for (float b : {b0, b1}) {
		drawLine(px, b, b, N - b, b, 1.6f, ink);
		drawLine(px, N - b, b, N - b, N - b, 1.6f, ink);
		drawLine(px, N - b, N - b, b, N - b, 1.6f, ink);
		drawLine(px, b, N - b, b, b, 1.6f, ink);
	}
}

const ThemeInfo kThemes[kThemeCount] = {
	//  nom         ambiante                  fenêtre                   feu                 bannière               part. vent goutt. grill. gronde.
	{"Château", {0.030f, 0.032f, 0.050f}, {0.10f, 0.15f, 0.32f}, {1.0f, 1.0f, 1.0f}, {0.42f, 0.06f, 0.07f}, 0, 0.35f, 0.0f, 0.25f, 0.0f},
	{"Forêt", {0.085f, 0.075f, 0.055f}, {0.95f, 0.70f, 0.36f}, {1.0f, 1.0f, 1.0f}, {0.10f, 0.30f, 0.12f}, 1, 0.45f, 0.0f, 0.0f, 0.0f},
	{"Cavernes", {0.022f, 0.028f, 0.045f}, {0.05f, 0.13f, 0.28f}, {1.0f, 0.95f, 0.9f}, {0.12f, 0.18f, 0.36f}, 0, 0.10f, 1.0f, 0.0f, 0.2f},
	{"Marais", {0.030f, 0.042f, 0.028f}, {0.16f, 0.26f, 0.12f}, {0.85f, 1.0f, 0.7f}, {0.20f, 0.28f, 0.10f}, 1, 0.25f, 0.3f, 1.0f, 0.0f},
	{"Mines", {0.035f, 0.028f, 0.022f}, {0.30f, 0.17f, 0.06f}, {1.0f, 0.95f, 0.85f}, {0.40f, 0.24f, 0.08f}, 0, 0.05f, 0.7f, 0.0f, 0.6f},
	{"Tour Blanche", {0.040f, 0.050f, 0.075f}, {0.30f, 0.45f, 0.70f}, {0.75f, 0.85f, 1.0f}, {0.60f, 0.64f, 0.72f}, 0, 1.0f, 0.0f, 0.0f, 0.0f},
	{"Cité", {0.050f, 0.038f, 0.045f}, {0.60f, 0.28f, 0.16f}, {1.0f, 1.0f, 1.0f}, {0.34f, 0.10f, 0.36f}, 0, 0.30f, 0.0f, 0.15f, 0.0f},
	{"Ruines", {0.036f, 0.032f, 0.045f}, {0.22f, 0.19f, 0.28f}, {1.0f, 0.95f, 0.95f}, {0.26f, 0.22f, 0.30f}, 0, 0.80f, 0.0f, 0.0f, 0.3f},
	{"Cimmeria", {0.045f, 0.018f, 0.018f}, {0.50f, 0.07f, 0.03f}, {1.0f, 0.75f, 0.65f}, {0.07f, 0.05f, 0.06f}, 2, 0.50f, 0.0f, 0.0f, 0.8f},
	{"Gouffre", {0.045f, 0.022f, 0.015f}, {0.75f, 0.24f, 0.04f}, {1.0f, 0.85f, 0.7f}, {0.42f, 0.14f, 0.04f}, 2, 0.15f, 0.3f, 0.0f, 1.0f},
};

} // namespace

const ThemeInfo &themeInfo(int theme) {
	if (theme < 0 || theme >= kThemeCount)
		theme = 0;
	return kThemes[theme];
}

int themeFromGame(const char *tileset, int level) {
	struct Entry {
		const char *prefix;
		int theme;
	};
	static const Entry table[] = {
		{"KEEP", kThemeKeep}, {"MANOR", kThemeKeep}, {"FOREST", kThemeForest}, {"CAVE", kThemeCave},
		{"SWAMP", kThemeSwamp}, {"URBISH", kThemeMine}, {"MINE", kThemeMine}, {"TOWER", kThemeTower},
		{"YVEL", kThemeCity}, {"CATWALK", kThemeLava}, {"RUIN", kThemeRuin}, {"CIMMERIA", kThemeCimmeria},
	};
	if (tileset && tileset[0]) {
		for (const Entry &e : table)
			if (!strncasecmp(tileset, e.prefix, strlen(e.prefix)))
				return e.theme;
	}
	// Repli : correspondance niveau -> zone du jeu d'origine.
	static const int8_t byLevel[30] = {
		kThemeKeep, kThemeKeep, kThemeForest, kThemeForest, kThemeKeep, kThemeCave, kThemeCave, kThemeCave, kThemeCave, kThemeCave,
		kThemeForest, kThemeSwamp, kThemeMine, kThemeMine, kThemeMine, kThemeMine, kThemeMine, kThemeForest, kThemeTower, kThemeTower,
		kThemeTower, kThemeTower, kThemeCity, kThemeLava, kThemeForest, kThemeLava, kThemeRuin, kThemeCimmeria, kThemeCimmeria, kThemeCimmeria};
	if (level >= 0 && level < 30)
		return byLevel[level];
	return kThemeKeep;
}

bool layerDependsOnTheme(int layer) {
	return layer == kTexSky || layer == kTexPaintTheme;
}

void generateTexture(int layer, int theme, uint8_t *rgba) {
	std::vector<Px> buf((size_t)N * N, Px{0, 0, 0, 1});
	Px *px = buf.data();
	switch (layer) {
	case kTexStone: genStone(px); break;
	case kTexFloor: genFloor(px); break;
	case kTexWood: genWood(px); break;
	case kTexPanel: genPanel(px); break;
	case kTexBooks: genBooks(px); break;
	case kTexRug: genRug(px); break;
	case kTexPaper: genPaper(px); break;
	case kTexMetal: genMetal(px); break;
	case kTexLeather: genLeather(px); break;
	case kTexGold: genGold(px); break;
	case kTexLattice: genLattice(px); break;
	case kTexBanner: genBanner(px); break;
	case kTexSky:
		drawLand(px, landFor(theme), 1000U + (uint32_t)theme * 77U);
		break;
	case kTexPaintTheme: {
		// Même région que la fenêtre, autre point de vue, rendu « peinture ».
		drawLand(px, landFor(theme), 5000U + (uint32_t)theme * 91U);
		painterly(px, 151, 1.0f);
		break;
	}
	case kTexPaintCastle: {
		Land L = landFor(kThemeCity);
		L.layers[1].feature = kFeatCastle;
		L.layers[1].scale = 1.7f;
		L.layers[1].base = 0.22f;
		L.layers[2].feature = kFeatConifers;
		L.layers[2].scale = 1.2f;
		L.layers[2].col = {0.05f, 0.07f, 0.05f};
		L.bx = 0.25f;
		L.by = 0.36f;
		drawLand(px, L, 7001U);
		painterly(px, 161, 1.0f);
		break;
	}
	case kTexPaintMap: genMap(px); break;
	case kTexWhite:
	default: genWhite(px); break;
	}
	for (int i = 0; i < N * N; i++) {
		rgba[i * 4 + 0] = (uint8_t)(clamp01(px[i].r) * 255.0f + 0.5f);
		rgba[i * 4 + 1] = (uint8_t)(clamp01(px[i].g) * 255.0f + 0.5f);
		rgba[i * 4 + 2] = (uint8_t)(clamp01(px[i].b) * 255.0f + 0.5f);
		rgba[i * 4 + 3] = (uint8_t)(clamp01(px[i].a) * 255.0f + 0.5f);
	}
}

} // namespace lolvr
