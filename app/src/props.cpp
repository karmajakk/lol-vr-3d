// lol-vr — objets attachés à l'écran (cadre, moniteur cathodique) et aux mains (gants).

#include "room.h"

#include <algorithm>

namespace lolvr {

namespace {

const float kPi = 3.14159265f;
float rad(float deg) { return deg * kPi / 180.0f; }

const Vec3 kBrass = {0.85f, 0.62f, 0.25f};

// Ellipsoïde (sphère aplatie) centré en c.
void ellipsoid(MeshBuilder &mb, Vec3 c, Vec3 r, const Material &m, int rings = 6, int segs = 10) {
	auto point = [&](int i, int j, Vec3 &p, Vec3 &n) {
		float a = -kPi * 0.5f + kPi * (float)i / (float)rings;
		float b = 2.0f * kPi * (float)j / (float)segs;
		Vec3 u{cosf(a) * cosf(b), sinf(a), cosf(a) * sinf(b)};
		p = c + mul(u, r);
		n = normalize(Vec3{u.x / r.x, u.y / r.y, u.z / r.z});
	};
	for (int i = 0; i < rings; i++)
		for (int j = 0; j < segs; j++) {
			Vec3 p00, p01, p10, p11, n00, n01, n10, n11;
			point(i, j, p00, n00);
			point(i, j + 1, p01, n01);
			point(i + 1, j, p10, n10);
			point(i + 1, j + 1, p11, n11);
			float u0 = (float)j / segs * 2.0f, u1 = (float)(j + 1) / segs * 2.0f, v0 = (float)i / rings, v1 = (float)(i + 1) / rings;
			mb.vertex(p01, n01, u1, v0, m);
			mb.vertex(p00, n00, u0, v0, m);
			mb.vertex(p10, n10, u0, v1, m);
			mb.vertex(p01, n01, u1, v0, m);
			mb.vertex(p10, n10, u0, v1, m);
			mb.vertex(p11, n11, u1, v1, m);
		}
}

// Disque plein de normale n.
void disc(MeshBuilder &mb, Vec3 c, Vec3 n, float r, const Material &m, int segs = 10) {
	Vec3 ref = fabsf(n.y) > 0.9f ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
	Vec3 z = normalize(cross(ref, n));
	Vec3 x = cross(n, z);
	const float uv[6] = {0.5f, 0.5f, 0.6f, 0.5f, 0.5f, 0.6f};
	for (int i = 0; i < segs; i++) {
		float a0 = (float)i / segs * 2.0f * kPi, a1 = (float)(i + 1) / segs * 2.0f * kPi;
		Vec3 d0 = x * cosf(a0) + z * sinf(a0), d1 = x * cosf(a1) + z * sinf(a1);
		mb.tri(c, c + d1 * r, c + d0 * r, n, uv, m);
	}
}

// Rosace en laiton avec un cabochon, face vers +z, centrée en c.
void rosette(MeshBuilder &mb, Vec3 c, float r, Vec3 gem, float gemGlow) {
	Mat4 saved = mb.xf;
	mb.setTransform(multiply(saved, poseMatrix(c, quatAxisAngle({1, 0, 0}, rad(90.0f)))));
	const ProfilePoint p[] = {{r, 0.0f}, {r, r * 0.20f}, {r * 0.80f, r * 0.42f}, {r * 0.55f, r * 0.36f}, {r * 0.50f, r * 0.50f}};
	mb.lathe({0, 0, 0}, p, 5, 12, mat(kTexMetal, kBrass, 6.0f, 0.03f));
	ProfilePoint g[5];
	for (int i = 0; i < 5; i++) {
		float a = kPi * 0.5f * (float)i / 4.0f;
		g[i] = {cosf(a) * r * 0.50f, r * 0.45f + sinf(a) * r * 0.42f};
	}
	mb.lathe({0, 0, 0}, g, 5, 10, mat(kTexWhite, gem, 1.0f, gemGlow));
	mb.setTransform(saved);
}

} // namespace

void buildScreenFrame(MeshBuilder &mb, float aspect) {
	mb.verts.clear();
	mb.resetTransform();
	const float w = 1.0f, h = aspect;
	// Le bord intérieur part exactement du bord de l'image : aucun pixel du jeu n'est caché.
	const ProfilePoint prof[] = {{0.000f, -0.002f}, {0.000f, 0.008f}, {0.005f, 0.020f}, {0.014f, 0.030f}, {0.042f, 0.036f},
	                             {0.064f, 0.052f}, {0.080f, 0.042f}, {0.092f, 0.012f}, {0.092f, -0.022f}};
	mb.frame(w, h, prof, 9, mat(kTexGold, {1, 1, 1}), 4.2f);
	// Dos du cadre
	mb.box({0, 0, -0.016f}, {w * 0.5f + 0.092f, h * 0.5f + 0.092f, 0.006f}, mat(kTexWood, {0.5f, 0.45f, 0.42f}, 2.0f), 16);
	// Ornements d'angle et fleuron
	const Vec3 ruby{0.85f, 0.10f, 0.12f}, sapphire{0.15f, 0.30f, 0.90f};
	for (int i = 0; i < 4; i++) {
		float sx = (i & 1) ? 1.0f : -1.0f, sy = (i & 2) ? 1.0f : -1.0f;
		rosette(mb, {sx * (w * 0.5f + 0.046f), sy * (h * 0.5f + 0.046f), 0.040f}, 0.040f, sapphire, 0.55f);
	}
	rosette(mb, {0, h * 0.5f + 0.060f, 0.046f}, 0.050f, ruby, 0.65f);
	rosette(mb, {0, -(h * 0.5f + 0.050f), 0.044f}, 0.030f, ruby, 0.55f);
	for (float sx : {-1.0f, 1.0f})
		rosette(mb, {sx * (w * 0.5f + 0.050f), 0, 0.044f}, 0.030f, ruby, 0.55f);
	// Petites volutes de part et d'autre du fleuron
	for (float sx : {-1.0f, 1.0f}) {
		Vec3 a{sx * 0.055f, h * 0.5f + 0.092f, 0.02f}, b{sx * 0.16f, h * 0.5f + 0.118f, 0.02f}, c{sx * 0.25f, h * 0.5f + 0.094f, 0.02f};
		mb.rod(a, b, 0.012f, 0.009f, 6, mat(kTexMetal, kBrass, 6.0f));
		mb.rod(b, c, 0.009f, 0.005f, 6, mat(kTexMetal, kBrass, 6.0f));
	}
}

Vec3 crtLedPosition() {
	return {0.47f, -0.508f, 0.046f};
}

namespace {
const float kCrtBulge = 0.030f;
const float kCrtR2 = 0.5f * 0.5f + 0.375f * 0.375f;
} // namespace

float crtGlassZ(float x, float y) {
	x = clampf(x, -0.5f, 0.5f);
	y = clampf(y, -0.375f, 0.375f);
	return kCrtBulge * (1.0f - (x * x + y * y) / kCrtR2) - 0.004f;
}

void buildCrt(MeshBuilder &body, MeshBuilder &glass) {
	body.verts.clear();
	glass.verts.clear();
	body.resetTransform();
	glass.resetTransform();
	const float w = 1.0f, h = 0.75f;
	const Vec3 beige{0.80f, 0.76f, 0.66f}, beigeDark{0.66f, 0.62f, 0.53f};
	Material plastic = mat(kTexWhite, beige);

	// Dalle bombée
	const int nx = 16, ny = 12;
	const float bulge = kCrtBulge, r2 = kCrtR2;
	auto gp = [&](int i, int j, Vec3 &p, Vec3 &n, float &u, float &v) {
		float x = -0.5f + (float)i / nx, y = -0.375f + 0.75f * (float)j / ny;
		p = {x * w, y, crtGlassZ(x, y)};
		n = normalize(Vec3{2.0f * bulge * x / r2, 2.0f * bulge * y / r2, 1.0f});
		u = x + 0.5f;
		v = 0.5f - y / h;
	};
	Material gm = mat(kTexWhite);
	for (int j = 0; j < ny; j++)
		for (int i = 0; i < nx; i++) {
			Vec3 p[4], n[4];
			float u[4], v[4];
			gp(i, j, p[0], n[0], u[0], v[0]);
			gp(i + 1, j, p[1], n[1], u[1], v[1]);
			gp(i + 1, j + 1, p[2], n[2], u[2], v[2]);
			gp(i, j + 1, p[3], n[3], u[3], v[3]);
			const int idx[6] = {0, 1, 2, 0, 2, 3};
			for (int k : idx)
				glass.vertex(p[k], n[k], u[k], v[k], gm);
		}

	// Contour de l'écran
	const ProfilePoint prof[] = {{-0.012f, -0.010f}, {0.000f, 0.022f}, {0.030f, 0.036f}, {0.085f, 0.042f}, {0.095f, 0.034f}, {0.095f, -0.05f}};
	body.frame(w, h, prof, 6, plastic, 1.0f);
	// Bandeau inférieur avec boutons
	const float outW = w * 0.5f + 0.095f, outH = h * 0.5f + 0.095f;
	body.box({0, -outH - 0.038f, -0.004f}, {outW, 0.038f, 0.046f}, plastic, 63 & ~8);
	body.box({-0.36f, -outH - 0.036f, 0.044f}, {0.11f, 0.014f, 0.002f}, mat(kTexWhite, beigeDark));
	for (int i = 0; i < 3; i++)
		body.box({0.20f + 0.075f * (float)i, -outH - 0.038f, 0.046f}, {0.022f, 0.012f, 0.005f}, mat(kTexWhite, beigeDark));
	body.box({0.41f, -outH - 0.038f, 0.046f}, {0.026f, 0.016f, 0.006f}, mat(kTexWhite, beigeDark * 0.9f));
	Vec3 led = crtLedPosition();
	body.box(led, {0.008f, 0.008f, 0.003f}, mat(kTexWhite, {0.2f, 1.0f, 0.3f}, 1.0f, 1.0f));

	// Caisson qui se rétrécit vers l'arrière
	const float zf = -0.05f, zb = -0.92f;
	const float fy0 = -outH - 0.076f, fy1 = outH;
	const float bw = 0.34f, by0 = -0.36f, by1 = 0.26f;
	Vec3 f00{-outW, fy0, zf}, f10{outW, fy0, zf}, f11{outW, fy1, zf}, f01{-outW, fy1, zf};
	Vec3 b00{-bw, by0, zb}, b10{bw, by0, zb}, b11{bw, by1, zb}, b01{-bw, by1, zb};
	Material shell = mat(kTexWhite, beige * 0.96f);
	body.quad(f01, f11, b11, b01, shell); // dessus
	body.quad(f10, f00, b00, b10, shell); // dessous
	body.quad(f11, f10, b10, b11, shell); // droite
	body.quad(f00, f01, b01, b00, shell); // gauche
	body.quad(b10, b00, b01, b11, mat(kTexWhite, beigeDark)); // arrière
	// Ouïes d'aération sur le dessus
	for (int i = 0; i < 7; i++) {
		float t = 0.30f + 0.08f * (float)i;
		Vec3 l = lerp(f01, b01, t), r = lerp(f11, b11, t);
		Vec3 l2 = lerp(f01, b01, t + 0.03f), r2v = lerp(f11, b11, t + 0.03f);
		Vec3 up{0, 0.003f, 0};
		body.quad(lerp(l, r, 0.2f) + up, lerp(l, r, 0.8f) + up, lerp(l2, r2v, 0.8f) + up, lerp(l2, r2v, 0.2f) + up, mat(kTexWhite, {0.12f, 0.12f, 0.11f}));
	}
	// Pied pivotant
	body.cylinder({0, fy0 - 0.07f, -0.42f}, 0.16f, 0.14f, 0.09f, 16, mat(kTexWhite, beigeDark), false, false);
	body.cylinder({0, fy0 - 0.10f, -0.42f}, 0.34f, 0.32f, 0.035f, 20, plastic, true, true);

	// --- L'ordinateur de bureau (un « 486 » du début des années 90) sous le moniteur ---
	{
		const float top = fy0 - 0.10f, ch = 0.36f, cy = top - ch * 0.5f;
		const float hw = 0.76f, zFront = 0.10f, zBack = -1.06f, zc = (zFront + zBack) * 0.5f, hd = (zFront - zBack) * 0.5f;
		Material dark = mat(kTexWhite, beigeDark);
		Material slot = mat(kTexWhite, {0.07f, 0.07f, 0.07f});
		body.box({0, cy, zc}, {hw, ch * 0.5f, hd}, mat(kTexWhite, beige * 0.98f));
		// Façade : léger retrait en bas, baies de lecteurs à droite
		const float zf = zFront;
		body.box({0, cy - 0.135f, zf + 0.002f}, {hw - 0.01f, 0.035f, 0.004f}, dark, 63 & ~16);
		// Lecteur 5 pouces 1/4
		body.box({0.40f, cy + 0.085f, zf + 0.004f}, {0.30f, 0.062f, 0.006f}, mat(kTexWhite, beige * 0.90f), 63 & ~16);
		body.box({0.40f, cy + 0.092f, zf + 0.011f}, {0.26f, 0.006f, 0.002f}, slot, 63 & ~16);
		body.box({0.40f, cy + 0.106f, zf + 0.014f}, {0.030f, 0.020f, 0.008f}, dark, 63 & ~16);
		body.box({0.15f, cy + 0.045f, zf + 0.011f}, {0.010f, 0.006f, 0.002f}, mat(kTexWhite, {0.25f, 0.9f, 0.3f}, 1.0f, 0.5f), 63 & ~16);
		// Lecteur de disquettes 3 pouces 1/2
		body.box({0.46f, cy - 0.045f, zf + 0.004f}, {0.24f, 0.045f, 0.006f}, mat(kTexWhite, beige * 0.90f), 63 & ~16);
		body.box({0.46f, cy - 0.036f, zf + 0.011f}, {0.19f, 0.007f, 0.002f}, slot, 63 & ~16);
		body.box({0.62f, cy - 0.066f, zf + 0.012f}, {0.030f, 0.010f, 0.005f}, dark, 63 & ~16);
		body.box({0.27f, cy - 0.066f, zf + 0.011f}, {0.010f, 0.006f, 0.002f}, mat(kTexWhite, {0.25f, 0.9f, 0.3f}, 1.0f, 0.4f), 63 & ~16);
		// Panneau de commande à gauche : afficheur de vitesse, turbo, reset, serrure, marche
		body.box({-0.44f, cy + 0.045f, zf + 0.004f}, {0.25f, 0.10f, 0.005f}, mat(kTexWhite, beige * 0.92f), 63 & ~16);
		body.box({-0.56f, cy + 0.085f, zf + 0.010f}, {0.085f, 0.038f, 0.003f}, mat(kTexWhite, {0.05f, 0.02f, 0.02f}), 63 & ~16);
		// Chiffres « 66 » en segments rouges
		Material seg = mat(kTexWhite, {1.0f, 0.16f, 0.08f}, 1.0f, 1.0f);
		for (float dx : {-0.036f, 0.036f}) {
			float x0 = -0.56f + dx;
			body.box({x0, cy + 0.111f, zf + 0.014f}, {0.020f, 0.004f, 0.001f}, seg, 32);
			body.box({x0, cy + 0.085f, zf + 0.014f}, {0.020f, 0.004f, 0.001f}, seg, 32);
			body.box({x0, cy + 0.059f, zf + 0.014f}, {0.020f, 0.004f, 0.001f}, seg, 32);
			body.box({x0 - 0.020f, cy + 0.098f, zf + 0.014f}, {0.004f, 0.013f, 0.001f}, seg, 32);
			body.box({x0 - 0.020f, cy + 0.072f, zf + 0.014f}, {0.004f, 0.013f, 0.001f}, seg, 32);
			body.box({x0 + 0.020f, cy + 0.072f, zf + 0.014f}, {0.004f, 0.013f, 0.001f}, seg, 32);
		}
		body.box({-0.40f, cy + 0.100f, zf + 0.012f}, {0.035f, 0.014f, 0.006f}, dark, 63 & ~16);  // turbo
		body.box({-0.40f, cy + 0.062f, zf + 0.012f}, {0.035f, 0.014f, 0.006f}, dark, 63 & ~16);  // reset
		body.box({-0.31f, cy + 0.100f, zf + 0.011f}, {0.008f, 0.006f, 0.002f}, mat(kTexWhite, {0.25f, 0.9f, 0.3f}, 1.0f, 1.0f), 63 & ~16);
		body.box({-0.31f, cy + 0.062f, zf + 0.011f}, {0.008f, 0.006f, 0.002f}, mat(kTexWhite, {1.0f, 0.55f, 0.1f}, 1.0f, 0.6f), 63 & ~16);
		Mat4 saved = body.xf;
		body.setTransform(multiply(saved, poseMatrix({-0.25f, cy + 0.000f, zf + 0.004f}, quatAxisAngle({1, 0, 0}, kPi * 0.5f))));
		body.cylinder({0, 0, 0}, 0.024f, 0.022f, 0.012f, 12, mat(kTexMetal, {0.75f, 0.75f, 0.78f}, 6.0f), true, false); // serrure
		body.setTransform(saved);
		body.box({-0.25f, cy, zf + 0.018f}, {0.004f, 0.012f, 0.002f}, slot, 63 & ~16);
		body.box({-0.60f, cy - 0.010f, zf + 0.013f}, {0.050f, 0.026f, 0.008f}, mat(kTexWhite, beige * 1.04f), 63 & ~16); // marche/arrêt
		// Ouïes d'aération
		for (int i = 0; i < 5; i++)
			body.box({-0.42f, cy - 0.075f - 0.012f * (float)i, zf + 0.003f}, {0.22f, 0.003f, 0.001f}, slot, 32);
		// Pieds
		for (float sx : {-1.0f, 1.0f})
			for (float z : {zFront - 0.10f, zBack + 0.10f})
				body.box({sx * (hw - 0.10f), cy - ch * 0.5f - 0.008f, z}, {0.05f, 0.008f, 0.05f}, slot);
	}
}

// ---------------------------------------------------------------------------
// Gants

namespace {

struct Finger {
	float y;          // hauteur de l'articulation sur la paume
	float len[3];
	float radius;
};

const Finger kFingers[4] = {
	{0.030f, {0.040f, 0.026f, 0.022f}, 0.0108f}, // index
	{0.010f, {0.044f, 0.028f, 0.023f}, 0.0110f},
	{-0.010f, {0.040f, 0.026f, 0.022f}, 0.0104f},
	{-0.030f, {0.032f, 0.020f, 0.019f}, 0.0094f},
};

const float kGloveScale = 1.12f;
// Centre de la paume dans le repère de visée (main droite).
const Vec3 kPalm = {0.016f, -0.046f, 0.088f};

void fingerAngles(int finger, float trigger, float grip, float out[3]) {
	if (finger == 0) {
		const float open[3] = {6.0f, 9.0f, 7.0f}, closed[3] = {50.0f, 66.0f, 42.0f};
		for (int i = 0; i < 3; i++)
			out[i] = rad(mixf(open[i], closed[i], trigger));
	} else {
		const float rest[3] = {42.0f, 52.0f, 34.0f}, closed[3] = {70.0f, 82.0f, 58.0f};
		for (int i = 0; i < 3; i++)
			out[i] = rad(mixf(rest[i], closed[i], grip));
	}
}

// Calcule les articulations d'un doigt (repère de la main droite, avant mise à l'échelle).
void fingerJoints(int finger, float trigger, float grip, Vec3 joints[4]) {
	const Finger &f = kFingers[finger];
	float ang[3];
	fingerAngles(finger, trigger, grip, ang);
	Vec3 p{-0.004f, f.y, -0.046f};
	float a = 0.0f;
	joints[0] = p;
	for (int i = 0; i < 3; i++) {
		a += ang[i];
		// Les doigts se replient vers la paume (côté -x pour la main droite).
		p = p + Vec3{-sinf(a), 0, -cosf(a)} * f.len[i];
		joints[i + 1] = p;
	}
}

Vec3 handToAim(Vec3 p, bool leftHand) {
	Vec3 q = kPalm + p * kGloveScale;
	if (leftHand)
		q.x = -q.x;
	return q;
}

} // namespace

Vec3 gloveFingertip(bool leftHand, float trigger) {
	Vec3 joints[4];
	fingerJoints(0, trigger, 0.0f, joints);
	return handToAim(joints[3], leftHand);
}

void buildGlove(MeshBuilder &out, bool leftHand, float trigger, float grip) {
	MeshBuilder mb;
	Material leather = mat(kTexLeather, {0.44f, 0.26f, 0.13f}, 14.0f);
	Material leatherDark = mat(kTexLeather, {0.30f, 0.17f, 0.09f}, 14.0f);
	Material steel = mat(kTexMetal, {0.66f, 0.68f, 0.72f}, 10.0f, 0.03f);
	Material brass = mat(kTexMetal, kBrass, 10.0f, 0.04f);

	// Paume et dos de la main
	ellipsoid(mb, {0, 0, 0}, {0.021f, 0.047f, 0.052f}, leather, 8, 12);
	ellipsoid(mb, {0.012f, 0.002f, -0.004f}, {0.014f, 0.040f, 0.040f}, steel, 6, 10);
	// Plaque des phalanges
	ellipsoid(mb, {0.004f, 0.0f, -0.044f}, {0.016f, 0.045f, 0.013f}, steel, 5, 10);
	for (int i = 0; i < 4; i++)
		ellipsoid(mb, {0.017f, kFingers[i].y, -0.047f}, {0.005f, 0.006f, 0.006f}, brass, 3, 6);

	// Doigts
	for (int f = 0; f < 4; f++) {
		Vec3 j[4];
		fingerJoints(f, trigger, grip, j);
		float r = kFingers[f].radius;
		for (int s = 0; s < 3; s++) {
			float r0 = r * (1.0f - 0.10f * (float)s), r1 = r * (1.0f - 0.10f * (float)(s + 1));
			mb.rod(j[s], j[s + 1], r0, r1, 8, s == 0 ? leather : leatherDark, false);
			ellipsoid(mb, j[s + 1], {r1, r1, r1}, leatherDark, 4, 8);
		}
		ellipsoid(mb, j[0], {r * 1.05f, r * 1.05f, r * 1.05f}, leather, 4, 8);
	}
	// Pouce, posé sur le dessus de la manette
	{
		Vec3 b{-0.006f, 0.036f, 0.018f};
		Vec3 d1 = normalize(Vec3{-0.30f, 0.42f, -0.86f}), d2 = normalize(Vec3{-0.52f, 0.18f, -0.84f});
		Vec3 m = b + d1 * 0.036f, t = m + d2 * 0.028f;
		mb.rod(b, m, 0.0135f, 0.0118f, 8, leather, false);
		mb.rod(m, t, 0.0118f, 0.0100f, 8, leatherDark, false);
		ellipsoid(mb, b, {0.0145f, 0.0145f, 0.0145f}, leather, 4, 8);
		ellipsoid(mb, m, {0.0118f, 0.0118f, 0.0118f}, leatherDark, 4, 8);
		ellipsoid(mb, t, {0.0100f, 0.0100f, 0.0100f}, leatherDark, 4, 8);
	}
	// Manchette évasée
	{
		Vec3 w0{0.0f, -0.004f, 0.040f};
		Vec3 dir = normalize(Vec3{0.0f, -0.36f, 1.0f});
		Vec3 w1 = w0 + dir * 0.022f, w2 = w0 + dir * 0.118f;
		mb.rod(w0, w1, 0.031f, 0.034f, 12, brass, false);
		mb.rod(w1, w2, 0.034f, 0.054f, 12, leather, false);
		mb.rod(w2, w2 + dir * 0.012f, 0.056f, 0.057f, 12, brass, false);
		disc(mb, w2 + dir * 0.012f, dir, 0.057f, mat(kTexWhite, {0.03f, 0.02f, 0.02f}), 12);
		// Rivets
		for (int i = 0; i < 6; i++) {
			float a = (float)i / 6.0f * 2.0f * kPi;
			Vec3 x{1, 0, 0};
			Vec3 y = normalize(cross(dir, x));
			Vec3 p = w0 + dir * 0.085f + (x * cosf(a) + y * sinf(a)) * 0.047f;
			ellipsoid(mb, p, {0.004f, 0.004f, 0.004f}, brass, 3, 5);
		}
	}

	// Passage dans le repère de visée ; la main gauche est le miroir de la droite.
	size_t n = mb.verts.size();
	out.verts.reserve(out.verts.size() + n);
	for (size_t i = 0; i + 2 < n; i += 3) {
		Vertex tri[3] = {mb.verts[i], mb.verts[i + 1], mb.verts[i + 2]};
		for (Vertex &v : tri) {
			Vec3 p = handToAim({v.pos[0], v.pos[1], v.pos[2]}, leftHand);
			Vec3 nn{leftHand ? -v.normal[0] : v.normal[0], v.normal[1], v.normal[2]};
			Vec3 wp = transformPoint(out.xf, p);
			Vec3 wn = normalize(transformDir(out.xf, nn));
			v.pos[0] = wp.x; v.pos[1] = wp.y; v.pos[2] = wp.z;
			v.normal[0] = wn.x; v.normal[1] = wn.y; v.normal[2] = wn.z;
		}
		if (leftHand)
			std::swap(tri[1], tri[2]);
		out.verts.push_back(tri[0]);
		out.verts.push_back(tri[1]);
		out.verts.push_back(tri[2]);
	}
}

} // namespace lolvr
