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
	// Facultatif : chaque type de mur dessiné par le moteur lui-même (décors et portes compris).
	const int16_t *wallSlot = nullptr;  // 256 : type -> emplacement (-1 = aucun)
	const uint8_t *wallPixels = nullptr; // emplacements x 128 x 96, indices de palette
	const uint8_t *wallAlpha = nullptr;
	int wallSlots = 0;
	const uint8_t *screenPalette = nullptr; // 256 couleurs, VGA 6 bits
	const uint8_t *doorPixels = nullptr; // la porte seule, mêmes emplacements
	const uint8_t *doorAlpha = nullptr;
	// Dehors (forêt, marais...) : pas de plafond, le ciel.
	bool outdoor = false;
};

// Comment un type de mur est montré en 3D.
enum WallKind {
	kWallFace = 0,   // un mur (avec ses décors) sur le côté du bloc
	kWallObject = 1, // un objet planté au milieu de la case (fontaine, statue...) : rien que le dessin
	kWallDecal = 2,  // un dessin au sol (flaque, dalle, trappe...)
};

struct DungeonTextures {
	int layers = 0;
	std::vector<uint8_t> rgba; // layers x 128 x 128 x 4, première ligne en haut
	int pageLayer[64];         // page VMP -> couche (-1 = aucune)
	int typeLayer[256];        // type de mur -> couche (dessin du moteur ; -1 = aucune)
	int doorLayer[256];        // type de mur -> couche de sa porte (-1 = aucune)
	float doorDepth[256];      // la porte est à cette fraction de la case (1 = sur la face, 2 = au milieu)
	float faceDepth[256];      // de même pour le mur lui-même (cadres de porte : au milieu de la case)
	int decalLayer[256];       // type de mur -> dessin au sol (-1 = aucun)
	uint8_t kind[256];         // WallKind
	int skyLayer = -1;
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

// Monstres et objets dessinés par le moteur (image de 176 x 120, l'objet centré en x = 88) :
// conversion en image RGBA de 176 x 128, et taille dans le donjon. Un pixel du jeu dessiné en
// grand correspond à ce qu'on voit tout près, au bas de la vue : environ S / 170 mètres de large,
// 1,2 fois plus haut (pixels du jeu).
const float kSpritePixel = kBlockSize / 170.0f;
void spriteToRgba(const uint8_t *pixels, const uint8_t *alpha, const uint8_t *palette768, uint8_t *rgba176x128);

// Centre d'un bloc, dans le repère du niveau.
inline void blockCenter(int block, float &x, float &z) {
	x = ((float)(block & 31) + 0.5f) * kBlockSize;
	z = ((float)(block >> 5) + 0.5f) * kBlockSize;
}

// Un bloc dans lequel on ne peut pas entrer (des murs pleins des quatre côtés).
bool isSolidBlock(const uint8_t *walls, int block, const DungeonGfx &gfx);

// Une case où le groupe est entré en franchissant un mur dessiné : porte d'une boutique, entrée
// d'une autre région. La vue 3D reste devant (le jeu montre alors la scène, ou change de région).
bool isHoldBlock(const uint8_t *walls, int block, const DungeonGfx &gfx);

} // namespace lolvr
