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

class Dungeon {
public:
	Dungeon();
	~Dungeon();

	// srgbTarget : vrai si l'image de destination est au format sRGB.
	bool init(bool srgbTarget);
	void setTextures(const DungeonTextures &tex);
	void setMesh(const std::vector<DungeonVertex> &verts);
	bool hasContent() const;
	// model : repère du niveau -> monde. eye : position de l'œil (monde), pour la pénombre.
	// light : 1 = éclairage normal (le jeu peut assombrir certaines zones).
	void render(const Mat4 &viewProj, const Mat4 &model, Vec3 eye, float light) const;

private:
	struct Impl;
	Impl *d;
};

} // namespace lolvr
