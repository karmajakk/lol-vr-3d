// lol-vr — hôte du cœur ScummVM (interface libretro).
// Code indépendant de la plateforme : il fait tourner ScummVM dans son propre fil
// d'exécution et expose l'image, le son et les entrées au reste de l'application.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

namespace lolvr {

struct HostConfig {
	std::string systemDir; // contient scummvm/theme et scummvm/extra (kyra.dat)
	std::string saveDir;   // sauvegardes
	std::string gameDir;   // fichiers du jeu ; vide = lanceur ScummVM
	int language = -1;     // langue du jeu au démarrage (0 anglais, 1 français, 2 allemand ; version CD)
	void (*log)(int level, const char *msg) = nullptr;
};

// Image courante du jeu, convertie en RGBA 8 bits (octets R,G,B,A).
struct Frame {
	std::vector<uint8_t> rgba;
	int width = 0;
	int height = 0;
	uint64_t serial = 0; // augmente à chaque nouvelle image
};

// État du jeu publié par notre correctif du moteur (patches/0002) : zone en cours et
// derniers événements. Les compteurs « serial » augmentent à chaque nouvel événement.
struct GameState {
	int level = 0;            // 0 tant qu'aucune partie n'est en cours
	char tileset[16] = {0};   // décor du niveau, par ex. « FOREST1 »
	uint32_t sfxSerial = 0;   // effet sonore joué
	int sfxVolume = 0;        // 0..255
	char sfxName[16] = {0};   // fichier son, par ex. « DOORMTOP »
	uint32_t hurtSerial = 0;  // un membre du groupe est blessé
	int hurtDamage = 0;
	uint32_t hitSerial = 0;   // le groupe blesse un monstre
	int hitDamage = 0;
	uint32_t loopSerial = 0;  // augmente à chaque tour de la boucle du donjon (vue de jeu active)
	int controlMode = 0;      // 0 = vue normale ; autre = scène spéciale sur toute la largeur
	bool automapOpen = false; // la carte (atlas) occupe tout l'écran
	bool charSheet = false;   // la fiche d'un personnage est ouverte (elle couvre la vue et la colonne de droite)
	int characters = 0;       // nombre de personnages du groupe (0 = inconnu)
	bool menuOpen = false;    // un menu du jeu est affiché
	bool nameEntry = false;   // le joueur écrit le nom d'une sauvegarde
	int vrMenuSerial = 0;     // change à chaque clic sur « Réglages VR » dans le menu du jeu
	int quickSaveSerial = 0;  // change à chaque sauvegarde faite depuis le casque (boutiques)
	int gameLang = -1;        // la langue du jeu (0 anglais, 1 français, 2 allemand ; -1 pas encore connue)
};

// Un monstre ou un objet près du groupe, dessiné par le moteur en grand dans une image de
// kSpriteW x kSpriteH (patches/0003).
const int kSpriteW = 176, kSpriteH = 120;
struct LevelSprite {
	int x = 0, y = 0;          // position dans le niveau : bloc x * 256 + fin (de même pour y)
	int kind = 0;              // 0 monstre, 1 objet
	bool ceiling = false;      // accroché au plafond
	int x0 = 0, y0 = 0, x1 = 0, y1 = 0; // partie dessinée de l'image
	uint32_t version = 0;
	int id = 0;
	int flying = 0;            // objet en vol : sa hauteur dans le jeu (0 = posé)
	int flightDir = 0;         // sa direction (0 nord .. 3 ouest)
};

// L'atlas tel que le groupe l'a exploré (patches/0003) : drapeaux de chaque case (bits 0-2 : vue),
// classe de carte de chaque type de mur (0xC0 plein, 5 bits bas : un symbole, 0x1F aucun), titre
// et légende (textes dans la page de code du jeu).
struct MapSnapshot {
	uint32_t serial = 0;
	bool available = false; // le groupe a la carte
	uint8_t flags[1024] = {};
	uint8_t cls[256] = {};
	std::string title;
	struct Legend {
		int trigger = 0, mark = 0xFFFF, symbol = 0;
		std::string text;
	};
	std::vector<Legend> legend;
	std::vector<uint8_t> rgb; // la page de l'atlas comme le jeu la dessine (320 x 200, RVB)
};

// Le niveau en cours, pour le donjon en 3D (patches/0003). Les gros tableaux ne sont recopiés que
// quand gfxSerial change (changement de niveau), les murs quand wallsSerial change (portes).
struct LevelSnapshot {
	uint32_t gfxSerial = 0;   // 0 = rien encore
	uint32_t wallsSerial = 0;
	int level = 0;
	int block = 0;            // case du groupe : y * 32 + x
	int direction = 0;        // 0 nord, 1 est, 2 sud, 3 ouest
	int nearObjects = 0;      // bit k : monstres ou objets k cases devant le groupe (0 = sa case)
	bool sceneBusy = false;   // la vue du jeu montre autre chose que le donjon (boutique, dialogue, scène)
	// La zone de texte est agrandie (83,122 à 318,179) : conversation, boutique ; le portrait de celui
	// qui parle y est souvent dessiné.
	bool textBox = false;
	uint32_t textRect = 0;     // la zone de texte (et portrait) : x | (y - 100) << 9 | w << 16 | h << 25
	// L'objet tenu par le pointeur (0 = rien) et l'image du curseur du jeu (RGBA, fond transparent),
	// point chaud compris. cursorSerial change avec l'image.
	int itemInHand = 0;
	int magicMenu = -1;        // le menu des niveaux de sort est ouvert pour ce personnage (-1 : fermé)
	int magicLevels = 0;       // niveaux qu'il peut lancer maintenant (à partir du niveau 1)
	// Les images des lieux (une hutte, un château, une marina) que le jeu montre en grand quand on
	// entre dans leur image sur un mur : 176 x 120 en RVB, une par type de mur (placeTypes).
	bool scenePicture = false; // une scène montre sa propre image (boutique, personnage), pas le donjon
	bool sceneCinema = false;  // ... et c'est un lieu, une boutique ou une séquence (le mode cinéma)
	int spellCount = 0;        // les sorts du parchemin
	MapSnapshot map;           // l'atlas
	bool animBusy = false;
	int lamp = -1;
	uint32_t menuRect = 0;     // le menu du jeu ouvert (la disquette) : x | y << 9 | l << 16 | (h / 2) << 25             // la lanterne dans un lieu sombre : 0 pleine .. 7 presque vide, 8 éteinte ; -1 sans objet     // une animation d'un script (un mur qui s'écroule) se joue
	uint32_t invSerial = 0;    // les 48 cases d'inventaire (176 x 132, RVB), refaites à chaque changement
	std::vector<uint8_t> invRgb;
	uint32_t sceneRect = 0;    // où la scène se montre : x | y << 9 | l << 16 | (h / 2) << 25 (0 : la vue)
	uint32_t placeSerial = 0;
	std::vector<int> placeTypes;
	std::vector<uint8_t> placeRgb;
	// Les objets en vol (lancés, projectiles) : où ils sont à chaque pas du jeu (item 0 = aucun).
	struct Flight {
		int item = 0;
		int x = 0, y = 0, height = 0, dir = 0;
	};
	Flight flights[8];
	uint32_t flightSerial = 0;
	uint32_t cursorSerial = 0;
	int cursorW = 0, cursorH = 0, cursorHotX = 0, cursorHotY = 0;
	std::vector<uint8_t> cursorRgba;
	int light = 256;          // clarté de la palette à l'écran (256 = pleine lumière ; pénombre, lampe, fondus)
	// Sort en cours : ce que le jeu dessine par-dessus sa vue (176 x 120, couleurs de la palette ;
	// alpha 0 ou 255). spellSerial change à chaque nouvelle image.
	bool spellBusy = false;
	uint32_t spellSerial = 0;
	std::vector<uint8_t> spellPixels, spellAlpha;
	uint8_t walls[1024 * 4] = {0};
	uint8_t vmpMap[256] = {0};
	uint8_t wallFlags[256] = {0};
	std::vector<uint16_t> vmp;
	std::vector<uint8_t> vcn;
	std::vector<uint8_t> vcnShift;
	uint8_t colTable[128] = {0};
	uint8_t palette[384] = {0};
	// Chaque type de mur dessiné par le moteur, vu de face à un pas (décors et portes compris) :
	// 128 x 96 indices de palette par emplacement, et leur couverture (0 ou 255).
	uint32_t wallCaptureSerial = 0; // égal à gfxSerial quand les dessins correspondent au niveau
	int16_t wallSlot[256] = {0};
	int wallSlots = 0;
	std::vector<uint8_t> wallPixels, wallAlpha;
	std::vector<uint8_t> doorPixels, doorAlpha; // la porte seule (elle se tient au milieu de sa case)
	uint8_t doorRaise[256] = {0};               // porte qui s'ouvre ou se ferme : de combien de lignes elle est levée
	uint8_t screenPalette[768] = {0};
	uint32_t spriteSerial = 0;
	std::vector<LevelSprite> sprites;
	std::vector<uint8_t> spritePixels, spriteAlpha; // sprites.size() x kSpriteW x kSpriteH
};

class CoreHost {
public:
	static CoreHost &get();

