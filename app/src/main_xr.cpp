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
	int dungeonMode = 1;            // 0 vue classique ; mode immersif : 1 écran au besoin, 2 écran toujours
	int classicEnv = lolvr::kEnvLibrary; // décor de la vue classique (le mode immersif n'en a qu'un)
	bool viewChosen = false;        // le joueur a choisi sa vue au premier lancement
	bool chooserOpen = false;       // la fenêtre du choix de la vue est ouverte
	bool chooserPlaced = false;
	bool screenShown = true;        // le grand écran est affiché
	double screenNeededAt = 0;
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
	bool dungeonVisible = false;
	lolvr::LevelSnapshot level;
	lolvr::DungeonTextures dunTex;
	uint32_t builtGfx = 0, builtWalls = 0, builtCapture = 0, builtSprites = 0;
	uint32_t spriteVersion[32] = {0};
	int spriteId[32] = {0};
	int shownLevel = 0;
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
	float widths[kEnvCount] = {1.1f, 1.8f, 0.62f}; // mètres : largeur de l'image entière (hauteur = 3/4)
	float distances[kEnvCount] = {1.15f, 2.0f, 0.95f};
	// Ce que l'écran affiche en ce moment : portion de l'image du jeu (pixels) et taille réelle (mètres).
	float cropX = 0, cropY = 0, cropW = 320, cropH = 200;
	float dispW = 2.4f, dispH = 1.8f;
	float bandH = 0;   // hauteur (pixels du jeu, à la largeur de la vue) de la ligne de messages collée sous la scène
	int quadRectW = 0, quadRectH = 0; // zone du calque quad réellement utilisée
	float quadScaleX = 1.0f, quadScaleY = 1.0f; // marge noire comprise

	// Interface détachée : l'écran ne garde que la vue du donjon, le reste part sur des panneaux.
	lolvr::Ui ui;
	bool detached = true;        // choix du joueur
	bool split = false;          // en vigueur en ce moment
	bool splitWanted = false;
	double splitWantedSince = 0;
	bool gameStarted = false, automapOpen = false;
	// Combat aux boutons : dans le donjon, A attaque et B ouvre la magie du personnage choisi.
	bool inDungeon = false;
	int characters = 0, selectedChar = 0;
	bool attackDown = false, magicDown = false, charFlick = false;
	double synthClickUntil = 0;   // clic envoyé au jeu à la place du pointeur (bouton de magie)
	float synthU = 0, synthV = 0;
	bool keyboardOpen = false, keyboardButtonDown = false;
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
	int builtDir = -1;        // direction du groupe pour laquelle le donjon a été construit (dessins au sol)
	bool aimOnUi = false;     // le doigt vise une fenêtre ou l'écran
	int visBlock = -1;        // case où se tient la vue 3D (elle ne suit pas le groupe dans une porte de boutique)
	float lightLevel = 1.0f;  // clarté du jeu (pénombre, lampe), adoucie
	double imagesCheckedAt = -100.0;
	bool imageLoaded[kImageSlots] = {false, false, false, false, false, false};
	std::string imageFile[kImageSlots];                    // fichier affiché dans chaque emplacement
	std::map<std::string, long> imageRejected;   // fichiers illisibles (chemin -> taille au moment de l'essai)
	std::string imageInfo[2];                    // état lisible, affiché sous le clavier
	// Direction de l'écran choisie par le joueur, par décor (lacet par rapport au regard de départ, tangage)
	float aimYaw[kEnvCount] = {0, 0, 0}, aimPitch[kEnvCount] = {0, 0, 0};
	bool aimSaved[kEnvCount] = {false, false, false};
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
float &screenDistance(App &app) { return app.distances[app.environment]; }
Quat screenRot(const App &app) { return quatYaw(app.screenYaw); }

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
	std::string body = app.language == lolvr::kLangFrench ?
		"Les fichiers du jeu sont introuvables.\n"
		"\n"
		"1. Branchez le casque à votre ordinateur.\n"
		"2. Copiez tout le contenu du CD de Lands of Lore\n"
		"   (les fichiers .PAK, dont GENERAL.PAK) dans ce\n"
		"   dossier du casque :\n"
		"\n"
		"   " + shortPath + "\n"
		"\n"
		"Le jeu démarrera tout seul dès que les fichiers\n"
		"seront en place. (Langue : engrenage au poignet gauche.)"
		:
		"The game files were not found.\n"
		"\n"
		"1. Connect the headset to your computer.\n"
		"2. Copy the whole content of the Lands of Lore CD\n"
		"   (the .PAK files, including GENERAL.PAK) into\n"
		"   this folder of the headset:\n"
		"\n"
		"   " + shortPath + "\n"
		"\n"
		"The game starts by itself as soon as the files\n"
		"are in place. (Language: gear on your left wrist.)";
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
			lolvr::HostConfig cfg;
			cfg.systemDir = app.systemDir;
			cfg.saveDir = app.saveDir;
			cfg.gameDir = app.gameDir;
			cfg.log = coreLog;
			lolvr::renderMessage(app.frame, "Lands of Lore VR", app.language == lolvr::kLangFrench ? "Démarrage du jeu…" : "Starting the game…");
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
const Limits kLimits[kEnvCount] = {{0.5f, 3.0f, 0.6f, 2.9f}, {0.4f, 5.0f, 0.4f, 5.0f}, {0.25f, 2.0f, 0.35f, 3.0f}};

