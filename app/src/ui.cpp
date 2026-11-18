// lol-vr — interface détachée : panneaux, poignées, clavier et pointeur.

#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#include <GLES3/gl3.h>

#include "font8x16.h"
#include "room.h"

#ifndef GL_SRGB8_ALPHA8
#define GL_SRGB8_ALPHA8 0x8C43
#endif

namespace lolvr {

namespace {

// --- Zones de l'écran du jeu (pixels, image 320x200) ------------------------------------------

struct Source {
	float x, y, w, h;
};

// Clavier : grille de 11 colonnes sur 6 rangées.
const int kCellW = 46, kCellH = 40, kCols = 11, kRows = 6;
// Menu du poignet : engrenage et petit menu
const int kGearSize = 64, kHintW = 336, kHintH = 76, kMenuW = 288, kMenuRowH = 44, kMenuRows = 7, kMenuH = kMenuRowH * kMenuRows;
// Panneau d'aide
const int kHelpW = 584, kHelpH = 470;
const int kInfoH = 44; // deux lignes d'information sous les touches
const int kKbW = kCellW * kCols, kKbH = kCellH * kRows + kInfoH;

const Source kSources[kUiPanelCount] = {
	{83, 121, 237, 58},  // groupe : ligne de messages + portraits
	{83, 178, 237, 22},  // inventaire
	{0, 0, 112, 128},    // colonne de gauche (parchemin des sorts)
	{0, 128, 84, 72},    // flèches, options, repos
	{288, 0, 32, 122},   // colonne de droite (boussole, atlas, lampe, bourse)
	{0, 0, (float)kKbW, (float)kKbH},
	{0, 0, (float)kHelpW, (float)kHelpH},
};

// Les pixels du jeu sont 1,2 fois plus hauts que larges ; ceux du clavier sont carrés.
float pixelAspect(int id) {
	return id >= kPanelKeyboard ? 1.0f : 1.2f;
}

struct Key {
	int col, row, span;
	std::string label, shiftLabel;
	unsigned key; // code ASCII envoyé au jeu, 0 pour une touche spéciale
	int action;   // UiAction, ou -1 pour la touche Maj
};

struct Quad {
	Vec3 c = {0, 0, 0}, r = {1, 0, 0}, u = {0, 1, 0};
	float hw = 0.1f, hh = 0.1f;
};

// Intersection d'un rayon avec un rectangle. x, y : position en demi-dimensions (-1..1 à l'intérieur).
bool rayQuad(Vec3 origin, Vec3 dir, const Quad &q, float &t, float &x, float &y) {
	Vec3 n = cross(q.r, q.u);
	float denom = dot(dir, n);
	if (fabsf(denom) < 1e-5f)
		return false;
	t = dot(q.c - origin, n) / denom;
	if (t <= 0.0f)
		return false;
	Vec3 dlt = origin + dir * t - q.c;
	x = dot(dlt, q.r) / q.hw;
	y = dot(dlt, q.u) / q.hh;
	return true;
}

float wrapAngle(float a) {
	const float pi = 3.14159265f;
	while (a > pi)
		a -= 2.0f * pi;
	while (a < -pi)
		a += 2.0f * pi;
	return a;
}

Vec3 directionFor(float yaw, float pitch) {
	return {-sinf(yaw) * cosf(pitch), sinf(pitch), -cosf(yaw) * cosf(pitch)};
}

// --- Dessin du clavier --------------------------------------------------------------------------

struct Canvas {
	int w, h;
	std::vector<uint8_t> px;
	void rect(int x0, int y0, int x1, int y1, uint8_t r, uint8_t g, uint8_t b) {
		for (int y = y0 < 0 ? 0 : y0; y < y1 && y < h; y++)
			for (int x = x0 < 0 ? 0 : x0; x < x1 && x < w; x++) {
				uint8_t *p = &px[((size_t)y * w + x) * 4];
				p[0] = r; p[1] = g; p[2] = b; p[3] = 255;
			}
	}
	void text(const std::string &s, int x, int y, int scale, uint8_t r, uint8_t g, uint8_t b) {
		for (size_t i = 0; i < s.size(); i++) {
			unsigned ch = (unsigned char)s[i];
			if (ch < 32)
				continue;
			const uint8_t *glyph = kFont8x16[ch - 32];
			for (int gy = 0; gy < 16; gy++)
				for (int gx = 0; gx < 8; gx++)
					if (glyph[gy] & (0x80 >> gx))
						rect(x + ((int)i * 8 + gx) * scale, y + gy * scale, x + ((int)i * 8 + gx + 1) * scale, y + (gy + 1) * scale, r, g, b);
		}
	}
};

} // namespace

struct Ui::Impl {
	std::vector<Key> keys;
	bool shift = false;
	bool fireOn = false, detached = true;
	int language = kLangEnglish;
	std::string info[2];
	GLuint kbTex = 0;
	bool kbDirty = true;
	bool srgb = true;
	int hotKey = -1;
	float pressFlash = 0.0f;
	int pressKey = -1;

	// Poignée tenue : -1 aucune, 0..kUiPanelCount-1 un panneau, kScreenTarget l'écran.
	static const int kScreenTarget = 100;
	GLuint helpTex = 0, gearTex = 0, menuTex = 0, hintTex = 0;
	bool helpDirty = true, menuDirty = true, hintDirty = true;
	bool menuKeyboard = false; // état montré dans le menu
	int dungeonMode = 1;
	void renderHint();
	// Invitation du premier lancement : elle suit doucement le regard.
	bool hintPlaced = false;
	Vec3 hintPos = {0, 0, 0};
	void renderHelp();
	void renderGear();
	void renderMenu();
	void upload(GLuint &tex, const std::vector<uint8_t> &px, int w, int h);
	// Menu du poignet : ouvert, il reste où il est apparu.
	bool menuOpen = false;
	Vec3 menuPos = {0, 0, 0}, menuRight = {1, 0, 0}, menuUp = {0, 1, 0};
	int menuHot = -1;
	int drag = -1;
	float grabDist = 1.0f;
	Vec3 grabOffset = {0, 0, 0};
	// Coin tenu (agrandir ou réduire) : même codage que « drag ».
	int resize = -1;
	float resizeSx = 1.0f, resizeSy = 1.0f; // quel coin
	float resizeRef = 0.0f;                 // distance du doigt au centre, à l'image précédente

