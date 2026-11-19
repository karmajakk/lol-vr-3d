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
const int kGearSize = 64, kHintW = 440, kHintH = 84, kMenuW = 360, kMenuRowH = 44, kMenuRows = 7, kMenuH = kMenuRowH * kMenuRows;
// Panneau d'aide
const int kHelpW = 640, kHelpH = 470;
const int kInfoH = 44; // deux lignes d'information sous les touches
const int kKbW = kCellW * kCols, kKbH = kCellH * kRows + kInfoH;

const Source kSources[kUiPanelCount] = {
	{0, 0, 100, 150},    // à droite : recomposé (voir compose), taille variable
	{83, 178, 237, 22},  // inventaire
	{0, 0, 224, 41},     // à gauche : recomposé, bandeau, (parchemin des sorts,) messages
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
const int kCompParty = 0, kCompSpells = 1;
const int kCompTexW[2] = {104, 224}, kCompTexH[2] = {200, 288};
const int kSpellTabH = 18; // bandeau du haut de la fenêtre de gauche : ouvrir ou replier le livre de sorts

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
	GLuint helpTex = 0, gearTex = 0, menuTex = 0, hintTex = 0, chooserTex = 0;
	bool helpDirty = true, menuDirty = true, hintDirty = true, chooserDirty = true;
	bool menuKeyboard = false; // état montré dans le menu
	int dungeonMode = 1;
	void renderHint();
	// Invitation du premier lancement : elle suit doucement le regard.
	bool hintPlaced = false;
	Vec3 hintPos = {0, 0, 0};
	void renderHelp();
	void renderChooser();
	void renderGear();
	void renderMenu();
	void upload(GLuint &tex, const std::vector<uint8_t> &px, int w, int h, const char *name = "fenetre");
	// Menu du poignet : ouvert, il reste où il est apparu.
	bool menuOpen = false;
	Vec3 menuPos = {0, 0, 0}, menuRight = {1, 0, 0}, menuUp = {0, 1, 0};
	int menuHot = -1;
	// Colonne du groupe recomposée
	int chooserHot = -1; // bouton visé dans la fenêtre du choix de la vue
	Composite comp[2];
	bool spellsCollapsed = true; // livre de sorts replié : seuls le bandeau et les messages restent
	bool spellTabHot = false;
	void compose(int which, const UiFrameInput &in);
	int compOf(int panel) const { return panel == kPanelParty ? kCompParty : panel == kPanelSpells ? kCompSpells : -1; }
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
	cv.window();
	const bool fr = language == kLangFrench;
	for (Key &k : keys) {
		if (k.action == kUiActionToggleFire)
			k.label = fr ? "Feu" : "Fire"; // allumé : la touche est enfoncée, son nom en bleu
		else if (k.action == kUiActionToggleDetach)
			k.label = fr ? "Panneaux" : "Panels";
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
	// Les deux lignes d'information, à la même taille
	const int infoScale = std::min(Canvas::fit(info[0], kKbW - 20, 2), Canvas::fit(info[1], kKbW - 20, 2));
	for (int l = 0; l < 2; l++)
		cv.gtext(info[l], 10, kCellH * kRows + 3 + l * (infoScale == 1 ? 14 : 20), infoScale, kPale);
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
//    bourse) ; dessous, dans un bandeau à part, les boutons des options et du repos ;
//  - à gauche, le parchemin des sorts (pixels doublés), et dessous la ligne de messages, à sa largeur.
void Ui::Impl::compose(int which, const UiFrameInput &in) {
	Composite &c = comp[which];
	const int n = in.characters < 0 ? 0 : in.characters > 4 ? 4 : in.characters;
	const bool blank = in.compassBlank;
	const int key = which == kCompParty ? n * 2 + (blank ? 1 : 0) : (spellsCollapsed ? 1 : 0) + language * 2 + (spellTabHot ? 4 : 0);
	if (c.tex && in.gameSerial == c.serial && key == c.key)
		return;
	c.serial = in.gameSerial;
	c.key = key;
	c.texW = kCompTexW[which];
	c.texH = kCompTexH[which];
	c.pieces.clear();
	int band = -1; // début du bandeau des boutons (colonne de droite)
	if (which == kCompParty) {
		for (int i = 0; i < n; i++)
			c.pieces.push_back({0, i * 36, 66, 34, in.portraitX[i], 143, 66, 34});
		c.pieces.push_back({68, 0, 32, 122, 288, 0, 32, 122});
		int top = n * 36 > 122 ? n * 36 : 122;
		band = top + 2;
		c.pieces.push_back({27, band + 4, 45, 22, 10, 178, 45, 22});
		c.w = 100;
		c.h = band + 30;
	} else {
		// Le bandeau (dessiné plus bas), puis le parchemin s'il est ouvert, puis les messages.
		int y = kSpellTabH;
		if (!spellsCollapsed) {
			c.pieces.push_back({0, y, 224, 242, 0, 0, 112, 121});
			y += 244;
		}
		c.pieces.push_back({0, y, 224, 21, 82, 121, 236, 22});
		c.w = 224;
		c.h = y + 21;
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
					const uint8_t *src = in.gameRgba + ((size_t)sy * in.gameW + sx) * 4;
					p[0] = src[0]; p[1] = src[1]; p[2] = src[2];
				}
		}
	}
	// Le bandeau du livre de sorts : un clic l'ouvre ou le replie.
	if (which == kCompSpells) {
		const bool fr = language == kLangFrench;
		// Un bouton du jeu, à la taille de ses pixels (doublés ici comme le parchemin).
		Canvas cv{c.w, kSpellTabH, std::vector<uint8_t>((size_t)c.w * kSpellTabH * 4)};
		cv.bevel(0, 0, c.w, kSpellTabH, 2, true, spellTabHot ? kLight : kFace);
		std::string label = latin1(spellsCollapsed ? (fr ? "Livre de sorts  +" : "Spellbook  +") : (fr ? "Livre de sorts  -" : "Spellbook  -"));
		const int sc = g_font.ok() ? 2 : 1;
		cv.gcenter(label, 0, c.w, 1, sc, spellTabHot ? kWhite : kGold);
		for (int y = 0; y < kSpellTabH; y++)
			memcpy(&c.px[(size_t)y * c.texW * 4], &cv.px[(size_t)y * c.w * 4], (size_t)c.w * 4);
	}
	upload(c.tex, c.px, c.texW, c.texH, which == kCompParty ? "groupe" : "sorts");
}

