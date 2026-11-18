// lol-vr-3d — le donjon en 3D : textures des murs, du sol et du plafond, et géométrie du niveau,
// à partir des graphismes du jeu (fournis par le moteur, ou lus dans les fichiers pour les tests).
// Aucun appel OpenGL ici : tout est calculé sur le processeur.
#pragma once

#include <stdint.h>
#include <vector>

namespace lolvr {

// Taille d'un bloc du donjon en mètres, et hauteur sous plafond. Les murs du jeu font 128 x 96
// pixels, avec des pixels 1,2 fois plus hauts que larges : hauteur = 0,9 x largeur.
const float kBlockSize = 3.0f;
const float kWallHeight = 2.7f;

// Couches de texture : 128 x 128 pixels, RGBA. Un mur occupe les 96 premières lignes.
const int kDunTexSize = 128;
const int kDunWallRows = 96;

// Graphismes du niveau, tels que le moteur les a chargés (voir LolvrLevelData dans le patch du moteur).
struct DungeonGfx {
	const uint8_t *vmpMap = nullptr;    // 256 : type de mur -> page VMP (0 = rien)
	const uint8_t *wallFlags = nullptr; // 256
	const uint16_t *vmp = nullptr;
	int vmpLen = 0;
	const uint8_t *vcn = nullptr;       // blocs 8x8, 4 bits par pixel
	const uint8_t *vcnShift = nullptr;
	int vcnCount = 0;
	const uint8_t *colTable = nullptr;  // 128
	const uint8_t *palette = nullptr;   // 128 couleurs, VGA 6 bits
};

struct DungeonTextures {
	int layers = 0;
	std::vector<uint8_t> rgba; // layers x 128 x 128 x 4, première ligne en haut
	int pageLayer[64];         // page VMP -> couche (-1 = aucune)
	int floorLayer = -1, ceilingLayer = -1;
};

// Décode les murs de chaque page VMP (vus de face, à un pas), et fabrique le sol et le plafond à
// partir de l'image de fond du jeu.
void decodeDungeonTextures(const DungeonGfx &gfx, DungeonTextures &out);

// Sommet du donjon : position (mètres, repère du niveau : x vers l'est, z vers le sud, y vers le
// haut, origine au coin nord-ouest du bloc 0, au sol), uv + couche, éclairage relatif.
struct DungeonVertex {
	float pos[3];
	float uvl[3];
	float shade;
};

// Construit les murs, le sol et le plafond. walls : 1024 blocs x 4 côtés (N, E, S, O).
void buildDungeonMesh(const uint8_t *walls, const DungeonGfx &gfx, const DungeonTextures &tex, std::vector<DungeonVertex> &out);

// Centre d'un bloc, dans le repère du niveau.
inline void blockCenter(int block, float &x, float &z) {
	x = ((float)(block & 31) + 0.5f) * kBlockSize;
	z = ((float)(block >> 5) + 0.5f) * kBlockSize;
}

// Un bloc dans lequel on ne peut pas entrer (des murs pleins des quatre côtés).
bool isSolidBlock(const uint8_t *walls, int block, const DungeonGfx &gfx);

} // namespace lolvr
