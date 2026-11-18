// lol-vr-3d — rendu du donjon en 3D (OpenGL ES 3).
#pragma once

#include <vector>

#include "dungeon_data.h"
#include "vecmath.h"

namespace lolvr {

// Repère du niveau -> monde. Le joueur se tient au point (partyX, partyZ) du niveau (le centre de la
// case du groupe), le regard dans la direction du groupe (partyYaw : 0 nord, pi/2 est, pi sud...).
// Dans le monde, ce point est floorPos (au sol) et ce regard est worldYaw.
inline Mat4 dungeonModel(Vec3 floorPos, float worldYaw, float partyX, float partyZ, float partyYaw) {
	return multiply(poseMatrix(floorPos, quatYaw(worldYaw + partyYaw)), translation({-partyX, 0.0f, -partyZ}));
}

// Monstres et objets : des images toujours tournées vers le joueur (comme dans le jeu).
const int kSpriteTexW = 176, kSpriteTexH = 128, kMaxSprites = 32;
struct DungeonSprite {
	float x = 0, z = 0;            // pied de l'image, dans le repère du niveau
	bool ceiling = false;          // accroché au plafond : le haut de l'image touche le plafond
	int layer = 0;                 // image (setSpriteImage)
	float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
	float width = 1, height = 1;   // mètres
	float offset = 0;              // décalage latéral de l'image, mètres
};

class Dungeon {
public:
	Dungeon();
	~Dungeon();

	// srgbTarget : vrai si l'image de destination est au format sRGB.
	bool init(bool srgbTarget);
	void setTextures(const DungeonTextures &tex);
	void setMesh(const std::vector<DungeonVertex> &verts);
	// Image d'un monstre ou d'un objet : kSpriteTexW x kSpriteTexH pixels RGBA (alpha 0 = transparent).
	void setSpriteImage(int layer, const uint8_t *rgba);
	void setSprites(const std::vector<DungeonSprite> &sprites);
	bool hasContent() const;
	// model : repère du niveau -> monde. eye : position de l'œil (monde), pour la pénombre.
	// light : 1 = éclairage normal (le jeu peut assombrir certaines zones).
	void render(const Mat4 &viewProj, const Mat4 &model, Vec3 eye, float light) const;

private:
	struct Impl;
	Impl *d;
};

} // namespace lolvr