// Boutons de la fenêtre du choix de la vue, en pixels de son image (même taille que l'aide).
const int kChooseY0 = 70, kChooseY1 = 330;
const int kChooseX[2][2] = {{24, 310}, {330, 616}};

// Premier lancement : vue classique ou mode immersif.
void Ui::Impl::renderChooser() {
	const bool fr = language == kLangFrench;
	Canvas cv{kHelpW, kHelpH, std::vector<uint8_t>((size_t)kHelpW * kHelpH * 4)};
	cv.window();
	cv.gcenter(latin1(fr ? "Choisis ta vue" : "Choose your view"), 0, kHelpW, 20, 3, kWhite);
	static const char *const labels[2][2] = {{"Classic", "Immersive"}, {"Classique", "Immersif"}};
	static const char *const texts[2][2] = {
		{"The game screen in a medieval library, in your room (mixed reality) or on an old 486 PC.",
		 "Walk inside the dungeon rebuilt in 3D around you. The game screen appears when needed (shops, map)."},
		{"L'écran du jeu dans une bibliothèque médiévale, dans ta pièce (réalité mixte) ou sur un vieux PC 486.",
		 "Marche dans le donjon reconstruit en 3D autour de toi. L'écran du jeu vient au besoin (boutiques, carte)."}};
	const int lang = fr ? 1 : 0;
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
	int y = 350;
	for (const std::string &l : Canvas::wrap(latin1(fr ? "Tu pourras changer plus tard : regarde ta main gauche, l'engrenage ouvre le menu des réglages."
	                                                   : "You can change this later: look at your left hand, the gear opens the settings menu."),
	                                         kHelpW - 48, 2)) {
		cv.gcenter(l, 0, kHelpW, y, 2, kGold);
		y += 24;
	}
	upload(chooserTex, cv.px, kHelpW, kHelpH, "choix");
	chooserDirty = false;
}

