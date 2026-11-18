// lol-vr-3d — rendu du donjon en 3D. Voir dungeon.h.
#include "dungeon.h"

#include <GLES3/gl3.h>
#include <math.h>
#include <string>

#ifdef __ANDROID__
#include <android/log.h>
#define DUNGEON_LOG(...) __android_log_print(ANDROID_LOG_INFO, "lolvr-dungeon", __VA_ARGS__)
#else
#include <stdio.h>
#define DUNGEON_LOG(...) (fprintf(stderr, __VA_ARGS__), fprintf(stderr, "\n"))
#endif

namespace lolvr {

namespace {

const char *kVs = R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aUvl;
layout(location = 2) in float aShade;
uniform mat4 uViewProj;
uniform mat4 uModel;
uniform vec3 uEye;
out vec3 vUvl;
out float vShade;
out float vDist;
void main() {
	vec4 w = uModel * vec4(aPos, 1.0);
	vUvl = aUvl;
	vShade = aShade;
	vDist = distance(w.xyz, uEye);
	gl_Position = uViewProj * w;
}
)";

const char *kFs = R"(
precision highp float;
precision highp sampler2DArray;
uniform sampler2DArray uTex;
uniform float uLight;
in vec3 vUvl;
in float vShade;
in float vDist;
out vec4 oColor;
void main() {
	vec4 t = texture(uTex, vUvl);
	if (t.a < 0.5)
		discard;
	vec3 c = t.rgb;
#ifdef MANUAL_SRGB
	c = pow(c, vec3(2.2));
#endif
	if (vShade > 1.5) {
		// Le ciel : pas de pénombre.
		c *= (vShade - 2.0) * uLight;
	} else {
		// Comme dans le jeu : bien éclairé tout près, la pénombre gagne en trois ou quatre pas.
		float k = clamp(1.15 - vDist / 11.0, 0.0, 1.0);
		k = k * k * (3.0 - 2.0 * k);
		c *= vShade * uLight * mix(0.04, 1.0, k);
	}
#ifdef MANUAL_SRGB
	c = pow(c, vec3(1.0 / 2.2));
#endif
	oColor = vec4(c, 1.0);
}
)";

GLuint compileShader(GLenum type, const std::string &src) {
	GLuint s = glCreateShader(type);
	const char *p = src.c_str();
	glShaderSource(s, 1, &p, nullptr);
	glCompileShader(s);
	GLint ok = 0;
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[2048] = {0};
		glGetShaderInfoLog(s, sizeof(log) - 1, nullptr, log);
		DUNGEON_LOG("erreur de shader (donjon) : %s", log);
	}
	return s;
}

} // namespace

struct Dungeon::Impl {
	bool srgb = false;
	GLuint program = 0, vao = 0, vbo = 0, tex = 0;
	GLuint spriteVao = 0, spriteVbo = 0, spriteTex = 0;
	std::vector<DungeonSprite> sprites;
	GLint uViewProj = -1, uModel = -1, uEye = -1, uLight = -1, uTex = -1;
	GLsizei count = 0;
	int layers = 0;
};

Dungeon::Dungeon() : d(new Impl()) {}

Dungeon::~Dungeon() {
	delete d;
}

bool Dungeon::init(bool srgbTarget) {
	d->srgb = srgbTarget;
	std::string head = "#version 300 es\n";
	if (!srgbTarget)
		head += "#define MANUAL_SRGB 1\n";
	d->program = glCreateProgram();
	GLuint v = compileShader(GL_VERTEX_SHADER, head + kVs);
	GLuint f = compileShader(GL_FRAGMENT_SHADER, head + kFs);
	glAttachShader(d->program, v);
	glAttachShader(d->program, f);
	glLinkProgram(d->program);
	glDeleteShader(v);
	glDeleteShader(f);
	GLint ok = 0;
	glGetProgramiv(d->program, GL_LINK_STATUS, &ok);
	if (!ok) {
		DUNGEON_LOG("erreur de liaison (donjon)");
		return false;
	}
	d->uViewProj = glGetUniformLocation(d->program, "uViewProj");
	d->uModel = glGetUniformLocation(d->program, "uModel");
	d->uEye = glGetUniformLocation(d->program, "uEye");
	d->uLight = glGetUniformLocation(d->program, "uLight");
	d->uTex = glGetUniformLocation(d->program, "uTex");
	glGenVertexArrays(1, &d->vao);
	glGenBuffers(1, &d->vbo);
	glBindVertexArray(d->vao);
	glBindBuffer(GL_ARRAY_BUFFER, d->vbo);
	const GLsizei stride = (GLsizei)sizeof(DungeonVertex);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (const void *)0);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (const void *)12);
	glEnableVertexAttribArray(2);
	glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride, (const void *)24);
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	// Monstres et objets
	glGenVertexArrays(1, &d->spriteVao);
	glGenBuffers(1, &d->spriteVbo);
	glBindVertexArray(d->spriteVao);
	glBindBuffer(GL_ARRAY_BUFFER, d->spriteVbo);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (const void *)0);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (const void *)12);
	glEnableVertexAttribArray(2);
	glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride, (const void *)24);
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glGenTextures(1, &d->spriteTex);
	glBindTexture(GL_TEXTURE_2D_ARRAY, d->spriteTex);
	glTexStorage3D(GL_TEXTURE_2D_ARRAY, 1, d->srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, kSpriteTexW, kSpriteTexH, kMaxSprites);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
	return true;
}

