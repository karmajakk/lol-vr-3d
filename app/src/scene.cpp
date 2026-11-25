// lol-vr — rendu du décor 3D : bibliothèque, cadre, moniteur cathodique, gants, pointeur.

#include "scene.h"
#include "dungeon.h"

#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include <GLES3/gl3.h>

#include "mesh.h"
#include "proctex.h"
#include "room.h"

#ifdef __ANDROID__
#include <android/log.h>
#define SCENE_LOG(...) __android_log_print(ANDROID_LOG_INFO, "lolvr-scene", __VA_ARGS__)
#else
#define SCENE_LOG(...) (fprintf(stderr, __VA_ARGS__), fprintf(stderr, "\n"))
#endif

#ifndef GL_SRGB8_ALPHA8
#define GL_SRGB8_ALPHA8 0x8C43
#endif
#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#endif

namespace lolvr {

namespace {

const int kMaxLights = 8;

// --- Shaders ---------------------------------------------------------------

const char *kLitVs = R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aUvl;
layout(location = 3) in vec4 aTint;
layout(location = 4) in vec4 aMisc;
uniform mat4 uViewProj;
uniform mat4 uModel;
uniform vec4 uLightPos[8];
uniform vec3 uLightCol[8];
uniform vec3 uAmbSky;
uniform vec3 uAmbGround;
uniform vec3 uCamPos;
out vec3 vUvl;
out vec3 vLight;
out vec3 vTint;
out float vEmissive;
void main() {
	vec4 wp = uModel * vec4(aPos, 1.0);
	vec3 n = normalize(mat3(uModel) * aNormal);
	vec3 light = mix(uAmbGround, uAmbSky, n.y * 0.5 + 0.5);
	for (int i = 0; i < 8; i++) {
		vec3 d = uLightPos[i].xyz - wp.xyz;
		float dist2 = dot(d, d);
		float r = uLightPos[i].w;
		float q = dist2 / (r * r);
		float att = 1.0 / (1.0 + q + 0.12 * q * q);
		float ndl = max((dot(n, d) * inversesqrt(dist2 + 0.0001) + 0.30) / 1.30, 0.0);
		light += uLightCol[i] * (att * ndl);
	}
	// Léger reflet rasant : donne du relief aux objets ronds.
	vec3 toCam = normalize(uCamPos - wp.xyz);
	float rim = pow(1.0 - max(dot(n, toCam), 0.0), 3.0);
	light *= 1.0 + 0.35 * rim;
	vLight = light * aMisc.x;
	vEmissive = aMisc.y;
	vTint = pow(aTint.rgb, vec3(2.2));
	vUvl = aUvl;
	gl_Position = uViewProj * wp;
}
)";

const char *kLitFs = R"(
precision highp float;
precision mediump sampler2DArray;
uniform sampler2DArray uTex;
uniform float uExposure;
uniform float uFade;
uniform float uAlphaMode;
in vec3 vUvl;
in vec3 vLight;
in vec3 vTint;
in float vEmissive;
out vec4 oColor;
void main() {
	vec4 t = texture(uTex, vUvl);
#ifdef MANUAL_SRGB
	t.rgb = pow(t.rgb, vec3(2.2));
#endif
	float tintAmount = mix(t.a, 1.0, uAlphaMode);
	vec3 base = t.rgb * mix(vec3(1.0), vTint, tintAmount);
	vec3 c = base * mix(vLight * uExposure, vec3(1.0), vEmissive);
	// Compression douce des hautes lumières
	c = c * (1.0 + c * 0.16) / (1.0 + c);
	c *= uFade * 1.30;
	float alpha = mix(1.0, t.a, uAlphaMode);
#ifdef MANUAL_SRGB
	c = pow(c, vec3(1.0 / 2.2));
#endif
	oColor = vec4(c, alpha);
}
)";

const char *kFxVs = R"(
layout(location = 0) in vec3 aCenter;
layout(location = 1) in vec2 aCorner;
layout(location = 2) in vec2 aUv;
layout(location = 3) in vec4 aColor;
layout(location = 4) in vec3 aParams;
uniform mat4 uViewProj;
uniform vec3 uCamRight;
uniform vec3 uCamUp;
uniform vec3 uCamPos;
out vec2 vUv;
out vec4 vColor;
out float vType;
out float vPhase;
void main() {
	vec3 p = aCenter;
	if (aParams.y > 1.5) {
		vec3 r = normalize(cross(vec3(0.0, 1.0, 0.0), uCamPos - aCenter));
		p += r * aCorner.x + vec3(0.0, aCorner.y, 0.0);
	} else if (aParams.y > 0.5) {
		p += uCamRight * aCorner.x + uCamUp * aCorner.y;
	}
	vUv = aUv;
	vColor = aColor;
	vType = aParams.x;
	vPhase = aParams.z;
	gl_Position = uViewProj * vec4(p, 1.0);
}
)";

const char *kFxFs = R"(
precision highp float;
uniform float uTime;
uniform float uFade;
in vec2 vUv;
in vec4 vColor;
in float vType;
in float vPhase;
out vec4 oColor;
float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float noise(vec2 p) {
	vec2 i = floor(p), f = fract(p);
	f = f * f * (3.0 - 2.0 * f);
	return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), f.x), mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), f.x), f.y);
}
void main() {
	vec3 col = vColor.rgb;
	float a = 0.0;
	float shadow = 0.0;
	if (vType < 0.5) {
		// Halo
		float d = length(vUv - 0.5) * 2.0;
		float k = max(1.0 - d, 0.0);
		a = k * k * (0.35 + 0.65 * k);
	} else if (vType < 1.5) {
		// Flamme
		float t = uTime * 1.25 + vPhase * 7.0;
		float n1 = noise(vec2(vUv.x * 3.5 + vPhase * 5.0, vUv.y * 2.6 - t * 2.0));
		float n2 = noise(vec2(vUv.x * 7.0 - vPhase * 3.0, vUv.y * 5.0 - t * 3.3));
		float x = (vUv.x - 0.5) * 2.0 + (n1 - 0.5) * 0.75 * vUv.y;
		float width = (1.0 - vUv.y) * (0.50 + 0.40 * n2) + 0.02;
		float body = smoothstep(width, width * 0.25, abs(x));
		body *= smoothstep(0.0, 0.10, vUv.y) * smoothstep(1.0, 0.55, vUv.y + (n2 - 0.5) * 0.35);
		float core = body * body;
		col = mix(vec3(1.0, 0.22, 0.02), vec3(1.0, 0.62, 0.14), core);
		col = mix(col, vec3(1.0, 0.92, 0.62), core * core * (1.0 - vUv.y));
		col *= vColor.rgb;
		a = body;
	} else if (vType < 2.5) {
		// Ombre de contact
		float d = length(vUv - 0.5) * 2.0;
		shadow = smoothstep(1.0, 0.25, d) * vColor.a;
	} else if (vType < 3.5) {
		// Faisceau : uv.x en travers, uv.y le long
		float across = max(1.0 - abs(vUv.x - 0.5) * 2.0, 0.0);
		float along = 1.0 - vUv.y;
		a = across * across * along * along;
	} else {
		// Rectangle à bords doux
		vec2 e = min(vUv, 1.0 - vUv);
		a = smoothstep(0.0, 0.30, e.x) * smoothstep(0.0, 0.30, e.y);
	}
	vec3 rgb = col * (a * vColor.a * uFade);
#ifdef MANUAL_SRGB
	rgb = pow(rgb, vec3(1.0 / 2.2));
#endif
	// Alpha prémultiplié : alpha 0 = lumière ajoutée, alpha > 0 = assombrissement (ombres).
	oColor = vec4(rgb, shadow * uFade);
}
)";

