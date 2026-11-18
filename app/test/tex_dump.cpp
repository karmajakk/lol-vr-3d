// Outil de mise au point : écrit chaque texture procédurale dans un fichier PPM.
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include "../src/proctex.h"
using namespace lolvr;
static void writePpm(const std::string &path, const uint8_t *rgba, bool showAlpha) {
	FILE *f = fopen(path.c_str(), "wb");
	fprintf(f, "P6\n%d %d\n255\n", kTexSize, kTexSize);
	for (int y = kTexSize - 1; y >= 0; y--)
		for (int x = 0; x < kTexSize; x++) {
			const uint8_t *p = rgba + (y * kTexSize + x) * 4;
			uint8_t o[3] = {p[0], p[1], p[2]};
			if (showAlpha) { int chk = ((x / 16 + y / 16) & 1) ? 200 : 120; for (int k = 0; k < 3; k++) o[k] = (uint8_t)((p[k] * p[3] + chk * (255 - p[3])) / 255); }
			fwrite(o, 1, 3, f);
		}
	fclose(f);
}
int main(int argc, char **argv) {
	std::string out = argc > 1 ? argv[1] : ".";
	std::vector<uint8_t> buf(kTexSize * kTexSize * 4);
	for (int l = 0; l < kTexCount; l++) {
		if (layerDependsOnTheme(l)) {
			for (int t = 0; t < kThemeCount; t++) {
				generateTexture(l, t, buf.data());
				writePpm(out + "/layer" + std::to_string(l) + "_theme" + std::to_string(t) + ".ppm", buf.data(), false);
			}
		} else {
			generateTexture(l, 0, buf.data());
			writePpm(out + "/layer" + std::to_string(l) + ".ppm", buf.data(), l == kTexLattice);
		}
	}
	return 0;
}