	void buildKeys();
	void renderKeyboard();
};

void Ui::Impl::buildKeys() {
	keys.clear();
	keys.push_back({0, 0, 3, "", "", 0, kUiActionToggleFire});
	keys.push_back({3, 0, 3, "", "", 0, kUiActionToggleDetach});
	keys.push_back({6, 0, 3, "", "", 0, kUiActionResetLayout});
	keys.push_back({9, 0, 2, "", "", 0, kUiActionCloseKeyboard});
	const char *digits = "1234567890";
	for (int i = 0; i < 10; i++)
		keys.push_back({i, 1, 1, std::string(1, digits[i]), "", (unsigned)digits[i], kUiActionNone});
	keys.push_back({10, 1, 1, "<-", "", 8, kUiActionNone});
	const char *rows[3] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
	for (int i = 0; i < 10; i++)
		keys.push_back({i, 2, 1, std::string(1, rows[0][i]), std::string(1, (char)(rows[0][i] - 32)), (unsigned)rows[0][i], kUiActionNone});
	keys.push_back({10, 2, 1, "-", "", '-', kUiActionNone});
	for (int i = 0; i < 9; i++)
		keys.push_back({i, 3, 1, std::string(1, rows[1][i]), std::string(1, (char)(rows[1][i] - 32)), (unsigned)rows[1][i], kUiActionNone});
	keys.push_back({9, 3, 1, "'", "", '\'', kUiActionNone});
	keys.push_back({10, 3, 1, "!", "", '!', kUiActionNone});
	keys.push_back({0, 4, 1, "Maj", "MAJ", 0, -1});
	for (int i = 0; i < 7; i++)
		keys.push_back({1 + i, 4, 1, std::string(1, rows[2][i]), std::string(1, (char)(rows[2][i] - 32)), (unsigned)rows[2][i], kUiActionNone});
	keys.push_back({8, 4, 1, ",", "", ',', kUiActionNone});
	keys.push_back({9, 4, 1, ".", "", '.', kUiActionNone});
	keys.push_back({10, 4, 1, "?", "", '?', kUiActionNone});
	keys.push_back({0, 5, 2, "\xC9" "chap", "", 27, kUiActionNone});
	keys.push_back({2, 5, 6, "Espace", "", ' ', kUiActionNone});
	keys.push_back({8, 5, 3, "Entr\xE9" "e", "", 13, kUiActionNone});
}

void Ui::Impl::renderKeyboard() {
	Canvas cv{kKbW, kKbH, std::vector<uint8_t>((size_t)kKbW * kKbH * 4)};
	cv.rect(0, 0, kKbW, kKbH, 34, 24, 16);
	const bool fr = language == kLangFrench;
	for (Key &k : keys) {
		if (k.action == kUiActionToggleFire)
			k.label = fr ? (fireOn ? "Feu : oui" : "Feu : non") : (fireOn ? "Fire: on" : "Fire: off");
		else if (k.action == kUiActionToggleDetach)
			k.label = fr ? (detached ? "Panneaux : oui" : "Panneaux : non") : (detached ? "Panels: on" : "Panels: off");
		else if (k.action == kUiActionResetLayout)
			k.label = fr ? "Tout ranger" : "Reset layout";
		else if (k.action == kUiActionCloseKeyboard)
			k.label = fr ? "Fermer" : "Close";
		else if (k.action == -1) {
			k.label = fr ? "Maj" : "Shift";
			k.shiftLabel = fr ? "MAJ" : "SHIFT";
		} else if (k.key == 27)
			k.label = fr ? "\xC9" "chap" : "Esc";
		else if (k.key == ' ')
			k.label = fr ? "Espace" : "Space";
		else if (k.key == 13)
			k.label = fr ? "Entr\xE9" "e" : "Enter";
	}
	for (size_t i = 0; i < keys.size(); i++) {
		const Key &k = keys[i];
		int x0 = k.col * kCellW + 2, y0 = k.row * kCellH + 2, x1 = (k.col + k.span) * kCellW - 2, y1 = (k.row + 1) * kCellH - 2;
		bool option = k.row == 0;
		bool lit = (k.action == -1 && shift) || (k.action == kUiActionToggleFire && fireOn) || (k.action == kUiActionToggleDetach && detached);
		// Touche en relief : bord clair en haut, sombre en bas
		cv.rect(x0, y0, x1, y1, 30, 20, 12);
		if (lit)
			cv.rect(x0 + 1, y0 + 1, x1 - 1, y1 - 1, 150, 104, 40);
		else if (option)
			cv.rect(x0 + 1, y0 + 1, x1 - 1, y1 - 1, 74, 60, 70);
		else
			cv.rect(x0 + 1, y0 + 1, x1 - 1, y1 - 1, 96, 72, 46);
		cv.rect(x0 + 1, y0 + 1, x1 - 1, y0 + 3, lit ? 220 : 140, lit ? 170 : 108, lit ? 80 : 72);
		cv.rect(x0 + 1, y1 - 3, x1 - 1, y1 - 1, 52, 38, 24);
		const std::string &label = (shift && !k.shiftLabel.empty()) ? k.shiftLabel : k.label;
		int scale = label.size() == 1 ? 2 : 1;
		int tw = (int)label.size() * 8 * scale, th = 16 * scale;
		cv.text(label, (x0 + x1 - tw) / 2, (y0 + y1 - th) / 2 - (scale == 2 ? 2 : 0), scale, 250, 236, 200);
	}
	for (int l = 0; l < 2; l++)
		cv.text(info[l].substr(0, (size_t)(kKbW - 12) / 8), 6, kCellH * kRows + 4 + l * 19, 1, 214, 196, 150);
	if (!kbTex)
		glGenTextures(1, &kbTex);
	glBindTexture(GL_TEXTURE_2D, kbTex);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, kKbW, kKbH, 0, GL_RGBA, GL_UNSIGNED_BYTE, cv.px.data());
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, 0);
	kbDirty = false;
}

// Texte UTF-8 -> Latin-1 (la police du clavier).
static std::string latin1(const char *utf8) {
	std::string out;
	for (const unsigned char *p = (const unsigned char *)utf8; *p; p++) {
		if (*p < 0x80)
			out += (char)*p;
		else if ((*p == 0xC2 || *p == 0xC3) && p[1]) {
			out += (char)(*p == 0xC3 ? p[1] + 0x40 : p[1]);
			p++;
		} else {
			out += '?';
			while ((p[1] & 0xC0) == 0x80)
				p++;
		}
	}
	return out;
}

void Ui::Impl::upload(GLuint &tex, const std::vector<uint8_t> &px, int w, int h) {
	if (!tex)
		glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, 0);
}

