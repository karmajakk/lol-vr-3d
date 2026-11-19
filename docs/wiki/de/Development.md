# Entwicklung

[English](../Development.md) · [Français](../fr/Development.md) · **Deutsch**

## Aufbau des Codes

- `app/`: die Quest-App (C++, OpenXR, OpenGL ES). `core_host.cpp` lässt ScummVM laufen; `main_xr.cpp` kümmert sich um Headset, Controller, Ebenen und Vibration.
- `app/src/scene.cpp`, `room.cpp`, `props.cpp`, `proctex.cpp`, `ambience.cpp`, `ui.cpp`: Darstellung, Raum und Gegenstände, erzeugte Texturen, Raumklang, Fenster, Tastatur, Diskette am Handgelenk und VR-Optionen.
- `app/src/dungeon.cpp`, `dungeon_data.cpp`: der 3D-Dungeon des immersiven Modus (Texturen aus den Zeichnungen der Engine, Gitter, Sprites).
- `app/test/`: Prüfstände, die am PC ohne Headset laufen.
- `core/` und `patches/`: Einstellungen und Änderungen an ScummVM (libretro-Kern nur für Lands of Lore). Die erste Änderung fügt den absoluten VR-Zeiger hinzu; die zweite meldet der App den Spielbereich, den aktuellen Bildschirm (Figurenbogen, Menüs) und Ereignisse (Treffer, Töne); die dritte veröffentlicht das Level, die Zeichnungen der Engine für Wände, Türen, Monster und Gegenstände, das Licht für den 3D-Dungeon, und fügt „VR-Optionen“ ins Spielmenü ein.
- `scripts/build.sh`: baut die APK.
- `keystore/debug.keystore`: ein öffentlicher Test-Signaturschlüssel, nur damit Aktualisierungen über die vorige Version installiert werden.

Die Kommentare im Code sind auf Französisch.

## Bauen und Versionen

Jeder Push wird automatisch von GitHub Actions gebaut (`.github/workflows/build.yml`).

Um **eine Version zu veröffentlichen**:

1. Füge oben in `CHANGELOG.md` einen Abschnitt `## [X.Y.Z]` hinzu.
2. Schreibe dieselbe Nummer in die Datei `VERSION`.
3. Pushe auf den Zweig `main`: Der Build veröffentlicht die Version `vX.Y.Z` mit der APK und den Notizen aus dem Änderungsprotokoll.

Nummerierung (semantische Versionierung): Die erste Ziffer ändert sich für einen großen Meilenstein, die zweite für eine neue Funktion, die dritte für eine Korrektur. Solange die Nummer mit 0 beginnt, ist das Projekt in Entwicklung.

## Lizenz

GPL Version 3 oder später, wie ScummVM (siehe `COPYING`). *Lands of Lore* ist eine Marke ihrer jeweiligen Eigentümer; dieses Projekt steht in keiner Verbindung zu Westwood, Electronic Arts oder dem ScummVM-Team.
