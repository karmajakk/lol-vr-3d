// lol-vr-3d (essais) — lecture directe des fichiers PAK de Lands of Lore, pour tester le donjon hors
// casque. Dans l'application, ces données viennent du moteur (retro_lolvr_level).
#pragma once

#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "dungeon_data.h"

namespace lolfiles {

typedef std::vector<uint8_t> Bytes;

inline std::string upper(std::string s) {
	for (auto &c : s)
		c = (char)toupper((unsigned char)c);
	return s;
}

inline bool readFile(const std::string &path, Bytes &out) {
	FILE *f = fopen(path.c_str(), "rb");
	if (!f)
		return false;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	out.resize((size_t)n);
	size_t r = fread(out.data(), 1, (size_t)n, f);
	fclose(f);
	return r == (size_t)n;
}

// Tous les fichiers contenus dans les PAK d'un dossier.
struct Archive {
	std::map<std::string, Bytes> files;

	bool open(const std::string &dir) {
		DIR *d = opendir(dir.c_str());
		if (!d)
			return false;
		std::vector<std::string> paks;
		while (dirent *e = readdir(d)) {
			std::string n = e->d_name;
			if (n.size() > 4 && !strcasecmp(n.c_str() + n.size() - 4, ".PAK"))
				paks.push_back(n);
		}
		closedir(d);
		std::sort(paks.begin(), paks.end());
		for (const std::string &p : paks) {
			Bytes b;
			if (!readFile(dir + "/" + p, b))
				continue;
			std::vector<std::pair<std::string, uint32_t>> ents;
			size_t pos = 0;
			while (pos + 5 < b.size()) {
				uint32_t off = b[pos] | (b[pos + 1] << 8) | (b[pos + 2] << 16) | ((uint32_t)b[pos + 3] << 24);
				pos += 4;
				std::string name;
				while (pos < b.size() && b[pos])
					name += (char)b[pos++];
				pos++;
				if (name.empty() || off == 0 || off > b.size())
					break;
				ents.push_back({upper(name), off});
			}
			for (size_t i = 0; i < ents.size(); i++) {
				uint32_t end = i + 1 < ents.size() ? ents[i + 1].second : (uint32_t)b.size();
				if (end < ents[i].second)
					continue;
				if (!files.count(ents[i].first))
					files[ents[i].first] = Bytes(b.begin() + ents[i].second, b.begin() + end);
			}
		}
		return !files.empty();
	}

	const Bytes *get(const std::string &name) const {
		auto it = files.find(upper(name));
		return it == files.end() ? nullptr : &it->second;
	}
};

// Décompression « format 80 » (LCW), comme Screen::decodeFrame4 de ScummVM.
inline Bytes lcw(const uint8_t *src, size_t srcLen, size_t size) {
	Bytes dst;
	dst.reserve(size);
	size_t s = 0;
	while (dst.size() < size && s < srcLen) {
		uint8_t c = src[s++];
		size_t left = size - dst.size();
		if (!(c & 0x80)) {
			size_t len = (c >> 4) + 3, off = ((c & 0xF) << 8) | src[s++];
			for (size_t i = 0; i < len && i < left; i++)
				dst.push_back(dst[dst.size() - off]);
		} else if (c & 0x40) {
			size_t len = (c & 0x3F) + 3;
			if (c == 0xFE) {
				len = src[s] | (src[s + 1] << 8);
				s += 2;
				uint8_t v = src[s++];
				for (size_t i = 0; i < len && i < left; i++)
					dst.push_back(v);
			} else {
				if (c == 0xFF) {
					len = src[s] | (src[s + 1] << 8);
					s += 2;
				}
				size_t off = src[s] | (src[s + 1] << 8);
				s += 2;
				for (size_t i = 0; i < len && i < left; i++)
					dst.push_back(dst[off + i]);
			}
		} else if (c != 0x80) {
			size_t len = c & 0x3F;
			for (size_t i = 0; i < len && i < left; i++)
				dst.push_back(src[s++]);
		} else {
			break;
		}
	}
	return dst;
}

// Comme Screen::loadBitmap : en-tête de 10 octets, palette éventuelle, puis les données.
inline bool bitmap(const Archive &a, const std::string &name, Bytes &out) {
	const Bytes *b = a.get(name);
	if (!b || b->size() < 10)
		return false;
	const uint8_t *p = b->data();
	uint8_t comp = p[2];
	uint32_t size = p[4] | (p[5] << 8) | (p[6] << 16) | ((uint32_t)p[7] << 24);
	uint16_t pal = p[8] | (p[9] << 8);
	const uint8_t *src = p + 10 + pal;
	size_t srcLen = b->size() - 10 - pal;
	if (comp == 0)
		out.assign(src, src + (size < srcLen ? size : srcLen));
	else if (comp == 4)
		out = lcw(src, srcLen, size);
	else
		return false;
	return true;
}

// Un niveau, chargé comme le fait le moteur (loadLevelWallData, loadBlockProperties, loadLevelGraphics).
struct Level {
	uint8_t walls[1024 * 4];
	uint8_t vmpMap[256], wallFlags[256];
	std::vector<uint16_t> vmp;
	Bytes vcnFile;
	const uint8_t *vcn = nullptr, *shift = nullptr, *col = nullptr, *pal = nullptr;
	int vcnCount = 0;
	std::string area;