void Ui::Impl::renderHelp() {
	static const char *const english[] = {
		"#CONTROLLER MAPPING",
		"",
		"#Everywhere",
		"Aim + trigger ............ left click",
		"Aim + grip, or X ......... right click (drink, use)",
		"Left stick ............... walk forward, back, strafe",
		"Right stick (sideways) ... turn",
		"Y ........................ map",
		"Menu button (left) ....... save, load, quit",
		"Gear on the left wrist ... settings, keyboard, help",
		"",
		"#Combat, in the dungeon",
		"A ........................ attack with the framed character",
		"B ........................ open their magic, then aim at a spell",
		"Right stick (up/down) .... change character",
		"Drink a potion: aim at it and press X",
		"",
		"#Menus and cutscenes",
		"A : Enter       B : Escape (skip)",
		"",
		"#Windows and screen",
		"Brass bar + hold trigger ........ move",
		"Corner (bracket) + hold trigger . resize",
		"Right stick click ........ recenter everything",
		"Left stick click ......... change scenery",
		"",
		"Click this panel to close it.",
		nullptr,
	};
	static const char *const french[] = {
		"#LES MANETTES ET LES BOUTONS",
		"",
		"#Partout",
		"Viser + gâchette ......... clic gauche",
		"Viser + poignée, ou X .... clic droit (boire, utiliser)",
		"Joystick gauche .......... avancer, reculer, pas de côté",
		"Joystick droit (côtés) ... tourner",
		"Y ........................ carte",
		"Bouton menu (gauche) ..... sauvegarder, charger, quitter",
		"Engrenage au poignet gauche : réglages, clavier, aide",
		"",
		"#Combat, dans le donjon",
		"A ........................ attaquer avec le personnage encadré",
		"B ........................ ouvrir sa magie, puis viser le sort",
		"Joystick droit (haut/bas)  changer de personnage",
		"Boire une potion : la viser et appuyer sur X",
		"",
		"#Menus et cinématiques",
		"A : Entrée      B : Échap (passer)",
		"",
		"#Fenêtres et écran",
		"Barrette de laiton + gâchette tenue ... déplacer",
		"Coin (équerre) + gâchette tenue ....... agrandir, réduire",
		"Clic du joystick droit ... tout recentrer devant soi",
		"Clic du joystick gauche .. changer de décor",
		"",
		"Cliquer ce panneau pour le fermer.",
		nullptr,
	};
	const char *const *lines = language == kLangFrench ? french : english;
	Canvas cv{kHelpW, kHelpH, std::vector<uint8_t>((size_t)kHelpW * kHelpH * 4)};
	cv.rect(0, 0, kHelpW, kHelpH, 30, 21, 14);
	int y = 10;
	for (int i = 0; lines[i]; i++) {
		const char *line = lines[i];
		bool title = line[0] == '#';
		std::string text = latin1(title ? line + 1 : line);
		if (title) {
			cv.rect(8, y + 16, kHelpW - 8, y + 17, 150, 104, 40);
			cv.text(text, 12, y - 1, 1, 255, 205, 110);
		} else {
			cv.text(text, 20, y, 1, 240, 228, 196);
		}
		y += text.empty() ? 8 : 18;
	}
	upload(helpTex, cv.px, kHelpW, kHelpH);
	helpDirty = false;
}

// Un engrenage de laiton sur fond sombre.
void Ui::Impl::renderGear() {
	const int N = kGearSize;
	std::vector<uint8_t> px((size_t)N * N * 4);
	for (int y = 0; y < N; y++)
		for (int x = 0; x < N; x++) {
			float dx = (float)x + 0.5f - N * 0.5f, dy = (float)y + 0.5f - N * 0.5f;
			float r = sqrtf(dx * dx + dy * dy), a = atan2f(dy, dx);
			float outer = cosf(a * 8.0f) > 0.0f ? 27.0f : 21.0f;
			uint8_t *p = &px[((size_t)y * N + x) * 4];
			bool metal = r < outer && r > 9.0f;
			float shade = 0.75f + 0.25f * (-dy / (N * 0.5f));
			p[0] = metal ? (uint8_t)(230 * shade) : 34;
			p[1] = metal ? (uint8_t)(170 * shade) : 24;
			p[2] = metal ? (uint8_t)(70 * shade) : 16;
			p[3] = 255;
		}
	upload(gearTex, px, N, N);
}

void Ui::Impl::renderMenu() {
	const bool fr = language == kLangFrench;
	Canvas cv{kMenuW, kMenuH, std::vector<uint8_t>((size_t)kMenuW * kMenuH * 4)};
	cv.rect(0, 0, kMenuW, kMenuH, 34, 24, 16);
	const std::string rows[kMenuRows] = {
		"Lands of Lore VR 3D",
		latin1(fr ? "Langue : Français" : "Language: English"),
		latin1(detached ? (fr ? "Écran classique : non" : "Classic screen: off") : (fr ? "Écran classique : oui" : "Classic screen: on")),
		latin1(dungeonMode == 1 ? (fr ? "Donjon 3D, écran au besoin" : "3D dungeon, screen if needed")
		     : dungeonMode == 2 ? (fr ? "Donjon 3D, écran toujours" : "3D dungeon, screen always")
		                        : (fr ? "Donjon 3D : non" : "3D dungeon: off")),
		latin1(menuKeyboard ? (fr ? "Cacher le clavier" : "Hide keyboard") : (fr ? "Afficher le clavier" : "Show keyboard")),
		latin1(fr ? "Manettes et boutons" : "Controller mapping"),
		latin1(fr ? "Fermer" : "Close"),
	};
	for (int i = 0; i < kMenuRows; i++) {
		int y0 = i * kMenuRowH;
		if (i > 0) {
			cv.rect(6, y0 + 4, kMenuW - 6, y0 + kMenuRowH - 4, 30, 20, 12);
			cv.rect(7, y0 + 5, kMenuW - 7, y0 + kMenuRowH - 5, 96, 72, 46);
			cv.rect(7, y0 + 5, kMenuW - 7, y0 + 7, 140, 108, 72);
		}
		int tw = (int)rows[i].size() * 8;
		if (i == 0)
			cv.text(rows[i], (kMenuW - tw) / 2, y0 + 14, 1, 255, 205, 110);
		else
			cv.text(rows[i], (kMenuW - tw) / 2, y0 + 14, 1, 250, 236, 200);
	}
	upload(menuTex, cv.px, kMenuW, kMenuH);
	menuDirty = false;
}

// Invitation du premier lancement : un engrenage et deux lignes.
void Ui::Impl::renderHint() {
	const bool fr = language == kLangFrench;
	Canvas cv{kHintW, kHintH, std::vector<uint8_t>((size_t)kHintW * kHintH * 4)};
	cv.rect(0, 0, kHintW, kHintH, 140, 108, 72);
	cv.rect(3, 3, kHintW - 3, kHintH - 3, 34, 24, 16);
	// Petit engrenage à gauche
	for (int y = 0; y < kHintH; y++)
		for (int x = 0; x < 72; x++) {
			float dx = (float)x + 0.5f - 38.0f, dy = (float)y + 0.5f - kHintH * 0.5f;
			float r = sqrtf(dx * dx + dy * dy), a = atan2f(dy, dx);
			float outer = cosf(a * 8.0f) > 0.0f ? 24.0f : 18.0f;
			if (r < outer && r > 8.0f) {
				float shade = 0.75f + 0.25f * (-dy / (kHintH * 0.5f));
				cv.rect(x, y, x + 1, y + 1, (uint8_t)(230 * shade), (uint8_t)(170 * shade), (uint8_t)(70 * shade));
			}
		}
	const std::string l1 = latin1(fr ? "Regarde ta main gauche :" : "Look at your left hand:");
	const std::string l2 = latin1(fr ? "l'engrenage ouvre les réglages." : "the gear opens the settings.");
	cv.text(l1, 76, 18, 1, 255, 205, 110);
	cv.text(l2, 76, 42, 1, 250, 236, 200);
	upload(hintTex, cv.px, kHintW, kHintH);
	hintDirty = false;
}

Ui::Ui() : d(new Impl()) {
	d->buildKeys();
}

Ui::~Ui() {
	delete d;
}