	bool start(const HostConfig &cfg);
	// Change la langue du jeu (version CD) : appliquée au prochain tour de sa boucle.
	void setGameLanguage(int language);
	// La fenêtre d'inventaire de l'application : l'activer, cliquer une case (0..47), trier.
	void inventoryEnable();
	void inventoryDisable();
	// Ramasser l'objet posé numéro item (sur la case du groupe ou celle devant).
	void pickupItem(int item);
	void inventoryClick(int slot);
	void inventorySort();
	void quickSave(int slot); // une sauvegarde à cet emplacement, même dans une boutique
	void nextSpell(); // le sort suivant du parchemin
	// L'app dessine elle-même le curseur du jeu, là où vise le joueur (le cœur ne le met plus dans l'image).
	void setOwnCursor(bool own);
	bool cursorShown() const; // le jeu montre son curseur (ni caché, ni menu de ScummVM)
	void stop();
	bool running() const;
	// Vrai quand ScummVM s'est terminé de lui-même (bouton « Quitter »).
	bool finished() const;

	// Copie l'image courante dans `out` si elle est plus récente que out.serial.
	bool fetchFrame(Frame &out);

	// Pointeur absolu : u,v dans [0,1] sur l'image du jeu (0,0 = coin haut-gauche).
	void setPointer(float u, float v);
	void setMouseButtons(bool left, bool right);
	// Touche clavier (codes RETROK_*, identiques aux codes ScummVM/SDL).
	void key(unsigned retroKey, bool down, unsigned retroMods = 0);

	// Boutons spéciaux gérés par ScummVM : clavier virtuel et menu principal (sauver, charger, quitter).
	void setSpecialButtons(bool virtualKeyboard, bool mainMenu);

	// Met le jeu en pause (casque retiré, menu système…) : plus d'images ni de son.
	void setPaused(bool paused);

	// Copie l'état du jeu ; faux si le cœur ne tourne pas.
	bool gameState(GameState &out) const;
	// Met à jour `out` avec le niveau en cours ; faux si aucun niveau n'a encore été chargé.
	bool levelSnapshot(LevelSnapshot &out) const;

	// Lit jusqu'à `frames` trames stéréo 16 bits ; complète par du silence.
	void readAudio(int16_t *out, size_t frames);
	int sampleRate() const;

private:
	CoreHost() = default;
};

// Dessine un écran de message (texte UTF-8, lignes séparées par '\n') dans une image.
void renderMessage(Frame &out, const char *title, const char *body);

} // namespace lolvr
