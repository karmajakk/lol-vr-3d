# Installation

[English](../Installation.md) · **Français** · [Deutsch](../de/Installation.md)

Rien à compiler.

## 1. Installer l'application

1. Télécharge `lol-vr-3d.apk` depuis la [dernière version](https://github.com/karmajakk/lol-vr-3d/releases/latest).
2. Branche le casque à ton PC, ouvre [SideQuest](https://sidequestvr.com) et installe l'APK (l'icône « Install APK file from folder »).
3. Dans le casque : Bibliothèque → filtre « Sources inconnues » → **Lands of Lore VR**.

Au premier lancement, une fenêtre te demande si tu veux la **vue classique** ou le **mode immersif**, et dans quelle langue (anglais, français ou allemand, sous leurs drapeaux) (tu pourras changer plus tard : disquette sur ton poignet gauche, puis « Réglages VR »), et un écran dit que les fichiers du jeu manquent : c'est normal. Ce lancement crée le dossier où ils vont.

## 2. Copier les fichiers du jeu

Copie **tout le contenu du CD du jeu** (les fichiers `.PAK`, dont `GENERAL.PAK`) dans ce dossier du casque :

`Android/data/com.karmajakk.lolvr3d/files/lol`

Utilise le gestionnaire de fichiers de SideQuest, ou l'Explorateur Windows une fois le casque branché et l'accès aux fichiers autorisé.

**Version GOG** : les fichiers `.PAK` sont rangés dans `GAME.DAT`, une image de CD. Ouvre `GAME.DAT` avec [7-Zip](https://www.7-zip.org) et copie le contenu de son dossier `DATA` (y compris le sous-dossier `ENG`).

Le jeu démarre tout seul dès que les fichiers sont en place.

Si tu utilisais l'ancienne application `lol-vr` : celle-ci s'installe à côté et a ses propres dossiers. Copie les fichiers du jeu une deuxième fois, et tes sauvegardes de `Android/data/com.karmajakk.lolvr/files/sauvegardes` si tu veux les garder.

## Langue

La langue choisie dans la première fenêtre est celle de l'application et du jeu (version CD, qui a les trois). Ensuite, l'application suit la langue choisie dans les options du jeu : disquette sur ton poignet gauche, « Game controls », « Language is ». Avant que le jeu ait démarré, elle prend la dernière langue vue, et au tout premier lancement celle du casque (anglais, français ou allemand).

## Mettre à jour

Installe simplement le nouvel APK **par-dessus** l'ancien : les fichiers du jeu et les sauvegardes sont gardés.

## Sauvegardes

Elles sont dans `Android/data/com.karmajakk.lolvr3d/files/sauvegardes`.

**Attention : désinstaller l'application efface ce dossier.** Copie-le sur ton PC avant de désinstaller.
