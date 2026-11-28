// lol-vr — application Meta Quest : Lands of Lore en VR.
//
// Application Android 100 % native (NativeActivity) :
//   - OpenXR pour le casque et les manettes ;
//   - l'image du jeu est affichée sur un calque « quad » (netteté maximale) ;
//   - un calque de projection dessine le décor autour de l'écran (bibliothèque médiévale,
//     cadre, moniteur cathodique), les gants et le pointeur (voir scene.cpp) ;
//   - la réalité mixte (passthrough) remplace la bibliothèque par la vraie pièce du joueur ;
//   - AAudio pour le son, avec une ambiance sonore spatialisée (voir ambience.cpp) ;
//   - le cœur ScummVM tourne dans son propre fil (voir core_host.cpp).

#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include <jni.h>

#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include <aaudio/AAudio.h>
#include <android/asset_manager.h>
#include <android/configuration.h>
#include <android/log.h>
#include <android/native_activity.h>
#include <android_native_app_glue.h>

#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_OPENGL_ES
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include "ambience.h"
#include "core_host.h"
#include "libretro.h"
#include "proctex.h"
#include "room.h"
#include "dungeon.h"
#include "dungeon_data.h"
#include "scene.h"
#include "ui.h"
#include "vecmath.h"

#include <ctype.h>
#include <dirent.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wsign-compare"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#include "third_party/stb_image.h"
#pragma clang diagnostic pop

#define LOG_TAG "lolvr"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#ifndef GL_SRGB8_ALPHA8
#define GL_SRGB8_ALPHA8 0x8C43
#endif

using lolvr::Ambience;
using lolvr::CoreHost;
using lolvr::Frame;
using lolvr::Mat4;
using lolvr::Quat;
using lolvr::Scene;
using lolvr::SceneState;
using lolvr::Vec3;
using lolvr::clampf;
using lolvr::quatYaw;

