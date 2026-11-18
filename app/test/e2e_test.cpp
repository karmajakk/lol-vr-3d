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
	uint32_t builtGfx = 0, builtWalls = 0;
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
			if (host.gameState(gs))
				printf("  état : niveau=%d boucle=%u mode=%d menu=%d\n", gs.level, gs.loopSerial, gs.controlMode, (int)gs.menuOpen);
		} else if (op == "dungeon") {
			std::string name;
			cs >> name;
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
			if (lvl.gfxSerial != builtGfx) {
				decodeDungeonTextures(g, tex);
				dun.setTextures(tex);
				builtGfx = lvl.gfxSerial;
				builtWalls = 0;
			}
			if (lvl.wallsSerial != builtWalls) {
				std::vector<DungeonVertex> verts;
				buildDungeonMesh(lvl.walls, g, tex, verts);
				dun.setMesh(verts);
				builtWalls = lvl.wallsSerial;
			}
			if (isSolidBlock(lvl.walls, lvl.block, g)) {
				printf("ÉCHEC le groupe est dans un bloc plein\n");
				failures++;
			}
			float px, pz;
			blockCenter(lvl.block, px, pz);
			Mat4 model = dungeonModel({0, 0, 0}, 0.0f, px, pz, (float)lvl.direction * 1.5707963f);
			Vec3 eye{0, 1.35f, 0};
			// Champ de vision proche de celui du jeu, pour comparer les deux images.
			Mat4 view = viewFromPose(eye, quatYaw(0.0f));
			float th = tanf(30.0f * 3.14159265f / 180.0f), tv = th * (float)H / (float)W;
			Mat4 proj = projectionFromTangents(-th, th, -tv, tv, 0.05f, 200.0f);
			glViewport(0, 0, W, H);
			glClearColor(0.02f, 0.0f, 0.04f, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			dun.render(multiply(proj, view), model, eye, 1.0f);
			saveGl(out + "/" + name + "_3d.ppm");
			saveFrame(f, out + "/" + name + "_jeu.ppm");
		}
	}
	host.stop();
	printf("%s\n", failures ? "ÉCHEC" : "fin du scénario");
	return failures ? 1 : 0;
}