const char *kCrtVs = R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aUvl;
uniform mat4 uViewProj;
uniform mat4 uModel;
uniform vec3 uCamPos;
out vec2 vUv;
out float vFresnel;
void main() {
	vec4 wp = uModel * vec4(aPos, 1.0);
	vec3 n = normalize(mat3(uModel) * aNormal);
	vec3 toCam = normalize(uCamPos - wp.xyz);
	vFresnel = pow(1.0 - max(dot(n, toCam), 0.0), 3.0);
	vUv = aUvl.xy;
	gl_Position = uViewProj * wp;
}
)";

const char *kCrtFs = R"(
precision highp float;
uniform sampler2D uGame;
uniform vec2 uGameSize;
uniform float uTime;
uniform float uHasGame;
in vec2 vUv;
in float vFresnel;
out vec4 oColor;
void main() {
	// Agrandissement net des pixels du jeu (un peu plus doux à l'horizontale, comme un vrai tube).
	vec2 texel = vUv * uGameSize;
	vec2 ti = floor(texel);
	vec2 tf = texel - ti;
	vec2 sharp = vec2(2.0, 4.0);
	vec2 region = 0.5 - 0.5 / sharp;
	vec2 tc = tf - 0.5;
	vec2 fpp = (tc - clamp(tc, -region, region)) * sharp + 0.5;
	vec3 c = texture(uGame, (ti + fpp) / uGameSize).rgb * uHasGame;
#ifdef MANUAL_SRGB
	c = pow(c, vec3(2.2));
#endif
	// Lignes de balayage, estompées quand elles deviennent trop fines pour être dessinées proprement
	float line = vUv.y * uGameSize.y;
	float fw = fwidth(line);
	float strength = clamp(1.0 - (fw - 0.22) * 3.0, 0.0, 1.0) * 0.42;
	float scan = 1.0 - strength * (0.5 - 0.5 * cos(line * 6.2831853));
	// Grille de luminophores (très légère, visible de près)
	float px = vUv.x * uGameSize.x * 3.0;
	float fwx = fwidth(px);
	float mask = 1.0 - clamp(1.0 - (fwx - 0.25) * 3.0, 0.0, 1.0) * 0.10 * (0.5 - 0.5 * cos(px * 6.2831853));
	vec2 q = vUv - 0.5;
	float vig = 1.0 - dot(q, q) * 0.85;
	float flicker = 1.0 + 0.012 * sin(uTime * 53.0);
	c = (c * 1.22 + 0.010) * scan * mask * vig * flicker;
	// Reflet du verre
	vec3 refl = vec3(0.55, 0.60, 0.65) * (0.02 + 0.22 * vFresnel);
	float sheen = smoothstep(0.55, 0.0, length(vUv - vec2(0.22, 0.16)));
	c += refl + vec3(0.035) * sheen;
#ifdef MANUAL_SRGB
	c = pow(c, vec3(1.0 / 2.2));
#endif
	oColor = vec4(c, 1.0);
}
)";

// Panneau d'interface : une portion de l'image du jeu, agrandie avec des pixels nets, dans une
// bordure en laiton. uRect = portion (u0, v0, u1, v1) ; uBorder = largeur de la bordure en uv.
const char *kPanelVs = R"(
layout(location = 0) in vec3 aPos;
layout(location = 2) in vec3 aUvl;
uniform mat4 uViewProj;
out vec2 vUv;
void main() {
	vUv = aUvl.xy;
	gl_Position = uViewProj * vec4(aPos, 1.0);
}
)";

const char *kPanelFs = R"(
precision highp float;
uniform sampler2D uGame;
uniform vec2 uGameSize;
uniform vec4 uRect;
uniform vec2 uBorder;
uniform float uAlpha;
uniform vec4 uHot;
uniform float uMode;
uniform float uGlow;
in vec2 vUv;
out vec4 oColor;
void main() {
	vec3 c;
	if (uMode > 0.5) {
		// Poignée : barrette de laiton striée, plus claire quand on la vise.
		vec3 brass = mix(vec3(0.30, 0.17, 0.04), vec3(0.90, 0.62, 0.20), smoothstep(0.0, 1.0, vUv.y));
		float groove = smoothstep(0.30, 0.40, abs(fract(vUv.x * 7.0) - 0.5));
		brass *= 0.70 + 0.30 * groove;
		vec2 e = min(vUv, 1.0 - vUv);
		brass *= 0.55 + 0.45 * smoothstep(0.0, 0.12, min(e.x * 4.0, e.y));
		brass = brass * (1.0 + 1.6 * uGlow) + vec3(0.25, 0.18, 0.06) * uGlow;
		oColor = vec4(brass * uAlpha, uAlpha);
		return;
	}
	if (uGlow > 2.5) {
		// Découpe (le curseur du jeu) : pixels nets, le fond transparent n'est pas dessiné.
		vec4 t = texture(uGame, (floor(vUv * uGameSize) + 0.5) / uGameSize);
		if (t.a < 0.5)
			discard;
		oColor = vec4(t.rgb * uAlpha, uAlpha);
		return;
	}
	vec2 outside = max(uRect.xy - vUv, vUv - uRect.zw);
	if (outside.x > 0.0 || outside.y > 0.0) {
		// Une ligne sombre, sobre, comme celles qui entourent les zones de l'interface du jeu (un
		// soupçon de brun au bord extérieur pour qu'elle ne soit pas qu'un trou noir).
		float e = clamp(max(outside.x / uBorder.x, outside.y / uBorder.y), 0.0, 1.0);
		c = mix(vec3(0.035, 0.025, 0.018), vec3(0.17, 0.12, 0.08), smoothstep(0.55, 1.0, e));
	} else {
		// Pixels nets quand l'image est grande ; quand un pixel du jeu tient sur moins de deux
		// pixels du casque (fenêtre lointaine ou vue de biais), une moyenne de plusieurs prises :
		// sinon les traits d'un pixel (les lettres l, r, i) tombent entre deux et disparaissent.
		vec2 texel = vUv * uGameSize;
		vec2 dd = fwidth(texel);
		float spp = max(max(dd.x, dd.y), 1e-4);
		if (spp > 0.5) {
			vec2 o = dd * 0.25;
			c = (texture(uGame, clamp((texel + vec2(-o.x, -o.y)) / uGameSize, uRect.xy, uRect.zw)).rgb +
			     texture(uGame, clamp((texel + vec2(o.x, -o.y)) / uGameSize, uRect.xy, uRect.zw)).rgb +
			     texture(uGame, clamp((texel + vec2(-o.x, o.y)) / uGameSize, uRect.xy, uRect.zw)).rgb +
			     texture(uGame, clamp((texel + vec2(o.x, o.y)) / uGameSize, uRect.xy, uRect.zw)).rgb) * 0.25;
		} else {
			float k = 1.0 / spp;
			vec2 ti = floor(texel);
			vec2 tc = texel - ti - 0.5;
			vec2 region = vec2(0.5 - 0.5 / k);
			vec2 fpp = (tc - clamp(tc, -region, region)) * k + 0.5;
			c = texture(uGame, clamp((ti + fpp) / uGameSize, uRect.xy, uRect.zw)).rgb;
		}
		if (uGlow > 0.5 && uGlow < 1.5)
			c = vec3(0.0);
		if (vUv.x > uHot.x && vUv.x < uHot.z && vUv.y > uHot.y && vUv.y < uHot.w) {
			if (uGlow > 1.5) {
				// Simple encadré jaune, épais d'un pixel et demi du jeu
				vec2 edge = min(vUv - uHot.xy, uHot.zw - vUv) * uGameSize;
				if (min(edge.x, edge.y) < 1.5)
					c = vec3(1.0, 0.85, 0.20);
			} else {
				c = mix(c, vec3(1.0, 0.80, 0.40), 0.30) + vec3(0.06, 0.04, 0.0);
			}
		}
	}
	oColor = vec4(c * uAlpha, uAlpha);
}
)";