	bool load(const Archive &a, int n) {
		static const char *areas[] = {"KEEP", "FOREST1", "MANOR", "CAVE1", "SWAMP", "URBISH", "MINE1", "TOWER1", "YVEL1", "CATWALK", "RUIN", "CIMMERIA"};
		memset(walls, 0, sizeof(walls));
		memset(vmpMap, 0, sizeof(vmpMap));
		memset(wallFlags, 0, sizeof(wallFlags));
		char name[32];
		snprintf(name, sizeof(name), "LEVEL%d.WLL", n);
		const Bytes *w = a.get(name);
		if (!w || w->size() < 2)
			return false;
		int ai = (*w)[0] | ((*w)[1] << 8);
		if (ai < 0 || ai > 11)
			return false;
		area = areas[ai];
		for (size_t i = 0; i < (w->size() - 2) / 12; i++) {
			const uint8_t *r = w->data() + 2 + i * 12;
			int c = r[0] | (r[1] << 8);
			if (c < 0 || c > 255)
				continue;
			vmpMap[c] = r[2];
			wallFlags[c] = r[8];
		}
		Bytes cmz;
		snprintf(name, sizeof(name), "LEVEL%d.CMZ", n);
		if (!bitmap(a, name, cmz) || cmz.size() < 6)
			return false;
		int len = cmz[4] | (cmz[5] << 8);
		for (int i = 0; i < 1024; i++)
			for (int k = 0; k < 4; k++)
				walls[i * 4 + k] = (size_t)(6 + i * len + k) < cmz.size() ? cmz[6 + i * len + k] : 0;
		if (!bitmap(a, area + ".VCN", vcnFile) || vcnFile.size() < 2)
			return false;
		vcnCount = vcnFile[0] | (vcnFile[1] << 8);
		size_t p = 2;
		shift = &vcnFile[p];
		p += vcnCount;
		col = &vcnFile[p];
		p += 128;
		pal = &vcnFile[p];
		p += 384;
		vcn = &vcnFile[p];
		Bytes m;
		if (!bitmap(a, area + ".VMP", m) || m.size() < 2)
			return false;
		int cnt = m[0] | (m[1] << 8);
		vmp.resize(cnt);
		for (int i = 0; i < cnt && (size_t)(3 + i * 2) < m.size(); i++)
			vmp[i] = m[2 + i * 2] | (m[3 + i * 2] << 8);
		return true;
	}

	lolvr::DungeonGfx gfx() const {
		lolvr::DungeonGfx g;
		g.vmpMap = vmpMap;
		g.wallFlags = wallFlags;
		g.vmp = vmp.data();
		g.vmpLen = (int)vmp.size();
		g.vcn = vcn;
		g.vcnShift = shift;
		g.vcnCount = vcnCount;
		g.colTable = col;
		g.palette = pal;
		return g;
	}
};

} // namespace lolfiles
