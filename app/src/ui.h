// lol-vr — interface détachée : panneaux flottants (groupe, inventaire, sorts, commandes,
// boussole, atlas), clavier avec options, poignées pour tout déplacer, et calcul du pointeur.
// Indépendant de la plateforme (OpenGL ES 3 pour la texture du clavier seulement).
#pragma once

#include <stdint.h>

#include <string>
#include <vector>

#include "scene.h"

namespace lolvr {

enum UiPanelId {
	kPanelParty = 0, // à droite : les personnages, l'atlas (lampe, bourse), dessous les options et le repos
	kPanelInventory, // rangée d'objets
	kPanelSpells,    // à gauche : parchemin des sorts, et dessous les lignes de messages
	kPanelAtlas,     // l'atlas : la carte du niveau, à jour à chaque pas (repliable)
	kPanelColumn,    // la colonne de la boussole détachée du livre de sorts (atlas, lampe, bourse)
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
// La disquette du poignet, dans le repère de la manette gauche (visée) : sur le dessus de la main
// (-X : le dos de la main gauche), vers le poignet (+Z).
// Les boutons des poignets (repère de visée de la main droite ; la gauche en miroir) : sur le côté du
// bracelet de laiton du gant, côté pouce, tournés vers le haut et un peu vers l'avant-bras.
const Vec3 kWristFloppy = {0.016f, -0.0045f, 0.1447f}; // (un peu en saillie, hors du bracelet)
// Les boutons des poignets sont côté intérieur (paume) : +X à gauche, -X à droite.
inline Vec3 wristButtonOffset(bool left) { return {left ? -kWristFloppy.x : kWristFloppy.x, kWristFloppy.y, kWristFloppy.z}; }
inline Vec3 wristButtonNormal() { return {0.0f, 0.941f, 0.339f}; }

enum UiAction { kUiActionNone = 0, kUiActionToggleFire, kUiActionToggleDetach, kUiActionResetLayout, kUiActionCloseKeyboard, kUiActionToggleHelp, kUiActionCloseHelp, kUiActionToggleLanguage, kUiActionToggleKeyboard, kUiActionToggleDungeon, kUiActionToggleScreenAlways, kUiActionChooseClassic, kUiActionChooseImmersive, kUiActionGameMenu, kUiActionCycleView, kUiActionChooseLanguage, kUiActionToggleAtlas, kUiActionOpenKeyboard, kUiActionAtlasPrev, kUiActionAtlasNext, kUiActionToggleHud, kUiActionCycleCinema, kUiActionToggleCinemaIntro };

// Les boutons qu'on peut changer dans un profil de manettes, et ce que chacun peut faire. La gâchette,
// les joysticks (marcher, tourner), le bouton menu et la disquette restent fixes.
enum UiButtonSlot { kSlotA = 0, kSlotB, kSlotX, kSlotY, kSlotGrip, kSlotL3, kSlotR3, kSlotCount };
enum UiButtonFn {
	kFnNone = 0,
	kFnAttack,     // attaquer (personnage encadré) ; hors combat : Entrée
	kFnMagic,      // magie du personnage encadré ; hors combat : Échap
	kFnRightClick, // clic droit (boire, utiliser)
	kFnMap,        // la carte du jeu
	kFnRecenter,   // recentrer ; menu de magie ouvert : niveau de sort suivant
	kFnView,       // vue suivante ; maintenu : taille et distance de l'écran
	kFnEnter,
	kFnEscape,
	kFnAtlas,      // replier ou ouvrir la fenêtre de l'atlas
	kFnNextChar,   // personnage suivant
	kFnKeyboard,   // ouvrir ou fermer le clavier
	kFnCount
};

enum UiLanguage { kLangEnglish = 0, kLangFrench = 1, kLangGerman = 2 };

const unsigned kUiModShift = 1;

// L'atlas du niveau, tel que le groupe l'a exploré (pour la fenêtre de la carte).
struct UiMap {
	uint32_t serial = 0;       // change avec la carte, les murs ou la place du groupe
	bool available = false;    // le groupe a la carte
	int block = 0, direction = 0;
	const uint8_t *flags = nullptr; // 1024 cases : bits 0-2 posés = case vue
	const uint8_t *cls = nullptr;   // 256 types de mur : 0xC0 plein, 5 bits bas un symbole (0x1F aucun)
	const uint8_t *walls = nullptr; // 1024 x 4 : le mur de chaque côté (N, E, S, O) de chaque case
	std::string title;              // le nom du niveau (page de code du jeu)
	struct Mark {
		int trigger = 0, mark = 0xFFFF;
		std::string text;
	};
	std::vector<Mark> legend;       // les lieux nommés : affichés quand leur case a été vue
	const uint8_t *rgb = nullptr;   // la page de l'atlas dessinée par le jeu (320 x 200, RVB)
};

struct UiFrameInput {
	float dt = 0.016f;
	// Ancrage
	Vec3 anchorPos = {0, 0, 0};
	float anchorYaw = 0.0f;
	// Pointeur (main active)
	bool aimValid = false;
	Vec3 tip = {0, 0, 0}, dir = {0, 0, -1};
	bool triggerDown = false, triggerPressed = false;
	// Gâchette maintenue sur une fenêtre ou l'écran : on la déplace (plus de poignées) ; un appui
	// bref est un clic, envoyé au relâcher.
	bool holdToMove = false;
	// Le bout de l'index de chaque main : on tape au clavier en touchant ses touches.
	Vec3 poke[2] = {{0, 0, 0}, {0, 0, 0}};
	bool pokeValid[2] = {false, false};
	bool spellsCovered = false; // une scène large couvre le parchemin des sorts : garder l'image d'avant
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
	float bandH = 0; // ligne de messages collée sous la vue (hauteur en pixels du jeu, à la largeur de la vue)
	// Ce que montre cette bande dans l'image du jeu (x, y, largeur, hauteur) : la ligne de messages,
	// ou toute la boîte de dialogue agrandie (avec le portrait de celui qui parle).
	float bandRect[4] = {83, 122, 234, 20};
	bool crt = false;
	// État du jeu
	bool split = false;       // l'interface détachée est affichée
	bool keyboardOpen = false;
	bool helpOpen = false;
	// Premier lancement : la fenêtre « vue classique ou immersive » (à la place de l'aide).
	bool chooserOpen = false;
	// Poignet : une disquette apparaît sur la main gauche quand on la regarde ; elle ouvre le menu du jeu.
	bool wristValid = false;
	Vec3 wristPos = {0, 0, 0};
	Quat wristRot = {0, 0, 0, 1};
	// Main droite (manette droite, visée) : le repos (ZZZ) apparaît sur son dessus tourné vers soi.
	bool palmValid = false;
	Vec3 palmPos = {0, 0, 0};
	Quat palmRot = {0, 0, 0, 1};
	Vec3 headPos = {0, 0, 0}, headDir = {0, 0, -1};
	// Zone à mettre en valeur dans la fenêtre du groupe (pixels de l'image du jeu ; largeur 0 = aucune) :
	// le personnage choisi pour les boutons d'attaque et de magie.
	float partyMark[4] = {0, 0, 0, 0};
	// Le niveau de sort en surbrillance dans le menu de magie ouvert (rectangle de l'image du jeu)
	float magicMark[4] = {0, 0, 0, 0};
	// La fiche d'un personnage occupe l'écran : la colonne de la boussole est éteinte.
	bool compassBlank = false;
	// Le jeu a agrandi sa zone de texte par-dessus les portraits (remarque, conversation) : les
	// fenêtres des personnages et des sorts gardent l'image d'avant (la boîte a sa propre fenêtre).
	bool textBoxActive = false;
	bool gameModal = false; // un menu du jeu (ou la défaite) couvre l'image : les fenêtres gardent celle d'avant
	bool partyMarkOff = false;    // la zone de texte du jeu couvre les portraits : pas de cadre jaune
	// L'image du jeu (RGBA, première ligne en haut) et son numéro : la colonne du groupe en est
	// recomposée (portraits empilés sous la colonne de la boussole).
	// L'atlas du jeu ouvert depuis la fenêtre d'atlas (ses flèches) : la fenêtre montre l'écran du jeu
	// (sa page, 320 x 200 agrandie deux fois) et lui passe les clics.
	bool atlasLive = false;
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
	// Mode cinéma : l'écran est courbe (un cylindre de ce rayon autour du joueur ; 0 = plat) et ne se
	// déplace ni ne se redimensionne à la main (sa taille est un réglage).
	float curveRadius = 0.0f;
	bool screenLocked = false;
	// Mode cinéma en partie : les fenêtres du jeu tournent avec l'écran (cinemaYaw, radians) et
	// descendent au besoin sous son bord du bas (cinemaBottom : élévation vue de anchorPos, radians).
	// L'atlas est caché. Elles ne se déplacent pas à la main pendant ce temps.
	bool cinemaOn = false;
	float cinemaYaw = 0.0f, cinemaBottom = 0.0f;
	// L'atlas : sa fenêtre est montrée (réglage) et la carte (nulle : pas de fenêtre).
	bool atlasOn = false;
	const UiMap *map = nullptr;
	// Les 48 cases d'inventaire dessinées par le jeu (8 par rangée, 6 rangées, 176 x 132, RVB) : la
	// fenêtre d'inventaire de l'application (barre rapide + grille). Nul : la bande du jeu.
	const uint8_t *invRgb = nullptr;
	uint32_t invSerial = 0;
	double time = 0.0;

};

struct UiFrameOutput {
	// Pointeur de la souris du jeu
	bool pointerValid = false;
	int language = -1; // kUiActionChooseLanguage : la langue choisie (UiLanguage)
	float gu = 0, gv = 0; // 0..1 dans l'image du jeu
	bool buttonsToGame = false; // les clics de cette image vont au jeu
	// Lueur du pointeur
	bool hitValid = false;
	Vec3 hit = {0, 0, 0}, hitRight = {0, 0, 0}, hitUp = {0, 0, 0};
	float hitRadius = 0.0f;
	float hitPixel = 0.0f;    // l'écran ou une fenêtre du jeu visé : mètres par pixel du jeu (0 : autre chose)
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
	int invSlot = -1;      // la case d'inventaire cliquée (0..47)
	bool invSort = false;  // le bouton de tri a été cliqué
	// Le bloc des personnages (la ligne de messages au-dessus) tel qu'il est posé : la bande de texte
	// du jeu (une conversation) vient s'y poser, à l'endroit où le jeu la dessine.
	bool partyBlockValid = false;
	Vec3 partyBlockTopLeft = {0, 0, 0}, partyBlockRight = {1, 0, 0}, partyBlockUp = {0, 1, 0};
	float partyBlockPixel = 0.0f;                // mètres par pixel du jeu (en largeur)
	float partyBlockGameX = 0.0f, partyBlockGameY = 0.0f; // le coin haut gauche, dans l'image du jeu
	float partyBlockGameW = 0.0f, partyBlockGameBottom = 0.0f; // sa largeur, sa dernière ligne (+1)
	bool partyBlockLiveLine = false;             // sa ligne de messages est celle du moment
	bool restClick = false;      // le repos (ZZZ) du dessus de la main droite cliqué : un clic sur le bouton du jeu
	bool gearMenuOpened = false; // le joueur a ouvert le menu du poignet (l'invitation peut disparaître)
	bool clicked = false;
	int pokeHand = -1;         // une touche du clavier touchée du doigt (la main, pour vibrer)
	bool gameClick = false;    // (gâchette maintenue pour déplacer) un appui bref : un clic au jeu, ici
	bool pressPending = false; // la gâchette vient d'être enfoncée sur une fenêtre : rien au jeu encore
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
	// Mode immersif : l'interface en fenêtres (0) ou classique (1, la barre du bas du jeu : les
	// personnages, les flèches et la ligne d'objets d'origine), montrée dans les réglages VR.
	void setHudClassic(bool classic);
	bool hudClassic() const;
	// L'interface du mode immersif : 0 tout regroupé (un seul bloc dans un cadre), 1 classique (la
	// barre du bas du jeu), 2 des fenêtres séparées (chacune se place et se redimensionne seule).
	void setHudMode(int mode);
	int hudMode() const;
	// Le mode cinéma, montré dans les réglages VR : la taille de l'écran (0 moyen, 1 grand, 2 petit,
	// 3 sans), et l'intro et les menus d'avant la partie dans le noir aussi.
	void setCinema(int size, bool intro);
	// L'inventaire collé sous les personnages (fenêtres) ; détaché par son bouton, recollé en le
	// reposant contre eux. Enregistré avec les fenêtres.
	void setInventoryGrouped(bool grouped);
	bool inventoryGrouped() const;
	// Pareil pour la colonne de la boussole, collée à droite du livre de sorts.
	void setColumnGrouped(bool grouped);
	bool columnGrouped() const;
	// Langue des textes (UiLanguage).
	void setLanguage(int language);
	// Deux lignes d'information affichées sous le clavier (Latin-1).
	void setInfo(const std::string &line1, const std::string &line2);
	// La police des menus du jeu (FONT9P.FNT), cherchée dans les .PAK du dossier du jeu : les
	// fenêtres de l'application prennent alors l'écriture du jeu. Sans elle, une police de secours.
	bool loadGameFont(const std::string &gameDir);
	// Disposition par défaut : autour de l'écran tel qu'il est placé à cet instant.
	void resetLayout(const UiFrameInput &in);
	// La fenêtre des personnages seule, à sa place par défaut (l'interface vient de changer).
	void placeParty(const UiFrameInput &in);
	// Les réglages VR (choisis dans le menu du jeu) : la fenêtre s'ouvre devant le joueur.
	void openMenu();
	// Le mode VR (0 immersif, 1 classique, 2 réalité mixte) et, hors immersif, la vue (0 grand écran,
	// 1 fenêtres, 2 PC 486), montrés dans les réglages VR.
	void setVrState(int mode, int view);
	// Rappel (la première fois en vue classique ou en réalité mixte) : L3 change la vue.
	void openViewNotice();
	// Livre de sorts replié (le bandeau et les messages seulement) ou ouvert.
	bool spellsCollapsed() const;
	void setSpellsCollapsed(bool collapsed);
	// L'atlas : la fenêtre repliée (le bandeau seul) ou ouverte ; le réglage montré dans le menu VR.
	bool inventoryExpanded() const;
	void setInventoryExpanded(bool expanded);
	bool atlasCollapsed() const;
	void setAtlasCollapsed(bool collapsed);
	void setAtlasOption(bool on);
	// Profils de manettes : « Classique » (toujours là, intouchable) et ceux du joueur. buttonMap :
	// ce que fait chaque bouton (UiButtonSlot -> UiButtonFn) en ce moment, changements en cours dans
	// la fenêtre de modification compris.
	const uint8_t *buttonMap() const;
	void addProfile(const std::string &name, const uint8_t *fns);
	void selectProfile(const std::string &name);
	std::string profilesText() const; // lignes « profil … » et « profilactif … » des réglages
	bool hasLayout() const { return placed; }
	void setHasLayout(bool v) { placed = v; }

	// Une fois par image : calcule le pointeur, gère les poignées et le clavier, et remplit
	// les panneaux et poignées de la scène.
	void update(const UiFrameInput &in, UiFrameOutput &out, SceneState &scene);

private:
	void updateFrame(const UiFrameInput &in, UiFrameOutput &out, SceneState &scene);

public:

	UiPlacement placement[kUiPanelCount];
	// Mode cinéma : la disposition des fenêtres du jeu pendant les scènes, réglée à la main (à la
	// première fenêtre déplacée ; le lacet est relatif à l'écran courbe). Sans elle, elles suivent
	// l'écran et passent dessous d'elles-mêmes.
	UiPlacement cinePlacement[kUiPanelCount];
	bool cineCustom = false;
	static UiPlacement defaultHelpPlacement(float yaw);
	static const char *panelKey(int id);

private:
	struct Impl;
	Impl *d;
	bool placed = false;
};

} // namespace lolvr
