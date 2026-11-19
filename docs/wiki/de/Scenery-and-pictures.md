# Umgebungen und Bilder

[English](../Scenery-and-pictures.md) · [Français](../fr/Scenery-and-pictures.md) · **Deutsch**

Neben dem immersiven Modus (nur der 3D-Dungeon) bieten die „VR-Optionen“ zwei Orte:

- **Klassisch**: eine mittelalterliche Bibliothek, ein geschlossener Raum mit Kamin, Fenster, Regalen, Gemälden, Bannern und Reliquien auf Sockeln. Der Blick aus dem Fenster, das Licht, ein Gemälde, die Banner und die Stimmung wechseln mit der Gegend im Spiel.
- **Mixed Reality**: dein echtes Zimmer.

In beiden gibt es drei Ansichten, umschaltbar mit einem kurzen Klick auf den linken Stick (L3): das **Großbild** (das Spiel in seinem goldenen Rahmen), die **Fenster** (die Oberfläche um die Spielansicht verteilt) und den **486er-PC** (das Spiel auf einem gewölbten 90er-Jahre-Bildschirm auf einem 486er-PC).

Alles wird vom Code erzeugt: Mit der App wird keine Bild- oder Tondatei ausgeliefert.

## Deine Bilder in der Bibliothek (optional)

Kopiere PNG- oder JPEG-Bilder in den Ordner:

`Android/data/com.karmajakk.lolvr3d/files/decor`

Der **Name** bestimmt, wohin ein Bild kommt:

| Name enthält | Wohin es kommt |
|---|---|
| `fenetre` (oder `castle`) | der Blick aus dem Fenster |
| `portrait` (oder `king`) | das Gemälde über dem Kamin |
| `tableau1` | linke Wand, anstelle der Karte |
| `tableau2` | hintere Wand, links von der Tür |
| `tableau3` | linke Wand, anstelle der Landschaft |
| `tableau4` | hintere Wand, rechts von der Tür (Rahmen nur, wenn das Bild existiert) |

Höchstens sechs Bilder. Ein Bild mit einem anderen Namen nimmt den ersten freien Platz. Die App übernimmt sie wenige Sekunden nach dem Kopieren, ohne Neustart.

Diese Bilder werden nicht mitgeliefert: Wie die Spieldateien bringt jeder seine eigenen mit.

## Ton

Der Ton des Spiels kommt vom Bildschirm: Er bleibt an seinem Platz, wenn du den Kopf drehst. Es gibt keinen Umgebungston.
