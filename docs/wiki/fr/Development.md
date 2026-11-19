# Développement

[English](../Development.md) · **Français** · [Deutsch](../de/Development.md)

## Organisation du code

- `app/` : l'application Quest (C++, OpenXR, OpenGL ES). `core_host.cpp` fait tourner ScummVM ; `main_xr.cpp` gère le casque, les manettes, les calques et les vibrations.
- `app/src/scene.cpp`, `room.cpp`, `props.cpp`, `proctex.cpp`, `ambience.cpp`, `ui.cpp` : le rendu, la pièce et les objets, les textures générées, le son dans l'espace, les fenêtres, le clavier, la disquette du poignet et les réglages VR.
- `app/src/dungeon.cpp`, `dungeon_data.cpp` : le donjon en 3D du mode immersif (textures tirées des dessins du moteur, maillage, sprites).
- `app/test/` : des bancs d'essai qui tournent sur PC, sans casque.
- `core/` et `patches/` : réglages et modifications de ScummVM (cœur libretro limité à Lands of Lore). La première modification ajoute le pointeur absolu de la VR ; la deuxième donne à l'application la zone du jeu, l'écran en cours (fiche de personnage, menus) et les événements (coups, sons) ; la troisième publie le niveau, les dessins du moteur pour les murs, les portes, les monstres et les objets, la lumière pour le donjon en 3D, et ajoute « Réglages VR » au menu du jeu.
- `scripts/build.sh` : construit l'APK.
- `keystore/debug.keystore` : une clé de signature de test, publique, qui sert seulement à installer les mises à jour par-dessus la version précédente.

Les commentaires du code sont en français.

## Compilation et versions

Chaque envoi est compilé automatiquement par GitHub Actions (`.github/workflows/build.yml`).

Pour **publier une version** :

1. Ajoute une section `## [X.Y.Z]` en haut de `CHANGELOG.md`.
2. Mets le même numéro dans le fichier `VERSION`.
3. Envoie sur la branche `main` : la compilation publie la version `vX.Y.Z` avec l'APK et les notes du journal.

Numérotation (versionnage sémantique) : le premier chiffre change pour une grande étape, le deuxième pour une nouvelle fonction, le troisième pour une correction. Tant que le numéro commence par 0, le projet est en développement.

## Licence

GPL version 3 ou ultérieure, comme ScummVM (voir `COPYING`). *Lands of Lore* est une marque de ses propriétaires respectifs ; ce projet n'est affilié ni à Westwood, ni à Electronic Arts, ni à l'équipe de ScummVM.
