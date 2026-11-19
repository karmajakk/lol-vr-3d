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

#include <stdlib.h>
#include <string.h>

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

float clampDepth(float r) {
	return r < 1.0f ? 1.0f : r > 3.0f ? 3.0f : r;
}

// Un type fait d'un seul dessin, sans mur : un objet au milieu de la case (fontaine, statue,
// buisson...), qu'on contourne ou qu'on voit à travers, ou une image qui couvre tout le côté
// (l'entrée d'une autre région, une maison au bout du chemin) et ferme la case.
bool isPictureType(const DungeonGfx &g, uint8_t t) {
	return !(g.wallFlags[t] & 0x89);
}

bool hasSolidWall(const DungeonGfx &g, uint8_t t) {
	if (t == 0 || (g.wallFlags[t] & 8))
		return false;
	if (g.wallSlot && g.wallSlot[t] >= 0 && !g.vmpMap[t])
		return isPictureType(g, t);
	return true;
}

} // namespace

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
	for (int i = 0; i < 256; i++) {
		out.typeLayer[i] = out.doorLayer[i] = out.decalLayer[i] = -1;
		out.doorDepth[i] = 1.0f;
		out.planeDepth[i] = 0.0f;
		out.faceDepth[i] = 1.0f;
		out.kind[i] = kWallFace;
	}
	if (!g.vmp || !g.vcn || !g.colTable || !g.palette || !g.vmpMap || g.vmpLen < 330)
		return;
	const int pages = (g.vmpLen - 330) / kPageLen;
	const size_t layerBytes = (size_t)kDunTexSize * kDunTexSize * 4;

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
					continue;
				}
				if (!isPictureType(g, t))
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
			// Une image (une maison au bout d'un chemin) : posée sur le mur ordinaire du niveau.
			if (picture && out.kind[t] == kWallFace && baseLayer >= 0) {
				const Rgba *base = (const Rgba *)&out.rgba[(size_t)baseLayer * layerBytes];
				for (int i = 0; i < n; i++)
					if (!img[i].a)
						img[i] = base[i];
			}
			// Un mur plein ne doit laisser passer aucun jour (le jeu y dessinait le sol, en bas) : chaque
			// pixel transparent prend la couleur du pixel opaque au-dessus de lui.
			if (!seeThrough && out.kind[t] == kWallFace) {
				for (int x = 0; x < kDunTexSize; x++) {
					Rgba last = {0, 0, 0, 255};
					for (int y = 0; y < kDunWallRows; y++) {
						Rgba &cc = img[y * kDunTexSize + x];
						if (cc.a)
							last = cc;
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
					const float r = (float)kDunTexSize / (float)(c1 - c0 + 1);
					if (dc1 >= dc0 && seeThrough && r > 1.05f && r < 3.5f && foot - dtop > 20) {
						out.planeDepth[t] = r;
						out.planeU0[t] = (float)c0 / kDunTexSize;
						out.planeU1[t] = (float)(c1 + 1) / kDunTexSize;
						out.planeTop[t] = (float)dtop / kDunTexSize;
						out.planeFoot[t] = (float)(foot + 1) / kDunTexSize;
					}
				}
			}
		}
		if (baseLayer >= 0)
			for (uint8_t pt : pendingBase)
				out.typeLayer[pt] = baseLayer;
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

	for (int b = 0; b < 1024; b++) {
		float cx, cz;
		blockCenter(b, cx, cz);
		const bool solid = isSolidBlock(walls, b, g);
		const uint8_t *w = walls + b * 4;
		bool objectDone = false, decalDone = false;
		for (int k = 0; k < 4; k++) {
			uint8_t t = w[k];
			if (!t)
				continue;
			// Un objet au milieu de la case : deux images croisées, visibles des deux côtés.
			if (tex.kind[t] == kWallObject) {
				if (!objectDone && tex.typeLayer[t] >= 0) {
					objectDone = true;
					uint8_t tn = w[0] && tex.kind[w[0]] == kWallObject ? w[0] : t;
					uint8_t te = w[1] && tex.kind[w[1]] == kWallObject ? w[1] : t;
					upright(cx, cz, 0.0f, 1.0f, (float)tex.typeLayer[tn], 1.0f, 1.0f, true);
					upright(cx, cz, 1.0f, 0.0f, (float)tex.typeLayer[te], 0.9f, 1.0f, true);
				}
				continue;
			}
			// Un dessin au sol, posé juste au-dessus des dalles.
			if (tex.kind[t] == kWallDecal) {
				if (!decalDone && tex.decalLayer[t] >= 0) {
					decalDone = true;
					float x0 = cx - S * 0.5f, x1 = cx + S * 0.5f, z0 = cz - S * 0.5f, z1 = cz + S * 0.5f, y = 0.015f;
					// Coins nord-ouest, nord-est, sud-est, sud-ouest ; le haut de l'image (le bord lointain)
					// est du côté où regarde le groupe.
					const float cxs[4] = {x0, x1, x1, x0}, czs[4] = {z0, z0, z1, z1};
					const int r = viewDir & 3;
					float a[5] = {cxs[r], y, czs[r], 0, 0}, d[5] = {cxs[(r + 1) & 3], y, czs[(r + 1) & 3], 1, 0};
					float c[5] = {cxs[(r + 2) & 3], y, czs[(r + 2) & 3], 1, 1}, bb[5] = {cxs[(r + 3) & 3], y, czs[(r + 3) & 3], 0, 1};
					quad(a, bb, c, d, (float)tex.decalLayer[t], 0.95f);
				}
				continue;
			}
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
			// Un mur n'est visible que depuis la case voisine, si l'on peut s'y tenir.
			int bx = (b & 31) + dx[k], bz = (b >> 5) + dz[k];
			if (bx < 0 || bx > 31 || bz < 0 || bz > 31)
				continue;
			if (isSolidBlock(walls, bz * 32 + bx, g))
				continue;
			// Normale sortante n ; vu de la case voisine, la droite du spectateur est (n.z, 0, -n.x).
			float nx = (float)dx[k], nz = (float)dz[k];
			float fx = cx + nx * S * 0.5f, fz = cz + nz * S * 0.5f;
			float layer = tex.typeLayer[t] >= 0 ? typeLayer : (float)tex.pageLayer[page];
			if (tex.planeDepth[t] > 0.0f && tex.typeLayer[t] >= 0) {
				// La porte dans son renfoncement : le cadre (les piliers) et la porte, au fond, sur toute
				// la largeur et toute la hauteur de la case ; les côtés du renfoncement sont les vrais
				// murs des cases voisines (la case de la porte est un passage).
				const float r = tex.planeDepth[t], rx = nz, rz = -nx;
				for (int pass = 0; pass < 2; pass++) {
					const float lyr = pass ? (float)tex.doorLayer[t] : layer;
					const float off = pass ? 0.004f : 0.0f; // la porte un rien devant le cadre
					const float px = fx - nx * ((r - 1.0f) * S * 0.5f - off), pz = fz - nz * ((r - 1.0f) * S * 0.5f - off);
					float bl[5] = {px - rx * S * 0.5f, 0.0f, pz - rz * S * 0.5f, tex.planeU0[t], tex.planeFoot[t]};
					float br[5] = {px + rx * S * 0.5f, 0.0f, pz + rz * S * 0.5f, tex.planeU1[t], tex.planeFoot[t]};
					float tr[5] = {px + rx * S * 0.5f, H, pz + rz * S * 0.5f, tex.planeU1[t], tex.planeTop[t]};
					float tl[5] = {px - rx * S * 0.5f, H, pz - rz * S * 0.5f, tex.planeU0[t], tex.planeTop[t]};
					quad(bl, br, tr, tl, lyr, faceShade[k]);
				}
				continue;
			}
			float fr = tex.faceDepth[t];
			upright(fx - nx * (fr - 1.0f) * S * 0.5f, fz - nz * (fr - 1.0f) * S * 0.5f, nx, nz, layer, faceShade[k], fr, false);
			// La porte se dresse plus loin dans la case, à la profondeur où le jeu la dessine.
			if (tex.doorLayer[t] >= 0) {
				float r = tex.doorDepth[t];
				float ddx = fx - nx * (r - 1.0f) * S * 0.5f, ddz = fz - nz * (r - 1.0f) * S * 0.5f;
				upright(ddx, ddz, nx, nz, (float)tex.doorLayer[t], faceShade[k], r, false);
			}
		}
		if (!solid) {
			float x0 = cx - S * 0.5f, x1 = cx + S * 0.5f, z0 = cz - S * 0.5f, z1 = cz + S * 0.5f;
			if (tex.floorLayer >= 0) {
				float a[5] = {x0, 0, z0, 0, 0}, bb[5] = {x0, 0, z1, 0, 1}, c[5] = {x1, 0, z1, 1, 1}, d[5] = {x1, 0, z0, 1, 0};
				quad(a, bb, c, d, (float)tex.floorLayer, 0.9f);
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