const char *Ui::panelKey(int id) {
	static const char *names[kUiPanelCount] = {"groupe", "inventaire", "sorts", "commandes", "boussole", "clavier", "aide"};
	return id >= 0 && id < kUiPanelCount ? names[id] : "";
}

void Ui::initGl(bool srgb) {
	d->srgb = srgb;
	d->kbDirty = true;
}

void Ui::setLanguage(int language) {
	if (language != d->language) {
		d->language = language;
		d->kbDirty = true;
		d->helpDirty = true;
		d->menuDirty = true;
		d->hintDirty = true;
	}
}

void Ui::setOptions(bool fireOn, bool detached) {
	if (fireOn != d->fireOn || detached != d->detached)
		d->kbDirty = true;
	if (detached != d->detached)
		d->menuDirty = true;
	d->fireOn = fireOn;
	d->detached = detached;
}

void Ui::setDungeonOption(int mode) {
	if (mode != d->dungeonMode) {
		d->dungeonMode = mode;
		d->menuDirty = true;
	}
}

void Ui::setInfo(const std::string &line1, const std::string &line2) {
	if (line1 != d->info[0] || line2 != d->info[1])
		d->kbDirty = true;
	d->info[0] = line1;
	d->info[1] = line2;
}

UiPlacement Ui::defaultHelpPlacement(float yaw) {
	// Devant soi, à hauteur des yeux, bien lisible.
	return {yaw, 0.03f, 1.05f, 0.0013f};
}

void Ui::resetLayout(const UiFrameInput &in) {
	// Au départ, tout est dans le plan de l'écran, autour de la vue du donjon. Chaque fenêtre est
	// ensuite indépendante : sa position est notée par rapport au joueur (lacet, tangage, distance),
	// et plus rien ne la lie à l'écran. Unités de travail : fractions de la largeur de l'image entière.
	const float fullW = in.fullW > 0.05f ? in.fullW : 0.05f;
	const float viewH = kSplitViewHeight, viewW = viewH * 176.0f / 144.0f, viewY = kSplitViewLift;
	const float frame = 0.092f * viewW, top = viewY + viewH * 0.5f, bottom = viewY - viewH * 0.5f;
	auto set = [&](int id, float x, float y, float scale) {
		Vec3 c = in.basePos + rotate(in.screenRot, Vec3{x, y, 0.0f} * fullW);
		Vec3 v = c - in.anchorPos;
		float dist = clampf(length(v), 0.20f, 6.0f);
		UiPlacement &pl = placement[id];
		pl.c = dist;
		pl.b = clampf(asinf(clampf(v.y / dist, -1.0f, 1.0f)), -1.35f, 1.35f);
		pl.a = wrapAngle(atan2f(-v.x, -v.z) - in.anchorYaw);
		pl.scale = clampf(scale * fullW, 0.0004f, 0.02f);
	};
	auto halfW = [](int id, float scale) { return kSources[id].w * scale * 0.5f; };
	auto halfH = [](int id, float scale) { return kSources[id].h * 1.2f * scale * 0.5f; };

	// À gauche : le parchemin des sorts, et dessous les commandes.
	const float sSpells = 0.0036f, sCtl = 0.0028f;
	float xl = -(viewW * 0.5f + frame + 0.035f + halfW(kPanelSpells, sSpells));
	set(kPanelSpells, xl, top - halfH(kPanelSpells, sSpells), sSpells);
	set(kPanelControls, xl, top - 2.0f * halfH(kPanelSpells, sSpells) - 0.05f - halfH(kPanelControls, sCtl), sCtl);
	// À droite : la colonne de la boussole.
	const float sCompass = 0.0038f;
	set(kPanelCompass, viewW * 0.5f + frame + 0.035f + halfW(kPanelCompass, sCompass), top - halfH(kPanelCompass, sCompass), sCompass);
	// Dessous : le groupe, puis l'inventaire.
	const float sRow = 0.0026f;
	float yParty = bottom - frame - 0.045f - halfH(kPanelParty, sRow);
	set(kPanelParty, 0.0f, yParty, sRow);
	set(kPanelInventory, 0.0f, yParty - halfH(kPanelParty, sRow) - 0.040f - halfH(kPanelInventory, sRow), sRow);

	// Le clavier vient se poser devant soi, par-dessus le reste.
	Vec3 v = in.basePos - in.anchorPos;
	float screenYaw = wrapAngle(atan2f(-v.x, -v.z) - in.anchorYaw);
	placement[kPanelKeyboard] = {screenYaw, -29.0f * 3.14159265f / 180.0f, 0.58f, 0.0011f};
	placement[kPanelHelp] = defaultHelpPlacement(screenYaw);
	placed = true;
}

namespace {

// Garde un panneau dans la bibliothèque : au-dessus du plancher, devant les murs.
Vec3 clampToRoom(const UiFrameInput &in, Vec3 c, float hw, float hh) {
	Quat q = quatYaw(in.roomYaw);
	Vec3 p = rotate(conjugate(q), c - in.roomPos);
	float mx = fmaxf(0.0f, kRoomHalfX - 0.40f - hw * 0.8f);
	p.x = clampf(p.x, -mx, mx);
	p.z = clampf(p.z, kRoomFrontZ + 0.12f, kRoomBackZ - 0.25f);
	float lo = hh + 0.04f, hi = kRoomHeight - hh - 0.06f;
	if (hi < lo)
		hi = lo;
	p.y = clampf(p.y, lo, hi);
	return rotate(q, p) + in.roomPos;
}

} // namespace

