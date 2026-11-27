// lol-vr-3d — le donjon en 3D : textures et géométrie. Voir dungeon_data.h.
//
// Comment le jeu dessine sa vue (ScummVM, engines/kyra/engine/scene_rpg.cpp) : la vue de 176 x 120
// pixels est une grille de 22 x 15 blocs de 8 x 8 pixels. La table VMP donne, pour chaque bloc, le
// numéro d'un bloc graphique du fichier VCN (4 bits par pixel). Ses 330 premières entrées forment le
// fond (plafond et sol jusqu'à l'horizon) ; viennent ensuite des « pages » de 431 entrées, une par
// famille de murs, qui contiennent le mur vu sous chaque angle et à chaque distance. Le mur vu de
// face, à un pas, occupe les 192 dernières entrées d'une page (16 x 12 blocs, soit 128 x 96 pixels) :
// c'est lui qui habille les murs en 3D.
#include "dungeon_data.h"
#include "vecmath.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <algorithm>

namespace lolvr {

namespace {

const int kBgW = 176, kBgH = 120;
const int kPageLen = 431;
const int kFrontWallOffset = 239; // _vmpOffsetsDefault[7]

struct Rgba {
	uint8_t r, g, b, a;
};

uint8_t vga(uint8_t v) {
	v &= 63;
	return (uint8_t)((v << 2) | (v >> 4));
}

Rgba color(const DungeonGfx &g, int block, int nibble) {
	int shift = (g.vcnShift && block < g.vcnCount) ? g.vcnShift[block] : 0;
	int idx = g.colTable[(nibble | shift) & 127] & 127;
	return {vga(g.palette[idx * 3]), vga(g.palette[idx * 3 + 1]), vga(g.palette[idx * 3 + 2]), 255};
}

// Dessine un bloc 8x8 dans une image RGBA de largeur w.
void drawBlock(const DungeonGfx &g, Rgba *img, int w, int x0, int y0, int block, bool flip, bool transparent) {
	if (block < 0 || block >= g.vcnCount)
		return;
	const uint8_t *src = g.vcn + block * 32;
	for (int y = 0; y < 8; y++)
		for (int x = 0; x < 8; x++) {
			int bx = flip ? 7 - x : x;
			uint8_t byte = src[y * 4 + bx / 2];
			int nib = (bx & 1) ? (byte & 15) : (byte >> 4);
			if (transparent && nib == 0)
				continue;
			img[(y0 + y) * w + x0 + x] = color(g, block, nib);
		}
}

// Dessine bw x bh blocs à partir des entrées VMP données.
void drawEntries(const DungeonGfx &g, Rgba *img, int w, const uint16_t *entries, int bw, int bh) {
	for (int by = 0; by < bh; by++)
		for (int bx = 0; bx < bw; bx++) {
			uint16_t v = entries[by * bw + bx];
			if (v & 0x8000) {
				// Bloc de mur posé sur le fond : ses pixels de couleur 0 laissent voir ce qu'il y a derrière.
				uint16_t e = v - 0x8000;
				drawBlock(g, img, w, bx * 8, by * 8, e & 0x3FFF, (e & 0x4000) != 0, true);
			} else if (v) {
				drawBlock(g, img, w, bx * 8, by * 8, v & 0x3FFF, (v & 0x4000) != 0, false);
			}
		}
}

// Échantillonne un trapèze de l'image de fond (une case de sol ou de plafond vue en perspective)
// pour en faire une texture carrée. yFar/yNear : lignes du bord lointain et du bord proche ;
// wFar/wNear : leurs largeurs, centrées sur la vue.
void sampleTrapezoid(const Rgba *bg, Rgba *dst, float yFar, float yNear, float wFar, float wNear) {
	for (int j = 0; j < kDunTexSize; j++) {
		float v = ((float)j + 0.5f) / kDunTexSize;
		float y = yFar + (yNear - yFar) * v;
		float w = wFar + (wNear - wFar) * v;
		int sy = (int)y;
		sy = sy < 0 ? 0 : sy >= kBgH ? kBgH - 1 : sy;
		for (int i = 0; i < kDunTexSize; i++) {
			float x = 88.0f + (((float)i + 0.5f) / kDunTexSize - 0.5f) * w;
			int sx = (int)x;
			sx = sx < 0 ? 0 : sx >= kBgW ? kBgW - 1 : sx;
			dst[j * kDunTexSize + i] = bg[sy * kBgW + sx];
		}
	}
}

// Un bruit de valeurs qui se répète tous les `period` pixels (le plafond d'une case touche celui de
// la suivante sans couture).
static float ceilNoise(float x, float y, int period, uint32_t seed) {
	auto h = [&](int ix, int iy) {
		ix = ((ix % period) + period) % period;
		iy = ((iy % period) + period) % period;
		uint32_t v = (uint32_t)ix * 374761393u + (uint32_t)iy * 668265263u + seed * 2246822519u;
		v = (v ^ (v >> 13)) * 1274126177u;
		return (float)((v ^ (v >> 16)) & 0xFFFF) / 65535.0f;
	};
	const int x0 = (int)floorf(x), y0 = (int)floorf(y);
	float fx = x - (float)x0, fy = y - (float)y0;
	fx = fx * fx * (3.0f - 2.0f * fx);
	fy = fy * fy * (3.0f - 2.0f * fy);
	const float a = h(x0, y0), b = h(x0 + 1, y0), c = h(x0, y0 + 1), d = h(x0 + 1, y0 + 1);
	return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}

// Plusieurs octaves, de `cells` cellules par case à leur double, leur quadruple.
static float ceilFbm(int i, int j, int cells, uint32_t seed) {
	float sum = 0.0f, amp = 0.5f, norm = 0.0f;
	for (int o = 0; o < 4; o++) {
		const int period = cells << o;
		const float k = (float)period / (float)kDunTexSize;
		sum += amp * ceilNoise((float)i * k, (float)j * k, period, seed + (uint32_t)o * 17u);
		norm += amp;
		amp *= 0.5f;
	}
	return sum / norm;
}

// Les plafonds dessinés : leurs couleurs viennent du plafond que le jeu dessine dans son image de
// fond (une rampe du plus sombre au plus clair), le motif est le nôtre (le jeu, vu de face, n'en
// montre qu'une bande en perspective, trop floue pour un plafond qu'on regarde en VR).
static void makeCeiling(int style, const Rgba *bg, const Rgba *wall, Rgba sky, Rgba *ceil) {
	const int N = kDunTexSize;
	// Les cavernes : le plafond est la paroi elle-même (étirée sur la case).
	if (style == 6 && wall) {
		for (int j = 0; j < N; j++)
			for (int i = 0; i < N; i++)
				ceil[j * N + i] = wall[(j * kDunWallRows / N) * N + i];
		return;
	}
	std::vector<Rgba> px;
	if (style == 5 && wall) {
		// (le château : les couleurs de ses murs, pâles, et non celles du plafond du jeu, plus sombre)
		for (int y = 0; y < kDunWallRows; y++)
			for (int x = 0; x < N; x++)
				px.push_back(wall[y * N + x]);
	} else {
		for (int y = 0; y < 40; y++)
			for (int x = 24; x < 152; x++)
				px.push_back(bg[y * kBgW + x]);
	}
	const bool pale = style == 5 && wall;
	if (style == 5)
		style = 1;
	if (style == 6)
		style = 3; // (sans dessin de paroi : de la roche)
	auto lum = [](const Rgba &c) { return 0.30f * c.r + 0.59f * c.g + 0.11f * c.b; };
	std::sort(px.begin(), px.end(), [&](const Rgba &a, const Rgba &b) { return lum(a) < lum(b); });
	// (sans le noir du fond lointain : la rampe commence au premier pixel qui n'est pas noir)
	size_t first = 0;
	while (first + 16 < px.size() && lum(px[first]) < 6.0f)
		first++;
	Rgba ramp[8];
	for (int k = 0; k < 8; k++)
		ramp[k] = px[first + (size_t)(((float)k + 0.5f) / 8.0f * (float)(px.size() - first - 1))];
	if (pale && px.size() > 16) {
		// (château : la teinte claire des murs, en nuances assez écartées pour lire les caissons)
		const Rgba mid = px[first + (px.size() - first) * 6 / 10];
		for (int k = 0; k < 8; k++) {
			const float f = 0.58f + 0.08f * (float)k;
			auto ch = [&](int v) { v = (int)((float)v * f); return (uint8_t)(v > 255 ? 255 : v); };
			ramp[k] = Rgba{ch(mid.r), ch(mid.g), ch(mid.b), 255};
		}
	}
	auto mix = [](Rgba a, Rgba b, float t) {
		t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
		return Rgba{(uint8_t)(a.r + (b.r - a.r) * t), (uint8_t)(a.g + (b.g - a.g) * t), (uint8_t)(a.b + (b.b - a.b) * t), 255};
	};
	auto rampAt = [&](float v) { // v de 0 (sombre) à 1 (clair)
		v = v < 0.0f ? 0.0f : v > 0.999f ? 0.999f : v;
		const float f = v * 7.0f;
		const int k = (int)f;
		return mix(ramp[k], ramp[k + 1 > 7 ? 7 : k + 1], f - (float)k);
	};
	// Dehors : le vert du feuillage des murs (forêt du nord, forêt du sud, marais), du plus sombre
	// au plus clair, pour que les cimes du plafond soient celles des arbres qu'on longe.
	Rgba leaves[8];
	bool haveLeaves = false;
	if (style == 4 && wall) {
		std::vector<Rgba> green;
		for (int y = 0; y < kDunWallRows; y++)
			for (int x = 0; x < N; x++) {
				const Rgba c = wall[y * N + x];
				if (c.a && (int)c.g >= (int)c.r + 12 && (int)c.g + (int)c.b > 60)
					green.push_back(c);
			}
		if (green.size() > 200) {
			std::sort(green.begin(), green.end(), [&](const Rgba &a, const Rgba &b) { return lum(a) < lum(b); });
			for (int k = 0; k < 8; k++)
				leaves[k] = green[(size_t)(((float)k + 0.5f) / 8.0f * (float)(green.size() - 1))];
			haveLeaves = true;
		}
	}
	auto scale = [](Rgba c, float k) {
		auto ch = [&](int v) { v = (int)((float)v * k); return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v); };
		return Rgba{ch(c.r), ch(c.g), ch(c.b), 255};
	};
	for (int j = 0; j < N; j++)
		for (int i = 0; i < N; i++) {
			const float grain = ceilFbm(i, j, 16, 7u);
			Rgba c;
			if (style == 1) {
				// Caissons : une poutre sur le bord de la case (la moitié, l'autre moitié est chez la
				// voisine), une moulure en deux marches, le creux sombre, un fleuron au milieu.
				const int d = std::min(std::min(i, N - 1 - i), std::min(j, N - 1 - j));
				const bool litSide = (i < j) == (i < N - 1 - j) ? i < N / 2 : j < N / 2; // (haut et gauche éclairés)
				if (d < 9)
					c = rampAt(0.80f + 0.18f * grain);
				else if (d < 10)
					c = rampAt(0.35f);
				else if (d < 15)
					c = rampAt((litSide ? 0.70f : 0.42f) + 0.12f * grain);
				else if (d < 16)
					c = rampAt(0.25f);
				else if (d < 21)
					c = rampAt((litSide ? 0.55f : 0.30f) + 0.10f * grain);
				else {
					c = rampAt(0.12f + 0.16f * grain);
					const int cx = abs(i - N / 2), cy = abs(j - N / 2);
					if (d == 28 || d == 29)
						c = rampAt(0.38f + 0.1f * grain); // (un filet dans le creux)
					if (cx + cy < 11)
						c = rampAt(cx + cy < 4 ? 0.95f : (cx + cy) % 4 == 0 ? 0.30f : 0.72f); // le fleuron
				}
			} else if (style == 2) {
				// Mines : des planches dans un sens, une grosse poutre en travers au bord de la case.
				const int plank = j % 16;
				const float wood = ceilFbm(i / 4, j, 8, 11u); // (fibres allongées)
				if (i < 8 || i >= N - 8) {
					c = rampAt(0.62f + 0.25f * ceilFbm(i, j / 4, 8, 13u));
					if (i == 7 || i == N - 8)
						c = rampAt(0.08f);
				} else if (plank == 0) {
					c = rampAt(0.04f);
				} else {
					c = rampAt(0.30f + 0.45f * wood + (plank == 1 ? 0.12f : 0.0f) - (plank == 15 ? 0.10f : 0.0f));
				}
			} else if (style == 3) {
				// Roche : des bosses et des creux, à la couleur des parois du jeu.
				// (en paliers, comme les pixels du jeu, avec un grain fin)
				const float n = ceilFbm(i, j, 4, 23u), fine = ceilFbm(i, j, 32, 41u);
				const float ridge = 1.0f - fabsf(ceilFbm(i, j, 8, 29u) - 0.5f) * 2.0f;
				float v = 0.10f + 0.95f * (0.50f * n + 0.20f * grain + 0.30f * fine) - (ridge > 0.94f ? 0.30f : 0.0f);
				v = floorf(v * 7.0f + 0.5f) / 7.0f;
				c = rampAt(v);
			} else {
				// Dehors : les cimes, et le ciel entre elles.
				const float n = ceilFbm(i, j, 4, 31u), leaf = ceilFbm(i, j, 32, 43u);
				const float branch = fabsf(ceilFbm(i, j, 8, 37u) - 0.5f);
				const float open = n + 0.18f * (leaf - 0.5f); // (des trouées aux bords feuillus)
				if (open > 0.60f) {
					const float t = (open - 0.60f) / 0.40f;
					c = scale(sky, 1.05f + 0.55f * t);
				} else {
					float v = 0.18f + 0.95f * (0.65f * leaf + 0.35f * grain) * (0.7f + 0.5f * (0.60f - open));
					v = floorf(v * 6.0f + 0.5f) / 6.0f;
					// les feuilles, en paliers : la rampe du jeu (trop sombre là-haut) mêlée de vert
					if (haveLeaves) {
						const float lv = v * 0.62f; // (le feuillage des murs est sombre : les cimes aussi)
						const float f = (lv < 0.0f ? 0.0f : lv > 0.999f ? 0.999f : lv) * 7.0f;
						const int k = (int)f;
						c = scale(mix(leaves[k], leaves[k + 1 > 7 ? 7 : k + 1], f - (float)k), 0.80f);
					} else {
						const Rgba gr = {(uint8_t)(24 + 70 * v), (uint8_t)(48 + 110 * v), (uint8_t)(18 + 45 * v), 255};
						c = mix(scale(rampAt(v), 1.3f), gr, 0.55f);
					}
				}
				if (branch < 0.018f && open < 0.66f)
					c = rampAt(0.04f); // une branche
			}
			ceil[j * N + i] = c;
		}
	// Aussi pixelisé que les murs (dont un pixel du jeu fait deux texels) : un pixel du plafond fait
	// deux texels sur deux, sinon il paraissait bien plus fin que les murs.
	// Et le grain des murs du jeu : chaque gros pixel un peu plus clair ou plus sombre que son voisin
	// (le bois des mines, un grain étiré le long des planches).
	const float amp = style == 2 ? 0.12f : style == 4 ? 0.0f : style == 1 ? 0.05f : 0.08f;
	for (int j = 0; j < N; j += 2)
		for (int i = 0; i < N; i += 2) {
			uint32_t h = (uint32_t)(i / 2) * 2654435761u ^ (uint32_t)(style == 2 ? j / 6 : j / 2) * 2246822519u ^ 0x9E3779B9u;
			h = (h ^ (h >> 15)) * 2246822519u;
			const float k = 1.0f + amp * ((float)((h >> 8) & 255) / 127.5f - 1.0f);
			ceil[j * N + i] = scale(ceil[j * N + i], k);
		}
	for (int j = 0; j < N; j++)
		for (int i = 0; i < N; i++)
			ceil[j * N + i] = ceil[(j & ~1) * N + (i & ~1)];
}

float clampDepth(float r) {
	return r < 1.0f ? 1.0f : r > 3.0f ? 3.0f : r;
}

// Un type fait d'un seul dessin, sans mur : un objet au milieu de la case (fontaine, statue,
// buisson...), qu'on contourne ou qu'on voit à travers, ou une image qui couvre tout le côté
// (l'entrée d'une autre région, une maison au bout du chemin) et ferme la case.
bool isPictureType(const DungeonGfx &g, uint8_t t) {
	return !(g.wallFlags[t] & 0x89);
}

// Un mur invisible : ni dessin ni image pour le moteur. Le jeu n'en dessine rien (des cases de
// forêt qu'on traverse, comme le passage au sud du niveau 3).
static bool invisibleWall(const DungeonGfx &g, uint8_t t) {
	// Plus généralement, tout type sans page de murs ni dessin du moteur : le jeu n'en dessine rien
	// (le type 255 au bord du niveau 14, une barrière invisible devant une salle au niveau 12).
	if (!t || g.vmpMap[t] || !g.wallSlot)
		return false;
	const int slot = g.wallSlot[t];
	if (slot < 0)
		return true;
	// un dessin capturé, mais vide
	if (!g.wallAlpha || slot >= g.wallSlots)
		return false;
	const uint8_t *al = g.wallAlpha + (size_t)slot * kDunTexSize * kDunWallRows;
	for (int i = 0; i < kDunTexSize * kDunWallRows; i++)
		if (al[i])
			return false;
	return true;
}

bool hasSolidWall(const DungeonGfx &g, uint8_t t) {
	if (t == 0 || (g.wallFlags[t] & 8) || invisibleWall(g, t))
		return false;
	if (g.wallSlot && g.wallSlot[t] >= 0 && !g.vmpMap[t]) {
		if (!isPictureType(g, t))
			return false;
		// Un dessin posé sur le sol (le wagonnet d'une mine) : un objet au milieu de la case, pas un mur.
		if (g.wallAlpha && g.wallSlot[t] < g.wallSlots) {
			const uint8_t *al = g.wallAlpha + (size_t)g.wallSlot[t] * kDunTexSize * kDunWallRows;
			for (int i = 88 * kDunTexSize; i < kDunWallRows * kDunTexSize; i++)
				if (al[i])
					return false;
		}
		// Un dessin seulement sous l'horizon (rails d'une mine, dalle) : quelque chose au sol, pas un
		// mur ; la case reste un passage (sinon ni sol ni plafond : un trou dans la vue).
		const int slot = g.wallSlot[t];
		if (g.wallAlpha && slot < g.wallSlots) {
			const uint8_t *al = g.wallAlpha + (size_t)slot * kDunTexSize * kDunWallRows;
			for (int i = 0; i < 62 * kDunTexSize; i++)
				if (al[i])
					return true;
			for (int i = 62 * kDunTexSize; i < kDunWallRows * kDunTexSize; i++)
				if (al[i])
					return false;
		}
		return true;
	}
	return true;
}

} // namespace