namespace {

// ---------------------------------------------------------------------------
// Petites conversions

Vec3 toVec3(const XrVector3f &v) { return {v.x, v.y, v.z}; }
Quat toQuat(const XrQuaternionf &q) { return {q.x, q.y, q.z, q.w}; }
XrVector3f toXr(Vec3 v) { return {v.x, v.y, v.z}; }
XrQuaternionf toXr(Quat q) { return {q.x, q.y, q.z, q.w}; }

double nowSeconds() {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

// ---------------------------------------------------------------------------
// Fichiers

void makeDirs(const std::string &path) {
	for (size_t i = 1; i <= path.size(); i++) {
		if (i == path.size() || path[i] == '/') {
			std::string sub = path.substr(0, i);
			if (mkdir(sub.c_str(), 0775) != 0 && errno != EEXIST)
				LOGW("mkdir %s : %s", sub.c_str(), strerror(errno));
		}
	}
}

// Copie les données de ScummVM (thèmes, kyra.dat) de l'APK vers le stockage de l'application.
void extractAssetDir(AAssetManager *mgr, const char *assetDir, const std::string &destDir) {
	makeDirs(destDir);
	AAssetDir *dir = AAssetManager_openDir(mgr, assetDir);
	if (!dir) {
		LOGW("dossier d'assets absent : %s", assetDir);
		return;
	}
	const char *name;
	while ((name = AAssetDir_getNextFileName(dir)) != nullptr) {
		std::string src = std::string(assetDir) + "/" + name;
		std::string dst = destDir + "/" + name;
		AAsset *asset = AAssetManager_open(mgr, src.c_str(), AASSET_MODE_STREAMING);
		if (!asset)
			continue;
		off64_t len = AAsset_getLength64(asset);
		struct stat st;
		if (stat(dst.c_str(), &st) == 0 && (off64_t)st.st_size == len) {
			AAsset_close(asset);
			continue; // déjà extrait
		}
		FILE *fp = fopen(dst.c_str(), "wb");
		if (fp) {
			char buf[65536];
			int n;
			while ((n = AAsset_read(asset, buf, sizeof(buf))) > 0)
				fwrite(buf, 1, (size_t)n, fp);
			fclose(fp);
			LOGI("extrait : %s", dst.c_str());
		} else {
			LOGE("écriture impossible : %s (%s)", dst.c_str(), strerror(errno));
		}
		AAsset_close(asset);
	}
	AAssetDir_close(dir);
}

// Cherche GENERAL.PAK (présent dans toutes les versions du jeu) dans `dir` et ses sous-dossiers.
bool findGameDir(const std::string &dir, int depth, std::string &found) {
	DIR *d = opendir(dir.c_str());
	if (!d)
		return false;
	std::vector<std::string> subdirs;
	bool ok = false;
	struct dirent *e;
	while ((e = readdir(d)) != nullptr) {
		if (e->d_name[0] == '.')
			continue;
		if (!strcasecmp(e->d_name, "GENERAL.PAK")) {
			ok = true;
			break;
		}
		std::string full = dir + "/" + e->d_name;
		struct stat st;
		if (depth > 0 && stat(full.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
			subdirs.push_back(full);
	}
	closedir(d);
	if (ok) {
		found = dir;
		return true;
	}
	for (const std::string &s : subdirs)
		if (findGameDir(s, depth - 1, found))
			return true;
	return false;
}

void coreLog(int level, const char *msg) {
	int prio = level >= 3 ? ANDROID_LOG_ERROR : level == 2 ? ANDROID_LOG_WARN : ANDROID_LOG_INFO;
	__android_log_print(prio, "lolvr-core", "%s", msg);
}

// ---------------------------------------------------------------------------
// Audio

aaudio_data_callback_result_t audioCallback(AAudioStream *, void *, void *audioData, int32_t numFrames) {
	CoreHost::get().readAudio((int16_t *)audioData, (size_t)numFrames);
	// Ancre le son du jeu à l'écran et ajoute l'ambiance de la pièce.
	Ambience::get().process((int16_t *)audioData, (size_t)numFrames);
	return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

AAudioStream *startAudio(int sampleRate) {
	Ambience::get().setSampleRate(sampleRate);
	AAudioStreamBuilder *builder = nullptr;
	if (AAudio_createStreamBuilder(&builder) != AAUDIO_OK || !builder)
		return nullptr;
	AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
	AAudioStreamBuilder_setSampleRate(builder, sampleRate);
	AAudioStreamBuilder_setChannelCount(builder, 2);
	AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
	AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
	AAudioStreamBuilder_setDataCallback(builder, audioCallback, nullptr);
	AAudioStream *stream = nullptr;
	aaudio_result_t res = AAudioStreamBuilder_openStream(builder, &stream);
	AAudioStreamBuilder_delete(builder);
	if (res != AAUDIO_OK || !stream) {
		LOGE("ouverture du son impossible : %d", (int)res);
		return nullptr;
	}
	AAudioStream_requestStart(stream);
	return stream;
}

void stopAudio(AAudioStream *&stream) {
	if (!stream)
		return;
	AAudioStream_requestStop(stream);
	AAudioStream_close(stream);
	stream = nullptr;
}

// ---------------------------------------------------------------------------
// OpenGL ES

GLuint compileShader(GLenum type, const char *src) {
	GLuint s = glCreateShader(type);
	glShaderSource(s, 1, &src, nullptr);
	glCompileShader(s);
	GLint ok = 0;
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[1024] = {0};
		glGetShaderInfoLog(s, sizeof(log) - 1, nullptr, log);
		LOGE("erreur de shader : %s", log);
	}
	return s;
}

GLuint linkProgram(const char *vs, const char *fs) {
	GLuint p = glCreateProgram();
	GLuint v = compileShader(GL_VERTEX_SHADER, vs);
	GLuint f = compileShader(GL_FRAGMENT_SHADER, fs);
	glAttachShader(p, v);
	glAttachShader(p, f);
	glLinkProgram(p);
	GLint ok = 0;
	glGetProgramiv(p, GL_LINK_STATUS, &ok);
	if (!ok) {
		char log[1024] = {0};
		glGetProgramInfoLog(p, sizeof(log) - 1, nullptr, log);
		LOGE("erreur de liaison : %s", log);
	}
	glDeleteShader(v);
	glDeleteShader(f);
	return p;
}

// Copie l'image du jeu vers le calque quad, avec un agrandissement net (« sharp bilinear »).
const char *kBlitVs = R"(#version 300 es
out vec2 vUv;
void main() {
	vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
	vUv = vec2(p.x, 1.0 - p.y);
	gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

const char *kBlitFs = R"(#version 300 es
precision highp float;
uniform sampler2D uTex;
uniform vec2 uTexSize;
uniform vec2 uOutSize;
uniform vec4 uRect; // portion copiée, en pixels de l'image du jeu : x, y, largeur, hauteur
in vec2 vUv;
out vec4 oColor;
void main() {
	vec2 texel = uRect.xy + vUv * uRect.zw;
	vec2 i = floor(texel);
	vec2 f = texel - i;
	vec2 scale = max(uOutSize / uRect.zw, vec2(1.0));
	vec2 region = 0.5 - 0.5 / scale;
	vec2 c = f - 0.5;
	vec2 fpp = (c - clamp(c, -region, region)) * scale + 0.5;
	vec2 p = clamp(i + fpp, uRect.xy + 0.5, uRect.xy + uRect.zw - 0.5);
	oColor = vec4(texture(uTex, p / uTexSize).rgb, 1.0);
}
)";

struct Egl {
	EGLDisplay display = EGL_NO_DISPLAY;
	EGLConfig config = nullptr;
	EGLContext context = EGL_NO_CONTEXT;
	EGLSurface surface = EGL_NO_SURFACE;
};

bool initEgl(Egl &egl) {
	egl.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if (egl.display == EGL_NO_DISPLAY || !eglInitialize(egl.display, nullptr, nullptr)) {
		LOGE("eglInitialize a échoué");
		return false;
	}
	const EGLint configAttribs[] = {
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
		EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
		EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
		EGL_DEPTH_SIZE, 0, EGL_STENCIL_SIZE, 0, EGL_SAMPLES, 0,
		EGL_NONE};
	EGLint count = 0;
	if (!eglChooseConfig(egl.display, configAttribs, &egl.config, 1, &count) || count < 1) {
		LOGE("eglChooseConfig a échoué");
		return false;
	}
	const EGLint contextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
	egl.context = eglCreateContext(egl.display, egl.config, EGL_NO_CONTEXT, contextAttribs);
	if (egl.context == EGL_NO_CONTEXT) {
		LOGE("eglCreateContext a échoué : 0x%x", eglGetError());
		return false;
	}
	const EGLint surfaceAttribs[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
	egl.surface = eglCreatePbufferSurface(egl.display, egl.config, surfaceAttribs);
	if (egl.surface == EGL_NO_SURFACE) {
		LOGE("eglCreatePbufferSurface a échoué : 0x%x", eglGetError());
		return false;
	}
	if (!eglMakeCurrent(egl.display, egl.surface, egl.surface, egl.context)) {
		LOGE("eglMakeCurrent a échoué : 0x%x", eglGetError());
		return false;
	}
	return true;
}

// ---------------------------------------------------------------------------
// OpenXR

#define XR_CHECK(call)                                                        \
	do {                                                                      \
		XrResult xrCheckRes = (call);                                         \
		if (XR_FAILED(xrCheckRes)) {                                          \
			LOGE("OpenXR : %s a échoué (%d)", #call, (int)xrCheckRes);        \
			return false;                                                     \
		}                                                                     \
	} while (0)

struct Swapchain {
	XrSwapchain handle = XR_NULL_HANDLE;
	int32_t width = 0;
	int32_t height = 0;
	std::vector<XrSwapchainImageOpenGLESKHR> images;
};

enum Hand { kLeft = 0, kRight = 1 };

// Images du joueur : fenêtre, portrait, quatre tableaux.
const int kImageSlots = 6;

struct Button {
	bool down = false;
};

// Image du jeu dans son calque : 1600x1200 au centre, plus une marge noire.
const int kQuadImageW = 1600, kQuadImageH = 1200, kQuadMarginX = 16, kQuadMarginY = 12;

// Extension GL_EXT_multisampled_render_to_texture (anticrénelage sans copie, standard sur Quest).
typedef void(GL_APIENTRY *RenderbufferStorageMultisampleFn)(GLenum, GLsizei, GLenum, GLsizei, GLsizei);
typedef void(GL_APIENTRY *FramebufferTexture2DMultisampleFn)(GLenum, GLenum, GLenum, GLuint, GLint, GLsizei);

const int kEnvCount = lolvr::kEnvCount;

struct App {
	android_app *androidApp = nullptr;
	bool resumed = false;

	Egl egl;

	XrInstance instance = XR_NULL_HANDLE;
	XrSystemId systemId = XR_NULL_SYSTEM_ID;
	XrSession session = XR_NULL_HANDLE;
	XrSpace localSpace = XR_NULL_HANDLE;
	XrSpace viewSpace = XR_NULL_HANDLE;
	XrSpace stageSpace = XR_NULL_HANDLE; // facultatif : donne la hauteur du sol
	XrSessionState sessionState = XR_SESSION_STATE_UNKNOWN;
	bool sessionRunning = false;
	bool exitRequested = false;

	bool srgb = false;
	std::vector<XrViewConfigurationView> viewConfigs;
	Swapchain eyeSwapchains[2];
	Swapchain quadSwapchain;

	// Réalité mixte (XR_FB_passthrough)
	bool passthroughSupported = false;
	bool passthroughActive = false;
	XrPassthroughFB passthrough = XR_NULL_HANDLE;
	XrPassthroughLayerFB passthroughLayer = XR_NULL_HANDLE;
	PFN_xrCreatePassthroughFB xrCreatePassthroughFB = nullptr;
	PFN_xrPassthroughStartFB xrPassthroughStartFB = nullptr;
	PFN_xrPassthroughPauseFB xrPassthroughPauseFB = nullptr;
	PFN_xrCreatePassthroughLayerFB xrCreatePassthroughLayerFB = nullptr;
	PFN_xrPassthroughLayerResumeFB xrPassthroughLayerResumeFB = nullptr;
	PFN_xrPassthroughLayerPauseFB xrPassthroughLayerPauseFB = nullptr;

	// Actions
	XrActionSet actionSet = XR_NULL_HANDLE;
	XrPath handPaths[2] = {XR_NULL_PATH, XR_NULL_PATH};
	XrAction aimAction = XR_NULL_HANDLE;
	XrAction triggerAction = XR_NULL_HANDLE;
	XrAction squeezeAction = XR_NULL_HANDLE;
	XrAction stickAction = XR_NULL_HANDLE;
	XrAction stickClickAction = XR_NULL_HANDLE;
	XrAction aAction = XR_NULL_HANDLE, bAction = XR_NULL_HANDLE;
	XrAction xAction = XR_NULL_HANDLE, yAction = XR_NULL_HANDLE;
	XrAction menuAction = XR_NULL_HANDLE;
	XrAction hapticAction = XR_NULL_HANDLE;
	XrSpace aimSpaces[2] = {XR_NULL_HANDLE, XR_NULL_HANDLE};

	// OpenGL
	GLuint fbo = 0;
	GLuint eyeFbo[2] = {0, 0};
	GLuint eyeDepth[2] = {0, 0};
	int msaa = 1; // échantillons par pixel des images des yeux
	bool eyeTargetsChecked = false;
	RenderbufferStorageMultisampleFn renderbufferStorageMultisample = nullptr;
	FramebufferTexture2DMultisampleFn framebufferTexture2DMultisample = nullptr;
	GLuint blitProgram = 0;
	GLint blitTexSize = -1, blitOutSize = -1, blitRect = -1;
	GLuint srcTex = 0;
	int srcTexW = 0, srcTexH = 0;

	// Décor
	Scene scene;
	// Le donjon en 3D (lol-vr-3d)
	lolvr::Dungeon dungeon;
	bool dungeonOk = false;
	char tileset[16] = {0};         // région du niveau (« KEEP », « FOREST1 »...)
	int dungeonMode = 1;            // 0 vue classique, 1 mode immersif (l'écran du jeu seulement quand il le faut)
	int classicEnv = lolvr::kEnvLibrary; // décor de la vue classique (le mode immersif n'en a qu'un)
	bool viewChosen = false;        // le joueur a choisi sa vue au premier lancement
	bool chooserOpen = false;       // la fenêtre du choix de la vue est ouverte
	bool chooserPlaced = false;
	bool screenShown = true;        // le grand écran est affiché
	double screenNeededAt = 0;
	double charSceneSince = -1.0; // (une scène d'un personnage devant le donjon : depuis quand)
	// Mode cinéma (mode immersif) : pendant une scène qui montre sa propre image (boutique, lieu,
	// personnage, cinématique), le monde s'efface au noir et l'image du jeu passe sur un écran courbe,
	// sans cadre, avec un halo de ses couleurs. Taille : 0 moyen, 1 grand, 2 petit, 3 sans (l'ancien
	// écran plat). Avant la partie (intro, menus) aussi, si cinemaIntro.
	int cinemaSize = 0;
	bool cinemaIntro = false;
	bool cinemaWanted = false;
	double cinemaSeenAt = -10.0, cinemaTime = 0.0;
	float cinema = 0.0f;   // 0 éteint, 1 plein (le fondu)
	float cinemaW = 0.0f;  // la largeur d'écran qui donne l'angle voulu
	float cinemaScreen = 0.0f; // l'écran courbe lui-même (0..1) : éteint quand rien n'est à montrer
	// L'écran courbe est posé devant la tête (au début de la scène ou au recentrage), à cinemaR, la
	// ligne des yeux des personnages (38 % du haut de l'image) à hauteur des yeux.
	bool cinemaArmed = true, prevGameStarted = false;
	Vec3 cinemaHead = {0, 1.6f, 0}, cinemaCenter = {0, 1.6f, -1.5f};
	float cinemaYaw = 0.0f, cinemaR = 1.5f;
	// Réglés à la main (poignée, coins, joystick) et gardés : la taille (facteur), la hauteur (en plus
	// de la ligne des yeux). La distance est cinemaR.
	float cinemaZoom = 1.0f, cinemaLift = 0.0f;
	// L'image de l'écran courbe, sa place et sa taille : figées dès que la scène se termine, pour
	// que l'écran s'éteigne sur elle (et non sur la vue du donjon qui revient derrière).
	bool cinemaLive = false;
	GLuint cinemaTex = 0;
	int cinemaTexW = 0, cinemaTexH = 0;
	Vec3 cineShowPos = {0, 0, 0};
	Quat cineShowRot = {0, 0, 0, 1};
	float cineShowHalfA = 0.0f, cineShowHalfH = 0.0f, cineShowRect[4] = {0, 0, 1, 1};
	float cineShowBandFrac = 0.0f, cineShowBand[4] = {0, 0, 1, 1};
	Vec3 cinemaEdge[4] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
	float dunYaw = 0;               // lacet (monde) vers lequel le groupe regarde
	bool dunYawSet = false;
	int shownDir = -1;              // direction du groupe déjà prise en compte
	float turnFade = 0, turnTarget = 0; // virage : bref fondu au noir, la pièce pivote au plus sombre
	int alignTurn = 0, alignExpect = -1; // le groupe se tourne vers le regard du joueur
	double alignUntil = 0, moveBlockUntil = 0;
	int moveAbs = -1;        // direction du donjon (0 nord .. 3 ouest) où le joueur marche
	double gazeOffSince = 0; // depuis quand le regard s'écarte de la direction du groupe
	float wallDim = 0;              // la tête du joueur entre dans un mur : on assombrit
	float dungeonLight = 1.0f;
	float dungeonReach = 11.0f; // jusqu'où porte la lumière (lanterne), adouci
	bool dungeonVisible = false;
	lolvr::LevelSnapshot level;
	GLuint cursorTex = 0;       // le curseur du jeu (l'objet tenu), pour le donjon en 3D
	uint32_t cursorTexSerial = 0;
	lolvr::DungeonTextures dunTex;
	uint32_t builtGfx = 0, builtWalls = 0, builtCapture = 0, builtSprites = 0, builtPlaces = 0;
	uint32_t spriteVersion[32] = {0};
	// Objets en vol (lancés, projectiles) : le jeu les avance par petits sauts ; on glisse de l'un à
	// l'autre. spriteList garde les images posées, flightOf[i] le vol de l'image i (-1 : aucun).
	struct Flight {
		int id = -1;
		float fx = 0, fz = 0, tx = 0, tz = 0; // de, vers
		float t0 = 0, interval = 0.1f, last = 0;
	};
	Flight flights[8];
	std::vector<lolvr::DungeonSprite> spriteList;
	int flightOf[32];
	float clock = 0.0f;
	uint32_t seenFlights = 0;
	int spriteId[32] = {0};
	int shownLevel = 0;
	uint32_t gfxWaitSerial = 0;     // décor attendu (ses murs dessinés par le jeu pas encore là)
	int builtLevelNo = -1;          // le niveau et la région des textures en place
	char builtTileset[16] = {0};
	uint64_t builtTexHash = 0;
	double gfxWaitSince = 0;
	float levelFadeIn = 1.0f;       // fondu d'arrivée d'un nouveau décor (0 noir, 1 fini)
	float gfxWaitAt = -10.0f;       // la dernière image où le nouveau décor était attendu
	float partyX = 0, partyZ = 0, partyYaw = 0; // position affichée du groupe (glisse vers la vraie)
	SceneState ss;
	bool sceneOk = false;
	int environment = lolvr::kEnvLibrary;
	bool environmentRestored = false;
	int shownTheme = 0, wantedTheme = 0;
	double wantedThemeSince = 0;
	double startTime = 0;
	Vec3 screenGlow = {0.05f, 0.05f, 0.05f};

	// Écran virtuel. La taille et la distance sont mémorisées pour chaque décor.
	bool screenPlaced = false;
	Vec3 screenPos = {0, 0, -2.2f};
	float screenYaw = 0.0f;
	float widths[kEnvCount] = {1.1f, 1.8f, 0.62f, 0.62f}; // mètres : largeur de l'image entière (hauteur = 3/4)
	float menuWidths[kEnvCount] = {0, 0, 0, 0};             // la même chose pour un menu du jeu montré seul (0 : comme l'écran)
	float distances[kEnvCount] = {1.15f, 2.0f, 0.95f, 0.95f};
	// Ce que l'écran affiche en ce moment : portion de l'image du jeu (pixels) et taille réelle (mètres).
	float cropX = 0, cropY = 0, cropW = 320, cropH = 200;
	float dispW = 2.4f, dispH = 1.8f;
	float bandH = 0;   // hauteur (pixels du jeu, à la largeur de la vue) de la ligne de messages collée sous la scène
	float bandRect[4] = {83, 122, 234, 20}; // ce qu'elle montre de l'image du jeu (x, y, largeur, hauteur)
	bool bandShown = false;              // la bande (messages, conversation) : fenêtre sous le cadre de l'écran
	int quadRectW = 0, quadRectH = 0; // zone du calque quad réellement utilisée
	float quadScaleX = 1.0f, quadScaleY = 1.0f; // marge noire comprise

	// Interface détachée : l'écran ne garde que la vue du donjon, le reste part sur des panneaux.
	lolvr::Ui ui;
	bool detached = true;        // choix du joueur
	int hudMode = 0;             // mode immersif : 0 tout regroupé, 1 la barre du bas du jeu, 2 des fenêtres séparées
	bool split = false;          // en vigueur en ce moment
	bool splitWanted = false;
	double splitWantedSince = 0;
	bool gameStarted = false, automapOpen = false;
	bool cutscene = false;       // une scène a pris la main (mode de contrôle du jeu non nul)
	bool gameMenuOpen = false;   // un menu du jeu est affiché
	bool menuAlone = false;
	bool atlasLive = false;      // l'atlas du jeu, ouvert par une flèche de la fenêtre d'atlas, y est montré
	int atlasBrowse = 0;         // flèche de la fenêtre d'atlas : l'atlas du jeu s'ouvre, puis tourne d'une page
	double atlasBrowseSince = 0.0;      // mode immersif : ce menu seul à l'écran, sans cadre
	int langRequested = -1;      // langue choisie dans le casque, en attente que le jeu la prenne
	double langRequestAt = 0;
	int viewNotices = 0;         // rappels « L3 change la vue » déjà montrés (1 : classique, 2 : réalité mixte)
	int vrMenuSerial = -1;       // « Réglages VR » dans le menu du jeu (-1 : pas encore lu)
	// Combat aux boutons : dans le donjon, A attaque et B ouvre la magie du personnage choisi.
	bool inDungeon = false;
	int characters = 0, selectedChar = 0;
	bool attackDown = false, magicDown = false, charFlick = false;
	// Le menu des niveaux de sort : le niveau en surbrillance (0 = niveau 1), R3 le change, B le lance.
	int spellLevel = 0, spellMenuChar = -1;
	bool r3Down = false;
	double synthClickUntil = 0;   // clic envoyé au jeu à la place du pointeur (bouton de magie)
	// Un clic sur un mur de côté (buisson, nid, trou d'arbre) : le jeu ne clique que sur le mur
	// d'en face. Le groupe se tourne d'abord vers ce mur, puis le clic part.
	int pickFace = -1;            // le mur visé : 0 nord .. 3 ouest (-1 : sol, au loin)
	int pickItem = -1;            // l'objet posé visé (sa case ou celle devant) : son numéro dans le jeu
	int pendingFace = -1;
	int pendingFrom = -1;          // la direction du groupe avant ce demi-tour vers un mur de côté
	int returnFace = -1;
	double quickSaveAsked = -1;    // une sauvegarde rapide demandée (boutique) : sa confirmation
	int quickSaveSeen = -1;
	int quickSavePulses = 0;
	double quickSavePulseAt = 0;           // après le clic : s'y retourner (le groupe regarde de nouveau devant le joueur)
	double returnAt = 0;
	float pendingU = 0, pendingV = 0;
	double pendingUntil = 0;
	bool holdTrigger = false;     // la gâchette du clic reporté est encore enfoncée
	float synthU = 0, synthV = 0;
	bool keyboardOpen = false, keyboardButtonDown = false;
	// Le clavier ouvert tout seul pour écrire le nom d'une sauvegarde : il se referme à la fin.
	bool nameEntry = false, keyboardAuto = false;
	bool gearKnown = false;      // le joueur a déjà ouvert le menu du poignet (plus d'invitation)
	double gearNudgeAt = 0;      // prochaine petite vibration de la main gauche pour l'invitation
	bool fireOn = false;
	bool layoutLoaded = false;
	// Atlas : copie de la dernière carte consultée
	float viewLift = 0.0f; // la vue du donjon seule est remontée d'autant (mètres)
	float viewShift = 0.0f; // la fiche d'un personnage, plus large, déborde d'autant vers la droite
	bool charSheet = false; // la fiche d'un personnage est ouverte
	bool helpOpen = false;  // panneau d'aide (ouvert depuis le menu du poignet)
	int language = lolvr::kLangEnglish; // langue des textes de l'application
	int moveDir = 0;       // direction tenue au joystick gauche
	uint32_t frameSerial = 0; // numéro de la dernière image du jeu reçue
	uint32_t spriteWalls = 0; // murs pour lesquels les objets ont été placés
	uint32_t builtSpell = 0;  // image des effets du sort déjà posée
	bool effectShown = false;
	int builtDir = -1;        // direction du groupe pour laquelle le donjon a été construit (dessins au sol)
	bool aimOnUi = false;     // le doigt vise une fenêtre ou l'écran
	int visBlock = -1;        // case où se tient la vue 3D (elle ne suit pas le groupe dans une porte de boutique)
	bool holdScene = false;   // une scène s'est jouée pendant que le groupe était dans un bloc d'entrée
	bool prevHold = false;    // le groupe était dans un bloc d'entrée à l'image d'avant
	bool holdOver = false;    // le groupe a quitté un bloc d'entrée : la vue reste devant l'image du lieu pendant sa scène
	bool holdOverScene = false;
	double holdOverUntil = 0.0;
	int holdOverBlock = -1;
	double holdOverIdle = 0.0; // depuis quand aucune scène ne tourne
	bool atHold = false;       // le groupe est sur le bloc d'entrée d'un lieu (sa porte)
	// Un mur vivant : l'image d'un lieu sur le côté d'une case de passage, que le jeu dessine (et fait
	// changer : la porte de la pompe qui s'ouvre, l'engrenage posé) sur sa vue quand on la regarde de
	// la case voisine. On y montre alors la vue du jeu elle-même. Repère local du niveau.
	bool liveWall = false;
	Vec3 liveWallC = {0, 0, 0}, liveWallR = {1, 0, 0};
	int openBlock = -1;       // bloc d'entrée où l'on se tient (montré ouvert), -1 sinon
	int builtOpen = -1;
	float lightLevel = 1.0f;  // clarté du jeu (pénombre, lampe), adoucie
	double imagesCheckedAt = -100.0;
	bool imageLoaded[kImageSlots] = {false, false, false, false, false, false};
	std::string imageFile[kImageSlots];                    // fichier affiché dans chaque emplacement
	std::map<std::string, long> imageRejected;   // fichiers illisibles (chemin -> taille au moment de l'essai)
	std::string imageInfo[2];                    // état lisible, affiché sous le clavier
	// Direction de l'écran choisie par le joueur, par décor (lacet par rapport au regard de départ, tangage)
	float aimYaw[kEnvCount] = {0, 0, 0, 0}, aimPitch[kEnvCount] = {0, 0, 0, 0};
	bool aimSaved[kEnvCount] = {false, false, false, false};
	double automapSince = 0;
	// Touches du clavier virtuel à relâcher
	struct PendingKey {
		unsigned key, mods;
		double releaseAt;
	};
	std::vector<PendingKey> pendingKeys;
	float anchorYaw = 0.0f; // orientation de la pièce et des panneaux (regard au dernier recentrage)
	Vec3 anchorPos = {0, 0, 0}; // tête du joueur au moment du placement
	Vec3 anchorDir = {0, 0, -1}; // direction tête -> écran
	Vec3 roomPos = {0, -1.25f, 0};
	float floorY = -1.25f;
	bool headValid = false;
	Vec3 headPos = {0, 0, 0};
	Quat headRot = {0, 0, 0, 1};

	// Pointeur
	int activeHand = kRight;
	bool aimValid[2] = {false, false};
	XrPosef aimPose[2] = {{{0, 0, 0, 1}, {0, 0, 0}}, {{0, 0, 0, 1}, {0, 0, 0}}};
	bool hitValid = false;
	Vec3 hitPoint = {0, 0, 0};
	Vec3 hitRight = {0, 0, 0}, hitUp = {0, 0, 0};
	float hitRadius = 0.0f;
	bool pointerLong = false;
	bool triggerDown[2] = {false, false};
	bool squeezeDown[2] = {false, false};
	float triggerValue[2] = {0, 0}, squeezeValue[2] = {0, 0};
	float clickPulse = 0.0f;

	// Touches
	Button kUp, kDown, kSlideL, kSlideR, kTurnL, kTurnR, kEsc, kMap, kEnter;
	bool recenterDown = false;
	bool zoomDown = false, zoomUsed = false;
	double zoomSince = 0;
	bool settingsDirty = false;
	// L'atlas : sa fenêtre (réglage « Atlas » du menu VR), et la carte montrée (refaite quand la carte,
	// les murs ou la place du groupe changent).
	bool atlasOn = false; // la fenêtre de l'atlas est ouverte (l'icône de l'atlas, un bouton, ou « Fermer »)
	bool fnWasDown[lolvr::kFnCount] = {false}; // les fonctions des boutons à l'image précédente
	lolvr::UiMap atlasMap;
	uint32_t atlasMapSerial = 0, atlasWallsSerial = 0;
	int atlasBlock = -1, atlasDir = -1;
	int spellsSeen = -1, spellsLevel = 0; // sorts du parchemin vus au dernier coup d'œil, et dans quel niveau

	// Jeu
	std::string systemDir, saveDir, gameRoot, gameDir, settingsPath, decorDir;
	bool imagesLoaded = false;
	bool coreStarted = false;
	double nextScan = 0;
	Frame frame;
	bool frameDirty = false;
	AAudioStream *audio = nullptr;
	int audioRate = 48000;

	// Événements du jeu déjà traités (vibrations)
	bool eventsPrimed = false;
	uint32_t sfxSerial = 0, hurtSerial = 0, hitSerial = 0;
};

float &screenWidth(App &app) { return app.widths[app.environment]; }
// Un menu du jeu montré seul (options, sauvegarde, défaite) a sa propre taille : agrandir l'écran
// pour une cinématique ne le grossit pas.
float &menuWidth(App &app) {
	// (au départ, la taille de l'écran par défaut, pas celle d'un écran agrandi pour une cinématique)
	static const float kDefault[kEnvCount] = {1.1f, 1.8f, 0.62f, 0.62f};
	if (app.menuWidths[app.environment] <= 0.0f)
		app.menuWidths[app.environment] = kDefault[app.environment];
	return app.menuWidths[app.environment];
}
float &shownWidth(App &app) { return app.menuAlone && app.split ? menuWidth(app) : screenWidth(app); }
float &screenDistance(App &app) { return app.distances[app.environment]; }
// Mode cinéma : l'écran courbe a sa propre place (voir updateLayout). Par défaut, le centre de
// l'écran est sous les yeux de cette fraction de sa hauteur (la ligne des yeux des personnages,
// vers le quart du haut de l'image, à hauteur des yeux).
const float kCinemaEyeLine = 0.24f;

// Le centre de l'écran courbe par rapport aux yeux (en hauteur) : la ligne des yeux de l'image à
// hauteur des yeux, la zone de texte éventuelle dessous.
float cinemaEyeOffset(const App &app) {
	const float pic = app.cropH + app.bandH > 0.0f ? app.dispH * app.cropH / (app.cropH + app.bandH) : app.dispH;
	return (0.5f - kCinemaEyeLine) * pic - 0.5f * app.dispH;
}
bool cinemaPlaced(const App &app) { return app.cinemaWanted || app.cinema > 0.0f; }

Quat screenRot(const App &app) { return quatYaw(cinemaPlaced(app) ? app.cinemaYaw : app.screenYaw); }

bool createSwapchain(App &app, int32_t width, int32_t height, Swapchain &out) {
	XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
	info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
	info.format = app.srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8;
	info.sampleCount = 1;
	info.width = (uint32_t)width;
	info.height = (uint32_t)height;
	info.faceCount = 1;
	info.arraySize = 1;
	info.mipCount = 1;
	XR_CHECK(xrCreateSwapchain(app.session, &info, &out.handle));
	out.width = width;
	out.height = height;
	uint32_t count = 0;
	XR_CHECK(xrEnumerateSwapchainImages(out.handle, 0, &count, nullptr));
	out.images.assign(count, XrSwapchainImageOpenGLESKHR{XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR});
	XR_CHECK(xrEnumerateSwapchainImages(out.handle, count, &count, (XrSwapchainImageBaseHeader *)out.images.data()));
	return true;
}

bool createAction(App &app, XrAction &action, const char *name, const char *label, XrActionType type, bool perHand) {
	XrActionCreateInfo info{XR_TYPE_ACTION_CREATE_INFO};
	strncpy(info.actionName, name, XR_MAX_ACTION_NAME_SIZE - 1);
	strncpy(info.localizedActionName, label, XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
	info.actionType = type;
	if (perHand) {
		info.countSubactionPaths = 2;
		info.subactionPaths = app.handPaths;
	}
	XR_CHECK(xrCreateAction(app.actionSet, &info, &action));
	return true;
}

bool initActions(App &app) {
	XR_CHECK(xrStringToPath(app.instance, "/user/hand/left", &app.handPaths[kLeft]));
	XR_CHECK(xrStringToPath(app.instance, "/user/hand/right", &app.handPaths[kRight]));

	XrActionSetCreateInfo setInfo{XR_TYPE_ACTION_SET_CREATE_INFO};
	strcpy(setInfo.actionSetName, "jeu");
	strcpy(setInfo.localizedActionSetName, "Jeu");
	XR_CHECK(xrCreateActionSet(app.instance, &setInfo, &app.actionSet));

	if (!createAction(app, app.aimAction, "viser", "Viser", XR_ACTION_TYPE_POSE_INPUT, true) ||
	    !createAction(app, app.triggerAction, "clic_gauche", "Clic gauche", XR_ACTION_TYPE_FLOAT_INPUT, true) ||
	    !createAction(app, app.squeezeAction, "clic_droit", "Clic droit", XR_ACTION_TYPE_FLOAT_INPUT, true) ||
	    !createAction(app, app.stickAction, "joystick", "Joystick", XR_ACTION_TYPE_VECTOR2F_INPUT, true) ||
	    !createAction(app, app.stickClickAction, "clic_joystick", "Clic du joystick", XR_ACTION_TYPE_BOOLEAN_INPUT, true) ||
	    !createAction(app, app.aAction, "bouton_a", "Bouton A", XR_ACTION_TYPE_BOOLEAN_INPUT, false) ||
	    !createAction(app, app.bAction, "bouton_b", "Bouton B", XR_ACTION_TYPE_BOOLEAN_INPUT, false) ||
	    !createAction(app, app.xAction, "bouton_x", "Bouton X", XR_ACTION_TYPE_BOOLEAN_INPUT, false) ||
	    !createAction(app, app.yAction, "bouton_y", "Bouton Y", XR_ACTION_TYPE_BOOLEAN_INPUT, false) ||
	    !createAction(app, app.menuAction, "bouton_menu", "Bouton menu", XR_ACTION_TYPE_BOOLEAN_INPUT, false) ||
	    !createAction(app, app.hapticAction, "vibration", "Vibration", XR_ACTION_TYPE_VIBRATION_OUTPUT, true))
		return false;

	struct Binding {
		XrAction action;
		const char *path;
	};
	const Binding table[] = {
		{app.aimAction, "/user/hand/left/input/aim/pose"},
		{app.aimAction, "/user/hand/right/input/aim/pose"},
		{app.triggerAction, "/user/hand/left/input/trigger/value"},
		{app.triggerAction, "/user/hand/right/input/trigger/value"},
		{app.squeezeAction, "/user/hand/left/input/squeeze/value"},
		{app.squeezeAction, "/user/hand/right/input/squeeze/value"},
		{app.stickAction, "/user/hand/left/input/thumbstick"},
		{app.stickAction, "/user/hand/right/input/thumbstick"},
		{app.stickClickAction, "/user/hand/left/input/thumbstick/click"},
		{app.stickClickAction, "/user/hand/right/input/thumbstick/click"},
		{app.aAction, "/user/hand/right/input/a/click"},
		{app.bAction, "/user/hand/right/input/b/click"},
		{app.xAction, "/user/hand/left/input/x/click"},
		{app.yAction, "/user/hand/left/input/y/click"},
		{app.menuAction, "/user/hand/left/input/menu/click"},
		{app.hapticAction, "/user/hand/left/output/haptic"},
		{app.hapticAction, "/user/hand/right/output/haptic"},
	};
	std::vector<XrActionSuggestedBinding> bindings;
	for (const Binding &b : table) {
		XrPath path = XR_NULL_PATH;
		XR_CHECK(xrStringToPath(app.instance, b.path, &path));
		bindings.push_back({b.action, path});
	}
	XrPath profile = XR_NULL_PATH;
	XR_CHECK(xrStringToPath(app.instance, "/interaction_profiles/oculus/touch_controller", &profile));
	XrInteractionProfileSuggestedBinding suggested{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
	suggested.interactionProfile = profile;
	suggested.suggestedBindings = bindings.data();
	suggested.countSuggestedBindings = (uint32_t)bindings.size();
	XR_CHECK(xrSuggestInteractionProfileBindings(app.instance, &suggested));

	for (int h = 0; h < 2; h++) {
		XrActionSpaceCreateInfo spaceInfo{XR_TYPE_ACTION_SPACE_CREATE_INFO};
		spaceInfo.action = app.aimAction;
		spaceInfo.subactionPath = app.handPaths[h];
		spaceInfo.poseInActionSpace.orientation.w = 1.0f;
		XR_CHECK(xrCreateActionSpace(app.session, &spaceInfo, &app.aimSpaces[h]));
	}

	XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
	attach.countActionSets = 1;
	attach.actionSets = &app.actionSet;
	XR_CHECK(xrAttachSessionActionSets(app.session, &attach));
	return true;
}

// Réalité mixte : facultative. En cas d'échec, l'application garde simplement la bibliothèque.
void initPassthrough(App &app) {
	struct Fn {
		const char *name;
		PFN_xrVoidFunction *ptr;
	};
	const Fn fns[] = {
		{"xrCreatePassthroughFB", (PFN_xrVoidFunction *)&app.xrCreatePassthroughFB},
		{"xrPassthroughStartFB", (PFN_xrVoidFunction *)&app.xrPassthroughStartFB},
		{"xrPassthroughPauseFB", (PFN_xrVoidFunction *)&app.xrPassthroughPauseFB},
		{"xrCreatePassthroughLayerFB", (PFN_xrVoidFunction *)&app.xrCreatePassthroughLayerFB},
		{"xrPassthroughLayerResumeFB", (PFN_xrVoidFunction *)&app.xrPassthroughLayerResumeFB},
		{"xrPassthroughLayerPauseFB", (PFN_xrVoidFunction *)&app.xrPassthroughLayerPauseFB},
	};
	for (const Fn &f : fns) {
		if (XR_FAILED(xrGetInstanceProcAddr(app.instance, f.name, f.ptr)) || !*f.ptr) {
			LOGW("réalité mixte indisponible : %s manquant", f.name);
			return;
		}
	}
	XrPassthroughCreateInfoFB ptInfo{XR_TYPE_PASSTHROUGH_CREATE_INFO_FB};
	XrResult res = app.xrCreatePassthroughFB(app.session, &ptInfo, &app.passthrough);
	if (XR_FAILED(res)) {
		LOGW("réalité mixte indisponible : xrCreatePassthroughFB (%d)", (int)res);
		return;
	}
	XrPassthroughLayerCreateInfoFB layerInfo{XR_TYPE_PASSTHROUGH_LAYER_CREATE_INFO_FB};
	layerInfo.passthrough = app.passthrough;
	layerInfo.purpose = XR_PASSTHROUGH_LAYER_PURPOSE_RECONSTRUCTION_FB;
	res = app.xrCreatePassthroughLayerFB(app.session, &layerInfo, &app.passthroughLayer);
	if (XR_FAILED(res)) {
		LOGW("réalité mixte indisponible : xrCreatePassthroughLayerFB (%d)", (int)res);
		return;
	}
	app.passthroughSupported = true;
	LOGI("réalité mixte disponible");
}

// Allume ou éteint les caméras du casque selon le décor choisi.
void setPassthrough(App &app, bool on) {
	if (!app.passthroughSupported || on == app.passthroughActive)
		return;
	if (on) {
		XrResult res = app.xrPassthroughStartFB(app.passthrough);
		if (XR_SUCCEEDED(res))
			res = app.xrPassthroughLayerResumeFB(app.passthroughLayer);
		if (XR_FAILED(res)) {
			LOGW("démarrage de la réalité mixte impossible (%d)", (int)res);
			app.passthroughSupported = false;
			return;
		}
	} else {
		app.xrPassthroughLayerPauseFB(app.passthroughLayer);
		app.xrPassthroughPauseFB(app.passthrough);
	}
	app.passthroughActive = on;
}

bool initXr(App &app) {
	ANativeActivity *activity = app.androidApp->activity;

	// Le chargeur OpenXR d'Android doit être initialisé avant toute autre fonction.
	PFN_xrInitializeLoaderKHR initLoader = nullptr;
	XR_CHECK(xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR", (PFN_xrVoidFunction *)&initLoader));
	XrLoaderInitInfoAndroidKHR loaderInfo{XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
	loaderInfo.applicationVM = activity->vm;
	loaderInfo.applicationContext = activity->clazz;
	XR_CHECK(initLoader((const XrLoaderInitInfoBaseHeaderKHR *)&loaderInfo));

	// Extensions facultatives proposées par le casque
	bool hasPassthrough = false;
	{
		uint32_t count = 0;
		if (XR_SUCCEEDED(xrEnumerateInstanceExtensionProperties(nullptr, 0, &count, nullptr)) && count > 0) {
			std::vector<XrExtensionProperties> props(count, XrExtensionProperties{XR_TYPE_EXTENSION_PROPERTIES});
			if (XR_SUCCEEDED(xrEnumerateInstanceExtensionProperties(nullptr, count, &count, props.data()))) {
				for (const XrExtensionProperties &p : props)
					if (!strcmp(p.extensionName, XR_FB_PASSTHROUGH_EXTENSION_NAME))
						hasPassthrough = true;
			}
		}
	}

	std::vector<const char *> extensions = {
		XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,
		XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME,
	};
	if (hasPassthrough)
		extensions.push_back(XR_FB_PASSTHROUGH_EXTENSION_NAME);
	XrInstanceCreateInfoAndroidKHR androidInfo{XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
	androidInfo.applicationVM = activity->vm;
	androidInfo.applicationActivity = activity->clazz;
	XrInstanceCreateInfo instanceInfo{XR_TYPE_INSTANCE_CREATE_INFO};
	instanceInfo.next = &androidInfo;
	strcpy(instanceInfo.applicationInfo.applicationName, "Lands of Lore VR 3D");
	instanceInfo.applicationInfo.applicationVersion = 2;
	strcpy(instanceInfo.applicationInfo.engineName, "lol-vr-3d");
	instanceInfo.applicationInfo.engineVersion = 2;
	instanceInfo.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
	instanceInfo.enabledExtensionCount = (uint32_t)extensions.size();
	instanceInfo.enabledExtensionNames = extensions.data();
	XR_CHECK(xrCreateInstance(&instanceInfo, &app.instance));

	XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
	systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
	XR_CHECK(xrGetSystem(app.instance, &systemInfo, &app.systemId));

	// Appel obligatoire avant de créer la session.
	PFN_xrGetOpenGLESGraphicsRequirementsKHR getRequirements = nullptr;
	XR_CHECK(xrGetInstanceProcAddr(app.instance, "xrGetOpenGLESGraphicsRequirementsKHR", (PFN_xrVoidFunction *)&getRequirements));
	XrGraphicsRequirementsOpenGLESKHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR};
	XR_CHECK(getRequirements(app.instance, app.systemId, &requirements));

	XrGraphicsBindingOpenGLESAndroidKHR binding{XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR};
	binding.display = app.egl.display;
	binding.config = app.egl.config;
	binding.context = app.egl.context;
	XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO};
	sessionInfo.next = &binding;
	sessionInfo.systemId = app.systemId;
	XR_CHECK(xrCreateSession(app.instance, &sessionInfo, &app.session));

	XrReferenceSpaceCreateInfo spaceInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
	spaceInfo.poseInReferenceSpace.orientation.w = 1.0f;
	spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
	XR_CHECK(xrCreateReferenceSpace(app.session, &spaceInfo, &app.localSpace));
	spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
	XR_CHECK(xrCreateReferenceSpace(app.session, &spaceInfo, &app.viewSpace));
	// L'espace « stage » a son origine au sol : il sert à poser la bibliothèque à la bonne hauteur.
	{
		uint32_t count = 0;
		if (XR_SUCCEEDED(xrEnumerateReferenceSpaces(app.session, 0, &count, nullptr)) && count > 0) {
			std::vector<XrReferenceSpaceType> types(count);
			if (XR_SUCCEEDED(xrEnumerateReferenceSpaces(app.session, count, &count, types.data()))) {
				for (XrReferenceSpaceType t : types) {
					if (t != XR_REFERENCE_SPACE_TYPE_STAGE)
						continue;
					spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
					if (XR_FAILED(xrCreateReferenceSpace(app.session, &spaceInfo, &app.stageSpace)))
						app.stageSpace = XR_NULL_HANDLE;
				}
			}
		}
	}

	if (hasPassthrough)
		initPassthrough(app);

	// Format des images : sRGB si possible (couleurs fidèles et filtrage correct).
	uint32_t formatCount = 0;
	XR_CHECK(xrEnumerateSwapchainFormats(app.session, 0, &formatCount, nullptr));
	std::vector<int64_t> formats(formatCount);
	XR_CHECK(xrEnumerateSwapchainFormats(app.session, formatCount, &formatCount, formats.data()));
	app.srgb = false;
	for (int64_t f : formats)
		if (f == GL_SRGB8_ALPHA8)
			app.srgb = true;
	LOGI("format des images : %s", app.srgb ? "sRGB" : "RGBA8");

	uint32_t viewCount = 0;
	XR_CHECK(xrEnumerateViewConfigurationViews(app.instance, app.systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &viewCount, nullptr));
	app.viewConfigs.assign(viewCount, XrViewConfigurationView{XR_TYPE_VIEW_CONFIGURATION_VIEW});
	XR_CHECK(xrEnumerateViewConfigurationViews(app.instance, app.systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, viewCount, &viewCount, app.viewConfigs.data()));
	if (viewCount != 2) {
		LOGE("nombre de vues inattendu : %u", viewCount);
		return false;
	}
	for (int eye = 0; eye < 2; eye++) {
		if (!createSwapchain(app, (int32_t)app.viewConfigs[eye].recommendedImageRectWidth,
		                     (int32_t)app.viewConfigs[eye].recommendedImageRectHeight, app.eyeSwapchains[eye]))
			return false;
	}
	// 1600x1200 = 320x200 agrandi 5x en largeur et 6x en hauteur : pixels nets, format 4:3 d'origine.
	// Une marge noire entoure l'image ; elle reste cachée sous le cadre et évite tout liseré
	// transparent au bord de l'écran quand la tête bouge vite.
	if (!createSwapchain(app, kQuadImageW + 2 * kQuadMarginX, kQuadImageH + 2 * kQuadMarginY, app.quadSwapchain))
		return false;

	return initActions(app);
}

// Tampons de profondeur des deux yeux (le décor est en 3D), anticrénelés si possible.
void createEyeDepth(App &app) {
	for (int eye = 0; eye < 2; eye++) {
		if (!app.eyeFbo[eye])
			glGenFramebuffers(1, &app.eyeFbo[eye]);
		if (app.eyeDepth[eye])
			glDeleteRenderbuffers(1, &app.eyeDepth[eye]);
		glGenRenderbuffers(1, &app.eyeDepth[eye]);
		glBindRenderbuffer(GL_RENDERBUFFER, app.eyeDepth[eye]);
		const Swapchain &sc = app.eyeSwapchains[eye];
		if (app.msaa > 1)
			app.renderbufferStorageMultisample(GL_RENDERBUFFER, app.msaa, GL_DEPTH_COMPONENT24, sc.width, sc.height);
		else
			glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, sc.width, sc.height);
		glBindRenderbuffer(GL_RENDERBUFFER, 0);
		glBindFramebuffer(GL_FRAMEBUFFER, app.eyeFbo[eye]);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, app.eyeDepth[eye]);
	}
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void attachEyeColor(App &app, GLuint texture) {
	if (app.msaa > 1)
		app.framebufferTexture2DMultisample(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0, app.msaa);
	else
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
}

// Lie l'image d'un œil comme destination du rendu. Au premier passage, vérifie que
// l'anticrénelage est accepté par le casque ; sinon on s'en passe.
void bindEyeTarget(App &app, int eye, GLuint texture) {
	glBindFramebuffer(GL_FRAMEBUFFER, app.eyeFbo[eye]);
	attachEyeColor(app, texture);
	if (app.eyeTargetsChecked)
		return;
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE && app.msaa > 1) {
		LOGW("anticrénelage refusé : rendu sans anticrénelage");
		app.msaa = 1;
		createEyeDepth(app);
		glBindFramebuffer(GL_FRAMEBUFFER, app.eyeFbo[eye]);
		attachEyeColor(app, texture);
	}
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		LOGE("image de l'œil %d inutilisable (0x%x)", eye, glCheckFramebufferStatus(GL_FRAMEBUFFER));
	if (eye == 1)
		app.eyeTargetsChecked = true;
}

bool initGl(App &app) {
	glGenFramebuffers(1, &app.fbo);
	app.blitProgram = linkProgram(kBlitVs, kBlitFs);
	app.blitTexSize = glGetUniformLocation(app.blitProgram, "uTexSize");
	app.blitOutSize = glGetUniformLocation(app.blitProgram, "uOutSize");
	app.blitRect = glGetUniformLocation(app.blitProgram, "uRect");
	glGenTextures(1, &app.srcTex);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_BLEND);

	const char *ext = (const char *)glGetString(GL_EXTENSIONS);
	if (ext && strstr(ext, "GL_EXT_multisampled_render_to_texture")) {
		app.renderbufferStorageMultisample = (RenderbufferStorageMultisampleFn)eglGetProcAddress("glRenderbufferStorageMultisampleEXT");
		app.framebufferTexture2DMultisample = (FramebufferTexture2DMultisampleFn)eglGetProcAddress("glFramebufferTexture2DMultisampleEXT");
		if (app.renderbufferStorageMultisample && app.framebufferTexture2DMultisample)
			app.msaa = 4;
	}
	LOGI("anticrénelage : x%d", app.msaa);
	createEyeDepth(app);

	app.sceneOk = app.scene.init(app.srgb);
	app.dungeonOk = app.dungeon.init(app.srgb);
	app.scene.setDungeon(&app.dungeon);
	app.ui.initGl(app.srgb);
	return app.sceneOk;
}

// ---------------------------------------------------------------------------
// Jeu

void showMissingFilesMessage(App &app) {
	std::string shortPath = app.gameRoot;
	size_t pos = shortPath.find("/Android/");
	if (pos != std::string::npos)
		shortPath = shortPath.substr(pos + 1);
	std::string body;
	if (app.language == lolvr::kLangFrench)
		body = "Les fichiers du jeu sont introuvables.\n"
		       "\n"
		       "1. Branchez le casque à votre ordinateur.\n"
		       "2. Copiez tout le contenu du CD de Lands of Lore\n"
		       "   (les fichiers .PAK, dont GENERAL.PAK) dans ce\n"
		       "   dossier du casque :\n"
		       "\n"
		       "   " + shortPath + "\n"
		       "\n"
		       "Le jeu démarrera tout seul dès que les fichiers\n"
		       "seront en place.";
	else if (app.language == lolvr::kLangGerman)
		body = "Die Spieldateien wurden nicht gefunden.\n"
		       "\n"
		       "1. Verbinde das Headset mit deinem Computer.\n"
		       "2. Kopiere den ganzen Inhalt der Lands-of-Lore-CD\n"
		       "   (die .PAK-Dateien, auch GENERAL.PAK) in diesen\n"
		       "   Ordner des Headsets:\n"
		       "\n"
		       "   " + shortPath + "\n"
		       "\n"
		       "Das Spiel startet von selbst, sobald die Dateien\n"
		       "da sind.";
	else
		body = "The game files were not found.\n"
		       "\n"
		       "1. Connect the headset to your computer.\n"
		       "2. Copy the whole content of the Lands of Lore CD\n"
		       "   (the .PAK files, including GENERAL.PAK) into\n"
		       "   this folder of the headset:\n"
		       "\n"
		       "   " + shortPath + "\n"
		       "\n"
		       "The game starts by itself as soon as the files\n"
		       "are in place.";
	lolvr::renderMessage(app.frame, "Lands of Lore VR", body.c_str());
	app.frameDirty = true;
}

void updateGame(App &app) {
	CoreHost &host = CoreHost::get();
	if (!app.coreStarted) {
		double now = nowSeconds();
		if (now < app.nextScan)
			return;
		app.nextScan = now + 1.5;
		std::string found;
		if (findGameDir(app.gameRoot, 2, found)) {
			app.gameDir = found;
			LOGI("jeu trouvé dans %s", found.c_str());
			// Les fenêtres de l'application prennent l'écriture des menus du jeu.
			LOGI("police du jeu : %s", app.ui.loadGameFont(found) ? "chargée" : "introuvable");
			lolvr::HostConfig cfg;
			cfg.systemDir = app.systemDir;
			cfg.saveDir = app.saveDir;
			cfg.gameDir = app.gameDir;
			cfg.language = app.language; // le jeu démarre dans la langue de l'application
			cfg.log = coreLog;
			lolvr::renderMessage(app.frame, "Lands of Lore VR", app.language == lolvr::kLangFrench ? "Démarrage du jeu…" : app.language == lolvr::kLangGerman ? "Das Spiel startet…" : "Starting the game…");
			app.frameDirty = true;
			if (host.start(cfg))
				app.coreStarted = true;
		} else if (app.frame.serial == 0) {
			showMissingFilesMessage(app);
		}
		return;
	}
	if (host.fetchFrame(app.frame)) {
		app.frameDirty = true;
		app.frameSerial++;
	}
	// Le son est ouvert dès le lancement (ambiance) ; on le rouvre si le jeu utilise une autre fréquence.
	if (host.sampleRate() != app.audioRate) {
		stopAudio(app.audio);
		app.audioRate = host.sampleRate();
		app.audio = startAudio(app.audioRate);
	}
	if (host.finished() && !app.exitRequested) {
		LOGI("ScummVM s'est terminé : fermeture de l'application");
		app.exitRequested = true;
		ANativeActivity_finish(app.androidApp->activity);
	}
}

// ---------------------------------------------------------------------------
// Entrées

float getFloat(App &app, XrAction action, int hand) {
	XrActionStateGetInfo info{XR_TYPE_ACTION_STATE_GET_INFO};
	info.action = action;
	info.subactionPath = app.handPaths[hand];
	XrActionStateFloat state{XR_TYPE_ACTION_STATE_FLOAT};
	if (XR_FAILED(xrGetActionStateFloat(app.session, &info, &state)) || !state.isActive)
		return 0.0f;
	return state.currentState;
}

bool getBool(App &app, XrAction action, int hand) {
	XrActionStateGetInfo info{XR_TYPE_ACTION_STATE_GET_INFO};
	info.action = action;
	if (hand >= 0)
		info.subactionPath = app.handPaths[hand];
	XrActionStateBoolean state{XR_TYPE_ACTION_STATE_BOOLEAN};
	if (XR_FAILED(xrGetActionStateBoolean(app.session, &info, &state)) || !state.isActive)
		return false;
	return state.currentState == XR_TRUE;
}

XrVector2f getStick(App &app, int hand) {
	XrActionStateGetInfo info{XR_TYPE_ACTION_STATE_GET_INFO};
	info.action = app.stickAction;
	info.subactionPath = app.handPaths[hand];
	XrActionStateVector2f state{XR_TYPE_ACTION_STATE_VECTOR2F};
	if (XR_FAILED(xrGetActionStateVector2f(app.session, &info, &state)) || !state.isActive)
		return {0, 0};
	return state.currentState;
}

// Seuil avec hystérésis : évite les rebonds autour de la valeur limite.
bool threshold(bool wasDown, float value) {
	return wasDown ? value > 0.35f : value > 0.6f;
}

void setKey(Button &b, bool down, unsigned key, unsigned mods = 0) {
	if (down == b.down)
		return;
	b.down = down;
	CoreHost::get().key(key, down, mods);
}

// ---------------------------------------------------------------------------
// Réglages mémorisés (décor choisi, taille et distance de l'écran)

struct Limits {
	float wMin, wMax, dMin, dMax;
};
// Dans la bibliothèque, l'écran doit tenir entre les étagères et devant le mur du fond.
const Limits kLimits[kEnvCount] = {{0.5f, 3.0f, 0.6f, 2.9f}, {0.4f, 5.0f, 0.4f, 5.0f}, {0.25f, 2.0f, 0.35f, 3.0f}, {0.25f, 2.0f, 0.35f, 2.9f}};

// Fichier de réglages : une ligne par réglage, « nom valeurs… ». Les lignes inconnues sont ignorées.
void loadSettings(App &app, int &environment) {
	FILE *f = fopen(app.settingsPath.c_str(), "r");
	if (!f)
		return;
	char line[256];
	int screenIndex = 0, aimIndex = 0, loadedPanels = 0, cineLoaded = 0;
	std::string activeProfile;
	// La disposition (écran, fenêtres) n'est reprise que si elle a été enregistrée avec la disposition
	// d'origine actuelle (« disposition 2 » : écran plus petit et plus proche, pour le donjon en 3D).
	bool layoutCurrent = false, panelsCurrent = false, colGrouped = true;
	while (fgets(line, sizeof(line), f)) {
		if (!strncmp(line, "disposition 2", 13))
			layoutCurrent = true;
		// (« fenetres 4 » : le grand bloc sorts / personnages + inventaire / colonne ; avant, les
		// fenêtres d'alors ne sont pas reprises)
		if (!strncmp(line, "fenetres 4", 10))
			panelsCurrent = true;
	}
	rewind(f);
	while (fgets(line, sizeof(line), f)) {
		char name[32] = {0};
		int used = 0;
		if (sscanf(line, " %31s %n", name, &used) < 1)
			continue;
		const char *rest = line + used;
		int iv = 0;
		if (!strcmp(name, "decor")) {
			if (sscanf(rest, "%d", &iv) == 1 && iv >= 0 && iv < kEnvCount)
				environment = iv;
		} else if (!layoutCurrent && (!strcmp(name, "ecran") || !strcmp(name, "visee") || !strcmp(name, "panneau"))) {
			continue;
		} else if (!strcmp(name, "taille_menu")) {
			int e = -1;
			float w = 0;
			if (sscanf(rest, "%d %f", &e, &w) == 2 && e >= 0 && e < kEnvCount && w >= kLimits[e].wMin && w <= kLimits[e].wMax)
				app.menuWidths[e] = w;
		} else if (!strcmp(name, "ecran")) {
			float w = 0, d = 0;
			if (screenIndex < kEnvCount && sscanf(rest, "%f %f", &w, &d) == 2) {
				if (w >= kLimits[screenIndex].wMin && w <= kLimits[screenIndex].wMax)
					app.widths[screenIndex] = w;
				if (d >= kLimits[screenIndex].dMin && d <= kLimits[screenIndex].dMax)
					app.distances[screenIndex] = d;
			}
			screenIndex++;
		} else if (!strcmp(name, "feu")) {
			if (sscanf(rest, "%d", &iv) == 1)
				app.fireOn = false; // (plus de bouton pour le feu : il reste éteint)
		} else if (!strcmp(name, "langue")) {
			if (sscanf(rest, "%d", &iv) == 1)
				app.language = iv == 1 ? lolvr::kLangFrench : iv == 2 ? lolvr::kLangGerman : lolvr::kLangEnglish;
		} else if (!strcmp(name, "donjon3d")) {
			if (sscanf(rest, "%d", &iv) == 1)
				app.dungeonMode = iv == 0 ? 0 : 1; // l'ancien « écran toujours » (2) n'existe plus
		} else if (!strcmp(name, "livresorts")) {
			if (sscanf(rest, "%d", &iv) == 1)
				app.ui.setSpellsCollapsed(iv != 0);
		} else if (!strcmp(name, "profil")) {
			// profil a b x y poignée L3 R3 nom : un profil de manettes du joueur
			int v[lolvr::kSlotCount] = {0};
			int used2 = 0;
			if (sscanf(rest, "%d %d %d %d %d %d %d %n", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &used2) == 7) {
				std::string nm(rest + used2);
				while (!nm.empty() && (nm.back() == '\n' || nm.back() == '\r' || nm.back() == ' '))
					nm.pop_back();
				uint8_t fns[lolvr::kSlotCount];
				for (int i = 0; i < lolvr::kSlotCount; i++)
					fns[i] = (uint8_t)(v[i] >= 0 && v[i] < lolvr::kFnCount ? v[i] : 0);
				app.ui.addProfile(nm, fns);
			}
		} else if (!strcmp(name, "profilactif")) {
			activeProfile = rest;
			while (!activeProfile.empty() && (activeProfile.back() == '\n' || activeProfile.back() == '\r' || activeProfile.back() == ' '))
				activeProfile.pop_back();
		} else if (!strcmp(name, "atlas") || !strcmp(name, "atlasreplie")) {
			// (anciens réglages : la fenêtre de l'atlas s'ouvre désormais avec son icône)
		} else if (!strcmp(name, "rappelvue")) {
			if (sscanf(rest, "%d", &iv) == 1)
				app.viewNotices = iv & 3;
		} else if (!strcmp(name, "choixvue")) {
			if (sscanf(rest, "%d", &iv) == 1)
				app.viewChosen = iv != 0;
		} else if (!strcmp(name, "engrenage")) {
			if (sscanf(rest, "%d", &iv) == 1)
				app.gearKnown = iv != 0;
		} else if (!strcmp(name, "colonne_collee")) {
			if (sscanf(rest, "%d", &iv) == 1 && panelsCurrent)
				colGrouped = iv != 0;
		} else if (!strcmp(name, "inventaire_colle")) {
			if (sscanf(rest, "%d", &iv) == 1 && panelsCurrent)
				app.ui.setInventoryGrouped(iv != 0);
		} else if (!strcmp(name, "cinema")) {
			if (sscanf(rest, "%d", &iv) == 1)
				app.cinemaSize = iv >= 0 && iv <= 3 ? iv : 0;
		} else if (!strcmp(name, "cinema_avant")) {
			if (sscanf(rest, "%d", &iv) == 1)
				app.cinemaIntro = iv != 0;
		} else if (!strcmp(name, "cinema_ecran")) {
			float z = 1.0f, r = 1.5f, l = 0.0f;
			if (sscanf(rest, "%f %f %f", &z, &r, &l) == 3) {
				app.cinemaZoom = clampf(z, 0.4f, 2.5f);
				app.cinemaR = clampf(r, 0.6f, 4.0f);
				app.cinemaLift = clampf(l, -1.5f, 1.5f);
			}
		} else if (!strcmp(name, "interface")) {
			if (sscanf(rest, "%d", &iv) == 1)
				app.hudMode = iv >= 0 && iv <= 2 ? iv : 0;
		} else if (!strcmp(name, "panneaux")) {
			if (sscanf(rest, "%d", &iv) == 1)
				app.detached = iv != 0;
		} else if (!strcmp(name, "visee")) {
			float yaw = 0, pitch = 0;
			if (aimIndex < kEnvCount && sscanf(rest, "%d %f %f", &iv, &yaw, &pitch) == 3 && iv != 0 && fabsf(yaw) <= 3.2f && fabsf(pitch) <= 1.4f) {
				app.aimYaw[aimIndex] = yaw;
				app.aimPitch[aimIndex] = pitch;
				app.aimSaved[aimIndex] = true;
			}
			aimIndex++;
		} else if (!strcmp(name, "panneau")) {
			// panneau <nom> lacet tangage distance échelle (par rapport à la place du joueur)
			char key[32] = "";
			float a = 0, b = 0, c = 0, scale = 0;
			bool ok = sscanf(rest, "%31s %f %f %f %f", key, &a, &b, &c, &scale) == 5;
			if (ok && panelsCurrent && scale >= 0.0004f && scale <= 0.02f && fabsf(a) <= 3.2f && fabsf(b) <= 1.4f && c >= 0.2f && c <= 6.0f)
				for (int i = 0; i < lolvr::kUiPanelCount; i++)
					if (!strcmp(key, lolvr::Ui::panelKey(i))) {
						app.ui.placement[i] = {a, b, c, scale};
						loadedPanels |= 1 << i;
					}
		} else if (!strcmp(name, "panneau_cinema")) {
			// (la disposition des fenêtres pendant les scènes, réglée à la main)
			char key[32] = "";
			float a = 0, b = 0, c = 0, scale = 0;
			bool ok = sscanf(rest, "%31s %f %f %f %f", key, &a, &b, &c, &scale) == 5;
			if (ok && scale >= 0.0004f && scale <= 0.02f && fabsf(a) <= 3.2f && fabsf(b) <= 1.4f && c >= 0.2f && c <= 6.0f)
				for (int i = 0; i < lolvr::kUiPanelCount; i++)
					if (!strcmp(key, lolvr::Ui::panelKey(i))) {
						app.ui.cinePlacement[i] = {a, b, c, scale};
						cineLoaded |= 1 << i;
					}
		}
	}
	fclose(f);
	app.ui.cineCustom = (cineLoaded & ((1 << lolvr::kPanelKeyboard) - 1)) == ((1 << lolvr::kPanelKeyboard) - 1);
	app.ui.selectProfile(activeProfile);
	// La disposition n'est reprise que si elle est complète (sinon : disposition d'origine).
	// (l'atlas, venu plus tard, se place à côté des personnages s'il manque)
	const int needed = (1 << lolvr::kUiPanelCount) - 1 - (1 << lolvr::kPanelHelp) - (1 << lolvr::kPanelAtlas) - (1 << lolvr::kPanelColumn);
	if (!(loadedPanels & (1 << lolvr::kPanelHelp)))
		app.ui.placement[lolvr::kPanelHelp] = lolvr::Ui::defaultHelpPlacement(0.0f);
	app.layoutLoaded = (loadedPanels & needed) == needed;
	if (app.layoutLoaded) {
		app.ui.setHasLayout(true);
		app.ui.setColumnGrouped(colGrouped || !(loadedPanels & (1 << lolvr::kPanelColumn)));
	}
}

void saveSettings(App &app) {
	app.settingsDirty = false;
	FILE *f = fopen(app.settingsPath.c_str(), "w");
	if (!f)
		return;
	fprintf(f, "disposition 2\n");
	fprintf(f, "fenetres 4\n");
	fprintf(f, "decor %d\n", app.dungeonMode ? app.classicEnv : app.environment);
	for (int i = 0; i < kEnvCount; i++)
		fprintf(f, "ecran %.3f %.3f\n", app.widths[i], app.distances[i]);
	for (int i = 0; i < kEnvCount; i++)
		if (app.menuWidths[i] > 0.0f)
			fprintf(f, "taille_menu %d %.3f\n", i, app.menuWidths[i]);
	fprintf(f, "feu %d\n", app.fireOn ? 1 : 0);
	fprintf(f, "panneaux %d\n", app.detached ? 1 : 0);
	fprintf(f, "interface %d\n", app.hudMode);
	fprintf(f, "cinema %d\n", app.cinemaSize);
	fprintf(f, "cinema_avant %d\n", app.cinemaIntro ? 1 : 0);
	fprintf(f, "cinema_ecran %.3f %.3f %.3f\n", app.cinemaZoom, app.cinemaR, app.cinemaLift);
	fprintf(f, "inventaire_colle %d\n", app.ui.inventoryGrouped() ? 1 : 0);
	fprintf(f, "colonne_collee %d\n", app.ui.columnGrouped() ? 1 : 0);
	fprintf(f, "langue %d\n", app.language);
	fprintf(f, "engrenage %d\n", app.gearKnown ? 1 : 0);
	fprintf(f, "donjon3d %d\n", app.dungeonMode);
	fprintf(f, "choixvue %d\n", app.viewChosen ? 1 : 0);
	fprintf(f, "rappelvue %d\n", app.viewNotices);
	fprintf(f, "livresorts %d\n", app.ui.spellsCollapsed() ? 1 : 0);
	fputs(app.ui.profilesText().c_str(), f);
	for (int i = 0; i < kEnvCount; i++)
		fprintf(f, "visee %d %.4f %.4f\n", app.aimSaved[i] ? 1 : 0, app.aimYaw[i], app.aimPitch[i]);
	if (app.ui.hasLayout())
		for (int i = 0; i < lolvr::kUiPanelCount; i++) {
			const lolvr::UiPlacement &pl = app.ui.placement[i];
			fprintf(f, "panneau %s %.4f %.4f %.4f %.5f\n", lolvr::Ui::panelKey(i), pl.a, pl.b, pl.c, pl.scale);
		}
	if (app.ui.hasLayout() && app.ui.cineCustom)
		for (int i = 0; i < lolvr::kUiPanelCount; i++) {
			const lolvr::UiPlacement &pl = app.ui.cinePlacement[i];
			fprintf(f, "panneau_cinema %s %.4f %.4f %.4f %.5f\n", lolvr::Ui::panelKey(i), pl.a, pl.b, pl.c, pl.scale);
		}
	fclose(f);
}

// ---------------------------------------------------------------------------
// Placement de l'écran et de la pièce

void updateHead(App &app, XrTime time) {
	XrSpaceLocation head{XR_TYPE_SPACE_LOCATION};
	app.headValid = XR_SUCCEEDED(xrLocateSpace(app.viewSpace, app.localSpace, time, &head)) &&
	                (head.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) &&
	                (head.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT);
	if (app.headValid) {
		app.headPos = toVec3(head.pose.position);
		app.headRot = toQuat(head.pose.orientation);
	}
}

// Hauteur du sol : donnée par le casque si possible, sinon on suppose un joueur assis.
void updateFloor(App &app, XrTime time) {
	if (app.dungeonMode) {
		// Le donjon 3D : les yeux du joueur, assis ou debout, à la hauteur de ceux du jeu (l'horizon
		// à mi-hauteur des murs, un peu au-dessus) ; R3 le refait.
		app.floorY = app.headPos.y - lolvr::kWallHeight * 0.55f;
		return;
	}
	float y = app.headPos.y - 1.25f;
	if (app.stageSpace != XR_NULL_HANDLE) {
		XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
		if (XR_SUCCEEDED(xrLocateSpace(app.stageSpace, app.localSpace, time, &loc)) &&
		    (loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)) {
			float below = app.headPos.y - loc.pose.position.y;
			if (below > 0.6f && below < 2.3f)
				y = loc.pose.position.y;
		}
	}
	app.floorY = y;
}

// Place l'écran devant le joueur. newAnchor : repart de la position et du regard actuels
// (premier affichage, recentrage, changement de décor) ; sinon seule la distance a changé.
void placeScreen(App &app, bool newAnchor) {
	bool library = lolvr::isLibraryEnv(app.environment);
	if (newAnchor) {
		app.cinemaArmed = true; // (l'écran courbe du mode cinéma aussi, devant soi)
		Vec3 fwd = rotate(app.headRot, {0, 0, -1});
		float yaw = atan2f(-fwd.x, -fwd.z);
		Vec3 flat = rotate(quatYaw(yaw), {0, 0, -1});
		app.screenYaw = yaw;
		app.anchorYaw = yaw;
		app.anchorPos = app.headPos;
		if (app.aimSaved[app.environment]) {
			// Le joueur a déjà choisi où il veut l'écran : on le remet au même endroit par rapport à lui.
			float y = yaw + app.aimYaw[app.environment], p = app.aimPitch[app.environment];
			app.anchorDir = rotate(quatYaw(y), {0, 0, -1}) * cosf(p) + Vec3{0, sinf(p), 0};
			app.screenYaw = y;
		} else if (library) {
			// Un peu sous la ligne des yeux, à portée de main : les fenêtres du jeu se logent dessous,
			// et le donjon reste dégagé droit devant.
			float d = screenDistance(app);
			app.anchorDir = normalize(flat * d + Vec3{0, -0.12f * d, 0});
		} else {
			// En réalité mixte, l'écran se pose là où l'on regarde (sur un bureau, par exemple).
			float pitch = clampf(asinf(clampf(fwd.y, -1.0f, 1.0f)), -0.6f, 0.45f);
			app.anchorDir = flat * cosf(pitch) + Vec3{0, sinf(pitch), 0};
		}
		app.roomPos = {app.headPos.x, app.floorY, app.headPos.z};
	}
	Vec3 pos = app.anchorPos + app.anchorDir * screenDistance(app);
	if (library) {
		// Sans jamais toucher le plancher ni le plafond : on mesure le cadre réellement affiché
		// (la vue du donjon seule est moins haute et remontée), moulure dorée comprise.
		float w = screenWidth(app), h, lift, dw;
		if (app.split) {
			h = w * lolvr::kSplitViewHeight;
			lift = w * lolvr::kSplitViewLift;
			dw = h * 176.0f / (120.0f * 1.2f);
		} else {
			h = w * 0.75f;
			lift = 0.0f;
			dw = w;
		}
		float rim = 0.095f * dw;
		float below = h * 0.5f - lift + rim + 0.03f; // du centre au bas du cadre, plus un doigt d'air
		float above = h * 0.5f + lift + rim + 0.15f; // du centre au haut du cadre et à sa poignée
		float lo = app.floorY + below, hi = app.floorY + lolvr::kRoomHeight - above;
		pos.y = clampf(pos.y, lo, fmaxf(lo, hi));
	}
	app.screenPos = pos;
	app.screenPlaced = true;
}

// Le joueur tient la poignée de l'écran : il le pose où il veut (et le joystick le rapproche ou l'agrandit).
void dragScreen(App &app, Vec3 center, float zoom) {
	const Limits &lim = kLimits[app.environment];
	Vec3 v = center - app.anchorPos;
	float dist = length(v);
	if (dist < 1e-3f)
		return;
	Vec3 dir = v * (1.0f / dist);
	if (lolvr::isLibraryEnv(app.environment)) {
		// Dans la bibliothèque, l'écran reste devant le mur du fond.
		float yaw = atan2f(-dir.x, -dir.z) - app.anchorYaw;
		while (yaw > 3.14159265f) yaw -= 6.2831853f;
		while (yaw < -3.14159265f) yaw += 6.2831853f;
		yaw = clampf(yaw, -0.45f, 0.45f);
		float pitch = clampf(asinf(clampf(dir.y, -1.0f, 1.0f)), -0.70f, 0.45f);
		Vec3 flat = rotate(quatYaw(app.anchorYaw + yaw), {0, 0, -1});
		dir = flat * cosf(pitch) + Vec3{0, sinf(pitch), 0};
	}
	app.anchorDir = dir;
	screenDistance(app) = clampf(dist, lim.dMin, lim.dMax);
	screenWidth(app) = clampf(screenWidth(app) * zoom, lim.wMin, lim.wMax);
	// L'écran reste tourné vers le joueur.
	app.screenYaw = atan2f(-dir.x, -dir.z);
	float rel = app.screenYaw - app.anchorYaw;
	while (rel > 3.14159265f) rel -= 6.2831853f;
	while (rel < -3.14159265f) rel += 6.2831853f;
	app.aimYaw[app.environment] = rel;
	app.aimPitch[app.environment] = asinf(clampf(dir.y, -1.0f, 1.0f));
	app.aimSaved[app.environment] = true;
	placeScreen(app, false);
}

// Le joueur tient un coin de l'écran : seule sa taille change.
void resizeScreen(App &app, float zoom) {
	const Limits &lim = kLimits[app.environment];
	shownWidth(app) = clampf(shownWidth(app) * zoom, lim.wMin, lim.wMax);
	placeScreen(app, false);
}

bool usesPassthrough(int env) { return env == lolvr::kEnvPassthroughFrame || env == lolvr::kEnvPassthroughCrt; }

void setEnvironment(App &app, int env, XrTime time) {
	if (env < 0 || env >= kEnvCount)
		env = lolvr::kEnvLibrary;
	if (usesPassthrough(env) && !app.passthroughSupported)
		env = lolvr::kEnvLibrary;
	setPassthrough(app, usesPassthrough(env));
	if (usesPassthrough(env) && !app.passthroughActive)
		env = lolvr::kEnvLibrary;
	app.environment = env;
	LOGI("décor : %d", env);
	if (app.headValid) {
		updateFloor(app, time);
		placeScreen(app, true);
	}
}

// Vue classique (mode 0 : bibliothèque, réalité mixte ou PC 486, au choix) ou mode immersif (1 et 2 :
// le donjon en 3D, une seule vue, sans réalité mixte ni PC 486).
void setViewMode(App &app, int mode, XrTime time) {
	const bool was = app.dungeonMode != 0, immersive = mode != 0;
	if (immersive && !was)
		app.classicEnv = app.environment;
	app.dungeonMode = mode;
	if (immersive != was)
		setEnvironment(app, immersive ? lolvr::kEnvLibrary : app.classicEnv, time);
	app.settingsDirty = true;
}

// Le mode VR, comme le montrent les réglages : immersif, classique (la bibliothèque) ou réalité mixte ;
// hors immersif, la vue que L3 fait tourner : grand écran, fenêtres, PC 486.
enum { kVrImmersive = 0, kVrClassic = 1, kVrMixed = 2 };
int vrModeOf(const App &app) { return app.dungeonMode ? kVrImmersive : usesPassthrough(app.classicEnv) ? kVrMixed : kVrClassic; }
int classicViewOf(const App &app) { return lolvr::isCrtEnv(app.classicEnv) ? 2 : app.detached ? 1 : 0; }

void setClassic(App &app, int mode, int view, XrTime time) {
	if (mode == kVrMixed && !app.passthroughSupported)
		mode = kVrClassic;
	if (view != 2)
		app.detached = view == 1;
	app.classicEnv = mode == kVrMixed ? (view == 2 ? lolvr::kEnvPassthroughCrt : lolvr::kEnvPassthroughFrame)
	                                  : (view == 2 ? lolvr::kEnvLibraryCrt : lolvr::kEnvLibrary);
	if (app.dungeonMode)
		setViewMode(app, 0, time);
	else
		setEnvironment(app, app.classicEnv, time);
	app.classicEnv = app.environment; // (la réalité mixte peut avoir échoué)
	// La première fois en vue classique ou en réalité mixte : rappeler que L3 change la vue.
	const int bit = vrModeOf(app) == kVrMixed ? 2 : 1;
	if (!(app.viewNotices & bit)) {
		app.viewNotices |= bit;
		app.ui.openViewNotice();
	}
	app.settingsDirty = true;
}

// ---------------------------------------------------------------------------
// Vibrations

void pulse(App &app, int hand, float amplitude, float seconds) {
	if (app.sessionState != XR_SESSION_STATE_FOCUSED || app.hapticAction == XR_NULL_HANDLE)
		return;
	XrHapticVibration vibration{XR_TYPE_HAPTIC_VIBRATION};
	vibration.amplitude = clampf(amplitude, 0.0f, 1.0f);
	vibration.duration = (XrDuration)(seconds * 1e9f);
	vibration.frequency = XR_FREQUENCY_UNSPECIFIED;
	XrHapticActionInfo info{XR_TYPE_HAPTIC_ACTION_INFO};
	info.action = app.hapticAction;
	info.subactionPath = app.handPaths[hand];
	xrApplyHapticFeedback(app.session, &info, (const XrHapticBaseHeader *)&vibration);
}

enum SfxKind { kSfxNone = 0, kSfxDoor, kSfxMechanism, kSfxSwing, kSfxSpell, kSfxExplosion };

// Classe un effet sonore du jeu d'après le nom de son fichier (version CD).
int classifySfx(const char *name) {
	struct Rule {
		const char *prefix;
		int kind;
	};
	static const Rule rules[] = {
		{"DOOR", kSfxDoor}, {"DOR", kSfxDoor}, {"MTLDR", kSfxDoor}, {"PORTCUL", kSfxDoor},
		{"LEVER", kSfxMechanism}, {"PRESS", kSfxMechanism}, {"PLATE", kSfxMechanism}, {"KEYOPEN", kSfxMechanism},
		{"KEYLOCK", kSfxMechanism}, {"LOKPICK", kSfxMechanism}, {"CHEST", kSfxMechanism}, {"OPENBOX", kSfxMechanism},
		{"SWING", kSfxSwing}, {"SHWING", kSfxSwing}, {"NUNCHUK", kSfxSwing}, {"THROW", kSfxSwing},
		{"CROSSBOW", kSfxSwing}, {"BOW", kSfxSwing},
		{"FIRESPL", kSfxSpell}, {"LITENIN", kSfxSpell}, {"ICESPEL", kSfxSpell}, {"SPELL", kSfxSpell},
		{"3BOLTC", kSfxSpell}, {"2BOLTC", kSfxSpell}, {"1BOLTC", kSfxSpell}, {"DAWNSPL", kSfxSpell},
		{"HEAL", kSfxSpell}, {"SHIELD", kSfxSpell}, {"MORPH", kSfxSpell},
		{"EXPLODE", kSfxExplosion},
	};
	if (!name || !name[0])
		return kSfxNone;
	for (const Rule &r : rules)
		if (!strncasecmp(name, r.prefix, strlen(r.prefix)))
			return r.kind;
	return kSfxNone;
}

// Suit ce qui se passe dans le jeu : zone traversée (décor), coups et sons (vibrations).
void updateGameEvents(App &app, double now) {
	lolvr::GameState gs;
	if (!app.coreStarted || !CoreHost::get().gameState(gs)) {
		app.splitWanted = false;
		app.gameStarted = false;
		app.automapOpen = false;
		return;
	}

	// L'interface se détache quand la partie est en cours et que l'écran a sa disposition
	// habituelle : pas de scène sur toute la largeur, pas de carte ni de menu ouverts.
	app.gameStarted = gs.level > 0;
	memcpy(app.tileset, gs.tileset, sizeof(app.tileset));
	app.tileset[sizeof(app.tileset) - 1] = 0;
	if (gs.automapOpen && !app.automapOpen)
		app.automapSince = now;
	app.automapOpen = gs.automapOpen;
	app.atlasLive = app.dungeonMode && gs.automapOpen && (app.atlasLive || app.atlasBrowse != 0);
	app.gameMenuOpen = gs.menuOpen;
	// La langue de l'application suit celle du jeu (son menu des options).
	if (gs.gameLang >= 0) {
		const int lang = gs.gameLang == 1 ? lolvr::kLangFrench : gs.gameLang == 2 ? lolvr::kLangGerman : lolvr::kLangEnglish;
		// (une langue tout juste choisie dans le casque : le temps que le jeu la prenne)
		const bool pending = app.langRequested >= 0 && lang != app.langRequested && now - app.langRequestAt < 3.0;
		if (!pending)
			app.langRequested = -1;
		if (!pending && lang != app.language) {
			app.language = lang;
			app.settingsDirty = true;
		}
	}
	// La sauvegarde rapide d'une boutique : faite, trois petites vibrations de la main gauche.
	if (gs.quickSaveSerial != app.quickSaveSeen) {
		if (app.quickSaveSeen >= 0 && app.quickSaveAsked >= 0 && now - app.quickSaveAsked < 5.0) {
			pulse(app, kLeft, 0.5f, 0.05f);
			app.quickSavePulses = 2;
			app.quickSavePulseAt = now + 0.15;
		}
		app.quickSaveSeen = gs.quickSaveSerial;
	}
	if (app.quickSavePulses > 0 && now >= app.quickSavePulseAt) {
		pulse(app, kLeft, 0.5f, 0.05f);
		app.quickSavePulses--;
		app.quickSavePulseAt = now + 0.15;
	}
	// « Réglages VR » choisi dans le menu du jeu : la fenêtre des réglages s'ouvre devant le joueur.
	if (gs.vrMenuSerial != app.vrMenuSerial) {
		if (app.vrMenuSerial >= 0)
			app.ui.openMenu();
		app.vrMenuSerial = gs.vrMenuSerial;
	}
	// Écrire le nom d'une sauvegarde : le clavier vient tout seul, et repart quand c'est fini (s'il
	// n'était pas déjà ouvert, et si le joueur ne l'a pas fermé entre-temps).
	if (gs.nameEntry != app.nameEntry) {
		app.nameEntry = gs.nameEntry;
		if (gs.nameEntry && !app.keyboardOpen) {
			app.keyboardOpen = true;
			app.keyboardAuto = true;
		} else if (!gs.nameEntry) {
			if (app.keyboardAuto && app.keyboardOpen)
				app.keyboardOpen = false;
			app.keyboardAuto = false;
		}
	}
	bool wanted = app.gameStarted && gs.controlMode == 0 && !gs.automapOpen && !gs.menuOpen;
	app.charSheet = gs.charSheet;
	app.inDungeon = wanted && !gs.charSheet;
	// Mode immersif : le menu du jeu (la disquette) se montre seul, sans le reste de l'image ni le cadre.
	app.menuAlone = app.dungeonMode && gs.menuOpen && app.level.menuRect != 0 && !gs.automapOpen;
	wanted = wanted || app.menuAlone || app.atlasLive;
	// Une scène qui prend la main (rencontre, cinématique) : en mode immersif, elle reste dans le
	// cadre de la vue, avec sa zone de texte dessous et la fenêtre des personnages à côté (leurs
	// portraits d'avant la scène : le jeu les cache sous sa grande zone de texte).
	app.cutscene = app.dungeonMode && app.gameStarted && gs.controlMode != 0 && !gs.automapOpen && !gs.menuOpen && !gs.charSheet;
	wanted = wanted || app.cutscene;
	app.characters = gs.characters >= 1 && gs.characters <= 4 ? gs.characters : 0;
	if (app.selectedChar >= app.characters)
		app.selectedChar = 0;
	if (wanted != app.splitWanted) {
		app.splitWanted = wanted;
		app.splitWantedSince = now;
	}

	// Le décor change une fois la nouvelle zone bien installée (pas pendant un chargement).
	int theme = lolvr::themeFromGame(gs.tileset, gs.level);
	if (theme != app.wantedTheme) {
		app.wantedTheme = theme;
		app.wantedThemeSince = now;
	}
	if (app.shownTheme != app.wantedTheme && now - app.wantedThemeSince > 0.75) {
		app.shownTheme = app.wantedTheme;
		LOGI("zone du jeu : %s (niveau %d) -> décor « %s »", gs.tileset, gs.level, lolvr::themeInfo(app.shownTheme).name);
	}

	if (!app.eventsPrimed) {
		app.eventsPrimed = true;
		app.sfxSerial = gs.sfxSerial;
		app.hurtSerial = gs.hurtSerial;
		app.hitSerial = gs.hitSerial;
		return;
	}
	int hand = app.activeHand, other = 1 - hand;
	if (gs.hurtSerial != app.hurtSerial) {
		// Un compagnon est blessé : forte secousse dans les deux mains.
		app.hurtSerial = gs.hurtSerial;
		float a = clampf(0.55f + (float)gs.hurtDamage / 60.0f, 0.55f, 1.0f);
		pulse(app, kLeft, a, 0.22f);
		pulse(app, kRight, a, 0.22f);
	}
	if (gs.hitSerial != app.hitSerial) {
		// Le groupe touche un monstre : coup sec dans la main qui a frappé.
		app.hitSerial = gs.hitSerial;
		pulse(app, hand, 0.75f, 0.07f);
		pulse(app, other, 0.25f, 0.05f);
	}
	if (gs.sfxSerial != app.sfxSerial) {
		app.sfxSerial = gs.sfxSerial;
		float v = 0.4f + 0.6f * clampf((float)gs.sfxVolume / 255.0f, 0.0f, 1.0f);
		switch (classifySfx(gs.sfxName)) {
		case kSfxDoor:
			pulse(app, kLeft, 0.40f * v, 0.28f);
			pulse(app, kRight, 0.40f * v, 0.28f);
			break;
		case kSfxMechanism:
			pulse(app, hand, 0.40f * v, 0.05f);
			break;
		case kSfxSwing:
			pulse(app, hand, 0.35f * v, 0.06f);
			break;
		case kSfxSpell:
			pulse(app, kLeft, 0.50f * v, 0.18f);
			pulse(app, kRight, 0.50f * v, 0.18f);
			break;
		case kSfxExplosion:
			pulse(app, kLeft, 1.0f * v, 0.30f);
			pulse(app, kRight, 1.0f * v, 0.30f);
			break;
		default:
			break;
		}
	}
}

// Centre de ce que l'écran affiche (la vue du donjon seule est remontée).
// L'objet tenu par le pointeur, posé sur le point visé du donjon en 3D : le curseur du jeu (l'icône
// de l'objet) que le jeu ne dessine que sur son image à plat. Face au joueur, il garde la même
// taille apparente quelle que soit la distance ; son point chaud tombe sur le point visé.
// Le curseur du jeu (flèche, ou l'objet tenu), posé à plat au point visé : son point chaud sur le
// point, « pixel » mètres par pixel du jeu. Dessiné par-dessus tout, dans le dernier panneau.
void drawGameCursor(App &app, Vec3 pos, Vec3 right, Vec3 up, float pixel) {
	const lolvr::LevelSnapshot &l = app.level;
	if (l.cursorW <= 0 || l.cursorH <= 0 || l.cursorRgba.size() < (size_t)l.cursorW * l.cursorH * 4)
		return;
	if (app.cursorTexSerial != l.cursorSerial || !app.cursorTex) {
		if (!app.cursorTex)
			glGenTextures(1, &app.cursorTex);
		glBindTexture(GL_TEXTURE_2D, app.cursorTex);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexImage2D(GL_TEXTURE_2D, 0, app.srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, l.cursorW, l.cursorH, 0, GL_RGBA, GL_UNSIGNED_BYTE, l.cursorRgba.data());
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glBindTexture(GL_TEXTURE_2D, 0);
		app.cursorTexSerial = l.cursorSerial;
	}
	lolvr::SceneState::Panel &p = app.ss.panels[lolvr::SceneState::kMaxPanels - 1];
	p = lolvr::SceneState::Panel();
	p.visible = true;
	p.cutout = true;
	p.onTop = true;
	p.texture = app.cursorTex;
	p.texW = l.cursorW;
	p.texH = l.cursorH;
	p.right = right;
	p.up = up;
	p.halfW = l.cursorW * pixel * 0.5f;
	p.halfH = l.cursorH * pixel * 0.5f;
	p.center = pos + cross(right, up) * 0.002f + right * ((l.cursorW * 0.5f - l.cursorHotX) * pixel) - up * ((l.cursorH * 0.5f - l.cursorHotY) * pixel);
}

// L'objet tenu par le pointeur, posé sur le point visé du donjon en 3D : le curseur du jeu (l'icône
// de l'objet) que le jeu ne dessine que sur son image à plat. Face au joueur, il garde la même
// taille apparente quelle que soit la distance ; son point chaud tombe sur le point visé.
void showHeldItem(App &app, Vec3 hit) {
	if (!app.level.itemInHand)
		return;
	Vec3 toEye = app.headPos - hit;
	const float dist = length(toEye);
	if (dist < 0.05f)
		return;
	toEye = toEye * (1.0f / dist);
	Vec3 right = cross(Vec3{0, 1, 0}, toEye);
	if (length(right) < 0.01f)
		right = rotate(app.headRot, {1, 0, 0});
	right = normalize(right);
	const Vec3 up = normalize(cross(toEye, right));
	drawGameCursor(app, hit + toEye * 0.01f, right, up, 0.0032f * dist); // un pixel du jeu : à peu près sa taille sur le grand écran
}

// La bande de texte (messages, conversation, remarque) : une fenêtre sous le cadre de l'écran, à
// sa largeur au plus. Un clic dessus va au jeu, à cet endroit de la zone de texte.
void placeBand(App &app, const lolvr::UiFrameInput &in, lolvr::UiFrameOutput &out);

Vec3 viewCenter(const App &app) {
	if (cinemaPlaced(app))
		return app.cinemaCenter;
	return app.screenPos + Vec3{0, app.viewLift, 0} + rotate(screenRot(app), {app.viewShift, 0, 0});
}

// Mode cinéma : le rayon de l'écran courbe, la distance (à plat) du joueur au centre de l'écran.
float cinemaRadius(const App &app) { return app.cinemaR; }

void placeBand(App &app, const lolvr::UiFrameInput &in, lolvr::UiFrameOutput &out) {
	if (!app.bandShown || !app.screenPlaced)
		return;
	float bx = app.bandRect[0], by = app.bandRect[1], bw = app.bandRect[2], bh = app.bandRect[3];
	if (out.partyBlockValid && out.partyBlockPixel > 0.0f) {
		// Sur le bloc des personnages : la ligne de messages seule, il la montre déjà (rien par-dessus) ;
		// une zone de texte agrandie se pose sur les portraits, sans déborder sur l'inventaire.
		if (app.level.textRect == 0 && out.partyBlockLiveLine)
			return;
		const float x0 = fmaxf(bx, out.partyBlockGameX), y0 = fmaxf(by, out.partyBlockGameY);
		float x1 = fminf(bx + bw, out.partyBlockGameX + out.partyBlockGameW), y1 = fminf(by + bh, out.partyBlockGameBottom);
		// (presque jusqu'au bas des portraits : jusqu'au bas, sinon elle semblait flotter au-dessus
		// d'une mince bande de portraits)
		if (out.partyBlockGameBottom - y1 < 14.0f)
			y1 = out.partyBlockGameBottom;
		if (x1 - x0 < 8.0f || y1 - y0 < 8.0f)
			return;
		bx = x0; by = y0; bw = x1 - x0; bh = y1 - y0;
	}
	const float viewPx = app.dispW / (app.cropW > 0.0f ? app.cropW : 176.0f);
	const float px = fminf(viewPx, app.dispW * 1.25f / bw); // mètres par pixel du jeu
	const Quat rot = screenRot(app);
	Vec3 r = rotate(rot, {1, 0, 0}), u = rotate(rot, {0, 1, 0}), n = rotate(rot, {0, 0, 1});
	float halfW = bw * px * 0.5f, halfH = bh * px * 1.2f * 0.5f;
	// Sous le cadre doré (il déborde de 9 % de la largeur sous l'image), un peu en avant.
	Vec3 c = viewCenter(app) - u * (app.dispH * 0.5f + app.dispW * 0.105f + halfH + 0.012f) + n * 0.03f;
	float pxUsed = px;
	if (out.partyBlockValid && out.partyBlockPixel > 0.0f) {
		// Le bloc des personnages (la ligne de messages, les portraits) est là : la bande s'y pose,
		// comme dans le jeu, où la zone de texte agrandie couvre les portraits. Sinon elle passait
		// devant ou derrière lui (deux textes l'un sur l'autre).
		const float pxb = out.partyBlockPixel;
		pxUsed = pxb;
		r = out.partyBlockRight;
		u = out.partyBlockUp;
		n = cross(r, u);
		halfW = bw * pxb * 0.5f;
		halfH = bh * pxb * 1.2f * 0.5f;
		c = out.partyBlockTopLeft + r * ((bx + bw * 0.5f - out.partyBlockGameX) * pxb) -
		    u * ((by + bh * 0.5f - out.partyBlockGameY) * pxb * 1.2f) + n * 0.002f;
	}
	lolvr::SceneState::Panel &p = app.ss.panels[lolvr::SceneState::kMaxPanels - 2];
	p = lolvr::SceneState::Panel();
	p.visible = true;
	p.center = c;
	p.right = r;
	p.up = u;
	p.halfW = halfW;
	p.halfH = halfH;
	p.u0 = bx / 320.0f;
	p.v0 = by / 200.0f;
	p.u1 = (bx + bw) / 320.0f;
	p.v1 = (by + bh) / 200.0f;
	p.border = out.partyBlockValid ? 0.002f : 0.004f; // (une ligne sombre, comme les fenêtres)
	p.layer = 1; // (dessinée après le bloc qu'elle recouvre)
	// Le pointeur sur la bande (si rien d'autre n'est visé devant elle ; posée sur le bloc des
	// personnages, elle passe devant lui)
	if (!in.aimValid || out.dragging)
		return;
	const float den = dot(in.dir, n);
	if (fabsf(den) < 1e-4f)
		return;
	const float t = dot(c - in.tip, n) / den;
	if (t <= 0.0f)
		return;
	if (out.hitValid && length(out.hit - in.tip) < t - 0.002f)
		return;
	const Vec3 h = in.tip + in.dir * t;
	const float hx = dot(h - c, r) / halfW, hy = dot(h - c, u) / halfH;
	if (fabsf(hx) > 1.0f || fabsf(hy) > 1.0f)
		return;
	out.pointerValid = true;
	out.buttonsToGame = true;
	out.hitValid = true;
	out.hit = h + n * 0.002f;
	out.hitRight = r;
	out.hitUp = u;
	out.hitRadius = 0.010f * t;
	out.hitPixel = pxUsed;
	out.pointerLong = false;
	out.gu = (bx + (hx + 1.0f) * 0.5f * bw) / 320.0f;
	out.gv = (by + (1.0f - hy) * 0.5f * bh) / 200.0f;
}

// ---------------------------------------------------------------------------
// Ce que montre l'écran principal : l'image entière, ou la vue du donjon seule

// La ligne de messages dans l'image du jeu (320 x 200) ; agrandie (conversations, boutiques), la
// zone de texte descend jusqu'à la ligne 179, sur les portraits du groupe.
const float kBandX = 83, kBandY = 122, kBandW = 234, kBandH = 20;

void updateLayout(App &app, double now) {
	const float kViewX = 112, kViewY = 0, kViewW = 176, kViewH = 120; // vue du donjon dans l'image 320x200
	// On attend que l'état soit stable un court instant, pour ne pas clignoter pendant une transition.
	bool allowed = (app.detached || app.dungeonMode) && !lolvr::isCrtEnv(app.environment);
	if (!allowed) {
		if (app.split && app.screenPlaced) {
			app.split = false;
			placeScreen(app, false);
		}
		app.split = false;
	} else if (app.splitWanted != app.split && now - app.splitWantedSince > (app.splitWanted ? 0.35 : 0.05)) {
		app.split = app.splitWanted;
		// L'image entière est plus haute que la vue seule : on la garde au-dessus du plancher.
		if (app.screenPlaced)
			placeScreen(app, false);
	}
	float w = shownWidth(app);
	// Mode cinéma : la taille ne se règle pas à la main ; l'écran couvre un angle fixe (le réglage).
	const bool cinemaSized = (app.cinemaWanted || app.cinema > 0.0f) && !(app.menuAlone && app.split);
	if (cinemaSized && app.cinemaW > 0.0f)
		w = app.cinemaW;
	if (app.split) {
		// La vue seule est un peu moins haute que l'image entière et remontée, pour laisser la place
		// aux fenêtres du groupe et de l'inventaire dessous, dans le même plan.
		// La fiche d'un personnage couvre la vue et la colonne de droite : l'écran s'élargit vers la
		// droite pour la montrer en entier, et les fenêtres restent en place.
		float cw = app.charSheet ? 320.0f - kViewX : kViewW, cx = kViewX, ch = kViewH;
		// Une cinématique plus large que la vue (l'empoisonnement du roi Richard, la cage de Baccata) :
		// l'écran s'élargit à toute la scène, ses personnages et son bouton de sortie.
		const uint32_t sr = app.level.sceneRect;
		float cy = kViewY;
		if (app.menuAlone) {
			const uint32_t mr = app.level.menuRect;
			float mx = (float)(mr & 511), my = (float)((mr >> 9) & 127), mw = (float)((mr >> 16) & 511), mh = (float)((mr >> 25) & 127) * 2.0f;
			if (mw >= 16.0f && mh >= 16.0f && mx + mw <= 320.0f && my + mh <= 200.0f) {
				// (la boîte de la défaite laisse des bandes noires dessus et dessous : on ne les montre pas)
				auto rowBlack = [&](int y) {
					const int fw = app.frame.width, fh = app.frame.height;
					if (fw < 320 || fh < 200 || app.frame.rgba.size() < (size_t)fw * fh * 4 || y < 0 || y >= 200)
						return false;
					// (sombre : peu de pixels clairs ; l'écran derrière la boîte est noir, assombri ou
					// tramé, alors que la boîte est claire presque partout)
					const uint8_t *row = app.frame.rgba.data() + (size_t)(y * fh / 200) * fw * 4;
					int n = 0, bright = 0;
					for (int x = (int)mx; x < (int)(mx + mw); x++, n++) {
						const uint8_t *q = row + (size_t)(x * fw / 320) * 4;
						bright += q[0] + q[1] + q[2] > 150 ? 1 : 0;
					}
					return n > 0 && bright * 10 < n;
				};
				const float mh0 = mh;
				while (mh > 16.0f && mh > mh0 * 0.3f && rowBlack((int)my)) {
					my += 1.0f;
					mh -= 1.0f;
				}
				while (mh > 16.0f && mh > mh0 * 0.3f && rowBlack((int)(my + mh) - 1))
					mh -= 1.0f;
				// (et à gauche et à droite : la question « quitter le jeu ? »)
				auto colBlack = [&](int x) {
					const int fw = app.frame.width, fh = app.frame.height;
					if (fw < 320 || fh < 200 || app.frame.rgba.size() < (size_t)fw * fh * 4 || x < 0 || x >= 320)
						return false;
					int n = 0, bright = 0;
					for (int y = (int)my; y < (int)(my + mh); y++, n++) {
						const uint8_t *q = app.frame.rgba.data() + ((size_t)(y * fh / 200) * fw + (size_t)(x * fw / 320)) * 4;
						bright += q[0] + q[1] + q[2] > 150 ? 1 : 0;
					}
					return n > 0 && bright * 10 < n;
				};
				const float mw0 = mw;
				while (mw > 16.0f && mw > mw0 * 0.3f && colBlack((int)mx)) {
					mx += 1.0f;
					mw -= 1.0f;
				}
				while (mw > 16.0f && mw > mw0 * 0.3f && colBlack((int)(mx + mw) - 1))
					mw -= 1.0f;
				cx = mx;
				cy = my;
				cw = mw;
				ch = mh;
			}
		} else if (app.dungeonMode && app.level.scenePicture && sr && !app.charSheet) {
			const float rx = (float)(sr & 511), rw = (float)((sr >> 16) & 511), rh = (float)((sr >> 25) & 127) * 2.0f;
			if (rw > kViewW + 1.0f && rx + rw <= 320.0f && rh >= 60.0f) {
				cx = rx;
				cw = rw;
				ch = rh;
			}
		}
		app.cropX = cx; app.cropY = cy; app.cropW = cw; app.cropH = ch;
		// Mode immersif, boutique ou rencontre : la ligne de messages (les choix à faire) est collée
		// sous la scène, à sa largeur.
		const bool band = app.dungeonMode && (app.level.sceneBusy || app.level.textRect != 0 || app.cutscene) && !app.charSheet && !app.menuAlone;
		// Une conversation ou la remarque d'un personnage : toute la zone de texte, avec le portrait de
		// celui qui parle (le moteur dit où elle est). La bande est une fenêtre à part, sous le cadre :
		// collée dans le cadre, elle bouchait la vue.
		const uint32_t tr = app.level.textRect;
		const bool box = band && tr != 0;
		app.bandRect[0] = box ? (float)(tr & 511) : kBandX;
		app.bandRect[1] = box ? (float)(((tr >> 9) & 127) + 100) : kBandY;
		app.bandRect[2] = box ? (float)((tr >> 16) & 511) : kBandW;
		app.bandRect[3] = box ? (float)((tr >> 25) & 127) : kBandH;
		if (app.bandRect[2] < 8.0f || app.bandRect[3] < 8.0f || app.bandRect[0] + app.bandRect[2] > 320.0f || app.bandRect[1] + app.bandRect[3] > 200.0f) {
			app.bandRect[0] = kBandX; app.bandRect[1] = kBandY; app.bandRect[2] = kBandW; app.bandRect[3] = kBandH;
		}
		app.bandShown = band;
		app.bandH = 0.0f;
		const float viewOnlyH = w * lolvr::kSplitViewHeight;
		app.dispH = viewOnlyH * (ch + app.bandH) / kViewH;
		app.dispW = viewOnlyH * cw / (kViewH * 1.2f);
		app.viewLift = w * lolvr::kSplitViewLift - (app.dispH - viewOnlyH) * 0.5f;
		// le centre de ce qui est montré, par rapport au centre de la vue
		app.viewShift = ((cx + cw * 0.5f) - (kViewX + kViewW * 0.5f)) * viewOnlyH / (kViewH * 1.2f);
	} else {
		app.viewShift = 0.0f;
		app.bandH = 0.0f;
		app.bandShown = false;
		app.cropX = 0; app.cropY = 0; app.cropW = 320; app.cropH = 200;
		app.dispW = w;
		app.dispH = w * 0.75f;
		app.viewLift = 0.0f;
	}
	if (cinemaSized && app.dispW > 1e-3f) {
		// Largeur apparente : 55° (moyen), 68° (grand), 44° (petit). Tout est proportionnel à w.
		static const float kAngle[3] = {60.0f, 72.0f, 48.0f};
		const float a = kAngle[app.cinemaSize >= 0 && app.cinemaSize < 3 ? app.cinemaSize : 0] * 3.14159265f / 180.0f;
		const float want = 2.0f * cinemaRadius(app) * tanf(a * 0.5f) * app.cinemaZoom;
		app.cinemaW = clampf(w * want / app.dispW, 0.2f, 12.0f);
		const Vec3 f{-sinf(app.cinemaYaw), 0.0f, -cosf(app.cinemaYaw)};
		app.cinemaCenter = Vec3{app.cinemaHead.x, app.cinemaHead.y + cinemaEyeOffset(app) + app.cinemaLift, app.cinemaHead.z} + f * cinemaRadius(app);
	} else {
		app.cinemaW = 0.0f;
	}
}

// Images du joueur pour le décor, dans le dossier « decor » (et, par tolérance, dans le dossier du
// jeu et à la racine). Le nom indique l'emplacement (fenetre, portrait, tableau1, tableau2, et
// quelques mots voisins) ; les images du dossier « decor » qui ne portent aucun de ces noms
// remplissent les emplacements restés libres, dans l'ordre alphabétique.
bool loadImageFile(App &app, int slot, const std::string &path) {
	FILE *f = fopen(path.c_str(), "rb");
	if (!f)
		return false;
	std::vector<unsigned char> data;
	unsigned char buf[65536];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0 && data.size() < (64u << 20))
		data.insert(data.end(), buf, buf + n);
	fclose(f);
	int w = 0, h = 0, comp = 0;
	unsigned char *px = data.empty() ? nullptr : stbi_load_from_memory(data.data(), (int)data.size(), &w, &h, &comp, 4);
	if (!px) {
		LOGW("image illisible : %s (%s)", path.c_str(), stbi_failure_reason() ? stbi_failure_reason() : "?");
		return false;
	}
	app.scene.setImage(slot, px, w, h);
	stbi_image_free(px);
	LOGI("image du décor : %s (%d x %d)", path.c_str(), w, h);
	return true;
}

// Texte affichable avec la police du clavier (ASCII seulement).
std::string plainName(const std::string &name) {
	std::string out;
	for (unsigned char ch : name)
		if (ch >= 32 && ch < 127)
			out += (char)ch;
		else if (out.empty() || out.back() != '?')
			out += '?';
	return out;
}

void scanDecorImages(App &app) {
	static const char *const words[kImageSlots][8] = {
		{"fen", "window", "chateau", "castle", "gladstone", "paysage", "vue", nullptr},
		{"portrait", "roi", "king", "richard", nullptr},
		{"tableau1", "tableau 1", "tableau_1", "tableau-1", nullptr},
		{"tableau2", "tableau 2", "tableau_2", "tableau-2", nullptr},
		{"tableau3", "tableau 3", "tableau_3", "tableau-3", nullptr},
		{"tableau4", "tableau 4", "tableau_4", "tableau-4", nullptr},
	};
	static const char *const exts[] = {".png", ".jpg", ".jpeg", ".jfif", ".bmp", ".gif", ".tga", ".webp", ".heic", ".avif"};
	const std::string dirs[3] = {app.decorDir, app.gameRoot, app.decorDir + "/.."};
	std::vector<std::string> seen, unreadable; // noms du dossier decor, pour l'information à l'écran
	bool decorExists = false;

	auto tryLoad = [&](int slot, const std::string &path) {
		struct stat st;
		long size = stat(path.c_str(), &st) == 0 ? (long)st.st_size : -1;
		auto it = app.imageRejected.find(path);
		if (it != app.imageRejected.end() && it->second == size)
			return false; // déjà essayé, le fichier n'a pas changé
		if (loadImageFile(app, slot, path)) {
			app.imageLoaded[slot] = true;
			app.imageFile[slot] = path;
			app.imageRejected.erase(path);
			return true;
		}
		app.imageRejected[path] = size;
		return false;
	};
	auto inUse = [&](const std::string &path) {
		for (int k = 0; k < kImageSlots; k++)
			if (app.imageLoaded[k] && app.imageFile[k] == path)
				return true;
		return false;
	};

	for (int di = 0; di < 3; di++) {
		const std::string &dir = dirs[di];
		DIR *dp = opendir(dir.c_str());
		if (!dp)
			continue;
		if (di == 0)
			decorExists = true;
		std::vector<std::string> names;
		while (struct dirent *e = readdir(dp))
			if (e->d_name[0] != '.')
				names.push_back(e->d_name);
		closedir(dp);
		std::sort(names.begin(), names.end());
		std::vector<std::string> unnamed;
		for (const std::string &name : names) {
			std::string lower;
			for (char ch : name)
				lower += (char)tolower((unsigned char)ch);
			bool image = false;
			for (const char *ext : exts)
				if (lower.size() > strlen(ext) && lower.find(ext) != std::string::npos)
					image = true;
			if (!image)
				continue;
			std::string path = dir + "/" + name;
			if (di == 0)
				seen.push_back(name);
			int slot = -1;
			for (int k = 0; k < kImageSlots && slot < 0; k++)
				for (int w = 0; words[k][w]; w++)
					if (lower.find(words[k][w]) != std::string::npos) {
						slot = k;
						break;
					}
			if (slot < 0) {
				if (di == 0)
					unnamed.push_back(path);
				continue;
			}
			if (!app.imageLoaded[slot] && !inUse(path))
				tryLoad(slot, path);
		}
		// Dossier decor : les images sans nom reconnu prennent les places libres.
		for (const std::string &path : unnamed) {
			if (inUse(path))
				continue;
			for (int k = 0; k < kImageSlots; k++)
				if (!app.imageLoaded[k]) {
					tryLoad(k, path);
					break;
				}
		}
	}
	for (const auto &r : app.imageRejected) {
		size_t slash = r.first.rfind('/');
		unreadable.push_back(slash == std::string::npos ? r.first : r.first.substr(slash + 1));
	}

	// Ce que le joueur lit sous le clavier (bouton X).
	static const char *const labels[kImageSlots] = {"fenetre", "portrait", "tableau1", "tableau2", "tableau3", "tableau4"};
	int shownCount = 0;
	std::string shownNames;
	for (int k = 0; k < kImageSlots; k++)
		if (app.imageLoaded[k]) {
			shownCount++;
			shownNames += std::string(" ") + labels[k];
		}
	const bool fr = app.language == lolvr::kLangFrench;
	std::string l1 = std::string(fr ? "Images affichees (" : "Images shown (") + std::to_string(shownCount) + "/" + std::to_string(kImageSlots) + (fr ? ") :" : "):") +
	                 (shownCount ? shownNames : std::string(fr ? " aucune" : " none"));
	std::string l2;
	if (!decorExists)
		l2 = fr ? "Dossier decor introuvable." : "Folder 'decor' not found.";
	else if (!unreadable.empty()) {
		l2 = fr ? "Illisible :" : "Unreadable:";
		for (const std::string &u : unreadable)
			l2 += " " + plainName(u);
		l2 += fr ? " (PNG ou JPEG)" : " (use PNG or JPEG)";
	} else if (seen.empty())
		l2 = fr ? "Dossier decor vide : aucune image vue." : "Folder 'decor' is empty: no image seen.";
	else {
		l2 = fr ? "Dossier decor :" : "Folder 'decor':";
		for (const std::string &u : seen)
			l2 += " " + plainName(u);
	}
	app.imageInfo[0] = l1;
	app.imageInfo[1] = l2;
}

// Envoie au jeu une touche du clavier virtuel (appui bref).
void typeKey(App &app, unsigned key, unsigned mods, double now) {
	unsigned retroMods = (mods & lolvr::kUiModShift) ? (unsigned)RETROKMOD_SHIFT : 0u;
	CoreHost::get().key(key, true, retroMods);
	app.pendingKeys.push_back({key, retroMods, now + 0.07});
}

void releaseTypedKeys(App &app, double now, bool all) {
	for (size_t i = 0; i < app.pendingKeys.size();) {
		if (all || now >= app.pendingKeys[i].releaseAt) {
			CoreHost::get().key(app.pendingKeys[i].key, false, app.pendingKeys[i].mods);
			app.pendingKeys.erase(app.pendingKeys.begin() + (long)i);
		} else {
			i++;
		}
	}
}

// Interface : pointeur, panneaux, poignées, clavier. pointer = faux quand l'application n'a pas la main.
// Angle ramené entre -pi et pi.
float wrapAngle(float a) {
	while (a > 3.14159265f)
		a -= 6.2831853f;
	while (a < -3.14159265f)
		a += 6.2831853f;
	return a;
}

// Bord gauche du cadre d'un personnage dans l'image du jeu (même calcul que le jeu).
int portraitX(int count, int index) {
	int t = (235 - count * 66) / (count + 1);
	return index * 66 + t * (index + 1) + 83;
}

// Le rayon du doigt dans le donjon 3D : le mur droit devant le groupe, ou le sol jusqu'à ce mur.
// Renvoie la position correspondante dans l'image du jeu (0..1), et le point touché (monde).
bool dungeonPick(App &app, Vec3 tip, Vec3 dir, float &gu, float &gv, Vec3 &hitW, Vec3 &rightW, Vec3 &upW) {
	const lolvr::LevelSnapshot &l = app.level;
	app.pickFace = -1;
	app.pickItem = -1;
	if (l.level <= 0)
		return false;
	Mat4 model = lolvr::dungeonModel(app.roomPos, app.dunYaw, app.partyX, app.partyZ, app.partyYaw);
	Vec3 c0{model.m[0], model.m[1], model.m[2]}, c1{model.m[4], model.m[5], model.m[6]}, c2{model.m[8], model.m[9], model.m[10]};
	Vec3 t{model.m[12], model.m[13], model.m[14]};
	auto toLevel = [&](Vec3 p) { Vec3 r = p - t; return Vec3{dot(c0, r), dot(c1, r), dot(c2, r)}; };
	auto dirLevel = [&](Vec3 v) { return Vec3{dot(c0, v), dot(c1, v), dot(c2, v)}; };
	Vec3 o = toLevel(tip), v = dirLevel(dir);
	lolvr::DungeonPick pk;
	lolvr::pickDungeon(l.walls, l.block, l.direction, app.dunTex, o, v, pk);
	// Un objet posé par terre, visé sur son image : le clic va à l'endroit du sol où le jeu le dessine
	// (sinon le rayon traverse l'objet et touche le sol derrière, une case plus loin).
	if (app.headValid) {
		const Vec3 cam = toLevel(app.headPos);
		const float wallT = pk.px >= 0.0f ? length(pk.hit - o) : 1e30f;
		float bestT = 1e30f;
		for (size_t j = 0; j < app.spriteList.size(); j++) {
			const lolvr::DungeonSprite &d = app.spriteList[j];
			if (d.layer < 0 || (size_t)d.layer >= l.sprites.size() || l.sprites[d.layer].kind != 1)
				continue;
			if (d.ceiling || d.base > 0.02f || app.flightOf[j] >= 0)
				continue;
			float tx = cam.x - d.x, tz = cam.z - d.z;
			const float tl = sqrtf(tx * tx + tz * tz);
			if (tl < 1e-3f)
				continue;
			const float nx = tx / tl, nz = tz / tl, rx = nz, rz = -nx;
			const float cx = d.x + rx * d.offset, cz = d.z + rz * d.offset;
			const float den = v.x * nx + v.z * nz;
			if (fabsf(den) < 1e-4f)
				continue;
			const float t = ((cx - o.x) * nx + (cz - o.z) * nz) / den;
			if (t <= 0.0f || t >= bestT || t > wallT + 0.02f)
				continue;
			const Vec3 h = o + v * t;
			const float u = (h.x - cx) * rx + (h.z - cz) * rz;
			// Une prise généreuse : un peu plus large que l'image, et plus haute (un petit objet se
			// vise sans pointer le sol, et sans se pencher).
			const float pad = 0.06f, top = fmaxf(d.height + pad, 0.28f);
			if (fabsf(u) > d.width * 0.5f + pad || h.y < d.base - pad || h.y > d.base + top)
				continue;
			float spx, spy;
			if (!lolvr::floorGamePoint(l.block, l.direction, d.x, d.z, spx, spy))
				continue;
			bestT = t;
			app.pickItem = l.sprites[d.layer].id;
			pk.px = spx;
			pk.py = spy;
			pk.face = -1;
			pk.hit = Vec3{d.x, 0.0f, d.z};
			pk.forward = Vec3{0, 1, 0};
			static const int ddx[4] = {0, 1, 0, -1}, ddz[4] = {-1, 0, 1, 0};
			const int dd = l.direction & 3;
			pk.right = Vec3{-(float)ddz[dd], 0.0f, (float)ddx[dd]};
		}
	}
	static const int dx[4] = {0, 1, 0, -1}, dz[4] = {-1, 0, 1, 0};
	const int d = l.direction & 3;
	const Vec3 fAhead{(float)dx[d], 0.0f, (float)dz[d]};
	const float px = pk.px, py = pk.py;
	const int face = pk.face;
	const Vec3 hit = pk.hit, finalF = pk.forward, finalR = pk.right;
	if (px < 0.0f)
		return false;
	app.pickFace = face;
	gu = px / 320.0f;
	gv = py / 200.0f;
	hitW = transformPoint(model, hit);
	rightW = normalize(transformDir(model, finalR));
	upW = finalF.y > 0.5f ? normalize(transformDir(model, fAhead)) : normalize(transformDir(model, Vec3{0, 1, 0}));
	return true;
}

void updateUi(App &app, XrTime time, float dt, double now, bool pointer, bool triggerPressed, XrVector2f rightStick, lolvr::UiFrameOutput &out) {
	lolvr::UiFrameInput in;
	in.dt = dt;
	in.anchorPos = app.anchorPos;
	in.anchorYaw = app.anchorYaw;
	int hand = app.activeHand;
	in.aimValid = pointer && app.aimValid[hand] && app.screenPlaced;
	if (in.aimValid) {
		Quat aimRot = toQuat(app.aimPose[hand].orientation);
		in.tip = toVec3(app.aimPose[hand].position) + rotate(aimRot, lolvr::gloveFingertip(hand == kLeft, 0.0f));
		in.dir = rotate(aimRot, {0, 0, -1});
	}
	in.triggerDown = pointer && app.triggerDown[hand];
	for (int h = 0; h < 2; h++) {
		in.pokeValid[h] = app.aimValid[h] && app.screenPlaced;
		if (in.pokeValid[h])
			in.poke[h] = toVec3(app.aimPose[h].position) + rotate(toQuat(app.aimPose[h].orientation), lolvr::gloveFingertip(h == kLeft, 0.0f));
	}
	in.triggerPressed = pointer && triggerPressed;
	in.holdToMove = true;
	{
		const uint32_t sr = app.level.sceneRect;
		in.spellsCovered = app.dungeonMode && app.level.scenePicture && app.level.sceneBusy && sr != 0 && (int)(sr & 511) < 112;
	}
	in.stickX = rightStick.x;
	in.stickY = rightStick.y;
	in.screenPos = viewCenter(app);
	in.basePos = app.screenPos;
	in.fullW = screenWidth(app);
	in.roomClamp = lolvr::isLibraryEnv(app.environment);
	in.roomPos = app.roomPos;
	in.roomYaw = app.anchorYaw;
	in.screenRot = screenRot(app);
	in.dispW = app.dispW;
	in.dispH = app.dispH;
	in.cropX = app.cropX; in.cropY = app.cropY; in.cropW = app.cropW; in.cropH = app.cropH;
	in.bandH = app.bandH;
	for (int i = 0; i < 4; i++)
		in.bandRect[i] = app.bandRect[i];
	in.crt = lolvr::isCrtEnv(app.environment);
	in.split = app.split;
	in.keyboardOpen = app.keyboardOpen;
	if (app.inDungeon && app.characters > 0) {
		in.partyMark[0] = (float)portraitX(app.characters, app.selectedChar);
		in.partyMark[1] = 143.0f;
		in.partyMark[2] = 66.0f;
		in.partyMark[3] = 34.0f;
		if (app.level.magicMenu >= 0 && app.level.magicMenu < app.characters) {
			// Le niveau en surbrillance, dans la colonne des niveaux à droite du portrait
			in.magicMark[0] = (float)portraitX(app.characters, app.level.magicMenu) + 44.0f;
			in.magicMark[1] = 144.0f + 8.0f * (float)app.spellLevel;
			in.magicMark[2] = 22.0f;
			in.magicMark[3] = 8.0f;
		}
	}
	app.ui.setOptions(app.fireOn, app.detached);
	app.ui.setDungeonOption(app.dungeonMode);
	app.ui.setHudMode(app.hudMode);
	app.ui.setCinema(app.cinemaSize, app.cinemaIntro);
	app.ui.setAtlasOption(app.atlasOn);
	if (app.dungeonMode && app.hudMode == 1) {
		CoreHost::get().inventoryDisable(); // la ligne d'objets du jeu, avec ses flèches
	} else if (app.dungeonMode) {
		CoreHost::get().inventoryEnable();
		if (app.level.invSerial != 0 && app.level.invRgb.size() == 176 * 132 * 3) {
			in.invRgb = app.level.invRgb.data();
			in.invSerial = app.level.invSerial;
		}
	}
	{
		const lolvr::LevelSnapshot &l = app.level;
		lolvr::UiMap &m = app.atlasMap;
		if (l.map.serial != app.atlasMapSerial || l.wallsSerial != app.atlasWallsSerial || l.block != app.atlasBlock || l.direction != app.atlasDir) {
			app.atlasMapSerial = l.map.serial;
			app.atlasWallsSerial = l.wallsSerial;
			app.atlasBlock = l.block;
			app.atlasDir = l.direction;
			m.serial++;
			m.available = l.map.available && l.level > 0 && l.map.serial != 0;
			m.block = l.block;
			m.direction = l.direction;
			m.flags = l.map.flags;
			m.cls = l.map.cls;
			m.walls = l.walls;
			m.title = l.map.title;
			m.rgb = l.map.rgb.size() >= 320 * 200 * 3 ? l.map.rgb.data() : nullptr;
			m.legend.clear();
			for (const lolvr::MapSnapshot::Legend &e : l.map.legend) {
				lolvr::UiMap::Mark k;
				k.trigger = e.trigger;
				k.mark = e.mark;
				k.text = e.text;
				m.legend.push_back(k);
			}
		}
		in.atlasOn = app.atlasOn && (app.inDungeon || app.atlasLive);
		in.atlasLive = app.atlasLive;
		in.map = &app.atlasMap;
	}
	app.ui.setVrState(vrModeOf(app), classicViewOf(app));
	app.ui.setLanguage(app.language);
	// Menu du poignet : sur la main gauche, quand on la regarde.
	in.palmValid = app.aimValid[kRight];
	if (in.palmValid) {
		in.palmPos = toVec3(app.aimPose[kRight].position);
		in.palmRot = toQuat(app.aimPose[kRight].orientation);
	}
	in.wristValid = app.aimValid[kLeft];
	if (in.wristValid) {
		in.wristPos = toVec3(app.aimPose[kLeft].position);
		in.wristRot = toQuat(app.aimPose[kLeft].orientation);
	}
	in.headPos = app.headPos;
	in.headDir = rotate(app.headRot, {0, 0, -1});
	in.helpOpen = app.helpOpen;
	in.compassBlank = app.split && app.charSheet;
	in.textBoxActive = (app.bandShown && app.level.textRect != 0) || app.cutscene || (app.dungeonMode && app.level.sceneBusy && !app.charSheet) || app.atlasLive;
	in.partyMarkOff = app.level.textRect != 0 || in.textBoxActive;
	in.gameModal = app.gameMenuOpen; // (le menu du jeu, la défaite : les fenêtres gardent l'image d'avant)
	if (!app.frame.rgba.empty()) {
		in.gameRgba = app.frame.rgba.data();
		in.gameW = app.frame.width;
		in.gameH = app.frame.height;
	}
	in.gameSerial = app.frameSerial;
	in.characters = app.inDungeon || app.cutscene ? app.characters : 0;
	for (int i = 0; i < in.characters && i < 4; i++)
		in.portraitX[i] = portraitX(in.characters, i);
	in.gearHint = !app.gearKnown && !app.chooserOpen;
	in.chooserOpen = app.chooserOpen;
	in.screenHidden = !app.screenShown && app.cinema <= 0.0f;
	in.curveRadius = app.cinema > 0.0f ? cinemaRadius(app) : 0.0f;
	in.screenLocked = false; // (l'écran du mode cinéma se déplace et se redimensionne aussi)
	in.cinemaOn = (app.cinemaWanted || app.cinema > 0.0f) && app.gameStarted;
	if (in.cinemaOn) {
		float dy = app.cinemaYaw - app.screenYaw;
		while (dy > 3.14159265f) dy -= 6.2831853f;
		while (dy < -3.14159265f) dy += 6.2831853f;
		in.cinemaYaw = dy;
		const Vec3 b = app.cinemaCenter - Vec3{0, app.dispH * 0.5f, 0} - app.anchorPos;
		in.cinemaBottom = atan2f(b.y, sqrtf(b.x * b.x + b.z * b.z));
	}
	in.time = now;
	app.ui.setInfo(app.imageInfo[0], app.imageInfo[1]);
	if (!app.ui.hasLayout() && app.screenPlaced)
		app.ui.resetLayout(in);
	// La fenêtre du choix de la vue s'ouvre droit devant le joueur.
	if (app.chooserOpen && !app.chooserPlaced && app.screenPlaced && app.ui.hasLayout()) {
		Vec3 f = rotate(app.headRot, {0, 0, -1});
		app.ui.placement[lolvr::kPanelHelp] = lolvr::Ui::defaultHelpPlacement(wrapAngle(atan2f(-f.x, -f.z) - app.anchorYaw));
		app.chooserPlaced = true;
	}
	app.ui.update(in, out, app.ss);
	placeBand(app, in, out);
	// Donjon 3D : le doigt qui ne vise ni l'écran ni une fenêtre peut viser le mur droit devant le
	// groupe (boutons, leviers, serrures, portes) ou le sol devant lui (objets à ramasser). Le clic
	// part dans le jeu, là où le jeu dessinerait ce point.
	app.aimOnUi = out.pointerValid || out.hitValid || out.dragging;
	if (in.aimValid && !out.pointerValid && !out.hitValid && !out.dragging && app.dungeonVisible) {
		float gu = 0, gv = 0;
		Vec3 hit, hr, hu;
		if (dungeonPick(app, in.tip, in.dir, gu, gv, hit, hr, hu)) {
			out.pointerValid = true;
			out.buttonsToGame = true;
			out.gu = gu;
			out.gv = gv;
			out.hitValid = true;
			out.hit = hit;
			out.hitRight = hr;
			out.hitUp = hu;
			out.hitRadius = 0.012f * length(hit - in.tip);
			out.pointerLong = false;
			showHeldItem(app, hit);
		}
	}

	if (cinemaPlaced(app)) {
		// Mode cinéma : l'écran courbe se déplace autour du joueur (distance, hauteur ; le côté pour
		// cette scène seulement) et se redimensionne ; distance, hauteur et taille sont gardées.
		if (out.screenDragging) {
			const Vec3 v = out.screenCenter - app.cinemaHead;
			const float hd = sqrtf(v.x * v.x + v.z * v.z);
			if (hd > 0.1f) {
				app.cinemaYaw = atan2f(-v.x, -v.z);
				app.cinemaR = clampf(hd, 0.6f, 4.0f);
				app.cinemaLift = clampf(out.screenCenter.y - (app.cinemaHead.y + cinemaEyeOffset(app)), -1.5f, 1.5f);
			}
			app.cinemaZoom = clampf(app.cinemaZoom * out.screenZoom, 0.4f, 2.5f);
			app.settingsDirty = true;
		}
		if (out.screenResizing) {
			app.cinemaZoom = clampf(app.cinemaZoom * out.screenZoom, 0.4f, 2.5f);
			app.settingsDirty = true;
		}
	} else {
		if (out.screenDragging)
			dragScreen(app, out.screenCenter - Vec3{0, app.viewLift, 0} - rotate(screenRot(app), {app.viewShift, 0, 0}), out.screenZoom);
		if (out.screenResizing)
			resizeScreen(app, out.screenZoom);
	}
	for (int i = 0; i < out.keyCount; i++)
		typeKey(app, out.keys[i], out.keyMods[i], now);
	// L'atlas du jeu ouvert par une flèche de la fenêtre d'atlas : on tourne la page demandée.
	if (app.atlasBrowse != 0) {
		if (app.automapOpen && now - app.atlasBrowseSince > 0.6) {
			typeKey(app, app.atlasBrowse > 0 ? RETROK_RIGHT : RETROK_LEFT, 0, now);
			app.atlasBrowse = 0;
		} else if (now - app.atlasBrowseSince > 4.0) {
			app.atlasBrowse = 0;
		}
	}
	if (out.invSlot >= 0)
		CoreHost::get().inventoryClick(out.invSlot);
	if (out.invSort)
		CoreHost::get().inventorySort();
	switch (out.action) {
	case lolvr::kUiActionToggleFire:
		app.fireOn = !app.fireOn;
		app.settingsDirty = true;
		break;
	case lolvr::kUiActionToggleHelp:
		app.helpOpen = !app.helpOpen;
		if (app.helpOpen) {
			// Les manettes s'ouvrent par-dessus l'écran du jeu (ou droit devant, l'écran caché),
			// un peu en avant de lui, à la même taille apparente qu'avant.
			const Vec3 target = app.screenShown ? viewCenter(app) : app.headPos + rotate(app.headRot, {0, 0, -1.5f});
			const Vec3 v = target - app.anchorPos;
			const float len = length(v) > 0.1f ? length(v) : 0.1f;
			const float dist = clampf(len - 0.35f, 0.7f, 1.05f);
			lolvr::UiPlacement pl = lolvr::Ui::defaultHelpPlacement(wrapAngle(atan2f(-v.x, -v.z) - app.anchorYaw));
			pl.b = asinf(clampf(v.y / len, -0.9f, 0.9f));
			pl.c = dist;
			pl.scale *= dist / 1.05f;
			app.ui.placement[lolvr::kPanelHelp] = pl;
		}
		break;
	case lolvr::kUiActionOpenKeyboard:
		app.keyboardOpen = true;
		app.keyboardAuto = false;
		break;
	case lolvr::kUiActionToggleAtlas:
		app.atlasOn = !app.atlasOn;
		if (!app.atlasOn && app.atlasLive)
			typeKey(app, RETROK_ESCAPE, 0, now); // (l'atlas du jeu ouvert dans la fenêtre : il se ferme aussi)
		break;
	case lolvr::kUiActionAtlasPrev:
	case lolvr::kUiActionAtlasNext:
		if (!app.automapOpen) {
			app.atlasBrowse = out.action == lolvr::kUiActionAtlasNext ? 1 : -1;
			app.atlasBrowseSince = now;
			typeKey(app, RETROK_m, 0, now);
		}
		break;
	case lolvr::kUiActionCloseHelp:
		app.helpOpen = false;
		break;
	case lolvr::kUiActionToggleLanguage:
		app.language = app.language == lolvr::kLangFrench ? lolvr::kLangEnglish : lolvr::kLangFrench;
		app.imagesCheckedAt = 0; // refaire les lignes d'information dans la nouvelle langue
		app.settingsDirty = true;
		break;
	case lolvr::kUiActionToggleDetach:
		// Le bouton Panneaux du clavier : grand écran ou fenêtres (depuis le PC 486 : les fenêtres)
		if (app.dungeonMode)
			app.detached = !app.detached;
		else
			setClassic(app, vrModeOf(app), classicViewOf(app) == 1 ? 0 : 1, time);
		app.settingsDirty = true;
		break;
	case lolvr::kUiActionToggleHud:
		// Mode immersif : tout regroupé, des fenêtres séparées, puis la barre du bas du jeu.
		app.hudMode = app.hudMode == 0 ? 2 : app.hudMode == 2 ? 1 : 0;
		app.ui.setHudMode(app.hudMode);
		app.settingsDirty = true;
		break;
	case lolvr::kUiActionCycleCinema:
		// Moyen, grand, petit, sans
		app.cinemaSize = (app.cinemaSize + 1) % 4;
		// (une taille choisie : l'écran revient à sa place et à sa taille d'origine)
		app.cinemaZoom = 1.0f;
		app.cinemaR = 1.5f;
		app.cinemaLift = 0.0f;
		app.ui.setCinema(app.cinemaSize, app.cinemaIntro);
		app.settingsDirty = true;
		break;
	case lolvr::kUiActionToggleCinemaIntro:
		app.cinemaIntro = !app.cinemaIntro;
		app.ui.setCinema(app.cinemaSize, app.cinemaIntro);
		app.settingsDirty = true;
		break;
	case lolvr::kUiActionCycleView:
		setClassic(app, vrModeOf(app), (classicViewOf(app) + 1) % 3, time);
		break;
	case lolvr::kUiActionResetLayout:
		app.ui.resetLayout(in);
		app.settingsDirty = true;
		break;
	case lolvr::kUiActionCloseKeyboard:
		app.keyboardOpen = false;
		app.keyboardAuto = false;
		break;
	case lolvr::kUiActionToggleDungeon: {
		// Le mode VR suivant : immersif, classique, réalité mixte (si le casque la permet).
		const int mode = vrModeOf(app);
		if (mode == kVrImmersive)
			setClassic(app, kVrClassic, classicViewOf(app), time);
		else if (mode == kVrClassic && app.passthroughSupported)
			setClassic(app, kVrMixed, classicViewOf(app), time);
		else
			setViewMode(app, 1, time);
		break;
	}
	case lolvr::kUiActionChooseClassic:
	case lolvr::kUiActionChooseImmersive:
		if (out.action == lolvr::kUiActionChooseImmersive)
			setViewMode(app, 1, time);
		else
			setClassic(app, kVrClassic, classicViewOf(app), time);
		app.viewChosen = true;
		app.chooserOpen = false;
		pulse(app, kRight, 0.3f, 0.03f);
		break;
	case lolvr::kUiActionChooseLanguage:
		// La langue, dans la fenêtre du premier lancement : l'application, et le jeu avec elle.
		if (out.language >= 0 && out.language <= 2) {
			app.language = out.language;
			app.langRequested = out.language;
			app.langRequestAt = now;
			if (app.coreStarted)
				CoreHost::get().setGameLanguage(out.language);
			app.settingsDirty = true;
		}
		pulse(app, kRight, 0.2f, 0.02f);
		break;
	case lolvr::kUiActionGameMenu:
		// La disquette du poignet : le bouton des options du jeu (11..32, 179..199), ou referme le menu.
		if (app.gameMenuOpen) {
			typeKey(app, RETROK_ESCAPE, 0, now);
		} else if (app.gameStarted && app.inDungeon && app.level.sceneBusy) {
			// Une boutique, un dialogue : le menu du jeu ne s'y ouvre pas. Une sauvegarde rapide
			// (emplacement 990, « VR - quick save ») ; la charger ramène devant la boutique.
			CoreHost::get().quickSave(990);
			app.quickSaveAsked = now;
		} else if (app.gameStarted && (app.inDungeon || app.charSheet)) {
			app.synthU = 21.5f / 320.0f;
			app.synthV = 189.0f / 200.0f;
			app.synthClickUntil = now + 0.16;
		}
		pulse(app, kLeft, 0.3f, 0.03f);
		break;
	case lolvr::kUiActionToggleKeyboard:
		app.keyboardOpen = !app.keyboardOpen;
		app.keyboardAuto = false;
		break;
	default:
		break;
	}
	if (out.gearMenuOpened && !app.gearKnown) {
		app.gearKnown = true;
		app.settingsDirty = true;
	}
	// Invitation du premier lancement : une petite vibration de la main gauche de temps en temps.
	if (!app.gearKnown && app.screenPlaced && now >= app.gearNudgeAt) {
		if (app.gearNudgeAt > 0)
			pulse(app, kLeft, 0.35f, 0.06f);
		app.gearNudgeAt = now + 8.0;
	}
	if (out.layoutChanged)
		app.settingsDirty = true;
	if (out.grabbed)
		pulse(app, hand, 0.45f, 0.03f);
	if (out.released)
		pulse(app, hand, 0.25f, 0.02f);
	if (out.clicked)
		pulse(app, hand, 0.35f, 0.02f);
	app.hitValid = out.hitValid;
	app.hitPoint = out.hit;
	app.hitRight = out.hitRight;
	app.hitUp = out.hitUp;
	app.hitRadius = out.hitRadius;
	app.pointerLong = out.pointerLong;
}

// ---------------------------------------------------------------------------
// Manettes

void updateInput(App &app, XrTime time, float dt, double now) {
	XrActiveActionSet active{app.actionSet, XR_NULL_PATH};
	XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};
	sync.countActiveActionSets = 1;
	sync.activeActionSets = &active;
	xrSyncActions(app.session, &sync);

	// Les boutons changeables : ce que fait chacun selon le profil de manettes (Réglages VR,
	// Manettes et boutons). fnDown[f] : un bouton qui fait f est enfoncé.
	bool fnDown[lolvr::kFnCount] = {false};
	{
		const uint8_t *map = app.ui.buttonMap();
		// (les poignées, des deux mains ; « personnage suivant » : voir plus bas, gauche et droite)
		const bool slot[lolvr::kSlotCount] = {getBool(app, app.aAction, -1), getBool(app, app.bAction, -1), getBool(app, app.xAction, -1),
		                                      getBool(app, app.yAction, -1), app.squeezeDown[kLeft] || app.squeezeDown[kRight],
		                                      getBool(app, app.stickClickAction, kLeft), getBool(app, app.stickClickAction, kRight)};
		for (int i = 0; i < lolvr::kSlotCount; i++)
			if (slot[i] && map[i] < lolvr::kFnCount && !(i == lolvr::kSlotGrip && map[i] == lolvr::kFnNextChar))
				fnDown[map[i]] = true;
	}
	bool fnPressed[lolvr::kFnCount];
	for (int i = 0; i < lolvr::kFnCount; i++) {
		fnPressed[i] = fnDown[i] && !app.fnWasDown[i];
		app.fnWasDown[i] = fnDown[i];
	}
	// Recentrer : l'écran (et la pièce) devant soi.
	const bool r3Raw = fnDown[lolvr::kFnRecenter];
	bool recenter = r3Raw;
	if (app.headValid && recenter && !app.recenterDown) {
		updateFloor(app, time);
		placeScreen(app, true);
		app.dunYaw = app.anchorYaw; // le groupe regarde de nouveau droit devant
		pulse(app, kRight, 0.3f, 0.03f);
	}
	app.recenterDown = r3Raw;

	// Position des manettes (les gants les suivent)
	for (int h = 0; h < 2; h++) {
		XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
		app.aimValid[h] = XR_SUCCEEDED(xrLocateSpace(app.aimSpaces[h], app.localSpace, time, &loc)) &&
		                  (loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) &&
		                  (loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT);
		if (app.aimValid[h])
			app.aimPose[h] = loc.pose;
	}

	// Gâchette = clic gauche ; la main qui clique devient le pointeur. Les poignées : ce que dit le
	// profil (« Classique » : le personnage d'avant à gauche, le suivant à droite).
	bool clicked[2] = {false, false};
	bool gripPressed[2] = {false, false};
	for (int h = 0; h < 2; h++) {
		app.triggerValue[h] = getFloat(app, app.triggerAction, h);
		app.squeezeValue[h] = getFloat(app, app.squeezeAction, h);
		bool trigger = threshold(app.triggerDown[h], app.triggerValue[h]);
		bool squeeze = threshold(app.squeezeDown[h], app.squeezeValue[h]);
		gripPressed[h] = squeeze && !app.squeezeDown[h];
		bool pressedNow = trigger && !app.triggerDown[h];
		int other = 1 - h;
		if (pressedNow && app.aimValid[h] && !app.triggerDown[other] && !app.squeezeDown[other])
			app.activeHand = h;
		clicked[h] = pressedNow;
		app.triggerDown[h] = trigger;
		app.squeezeDown[h] = squeeze;
	}
	if (!app.aimValid[app.activeHand] && app.aimValid[1 - app.activeHand])
		app.activeHand = 1 - app.activeHand;

	// Le pointeur part du bout de l'index du gant : panneaux, poignées, clavier ou écran.
	int hand = app.activeHand;
	XrVector2f left = getStick(app, kLeft);
	XrVector2f right = getStick(app, kRight);
	lolvr::UiFrameOutput ui;
	updateUi(app, time, dt, now, true, clicked[hand] && app.triggerDown[hand], right, ui);
	bool toGame = ui.buttonsToGame && !ui.dragging;
	if (ui.pokeHand >= 0)
		pulse(app, ui.pokeHand, 0.35f, 0.02f); // une touche du clavier touchée du doigt
	if (ui.restClick) {
		// Le repos (ZZZ) de la paume droite : un clic bref sur le bouton du jeu.
		app.synthU = 44.0f / 320.0f;
		app.synthV = 189.0f / 200.0f;
		app.synthClickUntil = now + 0.16;
		app.clickPulse = 1.0f;
		pulse(app, kRight, 0.30f, 0.02f);
	}
	if (ui.gameClick && ui.pointerValid) {
		// Un appui bref sur une fenêtre (la gâchette maintenue la déplace) : le clic, au relâcher.
		app.synthU = ui.gu;
		app.synthV = ui.gv;
		app.synthClickUntil = now + 0.16;
		app.clickPulse = 1.0f;
		pulse(app, hand, 0.28f, 0.018f);
	}
	// Le curseur du jeu : l'app le dessine elle-même sur l'écran ou la fenêtre visée, et nulle part
	// ailleurs (dans l'image du jeu, il débordait sur deux fenêtres voisines).
	{
		const bool own = app.level.cursorW > 0 && app.level.cursorRgba.size() >= (size_t)app.level.cursorW * app.level.cursorH * 4;
		CoreHost::get().setOwnCursor(own);
		// (mode cinéma : pas de flèche sur la scène, le pointeur suffit ; un objet tenu reste montré)
		const bool cinemaArrow = cinemaPlaced(app) && !app.level.itemInHand;
		if (own && ui.pointerValid && ui.hitPixel > 0.0f && CoreHost::get().cursorShown() && !cinemaArrow)
			drawGameCursor(app, ui.hit, ui.hitRight, ui.hitUp, ui.hitPixel);
	}
	// Un objet posé visé (sur la case du groupe ou celle devant), la main vide : le jeu le met dans la
	// main directement (sans chercher ses pixels dans la vue, de près comme de loin).
	if (clicked[hand] && toGame && app.dungeonVisible && !app.aimOnUi && app.pickItem > 0 && !app.level.itemInHand && app.inDungeon) {
		CoreHost::get().pickupItem(app.pickItem);
		app.holdTrigger = true; // (pas de clic dans le jeu pour cette pression)
	}
	// Un mur de côté visé en 3D : se tourner vers lui, puis cliquer (voir pendingFace).
	if (clicked[hand] && toGame && app.dungeonVisible && !app.aimOnUi && app.pickFace >= 0 && app.inDungeon &&
	    app.pickFace != (app.level.direction & 3) && app.alignTurn == 0) {
		const int pd = app.level.direction & 3, diff = (app.pickFace - pd) & 3;
		if (diff == 1 || diff == 3) {
			const bool leftTurn = diff == 3;
			typeKey(app, leftTurn ? RETROK_HOME : RETROK_PAGEUP, 0, now);
			app.alignTurn = leftTurn ? 1 : -1;
			app.alignExpect = app.pickFace;
			app.alignUntil = now + 1.0;
			app.moveBlockUntil = now + 0.30;
			app.pendingFace = app.pickFace;
			app.pendingFrom = pd;
			app.pendingU = ui.gu;
			app.pendingV = ui.gv;
			app.pendingUntil = now + 1.5;
			app.holdTrigger = true;
		}
	}
	if (app.holdTrigger) {
		if (!app.triggerDown[hand])
			app.holdTrigger = false;
		toGame = false;
	}
	if (app.pendingFace >= 0) {
		if (now > app.pendingUntil) {
			app.pendingFace = -1;
		} else if ((app.level.direction & 3) == app.pendingFace && app.alignTurn == 0 && now >= app.synthClickUntil) {
			app.synthU = app.pendingU;
			app.synthV = app.pendingV;
			app.synthClickUntil = now + 0.16;
			app.pendingFace = -1;
			// Puis se retourner : sans ça, le groupe restait tourné vers ce mur, le monde immobile,
			// et « devant » n'était plus devant le joueur (marcher menait au mur ; après un repos
			// réveillé par un tel clic, par exemple) jusqu'au recentrage.
			app.returnFace = app.pendingFrom;
			app.returnAt = now + 0.45;
		}
	}
	if (app.returnFace >= 0) {
		const int pd = app.level.direction & 3, diff = (app.returnFace - pd) & 3;
		if (diff == 0 || now > app.returnAt + 3.0) {
			app.returnFace = -1;
		} else if (now >= app.returnAt && app.alignTurn == 0 && !app.level.sceneBusy && (diff == 1 || diff == 3)) {
			const bool leftTurn = diff == 3;
			typeKey(app, leftTurn ? RETROK_HOME : RETROK_PAGEUP, 0, now);
			app.alignTurn = leftTurn ? 1 : -1;
			app.alignExpect = app.returnFace;
			app.alignUntil = now + 1.0;
			app.moveBlockUntil = now + 0.30;
			app.returnFace = -1;
		}
	}
	if (now < app.synthClickUntil) {
		// Bouton de magie : un clic bref sur le bouton du personnage, à la place du pointeur.
		CoreHost::get().setPointer(app.synthU, app.synthV);
		CoreHost::get().setMouseButtons(app.synthClickUntil - now > 0.06, false);
	} else {
		if (ui.pointerValid)
			CoreHost::get().setPointer(ui.gu, ui.gv);
		// Clic droit (boire une potion, utiliser un objet) : les boutons du profil qui le font
		// (dans « Classique », X).
		bool xRight = fnDown[lolvr::kFnRightClick] && ui.pointerValid && !ui.dragging && !(app.inDungeon && app.characters > 0 && app.level.magicMenu >= 0);
		CoreHost::get().setMouseButtons(app.triggerDown[hand] && toGame && !ui.pressPending, xRight);
		if (xRight && !app.keyboardButtonDown) {
			app.clickPulse = 1.0f;
			pulse(app, hand, 0.28f, 0.018f);
		}
		app.keyboardButtonDown = xRight;
	}
	if (clicked[hand] && toGame) {
		app.clickPulse = 1.0f;
		pulse(app, hand, 0.28f, 0.018f);
	}
	float w = screenWidth(app);

	// Joysticks : gauche = avancer/reculer/pas de côté, droit = tourner.
	bool zoomMode = fnDown[lolvr::kFnView];
	bool stickBusy = zoomMode || ui.dragging; // le joystick droit règle l'écran ou un panneau
	// Une seule direction à la fois : celle où le joystick penche le plus. La direction tenue garde
	// l'avantage, pour qu'un pas un peu en biais ne change pas de direction en route.
	{
		float ax = fabsf(left.x), ay = fabsf(left.y);
		float m = ax > ay ? ax : ay;
		bool pushed = m > (app.moveDir ? 0.35f : 0.55f);
		int dir = 0; // 1 avant, 2 arrière, 3 gauche, 4 droite (par rapport au groupe)
		// Donjon 3D : on marche vers là où l'on regarde. Le joystick indique une direction par rapport
		// au regard ; on prend la direction du donjon (nord, est, sud, ouest) la plus proche, et on
		// envoie au jeu le pas correspondant (avant, arrière ou de côté) selon l'orientation du groupe.
		const bool gaze3d = app.dungeonVisible && app.headValid;
		const int pdir = app.level.direction & 3;
		float gaze = 0.0f;
		// (le joystick suit la direction du groupe, pas la tête ; le groupe ne se tourne plus de
		// lui-même vers le regard)
		if (gaze3d)
			gaze = app.dunYaw;
		if (!pushed) {
			app.moveAbs = -1;
		} else if (gaze3d) {
			float want = gaze + atan2f(-left.x, left.y); // angle positif : vers la gauche
			auto roomYawOf = [&](int d) { return app.dunYaw - (float)((d - pdir) & 3) * 1.5707963f; };
			if (app.moveAbs < 0 || fabsf(wrapAngle(want - roomYawOf(app.moveAbs))) > 1.05f) {
				int r = (int)lroundf(wrapAngle(want - app.dunYaw) / 1.5707963f); // +1 : à gauche du groupe
				app.moveAbs = (pdir - r) & 3;
			}
			int rel = (app.moveAbs - pdir) & 3; // 0 devant, 1 à droite, 2 derrière, 3 à gauche
			dir = rel == 0 ? 1 : rel == 1 ? 4 : rel == 2 ? 2 : 3;
		} else {
			bool horizontal = ax > ay;
			if (app.moveDir == 1 || app.moveDir == 2)
				horizontal = ax > ay * 1.5f;
			else if (app.moveDir == 3 || app.moveDir == 4)
				horizontal = ax * 1.5f > ay;
			dir = horizontal ? (left.x < 0 ? 3 : 4) : (left.y > 0 ? 1 : 2);
		}
		// À l'arrêt, le groupe se tourne de lui-même vers l'endroit que le joueur regarde (sans que la
		// pièce bouge) : les clics, les attaques et le mur « devant » sont ainsi ceux qu'il voit. Pas
		// quand il vise une fenêtre ou l'écran (il consulte l'inventaire, il ne se détourne pas).
		bool canAlign = gaze3d && !pushed && !app.aimOnUi && app.alignTurn == 0 && !app.level.sceneBusy && !app.charSheet && !app.automapOpen &&
		                app.turnFade <= 0.0f && !stickBusy && fabsf(right.x) < 0.3f;
		float off = gaze3d ? wrapAngle(gaze - app.dunYaw) : 0.0f;
		if (canAlign && fabsf(off) > 0.87f) {
			if (app.gazeOffSince <= 0.0)
				app.gazeOffSince = now;
			if (now - app.gazeOffSince > 0.35) {
				bool leftTurn = off > 0.0f;
				typeKey(app, leftTurn ? RETROK_HOME : RETROK_PAGEUP, 0, now);
				app.alignTurn = leftTurn ? 1 : -1;
				app.alignExpect = (pdir + (leftTurn ? 3 : 1)) & 3;
				app.alignUntil = now + 1.0;
				app.moveBlockUntil = now + 0.30;
				app.gazeOffSince = 0.0;
			}
		} else {
			app.gazeOffSince = 0.0;
		}
		app.moveDir = dir;
		bool moveOk = now >= app.moveBlockUntil && app.alignTurn == 0;
		setKey(app.kUp, dir == 1 && moveOk, RETROK_UP);
		setKey(app.kDown, dir == 2 && moveOk, RETROK_DOWN);
		setKey(app.kSlideL, dir == 3 && moveOk, RETROK_LEFT);
		setKey(app.kSlideR, dir == 4 && moveOk, RETROK_RIGHT);
	}
	setKey(app.kTurnL, !stickBusy && threshold(app.kTurnL.down, -right.x), RETROK_HOME);
	setKey(app.kTurnR, !stickBusy && threshold(app.kTurnR.down, right.x), RETROK_PAGEUP);

	// Clic du joystick gauche :
	//   - maintenu + joystick droit haut/bas : taille de l'écran ; gauche/droite : distance ;
	//   - simple clic bref : décor suivant (bibliothèque, réalité mixte, moniteur cathodique).
	if (zoomMode && !app.zoomDown) {
		app.zoomSince = now;
		app.zoomUsed = false;
	}
	if (zoomMode && cinemaPlaced(app)) {
		// Mode cinéma : haut/bas la taille, gauche/droite la distance (gardées).
		if (fabsf(right.y) > 0.3f && fabsf(right.y) >= fabsf(right.x)) {
			app.cinemaZoom = clampf(app.cinemaZoom * (1.0f + right.y * 0.8f * dt), 0.4f, 2.5f);
			app.zoomUsed = true;
			app.settingsDirty = true;
		} else if (fabsf(right.x) > 0.3f) {
			app.cinemaR = clampf(app.cinemaR * (1.0f + right.x * 0.6f * dt), 0.6f, 4.0f);
			app.zoomUsed = true;
			app.settingsDirty = true;
		}
	} else if (zoomMode) {
		const Limits &lim = kLimits[app.environment];
		if (fabsf(right.y) > 0.3f && fabsf(right.y) >= fabsf(right.x)) {
			shownWidth(app) = clampf(shownWidth(app) * (1.0f + right.y * 0.8f * dt), lim.wMin, lim.wMax); // (un menu seul : sa taille à lui)
			app.zoomUsed = true;
		} else if (fabsf(right.x) > 0.3f) {
			screenDistance(app) = clampf(screenDistance(app) * (1.0f + right.x * 0.6f * dt), lim.dMin, lim.dMax);
			app.zoomUsed = true;
		}
		if (app.zoomUsed)
			placeScreen(app, false);
	} else if (app.zoomDown) {
		if (!app.zoomUsed && now - app.zoomSince < 0.6 && !app.dungeonMode) { // le mode immersif n'a qu'une vue
			setClassic(app, vrModeOf(app), (classicViewOf(app) + 1) % 3, time); // la vue suivante
			pulse(app, kLeft, 0.3f, 0.03f);
		} else if (!app.zoomUsed && now - app.zoomSince < 0.6 && app.dungeonMode && app.inDungeon) {
			// En immersif, L3 : le sort suivant ; menu des niveaux ouvert, le niveau suivant.
			if (app.level.magicMenu >= 0) {
				const int n = app.level.magicLevels > 0 ? app.level.magicLevels : 1;
				app.spellLevel = (app.spellLevel + 1) % n;
			} else {
				CoreHost::get().nextSpell();
			}
			pulse(app, kLeft, 0.25f, 0.02f);
		}
		app.settingsDirty = true;
	}
	app.zoomDown = zoomMode;

	// Boutons
	// A et B : dans le donjon, attaque et magie du personnage choisi (joystick droit haut/bas pour
	// en changer) ; partout ailleurs (menus, cinématiques, carte, fiche), Entrée et Échap.
	bool aDown = fnDown[lolvr::kFnAttack], bDown = fnDown[lolvr::kFnMagic];
	bool combat = app.inDungeon && app.characters > 0;
	if (combat) {
		if (aDown && !app.attackDown) {
			typeKey(app, (unsigned)RETROK_F1 + (unsigned)app.selectedChar, 0, now);
			pulse(app, kRight, 0.35f, 0.03f);
		}
		const int menu = app.level.magicMenu;
		if (menu != app.spellMenuChar) {
			// Le menu vient de s'ouvrir (ou de se fermer) : niveau 1 en surbrillance
			app.spellMenuChar = menu;
			app.spellLevel = 0;
		}
		if (bDown && !app.magicDown) {
			if (menu >= 0 && menu < app.characters) {
				// 2e appui : lancer le niveau en surbrillance
				app.synthU = ((float)portraitX(app.characters, menu) + 44.0f + 11.0f) / 320.0f;
				app.synthV = (144.0f + 8.0f * (float)app.spellLevel + 4.0f) / 200.0f;
			} else {
				// 1er appui : ouvrir le menu des niveaux du sort choisi
				app.synthU = ((float)portraitX(app.characters, app.selectedChar) + 44.0f + 11.0f) / 320.0f;
				app.synthV = (160.0f + 8.5f) / 200.0f;
			}
			app.synthClickUntil = now + 0.16;
			pulse(app, kRight, 0.25f, 0.03f);
		}
		// Le bouton du clic droit (X dans « Classique »), menu des niveaux ouvert : le niveau suivant
		// parmi ceux que le personnage peut lancer (et pas de clic droit).
		const bool lv = fnDown[lolvr::kFnRightClick];
		if (lv && !app.r3Down && menu >= 0) {
			const int n = app.level.magicLevels > 0 ? app.level.magicLevels : 1;
			app.spellLevel = (app.spellLevel + 1) % n;
			pulse(app, kRight, 0.20f, 0.02f);
		}
		app.r3Down = lv;
		// Changer de personnage : poignée gauche, celui d'avant (vers le haut) ; poignée droite, le
		// suivant (vers le bas) ; ou un bouton qui le fait dans le profil. Le joystick droit vers le
		// haut ou le bas ne le fait plus (il partait en plein combat).
		const bool gripsChar = app.ui.buttonMap()[lolvr::kSlotGrip] == lolvr::kFnNextChar;
		const bool prev = gripsChar && gripPressed[kLeft], next = (gripsChar && gripPressed[kRight]) || fnPressed[lolvr::kFnNextChar];
		if (prev != next) {
			app.selectedChar = (app.selectedChar + (next ? 1 : app.characters - 1)) % app.characters;
			pulse(app, prev ? kLeft : kRight, 0.30f, 0.02f);
		}
		app.charFlick = false;
	} else {
		app.charFlick = false;
	}
	app.attackDown = aDown;
	app.magicDown = bDown;
	setKey(app.kEnter, (aDown && !combat) || fnDown[lolvr::kFnEnter], RETROK_RETURN);
	setKey(app.kEsc, (bDown && !combat) || fnDown[lolvr::kFnEscape], RETROK_ESCAPE);
	// La carte (bouton « Carte » ou « Fenêtre de l'atlas ») : la fenêtre de l'atlas, pas la page plein écran.
	if (fnPressed[lolvr::kFnMap] || fnPressed[lolvr::kFnAtlas]) {
		if (app.atlasMap.available)
			app.atlasOn = !app.atlasOn;
		else
			app.atlasOn = false;
	if (!app.atlasOn && app.atlasLive)
			typeKey(app, RETROK_ESCAPE, 0, now);
	}
	setKey(app.kMap, false, RETROK_m);
	if (fnPressed[lolvr::kFnKeyboard]) {
		app.keyboardOpen = !app.keyboardOpen;
		app.keyboardAuto = false;
	}
	// X = clic droit (voir plus haut) ; le clavier s'ouvre depuis le menu du poignet. Bouton menu = menu ScummVM.
	CoreHost::get().setSpecialButtons(false, getBool(app, app.menuAction, -1));
	releaseTypedKeys(app, now, false);
}

// ---------------------------------------------------------------------------
// Rendu

void uploadFrame(App &app) {
	if (!app.frameDirty || app.frame.width <= 0 || app.frame.height <= 0)
		return;
	app.frameDirty = false;
	glBindTexture(GL_TEXTURE_2D, app.srcTex);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	if (app.srcTexW != app.frame.width || app.srcTexH != app.frame.height) {
		app.srcTexW = app.frame.width;
		app.srcTexH = app.frame.height;
		glTexImage2D(GL_TEXTURE_2D, 0, app.srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, app.srcTexW, app.srcTexH, 0,
		             GL_RGBA, GL_UNSIGNED_BYTE, app.frame.rgba.data());
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	} else {
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, app.srcTexW, app.srcTexH, GL_RGBA, GL_UNSIGNED_BYTE, app.frame.rgba.data());
	}
	glBindTexture(GL_TEXTURE_2D, 0);
	// Mode cinéma : la copie de l'image pour l'écran courbe, tant que la scène dure.
	const bool cineNow = app.cinemaLive && (!app.gameStarted || (app.level.sceneCinema && app.level.scenePicture && (app.level.sceneBusy || app.cutscene)));
	if (cineNow) {
		if (!app.cinemaTex)
			glGenTextures(1, &app.cinemaTex);
		glBindTexture(GL_TEXTURE_2D, app.cinemaTex);
		if (app.cinemaTexW != app.frame.width || app.cinemaTexH != app.frame.height) {
			app.cinemaTexW = app.frame.width;
			app.cinemaTexH = app.frame.height;
			glTexImage2D(GL_TEXTURE_2D, 0, app.srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, app.cinemaTexW, app.cinemaTexH, 0, GL_RGBA, GL_UNSIGNED_BYTE, app.frame.rgba.data());
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		} else {
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, app.cinemaTexW, app.cinemaTexH, GL_RGBA, GL_UNSIGNED_BYTE, app.frame.rgba.data());
		}
		glBindTexture(GL_TEXTURE_2D, 0);
	}

	// Couleur moyenne de l'image : l'écran éclaire doucement les gants et la pièce.
	const uint8_t *px = app.frame.rgba.data();
	const size_t count = (size_t)app.frame.width * (size_t)app.frame.height;
	float sum[3] = {0, 0, 0};
	size_t n = 0;
	for (size_t i = 0; i < count; i += 53, n++)
		for (int k = 0; k < 3; k++) {
			float c = (float)px[i * 4 + k] * (1.0f / 255.0f);
			sum[k] += c * c; // approximation de la conversion sRGB -> linéaire
		}
	if (n > 0) {
		Vec3 avg{sum[0] / (float)n, sum[1] / (float)n, sum[2] / (float)n};
		app.screenGlow = lolvr::lerp(app.screenGlow, avg, 0.25f);
	}
	// Mode cinéma : la couleur de chaque bord de la partie montrée (une bande de 6 pixels), pour le
	// halo autour de l'écran courbe. Lissée, pour qu'il ne papillote pas.
	if (cineNow) {
		const int W = app.frame.width, Hh = app.frame.height;
		const float sx = (float)W / 320.0f, sy = (float)Hh / 200.0f;
		const int x0 = (int)(app.cropX * sx), y0 = (int)(app.cropY * sy);
		const int x1 = std::min(W, (int)((app.cropX + app.cropW) * sx)), y1 = std::min(Hh, (int)((app.cropY + app.cropH) * sy));
		const int bw = std::max(1, (int)(6 * sx)), bh = std::max(1, (int)(6 * sy));
		const int boxes[4][4] = {{x0, y0, x0 + bw, y1}, {x1 - bw, y0, x1, y1}, {x0, y0, x1, y0 + bh}, {x0, y1 - bh, x1, y1}};
		for (int k = 0; k < 4; k++) {
			float acc[3] = {0, 0, 0};
			int cnt = 0;
			for (int y = std::max(0, boxes[k][1]); y < std::min(Hh, boxes[k][3]); y += 2)
				for (int x = std::max(0, boxes[k][0]); x < std::min(W, boxes[k][2]); x += 2, cnt++)
					for (int c = 0; c < 3; c++) {
						const float v = (float)px[((size_t)y * W + x) * 4 + c] * (1.0f / 255.0f);
						acc[c] += v * v;
					}
			if (cnt > 0)
				app.cinemaEdge[k] = lolvr::lerp(app.cinemaEdge[k], Vec3{acc[0] / cnt, acc[1] / cnt, acc[2] / cnt}, 0.2f);
		}
	}
}

bool acquireImage(Swapchain &sc, uint32_t &index) {
	XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
	if (XR_FAILED(xrAcquireSwapchainImage(sc.handle, &acquire, &index)))
		return false;
	XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
	wait.timeout = XR_INFINITE_DURATION;
	if (XR_FAILED(xrWaitSwapchainImage(sc.handle, &wait))) {
		XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
		xrReleaseSwapchainImage(sc.handle, &release);
		return false;
	}
	return true;
}

void releaseImage(Swapchain &sc) {
	XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
	xrReleaseSwapchainImage(sc.handle, &release);
}

bool renderQuad(App &app) {
	uint32_t index = 0;
	if (!acquireImage(app.quadSwapchain, index))
		return false;
	const int sw = app.quadSwapchain.width, sh = app.quadSwapchain.height;
	// L'image occupe le centre du calque, au format de ce qui est affiché, entourée d'une petite
	// marge noire cachée sous le cadre.
	float aspect = app.dispW / app.dispH;
	int vh = kQuadImageH, vw = (int)((float)vh * aspect + 0.5f);
	if (vw > kQuadImageW) {
		vw = kQuadImageW;
		vh = (int)((float)vw / aspect + 0.5f);
	}
	int vx = kQuadMarginX, vy = kQuadMarginY;
	app.quadRectW = vw + 2 * kQuadMarginX;
	app.quadRectH = vh + 2 * kQuadMarginY;
	app.quadScaleX = (float)app.quadRectW / (float)vw;
	app.quadScaleY = (float)app.quadRectH / (float)vh;
	(void)sh;
	glBindFramebuffer(GL_FRAMEBUFFER, app.fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, app.quadSwapchain.images[index].image, 0);
	glViewport(0, 0, sw, app.quadSwapchain.height);
	glClearColor(0, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT);
	if (app.srcTexW > 0) {
		glViewport(vx, vy, vw, vh);
		glUseProgram(app.blitProgram);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, app.srcTex);
		glUniform2f(app.blitTexSize, (float)app.srcTexW, (float)app.srcTexH);
		glUniform2f(app.blitOutSize, (float)vw, (float)vh);
		float sx = (float)app.srcTexW / 320.0f, sy = (float)app.srcTexH / 200.0f;
		// Avec la ligne de messages collée dessous : la scène en haut, la ligne en bas.
		int bandPx = app.bandH > 0.0f ? (int)((float)vh * app.bandH / (app.cropH + app.bandH) + 0.5f) : 0;
		glViewport(vx, vy + bandPx, vw, vh - bandPx);
		glUniform2f(app.blitOutSize, (float)vw, (float)(vh - bandPx));
		glUniform4f(app.blitRect, app.cropX * sx, app.cropY * sy, app.cropW * sx, app.cropH * sy);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		if (bandPx > 0) {
			glViewport(vx, vy, vw, bandPx);
			glUniform2f(app.blitOutSize, (float)vw, (float)bandPx);
			glUniform4f(app.blitRect, app.bandRect[0] * sx, app.bandRect[1] * sy, app.bandRect[2] * sx, app.bandRect[3] * sy);
			glDrawArrays(GL_TRIANGLES, 0, 3);
		}
		glBindTexture(GL_TEXTURE_2D, 0);
	}
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	releaseImage(app.quadSwapchain);
	return true;
}

// Les régions à ciel ouvert : pas de plafond, le ciel.
bool isOutdoorTileset(const char *t) {
	return !strcmp(t, "FOREST1") || !strcmp(t, "SWAMP") || !strcmp(t, "YVEL1");
}

// Le donjon en 3D : suit le niveau publié par le moteur, reconstruit les murs quand ils changent
// (portes) et fait glisser le groupe d'une case à l'autre.
int findFlight(App &app, int id) {
	for (int i = 0; i < 8; i++)
		if (app.flights[i].id == id && app.clock - app.flights[i].last < 1.0f)
			return i;
	return -1;
}

// Le vol d'un objet : la position où le jeu vient de le mettre devient la nouvelle cible ; on part
// de là où on le montre, et on y arrive au rythme des pas du jeu.
int flightFor(App &app, int id, float x, float z) {
	const float now = app.clock;
	int slot = -1, freeSlot = -1;
	for (int i = 0; i < 8; i++) {
		App::Flight &f = app.flights[i];
		if (f.id == id && now - f.last < 1.0f)
			slot = i;
		else if (freeSlot < 0 && (f.id < 0 || now - f.last >= 1.0f))
			freeSlot = i;
	}
	if (slot < 0) {
		if (freeSlot < 0)
			return -1;
		App::Flight &f = app.flights[freeSlot];
		f.id = id;
		f.fx = f.tx = x;
		f.fz = f.tz = z;
		f.t0 = f.last = now;
		f.interval = 0.1f;
		return freeSlot;
	}
	App::Flight &f = app.flights[slot];
	if (x != f.tx || z != f.tz) {
		const float k = f.interval > 0.0f ? (now - f.t0) / f.interval : 1.0f;
		const float c = k < 1.0f ? k : 1.0f;
		f.fx = f.fx + (f.tx - f.fx) * c;
		f.fz = f.fz + (f.tz - f.fz) * c;
		const float gap = now - f.t0;
		f.interval = 0.5f * f.interval + 0.5f * (gap < 0.03f ? 0.03f : gap > 0.3f ? 0.3f : gap);
		f.tx = x;
		f.tz = z;
		f.t0 = now;
	}
	f.last = now;
	return slot;
}

// Pose les objets en vol à leur position du moment, entre deux pas du jeu.
void placeFlights(App &app) {
	for (size_t i = 0; i < app.spriteList.size() && i < 32; i++) {
		const int fl = app.flightOf[i];
		if (fl < 0)
			continue;
		const App::Flight &f = app.flights[fl];
		const float k = f.interval > 0.0f ? (app.clock - f.t0) / f.interval : 1.0f;
		const float c = k < 0.0f ? 0.0f : k > 1.0f ? 1.0f : k;
		app.spriteList[i].x = f.fx + (f.tx - f.fx) * c;
		app.spriteList[i].z = f.fz + (f.tz - f.fz) * c;
	}
	{
		static const int rdx[4] = {0, 1, 0, -1}, rdz[4] = {-1, 0, 1, 0};
		const int d = app.level.direction & 3;
		app.dungeon.setPartyRight((float)-rdz[d], (float)rdx[d]);
	}
	app.dungeon.setSprites(app.spriteList);
}

void updateDungeon(App &app, float dt) {
	app.clock += dt;
	app.dungeonVisible = false;
	if (!app.coreStarted || !app.gameStarted)
		return;
	// Le niveau et le curseur du jeu, dans toutes les vues : en vue classique aussi, l'appli dessine
	// le curseur elle-même (l'objet tenu), il doit suivre celui du jeu.
	lolvr::LevelSnapshot &l = app.level;
	const bool gotLevel = CoreHost::get().levelSnapshot(l);
	// Un sort de plus (le jeu l'écrit sur le parchemin, avec son animation ; le premier fait apparaître
	// le parchemin) : le livre de sorts s'ouvre. Pas au chargement d'une partie (le niveau change).
	if (gotLevel && l.level > 0) {
		if (app.spellsSeen >= 0 && l.spellCount > app.spellsSeen && l.level == app.spellsLevel && app.ui.spellsCollapsed()) {
			app.ui.setSpellsCollapsed(false);
			app.settingsDirty = true;
		}
		app.spellsSeen = l.spellCount;
		app.spellsLevel = l.level;
	}
	if (!app.dungeonOk || !app.dungeonMode)
		return;
	if (!gotLevel || l.level <= 0 || l.vmp.empty())
		return;
	lolvr::DungeonGfx g;
	g.vmpMap = l.vmpMap;
	g.wallFlags = l.wallFlags;
	g.vmp = l.vmp.data();
	g.vmpLen = (int)l.vmp.size();
	g.vcn = l.vcn.data();
	g.vcnShift = l.vcnShift.data();
	g.vcnCount = (int)(l.vcn.size() / 32);
	g.colTable = l.colTable;
	g.palette = l.palette;
	if (l.wallCaptureSerial == l.gfxSerial && l.wallSlots > 0) {
		g.wallSlot = l.wallSlot;
		g.wallPixels = l.wallPixels.data();
		g.wallAlpha = l.wallAlpha.data();
		g.wallSlots = l.wallSlots;
		g.screenPalette = l.screenPalette;
		if (!l.doorPixels.empty() && !l.doorAlpha.empty()) {
			g.doorPixels = l.doorPixels.data();
			g.doorAlpha = l.doorAlpha.data();
			g.doorRaise = l.doorRaise;
		}
	}
	g.outdoor = isOutdoorTileset(app.tileset);
	g.ceilingStyle = lolvr::ceilingStyleOf(app.tileset);
	// Un nouveau décor (sortie du château, arrivée devant le garde) : les murs tels que le jeu les
	// dessine arrivent un instant après ses graphismes. On attend ces dessins, la pièce dans le noir,
	// plutôt que de montrer une fraction de seconde le décor tiré des seuls graphismes, puis l'autre.
	// (le même niveau rechargé — retour d'une boutique, d'un lieu — : l'ancien décor est le bon, on le
	// garde allumé ; seul un vrai changement de région attend dans le noir)
	const bool sameScene = l.level == app.builtLevelNo && !strcmp(app.tileset, app.builtTileset);
	if (l.gfxSerial != app.builtGfx && l.wallCaptureSerial != l.gfxSerial && !sameScene) {
		if (app.gfxWaitSerial != l.gfxSerial) {
			app.gfxWaitSerial = l.gfxSerial;
			app.gfxWaitSince = app.clock;
		}
		if (app.clock - app.gfxWaitSince < 1.0) {
			app.dungeonLight = 0.0f;
			app.levelFadeIn = 0.0f;
			app.gfxWaitAt = app.clock;
			app.dungeonVisible = app.dungeon.hasContent(); // (l'ancien décor, éteint)
			return;
		}
	}
	const bool gfxChanged = l.gfxSerial != app.builtGfx || l.wallCaptureSerial != app.builtCapture;
	if (gfxChanged && app.shownLevel != 0 && !sameScene)
		app.levelFadeIn = 0.0f; // (le nouveau décor apparaît en fondu)
	// Une image de lieu de plus (le jeu vient de la montrer) : seulement les textures et le maillage.
	bool texChanged = gfxChanged || l.placeSerial != app.builtPlaces;
	if (texChanged) {
		app.builtCapture = l.wallCaptureSerial;
		app.builtPlaces = l.placeSerial;
		lolvr::decodeDungeonTextures(g, app.dunTex);
		for (size_t i = 0; i < l.placeTypes.size() && (i + 1) * 176 * 120 * 3 <= l.placeRgb.size(); i++)
			lolvr::applyPlacePicture(app.dunTex, l.placeTypes[i], &l.placeRgb[i * 176 * 120 * 3]);
		// Les mêmes textures qu'avant (le niveau rechargé à la sortie d'une boutique) : rien à
		// renvoyer à la carte graphique ni à refaire — c'était l'à-coup du retour.
		uint64_t h = 1469598103934665603ull ^ (uint64_t)app.dunTex.layers;
		for (size_t i = 0; i < app.dunTex.rgba.size(); i += 7)
			h = (h ^ app.dunTex.rgba[i]) * 1099511628211ull;
		for (int i = 0; i < 256; i++)
			h = (h ^ (uint64_t)(app.dunTex.typeLayer[i] + 1)) * 1099511628211ull;
		if (h == app.builtTexHash && app.builtLevelNo == l.level) {
			texChanged = false;
		} else {
			app.builtTexHash = h;
			app.dungeon.setTextures(app.dunTex);
			LOGI("donjon 3D : niveau %d, %d textures, %d lieux", l.level, app.dunTex.layers, (int)l.placeTypes.size());
		}
		app.builtGfx = l.gfxSerial;
		app.builtLevelNo = l.level;
		memcpy(app.builtTileset, app.tileset, sizeof(app.builtTileset));
	}
	if (texChanged || l.wallsSerial != app.builtWalls || (l.direction & 3) != app.builtDir || app.openBlock != app.builtOpen) {
		app.builtDir = l.direction & 3;
		app.builtOpen = app.openBlock;
		std::vector<lolvr::DungeonVertex> verts;
		// Le bloc d'entrée où se tient le groupe : sans ses murs, comme le jeu le dessine de l'intérieur.
		uint8_t walls[4096];
		memcpy(walls, l.walls, sizeof(walls));
		if (app.openBlock >= 0)
			memset(walls + app.openBlock * 4, 0, 4);
		lolvr::buildDungeonMesh(walls, g, app.dunTex, verts, app.builtDir);
		app.dungeon.setMesh(verts);
		app.builtWalls = l.wallsSerial;
	}
	// Monstres et objets proches : des images tournées vers le joueur.
	if (l.spriteSerial != app.builtSprites || l.wallsSerial != app.spriteWalls) {
		app.builtSprites = l.spriteSerial;
		app.spriteWalls = l.wallsSerial;
		static std::vector<uint8_t> rgba((size_t)lolvr::kSpriteTexW * lolvr::kSpriteTexH * 4);
		std::vector<lolvr::DungeonSprite> list;
		const size_t one = (size_t)lolvr::kSpriteW * lolvr::kSpriteH;
		for (size_t i = 0; i < l.sprites.size() && i < (size_t)lolvr::kMaxSprites; i++) {
			const lolvr::LevelSprite &s = l.sprites[i];
			if ((i + 1) * one > l.spritePixels.size())
				break;
			if (app.spriteVersion[i] != s.version || app.spriteId[i] != s.id) {
				lolvr::spriteToRgba(&l.spritePixels[i * one], &l.spriteAlpha[i * one], l.screenPalette, rgba.data());
				app.dungeon.setSpriteImage((int)i, rgba.data());
				app.spriteVersion[i] = s.version;
				app.spriteId[i] = s.id;
			}
			lolvr::DungeonSprite d;
			d.x = (float)s.x / 256.0f * lolvr::kBlockSize;
			d.z = (float)s.y / 256.0f * lolvr::kBlockSize;
			d.ceiling = s.ceiling;
			d.layer = (int)i;
			d.u0 = (float)s.x0 / lolvr::kSpriteTexW;
			d.u1 = (float)s.x1 / lolvr::kSpriteTexW;
			d.v0 = (float)s.y0 / lolvr::kSpriteTexH;
			d.v1 = (float)s.y1 / lolvr::kSpriteTexH;
			d.width = (float)(s.x1 - s.x0) * lolvr::kSpritePixel;
			d.height = (float)(s.y1 - s.y0) * lolvr::kSpritePixel * 1.2f;
			d.offset = ((float)(s.x0 + s.x1) * 0.5f - 88.0f) * lolvr::kSpritePixel;
			d.faceParty = s.kind == 0;
			// Un objet rangé dans un mur (niche, cachette) : le jeu ne le montre que si ce côté du mur
			// laisse voir les objets (drapeau 0x80), comme vu depuis la case voisine. On le pose alors
			// contre le mur, là où il apparaît dans le jeu ; sinon il reste caché.
			const int ob = ((s.y >> 8) & 31) * 32 + ((s.x >> 8) & 31);
			bool inFlight = false;
			for (const auto &fo : l.flights)
				inFlight = inFlight || (fo.item && fo.item == s.id);
			if (s.kind == 1 && !inFlight && lolvr::isSolidBlock(l.walls, ob, g)) {
				static const int sdx[4] = {0, 1, 0, -1}, sdz[4] = {-1, 0, 1, 0};
				int best = -1;
				float bestD = 1e9f;
				for (int k = 0; k < 4; k++) {
					int nx = (ob & 31) + sdx[k], nz = (ob >> 5) + sdz[k];
					if (nx < 0 || nx > 31 || nz < 0 || nz > 31 || lolvr::isSolidBlock(l.walls, nz * 32 + nx, g))
						continue;
					if (!(l.wallFlags[l.walls[ob * 4 + k]] & 0x80))
						continue;
					float cx, cz;
					lolvr::blockCenter(nz * 32 + nx, cx, cz);
					float dd = (cx - app.partyX) * (cx - app.partyX) + (cz - app.partyZ) * (cz - app.partyZ);
					if (dd < bestD) {
						bestD = dd;
						best = k;
					}
				}
				if (best < 0)
					continue;
				const float S = lolvr::kBlockSize, eyeY = lolvr::kWallHeight * 0.5f;
				float vx, vz;
				lolvr::blockCenter(ob + sdz[best] * 32 + sdx[best], vx, vz);
				// Distances au mur et à l'objet, le long de la normale du mur.
				float nx = (float)sdx[best], nz = (float)sdz[best];
				float faceD = S * 0.5f;
				float itemD = (vx - d.x) * nx + (vz - d.z) * nz;
				if (itemD < faceD + 0.01f)
					itemD = faceD + 0.01f;
				// Le point du mur où l'œil, au centre de la case voisine, voit le pied de l'objet ; un
				// peu en avant du mur.
				float r = faceD / itemD;
				d.x = vx + (d.x - vx) * r + nx * 0.012f;
				d.z = vz + (d.z - vz) * r + nz * 0.012f;
				d.base = eyeY * (1.0f - r);
				// Posé en hauteur dans le mur (une alcôve) : le jeu le dessine plus haut, de 6 pixels par
				// cran (pixels du mur vu de face : sa hauteur fait 96 pixels).
				if (s.flying > 1) {
					const float rows = (float)(s.flying - 1) * 6.0f - 13.0f + 5.0f; // (+5 : au fond de l'alcôve, pas sur son bord)
					d.base += (rows > 0.0f ? rows : 0.0f) * lolvr::kWallHeight / 96.0f * r;
				}
				d.width *= r;
				d.height *= r;
				d.offset *= r;
				d.ceiling = false;
			}
			int fl = -1;
			if (s.kind == 1 && s.flying && inFlight) {
				// En vol : à la hauteur où le jeu le dessine (un peu sous les yeux, comme lancé de la main),
				// et sa position glisse d'un pas du jeu au suivant.
				const float base = (float)(s.flying - 1) * 4.5f * lolvr::kSpritePixel;
				d.base = base < lolvr::kWallHeight * 0.6f ? base : lolvr::kWallHeight * 0.6f;
				d.ceiling = false;
				// La position vient des pas du vol (exportés à chaque pas) ; la capture, plus lente,
				// ne sert qu'à la première image.
				fl = findFlight(app, s.id);
				if (fl < 0)
					fl = flightFor(app, s.id, d.x, d.z);
			}
			app.flightOf[list.size()] = fl;
			list.push_back(d);
		}
		app.spriteList = list;
		placeFlights(app);
	}
	if (l.flightSerial != app.seenFlights) {
		app.seenFlights = l.flightSerial;
		for (const auto &fo : l.flights)
			if (fo.item)
				flightFor(app, fo.item, (float)fo.x / 256.0f * lolvr::kBlockSize, (float)fo.y / 256.0f * lolvr::kBlockSize);
	}
	{
		for (size_t i = 0; i < app.spriteList.size() && i < 32; i++)
			if (app.flightOf[i] >= 0) {
				placeFlights(app);
				break;
			}
	}
	// Le groupe glisse vers sa case (quelques dixièmes de seconde). Les virages ne glissent pas (cela
	// donne mal au cœur) : un bref fondu au noir, et la pièce a pivoté. Quand le groupe se tourne de
	// lui-même vers l'endroit que le joueur regarde, rien ne bouge. Un changement de niveau ou une
	// téléportation pose le groupe directement.
	if (!app.dunYawSet) {
		app.dunYaw = app.anchorYaw;
		app.dunYawSet = true;
	}
	// Un sort : ses effets (boule de feu, éclair, brume...) tels que le jeu les dessine sur sa vue,
	// posés devant le groupe dans le donjon.
	if (l.spellBusy && l.spellPixels.size() == 176u * 120u) {
		if (l.spellSerial != app.builtSpell || !app.effectShown) {
			app.builtSpell = l.spellSerial;
			static std::vector<uint8_t> fx((size_t)lolvr::kSpriteTexW * lolvr::kSpriteTexH * 4);
			lolvr::spriteToRgba(l.spellPixels.data(), l.spellAlpha.data(), l.screenPalette, fx.data());
			float bx, bz;
			lolvr::blockCenter(app.visBlock >= 0 ? app.visBlock : l.block, bx, bz);
			app.dungeon.setEffect(fx.data(), bx, bz, l.direction);
			app.effectShown = true;
		}
	} else if (app.effectShown) {
		app.dungeon.setEffect(nullptr, 0, 0, 0);
		app.effectShown = false;
	}
	// Porte d'une boutique, entrée d'une autre région : le jeu fait entrer le groupe dans un bloc
	// plein. La vue reste devant, face à l'image ; le jeu montre la scène ou change de région.
	const bool hold = lolvr::isHoldBlock(l.walls, l.block, g);
	app.atHold = hold;
	{
		static const int ddx[4] = {0, 1, 0, -1}, ddz[4] = {-1, 0, 1, 0};
		const int d = l.direction & 3, ahead = (l.block + ddx[d] + ddz[d] * 32) & 1023;
		const uint8_t t = l.walls[ahead * 4 + ((d + 2) & 3)];
		app.liveWall = t && app.dunTex.place[t] && app.dunTex.typeLayer[t] >= 0 && !lolvr::isSolidBlock(l.walls, ahead, g) && !l.sceneBusy;
		// (seulement quand le jeu la dessine vraiment : sa vue ressemble à l'image du lieu ; sinon,
		// pendant qu'il tourne ou redessine, l'image fixe du mur reste)
		if (app.liveWall) {
			int pi = -1;
			for (size_t i = 0; i < l.placeTypes.size(); i++)
				if (l.placeTypes[i] == (int)t && (i + 1) * 176 * 120 * 3 <= l.placeRgb.size())
					pi = (int)i;
			const int fw = app.frame.width, fh = app.frame.height;
			// (en clarté, et corrélées : la mine peut être assombrie par la lanterne)
			double sa = 0, sb = 0, saa = 0, sbb = 0, sab = 0;
			int n = 0;
			if (pi >= 0 && fw >= 320 && fh >= 200 && app.frame.rgba.size() >= (size_t)fw * fh * 4) {
				const uint8_t *pic = &l.placeRgb[(size_t)pi * 176 * 120 * 3];
				for (int y = 4; y < 120; y += 7)
					for (int x = 4; x < 176; x += 9, n++) {
						const uint8_t *a = pic + ((size_t)y * 176 + x) * 3;
						const uint8_t *b = app.frame.rgba.data() + ((size_t)(y * fh / 200) * fw + (size_t)((112 + x) * fw / 320)) * 4;
						const double la = a[0] * 0.3 + a[1] * 0.59 + a[2] * 0.11, lb = b[0] * 0.3 + b[1] * 0.59 + b[2] * 0.11;
						sa += la; sb += lb; saa += la * la; sbb += lb * lb; sab += la * lb;
					}
			}
			double corr = 0.0;
			if (n > 0) {
				const double ca = saa - sa * sa / n, cb = sbb - sb * sb / n, cab = sab - sa * sb / n;
				corr = ca > 1e-6 && cb > 1e-6 ? cab / sqrt(ca * cb) : 0.0;
			}
			app.liveWall = corr > 0.6;
		}
		if (app.liveWall) {
			float bx, bz;
			lolvr::blockCenter(ahead, bx, bz);
			const float S = lolvr::kBlockSize;
			app.liveWallC = Vec3{bx - ddx[d] * (S * 0.5f - 0.03f), lolvr::kWallHeight * 0.5f, bz - ddz[d] * (S * 0.5f - 0.03f)};
			app.liveWallR = Vec3{(float)-ddz[d], 0.0f, (float)ddx[d]};
		}
	}
	// Une scène jouée sur ce bloc (l'intérieur de la hutte de Buck) : à la fin, le jeu y laisse le
	// groupe tourné vers la sortie. C'est une arrivée, comme dans une nouvelle région.
	bool leftScene = false;
	if (!hold)
		app.holdScene = false;
	else if (l.sceneBusy)
		app.holdScene = true;
	else if (app.holdScene) {
		app.holdScene = false;
		leftScene = true;
	}
	// Un bloc plein qu'on traverse (ses murs portent le drapeau 0x80 : un faux mur, l'entrée d'une
	// maison à Yvel) : le jeu y fait entrer le groupe et dessine ce qui l'entoure ; ici aussi.
	const bool fake = !hold && lolvr::isSolidBlock(l.walls, l.block, g);
	// De la porte d'un lieu (le bloc de l'image) à l'intérieur (l'auberge, la marina) : le groupe fait un
	// pas hors du bloc de l'image et le jeu y joue sa scène. La vue reste devant l'image du lieu, comme
	// avant ; autrement on voit, derrière la scène, le mur de forêt de la case où le groupe est entré.
	{
		const bool pictureScene = l.sceneBusy && l.scenePicture;
		// (seulement quand une scène est en cours : celle du lieu, qui va montrer son intérieur ; en
		// repartant d'une porte, ou après la sortie d'un lieu, la vue suit le groupe aussitôt)
		if (app.prevHold && !hold && l.sceneBusy && !gfxChanged && app.shownLevel == l.level && app.visBlock >= 0) {
			app.holdOver = true;
			app.holdOverScene = false;
			app.holdOverUntil = app.clock + 1.0;
			app.holdOverBlock = l.block;
			app.holdOverIdle = 0.0;
		}
		app.prevHold = hold;
		if (app.holdOver) {
			if (pictureScene) {
				app.holdOverScene = true;
				app.holdOverUntil = app.clock + 120.0;
			}
			if (!l.sceneBusy && !app.holdOverScene) {
				if (app.holdOverIdle <= 0.0)
					app.holdOverIdle = app.clock;
			} else {
				app.holdOverIdle = 0.0;
			}
			if ((app.holdOverIdle > 0.0 && app.clock - app.holdOverIdle > 0.2) || gfxChanged || app.shownLevel != l.level || hold || (!app.holdOverScene && l.block != app.holdOverBlock) || (app.holdOverScene && !pictureScene) || app.clock > app.holdOverUntil)
				app.holdOver = false;
		}
	}
	if (app.holdOver) {
		// (la vue ne bouge pas)
	} else if (gfxChanged || app.shownLevel != l.level || app.visBlock < 0 || !hold || leftScene) {
		app.visBlock = l.block;
		// Arrivée sur une entrée (le bloc de l'image, à la sortie du château, du manoir, de la hutte de
		// Buck) : comme le jeu, on se tient dans ce bloc, ouvert (ses murs ne se voient que de dehors).
		// On en repart comme d'une case ordinaire, et les clics visent les murs autour.
		app.openBlock = hold || fake ? l.block : -1;
	} else if (!hold || app.openBlock != l.block) {
		app.openBlock = -1;
	}
	float tx, tz;
	lolvr::blockCenter(app.visBlock, tx, tz);
	const int dir = l.direction & 3;
	float dx = tx - app.partyX, dz = tz - app.partyZ;
	{
		// Pas au-delà du bord du plan (le jeu passe à la case suivante de sa suite de 1024 : une forêt
		// qui boucle) : la case d'arrivée est à côté, dans la bordure recopiée ; on y reporte le groupe
		// pour qu'il y marche comme ailleurs.
		const float S = lolvr::kBlockSize;
		const int cbx = (int)floorf(app.partyX / S), cbz = (int)floorf(app.partyZ / S);
		const int vbx = app.visBlock & 31, vbz = app.visBlock >> 5;
		if (!gfxChanged && app.shownLevel == l.level && (abs(vbx - cbx) > 16 || abs(vbz - cbz) > 16)) {
			int delta = (app.visBlock - (cbz * 32 + cbx)) & 1023;
			if (delta >= 512)
				delta -= 1024;
			const int dzv = (int)lroundf((float)delta / 32.0f), dxv = delta - dzv * 32;
			if (abs(dxv) <= 2 && abs(dzv) <= 2) {
				const float vtx = ((float)(cbx + dxv) + 0.5f) * S, vtz = ((float)(cbz + dzv) + 0.5f) * S;
				app.partyX += tx - vtx;
				app.partyZ += tz - vtz;
				dx = tx - app.partyX;
				dz = tz - app.partyZ;
			}
		}
	}
	bool snap = gfxChanged || app.shownLevel != l.level || app.shownDir < 0 ||
	            dx * dx + dz * dz > 1.6f * lolvr::kBlockSize * 1.6f * lolvr::kBlockSize;
	auto wrapPi = [](float a) {
		while (a > 3.14159265f) a -= 6.2831853f;
		while (a < -3.14159265f) a += 6.2831853f;
		return a;
	};
	// (pendant un fondu de virage, le lacet du groupe n'a pas encore rejoint sa cible : c'est d'elle
	// qu'on part, sinon deux virages rapprochés — ceux mis en attente pendant un repos, par exemple —
	// décalaient la pièce d'un quart de tour par rapport au jeu, jusqu'au rechargement)
	const float yawBase = app.turnFade > 0.0f ? app.turnTarget : app.partyYaw;
	const float dyaw = wrapPi((float)dir * 1.5707963f - yawBase);
	if (snap) {
		app.partyX = tx;
		app.partyZ = tz;
		app.partyYaw = yawBase + dyaw;
		app.turnFade = 0;
	} else {
		float k = clampf(dt * 12.0f, 0.0f, 1.0f);
		app.partyX += dx * k;
		app.partyZ += dz * k;
		if (fabsf(tx - app.partyX) < 0.002f && fabsf(tz - app.partyZ) < 0.002f)
			app.partyX = tx, app.partyZ = tz;
		if (dir != app.shownDir) {
			if (app.alignTurn != 0 && dir == app.alignExpect) {
				app.dunYaw -= dyaw; // le monde reste immobile : c'est le groupe qui s'est tourné
				app.partyYaw += dyaw;
				if (app.turnFade > 0.0f)
					app.turnTarget += dyaw;
				app.alignTurn = 0;
			} else {
				app.turnTarget = yawBase + dyaw;
				if (app.turnFade <= 0.0f)
					app.turnFade = 0.0001f;
			}
		}
	}
	app.shownDir = dir;
	if (app.alignTurn != 0 && nowSeconds() > app.alignUntil)
		app.alignTurn = 0;
	// Garde-fou : hors virage, la pièce suit toujours la direction du jeu.
	if (app.turnFade <= 0.0f) {
		const float drift = wrapPi((float)dir * 1.5707963f - app.partyYaw);
		if (fabsf(drift) > 0.01f)
			app.partyYaw += drift;
	}
	float light = 1.0f;
	if (app.turnFade > 0.0f) {
		app.turnFade += dt / 0.22f;
		if (app.turnFade >= 0.5f)
			app.partyYaw = app.turnTarget;
		if (app.turnFade >= 1.0f)
			app.turnFade = 0.0f;
		light = app.turnFade > 0.0f ? clampf(fabsf(app.turnFade - 0.5f) * 2.0f, 0.0f, 1.0f) : 1.0f;
	}
	// La tête du joueur entre dans un mur (il s'est penché, ou a marché dans sa pièce) : pénombre.
	{
		Vec3 rel = app.headPos - app.roomPos;
		Vec3 p = rotate(quatYaw(-(app.dunYaw + app.partyYaw)), rel);
		float lx = p.x + app.partyX, lz = p.z + app.partyZ;
		const float S = lolvr::kBlockSize, m = 0.20f;
		bool inWall = false;
		for (int k = 0; k < 4 && !inWall; k++) {
			float sx = lx + ((k & 1) ? m : -m), sz = lz + ((k & 2) ? m : -m);
			int bx = (int)floorf(sx / S), bz = (int)floorf(sz / S);
			const int wb = (bz * 32 + bx) & 1023; // au bord, la case suivante
			inWall = wb != app.openBlock && lolvr::isSolidBlock(l.walls, wb, g);
		}
		app.wallDim += ((inWall ? 1.0f : 0.0f) - app.wallDim) * clampf(dt * 8.0f, 0.0f, 1.0f);
		light *= 1.0f - 0.92f * app.wallDim;
	}
	// Pénombre du jeu (lieux sombres, lampe, fondus) : la clarté de la palette à l'écran.
	app.lightLevel += ((float)l.light / 256.0f - app.lightLevel) * clampf(dt * 6.0f, 0.0f, 1.0f);
	light *= app.lightLevel;
	if (app.levelFadeIn < 1.0f) {
		app.levelFadeIn = fminf(1.0f, app.levelFadeIn + dt / 0.30f);
		light *= app.levelFadeIn;
	}
	app.dungeonLight = light;
	// La lanterne dans un lieu sombre : pleine d'huile, on voit loin ; presque vide, moins ; éteinte,
	// comme dans le jeu : une pénombre où l'on voit encore la case devant, sans plus. Sa flamme vacille un peu.
	{
		static const float reachOf[9] = {10.0f, 10.0f, 8.5f, 8.5f, 7.0f, 7.0f, 6.0f, 6.0f, 4.8f};
		float want = l.lamp < 0 || l.lamp > 8 ? 11.0f : reachOf[l.lamp];
		if (l.lamp >= 0 && l.lamp < 8) {
			const float t = (float)app.clock;
			want *= 1.0f + 0.035f * sinf(t * 13.1f) + 0.025f * sinf(t * 7.3f + 1.0f);
		}
		app.dungeonReach += (want - app.dungeonReach) * clampf(dt * 3.0f, 0.0f, 1.0f);
	}
	app.shownLevel = l.level;
	app.dungeonVisible = app.dungeon.hasContent();
}

// Donjon 3D : le grand écran n'apparaît que lorsqu'on en a besoin. Le donjon en 3D montre les murs,
// les monstres et les objets ; l'écran revient pour les boutiques, les dialogues et les scènes, la
// carte, les menus et la fiche d'un personnage, et reste un moment après.
void updateScreenShown(App &app, double now) {
	// La carte, une fiche, un menu : l'écran disparaît dès qu'ils se ferment. Une scène (boutique,
	// rencontre) : un court instant de grâce, le temps qu'une scène suivante enchaîne.
	bool need = !app.dungeonVisible || !app.splitWanted || app.charSheet || (app.automapOpen && !app.atlasLive) || app.menuAlone;
	// Une scène qui montre sa propre image (une boutique, un personnage, une cinématique). Une
	// remarque ou un monstre qui parle (les chandeliers, le gros orc) laisse le donjon dans la fenêtre
	// du jeu : seule la boîte de texte apparaît, le donjon en 3D le montre déjà.
	// Devant la porte d'un lieu (l'auberge, la hutte de Buck) : la vue du jeu montre le lieu, où l'on
	// clique la porte ; elle apparaît aussi.
	// (une animation d'un script hors conversation, un mur qui s'écroule sous le marteau : la vue du
	// jeu apparaît le temps qu'elle se joue)
	bool scene = (app.level.scenePicture || (app.atHold && app.level.sceneBusy && app.dungeonMode)) && (app.level.sceneBusy || app.cutscene || (app.dungeonMode && app.level.animBusy));
	// Un personnage qui parle devant le donjon : sa fenêtre attend un instant, le temps qu'on le voie
	// se tourner vers nous dans le donjon, comme dans le jeu.
	if (scene && app.dungeonMode && app.gameStarted && !app.level.sceneCinema) {
		if (app.charSceneSince < 0.0)
			app.charSceneSince = now;
		if (now - app.charSceneSince < 0.6)
			scene = false;
	} else if (!scene) {
		app.charSceneSince = -1.0;
	}
	if (scene)
		app.screenNeededAt = now;
	bool show = need || scene || now - app.screenNeededAt < 0.1;
	// Mode cinéma : une scène (en partie), ou l'avant-partie (intro, menus, choix du personnage).
	const bool cinemaOk = app.dungeonMode && app.cinemaSize != 3 && !lolvr::isCrtEnv(app.environment);
	// (seulement un lieu, une boutique ou une séquence : pas un personnage qui parle devant le donjon)
	const bool cinemaScene = scene && app.level.sceneCinema && app.level.scenePicture;
	if (cinemaOk && cinemaScene && app.gameStarted && !app.charSheet && !app.menuAlone && !app.automapOpen)
		app.cinemaSeenAt = now;
	const bool beforeGame = cinemaOk && app.cinemaIntro && !app.gameStarted && app.coreStarted;
	// (un court délai : deux scènes qui s'enchaînent ne rallument pas le donjon entre elles)
	// La partie commence (après l'intro, ou une partie chargée) : on reste dans le noir un moment, le
	// temps que la première scène (le garde) arrive, sans montrer le château avant.
	if (app.gameStarted && !app.prevGameStarted && cinemaOk)
		app.cinemaSeenAt = now + 2.75;
	app.prevGameStarted = app.gameStarted;
	// Une scène du mode cinéma qui mène ailleurs (l'entrée de la mine) : on reste dans le noir le
	// temps que le nouveau décor arrive, puis un seul fondu (celui du mode cinéma) le montre.
	if (cinemaOk && app.cinema > 0.0f && app.levelFadeIn < 1.0f) {
		if (app.clock - app.gfxWaitAt < 0.05)
			app.cinemaSeenAt = fmax(app.cinemaSeenAt, now - 0.1);
		else
			app.levelFadeIn = 1.0f;
	}
	app.cinemaWanted = cinemaOk && (now - app.cinemaSeenAt < 0.25 || beforeGame);
	{
		const float dt = (float)clampf((float)(now - app.cinemaTime), 0.0f, 0.1f);
		app.cinemaTime = now;
		app.cinema = clampf(app.cinema + (app.cinemaWanted ? dt : -dt) / 0.4f, 0.0f, 1.0f);
		// L'écran lui-même : seulement quand il y a une scène (ou l'avant-partie) à montrer.
		const bool picture = cinemaOk && ((cinemaScene && app.gameStarted) || beforeGame);
		app.cinemaLive = picture;
		app.cinemaScreen = clampf(app.cinemaScreen + (picture ? dt : -dt) / 0.4f, 0.0f, 1.0f);
	}
	// Sa place : devant la tête, au début (ou au recentrage).
	if (!app.cinemaWanted && app.cinema <= 0.0f)
		app.cinemaArmed = true;
	if (app.cinemaWanted && app.cinemaArmed && app.headValid) {
		app.cinemaArmed = false;
		const Vec3 f = rotate(app.headRot, {0, 0, -1});
		app.cinemaYaw = atan2f(-f.x, -f.z);
		app.cinemaHead = app.headPos;
	}
	if (show && !app.screenShown && app.dungeonVisible && app.headValid && app.screenPlaced) {
		// Il revient là où le joueur regarde, à sa distance et à sa hauteur habituelles.
		Vec3 f = rotate(app.headRot, {0, 0, -1});
		float delta = atan2f(-f.x, -f.z) - app.screenYaw;
		while (delta > 3.14159265f) delta -= 6.2831853f;
		while (delta < -3.14159265f) delta += 6.2831853f;
		if (fabsf(delta) > 0.6f) {
			app.anchorDir = rotate(quatYaw(delta), app.anchorDir);
			app.screenYaw += delta;
			placeScreen(app, false);
		}
	}
	app.screenShown = show;
}

void fillSceneState(App &app, double now, bool focused) {
	SceneState &s = app.ss;
	s.environment = app.environment;
	s.theme = app.shownTheme;
	s.time = now - app.startTime;
	s.screenPos = viewCenter(app);
	s.screenRot = screenRot(app);
	s.screenWidth = app.dispW;
	s.screenAspect = app.dispH / app.dispW;
	s.hitRight = app.hitRight;
	s.hitUp = app.hitUp;
	s.hitRadius = app.hitRadius;
	s.pointerLong = app.pointerLong;
	s.roomPos = app.roomPos;
	s.roomYaw = app.anchorYaw;
	// Le donjon : la case du groupe sous les pieds du joueur, la direction du groupe droit devant lui.
	s.dungeonVisible = app.dungeonVisible;
	s.dungeonModel = lolvr::dungeonModel(app.roomPos, app.dunYaw, app.partyX, app.partyZ, app.partyYaw);
	s.dungeonLight = app.dungeonLight;
	s.dungeonReach = app.dungeonReach;
	{
		lolvr::SceneState::Panel &p = s.panels[lolvr::SceneState::kMaxPanels - 3];
		p = lolvr::SceneState::Panel();
		if (app.liveWall && app.dungeonVisible && app.inDungeon && app.cinema <= 0.0f && app.srcTexW > 0) {
			p.visible = true;
			p.center = lolvr::transformPoint(s.dungeonModel, app.liveWallC);
			p.right = normalize(lolvr::transformDir(s.dungeonModel, app.liveWallR));
			p.up = normalize(lolvr::transformDir(s.dungeonModel, Vec3{0, 1, 0}));
			p.halfW = lolvr::kBlockSize * 0.5f;
			p.halfH = lolvr::kWallHeight * 0.5f;
			p.u0 = 112.0f / 320.0f; p.v0 = 0.0f; p.u1 = 288.0f / 320.0f; p.v1 = 120.0f / 200.0f;
			p.border = 0.0f;
			p.layer = -1;
		}
	}
	// Mode cinéma : l'écran plat (calque quad) laisse la place à l'écran courbe.
	s.screenVisible = app.screenShown && app.cinema <= 0.0f;
	s.cinema = app.cinema * app.cinema * (3.0f - 2.0f * app.cinema);
	s.cinemaScreenAlpha = app.cinemaScreen * app.cinemaScreen * (3.0f - 2.0f * app.cinemaScreen);
	if (app.cinema > 0.0f) {
		// (pendant la scène, la place et la taille suivent ; après, elles restent figées)
		if (app.cinemaLive || app.cineShowHalfH <= 0.0f) {
			app.cineShowPos = viewCenter(app);
			app.cineShowRot = screenRot(app);
			app.cineShowHalfA = atanf(app.dispW * 0.5f / cinemaRadius(app));
			app.cineShowHalfH = app.dispH * 0.5f;
			app.cineShowRect[0] = app.cropX / 320.0f;
			app.cineShowRect[1] = app.cropY / 200.0f;
			app.cineShowRect[2] = (app.cropX + app.cropW) / 320.0f;
			app.cineShowRect[3] = (app.cropY + app.cropH) / 200.0f;
			app.cineShowBandFrac = app.bandH > 0.0f ? app.bandH / (app.cropH + app.bandH) : 0.0f;
			app.cineShowBand[0] = app.bandRect[0] / 320.0f;
			app.cineShowBand[1] = app.bandRect[1] / 200.0f;
			app.cineShowBand[2] = (app.bandRect[0] + app.bandRect[2]) / 320.0f;
			app.cineShowBand[3] = (app.bandRect[1] + app.bandRect[3]) / 200.0f;
		}
		s.cinemaBandFrac = app.cineShowBandFrac;
		for (int k = 0; k < 4; k++)
			s.cinemaBandRect[k] = app.cineShowBand[k];
		s.cinemaPos = app.cineShowPos;
		s.cinemaRot = app.cineShowRot;
		s.cinemaRadius = cinemaRadius(app);
		s.cinemaHalfAngle = app.cineShowHalfA;
		s.cinemaHalfH = app.cineShowHalfH;
		for (int k = 0; k < 4; k++)
			s.cinemaRect[k] = app.cineShowRect[k];
		s.cinemaTexture = app.cinemaTex;
		Vec3 mean{0, 0, 0};
		for (int k = 0; k < 4; k++) {
			s.cinemaEdge[k] = app.cinemaEdge[k];
			mean = mean + app.cinemaEdge[k] * 0.25f;
		}
		// Le fond : un noir teinté des couleurs de la scène, à peine.
		s.cinemaVoid = mean * (0.035f * s.cinemaScreenAlpha);
	}
	s.screenFrameless = app.menuAlone && app.split;
	for (int h = 0; h < 2; h++) {
		lolvr::HandState &hand = s.hands[h];
		bool pointing = h == app.activeHand;
		hand.visible = focused && app.aimValid[h];
		hand.pos = toVec3(app.aimPose[h].position);
		hand.rot = toQuat(app.aimPose[h].orientation);
		// L'index qui vise ne se replie qu'un peu au clic, pour que le pointeur ne bouge pas.
		hand.trigger = pointing ? app.triggerValue[h] * 0.3f : app.triggerValue[h];
		hand.grip = app.squeezeValue[h];
		hand.pointing = pointing;
	}
	s.hitValid = focused && app.hitValid;
	s.hit = app.hitPoint;
	s.clickPulse = app.clickPulse;
	s.screenGlow = app.screenGlow;
	s.gameTexture = app.srcTexW > 0 ? app.srcTex : 0;
	s.gameWidth = app.srcTexW > 0 ? app.srcTexW : 320;
	s.gameHeight = app.srcTexH > 0 ? app.srcTexH : 200;
}

void renderFrame(App &app, float dt, int savedEnvironment) {
	XrFrameWaitInfo waitInfo{XR_TYPE_FRAME_WAIT_INFO};
	XrFrameState frameState{XR_TYPE_FRAME_STATE};
	if (XR_FAILED(xrWaitFrame(app.session, &waitInfo, &frameState)))
		return;
	XrFrameBeginInfo beginInfo{XR_TYPE_FRAME_BEGIN_INFO};
	if (XR_FAILED(xrBeginFrame(app.session, &beginInfo)))
		return;

	XrCompositionLayerPassthroughFB passthroughLayer{XR_TYPE_COMPOSITION_LAYER_PASSTHROUGH_FB};
	XrCompositionLayerQuad quadLayer{XR_TYPE_COMPOSITION_LAYER_QUAD};
	XrCompositionLayerProjection projLayer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
	XrCompositionLayerProjectionView projViews[2] = {{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}, {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
	std::vector<const XrCompositionLayerBaseHeader *> layers;

	if (frameState.shouldRender) {
		const XrTime time = frameState.predictedDisplayTime;
		const double now = nowSeconds();
		const bool focused = app.sessionState == XR_SESSION_STATE_FOCUSED;
		const bool visible = focused || app.sessionState == XR_SESSION_STATE_VISIBLE;

		updateHead(app, time);
		// Au premier affichage : on retrouve le décor choisi la dernière fois et on place l'écran.
		if (!app.environmentRestored && app.headValid) {
			app.environmentRestored = true;
			app.classicEnv = savedEnvironment;
			setEnvironment(app, app.dungeonMode ? lolvr::kEnvLibrary : savedEnvironment, time);
			// Premier lancement : le joueur choisit sa vue, droit devant lui.
			if (!app.viewChosen) {
				app.chooserOpen = true;
				app.helpOpen = false;
			}
		}
		if (app.headValid && !app.screenPlaced) {
			updateFloor(app, time);
			placeScreen(app, true);
		}

		updateGameEvents(app, now);
		updateLayout(app, now);
		if (app.scene.ready() && now - app.imagesCheckedAt > 4.0) {
			// Images du joueur (facultatives) : on regarde de temps en temps si elles sont arrivées.
			app.imagesCheckedAt = now;
			scanDecorImages(app);
		}
		if (focused) {
			updateInput(app, time, dt, now);
		} else {
			CoreHost::get().setMouseButtons(false, false);
			lolvr::UiFrameOutput ui;
			updateUi(app, time, dt, now, false, false, XrVector2f{0, 0}, ui);
			releaseTypedKeys(app, now, true);
		}
		app.clickPulse *= expf(-dt * 7.0f);
		if (app.settingsDirty)
			saveSettings(app);

		uploadFrame(app);
		updateDungeon(app, dt);
		updateScreenShown(app, now);
		fillSceneState(app, now, focused);
		app.scene.prepare(app.ss);

		// Son : l'auditeur est la tête du joueur ; le son du jeu vient de l'écran.
		Ambience &ambience = Ambience::get();
		if (app.headValid)
			ambience.setListener(app.headPos, app.headRot);
		ambience.setScreen(viewCenter(app), screenRot(app), app.dispW);
		ambience.setRoom(app.roomPos, app.anchorYaw);
		ambience.setFire(app.fireOn && !app.dungeonVisible);
		ambience.setMood(visible && lolvr::isLibraryEnv(app.environment) && app.scene.ready() && !app.dungeonVisible, app.shownTheme);

		// Calques, du fond vers l'avant : vraie pièce (réalité mixte), image du jeu, décor.
		if (app.passthroughActive) {
			passthroughLayer.flags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
			passthroughLayer.space = XR_NULL_HANDLE;
			passthroughLayer.layerHandle = app.passthroughLayer;
			layers.push_back((const XrCompositionLayerBaseHeader *)&passthroughLayer);
		}
		if (app.screenPlaced && app.screenShown && app.cinema <= 0.0f && Scene::usesQuadLayer(app.environment) && renderQuad(app)) {
			quadLayer.space = app.localSpace;
			quadLayer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
			quadLayer.subImage.swapchain = app.quadSwapchain.handle;
			quadLayer.subImage.imageRect.offset = {0, 0};
			quadLayer.subImage.imageRect.extent = {app.quadRectW, app.quadRectH};
			quadLayer.pose.orientation = toXr(screenRot(app));
			quadLayer.pose.position = toXr(viewCenter(app));
			quadLayer.size = {app.dispW * app.quadScaleX, app.dispH * app.quadScaleY};
			layers.push_back((const XrCompositionLayerBaseHeader *)&quadLayer);
		}

		XrViewLocateInfo locateInfo{XR_TYPE_VIEW_LOCATE_INFO};
		locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
		locateInfo.displayTime = time;
		locateInfo.space = app.localSpace;
		XrViewState viewState{XR_TYPE_VIEW_STATE};
		XrView views[2] = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
		uint32_t viewCount = 0;
		bool viewsOk = XR_SUCCEEDED(xrLocateViews(app.session, &locateInfo, &viewState, 2, &viewCount, views)) && viewCount == 2 &&
		               (viewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) &&
		               (viewState.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT);
		if (viewsOk && app.screenPlaced) {
			bool ok = true;
			for (int eye = 0; eye < 2 && ok; eye++) {
				Swapchain &sc = app.eyeSwapchains[eye];
				uint32_t index = 0;
				if (!acquireImage(sc, index)) {
					ok = false;
					break;
				}
				bindEyeTarget(app, eye, sc.images[index].image);
				glViewport(0, 0, sc.width, sc.height);
				const XrFovf &fov = views[eye].fov;
				Mat4 proj = lolvr::projectionFromTangents(tanf(fov.angleLeft), tanf(fov.angleRight), tanf(fov.angleDown), tanf(fov.angleUp), 0.05f, 60.0f);
				Mat4 view = lolvr::viewFromPose(toVec3(views[eye].pose.position), toQuat(views[eye].pose.orientation));
				app.scene.render(view, proj, app.ss);
				const GLenum discard[] = {GL_DEPTH_ATTACHMENT};
				glInvalidateFramebuffer(GL_FRAMEBUFFER, 1, discard);
				glBindFramebuffer(GL_FRAMEBUFFER, 0);
				releaseImage(sc);
				projViews[eye].pose = views[eye].pose;
				projViews[eye].fov = views[eye].fov;
				projViews[eye].subImage.swapchain = sc.handle;
				projViews[eye].subImage.imageRect.offset = {0, 0};
				projViews[eye].subImage.imageRect.extent = {sc.width, sc.height};
			}
			if (ok) {
				// Le décor est en alpha prémultiplié : là où il est transparent (l'ouverture du
				// cadre, ou tout ce qui entoure l'écran en réalité mixte), on voit les calques du dessous.
				projLayer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
				projLayer.space = app.localSpace;
				projLayer.viewCount = 2;
				projLayer.views = projViews;
				layers.push_back((const XrCompositionLayerBaseHeader *)&projLayer);
			}
		}
	}

	XrFrameEndInfo endInfo{XR_TYPE_FRAME_END_INFO};
	endInfo.displayTime = frameState.predictedDisplayTime;
	endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	endInfo.layerCount = (uint32_t)layers.size();
	endInfo.layers = layers.empty() ? nullptr : layers.data();
	xrEndFrame(app.session, &endInfo);
}

// ---------------------------------------------------------------------------
// Boucle principale

void releaseAllKeys(App &app) {
	Button *all[] = {&app.kUp, &app.kDown, &app.kSlideL, &app.kSlideR, &app.kTurnL, &app.kTurnR,
	                 &app.kEsc, &app.kMap, &app.kEnter};
	const unsigned keys[] = {RETROK_UP, RETROK_DOWN, RETROK_LEFT, RETROK_RIGHT, RETROK_HOME, RETROK_PAGEUP,
	                         RETROK_ESCAPE, RETROK_m, RETROK_RETURN};
	for (int i = 0; i < 9; i++)
		setKey(*all[i], false, keys[i]);
	CoreHost::get().setSpecialButtons(false, false);
	app.triggerDown[0] = app.triggerDown[1] = false;
	app.squeezeDown[0] = app.squeezeDown[1] = false;
	app.triggerValue[0] = app.triggerValue[1] = 0.0f;
	app.squeezeValue[0] = app.squeezeValue[1] = 0.0f;
	app.zoomDown = false;
	app.recenterDown = false;
	app.keyboardButtonDown = false;
	releaseTypedKeys(app, 0.0, true);
	CoreHost::get().setMouseButtons(false, false);
}

void pollXrEvents(App &app) {
	XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
	while (xrPollEvent(app.instance, &event) == XR_SUCCESS) {
		switch (event.type) {
		case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
			const XrEventDataSessionStateChanged *changed = (const XrEventDataSessionStateChanged *)&event;
			app.sessionState = changed->state;
			LOGI("état de la session : %d", (int)changed->state);
			switch (changed->state) {
			case XR_SESSION_STATE_READY: {
				XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO};
				begin.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
				if (XR_SUCCEEDED(xrBeginSession(app.session, &begin)))
					app.sessionRunning = true;
				break;
			}
			case XR_SESSION_STATE_STOPPING:
				xrEndSession(app.session);
				app.sessionRunning = false;
				break;
			case XR_SESSION_STATE_EXITING:
			case XR_SESSION_STATE_LOSS_PENDING:
				app.exitRequested = true;
				ANativeActivity_finish(app.androidApp->activity);
				break;
			default:
				break;
			}
			// Le jeu ne tourne que lorsque le casque est porté et l'application visible.
			bool active = changed->state == XR_SESSION_STATE_VISIBLE || changed->state == XR_SESSION_STATE_FOCUSED;
			CoreHost::get().setPaused(!active);
			if (!active)
				Ambience::get().setMood(false, app.shownTheme);
			if (changed->state != XR_SESSION_STATE_FOCUSED)
				releaseAllKeys(app);
			break;
		}
		case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
			app.exitRequested = true;
			ANativeActivity_finish(app.androidApp->activity);
			break;
		default:
			break;
		}
		event = XrEventDataBuffer{XR_TYPE_EVENT_DATA_BUFFER};
	}
}

void onAppCmd(android_app *androidApp, int32_t cmd) {
	App *app = (App *)androidApp->userData;
	if (cmd == APP_CMD_RESUME)
		app->resumed = true;
	else if (cmd == APP_CMD_PAUSE)
		app->resumed = false;
}

} // namespace

