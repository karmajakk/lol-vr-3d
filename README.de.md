# Lands of Lore VR

[English](README.md) · [Français](README.fr.md) · **Deutsch**

*Lands of Lore: The Throne of Chaos* (Westwood, 1993) auf der Meta Quest 3, eigenständig, ohne PC.
Das Originalspiel läuft in [ScummVM](https://www.scummvm.org) und entscheidet alles (Kämpfe,
Gegenstände, Zauber, Spielstände). Zwei Arten zu spielen, jederzeit umschaltbar unter
„VR-Optionen“ im Spielmenü (die Diskette an deinem linken Handgelenk):

- **Immersiver Modus**: der Dungeon **in echtem 3D um dich herum** nachgebaut, in Originalgröße, aus
  dem Level, das das Spiel geladen hat. Nur eine Ansicht; der Spielbildschirm erscheint bei Bedarf
  (Läden, Gespräche, Karte, Menüs).
- **Klassische Ansicht**: das Spiel auf einem schwebenden Bildschirm in einer mittelalterlichen
  Bibliothek, in deinem eigenen Zimmer (Mixed Reality) oder auf einem alten 486er-PC, mit der
  Oberfläche in verschiebbaren Fenstern.

Beim ersten Start fragt ein Fenster, welche du möchtest.

