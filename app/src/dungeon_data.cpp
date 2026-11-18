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

bool hasSolidWall(const DungeonGfx &g, uint8_t t) {
	return t != 0 && !(g.wallFlags[t] & 8);
}

} // namespace

bool isSolidBlock(const uint8_t *walls, int block, const DungeonGfx &gfx) {
	const uint8_t *w = walls + (block & 1023) * 4;
	for (int k = 0; k < 4; k++)
		if (!hasSolidWall(gfx, w[k]))
			return false;
	return true;
}

void decodeDungeonTextures(const DungeonGfx &g, DungeonTextures &out) {
	out = DungeonTextures();
	for (int i = 0; i < 64; i++)
		out.pageLayer[i] = -1;
	for (int i = 0; i < 256; i++)
		out.typeLayer[i] = -1;
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

	// Les murs dessinés par le moteur : une couche par type, décors et portes compris.
	if (g.wallSlot && g.wallPixels && g.wallAlpha && g.screenPalette && g.wallSlots > 0) {
		const int n = kDunTexSize * kDunWallRows;
		// Le mur de pierre ordinaire du niveau, pour habiller les murs pleins faits d'un seul décor.
		int plainSlot = g.wallSlot[1];
		for (int t = 1; t < 256; t++) {
			int slot = g.wallSlot[t];
			if (slot < 0 || slot >= g.wallSlots)
				continue;
			out.typeLayer[t] = out.layers++;
			out.rgba.resize(out.layers * layerBytes, 0);
			Rgba *img = (Rgba *)&out.rgba[(out.layers - 1) * layerBytes];
			const uint8_t *px = g.wallPixels + (size_t)slot * n, *al = g.wallAlpha + (size_t)slot * n;
			const bool seeThrough = (g.wallFlags[t] & 8) != 0;
			const uint8_t *bpx = (!seeThrough && plainSlot >= 0 && plainSlot < g.wallSlots) ? g.wallPixels + (size_t)plainSlot * n : nullptr;
			const uint8_t *bal = bpx ? g.wallAlpha + (size_t)plainSlot * n : nullptr;
			for (int y = 0; y < kDunWallRows; y++)
				for (int x = 0; x < kDunTexSize; x++) {
					int i = y * kDunTexSize + x;
					int c = -1;
					if (al[i])
						c = px[i];
					else if (bpx && bal[i])
						c = bpx[i];
					if (c >= 0)
						img[i] = Rgba{vga(g.screenPalette[c * 3]), vga(g.screenPalette[c * 3 + 1]), vga(g.screenPalette[c * 3 + 2]), 255};
				}
			if (!seeThrough) {
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
	}
}

void buildDungeonMesh(const uint8_t *walls, const DungeonGfx &g, const DungeonTextures &tex, std::vector<DungeonVertex> &out) {
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
	// Quad a-b-c-d, sens trigonométrique vu de face.
	auto quad = [&](const float *a, const float *b, const float *c, const float *d, float layer, float shade) {
		put(a[0], a[1], a[2], a[3], a[4], layer, shade);
		put(b[0], b[1], b[2], b[3], b[4], layer, shade);
		put(c[0], c[1], c[2], c[3], c[4], layer, shade);
		put(a[0], a[1], a[2], a[3], a[4], layer, shade);
		put(c[0], c[1], c[2], c[3], c[4], layer, shade);
		put(d[0], d[1], d[2], d[3], d[4], layer, shade);
	};
	static const int dx[4] = {0, 1, 0, -1}, dz[4] = {-1, 0, 1, 0};
	// Comme dans les jeux de l'époque, les murs est-ouest sont un peu plus sombres.
	static const float faceShade[4] = {1.0f, 0.82f, 1.0f, 0.82f};

	for (int b = 0; b < 1024; b++) {
		float cx, cz;
		blockCenter(b, cx, cz);
		const bool solid = isSolidBlock(walls, b, g);
		for (int k = 0; k < 4; k++) {
			uint8_t t = walls[b * 4 + k];
			if (!t)
				continue;
			int page = g.vmpMap[t];
			float typeLayer = (float)tex.typeLayer[t];
			if (tex.typeLayer[t] < 0 && (page <= 0 || page >= 64 || tex.pageLayer[page] < 0)) {
				// Mur que le jeu dessine seulement avec un décor (porte, grille...) : en attendant les
				// décors, un mur plein prend la pierre ordinaire du niveau ; un mur ajouré n'est pas dessiné.
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
			float rx = nz, rz = -nx;
			float fx = cx + nx * S * 0.5f, fz = cz + nz * S * 0.5f;
			float layer = tex.typeLayer[t] >= 0 ? typeLayer : (float)tex.pageLayer[page];
			float bl[5] = {fx - rx * S * 0.5f, 0.0f, fz - rz * S * 0.5f, 0.0f, vBottom};
			float br[5] = {fx + rx * S * 0.5f, 0.0f, fz + rz * S * 0.5f, 1.0f, vBottom};
			float tr[5] = {fx + rx * S * 0.5f, H, fz + rz * S * 0.5f, 1.0f, 0.0f};
			float tl[5] = {fx - rx * S * 0.5f, H, fz - rz * S * 0.5f, 0.0f, 0.0f};
			quad(bl, br, tr, tl, layer, faceShade[k]);
		}
		if (!solid) {
			float x0 = cx - S * 0.5f, x1 = cx + S * 0.5f, z0 = cz - S * 0.5f, z1 = cz + S * 0.5f;
			if (tex.floorLayer >= 0) {
				float a[5] = {x0, 0, z0, 0, 0}, bb[5] = {x0, 0, z1, 0, 1}, c[5] = {x1, 0, z1, 1, 1}, d[5] = {x1, 0, z0, 1, 0};
				quad(a, bb, c, d, (float)tex.floorLayer, 0.9f);
			}
			if (tex.ceilingLayer >= 0) {
				float a[5] = {x0, H, z0, 0, 0}, d[5] = {x1, H, z0, 1, 0}, c[5] = {x1, H, z1, 1, 1}, bb[5] = {x0, H, z1, 0, 1};
				quad(a, d, c, bb, (float)tex.ceilingLayer, 0.9f);
			}
		}
	}
}

} // namespace lolvr