const char *kFlatVs = R"(
layout(location = 0) in vec3 aPos;
uniform mat4 uMvp;
void main() {
	gl_Position = uMvp * vec4(aPos, 1.0);
}
)";

const char *kFlatFs = R"(
precision mediump float;
uniform vec4 uColor;
out vec4 oColor;
void main() {
	oColor = uColor;
}
)";

GLuint compile(GLenum type, const std::string &src) {
	GLuint s = glCreateShader(type);
	const char *p = src.c_str();
	glShaderSource(s, 1, &p, nullptr);
	glCompileShader(s);
	GLint ok = 0;
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[2048] = {0};
		glGetShaderInfoLog(s, sizeof(log) - 1, nullptr, log);
		SCENE_LOG("erreur de shader : %s", log);
	}
	return s;
}

GLuint link(const char *vs, const char *fs, bool manualSrgb) {
	std::string head = "#version 300 es\n";
	if (manualSrgb)
		head += "#define MANUAL_SRGB 1\n";
	GLuint p = glCreateProgram();
	GLuint v = compile(GL_VERTEX_SHADER, head + vs);
	GLuint f = compile(GL_FRAGMENT_SHADER, head + fs);
	glAttachShader(p, v);
	glAttachShader(p, f);
	glLinkProgram(p);
	GLint ok = 0;
	glGetProgramiv(p, GL_LINK_STATUS, &ok);
	if (!ok) {
		char log[2048] = {0};
		glGetProgramInfoLog(p, sizeof(log) - 1, nullptr, log);
		SCENE_LOG("erreur de liaison : %s", log);
	}
	glDeleteShader(v);
	glDeleteShader(f);
	return p;
}

struct GlMesh {
	GLuint vao = 0, vbo = 0;
	GLsizei count = 0;
};

void setupLitMesh(GlMesh &m) {
	glGenVertexArrays(1, &m.vao);
	glGenBuffers(1, &m.vbo);
	glBindVertexArray(m.vao);
	glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
	const GLsizei stride = (GLsizei)sizeof(Vertex);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (const void *)0);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (const void *)12);
	glEnableVertexAttribArray(2);
	glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride, (const void *)24);
	glEnableVertexAttribArray(3);
	glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (const void *)36);
	glEnableVertexAttribArray(4);
	glVertexAttribPointer(4, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (const void *)40);
	glBindVertexArray(0);
}

void uploadLitMesh(GlMesh &m, const std::vector<Vertex> &verts, GLenum usage) {
	glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
	glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(verts.size() * sizeof(Vertex)), verts.empty() ? nullptr : verts.data(), usage);
	m.count = (GLsizei)verts.size();
}

void setupFxMesh(GlMesh &m) {
	glGenVertexArrays(1, &m.vao);
	glGenBuffers(1, &m.vbo);
	glBindVertexArray(m.vao);
	glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
	const GLsizei stride = (GLsizei)sizeof(FxVertex);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (const void *)0);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (const void *)12);
	glEnableVertexAttribArray(2);
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (const void *)20);
	glEnableVertexAttribArray(3);
	glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, (const void *)28);
	glEnableVertexAttribArray(4);
	glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, stride, (const void *)44);
	glBindVertexArray(0);
}

float hash01(uint32_t x) {
	x ^= x >> 16;
	x *= 0x7feb352dU;
	x ^= x >> 15;
	x *= 0x846ca68bU;
	x ^= x >> 16;
	return (float)(x & 0xFFFFFF) / 16777216.0f;
}

// Tâche de fond : calcul des textures procédurales.
struct TexJob {
	std::thread thread;
	std::atomic<bool> done{false};
	int theme = 0;
	std::vector<int> layers;
	std::vector<std::vector<uint8_t>> pixels;
};

} // namespace

struct Scene::Impl {
	bool srgb = true;
	const Dungeon *dungeon = nullptr;
	GLuint litProgram = 0, fxProgram = 0, crtProgram = 0, flatProgram = 0, panelProgram = 0;
	struct {
		GLint viewProj, game, gameSize, rect, border, alpha, hot, mode, glow;
	} panel;
	// Panneaux et poignées à dessiner pour l'image courante (triés du plus loin au plus près).
	struct PanelDraw {
		int first = 0, count = 0;
		float rect[4] = {0, 0, 1, 1}, border[2] = {0.01f, 0.01f}, alpha = 1.0f, hot[4] = {0, 0, 0, 0};
		float mode = 0.0f, glow = 0.0f;
		bool onTop = false;
		int layer = 0;
		GLuint texture = 0;
		float texSize[2] = {320, 200};
		Vec3 center = {0, 0, 0};
	};
	std::vector<PanelDraw> panelDraws;
	float frameAspect = 0.75f;
	double lastRebuild = -1.0;
	Mat4 holeMat;
	RoomOptions roomOptions;
	bool roomDirty = false;
	struct {
		GLint viewProj, model, lightPos, lightCol, ambSky, ambGround, camPos, tex, exposure, fade, alphaMode;
	} lit;
	struct {
		GLint viewProj, camRight, camUp, camPos, time, fade;
	} fx;
	struct {
		GLint viewProj, model, camPos, game, gameSize, time, hasGame;
	} crt;
	struct {
		GLint mvp, color;
	} flat;

	GLuint texArray = 0;
	bool texturesReady = false;
	double readyTime = -1.0;
	TexJob *job = nullptr;
	int loadedTheme = -1;

	GlMesh roomSolid, roomAlpha, frameMesh, crtBody, crtGlass, dynMesh, fxMesh, holeMesh, panelMesh;
	RoomInfo roomInfo;

	// État préparé pour l'image courante
	float lightPos[kMaxLights * 4];
	float lightCol[kMaxLights * 3];
	float ambSky[3], ambGround[3];
	Mat4 roomMat, screenMat;
	float fade = 0.0f;
	MeshBuilder dynBuilder;
	FxBuilder fxBuilder;
	int fxAmbientCount = 0;

	void startJob(int theme, bool all);
	void finishJob();
	void rebuildRoom();
	void buildFx(const SceneState &s);
};

void Scene::Impl::startJob(int theme, bool all) {
	job = new TexJob();
	job->theme = theme;
	for (int l = 0; l < kTexProcCount; l++)
		if (all || layerDependsOnTheme(l))
			job->layers.push_back(l);
	job->pixels.resize(job->layers.size());
	TexJob *j = job;
	job->thread = std::thread([j]() {
		for (size_t i = 0; i < j->layers.size(); i++) {
			j->pixels[i].resize((size_t)kTexSize * kTexSize * 4);
			generateTexture(j->layers[i], j->theme, j->pixels[i].data());
		}
		j->done.store(true);
	});
}

void Scene::Impl::finishJob() {
	job->thread.join();
	glBindTexture(GL_TEXTURE_2D_ARRAY, texArray);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	for (size_t i = 0; i < job->layers.size(); i++)
		glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, job->layers[i], kTexSize, kTexSize, 1, GL_RGBA, GL_UNSIGNED_BYTE, job->pixels[i].data());
	glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
	glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
	loadedTheme = job->theme;
	delete job;
	job = nullptr;

	rebuildRoom();
}

void Scene::Impl::rebuildRoom() {
	MeshBuilder solid, alpha;
	buildRoom(solid, alpha, roomInfo, loadedTheme, roomOptions);
	uploadLitMesh(roomSolid, solid.verts, GL_STATIC_DRAW);
	uploadLitMesh(roomAlpha, alpha.verts, GL_STATIC_DRAW);
	roomDirty = false;
	SCENE_LOG("décor prêt (thème %s, %d sommets)", themeInfo(loadedTheme).name, (int)solid.verts.size());
}

