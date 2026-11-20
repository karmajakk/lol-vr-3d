# Lands of Lore VR

[English](README.md) · **Français** · [Deutsch](README.de.md)

*Lands of Lore: The Throne of Chaos* (Westwood, 1993) sur Meta Quest 3, en autonome, sans PC.
Le jeu original tourne sur [ScummVM](https://www.scummvm.org) et décide de tout (combats,
objets, sorts, sauvegardes). Deux façons de jouer, à changer quand on veut dans « Réglages VR », dans
le menu du jeu (la disquette sur ton poignet gauche) :

- **Mode immersif** : le donjon reconstruit **en vraie 3D autour de toi**, à taille réelle, à partir
  du niveau chargé par le jeu. Une seule vue ; l'écran du jeu apparaît au besoin (boutiques,
  conversations, carte, menus).
- **Vue classique** : le jeu sur un écran flottant dans une bibliothèque médiévale, dans ta propre
  pièce (réalité mixte) ou sur un vieux PC 486, avec l'interface dans des fenêtres déplaçables.

Au premier lancement, une fenêtre te demande laquelle tu veux, et ta langue (anglais, français ou allemand).

Ce projet inclut maintenant [lol-vr](https://github.com/karmajakk/lol-vr), où la vue classique a
d'abord été développée (son historique est conservé ici ; son journal des versions est dans
[docs/lol-vr-CHANGELOG.md](docs/lol-vr-CHANGELOG.md)). Documentation :
[docs/wiki/fr](docs/wiki/fr/Home.md).

**Les fichiers du jeu ne sont pas fournis.** Il te faut ta propre copie du jeu (version CD ou GOG).

## État

Démo en développement. Ce qui fonctionne :

- Les murs de chaque niveau, tirés de tes propres fichiers du jeu, à taille réelle autour de toi,
  avec leurs décorations et leurs portes dessinées par le moteur du jeu lui-même.
- Le sol et le plafond, reconstruits à partir du fond d'image du jeu.
- Les déplacements et les rotations du groupe glissent en douceur ; les portes qui s'ouvrent ou se
  ferment mettent les murs à jour.
- Tout ce que faisait lol-vr : l'écran du jeu, les fenêtres détachées, les manettes, le clavier, la
  disquette du poignet gauche (menu du jeu). Les fenêtres sont disposées pour le donjon en 3D : à
  gauche le parchemin des sorts avec la ligne de messages collée dessous ; à droite les personnages
  l'un sous l'autre, la colonne de l'atlas (lanterne, bourse) à côté et le bouton du repos sous la
  bourse, dans une seule fenêtre ; l'inventaire en dessous.
- Les portes des boutiques et les passages vers d'autres régions restent devant toi, comme un mur
  avec leur image : avance dedans et l'écran du jeu montre la boutique, ou la nouvelle région
  apparaît.
- Les endroits sombres restent sombres tant que tu n'as pas allumé la lanterne.
- « Mode VR » dans « Réglages VR » (menu du jeu) : Immersif, Classique (bibliothèque) ou Réalité
  mixte ; dans les deux derniers, L3 change la vue : grand écran, fenêtres, PC 486.
- Les conversations et les remarques : la boîte de texte du jeu, avec le portrait de celui qui
  parle, apparaît dans sa propre fenêtre juste sous le cadre de l'écran, qui garde toute la vue.
- Les monstres et les objets près du groupe, dessinés par le jeu lui-même et toujours tournés vers
  toi.
- Les clics dans le donjon en 3D : sur les murs autour du groupe (boutons de porte, leviers,
  serrures, buissons, nids) et sur le sol devant toi (objets).
- Les effets des sorts sont montrés en 3D, comme le jeu les dessine. L'écran du jeu apparaît tout
  seul pour les scènes, les dialogues, la carte et les menus.

- Les portes montent dans leur cadre par étapes, comme dans le jeu ; les objets lancés et les
  projectiles traversent le donjon en 3D à la hauteur que le jeu leur donne.

Détail des versions : [CHANGELOG.md](CHANGELOG.md) (en anglais).

## Installation (rien à compiler)

1. Télécharge `lol-vr-3d.apk` depuis la
   [dernière version](https://github.com/karmajakk/lol-vr-3d/releases/latest).
2. Branche le casque à ton PC, ouvre [SideQuest](https://sidequestvr.com) et installe l'APK.
3. Dans le casque, lance **Lands of Lore VR 3D** (Bibliothèque → filtre « Sources inconnues ») et
   choisis la vue classique ou le mode immersif.
   Un écran dit que les fichiers du jeu manquent : c'est normal. Ce premier lancement crée le dossier
   où ils vont.
4. Sur le PC, copie les fichiers du jeu (les fichiers `.PAK`, dont `GENERAL.PAK`, et le dossier
   `ENG`) dans ce dossier du casque :

   `Android/data/com.karmajakk.lolvr3d/files/lol`

   C'est un autre dossier que celui de lol-vr : copie les mêmes fichiers une deuxième fois.
   **Version GOG :** ouvre `GAME.DAT` avec [7-Zip](https://www.7-zip.org) et copie le contenu de son
   dossier `DATA`.
5. Le jeu démarre tout seul dès que les fichiers sont en place.

Les sauvegardes sont dans `Android/data/com.karmajakk.lolvr3d/files/sauvegardes`. Tu peux y copier
tes sauvegardes de lol-vr.

## Commandes

Comme dans lol-vr : vise et appuie sur la gâchette pour cliquer, X (ou la poignée) pour un clic
droit (boire une potion, utiliser un objet), joystick gauche pour marcher, joystick droit pour
tourner, A pour attaquer, B pour la magie, Y pour la carte. Regarde ta main gauche et clique sur la
disquette pour le menu du jeu ; on y trouve « Réglages VR », les réglages du casque (mode VR,
la vue en Classique et en Réalité mixte, manettes et boutons). L'application parle la langue choisie dans les options du jeu (anglais,
français ou allemand). Le clavier apparaît tout seul pour nommer une sauvegarde.

Dans le donjon, la position du groupe est sous tes pieds. Tu marches là où tu regardes : le joystick
gauche s'oriente par rapport à ton regard (devant, c'est là où tu regardes ; de côté, c'est un pas
de côté), et quand tu ne bouges pas, le groupe se tourne doucement vers ton regard. Tourner avec le
joystick droit est instantané, avec un très court fondu, pour rester confortable.

## Comment ça marche (pour les curieux)

Le moteur est modifié (`patches/0003-niveau-pour-le-donjon-3d.patch`) pour publier le niveau en
cours : le type de mur de chaque côté des 32 x 32 cases, la case et la direction du groupe. Quand un
niveau se charge, le moteur dessine aussi chaque type de mur comme il montre un mur juste devant le
groupe (128 x 96 pixels, décorations et portes comprises) ; ces images habillent les murs en 3D. Le
sol et le plafond viennent de l'image de fond du jeu.
Notes sur les formats : [KForestland/lands-of-lore-1-re](https://github.com/KForestland/lands-of-lore-1-re)
et les sources de ScummVM (`engines/kyra/engine/scene_rpg.cpp`).

## Compilation

Chaque envoi est compilé par GitHub Actions (`.github/workflows/build.yml`, `scripts/build.sh`). Une
version est publiée quand le fichier `VERSION` contient un numéro sans version publiée. Pour
tester sur un PC Linux sans le casque, voir [scripts/dev](scripts/dev/README.md) (en anglais).

## Licence

GPL-3.0, comme ScummVM (voir `COPYING`).
