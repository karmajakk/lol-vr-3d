// Banc d'essai du décor : rend la scène hors écran (EGL sans surface, Mesa) et écrit des images PPM.
// Usage : scene_test <dossier de sortie> [prise de vue…]
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "../src/proctex.h"
#include "../src/room.h"
#include "../src/scene.h"
#include "../src/ui.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "../src/third_party/stb_image.h"

using namespace lolvr;

#ifndef GL_SRGB8_ALPHA8
#define GL_SRGB8_ALPHA8 0x8C43
#endif

static const int W = 1600, H = 1400;

static GLuint makeProgram(const char *vs, const char *fs) {
	GLuint p = glCreateProgram();
	for (int i = 0; i < 2; i++) {
		GLuint s = glCreateShader(i ? GL_FRAGMENT_SHADER : GL_VERTEX_SHADER);
		const char *src = i ? fs : vs;
		glShaderSource(s, 1, &src, nullptr);
		glCompileShader(s);
		GLint ok;
		glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
		if (!ok) {
			char log[1024];
			glGetShaderInfoLog(s, 1023, nullptr, log);
			fprintf(stderr, "shader: %s\n", log);
		}
		glAttachShader(p, s);
	}
	glLinkProgram(p);
	return p;
}

// Fausse image de jeu (aucune donnée du vrai jeu ici) : un schéma qui reprend la disposition de
// l'écran — vue du donjon, colonnes de gauche et de droite, messages, portraits, inventaire.
static GLuint makeFakeGame() {
	std::vector<uint8_t> px(320 * 200 * 4);
	auto in = [](int x, int y, int x0, int y0, int w, int h) { return x >= x0 && x < x0 + w && y >= y0 && y < y0 + h; };
	for (int y = 0; y < 200; y++)
		for (int x = 0; x < 320; x++) {
			uint8_t *p = &px[(y * 320 + x) * 4];
			float r = 0.16f, g = 0.13f, b = 0.10f; // pierre du pourtour
			if ((x / 8 + y / 8) % 2 == 0) { r *= 0.9f; g *= 0.9f; b *= 0.9f; }
			if (in(x, y, 112, 0, 176, 120)) {
				// Vue du donjon : un couloir
				float cx = (x - 200) / 88.0f, cy = (y - 60) / 60.0f;
				float d = fmaxf(fabsf(cx), fabsf(cy));
				float shade = 0.15f + 0.6f * d;
				r = 0.45f * shade; g = 0.40f * shade; b = 0.33f * shade;
				if (d < 0.25f) { r = 0.05f; g = 0.04f; b = 0.06f; }
				if (((int)(d * 8)) % 2 == 0) { r *= 0.85f; g *= 0.85f; b *= 0.85f; }
			} else if (in(x, y, 12, 0, 87, 100)) {
				// Parchemin des sorts
				r = 0.70f; g = 0.60f; b = 0.40f;
				if (y > 14 && (y - 14) % 12 < 2 && x > 20 && x < 20 + 40 + (y * 7) % 30) { r = 0.25f; g = 0.15f; b = 0.10f; }
			} else if (in(x, y, 12, 132, 63, 36)) {
				// Flèches de déplacement (3 x 2)
				int bx = (x - 12) % 21, by = (y - 132) % 18;
				r = 0.42f; g = 0.36f; b = 0.28f;
				if (bx > 6 && bx < 14 && by > 5 && by < 12) { r = 0.9f; g = 0.8f; b = 0.5f; }
				if (bx == 0 || by == 0) { r = 0.1f; g = 0.08f; b = 0.06f; }
			} else if (in(x, y, 12, 180, 40, 16)) {
				r = (x - 12) / 21 == 0 ? 0.3f : 0.55f; g = 0.35f; b = 0.5f; // options, repos
			} else if (in(x, y, 288, 0, 32, 32)) {
				// Boussole
				float dx = x - 304.0f, dy = y - 16.0f, rr = sqrtf(dx * dx + dy * dy);
				r = 0.2f; g = 0.2f; b = 0.25f;
				if (rr < 14) { r = 0.75f; g = 0.65f; b = 0.35f; }
				if (rr < 12 && fabsf(dx) < 1.5f && dy < 0) { r = 0.8f; g = 0.1f; b = 0.1f; }
			} else if (in(x, y, 290, 34, 28, 20)) {
				r = 0.45f; g = 0.25f; b = 0.12f; // atlas
			} else if (in(x, y, 292, 58, 24, 30)) {
				r = 0.85f; g = 0.7f; b = 0.25f; // lampe
			} else if (in(x, y, 83, 122, 235, 18)) {
				// Ligne de messages
				r = 0.06f; g = 0.05f; b = 0.07f;
				if (y > 127 && y < 134 && x > 90 && x < 250 && (x / 5) % 4 != 3) { r = 0.8f; g = 0.75f; b = 0.6f; }
			} else if (in(x, y, 84, 143, 232, 34)) {
				// Trois portraits avec leurs barres
				int slot = (x - 87) / 76, lx = (x - 87) % 76;
				if (x >= 87 && slot < 3 && lx < 66) {
					r = 0.22f + 0.06f * slot; g = 0.14f; b = 0.12f;
					if (lx > 1 && lx < 32) { r = 0.78f; g = 0.58f; b = 0.45f; }
					if (lx >= 33 && lx < 38) { r = 0.2f; g = 0.3f; b = 0.9f; }
					if (lx >= 39 && lx < 44) { r = 0.1f; g = 0.7f; b = 0.2f; }
					if (lx >= 46 && lx < 64 && y > 146 && y < 160) { r = 0.6f; g = 0.6f; b = 0.65f; }
					if (lx >= 46 && lx < 64 && y > 162 && y < 175) { r = 0.5f; g = 0.3f; b = 0.7f; }
				}
			} else if (in(x, y, 86, 179, 232, 20)) {
				// Inventaire : neuf cases et deux flèches
				int lx = (x - 106) % 21;
				r = 0.13f; g = 0.11f; b = 0.10f;
				if (x >= 106 && x < 295 && lx > 0 && lx < 20 && y > 180 && y < 198) { r = 0.30f; g = 0.26f; b = 0.20f; }
				if (x >= 106 && x < 295 && lx > 5 && lx < 15 && y > 184 && y < 194 && ((x - 106) / 21) % 3 != 2) { r = 0.75f; g = 0.6f; b = 0.25f; }
				if (x < 104 || x > 297) { r = 0.6f; g = 0.5f; b = 0.3f; }
			}
			p[0] = (uint8_t)(r * 255); p[1] = (uint8_t)(g * 255); p[2] = (uint8_t)(b * 255); p[3] = 255;
		}
	GLuint t;
	glGenTextures(1, &t);
	glBindTexture(GL_TEXTURE_2D, t);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, 320, 200, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	return t;
}

