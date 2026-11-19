# Dépannage

[English](../Troubleshooting.md) · **Français** · [Deutsch](../de/Troubleshooting.md)

**L'écran dit que les fichiers du jeu manquent.**
Normal au premier lancement. Copie le contenu du CD dans `Android/data/com.karmajakk.lolvr3d/files/lol` (voir [Installation](Installation.md)). Avec la version GOG, ouvre d'abord `GAME.DAT` avec 7-Zip.

**L'application n'est pas dans la bonne langue.**
Elle suit le jeu : disquette sur ton poignet gauche, « Game controls », puis « Language is ».

**Je ne me souviens plus des boutons.**
Regarde ta main gauche, clique sur la disquette, puis « Réglages VR » et « Manettes et boutons ». Voir aussi [Commandes](Controls.md).

**Mes images (fenêtre, portrait) ne s'affichent pas.**
Vérifie que les fichiers sont dans `Android/data/com.karmajakk.lolvr3d/files/decor`, en PNG ou en JPEG (pas en WebP ni en HEIC). Voir [Décors et images](Scenery-and-pictures.md).

**Les fenêtres ou l'écran sont mal placés.**
Clique sur le joystick droit pour recentrer, ou « Tout ranger » sur le clavier.

**Un reflet ou une lumière parasite sur l'image du jeu.**
Signale-le dans les [Issues](https://github.com/karmajakk/lol-vr-3d/issues), en disant où sur l'écran.

**Le pas de côté est difficile.**
Le joystick gauche donne une direction à la fois, celle vers laquelle il penche le plus ; pousse-le franchement sur le côté plutôt qu'en diagonale.

**J'ai perdu mes sauvegardes.**
Désinstaller l'application efface le dossier `sauvegardes`. Pour mettre à jour, installe l'APK par-dessus l'ancien sans désinstaller.

**Autre chose.**
Ouvre une [Issue](https://github.com/karmajakk/lol-vr-3d/issues) avec la version (voir la page des [Releases](https://github.com/karmajakk/lol-vr-3d/releases)) et ce que tu faisais.
