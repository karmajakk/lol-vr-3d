// lol-vr — interface détachée : panneaux, poignées, clavier et pointeur.

#include "ui.h"

#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <algorithm>
#include <string>
#include <utility>
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
const int kGearSize = 64, kHintW = 440, kHintH = 84, kMenuW = 360, kMenuRowH = 44, kMenuMaxRows = 7, kMenuFootH = 38;
// Panneau d'aide
const int kHelpW = 640, kHelpH = 520, kChooserH = 624; // (le choix du premier lancement : un peu plus haut, pour les crédits)
const int kInfoH = 0; // (plus de lignes d'information sous les touches)
const int kKbW = kCellW * kCols, kKbH = kCellH * kRows + kInfoH;

// L'atlas : le bandeau, le nom du niveau, la carte (carrée), la légende.
// (la page du jeu, 320 x 200, en pixels doublés, sous le bandeau)
const int kAtlasW = 640, kAtlasH = 400;
// La case vide en bas à droite de la page du jeu : on y écrit « Fermer » (pixels de la fenêtre).
const int kAtlasCloseX0 = 553, kAtlasCloseX1 = 630, kAtlasCloseY0 = 352, kAtlasCloseY1 = 390;

const Source kSources[kUiPanelCount] = {
	{0, 0, 100, 150},    // à droite : recomposé (voir compose), taille variable
	{83, 178, 237, 22},  // inventaire
	{0, 0, 236, 40},     // à gauche : recomposé, bandeau, (parchemin des sorts,) messages
	{0, 0, (float)kAtlasW, (float)kAtlasH}, // l'atlas, dessiné par l'application
	{0, 0, (float)kKbW, (float)kKbH},
	{0, 0, (float)kHelpW, (float)kHelpH},
};

// Fenêtres recomposées : des morceaux de l'image du jeu recopiés dans une petite image, chacun à
// sa place et à sa taille (d* : dans la petite image, s* : dans l'image du jeu 320 x 200).
struct Piece {
	int dx, dy, dw, dh, sx, sy, sw, sh;
	bool contains(float x, float y) const { return x >= dx && y >= dy && x < dx + dw && y < dy + dh; }
};
struct Composite {
	int texW = 0, texH = 0;
	GLuint tex = 0;
	std::vector<uint8_t> px;
	std::vector<Piece> pieces;
	int w = 0, h = 0;      // partie utilisée
	uint32_t serial = 0;
	int key = -1;          // nombre de personnages, fenêtre éteinte... : refaire si cela change
};
const int kCompParty = 0, kCompSpells = 1, kCompAtlas = 2, kCompInv = 3;
const int kCompTexW[4] = {104, 240, kAtlasW, 240}, kCompTexH[4] = {200, 288, kAtlasH, 132};
const int kSpellTabH = 0; // (plus de bandeau au-dessus du livre de sorts) // bandeau du haut de la fenêtre de gauche : ouvrir ou replier le livre de sorts

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

// --- La police et les couleurs du jeu -----------------------------------------------------------

// La police des menus du jeu (FONT9P.FNT, format DOSFont de Westwood) : chaque caractère a sa
// largeur (l'espace qui le suit comprise), 9 pixels de haut, un bit de couleur par pixel.
struct GameFont {
	std::vector<uint8_t> data;
	int glyphs = 0, height = 0, bitmaps = 0, widths = 0, heights = 0;
	bool ok() const { return glyphs > 0; }
	int u16(size_t o) const { return o + 1 < data.size() ? (int)(data[o] | (data[o + 1] << 8)) : -1; }
	bool parse(std::vector<uint8_t> &&d) {
		data = std::move(d);
		glyphs = 0;
		if (data.size() < 16 || u16(2) != 0x0500)
			return false;
		const int desc = u16(4);
		if (desc < 0 || desc + 5 >= (int)data.size())
			return false;
		const int n = data[desc + 3] + 1;
		height = data[desc + 4];
		bitmaps = u16(6);
		widths = u16(8);
		heights = u16(12);
		const int size = (int)data.size();
		if (height < 4 || height > 16 || bitmaps < 0 || widths < 0 || heights < 0 || bitmaps + 2 * n > size || widths + n > size || heights + 2 * n > size)
			return false;
		glyphs = n;
		return true;
	}
	int width(unsigned c) const { return c < (unsigned)glyphs ? data[widths + c] : 0; }
};
GameFont g_font;

// Latin-1 -> code du jeu (page de code 437 des PC de l'époque) pour les lettres accentuées.
unsigned gameCode(unsigned char c) {
	static const unsigned char from[] = {0xE9, 0xE8, 0xEA, 0xEB, 0xE0, 0xE2, 0xE4, 0xE7, 0xC7, 0xC9 /* É : E, plus lisible */, 0xEE, 0xEF, 0xF4, 0xF6, 0xFB, 0xF9, 0xFC, 0xC4, 0xD6, 0xDC, 0xE1, 0xED, 0xF3, 0xFA, 0xF1, 0xD1, 0xDF};
	static const unsigned char to[] = {130, 138, 136, 137, 133, 131, 132, 135, 128, 'E', 140, 139, 147, 148, 150, 151, 129, 142, 153, 154, 160, 161, 162, 163, 164, 165, 225};
	if (c < 128)
		return c;
	for (size_t i = 0; i < sizeof(from); i++)
		if (from[i] == c)
			return to[i];
	return '?';
}

// Le texte dans la langue de l'application : anglais, français ou allemand.
const char *tr(int lang, const char *en, const char *fr, const char *de) { return lang == kLangFrench ? fr : lang == kLangGerman ? de : en; }

// Les couleurs du menu des options du jeu : terre cuite, reliefs clair et sombre, texte or,
// titre blanc ; le bleu du choix en cours du menu principal.
struct Rgb {
	uint8_t r, g, b;
};
const Rgb kFace = {164, 89, 77}, kLight = {206, 124, 100}, kDark = {119, 52, 48};
const Rgb kGold = {246, 207, 121}, kWhite = {255, 255, 255}, kBlue = {79, 192, 255}, kPale = {240, 228, 204};

// --- Dessin des fenêtres ------------------------------------------------------------------------

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
	void rect(int x0, int y0, int x1, int y1, Rgb c) { rect(x0, y0, x1, y1, c.r, c.g, c.b); }
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

	// L'écriture du jeu (Latin-1), à `scale` pixels par pixel de la police. Sans la police du jeu, la
	// police 8 x 16 (à peu près la même taille à l'échelle 2).
	static int fallbackScale(int scale) { return scale >= 3 ? 2 : 1; }
	static int textW(const std::string &s, int scale) {
		if (!g_font.ok())
			return (int)s.size() * 8 * fallbackScale(scale);
		int w = 0;
		for (unsigned char ch : s)
			w += g_font.width(gameCode(ch));
		return w * scale;
	}
	static int textH(int scale) { return g_font.ok() ? g_font.height * scale : 16 * fallbackScale(scale); }
	void glyph(unsigned c, int x, int y, int s, Rgb col) {
		if (c >= (unsigned)g_font.glyphs)
			return;
		const std::vector<uint8_t> &d = g_font.data;
		int src = g_font.u16((size_t)g_font.bitmaps + 2 * c);
		const int w = d[(size_t)g_font.widths + c], h1 = d[(size_t)g_font.heights + 2 * c], h2 = d[(size_t)g_font.heights + 2 * c + 1];
		if (src <= 0 || !w)
			return;
		for (int row = 0; row < h2; row++) {
			uint8_t b = 0;
			for (int i = 0; i < w; i++) {
				int v;
				if (i & 1) {
					v = b >> 4;
				} else {
					if (src >= (int)d.size())
						return;
					b = d[(size_t)src++];
					v = b & 15;
				}
				if (v)
					rect(x + i * s, y + (h1 + row) * s, x + (i + 1) * s, y + (h1 + row + 1) * s, col);
			}
		}
	}
	void gameLine(const std::string &s, int x, int y, int scale, Rgb col) {
		for (unsigned char ch : s) {
			unsigned c = gameCode(ch);
			glyph(c, x, y, scale, col);
			x += g_font.width(c) * scale;
		}
	}
	// Comme dans le jeu : une ombre à gauche et dessous.
	void gtext(const std::string &s, int x, int y, int scale, Rgb col, Rgb shadow = kDark) {
		if (!g_font.ok()) {
			const int fs = fallbackScale(scale);
			text(s, x - fs, y + fs, fs, shadow.r, shadow.g, shadow.b);
			text(s, x, y, fs, col.r, col.g, col.b);
			return;
		}
		gameLine(s, x, y + scale, scale, shadow);
		gameLine(s, x - scale, y, scale, shadow);
		gameLine(s, x - scale, y + scale, scale, shadow);
		gameLine(s, x, y, scale, col);
	}
	void gcenter(const std::string &s, int x0, int x1, int y, int scale, Rgb col) { gtext(s, x0 + (x1 - x0 - textW(s, scale)) / 2, y, scale, col); }
	// La plus grande échelle (jusqu'à maxScale) où le texte tient dans maxW.
	static int fit(const std::string &s, int maxW, int maxScale) {
		int sc = maxScale;
		while (sc > 1 && textW(s, sc) > maxW)
			sc--;
		return sc;
	}
	// Coupe un paragraphe en lignes de largeur maxW au plus.
	static std::vector<std::string> wrap(const std::string &s, int maxW, int scale) {
		std::vector<std::string> lines;
		std::string line, word;
		auto flush = [&]() {
			if (word.empty())
				return;
			std::string t = line.empty() ? word : line + " " + word;
			if (!line.empty() && textW(t, scale) > maxW) {
				lines.push_back(line);
				line = word;
			} else {
				line = t;
			}
			word.clear();
		};
		for (char ch : s) {
			if (ch == ' ')
				flush();
			else
				word += ch;
		}
		flush();
		if (!line.empty())
			lines.push_back(line);
		return lines;
	}
	// Un bouton ou un cadre du jeu : face terre cuite, bord clair en haut et à gauche, sombre en bas et
	// à droite (l'inverse s'il est enfoncé).
	void bevel(int x0, int y0, int x1, int y1, int t, bool raised, Rgb face = kFace) {
		rect(x0, y0, x1, y1, face);
		const Rgb hi = raised ? kLight : kDark, lo = raised ? kDark : kLight;
		rect(x0, y1 - t, x1, y1, lo);
		rect(x1 - t, y0, x1, y1, lo);
		rect(x0, y0, x1 - 1, y0 + t, hi);
		rect(x0, y0, x0 + t, y1 - 1, hi);
	}
	void window() { bevel(0, 0, w, h, 3, true); }
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
	GLuint helpTex = 0, gearTex = 0, menuTex = 0, hintTex = 0, chooserTex = 0, noticeTex = 0;
	bool helpDirty = true, menuDirty = true, hintDirty = true, chooserDirty = true;
	bool menuKeyboard = false; // état montré dans le menu
	int dungeonMode = 1;
	void renderHint();
	// Invitation du premier lancement : elle suit doucement le regard.
	bool hintPlaced = false;
	Vec3 hintPos = {0, 0, 0};
	void renderHelp();
	// Profils de manettes
	struct Profile {
		std::string name; // vide : « Classique »
		uint8_t fn[kSlotCount];
	};
	std::vector<Profile> profiles;
	int activeProfile = 0;
	uint8_t draft[kSlotCount] = {0};
	bool draftChanged() const { return memcmp(draft, profiles[activeProfile].fn, kSlotCount) != 0; }
	void selectProfileIndex(int i) {
		activeProfile = i;
		memcpy(draft, profiles[i].fn, kSlotCount);
		helpDirty = editDirty = true;
	}
	int helpMode = 0; // 0 l'aide, 1 la fenêtre de modification
	bool helpWasOpen = false, naming = false, namingKbSeen = false;
	std::string nameText;
	int helpHot = -1, editHot = -1;
	GLuint editTex = 0;
	bool editDirty = true;
	void renderEditor();
	void renderChooser();
	void renderGear();
	void renderMenu();
	void upload(GLuint &tex, const std::vector<uint8_t> &px, int w, int h, const char *name = "fenetre");
	// Menu du poignet : ouvert, il reste où il est apparu.
	bool menuOpen = false;
	bool menuAhead = false; // à placer devant le joueur à la prochaine image
	int vrMode = 1, classicView = 0;
	// Le rappel « L3 change la vue », devant le joueur
	bool noticeOpen = false, noticeAhead = false, noticeDirty = true;
	int noticeKind = 0; // 0 : « L3 change la vue », 1 : crédits et prérequis
	Vec3 noticePos{0, 0, 0}, noticeRight{1, 0, 0}, noticeUp{0, 1, 0};
	void renderNotice();
	Vec3 menuPos = {0, 0, 0}, menuRight = {1, 0, 0}, menuUp = {0, 1, 0};
	int menuHot = -1;
	// Mode immersif : pas de ligne « vue » (l'écran du jeu n'apparaît que quand il le faut).
	// Lignes : le titre, le mode VR, la vue, l'atlas, les manettes, crédits et prérequis, fermer.
	int menuRows() const { return dungeonMode ? 5 : 6; }
	// (dessous, sans bouton, les remerciements)
	int menuH() const { return kMenuRowH * menuRows() + kMenuFootH; }
	int menuItem(int row) const { return menuRows() == 5 && row >= 2 ? row + 1 : row; }
	// Colonne du groupe recomposée
	int chooserHot = -1; // bouton visé dans la fenêtre du choix de la vue
	Composite comp[4];
	bool atlasCollapsed = false, atlasTabHot = false, atlasOn = true;
	bool invExpanded = false;
	int invHot = -1; // case visée : 0..47, 100 le bouton qui déplie, 101 le tri
	void composeInv(const UiFrameInput &in);
	uint32_t atlasSerial = 0;
	int atlasKey = -1;
	void composeAtlas(const UiFrameInput &in);
	std::vector<uint8_t> heldFrame; // l'image du jeu d'avant la boîte de texte (voir textBoxActive)
	int heldW = 0, heldH = 0;
	bool spellsCollapsed = false; // (le livre de sorts ne se replie plus)
	bool spellTabHot = false;
	void compose(int which, const UiFrameInput &in);
	int compOf(int panel) const { return panel == kPanelParty ? kCompParty : panel == kPanelSpells ? kCompSpells : panel == kPanelAtlas ? kCompAtlas : panel == kPanelInventory ? kCompInv : -1; }
	// L'atlas à droite de la fenêtre des personnages, haut contre haut.
	void placeAtlas(UiPlacement *placement);
	// Gâchette maintenue pour déplacer : l'appui en attente, et ce que vise le doigt.
	bool pendPress = false;
	bool pokeArmed[2] = {true, true}; // le doigt s'est écarté du clavier depuis la dernière touche
	double pendSince = 0.0;
	bool lastHoldable = false;
	int lastHoldTarget = -1;
	float lastHoldT = 1.0f;
	Vec3 lastHoldOffset = {0, 0, 0};
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
	keys.push_back({0, 0, 4, "", "", 0, kUiActionToggleDetach});
	keys.push_back({4, 0, 4, "", "", 0, kUiActionResetLayout});
	keys.push_back({8, 0, 3, "", "", 0, kUiActionCloseKeyboard});
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
	cv.window();
	const int lg = language;
	// (en Latin-1, comme les touches)
	for (Key &k : keys) {
		if (k.action == kUiActionToggleFire)
			k.label = tr(lg, "Fire", "Feu", "Feuer"); // allumé : la touche est enfoncée, son nom en bleu
		else if (k.action == kUiActionToggleDetach)
			k.label = tr(lg, "Panels", "Panneaux", "Fenster");
		else if (k.action == kUiActionResetLayout)
			k.label = tr(lg, "Reset layout", "Tout ranger", "Ordnen");
		else if (k.action == kUiActionCloseKeyboard)
			k.label = tr(lg, "Close", "Fermer", "Schlie\xDF" "en");
		else if (k.action == -1) {
			k.label = tr(lg, "Shift", "Maj", "Umschalt");
			k.shiftLabel = tr(lg, "SHIFT", "MAJ", "UMSCHALT");
		} else if (k.key == 27)
			k.label = tr(lg, "Esc", "\xC9" "chap", "Esc");
		else if (k.key == ' ')
			k.label = tr(lg, "Space", "Espace", "Leertaste");
		else if (k.key == 13)
			k.label = tr(lg, "Enter", "Entr\xE9" "e", "Eingabe");
	}
	for (size_t i = 0; i < keys.size(); i++) {
		const Key &k = keys[i];
		int x0 = k.col * kCellW + 2, y0 = k.row * kCellH + 2, x1 = (k.col + k.span) * kCellW - 2, y1 = (k.row + 1) * kCellH - 2;
		bool option = k.row == 0;
		bool lit = (k.action == -1 && shift) || (k.action == kUiActionToggleFire && fireOn) || (k.action == kUiActionToggleDetach && detached);
		// Une touche est un bouton du jeu ; une option active est enfoncée, son nom en bleu.
		cv.bevel(x0, y0, x1, y1, 2, !lit);
		const Rgb col = lit ? kBlue : option ? kWhite : kGold;
		if (k.action == -1) {
			// Majuscules : une flèche vers le haut (le mot ne tient pas sur la touche)
			const int cx = (x0 + x1) / 2 + (lit ? 1 : 0), cy = (y0 + y1) / 2 + (lit ? 1 : 0);
			for (int pass = 0; pass < 2; pass++) {
				const int o = pass ? 0 : 2;
				const Rgb c = pass ? col : kDark;
				for (int r = 0; r < 9; r++)
					cv.rect(cx - r - o, cy - 11 + r + o, cx + r + 1 - o, cy - 10 + r + o, c);
				cv.rect(cx - 4 - o, cy - 2 + o, cx + 5 - o, cy + 10 + o, c);
			}
			continue;
		}
		const std::string &label = (shift && !k.shiftLabel.empty()) ? k.shiftLabel : k.label; // déjà en Latin-1
		int scale = Canvas::fit(label, x1 - x0 - 6, label.size() == 1 ? 3 : 2);
		int tw = Canvas::textW(label, scale), th = Canvas::textH(scale);
		cv.gtext(label, (x0 + x1 - tw) / 2 + (lit ? 1 : 0), (y0 + y1 - th) / 2 + (lit ? 1 : 0), scale, col);
	}
	upload(kbTex, cv.px, kKbW, kKbH, "clavier");
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

