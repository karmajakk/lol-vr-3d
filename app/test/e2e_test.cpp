// Essai de bout en bout, hors casque : fait tourner le vrai jeu (cœur ScummVM pour Linux), suit un
// petit scénario d'actions, puis rend le donjon 3D à la position du groupe à côté de l'image du jeu.
// Usage : e2e_test <système> <sauvegardes> <jeu> <sortie> "<actions>"
// Actions, séparées par « ; » : wait S | key NOM | click U V | rclick U V | shot NOM | dungeon NOM
//   NOM de touche : esc, enter, up, down, left, right, space, ou une lettre.
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sstream>
#include <string>
#include <vector>

#include "core_host.h"
#include "dungeon.h"
#include "dungeon_data.h"
#include "libretro.h"
#include "ui.h"

using namespace lolvr;

static const int W = 960, H = 720;

static void logLine(int level, const char *msg) {
	if (level >= 2)
		fprintf(stderr, "[%d] %s%s", level, msg, (msg[0] && msg[strlen(msg) - 1] == '\n') ? "" : "\n");
}

static void saveFrame(const Frame &f, const std::string &path) {
	FILE *fp = fopen(path.c_str(), "wb");
	if (!fp)
		return;
	fprintf(fp, "P6\n%d %d\n255\n", f.width, f.height);
	for (size_t i = 0; i < (size_t)f.width * f.height; i++)
		fwrite(&f.rgba[i * 4], 1, 3, fp);
	fclose(fp);
	printf("écrit %s\n", path.c_str());
}

static void saveGl(const std::string &path) {
	std::vector<uint8_t> px((size_t)W * H * 4);
	glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
	FILE *f = fopen(path.c_str(), "wb");
	if (!f)
		return;
	fprintf(f, "P6\n%d %d\n255\n", W, H);
	for (int y = H - 1; y >= 0; y--)
		for (int x = 0; x < W; x++)
			fwrite(&px[((size_t)y * W + x) * 4], 1, 3, f);
	fclose(f);
	printf("écrit %s\n", path.c_str());
}

extern "C" void retro_lolvr_teleport(int level, int block, int dir);
extern "C" void retro_lolvr_test_spell(int spell);
extern "C" void retro_lolvr_test_shield(int on);

static unsigned keyCode(const std::string &k) {
	if (k == "esc") return RETROK_ESCAPE;
	if (k == "enter") return RETROK_RETURN;
	if (k == "up") return RETROK_UP;
	if (k == "down") return RETROK_DOWN;
	if (k == "left") return RETROK_LEFT;
	if (k == "right") return RETROK_RIGHT;
	if (k == "space") return RETROK_SPACE;
	if (k == "turnl") return RETROK_HOME;
	if (k == "turnr") return RETROK_PAGEUP;
	if (k.size() == 1) return (unsigned)k[0];
	return 0;
}