// Copie une image du joueur dans deux couches du tableau de textures (moitié gauche, moitié droite).
void Scene::setImage(int slot, const unsigned char *rgba, int w, int h) {
	if (slot < 0 || slot > 5 || !rgba || w < 2 || h < 2 || !d->texArray)
		return;
	const int base = kTexImgWindowA + slot * 2;
	const int N = kTexSize;
	const float m = kImgMargin;
	std::vector<uint8_t> px((size_t)N * N * 4);
	glBindTexture(GL_TEXTURE_2D_ARRAY, d->texArray);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	for (int half = 0; half < 2; half++) {
		for (int y = 0; y < N; y++) {
			float sv = (((float)y + 0.5f) / (float)N - m) / (1.0f - 2.0f * m);
			float fy = clampf((1.0f - sv) * (float)h - 0.5f, 0.0f, (float)(h - 1));
			int y0 = (int)fy, y1 = y0 + 1 < h ? y0 + 1 : y0;
			float ty = fy - (float)y0;
			for (int x = 0; x < N; x++) {
				float su = (((float)x + 0.5f) / (float)N - m) / (1.0f - 2.0f * m);
				float fx = clampf(((float)half + su) * 0.5f * (float)w - 0.5f, 0.0f, (float)(w - 1));
				int x0 = (int)fx, x1 = x0 + 1 < w ? x0 + 1 : x0;
				float tx = fx - (float)x0;
				const unsigned char *p00 = rgba + ((size_t)y0 * w + x0) * 4, *p10 = rgba + ((size_t)y0 * w + x1) * 4;
				const unsigned char *p01 = rgba + ((size_t)y1 * w + x0) * 4, *p11 = rgba + ((size_t)y1 * w + x1) * 4;
				uint8_t *o = &px[((size_t)y * N + x) * 4];
				for (int k = 0; k < 3; k++) {
					float top = (float)p00[k] + ((float)p10[k] - (float)p00[k]) * tx;
					float bot = (float)p01[k] + ((float)p11[k] - (float)p01[k]) * tx;
					o[k] = (uint8_t)(top + (bot - top) * ty + 0.5f);
				}
				o[3] = 0; // couleur propre : jamais teintée par le sommet
			}
		}
		glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, base + half, N, N, 1, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
	}
	glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
	glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
	float aspect = (float)w / (float)h;
	if (slot == 0) {
		d->roomOptions.windowImage = true;
		d->roomOptions.windowAspect = aspect;
	} else if (slot >= 2) {
		d->roomOptions.paintingImage[slot - 2] = true;
		d->roomOptions.paintingAspect[slot - 2] = aspect;
	} else {
		d->roomOptions.portraitImage = true;
		d->roomOptions.portraitAspect = aspect;
	}
	d->roomDirty = true;
	SCENE_LOG("image du joueur %d : %d x %d", slot, w, h);
}

Scene::Scene() : d(new Impl()) {}

Scene::~Scene() {
	if (d->job) {
		d->job->thread.join();
		delete d->job;
	}
	delete d;
}

void Scene::setDungeon(const Dungeon *dungeon) {
	d->dungeon = dungeon;
}

bool Scene::ready() const {
	return d->texturesReady;
}

bool Scene::init(bool srgbTarget) {
	d->srgb = srgbTarget;
	bool manual = !srgbTarget;
	d->litProgram = link(kLitVs, kLitFs, manual);
	d->fxProgram = link(kFxVs, kFxFs, manual);
	d->crtProgram = link(kCrtVs, kCrtFs, manual);
	d->flatProgram = link(kFlatVs, kFlatFs, false);
	// L'image du jeu et la destination ont le même encodage : pas de conversion à faire.
	d->panelProgram = link(kPanelVs, kPanelFs, false);
	d->panel.viewProj = glGetUniformLocation(d->panelProgram, "uViewProj");
	d->panel.game = glGetUniformLocation(d->panelProgram, "uGame");
	d->panel.gameSize = glGetUniformLocation(d->panelProgram, "uGameSize");
	d->panel.rect = glGetUniformLocation(d->panelProgram, "uRect");
	d->panel.border = glGetUniformLocation(d->panelProgram, "uBorder");
	d->panel.alpha = glGetUniformLocation(d->panelProgram, "uAlpha");
	d->panel.hot = glGetUniformLocation(d->panelProgram, "uHot");
	d->panel.mode = glGetUniformLocation(d->panelProgram, "uMode");
	d->panel.glow = glGetUniformLocation(d->panelProgram, "uGlow");
	d->lit.viewProj = glGetUniformLocation(d->litProgram, "uViewProj");
	d->lit.model = glGetUniformLocation(d->litProgram, "uModel");
	d->lit.lightPos = glGetUniformLocation(d->litProgram, "uLightPos");
	d->lit.lightCol = glGetUniformLocation(d->litProgram, "uLightCol");
	d->lit.ambSky = glGetUniformLocation(d->litProgram, "uAmbSky");
	d->lit.ambGround = glGetUniformLocation(d->litProgram, "uAmbGround");
	d->lit.camPos = glGetUniformLocation(d->litProgram, "uCamPos");
	d->lit.tex = glGetUniformLocation(d->litProgram, "uTex");
	d->lit.exposure = glGetUniformLocation(d->litProgram, "uExposure");
	d->lit.fade = glGetUniformLocation(d->litProgram, "uFade");
	d->lit.alphaMode = glGetUniformLocation(d->litProgram, "uAlphaMode");
	d->fx.viewProj = glGetUniformLocation(d->fxProgram, "uViewProj");
	d->fx.camRight = glGetUniformLocation(d->fxProgram, "uCamRight");
	d->fx.camUp = glGetUniformLocation(d->fxProgram, "uCamUp");
	d->fx.camPos = glGetUniformLocation(d->fxProgram, "uCamPos");
	d->fx.time = glGetUniformLocation(d->fxProgram, "uTime");
	d->fx.fade = glGetUniformLocation(d->fxProgram, "uFade");
	d->crt.viewProj = glGetUniformLocation(d->crtProgram, "uViewProj");
	d->crt.model = glGetUniformLocation(d->crtProgram, "uModel");
	d->crt.camPos = glGetUniformLocation(d->crtProgram, "uCamPos");
	d->crt.game = glGetUniformLocation(d->crtProgram, "uGame");
	d->crt.gameSize = glGetUniformLocation(d->crtProgram, "uGameSize");
	d->crt.time = glGetUniformLocation(d->crtProgram, "uTime");
	d->crt.hasGame = glGetUniformLocation(d->crtProgram, "uHasGame");
	d->flat.mvp = glGetUniformLocation(d->flatProgram, "uMvp");
	d->flat.color = glGetUniformLocation(d->flatProgram, "uColor");

	// Tableau de textures, rempli plus tard par la tâche de fond.
	glGenTextures(1, &d->texArray);
	glBindTexture(GL_TEXTURE_2D_ARRAY, d->texArray);
	int levels = 1;
	for (int s = kTexSize; s > 1; s >>= 1)
		levels++;
	glTexStorage3D(GL_TEXTURE_2D_ARRAY, levels, srgbTarget ? GL_SRGB8_ALPHA8 : GL_RGBA8, kTexSize, kTexSize, kTexCount);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
	const char *ext = (const char *)glGetString(GL_EXTENSIONS);
	if (ext && strstr(ext, "texture_filter_anisotropic"))
		glTexParameterf(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_ANISOTROPY_EXT, 4.0f);
	glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

	setupLitMesh(d->roomSolid);
	setupLitMesh(d->roomAlpha);
	setupLitMesh(d->frameMesh);
	setupLitMesh(d->crtBody);
	setupLitMesh(d->crtGlass);
	setupLitMesh(d->dynMesh);
	setupLitMesh(d->panelMesh);
	setupFxMesh(d->fxMesh);

	MeshBuilder frame, body, glass;
	buildScreenFrame(frame, d->frameAspect);
	uploadLitMesh(d->frameMesh, frame.verts, GL_STATIC_DRAW);
	buildCrt(body, glass);
	uploadLitMesh(d->crtBody, body.verts, GL_STATIC_DRAW);
	uploadLitMesh(d->crtGlass, glass.verts, GL_STATIC_DRAW);

	// Rectangle de l'écran (largeur 1, hauteur 0,75) pour « percer » le décor.
	const float hole[] = {-0.5f, -0.375f, 0, 0.5f, -0.375f, 0, 0.5f, 0.375f, 0, -0.5f, -0.375f, 0, 0.5f, 0.375f, 0, -0.5f, 0.375f, 0};
	glGenVertexArrays(1, &d->holeMesh.vao);
	glGenBuffers(1, &d->holeMesh.vbo);
	glBindVertexArray(d->holeMesh.vao);
	glBindBuffer(GL_ARRAY_BUFFER, d->holeMesh.vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(hole), hole, GL_STATIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 12, nullptr);
	glBindVertexArray(0);
	d->holeMesh.count = 6;

	d->startJob(0, true);
	return true;
}