int ceilingStyleOf(const char *t) {
	if (!t || !t[0])
		return 0;
	struct Entry {
		const char *prefix;
		int style;
	};
	static const Entry table[] = {{"KEEP", 5}, {"MANOR", 1}, {"TOWER", 1}, {"URBISH", 2}, {"MINE", 2}, {"CAVE", 6},
	                              {"RUIN", 3}, {"CIMMERIA", 3}, {"CATWALK", 3}, {"FOREST", 4}, {"SWAMP", 6}, {"YVEL", 4}};
	for (const Entry &e : table)
		if (!strncmp(t, e.prefix, strlen(e.prefix)))
			return e.style;
	return 0;
}


void spriteToRgba(const uint8_t *px, const uint8_t *al, const uint8_t *pal, uint8_t *out) {
	const int W = 176, H = 120, OH = 128;
	memset(out, 0, (size_t)W * OH * 4);
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			int i = y * W + x;
			if (!al[i])
				continue;
			uint8_t *o = out + (size_t)i * 4;
			int c = px[i];
			o[0] = vga(pal[c * 3]);
			o[1] = vga(pal[c * 3 + 1]);
			o[2] = vga(pal[c * 3 + 2]);
			o[3] = 255;
		}
}

bool isSolidBlock(const uint8_t *walls, int block, const DungeonGfx &gfx) {
	const uint8_t *w = walls + (block & 1023) * 4;
	for (int k = 0; k < 4; k++)
		if (!hasSolidWall(gfx, w[k]))
			return false;
	return true;
}

bool isHoldBlock(const uint8_t *walls, int block, const DungeonGfx &gfx) {
	// Une case où l'on peut se tenir n'a pas de mur à elle (les murs qui la bordent appartiennent aux
	// cases voisines). Si l'un de ses côtés porte un mur dessiné, c'est que le groupe vient de le
	// franchir : porte d'une boutique, image d'une autre région. Un faux mur qu'on traverse (le jeu y
	// montre les objets : drapeau 0x80) reste un passage.
	const uint8_t *w = walls + (block & 1023) * 4;
	for (int k = 0; k < 4; k++)
		if (hasSolidWall(gfx, w[k]) && !(gfx.wallFlags[w[k]] & 0x80))
			return true;
	return false;
}