void Ui::Impl::renderHelp() {
	static const char *const english[] = {
		"#CONTROLLER MAPPING",
		"",
		"#Everywhere",
		"Aim + trigger ............ left click",
		"Aim + grip, or X ......... right click (drink, use)",
		"Left stick ............... walk where you look",
		"Right stick (sideways) ... turn",
		"Y ........................ map",
		"Menu button (left) ....... save, load, quit",
		"Gear on the left wrist ... settings, keyboard, help",
		"",
		"#Combat, in the dungeon",
		"A ........................ attack with the framed character",
		"B ........................ open their magic, aim at a spell",
		"Right stick (up/down) .... change character",
		"Drink a potion: aim at it and press X",
		"",
		"#Menus and cutscenes",
		"A : Enter       B : Escape (skip)",
		"",
		"#Windows and screen",
		"Brass bar + hold trigger ........ move",
		"Corner + hold trigger ..... resize",
		"Right stick click ........ recenter everything",
		"Left stick click ......... scenery (classic view)",
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
		"Joystick gauche .......... marcher là où l'on regarde",
		"Joystick droit (côtés) ... tourner",
		"Y ........................ carte",
		"Bouton menu (gauche) ..... sauvegarder, charger, quitter",
		"Engrenage au poignet gauche : réglages, clavier, aide",
		"",
		"#Combat, dans le donjon",
		"A ........................ attaquer (personnage encadré)",
		"B ........................ sa magie, puis viser un sort",
		"Joystick droit (haut/bas)  changer de personnage",
		"Boire une potion : la viser et appuyer sur X",
		"",
		"#Menus et cinématiques",
		"A : Entrée      B : Échap (passer)",
		"",
		"#Fenêtres et écran",
		"Barrette + gâchette tenue ... déplacer",
		"Coin + gâchette tenue ....... agrandir, réduire",
		"Clic du joystick droit ... tout recentrer devant soi",
		"Clic du joystick gauche .. décor (vue classique)",
		"",
		"Cliquer ce panneau pour le fermer.",
		nullptr,
	};
	const char *const *lines = language == kLangFrench ? french : english;
	Canvas cv{kHelpW, kHelpH, std::vector<uint8_t>((size_t)kHelpW * kHelpH * 4)};
	cv.window();
	// « bouton .... action » (ou deux espaces) : deux colonnes, l'écriture du jeu n'ayant pas des
	// lettres de même largeur pour aligner les points.
	auto split = [](const std::string &s, std::string &l, std::string &r) {
		size_t p = s.find(" ..");
		size_t q = p != std::string::npos ? s.find_first_not_of(". ", p) : std::string::npos;
		if (p == std::string::npos) {
			p = s.find("  ");
			q = p != std::string::npos ? s.find_first_not_of(' ', p) : std::string::npos;
		}
		if (p == std::string::npos || q == std::string::npos)
			return false;
		l = s.substr(0, p);
		r = s.substr(q);
		return true;
	};
	const int x0 = 22, xMax = kHelpW - 12;
	int col = x0, y = 10;
	for (int i = 0; lines[i]; i++) {
		const char *line = lines[i];
		bool title = line[0] == '#';
		std::string text = latin1(title ? line + 1 : line), l, r;
		if (text.empty()) {
			y += 6;
			continue;
		}
		if (title) {
			// Titre blanc, souligné d'un filet gravé (sombre puis clair)
			cv.gtext(text, 14, y, 2, kWhite);
			cv.rect(10, y + 18, kHelpW - 10, y + 19, kDark);
			cv.rect(10, y + 19, kHelpW - 10, y + 20, kLight);
			y += 23;
			// La colonne des actions de cette partie : après le plus long nom de bouton.
			col = x0;
			for (int j = i + 1; lines[j] && lines[j][0] != '#'; j++) {
				std::string t = latin1(lines[j]), a, b;
				if (split(t, a, b) && x0 + Canvas::textW(a, 2) + 24 > col)
					col = x0 + Canvas::textW(a, 2) + 24;
			}
		} else if (split(text, l, r)) {
			cv.gtext(l, x0, y, 2, kGold);
			// L'action dans sa colonne ; trop longue, collée au bord droit.
			int rw = Canvas::textW(r, 2), rx = col + rw > xMax ? xMax - rw : col;
			int sc = 2;
			if (rx < x0 + Canvas::textW(l, 2) + 8) {
				rx = col;
				sc = Canvas::fit(r, xMax - col, 2);
			}
			cv.gtext(r, rx, y, sc, kPale);
			y += 18;
		} else {
			cv.gtext(text, x0, y, Canvas::fit(text, xMax - x0, 2), kGold);
			y += 18;
		}
	}
	upload(helpTex, cv.px, kHelpW, kHelpH, "aide");
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
			// Sur la terre cuite des fenêtres, bord en relief
			const bool edge = x < 2 || y < 2 || x >= N - 2 || y >= N - 2;
			const Rgb bg = !edge ? kFace : (x < 2 || y < 2) && x < N - 2 && y < N - 2 ? kLight : kDark;
			p[0] = metal ? (uint8_t)(230 * shade) : bg.r;
			p[1] = metal ? (uint8_t)(170 * shade) : bg.g;
			p[2] = metal ? (uint8_t)(70 * shade) : bg.b;
			p[3] = 255;
		}
	upload(gearTex, px, N, N, "engrenage");
}

