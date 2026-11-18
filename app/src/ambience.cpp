// lol-vr — audio spatial et ambiance sonore synthétisée.

#include "ambience.h"

#include <math.h>

#include <algorithm>
#include <atomic>
#include <vector>

#include "proctex.h"

namespace lolvr {

namespace {

const float kPi = 3.14159265f;

// Part du son du jeu qui reste « collée » à l'écran quand on tourne la tête (0 = stéréo classique).
const float kAnchorAmount = 0.65f;
// Volume général de l'ambiance.
const float kAmbienceVolume = 4.0f;

// Positions des sources dans le repère de la pièce (voir room.cpp).
const Vec3 kFireSource = {2.45f, 0.45f, -0.6f};
const Vec3 kWindowSource = {-2.75f, 1.9f, -0.6f};

struct Shared {
	std::atomic<float> listener[3], right[3], fwd[3];
	std::atomic<float> screen[3], screenRight[3], screenWidth{2.4f};
	std::atomic<float> roomPos[3], roomYaw{0.0f};
	std::atomic<int> ambienceOn{0}, theme{0}, fireOn{0};
	Shared() {
		for (int i = 0; i < 3; i++) {
			listener[i] = 0.0f;
			right[i] = i == 0 ? 1.0f : 0.0f;
			fwd[i] = i == 2 ? -1.0f : 0.0f;
			screen[i] = i == 2 ? -2.2f : 0.0f;
			screenRight[i] = i == 0 ? 1.0f : 0.0f;
			roomPos[i] = 0.0f;
		}
	}
};

Shared g;

struct Rng {
	uint32_t s = 0x2F6E2B1u;
	float uni() {
		s ^= s << 13;
		s ^= s >> 17;
		s ^= s << 5;
		return (float)(s >> 8) / 16777216.0f;
	}
	float bi() { return uni() * 2.0f - 1.0f; }
};

// État propre au fil audio.
struct State {
	int rate = 48000;
	Rng rng;
	float gains[4] = {1, 0, 0, 1}; // G->G, D->G, G->D, D->D
	float master = 0.0f;
	float level[4] = {0, 0, 0, 0}; // vent, gouttes, grillons, grondement (lissés)
	float firePan[2] = {0.5f, 0.5f}, windowPan[2] = {0.5f, 0.5f};
	float fireGain = 0.0f;
	// Feu
	float fireLp = 0, fireLp2 = 0, crEnv = 0, crDecay = 0.99f, crLp = 0, fireSlow = 0.5f, fireSlowTarget = 0.5f;
	int fireSlowCount = 0;
	// Vent
	float windLow = 0, windBand = 0;
	double windPhase[4] = {0.0, 1.3, 2.9, 4.1};
	// Gouttes
	int dripTimer = 24000;
	float dripPhase = 0, dripFreq = 1000, dripEnv = 0, dripDecay = 0.999f, dripSweep = 1.0f, dripPan = 0.5f;
	std::vector<float> echoL, echoR;
	size_t echoPos = 0;
	// Grillons
	double cricketT = 0;
	float cricketPhase[2] = {0, 0};
	// Grondement
	float rumLp1 = 0, rumLp2 = 0;
	double rumPhase = 0;
};

State st;

Vec3 load3(const std::atomic<float> *a) {
	return {a[0].load(std::memory_order_relaxed), a[1].load(std::memory_order_relaxed), a[2].load(std::memory_order_relaxed)};
}

void store3(std::atomic<float> *a, Vec3 v) {
	a[0].store(v.x, std::memory_order_relaxed);
	a[1].store(v.y, std::memory_order_relaxed);
	a[2].store(v.z, std::memory_order_relaxed);
}

// Gains gauche/droite d'une source ponctuelle, à puissance constante.
void panFor(Vec3 source, Vec3 listener, Vec3 right, Vec3 fwd, float out[2]) {
	Vec3 d = source - listener;
	float dist = length(d);
	if (dist < 1e-3f) {
		out[0] = out[1] = 0.707f;
		return;
	}
	float x = clampf(dot(d, right) / dist, -1.0f, 1.0f);
	float front = dot(d, fwd) / dist;
	float att = std::min(1.0f, 1.6f / (1.0f + dist)) * (0.85f + 0.15f * front);
	// On garde un peu de son dans l'oreille opposée : plus naturel au casque.
	float pan = x * 0.85f;
	out[0] = sqrtf(0.5f * (1.0f - pan)) * att;
	out[1] = sqrtf(0.5f * (1.0f + pan)) * att;
}

float sat(float v) {
	return v > 32767.0f ? 32767.0f : (v < -32768.0f ? -32768.0f : v);
}

} // namespace

Ambience &Ambience::get() {
	static Ambience instance;
	return instance;
}

void Ambience::setSampleRate(int rate) {
	st.rate = rate > 8000 ? rate : 48000;
	size_t n = (size_t)(st.rate * 0.137f);
	st.echoL.assign(n, 0.0f);
	st.echoR.assign(n, 0.0f);
	st.echoPos = 0;
}

void Ambience::setListener(Vec3 pos, Quat rot) {
	store3(g.listener, pos);
	store3(g.right, rotate(rot, {1, 0, 0}));
	store3(g.fwd, rotate(rot, {0, 0, -1}));
}

void Ambience::setScreen(Vec3 pos, Quat rot, float width) {
	store3(g.screen, pos);
	store3(g.screenRight, rotate(rot, {1, 0, 0}));
	g.screenWidth.store(width, std::memory_order_relaxed);
}

void Ambience::setRoom(Vec3 pos, float yaw) {
	store3(g.roomPos, pos);
	g.roomYaw.store(yaw, std::memory_order_relaxed);
}

void Ambience::setMood(bool ambienceOn, int theme) {
	g.ambienceOn.store(ambienceOn ? 1 : 0, std::memory_order_relaxed);
	g.theme.store(theme, std::memory_order_relaxed);
}

void Ambience::setFire(bool on) {
	g.fireOn.store(on ? 1 : 0, std::memory_order_relaxed);
}

void Ambience::process(int16_t *samples, size_t frames) {
	if (frames == 0)
		return;
	if (st.echoL.empty())
		setSampleRate(st.rate);
	const float rate = (float)st.rate;
	Vec3 listener = load3(g.listener), right = load3(g.right), fwd = load3(g.fwd);

	// --- Son du jeu : deux enceintes virtuelles aux bords de l'écran ---
	float target[4];
	{
		Vec3 screen = load3(g.screen), sRight = load3(g.screenRight);
		float half = g.screenWidth.load(std::memory_order_relaxed) * 0.5f;
		Vec3 toScreen = screen - listener;
		float dist = std::max(0.3f, length(toScreen));
		float alpha = std::max(0.12f, atanf(half / dist));
		float sideGain[2][2];
		for (int ch = 0; ch < 2; ch++) {
			Vec3 sp = screen + sRight * (ch == 0 ? -half : half);
			Vec3 d = sp - listener;
			float az = atan2f(dot(d, right), dot(d, fwd));
			float pan = clampf(az / alpha, -1.0f, 1.0f);
			sideGain[ch][0] = cosf((pan + 1.0f) * kPi * 0.25f);
			sideGain[ch][1] = sinf((pan + 1.0f) * kPi * 0.25f);
		}
		const float k = kAnchorAmount;
		target[0] = (1.0f - k) + k * sideGain[0][0]; // G -> oreille gauche
		target[1] = k * sideGain[1][0];              // D -> oreille gauche
		target[2] = k * sideGain[0][1];              // G -> oreille droite
		target[3] = (1.0f - k) + k * sideGain[1][1]; // D -> oreille droite
	}
	float gainStep[4];
	for (int i = 0; i < 4; i++)
		gainStep[i] = (target[i] - st.gains[i]) / (float)frames;

	// --- Ambiance ---
	const ThemeInfo &th = themeInfo(g.theme.load(std::memory_order_relaxed));
	bool on = g.ambienceOn.load(std::memory_order_relaxed) != 0;
	float masterTarget = on ? kAmbienceVolume : 0.0f;
	float slew = 1.0f - expf(-(float)frames / (rate * 0.8f));
	// Les sons d'extérieur (vent, gouttes, grillons, grondement) sont retirés : il ne reste que le
	// feu de cheminée, que le joueur allume ou non.
	(void)th;
	const float want[4] = {0.0f, 0.0f, 0.0f, 0.0f};
	for (int i = 0; i < 4; i++)
		st.level[i] += (want[i] - st.level[i]) * slew;
	float masterStep = ((st.master + (masterTarget - st.master) * slew) - st.master) / (float)frames;

	const float fireTarget = g.fireOn.load(std::memory_order_relaxed) ? 1.0f : 0.0f;
	float firePanT[2], windowPanT[2];
	{
		float yaw = g.roomYaw.load(std::memory_order_relaxed);
		Vec3 rp = load3(g.roomPos);
		Quat q = quatYaw(yaw);
		panFor(rp + rotate(q, kFireSource), listener, right, fwd, firePanT);
		panFor(rp + rotate(q, kWindowSource), listener, right, fwd, windowPanT);
	}
	float firePanStep[2], windowPanStep[2];
	for (int i = 0; i < 2; i++) {
		firePanStep[i] = (firePanT[i] - st.firePan[i]) / (float)frames;
		windowPanStep[i] = (windowPanT[i] - st.windowPan[i]) / (float)frames;
	}

	bool silent = st.master < 1e-4f && masterTarget == 0.0f;
	const float aFire = 1.0f - expf(-2.0f * kPi * 240.0f / rate);
	const float aCr = 1.0f - expf(-2.0f * kPi * 1800.0f / rate);
	const float aRum = 1.0f - expf(-2.0f * kPi * 75.0f / rate);
	const size_t echoN = st.echoL.size();

	for (size_t i = 0; i < frames; i++) {
		float inL = (float)samples[i * 2], inR = (float)samples[i * 2 + 1];
		float outL = inL * st.gains[0] + inR * st.gains[1];
		float outR = inL * st.gains[2] + inR * st.gains[3];
		for (int k = 0; k < 4; k++)
			st.gains[k] += gainStep[k];

		if (!silent) {
			float ambL = 0.0f, ambR = 0.0f;
			// Feu : souffle grave, crépitements, claquements occasionnels
			{
				float n = st.rng.bi();
				st.fireLp += aFire * (n - st.fireLp);
				st.fireLp2 += aFire * (st.fireLp - st.fireLp2);
				if (--st.fireSlowCount <= 0) {
					st.fireSlowCount = st.rate / 6;
					st.fireSlowTarget = 0.35f + 0.65f * st.rng.uni();
				}
				st.fireSlow += (st.fireSlowTarget - st.fireSlow) * (4.0f / rate);
				float u = st.rng.uni();
				if (u < 11.0f / rate) {
					float a = st.rng.uni();
					st.crEnv = std::max(st.crEnv, 0.10f + 0.9f * a * a);
					st.crDecay = expf(-1.0f / (rate * (0.0015f + 0.010f * st.rng.uni())));
				} else if (u < 11.5f / rate) {
					st.crEnv = 1.7f;
					st.crDecay = expf(-1.0f / (rate * 0.018f));
				}
				st.crLp += aCr * (n - st.crLp);
				float crackle = (n - st.crLp) * st.crEnv;
				st.crEnv *= st.crDecay;
				st.fireGain += (fireTarget - st.fireGain) * (3.0f / rate);
				float fire = (st.fireLp2 * 9000.0f * st.fireSlow + crackle * 2100.0f + n * 22.0f) * st.fireGain;
				ambL += fire * st.firePan[0];
				ambR += fire * st.firePan[1];
			}
			// Vent à la fenêtre : bruit filtré dont la hauteur et la force ondulent lentement
			if (st.level[0] > 0.01f) {
				static const double speed[4] = {0.071, 0.113, 0.047, 0.173};
				for (int k = 0; k < 4; k++) {
					st.windPhase[k] += 2.0 * 3.14159265358979 * speed[k] / (double)rate;
					if (st.windPhase[k] > 6.283185307179586)
						st.windPhase[k] -= 6.283185307179586;
				}
				float fc = 330.0f + 190.0f * sinf((float)st.windPhase[0]) + 110.0f * sinf((float)st.windPhase[1]);
				float amp = std::max(0.08f, 0.45f + 0.38f * sinf((float)st.windPhase[2]) + 0.22f * sinf((float)st.windPhase[3]));
				float f = 2.0f * sinf(kPi * fc / rate);
				float n = st.rng.bi();
				st.windLow += f * st.windBand;
				float high = n - st.windLow - 0.38f * st.windBand;
				st.windBand += f * high;
				float wind = st.windBand * 8000.0f * amp * st.level[0];
				ambL += wind * st.windowPan[0];
				ambR += wind * st.windowPan[1];
			}
			// Gouttes d'eau avec écho
			float dripL = 0.0f, dripR = 0.0f;
			if (st.level[1] > 0.01f) {
				if (--st.dripTimer <= 0) {
					st.dripTimer = (int)(rate * (0.9f + 3.6f * st.rng.uni()) / std::max(0.3f, st.level[1]));
					st.dripFreq = 750.0f + 950.0f * st.rng.uni();
					st.dripEnv = 0.5f + 0.5f * st.rng.uni();
					st.dripDecay = expf(-1.0f / (rate * (0.030f + 0.035f * st.rng.uni())));
					st.dripSweep = expf(logf(1.45f) / (0.05f * rate));
					st.dripPan = 0.15f + 0.7f * st.rng.uni();
					st.dripPhase = 0.0f;
				}
				if (st.dripEnv > 1e-4f) {
					st.dripPhase += 2.0f * kPi * st.dripFreq / rate;
					if (st.dripPhase > 2.0f * kPi)
						st.dripPhase -= 2.0f * kPi;
					float s = sinf(st.dripPhase) * st.dripEnv * 3200.0f * st.level[1];
					st.dripFreq *= st.dripSweep;
					st.dripEnv *= st.dripDecay;
					dripL = s * (1.0f - st.dripPan);
					dripR = s * st.dripPan;
				}
			}
			if (echoN > 0) {
				// L'écho croise les canaux : impression d'espace.
				float eL = st.echoL[st.echoPos], eR = st.echoR[st.echoPos];
				float wetL = dripL + eR * 0.42f, wetR = dripR + eL * 0.42f;
				st.echoL[st.echoPos] = wetL;
				st.echoR[st.echoPos] = wetR;
				st.echoPos = (st.echoPos + 1) % echoN;
				ambL += wetL;
				ambR += wetR;
			}
			// Grillons : deux chanteurs légèrement désaccordés
			if (st.level[2] > 0.01f) {
				st.cricketT += 1.0 / (double)rate;
				if (st.cricketT > 3600.0)
					st.cricketT -= 3600.0;
				static const float period[2] = {0.78f, 1.03f}, freq[2] = {4350.0f, 4720.0f}, side[2] = {0.75f, 0.35f};
				for (int c = 0; c < 2; c++) {
					float tc = fmodf((float)st.cricketT + 0.37f * (float)c, period[c]);
					float env = 0.0f;
					if (tc < 0.105f) {
						float p = fmodf(tc, 0.035f) / 0.035f;
						env = p < 0.55f ? sinf(p / 0.55f * kPi) : 0.0f;
						env *= env;
					}
					st.cricketPhase[c] += 2.0f * kPi * freq[c] / rate;
					if (st.cricketPhase[c] > 2.0f * kPi)
						st.cricketPhase[c] -= 2.0f * kPi;
					float s = sinf(st.cricketPhase[c]) * env * 800.0f * st.level[2];
					ambL += s * st.windowPan[0] * (0.6f + side[c]);
					ambR += s * st.windowPan[1] * (1.35f - side[c]);
				}
			}
			// Grondement sourd, sans direction
			if (st.level[3] > 0.01f) {
				float n = st.rng.bi();
				st.rumLp1 += aRum * (n - st.rumLp1);
				st.rumLp2 += aRum * (st.rumLp1 - st.rumLp2);
				st.rumPhase += 2.0 * 3.14159265358979 * 0.09 / (double)rate;
				if (st.rumPhase > 6.283185307179586)
					st.rumPhase -= 6.283185307179586;
				float r = st.rumLp2 * 11000.0f * (0.55f + 0.45f * sinf((float)st.rumPhase)) * st.level[3];
				ambL += r;
				ambR += r;
			}
			outL += ambL * st.master;
			outR += ambR * st.master;
			st.master += masterStep;
			for (int k = 0; k < 2; k++) {
				st.firePan[k] += firePanStep[k];
				st.windowPan[k] += windowPanStep[k];
			}
		}
		samples[i * 2] = (int16_t)sat(outL);
		samples[i * 2 + 1] = (int16_t)sat(outR);
	}
	if (silent) {
		for (int k = 0; k < 2; k++) {
			st.firePan[k] = firePanT[k];
			st.windowPan[k] = windowPanT[k];
		}
		st.master = 0.0f;
	}
}

} // namespace lolvr