void decodeDungeonTextures(const DungeonGfx &g, DungeonTextures &out) {
	out = DungeonTextures();
	for (int i = 0; i < 64; i++)
		out.pageLayer[i] = -1;
	out.baseWallLayer = -1;
	for (int i = 0; i < 256; i++) {
		out.typeLayer[i] = out.doorLayer[i] = out.decalLayer[i] = -1;
		out.doorDepth[i] = 1.0f;
		out.planeDepth[i] = 0.0f;
		memset(out.planeColFoot[i], 0, kDunTexSize);
		out.picBox[i][0] = out.picBox[i][1] = out.picBox[i][2] = out.picBox[i][3] = 0;
		out.footFit[i] = false;
		out.holeHalf[i] = 0.0f;
		out.plateR[i] = 0.0f;
		out.plateLayer[i] = -1;
		out.platePressed[i] = false;
		out.plateFoot[i] = 0;
		out.scenery[i] = false;
		out.hidden[i] = invisibleWall(g, (uint8_t)i);
		out.doorTopV[i] = out.doorFootV[i] = out.doorLift[i] = 0.0f;
		out.faceDepth[i] = 1.0f;
		out.kind[i] = kWallFace;
	}
	if (!g.vmp || !g.vcn || !g.colTable || !g.palette || !g.vmpMap || g.vmpLen < 330)
		return;
	const int pages = (g.vmpLen - 330) / kPageLen;
	const size_t layerBytes = (size_t)kDunTexSize * kDunTexSize * 4;

	// Le fond (sol et plafond à perte de vue), comme le jeu le dessine quand aucun mur ne cache la vue :
	// ce qu'il montre par une ouverture au bas d'un mur (le seuil d'une sortie).
	std::vector<Rgba> wallBg;
	{
		const int page = g.vmpMap[1] ? g.vmpMap[1] : 1;
		if ((page - 1) * kPageLen + 330 <= g.vmpLen) {
			wallBg.assign((size_t)kBgW * kBgH, Rgba{0, 0, 0, 255});
			drawEntries(g, wallBg.data(), kBgW, g.vmp + (page - 1) * kPageLen, 22, 15);
		}
	}

	// Les murs : une couche par page utilisée par au moins un type de mur.
	for (int p = 1; p <= pages && p < 64; p++) {
		bool used = false;
		for (int t = 1; t < 256 && !used; t++)
			used = g.vmpMap[t] == p;
		if (!used)
			continue;
		out.pageLayer[p] = out.layers++;
		out.rgba.resize(out.layers * layerBytes, 0);
		Rgba *img = (Rgba *)&out.rgba[(out.layers - 1) * layerBytes];
		drawEntries(g, img, kDunTexSize, g.vmp + (p - 1) * kPageLen + 330 + kFrontWallOffset, 16, 12);
		// Un mur plein ne doit laisser passer aucun jour (le jeu y dessinait le sol, en bas) : chaque
		// pixel transparent prend la couleur du pixel opaque au-dessus de lui. Les murs ajourés
		// (grilles, passages : drapeau 8) gardent leur transparence.
		bool seeThrough = false;
		for (int t = 1; t < 256; t++)
			if (g.vmpMap[t] == p && (g.wallFlags[t] & 8))
				seeThrough = true;
		if (!seeThrough) {
			for (int x = 0; x < kDunTexSize; x++) {
				Rgba last = {0, 0, 0, 255};
				for (int y = 0; y < kDunWallRows; y++) {
					Rgba &c = img[y * kDunTexSize + x];
					if (c.a)
						last = c;
					else
						c = last;
				}
			}
		}
	}

	// Les murs dessinés par le moteur : une couche par type, décors compris ; la porte à part.
	if (g.wallSlot && g.wallPixels && g.wallAlpha && g.screenPalette && g.wallSlots > 0) {
		const int n = kDunTexSize * kDunWallRows;
		auto rgb = [&](int c) {
			return Rgba{vga(g.screenPalette[c * 3]), vga(g.screenPalette[c * 3 + 1]), vga(g.screenPalette[c * 3 + 2]), 255};
		};
		auto newLayer = [&]() {
			out.layers++;
			out.rgba.resize(out.layers * layerBytes, 0);
			return (Rgba *)&out.rgba[(out.layers - 1) * layerBytes];
		};
		// Le mur ordinaire du niveau (le premier mur de pierre ou d'arbres plein) : il habille le tour
		// des images (une maison au bout d'un chemin) et les côtés où le jeu n'en dessine pas.
		int baseLayer = -1;
		// Types à rattacher au mur ordinaire une fois celui-ci trouvé : son type peut être décodé
		// après eux (l'ordre des types n'a aucun rapport avec leur usage dans le niveau), auquel cas
		// on ne peut pas encore les poser dessus lors de leur propre tour de boucle.
		std::vector<uint8_t> pendingBase;
		for (int t = 1; t < 256; t++) {
			int slot = g.wallSlot[t];
			if (slot < 0 || slot >= g.wallSlots)
				continue;
			const uint8_t *px = g.wallPixels + (size_t)slot * n, *al = g.wallAlpha + (size_t)slot * n;
			const bool seeThrough = (g.wallFlags[t] & 8) != 0;
			int top = kDunWallRows, bottom = -1;
			for (int i = 0; i < n; i++)
				if (al[i]) {
					top = top < i / kDunTexSize ? top : i / kDunTexSize;
					bottom = i / kDunTexSize;
				}
			const bool picture = !g.vmpMap[t] && isPictureType(g, t);
			if (bottom < 0 && !(g.doorAlpha && seeThrough)) {
				// Une image que le jeu ne montre que de loin : de près, c'est le mur ordinaire.
				if (picture) {
					if (baseLayer >= 0)
						out.typeLayer[t] = baseLayer;
					else
						pendingBase.push_back((uint8_t)t);
				}
				continue;
			}
			// Un type sans mur, fait d'un seul dessin : un objet au milieu de la case, ou, s'il ne
			// dessine que bas sous l'horizon (au-delà de la ligne 62, dans la case devant), quelque
			// chose au sol (flaque, dalle, trappe).
			if (!g.vmpMap[t] && bottom >= 0) {
				if (top >= 62) {
					out.kind[t] = kWallDecal;
					// Le sol de la case vue de face occupe les lignes 64 (bord lointain) à 96 (bord
					// proche) ; on le remet à plat, vu de dessus.
					Rgba *img = newLayer();
					out.decalLayer[t] = out.layers - 1;
					for (int j = 0; j < kDunTexSize; j++) {
						float k = 3.0f - 2.0f * ((float)j + 0.5f) / kDunTexSize; // 3 au loin, 1 tout près
						int row = (int)(48.0f + 48.0f / k);
						if (row >= kDunWallRows)
							row = kDunWallRows - 1;
						for (int i = 0; i < kDunTexSize; i++) {
							int col = (int)(64.0f + (((float)i + 0.5f) / kDunTexSize - 0.5f) * 128.0f / k);
							int si = row * kDunTexSize + (col < 0 ? 0 : col > 127 ? 127 : col);
							if (al[si])
								img[j * kDunTexSize + i] = rgb(px[si]);
						}
					}
					int nDark = 0;
					for (int j = 0; j < kDunTexSize; j++)
						for (int i = 0; i < kDunTexSize; i++) {
							const Rgba &q = img[j * kDunTexSize + i];
							if (q.a && (q.r * 3 + q.g * 6 + q.b) < 450) {
								nDark++;
							}
						}
					// Un trou (une fosse) : surtout une grande tache sombre. Le maillage creuse un puits au
					// milieu de la case (le dessin du jeu, une ellipse vue en perspective, n'est pas tracé).
					int nOp = 0;
					for (int q = 0; q < kDunTexSize * kDunTexSize; q++)
						nOp += img[q].a ? 1 : 0;
					if (nDark > kDunTexSize * kDunTexSize / 16 && nDark * 10 > nOp * 3) {
						out.holeHalf[t] = 0.36f;
					} else {
						// Une dalle à presser : une ellipse plate, large, posée au sol dans la case devant.
						// Le maillage en fait une dalle ronde en relief ; son dessus reprend le dessin du jeu.
						int c0 = kDunTexSize, c1 = -1, cnt = 0;
						for (int y = top; y <= bottom; y++)
							for (int x = 0; x < kDunTexSize; x++)
								if (al[y * kDunTexSize + x]) {
									c0 = x < c0 ? x : c0;
									c1 = x > c1 ? x : c1;
									cnt++;
								}
						const int w = c1 - c0 + 1, h = bottom - top + 1;
						if (c1 >= c0 && w >= 24 && h >= 6 && w * 2 >= h * 5 && bottom >= 84 && cnt > 150 && cnt * 10 > w * h * 5) {
							const float rowC = (float)(top + bottom) * 0.5f, depth = 48.0f / (rowC + 0.5f - 48.0f);
							float r = 0.5f * (float)w * depth / (float)kDunTexSize;
							out.plateR[t] = r < 0.15f ? 0.15f : r > 0.42f ? 0.42f : r;
							out.plateFoot[t] = bottom;
							Rgba *pimg = newLayer();
							out.plateLayer[t] = out.layers - 1;
							const float cxp = (float)(c0 + c1 + 1) * 0.5f;
							for (int j = 0; j < kDunTexSize; j++)
								for (int i = 0; i < kDunTexSize; i++) {
									const float u = ((float)i + 0.5f) / 64.0f - 1.0f, v = ((float)j + 0.5f) / 64.0f - 1.0f;
									if (u * u + v * v > 1.0f)
										continue;
									// le bord lointain (haut du dessin) en haut de la couche
									int col = (int)(cxp + u * 0.92f * (float)w * 0.5f), row = (int)(rowC + v * 0.92f * (float)h * 0.5f);
									col = col < 0 ? 0 : col > 127 ? 127 : col;
									row = row < 0 ? 0 : row >= kDunWallRows ? kDunWallRows - 1 : row;
									int si = row * kDunTexSize + col;
									if (!al[si]) // (un trou dans le dessin : le pixel le plus proche vers le centre)
										si = (int)rowC * kDunTexSize + (int)cxp;
									if (al[si])
										pimg[j * kDunTexSize + i] = rgb(px[si]);
								}
						}
					}
					continue;
				}
				if (!isPictureType(g, t) || bottom >= 88) // (posé sur le sol : un objet, comme le wagonnet)
					out.kind[t] = kWallObject;
			}
			out.typeLayer[t] = out.layers;
			// Un cadre de porte (mur ajouré) est dessiné par le jeu à la profondeur de sa porte : son
			// pied dit où, comme pour la porte.
			if (seeThrough && bottom >= 50 && bottom < kDunWallRows - 2)
				out.faceDepth[t] = clampDepth(48.0f / ((float)bottom + 1.0f - 48.0f));
			Rgba *img = newLayer();
			for (int i = 0; i < n; i++)
				if (al[i])
					img[i] = rgb(px[i]);
			// Un « objet » qui est un grand décor (la sortie d'une grotte, vue de l'intérieur) : une seule
			// image, agrandie à tout le mur, dressée au fond de la case (voir le maillage) ; deux images
			// croisées au milieu de la case la coupaient en deux.
			if (out.kind[t] == kWallObject && !g.vmpMap[t]) {
				int x0 = kDunTexSize, y0 = kDunWallRows, x1 = -1, y1 = -1;
				for (int y = 0; y < kDunWallRows; y++)
					for (int x = 0; x < kDunTexSize; x++)
						if (al[y * kDunTexSize + x]) {
							x0 = x < x0 ? x : x0; x1 = x > x1 ? x : x1;
							y0 = y < y0 ? y : y0; y1 = y > y1 ? y : y1;
						}
				const int bw = x1 - x0 + 1, bh = y1 - y0 + 1;
				int filled = 0;
				for (int y = y0; y <= y1 && x1 >= 0; y++)
					for (int x = x0; x <= x1; x++)
						filled += al[y * kDunTexSize + x] ? 1 : 0;
				// (et une image rectangulaire avec son fond — le mendiant d'Opinwood — : comme un lieu à
				// visiter, au fond de la case ; on avance vers elle et le jeu fait la suite)
				const bool framedPicture = x1 >= 0 && bw >= 24 && bh >= 16 && filled == bw * bh;
				if (x1 >= 0 && ((bw >= 56 && bh >= 44 && y0 < 28) || framedPicture)) { // (la fontaine du château : y0 36)
					out.scenery[t] = true;
					for (int y = 0; y < kDunWallRows; y++)
						for (int x = 0; x < kDunTexSize; x++) {
							const int si = (y0 + y * bh / kDunWallRows) * kDunTexSize + x0 + x * bw / kDunTexSize;
							img[y * kDunTexSize + x] = al[si] ? rgb(px[si]) : Rgba{0, 0, 0, 0};
						}
				}
			}
			// Un « objet » qui est une image rectangulaire avec son fond (le mendiant couché dans la forêt) :
			// le jeu la pose dans le décor sombre ; ici, ses bords s'effilochent (un tramage, le très
			// sombre qui touche le bord disparaît), et l'image descend jusqu'au sol (elle flottait).
			if (out.kind[t] == kWallObject && !g.vmpMap[t] && !out.scenery[t]) {
				int x0 = kDunTexSize, y0 = kDunWallRows, x1 = -1, y1 = -1, cnt = 0;
				for (int y = 0; y < kDunWallRows; y++)
					for (int x = 0; x < kDunTexSize; x++)
						if (al[y * kDunTexSize + x]) {
							cnt++;
							x0 = x < x0 ? x : x0; x1 = x > x1 ? x : x1;
							y0 = y < y0 ? y : y0; y1 = y > y1 ? y : y1;
						}
				const int bw = x1 - x0 + 1, bh = y1 - y0 + 1;
				if (x1 >= 0 && bw >= 24 && bh >= 16 && cnt == bw * bh) {
					auto dark = [&](int x, int y) {
						const Rgba &q = img[y * kDunTexSize + x];
						return q.a && q.r * 3 + q.g * 6 + q.b < 10 * 14;
					};
					std::vector<int> stack;
					auto push = [&](int x, int y) {
						if (x < x0 || x > x1 || y < y0 || y > y1 || !dark(x, y))
							return;
						img[y * kDunTexSize + x].a = 0;
						stack.push_back(y * kDunTexSize + x);
					};
					for (int x = x0; x <= x1; x++) { push(x, y0); push(x, y1); }
					for (int y = y0; y <= y1; y++) { push(x0, y); push(x1, y); }
					while (!stack.empty()) {
						const int q = stack.back();
						stack.pop_back();
						const int x = q % kDunTexSize, y = q / kDunTexSize;
						push(x - 1, y); push(x + 1, y); push(x, y - 1); push(x, y + 1);
					}
					// Le tramage des bords : de moins en moins de pixels vers l'extérieur (8 pixels de marge ;
					// le bas, posé au sol, garde un bord plus net).
					static const int bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
					for (int y = y0; y <= y1; y++)
						for (int x = x0; x <= x1; x++) {
							const int e = std::min(std::min(x - x0, x1 - x), std::min((y - y0), (y1 - y) * 3));
							if (e < 8 && bayer[y & 3][x & 3] >= e * 2)
								img[y * kDunTexSize + x].a = 0;
						}
					const int drop = kDunWallRows - 1 - y1;
					if (drop > 0)
						for (int y = kDunWallRows - 1; y >= 0; y--)
							for (int x = 0; x < kDunTexSize; x++)
								img[y * kDunTexSize + x] = y - drop >= 0 ? img[(y - drop) * kDunTexSize + x] : Rgba{0, 0, 0, 0};
				}
			}
			// Une image (une maison au bout d'un chemin, l'entrée d'une autre région) : le jeu la montre
			// au loin, petite au milieu du mur ; ici, de près, elle couvre tout le mur.
			if (picture && out.kind[t] == kWallFace) {
				int x0 = kDunTexSize, y0 = kDunWallRows, x1 = -1, y1 = -1;
				for (int y = 0; y < kDunWallRows; y++)
					for (int x = 0; x < kDunTexSize; x++)
						if (al[y * kDunTexSize + x]) {
							x0 = x < x0 ? x : x0; x1 = x > x1 ? x : x1;
							y0 = y < y0 ? y : y0; y1 = y > y1 ? y : y1;
						}
				const int bw = x1 - x0 + 1, bh = y1 - y0 + 1;
				if (x1 >= 0 && bw > 16 && bh > 12 && (bw < kDunTexSize - 8 || bh < kDunWallRows - 8)) {
					out.picBox[t][0] = (int16_t)x0;
					out.picBox[t][1] = (int16_t)y0;
					out.picBox[t][2] = (int16_t)bw;
					out.picBox[t][3] = (int16_t)bh;
					for (int y = 0; y < kDunWallRows; y++)
						for (int x = 0; x < kDunTexSize; x++) {
							const int sx = x0 + x * bw / kDunTexSize, sy = y0 + y * bh / kDunWallRows;
							const int si = sy * kDunTexSize + sx;
							img[y * kDunTexSize + x] = al[si] ? rgb(px[si]) : Rgba{0, 0, 0, 0};
						}
				}
			}
			// … posée sur le mur ordinaire du niveau (ce qui reste transparent).
			if (picture && out.kind[t] == kWallFace && baseLayer >= 0) {
				const Rgba *base = (const Rgba *)&out.rgba[(size_t)baseLayer * layerBytes];
				for (int i = 0; i < n; i++)
					if (!img[i].a)
						img[i] = base[i];
			}
			// Un mur plein ne doit laisser passer aucun jour (le jeu y dessinait le sol, en bas) : chaque
			// pixel transparent prend la couleur du pixel opaque au-dessus de lui. Sauf une ouverture
			// jusqu'en bas d'un mur qui, lui, descend jusqu'au sol (une sortie, son chemin peint au fond) :
			// le jeu y montre le sol qui continue, on y met donc son fond.
			if (!seeThrough && out.kind[t] == kWallFace) {
				int faceFoot = -1, lastRow[kDunTexSize];
				for (int x = 0; x < kDunTexSize; x++) {
					lastRow[x] = -1;
					for (int y = 0; y < kDunWallRows; y++)
						if (img[y * kDunTexSize + x].a)
							lastRow[x] = y;
					faceFoot = lastRow[x] > faceFoot ? lastRow[x] : faceFoot;
				}
				for (int x = 0; x < kDunTexSize; x++) {
					const bool opening = !wallBg.empty() && faceFoot >= kDunWallRows - 4 && lastRow[x] >= 0 && lastRow[x] < kDunWallRows - 8;
					// Le bas de l'ouverture (un chemin peint, une porte de boutique) sera posé au sol.
					out.planeColFoot[t][x] = lastRow[x] >= 0 ? (uint8_t)(lastRow[x] + 1) : 0;
					if (opening)
						out.footFit[t] = true;
					Rgba last = {0, 0, 0, 255};
					for (int y = 0; y < kDunWallRows; y++) {
						Rgba &cc = img[y * kDunTexSize + x];
						if (cc.a)
							last = cc;
						else if (opening && y > lastRow[x])
							cc = wallBg[(size_t)(8 + y) * kBgW + 24 + x];
						else
							cc = last;
					}
				}
			}
			if (baseLayer < 0 && g.vmpMap[t] && !seeThrough && out.kind[t] == kWallFace && top == 0)
				baseLayer = out.typeLayer[t];
			// La porte, à part : dans le jeu, elle se dresse au milieu de sa case. Son pied (le bas
			// du dessin) dit à quelle profondeur : sur la face, il serait à la ligne 96.
			if (g.doorPixels && g.doorAlpha) {
				const uint8_t *dpx = g.doorPixels + (size_t)slot * n, *dal = g.doorAlpha + (size_t)slot * n;
				int foot = -1;
				for (int i = 0; i < n; i++)
					if (dal[i])
						foot = i / kDunTexSize;
				if (foot > 50) {
					out.doorLayer[t] = out.layers;
					out.doorDepth[t] = clampDepth(48.0f / ((float)foot + 1.0f - 48.0f));
					Rgba *dimg = newLayer();
					for (int i = 0; i < n; i++)
						if (dal[i])
							dimg[i] = rgb(dpx[i]);
					// Le plan du fond : les colonnes de la porte, plus, de chaque côté, celles du cadre
					// qui ont la même hauteur qu'elle (les piliers). Sa largeur dit sa profondeur : la
					// largeur entière d'une case y prend 128 / profondeur pixels.
					int dtop = kDunWallRows, dc0 = kDunTexSize, dc1 = -1;
					for (int y = 0; y < kDunWallRows; y++)
						for (int x = 0; x < kDunTexSize; x++)
							if (dal[y * kDunTexSize + x]) {
								dtop = y < dtop ? y : dtop;
								dc0 = x < dc0 ? x : dc0;
								dc1 = x > dc1 ? x : dc1;
							}
					out.doorTopV[t] = (float)dtop / kDunTexSize;
					out.doorFootV[t] = (float)(foot + 1) / kDunTexSize;
					if (g.doorRaise && g.doorRaise[t]) {
						const float lift = (float)g.doorRaise[t] / (float)(foot - dtop + 1);
						out.doorLift[t] = lift > 1.0f ? 1.0f : lift;
					}
					auto sameHeight = [&](int x) {
						int t0 = -1, t1 = -1;
						for (int y = 0; y < kDunWallRows; y++)
							if (al[y * kDunTexSize + x] || dal[y * kDunTexSize + x]) {
								if (t0 < 0)
									t0 = y;
								t1 = y;
							}
						return t0 >= 0 && abs(t0 - dtop) <= 1 && abs(t1 - foot) <= 1;
					};
					int c0 = dc0, c1 = dc1;
					while (c0 > 0 && sameHeight(c0 - 1))
						c0--;
					while (c1 < kDunTexSize - 1 && sameHeight(c1 + 1))
						c1++;
					int pTop = dtop, pFoot = foot;
					// Piliers un peu plus hauts que la porte, posés plus bas qu'elle, un seuil entre eux
					// (manoir de Roland) : on les prend aussi, le plan va du haut des piliers à leur pied.
					if (!(c0 < dc0 - 2 && c1 > dc1 + 2)) {
						auto column = [&](int x, int &t0, int &t1) {
							t0 = -1;
							t1 = -1;
							for (int y = 0; y < kDunWallRows; y++)
								if (al[y * kDunTexSize + x] || dal[y * kDunTexSize + x]) {
									if (t0 < 0)
										t0 = y;
									t1 = y;
								}
						};
						auto pillar = [&](int x) {
							int t0, t1;
							column(x, t0, t1);
							return t0 >= 0 && abs(t0 - dtop) <= 3 && t1 >= foot - 1 && t1 <= foot + 12;
						};
						int a0 = dc0, a1 = dc1;
						while (a0 > 0 && pillar(a0 - 1))
							a0--;
						while (a1 < kDunTexSize - 1 && pillar(a1 + 1))
							a1++;
						if (a0 < dc0 - 2 && a1 > dc1 + 2) {
							c0 = a0;
							c1 = a1;
							for (int x = c0; x <= c1; x++) {
								int t0, t1;
								column(x, t0, t1);
								if (t0 >= 0) {
									pTop = t0 < pTop ? t0 : pTop;
									pFoot = t1 > pFoot ? t1 : pFoot;
								}
							}
						} else {
							// Piliers qui montent jusqu'à un linteau d'une seule pièce au-dessus de la porte,
							// les côtés du renfoncement en biais au-delà (dernier niveau) : les piliers sont
							// les colonnes droites, au même pied, de part et d'autre de la porte ; le plan va
							// du haut de la porte au pied des piliers, le linteau reste sur la face.
							int l0, l1, r0, r1;
							column(dc0 - 1 >= 0 ? dc0 - 1 : 0, l0, l1);
							column(dc1 + 1 < kDunTexSize ? dc1 + 1 : kDunTexSize - 1, r0, r1);
							auto straight = [&](int x, int f) {
								int t0, t1;
								column(x, t0, t1);
								return t0 >= 0 && t0 <= dtop + 3 && abs(t1 - f) <= 1;
							};
							if (dc0 > 0 && dc1 < kDunTexSize - 1 && l0 >= 0 && r0 >= 0 &&
							    l1 >= foot - 1 && l1 <= foot + 12 && r1 >= foot - 1 && r1 <= foot + 12) {
								int b0 = dc0 - 1, b1 = dc1 + 1;
								while (b0 > 0 && straight(b0 - 1, l1))
									b0--;
								while (b1 < kDunTexSize - 1 && straight(b1 + 1, r1))
									b1++;
								if (b0 < dc0 - 2 && b1 > dc1 + 2 && b0 > 0 && b1 < kDunTexSize - 1) {
									c0 = b0;
									c1 = b1;
									pTop = dtop;
									pFoot = l1 > r1 ? l1 : r1;
									pFoot = pFoot > foot ? pFoot : foot;
								}
							}
						}
					}
					const float r = (float)kDunTexSize / (float)(c1 - c0 + 1);
					// Seulement si le cadre a, des deux côtés de la porte, des piliers : sinon la porte reste
					// debout derrière la face.
					const bool pillars = c0 < dc0 - 2 && c1 > dc1 + 2;
					if (dc1 >= dc0 && seeThrough && pillars && r > 1.0f && r < 3.5f && pFoot - pTop > 20) {
						out.planeDepth[t] = r;
						out.planeU0[t] = (float)c0 / kDunTexSize;
						out.planeU1[t] = (float)(c1 + 1) / kDunTexSize;
						out.planeTop[t] = (float)pTop / kDunTexSize;
						out.planeFoot[t] = (float)(pFoot + 1) / kDunTexSize;
						for (int x = 0; x < kDunTexSize; x++) {
							int t1 = -1;
							for (int y = 0; y < kDunWallRows; y++)
								if (al[y * kDunTexSize + x] || dal[y * kDunTexSize + x])
									t1 = y;
							out.planeColFoot[t][x] = t1 >= 0 ? (uint8_t)(t1 + 1) : 0;
						}
					} else if (dc1 >= dc0 && seeThrough && out.doorDepth[t] > 1.08f && out.doorDepth[t] < 3.5f) {
						// Les autres portes au fond d'une ouverture (le cadre dessiné sur la face, des piliers
						// jusqu'en haut, sans pied commun avec la porte : grottes...) : le même renfoncement.
						// Le plan est à la profondeur de la porte ; la case entière y prend 128 / profondeur
						// colonnes, au milieu ; ce qui est dessiné de part et d'autre devient les côtés du
						// renfoncement, et chaque colonne du plan est posée au sol.
						const float rd = out.doorDepth[t];
						const int half = (int)lroundf(64.0f / rd);
						const int p0 = 64 - half, p1 = 64 + half - 1;
						int top = dtop;
						for (int x = p0; x <= p1; x++)
							for (int y = 0; y < top; y++)
								if (al[y * kDunTexSize + x] || dal[y * kDunTexSize + x]) {
									top = y;
									break;
								}
						if (p0 > 2 && p1 < kDunTexSize - 3 && dc0 >= p0 - 2 && dc1 <= p1 + 2 && foot - top > 20) {
							out.planeDepth[t] = rd;
							out.planeU0[t] = (float)p0 / kDunTexSize;
							out.planeU1[t] = (float)(p1 + 1) / kDunTexSize;
							out.planeTop[t] = (float)top / kDunTexSize;
							out.planeFoot[t] = (float)(foot + 1) / kDunTexSize;
							for (int x = 0; x < kDunTexSize; x++) {
								int t1 = -1;
								for (int y = 0; y < kDunWallRows; y++)
									if (al[y * kDunTexSize + x] || dal[y * kDunTexSize + x])
										t1 = y;
								out.planeColFoot[t][x] = t1 >= 0 ? (uint8_t)(t1 + 1) : 0;
							}
						}
					}
				}
			}
		}
		if (baseLayer >= 0)
			for (uint8_t pt : pendingBase)
				out.typeLayer[pt] = baseLayer;
		out.baseWallLayer = baseLayer;
		// Deux dalles qui se suivent, la seconde dessinée plus bas : c'est la même, enfoncée.
		// (les dalles vont par deux : levée et enfoncée ; un dessin seul, une flaque d'eau, reste à plat)
		bool pairOk[256] = {false};
		for (int t = 2; t < 256; t++)
			if (out.plateR[t] > 0.0f && out.plateR[t - 1] > 0.0f && out.plateFoot[t] == out.plateFoot[t - 1] + 1) {
				out.platePressed[t] = true;
				pairOk[t] = pairOk[t - 1] = true;
			}
		for (int t = 1; t < 256; t++)
			if (out.plateR[t] > 0.0f && !pairOk[t]) {
				out.plateR[t] = 0.0f;
				out.plateLayer[t] = -1;
			}
	}

	// Le fond (plafond et sol), comme le jeu le dessine quand aucun mur ne cache la vue.
	int bgPage = g.vmpMap[1] ? g.vmpMap[1] : 1;
	if ((bgPage - 1) * kPageLen + 330 <= g.vmpLen) {
		std::vector<Rgba> bg((size_t)kBgW * kBgH, Rgba{0, 0, 0, 255});
		drawEntries(g, bg.data(), kBgW, g.vmp + (bgPage - 1) * kPageLen, 22, 15);
		out.floorLayer = out.layers++;
		out.ceilingLayer = out.layers++;
		out.rgba.resize(out.layers * layerBytes, 0);
		Rgba *floor = (Rgba *)&out.rgba[out.floorLayer * layerBytes];
		Rgba *ceil = (Rgba *)&out.rgba[out.ceilingLayer * layerBytes];
		// Le sol : la bande la plus proche du joueur (lignes 104 à 119, sous le mur à un pas) est vue
		// presque de dessus ; étirée en hauteur et répétée deux fois, elle redonne le dallage.
		for (int j = 0; j < kDunTexSize; j++) {
			int sy = 104 + (j * 16 * 2 / kDunTexSize) % 16;
			for (int i = 0; i < kDunTexSize; i++)
				floor[j * kDunTexSize + i] = bg[sy * kBgW + 24 + i];
		}
		// Le plafond : la case devant le joueur, entre le haut du mur à un pas (ligne 8, 128 pixels
		// de large) et le haut du mur à deux pas (ligne 16, 80 pixels).
		sampleTrapezoid(bg.data(), ceil, 16.0f, 8.0f, 80.0f, 128.0f);
		// (un vrai plafond quand on connaît la région : caissons, poutres, roche, cimes)
		if (g.ceilingStyle > 0) {
			Rgba skyCol = {70, 90, 140, 255};
			float best = -1.0f;
			for (int y = 0; y < 20; y++)
				for (int x = 60; x < 116; x++) {
					const Rgba &c = bg[y * kBgW + x];
					const float bl = (float)c.b - 0.5f * ((float)c.r + (float)c.g);
					if (bl > best && bl >= 8.0f) {
						best = bl;
						skyCol = c;
					}
				}
			const Rgba *wall = out.baseWallLayer >= 0 ? (const Rgba *)&out.rgba[(size_t)out.baseWallLayer * layerBytes] : nullptr;
			makeCeiling(g.ceilingStyle, bg.data(), wall, skyCol, ceil);
		}
		// Dehors : le ciel, de la couleur du haut de l'image de fond (au centre, entre les arbres),
		// plus clair au zénith, plus sombre vers l'horizon.
		if (g.outdoor) {
			float sum[3] = {0, 0, 0}, best = -1.0f;
			Rgba sky = {40, 50, 90, 255};
			for (int y = 0; y < 20; y++)
				for (int x = 60; x < 116; x++) {
					const Rgba &c = bg[y * kBgW + x];
					float bl = (float)c.b - 0.5f * ((float)c.r + (float)c.g);
					if (bl > best) {
						best = bl;
						sky = c;
					}
					sum[0] += c.r; sum[1] += c.g; sum[2] += c.b;
				}
			if (best < 8.0f)
				sky = Rgba{70, 90, 140, 255};
			out.skyLayer = out.layers++;
			out.rgba.resize(out.layers * layerBytes, 0);
			Rgba *img = (Rgba *)&out.rgba[out.skyLayer * layerBytes];
			for (int j = 0; j < kDunTexSize; j++) {
				float t = (float)j / (kDunTexSize - 1); // 0 zénith, 1 horizon
				float k = 1.25f - 0.75f * t;
				for (int i = 0; i < kDunTexSize; i++) {
					int r = (int)(sky.r * k), gg = (int)(sky.g * k), b = (int)(sky.b * k);
					img[j * kDunTexSize + i] = Rgba{(uint8_t)(r > 255 ? 255 : r), (uint8_t)(gg > 255 ? 255 : gg), (uint8_t)(b > 255 ? 255 : b), 255};
				}
			}
		}
	}
}