void Ui::Impl::renderMenu() {
	const bool fr = language == kLangFrench;
	// Comme le menu des options du jeu : le nom du réglage à gauche, sa valeur sur un bouton à droite ;
	// dessous, deux grands boutons.
	Canvas cv{kMenuW, kMenuH, std::vector<uint8_t>((size_t)kMenuW * kMenuH * 4)};
	cv.window();
	const char *names[4] = {fr ? "Langue" : "Language", fr ? "Mode immersif" : "Immersive mode",
	                        dungeonMode ? (fr ? "Écran du jeu" : "Game screen") : (fr ? "Écran classique" : "Classic screen"),
	                        fr ? "Clavier" : "Keyboard"};
	const char *values[4] = {fr ? "Français" : "English", dungeonMode ? (fr ? "Oui" : "On") : (fr ? "Non" : "Off"),
	                         // Mode immersif : l'écran du jeu au besoin ou toujours ; vue classique : l'écran entier ou en fenêtres.
	                         dungeonMode ? (dungeonMode == 2 ? (fr ? "Toujours" : "Always") : (fr ? "Au besoin" : "If needed"))
	                                     : (detached ? (fr ? "Non" : "Off") : (fr ? "Oui" : "On")),
	                         menuKeyboard ? (fr ? "Affiché" : "Shown") : (fr ? "Caché" : "Hidden")};
	const int split = 196, th = Canvas::textH(2);
	cv.gcenter("Lands of Lore VR", 0, kMenuW, (kMenuRowH - th) / 2 + 2, 2, kWhite);
	for (int i = 1; i < kMenuRows; i++) {
		const int y0 = i * kMenuRowH, by0 = y0 + 5, by1 = y0 + kMenuRowH - 5, ty = (by0 + by1 - th) / 2;
		if (i <= 4) {
			const std::string n = latin1(names[i - 1]), v = latin1(values[i - 1]);
			cv.gtext(n, split - 10 - Canvas::textW(n, Canvas::fit(n, split - 24, 2)), ty, Canvas::fit(n, split - 24, 2), kGold);
			cv.bevel(split, by0, kMenuW - 12, by1, 2, true);
			cv.gcenter(v, split, kMenuW - 12, ty, Canvas::fit(v, kMenuW - 24 - split, 2), kGold);
		} else {
			const std::string l = latin1(i == 5 ? (fr ? "Manettes et boutons" : "Controller mapping") : (fr ? "Fermer" : "Close"));
			cv.bevel(12, by0, kMenuW - 12, by1, 2, true);
			cv.gcenter(l, 12, kMenuW - 12, ty, 2, kGold);
		}
	}
	upload(menuTex, cv.px, kMenuW, kMenuH, "menu");
	menuDirty = false;
}