void Ui::Impl::upload(GLuint &tex, const std::vector<uint8_t> &px, int w, int h, const char *name) {
	// Banc d'essai : chaque fenêtre dessinée est aussi écrite en image (LOLVR_UI_DUMP = dossier).
	if (const char *dir = getenv("LOLVR_UI_DUMP")) {
		std::string path = std::string(dir) + "/" + name + ".ppm";
		if (FILE *f = fopen(path.c_str(), "wb")) {
			fprintf(f, "P6\n%d %d\n255\n", w, h);
			for (int i = 0; i < w * h; i++)
				fwrite(&px[(size_t)i * 4], 1, 3, f);
			fclose(f);
		}
	}
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

// Les deux fenêtres recomposées :
//  - à droite, les personnages l'un sous l'autre, à leur droite la colonne de l'atlas (lampe,
//    bourse), et sous la bourse le bouton du repos ;
//  - à gauche, le parchemin des sorts (pixels doublés), et dessous la ligne de messages, à sa largeur.
// La fenêtre d'inventaire : la barre rapide (les 8 premières cases) toujours, et dépliée les 5
// autres rangées (40 cases, triables). Les cases sont celles que dessine le jeu.
void Ui::Impl::composeInv(const UiFrameInput &in) {
	Composite &c = comp[kCompInv];
	const int key = 1000 + (invExpanded ? 1 : 0) + (invHot + 1) * 2;
	if (c.tex && c.serial == in.invSerial && c.key == key)
		return;
	c.serial = in.invSerial;
	c.key = key;
	c.texW = kCompTexW[kCompInv];
	c.texH = kCompTexH[kCompInv];
	c.pieces.clear();
	c.w = 200;
	c.h = invExpanded ? 132 : 22;
	c.px.assign((size_t)c.texW * c.texH * 4, 0);
	auto put = [&](int x, int y, uint8_t r, uint8_t g, uint8_t b) {
		if (x < 0 || y < 0 || x >= c.texW || y >= c.texH)
			return;
		uint8_t *p = &c.px[((size_t)y * c.texW + x) * 4];
		p[0] = r; p[1] = g; p[2] = b; p[3] = 255;
	};
	for (int y = 0; y < c.h; y++)
		for (int x = 0; x < c.w; x++)
			put(x, y, 42, 28, 20);
	const int rows = invExpanded ? 6 : 1;
	for (int y = 0; y < rows * 22; y++)
		for (int x = 0; x < 176; x++) {
			const uint8_t *s = in.invRgb + ((size_t)y * 176 + x) * 3;
			put(x, y, s[0], s[1], s[2]);
		}
	// Un filet entre la barre rapide et le reste.
	if (invExpanded)
		for (int x = 0; x < 176; x++)
			put(x, 21, 200, 160, 80);
	auto box = [&](int x0, int y0, int x1, int y1, bool hot) {
		for (int x = x0; x < x1; x++) {
			put(x, y0, hot ? 255 : 140, hot ? 230 : 108, hot ? 140 : 72);
			put(x, y1 - 1, hot ? 255 : 140, hot ? 230 : 108, hot ? 140 : 72);
		}
		for (int y = y0; y < y1; y++) {
			put(x0, y, hot ? 255 : 140, hot ? 230 : 108, hot ? 140 : 72);
			put(x1 - 1, y, hot ? 255 : 140, hot ? 230 : 108, hot ? 140 : 72);
		}
	};
	if (invHot >= 0 && invHot < 48 && invHot / 8 < rows) {
		const int cx = (invHot % 8) * 22, cy = (invHot / 8) * 22;
		box(cx, cy, cx + 22, cy + 22, true);
		box(cx + 1, cy + 1, cx + 21, cy + 21, true);
	}
	// Les boutons à droite : déplier / replier, et trier.
	auto button = [&](int y0, bool hot) {
		for (int y = y0 + 1; y < y0 + 21; y++)
			for (int x = 179; x < 199; x++)
				put(x, y, hot ? 110 : 80, hot ? 80 : 58, hot ? 52 : 40);
		box(178, y0, 200, y0 + 22, hot);
	};
	button(0, invHot == 100);
	const uint8_t gr = invHot == 100 ? 255 : 230, gg = invHot == 100 ? 235 : 190, gb = invHot == 100 ? 170 : 90;
	for (int i = 0; i < 6; i++) { // un triangle : vers le bas = déplier, vers le haut = replier
		const int w = invExpanded ? i : 5 - i;
		for (int x = 189 - w; x <= 189 + w; x++)
			put(x, 8 + i * 1 + (invExpanded ? 0 : 0), gr, gg, gb);
	}
	if (invExpanded) {
		button(22, invHot == 101);
		const uint8_t sr = invHot == 101 ? 255 : 230, sg = invHot == 101 ? 235 : 190, sb = invHot == 101 ? 170 : 90;
		for (int i = 0; i < 4; i++) // trois barres de longueurs croissantes : le tri
			for (int x = 183; x < 183 + 4 + i * 3; x++)
				put(x, 28 + i * 3, sr, sg, sb);
	}
	upload(c.tex, c.px, c.texW, c.texH, "inventaire");
}

void Ui::Impl::compose(int which, const UiFrameInput &in) {
	if (which == kCompInv && in.invRgb) {
		composeInv(in);
		return;
	}
	Composite &c = comp[which];
	const int n = in.characters < 0 ? 0 : in.characters > 4 ? 4 : in.characters;
	const bool blank = in.compassBlank;
	const int key = (which == kCompInv ? 1 : which == kCompParty ? n * 2 + (blank ? 1 : 0) : (spellsCollapsed ? 1 : 0) + language * 2 + (spellTabHot ? 4 : 0)) + (in.textBoxActive ? 64 : 0) + (in.spellsCovered ? 128 : 0);
	if (c.tex && in.gameSerial == c.serial && key == c.key)
		return;
	// Pendant une boîte de texte, les morceaux viennent de l'image d'avant ; sinon on la garde à jour.
	const uint8_t *game = in.gameRgba;
	if (in.gameRgba && in.gameW > 0 && in.gameH > 0) {
		if (in.textBoxActive && heldW == in.gameW && heldH == in.gameH) {
			game = heldFrame.data();
		} else if (!in.textBoxActive && which == kCompParty) {
			// Pas une image assombrie (le fondu au noir qui ouvre une scène) : sinon les fenêtres
			// resteraient noires toute la conversation.
			uint64_t sum = 0;
			int n = 0;
			for (int y = 140; y < 200; y += 4)
				for (int x = 0; x < 320; x += 4, n++) {
					const uint8_t *q = in.gameRgba + ((size_t)(y * in.gameH / 200) * in.gameW + (size_t)(x * in.gameW / 320)) * 4;
					sum += q[0] + q[1] + q[2];
				}
			if (sum > (uint64_t)n * 3 * 24) {
				heldFrame.assign(in.gameRgba, in.gameRgba + (size_t)in.gameW * in.gameH * 4);
				heldW = in.gameW;
				heldH = in.gameH;
			}
		}
	}
	c.serial = in.gameSerial;
	c.key = key;
	c.texW = kCompTexW[which];
	c.texH = kCompTexH[which];
	c.pieces.clear();
	int band = -1; // début du bandeau des boutons (colonne de droite)
	if (which == kCompInv) {
		// La ligne d'objets du bas : pendant une grande boîte de texte, le jeu la recouvre ; on garde celle d'avant.
		c.pieces.push_back({0, 0, 237, 22, 83, 178, 237, 22});
		c.w = 237;
		c.h = 22;
	} else if (which == kCompParty) {
		for (int i = 0; i < n; i++)
			c.pieces.push_back({0, i * 36, 66, 34, in.portraitX[i], 143, 66, 34});
		c.pieces.push_back({68, 0, 32, 122, 288, 0, 32, 122});
		// Le repos (ZZZ) sous la bourse ; la disquette, elle, est sur la main gauche.
		c.pieces.push_back({73, 124, 22, 22, 33, 178, 22, 22});
		c.w = 100;
		c.h = n * 36 > 148 ? n * 36 : 148;
	} else {
		// Le bandeau (dessiné plus bas), puis le parchemin s'il est ouvert, puis les messages.
		int y = kSpellTabH;
		if (!spellsCollapsed) {
			c.pieces.push_back({6, y, 224, 242, 0, 0, 112, 121});
			y += 244;
		}
		// La ligne de messages pixel pour pixel : resserrée, elle perdait une colonne sur vingt (des
		// morceaux de lettres, comme des fautes de frappe).
		c.pieces.push_back({0, y, 236, 22, 82, 121, 236, 22});
		c.w = 236;
		c.h = y + 22;
	}
	c.px.assign((size_t)c.texW * c.texH * 4, 0);
	for (int i = 0; i < c.texW * c.texH; i++) {
		uint8_t *p = &c.px[(size_t)i * 4];
		p[0] = 42; p[1] = 28; p[2] = 20; p[3] = 255; // bois sombre entre les morceaux
	}
	// Le bandeau des boutons : un peu plus clair, séparé du reste par un filet.
	if (band >= 0)
		for (int y = band; y < c.h && y < c.texH; y++)
			for (int x = 0; x < c.w; x++) {
				uint8_t *p = &c.px[((size_t)y * c.texW + x) * 4];
				bool line = y < band + 1;
				p[0] = line ? 140 : 62; p[1] = line ? 108 : 44; p[2] = line ? 72 : 30;
			}
	if (in.gameRgba && in.gameW >= 320 && in.gameH >= 200) {
		for (const Piece &pc : c.pieces) {
			const bool off = which == kCompParty && pc.sx == 288 && blank;
			for (int y = 0; y < pc.dh; y++)
				for (int x = 0; x < pc.dw; x++) {
					int dx = pc.dx + x, dy = pc.dy + y;
					if (dx >= c.texW || dy >= c.texH)
						continue;
					uint8_t *p = &c.px[((size_t)dy * c.texW + dx) * 4];
					if (off) {
						p[0] = p[1] = p[2] = 0;
						continue;
					}
					int gx = pc.sx + x * pc.sw / pc.dw, gy = pc.sy + y * pc.sh / pc.dh;
					int sx = gx * in.gameW / 320, sy = gy * in.gameH / 200;
					// Le parchemin des sorts (au-dessus de la boîte de texte) reste à jour : un sort
					// appris pendant une conversation s'y écrit.
					const uint8_t *from = which == kCompSpells && pc.sy + pc.sh <= 122 && !in.spellsCovered ? in.gameRgba : game;
					const uint8_t *src = from + ((size_t)sy * in.gameW + sx) * 4;
					p[0] = src[0]; p[1] = src[1]; p[2] = src[2];
				}
		}
	}
	// Le bandeau du livre de sorts : un clic l'ouvre ou le replie.
	if (which == kCompSpells && kSpellTabH > 0) {
		// Un bouton du jeu, à la taille de ses pixels (doublés ici comme le parchemin).
		Canvas cv{c.w, kSpellTabH, std::vector<uint8_t>((size_t)c.w * kSpellTabH * 4)};
		cv.bevel(0, 0, c.w, kSpellTabH, 2, true, spellTabHot ? kLight : kFace);
		std::string label = latin1(tr(language, "Spellbook", "Livre de sorts", "Zauberbuch"));
		const int sc = 1;
		cv.gcenter(label, 0, c.w, (kSpellTabH - Canvas::textH(sc)) / 2 > 0 ? (kSpellTabH - Canvas::textH(sc)) / 2 : 0, sc, spellTabHot ? kWhite : kGold);
		for (int y = 0; y < kSpellTabH; y++)
			memcpy(&c.px[(size_t)y * c.texW * 4], &cv.px[(size_t)y * c.w * 4], (size_t)c.w * 4);
	}
	upload(c.tex, c.px, c.texW, c.texH, which == kCompParty ? "groupe" : which == kCompInv ? "inventaire" : "sorts");
}

// L'atlas : la page de l'atlas telle que le jeu la dessine (le moteur la refait à chaque pas),
// pixels doublés comme le livre de sorts, sous un bandeau qui la replie.
void Ui::Impl::composeAtlas(const UiFrameInput &in) {
	const UiMap &m = *in.map;
	Composite &c = comp[kCompAtlas];
	const int key = (atlasTabHot ? 2 : 0) + language * 4;
	if (c.tex && atlasSerial == m.serial && atlasKey == key)
		return;
	atlasSerial = m.serial;
	atlasKey = key;
	c.texW = kAtlasW;
	c.texH = kAtlasH;
	c.pieces.clear();
	c.w = kAtlasW;
	c.h = kAtlasH;
	Canvas cv{kAtlasW, kAtlasH, std::vector<uint8_t>((size_t)kAtlasW * kAtlasH * 4)};
	if (m.rgb)
		for (int y = 0; y < 400; y++)
			for (int x = 0; x < 640; x++) {
				const uint8_t *src = m.rgb + ((size_t)(y >> 1) * 320 + (x >> 1)) * 3;
				uint8_t *p = &cv.px[((size_t)y * kAtlasW + x) * 4];
				p[0] = src[0];
				p[1] = src[1];
				p[2] = src[2];
				p[3] = 255;
			}
	// « Fermer » dans la case vide de la page : un clic ferme la fenêtre.
	{
		const std::string t = latin1(tr(language, "Close", "Fermer", "Schließen"));
		const int sc = Canvas::fit(t, kAtlasCloseX1 - kAtlasCloseX0 - 8, 2);
		cv.gcenter(t, kAtlasCloseX0, kAtlasCloseX1, (kAtlasCloseY0 + kAtlasCloseY1 - Canvas::textH(sc)) / 2, sc, atlasTabHot ? kWhite : kGold);
	}
	upload(c.tex, cv.px, kAtlasW, kAtlasH, "atlas");
}

void Ui::Impl::placeAtlas(UiPlacement *placement) {
	const UiPlacement &p = placement[kPanelParty];
	const float pw = (comp[kCompParty].w ? (float)comp[kCompParty].w : kSources[kPanelParty].w) * p.scale * 0.5f;
	const float ph = (comp[kCompParty].h ? (float)comp[kCompParty].h : kSources[kPanelParty].h) * 1.2f * p.scale * 0.5f;
	UiPlacement &a = placement[kPanelAtlas];
	a.scale = clampf(p.scale * (0.0008f / 0.0034f), 0.0004f, 0.02f);
	const float aw = kAtlasW * a.scale * 0.5f, ah = kAtlasH * 1.2f * a.scale * 0.5f;
	const float dist = p.c >= 0.2f ? p.c : 1.0f;
	a.c = dist;
	a.a = wrapAngle(p.a - (pw + 0.03f + aw) / dist);
	a.b = clampf(p.b + (ph - ah) / dist, -1.35f, 1.35f);
}

// Boutons de la fenêtre du choix de la vue, en pixels de son image (même taille que l'aide).
const int kChooseY0 = 70, kChooseY1 = 292;
const int kChooseX[2][2] = {{24, 310}, {330, 616}};
// Dessous, la langue : trois boutons, le nom et le drapeau.
const int kLangY0 = 304, kLangY1 = 412;
const int kLangX[3][2] = {{24, 212}, {226, 414}, {428, 616}};

// Un drapeau de w x h pixels : anglais (Union Jack), français (le Québec), allemand.
static void drawFlag(Canvas &cv, int lang, int x0, int y0, int w, int h) {
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) {
			Rgb c;
			if (lang == kLangFrench) {
				// Le fleurdelisé du Québec (3:2) : croix blanche (1/6 de la largeur, 1/4 de la hauteur),
				// une fleur de lys au milieu de chaque quart bleu. La fleur : la couverture en blanc de
				// chaque pixel d'un quart de 40 x 24, mesurée sur le drapeau officiel.
				static const uint8_t kLys[24][40] = {
		{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
		{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
		{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
		{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
		{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 82, 108, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
		{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 32, 244, 251, 50, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
		{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 143, 255, 255, 168, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
		{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 214, 255, 255, 238, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
		{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 233, 255, 255, 254, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
		{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 192, 255, 255, 216, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
		{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 61, 85, 8, 108, 255, 255, 133, 4, 80, 67, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
		{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 94, 255, 255, 203, 31, 252, 255, 47, 184, 255, 255, 121, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
		{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 199, 255, 255, 255, 106, 197, 221, 84, 255, 255, 255, 225, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
		{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 181, 254, 116, 150, 182, 140, 164, 161, 168, 113, 254, 205, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
		{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 33, 182, 1, 3, 200, 117, 140, 193, 13, 1, 187, 45, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
		{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 13, 255, 255, 255, 255, 255, 255, 37, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
		{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 57, 57, 206, 227, 57, 57, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
		{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 83, 253, 255, 105, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
		{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 50, 213, 225, 63, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
		{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 86, 112, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
		{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
		{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
		{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
		{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
				};
				const int qw = w * 5 / 12, qh = h * 3 / 8; // un quart bleu
				const Rgb blue = {0, 61, 165};
				const bool inX = x < qw || x >= w - qw, inY = y < qh || y >= h - qh;
				if (!inX || !inY) {
					c = {255, 255, 255};
				} else {
					const int lx = (x < qw ? x : x - (w - qw)) * 40 / qw, ly = (y < qh ? y : y - (h - qh)) * 24 / qh;
					const int a = kLys[ly < 24 ? ly : 23][lx < 40 ? lx : 39];
					c = {(uint8_t)(blue.r + (255 - blue.r) * a / 255), (uint8_t)(blue.g + (255 - blue.g) * a / 255), (uint8_t)(blue.b + (255 - blue.b) * a / 255)};
				}
			} else if (lang == kLangGerman) {
				c = y * 3 < h ? Rgb{0, 0, 0} : y * 3 < 2 * h ? Rgb{221, 0, 0} : Rgb{255, 206, 0};
			} else {
				// Fond bleu, sautoirs blancs et rouges, croix blanche et rouge
				const float fx = (float)x + 0.5f, fy = (float)y + 0.5f, k = (float)h / (float)w;
				const float n = sqrtf(1.0f + k * k);
				const float d1 = fabsf(fy - fx * k) / n, d2 = fabsf(fy - ((float)h - fx * k)) / n;
				const float dd = d1 < d2 ? d1 : d2;
				const float cx = fabsf(fx - w * 0.5f), cy = fabsf(fy - h * 0.5f);
				c = {1, 33, 105};
				if (dd < h * 0.10f)
					c = {255, 255, 255};
				if (dd < h * 0.04f)
					c = {200, 16, 46};
				if (cx < h * 0.17f || cy < h * 0.17f)
					c = {255, 255, 255};
				if (cx < h * 0.10f || cy < h * 0.10f)
					c = {200, 16, 46};
			}
			cv.rect(x0 + x, y0 + y, x0 + x + 1, y0 + y + 1, c);
		}
	// Un liseré sombre, comme les boutons du jeu
	cv.rect(x0 - 1, y0 - 1, x0 + w + 1, y0, kDark);
	cv.rect(x0 - 1, y0 + h, x0 + w + 1, y0 + h + 1, kDark);
	cv.rect(x0 - 1, y0, x0, y0 + h, kDark);
	cv.rect(x0 + w, y0, x0 + w + 1, y0 + h, kDark);
}

// Crédits et prérequis : sur quoi le mod repose, et merci.
static const char *creditsTitle(int lang) { return tr(lang, "Credits & Requirements", "Crédits et prérequis", "Credits & Voraussetzungen"); }
// Mentions légales : projet de fan, sans lien avec les ayants droit, fichiers du jeu à fournir.
static const char *legalTitle(int lang) { return tr(lang, "Legal Notice", "Mentions légales", "Rechtlicher Hinweis"); }
static const char *requirementsText(int lang) {
	return tr(lang, "Unofficial, non-commercial fan project. Not affiliated with or endorsed by Electronic Arts, Westwood Studios, or Virgin Interactive. Requires legally obtained original game files (e.g., GOG release) to play.",
	          "Projet de fan non officiel et non commercial. Non affilié à ou approuvé par Electronic Arts, Westwood Studios ou Virgin Interactive. Nécessite les fichiers originaux du jeu obtenus légalement (ex. version GOG).",
	          "Inoffizielles, nicht kommerzielles Fanprojekt. Nicht mit Electronic Arts, Westwood Studios oder Virgin Interactive verbunden oder von ihnen unterstützt. Erfordert legal erworbene Originalspieldateien (z. B. die GOG-Version).");
}
static const char *creditsText(int lang) {
	return tr(lang, "This VR mod runs on ScummVM and was made possible thanks to the hard work of KForestland (lands-of-lore-1-re).",
	          "Ce mod VR fonctionne avec ScummVM et a été rendu possible grâce au travail acharné de KForestland (lands-of-lore-1-re).",
	          "Diese VR-Mod läuft mit ScummVM und wurde dank der harten Arbeit von KForestland (lands-of-lore-1-re) möglich.");
}

// Premier lancement : vue classique ou mode immersif.
void Ui::Impl::renderChooser() {
	Canvas cv{kHelpW, kChooserH, std::vector<uint8_t>((size_t)kHelpW * kChooserH * 4)};
	cv.window();
	cv.gcenter(latin1(tr(language, "Choose your view and language", "Choisis ta vue et ta langue", "Wähle Ansicht und Sprache")), 0, kHelpW, 20, 3, kWhite);
	static const char *const labels[3][2] = {{"Classic", "Immersive"}, {"Classique", "Immersif"}, {"Klassisch", "Immersiv"}};
	static const char *const texts[3][2] = {
		{"The game screen in a medieval library, in your room (mixed reality) or on an old 486 PC.",
		 "Walk inside the dungeon rebuilt in 3D around you. The game screen appears when needed (shops, map)."},
		{"L'écran du jeu dans une bibliothèque médiévale, dans ta pièce (réalité mixte) ou sur un vieux PC 486.",
		 "Marche dans le donjon reconstruit en 3D autour de toi. L'écran du jeu vient au besoin (boutiques, carte)."},
		{"Der Spielbildschirm in einer mittelalterlichen Bibliothek, in deinem Zimmer (Mixed Reality) oder auf einem alten 486er-PC.",
		 "Geh durch den Dungeon, in 3D um dich herum nachgebaut. Der Spielbildschirm erscheint bei Bedarf (Läden, Karte)."}};
	const int lang = language == kLangFrench ? 1 : language == kLangGerman ? 2 : 0;
	for (int b = 0; b < 2; b++) {
		int x0 = kChooseX[b][0], x1 = kChooseX[b][1];
		cv.bevel(x0, kChooseY0, x1, kChooseY1, 3, true);
		cv.gcenter(latin1(labels[lang][b]), x0, x1, kChooseY0 + 22, 3, kGold);
		int y = kChooseY0 + 78;
		for (const std::string &l : Canvas::wrap(latin1(texts[lang][b]), x1 - x0 - 28, 2)) {
			cv.gcenter(l, x0, x1, y, 2, kPale);
			y += 22;
		}
	}
	// La langue (celle de l'application et du jeu) : le nom dans sa langue, le drapeau dessous.
	static const char *const names[3] = {"English", "Français", "Deutsch"};
	for (int b = 0; b < 3; b++) {
		const int x0 = kLangX[b][0], x1 = kLangX[b][1];
		const bool on = language == b;
		cv.bevel(x0, kLangY0, x1, kLangY1, 3, !on);
		cv.gcenter(latin1(names[b]), x0 + (on ? 1 : 0), x1 + (on ? 1 : 0), kLangY0 + 9 + (on ? 1 : 0), 2, on ? kBlue : kGold);
		drawFlag(cv, b, (x0 + x1) / 2 - 48 + (on ? 1 : 0), kLangY0 + 33 + (on ? 1 : 0), 96, 64);
	}
	int y = 426;
	for (const std::string &l : Canvas::wrap(latin1(tr(language, "You can change this later: the floppy on your left hand opens the game menu, then \"VR controls\".",
	                                                   "Tu pourras changer plus tard : la disquette sur ta main gauche ouvre le menu du jeu, puis \"Réglages VR\".",
	                                                   "Du kannst das später ändern: Die Diskette an deiner linken Hand öffnet das Spielmenü, dann \"VR-Optionen\".")),
	                                         kHelpW - 48, 2)) {
		cv.gcenter(l, 0, kHelpW, y, 2, kGold);
		y += 22;
	}
	// Crédits et prérequis, sous un filet
	y += 6;
	cv.rect(24, y, kHelpW - 24, y + 1, kDark);
	cv.rect(24, y + 1, kHelpW - 24, y + 2, kLight);
	y += 8;
	cv.gcenter(latin1(creditsTitle(language)), 0, kHelpW, y, 2, kWhite);
	y += 24;
	for (const std::string &l : Canvas::wrap(latin1(creditsText(language)), kHelpW - 48, 2)) {
		cv.gcenter(l, 0, kHelpW, y, 2, kPale);
		y += 20;
	}
	y += 4;
	const std::string legal = latin1(legalTitle(language)) + " : " + latin1(requirementsText(language));
	for (const std::string &l : Canvas::wrap(legal, kHelpW - 48, 1)) {
		cv.gcenter(l, 0, kHelpW, y, 1, kGold);
		y += 12;
	}
	upload(chooserTex, cv.px, kHelpW, kChooserH, "choix");
	chooserDirty = false;
}

// --- Profils de manettes --------------------------------------------------------------------------

const uint8_t kClassicMap[kSlotCount] = {kFnAttack, kFnMagic, kFnRightClick, kFnMap, kFnRightClick, kFnView, kFnRecenter};

static const char *slotName(int lang, int s) {
	switch (s) {
	case kSlotA: return "A";
	case kSlotB: return "B";
	case kSlotX: return "X";
	case kSlotY: return "Y";
	case kSlotGrip: return tr(lang, "Grip (aiming hand)", "Poignée (main qui vise)", "Griff (zielende Hand)");
	case kSlotL3: return tr(lang, "Left stick click", "Clic joystick gauche", "Linker Stick-Klick");
	default: return tr(lang, "Right stick click", "Clic joystick droit", "Rechter Stick-Klick");
	}
}

static const char *fnName(int lang, int f) {
	switch (f) {
	case kFnAttack: return tr(lang, "Attack (menus: Enter)", "Attaquer (menus : Entrée)", "Angriff (Menüs: Eingabe)");
	case kFnMagic: return tr(lang, "Magic (menus: Escape)", "Magie (menus : Échap)", "Magie (Menüs: Esc)");
	case kFnRightClick: return tr(lang, "Right click (drink, use)", "Clic droit (boire, utiliser)", "Rechtsklick (trinken, benutzen)");
	case kFnMap: return tr(lang, "Map", "Carte", "Karte");
	case kFnRecenter: return tr(lang, "Recenter (spell level)", "Recentrer (niveau de sort)", "Zentrieren (Zauberstufe)");
	case kFnView: return tr(lang, "View; hold: screen size", "Vue ; maintenu : taille écran", "Ansicht; halten: Bildgröße");
	case kFnEnter: return tr(lang, "Enter", "Entrée", "Eingabe");
	case kFnEscape: return tr(lang, "Escape (skip)", "Échap (passer)", "Esc (überspringen)");
	case kFnAtlas: return tr(lang, "Atlas window", "Fenêtre de l'atlas", "Atlas-Fenster");
	case kFnNextChar: return tr(lang, "Next character", "Personnage suivant", "Nächste Figur");
	case kFnKeyboard: return tr(lang, "Keyboard", "Clavier", "Tastatur");
	default: return tr(lang, "Nothing", "Rien", "Nichts");
	}
}

// Le nom d'un profil, en Latin-1 (« Classique » dans la langue de l'application).
static std::string profileLabel(int lang, const std::string &name) {
	return name.empty() ? latin1(tr(lang, "Classic", "Classique", "Klassisch")) : name;
}

// Boutons du bas de l'aide, et de la fenêtre de modification (pixels de leur image).
const int kHelpBtnY0 = 466, kHelpBtnY1 = 506;
const int kHelpBtnX[2][2] = {{110, 310}, {330, 530}};
const int kEditProfY0 = 50, kEditProfY1 = 88, kEditRowY0 = 100, kEditRowH = 40, kEditValX0 = 300, kEditValX1 = 618;
const int kEditArrowX[2][2] = {{232, 272}, {578, 618}};
const int kEditBtnY0 = 438, kEditBtnY1 = 478;
const int kEditBtnX[4][2] = {{12, 158}, {168, 314}, {324, 470}, {480, 628}};

// La zone visée dans la fenêtre de modification : 0 et 1 profil précédent ou suivant, 10 + bouton
// (20 + bouton : à gauche, la fonction précédente), 30 + n les boutons du bas ; -1 rien.
static int editorZone(int px, int py) {
	if (py >= kEditProfY0 && py < kEditProfY1)
		for (int k = 0; k < 2; k++)
			if (px >= kEditArrowX[k][0] && px < kEditArrowX[k][1])
				return k;
	for (int s = 0; s < kSlotCount; s++) {
		const int y0 = kEditRowY0 + s * kEditRowH;
		if (py >= y0 + 3 && py < y0 + kEditRowH - 3 && px >= kEditValX0 && px < kEditValX1)
			return (px < kEditValX0 + 44 ? 20 : 10) + s;
	}
	if (py >= kEditBtnY0 && py < kEditBtnY1)
		for (int k = 0; k < 4; k++)
			if (px >= kEditBtnX[k][0] && px < kEditBtnX[k][1])
				return 30 + k;
	return -1;
}

void Ui::Impl::renderHelp() {
	const int L = language;
	auto line = [](const char *a, const char *b) { return std::string(a) + " .. " + b; };
	std::vector<std::string> lines = {
		std::string("#") + tr(L, "CONTROLLER MAPPING", "LES MANETTES ET LES BOUTONS", "STEUERUNG UND TASTEN"),
		"",
		std::string("#") + tr(L, "Always", "Toujours", "Immer"),
		line(tr(L, "Aim + trigger", "Viser + gâchette", "Zielen + Abzug"), tr(L, "left click", "clic gauche", "Linksklick")),
		line(tr(L, "Left stick", "Joystick gauche", "Linker Stick"), tr(L, "walk where you look", "marcher là où l'on regarde", "dorthin gehen, wohin du schaust")),
		line(tr(L, "Right stick (sideways)", "Joystick droit (côtés)", "Rechter Stick (seitlich)"), tr(L, "turn", "tourner", "drehen")),
		line(tr(L, "Right stick (up/down)", "Joystick droit (haut/bas)", "Rechter Stick (hoch/runter)"), tr(L, "change character", "changer de personnage", "Figur wechseln")),
		line(tr(L, "Menu button (left)", "Bouton menu (gauche)", "Menütaste (links)"), tr(L, "save, load, quit", "sauver, charger, quitter", "speichern, laden, beenden")),
		line(tr(L, "Floppy on the left wrist", "Disquette (main gauche)", "Diskette (linke Hand)"), tr(L, "game menu, VR controls", "menu du jeu, réglages VR", "Spielmenü, VR-Optionen")),
		line(tr(L, "Brass bar + hold trigger", "Barrette + gâchette tenue", "Messingleiste + Abzug halten"), tr(L, "move a window", "déplacer une fenêtre", "Fenster verschieben")),
		line(tr(L, "Corner + hold trigger", "Coin + gâchette tenue", "Ecke + Abzug halten"), tr(L, "resize", "agrandir, réduire", "Größe ändern")),
		"",
		std::string("#") + tr(L, "Buttons", "Boutons", "Tasten") + " : " + "\x01",
	};
	for (int sl = 0; sl < kSlotCount; sl++)
		lines.push_back(line(slotName(L, sl), fnName(L, draft[sl])));
	Canvas cv{kHelpW, kHelpH, std::vector<uint8_t>((size_t)kHelpW * kHelpH * 4)};
	cv.window();
	// « bouton .. action » : deux colonnes, l'écriture du jeu n'ayant pas des lettres de même
	// largeur pour aligner les points.
	auto split = [](const std::string &s, std::string &l, std::string &r) {
		size_t p = s.find(" ..");
		if (p == std::string::npos)
			return false;
		l = s.substr(0, p);
		r = s.substr(p + 4);
		return true;
	};
	const int x0 = 22, xMax = kHelpW - 12;
	int col = x0, y = 20;
	for (size_t i = 0; i < lines.size(); i++) {
		const bool title = !lines[i].empty() && lines[i][0] == '#';
		std::string text = latin1((title ? lines[i].substr(1) : lines[i]).c_str()), l, r;
		const size_t mark = text.find('\x01');
		if (mark != std::string::npos)
			text = text.substr(0, mark) + profileLabel(L, profiles[activeProfile].name) + (draftChanged() ? " *" : "");
		if (text.empty()) {
			y += 6;
			continue;
		}
		if (title) {
			cv.gtext(text, 14, y, 2, kWhite);
			cv.rect(10, y + 18, kHelpW - 10, y + 19, kDark);
			cv.rect(10, y + 19, kHelpW - 10, y + 20, kLight);
			y += 23;
			col = x0;
			for (size_t j = i + 1; j < lines.size() && (lines[j].empty() || lines[j][0] != '#'); j++) {
				std::string t = latin1(lines[j].c_str()), a, b;
				if (split(t, a, b) && x0 + Canvas::textW(a, 2) + 24 > col)
					col = x0 + Canvas::textW(a, 2) + 24;
			}
		} else if (split(text, l, r)) {
			cv.gtext(l, x0, y, 2, kGold);
			const int sc = Canvas::fit(r, xMax - col, 2);
			cv.gtext(r, col, y, sc, kPale);
			y += 18;
		} else {
			cv.gtext(text, x0, y, Canvas::fit(text, xMax - x0, 2), kGold);
			y += 18;
		}
	}
	// En bas : modifier les boutons, fermer.
	const char *labels[2] = {tr(L, "Customize", "Modifier", "Anpassen"), tr(L, "Close", "Fermer", "Schließen")};
	for (int k = 0; k < 2; k++) {
		cv.bevel(kHelpBtnX[k][0], kHelpBtnY0, kHelpBtnX[k][1], kHelpBtnY1, 2, true);
		cv.gcenter(latin1(labels[k]), kHelpBtnX[k][0], kHelpBtnX[k][1], (kHelpBtnY0 + kHelpBtnY1 - Canvas::textH(2)) / 2, 2, kGold);
	}
	upload(helpTex, cv.px, kHelpW, kHelpH, "aide");
	helpDirty = false;
}

// La fenêtre de modification : le profil (flèches pour en changer), chaque bouton et ce qu'il fait
// (un clic : la fonction suivante ; sur la flèche de gauche : la précédente), puis Enregistrer,
// Nouveau profil, Supprimer, Retour. « Classique » ne s'enregistre ni ne se supprime.
void Ui::Impl::renderEditor() {
	const int L = language;
	Canvas cv{kHelpW, kHelpH, std::vector<uint8_t>((size_t)kHelpW * kHelpH * 4)};
	cv.window();
	cv.gcenter(latin1(tr(L, "Customize the buttons", "Modifier les boutons", "Tasten anpassen")), 0, kHelpW, 12, 2, kWhite);
	cv.rect(10, 36, kHelpW - 10, 37, kDark);
	cv.rect(10, 37, kHelpW - 10, 38, kLight);
	const int th = Canvas::textH(2);
	// Le profil
	cv.gtext(latin1(tr(L, "Profile", "Profil", "Profil")), 22, (kEditProfY0 + kEditProfY1 - th) / 2, 2, kGold);
	for (int k = 0; k < 2; k++) {
		cv.bevel(kEditArrowX[k][0], kEditProfY0, kEditArrowX[k][1], kEditProfY1, 2, true);
		cv.gcenter(k ? ">" : "<", kEditArrowX[k][0], kEditArrowX[k][1], (kEditProfY0 + kEditProfY1 - th) / 2, 2, kGold);
	}
	cv.bevel(kEditArrowX[0][1] + 6, kEditProfY0, kEditArrowX[1][0] - 6, kEditProfY1, 2, false);
	const std::string pname = profileLabel(L, profiles[activeProfile].name) + (draftChanged() ? " *" : "");
	cv.gcenter(pname, kEditArrowX[0][1] + 6, kEditArrowX[1][0] - 6, (kEditProfY0 + kEditProfY1 - th) / 2, Canvas::fit(pname, kEditArrowX[1][0] - kEditArrowX[0][1] - 24, 2), kBlue);
	// Les boutons
	for (int sl = 0; sl < kSlotCount; sl++) {
		const int y0 = kEditRowY0 + sl * kEditRowH, by0 = y0 + 3, by1 = y0 + kEditRowH - 3, ty = (by0 + by1 - th) / 2;
		const std::string n = latin1(slotName(L, sl)), v = latin1(fnName(L, draft[sl]));
		cv.gtext(n, 22, ty, Canvas::fit(n, kEditValX0 - 34, 2), kGold);
		cv.bevel(kEditValX0, by0, kEditValX1, by1, 2, true);
		cv.gtext("<", kEditValX0 + 14, ty, 2, kLight);
		cv.gtext(">", kEditValX1 - 26, ty, 2, kLight);
		cv.gcenter(v, kEditValX0 + 40, kEditValX1 - 30, ty, Canvas::fit(v, kEditValX1 - kEditValX0 - 76, 2), kPale);
	}
	// Le nom du nouveau profil, tapé au clavier
	const int ny = kEditRowY0 + kSlotCount * kEditRowH + 4;
	if (naming) {
		const std::string t = latin1(tr(L, "Name: ", "Nom : ", "Name: ")) + nameText + "_";
		cv.gtext(t, 22, ny, 2, kWhite);
	} else if (draftChanged()) {
		cv.gtext(latin1(tr(L, "* not saved: the buttons already work this way.", "* pas enregistré : les boutons marchent déjà ainsi.", "* nicht gespeichert: die Tasten gehen schon so.")), 22, ny, 1, kPale);
	}
	// Les boutons du bas
	const char *labels[4] = {tr(L, "Save", "Enregistrer", "Speichern"), tr(L, "Save as new", "Nouveau profil", "Neues Profil"),
	                         tr(L, "Delete", "Supprimer", "Löschen"), tr(L, "Back", "Retour", "Zurück")};
	for (int k = 0; k < 4; k++) {
		const bool off = (k == 0 && (activeProfile == 0 || !draftChanged())) || (k == 2 && activeProfile == 0);
		cv.bevel(kEditBtnX[k][0], kEditBtnY0, kEditBtnX[k][1], kEditBtnY1, 2, true);
		const std::string l = latin1(labels[k]);
		cv.gcenter(l, kEditBtnX[k][0], kEditBtnX[k][1], (kEditBtnY0 + kEditBtnY1 - th) / 2, Canvas::fit(l, kEditBtnX[k][1] - kEditBtnX[k][0] - 12, 2), off ? kDark : kGold);
	}
	const char *hint = naming ? tr(L, "Type the name on the keyboard: Enter saves, Esc cancels.", "Tape le nom au clavier : Entrée enregistre, Échap annule.", "Tippe den Namen: Eingabe speichert, Esc bricht ab.")
	                          : tr(L, "Click a button's action to change it. Classic stays as it is.", "Clique sur l'action d'un bouton pour la changer. Classique ne change pas.", "Klicke auf eine Aktion, um sie zu ändern. Klassisch bleibt, wie es ist.");
	cv.gcenter(latin1(hint), 0, kHelpW, kEditBtnY1 + 14, 1, kPale);
	upload(editTex, cv.px, kHelpW, kHelpH, "boutons");
	editDirty = false;
}

// Un engrenage de laiton sur fond sombre.
// Une disquette, comme le bouton des options du jeu : un bouton de terre cuite, la disquette bleu
// nuit à étiquette lavande, ombre à gauche et dessous.
static const char *const kFloppy[16] = {
	"................",
	".BBBBBBBBBBBBB..",
	".BLLLLLLLLLLLBB.",
	".BLWWWWWWWWWLBBB",
	".BLLLLLLLLLLLBBB",
	".BLWWWWWWWWWLBBB",
	".BLLLLLLLLLLLBBB",
	".BBBBBBBBBBBBBBB",
	".BBBBBBBBBBBBBBB",
	".BBBSSSSSSSSBBBB",
	".BBBSSSSDDSSBBBB",
	".BBBSSSSDDSSBBBB",
	".BBBSSSSDDSSBBBB",
	".BBBSSSSSSSSBBBB",
	".BBBBBBBBBBBBBBB",
	"................",
};

void Ui::Impl::renderGear() {
	const int N = kGearSize, S = 3, ox = (N - 16 * S) / 2 + 1, oy = (N - 16 * S) / 2;
	Canvas cv{N, N, std::vector<uint8_t>((size_t)N * N * 4)};
	cv.bevel(0, 0, N, N, 3, true);
	for (int pass = 0; pass < 2; pass++)
		for (int y = 0; y < 16; y++)
			for (int x = 0; x < 16; x++) {
				const char c = kFloppy[y][x];
				if (c == '.')
					continue;
				Rgb col = kDark; // ombre
				if (pass) {
					col = c == 'L' ? Rgb{184, 176, 216} : c == 'W' ? Rgb{150, 142, 190} : c == 'S' ? Rgb{168, 168, 184} : c == 'D' ? Rgb{48, 48, 64} : Rgb{56, 60, 104};
				}
				const int px = ox + x * S - (pass ? 0 : 2), py = oy + y * S + (pass ? 0 : 2);
				cv.rect(px, py, px + S, py + S, col);
			}
	upload(gearTex, cv.px, N, N, "disquette");
}

void Ui::Impl::renderMenu() {
	// Comme le menu des options du jeu : le nom du réglage à gauche, sa valeur sur un bouton à droite ;
	// dessous, deux grands boutons.
	const int kMenuH = menuH(), kMenuRows = menuRows();
	Canvas cv{kMenuW, kMenuH, std::vector<uint8_t>((size_t)kMenuW * kMenuH * 4)};
	cv.window();
	static const char *const modes[3][3] = {{"Immersive", "Classic", "Mixed reality"}, {"Immersif", "Classique", "Réalité mixte"}, {"Immersiv", "Klassisch", "Mixed Reality"}};
	static const char *const views[3][3] = {{"Big screen", "Windows", "486 PC"}, {"Grand écran", "Fenêtres", "PC 486"}, {"Großbild", "Fenster", "486er-PC"}};
	const int L = language, lg = L == kLangFrench ? 1 : L == kLangGerman ? 2 : 0, mode = dungeonMode ? 0 : vrMode, view = classicView < 0 || classicView > 2 ? 0 : classicView;
	const char *names[2] = {tr(L, "VR mode", "Mode VR", "VR-Modus"), tr(L, "View (L3)", "Vue (L3)", "Ansicht (L3)")};
	const char *values[2] = {modes[lg][mode < 0 || mode > 2 ? 1 : mode], views[lg][view]};
	const int split = 196, th = Canvas::textH(2);
	cv.gcenter(latin1(tr(L, "VR controls", "Réglages VR", "VR-Optionen")), 0, kMenuW, (kMenuRowH - th) / 2 + 2, 2, kWhite);
	for (int r = 1; r < kMenuRows; r++) {
		const int i = menuItem(r), y0 = r * kMenuRowH, by0 = y0 + 5, by1 = y0 + kMenuRowH - 5, ty = (by0 + by1 - th) / 2;
		if (i <= 2) {
			const std::string n = latin1(names[i - 1]);
			const std::string v = latin1(values[i - 1]);
			cv.gtext(n, split - 10 - Canvas::textW(n, Canvas::fit(n, split - 24, 2)), ty, Canvas::fit(n, split - 24, 2), kGold);
			cv.bevel(split, by0, kMenuW - 12, by1, 2, true);
			cv.gcenter(v, split, kMenuW - 12, ty, Canvas::fit(v, kMenuW - 24 - split, 2), kGold);
		} else {
			const std::string l = latin1(i == 3 ? tr(L, "Controller mapping", "Manettes et boutons", "Steuerung und Tasten") : i == 4 ? creditsTitle(L) : tr(L, "Close", "Fermer", "Schließen"));
			cv.bevel(12, by0, kMenuW - 12, by1, 2, true);
			cv.gcenter(l, 12, kMenuW - 12, ty, 2, kGold);
		}
	}
	// Les remerciements
	{
		cv.rect(12, kMenuH - kMenuFootH + 2, kMenuW - 12, kMenuH - kMenuFootH + 3, kDark);
		cv.rect(12, kMenuH - kMenuFootH + 3, kMenuW - 12, kMenuH - kMenuFootH + 4, kLight);
		const std::string t = latin1(tr(L, "Made possible by ScummVM & KForestland", "Ce mod est possible grâce à ScummVM et KForestland", "Möglich gemacht durch ScummVM & KForestland"));
		cv.gcenter(t, 0, kMenuW, kMenuH - kMenuFootH + 9, 1, kPale);
		const std::string u = latin1(tr(L, "Unofficial fan project, not affiliated with EA, Westwood or Virgin", "Projet de fan non officiel, non affilié à EA, Westwood ou Virgin", "Inoffizielles Fanprojekt, nicht mit EA, Westwood oder Virgin verbunden"));
		cv.gcenter(u, 0, kMenuW, kMenuH - kMenuFootH + 22, 1, kPale);
	}
	upload(menuTex, cv.px, kMenuW, kMenuH, "menu");
	menuDirty = false;
}

// Invitation du premier lancement : un engrenage et deux lignes.
// Le rappel de la vue classique : L3 passe du grand écran aux fenêtres et au PC 486.
const int kNoticeW = 480, kNoticeH = 200, kCreditsH = 350; // (crédits et prérequis : plus haut)

void Ui::Impl::renderNotice() {
	const int nh = noticeKind == 1 ? kCreditsH : kNoticeH;
	Canvas cv{kNoticeW, nh, std::vector<uint8_t>((size_t)kNoticeW * nh * 4)};
	cv.window();
	if (noticeKind == 1) {
		// Crédits et prérequis
		cv.gcenter(latin1(creditsTitle(language)), 0, kNoticeW, 14, 2, kWhite);
		cv.rect(12, 34, kNoticeW - 12, 35, kDark);
		cv.rect(12, 35, kNoticeW - 12, 36, kLight);
		int y = 50;
		for (const std::string &l : Canvas::wrap(latin1(creditsText(language)), kNoticeW - 40, 2)) {
			cv.gcenter(l, 0, kNoticeW, y, 2, kGold);
			y += 20;
		}
		y += 8;
		cv.gcenter(latin1(legalTitle(language)), 0, kNoticeW, y, 2, kWhite);
		y += 22;
		for (const std::string &l : Canvas::wrap(latin1(requirementsText(language)), kNoticeW - 40, 2)) {
			cv.gcenter(l, 0, kNoticeW, y, 2, kPale);
			y += 20;
		}
		cv.bevel(kNoticeW / 2 - 50, nh - 42, kNoticeW / 2 + 50, nh - 12, 2, true);
		cv.gcenter("OK", kNoticeW / 2 - 50, kNoticeW / 2 + 50, nh - 36, 2, kGold);
		upload(noticeTex, cv.px, kNoticeW, nh, "credits");
		noticeDirty = false;
		return;
	}
	cv.gcenter(latin1(tr(language, "Change the view", "Changer de vue", "Ansicht wechseln")), 0, kNoticeW, 14, 2, kWhite);
	cv.rect(12, 34, kNoticeW - 12, 35, kDark);
	cv.rect(12, 35, kNoticeW - 12, 36, kLight);
	int y = 48;
	for (const std::string &l : Canvas::wrap(latin1(tr(language, "Click the left stick (L3) to go from the big screen to the windows, then to the 486 PC.",
	                                                   "Clique sur le joystick gauche (L3) pour passer du grand écran aux fenêtres, puis au PC 486.",
	                                                   "Klicke auf den linken Stick (L3), um vom Großbild zu den Fenstern und dann zum 486er-PC zu wechseln.")),
	                                         kNoticeW - 40, 2)) {
		cv.gcenter(l, 0, kNoticeW, y, 2, kGold);
		y += 20;
	}
	cv.gcenter(latin1(tr(language, "Also in the game menu, VR controls.", "Aussi dans le menu du jeu, Réglages VR.", "Auch im Spielmenü, VR-Optionen.")), 0, kNoticeW, y + 6, 2, kPale);
	cv.bevel(kNoticeW / 2 - 50, nh - 42, kNoticeW / 2 + 50, nh - 12, 2, true);
	cv.gcenter("OK", kNoticeW / 2 - 50, kNoticeW / 2 + 50, nh - 36, 2, kGold);
	upload(noticeTex, cv.px, kNoticeW, nh, "rappel");
	noticeDirty = false;
}

void Ui::Impl::renderHint() {
	Canvas cv{kHintW, kHintH, std::vector<uint8_t>((size_t)kHintW * kHintH * 4)};
	cv.window();
	// La disquette du poignet, à gauche
	for (int pass = 0; pass < 2; pass++)
		for (int y = 0; y < 16; y++)
			for (int x = 0; x < 16; x++) {
				const char c = kFloppy[y][x];
				if (c == '.')
					continue;
				const Rgb col = !pass ? kDark : c == 'L' ? Rgb{184, 176, 216} : c == 'W' ? Rgb{150, 142, 190} : c == 'S' ? Rgb{168, 168, 184} : c == 'D' ? Rgb{48, 48, 64} : Rgb{56, 60, 104};
				const int px = 16 + x * 3 - (pass ? 0 : 2), py = (kHintH - 48) / 2 + y * 3 + (pass ? 0 : 2);
				cv.rect(px, py, px + 3, py + 3, col);
			}
	const std::string l1 = latin1(tr(language, "Look at your left hand:", "Regarde ta main gauche :", "Schau auf deine linke Hand:"));
	const std::string l2 = latin1(tr(language, "the floppy opens the game menu.", "la disquette ouvre le menu du jeu.", "die Diskette öffnet das Spielmenü."));
	cv.gtext(l1, 80, 18, Canvas::fit(l1, kHintW - 92, 2), kWhite);
	cv.gtext(l2, 80, 46, Canvas::fit(l2, kHintW - 92, 2), kGold);
	upload(hintTex, cv.px, kHintW, kHintH, "invitation");
	hintDirty = false;
}

Ui::Ui() : d(new Impl()) {
	d->buildKeys();
	Impl::Profile classic;
	memcpy(classic.fn, kClassicMap, kSlotCount);
	d->profiles.push_back(classic);
	d->selectProfileIndex(0);
}

const uint8_t *Ui::buttonMap() const {
	return d->draft;
}

void Ui::addProfile(const std::string &name, const uint8_t *fns) {
	if (name.empty())
		return;
	Impl::Profile p;
	p.name = name.substr(0, 18);
	for (int i = 0; i < kSlotCount; i++)
		p.fn[i] = fns[i] < kFnCount ? fns[i] : (uint8_t)kFnNone;
	for (size_t i = 1; i < d->profiles.size(); i++)
		if (d->profiles[i].name == p.name) {
			d->profiles[i] = p;
			return;
		}
	d->profiles.push_back(p);
}

void Ui::selectProfile(const std::string &name) {
	for (size_t i = 0; i < d->profiles.size(); i++)
		if (d->profiles[i].name == name) {
			d->selectProfileIndex((int)i);
			return;
		}
	d->selectProfileIndex(0);
}

std::string Ui::profilesText() const {
	std::string out;
	char buf[128];
	for (size_t i = 1; i < d->profiles.size(); i++) {
		const Impl::Profile &p = d->profiles[i];
		snprintf(buf, sizeof(buf), "profil %d %d %d %d %d %d %d %s\n", p.fn[0], p.fn[1], p.fn[2], p.fn[3], p.fn[4], p.fn[5], p.fn[6], p.name.c_str());
		out += buf;
	}
	out += "profilactif " + d->profiles[d->activeProfile].name + "\n";
	return out;
}

Ui::~Ui() {
	delete d;
}

const char *Ui::panelKey(int id) {
	static const char *names[kUiPanelCount] = {"groupe3", "inventaire", "sorts2", "atlas", "clavier", "aide"};
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
		d->editDirty = true;
		d->menuDirty = true;
		d->hintDirty = true;
		d->chooserDirty = true;
		d->noticeDirty = true;
	}
}

void Ui::setVrState(int mode, int view) {
	if (mode != d->vrMode || view != d->classicView) {
		d->vrMode = mode;
		d->classicView = view;
		d->menuDirty = true;
	}
}

void Ui::openViewNotice() {
	if (d->noticeKind != 0)
		d->noticeDirty = true;
	d->noticeKind = 0;
	d->noticeOpen = true;
	d->noticeAhead = true;
}

void Ui::setOptions(bool fireOn, bool detached) {
	if (fireOn != d->fireOn || detached != d->detached)
		d->kbDirty = true;
	if (detached != d->detached)
		d->menuDirty = true;
	d->fireOn = fireOn;
	d->detached = detached;
}

void Ui::openMenu() {
	d->menuOpen = true;
	d->menuAhead = true;
	d->menuDirty = true;
}

bool Ui::spellsCollapsed() const {
	return d->spellsCollapsed;
}

void Ui::setSpellsCollapsed(bool collapsed) {
	collapsed = false; // (le livre de sorts ne se replie plus)
	if (collapsed != d->spellsCollapsed) {
		d->spellsCollapsed = collapsed;
		d->comp[kCompSpells].key = -1;
	}
}

bool Ui::inventoryExpanded() const { return d->invExpanded; }
void Ui::setInventoryExpanded(bool expanded) {
	d->invExpanded = expanded;
	d->comp[kCompInv].key = -1;
}
bool Ui::atlasCollapsed() const {
	return d->atlasCollapsed;
}

void Ui::setAtlasCollapsed(bool collapsed) {
	if (collapsed != d->atlasCollapsed) {
		d->atlasCollapsed = collapsed;
		d->atlasKey = -1;
	}
}

void Ui::setAtlasOption(bool on) {
	if (on != d->atlasOn) {
		d->atlasOn = on;
		d->menuDirty = true;
	}
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

namespace {

// Une entrée d'une archive .PAK du jeu : une table (position sur 4 octets, nom terminé par un zéro)
// jusqu'à la première donnée.
bool readPakEntry(const std::string &path, const char *wanted, std::vector<uint8_t> &out) {
	FILE *f = fopen(path.c_str(), "rb");
	if (!f)
		return false;
	fseek(f, 0, SEEK_END);
	const long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	std::vector<uint8_t> head((size_t)(size < 65536 ? (size > 0 ? size : 0) : 65536));
	head.resize(fread(head.data(), 1, head.size(), f));
	bool found = false;
	size_t pos = 0;
	long first = -1;
	while (pos + 4 <= head.size()) {
		long off = (long)(head[pos] | head[pos + 1] << 8 | head[pos + 2] << 16 | (uint32_t)head[pos + 3] << 24);
		pos += 4;
		size_t end = pos;
		while (end < head.size() && head[end])
			end++;
		if (end >= head.size())
			break;
		std::string name((const char *)&head[pos], end - pos);
		pos = end + 1;
		if (name.empty() || off <= 0 || off > size)
			break;
		if (first < 0)
			first = off;
		if (!strcasecmp(name.c_str(), wanted)) {
			// La fin : la position de l'entrée suivante, ou la fin du fichier.
			long next = size;
			if (pos + 4 <= head.size()) {
				long n = (long)(head[pos] | head[pos + 1] << 8 | head[pos + 2] << 16 | (uint32_t)head[pos + 3] << 24);
				if (n > off && n <= size)
					next = n;
			}
			if (next - off > 0 && next - off < 65536) {
				out.resize((size_t)(next - off));
				fseek(f, off, SEEK_SET);
				found = fread(out.data(), 1, out.size(), f) == out.size();
			}
			break;
		}
		if ((long)pos >= first)
			break;
	}
	fclose(f);
	return found;
}

} // namespace

bool Ui::loadGameFont(const std::string &gameDir) {
	if (g_font.ok())
		return true;
	DIR *dir = opendir(gameDir.c_str());
	if (!dir)
		return false;
	std::vector<std::string> paks;
	while (dirent *e = readdir(dir)) {
		size_t n = strlen(e->d_name);
		if (n > 4 && !strcasecmp(e->d_name + n - 4, ".PAK"))
			paks.push_back(gameDir + "/" + e->d_name);
	}
	closedir(dir);
	// FONT9P.FNT (le menu du jeu), sinon sa variante FONT9PN.FNT.
	for (const char *name : {"FONT9P.FNT", "FONT9PN.FNT"})
		for (const std::string &p : paks) {
			std::vector<uint8_t> data;
			if (readPakEntry(p, name, data) && g_font.parse(std::move(data))) {
				d->kbDirty = d->helpDirty = d->menuDirty = d->hintDirty = d->chooserDirty = d->editDirty = true;
				d->comp[kCompSpells].key = -1;
				d->atlasKey = -1;
				return true;
			}
		}
	return false;
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
	auto srcW = [&](int id) { int c = d->compOf(id); return c >= 0 && d->comp[c].w ? (float)d->comp[c].w : kSources[id].w; };
	auto srcH = [&](int id) { int c = d->compOf(id); return c >= 0 && d->comp[c].h ? (float)d->comp[c].h : kSources[id].h; };
	auto halfW = [&](int id, float scale) { return srcW(id) * scale * 0.5f; };
	auto halfH = [&](int id, float scale) { return srcH(id) * pixelAspect(id) * scale * 0.5f; };

	// À gauche : le parchemin des sorts et les messages (pixels doublés dans la fenêtre recomposée).
	// Le bas de la fenêtre (les messages) est à la hauteur où il serait, livre de sorts ouvert, sous
	// le haut de la vue : ouvert ou replié, le livre garde ses messages au même endroit.
	const float sSpells = 0.0018f, openH = (float)(kSpellTabH + 244 + 22) * 1.2f * sSpells;
	set(kPanelSpells, -(viewW * 0.5f + frame + 0.035f + halfW(kPanelSpells, sSpells)), top - openH + halfH(kPanelSpells, sSpells), sSpells);
	// À droite : les personnages, l'atlas, les options et le repos.
	const float sParty = 0.0034f;
	set(kPanelParty, viewW * 0.5f + frame + 0.035f + halfW(kPanelParty, sParty), top - halfH(kPanelParty, sParty), sParty);
	// Dessous : l'inventaire.
	const float sRow = 0.0026f;
	set(kPanelInventory, 0.0f, bottom - frame - 0.045f - halfH(kPanelInventory, sRow), sRow);
	// À droite des personnages : l'atlas.
	d->placeAtlas(placement);

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

// Gâchette maintenue sur une fenêtre (ou l'écran) : au bout d'un instant, la fenêtre suit le doigt.
// Relâchée avant, c'est un clic ordinaire, joué au relâcher.
void Ui::update(const UiFrameInput &in0, UiFrameOutput &out, SceneState &scene) {
	if (!in0.holdToMove) {
		d->pendPress = false;
		updateFrame(in0, out, scene);
		return;
	}
	static const double kHoldToMove = 0.4;
	UiFrameInput in = in0;
	bool synth = false, startDrag = false;
	if (d->pendPress) {
		if (!in0.triggerDown || !in0.aimValid) {
			d->pendPress = false;
			synth = in0.aimValid;
			in.triggerPressed = synth;
			in.triggerDown = synth;
		} else if (in0.time - d->pendSince >= kHoldToMove) {
			d->pendPress = false;
			startDrag = d->lastHoldTarget != -1;
			in.triggerPressed = false;
		} else {
			in.triggerPressed = false;
			in.triggerDown = false;
		}
	} else if (in0.triggerPressed && in0.aimValid && d->drag == -1 && d->resize == -1 && d->lastHoldable) {
		d->pendPress = true;
		d->pendSince = in0.time;
		in.triggerPressed = false;
		in.triggerDown = false;
	}
	if (startDrag) {
		d->drag = d->lastHoldTarget;
		d->grabDist = d->lastHoldT;
		d->grabOffset = d->lastHoldOffset;
	}
	updateFrame(in, out, scene);
	if (startDrag)
		out.grabbed = true;
	if (synth)
		out.gameClick = out.pointerValid && out.buttonsToGame;
	out.pressPending = d->pendPress;
}

void Ui::updateFrame(const UiFrameInput &in, UiFrameOutput &out, SceneState &scene) {
	out = UiFrameOutput();
	for (int i = 0; i < SceneState::kMaxPanels; i++)
		scene.panels[i].visible = false;
	for (int i = 0; i < SceneState::kMaxHandles; i++)
		scene.handles[i].visible = false;
	if (d->kbDirty)
		d->renderKeyboard();
	// L'aide fermée : retour à l'aide, nom abandonné, changements non enregistrés oubliés.
	if (d->helpWasOpen && !in.helpOpen) {
		d->helpMode = 0;
		if (d->naming) {
			d->naming = false;
			out.action = kUiActionCloseKeyboard;
		}
		if (d->draftChanged())
			d->selectProfileIndex(d->activeProfile);
	}
	d->helpWasOpen = in.helpOpen;
	// Le nom en cours : le clavier refermé l'abandonne.
	if (d->naming) {
		if (in.keyboardOpen)
			d->namingKbSeen = true;
		else if (d->namingKbSeen) {
			d->naming = false;
			d->editDirty = true;
		}
	}
	if (d->helpDirty || !d->helpTex)
		d->renderHelp();
	if (d->helpMode == 1 && (d->editDirty || !d->editTex))
		d->renderEditor();
	if (!d->gearTex)
		d->renderGear();
	if (in.keyboardOpen != d->menuKeyboard) {
		d->menuKeyboard = in.keyboardOpen;
		d->menuDirty = true;
	}
	if (d->menuDirty || !d->menuTex)
		d->renderMenu();
	if (d->noticeOpen && (d->noticeDirty || !d->noticeTex))
		d->renderNotice();
	if (in.gearHint && (d->hintDirty || !d->hintTex))
		d->renderHint();
	if (in.chooserOpen && (d->chooserDirty || !d->chooserTex))
		d->renderChooser();
	d->pressFlash -= in.dt;

	// Tant que la disposition n'est pas établie (écran pas encore placé), aucun panneau.
	bool visible[kUiPanelCount];
	for (int i = 0; i <= kPanelSpells; i++)
		visible[i] = placed && in.split;
	if (visible[kPanelParty])
		d->compose(kCompParty, in);
	if (visible[kPanelInventory])
		d->compose(kCompInv, in);
	if (visible[kPanelSpells])
		d->compose(kCompSpells, in);
	// L'atlas : quand le groupe a la carte (et le réglage le montre).
	visible[kPanelAtlas] = placed && in.split && in.atlasOn && in.map && in.map->available && in.map->rgb;
	if (visible[kPanelAtlas]) {
		if (placement[kPanelAtlas].c < 0.19f) // (une disposition d'avant l'atlas)
			d->placeAtlas(placement);
		d->composeAtlas(in);
	}
	visible[kPanelKeyboard] = placed && in.keyboardOpen;
	visible[kPanelHelp] = placed && (in.helpOpen || in.chooserOpen);
	d->chooserHot = -1;
	d->helpHot = d->editHot = -1;
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
		const int ci = d->compOf(i);
		float sw = ci >= 0 && d->comp[ci].w ? (float)d->comp[ci].w : kSources[i].w, sh = ci >= 0 && d->comp[ci].h ? (float)d->comp[ci].h : kSources[i].h;
		if (i == kPanelHelp && in.chooserOpen)
			sh = (float)kChooserH;
		q.hw = sw * scale * 0.5f;
		q.hh = sh * pixelAspect(i) * scale * 0.5f;
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
		if (i < kPanelKeyboard) {
			// Fenêtres du jeu (inventaire, livre de sorts, personnages) : barrette dessous, toujours.
			hq.hh = thick;
			hq.hw = fminf(q.hw, longSide);
			hq.c = q.c - q.u * (q.hh + border + hq.hh + 0.002f);
		} else {
			// Clavier, aide : barrette au-dessus.
			hq.hh = thick;
			hq.hw = fminf(q.hw, longSide);
			hq.c = q.c + q.u * (q.hh + border + hq.hh + 0.002f);
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

	auto handleShown = [&](int i) { return !in.holdToMove && (i == kHandleScreen ? !in.screenHidden : visible[i]); };
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
	// Ouvert depuis le menu du jeu : devant le joueur, un peu sous le regard, tourné vers lui.
	if (d->menuOpen && d->menuAhead) {
		Vec3 f{in.headDir.x, 0.0f, in.headDir.z};
		float fl = length(f);
		f = fl > 1e-3f ? f * (1.0f / fl) : Vec3{0, 0, -1};
		d->menuPos = in.headPos + f * 0.55f + Vec3{0, -0.10f, 0};
		d->menuRight = cross({0, 1, 0}, f * -1.0f);
		d->menuUp = {0, 1, 0};
		d->menuAhead = false;
	}
	const float kNoticeScale = 0.0009f;
	if (d->noticeOpen && d->noticeAhead) {
		Vec3 f{in.headDir.x, 0.0f, in.headDir.z};
		float fl = length(f);
		f = fl > 1e-3f ? f * (1.0f / fl) : Vec3{0, 0, -1};
		d->noticePos = in.headPos + f * 0.6f + Vec3{0, -0.05f, 0};
		d->noticeRight = cross({0, 1, 0}, f * -1.0f);
		d->noticeUp = {0, 1, 0};
		d->noticeAhead = false;
	}
	Quad noticeQ;
	noticeQ.c = d->noticePos;
	noticeQ.r = d->noticeRight;
	noticeQ.u = d->noticeUp;
	noticeQ.hw = kNoticeW * kNoticeScale * 0.5f;
	noticeQ.hh = (d->noticeKind == 1 ? kCreditsH : kNoticeH) * kNoticeScale * 0.5f;
	if (d->menuOpen) {
		menuQ.c = d->menuPos;
		menuQ.r = d->menuRight;
		menuQ.u = d->menuUp;
		menuQ.hw = kMenuW * kMenuScale * 0.5f;
		menuQ.hh = d->menuH() * kMenuScale * 0.5f;
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

	bool tabHot = false; // le doigt est sur le bandeau du livre de sorts
	bool atlasHot = false; // … ou sur celui de l'atlas
	int invHot = -1;       // … ou sur une case de la fenêtre d'inventaire
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
		for (int w = 0; w < 3; w++) {
			if (w == 0 ? !gearVisible : w == 1 ? !d->menuOpen : !d->noticeOpen)
				continue;
			float t, x, y;
			if (rayQuad(in.tip, in.dir, w == 0 ? gearQ : w == 1 ? menuQ : noticeQ, t, x, y) && fabsf(x) <= 1.0f && fabsf(y) <= 1.0f && t < bestT) {
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
	// Une touche du clavier enfoncée (au doigt pointé ou du bout du doigt)
	auto pressKey = [&](int idx) {
		if (idx < 0 || idx >= (int)d->keys.size())
			return;
		const Key &key = d->keys[idx];
		out.clicked = true;
		d->pressKey = idx;
		d->pressFlash = 0.15f;
		if (key.action == -1) {
			d->shift = !d->shift;
			d->kbDirty = true;
		} else if (key.action != kUiActionNone) {
			out.action = key.action;
		} else if (d->naming && key.key) {
			// Le nom d'un nouveau profil de manettes
			const unsigned c = key.key;
			if (c == 13 || c == 27) {
				if (c == 13) {
					std::string nm = d->nameText;
					while (!nm.empty() && nm.back() == ' ')
						nm.pop_back();
					if (nm.empty())
						nm = "Profil " + std::to_string(d->profiles.size());
					uint8_t fns[kSlotCount];
					memcpy(fns, d->draft, kSlotCount);
					addProfile(nm, fns);
					selectProfile(nm.substr(0, 18));
					out.layoutChanged = true;
				}
				d->naming = false;
				out.action = kUiActionCloseKeyboard;
			} else if (c == 8) {
				if (!d->nameText.empty())
					d->nameText.pop_back();
			} else if (c >= 32 && c < 127 && d->nameText.size() < 18) {
				const bool letter = c >= 'a' && c <= 'z';
				d->nameText += (char)(letter && d->shift ? c - 32 : c);
				if (d->shift) {
					d->shift = false;
					d->kbDirty = true;
				}
			}
			d->editDirty = true;
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
	};
	// Du bout du doigt : l'index qui traverse la surface du clavier enfonce la touche dessous ; il faut
	// le ressortir (2 cm) pour taper la suivante.
	for (int h = 0; h < 2; h++) {
		bool near = false;
		if (in.pokeValid[h] && visible[kPanelKeyboard] && d->drag == -1) {
			const Quad &q = content[kPanelKeyboard];
			const Vec3 n = cross(q.r, q.u);
			const float side = dot(in.anchorPos - q.c, n) >= 0.0f ? 1.0f : -1.0f;
			const Vec3 rel = in.poke[h] - q.c;
			const float dist = side * dot(rel, n), x = dot(rel, q.r) / q.hw, y = dot(rel, q.u) / q.hh;
			if (fabsf(x) <= 1.0f && fabsf(y) <= 1.0f && dist > -0.06f && dist < 0.10f) {
				near = true;
				if (dist > 0.02f) {
					d->pokeArmed[h] = true;
				} else if (dist < 0.006f && d->pokeArmed[h]) {
					d->pokeArmed[h] = false;
					const float u = (x + 1.0f) * 0.5f, v = (1.0f - y) * 0.5f;
					const int col = (int)(u * (float)kCols), row = (int)(v * (float)kKbH / (float)kCellH);
					for (size_t k = 0; k < d->keys.size(); k++) {
						const Key &key = d->keys[k];
						if (key.row == row && col >= key.col && col < key.col + key.span) {
							pressKey((int)k);
							out.pokeHand = h;
							break;
						}
					}
				}
			}
		}
		if (!near)
			d->pokeArmed[h] = true;
	}
	// Ce qu'une gâchette maintenue déplacerait (voir update).
	d->lastHoldable = false;
	if (in.aimValid && d->drag == -1 && d->resize == -1 && ((hitKind == kHitPanel && hitIndex >= 0 && hitIndex < kUiPanelCount) || (hitKind == kHitScreen && !in.screenHidden))) {
		const Vec3 p = in.tip + in.dir * bestT;
		d->lastHoldable = true;
		d->lastHoldTarget = hitKind == kHitScreen ? Impl::kScreenTarget : hitIndex;
		d->lastHoldT = bestT;
		d->lastHoldOffset = (hitKind == kHitScreen ? in.screenPos : content[hitIndex].c) - p;
	}

	if (hitKind == kHitScreen && screenHitOk) {
		out.pointerValid = true;
		out.buttonsToGame = true;
		out.hitValid = true;
		out.hitRadius = 0.0f;
		float rowY = screenV01 * (in.cropH + in.bandH);
		if (in.bandH > 0.0f && rowY > in.cropH) {
			// La bande collée sous la vue : la ligne de messages, ou la boîte de dialogue agrandie.
			out.gu = (in.bandRect[0] + in.bandRect[2] * screenU01) / 320.0f;
			out.gv = (in.bandRect[1] + (rowY - in.cropH) * in.bandRect[3] / in.bandH) / 200.0f;
		} else {
			out.gu = (in.cropX + in.cropW * screenU01) / 320.0f;
			out.gv = (in.cropY + rowY) / 200.0f;
		}
		Vec3 local{(screenU01 - 0.5f) * in.dispW, (0.5f - screenV01) * in.dispH, screenZ + 0.004f};
		out.hit = rotate(in.screenRot, local) + in.screenPos;
		out.hitRight = rotate(in.screenRot, Vec3{1, 0, 0});
		out.hitUp = rotate(in.screenRot, Vec3{0, 1, 0});
		out.hitPixel = in.cropW > 0.0f ? in.dispW / in.cropW : 0.0f;
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
		const Quad &q = hitIndex == 0 ? gearQ : hitIndex == 1 ? menuQ : noticeQ;
		out.hitValid = true;
		out.hit = q.c + q.r * (hx * q.hw) + q.u * (hy * q.hh) + cross(q.r, q.u) * 0.001f;
		out.hitRight = q.r;
		out.hitUp = q.u;
		out.hitRadius = 0.010f * bestT;
		if (hitIndex == 2) {
			// Le rappel : un clic n'importe où le ferme
			if (in.triggerPressed) {
				d->noticeOpen = false;
				out.clicked = true;
			}
		} else if (hitIndex == 0) {
			if (in.triggerPressed) {
				// La disquette : le menu du jeu (sauvegarder, charger, et les réglages VR).
				out.action = kUiActionGameMenu;
				out.gearMenuOpened = true;
				out.clicked = true;
			}
		} else {
			const int rows = d->menuRows();
			int hotRow = (int)((1.0f - hy) * 0.5f * (float)d->menuH()) / kMenuRowH;
			hotRow = hotRow < 0 ? 0 : hotRow >= rows ? -1 : hotRow; // (les remerciements : rien)
			d->menuHot = hotRow;
			const int row = hotRow < 0 ? -1 : d->menuItem(hotRow);
			if (in.triggerPressed && row >= 1) {
				out.clicked = true;
				if (row == 1) {
					out.action = kUiActionToggleDungeon; // le mode VR suivant
				} else if (row == 2) {
					out.action = kUiActionCycleView;
				} else if (row == 3) {
					out.action = kUiActionToggleHelp;
					d->menuOpen = false;
				} else if (row == 4) {
					// Crédits et prérequis : un petit mot devant le joueur, un clic le ferme.
					d->noticeKind = 1;
					d->noticeDirty = true;
					d->noticeOpen = true;
					d->noticeAhead = true;
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
		if (hitIndex == kPanelHelp && in.chooserOpen) {
			// Choix de la vue : un des deux grands boutons.
			int px = (int)(u * (float)kHelpW), py = (int)(v * (float)kChooserH);
			for (int b = 0; b < 2; b++)
				if (px >= kChooseX[b][0] && px < kChooseX[b][1] && py >= kChooseY0 && py < kChooseY1)
					d->chooserHot = b;
			// La langue : 2, 3, 4 (anglais, français, allemand) ; la fenêtre reste ouverte.
			for (int b = 0; b < 3; b++)
				if (px >= kLangX[b][0] && px < kLangX[b][1] && py >= kLangY0 && py < kLangY1)
					d->chooserHot = 2 + b;
			if (in.triggerPressed && d->chooserHot >= 2) {
				out.action = kUiActionChooseLanguage;
				out.language = d->chooserHot - 2;
				out.clicked = true;
			} else if (in.triggerPressed && d->chooserHot >= 0) {
				out.action = d->chooserHot == 0 ? kUiActionChooseClassic : kUiActionChooseImmersive;
				out.clicked = true;
			}
		} else if (hitIndex == kPanelHelp && d->helpMode == 0) {
			// L'aide : Modifier (la fenêtre des boutons), Fermer.
			const int px = (int)(u * (float)kHelpW), py = (int)(v * (float)kHelpH);
			for (int k = 0; k < 2; k++)
				if (px >= kHelpBtnX[k][0] && px < kHelpBtnX[k][1] && py >= kHelpBtnY0 && py < kHelpBtnY1)
					d->helpHot = k;
			if (in.triggerPressed && d->helpHot >= 0) {
				out.clicked = true;
				if (d->helpHot == 0) {
					d->helpMode = 1;
					d->editDirty = true;
				} else {
					out.action = kUiActionCloseHelp;
				}
			}
		} else if (hitIndex == kPanelHelp) {
			const int zone = editorZone((int)(u * (float)kHelpW), (int)(v * (float)kHelpH));
			d->editHot = zone;
			if (in.triggerPressed && zone >= 0) {
				out.clicked = true;
				const int n = (int)d->profiles.size();
				auto endNaming = [&]() {
					if (d->naming) {
						d->naming = false;
						out.action = kUiActionCloseKeyboard;
					}
				};
				if (zone <= 1) {
					endNaming();
					d->selectProfileIndex((d->activeProfile + (zone ? 1 : n - 1)) % n);
					out.layoutChanged = true; // (le profil choisi est retenu)
				} else if (zone >= 10 && zone < 30) {
					const int sl = zone % 10;
					d->draft[sl] = (uint8_t)((d->draft[sl] + (zone >= 20 ? kFnCount - 1 : 1)) % kFnCount);
					d->helpDirty = d->editDirty = true;
				} else if (zone == 30) {
					if (d->activeProfile > 0 && d->draftChanged()) {
						memcpy(d->profiles[d->activeProfile].fn, d->draft, kSlotCount);
						d->helpDirty = d->editDirty = true;
						out.layoutChanged = true;
					}
				} else if (zone == 31) {
					d->naming = true;
					d->namingKbSeen = false;
					d->nameText.clear();
					d->editDirty = true;
					out.action = kUiActionOpenKeyboard;
				} else if (zone == 32) {
					if (d->activeProfile > 0) {
						endNaming();
						d->profiles.erase(d->profiles.begin() + d->activeProfile);
						d->selectProfileIndex(0);
						out.layoutChanged = true;
					}
				} else {
					endNaming();
					d->helpMode = 0;
					d->helpDirty = true;
				}
			}
		} else if (hitIndex == kPanelKeyboard) {
			int col = (int)(u * (float)kCols), row = (int)(v * (float)kKbH / (float)kCellH);
			for (size_t k = 0; k < d->keys.size(); k++) {
				const Key &key = d->keys[k];
				if (key.row == row && col >= key.col && col < key.col + key.span)
					d->hotKey = (int)k;
			}
			if (d->hotKey >= 0 && in.triggerPressed)
				pressKey(d->hotKey);
		} else {
			if (false) {
				// Le bandeau : ouvrir ou replier le livre de sorts. Le bas de la fenêtre (les messages)
				// ne bouge pas.
				tabHot = true;
				if (in.triggerPressed) {
					Composite &c = d->comp[kCompSpells];
					const int oldH = c.h;
					d->spellsCollapsed = !d->spellsCollapsed;
					c.key = -1;
					d->compose(kCompSpells, in);
					UiPlacement &pl = placement[kPanelSpells];
					float dh = (float)(c.h - oldH) * pixelAspect(kPanelSpells) * pl.scale * 0.5f;
					pl.b += atan2f(dh, pl.c);
					out.clicked = true;
					out.layoutChanged = true;
				}
			} else if (hitIndex == kPanelAtlas) {
				// La case « Fermer » de la page de l'atlas.
				const float ax = u * (float)kAtlasW, ay = v * (float)kAtlasH;
				if (ax >= kAtlasCloseX0 && ax < kAtlasCloseX1 && ay >= kAtlasCloseY0 && ay < kAtlasCloseY1) {
					atlasHot = true;
					if (in.triggerPressed) {
						out.action = kUiActionToggleAtlas;
						out.clicked = true;
					}
				}
			} else if (hitIndex == kPanelInventory && in.invRgb) {
				// La fenêtre d'inventaire de l'application : une case, le bouton qui déplie, le tri.
				const Composite &c = d->comp[kCompInv];
				const float cx = u * (float)c.w, cy = v * (float)c.h;
				// Le curseur du jeu (l'objet tenu) se dessine ici aussi ; le pointeur du jeu attend sur sa
				// propre bande d'objets, sans clic.
				out.pointerValid = true;
				out.buttonsToGame = false;
				out.gu = 160.0f / 320.0f;
				out.gv = 189.0f / 200.0f;
				out.hitPixel = c.w > 0 ? 2.0f * q.hw / (float)c.w : 0.0f;
				if (cx >= 178.0f && cy < 22.0f)
					invHot = 100;
				else if (cx >= 178.0f && cy >= 22.0f && cy < 44.0f && d->invExpanded)
					invHot = 101;
				else if (cx < 176.0f && cy >= 0.0f && cy < (float)c.h)
					invHot = ((int)cy / 22) * 8 + (int)cx / 22;
				if (invHot >= 0 && in.triggerPressed) {
					if (invHot == 100) {
						Composite &cc = d->comp[kCompInv];
						const int oldH = cc.h;
						d->invExpanded = !d->invExpanded;
						cc.key = -1;
						d->composeInv(in);
						UiPlacement &pl = placement[kPanelInventory];
						const float dh = (float)(cc.h - oldH) * pixelAspect(kPanelInventory) * pl.scale * 0.5f;
						pl.b -= atan2f(dh, pl.c);
						out.layoutChanged = true;
					} else if (invHot == 101) {
						out.invSort = true;
					} else {
						out.invSlot = invHot;
					}
					out.clicked = true;
				}
			} else if (d->compOf(hitIndex) >= 0) {
				// Fenêtre recomposée : retrouver le morceau visé, et l'endroit du jeu qu'il montre.
				const Composite &c = d->comp[d->compOf(hitIndex)];
				// (au bord exact d'une fenêtre : dans son dernier pixel)
				float cx = clampf(u * (float)c.w, 0.0f, (float)c.w - 0.001f), cy = clampf(v * (float)c.h, 0.0f, (float)c.h - 0.001f);
				for (const Piece &pc : c.pieces) {
					if (!pc.contains(cx, cy))
						continue;
					if (hitIndex == kPanelParty && pc.sx == 288 && in.compassBlank) // fenêtre éteinte : aucun clic
						break;
					out.pointerValid = true;
					out.buttonsToGame = true;
					out.gu = ((float)pc.sx + (cx - (float)pc.dx) * (float)pc.sw / (float)pc.dw) / 320.0f;
					out.gv = ((float)pc.sy + (cy - (float)pc.dy) * (float)pc.sh / (float)pc.dh) / 200.0f;
					out.hitPixel = 2.0f * q.hw / (float)c.w * (float)pc.dw / (float)(pc.sw > 0 ? pc.sw : 1);
					// L'icône de l'atlas (le livre ouvert, sous la boussole) ouvre la fenêtre de l'atlas,
					// à la place de la page plein écran du jeu.
					if (hitIndex == kPanelParty && pc.sx == 288 && in.map && in.map->available) {
						const float gx = out.gu * 320.0f, gy = out.gv * 200.0f;
						if (gx >= 289.0f && gx < 319.0f && gy >= 31.0f && gy < 52.0f) {
							out.buttonsToGame = false;
							if (in.triggerPressed) {
								out.action = kUiActionToggleAtlas;
								out.clicked = true;
							}
						}
					}
					break;
				}
			} else {
				const Source &src = kSources[hitIndex];
				out.pointerValid = true;
				out.buttonsToGame = true;
				out.gu = (src.x + u * src.w) / 320.0f;
				out.gv = (src.y + v * src.h) / 200.0f;
				out.hitPixel = src.w > 0 ? 2.0f * q.hw / (float)src.w : 0.0f;
			}
		}
	}

	if (invHot != d->invHot) {
		d->invHot = invHot;
		d->comp[kCompInv].key = -1;
	}
	if (atlasHot != d->atlasTabHot) {
		d->atlasTabHot = atlasHot;
		d->atlasKey = -1;
	}
	if (tabHot != d->spellTabHot) {
		d->spellTabHot = tabHot;
		d->comp[kCompSpells].key = -1;
	}

	// --- 4. Ce que la scène doit dessiner ---
	int np = 0, nh = 0;
	for (int i = 0; i < kUiPanelCount; i++) {
		if (!visible[i])
			continue;
		const Quad &q = content[i];
		const Source &src = kSources[i];
		if (np < SceneState::kMaxPanels - 2) {
			SceneState::Panel &p = scene.panels[np++];
			p = SceneState::Panel();
			p.visible = true;
			p.center = q.c;
			p.right = q.r;
			p.up = q.u;
			p.halfW = q.hw;
			p.halfH = q.hh;
			p.border = clampf(0.012f * panelDist[i], 0.004f, 0.012f);
			if (i == kPanelHelp && in.chooserOpen) {
				p.texture = d->chooserTex;
				p.texW = kHelpW;
				p.texH = kChooserH;
				if (d->chooserHot >= 0) {
					p.hotOutline = true;
					const bool lg = d->chooserHot >= 2;
					const int *xs = lg ? kLangX[d->chooserHot - 2] : kChooseX[d->chooserHot];
					p.hot[0] = (float)xs[0] / (float)kHelpW;
					p.hot[1] = (float)(lg ? kLangY0 : kChooseY0) / (float)kChooserH;
					p.hot[2] = (float)xs[1] / (float)kHelpW;
					p.hot[3] = (float)(lg ? kLangY1 : kChooseY1) / (float)kChooserH;
				}
			} else if (i == kPanelHelp) {
				p.texture = d->helpMode == 1 && d->editTex ? d->editTex : d->helpTex;
				p.texW = kHelpW;
				p.texH = kHelpH;
				// Le bouton visé, encadré
				int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
				if (d->helpMode == 0 && d->helpHot >= 0) {
					x0 = kHelpBtnX[d->helpHot][0]; x1 = kHelpBtnX[d->helpHot][1]; y0 = kHelpBtnY0; y1 = kHelpBtnY1;
				} else if (d->helpMode == 1 && d->editHot >= 0) {
					const int z = d->editHot;
					if (z <= 1) {
						x0 = kEditArrowX[z][0]; x1 = kEditArrowX[z][1]; y0 = kEditProfY0; y1 = kEditProfY1;
					} else if (z < 30) {
						x0 = z >= 20 ? kEditValX0 : kEditValX0 + 44; x1 = z >= 20 ? kEditValX0 + 44 : kEditValX1;
						y0 = kEditRowY0 + (z % 10) * kEditRowH + 3; y1 = y0 + kEditRowH - 6;
					} else {
						x0 = kEditBtnX[z - 30][0]; x1 = kEditBtnX[z - 30][1]; y0 = kEditBtnY0; y1 = kEditBtnY1;
					}
				}
				if (x1 > x0) {
					p.hotOutline = true;
					p.hot[0] = (float)x0 / kHelpW;
					p.hot[1] = (float)y0 / kHelpH;
					p.hot[2] = (float)x1 / kHelpW;
					p.hot[3] = (float)y1 / kHelpH;
				}
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
			} else if (d->compOf(i) >= 0) {
				const Composite &c = d->comp[d->compOf(i)];
				p.texture = c.tex;
				p.texW = c.texW;
				p.texH = c.texH;
				p.u0 = 0.0f;
				p.v0 = 0.0f;
				p.u1 = (float)c.w / (float)c.texW;
				p.v1 = (float)c.h / (float)c.texH;
				// Le personnage choisi pour les boutons d'attaque et de magie : son portrait encadré.
				if (i == kPanelParty && in.partyMark[2] > 0.0f) {
					for (const Piece &pc : c.pieces) {
						if (pc.sy != 143 || pc.sx != (int)in.partyMark[0])
							continue;
						p.hotOutline = true;
						p.hot[0] = (float)pc.dx / (float)c.texW;
						p.hot[1] = (float)pc.dy / (float)c.texH;
						p.hot[2] = (float)(pc.dx + pc.dw) / (float)c.texW;
						p.hot[3] = (float)(pc.dy + pc.dh) / (float)c.texH;
					}
				}
				// Le menu de magie ouvert : le niveau qui partira au prochain B, encadré.
				if (i == kPanelParty && in.magicMark[2] > 0.0f) {
					for (const Piece &pc : c.pieces) {
						const float mx = in.magicMark[0], my = in.magicMark[1];
						if (pc.sy != 143 || mx < pc.sx || mx >= pc.sx + pc.sw || my < pc.sy || my >= pc.sy + pc.sh)
							continue;
						const float kx = (float)pc.dw / (float)pc.sw, ky = (float)pc.dh / (float)pc.sh;
						p.hotOutline = true;
						p.hot[0] = (pc.dx + (mx - pc.sx) * kx) / (float)c.texW;
						p.hot[1] = (pc.dy + (my - pc.sy) * ky) / (float)c.texH;
						p.hot[2] = (pc.dx + (mx + in.magicMark[2] - pc.sx) * kx) / (float)c.texW;
						p.hot[3] = (pc.dy + (my + in.magicMark[3] - pc.sy) * ky) / (float)c.texH;
					}
				}
			} else {
				p.u0 = src.x / 320.0f;
				p.u1 = (src.x + src.w) / 320.0f;
				p.v0 = src.y / 200.0f;
				p.v1 = (src.y + src.h) / 200.0f;
			}
		}
	}
	// Invitation du premier lancement : un peu sous le regard, à portée de lecture, elle suit la tête
	// sans à-coups. Elle s'efface quand on regarde sa main (l'engrenage prend le relais).
	if (in.gearHint && placed && !d->menuOpen && !gearVisible && d->hintTex && np < SceneState::kMaxPanels - 2) {
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
	for (int w = 0; w < 3; w++) {
		if ((w == 0 ? !gearVisible : w == 1 ? !d->menuOpen : !d->noticeOpen) || np >= SceneState::kMaxPanels - 2)
			continue;
		const Quad &q = w == 0 ? gearQ : w == 1 ? menuQ : noticeQ;
		SceneState::Panel &p = scene.panels[np++];
		p = SceneState::Panel();
		p.visible = true;
		p.center = q.c;
		p.right = q.r;
		p.up = q.u;
		p.halfW = q.hw;
		p.halfH = q.hh;
		p.border = 0.004f;
		p.texture = w == 0 ? d->gearTex : w == 1 ? d->menuTex : d->noticeTex;
		p.texW = w == 0 ? kGearSize : w == 1 ? kMenuW : kNoticeW;
		p.texH = w == 0 ? kGearSize : w == 1 ? d->menuH() : (d->noticeKind == 1 ? kCreditsH : kNoticeH);
		if (w == 1 && d->menuHot >= 1) {
			p.hot[0] = 0.0f;
			p.hot[1] = (float)(d->menuHot * kMenuRowH) / (float)d->menuH();
			p.hot[2] = 1.0f;
			p.hot[3] = (float)((d->menuHot + 1) * kMenuRowH) / (float)d->menuH();
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