void Scene::Impl::buildFx(const SceneState &s) {
	FxBuilder &fx = fxBuilder;
	fx.verts.clear();
	const float t = (float)s.time;
	const ThemeInfo &th = themeInfo(loadedTheme < 0 ? 0 : loadedTheme);
	Vec3 screenN = rotate(s.screenRot, {0, 0, 1});

	if (isLibraryEnv(s.environment) && texturesReady) {
		auto toWorld = [&](Vec3 p) { return transformPoint(roomMat, p); };
		// Ombres de contact
		for (const RoomShadow &sh : roomInfo.shadows) {
			const float col[4] = {0, 0, 0, sh.strength};
			Vec3 c = sh.pos;
			fx.quad(toWorld(c + Vec3{-sh.sx, 0, sh.sz}), toWorld(c + Vec3{sh.sx, 0, sh.sz}), toWorld(c + Vec3{sh.sx, 0, -sh.sz}),
			        toWorld(c + Vec3{-sh.sx, 0, -sh.sz}), col, kFxShadow);
		}
		// Tache de lumière de la fenêtre sur le plancher
		{
			float wz = roomInfo.windowCenter.z;
			const float col[4] = {th.windowLight[0], th.windowLight[1], th.windowLight[2], 0.20f};
			fx.quad(toWorld({-kRoomHalfX + 0.2f, 0.014f, wz + 0.80f}), toWorld({-kRoomHalfX + 2.5f, 0.014f, wz + 1.15f}),
			        toWorld({-kRoomHalfX + 2.5f, 0.014f, wz - 1.15f}), toWorld({-kRoomHalfX + 0.2f, 0.014f, wz - 0.80f}), col, kFxSoftRect);
		}
		// Feu de cheminée
		{
			Vec3 f = roomInfo.firePos;
			const float tint[4] = {th.fireTint[0], th.fireTint[1], th.fireTint[2], 1.0f};
			fx.sprite(toWorld(f + Vec3{0.00f, 0.30f, -0.14f}), 0.50f, 0.66f, tint, kFxFlame, 2, 0.13f);
			fx.sprite(toWorld(f + Vec3{0.05f, 0.25f, 0.12f}), 0.46f, 0.56f, tint, kFxFlame, 2, 0.57f);
			fx.sprite(toWorld(f + Vec3{-0.05f, 0.21f, 0.00f}), 0.40f, 0.46f, tint, kFxFlame, 2, 0.91f);
			float pulse = 0.85f + 0.15f * sinf(t * 9.0f) * sinf(t * 5.3f);
			const float glow[4] = {1.0f * th.fireTint[0], 0.45f * th.fireTint[1], 0.12f * th.fireTint[2], 0.50f * pulse};
			fx.sprite(toWorld(f + Vec3{-0.05f, 0.25f, 0.0f}), 1.9f, 1.6f, glow, kFxGlow, 1);
		}
		// Bougies
		for (size_t i = 0; i < roomInfo.candleFlames.size(); i++) {
			Vec3 p = toWorld(roomInfo.candleFlames[i]);
			float ph = hash01((uint32_t)i * 977u + 13u);
			const float white[4] = {1.0f, 0.95f, 0.85f, 1.0f};
			fx.sprite(p, 0.030f, 0.075f, white, kFxFlame, 2, ph);
			float pulse = 0.85f + 0.15f * sinf(t * (7.0f + 5.0f * ph) + ph * 20.0f);
			const float glow[4] = {1.0f, 0.62f, 0.25f, 0.34f * pulse};
			fx.sprite(p, 0.34f, 0.34f, glow, kFxGlow, 1);
		}
		// Fioles lumineuses
		for (const RoomGlow &g : roomInfo.glows) {
			const float col[4] = {g.color.x, g.color.y, g.color.z, 0.55f};
			fx.sprite(toWorld(g.pos), g.size, g.size, col, kFxGlow, 1);
		}
		// Particules : poussière dans la lumière, lucioles ou braises selon la zone
		Vec3 win = roomInfo.windowCenter + Vec3{0.9f, -0.5f, 0.0f};
		for (int i = 0; i < 70; i++) {
			uint32_t h = (uint32_t)i * 2654435761u;
			Vec3 base{mixf(-kRoomHalfX + 0.2f, kRoomHalfX - 0.2f, hash01(h + 1)), mixf(0.3f, kRoomHeight - 0.4f, hash01(h + 2)),
			          mixf(kRoomFrontZ + 0.2f, kRoomBackZ - 0.2f, hash01(h + 3))};
			if (i < 40)
				base = win + Vec3{(hash01(h + 4) - 0.3f) * 1.8f, (hash01(h + 5) - 0.5f) * 1.6f, (hash01(h + 6) - 0.5f) * 1.5f};
			Vec3 p = base + Vec3{sinf(t * 0.07f + hash01(h + 7) * 6.28f) * 0.30f, sinf(t * 0.05f + hash01(h + 8) * 6.28f) * 0.22f,
			                     cosf(t * 0.06f + hash01(h + 9) * 6.28f) * 0.30f};
			float dw = length(p - win);
			float lit = expf(-dw * dw / 1.4f);
			float tw = 0.6f + 0.4f * sinf(t * 1.3f + hash01(h + 10) * 6.28f);
			const float col[4] = {th.windowLight[0] + 0.05f, th.windowLight[1] + 0.04f, th.windowLight[2] + 0.03f, (0.10f + 0.9f * lit) * tw * 0.6f};
			fx.sprite(toWorld(p), 0.014f, 0.014f, col, kFxGlow, 1);
		}
		if (th.particles == 1) {
			for (int i = 0; i < 14; i++) {
				uint32_t h = (uint32_t)i * 40503u + 99u;
				Vec3 base = roomInfo.windowCenter + Vec3{0.3f + hash01(h + 1) * 2.6f, (hash01(h + 2) - 0.6f) * 1.6f, (hash01(h + 3) - 0.5f) * 3.0f};
				Vec3 p = base + Vec3{sinf(t * 0.31f + hash01(h + 4) * 6.28f) * 0.5f, sinf(t * 0.43f + hash01(h + 5) * 6.28f) * 0.3f,
				                     cosf(t * 0.27f + hash01(h + 6) * 6.28f) * 0.5f};
				float blink = powf(0.5f + 0.5f * sinf(t * (0.9f + hash01(h + 7)) + hash01(h + 8) * 6.28f), 3.0f);
				const float col[4] = {0.65f, 1.0f, 0.25f, 0.9f * blink};
				fx.sprite(toWorld(p), 0.05f, 0.05f, col, kFxGlow, 1);
			}
		} else if (th.particles == 2) {
			for (int i = 0; i < 18; i++) {
				uint32_t h = (uint32_t)i * 7919u + 5u;
				float life = fmodf(t * (0.10f + 0.10f * hash01(h + 1)) + hash01(h + 2), 1.0f);
				Vec3 p = roomInfo.firePos + Vec3{-0.15f - life * 0.5f + sinf(t + hash01(h + 3) * 6.28f) * 0.1f, 0.2f + life * 1.5f,
				                                 (hash01(h + 4) - 0.5f) * 0.8f + sinf(t * 0.7f + hash01(h + 5) * 6.28f) * 0.15f};
				const float col[4] = {1.0f, 0.42f, 0.08f, (1.0f - life) * 0.9f};
				fx.sprite(toWorld(p), 0.022f, 0.022f, col, kFxGlow, 1);
			}
		}
		// Aura de l'écran magique
		{
			const float col[4] = {s.screenGlow.x + 0.02f, s.screenGlow.y + 0.02f, s.screenGlow.z + 0.03f, 0.10f};
			fx.sprite(s.screenPos - screenN * 0.04f, s.screenWidth * 2.1f, s.screenWidth * 1.75f, col, kFxGlow, 1);
		}
	}

	if (isCrtEnv(s.environment) && texturesReady) {
		Vec3 led = transformPoint(screenMat, crtLedPosition());
		const float col[4] = {0.2f, 1.0f, 0.3f, 0.5f};
		fx.sprite(led + screenN * 0.004f, 0.05f * s.screenWidth, 0.05f * s.screenWidth, col, kFxGlow, 1);
	}

	fxAmbientCount = (int)fx.verts.size(); // tout ce qui suit est dessiné par-dessus l'image du jeu
	// Pointeur : un filet de lumière qui part du bout de l'index, et une lueur là où il touche l'écran.
	for (int h = 0; h < 2; h++) {
		const HandState &hand = s.hands[h];
		if (!hand.visible || !hand.pointing)
			continue;
		Vec3 tip = hand.pos + rotate(hand.rot, gloveFingertip(h == 0, hand.trigger));
		Vec3 dir = rotate(hand.rot, {0, 0, -1});
		Vec3 start = tip + dir * 0.012f;
		// Court quand on vise l'écran ou un panneau (la lueur suffit) ; entier sinon, pour bien
		// voir où l'on vise et attraper les poignées.
		float len = s.pointerLong ? 3.5f : 0.34f;
		if (s.hitValid)
			len = fminf(len, fmaxf(0.02f, length(s.hit - start) * (s.pointerLong ? 1.0f : 0.9f)));
		Vec3 end = start + dir * len;
		Vec3 side = normalize(cross(dir, {0, 1, 0}));
		if (dot(side, side) < 0.5f)
			side = {1, 0, 0};
		Vec3 up = normalize(cross(side, dir));
		const float w = s.pointerLong ? 0.007f : 0.011f;
		const float col[4] = {1.0f, 0.82f, 0.45f, 0.55f + 0.45f * s.clickPulse};
		fx.quad(start - side * w, start + side * w, end + side * w, end - side * w, col, kFxBeam);
		fx.quad(start - up * w, start + up * w, end + up * w, end - up * w, col, kFxBeam);
		const float spark[4] = {1.0f, 0.85f, 0.55f, 0.5f + 0.5f * s.clickPulse};
		fx.sprite(tip, 0.030f, 0.030f, spark, kFxGlow, 1);
	}
	if (s.hitValid) {
		Vec3 sx = rotate(s.screenRot, {1, 0, 0}), sy = rotate(s.screenRot, {0, 1, 0});
		float r = s.screenWidth * (0.014f + 0.030f * s.clickPulse);
		Vec3 nrm = screenN;
		if (s.hitRadius > 0.0f) {
			sx = s.hitRight;
			sy = s.hitUp;
			r = s.hitRadius * (1.0f + 1.8f * s.clickPulse);
			nrm = cross(sx, sy);
		}
		Vec3 c = s.hit + nrm * 0.006f;
		const float col[4] = {1.0f, 0.84f, 0.50f, 0.30f + 0.55f * s.clickPulse};
		fx.quad(c - sx * r - sy * r, c + sx * r - sy * r, c + sx * r + sy * r, c - sx * r + sy * r, col, kFxGlow);
	}
}

