// lol-vr — textures procédurales du décor (calculées au démarrage, aucun fichier d'image).
#pragma once

#include <stdint.h>

namespace lolvr {

// Couches du tableau de textures. Le canal alpha indique à quel point la couleur du sommet
// teinte la texture (255 = entièrement teintée, 0 = couleur propre, par ex. les dorures).
enum TexLayer {
	kTexStone = 0,  // mur en pierre de taille
	kTexFloor,      // plancher
	kTexWood,       // bois sombre (meubles, poutres)
	kTexPanel,      // lambris à caissons
	kTexBooks,      // dos de livre (teinté par sommet)
	kTexRug,        // tapis
	kTexPaper,      // livre ouvert / parchemin
	kTexMetal,      // fer
	kTexSky,        // vue par la fenêtre (dépend du thème)
	kTexPaintTheme, // tableau de la région (dépend du thème)
	kTexPaintCastle,
	kTexPaintMap,
	kTexBanner,     // bannière (teintée par sommet)
	kTexLeather,    // cuir (teinté par sommet)
	kTexGold,       // dorure sculptée
	kTexWhite,      // blanc uni (objets de couleur unie)
	kTexLattice,    // vitrail au plomb (alpha = opacité)
	kTexProcCount,  // fin des couches calculées
	// Images fournies par le joueur (dossier « decor ») : chacune occupe deux couches,
	// moitié gauche et moitié droite, pour garder toute sa finesse.
	kTexImgWindowA = kTexProcCount,
	kTexImgWindowB,
	kTexImgPortraitA,
	kTexImgPortraitB,
	kTexImgPainting1A,
	kTexImgPainting1B,
	kTexImgPainting2A,
	kTexImgPainting2B,
	kTexImgPainting3A,
	kTexImgPainting3B,
	kTexImgPainting4A,
	kTexImgPainting4B,
	kTexCount
};

// Marge (en fraction de la couche) autour de chaque moitié d'image, contre les bavures de filtrage.
const float kImgMargin = 8.0f / 512.0f;

const int kTexSize = 512;

// Zones du jeu : chacune change la vue par la fenêtre, la lumière, un tableau et l'ambiance sonore.
enum Theme {
	kThemeKeep = 0, // château de Gladstone, manoir (par défaut)
	kThemeForest,   // forêts
	kThemeCave,     // cavernes du Draracle
	kThemeSwamp,    // marais de Gorkha
	kThemeMine,     // mines d'Urbish
	kThemeTower,    // Tour Blanche
	kThemeCity,     // cité d'Yvel
	kThemeRuin,     // ruines
	kThemeCimmeria, // château Cimmeria
	kThemeLava,     // cavernes des passerelles
	kThemeCount
};

struct ThemeInfo {
	const char *name;
	float ambient[3];     // lumière ambiante de la pièce (linéaire)
	float windowLight[3]; // lumière entrant par la fenêtre (linéaire)
	float fireTint[3];    // teinte du feu
	float banner[3];      // couleur des bannières (sRGB)
	int particles;        // 0 poussière, 1 lucioles, 2 braises
	// Ambiance sonore (0..1)
	float wind, drips, crickets, rumble;
};

const ThemeInfo &themeInfo(int theme);

// Trouve le thème à partir du nom du jeu de tuiles (« FOREST1 », « CAVE1 »…) ou du numéro de niveau.
int themeFromGame(const char *tileset, int level);

bool layerDependsOnTheme(int layer);

// Écrit kTexSize x kTexSize pixels RGBA (couleurs sRGB) dans `rgba`.
void generateTexture(int layer, int theme, uint8_t *rgba);

} // namespace lolvr