int main(int argc, char **argv) {
	if (argc < 6) {
		fprintf(stderr, "usage: %s system saves game out actions\n", argv[0]);
		return 2;
	}
	std::string out = argv[4];
	HostConfig cfg;
	cfg.systemDir = argv[1];
	cfg.saveDir = argv[2];
	cfg.gameDir = argv[3];
	if (const char *l = getenv("LOLVR_LANG"))
		cfg.language = atoi(l); // la langue choisie dans le casque
	cfg.log = logLine;
	CoreHost &host = CoreHost::get();
	if (!host.start(cfg))
		return 1;
	// Le groupe ne prend pas de coups (LOLVR_BOUCLIER=0 pour les prendre) : les longues vérifications
	// traversent des lieux dangereux.
	{
		const char *b = getenv("LOLVR_BOUCLIER");
		retro_lolvr_test_shield(!b || atoi(b) != 0);
	}

	// OpenGL hors écran pour le donjon
	EGLDisplay dpy = eglGetPlatformDisplay(0x31DD, EGL_DEFAULT_DISPLAY, nullptr);
	eglInitialize(dpy, nullptr, nullptr);
	EGLint ca[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_NONE};
	EGLConfig ecfg;
	EGLint n;
	eglChooseConfig(dpy, ca, &ecfg, 1, &n);
	eglBindAPI(EGL_OPENGL_ES_API);
	EGLint xa[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
	EGLContext ctx = eglCreateContext(dpy, ecfg, EGL_NO_CONTEXT, xa);
	eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);
	GLuint fbo, color, depth;
	glGenFramebuffers(1, &fbo);
	glGenTextures(1, &color);
	glBindTexture(GL_TEXTURE_2D, color);
	glTexStorage2D(GL_TEXTURE_2D, 1, GL_SRGB8_ALPHA8, W, H);
	glGenRenderbuffers(1, &depth);
	glBindRenderbuffer(GL_RENDERBUFFER, depth);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, W, H);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
	Dungeon dun;
	dun.init(true);

	Frame f;
	LevelSnapshot lvl;
	DungeonTextures tex;
	int builtDir = -1;
	uint32_t builtGfx = 0, builtWalls = 0, builtCapture = 0, builtPlaces = 0;
	int16_t audio[960];
	auto pump = [&](double seconds) {
		int steps = (int)(seconds * 100);
		for (int i = 0; i < steps && !host.finished(); i++) {
			usleep(10000);
			host.readAudio(audio, 480);
			host.fetchFrame(f);
		}
	};

	std::stringstream ss(argv[5]);
	std::string cmd;
	int failures = 0;
	while (std::getline(ss, cmd, ';')) {
		std::stringstream cs(cmd);
		std::string op;
		cs >> op;
		if (op == "wait") {
			double s = 1;
			cs >> s;
			pump(s);
		} else if (op == "watch") {
			// watch S : pendant S secondes, les objets en vol (position, hauteur) à chaque capture
			double s = 1;
			cs >> s;
			uint32_t seen = 0;
			for (int i = 0; i < (int)(s * 100) && !host.finished(); i++) {
				usleep(10000);
				host.readAudio(audio, 480);
				host.fetchFrame(f);
				LevelSnapshot w;
				if (host.levelSnapshot(w) && w.flightSerial != seen) {
					seen = w.flightSerial;
					for (const auto &fo : w.flights)
						if (fo.item) {
							int img = -1;
							for (const LevelSprite &sp : w.sprites)
								if (sp.kind == 1 && sp.id == fo.item)
									img = sp.flying ? 1 : 0;
							printf("  %4d ms : objet %d en vol à (%.3f, %.3f), hauteur %d, direction %d, image %s\n", i * 10, fo.item, fo.x / 256.0, fo.y / 256.0, fo.height, fo.dir,
							       img < 0 ? "absente" : img ? "en vol" : "au sol");
						}
				}
			}
		} else if (op == "sort") {
			// sort N : le groupe apprend le sort N, avec l'animation du jeu (le livre de sorts)
			int sp = 0;
			cs >> sp;
			retro_lolvr_test_spell(sp);
		} else if (op == "etat") {
			// etat S [NOM] : pendant S secondes, chaque changement de l'état du jeu (vue, scène, texte,
			// case, direction) ; avec NOM, une image NOM_<ms>.ppm à chaque changement
			double s = 1;
			std::string name;
			cs >> s >> name;
			std::string last;
			for (int i = 0; i < (int)(s * 100) && !host.finished(); i++) {
				usleep(10000);
				host.readAudio(audio, 480);
				host.fetchFrame(f);
				GameState gs;
				LevelSnapshot w;
				host.gameState(gs);
				host.levelSnapshot(w);
				char line[256];
				snprintf(line, sizeof line, "niveau %d mode %d scène %d image %d texte %08x rect %08x case %d dir %d", gs.level, gs.controlMode, w.sceneBusy, (int)w.scenePicture + (int)w.sceneCinema, w.textRect, w.sceneRect, w.block, w.direction);
				if (last != line) {
					last = line;
					printf("  %5d ms : %s\n", i * 10, line);
					if (!name.empty())
						saveFrame(f, out + "/" + name + "_" + std::to_string(i * 10) + ".ppm");
				}
			}
		} else if (op == "key") {
			std::string k;
			cs >> k;
			unsigned c = keyCode(k);
			host.key(c, true);
			pump(0.12);
			host.key(c, false);
			pump(0.25);
		} else if (op == "click" || op == "rclick") {
			float u = 0.5f, v = 0.5f, after = 0.3f; // click U V [attente après, 0.3 s]
			cs >> u >> v;
			if (!(cs >> after))
				after = 0.3f;
			host.setPointer(u, v);
			pump(0.15);
			host.setMouseButtons(op == "click", op == "rclick");
			pump(0.03);
			host.setMouseButtons(false, false);
			pump(after);
		} else if (op == "goto") {
			// goto NIVEAU CASE DIR : le groupe va directement là (banc d'essai)
			int lv = 1, b = 0, d = 0;
			cs >> lv >> b >> d;
			retro_lolvr_teleport(lv, b, d);
			pump(3.0);
		} else if (op == "carte") {
			// carte : le plan du niveau (# plein, . passage, @ le groupe, chiffre : type de mur spécial)
			LevelSnapshot m;
			if (host.levelSnapshot(m) && !m.vmp.empty()) {
				DungeonGfx g;
				g.vmpMap = m.vmpMap;
				g.wallFlags = m.wallFlags;
				if (m.wallCaptureSerial == m.gfxSerial && m.wallSlots > 0) { // comme l'appli : les dessins du moteur
					g.wallSlot = m.wallSlot;
					g.wallAlpha = m.wallAlpha.data();
					g.wallSlots = m.wallSlots;
				}
				printf("  niveau %d, groupe en %d\n", m.level, m.block);
				for (int z = 0; z < 32; z++) {
					char row[33];
					for (int x = 0; x < 32; x++) {
						const int b = z * 32 + x;
						row[x] = b == m.block ? '@' : isSolidBlock(m.walls, b, g) ? '#' : '.';
					}
					row[32] = 0;
					printf("  %2d %s\n", z, row);
				}
				if (getenv("LOLVR_MURS")) // les types de mur de chaque case (N E S O)
					for (int b = 0; b < 1024; b++)
						printf("    murs %d (%d,%d) : %d %d %d %d\n", b, b & 31, b >> 5, m.walls[b * 4], m.walls[b * 4 + 1], m.walls[b * 4 + 2], m.walls[b * 4 + 3]);
			}
		} else if (op == "atlas") {
			// atlas NOM [replie] : la fenêtre de l'atlas, comme dans le casque (image NOM.ppm)
			std::string name, fold;
			cs >> name >> fold;
			LevelSnapshot m;
			if (host.levelSnapshot(m)) {
				static Ui ui;
				static bool fontLoaded = false;
				if (!fontLoaded) {
					fontLoaded = true;
					ui.initGl(false);
					ui.loadGameFont(argv[3]);
				}
				ui.setAtlasCollapsed(fold == "replie");
				UiMap map;
				map.serial = m.map.serial * 4096u + (uint32_t)m.block * 4 + (uint32_t)m.direction;
				map.available = m.map.available;
				map.block = m.block;
				map.direction = m.direction;
				map.flags = m.map.flags;
				map.cls = m.map.cls;
				map.walls = m.walls;
				map.title = m.map.title;
				map.rgb = m.map.rgb.size() >= 320 * 200 * 3 ? m.map.rgb.data() : nullptr;
				for (const MapSnapshot::Legend &e : m.map.legend) {
					printf("    lieu : %s (trigger %d, case %d)\n", e.text.c_str(), e.trigger, e.mark);
					UiMap::Mark k;
					k.trigger = e.trigger;
					k.mark = e.mark;
					k.text = e.text;
					map.legend.push_back(k);
				}
				UiFrameInput in;
				in.split = true;
				in.atlasOn = true;
				in.map = &map;
				in.screenPos = in.basePos;
				ui.resetLayout(in);
				const std::string dir = out;
				setenv("LOLVR_UI_DUMP", dir.c_str(), 1);
				UiFrameOutput o;
				SceneState ss;
				ui.update(in, o, ss);
				unsetenv("LOLVR_UI_DUMP");
				char nm[512];
				snprintf(nm, sizeof(nm), "%s/atlas.ppm", dir.c_str());
				const std::string dst = out + "/" + name + ".ppm";
				rename(nm, dst.c_str());
				int seen = 0;
				for (int b = 0; b < 1024; b++)
					seen += (m.map.flags[b] & 7) == 7;
				printf("  atlas : carte %s, %d cases vues, %d lieux, titre \"%s\"\n", m.map.available ? "oui" : "non", seen, (int)m.map.legend.size(), m.map.title.c_str());
			}
		} else if (op == "sauverapide") {
			// sauverapide N : la sauvegarde du casque (boutiques) à l'emplacement N ; on attend sa confirmation
			int slot = 990;
			cs >> slot;
			GameState g0;
			host.gameState(g0);
			host.quickSave(slot);
			bool ok = false;
			for (int i = 0; i < 40 && !ok; i++) {
				pump(0.05);
				GameState g1;
				ok = host.gameState(g1) && g1.quickSaveSerial != g0.quickSaveSerial;
			}
			printf("  sauvegarde rapide %d : %s\n", slot, ok ? "faite" : "ÉCHEC");
			if (!ok)
				failures++;
		} else if (op == "ramasse") {
			// ramasse : chaque objet posé visible tour à tour, ramassé directement (sa case ou celle
			// devant) ; on dit ce qui est arrivé dans la main, puis on le repose (clic sur la vue)
			LevelSnapshot m;
			host.levelSnapshot(m);
			for (const LevelSprite &sp : m.sprites) {
				if (sp.kind != 1 || sp.ceiling || sp.flying)
					continue;
				host.pickupItem(sp.id);
				pump(0.6);
				GameState gs;
				LevelSnapshot a;
				host.gameState(gs);
				host.levelSnapshot(a);
				printf("  ramasse objet %d (x %d y %d ; groupe %d) : dans la main %d\n", sp.id, sp.x, sp.y, m.block, a.itemInHand);
				if (a.itemInHand)
					break;
			}
		} else if (op == "inv") {
			// inv NOM [deplie] [tri] : la fenêtre d'inventaire (image NOM.ppm)
			std::string name, a1, a2;
			cs >> name >> a1 >> a2;
			host.inventoryEnable();
			pump(1.5);
			if (a1 == "deplace" || a2 == "deplace") { // la case 2 vers la case 20 (prendre, poser)
				host.inventoryClick(2);
				pump(0.5);
				host.inventoryClick(20);
				pump(1.0);
			}
			if (a1 == "tri" || a2 == "tri") {
				host.inventorySort();
				pump(0.3);
			}
			LevelSnapshot m;
			const bool okL = host.levelSnapshot(m);
			printf("  inv : niveau %d, serial %u, %d octets\n", (int)okL, m.invSerial, (int)m.invRgb.size());
			if (okL && m.invRgb.size() == 176 * 132 * 3) {
				static Ui ui;
				static bool fontLoaded = false;
				if (!fontLoaded) {
					fontLoaded = true;
					ui.initGl(false);
					ui.loadGameFont(argv[3]);
				}
				ui.setInventoryExpanded(a1 == "deplie" || a2 == "deplie");
				UiFrameInput in;
				in.split = true;
				in.invRgb = m.invRgb.data();
				in.invSerial = m.invSerial;
				in.screenPos = in.basePos;
				ui.resetLayout(in);
				const std::string dir = out;
				setenv("LOLVR_UI_DUMP", dir.c_str(), 1);
				UiFrameOutput o;
				SceneState ss;
				ui.update(in, o, ss);
				unsetenv("LOLVR_UI_DUMP");
				const std::string src = dir + "/inventaire.ppm", dst = dir + "/" + name + ".ppm";
				rename(src.c_str(), dst.c_str());
			}
		} else if (op == "lang") {
			int l = 0;
			cs >> l;
			host.setGameLanguage(l); // comme le bouton de langue du casque
		} else if (op == "shot") {
			std::string name;
			cs >> name;
			pump(0.05);
			saveFrame(f, out + "/" + name + ".ppm");
			GameState gs;
			LevelSnapshot ls;
			bool hasL = host.levelSnapshot(ls);
			if (host.gameState(gs))
				printf("  état : langue=%d vr=%d nom=%d niveau=%d boucle=%u mode=%d menu=%d scène=%d boîte=%u lumière=%d case=%d dir=%d sort=%d/%u\n", gs.gameLang, gs.vrMenuSerial, (int)gs.nameEntry, gs.level, gs.loopSerial, gs.controlMode, (int)gs.menuOpen, hasL ? (int)ls.sceneBusy : -1, hasL ? (int)ls.textBox : -1, hasL ? ls.light : -1, hasL ? ls.block : -1, hasL ? ls.direction : -1, hasL ? (int)ls.spellBusy : -1, hasL ? ls.spellSerial : 0);
			if (hasL && ls.textRect)
					printf("  zone de texte : x %u y %u, %u x %u\n", ls.textRect & 511, ((ls.textRect >> 9) & 127) + 100, (ls.textRect >> 16) & 511, (ls.textRect >> 25) & 127);
			if (hasL)
				printf("  magie : menu %d, %d niveaux\n", ls.magicMenu, ls.magicLevels);
			if (hasL)
				printf("  tenu=%d curseur=%dx%d chaud=%d,%d n°%u\n", ls.itemInHand, ls.cursorW, ls.cursorH, ls.cursorHotX, ls.cursorHotY, ls.cursorSerial);
			if (hasL && ls.cursorW > 0 && ls.cursorRgba.size() >= (size_t)ls.cursorW * ls.cursorH * 4) {
				// Le curseur du jeu, agrandi 8 fois, le fond transparent en magenta
				if (FILE *cf = fopen((out + "/" + name + "_curseur.ppm").c_str(), "wb")) {
					const int z = 8;
					fprintf(cf, "P6\n%d %d\n255\n", ls.cursorW * z, ls.cursorH * z);
					for (int y = 0; y < ls.cursorH * z; y++)
						for (int x = 0; x < ls.cursorW * z; x++) {
							const uint8_t *p = &ls.cursorRgba[((size_t)(y / z) * ls.cursorW + x / z) * 4];
							const uint8_t c[3] = {p[3] ? p[0] : (uint8_t)255, p[3] ? p[1] : (uint8_t)0, p[3] ? p[2] : (uint8_t)255};
							fwrite(c, 1, 3, cf);
						}
					fclose(cf);
				}
			}
		} else if (op == "dungeon" || op == "dungeonat") {
			std::string name;
			cs >> name;
			int atBlock = -1, atDir = 0;
			// dungeonat NOM CASE DIR [CASE2 FACE TYPE] : vue depuis une case ; le mur FACE de CASE2 montré au type TYPE
			int ovBlock = -1, ovFace = 0, ovType = 0;
			if (op == "dungeonat") {
				cs >> atBlock >> atDir;
				if (!(cs >> ovBlock >> ovFace >> ovType))
					ovBlock = -1;
			}
			static int builtOverride = -2;
			if (!host.levelSnapshot(lvl)) {
				printf("ÉCHEC pas de niveau publié par le moteur\n");
				failures++;
				continue;
			}
			printf("  menu %08x, lanterne %d, clarté %d ; niveau publié : %d, case %d (x=%d y=%d), direction %d, vmp=%zu vcn=%zu, murs n°%u, graphismes n°%u\n", lvl.menuRect, lvl.lamp, lvl.light, lvl.level, lvl.block,
			       lvl.block & 31, lvl.block >> 5, lvl.direction, lvl.vmp.size(), lvl.vcn.size() / 32, lvl.wallsSerial, lvl.gfxSerial);
			{
				GameState ts;
				if (host.gameState(ts))
					printf("  décor : %s (plafond %d)\n", ts.tileset, ceilingStyleOf(ts.tileset));
			}
			DungeonGfx g;
			g.vmpMap = lvl.vmpMap;
			g.wallFlags = lvl.wallFlags;
			g.vmp = lvl.vmp.data();
			g.vmpLen = (int)lvl.vmp.size();
			g.vcn = lvl.vcn.data();
			g.vcnShift = lvl.vcnShift.data();
			g.vcnCount = (int)(lvl.vcn.size() / 32);
			g.colTable = lvl.colTable;
			g.palette = lvl.palette;
			if (lvl.wallCaptureSerial == lvl.gfxSerial && lvl.wallSlots > 0) {
				g.wallSlot = lvl.wallSlot;
				g.wallPixels = lvl.wallPixels.data();
				g.wallAlpha = lvl.wallAlpha.data();
				g.wallSlots = lvl.wallSlots;
				g.screenPalette = lvl.screenPalette;
				if (!lvl.doorPixels.empty()) {
					g.doorPixels = lvl.doorPixels.data();
					g.doorAlpha = lvl.doorAlpha.data();
					g.doorRaise = lvl.doorRaise;
				}
			}
			{
				GameState gs;
				if (host.gameState(gs))
					g.outdoor = !strcmp(gs.tileset, "FOREST1") || !strcmp(gs.tileset, "SWAMP") || !strcmp(gs.tileset, "YVEL1");
				if (host.gameState(gs))
					g.ceilingStyle = ceilingStyleOf(gs.tileset);
			}
			printf("  murs dessinés par le moteur : %d (n°%u)\n", lvl.wallSlots, lvl.wallCaptureSerial);
			if (lvl.gfxSerial != builtGfx || lvl.wallCaptureSerial != builtCapture || lvl.placeSerial != builtPlaces) {
				builtCapture = lvl.wallCaptureSerial;
				builtPlaces = lvl.placeSerial;
				builtWalls = 0xFFFFFFFFu; // le maillage aussi (les couches des images de lieux)
				decodeDungeonTextures(g, tex);
				for (size_t i = 0; i < lvl.placeTypes.size() && (i + 1) * 176 * 120 * 3 <= lvl.placeRgb.size(); i++) {
					applyPlacePicture(tex, lvl.placeTypes[i], &lvl.placeRgb[i * 176 * 120 * 3]);
					printf("  image de lieu pour le type %d\n", lvl.placeTypes[i]);
				}
				// LOLVR_DUMP_TYPE=t : les dessins d'un type de mur (mur, porte), en images
				if (const char *dt = getenv("LOLVR_DUMP_TYPE"))
				for (int t = 1; t < 256; t++) {
					bool used = false;
					for (int i = 0; i < 4096 && !used; i++)
						used = lvl.walls[i] == t;
					if (!strcmp(dt, "tous") ? !used : strcmp(dt, "portes") ? t != (atoi(dt) & 255) : tex.doorLayer[t] < 0)
						continue;
					printf("    type %d : drapeaux %02x vmp %d genre %d couche %d porte %d profondeur face %.2f porte %.2f plan %.2f levée %.2f%s\n", t, lvl.wallFlags[t], lvl.vmpMap[t], tex.kind[t], tex.typeLayer[t], tex.doorLayer[t], tex.faceDepth[t], tex.doorDepth[t], tex.planeDepth[t], tex.doorLift[t], tex.footFit[t] ? " ouverture au sol" : tex.picBox[t][2] > 0 ? " image agrandie" : "");
					{
						int shown = 0;
						for (int bb = 0; bb < 1024 && shown < 3; bb++)
							for (int k = 0; k < 4; k++)
								if (lvl.walls[bb * 4 + k] == t) {
									printf("      en case %d face %d\n", bb, k);
									shown++;
									break;
								}
					}
					// le dessin brut du moteur (le seul dessin de ce type, sans le mur dessous)
					if (g.wallSlot && g.wallSlot[t] >= 0 && g.wallSlot[t] < g.wallSlots) {
						FILE *fo = fopen((out + "/type" + std::to_string(t) + "_brut.ppm").c_str(), "wb");
						fprintf(fo, "P6\n128 96\n255\n");
						const size_t base = (size_t)g.wallSlot[t] * 128 * 96;
						for (int i = 0; i < 128 * 96; i++) {
							const uint8_t c = g.wallPixels[base + i];
							uint8_t px3[3] = {(uint8_t)(lvl.screenPalette[c * 3] * 255 / 63), (uint8_t)(lvl.screenPalette[c * 3 + 1] * 255 / 63), (uint8_t)(lvl.screenPalette[c * 3 + 2] * 255 / 63)};
							if (!g.wallAlpha[base + i]) { px3[0] = 255; px3[1] = 0; px3[2] = 255; }
							fwrite(px3, 1, 3, fo);
						}
						fclose(fo);
					}
					const int ls[3] = {tex.typeLayer[t], tex.doorLayer[t], tex.decalLayer[t]};
					for (int k = 0; k < 3; k++) {
						if (ls[k] < 0)
							continue;
						FILE *fo = fopen((out + "/type" + std::to_string(t) + (k == 2 ? "_sol" : k ? "_porte" : "_mur") + ".ppm").c_str(), "wb");
						fprintf(fo, "P6\n128 128\n255\n");
						const uint8_t *src = &tex.rgba[(size_t)ls[k] * 128 * 128 * 4];
						for (int i = 0; i < 128 * 128; i++) {
							uint8_t px3[3] = {src[i * 4], src[i * 4 + 1], src[i * 4 + 2]};
							if (!src[i * 4 + 3]) { px3[0] = 255; px3[1] = 0; px3[2] = 255; }
							fwrite(px3, 1, 3, fo);
						}
						fclose(fo);
					}
				}
				dun.setTextures(tex);
				// Les types particuliers (sans mur de pierre) : sorte et étendue de leur dessin
				for (int t = 1; t < 256; t++) {
					if (g.vmpMap[t] || !g.wallSlot || g.wallSlot[t] < 0)
						continue;
					const uint8_t *al = g.wallAlpha + (size_t)g.wallSlot[t] * 128 * 96;
					int top = 99, bottom = -1, cnt = 0;
					for (int i = 0; i < 128 * 96; i++)
						if (al[i]) { cnt++; if (top == 99) top = i / 128; bottom = i / 128; }
					if (cnt)
						printf("  type %3d drapeaux %02x : sorte %d, lignes %d-%d, %d pixels\n", t, g.wallFlags[t], tex.kind[t], top, bottom, cnt);
				}
				builtGfx = lvl.gfxSerial;
				builtWalls = 0;
			}
			const int meshDir = atBlock >= 0 ? atDir : lvl.direction;
			// comme l'app à l'arrivée sur une entrée (sortie du manoir, de la hutte) : le groupe s'y tient,
			// le bloc montré ouvert
			const int viewBlock = atBlock >= 0 ? atBlock : lvl.block;
			const int openBlock = isHoldBlock(lvl.walls, viewBlock & 1023, g) || isSolidBlock(lvl.walls, viewBlock & 1023, g) ? (viewBlock & 1023) : -1;
			const int overrideKey = (ovBlock < 0 ? -1 : (ovBlock * 4 + ovFace) * 256 + ovType) * 2048 + openBlock;
			if (lvl.wallsSerial != builtWalls || meshDir != builtDir || overrideKey != builtOverride) {
				builtDir = meshDir;
				builtOverride = overrideKey;
				std::vector<DungeonVertex> verts;
				// LOLVR_FORCE_DOOR=t : les portes fermées (type 3) montrées au type t (une porte qui
				// s'ouvre), pour voir chaque étape.
				uint8_t wallsCopy[1024 * 4];
				memcpy(wallsCopy, lvl.walls, sizeof(wallsCopy));
				if (const char *fd = getenv("LOLVR_FORCE_DOOR"))
					for (uint8_t &w : wallsCopy)
						if (w == 3)
							w = (uint8_t)atoi(fd);
				if (ovBlock >= 0) {
					// la face vue depuis l'autre côté porte le même type (le jeu les tient ensemble)
					static const int odx[4] = {0, 1, 0, -1}, odz[4] = {-1, 0, 1, 0};
					wallsCopy[(ovBlock & 1023) * 4 + (ovFace & 3)] = (uint8_t)ovType;
					const int nb = (ovBlock + odx[ovFace & 3] + odz[ovFace & 3] * 32) & 1023;
					if (lvl.walls[nb * 4 + ((ovFace + 2) & 3)] == lvl.walls[(ovBlock & 1023) * 4 + (ovFace & 3)])
						wallsCopy[nb * 4 + ((ovFace + 2) & 3)] = (uint8_t)ovType;
				}
				if (openBlock >= 0)
					memset(wallsCopy + openBlock * 4, 0, 4);
				buildDungeonMesh(wallsCopy, g, tex, verts, meshDir);
				dun.setMesh(verts);
				builtWalls = lvl.wallsSerial;
			}
			if (getenv("LOLVR_PORTES"))
				for (int b = 0; b < 1024; b++)
					for (int k = 0; k < 4; k++)
						if (lvl.walls[b * 4 + k] && tex.doorLayer[lvl.walls[b * 4 + k]] >= 0)
							printf("    porte : bloc %d (%d,%d) côté %d type %d\n", b, b & 31, b >> 5, k, lvl.walls[b * 4 + k]);
			if (!getenv("LOLVR_NOIR"))
			for (int b : {lvl.block - 33, lvl.block - 32, lvl.block - 31, lvl.block - 1, lvl.block, lvl.block + 1})
				if (b >= 0 && b < 1024)
					printf("    bloc %d (%d,%d) : %d %d %d %d\n", b, b & 31, b >> 5, lvl.walls[b * 4], lvl.walls[b * 4 + 1], lvl.walls[b * 4 + 2], lvl.walls[b * 4 + 3]);
			{
				// Monstres et objets
				static std::vector<uint8_t> rgba((size_t)kSpriteTexW * kSpriteTexH * 4);
				std::vector<DungeonSprite> list;
				const size_t one = (size_t)kSpriteW * kSpriteH;
				for (size_t i = 0; i < lvl.sprites.size() && i < (size_t)kMaxSprites; i++) {
					const LevelSprite &s = lvl.sprites[i];
					spriteToRgba(&lvl.spritePixels[i * one], &lvl.spriteAlpha[i * one], lvl.screenPalette, rgba.data());
					dun.setSpriteImage((int)i, rgba.data());
					DungeonSprite d;
					d.x = (float)s.x / 256.0f * kBlockSize;
					d.z = (float)s.y / 256.0f * kBlockSize;
					d.ceiling = s.ceiling;
					d.layer = (int)i;
					d.u0 = (float)s.x0 / kSpriteTexW; d.u1 = (float)s.x1 / kSpriteTexW;
					d.v0 = (float)s.y0 / kSpriteTexH; d.v1 = (float)s.y1 / kSpriteTexH;
					d.width = (float)(s.x1 - s.x0) * kSpritePixel;
					d.height = (float)(s.y1 - s.y0) * kSpritePixel * 1.2f;
					d.offset = ((float)(s.x0 + s.x1) * 0.5f - 88.0f) * kSpritePixel;
					bool inFlight = false;
					for (const auto &fo : lvl.flights)
						inFlight = inFlight || (fo.item && fo.item == s.id);
					const int ob = ((s.y >> 8) & 31) * 32 + ((s.x >> 8) & 31);
					if (s.kind == 1 && !inFlight && isSolidBlock(lvl.walls, ob, g)) {
						// Rangé dans un mur : comme dans l'app (main_xr.cpp), contre la face vue depuis la case de la vue
						static const int sdx[4] = {0, 1, 0, -1}, sdz[4] = {-1, 0, 1, 0};
						float px0, pz0;
						blockCenter(atBlock >= 0 ? atBlock : lvl.block, px0, pz0);
						int best = -1;
						float bestD = 1e9f;
						for (int k = 0; k < 4; k++) {
							int nx = (ob & 31) + sdx[k], nz = (ob >> 5) + sdz[k];
							if (nx < 0 || nx > 31 || nz < 0 || nz > 31 || isSolidBlock(lvl.walls, nz * 32 + nx, g))
								continue;
							if (!(lvl.wallFlags[lvl.walls[ob * 4 + k]] & 0x80))
								continue;
							float cx, cz;
							blockCenter(nz * 32 + nx, cx, cz);
							float dd = (cx - px0) * (cx - px0) + (cz - pz0) * (cz - pz0);
							if (dd < bestD) { bestD = dd; best = k; }
						}
						if (best < 0)
							continue;
						const float S = kBlockSize, eyeY = kWallHeight * 0.5f;
						float vx, vz;
						blockCenter(ob + sdz[best] * 32 + sdx[best], vx, vz);
						float nx = (float)sdx[best], nz = (float)sdz[best];
						float faceD = S * 0.5f, itemD = (vx - d.x) * nx + (vz - d.z) * nz;
						if (itemD < faceD + 0.01f) itemD = faceD + 0.01f;
						float r = faceD / itemD;
						d.x = vx + (d.x - vx) * r + nx * 0.04f;
						d.z = vz + (d.z - vz) * r + nz * 0.04f;
						d.base = eyeY * (1.0f - r);
						if (s.flying > 1) {
							const float rows = (float)(s.flying - 1) * 6.0f - 13.0f;
							d.base += (rows > 0.0f ? rows : 0.0f) * kWallHeight / 96.0f * r;
						}
						d.width *= r; d.height *= r; d.offset *= r;
						d.ceiling = false;
					}
					if (s.kind == 1 && s.flying && inFlight) { // en vol, comme dans l'app : à sa position du moment
						const float base = (float)(s.flying - 1) * 4.5f * kSpritePixel;
						d.base = base < kWallHeight * 0.6f ? base : kWallHeight * 0.6f;
						d.ceiling = false;
						for (const auto &fo : lvl.flights)
							if (fo.item == s.id) {
								d.x = (float)fo.x / 256.0f * kBlockSize;
								d.z = (float)fo.y / 256.0f * kBlockSize;
							}
					}
					list.push_back(d);
					if (!getenv("LOLVR_NOIR")) printf("    sprite %zu : %s id %d à (%.2f, %.2f) bloc %d, %d x %d px%s\n", i, s.kind ? "objet" : "monstre", s.id, s.x / 256.0, s.y / 256.0,
					       (s.y >> 8) * 32 + (s.x >> 8), s.x1 - s.x0, s.y1 - s.y0, s.flying ? (" en vol, hauteur " + std::to_string(s.flying) + " direction " + std::to_string(s.flightDir)).c_str() : "");
				}
				dun.setSprites(list);
			}
			if (getenv("DUMP_DOOR") && lvl.wallSlots > 0) {
				// La porte du type 3, et sa face, pour voir ce que le moteur dessine
				for (int part = 0; part < 2; part++) {
					int slot = lvl.wallSlot[3];
					if (slot < 0) break;
					const uint8_t *px = (part ? lvl.doorPixels : lvl.wallPixels).data() + slot * 128 * 96;
					const uint8_t *al = (part ? lvl.doorAlpha : lvl.wallAlpha).data() + slot * 128 * 96;
					std::string fn = out + (part ? "/porte3.ppm" : "/face3.ppm");
					FILE *fp = fopen(fn.c_str(), "wb");
					fprintf(fp, "P6\n128 96\n255\n");
					for (int i = 0; i < 128 * 96; i++) {
						uint8_t c[3] = {255, 0, 255};
						if (al[i]) for (int k = 0; k < 3; k++) c[k] = (uint8_t)((lvl.screenPalette[px[i] * 3 + k] & 63) * 4);
						fwrite(c, 1, 3, fp);
					}
					fclose(fp);
				}
			}
			if (openBlock >= 0)
				printf("  le groupe est dans une entrée : le bloc est montré ouvert\n");
			if (atBlock < 0 && openBlock < 0 && isSolidBlock(lvl.walls, lvl.block, g)) {
				printf("ÉCHEC le groupe est dans un bloc plein\n");
				failures++;
			}
			float px, pz;
			int vb = atBlock >= 0 ? atBlock : lvl.block, vd = atBlock >= 0 ? atDir : lvl.direction;
			blockCenter(vb, px, pz);
			Mat4 model = dungeonModel({0, 0, 0}, 0.0f, px, pz, (float)vd * 1.5707963f);
			Vec3 eye{0, 1.35f, 0};
			float camYaw = 0.0f, camPitch = 0.0f;
			// LOLVR_CAM=x,y,z,lacet,tangage : la tête ailleurs dans la case (mètres, degrés), pour
			// voir un détail de près ou de biais (pied d'une porte, bas d'une image)
			if (const char *cam = getenv("LOLVR_CAM"))
				sscanf(cam, "%f,%f,%f,%f,%f", &eye.x, &eye.y, &eye.z, &camYaw, &camPitch);
			// Champ de vision proche de celui du jeu, pour comparer les deux images.
			Quat camRot = multiply(quatYaw(camYaw * 3.14159265f / 180.0f), quatAxisAngle({1, 0, 0}, camPitch * 3.14159265f / 180.0f));
			Mat4 view = viewFromPose(eye, camRot);
			float th = tanf(45.0f * 3.14159265f / 180.0f), tv = th * (float)H / (float)W;
			Mat4 proj = projectionFromTangents(-th, th, -tv, tv, 0.05f, 200.0f);
			glViewport(0, 0, W, H);
			glClearColor(0.02f, 0.0f, 0.04f, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			if (lvl.spellBusy && lvl.spellPixels.size() == 176u * 120u) {
				static std::vector<uint8_t> fx((size_t)kSpriteTexW * kSpriteTexH * 4);
				spriteToRgba(lvl.spellPixels.data(), lvl.spellAlpha.data(), lvl.screenPalette, fx.data());
				int on = 0;
				for (uint8_t a : lvl.spellAlpha) on += a ? 1 : 0;
				printf("  sort en cours : %d pixels d'effet\n", on);
				float bx, bz;
				blockCenter(lvl.block, bx, bz);
				dun.setEffect(fx.data(), bx, bz, lvl.direction);
			} else {
				dun.setEffect(nullptr, 0, 0, 0);
			}
			dun.render(multiply(proj, view), model, eye, 1.0f);
			if (getenv("LOLVR_NOIR")) {
				// Pas d'image : la part de vide (rien de dessiné, la couleur de fond) au milieu de la vue
				std::vector<uint8_t> px((size_t)W * H * 4);
				glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
				int empty = 0, n = 0;
				for (int y = H / 4; y < H * 3 / 4; y++)
					for (int x = W / 4; x < W * 3 / 4; x++, n++) {
						const uint8_t *c = &px[((size_t)y * W + x) * 4];
						if (c[0] >= 34 && c[0] <= 44 && c[1] < 4 && c[2] >= 50 && c[2] <= 62)
							empty++;
					}
				if (empty * 100 > n * 3)
					printf("  VIDE case %d dir %d : %d %%\n", vb, vd, empty * 100 / n);
			} else {
				saveGl(out + "/" + name + "_3d.ppm");
				saveFrame(f, out + "/" + name + "_jeu.ppm");
			}
			bool placeAhead = false; // une image de lieu en grand : la 3D ne montre pas le même dessin que le jeu
			{
				static const int adx[4] = {0, 1, 0, -1}, adz[4] = {-32, 0, 32, 0};
				const int ab = (vb + adx[vd & 3] + adz[vd & 3]) & 1023;
				for (int pt : lvl.placeTypes)
					placeAhead = placeAhead || lvl.walls[ab * 4 + ((vd + 2) & 3)] == pt;
			}
			if (getenv("LOLVR_CLIC") && placeAhead)
				printf("  CLIC case %d dir %d : image de lieu en grand, non comparée\n", vb, vd);
			GameState gsClic;
			host.gameState(gsClic);
			if (getenv("LOLVR_CLIC") && !placeAhead && (lvl.sceneBusy || lvl.scenePicture || gsClic.menuOpen))
				printf("  CLIC case %d dir %d : scène en cours, non comparée\n", vb, vd);
			else if (getenv("LOLVR_CLIC") && !placeAhead && vb == lvl.block && vd == lvl.direction && f.width == 320 && f.height == 200) {
				// Les clics tombent-ils sur ce qu'on voit ? Des rayons à travers l'image 3D, comme le doigt :
				// le mur devant visé, le point du jeu où l'appli enverrait le clic ; sa couleur dans l'image
				// du jeu comparée à celle de l'image 3D (à un facteur d'éclairage près). Témoin : le même
				// calcul avec le point du jeu décalé de 8 pixels.
				std::vector<uint8_t> img((size_t)W * H * 4);
				glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, img.data());
				Vec3 c0{model.m[0], model.m[1], model.m[2]}, c1{model.m[4], model.m[5], model.m[6]}, c2{model.m[8], model.m[9], model.m[10]};
				Vec3 tr{model.m[12], model.m[13], model.m[14]};
				auto toLevel = [&](Vec3 p) { Vec3 r = p - tr; return Vec3{dot(c0, r), dot(c1, r), dot(c2, r)}; };
				auto dirLevel = [&](Vec3 v) { return Vec3{dot(c0, v), dot(c1, v), dot(c2, v)}; };
				double err[2] = {0, 0}, sab[2] = {0, 0}, sbb[2] = {0, 0}, saa = 0;
				std::vector<float> A, B[2];
				int n = 0;
				for (int sy = 0; sy < 40; sy++)
					for (int sx = 0; sx < 40; sx++) {
						const int x = W / 8 + sx * (W * 3 / 4) / 40, y = H / 8 + sy * (H * 3 / 4) / 40;
						Vec3 dcam{(2.0f * (x + 0.5f) / W - 1.0f) * th, (2.0f * (y + 0.5f) / H - 1.0f) * tv, -1.0f};
						Vec3 dir = rotate(camRot, normalize(dcam));
						DungeonPick pk;
						if (!pickDungeon(lvl.walls, vb, vd, tex, toLevel(eye), dirLevel(dir), pk) || pk.face != (vd & 3))
							continue;
						const uint8_t *a = &img[((size_t)y * W + x) * 4];
						for (int k = 0; k < 2; k++) {
							const int gx = (int)pk.px + (k ? 8 : 0), gy = (int)pk.py;
							if (gx < 112 || gx > 287 || gy < 0 || gy > 119)
								goto next;
						}
						for (int k = 0; k < 2; k++) {
							const uint8_t *b = &f.rgba[((size_t)(int)pk.py * 320 + (int)pk.px + (k ? 8 : 0)) * 4];
							for (int c = 0; c < 3; c++)
								B[k].push_back(b[c]);
						}
						for (int c = 0; c < 3; c++)
							A.push_back(a[c]);
						n++;
					next:;
					}
				if (n >= 30) {
					for (size_t i = 0; i < A.size(); i++) {
						saa += A[i] * A[i];
						for (int k = 0; k < 2; k++) { sab[k] += A[i] * B[k][i]; sbb[k] += B[k][i] * B[k][i]; }
					}
					for (int k = 0; k < 2; k++) {
						const double sc = sbb[k] > 0 ? sab[k] / sbb[k] : 1.0; // le jeu ramené à l'éclairage de la 3D
						double e = 0, m = 0;
						for (size_t i = 0; i < A.size(); i++) { e += fabs(A[i] - sc * B[k][i]); m += A[i]; }
						err[k] = m > 0 ? e / m : 0;
					}
					printf("  CLIC case %d dir %d : %d rayons sur le mur devant, écart %.0f %% (décalé de 8 px : %.0f %%)%s\n", vb, vd, n,
					       err[0] * 100, err[1] * 100, err[0] > err[1] * 0.8 ? " MAUVAIS" : "");
				}
			}
		}
	}
	host.stop();
	printf("%s\n", failures ? "ÉCHEC" : "fin du scénario");
	return failures ? 1 : 0;
}
