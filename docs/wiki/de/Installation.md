# Installation

[English](../Installation.md) · [Français](../fr/Installation.md) · **Deutsch**

Nichts zu kompilieren.

## 1. Die App installieren

1. Lade `lol-vr-3d.apk` aus der [neuesten Version](https://github.com/karmajakk/lol-vr-3d/releases/latest) herunter.
2. Verbinde das Headset mit deinem PC, öffne [SideQuest](https://sidequestvr.com) und installiere die APK (das Symbol „Install APK file from folder“).
3. Im Headset: Bibliothek → Filter „Unbekannte Quellen“ → **Lands of Lore VR**.

Beim ersten Start fragt ein Fenster, ob du die **klassische Ansicht** oder den **immersiven Modus** möchtest, und in welcher Sprache (Englisch, Französisch oder Deutsch, unter ihren Flaggen) (du kannst das später ändern: Diskette an deinem linken Handgelenk, dann „VR-Optionen“), und ein Bildschirm meldet, dass die Spieldateien fehlen: Das ist normal. Dieser Start legt den Ordner an, in den sie gehören.

## 2. Die Spieldateien kopieren

Kopiere **den ganzen Inhalt der Spiel-CD** (die `.PAK`-Dateien, auch `GENERAL.PAK`) in diesen Ordner des Headsets:

`Android/data/com.karmajakk.lolvr3d/files/lol`

Nutze den Dateimanager von SideQuest oder den Windows-Explorer, sobald das Headset verbunden und der Dateizugriff erlaubt ist.

**GOG-Version**: Die `.PAK`-Dateien stecken in `GAME.DAT`, einem CD-Abbild. Öffne `GAME.DAT` mit [7-Zip](https://www.7-zip.org) und kopiere den Inhalt seines Ordners `DATA` (mit dem Unterordner `ENG`).

Das Spiel startet von selbst, sobald die Dateien da sind.

Falls du die ältere App `lol-vr` benutzt hast: Diese App wird daneben installiert und hat ihre eigenen Ordner. Kopiere die Spieldateien ein zweites Mal, und deine Spielstände aus `Android/data/com.karmajakk.lolvr/files/sauvegardes`, wenn du sie behalten willst.

## Sprache

Die im ersten Fenster gewählte Sprache gilt für die App und das Spiel (CD-Version, die alle drei hat). Danach folgt die App der Sprache, die in den Optionen des Spiels gewählt ist: Diskette an deinem linken Handgelenk, „Game controls“, „Language is“. Bevor das Spiel gestartet ist, nimmt sie die zuletzt gesehene Sprache, und beim allerersten Start die Sprache des Headsets (Englisch, Französisch oder Deutsch).

## Aktualisieren

Installiere einfach die neue APK **über** die alte: Spieldateien und Spielstände bleiben erhalten.

## Spielstände

Sie liegen in `Android/data/com.karmajakk.lolvr3d/files/sauvegardes`.

**Achtung: Wenn du die App deinstallierst, wird dieser Ordner gelöscht.** Kopiere ihn vor dem Deinstallieren auf deinen PC.