void android_main(android_app *androidApp) {
	App app;
	app.androidApp = androidApp;
	androidApp->userData = &app;
	androidApp->onAppCmd = onAppCmd;

	JNIEnv *env = nullptr;
	androidApp->activity->vm->AttachCurrentThread(&env, nullptr);

	ANativeActivity *activity = androidApp->activity;
	std::string internal = activity->internalDataPath ? activity->internalDataPath : "/data/local/tmp";
	std::string external = activity->externalDataPath ? activity->externalDataPath : internal;
	app.decorDir = external + "/decor";
	app.systemDir = internal + "/system";
	app.saveDir = external + "/sauvegardes";
	app.gameRoot = external + "/lol";
	app.settingsPath = internal + "/reglages.txt";
	makeDirs(app.systemDir);
	makeDirs(app.saveDir);
	makeDirs(app.gameRoot);
	makeDirs(app.decorDir);
	extractAssetDir(activity->assetManager, "scummvm/theme", app.systemDir + "/scummvm/theme");
	extractAssetDir(activity->assetManager, "scummvm/extra", app.systemDir + "/scummvm/extra");
	LOGI("dossier du jeu : %s", app.gameRoot.c_str());
	// Au tout premier lancement, la langue du casque ; ensuite celle enregistrée, puis celle du jeu.
	if (androidApp->config) {
		char lang[2] = {0, 0};
		AConfiguration_getLanguage(androidApp->config, lang);
		app.language = lang[0] == 'f' && lang[1] == 'r' ? lolvr::kLangFrench : lang[0] == 'd' && lang[1] == 'e' ? lolvr::kLangGerman : lolvr::kLangEnglish;
	}
	int savedEnvironment = lolvr::kEnvLibrary;
	loadSettings(app, savedEnvironment);
	app.startTime = nowSeconds();

	// Tant que la session VR n'est pas active, le jeu reste en pause.
	CoreHost::get().setPaused(true);

	bool ready = initEgl(app.egl) && initXr(app) && initGl(app);
	if (!ready) {
		LOGE("initialisation impossible : fermeture");
		ANativeActivity_finish(activity);
	} else {
		// Le son démarre tout de suite : l'ambiance de la pièce accompagne même l'écran d'accueil.
		app.audio = startAudio(app.audioRate);
	}

	double lastTime = nowSeconds();
	while (!androidApp->destroyRequested) {
		// Événements Android : on attend seulement quand il n'y a rien à afficher.
		for (;;) {
			int events = 0;
			android_poll_source *source = nullptr;
			int timeout = (ready && app.sessionRunning) || androidApp->destroyRequested ? 0 : 100;
			if (ALooper_pollOnce(timeout, nullptr, &events, (void **)&source) < 0)
				break;
			if (source)
				source->process(androidApp, source);
			if (androidApp->destroyRequested)
				break;
		}
		if (androidApp->destroyRequested || !ready)
			continue;

		pollXrEvents(app);
		updateGame(app);

		double now = nowSeconds();
		float dt = (float)fmin(now - lastTime, 0.1);
		lastTime = now;
		if (app.sessionRunning)
			renderFrame(app, dt, savedEnvironment);
	}

	// Fermeture : on arrête le son et le jeu, puis on quitte le processus pour repartir
	// d'un état propre au prochain lancement.
	stopAudio(app.audio);
	CoreHost::get().stop();
	if (app.session != XR_NULL_HANDLE)
		xrDestroySession(app.session);
	if (app.instance != XR_NULL_HANDLE)
		xrDestroyInstance(app.instance);
	androidApp->activity->vm->DetachCurrentThread();
	LOGI("fin de l'application");
	_exit(0);
}