// Fichier de réglages : une ligne par réglage, « nom valeurs… ». Les lignes inconnues sont ignorées.
void loadSettings(App &app, int &environment) {
	FILE *f = fopen(app.settingsPath.c_str(), "r");
	if (!f)
		return;
	char line[256];
	int screenIndex = 0, aimIndex = 0, loadedPanels = 0;
	// La disposition (écran, fenêtres) n'est reprise que si elle a été enregistrée avec la disposition
	// d'origine actuelle (« disposition 2 » : écran plus petit et plus proche, pour le donjon en 3D).
	bool layoutCurrent = false;
	while (fgets(line, sizeof(line), f))
		if (!strncmp(line, "disposition 2", 13))
			layoutCurrent = true;
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
				app.fireOn = iv != 0;
		} else if (!strcmp(name, "langue")) {
			if (sscanf(rest, "%d", &iv) == 1)
				app.language = iv == 1 ? lolvr::kLangFrench : lolvr::kLangEnglish;
		} else if (!strcmp(name, "donjon3d")) {
			if (sscanf(rest, "%d", &iv) == 1)
				app.dungeonMode = iv >= 0 && iv <= 2 ? iv : 1;
		} else if (!strcmp(name, "choixvue")) {
			if (sscanf(rest, "%d", &iv) == 1)
				app.viewChosen = iv != 0;
		} else if (!strcmp(name, "engrenage")) {
			if (sscanf(rest, "%d", &iv) == 1)
				app.gearKnown = iv != 0;
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
			if (ok && scale >= 0.0004f && scale <= 0.02f && fabsf(a) <= 3.2f && fabsf(b) <= 1.4f && c >= 0.2f && c <= 6.0f)
				for (int i = 0; i < lolvr::kUiPanelCount; i++)
					if (!strcmp(key, lolvr::Ui::panelKey(i))) {
						app.ui.placement[i] = {a, b, c, scale};
						loadedPanels |= 1 << i;
					}
		}
	}
	fclose(f);
	// La disposition n'est reprise que si elle est complète (sinon : disposition d'origine).
	const int needed = (1 << lolvr::kUiPanelCount) - 1 - (1 << lolvr::kPanelHelp);
	if (!(loadedPanels & (1 << lolvr::kPanelHelp)))
		app.ui.placement[lolvr::kPanelHelp] = lolvr::Ui::defaultHelpPlacement(0.0f);
	app.layoutLoaded = (loadedPanels & needed) == needed;
	if (app.layoutLoaded)
		app.ui.setHasLayout(true);
}

