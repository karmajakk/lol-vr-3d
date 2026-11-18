// lol-vr — rendu du décor 3D autour de l'écran du jeu (OpenGL ES 3, indépendant de la plateforme).
#pragma once

#include "vecmath.h"

namespace lolvr {

enum Environment {
	kEnvLibrary = 0,          // bibliothèque médiévale, écran dans un cadre ouvragé
	kEnvPassthroughFrame = 1, // la vraie pièce du joueur, écran dans le cadre
	kEnvPassthroughCrt = 2,   // la vraie pièce du joueur, moniteur cathodique
	kEnvCount
};

struct HandState {
	bool visible = false;
	Vec3 pos = {0, 0, 0}; // pose de visée de la manette
	Quat rot = {0, 0, 0, 1};
	float trigger = 0.0f;
	float grip = 0.0f;
	bool pointing = false; // cette main dirige le pointeur
};

struct SceneState {
	int environment = kEnvLibrary;
	int theme = 0;
	double time = 0.0;

	// Écran du jeu : centre, orientation, largeur de l'image en mètres, rapport hauteur / largeur
	// (0,75 pour l'image entière ; autre valeur quand seule la vue du donjon est affichée).
	Vec3 screenPos = {0, 0, -2.2f};
	Quat screenRot = {0, 0, 0, 1};
	float screenWidth = 2.4f;
	float screenAspect = 0.75f;

	// Origine de la pièce (au sol, sous le joueur) et son orientation.
	Vec3 roomPos = {0, -1.2f, 0};
	float roomYaw = 0.0f;

	HandState hands[2];
	bool hitValid = false;
	Vec3 hit = {0, 0, 0};
	float clickPulse = 0.0f; // 1 au moment du clic, retombe vers 0

	Vec3 screenGlow = {0.1f, 0.1f, 0.1f}; // couleur moyenne de l'image du jeu (linéaire)
	unsigned gameTexture = 0;             // texture OpenGL de l'image du jeu (mode cathodique)
	int gameWidth = 320, gameHeight = 200;

	// Panneaux flottants : une portion d'image (celle du jeu, ou une texture à part) agrandie avec
	// des pixels nets, dans une bordure en laiton. Repère : right/up unitaires, demi-dimensions en mètres.
	struct Panel {
		bool visible = false;
		float alpha = 1.0f;
		Vec3 center = {0, 0, 0}, right = {1, 0, 0}, up = {0, 1, 0};
		float halfW = 0.1f, halfH = 0.05f;
		float u0 = 0, v0 = 0, u1 = 1, v1 = 1; // portion de l'image (0..1, v vers le bas)
		unsigned texture = 0;                 // 0 = image du jeu
		int texW = 0, texH = 0;
		float border = 0.009f;                // largeur de la bordure, en mètres
		float hot[4] = {0, 0, 0, 0};          // zone mise en valeur (u0, v0, u1, v1), vide = aucune
		bool hotOutline = false;              // la zone mise en valeur n'est qu'encadrée (pas de voile)
		bool blank = false;                   // fenêtre éteinte : du noir à la place de l'image
	};
	static const int kMaxPanels = 12;
	Panel panels[kMaxPanels];

	// Poignées en laiton : on les attrape pour déplacer un panneau ou l'écran.
	struct Handle {
		bool visible = false;
		float alpha = 1.0f;
		Vec3 center = {0, 0, 0}, right = {1, 0, 0}, up = {0, 1, 0};
		float halfW = 0.05f, halfH = 0.012f;
		float glow = 0.0f; // 1 = visée ou tenue
	};
	static const int kMaxHandles = 12;
	Handle handles[kMaxHandles];

	// Lueur du pointeur : repère du plan touché (par défaut, celui de l'écran).
	Vec3 hitRight = {0, 0, 0}, hitUp = {0, 0, 0};
	float hitRadius = 0.0f; // 0 = valeurs par défaut
	bool pointerLong = false; // rayon entier (le doigt ne vise ni l'écran ni un panneau)

	// Donjon en 3D (lol-vr-3d) : remplace la pièce autour du joueur quand il est affiché.
	bool dungeonVisible = false;
	bool screenVisible = true; // le grand écran (et son cadre) est affiché
	Mat4 dungeonModel = identity(); // repère du niveau -> monde
	float dungeonLight = 1.0f;
};

class Dungeon;

class Scene {
public:
	Scene();
	~Scene();

	// srgbTarget : vrai si l'image de destination est au format sRGB.
	bool init(bool srgbTarget);
	// Image fournie par le joueur. slot 0 : vue par la fenêtre ; slot 1 : portrait au-dessus de la
	// cheminée ; slots 2 et 3 : deux autres tableaux. rgba : w x h pixels, première ligne en haut. À appeler après init(), sur le fil OpenGL.
	void setImage(int slot, const unsigned char *rgba, int w, int h);
	// À appeler une fois par image, avant render() : thèmes, objets animés.
	void prepare(const SceneState &state);
	// Dessine une vue. Le tampon de destination (couleur + profondeur) doit être lié et le
	// viewport réglé ; la fonction l'efface elle-même. Le résultat est en alpha prémultiplié :
	// alpha 0 = transparent (image du jeu en dessous, ou passthrough).
	void render(const Mat4 &view, const Mat4 &proj, const SceneState &state);

	// Vrai quand l'image du jeu doit être affichée par un calque « quad » sous le décor.
	static bool usesQuadLayer(int environment) { return environment != kEnvPassthroughCrt; }
	// Vrai quand les textures sont prêtes (le décor apparaît alors en fondu).
	bool ready() const;
	// Le donjon en 3D, dessiné à la place de la pièce quand state.dungeonVisible est vrai.
	void setDungeon(const Dungeon *dungeon);

private:
	struct Impl;
	Impl *d;
};

} // namespace lolvr