void buildDungeonMesh(const uint8_t *walls, const DungeonGfx &g, const DungeonTextures &tex, std::vector<DungeonVertex> &out, int viewDir) {
	out.clear();
	if (!walls || !g.vmpMap || !g.wallFlags)
		return;
	const float S = kBlockSize, H = kWallHeight, vBottom = (float)kDunWallRows / kDunTexSize;
	auto put = [&](float x, float y, float z, float u, float v, float layer, float shade) {
		DungeonVertex dv;
		dv.pos[0] = x; dv.pos[1] = y; dv.pos[2] = z;
		dv.uvl[0] = u; dv.uvl[1] = v; dv.uvl[2] = layer;
		dv.shade = shade;
		out.push_back(dv);
	};
	// Quad a-b-c-d, sens trigonométrique vu de face (twoSided : visible des deux côtés).
	auto quad = [&](const float *a, const float *b, const float *c, const float *d, float layer, float shade, bool twoSided = false) {
		put(a[0], a[1], a[2], a[3], a[4], layer, shade);
		put(b[0], b[1], b[2], b[3], b[4], layer, shade);
		put(c[0], c[1], c[2], c[3], c[4], layer, shade);
		put(a[0], a[1], a[2], a[3], a[4], layer, shade);
		put(c[0], c[1], c[2], c[3], c[4], layer, shade);
		put(d[0], d[1], d[2], d[3], d[4], layer, shade);
		if (twoSided) {
			put(a[0], a[1], a[2], a[3], a[4], layer, shade);
			put(c[0], c[1], c[2], c[3], c[4], layer, shade);
			put(b[0], b[1], b[2], b[3], b[4], layer, shade);
			put(a[0], a[1], a[2], a[3], a[4], layer, shade);
			put(d[0], d[1], d[2], d[3], d[4], layer, shade);
			put(c[0], c[1], c[2], c[3], c[4], layer, shade);
		}
	};
	// Une image de mur (128 x 96, horizon au milieu) dressée verticalement : centre au sol (cx, cz),
	// tournée vers n, agrandie de `scale` autour de la hauteur des yeux du jeu (mi-hauteur).
	auto upright = [&](float cx, float cz, float nx, float nz, float layer, float shade, float scale, bool twoSided) {
		float rx = nz, rz = -nx;
		float hw = S * 0.5f * scale, y0 = H * 0.5f - H * 0.5f * scale, y1 = H * 0.5f + H * 0.5f * scale;
		float bl[5] = {cx - rx * hw, y0, cz - rz * hw, 0.0f, vBottom};
		float br[5] = {cx + rx * hw, y0, cz + rz * hw, 1.0f, vBottom};
		float tr[5] = {cx + rx * hw, y1, cz + rz * hw, 1.0f, 0.0f};
		float tl[5] = {cx - rx * hw, y1, cz - rz * hw, 0.0f, 0.0f};
		quad(bl, br, tr, tl, layer, shade, twoSided);
	};
	static const int dx[4] = {0, 1, 0, -1}, dz[4] = {-1, 0, 1, 0};
	// Comme dans les jeux de l'époque, les murs est-ouest sont un peu plus sombres.
	static const float faceShade[4] = {1.0f, 0.82f, 1.0f, 0.82f};
	// Dehors aussi, un plafond tiré de l'image de fond du jeu (les cimes des arbres) : le ciel d'une
	// grande boîte autour du niveau ne rendait pas bien.
	const bool sky = false;

	// Le plan du jeu est une suite de 1024 cases : à l'est de la dernière colonne vient la première
	// de la rangée suivante, au nord de la première rangée la dernière (une forêt qui boucle sur
	// elle-même). Autour du plan, une bordure de cases recopiées là où le jeu les montre.
	const int G = 6;
	for (int vz = -G; vz < 32 + G; vz++)
	for (int vx = -G; vx < 32 + G; vx++) {
		const int b = (vz * 32 + vx) & 1023;
		const float cx = ((float)vx + 0.5f) * S, cz = ((float)vz + 0.5f) * S;
		const bool solid = isSolidBlock(walls, b, g);
		if (solid && (vx < 0 || vx > 31 || vz < 0 || vz > 31)) {
			// Une case pleine recopiée ne sert qu'à poser ses murs devant une case de passage voisine.
			bool any = false;
			for (int k = 0; k < 4 && !any; k++) {
				const int ux = vx + (k == 1) - (k == 3), uz = vz + (k == 2) - (k == 0);
				any = !isSolidBlock(walls, (uz * 32 + ux) & 1023, g);
			}
			if (!any)
				continue;
		}
		const uint8_t *w = walls + b * 4;
		bool objectDone = false, decalDone = false;
		float holeH = 0.0f; // un trou au sol : demi-côté du puits
		uint8_t plateT = 0; // une dalle à presser : son type
		for (int k = 0; k < 4; k++) {
			uint8_t t = w[k];
			if (!t)
				continue;
			// Un objet au milieu de la case : deux images croisées, visibles des deux côtés.
			if (tex.kind[t] == kWallObject) {
				if (!objectDone && tex.typeLayer[t] >= 0 && tex.scenery[t]) {
					// Un décor entier : au fond de la case, face à l'unique ouverture (sinon, comme un objet).
					// (une case voisine en cul-de-sac, qui ne mène qu'ici, ne compte pas : l'entrée de la mine
					// d'Upper Opinwood est au bout d'un couloir, avec une case derrière elle)
					auto deadEnd = [&](int nb) {
						int n = 0;
						for (int k3 = 0; k3 < 4; k3++)
							n += !isSolidBlock(walls, (nb + dx[k3] + dz[k3] * 32) & 1023, g) ? 1 : 0;
						return n <= 1;
					};
					int open = -1, nOpen = 0, openMask = 0, nLive = 0, live = -1;
					for (int k2 = 0; k2 < 4; k2++) {
						const int nb = (b + dx[k2] + dz[k2] * 32) & 1023; // (le plan boucle, comme le jeu)
						if (!isSolidBlock(walls, nb, g)) {
							open = k2;
							nOpen++;
							openMask |= 1 << k2;
							if (!deadEnd(nb)) {
								nLive++;
								live = k2;
							}
						}
					}
					bool backdrop = false;
					if (nOpen == 2 && nLive == 1) {
						nOpen = 1;
						open = live;
						openMask = 1 << live;
						backdrop = true;
					}
					if (nOpen == 1) {
						objectDone = true;
						// (derrière l'image, un mur de la région cache la case en cul-de-sac)
						if (backdrop && tex.baseWallLayer >= 0)
							upright(cx - dx[open] * (S * 0.5f + 0.002f), cz - dz[open] * (S * 0.5f + 0.002f), (float)dx[open], (float)dz[open], (float)tex.baseWallLayer, faceShade[open], 1.0f, false);
						upright(cx - dx[open] * (S * 0.5f - 0.01f), cz - dz[open] * (S * 0.5f - 0.01f), (float)dx[open], (float)dz[open], (float)tex.typeLayer[t], faceShade[open], 1.0f, false);
						continue;
					}
					if (openMask == 5 || openMask == 10) {
						// Dans un couloir (une barrière, celle de Scotia) : une seule image en travers,
						// visible des deux côtés, et non deux images croisées.
						objectDone = true;
						const int k2 = openMask == 5 ? 0 : 1;
						upright(cx, cz, (float)dx[k2], (float)dz[k2], (float)tex.typeLayer[t], faceShade[k2], 1.0f, true);
						continue;
					}
				}
				if (!objectDone && tex.typeLayer[t] >= 0) {
					// Comme dans le jeu, une seule image, tournée vers le groupe (celle du côté qu'il
					// regarde) : deux images croisées se voyaient de biais, de côté (le voleur).
					objectDone = true;
					const int toward = (viewDir + 2) & 3; // (le côté de la case tourné vers le groupe)
					const uint8_t tv = w[toward] && tex.kind[w[toward]] == kWallObject && tex.typeLayer[w[toward]] >= 0 ? w[toward] : t;
					upright(cx, cz, (float)dx[toward], (float)dz[toward], (float)tex.typeLayer[tv], 1.0f, 1.0f, true);
				}
				continue;
			}
			// Un dessin au sol, posé juste au-dessus des dalles. Le jeu montre celui du côté qu'on regarde
			// (rails d'une mine le long du couloir) ; sinon celui d'un autre côté, tourné comme vu de là.
			if (tex.kind[t] == kWallDecal) {
				int r = viewDir & 3, kk = (r + 2) & 3;
				if (!(w[kk] && tex.kind[w[kk]] == kWallDecal && tex.decalLayer[w[kk]] >= 0)) {
					kk = k;
					r = (k + 2) & 3;
				}
				t = w[kk];
				if (!decalDone && tex.decalLayer[t] >= 0) {
					decalDone = true;
					holeH = tex.holeHalf[t] * S;
					if (holeH > 0.0f)
						continue; // (un trou : le puits, pas le dessin)
					if (tex.plateR[t] > 0.0f && tex.plateLayer[t] >= 0) {
						plateT = t;
						continue; // (une dalle : en relief, plus bas)
					}
					float x0 = cx - S * 0.5f, x1 = cx + S * 0.5f, z0 = cz - S * 0.5f, z1 = cz + S * 0.5f, y = 0.015f;
					// Coins nord-ouest, nord-est, sud-est, sud-ouest ; le haut de l'image (le bord lointain)
					// est du côté où regarde le groupe.
					const float cxs[4] = {x0, x1, x1, x0}, czs[4] = {z0, z0, z1, z1};
					float a[5] = {cxs[r], y, czs[r], 0, 0}, d[5] = {cxs[(r + 1) & 3], y, czs[(r + 1) & 3], 1, 0};
					float c[5] = {cxs[(r + 2) & 3], y, czs[(r + 2) & 3], 1, 1}, bb[5] = {cxs[(r + 3) & 3], y, czs[(r + 3) & 3], 0, 1};
					quad(a, bb, c, d, (float)tex.decalLayer[t], 0.95f);
				}
				continue;
			}
			if (invisibleWall(g, t))
				continue;
			int page = g.vmpMap[t];
			float typeLayer = (float)tex.typeLayer[t];
			if (tex.typeLayer[t] < 0 && (page <= 0 || page >= 64 || tex.pageLayer[page] < 0)) {
				// Mur que le jeu dessine seulement avec un décor (porte, grille...), sans dessin du
				// moteur : un mur plein prend la pierre ordinaire du niveau ; un mur ajouré n'est pas dessiné.
				if (g.wallFlags[t] & 8)
					continue;
				page = g.vmpMap[1];
				if (page <= 0 || page >= 64 || tex.pageLayer[page] < 0)
					continue;
			}
			// Un mur n'est visible que depuis la case voisine, si l'on peut s'y tenir (au bord du plan,
			// celle d'après dans la suite des cases : la bordure recopiée l'y montre).
			const int nb = (b + dx[k] + dz[k] * 32) & 1023;
			if (isSolidBlock(walls, nb, g))
				continue;
			const float wcx = cx, wcz = cz;
			// Normale sortante n ; vu de la case voisine, la droite du spectateur est (n.z, 0, -n.x).
			float nx = (float)dx[k], nz = (float)dz[k];
			float fx = wcx + nx * S * 0.5f, fz = wcz + nz * S * 0.5f;
			float layer = tex.typeLayer[t] >= 0 ? typeLayer : (float)tex.pageLayer[page];
			if (tex.planeDepth[t] > 0.0f && tex.typeLayer[t] >= 0) {
				// La porte dans son renfoncement : le cadre (les piliers) et la porte, au fond, sur toute
				// la largeur et toute la hauteur de la case ; les côtés du renfoncement sont les vrais
				// murs des cases voisines (la case de la porte est un passage).
				const float r = tex.planeDepth[t], rx = nz, rz = -nx;
				// Par bandes de colonnes de même pied : le pied dessiné de chaque colonne (un pilier, le bas
				// de la porte, le bas d'un chemin peint) est posé au sol. Le jeu dessine le sol dessous ; ici,
				// sans cela, la porte flotte et l'on voit le sol de l'autre côté sous elle.
				const int c0 = (int)lroundf(tex.planeU0[t] * kDunTexSize), c1 = (int)lroundf(tex.planeU1[t] * kDunTexSize);
				auto colFoot = [&](int c) {
					const float vf = tex.planeFoot[t];
					if (c < 0 || c >= kDunTexSize || !tex.planeColFoot[t][c])
						return vf;
					const float f = (float)tex.planeColFoot[t][c] / kDunTexSize;
					return f > tex.planeTop[t] + 0.2f && f < vf ? f : vf;
				};
				for (int pass = 0; pass < 2; pass++) {
					const float lyr = pass ? (float)tex.doorLayer[t] : layer;
					const float off = pass ? 0.004f : 0.0f; // la porte un rien devant le cadre
					const float px = fx - nx * ((r - 1.0f) * S * 0.5f - off), pz = fz - nz * ((r - 1.0f) * S * 0.5f - off);
					// Une porte qui s'ouvre glisse vers le haut, son haut caché sous le linteau : on en
					// montre le bas, plus haut d'autant.
					const float vt = tex.planeTop[t];
					for (int x = c0; x < c1;) {
						const float vf = colFoot(x);
						int e = x + 1;
						while (e < c1 && fabsf(colFoot(e) - vf) * kDunTexSize <= 1.5f)
							e++;
						// Le plan va de vt (en haut, à H) à vf (au sol) ; la porte occupe [dt, df] de son image.
						auto yOf = [&](float v) { return H * (vf - v) / (vf - vt); };
						float dt = vt, df = vf, lift = 0.0f;
						if (pass) {
							dt = tex.doorTopV[t] > vt ? tex.doorTopV[t] : vt;
							df = tex.doorFootV[t] < vf ? tex.doorFootV[t] : vf;
							lift = tex.doorLift[t] * (df - dt);
						}
						const float ua = (float)x / kDunTexSize, ub = (float)e / kDunTexSize;
						const float sa = (ua - tex.planeU0[t]) / (tex.planeU1[t] - tex.planeU0[t]) - 0.5f;
						const float sb = (ub - tex.planeU0[t]) / (tex.planeU1[t] - tex.planeU0[t]) - 0.5f;
						x = e;
						if (lift >= (df - dt) - 0.002f || df <= dt)
							continue;
						const float y0 = yOf(df - lift), y1 = yOf(dt);
						float bl[5] = {px + rx * S * sa, y0, pz + rz * S * sa, ua, df};
						float br[5] = {px + rx * S * sb, y0, pz + rz * S * sb, ub, df};
						float tr[5] = {px + rx * S * sb, y1, pz + rz * S * sb, ub, dt + lift};
						float tl[5] = {px + rx * S * sa, y1, pz + rz * S * sa, ua, dt + lift};
						quad(bl, br, tr, tl, lyr, faceShade[k]);
					}
				}
				// Les côtés du renfoncement, de la face au plan : le jeu les dessine en biais dans le cadre,
				// à gauche et à droite des piliers (montants, rocher, boutons d'ouverture). Le dessin étant
				// une perspective, ses coordonnées varient comme 1 / profondeur : en bandes.
				if (tex.planeU0[t] > 0.02f && tex.planeU1[t] < 0.98f) {
					const float rx = nz, rz = -nx, inset = 0.012f * S, vb = vBottom;
					const int N = 12;
					for (int side = 0; side < 2; side++) {
						const float lat = (side ? 1.0f : -1.0f) * (S * 0.5f - inset);
						const float uFace = side ? 1.0f : 0.0f, uPlane = side ? tex.planeU1[t] : tex.planeU0[t];
						for (int i = 0; i < N; i++) {
							float pt[2][4][5];
							for (int e = 0; e < 2; e++) {
								const float s = (float)(i + e) / N, d = 1.0f + s * (r - 1.0f);
								const float w = (1.0f / d - 1.0f / r) / (1.0f - 1.0f / r); // 1 à la face, 0 au plan
								const float u = w * uFace + (1.0f - w) * uPlane;
								const float vTop = (1.0f - w) * tex.planeTop[t];
								float vFoot = w * vb + (1.0f - w) * tex.planeFoot[t];
								// Le pied de la colonne dessinée, au sol : le dessin n'est pas une perspective exacte,
								// et un montant qui s'arrête avant le sol laisse voir le sol dessous.
								const int cx = (int)fminf(fmaxf(u * kDunTexSize - (side ? 0.5f : 0.0f), 0.0f), kDunTexSize - 1.0f);
								if (tex.planeColFoot[t][cx])
									vFoot = (float)tex.planeColFoot[t][cx] / kDunTexSize;
								const float cxp = fx - nx * ((d - 1.0f) * S * 0.5f) + rx * lat, czp = fz - nz * ((d - 1.0f) * S * 0.5f) + rz * lat;
								const float lo[5] = {cxp, 0.0f, czp, u, vFoot}, hi[5] = {cxp, H, czp, u, vTop};
								memcpy(pt[e][0], lo, sizeof lo);
								memcpy(pt[e][1], hi, sizeof hi);
							}
							quad(pt[0][0], pt[1][0], pt[1][1], pt[0][1], layer, faceShade[(k + 1) & 3], true);
						}
					}
				}
				// Le linteau : au-dessus de l'ouverture, le haut du mur reste sur la face (sinon on voit
				// le plafond de la case de la porte par-dessus, surtout porte ouverte).
				const float topRows = tex.planeTop[t] * (float)kDunTexSize;
				if (topRows > 1.0f) {
					const float yb = H * (1.0f - topRows / (float)kDunWallRows);
					float bl[5] = {fx - rx * S * 0.5f, yb, fz - rz * S * 0.5f, 0.0f, tex.planeTop[t]};
					float br[5] = {fx + rx * S * 0.5f, yb, fz + rz * S * 0.5f, 1.0f, tex.planeTop[t]};
					float tr[5] = {fx + rx * S * 0.5f, H, fz + rz * S * 0.5f, 1.0f, 0.0f};
					float tl[5] = {fx - rx * S * 0.5f, H, fz - rz * S * 0.5f, 0.0f, 0.0f};
					quad(bl, br, tr, tl, layer, faceShade[k]);
					// et son dessous, du linteau au plan de la porte
					const float px2 = fx - nx * ((r - 1.0f) * S * 0.5f), pz2 = fz - nz * ((r - 1.0f) * S * 0.5f);
					const float yp = H; // le haut du plan
					float a0[5] = {fx - rx * S * 0.5f, yb, fz - rz * S * 0.5f, 0.0f, tex.planeTop[t]};
					float a1[5] = {px2 - rx * S * 0.5f, yp, pz2 - rz * S * 0.5f, 0.0f, tex.planeTop[t] * 0.5f};
					float a2[5] = {px2 + rx * S * 0.5f, yp, pz2 + rz * S * 0.5f, 1.0f, tex.planeTop[t] * 0.5f};
					float a3[5] = {fx + rx * S * 0.5f, yb, fz + rz * S * 0.5f, 1.0f, tex.planeTop[t]};
					quad(a0, a3, a2, a1, layer, faceShade[k] * 0.7f, true);
				}
				continue;
			}
			float fr = tex.faceDepth[t];
			if (tex.footFit[t] && fr == 1.0f && tex.doorLayer[t] < 0) {
				// Une ouverture jusqu'au sol (une sortie, une porte de boutique) : chaque colonne du dessin
				// descend jusqu'au sol, son pied posé dessus, par bandes de même pied.
				const float rx = nz, rz = -nx;
				auto foot = [&](int c) { return tex.planeColFoot[t][c] ? (float)tex.planeColFoot[t][c] / kDunTexSize : vBottom; };
				for (int x = 0; x < kDunTexSize;) {
					const float vf = foot(x);
					int e = x + 1;
					while (e < kDunTexSize && fabsf(foot(e) - vf) * kDunTexSize <= 1.5f)
						e++;
					const float ua = (float)x / kDunTexSize, ub = (float)e / kDunTexSize;
					const float sa = ua - 0.5f, sb = ub - 0.5f;
					x = e;
					float bl[5] = {fx + rx * S * sa, 0.0f, fz + rz * S * sa, ua, vf};
					float br[5] = {fx + rx * S * sb, 0.0f, fz + rz * S * sb, ub, vf};
					float tr[5] = {fx + rx * S * sb, H, fz + rz * S * sb, ub, 0.0f};
					float tl[5] = {fx + rx * S * sa, H, fz + rz * S * sa, ua, 0.0f};
					quad(bl, br, tr, tl, layer, faceShade[k]);
				}
				continue;
			}
			upright(fx - nx * (fr - 1.0f) * S * 0.5f, fz - nz * (fr - 1.0f) * S * 0.5f, nx, nz, layer, faceShade[k], fr, false);
			// La porte se dresse plus loin dans la case, à la profondeur où le jeu la dessine.
			if (tex.doorLayer[t] >= 0) {
				float r = tex.doorDepth[t];
				float ddx = fx - nx * (r - 1.0f) * S * 0.5f, ddz = fz - nz * (r - 1.0f) * S * 0.5f;
				if (tex.doorLift[t] <= 0.0f) {
					upright(ddx, ddz, nx, nz, (float)tex.doorLayer[t], faceShade[k], r, false);
				} else {
					// Levée en partie : seule la part restée sous le haut de l'ouverture, plus haut d'autant.
					const float vt = tex.doorTopV[t], vf = tex.doorFootV[t], lift = tex.doorLift[t] * (vf - vt);
					if (lift < (vf - vt) - 0.002f) {
						const float rx = nz, rz = -nx, hw = S * 0.5f * r;
						const float y0 = H * 0.5f - H * 0.5f * r, y1 = H * 0.5f + H * 0.5f * r;
						auto yOf = [&](float v) { return y1 - (v / vBottom) * (y1 - y0); };
						float bl[5] = {ddx - rx * hw, yOf(vf - lift), ddz - rz * hw, 0.0f, vf};
						float br[5] = {ddx + rx * hw, yOf(vf - lift), ddz + rz * hw, 1.0f, vf};
						float tr[5] = {ddx + rx * hw, yOf(vt), ddz + rz * hw, 1.0f, vt + lift};
						float tl[5] = {ddx - rx * hw, yOf(vt), ddz - rz * hw, 0.0f, vt + lift};
						quad(bl, br, tr, tl, (float)tex.doorLayer[t], faceShade[k]);
					}
				}
			}
		}
		if (!solid) {
			float x0 = cx - S * 0.5f, x1 = cx + S * 0.5f, z0 = cz - S * 0.5f, z1 = cz + S * 0.5f;
			if (tex.floorLayer >= 0 && holeH <= 0.0f) {
				float a[5] = {x0, 0, z0, 0, 0}, bb[5] = {x0, 0, z1, 0, 1}, c[5] = {x1, 0, z1, 1, 1}, d[5] = {x1, 0, z0, 1, 0};
				quad(a, bb, c, d, (float)tex.floorLayer, 0.9f);
			} else if (tex.floorLayer >= 0) {
				// Un trou : le sol autour, et un puits qui s'enfonce dans le noir.
				const float L = (float)tex.floorLayer;
				auto floorRect = [&](float xa, float xb, float za, float zb) {
					const float ua = (xa - x0) / S, ub = (xb - x0) / S, va = (za - z0) / S, vb = (zb - z0) / S;
					float a[5] = {xa, 0, za, ua, va}, bb[5] = {xa, 0, zb, ua, vb}, c[5] = {xb, 0, zb, ub, vb}, d[5] = {xb, 0, za, ub, va};
					quad(a, bb, c, d, L, 0.9f);
				};
				const float hx0 = cx - holeH, hx1 = cx + holeH, hz0 = cz - holeH, hz1 = cz + holeH;
				floorRect(x0, x1, z0, hz0);
				floorRect(x0, x1, hz1, z1);
				floorRect(x0, hx0, hz0, hz1);
				floorRect(hx1, x1, hz0, hz1);
				const float depth = 2.0f * S;
				static const float bandShade[3] = {0.2f, 0.07f, 0.02f};
				const float px[4] = {hx0, hx1, hx1, hx0}, pz[4] = {hz0, hz0, hz1, hz1};
				for (int side = 0; side < 4; side++) {
					const float ax = px[side], az = pz[side], bx = px[(side + 1) & 3], bz = pz[(side + 1) & 3];
					for (int band = 0; band < 3; band++) {
						const float yt = -depth * (float)band / 3.0f, yb = -depth * (float)(band + 1) / 3.0f;
						float a[5] = {ax, yb, az, 0, 1}, bb[5] = {bx, yb, bz, 1, 1}, c[5] = {bx, yt, bz, 1, 0}, d[5] = {ax, yt, az, 0, 0};
						quad(a, bb, c, d, L, bandShade[band], true);
					}
				}
				float a[5] = {hx0, -depth, hz0, 0, 0}, bb[5] = {hx0, -depth, hz1, 0, 1}, c[5] = {hx1, -depth, hz1, 1, 1}, d[5] = {hx1, -depth, hz0, 1, 0};
				quad(a, bb, c, d, L, 0.0f, true);
			}
			if (plateT && tex.floorLayer >= 0) {
				// La dalle à presser : ronde, en relief (enfoncée : presque au ras du sol), cernée d'une rainure
				// sombre ; son dessus est le dessin du jeu.
				const float rr = tex.plateR[plateT] * S * 0.62f, top = tex.platePressed[plateT] ? 0.005f : 0.022f;
				const float L = (float)tex.floorLayer, P = (float)tex.plateLayer[plateT];
				const int N = 32;
				for (int i = 0; i < N; i++) {
					const float a0 = 6.2831853f * (float)i / N, a1 = 6.2831853f * (float)(i + 1) / N;
					const float c0 = cosf(a0), s0 = sinf(a0), c1 = cosf(a1), s1 = sinf(a1);
					// la rainure, un anneau sombre au ras du sol
					const float g = rr + 0.022f, yg = 0.003f;
					float ga[5] = {cx + c0 * rr, yg, cz + s0 * rr, 0.5f, 0.5f}, gb[5] = {cx + c0 * g, yg, cz + s0 * g, 0.5f, 0.5f};
					float gc[5] = {cx + c1 * g, yg, cz + s1 * g, 0.5f, 0.5f}, gd[5] = {cx + c1 * rr, yg, cz + s1 * rr, 0.5f, 0.5f};
					quad(ga, gb, gc, gd, L, 0.12f, true);
					// le flanc
					float fa[5] = {cx + c0 * rr, 0.0f, cz + s0 * rr, (float)i / N, 1.0f}, fb[5] = {cx + c1 * rr, 0.0f, cz + s1 * rr, (float)(i + 1) / N, 1.0f};
					float fc[5] = {cx + c1 * rr, top - 0.008f, cz + s1 * rr, (float)(i + 1) / N, 0.9f}, fd[5] = {cx + c0 * rr, top - 0.008f, cz + s0 * rr, (float)i / N, 0.9f};
					quad(fa, fb, fc, fd, L, 0.5f, true);
					// le dessus : anneaux de la part de disque ; un cercle gravé, un bord biseauté plus clair
					// (une dalle que l'on comprend faite pour être pressée)
					static const float ring[6] = {0.0f, 0.52f, 0.60f, 0.86f, 1.0f, 1.0f};
					const float dim = tex.platePressed[plateT] ? 0.8f : 1.0f;
					const float shadeOf[4] = {0.72f * dim, 0.72f * dim, 0.72f * dim, 1.05f * dim}; // (plus sombre que le sol, comme dans le jeu)
					for (int k = 0; k < 4; k++) {
						const float ra = ring[k] * rr, rb = ring[k + 1] * rr;
						const float ya = k == 3 ? top : top, yb = k == 3 ? top - 0.008f : top;
						float qa[5] = {cx + c0 * ra, ya, cz + s0 * ra, 0.5f + 0.49f * ring[k] * c0, 0.5f + 0.49f * ring[k] * s0};
						float qb[5] = {cx + c0 * rb, yb, cz + s0 * rb, 0.5f + 0.49f * ring[k + 1] * c0, 0.5f + 0.49f * ring[k + 1] * s0};
						float qc[5] = {cx + c1 * rb, yb, cz + s1 * rb, 0.5f + 0.49f * ring[k + 1] * c1, 0.5f + 0.49f * ring[k + 1] * s1};
						float qd[5] = {cx + c1 * ra, ya, cz + s1 * ra, 0.5f + 0.49f * ring[k] * c1, 0.5f + 0.49f * ring[k] * s1};
						quad(qa, qb, qc, qd, P, shadeOf[k], true);
					}
				}
			}
			if (tex.ceilingLayer >= 0 && !sky) {
				float a[5] = {x0, H, z0, 0, 0}, d[5] = {x1, H, z0, 1, 0}, c[5] = {x1, H, z1, 1, 1}, bb[5] = {x0, H, z1, 0, 1};
				quad(a, d, c, bb, (float)tex.ceilingLayer, 0.9f);
			}
		}
	}

	// Dehors : une grande boîte de ciel autour du niveau, qui échappe à la pénombre (ombre >= 2).
	if (sky) {
		const float L = -40.0f, R = 32.0f * S + 40.0f, top = 45.0f, bot = -2.0f, layer = (float)tex.skyLayer, e = 2.0f + 1.0f;
		// Faces tournées vers l'intérieur. Côtés : haut de la texture (zénith) en haut.
		float n0[5] = {L, bot, L, 0, 1}, n1[5] = {R, bot, L, 1, 1}, n2[5] = {R, top, L, 1, 0}, n3[5] = {L, top, L, 0, 0};
		quad(n0, n1, n2, n3, layer, e);
		float s0[5] = {R, bot, R, 0, 1}, s1[5] = {L, bot, R, 1, 1}, s2[5] = {L, top, R, 1, 0}, s3[5] = {R, top, R, 0, 0};
		quad(s0, s1, s2, s3, layer, e);
		float w0[5] = {L, bot, R, 0, 1}, w1[5] = {L, bot, L, 1, 1}, w2[5] = {L, top, L, 1, 0}, w3[5] = {L, top, R, 0, 0};
		quad(w0, w1, w2, w3, layer, e);
		float e0[5] = {R, bot, L, 0, 1}, e1[5] = {R, bot, R, 1, 1}, e2[5] = {R, top, R, 1, 0}, e3[5] = {R, top, L, 0, 0};
		quad(e0, e1, e2, e3, layer, e);
		float t0[5] = {L, top, L, 0, 0}, t1[5] = {R, top, L, 1, 0}, t2[5] = {R, top, R, 1, 0.02f}, t3[5] = {L, top, R, 0, 0.02f};
		quad(t0, t1, t2, t3, layer, e);
	}
}

} // namespace lolvr

