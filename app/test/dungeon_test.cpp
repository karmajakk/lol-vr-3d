// Banc d'essai du donjon en 3D : lit les fichiers du jeu, construit le niveau et en rend quelques
// vues hors écran (EGL sans surface, Mesa) dans des images PPM.
// Usage : dungeon_test <dossier DATA du jeu> <dossier de sortie> [niveau] [bloc direction]...
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <algorithm>
#include <string>
#include <vector>

#include "dungeon.h"
#include "dungeon_data.h"
#include "lolfiles.h"

using namespace lolvr;

static const int W = 1280, H = 960;

static void savePpm(const std::string &path) {
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

int main(int argc, char **argv) {
	if (argc < 3) {
		fprintf(stderr, "usage: %s dataDir outDir [level] [block dir]...\n", argv[0]);
		return 2;
	}
	std::string out = argv[2];
	int level = argc > 3 ? atoi(argv[3]) : 1;
	lolfiles::Archive arc;
	if (!arc.open(argv[1])) {
		fprintf(stderr, "aucun fichier PAK dans %s\n", argv[1]);
		return 1;
	}
	lolfiles::Level lv;
	if (!lv.load(arc, level)) {
		fprintf(stderr, "niveau %d illisible\n", level);
		return 1;
	}
	DungeonGfx g = lv.gfx();
	DungeonTextures tex;
	decodeDungeonTextures(g, tex);
	std::vector<DungeonVertex> verts;
	buildDungeonMesh(lv.walls, g, tex, verts);
	int open = 0;
	for (int b = 0; b < 1024; b++)
		open += isSolidBlock(lv.walls, b, g) ? 0 : 1;
	printf("niveau %d (%s) : %d couches, %zu sommets, %d cases libres\n", level, lv.area.c_str(), tex.layers, verts.size(), open);

	// Contrôles simples
	int failures = 0;
	auto check = [&](bool ok, const char *what) {
		printf("%s %s\n", ok ? "ok  " : "ÉCHEC", what);
		failures += ok ? 0 : 1;
	};
	check(tex.layers >= 3 && tex.floorLayer >= 0 && tex.ceilingLayer >= 0, "textures : murs, sol et plafond");
	check(!verts.empty() && verts.size() % 3 == 0, "géométrie : des triangles");
	check(open > 10 && open < 1024, "des cases libres et des cases pleines");
	{
		// Chaque mur visible est tourné vers une case libre (son côté visible regarde vers elle).
		bool ok = true;
		for (size_t i = 0; i + 2 < verts.size(); i += 3) {
			Vec3 a{verts[i].pos[0], verts[i].pos[1], verts[i].pos[2]}, b{verts[i + 1].pos[0], verts[i + 1].pos[1], verts[i + 1].pos[2]}, c{verts[i + 2].pos[0], verts[i + 2].pos[1], verts[i + 2].pos[2]};
			Vec3 n = normalize(cross(b - a, c - a));
			if (fabsf(n.y) > 0.5f)
				continue;
			Vec3 m = (a + b + c) * (1.0f / 3.0f) + n * (kBlockSize * 0.5f);
			int bx = (int)floorf(m.x / kBlockSize), bz = (int)floorf(m.z / kBlockSize);
			if (bx < 0 || bx > 31 || bz < 0 || bz > 31 || isSolidBlock(lv.walls, bz * 32 + bx, g))
				ok = false;
		}
		check(ok, "les murs regardent vers les cases où l'on peut se tenir");
	}

	EGLDisplay dpy = eglGetPlatformDisplay(0x31DD, EGL_DEFAULT_DISPLAY, nullptr);
	if (!eglInitialize(dpy, nullptr, nullptr)) {
		fprintf(stderr, "eglInitialize\n");
		return 1;
	}
	EGLint ca[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_NONE};
	EGLConfig cfg;
	EGLint n;
	eglChooseConfig(dpy, ca, &cfg, 1, &n);
	eglBindAPI(EGL_OPENGL_ES_API);
	EGLint xa[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
	EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, xa);
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
	dun.setTextures(tex);
	dun.setMesh(verts);
	check(dun.hasContent(), "rendu : prêt");

	// Vues : (bloc, direction) donnés, sinon les premières cases libres, regard vers chaque direction ouverte.
	std::vector<std::pair<int, int>> views;
	for (int i = 4; i + 1 < argc; i += 2)
		views.push_back({atoi(argv[i]), atoi(argv[i + 1])});
	if (views.empty())
		for (int b = 0; b < 1024 && views.size() < 4; b++)
			if (!isSolidBlock(lv.walls, b, g))
				views.push_back({b, (int)views.size() % 4});
	const float eyeH = 1.35f;
	for (size_t v = 0; v < views.size(); v++) {
		int block = views[v].first, dir = views[v].second;
		float px, pz;
		blockCenter(block, px, pz);
		// Monde : le joueur à l'origine, au sol, regard vers -z.
		Mat4 model = dungeonModel({0, 0, 0}, 0.0f, px, pz, dir * 1.5707963f);
		Vec3 eye{0, eyeH, 0};
		Mat4 view = viewFromPose(eye, quatAxisAngle({1, 0, 0}, -0.05f));
		float th = tanf(45.0f * 3.14159265f / 180.0f), tv = th * (float)H / (float)W;
		Mat4 proj = projectionFromTangents(-th, th, -tv, tv, 0.05f, 200.0f);
		glViewport(0, 0, W, H);
		glClearColor(0.02f, 0.0f, 0.04f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		dun.render(multiply(proj, view), model, eye, 1.0f);
		char name[128];
		snprintf(name, sizeof(name), "%s/niveau%d_bloc%d_dir%d.ppm", out.c_str(), level, block, dir);
		savePpm(name);
	}
	printf("%s\n", failures ? "ÉCHEC" : "TOUT EST BON");
	return failures ? 1 : 0;
}
