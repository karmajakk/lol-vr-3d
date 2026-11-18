// lol-vr — interface détachée : panneaux flottants (groupe, inventaire, sorts, commandes,
// boussole, atlas), clavier avec options, poignées pour tout déplacer, et calcul du pointeur.
// Indépendant de la plateforme (OpenGL ES 3 pour la texture du clavier seulement).
#pragma once

#include <stdint.h>

#include <string>

#include "scene.h"

namespace lolvr {

enum UiPanelId {
	kPanelParty = 0, // colonne de droite : atlas, lampe, bourse, puis les personnages, options et repos
	kPanelInventory, // rangée d'objets
	kPanelSpells,    // colonne de gauche : parchemin des sorts
	kPanelText,      // lignes de messages
	kPanelKeyboard,  // clavier et options
	kPanelHelp,      // aide : les manettes et les boutons
	kUiPanelCount
};

// Position d'un panneau, indépendante de l'écran : a = lacet, b = tangage, c = distance depuis
// le point d'ancrage (la place du joueur) ; scale en mètres par pixel.
struct UiPlacement {
	float a = 0.0f, b = 0.0f, c = 0.0f, scale = 0.002f;
};

// Vue du donjon seule : hauteur et décalage vers le haut, en fractions de la largeur de l'image entière.
const float kSplitViewHeight = 0.54f;
const float kSplitViewLift = 0.16f;

enum UiAction { kUiActionNone = 0, kUiActionToggleFire, kUiActionToggleDetach, kUiActionResetLayout, kUiActionCloseKeyboard, kUiActionToggleHelp, kUiActionCloseHelp, kUiActionToggleLanguage, kUiActionToggleKeyboard, kUiActionToggleDungeon, kUiActionToggleScreenAlways, kUiActionChooseClassic, kUiActionChooseImmersive };

enum UiLanguage { kLangEnglish = 0, kLangFrench = 1 };

const unsigned kUiModShift = 1;

struct UiFrameInput {
	float dt = 0.016f;
	// Ancrage
	Vec3 anchorPos = {0, 0, 0};
	float anchorYaw = 0.0f;
	// Pointeur (main active)
	bool aimValid = false;
	Vec3 tip = {0, 0, 0}, dir = {0, 0, -1};
	bool triggerDown = false, triggerPressed = false;
	float stickX = 0.0f, stickY = 0.0f; // joystick droit
	// Repère des panneaux : centre et largeur de l'image entière
	Vec3 basePos = {0, 0, -2.2f};
	float fullW = 2.4f;
	// La pièce (bibliothèque) : les panneaux ne traversent ni le plancher, ni les murs.
	bool roomClamp = false;
	Vec3 roomPos = {0, 0, 0};
	float roomYaw = 0.0f;
	// Écran principal : pose, taille affichée, portion de l'image du jeu (pixels 320x200)
	Vec3 screenPos = {0, 0, -2.2f};
	Quat screenRot = {0, 0, 0, 1};
	float dispW = 2.4f, dispH = 1.8f;
	float cropX = 0, cropY = 0, cropW = 320, cropH = 200;
	bool crt = false;
	// État du jeu
	bool split = false;       // l'interface détachée est affichée
	bool keyboardOpen = false;
	bool helpOpen = false;
	// Premier lancement : la fenêtre « vue classique ou immersive » (à la place de l'aide).
	bool chooserOpen = false;
	// Menu du poignet : un petit engrenage apparaît sur la main gauche quand on la regarde.
	bool wristValid = false;
	Vec3 wristPos = {0, 0, 0};
	Quat wristRot = {0, 0, 0, 1};
	Vec3 headPos = {0, 0, 0}, headDir = {0, 0, -1};
	// Zone à mettre en valeur dans la fenêtre du groupe (pixels de l'image du jeu ; largeur 0 = aucune) :
	// le personnage choisi pour les boutons d'attaque et de magie.
	float partyMark[4] = {0, 0, 0, 0};
	// La fiche d'un personnage occupe l'écran : la colonne de la boussole est éteinte.
	bool compassBlank = false;
	// L'image du jeu (RGBA, première ligne en haut) et son numéro : la colonne du groupe en est
	// recomposée (portraits empilés sous la colonne de la boussole).
	const uint8_t *gameRgba = nullptr;
	int gameW = 0, gameH = 0;
	uint32_t gameSerial = 0;
	int characters = 0;
	int portraitX[4] = {0, 0, 0, 0};
	// Premier lancement : un petit mot invite à regarder l'engrenage du poignet, qui bat doucement,
	// tant que le menu n'a jamais été ouvert.
	bool gearHint = false;
	// Le grand écran est caché (donjon 3D : il n'apparaît que lorsqu'on en a besoin).
	bool screenHidden = false;
	double time = 0.0;

};

struct UiFrameOutput {
	// Pointeur de la souris du jeu
	bool pointerValid = false;
	float gu = 0, gv = 0; // 0..1 dans l'image du jeu
	bool buttonsToGame = false; // les clics de cette image vont au jeu
	// Lueur du pointeur
	bool hitValid = false;
	Vec3 hit = {0, 0, 0}, hitRight = {0, 0, 0}, hitUp = {0, 0, 0};
	float hitRadius = 0.0f;
	bool pointerLong = false; // le doigt ne vise ni l'écran ni un panneau : rayon entier, pour viser les poignées
	// Déplacement à la poignée
	bool dragging = false;        // un panneau ou l'écran est tenu (le joystick droit le règle)
	bool screenDragging = false;  // … et c'est l'écran principal
	bool screenResizing = false;  // un coin de l'écran est tenu : appliquer screenZoom à sa taille
	Vec3 screenCenter = {0, 0, 0}; // où le joueur veut l'écran
	float screenZoom = 1.0f;      // facteur à appliquer à sa taille
	bool layoutChanged = false;   // à mémoriser
	bool grabbed = false, released = false; // retours haptiques
	// Clavier
	int keyCount = 0;
	unsigned keys[4] = {0, 0, 0, 0}, keyMods[4] = {0, 0, 0, 0};
	int action = kUiActionNone;
	bool gearMenuOpened = false; // le joueur a ouvert le menu du poignet (l'invitation peut disparaître)
	bool clicked = false;
};

class Ui {
public:
	Ui();
	~Ui();

	// À appeler une fois, contexte OpenGL actif.
	void initGl(bool srgb);
	// Étiquettes des boutons d'options.
	void setOptions(bool fireOn, bool detached);
	// Le donjon en 3D (menu du poignet) : 0 non, 1 oui avec l'écran au besoin, 2 oui avec l'écran toujours.
	void setDungeonOption(int mode);
	// Langue des textes (UiLanguage).
	void setLanguage(int language);
	// Deux lignes d'information affichées sous le clavier (Latin-1).
	void setInfo(const std::string &line1, const std::string &line2);
	// Disposition par défaut : autour de l'écran tel qu'il est placé à cet instant.
	void resetLayout(const UiFrameInput &in);
	bool hasLayout() const { return placed; }
	void setHasLayout(bool v) { placed = v; }

	// Une fois par image : calcule le pointeur, gère les poignées et le clavier, et remplit
	// les panneaux et poignées de la scène.
	void update(const UiFrameInput &in, UiFrameOutput &out, SceneState &scene);

	UiPlacement placement[kUiPanelCount];
	static UiPlacement defaultHelpPlacement(float yaw);
	static const char *panelKey(int id);

private:
	struct Impl;
	Impl *d;
	bool placed = false;
};

} // namespace lolvr
