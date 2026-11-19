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
	cfg.log = logLine;
	CoreHost &host = CoreHost::get();
	if (!host.start(cfg))
		return 1;

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
	uint32_t builtGfx = 0, builtWalls = 0, builtCapture = 0;
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
		} else if (op == "key") {
			std::string k;
			cs >> k;
			unsigned c = keyCode(k);
			host.key(c, true);
			pump(0.12);
			host.key(c, false);
			pump(0.25);
		} else if (op == "click" || op == "rclick") {
			float u = 0.5f, v = 0.5f;
			cs >> u >> v;
			host.setPointer(u, v);
			pump(0.15);
			host.setMouseButtons(op == "click", op == "rclick");
			pump(0.12);
			host.setMouseButtons(false, false);
			pump(0.3);
		} else if (op == "shot") {
			std::string name;
			cs >> name;
			pump(0.05);
			saveFrame(f, out + "/" + name + ".ppm");
			GameState gs;
			LevelSnapshot ls;
			bool hasL = host.levelSnapshot(ls);
			if (host.gameState(gs))
				printf("  état : niveau=%d boucle=%u mode=%d menu=%d scène=%d boîte=%d lumière=%d case=%d dir=%d sort=%d/%u\n", gs.level, gs.loopSerial, gs.controlMode, (int)gs.menuOpen, hasL ? (int)ls.sceneBusy : -1, hasL ? (int)ls.textBox : -1, hasL ? ls.light : -1, hasL ? ls.block : -1, hasL ? ls.direction : -1, hasL ? (int)ls.spellBusy : -1, hasL ? ls.spellSerial : 0);
		} else if (op == "dungeon" || op == "dungeonat") {
			std::string name;
			cs >> name;
			int atBlock = -1, atDir = 0;
			if (op == "dungeonat")
				cs >> atBlock >> atDir;
			if (!host.levelSnapshot(lvl)) {
				printf("ÉCHEC pas de niveau publié par le moteur\n");
				failures++;
				continue;
			}
			printf("  niveau publié : %d, case %d (x=%d y=%d), direction %d, vmp=%zu vcn=%zu, murs n°%u, graphismes n°%u\n", lvl.level, lvl.block,
			       lvl.block & 31, lvl.block >> 5, lvl.direction, lvl.vmp.size(), lvl.vcn.size() / 32, lvl.wallsSerial, lvl.gfxSerial);
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
				}
			}
			{
				GameState gs;
				if (host.gameState(gs))
					g.outdoor = !strcmp(gs.tileset, "FOREST1") || !strcmp(gs.tileset, "SWAMP") || !strcmp(gs.tileset, "YVEL1");
			}
			printf("  murs dessinés par le moteur : %d (n°%u)\n", lvl.wallSlots, lvl.wallCaptureSerial);
			if (lvl.gfxSerial != builtGfx || lvl.wallCaptureSerial != builtCapture) {
				builtCapture = lvl.wallCaptureSerial;
				decodeDungeonTextures(g, tex);
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
			if (lvl.wallsSerial != builtWalls || meshDir != builtDir) {
				builtDir = meshDir;
				std::vector<DungeonVertex> verts;
				buildDungeonMesh(lvl.walls, g, tex, verts, meshDir);
				dun.setMesh(verts);
				builtWalls = lvl.wallsSerial;
			}
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
					list.push_back(d);
					printf("    sprite %zu : %s id %d à (%.2f, %.2f) bloc %d, %d x %d px\n", i, s.kind ? "objet" : "monstre", s.id, s.x / 256.0, s.y / 256.0,
					       (s.y >> 8) * 32 + (s.x >> 8), s.x1 - s.x0, s.y1 - s.y0);
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
			if (atBlock < 0 && isHoldBlock(lvl.walls, lvl.block, g)) {
				printf("  le groupe est dans une porte ou une entrée : la vue reste devant\n");
				atBlock = -2;
			}
			if (atBlock == -1 && isSolidBlock(lvl.walls, lvl.block, g)) {
				printf("ÉCHEC le groupe est dans un bloc plein\n");
				failures++;
			}
			float px, pz;
			static const int bdx[4] = {0, 1, 0, -1}, bdz[4] = {-32, 0, 32, 0};
			int vb = atBlock >= 0 ? atBlock : atBlock == -2 ? lvl.block - bdx[lvl.direction & 3] - bdz[lvl.direction & 3] : lvl.block, vd = atBlock >= 0 ? atDir : lvl.direction;
			blockCenter(vb, px, pz);
			Mat4 model = dungeonModel({0, 0, 0}, 0.0f, px, pz, (float)vd * 1.5707963f);
			Vec3 eye{0, 1.35f, 0};
			// Champ de vision proche de celui du jeu, pour comparer les deux images.
			Mat4 view = viewFromPose(eye, quatYaw(0.0f));
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
			saveGl(out + "/" + name + "_3d.ppm");
			saveFrame(f, out + "/" + name + "_jeu.ppm");
		}
	}
	host.stop();
	printf("%s\n", failures ? "ÉCHEC" : "fin du scénario");
	return failures ? 1 : 0;
}