Dieses Projekt enthält jetzt [lol-vr](https://github.com/karmajakk/lol-vr), in dem die klassische
Ansicht zuerst entwickelt wurde (seine Geschichte bleibt hier erhalten; sein Änderungsprotokoll
liegt in [docs/lol-vr-CHANGELOG.md](docs/lol-vr-CHANGELOG.md)). Dokumentation:
[docs/wiki/de](docs/wiki/de/Home.md).

**Die Spieldateien sind nicht enthalten.** Du brauchst deine eigene Kopie des Spiels (CD- oder
GOG-Version).

## Stand

Frühe Demo. Was funktioniert:

- Die Wände jedes Levels, aus deinen eigenen Spieldateien, in Originalgröße um dich herum, mit ihren
  Verzierungen und Türen, gezeichnet von der Spiel-Engine selbst.
- Boden und Decke, nachgebaut aus dem Hintergrundbild des Spiels.
- Die Schritte und Drehungen der Gruppe gleiten sanft; Türen, die sich öffnen oder schließen,
  aktualisieren die Wände.
- Alles aus lol-vr: der Spielbildschirm, die losgelösten Fenster, die Controller, die Tastatur, die
  Diskette (Spielmenü) an der linken Hand. Die Fenster sind für den 3D-Dungeon angeordnet: links die
  Zauberrolle mit der Nachrichtenzeile darunter; rechts die Figuren untereinander, daneben die
  Atlas-Spalte (Laterne, Geldbeutel) und unter dem Geldbeutel der Rast-Knopf, in einem einzigen
  Fenster; darunter das Inventar.
- Ladentüren und Durchgänge in andere Gegenden bleiben vor dir, wie eine Wand mit ihrem Bild: geh
  hinein, und der Spielbildschirm zeigt den Laden, oder die neue Gegend erscheint.
- Dunkle Orte bleiben dunkel, bis du die Laterne anzündest.
- „VR-Modus“ in den „VR-Optionen“ (Spielmenü): Immersiv, Klassisch (Bibliothek) oder Mixed Reality;
  in den letzten beiden wechselt L3 die Ansicht: Großbild, Fenster, 486er-PC.
- Gespräche: das große Textfeld des Spiels, mit dem Porträt des Sprechenden, erscheint unter dem
  Spielbildschirm.
- Monster und Gegenstände nahe der Gruppe, vom Spiel selbst gezeichnet und immer dir zugewandt.
- Klicks im 3D-Dungeon: auf die Wände um die Gruppe (Türknöpfe, Hebel, Schlüssellöcher, Büsche,
  Nester) und auf den Boden vor dir (Gegenstände).
- Zaubereffekte werden in 3D gezeigt, so wie das Spiel sie zeichnet. Der Spielbildschirm erscheint
  von selbst für Szenen, Dialoge, die Karte und Menüs.

Noch nicht da: Türen, die in 3D aufgleiten (sie springen von zu auf offen), geworfene Gegenstände
im Flug.

Versionsdetails: [CHANGELOG.md](CHANGELOG.md) (auf Englisch).

## Installation (nichts zu kompilieren)

1. Lade `lol-vr-3d.apk` aus der
   [neuesten Version](https://github.com/karmajakk/lol-vr-3d/releases/latest) herunter.
2. Verbinde das Headset mit deinem PC, öffne [SideQuest](https://sidequestvr.com) und installiere
   die APK.
3. Starte im Headset **Lands of Lore VR 3D** (Bibliothek → Filter „Unbekannte Quellen“) und wähle
   die klassische Ansicht oder den immersiven Modus.
   Ein Bildschirm meldet, dass die Spieldateien fehlen: Das ist normal. Dieser erste Start legt den
   Ordner an, in den sie gehören.
4. Kopiere am PC die Spieldateien (die `.PAK`-Dateien, auch `GENERAL.PAK`, und den Ordner `ENG`)
   in diesen Ordner des Headsets:

   `Android/data/com.karmajakk.lolvr3d/files/lol`

   Das ist ein anderer Ordner als der von lol-vr: Kopiere dieselben Dateien ein zweites Mal.
   **GOG-Version:** Öffne `GAME.DAT` mit [7-Zip](https://www.7-zip.org) und kopiere den Inhalt
   seines Ordners `DATA`.
5. Das Spiel startet von selbst, sobald die Dateien da sind.

Die Spielstände liegen in `Android/data/com.karmajakk.lolvr3d/files/sauvegardes`. Du kannst deine
Spielstände aus lol-vr dorthin kopieren.

## Steuerung

Wie in lol-vr: zielen und den Abzug drücken, um zu klicken, X (oder der Griff) für einen
Rechtsklick (Trank trinken, Gegenstand benutzen), linker Stick zum Gehen, rechter Stick zum Drehen,
A zum Angreifen, B für Magie, Y für die Karte. Schau auf deine linke Hand und klicke auf die
Diskette für das Spielmenü; dort findest du „VR-Optionen“, die Einstellungen des Headsets (VR-Modus,
Spielbildschirm „bei Bedarf“ oder „immer“ im immersiven Modus, die Ansicht in Klassisch und Mixed
Reality, Steuerung und Tasten). Die App spricht die Sprache, die in den Optionen des Spiels gewählt
ist (Englisch, Französisch oder Deutsch). Die Tastatur erscheint von selbst, um einen Spielstand zu
benennen.

Im Dungeon ist die Position der Gruppe unter deinen Füßen. Du gehst dorthin, wohin du schaust: Der
linke Stick richtet sich nach deinem Blick (vorwärts ist, wohin du schaust, seitwärts ist ein
Seitenschritt), und wenn du stehst, dreht sich die Gruppe ruhig in deine Blickrichtung. Drehen mit
dem rechten Stick geschieht sofort, mit einer sehr kurzen Abblendung, damit es angenehm bleibt.

## Wie es funktioniert (für Neugierige)

Die Engine ist angepasst (`patches/0003-niveau-pour-le-donjon-3d.patch`), um das aktuelle Level zu
veröffentlichen: den Wandtyp jeder Seite der 32 x 32 Felder sowie Feld und Richtung der Gruppe.
Wenn ein Level lädt, zeichnet die Engine außerdem jeden Wandtyp so, wie sie eine Wand direkt vor der
Gruppe zeigt (128 x 96 Pixel, Verzierungen und Türen inklusive); diese Bilder kleiden die 3D-Wände.
Boden und Decke stammen aus dem Hintergrundbild des Spiels.
Hinweise zu den Formaten: [KForestland/lands-of-lore-1-re](https://github.com/KForestland/lands-of-lore-1-re)
und der Quellcode von ScummVM (`engines/kyra/engine/scene_rpg.cpp`).

## Bauen

Jeder Push wird von GitHub Actions gebaut (`.github/workflows/build.yml`, `scripts/build.sh`). Eine
Version wird veröffentlicht, wenn die Datei `VERSION` eine Nummer ohne veröffentlichte Version
enthält.

## Lizenz

GPL-3.0, wie ScummVM (siehe `COPYING`).
