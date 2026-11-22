// lol-vr-3d — le donjon en 3D : textures des murs, du sol et du plafond, et géométrie du niveau,
// à partir des graphismes du jeu (fournis par le moteur, ou lus dans les fichiers pour les tests).
// Aucun appel OpenGL ici : tout est calculé sur le processeur.
#pragma once

#include <stdint.h>
#include <vector>

#include "vecmath.h"

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
	// Une porte entre fermée et ouverte : de combien de lignes (sur 96) elle est levée ; son dessin
	// (doorPixels) la montre fermée.
	const uint8_t *doorRaise = nullptr; // 256
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
	float holeHalf[256];
	float plateR[256];         // une dalle à presser (ronde) : son rayon (fraction de la case), 0 sinon
	int plateLayer[256];       // son dessus, vu d'en haut (couche), -1 sinon
	bool platePressed[256];    // la dalle enfoncée (le même dessin, un pixel plus bas)
	int plateFoot[256];       // un dessin au sol qui est un trou (une fosse) : demi-côté du puits (0..0.5 de la case), 0 sinon
	// Une porte dans son renfoncement : le jeu peint les côtés du renfoncement en perspective, puis,
	// au fond, la porte et les piliers qui la bordent, sur toute la largeur de la case. Ce plan du
	// fond est remis à sa vraie place : à cette profondeur (0 = pas de plan), colonnes u0..u1 et
	// lignes top..foot de l'image (fractions de 128) sur toute la largeur et toute la hauteur.
	float planeDepth[256];
	float planeU0[256], planeU1[256], planeTop[256], planeFoot[256];
	// Les côtés du renfoncement : pour chaque colonne de l'image, la ligne sous son dernier pixel
	// (0 = colonne vide) ; ce pied est posé au sol, pour que montants et piliers y touchent.
	uint8_t planeColFoot[256][kDunTexSize];
	// Une image agrandie à tout le mur : où elle était dans le dessin du jeu (x, y, largeur, hauteur ;
	// largeur 0 = pas agrandie). Un clic sur le mur se ramène là, où le jeu l'attend.
	int16_t picBox[256][4];
	// Une ouverture jusqu'au sol dans un mur (une sortie, une porte de boutique) : chaque colonne
	// posée au sol par son pied (planeColFoot).
	bool footFit[256];
	bool scenery[256];         // un objet qui est en fait un décor entier (la sortie d'une grotte) : dressé au fond de la case
	// Un mur que le jeu ne dessine pas (invisible et franchissable) : ni maillage ni clic.
	bool hidden[256];
	// La porte dans son dessin (fraction de la texture, en hauteur) : son haut et son pied ; et une
	// porte qui s'ouvre ou se ferme : la part de sa hauteur dont elle est levée (0 fermée, 1 ouverte).
	float doorTopV[256], doorFootV[256], doorLift[256];
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
// viewDir : direction du groupe (0 nord .. 3 ouest). Le jeu montre un dessin au sol toujours de la
// même façon, d'où qu'on le regarde : il est tourné vers le groupe.
void buildDungeonMesh(const uint8_t *walls, const DungeonGfx &gfx, const DungeonTextures &tex, std::vector<DungeonVertex> &out, int viewDir = 0);

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
// L'image d'un lieu (176 x 120, RVB) à la place de la petite image d'un type de mur, sur tout le mur.
void applyPlacePicture(DungeonTextures &tex, int type, const uint8_t *rgb);

bool isHoldBlock(const uint8_t *walls, int block, const DungeonGfx &gfx);

// Ce que vise un rayon dans le donjon (voir pickDungeon) : px, py dans l'écran du jeu (-1 : rien),
// face = mur visé (0 nord .. 3 ouest, -1 sol ou au loin), le point touché et l'orientation de la
// surface (coordonnées du niveau).
struct DungeonPick {
	float px = -1.0f, py = -1.0f;
	int face = -1;
	Vec3 hit{0, 0, 0}, forward{0, 0, 1}, right{1, 0, 0};
};
// Où le jeu dessine un point du sol dans sa vue (320 x 200) ; faux s'il est hors de la vue.
bool floorGamePoint(int block, int direction, float x, float z, float &px, float &py);
bool pickDungeon(const uint8_t *walls, int block, int direction, const DungeonTextures &tex, Vec3 o, Vec3 v, DungeonPick &out);

} // namespace lolvr
