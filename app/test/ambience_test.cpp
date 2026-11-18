// Banc d'essai du son : calcule l'ambiance hors ligne, vérifie les niveaux et écrit des fichiers WAV.
// Usage : ambience_test <dossier de sortie>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#include "../src/ambience.h"
#include "../src/proctex.h"

using namespace lolvr;

static void writeWav(const std::string &path, const std::vector<int16_t> &pcm, int rate) {
	FILE *f = fopen(path.c_str(), "wb");
	if (!f)
		return;
	uint32_t dataLen = (uint32_t)(pcm.size() * 2), riffLen = 36 + dataLen, fmtLen = 16, byteRate = (uint32_t)rate * 4;
	uint16_t pcmFmt = 1, channels = 2, align = 4, bits = 16;
	uint32_t r = (uint32_t)rate;
	fwrite("RIFF", 1, 4, f); fwrite(&riffLen, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fmtLen, 4, 1, f);
	fwrite(&pcmFmt, 2, 1, f); fwrite(&channels, 2, 1, f); fwrite(&r, 4, 1, f); fwrite(&byteRate, 4, 1, f);
	fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&dataLen, 4, 1, f);
	fwrite(pcm.data(), 2, pcm.size(), f);
	fclose(f);
}

struct Stats {
	double rmsL, rmsR;
	int peak;
};

static Stats measure(const std::vector<int16_t> &pcm, size_t fromFrame) {
	double sl = 0, sr = 0;
	int peak = 0;
	size_t n = 0;
	for (size_t i = fromFrame; i < pcm.size() / 2; i++, n++) {
		int l = pcm[i * 2], r = pcm[i * 2 + 1];
		sl += (double)l * l;
		sr += (double)r * r;
		if (abs(l) > peak) peak = abs(l);
		if (abs(r) > peak) peak = abs(r);
	}
	return {sqrt(sl / (double)n), sqrt(sr / (double)n), peak};
}

int main(int argc, char **argv) {
	std::string out = argc > 1 ? argv[1] : ".";
	const int rate = 48000, block = 480;
	Ambience &amb = Ambience::get();
	amb.setSampleRate(rate);
	amb.setScreen({0, 0, -2.2f}, {0, 0, 0, 1}, 2.4f);
	amb.setRoom({0, -1.25f, 0}, 0.0f);
	amb.setFire(true);
	int failures = 0;

	// 1. Ambiance seule, pour chaque zone : pas de saturation, niveau raisonnable.
	for (int theme = 0; theme < kThemeCount; theme++) {
		amb.setListener({0, 0, 0}, {0, 0, 0, 1});
		amb.setMood(true, theme);
		std::vector<int16_t> pcm((size_t)rate * 12 * 2, 0);
		for (size_t i = 0; i + (size_t)block * 2 <= pcm.size(); i += (size_t)block * 2)
			amb.process(&pcm[i], block);
		Stats s = measure(pcm, (size_t)rate * 4);
		bool ok = s.peak < 20000 && s.rmsL > 100 && s.rmsL < 2000 && s.rmsR > 100 && s.rmsR < 2000;
		printf("%-14s rms G %6.0f D %6.0f  crête %5d  %s\n", themeInfo(theme).name, s.rmsL, s.rmsR, s.peak, ok ? "ok" : "ÉCHEC");
		failures += ok ? 0 : 1;
		if (theme == kThemeKeep || theme == kThemeForest || theme == kThemeCave)
			writeWav(out + "/ambiance_" + std::to_string(theme) + ".wav", pcm, rate);
	}

	// 2. Ambiance coupée : le son du jeu passe intact quand on regarde l'écran.
	amb.setMood(false, 0);
	{
		std::vector<int16_t> warm((size_t)rate * 14 * 2, 0);
		for (size_t i = 0; i + (size_t)block * 2 <= warm.size(); i += (size_t)block * 2)
			amb.process(&warm[i], block);
		Stats s = measure(warm, (size_t)rate * 12);
		bool ok = s.peak == 0;
		printf("silence après coupure : crête %d  %s\n", s.peak, ok ? "ok" : "ÉCHEC");
		failures += ok ? 0 : 1;
	}
	auto tone = [&](bool left, Quat head) {
		amb.setListener({0, 0, 0}, head);
		std::vector<int16_t> pcm((size_t)rate * 2 * 2, 0);
		for (size_t i = 0; i < pcm.size() / 2; i++)
			pcm[i * 2 + (left ? 0 : 1)] = (int16_t)(8000.0 * sin(2.0 * 3.14159265 * 440.0 * (double)i / rate));
		for (size_t i = 0; i + (size_t)block * 2 <= pcm.size(); i += (size_t)block * 2)
			amb.process(&pcm[i], block);
		return measure(pcm, (size_t)rate);
	};
	Stats a = tone(true, {0, 0, 0, 1});
	Stats b = tone(false, {0, 0, 0, 1});
	printf("face à l'écran  : canal G -> G %5.0f D %5.0f ; canal D -> G %5.0f D %5.0f\n", a.rmsL, a.rmsR, b.rmsL, b.rmsR);
	bool ok = a.rmsL > 4.0 * a.rmsR && b.rmsR > 4.0 * b.rmsL;
	failures += ok ? 0 : 1;
	// Tête tournée de 90° vers la gauche : l'écran est à droite, le son doit venir surtout de droite.
	Stats c = tone(true, quatYaw(3.14159265f / 2.0f));
	Stats d = tone(false, quatYaw(3.14159265f / 2.0f));
	printf("tête à gauche   : canal G -> G %5.0f D %5.0f ; canal D -> G %5.0f D %5.0f\n", c.rmsL, c.rmsR, d.rmsL, d.rmsR);
	bool ok2 = (c.rmsR + d.rmsR) > 1.3 * (c.rmsL + d.rmsL);
	failures += ok2 ? 0 : 1;
	printf("%s\n", failures ? "ÉCHEC" : "TOUT EST BON");
	return failures ? 1 : 0;
}
