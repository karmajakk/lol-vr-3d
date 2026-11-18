// lol-vr — audio spatial : ancre le son du jeu à l'écran et ajoute une ambiance sonore
// synthétisée (feu de cheminée, vent à la fenêtre, gouttes, grillons) placée dans la pièce.
// Aucun fichier son : tout est calculé à la volée.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "vecmath.h"

namespace lolvr {

class Ambience {
public:
	static Ambience &get();

	void setSampleRate(int rate);

	// --- Appelé par le fil d'affichage ---
	void setListener(Vec3 pos, Quat rot);
	void setScreen(Vec3 pos, Quat rot, float width);
	void setRoom(Vec3 pos, float yaw);
	// ambienceOn : sons d'ambiance de la bibliothèque (coupés en réalité mixte).
	void setMood(bool ambienceOn, int theme);
	// Crépitement du feu de cheminée (coupé par défaut ; le reste de l'ambiance n'est pas touché).
	void setFire(bool on);

	// --- Appelé par le fil audio ---
	// Traite sur place `frames` trames stéréo 16 bits (le son du jeu déjà mélangé).
	void process(int16_t *samples, size_t frames);

private:
	Ambience() = default;
};

} // namespace lolvr