// Charge une image du dossier donné par la variable LOLVR_DECOR (facultatif).
static void loadDecor(Scene &scene, int slot, const char *name) {
	const char *dir = getenv("LOLVR_DECOR");
	if (!dir)
		return;
	std::string path = std::string(dir) + "/" + name + ".png";
	int w = 0, h = 0, comp = 0;
	unsigned char *px = stbi_load(path.c_str(), &w, &h, &comp, 4);
	if (!px) {
		fprintf(stderr, "image absente : %s\n", path.c_str());
		return;
	}
	scene.setImage(slot, px, w, h);
	stbi_image_free(px);
}

struct Shot {
	const char *name;
	int env, theme;
	Vec3 eye;
	float yawDeg, pitchDeg;
	float tanHalf; // demi-champ horizontal
	bool hands;
	float trigger, grip;
	int mode = 0; // 0 image entière, 1 interface détachée, 2 détachée + clavier ouvert
};

int main(int argc, char **argv) {
	std::string out = argc > 1 ? argv[1] : ".";
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
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		fprintf(stderr, "fbo incomplet\n");
		return 1;
	}

	// Programme « dessous » : image du jeu (comme le calque quad) puis faux passthrough.
	GLuint under = makeProgram(
		"#version 300 es\nlayout(location=0) in vec3 aPos; uniform mat4 uMvp; uniform vec4 uRect; out vec2 vUv;\n"
		"void main(){ vUv = uRect.xy + uRect.zw * vec2(aPos.x + 0.5, 0.5 - aPos.y / 0.75); gl_Position = uMvp * vec4(aPos, 1.0); }",
		"#version 300 es\nprecision highp float; uniform sampler2D uTex; uniform float uMode; in vec2 vUv; out vec4 o;\n"
		"void main(){ if (uMode < 0.5) { o = vec4(texture(uTex, vUv).rgb, 1.0); } else {"
		" vec2 p = gl_FragCoord.xy / vec2(1600.0, 1400.0); vec3 wall = mix(vec3(0.55,0.52,0.47), vec3(0.78,0.76,0.72), p.y);"
		" vec3 fl = vec3(0.30,0.22,0.15) * (0.6 + p.y); o = vec4(pow(p.y < 0.33 ? fl : wall, vec3(2.2)), 1.0); } }");
	const float quad[] = {-0.5f, -0.375f, 0, 0.5f, -0.375f, 0, 0.5f, 0.375f, 0, -0.5f, -0.375f, 0, 0.5f, 0.375f, 0, -0.5f, 0.375f, 0};
	GLuint qvao, qvbo;
	glGenVertexArrays(1, &qvao);
	glGenBuffers(1, &qvbo);
	glBindVertexArray(qvao);
	glBindBuffer(GL_ARRAY_BUFFER, qvbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 12, nullptr);
	glBindVertexArray(0);

	GLuint game = makeFakeGame();
	Scene scene;
	scene.init(true);
	Ui ui;
	ui.initGl(true);
	ui.setOptions(false, true);

	SceneState st;
	st.screenPos = {0, 1.30f, -2.2f};
	st.screenWidth = 2.4f;
	st.roomPos = {0, 0, 0};
	st.gameTexture = game;
	st.screenGlow = {0.10f, 0.08f, 0.06f};
	st.time = 0.0;
	// Attente des textures
	for (int i = 0; i < 4000 && !scene.ready(); i++) {
		scene.prepare(st);
		usleep(5000);
	}
	loadDecor(scene, 0, "fenetre");
	loadDecor(scene, 1, "portrait");
	loadDecor(scene, 2, "tableau1");
	loadDecor(scene, 3, "tableau2");
	st.time = 3.0; // après le fondu d'apparition

	// Joueur assis à la table : yeux à 1,20 m.
	const Vec3 seat{0, 1.20f, 0};
	const Shot shots[] = {
		{"lib_front", kEnvLibrary, kThemeKeep, seat, 0, 0, 1.15f, true, 0.0f, 0.0f},
		{"lib_left", kEnvLibrary, kThemeKeep, seat, 75, 5, 1.15f, false, 0, 0},
		{"lib_right", kEnvLibrary, kThemeKeep, seat, -75, -5, 1.15f, false, 0, 0},
		{"lib_back", kEnvLibrary, kThemeKeep, seat, 180, 10, 1.15f, false, 0, 0},
		{"lib_up", kEnvLibrary, kThemeKeep, seat, 0, 55, 1.15f, false, 0, 0},
		{"lib_table", kEnvLibrary, kThemeKeep, seat, 0, -40, 1.15f, true, 0, 0},
		{"lib_wide", kEnvLibrary, kThemeKeep, {1.9f, 2.4f, 2.2f}, 40, -22, 1.0f, false, 0, 0},
		{"lib_stand", kEnvLibrary, kThemeKeep, {0, 1.70f, 0}, 0, 0, 1.15f, false, 0, 0},
		{"window_close", kEnvLibrary, kThemeKeep, {-0.6f, 1.6f, -0.3f}, 80, 12, 0.6f, false, 0, 0},
		{"portrait_close", kEnvLibrary, kThemeKeep, {0.4f, 1.5f, -0.6f}, -90, 18, 0.75f, false, 0, 0},
		{"relics", kEnvLibrary, kThemeKeep, {0, 1.4f, -1.2f}, 0, -8, 1.3f, false, 0, 0},
		{"forest_left", kEnvLibrary, kThemeForest, seat, 75, 5, 1.15f, false, 0, 0},
		{"pt_frame", kEnvPassthroughFrame, kThemeKeep, seat, 0, 0, 1.15f, true, 0.0f, 0.0f},
		{"pt_crt", kEnvPassthroughCrt, kThemeKeep, seat, 0, -8, 1.15f, true, 0.0f, 0.0f},
		{"pt_crt_side", kEnvPassthroughCrt, kThemeKeep, {1.1f, 1.45f, -0.5f}, 38, -14, 0.8f, false, 0, 0},
		{"split_front", kEnvLibrary, kThemeKeep, seat, 0, 2, 1.25f, true, 0.0f, 0.0f, 1},
		{"split_wide", kEnvLibrary, kThemeKeep, {0, 1.20f, 0.9f}, 0, 2, 1.25f, false, 0.0f, 0.0f, 1},
		{"split_down", kEnvLibrary, kThemeKeep, seat, 0, -32, 1.0f, true, 0.0f, 0.0f, 1},
		{"split_left", kEnvLibrary, kThemeKeep, seat, 42, -10, 1.0f, false, 0, 0, 1},
		{"split_right", kEnvLibrary, kThemeKeep, seat, -42, -12, 1.0f, false, 0, 0, 1},
		{"split_pt", kEnvPassthroughFrame, kThemeKeep, seat, 0, -10, 1.25f, true, 0.0f, 0.0f, 1},
		{"corner", kEnvLibrary, kThemeKeep, seat, 0, 2, 1.25f, true, 0.0f, 0.0f, 3},
		{"help", kEnvLibrary, kThemeKeep, seat, 0, 2, 1.0f, false, 0.0f, 0.0f, 4},
		{"wrist", kEnvLibrary, kThemeKeep, seat, 10, -12, 0.6f, true, 0.0f, 0.0f, 5},
		{"hint", kEnvLibrary, kThemeKeep, seat, 0, -10, 1.0f, false, 0.0f, 0.0f, 6},
		{"keyboard", kEnvLibrary, kThemeKeep, seat, 0, -26, 0.85f, true, 0.0f, 0.0f, 2},
	};

	// --- Essai de l'interface sans casque : pointage, poignées, clavier, butées ---
	if (argc > 2 && !strcmp(argv[2], "uitest")) {
		int failures = 0;
		auto check = [&](bool ok, const char *what) {
			printf("%-62s %s\n", what, ok ? "ok" : "ÉCHEC");
			failures += ok ? 0 : 1;
		};
		const float fullW = 2.4f;
		UiFrameInput in;
		in.anchorPos = seat;
		in.basePos = {0, 1.30f, -2.2f};
		in.fullW = fullW;
		in.screenPos = in.basePos + Vec3{0, fullW * kSplitViewLift, 0};
		in.dispH = fullW * kSplitViewHeight;
		in.dispW = in.dispH * 176.0f / 144.0f;
		in.cropX = 112; in.cropY = 0; in.cropW = 176; in.cropH = 120;
		in.split = true;
		in.characters = 2;
		in.portraitX[0] = 117;
		in.portraitX[1] = 217;
		in.roomClamp = true;
		in.aimValid = true;
		in.tip = {0.2f, 0.95f, -0.3f};
		ui.resetLayout(in);
		UiFrameOutput o;
		SceneState tmp;
		auto aimAt = [&](Vec3 target) { in.dir = normalize(target - in.tip); };
		// Centre et poignée d'un panneau, tels que la scène les reçoit
		auto panel = [&](int index) { ui.update(in, o, tmp); return tmp.panels[index]; };
		// 1. Le centre de l'écran donne le centre de la vue du donjon
		aimAt(in.screenPos);
		ui.update(in, o, tmp);
		check(o.pointerValid && fabsf(o.gu * 320 - 200) < 1.5f && fabsf(o.gv * 200 - 60) < 1.5f && o.buttonsToGame && !o.pointerLong, "écran : centre de la vue du donjon (200, 60)");
		// 2. Le centre de chaque panneau donne le centre de sa zone de l'image
		// (colonne du groupe : au centre, le second personnage, 16 pixels après le bord de son cadre)
		const float want[4][2] = {{233, 168}, {201.5f, 189}, {56, 60.5f}, {200, 132}};
		const char *names[4] = {"colonne du groupe : second personnage", "fenêtre de l'inventaire", "fenêtre des sorts", "fenêtre des messages"};
		for (int i = 0; i < 4; i++) {
			aimAt(panel(i).center);
			ui.update(in, o, tmp);
			check(o.pointerValid && fabsf(o.gu * 320 - want[i][0]) < 1.5f && fabsf(o.gv * 200 - want[i][1]) < 1.5f && o.buttonsToGame, names[i]);
		}
		{
			// Haut de la colonne du groupe : la colonne de la boussole (bourse, lampe...).
			SceneState::Panel p = panel(0);
			aimAt(p.center + p.right * (-p.halfW * 0.7f) + p.up * (p.halfH * 0.5f));
			ui.update(in, o, tmp);
			check(o.pointerValid && o.gu * 320 > 288 && o.gv * 200 < 122, "colonne du groupe : à gauche, la colonne de la boussole");
		}
		// 3. Tout est dans le plan de l'écran, au-dessus du plancher, sans chevauchement
		{
			bool planar = true, above = true;
			for (int i = 0; i < 4; i++) {
				SceneState::Panel p = panel(i);
				planar = planar && fabsf(p.center.z - in.basePos.z) < 0.02f;
				above = above && p.center.y - p.halfH > 0.02f;
			}
			SceneState::Panel spells = panel(2), text = panel(3), inv = panel(1);
			check(planar, "les fenêtres sont dans le plan de l'écran");
			check(above, "aucune fenêtre ne descend sous le plancher");
			check(spells.center.y - spells.halfH > text.center.y + text.halfH + 0.01f, "les messages sous les sorts, sans chevauchement");
			check(fabsf(spells.halfW - text.halfW) < 0.01f, "les messages ont la largeur des sorts");
			check(inv.center.y + inv.halfH < in.screenPos.y - in.dispH * 0.5f - 0.10f, "l'inventaire est sous le cadre de l'écran");
		}
		// 4. Bas de l'écran : le rayon de la main arrive bien sur l'écran (rien devant), puis glisse
		//    vers le groupe et l'inventaire sans trou
		{
			aimAt(in.screenPos + Vec3{0.3f, -in.dispH * 0.5f + 0.03f, 0});
			ui.update(in, o, tmp);
			check(o.pointerValid && o.gv * 200 > 110 && o.gv * 200 < 120 && o.gu * 320 > 112 && o.gu * 320 < 288, "bas de l'écran : on vise bien le sol de la vue du donjon");
			SceneState::Panel party = panel(1);
			int valid = 0, total = 0;
			float y0 = in.screenPos.y - in.dispH * 0.5f, y1 = party.center.y + party.halfH;
			for (float y = y0; y >= y1; y -= 0.01f, total++) {
				aimAt({0.3f, y, in.basePos.z});
				ui.update(in, o, tmp);
				valid += (o.pointerValid || o.pointerLong) ? 1 : 0;
			}
			aimAt({0.3f, y1 + 0.02f, in.basePos.z});
			ui.update(in, o, tmp);
			check(valid == total, "entre l'écran et l'inventaire : toujours un retour visuel");
			// Aucune barrette sur le chemin : de l'écran à l'inventaire, à la verticale, on ne survole
			// que l'écran, son cadre et les fenêtres.
			SceneState::Panel inv = panel(1);
			bool clear = true;
			for (float y = y0; y >= inv.center.y - inv.halfH; y -= 0.01f) {
				aimAt({0.3f, y, in.basePos.z});
				in.triggerPressed = in.triggerDown = true;
				ui.update(in, o, tmp);
				clear = clear && !o.grabbed;
				in.triggerPressed = in.triggerDown = false;
				ui.update(in, o, tmp);
			}
			check(clear, "de l'écran à l'inventaire : aucune barrette sur le chemin");
		}
		// 5. Juste à côté d'une fenêtre : le pointeur s'y accroche
		{
			SceneState::Panel inv = panel(1);
			aimAt(inv.center + inv.right * (inv.halfW + 0.04f));
			ui.update(in, o, tmp);
			check(o.pointerValid && fabsf(o.gu * 320 - 320) < 2.0f, "juste à droite de l'inventaire : accroché à son bord");
		}
		// 6. Poignée de l'inventaire : on l'attrape, on la déplace, on la lâche
		{
			int id = kPanelInventory;
			ui.update(in, o, tmp);
			Vec3 handle = tmp.handles[id].center;
			UiPlacement before = ui.placement[id];
			aimAt(handle);
			ui.update(in, o, tmp);
			check(o.pointerLong && o.hitValid, "poignée visée : rayon entier jusqu'à la poignée");
			in.triggerDown = in.triggerPressed = true;
			ui.update(in, o, tmp);
			check(o.grabbed && o.dragging && !o.buttonsToGame, "poignée de l'inventaire : prise");
			in.triggerPressed = false;
			aimAt(handle + Vec3{0.25f, 0.10f, 0});
			ui.update(in, o, tmp);
			UiPlacement after = ui.placement[id];
			check(o.dragging && after.a < before.a - 0.03f && after.b > before.b + 0.02f, "la fenêtre suit le doigt (vers la droite et le haut)");
			in.stickX = 1.0f; in.dt = 0.5f;
			ui.update(in, o, tmp);
			check(ui.placement[id].scale > after.scale * 1.2f, "joystick à droite : la fenêtre grandit");
			in.stickX = 0; in.stickY = -1.0f;
			ui.update(in, o, tmp);
			check(ui.placement[id].c > 0.2f, "joystick vers soi : la fenêtre se rapproche");
			in.stickY = 0; in.dt = 0.016f;
			in.triggerDown = false;
			ui.update(in, o, tmp);
			check(o.released && o.layoutChanged && !o.dragging, "poignée lâchée : disposition à mémoriser");
		}
		// 7. Butées : une fenêtre poussée sous le plancher ou dans un mur est ramenée dans la pièce
		{
			ui.placement[kPanelParty] = {0.0f, -1.2f, 2.0f, 0.006f};
			SceneState::Panel p = panel(0);
			check(p.center.y - p.halfH > 0.0f, "fenêtre placée sous le plancher : remontée");
			ui.placement[kPanelSpells] = {1.5f, 0.0f, 5.0f, 0.006f};
			p = panel(2);
			check(p.center.x > -kRoomHalfX + 0.3f, "fenêtre placée dans le mur : ramenée dans la pièce");
			ui.resetLayout(in);
		}
		// 7b. Indépendance : déplacer ou agrandir l'écran ne bouge aucune fenêtre
		{
			Vec3 before[5];
			float size[5];
			for (int i = 0; i < 5; i++) { SceneState::Panel p = panel(i); before[i] = p.center; size[i] = p.halfW; }
			UiFrameInput saved = in;
			in.basePos = in.basePos + Vec3{0.6f, 0.2f, 0.3f};
			in.screenPos = in.screenPos + Vec3{0.6f, 0.2f, 0.3f};
			in.fullW *= 1.5f; in.dispW *= 1.5f; in.dispH *= 1.5f;
			bool same = true;
			for (int i = 0; i < 5; i++) { SceneState::Panel p = panel(i); same = same && length(p.center - before[i]) < 1e-3f && fabsf(p.halfW - size[i]) < 1e-4f; }
			check(same, "écran déplacé et agrandi : les fenêtres ne bougent pas");
			in = saved;
		}
		// 7c. Coins : on attrape un coin, on l'écarte, la fenêtre grandit ; pareil pour l'écran
		{
			SceneState::Panel inv = panel(1);
			Vec3 corner = inv.center + inv.right * (inv.halfW + 0.02f) + inv.up * (inv.halfH + 0.02f); // coin sans barrette
			float scale0 = ui.placement[kPanelInventory].scale;
			aimAt(corner);
			ui.update(in, o, tmp);
			check(o.hitValid && o.pointerLong && !o.pointerValid, "coin de l'inventaire visé : équerre, pas de clic pour le jeu");
			in.triggerDown = in.triggerPressed = true;
			ui.update(in, o, tmp);
			check(o.grabbed && o.dragging && !o.buttonsToGame, "coin de l'inventaire : pris");
			in.triggerPressed = false;
			for (int k = 0; k < 30; k++) {
				aimAt(inv.center + (corner - inv.center) * 1.5f);
				ui.update(in, o, tmp);
			}
			float grown = ui.placement[kPanelInventory].scale / scale0;
			check(grown > 1.4f && grown < 1.6f, "coin écarté de moitié : la fenêtre grandit d'autant");
			SceneState::Panel inv2 = panel(1);
			check(length(inv2.center - inv.center) < 1e-3f, "… autour de son centre");
			for (int k = 0; k < 40; k++) {
				aimAt(inv.center + (corner - inv.center) * 0.7f);
				ui.update(in, o, tmp);
			}
			check(ui.placement[kPanelInventory].scale < scale0 * 0.8f, "coin rapproché : la fenêtre rapetisse");
			in.triggerDown = false;
			ui.update(in, o, tmp);
			check(o.released && o.layoutChanged, "coin lâché : disposition à mémoriser");
			ui.resetLayout(in);
			// Écran : coin haut-gauche, sur le cadre
			Vec3 sc = in.screenPos + Vec3{-(in.dispW * 0.5f + 0.095f * in.dispW), in.dispH * 0.5f + 0.095f * in.dispW, 0};
			aimAt(sc);
			ui.update(in, o, tmp);
			check(o.hitValid && !o.pointerValid, "coin du cadre de l'écran visé");
			in.triggerDown = in.triggerPressed = true;
			ui.update(in, o, tmp);
			in.triggerPressed = false;
			aimAt(in.screenPos + (sc - in.screenPos) * 1.2f);
			ui.update(in, o, tmp);
			check(o.screenResizing && o.screenZoom > 1.05f && !o.screenDragging, "coin de l'écran écarté : l'écran grandit");
			in.triggerDown = false;
			ui.update(in, o, tmp);
			// Le coin de l'image elle-même reste au jeu
			aimAt(in.screenPos + Vec3{-(in.dispW * 0.5f - 0.02f), in.dispH * 0.5f - 0.02f, 0});
			ui.update(in, o, tmp);
			check(o.pointerValid && o.buttonsToGame, "coin de l'image du jeu : le clic va au jeu");
		}
		// 8. Poignée de l'écran
		{
			ui.update(in, o, tmp);
			Vec3 handle = tmp.handles[4].center;
			aimAt(handle);
			in.triggerDown = in.triggerPressed = true;
			ui.update(in, o, tmp);
			check(o.grabbed && o.screenDragging, "poignée de l'écran : prise");
			in.triggerPressed = false;
			aimAt(handle + Vec3{-0.5f, 0, 0});
			ui.update(in, o, tmp);
			check(o.screenDragging && o.screenCenter.x < in.screenPos.x - 0.3f && fabsf(o.screenCenter.y - in.screenPos.y) < 0.1f, "l'écran suit le doigt vers la gauche");
			in.triggerDown = false;
			ui.update(in, o, tmp);
		}
		// 9. Clavier : la touche « q », puis Maj + « a »
		{
			in.keyboardOpen = true;
			ui.update(in, o, tmp);
			SceneState::Panel kb = tmp.panels[4];
			auto keyPos = [&](float col, float row) {
				return kb.center + kb.right * ((col - 5.5f) / 5.5f * kb.halfW) + kb.up * ((1.0f - 2.0f * row * 40.0f / 284.0f) * kb.halfH);
			};
			auto press = [&](float col, float row) {
				aimAt(keyPos(col, row));
				in.triggerDown = in.triggerPressed = true;
				ui.update(in, o, tmp);
				UiFrameOutput first = o;
				in.triggerDown = in.triggerPressed = false;
				ui.update(in, o, tmp);
				return first;
			};
			UiFrameOutput k = press(0.5f, 2.5f);
			check(k.keyCount == 1 && k.keys[0] == 'q' && k.keyMods[0] == 0 && !k.buttonsToGame, "clavier : touche q");
			press(0.5f, 4.5f);
			k = press(0.5f, 3.5f);
			check(k.keyCount == 1 && k.keys[0] == 'a' && k.keyMods[0] == kUiModShift, "clavier : Maj puis a = A");
			k = press(9.5f, 5.5f);
			check(k.keyCount == 1 && k.keys[0] == 13, "clavier : Entrée");
			k = press(1.0f, 0.5f);
			check(k.action == kUiActionToggleFire, "clavier : bouton du feu");
			in.keyboardOpen = false;
			// Panneau d'aide : visible, et un clic dessus le ferme
			in.helpOpen = true;
			ui.update(in, o, tmp);
			SceneState::Panel help = tmp.panels[4];
			check(help.visible && help.texture != 0, "aide : le panneau est affiché");
			aimAt(help.center);
			in.triggerDown = in.triggerPressed = true;
			ui.update(in, o, tmp);
			check(o.action == kUiActionCloseHelp && !o.buttonsToGame, "aide : un clic la ferme");
			in.triggerDown = in.triggerPressed = false;
			in.helpOpen = false;
			// Premier lancement : la fenêtre du choix de la vue, deux boutons.
			in.chooserOpen = true;
			ui.update(in, o, tmp);
			SceneState::Panel ch = tmp.panels[4];
			check(ch.visible && ch.texture != 0 && ch.texture != help.texture, "choix de la vue : la fenêtre est affichée");
			auto pressAt = [&](float px, float py) {
				aimAt(ch.center + ch.right * ((px / 584.0f * 2.0f - 1.0f) * ch.halfW) + ch.up * ((1.0f - py / 470.0f * 2.0f) * ch.halfH));
				in.triggerDown = in.triggerPressed = true;
				ui.update(in, o, tmp);
				UiFrameOutput r = o;
				in.triggerDown = in.triggerPressed = false;
				ui.update(in, o, tmp);
				return r;
			};
			check(pressAt(150, 200).action == kUiActionChooseClassic, "choix de la vue : bouton classique");
			check(pressAt(430, 200).action == kUiActionChooseImmersive, "choix de la vue : bouton immersif");
			UiFrameOutput none = pressAt(292, 420);
			check(none.action == kUiActionNone && !none.buttonsToGame, "choix de la vue : à côté des boutons, rien");
			in.chooserOpen = false;
			in.triggerDown = in.triggerPressed = false;
			in.helpOpen = false;
			// Menu du poignet : l'engrenage n'apparaît que si l'on regarde la main gauche
			in.wristValid = true;
			in.wristPos = {-0.25f, 0.95f, -0.35f};
			in.headPos = seat;
			in.headDir = {0, 0, -1};
			ui.update(in, o, tmp);
			int shown = 0;
			for (int i = 0; i < SceneState::kMaxPanels; i++) shown += tmp.panels[i].visible ? 1 : 0;
			// Premier lancement : une invitation sous le regard, tant que le menu n'a jamais été ouvert
			in.gearHint = true;
			ui.update(in, o, tmp);
			int withHint = 0;
			for (int i = 0; i < SceneState::kMaxPanels; i++) withHint += tmp.panels[i].visible ? 1 : 0;
			check(withHint == shown + 1, "premier lancement : l'invitation à regarder la main gauche est affichée");
			in.headDir = normalize(in.wristPos + Vec3{0, 0.075f, 0.06f} - in.headPos);
			ui.update(in, o, tmp);
			int shown2 = 0, gear = -1;
			for (int i = 0; i < SceneState::kMaxPanels; i++)
				if (tmp.panels[i].visible) { shown2++; gear = i; }
			check(shown2 == shown + 1, "poignet : l'engrenage apparaît (et l'invitation s'efface) quand on regarde la main gauche");
			Vec3 gearPos = tmp.panels[gear].center;
			in.tip = {0.25f, 0.95f, -0.30f};
			aimAt(gearPos);
			in.triggerDown = in.triggerPressed = true;
			ui.update(in, o, tmp);
			check(o.gearMenuOpened, "poignet : ouvrir le menu est signalé (l'invitation ne reviendra plus)");
			in.gearHint = false;
			in.triggerDown = in.triggerPressed = false;
			ui.update(in, o, tmp);
			int shown3 = 0, menu = -1;
			for (int i = 0; i < SceneState::kMaxPanels; i++)
				if (tmp.panels[i].visible) { shown3++; menu = i; }
			check(o.action == kUiActionNone && shown3 == shown2 + 1, "poignet : un clic sur l'engrenage ouvre le menu");
			SceneState::Panel m = tmp.panels[menu];
			auto row = [&](int r) { return m.center + m.up * ((1.0f - 2.0f * (r + 0.5f) / 7.0f) * m.halfH); };
			aimAt(row(1));
			in.triggerDown = in.triggerPressed = true;
			ui.update(in, o, tmp);
			check(o.action == kUiActionToggleLanguage && !o.buttonsToGame, "menu : première ligne, changer de langue");
			in.triggerDown = in.triggerPressed = false;
			ui.update(in, o, tmp);
			aimAt(row(2));
			in.triggerDown = in.triggerPressed = true;
			ui.update(in, o, tmp);
			check(o.action == kUiActionToggleDungeon, "menu : deuxième ligne, le mode immersif");
			in.triggerDown = in.triggerPressed = false;
			ui.update(in, o, tmp);
			aimAt(row(3));
			in.triggerDown = in.triggerPressed = true;
			ui.update(in, o, tmp);
			check(o.action == kUiActionToggleScreenAlways, "menu : troisième ligne, l'écran du jeu (mode immersif)");
			in.triggerDown = in.triggerPressed = false;
			ui.setDungeonOption(0);
			ui.update(in, o, tmp);
			aimAt(row(3));
			in.triggerDown = in.triggerPressed = true;
			ui.update(in, o, tmp);
			check(o.action == kUiActionToggleDetach, "menu : troisième ligne, l'écran classique (vue classique)");
			ui.setDungeonOption(1);
			in.triggerDown = in.triggerPressed = false;
			ui.update(in, o, tmp);
			aimAt(row(4));
			in.triggerDown = in.triggerPressed = true;
			ui.update(in, o, tmp);
			check(o.action == kUiActionToggleKeyboard, "menu : quatrième ligne, le clavier (et le menu se ferme)");
			in.triggerDown = in.triggerPressed = false;
			ui.update(in, o, tmp);
			// Rouvrir le menu pour la ligne des manettes
			in.headDir = normalize(in.wristPos + Vec3{0, 0.075f, 0.06f} - in.headPos);
			aimAt(gearPos);
			in.triggerDown = in.triggerPressed = true;
			ui.update(in, o, tmp);
			in.triggerDown = in.triggerPressed = false;
			ui.update(in, o, tmp);
			aimAt(row(5));
			in.triggerDown = in.triggerPressed = true;
			ui.update(in, o, tmp);
			check(o.action == kUiActionToggleHelp, "menu : cinquième ligne, les manettes");
			in.triggerDown = in.triggerPressed = false;
			in.wristValid = false;
			in.keyboardOpen = false;
		}
		// 10. Image entière : pas de fenêtres, le coin de l'écran donne le coin de l'image
		{
			in.split = false;
			in.screenPos = in.basePos;
			in.dispW = 2.4f; in.dispH = 1.8f;
			in.cropX = 0; in.cropY = 0; in.cropW = 320; in.cropH = 200;
			aimAt(in.screenPos + Vec3{-1.19f, 0.89f, 0});
			ui.update(in, o, tmp);
			int n = 0;
			for (int i = 0; i < SceneState::kMaxPanels; i++) n += tmp.panels[i].visible ? 1 : 0;
			check(n == 0 && o.pointerValid && o.gu * 320 < 3.0f && o.gv * 200 < 3.0f, "image entière : aucune fenêtre, coin haut gauche");
			aimAt(in.screenPos + Vec3{2.2f, 0, 0});
			ui.update(in, o, tmp);
			check(!o.pointerValid && o.pointerLong, "loin de l'écran : rayon entier, pas de clic envoyé au jeu");
		}
		printf("%s\n", failures ? "ÉCHEC" : "TOUT EST BON");
		return failures ? 1 : 0;
	}

	std::vector<uint8_t> buf(W * H * 4);
	for (const Shot &sh : shots) {
		bool wanted = argc <= 2;
		for (int i = 2; i < argc; i++)
			if (!strcmp(argv[i], sh.name))
				wanted = true;
		if (!wanted)
			continue;
		st.environment = sh.env;
		st.theme = sh.theme;
		const bool split = sh.mode == 1 || (sh.mode >= 3 && sh.mode != 6);
		// Écran : image entière (4:3), ou vue du donjon seule à la même hauteur.
		float fullW = 2.4f;
		Vec3 screenPos = {0, 1.30f, -2.2f};
		if (sh.env == kEnvPassthroughCrt) {
			fullW = 0.62f;
			screenPos = {0, 1.02f, -0.95f};
		} else if (sh.env == kEnvPassthroughFrame) {
			fullW = 1.8f;
			screenPos = {0, 1.25f, -2.0f};
		}
		float rect[4] = {0, 0, 1, 1};
		float quadW = fullW, quadH = fullW * 0.75f;
		Vec3 basePos = screenPos;
		if (split) {
			rect[0] = 112.0f / 320.0f; rect[1] = 0.0f; rect[2] = 176.0f / 320.0f; rect[3] = 120.0f / 200.0f;
			quadH = fullW * kSplitViewHeight;
			quadW = quadH * 176.0f / 144.0f;
			screenPos.y += fullW * kSplitViewLift;
		}
		st.screenPos = screenPos;
		st.screenWidth = quadW;
		st.screenAspect = quadH / quadW;
		for (int h = 0; h < 2; h++) {
			HandState &hand = st.hands[h];
			hand.visible = sh.hands;
			float sx = h == 0 ? -1.0f : 1.0f;
			hand.pos = {sx * 0.24f, 0.90f, -0.36f};
			hand.rot = multiply(quatYaw(-sx * 0.12f), quatAxisAngle({1, 0, 0}, 0.12f));
			hand.trigger = sh.trigger;
			hand.grip = sh.grip;
			hand.pointing = h == 1;
		}
		// Interface : mêmes calculs que dans le casque
		UiFrameInput in;
		in.anchorPos = seat;
		in.anchorYaw = 0.0f;
		in.screenPos = st.screenPos;
		in.screenRot = st.screenRot;
		in.dispW = quadW;
		in.dispH = quadH;
		in.cropX = rect[0] * 320.0f; in.cropY = rect[1] * 200.0f; in.cropW = rect[2] * 320.0f; in.cropH = rect[3] * 200.0f;
		in.crt = sh.env == kEnvPassthroughCrt;
		in.split = split;
		in.basePos = basePos;
		in.fullW = fullW;
		in.roomClamp = sh.env == kEnvLibrary;
		in.keyboardOpen = sh.mode == 2;
		in.aimValid = sh.hands;
		if (sh.hands) {
			const HandState &hand = st.hands[1];
			in.tip = hand.pos + rotate(hand.rot, gloveFingertip(false, 0.0f));
			if (sh.mode == 2) {
				// Le doigt vise une touche du clavier
				st.hands[1].rot = multiply(quatYaw(0.30f), quatAxisAngle({1, 0, 0}, -0.55f));
				in.tip = st.hands[1].pos + rotate(st.hands[1].rot, gloveFingertip(false, 0.0f));
			}
			in.dir = rotate(st.hands[1].rot, {0, 0, -1});
			if (sh.mode == 3) // le doigt vise le coin haut droit du cadre de l'écran
				in.dir = normalize(in.screenPos + Vec3{in.dispW * 0.595f, in.dispH * 0.5f + in.dispW * 0.095f, 0} - in.tip);
		}
		ui.setInfo("Images : fenetre OUI, portrait OUI, tableau1 non, tableau2 non", "Dossier decor : fenetre.png portrait.png");
		in.helpOpen = sh.mode == 4;
		ui.setLanguage(sh.mode == 4 ? kLangFrench : kLangEnglish);
		if (sh.mode == 5) {
			// La main gauche levée devant le visage, l'engrenage puis le menu
			st.hands[0].pos = {-0.10f, 1.08f, -0.42f};
			in.wristValid = true;
			in.wristPos = st.hands[0].pos;
			in.wristRot = st.hands[0].rot;
			in.headPos = sh.eye;
			in.headDir = normalize(in.wristPos - in.headPos);
		}
		in.gearHint = sh.mode == 6;
		if (sh.mode == 6) {
			in.headPos = sh.eye;
			in.headDir = {0, 0, -1};
		}
		if (sh.mode == 1) {
			in.partyMark[0] = 149; in.partyMark[1] = 143; in.partyMark[2] = 66; in.partyMark[3] = 34;
		}
		ui.resetLayout(in);
		UiFrameOutput uo;
		if (sh.mode == 5) {
			UiFrameInput click = in;
			click.dir = normalize(in.wristPos + rotate(in.wristRot, {0.0f, 0.075f, 0.06f}) - in.tip);
			click.triggerDown = click.triggerPressed = true;
			ui.update(click, uo, st);
			if (!uo.clicked) { // refermé par une prise précédente : on rouvre
				ui.update(in, uo, st);
				ui.update(click, uo, st);
			}
		}
		ui.update(in, uo, st);
		ui.update(in, uo, st);
		st.hitValid = uo.hitValid;
		st.hit = uo.hit;
		st.hitRight = uo.hitRight;
		st.hitUp = uo.hitUp;
		st.hitRadius = uo.hitRadius;
		st.pointerLong = uo.pointerLong;
		st.clickPulse = 0.3f;
		// Changement de thème : attendre la tâche de fond
		for (int i = 0; i < 4000; i++) {
			scene.prepare(st);
			st.time += 0.016;
			if (i > 2 && scene.ready())
				break;
			usleep(2000);
		}
		for (int i = 0; i < 150; i++) {
			usleep(3000);
			scene.prepare(st);
		}
		Quat rot = multiply(quatYaw(sh.yawDeg * 3.14159265f / 180.0f), quatAxisAngle({1, 0, 0}, sh.pitchDeg * 3.14159265f / 180.0f));
		Mat4 view = viewFromPose(sh.eye, rot);
		float th = sh.tanHalf, tv = th * (float)H / (float)W;
		Mat4 proj = projectionFromTangents(-th, th, -tv, tv, 0.05f, 100.0f);
		glBindFramebuffer(GL_FRAMEBUFFER, fbo);
		glViewport(0, 0, W, H);
		scene.render(view, proj, st);
		// Compositeur simulé : image du jeu sous le décor, puis faux passthrough sous le tout.
		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE_MINUS_DST_ALPHA, GL_ONE);
		glUseProgram(under);
		glBindVertexArray(qvao);
		if (Scene::usesQuadLayer(sh.env)) {
			// Le quad de test mesure 1 x 0,75 : on l'étire à la taille réelle de la vue affichée.
			Mat4 stretch = identity();
			stretch.m[0] = quadW;
			stretch.m[5] = quadH / 0.75f;
			Mat4 mvp = multiply(multiply(multiply(proj, view), poseMatrix(st.screenPos, st.screenRot, 1.0f)), stretch);
			glUniformMatrix4fv(glGetUniformLocation(under, "uMvp"), 1, GL_FALSE, mvp.m);
			glUniform4f(glGetUniformLocation(under, "uRect"), rect[0], rect[1], rect[2], rect[3]);
			glUniform1f(glGetUniformLocation(under, "uMode"), 0.0f);
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, game);
			glDrawArrays(GL_TRIANGLES, 0, 6);
		}
		if (sh.env != kEnvLibrary) {
			Mat4 full = identity();
			full.m[0] = 2.0f;
			full.m[5] = 2.0f / 0.75f;
			glUniformMatrix4fv(glGetUniformLocation(under, "uMvp"), 1, GL_FALSE, full.m);
			glUniform4f(glGetUniformLocation(under, "uRect"), 0, 0, 1, 1);
			glUniform1f(glGetUniformLocation(under, "uMode"), 1.0f);
			glDrawArrays(GL_TRIANGLES, 0, 6);
		}
		glDisable(GL_BLEND);
		glBindVertexArray(0);
		glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, buf.data());
		std::string path = out + "/" + sh.name + ".ppm";
		FILE *f = fopen(path.c_str(), "wb");
		fprintf(f, "P6\n%d %d\n255\n", W, H);
		for (int y = H - 1; y >= 0; y--)
			for (int x = 0; x < W; x++)
				fwrite(&buf[(y * W + x) * 4], 1, 3, f);
		fclose(f);
		fprintf(stderr, "écrit %s (GL %x)\n", path.c_str(), glGetError());
	}
	return 0;
}
