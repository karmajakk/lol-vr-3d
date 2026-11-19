# Décors et images

[English](../Scenery-and-pictures.md) · **Français** · [Deutsch](../de/Scenery-and-pictures.md)

En plus du mode immersif (le donjon en 3D seulement), « Réglages VR » propose deux lieux :

- **Classique** : une bibliothèque médiévale, une pièce fermée avec une cheminée, une fenêtre, des étagères, des tableaux, des bannières et des reliques sur des socles. La vue par la fenêtre, la lumière, un tableau, les bannières et l'ambiance changent avec la région du jeu.
- **Réalité mixte** : ta vraie pièce.

Dans les deux, trois vues, à changer d'un clic court sur le joystick gauche (L3) : le **grand écran** (le jeu dans son cadre doré), les **fenêtres** (l'interface répartie autour de la vue du jeu) et le **PC 486** (le jeu sur un écran bombé des années 90 posé sur un PC 486).

Tout est généré par le code : aucune image ni aucun son n'est livré avec l'application.

## Tes images dans la bibliothèque (facultatif)

Copie des images PNG ou JPEG dans le dossier :

`Android/data/com.karmajakk.lolvr3d/files/decor`

Le **nom** dit où va chaque image :

| Nom contenant | Où elle va |
|---|---|
| `fenetre` (ou `castle`) | la vue par la fenêtre |
| `portrait` (ou `king`) | le tableau au-dessus de la cheminée |
| `tableau1` | mur de gauche, à la place de la carte |
| `tableau2` | mur du fond, à gauche de la porte |
| `tableau3` | mur de gauche, à la place du paysage |
| `tableau4` | mur du fond, à droite de la porte (cadre ajouté seulement si l'image existe) |

Six images au plus. Une image avec un autre nom prend la première place libre. L'application les prend en compte quelques secondes après la copie, sans redémarrer.

Ces images ne sont pas fournies : comme les fichiers du jeu, chacun apporte les siennes.

## Son

Le son du jeu vient de l'écran : il reste en place quand tu tournes la tête. Il n'y a pas de son d'ambiance.