void Ui::update(const UiFrameInput &in, UiFrameOutput &out, SceneState &scene) {
	out = UiFrameOutput();
	for (int i = 0; i < SceneState::kMaxPanels; i++)
		scene.panels[i].visible = false;
	for (int i = 0; i < SceneState::kMaxHandles; i++)
		scene.handles[i].visible = false;
	if (d->kbDirty)
		d->renderKeyboard();
	if (d->helpDirty || !d->helpTex)
		d->renderHelp();
	if (!d->gearTex)
		d->renderGear();
	if (in.keyboardOpen != d->menuKeyboard) {
		d->menuKeyboard = in.keyboardOpen;
		d->menuDirty = true;
	}
	if (d->menuDirty || !d->menuTex)
		d->renderMenu();
	if (in.gearHint && (d->hintDirty || !d->hintTex))
		d->renderHint();
	d->pressFlash -= in.dt;

	// Tant que la disposition n'est pas établie (écran pas encore placé), aucun panneau.
	bool visible[kUiPanelCount];
	for (int i = 0; i <= kPanelCompass; i++)
		visible[i] = placed && in.split;
	visible[kPanelKeyboard] = placed && in.keyboardOpen;
	visible[kPanelHelp] = placed && in.helpOpen;
	auto isPanel = [](int target) { return target >= 0 && target < kUiPanelCount; };

	Vec3 screenR = rotate(in.screenRot, {1, 0, 0}), screenU = rotate(in.screenRot, {0, 1, 0}), screenN = rotate(in.screenRot, {0, 0, 1});
	Quat screenInv = conjugate(in.screenRot);
	float screenDist = length(in.screenPos - in.anchorPos);
	if (screenDist < 0.3f)
		screenDist = 0.3f;
	// --- 1. Poignée tenue : l'objet suit le rayon du doigt ---
	if (d->drag != -1) {
		bool gone = isPanel(d->drag) && !visible[d->drag];
		if (!in.triggerDown || !in.aimValid || gone) {
			d->drag = -1;
			out.released = true;
			out.layoutChanged = true;
		} else {
			d->grabDist = clampf(d->grabDist * expf(in.stickY * 1.3f * in.dt), 0.20f, 6.0f);
			Vec3 c = in.tip + in.dir * d->grabDist + d->grabOffset;
			out.dragging = true;
			if (d->drag == Impl::kScreenTarget) {
				out.screenDragging = true;
				out.screenCenter = c;
				out.screenZoom = expf(in.stickX * 0.9f * in.dt);
			} else {
				UiPlacement &pl = placement[d->drag];
				Vec3 v = c - in.anchorPos;
				pl.c = clampf(length(v), 0.20f, 6.0f);
				pl.b = clampf(asinf(clampf(v.y / pl.c, -1.0f, 1.0f)), -1.35f, 1.35f);
				pl.a = wrapAngle(atan2f(-v.x, -v.z) - in.anchorYaw);
				pl.scale = clampf(pl.scale * expf(in.stickX * 0.9f * in.dt), 0.0004f, 0.02f);
			}
		}
	}
	// Coin tenu : relâché ?
	if (d->resize != -1) {
		bool gone = isPanel(d->resize) && !visible[d->resize];
		if (!in.triggerDown || !in.aimValid || gone) {
			d->resize = -1;
			out.released = true;
			out.layoutChanged = true;
		}
	}

	// --- 2. Pose des panneaux et de leurs poignées ---
	// Poignées : une par panneau, puis celle de l'écran.
	const int kHandleScreen = kUiPanelCount, kHandleCount = kUiPanelCount + 1;
	Quad content[kUiPanelCount], handle[kUiPanelCount + 1];
	float panelDist[kUiPanelCount];
	for (int i = 0; i < kUiPanelCount; i++) {
		const UiPlacement &pl = placement[i];
		Quad &q = content[i];
		q.c = in.anchorPos + directionFor(in.anchorYaw + pl.a, pl.b) * pl.c;
		float scale = pl.scale;
		q.hw = kSources[i].w * scale * 0.5f;
		q.hh = kSources[i].h * pixelAspect(i) * scale * 0.5f;
		if (in.roomClamp)
			q.c = clampToRoom(in, q.c, q.hw, q.hh);
		// Chaque panneau est tourné vers le joueur.
		Vec3 v = q.c - in.anchorPos;
		float dist = length(v);
		if (dist < 0.15f) {
			q.c = in.anchorPos + (dist > 1e-4f ? v * (0.15f / dist) : Vec3{0, 0, -0.15f});
			v = q.c - in.anchorPos;
			dist = 0.15f;
		}
		panelDist[i] = dist;
		Vec3 n = v * (-1.0f / dist);
		Vec3 r = cross({0, 1, 0}, n);
		float rl = length(r);
		q.r = rl > 1e-4f ? r * (1.0f / rl) : Vec3{1, 0, 0};
		q.u = cross(n, q.r);
		Quad &hq = handle[i];
		float border = clampf(0.012f * dist, 0.004f, 0.012f);
		float thick = clampf(0.017f * dist, 0.008f, 0.06f), longSide = clampf(0.10f * dist, 0.03f, 0.35f);
		hq.r = q.r;
		hq.u = q.u;
		if (i >= kPanelKeyboard) {
			// Clavier, aide : barrette au-dessus.
			hq.hh = thick;
			hq.hw = fminf(q.hw, longSide);
			hq.c = q.c + q.u * (q.hh + border + hq.hh + 0.002f);
		} else {
			// Fenêtres du jeu : barrette sur le côté, pour ne jamais se trouver sur le chemin du
			// pointeur entre l'écran et une fenêtre placée dessous. À droite pour la boussole, à gauche
			// pour les autres.
			float side = i == kPanelCompass ? 1.0f : -1.0f;
			hq.hw = thick;
			hq.hh = fminf(q.hh, longSide);
			hq.c = q.c + q.r * (side * (q.hw + border + hq.hw + 0.002f));
		}
	}
	// Poignée de l'écran : au-dessus du cadre (ou du moniteur)
	{
		Quad &hq = handle[kUiPanelCount];
		hq.r = screenR;
		hq.u = screenU;
		hq.hh = clampf(0.020f * screenDist, 0.008f, 0.07f);
		hq.hw = clampf(0.11f * screenDist, 0.03f, 0.40f);
		float top = in.crt ? 0.50f * in.dispW : in.dispH * 0.5f + 0.165f * in.dispW;
		hq.c = in.screenPos + screenU * (top + hq.hh) + screenN * (in.crt ? -0.02f * in.dispW : 0.02f * in.dispW);
	}

	auto handleShown = [&](int i) { return i == kHandleScreen ? !in.screenHidden : visible[i]; };
	if (in.screenHidden && (d->drag == Impl::kScreenTarget || d->resize == Impl::kScreenTarget)) {
		d->drag = -1;
		d->resize = -1;
	}

	// Menu du poignet : l'engrenage apparaît sur la main gauche quand on la regarde de près.
	bool gearVisible = false;
	Quad gearQ, menuQ;
	if (placed && in.wristValid) {
		Vec3 c = in.wristPos + rotate(in.wristRot, {0.0f, 0.075f, 0.06f});
		Vec3 v = c - in.headPos;
		float dist = length(v);
		if (dist > 0.12f && dist < 0.85f && dot(v * (1.0f / dist), in.headDir) > 0.86f) {
			gearVisible = true;
			Vec3 n = v * (-1.0f / dist);
			Vec3 r = cross({0, 1, 0}, n);
			float rl = length(r);
			gearQ.c = c;
			gearQ.r = rl > 1e-4f ? r * (1.0f / rl) : Vec3{1, 0, 0};
			gearQ.u = cross(n, gearQ.r);
			gearQ.hw = gearQ.hh = 0.026f;
			// Tant que le joueur ne l'a jamais ouvert, l'engrenage bat doucement pour se faire remarquer.
			if (in.gearHint && !d->menuOpen)
				gearQ.hw = gearQ.hh = 0.026f * (1.15f + 0.15f * sinf((float)in.time * 6.0f));
		}
	}
	const float kMenuScale = 0.0009f;
	if (d->menuOpen) {
		menuQ.c = d->menuPos;
		menuQ.r = d->menuRight;
		menuQ.u = d->menuUp;
		menuQ.hw = kMenuW * kMenuScale * 0.5f;
		menuQ.hh = kMenuH * kMenuScale * 0.5f;
	}
	d->menuHot = -1;

	// Coins : une zone de prise juste à l'extérieur de chaque fenêtre (sur le cadre, pour l'écran).
	Quad screenQuad;
	screenQuad.c = in.screenPos;
	screenQuad.r = screenR;
	screenQuad.u = screenU;
	screenQuad.hw = in.dispW * 0.5f;
	screenQuad.hh = in.dispH * 0.5f;
	float cornerMargin[kUiPanelCount + 1];
	for (int i = 0; i < kUiPanelCount; i++)
		cornerMargin[i] = clampf(0.012f * panelDist[i], 0.004f, 0.012f) + 0.004f;
	cornerMargin[kUiPanelCount] = 0.095f * in.dispW;
	auto targetQuad = [&](int target) -> const Quad & { return target == Impl::kScreenTarget ? screenQuad : content[target]; };
	auto targetMargin = [&](int target) { return cornerMargin[target == Impl::kScreenTarget ? kUiPanelCount : target]; };

	// Coin tenu : la fenêtre grandit ou rapetisse autour de son centre, le coin suit le doigt.
	if (d->resize != -1) {
		const Quad &q = targetQuad(d->resize);
		float m = targetMargin(d->resize), t, x, y;
		out.dragging = true;
		if (rayQuad(in.tip, in.dir, q, t, x, y)) {
			float dd = sqrtf(x * q.hw * x * q.hw + y * q.hh * y * q.hh);
			float cornerDist = sqrtf((q.hw + m) * (q.hw + m) + (q.hh + m) * (q.hh + m));
			if (dd > 0.01f && cornerDist > 0.01f && d->resizeRef > 0.05f) {
				float ratio = clampf(dd / d->resizeRef / cornerDist, 0.90f, 1.10f);
				if (d->resize == Impl::kScreenTarget) {
					out.screenResizing = true;
					out.screenZoom = ratio;
				} else {
					UiPlacement &pl = placement[d->resize];
					pl.scale = clampf(pl.scale * ratio, 0.0004f, 0.02f);
				}
			}
		}
	}

	// --- 3. Que vise le doigt ? ---
	enum { kHitNone, kHitHandle, kHitPanel, kHitScreen, kHitCorner, kHitWrist };
	int hitKind = kHitNone, hitIndex = -1;
	float bestT = 1e9f, hx = 0, hy = 0;
	// Repli : quand le doigt passe juste à côté d'une fenêtre, on prend la plus proche.
	int nearKind = kHitNone, nearIndex = -1;
	float nearExcess = 1e9f, nearT = 0, nearX = 0, nearY = 0;
	bool screenHitOk = false;
	float screenU01 = 0, screenV01 = 0, screenZ = 0;
	d->hotKey = -1;
	int cornerTarget = -1;
	float cornerSx = 1.0f, cornerSy = 1.0f, cornerK = 1.0f;
	if (in.aimValid && d->drag == -1 && d->resize == -1) {
		for (int i = 0; i < kHandleCount; i++) {
			if (!handleShown(i))
				continue;
			float t, x, y;
			// Zone de prise un peu plus généreuse que la poignée dessinée
			bool upright = handle[i].hh > handle[i].hw;
			if (rayQuad(in.tip, in.dir, handle[i], t, x, y) && t < bestT && fabsf(x) <= (upright ? 1.35f : 1.15f) && fabsf(y) <= (upright ? 1.15f : 1.35f)) {
				bestT = t; hitKind = kHitHandle; hitIndex = i; hx = x; hy = y;
			}
		}
		for (int i = 0; i < kUiPanelCount; i++) {
			if (!visible[i])
				continue;
			float t, x, y;
			if (!rayQuad(in.tip, in.dir, content[i], t, x, y))
				continue;
			if (fabsf(x) <= 1.0f && fabsf(y) <= 1.0f) {
				if (t < bestT) {
					bestT = t; hitKind = kHitPanel; hitIndex = i; hx = x; hy = y;
				}
			} else {
				float e = fmaxf((fabsf(x) - 1.0f) * content[i].hw, (fabsf(y) - 1.0f) * content[i].hh);
				if (e < 0.05f * t && e < nearExcess) {
					nearExcess = e; nearKind = kHitPanel; nearIndex = i; nearT = t;
					nearX = clampf(x, -1.0f, 1.0f); nearY = clampf(y, -1.0f, 1.0f);
				}
			}
		}
		// L'engrenage et le menu du poignet
		for (int w = 0; w < 2; w++) {
			if (w == 0 ? !gearVisible : !d->menuOpen)
				continue;
			float t, x, y;
			if (rayQuad(in.tip, in.dir, w == 0 ? gearQ : menuQ, t, x, y) && fabsf(x) <= 1.0f && fabsf(y) <= 1.0f && t < bestT) {
				bestT = t; hitKind = kHitWrist; hitIndex = w; hx = x; hy = y;
			}
		}
		// L'écran principal (la dalle du moniteur cathodique est bombée : on affine le point d'impact)
		if (!in.screenHidden) {
			Vec3 origin = rotate(screenInv, in.tip - in.screenPos);
			Vec3 dir = rotate(screenInv, in.dir);
			if (dir.z < -1e-4f && origin.z > 0.0f) {
				float zPlane = 0.0f, t = 0.0f;
				Vec3 hit = origin;
				for (int it = 0; it < (in.crt ? 3 : 1); it++) {
					t = (zPlane - origin.z) / dir.z;
					hit = origin + dir * t;
					if (in.crt)
						zPlane = in.dispW * crtGlassZ(hit.x / in.dispW, hit.y / in.dispW);
				}
				float u = hit.x / in.dispW + 0.5f, v = 0.5f - hit.y / in.dispH;
				float e = fmaxf(fmaxf(-u, u - 1.0f) * in.dispW, fmaxf(-v, v - 1.0f) * in.dispH);
				screenU01 = clampf(u, 0.0f, 1.0f);
				screenV01 = clampf(v, 0.0f, 1.0f);
				screenZ = zPlane;
				if (e <= 0.0f) {
					if (t < bestT) {
						bestT = t; hitKind = kHitScreen; hitIndex = 0;
					}
				} else if (e < 0.05f * t && e < nearExcess) {
					nearExcess = e; nearKind = kHitScreen; nearIndex = 0; nearT = t;
				}
				screenHitOk = true;
			}
		}
		// Les coins
		{
			float cornerT = 1e9f, cornerBest = 1e9f;
			for (int i = 0; i <= kUiPanelCount; i++) {
				if (i < kUiPanelCount ? !visible[i] : in.screenHidden)
					continue;
				const Quad &q = i < kUiPanelCount ? content[i] : screenQuad;
				float t, x, y;
				if (!rayQuad(in.tip, in.dir, q, t, x, y) || (fabsf(x) <= 1.0f && fabsf(y) <= 1.0f))
					continue; // à l'intérieur, les clics sont pour le jeu
				float m = cornerMargin[i];
				float px = fabsf(x) * q.hw - (q.hw + m), py = fabsf(y) * q.hh - (q.hh + m);
				float e = sqrtf(px * px + py * py);
				if (e < 0.045f * t && e < cornerBest) {
					cornerBest = e; cornerT = t;
					cornerTarget = i < kUiPanelCount ? i : Impl::kScreenTarget;
					cornerSx = x < 0.0f ? -1.0f : 1.0f;
					cornerSy = y < 0.0f ? -1.0f : 1.0f;
					float dd = sqrtf(x * q.hw * x * q.hw + y * q.hh * y * q.hh);
					cornerK = dd / sqrtf((q.hw + m) * (q.hw + m) + (q.hh + m) * (q.hh + m));
				}
			}
			// Une poignée garde la priorité ; une fenêtre placée devant le coin aussi.
			if (cornerTarget != -1 && hitKind != kHitHandle && (hitKind == kHitNone || cornerT < bestT - 0.01f)) {
				hitKind = kHitCorner;
				bestT = cornerT;
			} else {
				cornerTarget = -1;
			}
		}
		if (hitKind == kHitNone && nearKind != kHitNone) {
			hitKind = nearKind; hitIndex = nearIndex; bestT = nearT; hx = nearX; hy = nearY;
		}
	}

	float handleGlow[kUiPanelCount + 1];
	for (int i = 0; i < kHandleCount; i++)
		handleGlow[i] = 0.0f;
	if (d->drag != -1)
		handleGlow[d->drag == Impl::kScreenTarget ? kHandleScreen : d->drag] = 1.0f;
	out.pointerLong = hitKind != kHitScreen && hitKind != kHitPanel && hitKind != kHitWrist;

	if (hitKind == kHitScreen && screenHitOk) {
		out.pointerValid = true;
		out.buttonsToGame = true;
		out.hitValid = true;
		out.hitRadius = 0.0f;
		out.gu = (in.cropX + in.cropW * screenU01) / 320.0f;
		out.gv = (in.cropY + in.cropH * screenV01) / 200.0f;
		Vec3 local{(screenU01 - 0.5f) * in.dispW, (0.5f - screenV01) * in.dispH, screenZ + 0.004f};
		out.hit = rotate(in.screenRot, local) + in.screenPos;
	} else if (hitKind == kHitHandle) {
		handleGlow[hitIndex] = 0.7f;
		const Quad &hq = handle[hitIndex];
		Vec3 p = in.tip + in.dir * bestT;
		out.hitValid = true;
		out.hit = p;
		out.hitRight = hq.r;
		out.hitUp = hq.u;
		out.hitRadius = 0.016f * bestT;
		if (in.triggerPressed) {
			d->drag = hitIndex == kHandleScreen ? Impl::kScreenTarget : hitIndex;
			d->grabDist = bestT;
			d->grabOffset = (hitIndex == kHandleScreen ? in.screenPos : content[hitIndex].c) - p;
			handleGlow[hitIndex] = 1.0f;
			out.grabbed = true;
			out.dragging = true;
			if (d->drag == Impl::kScreenTarget) {
				out.screenDragging = true;
				out.screenCenter = in.screenPos;
			}
		}
	} else if (hitKind == kHitCorner) {
		const Quad &q = targetQuad(cornerTarget);
		out.hitValid = true;
		out.hit = in.tip + in.dir * bestT;
		out.hitRight = q.r;
		out.hitUp = q.u;
		out.hitRadius = 0.012f * bestT;
		if (in.triggerPressed) {
			d->resize = cornerTarget;
			d->resizeSx = cornerSx;
			d->resizeSy = cornerSy;
			d->resizeRef = clampf(cornerK, 0.3f, 3.0f);
			out.grabbed = true;
			out.dragging = true;
		}
	} else if (hitKind == kHitWrist) {
		const Quad &q = hitIndex == 0 ? gearQ : menuQ;
		out.hitValid = true;
		out.hit = q.c + q.r * (hx * q.hw) + q.u * (hy * q.hh) + cross(q.r, q.u) * 0.001f;
		out.hitRight = q.r;
		out.hitUp = q.u;
		out.hitRadius = 0.010f * bestT;
		if (hitIndex == 0) {
			if (in.triggerPressed) {
				// Le menu s'ouvre au-dessus de la main et reste là.
				d->menuOpen = !d->menuOpen;
				if (d->menuOpen)
					out.gearMenuOpened = true;
				d->menuPos = gearQ.c + gearQ.u * (kMenuH * kMenuScale * 0.5f + 0.05f);
				d->menuRight = gearQ.r;
				d->menuUp = gearQ.u;
				out.clicked = true;
			}
		} else {
			int row = (int)((1.0f - hy) * 0.5f * (float)kMenuRows);
			row = row < 0 ? 0 : row >= kMenuRows ? kMenuRows - 1 : row;
			d->menuHot = row;
			if (in.triggerPressed && row >= 1) {
				out.clicked = true;
				if (row == 1) {
					out.action = kUiActionToggleLanguage;
				} else if (row == 2) {
					out.action = kUiActionToggleDetach;
				} else if (row == 3) {
					out.action = kUiActionToggleDungeon;
				} else if (row == 4) {
					out.action = kUiActionToggleKeyboard;
					d->menuOpen = false;
				} else if (row == 5) {
					out.action = kUiActionToggleHelp;
					d->menuOpen = false;
				} else {
					d->menuOpen = false;
				}
			}
		}
	} else if (hitKind == kHitPanel) {
		const Quad &q = content[hitIndex];
		out.hitValid = true;
		out.hit = q.c + q.r * (hx * q.hw) + q.u * (hy * q.hh) + cross(q.r, q.u) * 0.001f;
		out.hitRight = q.r;
		out.hitUp = q.u;
		out.hitRadius = 0.010f * bestT;
		float u = (hx + 1.0f) * 0.5f, v = (1.0f - hy) * 0.5f;
		if (hitIndex == kPanelHelp) {
			if (in.triggerPressed) {
				out.action = kUiActionCloseHelp;
				out.clicked = true;
			}
		} else if (hitIndex == kPanelKeyboard) {
			int col = (int)(u * (float)kCols), row = (int)(v * (float)kKbH / (float)kCellH);
			for (size_t k = 0; k < d->keys.size(); k++) {
				const Key &key = d->keys[k];
				if (key.row == row && col >= key.col && col < key.col + key.span)
					d->hotKey = (int)k;
			}
			if (d->hotKey >= 0 && in.triggerPressed) {
				const Key &key = d->keys[d->hotKey];
				out.clicked = true;
				d->pressKey = d->hotKey;
				d->pressFlash = 0.15f;
				if (key.action == -1) {
					d->shift = !d->shift;
					d->kbDirty = true;
				} else if (key.action != kUiActionNone) {
					out.action = key.action;
				} else if (key.key && out.keyCount < 4) {
					bool letter = key.key >= 'a' && key.key <= 'z';
					out.keys[out.keyCount] = key.key;
					out.keyMods[out.keyCount] = (letter && d->shift) ? kUiModShift : 0;
					out.keyCount++;
					if (d->shift) {
						d->shift = false;
						d->kbDirty = true;
					}
				}
			}
		} else {
			const Source &src = kSources[hitIndex];
			bool off = hitIndex == kPanelCompass && in.compassBlank; // fenêtre éteinte : aucun clic
			out.pointerValid = !off;
			out.buttonsToGame = !off;
			out.gu = (src.x + u * src.w) / 320.0f;
			out.gv = (src.y + v * src.h) / 200.0f;
		}
	}

	// --- 4. Ce que la scène doit dessiner ---
	int np = 0, nh = 0;
	for (int i = 0; i < kUiPanelCount; i++) {
		if (!visible[i])
			continue;
		const Quad &q = content[i];
		const Source &src = kSources[i];
		if (np < SceneState::kMaxPanels) {
			SceneState::Panel &p = scene.panels[np++];
			p = SceneState::Panel();
			p.visible = true;
			p.center = q.c;
			p.right = q.r;
			p.up = q.u;
			p.halfW = q.hw;
			p.halfH = q.hh;
			p.border = clampf(0.012f * panelDist[i], 0.004f, 0.012f);
			if (i == kPanelHelp) {
				p.texture = d->helpTex;
				p.texW = kHelpW;
				p.texH = kHelpH;
			} else if (i == kPanelKeyboard) {
				p.texture = d->kbTex;
				p.texW = kKbW;
				p.texH = kKbH;
				int k = d->pressFlash > 0.0f ? d->pressKey : d->hotKey;
				if (k >= 0) {
					const Key &key = d->keys[k];
					p.hot[0] = (float)(key.col * kCellW) / (float)kKbW;
					p.hot[1] = (float)(key.row * kCellH) / (float)kKbH;
					p.hot[2] = (float)((key.col + key.span) * kCellW) / (float)kKbW;
					p.hot[3] = (float)((key.row + 1) * kCellH) / (float)kKbH;
				}
			} else {
				p.u0 = src.x / 320.0f;
				p.u1 = (src.x + src.w) / 320.0f;
				p.v0 = src.y / 200.0f;
				p.v1 = (src.y + src.h) / 200.0f;
				p.blank = i == kPanelCompass && in.compassBlank;
				if (i == kPanelParty && in.partyMark[2] > 0.0f) {
					p.hotOutline = true;
					p.hot[0] = in.partyMark[0] / 320.0f;
					p.hot[1] = in.partyMark[1] / 200.0f;
					p.hot[2] = (in.partyMark[0] + in.partyMark[2]) / 320.0f;
					p.hot[3] = (in.partyMark[1] + in.partyMark[3]) / 200.0f;
				}
			}
		}
	}
	// Invitation du premier lancement : un peu sous le regard, à portée de lecture, elle suit la tête
	// sans à-coups. Elle s'efface quand on regarde sa main (l'engrenage prend le relais).
	if (in.gearHint && placed && !d->menuOpen && !gearVisible && d->hintTex && np < SceneState::kMaxPanels) {
		Vec3 flat = {in.headDir.x, 0.0f, in.headDir.z};
		float fl = length(flat);
		flat = fl > 1e-3f ? flat * (1.0f / fl) : Vec3{0, 0, -1};
		Vec3 target = in.headPos + flat * 0.80f + Vec3{0, -0.36f, 0};
		if (!d->hintPlaced) {
			d->hintPos = target;
			d->hintPlaced = true;
		}
		d->hintPos = d->hintPos + (target - d->hintPos) * clampf(in.dt * 2.0f, 0.0f, 1.0f);
		Vec3 n = in.headPos - d->hintPos;
		float nl = length(n);
		n = nl > 1e-3f ? n * (1.0f / nl) : Vec3{0, 0, 1};
		Vec3 r = cross({0, 1, 0}, n);
		float rl = length(r);
		r = rl > 1e-4f ? r * (1.0f / rl) : Vec3{1, 0, 0};
		SceneState::Panel &p = scene.panels[np++];
		p = SceneState::Panel();
		p.visible = true;
		p.center = d->hintPos;
		p.right = r;
		p.up = cross(n, r);
		p.halfW = 0.21f;
		p.halfH = 0.21f * (float)kHintH / (float)kHintW;
		p.border = 0.003f;
		p.texture = d->hintTex;
		p.texW = kHintW;
		p.texH = kHintH;
	}
	for (int w = 0; w < 2; w++) {
		if ((w == 0 ? !gearVisible : !d->menuOpen) || np >= SceneState::kMaxPanels)
			continue;
		const Quad &q = w == 0 ? gearQ : menuQ;
		SceneState::Panel &p = scene.panels[np++];
		p = SceneState::Panel();
		p.visible = true;
		p.center = q.c;
		p.right = q.r;
		p.up = q.u;
		p.halfW = q.hw;
		p.halfH = q.hh;
		p.border = 0.004f;
		p.texture = w == 0 ? d->gearTex : d->menuTex;
		p.texW = w == 0 ? kGearSize : kMenuW;
		p.texH = w == 0 ? kGearSize : kMenuH;
		if (w == 1 && d->menuHot >= 1) {
			p.hot[0] = 0.0f;
			p.hot[1] = (float)d->menuHot / (float)kMenuRows;
			p.hot[2] = 1.0f;
			p.hot[3] = (float)(d->menuHot + 1) / (float)kMenuRows;
		}
	}
	for (int i = 0; i < kHandleCount; i++) {
		if (!handleShown(i) || nh >= SceneState::kMaxHandles)
			continue;
		SceneState::Handle &h = scene.handles[nh++];
		const Quad &hq = handle[i];
		h.visible = true;
		h.center = hq.c;
		h.right = hq.r;
		h.up = hq.u;
		h.halfW = hq.hw;
		h.halfH = hq.hh;
		h.glow = handleGlow[i];
		h.alpha = 0.8f + 0.2f * handleGlow[i];
	}
	// Le coin visé ou tenu : une équerre de laiton.
	int shown = d->resize != -1 ? d->resize : cornerTarget;
	if (shown != -1) {
		const Quad &q = targetQuad(shown);
		float m = targetMargin(shown);
		float sx = d->resize != -1 ? d->resizeSx : cornerSx, sy = d->resize != -1 ? d->resizeSy : cornerSy;
		float dist = length(q.c - in.anchorPos);
		float len = clampf(0.075f * dist, 0.03f, 0.26f), th = clampf(0.008f * dist, 0.003f, 0.03f);
		len = fminf(len, fminf(q.hw, q.hh) + m);
		// Devant le cadre en relief de l'écran ; à fleur des autres fenêtres.
		float lift = shown == Impl::kScreenTarget ? 0.07f * in.dispW : 0.003f;
		Vec3 corner = q.c + q.r * (sx * (q.hw + m)) + q.u * (sy * (q.hh + m)) + cross(q.r, q.u) * lift;
		for (int k = 0; k < 2 && nh < SceneState::kMaxHandles; k++) {
			SceneState::Handle &h = scene.handles[nh++];
			h.visible = true;
			h.right = q.r;
			h.up = q.u;
			h.center = k == 0 ? corner - q.r * (sx * (len * 0.5f - th)) : corner - q.u * (sy * (len * 0.5f - th));
			h.halfW = k == 0 ? len * 0.5f : th;
			h.halfH = k == 0 ? th : len * 0.5f;
			h.glow = d->resize != -1 ? 1.0f : 0.7f;
			h.alpha = 1.0f;
		}
	}
}

} // namespace lolvr