void Dungeon::setSpriteImage(int layer, const uint8_t *rgba) {
	if (!d->spriteTex || layer < 0 || layer >= kMaxSprites)
		return;
	glBindTexture(GL_TEXTURE_2D_ARRAY, d->spriteTex);
	glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer, kSpriteTexW, kSpriteTexH, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
	glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
}

void Dungeon::setSprites(const std::vector<DungeonSprite> &sprites) {
	d->sprites = sprites;
}

void Dungeon::setTextures(const DungeonTextures &tex) {
	if (d->tex) {
		glDeleteTextures(1, &d->tex);
		d->tex = 0;
	}
	d->layers = tex.layers;
	if (tex.layers <= 0)
		return;
	glGenTextures(1, &d->tex);
	glBindTexture(GL_TEXTURE_2D_ARRAY, d->tex);
	const int levels = 8; // 128 -> 1
	glTexStorage3D(GL_TEXTURE_2D_ARRAY, levels, d->srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, kDunTexSize, kDunTexSize, tex.layers);
	glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, 0, kDunTexSize, kDunTexSize, tex.layers, GL_RGBA, GL_UNSIGNED_BYTE, tex.rgba.data());
	glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
	// Pixels nets de près, sans scintillement de loin.
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
}

void Dungeon::setMesh(const std::vector<DungeonVertex> &verts) {
	glBindBuffer(GL_ARRAY_BUFFER, d->vbo);
	glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(verts.size() * sizeof(DungeonVertex)), verts.empty() ? nullptr : verts.data(), GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	d->count = (GLsizei)verts.size();
}

bool Dungeon::hasContent() const {
	return d->program && d->tex && d->count > 0;
}

void Dungeon::render(const Mat4 &viewProj, const Mat4 &model, Vec3 eye, float light) const {
	if (!hasContent())
		return;
	glUseProgram(d->program);
	glUniformMatrix4fv(d->uViewProj, 1, GL_FALSE, viewProj.m);
	glUniformMatrix4fv(d->uModel, 1, GL_FALSE, model.m);
	glUniform3f(d->uEye, eye.x, eye.y, eye.z);
	glUniform1f(d->uLight, light);
	glUniform1i(d->uTex, 0);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D_ARRAY, d->tex);
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);
	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);
	glFrontFace(GL_CCW);
	glDisable(GL_BLEND);
	glBindVertexArray(d->vao);
	glDrawArrays(GL_TRIANGLES, 0, d->count);

	// Monstres et objets, tournés vers l'œil (repère du niveau : on y ramène la position de l'œil).
	if (!d->sprites.empty() && d->spriteTex) {
		Vec3 c0{model.m[0], model.m[1], model.m[2]}, c1{model.m[4], model.m[5], model.m[6]}, c2{model.m[8], model.m[9], model.m[10]};
		Vec3 rel = eye - Vec3{model.m[12], model.m[13], model.m[14]};
		Vec3 cam{dot(c0, rel), dot(c1, rel), dot(c2, rel)};
		std::vector<DungeonVertex> v;
		v.reserve(d->sprites.size() * 6);
		auto put = [&](float x, float y, float z, float u, float vv, float layer) {
			DungeonVertex dv;
			dv.pos[0] = x; dv.pos[1] = y; dv.pos[2] = z;
			dv.uvl[0] = u; dv.uvl[1] = vv; dv.uvl[2] = layer;
			dv.shade = 1.0f;
			v.push_back(dv);
		};
		for (const DungeonSprite &s : d->sprites) {
			float tx = cam.x - s.x, tz = cam.z - s.z;
			float tl = sqrtf(tx * tx + tz * tz);
			if (tl < 1e-3f)
				continue;
			// Droite de l'image, vue de l'œil : perpendiculaire au regard, à l'horizontale.
			float rx = tz / tl, rz = -tx / tl;
			float cx = s.x + rx * s.offset, cz = s.z + rz * s.offset;
			float hw = s.width * 0.5f;
			float y0 = s.ceiling ? kWallHeight - s.height : 0.0f, y1 = y0 + s.height;
			float L = (float)s.layer;
			float ax = cx - rx * hw, az = cz - rz * hw, bx = cx + rx * hw, bz = cz + rz * hw;
			put(ax, y0, az, s.u0, s.v1, L);
			put(bx, y0, bz, s.u1, s.v1, L);
			put(bx, y1, bz, s.u1, s.v0, L);
			put(ax, y0, az, s.u0, s.v1, L);
			put(bx, y1, bz, s.u1, s.v0, L);
			put(ax, y1, az, s.u0, s.v0, L);
		}
		if (!v.empty()) {
			glBindBuffer(GL_ARRAY_BUFFER, d->spriteVbo);
			glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(v.size() * sizeof(DungeonVertex)), v.data(), GL_STREAM_DRAW);
			glBindBuffer(GL_ARRAY_BUFFER, 0);
			glDisable(GL_CULL_FACE);
			glBindTexture(GL_TEXTURE_2D_ARRAY, d->spriteTex);
			glBindVertexArray(d->spriteVao);
			glDrawArrays(GL_TRIANGLES, 0, (GLsizei)v.size());
			glEnable(GL_CULL_FACE);
		}
	}
	glBindVertexArray(0);
	glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
}

} // namespace lolvr