// Invitation du premier lancement : un engrenage et deux lignes.
void Ui::Impl::renderHint() {
	const bool fr = language == kLangFrench;
	Canvas cv{kHintW, kHintH, std::vector<uint8_t>((size_t)kHintW * kHintH * 4)};
	cv.window();
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
	cv.gtext(l1, 80, 18, Canvas::fit(l1, kHintW - 92, 2), kWhite);
	cv.gtext(l2, 80, 46, Canvas::fit(l2, kHintW - 92, 2), kGold);
	upload(hintTex, cv.px, kHintW, kHintH, "invitation");
	hintDirty = false;
}

Ui::Ui() : d(new Impl()) {
	d->buildKeys();
}

Ui::~Ui() {
	delete d;
}

const char *Ui::panelKey(int id) {
	static const char *names[kUiPanelCount] = {"groupe3", "inventaire", "sorts2", "clavier", "aide"};
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
		d->chooserDirty = true;
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

bool Ui::spellsCollapsed() const {
	return d->spellsCollapsed;
}

void Ui::setSpellsCollapsed(bool collapsed) {
	if (collapsed != d->spellsCollapsed) {
		d->spellsCollapsed = collapsed;
		d->comp[kCompSpells].key = -1;
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
				d->kbDirty = d->helpDirty = d->menuDirty = d->hintDirty = d->chooserDirty = true;
				d->comp[kCompSpells].key = -1;
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
	auto halfH = [&](int id, float scale) { return srcH(id) * 1.2f * scale * 0.5f; };

	// À gauche : le parchemin des sorts et les messages (pixels doublés dans la fenêtre recomposée).
	// Le bas de la fenêtre (les messages) est à la hauteur où il serait, livre de sorts ouvert, sous
	// le haut de la vue : ouvert ou replié, le livre garde ses messages au même endroit.
	const float sSpells = 0.0018f, openH = (float)(kSpellTabH + 244 + 21) * 1.2f * sSpells;
	set(kPanelSpells, -(viewW * 0.5f + frame + 0.035f + halfW(kPanelSpells, sSpells)), top - openH + halfH(kPanelSpells, sSpells), sSpells);
	// À droite : les personnages, l'atlas, les options et le repos.
	const float sParty = 0.0034f;
	set(kPanelParty, viewW * 0.5f + frame + 0.035f + halfW(kPanelParty, sParty), top - halfH(kPanelParty, sParty), sParty);
	// Dessous : l'inventaire.
	const float sRow = 0.0026f;
	set(kPanelInventory, 0.0f, bottom - frame - 0.045f - halfH(kPanelInventory, sRow), sRow);

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
	if (in.chooserOpen && (d->chooserDirty || !d->chooserTex))
		d->renderChooser();
	d->pressFlash -= in.dt;

	// Tant que la disposition n'est pas établie (écran pas encore placé), aucun panneau.
	bool visible[kUiPanelCount];
	for (int i = 0; i <= kPanelSpells; i++)
		visible[i] = placed && in.split;
	if (visible[kPanelParty])
		d->compose(kCompParty, in);
	if (visible[kPanelSpells])
		d->compose(kCompSpells, in);
	visible[kPanelKeyboard] = placed && in.keyboardOpen;
	visible[kPanelHelp] = placed && (in.helpOpen || in.chooserOpen);
	d->chooserHot = -1;
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
		if (i >= kPanelKeyboard) {
			// Clavier, aide : barrette au-dessus.
			hq.hh = thick;
			hq.hw = fminf(q.hw, longSide);
			hq.c = q.c + q.u * (q.hh + border + hq.hh + 0.002f);
		} else {
			// Fenêtres du jeu : barrette sur le côté, pour ne jamais se trouver sur le chemin du
			// pointeur entre l'écran et une fenêtre placée dessous. À droite pour la boussole, à gauche
			// pour les autres.
			float side = i == kPanelParty ? 1.0f : -1.0f;
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

	bool tabHot = false; // le doigt est sur le bandeau du livre de sorts
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
					out.action = kUiActionToggleDungeon;
				} else if (row == 3) {
					out.action = d->dungeonMode ? kUiActionToggleScreenAlways : kUiActionToggleDetach;
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
		if (hitIndex == kPanelHelp && in.chooserOpen) {
			// Choix de la vue : un des deux grands boutons.
			int px = (int)(u * (float)kHelpW), py = (int)(v * (float)kHelpH);
			for (int b = 0; b < 2; b++)
				if (px >= kChooseX[b][0] && px < kChooseX[b][1] && py >= kChooseY0 && py < kChooseY1)
					d->chooserHot = b;
			if (in.triggerPressed && d->chooserHot >= 0) {
				out.action = d->chooserHot == 0 ? kUiActionChooseClassic : kUiActionChooseImmersive;
				out.clicked = true;
			}
		} else if (hitIndex == kPanelHelp) {
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
			if (hitIndex == kPanelSpells && v * (float)d->comp[kCompSpells].h < (float)kSpellTabH) {
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
			} else if (d->compOf(hitIndex) >= 0) {
				// Fenêtre recomposée : retrouver le morceau visé, et l'endroit du jeu qu'il montre.
				const Composite &c = d->comp[d->compOf(hitIndex)];
				float cx = u * (float)c.w, cy = v * (float)c.h;
				for (const Piece &pc : c.pieces) {
					if (!pc.contains(cx, cy))
						continue;
					if (hitIndex == kPanelParty && pc.sx == 288 && in.compassBlank) // fenêtre éteinte : aucun clic
						break;
					out.pointerValid = true;
					out.buttonsToGame = true;
					out.gu = ((float)pc.sx + (cx - (float)pc.dx) * (float)pc.sw / (float)pc.dw) / 320.0f;
					out.gv = ((float)pc.sy + (cy - (float)pc.dy) * (float)pc.sh / (float)pc.dh) / 200.0f;
					break;
				}
			} else {
				const Source &src = kSources[hitIndex];
				out.pointerValid = true;
				out.buttonsToGame = true;
				out.gu = (src.x + u * src.w) / 320.0f;
				out.gv = (src.y + v * src.h) / 200.0f;
			}
		}
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
			if (i == kPanelHelp && in.chooserOpen) {
				p.texture = d->chooserTex;
				p.texW = kHelpW;
				p.texH = kHelpH;
				if (d->chooserHot >= 0) {
					p.hotOutline = true;
					p.hot[0] = (float)kChooseX[d->chooserHot][0] / (float)kHelpW;
					p.hot[1] = (float)kChooseY0 / (float)kHelpH;
					p.hot[2] = (float)kChooseX[d->chooserHot][1] / (float)kHelpW;
					p.hot[3] = (float)kChooseY1 / (float)kHelpH;
				}
			} else if (i == kPanelHelp) {
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
