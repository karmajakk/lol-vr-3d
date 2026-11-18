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
};

// Le niveau en cours, pour le donjon en 3D (patches/0003). Les gros tableaux ne sont recopiés que
// quand gfxSerial change (changement de niveau), les murs quand wallsSerial change (portes).
struct LevelSnapshot {
	uint32_t gfxSerial = 0;   // 0 = rien encore
	uint32_t wallsSerial = 0;
	int level = 0;
	int block = 0;            // case du groupe : y * 32 + x
	int direction = 0;        // 0 nord, 1 est, 2 sud, 3 ouest
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
	uint8_t screenPalette[768] = {0};
};

class CoreHost {
public:
	static CoreHost &get();

	bool start(const HostConfig &cfg);
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