void saveSettings(App &app) {
	app.settingsDirty = false;
	FILE *f = fopen(app.settingsPath.c_str(), "w");
	if (!f)
		return;
	fprintf(f, "disposition 2\n");
	fprintf(f, "decor %d\n", app.dungeonMode ? app.classicEnv : app.environment);
	for (int i = 0; i < kEnvCount; i++)
		fprintf(f, "ecran %.3f %.3f\n", app.widths[i], app.distances[i]);
	fprintf(f, "feu %d\n", app.fireOn ? 1 : 0);
	fprintf(f, "panneaux %d\n", app.detached ? 1 : 0);
	fprintf(f, "langue %d\n", app.language);
	fprintf(f, "engrenage %d\n", app.gearKnown ? 1 : 0);
	fprintf(f, "donjon3d %d\n", app.dungeonMode);
	fprintf(f, "choixvue %d\n", app.viewChosen ? 1 : 0);
	for (int i = 0; i < kEnvCount; i++)
		fprintf(f, "visee %d %.4f %.4f\n", app.aimSaved[i] ? 1 : 0, app.aimYaw[i], app.aimPitch[i]);
	if (app.ui.hasLayout())
		for (int i = 0; i < lolvr::kUiPanelCount; i++) {
			const lolvr::UiPlacement &pl = app.ui.placement[i];
			fprintf(f, "panneau %s %.4f %.4f %.4f %.5f\n", lolvr::Ui::panelKey(i), pl.a, pl.b, pl.c, pl.scale);
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
	bool library = app.environment == lolvr::kEnvLibrary;
	if (newAnchor) {
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
	if (app.environment == lolvr::kEnvLibrary) {
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
	screenWidth(app) = clampf(screenWidth(app) * zoom, lim.wMin, lim.wMax);
	placeScreen(app, false);
}

bool usesPassthrough(int env) { return env == lolvr::kEnvPassthroughFrame || env == lolvr::kEnvPassthroughCrt; }

// Décor suivant, en sautant ceux qui demandent la réalité mixte quand elle n'est pas disponible.
int nextEnvironment(const App &app, int env) {
	for (int i = 0; i < kEnvCount; i++) {
		env = (env + 1) % kEnvCount;
		if (!usesPassthrough(env) || app.passthroughSupported)
			return env;
	}
	return lolvr::kEnvLibrary;
}

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
	bool wanted = app.gameStarted && gs.controlMode == 0 && !gs.automapOpen && !gs.menuOpen;
	app.charSheet = gs.charSheet;
	app.inDungeon = wanted && !gs.charSheet;
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
Vec3 viewCenter(const App &app) {
	return app.screenPos + Vec3{0, app.viewLift, 0} + rotate(screenRot(app), {app.viewShift, 0, 0});
}

// ---------------------------------------------------------------------------
// Ce que montre l'écran principal : l'image entière, ou la vue du donjon seule

// La ligne de messages dans l'image du jeu (320 x 200)
const float kBandX = 83, kBandY = 122, kBandW = 234, kBandH = 20;

void updateLayout(App &app, double now) {
	const float kViewX = 112, kViewY = 0, kViewW = 176, kViewH = 120; // vue du donjon dans l'image 320x200
	// On attend que l'état soit stable un court instant, pour ne pas clignoter pendant une transition.
	bool allowed = (app.detached || app.dungeonMode) && app.environment != lolvr::kEnvPassthroughCrt;
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
	float w = screenWidth(app);
	if (app.split) {
		// La vue seule est un peu moins haute que l'image entière et remontée, pour laisser la place
		// aux fenêtres du groupe et de l'inventaire dessous, dans le même plan.
		// La fiche d'un personnage couvre la vue et la colonne de droite : l'écran s'élargit vers la
		// droite pour la montrer en entier, et les fenêtres restent en place.
		float cw = app.charSheet ? 320.0f - kViewX : kViewW;
		app.cropX = kViewX; app.cropY = kViewY; app.cropW = cw; app.cropH = kViewH;
		// Mode immersif, boutique ou rencontre : la ligne de messages (les choix à faire) est collée
		// sous la scène, à sa largeur.
		const bool band = app.dungeonMode && app.level.sceneBusy && !app.charSheet;
		app.bandH = band ? kBandH * cw / kBandW : 0.0f;
		const float viewOnlyH = w * lolvr::kSplitViewHeight;
		app.dispH = viewOnlyH * (kViewH + app.bandH) / kViewH;
		app.dispW = viewOnlyH * cw / (kViewH * 1.2f);
		app.viewLift = w * lolvr::kSplitViewLift - (app.dispH - viewOnlyH) * 0.5f;
		app.viewShift = (app.dispW - viewOnlyH * kViewW / (kViewH * 1.2f)) * 0.5f;
	} else {
		app.viewShift = 0.0f;
		app.bandH = 0.0f;
		app.cropX = 0; app.cropY = 0; app.cropW = 320; app.cropH = 200;
		app.dispW = w;
		app.dispH = w * 0.75f;
		app.viewLift = 0.0f;
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
	if (l.level <= 0)
		return false;
	const float S = lolvr::kBlockSize, H = lolvr::kWallHeight;
	Mat4 model = lolvr::dungeonModel(app.roomPos, app.dunYaw, app.partyX, app.partyZ, app.partyYaw);
	Vec3 c0{model.m[0], model.m[1], model.m[2]}, c1{model.m[4], model.m[5], model.m[6]}, c2{model.m[8], model.m[9], model.m[10]};
	Vec3 t{model.m[12], model.m[13], model.m[14]};
	auto toLevel = [&](Vec3 p) { Vec3 r = p - t; return Vec3{dot(c0, r), dot(c1, r), dot(c2, r)}; };
	auto dirLevel = [&](Vec3 v) { return Vec3{dot(c0, v), dot(c1, v), dot(c2, v)}; };
	Vec3 o = toLevel(tip), v = dirLevel(dir);
	static const int dx[4] = {0, 1, 0, -1}, dz[4] = {-1, 0, 1, 0};
	const int d = l.direction & 3;
	Vec3 f{(float)dx[d], 0.0f, (float)dz[d]}, r{-f.z, 0.0f, f.x};
	float cx, cz;
	lolvr::blockCenter(l.block, cx, cz);
	Vec3 center{cx, 0.0f, cz};
	int ahead = l.block + dz[d] * 32 + dx[d];
	const uint8_t faceType = ahead >= 0 && ahead < 1024 ? l.walls[ahead * 4 + ((d + 2) & 3)] : 0;
	bool faceWall = faceType != 0 && app.dunTex.kind[faceType] == lolvr::kWallFace;
	// Le mur (ou le cadre de porte) se tient à cette profondeur dans la case devant : 1 = sur sa face.
	const float fr = faceWall ? app.dunTex.faceDepth[faceType] : 1.0f;
	Vec3 hit;
	float px = -1, py = -1;
	// 1. Le mur droit devant (sa face tournée vers le groupe)
	if (faceWall) {
		float den = dot(v, f);
		if (den > 1e-4f) {
			Vec3 facePt = center + f * (S * 0.5f * fr);
			float tt = dot(facePt - o, f) / den;
			if (tt > 0.0f) {
				hit = o + v * tt;
				// L'image du mur (128 x 96, horizon au milieu) est agrandie de fr autour de mi-hauteur.
				float u = 0.5f + dot(hit - facePt, r) / (S * fr);
				float row = 48.0f - (hit.y - H * 0.5f) / (H * 0.5f * fr) * 48.0f;
				if (u >= 0.0f && u <= 1.0f && row >= 0.0f && row <= 96.0f && hit.y >= 0.0f && hit.y <= H) {
					px = 136.0f + u * 128.0f;
					py = 8.0f + row;
				}
			}
		}
	}
	// 2. Le sol, de sous le joueur jusqu'au bout de la case devant (ou jusqu'au mur)
	if (px < 0.0f && v.y < -1e-4f) {
		float tt = -o.y / v.y;
		hit = o + v * tt;
		float fw = dot(hit - center, f) / S, lat = dot(hit - center, r) / S;
		float maxF = faceWall ? 0.5f * fr : 1.5f;
		if (fw >= 0.0f && fw <= maxF && fabsf(lat) <= 0.6f) {
			float y, w;
			if (fw >= 0.5f) {
				float k = fw - 0.5f;
				y = 104.0f - 24.0f * k;
				w = 128.0f - 48.0f * k;
			} else {
				y = 120.0f - 32.0f * fw;
				w = 176.0f - 96.0f * fw;
			}
			px = 112.0f + 88.0f + clampf(lat, -0.5f, 0.5f) * w;
			py = clampf(y, 1.0f, 119.0f);
			// Le point du sol est montré à plat
			f = Vec3{0, 1, 0};
		}
	}
	// 3. Ailleurs devant le groupe (au loin, en l'air) : la vue du jeu, selon l'angle. C'est là qu'on
	// lance un objet tenu (on peut ainsi sortir un objet de l'inventaire vers le donjon).
	if (px < 0.0f) {
		Vec3 flat{v.x, 0.0f, v.z};
		float fl = length(flat);
		if (fl > 1e-3f && dot(flat, f) / fl > 0.5f) {
			float side = dot(v, r) / dot(v, f), up = v.y / dot(v, f);
			// Le mur à une case montre 128 x 96 pixels pour S x H mètres, à S/2 du groupe.
			px = clampf(200.0f + side * 64.0f, 113.0f, 287.0f);
			py = clampf(56.0f - up * 48.0f * S / H, 2.0f, 100.0f);
			hit = o + v * 3.0f;
		}
	}
	if (px < 0.0f)
		return false;
	gu = px / 320.0f;
	gv = py / 200.0f;
	hitW = transformPoint(model, hit);
	rightW = normalize(transformDir(model, r));
	upW = f.y > 0.5f ? normalize(transformDir(model, Vec3{(float)dx[d], 0.0f, (float)dz[d]})) : normalize(transformDir(model, Vec3{0, 1, 0}));
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
	in.triggerPressed = pointer && triggerPressed;
	in.stickX = rightStick.x;
	in.stickY = rightStick.y;
	in.screenPos = viewCenter(app);
	in.basePos = app.screenPos;
	in.fullW = screenWidth(app);
	in.roomClamp = app.environment == lolvr::kEnvLibrary;
	in.roomPos = app.roomPos;
	in.roomYaw = app.anchorYaw;
	in.screenRot = screenRot(app);
	in.dispW = app.dispW;
	in.dispH = app.dispH;
	in.cropX = app.cropX; in.cropY = app.cropY; in.cropW = app.cropW; in.cropH = app.cropH;
	in.bandH = app.bandH;
	in.crt = app.environment == lolvr::kEnvPassthroughCrt;
	in.split = app.split;
	in.keyboardOpen = app.keyboardOpen;
	if (app.inDungeon && app.characters > 0) {
		in.partyMark[0] = (float)portraitX(app.characters, app.selectedChar);
		in.partyMark[1] = 143.0f;
		in.partyMark[2] = 66.0f;
		in.partyMark[3] = 34.0f;
	}
	app.ui.setOptions(app.fireOn, app.detached);
	app.ui.setDungeonOption(app.dungeonMode);
	app.ui.setLanguage(app.language);
	// Menu du poignet : sur la main gauche, quand on la regarde.
	in.wristValid = app.aimValid[kLeft];
	if (in.wristValid) {
		in.wristPos = toVec3(app.aimPose[kLeft].position);
		in.wristRot = toQuat(app.aimPose[kLeft].orientation);
	}
	in.headPos = app.headPos;
	in.headDir = rotate(app.headRot, {0, 0, -1});
	in.helpOpen = app.helpOpen;
	in.compassBlank = app.split && app.charSheet;
	if (!app.frame.rgba.empty()) {
		in.gameRgba = app.frame.rgba.data();
		in.gameW = app.frame.width;
		in.gameH = app.frame.height;
	}
	in.gameSerial = app.frameSerial;
	in.characters = app.inDungeon ? app.characters : 0;
	for (int i = 0; i < in.characters && i < 4; i++)
		in.portraitX[i] = portraitX(in.characters, i);
	in.gearHint = !app.gearKnown && !app.chooserOpen;
	in.chooserOpen = app.chooserOpen;
	in.screenHidden = !app.screenShown;
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
		}
	}

	if (out.screenDragging)
		dragScreen(app, out.screenCenter - Vec3{0, app.viewLift, 0} - rotate(screenRot(app), {app.viewShift, 0, 0}), out.screenZoom);
	if (out.screenResizing)
		resizeScreen(app, out.screenZoom);
	for (int i = 0; i < out.keyCount; i++)
		typeKey(app, out.keys[i], out.keyMods[i], now);
	switch (out.action) {
	case lolvr::kUiActionToggleFire:
		app.fireOn = !app.fireOn;
		app.settingsDirty = true;
		break;
	case lolvr::kUiActionToggleHelp:
		app.helpOpen = !app.helpOpen;
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
		app.detached = !app.detached;
		app.settingsDirty = true;
		break;
	case lolvr::kUiActionResetLayout:
		app.ui.resetLayout(in);
		app.settingsDirty = true;
		break;
	case lolvr::kUiActionCloseKeyboard:
		app.keyboardOpen = false;
		break;
	case lolvr::kUiActionToggleDungeon:
		setViewMode(app, app.dungeonMode ? 0 : 1, time);
		break;
	case lolvr::kUiActionToggleScreenAlways:
		setViewMode(app, app.dungeonMode == 2 ? 1 : 2, time);
		break;
	case lolvr::kUiActionChooseClassic:
	case lolvr::kUiActionChooseImmersive:
		setViewMode(app, out.action == lolvr::kUiActionChooseImmersive ? 1 : 0, time);
		app.viewChosen = true;
		app.chooserOpen = false;
		pulse(app, kRight, 0.3f, 0.03f);
		break;
	case lolvr::kUiActionToggleKeyboard:
		app.keyboardOpen = !app.keyboardOpen;
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

	// Clic du joystick droit : recentrer l'écran (et la pièce) devant soi.
	bool recenter = getBool(app, app.stickClickAction, kRight);
	if (app.headValid && recenter && !app.recenterDown) {
		updateFloor(app, time);
		placeScreen(app, true);
		app.dunYaw = app.anchorYaw; // le groupe regarde de nouveau droit devant
	}
	app.recenterDown = recenter;

	// Position des manettes (les gants les suivent)
	for (int h = 0; h < 2; h++) {
		XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
		app.aimValid[h] = XR_SUCCEEDED(xrLocateSpace(app.aimSpaces[h], app.localSpace, time, &loc)) &&
		                  (loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) &&
		                  (loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT);
		if (app.aimValid[h])
			app.aimPose[h] = loc.pose;
	}

	// Gâchette = clic gauche, poignée = clic droit. La main qui clique devient le pointeur.
	bool clicked[2] = {false, false};
	for (int h = 0; h < 2; h++) {
		app.triggerValue[h] = getFloat(app, app.triggerAction, h);
		app.squeezeValue[h] = getFloat(app, app.squeezeAction, h);
		bool trigger = threshold(app.triggerDown[h], app.triggerValue[h]);
		bool squeeze = threshold(app.squeezeDown[h], app.squeezeValue[h]);
		bool pressedNow = (trigger && !app.triggerDown[h]) || (squeeze && !app.squeezeDown[h]);
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
	if (now < app.synthClickUntil) {
		// Bouton de magie : un clic bref sur le bouton du personnage, à la place du pointeur.
		CoreHost::get().setPointer(app.synthU, app.synthV);
		CoreHost::get().setMouseButtons(app.synthClickUntil - now > 0.06, false);
	} else {
		if (ui.pointerValid)
			CoreHost::get().setPointer(ui.gu, ui.gv);
		// Clic droit : la poignée de la main qui vise, ou le bouton X (boire une potion, utiliser un objet).
		bool xRight = getBool(app, app.xAction, -1) && ui.pointerValid;
		CoreHost::get().setMouseButtons(app.triggerDown[hand] && toGame, (app.squeezeDown[hand] && toGame) || xRight);
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
	bool zoomMode = getBool(app, app.stickClickAction, kLeft);
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
		if (gaze3d) {
			Vec3 f = rotate(app.headRot, {0, 0, -1});
			gaze = atan2f(-f.x, -f.z);
		}
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
	if (zoomMode) {
		const Limits &lim = kLimits[app.environment];
		if (fabsf(right.y) > 0.3f && fabsf(right.y) >= fabsf(right.x)) {
			screenWidth(app) = clampf(w * (1.0f + right.y * 0.8f * dt), lim.wMin, lim.wMax);
			app.zoomUsed = true;
		} else if (fabsf(right.x) > 0.3f) {
			screenDistance(app) = clampf(screenDistance(app) * (1.0f + right.x * 0.6f * dt), lim.dMin, lim.dMax);
			app.zoomUsed = true;
		}
		if (app.zoomUsed)
			placeScreen(app, false);
	} else if (app.zoomDown) {
		if (!app.zoomUsed && now - app.zoomSince < 0.6 && !app.dungeonMode) { // le mode immersif n'a qu'une vue
			setEnvironment(app, nextEnvironment(app, app.environment), time);
			pulse(app, kLeft, 0.3f, 0.03f);
		}
		app.settingsDirty = true;
	}
	app.zoomDown = zoomMode;

	// Boutons
	// A et B : dans le donjon, attaque et magie du personnage choisi (joystick droit haut/bas pour
	// en changer) ; partout ailleurs (menus, cinématiques, carte, fiche), Entrée et Échap.
	bool aDown = getBool(app, app.aAction, -1), bDown = getBool(app, app.bAction, -1);
	bool combat = app.inDungeon && app.characters > 0;
	if (combat) {
		if (aDown && !app.attackDown) {
			typeKey(app, (unsigned)RETROK_F1 + (unsigned)app.selectedChar, 0, now);
			pulse(app, kRight, 0.35f, 0.03f);
		}
		if (bDown && !app.magicDown) {
			app.synthU = ((float)portraitX(app.characters, app.selectedChar) + 44.0f + 11.0f) / 320.0f;
			app.synthV = (160.0f + 8.5f) / 200.0f;
			app.synthClickUntil = now + 0.16;
			pulse(app, kRight, 0.25f, 0.03f);
		}
		bool flick = !stickBusy && fabsf(right.y) > 0.75f && fabsf(right.x) < 0.40f;
		if (flick && !app.charFlick) {
			int step = right.y > 0 ? 1 : app.characters - 1;
			app.selectedChar = (app.selectedChar + step) % app.characters;
			pulse(app, kRight, 0.30f, 0.02f);
		}
		app.charFlick = flick || (app.charFlick && fabsf(right.y) > 0.45f);
	} else {
		app.charFlick = false;
	}
	app.attackDown = aDown;
	app.magicDown = bDown;
	setKey(app.kEnter, aDown && !combat, RETROK_RETURN);
	setKey(app.kEsc, bDown && !combat, RETROK_ESCAPE);
	setKey(app.kMap, getBool(app, app.yAction, -1), RETROK_m);
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
			glUniform4f(app.blitRect, kBandX * sx, kBandY * sy, kBandW * sx, kBandH * sy);
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
void updateDungeon(App &app, float dt) {
	app.dungeonVisible = false;
	if (!app.dungeonOk || !app.dungeonMode || !app.coreStarted || !app.gameStarted)
		return;
	lolvr::LevelSnapshot &l = app.level;
	if (!CoreHost::get().levelSnapshot(l) || l.level <= 0 || l.vmp.empty())
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
		}
	}
	g.outdoor = isOutdoorTileset(app.tileset);
	const bool gfxChanged = l.gfxSerial != app.builtGfx || l.wallCaptureSerial != app.builtCapture;
	if (gfxChanged) {
		app.builtCapture = l.wallCaptureSerial;
		lolvr::decodeDungeonTextures(g, app.dunTex);
		app.dungeon.setTextures(app.dunTex);
		app.builtGfx = l.gfxSerial;
		LOGI("donjon 3D : niveau %d, %d textures", l.level, app.dunTex.layers);
	}
	if (gfxChanged || l.wallsSerial != app.builtWalls || (l.direction & 3) != app.builtDir) {
		app.builtDir = l.direction & 3;
		std::vector<lolvr::DungeonVertex> verts;
		lolvr::buildDungeonMesh(l.walls, g, app.dunTex, verts, app.builtDir);
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
			// Un objet rangé dans un mur (niche, cachette) : le jeu ne le montre que si ce côté du mur
			// laisse voir les objets (drapeau 0x80), comme vu depuis la case voisine. On le pose alors
			// contre le mur, là où il apparaît dans le jeu ; sinon il reste caché.
			const int ob = ((s.y >> 8) & 31) * 32 + ((s.x >> 8) & 31);
			if (s.kind == 1 && lolvr::isSolidBlock(l.walls, ob, g)) {
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
				d.x = vx + (d.x - vx) * r + nx * 0.04f;
				d.z = vz + (d.z - vz) * r + nz * 0.04f;
				d.base = eyeY * (1.0f - r);
				d.width *= r;
				d.height *= r;
				d.offset *= r;
				d.ceiling = false;
			}
			list.push_back(d);
		}
		app.dungeon.setSprites(list);
	}
	// Le groupe glisse vers sa case (quelques dixièmes de seconde). Les virages ne glissent pas (cela
	// donne mal au cœur) : un bref fondu au noir, et la pièce a pivoté. Quand le groupe se tourne de
	// lui-même vers l'endroit que le joueur regarde, rien ne bouge. Un changement de niveau ou une
	// téléportation pose le groupe directement.
	if (!app.dunYawSet) {
		app.dunYaw = app.anchorYaw;
		app.dunYawSet = true;
	}
	// Porte d'une boutique, entrée d'une autre région : le jeu fait entrer le groupe dans un bloc
	// plein. La vue reste devant, face à l'image ; le jeu montre la scène ou change de région.
	if (gfxChanged || app.shownLevel != l.level || app.visBlock < 0 || !lolvr::isHoldBlock(l.walls, l.block, g))
		app.visBlock = l.block;
	float tx, tz;
	lolvr::blockCenter(app.visBlock, tx, tz);
	const int dir = l.direction & 3;
	float dx = tx - app.partyX, dz = tz - app.partyZ;
	bool snap = gfxChanged || app.shownLevel != l.level || app.shownDir < 0 ||
	            dx * dx + dz * dz > 1.6f * lolvr::kBlockSize * 1.6f * lolvr::kBlockSize;
	auto wrapPi = [](float a) {
		while (a > 3.14159265f) a -= 6.2831853f;
		while (a < -3.14159265f) a += 6.2831853f;
		return a;
	};
	const float dyaw = wrapPi((float)dir * 1.5707963f - app.partyYaw);
	if (snap) {
		app.partyX = tx;
		app.partyZ = tz;
		app.partyYaw += dyaw;
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
				app.alignTurn = 0;
			} else {
				app.turnTarget = app.partyYaw + dyaw;
				if (app.turnFade <= 0.0f)
					app.turnFade = 0.0001f;
			}
		}
	}
	app.shownDir = dir;
	if (app.alignTurn != 0 && nowSeconds() > app.alignUntil)
		app.alignTurn = 0;
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
			inWall = bx < 0 || bx > 31 || bz < 0 || bz > 31 || lolvr::isSolidBlock(l.walls, bz * 32 + bx, g);
		}
		app.wallDim += ((inWall ? 1.0f : 0.0f) - app.wallDim) * clampf(dt * 8.0f, 0.0f, 1.0f);
		light *= 1.0f - 0.92f * app.wallDim;
	}
	// Pénombre du jeu (lieux sombres, lampe, fondus) : la clarté de la palette à l'écran.
	app.lightLevel += ((float)l.light / 256.0f - app.lightLevel) * clampf(dt * 6.0f, 0.0f, 1.0f);
	light *= app.lightLevel;
	app.dungeonLight = light;
	app.shownLevel = l.level;
	app.dungeonVisible = app.dungeon.hasContent();
}

