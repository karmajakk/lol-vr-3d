// Test hors casque : fait tourner le cœur ScummVM sur PC (Linux) et enregistre des images PPM.
// Usage : host_test <dossier_systeme> <dossier_sauvegardes> <dossier_jeu|-> <dossier_sortie> [secondes]
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <string>

#include "../src/core_host.h"

using namespace lolvr;

static void logLine(int level, const char *msg) {
	fprintf(stderr, "[%d] %s%s", level, msg, (msg[0] && msg[strlen(msg) - 1] == '\n') ? "" : "\n");
}

static void savePpm(const Frame &f, const std::string &path) {
	FILE *fp = fopen(path.c_str(), "wb");
	if (!fp)
		return;
	fprintf(fp, "P6\n%d %d\n255\n", f.width, f.height);
	for (size_t i = 0; i < (size_t)f.width * f.height; i++)
		fwrite(&f.rgba[i * 4], 1, 3, fp);
	fclose(fp);
}

int main(int argc, char **argv) {
	if (argc < 5) {
		fprintf(stderr, "usage: %s systemDir saveDir gameDir|- outDir [seconds]\n", argv[0]);
		return 2;
	}
	std::string out = argv[4];
	double seconds = argc > 5 ? atof(argv[5]) : 6.0;

	Frame msg;
	renderMessage(msg, "Lands of Lore VR", "Fichiers du jeu introuvables.\nCopiez-les dans le dossier indiqué, puis relancez.\nAccents : é è à ç ê î ô ù");
	savePpm(msg, out + "/message.ppm");

	HostConfig cfg;
	cfg.systemDir = argv[1];
	cfg.saveDir = argv[2];
	cfg.gameDir = std::string(argv[3]) == "-" ? "" : argv[3];
	cfg.log = logLine;
	CoreHost &host = CoreHost::get();
	if (!host.start(cfg))
		return 1;

	Frame f;
	int16_t audio[960];
	long nonSilent = 0;
	int steps = (int)(seconds * 100);
	for (int i = 0; i < steps && !host.finished(); i++) {
		usleep(10000);
		host.readAudio(audio, 480);
		for (int k = 0; k < 960; k++)
			nonSilent += audio[k] != 0;
		// Balaye le pointeur en diagonale, clique à mi-parcours.
		float t = (float)i / steps;
		host.setPointer(0.1f + 0.8f * t, 0.1f + 0.8f * t);
		host.setMouseButtons(i > steps / 2 && i < steps / 2 + 10, false);
		// Ouvre le clavier virtuel au tiers du parcours.
		host.setSpecialButtons(i >= steps / 3 && i < steps / 3 + 5, false);
		if (host.fetchFrame(f) && (i % 100 == 99)) {
			char name[64];
			snprintf(name, sizeof(name), "/frame_%02d.ppm", i / 100);
			savePpm(f, out + name);
		}
	}
	GameState gs;
	bool hasState = host.gameState(gs);
	printf("état du jeu : %s niveau=%d décor='%s' sons=%u blessures=%u coups=%u\n", hasState ? "disponible" : "ABSENT",
	       gs.level, gs.tileset, gs.sfxSerial, gs.hurtSerial, gs.hitSerial);
	if (!hasState)
		return 1;
	printf("images: serial=%llu taille=%dx%d, échantillons audio non nuls=%ld, fini=%d\n",
	       (unsigned long long)f.serial, f.width, f.height, nonSilent, (int)host.finished());
	host.stop();
	return f.serial > 0 ? 0 : 1;
}