namespace lolvr {

void applyPlacePicture(DungeonTextures &tex, int type, const uint8_t *rgb) {
	if (type <= 0 || type > 255 || !rgb || tex.kind[type] != kWallFace || tex.planeDepth[type] > 0.0f)
		return;
	const size_t layerBytes = (size_t)kDunTexSize * kDunTexSize * 4;
	tex.rgba.resize((size_t)(tex.layers + 1) * layerBytes, 0);
	uint8_t *img = &tex.rgba[(size_t)tex.layers * layerBytes];
	// 176 x 120 ramenés à 128 x 96 : chaque pixel du mur, la moyenne des pixels de l'image qu'il couvre
	for (int y = 0; y < kDunTexSize; y++)
		for (int x = 0; x < kDunTexSize; x++) {
			uint8_t *d = img + ((size_t)y * kDunTexSize + x) * 4;
			const int yy = y < kDunWallRows ? y : kDunWallRows - 1;
			const int sx0 = x * 176 / kDunTexSize, sx1 = (x + 1) * 176 / kDunTexSize;
			const int sy0 = yy * 120 / kDunWallRows, sy1 = (yy + 1) * 120 / kDunWallRows;
			int sum[3] = {0, 0, 0}, n = 0;
			for (int sy = sy0; sy < sy1; sy++)
				for (int sx = sx0; sx < sx1; sx++, n++)
					for (int k = 0; k < 3; k++)
						sum[k] += rgb[((size_t)sy * 176 + sx) * 3 + k];
			for (int k = 0; k < 3; k++)
				d[k] = (uint8_t)(n ? sum[k] / n : 0);
			d[3] = 255;
		}
	tex.typeLayer[type] = tex.layers++;
}


// Où le jeu dessine un point du sol (x, z en coordonnées du niveau) dans sa vue 320 x 200 : même calcul
// que le sol de pickDungeon. Faux si le point est hors de la vue (derrière, trop loin, de côté).
bool floorGamePoint(int block, int direction, float x, float z, float &px, float &py) {
	static const int dx[4] = {0, 1, 0, -1}, dz[4] = {-1, 0, 1, 0};
	const int d = direction & 3;
	Vec3 fAhead{(float)dx[d], 0.0f, (float)dz[d]}, rAhead{-fAhead.z, 0.0f, fAhead.x};
	float cx, cz;
	blockCenter(block, cx, cz);
	const Vec3 rel{x - cx, 0.0f, z - cz};
	const float fw = dot(rel, fAhead) / kBlockSize, lat = dot(rel, rAhead) / kBlockSize;
	if (fw < 0.0f || fw > 1.5f || fabsf(lat) > 0.6f)
		return false;
	float y, w;
	if (fw >= 0.5f) {
		const float k = fw - 0.5f;
		y = 104.0f - 24.0f * k;
		w = 128.0f - 48.0f * k;
	} else {
		// (la case du groupe : le jeu dessine ses objets tout en bas de sa vue)
		y = 128.0f - 48.0f * fw;
		w = 176.0f - 96.0f * fw;
	}
	px = 112.0f + 88.0f + clampf(lat, -0.5f, 0.5f) * w;
	py = clampf(y, 1.0f, 119.0f);
	return true;
}

// Le rayon d'un doigt dans le donjon (coordonnées du niveau) : le mur touché autour de la case où se
// tient le groupe, le sol devant, ou au loin ; et le point correspondant dans la vue du jeu (pixels
// de l'écran 320 x 200), là où le jeu attend le clic. Partagé par l'appli et le banc d'essai.
bool pickDungeon(const uint8_t *walls, int block, int direction, const DungeonTextures &tex, Vec3 o, Vec3 v, DungeonPick &out) {
	const float S = kBlockSize, H = kWallHeight;
	int face = -1;
	static const int dx[4] = {0, 1, 0, -1}, dz[4] = {-1, 0, 1, 0};
	const int d = direction & 3;
	Vec3 fAhead{(float)dx[d], 0.0f, (float)dz[d]}, rAhead{-fAhead.z, 0.0f, fAhead.x};
	float cx, cz;
	blockCenter(block, cx, cz);
	Vec3 center{cx, 0.0f, cz};
	int ahead = block + dz[d] * 32 + dx[d];
	const uint8_t faceType = ahead >= 0 && ahead < 1024 ? walls[ahead * 4 + ((d + 2) & 3)] : 0;
	bool faceWall = faceType != 0 && !tex.hidden[faceType] && tex.kind[faceType] == kWallFace;
	// Le mur (ou le cadre de porte) se tient à cette profondeur dans la case devant : 1 = sur sa face.
	const float fr = faceWall ? tex.faceDepth[faceType] : 1.0f;
	Vec3 hit{0.0f, 0.0f, 0.0f}, finalF = fAhead, finalR = rAhead;
	float px = -1, py = -1, bestT = 1e30f;
	// 1. Les murs autour de la case où se tient le groupe : la face devant (le passage bloqué) comme
	// les côtés (buissons, nids, trous dans les arbres) qu'on ne voit qu'en tournant la tête vers eux.
	for (int k = 0; k < 4; k++) {
		int nb = block + dz[k] * 32 + dx[k];
		if (nb < 0 || nb >= 1024)
			continue;
		uint8_t backT = walls[nb * 4 + ((k + 2) & 3)];
		if (!backT || tex.hidden[backT])
			continue;
		Vec3 f{(float)dx[k], 0.0f, (float)dz[k]}, r{-f.z, 0.0f, f.x};
		if (tex.kind[backT] == kWallFace && tex.planeDepth[backT] > 0.0f) {
			// Une porte dans son renfoncement : le plan du fond (porte et piliers) sur toute la case.
			const DungeonTextures &dt = tex;
			if (dt.planeU0[backT] > 0.02f && dt.planeU1[backT] < 0.98f) {
				// Les côtés du renfoncement (montants, bouton d'ouverture), comme le maillage les pose.
				const float rd = dt.planeDepth[backT];
				for (int side = 0; side < 2; side++) {
					const float sg = side ? 1.0f : -1.0f;
					float den = dot(v, r);
					if (fabsf(den) < 1e-4f)
						continue;
					Vec3 sidePt = center + f * (S * 0.5f) + r * (sg * S * 0.5f);
					float tt = dot(sidePt - o, r) / den;
					if (tt <= 0.0f || tt >= bestT)
						continue;
					Vec3 hh = o + v * tt;
					const float dd = dot(hh - center, f) / (S * 0.5f);
					if (dd < 1.0f || dd > rd || hh.y < 0.0f || hh.y > H)
						continue;
					const float w = (1.0f / dd - 1.0f / rd) / (1.0f - 1.0f / rd);
					const float uu = w * (side ? 1.0f : 0.0f) + (1.0f - w) * (side ? dt.planeU1[backT] : dt.planeU0[backT]);
					const float vTop = (1.0f - w) * dt.planeTop[backT];
					float vFoot = w * (96.0f / 128.0f) + (1.0f - w) * dt.planeFoot[backT];
					const int cx = (int)fminf(fmaxf(uu * 128.0f - (side ? 0.5f : 0.0f), 0.0f), 127.0f);
					if (dt.planeColFoot[backT][cx]) // comme le maillage : le pied dessiné de la colonne au sol
						vFoot = (float)dt.planeColFoot[backT][cx] / 128.0f;
					bestT = tt;
					hit = hh;
					px = 136.0f + uu * 128.0f;
					py = 8.0f + (vTop + (1.0f - hh.y / H) * (vFoot - vTop)) * 128.0f;
					finalF = r * sg;
					finalR = f * (side ? -1.0f : 1.0f);
					face = k;
				}
			}
			float den = dot(v, f);
			if (den <= 1e-4f)
				continue;
			Vec3 facePt = center + f * (S * 0.5f * dt.planeDepth[backT]);
			float tt = dot(facePt - o, f) / den;
			if (tt <= 0.0f || tt >= bestT)
				continue;
			Vec3 hh = o + v * tt;
			float u = 0.5f + dot(hh - facePt, r) / S;
			if (u < 0.0f || u > 1.0f || hh.y < 0.0f || hh.y > H)
				continue;
			bestT = tt;
			hit = hh;
			const float uu = dt.planeU0[backT] + u * (dt.planeU1[backT] - dt.planeU0[backT]);
			// comme le maillage : le pied dessiné de chaque colonne est au sol
			float vf = dt.planeFoot[backT];
			const int col = (int)fminf(fmaxf(uu * 128.0f, 0.0f), 127.0f);
			if (dt.planeColFoot[backT][col]) {
				const float f = (float)dt.planeColFoot[backT][col] / 128.0f;
				if (f > dt.planeTop[backT] + 0.2f && f < vf)
					vf = f;
			}
			px = 136.0f + uu * 128.0f;
			py = 8.0f + (dt.planeTop[backT] + (1.0f - hh.y / H) * (vf - dt.planeTop[backT])) * 128.0f;
			finalF = f;
			face = k;
			finalR = r;
		} else if (tex.kind[backT] == kWallFace) {
			float wfr = tex.faceDepth[backT];
			float den = dot(v, f);
			if (den <= 1e-4f)
				continue;
			Vec3 facePt = center + f * (S * 0.5f * wfr);
			float tt = dot(facePt - o, f) / den;
			if (tt <= 0.0f || tt >= bestT)
				continue;
			Vec3 hh = o + v * tt;
			// L'image du mur (128 x 96, horizon au milieu) est agrandie de wfr autour de mi-hauteur.
			float u = 0.5f + dot(hh - facePt, r) / (S * wfr);
			float row = 48.0f - (hh.y - H * 0.5f) / (H * 0.5f * wfr) * 48.0f;
			if (tex.footFit[backT] && wfr == 1.0f && tex.doorLayer[backT] < 0) {
				// comme le maillage : la colonne descend jusqu'à son pied dessiné
				const uint8_t f = tex.planeColFoot[backT][(int)fminf(fmaxf(u * 128.0f, 0.0f), 127.0f)];
				row = (1.0f - hh.y / H) * (f ? (float)f : 96.0f);
			}
			if (u < 0.0f || u > 1.0f || row < 0.0f || row > 96.0f || hh.y < 0.0f || hh.y > H)
				continue;
			bestT = tt;
			hit = hh;
			px = 136.0f + u * 128.0f;
			py = 8.0f + row;
			// Une image agrandie à tout le mur (un buisson, un lieu) : le clic va là où le jeu la dessine.
			if (const int16_t *pb = tex.picBox[backT]; pb[2] > 0) {
				px = 136.0f + (float)pb[0] + u * (float)pb[2];
				py = 8.0f + (float)pb[1] + row / 96.0f * (float)pb[3];
			}
			finalF = f;
			face = k;
			finalR = r;
		} else if (tex.kind[backT] == kWallObject) {
			// Un objet au milieu de la case voisine (buisson, nid...) : deux images croisées.
			float ncx, ncz;
			blockCenter(nb, ncx, ncz);
			Vec3 ocenter{ncx, 0.0f, ncz};
			const Vec3 planes[2] = {Vec3{0.0f, 0.0f, 1.0f}, Vec3{1.0f, 0.0f, 0.0f}};
			for (int pi = 0; pi < 2; pi++) {
				Vec3 pf = planes[pi], pr{-pf.z, 0.0f, pf.x};
				float den = dot(v, pf);
				if (fabsf(den) < 1e-4f)
					continue;
				float tt = dot(ocenter - o, pf) / den;
				if (tt <= 0.0f || tt >= bestT)
					continue;
				Vec3 hh = o + v * tt;
				float u = 0.5f + dot(hh - ocenter, pr) / S;
				float row = 48.0f - (hh.y - H * 0.5f) / (H * 0.5f) * 48.0f;
				if (u < 0.0f || u > 1.0f || row < 0.0f || row > 96.0f || hh.y < 0.0f || hh.y > H)
					continue;
				bestT = tt;
				hit = hh;
				px = 136.0f + u * 128.0f;
				py = 8.0f + row;
				finalF = pf;
				finalR = pr;
				face = k;
			}
		}
	}
	// 2. Le sol, de sous le joueur jusqu'au bout de la case devant (ou jusqu'au mur)
	if (px < 0.0f && v.y < -1e-4f) {
		float tt = -o.y / v.y;
		Vec3 hh = o + v * tt;
		float fw = dot(hh - center, fAhead) / S, lat = dot(hh - center, rAhead) / S;
		float maxF = faceWall ? 0.5f * fr : 1.5f;
		if (fw >= 0.0f && fw <= maxF && fabsf(lat) <= 0.6f) {
			float y, w;
			if (fw >= 0.5f) {
				float k = fw - 0.5f;
				y = 104.0f - 24.0f * k;
				w = 128.0f - 48.0f * k;
			} else {
				y = 120.0f - 32.0f * fw;
				w = 176.0f - 96.0f * fw;
			}
			px = 112.0f + 88.0f + clampf(lat, -0.5f, 0.5f) * w;
			py = clampf(y, 1.0f, 119.0f);
			hit = hh;
			// Le point du sol est montré à plat
			finalF = Vec3{0, 1, 0};
			finalR = rAhead;
		}
	}
	// 3. Ailleurs devant le groupe (au loin, en l'air) : la vue du jeu, selon l'angle. C'est là qu'on
	// lance un objet tenu (on peut ainsi sortir un objet de l'inventaire vers le donjon).
	if (px < 0.0f) {
		Vec3 flat{v.x, 0.0f, v.z};
		float fl = length(flat);
		if (fl > 1e-3f && dot(flat, fAhead) / fl > 0.5f) {
			float side = dot(v, rAhead) / dot(v, fAhead), up = v.y / dot(v, fAhead);
			// Le mur à une case montre 128 x 96 pixels pour S x H mètres, à S/2 du groupe.
			px = clampf(200.0f + side * 64.0f, 113.0f, 287.0f);
			py = clampf(56.0f - up * 48.0f * S / H, 2.0f, 100.0f);
			hit = o + v * 3.0f;
			finalF = fAhead;
			finalR = rAhead;
		}
	}
	out.px = px;
	out.py = py;
	out.face = face;
	out.hit = hit;
	out.forward = finalF;
	out.right = finalR;
	return px >= 0.0f;
}

} // namespace lolvr