// Donjon 3D : le grand écran n'apparaît que lorsqu'on en a besoin. Le donjon en 3D montre les murs,
// les monstres et les objets ; l'écran revient pour les boutiques, les dialogues et les scènes, la
// carte, les menus et la fiche d'un personnage, et reste un moment après.
void updateScreenShown(App &app, double now) {
	// La carte, une fiche, un menu : l'écran disparaît dès qu'ils se ferment. Une scène (boutique,
	// rencontre) : un court instant de grâce, le temps qu'une scène suivante enchaîne.
	bool need = !app.dungeonVisible || app.dungeonMode == 2 || !app.splitWanted || app.charSheet || app.automapOpen;
	if (app.level.sceneBusy)
		app.screenNeededAt = now;
	bool show = need || app.level.sceneBusy || now - app.screenNeededAt < 0.1;
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
	s.screenVisible = app.screenShown;
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
		ambience.setMood(visible && app.environment == lolvr::kEnvLibrary && app.scene.ready() && !app.dungeonVisible, app.shownTheme);

		// Calques, du fond vers l'avant : vraie pièce (réalité mixte), image du jeu, décor.
		if (app.passthroughActive) {
			passthroughLayer.flags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
			passthroughLayer.space = XR_NULL_HANDLE;
			passthroughLayer.layerHandle = app.passthroughLayer;
			layers.push_back((const XrCompositionLayerBaseHeader *)&passthroughLayer);
		}
		if (app.screenPlaced && app.screenShown && Scene::usesQuadLayer(app.environment) && renderQuad(app)) {
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