void Scene::prepare(const SceneState &s) {
	// Textures : fin de la tâche en cours, puis changement de thème si nécessaire.
	if (d->job && d->job->done.load()) {
		d->finishJob();
		if (!d->texturesReady) {
			d->texturesReady = true;
			d->readyTime = s.time;
		}
	}
	if (!d->job && d->texturesReady && s.theme != d->loadedTheme && s.theme >= 0 && s.theme < kThemeCount)
		d->startJob(s.theme, false);
	// Pas plus d'une reconstruction tous les 80 ms (table tenue à la main).
	if (!d->job && d->texturesReady && d->roomDirty && fabs(s.time - d->lastRebuild) > 0.08) {
		d->lastRebuild = s.time;
		d->rebuildRoom();
	}

	// Le cadre suit le format de l'image affichée (image entière ou vue du donjon seule).
	float aspect = isCrtEnv(s.environment) ? 0.75f : clampf(s.screenAspect, 0.3f, 1.5f);
	if (fabsf(aspect - d->frameAspect) > 1e-4f) {
		d->frameAspect = aspect;
		MeshBuilder frame;
		buildScreenFrame(frame, aspect);
		uploadLitMesh(d->frameMesh, frame.verts, GL_DYNAMIC_DRAW);
	}

	d->fade = d->texturesReady ? clampf((float)(s.time - d->readyTime) / 1.6f, 0.0f, 1.0f) : 0.0f;
	d->roomMat = poseMatrix(s.roomPos, quatYaw(s.roomYaw));
	d->screenMat = poseMatrix(s.screenPos, s.screenRot, s.screenWidth);
	{
		Mat4 stretch = identity();
		stretch.m[5] = d->frameAspect / 0.75f;
		d->holeMat = multiply(d->screenMat, stretch);
	}

	// Lumières
	memset(d->lightPos, 0, sizeof(d->lightPos));
	memset(d->lightCol, 0, sizeof(d->lightCol));
	for (int i = 0; i < kMaxLights; i++)
		d->lightPos[i * 4 + 3] = 1.0f;
	const float t = (float)s.time;
	int n = 0;
	Vec3 screenN = rotate(s.screenRot, {0, 0, 1});
	if (isLibraryEnv(s.environment)) {
		const ThemeInfo &th = themeInfo(d->loadedTheme < 0 ? 0 : d->loadedTheme);
		for (size_t i = 0; i < d->roomInfo.lights.size() && n < kMaxLights - 1; i++, n++) {
			const RoomLight &l = d->roomInfo.lights[i];
			Vec3 p = transformPoint(d->roomMat, l.pos);
			float ph = (float)i * 2.4f;
			float fl = 1.0f + l.flicker * 0.20f * (sinf(t * 11.3f + ph) * 0.45f + sinf(t * 17.7f + ph * 2.0f) * 0.25f + sinf(t * 5.1f + ph * 3.0f) * 0.30f);
			d->lightPos[n * 4 + 0] = p.x; d->lightPos[n * 4 + 1] = p.y; d->lightPos[n * 4 + 2] = p.z; d->lightPos[n * 4 + 3] = l.radius;
			d->lightCol[n * 3 + 0] = l.color.x * fl; d->lightCol[n * 3 + 1] = l.color.y * fl; d->lightCol[n * 3 + 2] = l.color.z * fl;
		}
		for (int k = 0; k < 3; k++) {
			d->ambSky[k] = th.ambient[k] * 1.25f;
			d->ambGround[k] = th.ambient[k] * 0.75f;
		}
	} else {
		// Réalité mixte : éclairage neutre, venant d'en haut et de face.
		Vec3 key = s.screenPos + Vec3{0.3f, 1.4f, 0.0f} + screenN * 1.6f;
		d->lightPos[0] = key.x; d->lightPos[1] = key.y; d->lightPos[2] = key.z; d->lightPos[3] = 3.0f;
		d->lightCol[0] = 1.25f; d->lightCol[1] = 1.18f; d->lightCol[2] = 1.05f;
		n = 1;
		d->ambSky[0] = 0.40f; d->ambSky[1] = 0.42f; d->ambSky[2] = 0.46f;
		d->ambGround[0] = 0.20f; d->ambGround[1] = 0.19f; d->ambGround[2] = 0.18f;
	}
	// La lueur de l'écran éclaire les mains et les objets proches.
	{
		Vec3 p = s.screenPos + screenN * (0.30f * s.screenWidth);
		float k = isLibraryEnv(s.environment) ? 1.3f : 0.9f;
		d->lightPos[n * 4 + 0] = p.x; d->lightPos[n * 4 + 1] = p.y; d->lightPos[n * 4 + 2] = p.z; d->lightPos[n * 4 + 3] = 0.6f * s.screenWidth;
		d->lightCol[n * 3 + 0] = s.screenGlow.x * k; d->lightCol[n * 3 + 1] = s.screenGlow.y * k; d->lightCol[n * 3 + 2] = s.screenGlow.z * k;
	}

	// Gants
	d->dynBuilder.verts.clear();
	if (d->texturesReady) {
		for (int h = 0; h < 2; h++) {
			const HandState &hand = s.hands[h];
			if (!hand.visible)
				continue;
			d->dynBuilder.setTransform(poseMatrix(hand.pos, hand.rot));
			buildGlove(d->dynBuilder, h == 0, hand.trigger, hand.grip);
		}
	}
	// Panneaux flottants et poignées
	MeshBuilder panel;
	d->panelDraws.clear();
	for (int i = 0; i < SceneState::kMaxPanels && d->texturesReady; i++) {
		const SceneState::Panel &pn = s.panels[i];
		GLuint tex = pn.texture ? pn.texture : s.gameTexture;
		if (!pn.visible || pn.alpha <= 0.01f || !tex)
			continue;
		Impl::PanelDraw pd;
		const float b = pn.cutout ? 0.0f : pn.border;
		float du = (pn.u1 - pn.u0) * b / (2.0f * pn.halfW), dv = (pn.v1 - pn.v0) * b / (2.0f * pn.halfH);
		Vec3 r = pn.right * (pn.halfW + b), u = pn.up * (pn.halfH + b);
		pd.first = (int)panel.verts.size();
		panel.quad(pn.center - r - u, pn.center + r - u, pn.center + r + u, pn.center - r + u, mat(kTexWhite),
		           pn.u0 - du, pn.v1 + dv, pn.u1 + du, pn.v0 - dv);
		pd.count = (int)panel.verts.size() - pd.first;
		pd.rect[0] = pn.u0; pd.rect[1] = pn.v0; pd.rect[2] = pn.u1; pd.rect[3] = pn.v1;
		pd.border[0] = du; pd.border[1] = dv;
		pd.alpha = pn.alpha;
		pd.glow = pn.cutout ? 3.0f : pn.blank ? 1.0f : (pn.hotOutline ? 2.0f : 0.0f); // 1 éteint, 2 zone encadrée, 3 découpe
		for (int k = 0; k < 4; k++)
			pd.hot[k] = pn.hot[k];
		pd.texture = tex;
		pd.texSize[0] = (float)(pn.texture ? pn.texW : s.gameWidth);
		pd.texSize[1] = (float)(pn.texture ? pn.texH : s.gameHeight);
		pd.center = pn.center;
		pd.onTop = pn.onTop;
		pd.layer = pn.layer;
		d->panelDraws.push_back(pd);
	}
	for (int i = 0; i < SceneState::kMaxHandles && d->texturesReady; i++) {
		const SceneState::Handle &hn = s.handles[i];
		if (!hn.visible || hn.alpha <= 0.01f)
			continue;
		Impl::PanelDraw pd;
		Vec3 r = hn.right * hn.halfW, u = hn.up * hn.halfH;
		pd.first = (int)panel.verts.size();
		panel.quad(hn.center - r - u, hn.center + r - u, hn.center + r + u, hn.center - r + u, mat(kTexWhite), 0, 0, 1, 1);
		pd.count = (int)panel.verts.size() - pd.first;
		pd.alpha = hn.alpha;
		pd.mode = 1.0f;
		pd.glow = hn.glow;
		pd.center = hn.center;
		d->panelDraws.push_back(pd);
	}
	uploadLitMesh(d->panelMesh, panel.verts, GL_DYNAMIC_DRAW);
	uploadLitMesh(d->dynMesh, d->dynBuilder.verts, GL_DYNAMIC_DRAW);

	d->buildFx(s);
	glBindBuffer(GL_ARRAY_BUFFER, d->fxMesh.vbo);
	glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(d->fxBuilder.verts.size() * sizeof(FxVertex)),
	             d->fxBuilder.verts.empty() ? nullptr : d->fxBuilder.verts.data(), GL_DYNAMIC_DRAW);
	d->fxMesh.count = (GLsizei)d->fxBuilder.verts.size();
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void Scene::render(const Mat4 &view, const Mat4 &proj, const SceneState &s) {
	Mat4 viewProj = multiply(proj, view);
	Vec3 camRight{view.m[0], view.m[4], view.m[8]}, camUp{view.m[1], view.m[5], view.m[9]}, camBack{view.m[2], view.m[6], view.m[10]};
	Vec3 camPos = (camRight * view.m[12] + camUp * view.m[13] + camBack * view.m[14]) * -1.0f;
	const Mat4 ident = identity();
	const bool inDungeon = s.dungeonVisible && d->dungeon && d->dungeon->hasContent();
	bool library = isLibraryEnv(s.environment) && !inDungeon;
	bool crt = isCrtEnv(s.environment);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glDepthMask(GL_TRUE);
	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);
	glFrontFace(GL_CCW);
	glDisable(GL_BLEND);
	glClearColor(0, 0, 0, 0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	// --- Le donjon en 3D, tout autour : l'interface (écran, fenêtres, mains) passe toujours devant. ---
	if (inDungeon) {
		d->dungeon->render(viewProj, s.dungeonModel, camPos, s.dungeonLight, s.dungeonReach);
		glClear(GL_DEPTH_BUFFER_BIT);
		glEnable(GL_CULL_FACE);
	}

	// --- Objets opaques ---
	if (d->texturesReady) {
		glUseProgram(d->litProgram);
		glUniformMatrix4fv(d->lit.viewProj, 1, GL_FALSE, viewProj.m);
		glUniform4fv(d->lit.lightPos, kMaxLights, d->lightPos);
		glUniform3fv(d->lit.lightCol, kMaxLights, d->lightCol);
		glUniform3fv(d->lit.ambSky, 1, d->ambSky);
		glUniform3fv(d->lit.ambGround, 1, d->ambGround);
		glUniform3f(d->lit.camPos, camPos.x, camPos.y, camPos.z);
		glUniform1f(d->lit.exposure, 1.0f);
		glUniform1f(d->lit.fade, d->fade);
		glUniform1f(d->lit.alphaMode, 0.0f);
		glUniform1i(d->lit.tex, 0);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D_ARRAY, d->texArray);
		if (library && d->roomSolid.count > 0) {
			glUniformMatrix4fv(d->lit.model, 1, GL_FALSE, d->roomMat.m);
			glBindVertexArray(d->roomSolid.vao);
			glDrawArrays(GL_TRIANGLES, 0, d->roomSolid.count);
		}
		glUniformMatrix4fv(d->lit.model, 1, GL_FALSE, d->screenMat.m);
		if (s.screenVisible && !s.screenFrameless) {
			GlMesh &around = crt ? d->crtBody : d->frameMesh;
			glBindVertexArray(around.vao);
			glDrawArrays(GL_TRIANGLES, 0, around.count);
		}
		if (d->dynMesh.count > 0) {
			glUniformMatrix4fv(d->lit.model, 1, GL_FALSE, ident.m);
			glBindVertexArray(d->dynMesh.vao);
			glDrawArrays(GL_TRIANGLES, 0, d->dynMesh.count);
		}
	}

	// --- Écran du jeu ---
	if (!s.screenVisible) {
		// Écran caché : ni dalle, ni trou.
	} else if (crt) {
		// Dalle bombée du moniteur, avec l'image du jeu.
		glUseProgram(d->crtProgram);
		glUniformMatrix4fv(d->crt.viewProj, 1, GL_FALSE, viewProj.m);
		glUniformMatrix4fv(d->crt.model, 1, GL_FALSE, d->screenMat.m);
		glUniform3f(d->crt.camPos, camPos.x, camPos.y, camPos.z);
		glUniform2f(d->crt.gameSize, (float)s.gameWidth, (float)s.gameHeight);
		glUniform1f(d->crt.time, (float)fmod(s.time, 1000.0));
		glUniform1f(d->crt.hasGame, s.gameTexture ? 1.0f : 0.0f);
		glUniform1i(d->crt.game, 0);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, s.gameTexture);
		glBindVertexArray(d->crtGlass.vao);
		glDrawArrays(GL_TRIANGLES, 0, d->crtGlass.count);
		glBindTexture(GL_TEXTURE_2D, 0);
	} else {
		// « Trou » transparent : l'image nette du jeu (calque quad) apparaît à travers.
		glUseProgram(d->flatProgram);
		Mat4 mvp = multiply(viewProj, d->holeMat);
		glUniformMatrix4fv(d->flat.mvp, 1, GL_FALSE, mvp.m);
		glUniform4f(d->flat.color, 0, 0, 0, 0);
		glBindVertexArray(d->holeMesh.vao);
		glDrawArrays(GL_TRIANGLES, 0, d->holeMesh.count);
	}

	// --- Transparences ---
	glEnable(GL_BLEND);
	glDepthMask(GL_FALSE);
	if (library && d->texturesReady && d->roomAlpha.count > 0) {
		glUseProgram(d->litProgram);
		glUniform1f(d->lit.alphaMode, 1.0f);
		glUniformMatrix4fv(d->lit.model, 1, GL_FALSE, d->roomMat.m);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D_ARRAY, d->texArray);
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
		glBindVertexArray(d->roomAlpha.vao);
		glDrawArrays(GL_TRIANGLES, 0, d->roomAlpha.count);
	}
	const int fxTotal = d->fxMesh.count;
	const int fxAmbient = d->fxAmbientCount < fxTotal ? d->fxAmbientCount : fxTotal;
	auto drawFx = [&](int first, int count) {
		if (count <= 0)
			return;
		glUseProgram(d->fxProgram);
		glUniformMatrix4fv(d->fx.viewProj, 1, GL_FALSE, viewProj.m);
		glUniform3f(d->fx.camRight, camRight.x, camRight.y, camRight.z);
		glUniform3f(d->fx.camUp, camUp.x, camUp.y, camUp.z);
		glUniform3f(d->fx.camPos, camPos.x, camPos.y, camPos.z);
		glUniform1f(d->fx.time, (float)fmod(s.time, 1000.0));
		glUniform1f(d->fx.fade, library ? d->fade : 1.0f);
		glDisable(GL_CULL_FACE);
		glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
		glActiveTexture(GL_TEXTURE0);
		glBindVertexArray(d->fxMesh.vao);
		glDrawArrays(GL_TRIANGLES, first, count);
		glEnable(GL_CULL_FACE);
	};
	// 1. Lueurs, flammes, ombres du décor (pas dans le donjon)
	if (!inDungeon)
		drawFx(0, fxAmbient);
	// 2. L'image du jeu doit rester pure : on efface tout ce que les lueurs du décor ont déposé
	//    sur elle (sauf là où un objet solide, comme un gant, passe devant).
	if (!crt && s.screenVisible) {
		glDisable(GL_BLEND);
		glUseProgram(d->flatProgram);
		Mat4 mvp = multiply(viewProj, d->holeMat);
		glUniformMatrix4fv(d->flat.mvp, 1, GL_FALSE, mvp.m);
		glUniform4f(d->flat.color, 0, 0, 0, 0);
		glBindVertexArray(d->holeMesh.vao);
		glDrawArrays(GL_TRIANGLES, 0, d->holeMesh.count);
		glEnable(GL_BLEND);
	}
	// 3. Panneaux et poignées
	if (d->panelMesh.count > 0 && !d->panelDraws.empty()) {
		// Du plus loin au plus près de l'œil, pour que les panneaux se recouvrent correctement.
		std::vector<int> order(d->panelDraws.size());
		for (size_t i = 0; i < order.size(); i++)
			order[i] = (int)i;
		std::sort(order.begin(), order.end(), [&](int a, int b) {
			if (d->panelDraws[a].onTop != d->panelDraws[b].onTop)
				return d->panelDraws[b].onTop; // par-dessus tout : en dernier
			// Du plus loin au plus proche ; « layer » rapproche (positif) ou éloigne (négatif) un panneau
			// de quelques centimètres dans cet ordre seulement : l'onglet passe après l'inventaire, le
			// cadre d'un groupe avant ses fenêtres, sans passer devant un clavier plus proche.
			const float la = length(d->panelDraws[a].center - camPos) - 0.04f * (float)d->panelDraws[a].layer;
			const float lb = length(d->panelDraws[b].center - camPos) - 0.04f * (float)d->panelDraws[b].layer;
			return la > lb;
		});
		glUseProgram(d->panelProgram);
		glUniformMatrix4fv(d->panel.viewProj, 1, GL_FALSE, viewProj.m);
		glUniform1i(d->panel.game, 0);
		glActiveTexture(GL_TEXTURE0);
		glDisable(GL_CULL_FACE);
		glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
		glBindVertexArray(d->panelMesh.vao);
		for (int idx : order) {
			const Impl::PanelDraw &pd = d->panelDraws[idx];
			glBindTexture(GL_TEXTURE_2D, pd.texture);
			glUniform2f(d->panel.gameSize, pd.texSize[0], pd.texSize[1]);
			glUniform4f(d->panel.rect, pd.rect[0], pd.rect[1], pd.rect[2], pd.rect[3]);
			glUniform2f(d->panel.border, pd.border[0], pd.border[1]);
			glUniform1f(d->panel.alpha, pd.alpha);
			glUniform4f(d->panel.hot, pd.hot[0], pd.hot[1], pd.hot[2], pd.hot[3]);
			glUniform1f(d->panel.mode, pd.mode);
			glUniform1f(d->panel.glow, pd.glow);
			if (pd.onTop)
				glDisable(GL_DEPTH_TEST);
			glDrawArrays(GL_TRIANGLES, pd.first, pd.count);
			if (pd.onTop)
				glEnable(GL_DEPTH_TEST);
		}
		glBindTexture(GL_TEXTURE_2D, 0);
		glEnable(GL_CULL_FACE);
	}
	// 4. Pointeur
	drawFx(fxAmbient, fxTotal - fxAmbient);
	glBindVertexArray(0);
	glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
	glUseProgram(0);
	glDisable(GL_BLEND);
	glDepthMask(GL_TRUE);
	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);
}

} // namespace lolvr
