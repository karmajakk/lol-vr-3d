// lol-vr — la bibliothèque médiévale et les objets attachés à l'écran et aux mains.
#pragma once

#include <vector>

#include "mesh.h"

namespace lolvr {

// Dimensions de la pièce (repère : origine au sol sous le joueur, -z vers l'écran, y vers le haut).
const float kRoomHalfX = 2.7f;
const float kRoomFrontZ = -3.5f;
const float kRoomBackZ = 2.7f;
const float kRoomHeight = 3.3f;

enum LightKind { kLightWarm = 0, kLightWindow = 1, kLightFire = 2 };

struct RoomLight {
	Vec3 pos;
	Vec3 color; // linéaire
	float radius;
	float flicker; // 0 = fixe
	int kind;
};

struct RoomGlow {
	Vec3 pos;
	Vec3 color;
	float size;
};

// Ombre de contact au sol sous un meuble.
struct RoomShadow {
	Vec3 pos;
	float sx, sz;
	float strength;
};

struct RoomInfo {
	std::vector<RoomLight> lights;
	std::vector<RoomShadow> shadows;
	std::vector<Vec3> candleFlames;
	std::vector<RoomGlow> glows;
	Vec3 firePos = {0, 0, 0};
	Vec3 windowCenter = {0, 0, 0};
	float windowWidth = 0, windowHeight = 0;
};

// Images fournies par le joueur (fenêtre, portrait au-dessus de la cheminée).
struct RoomOptions {
	bool windowImage = false;
	float windowAspect = 1.6f; // largeur / hauteur
	bool portraitImage = false;
	float portraitAspect = 1.5f;
	// Quatre autres tableaux au choix (deux au mur de gauche, deux au mur du fond)
	bool paintingImage[4] = {false, false, false, false};
	float paintingAspect[4] = {1.3f, 1.3f, 1.3f, 1.3f};
};

// Construit la pièce. `solid` reçoit la géométrie opaque, `alpha` les parties translucides (vitrail).
void buildRoom(MeshBuilder &solid, MeshBuilder &alpha, RoomInfo &info, int theme, const RoomOptions &options);

// Cadre ouvragé autour de l'écran. Repère de l'écran : image de largeur 1 et de hauteur `aspect`
// (0,75 pour l'image entière) centrée dans le plan z = 0, regardant vers +z.
void buildScreenFrame(MeshBuilder &mb, float aspect = 0.75f);

// Moniteur cathodique des années 90 (même repère). `glass` reçoit la dalle bombée ; ses uv
// (0..1) désignent l'image du jeu.
void buildCrt(MeshBuilder &body, MeshBuilder &glass);
// Position de la diode du moniteur (repère de l'écran).
Vec3 crtLedPosition();
// Relief de la dalle bombée au point (x, y) de l'image (repère de l'écran, largeur 1).
float crtGlassZ(float x, float y);

// Gant médiéval dans le repère de visée de la manette (-z = direction pointée, +y = haut).
// trigger et grip (0..1) replient l'index et les autres doigts.
void buildGlove(MeshBuilder &mb, bool leftHand, float trigger, float grip);
// Bout de l'index tendu (repère de visée), pour faire partir le pointeur du doigt.
Vec3 gloveFingertip(bool leftHand, float trigger);

// Bas du moniteur cathodique et de l'ordinateur posé dessous (repère de l'écran, largeur 1).
const float kCrtBottomY = -1.02f;

} // namespace lolvr
