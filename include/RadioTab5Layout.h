#ifndef RADIO_TAB5_LAYOUT_H
#define RADIO_TAB5_LAYOUT_H
#include "StationManager.h"

/**
 * @brief Native 1280x720-Geometrie des Radios ohne Skalierung.
 *
 * Abgeleitet aus dem Layout des Originals (800x480): Größen, Höhen und Schriften 1,5-fach (720/480), waagerechte Verteilung 1,6-fach (1280/800). Halbe Pixel werden aufgerundet. Visualizer, Uhren und Tasten liegen im rechts unten bündigen Bereich Visual. Der Visualizer rechnet dort mit seiner eigenen 1,5-fachen Geometrie (VisualizerGeometry).
 */
namespace RadioTab5Layout
{
    constexpr int WIDTH = 1280; ///< Breite der Radioszene, deckungsgleich mit dem Display
    constexpr int HEIGHT = 720; ///< Höhe der Radioszene

    /** @brief Senderskala, Trennlinien, Logo und Zeiger. */
    namespace Scale
    {
        constexpr int DIVIDER_HEIGHT = 5;     ///< Original 3
        constexpr int TOP_DIVIDER_Y = 0;      ///< Original 0
        constexpr int Y = 8;                  ///< Oberkante des Skalensprites, Original 5
        constexpr int HEIGHT = 450;           ///< Original 300
        constexpr int BOTTOM_DIVIDER_Y = 461; ///< Original 307
        constexpr int STATION_X0 = 16;        ///< X-Position des ersten Senders, Original 10
        constexpr int STATION_STEP_X = 16;    ///< Waagerechter Senderabstand, Original 10
        constexpr int STATION_Y0 = 153;       ///< Zeile des ersten Senders, Original 102
        constexpr int STATION_STEP_Y = 30;    ///< Zeilenabstand, Original 20
        constexpr int ROW_WRAP_Y = 450;       ///< Zeilen darüber beginnen oben neu, Original 300
        constexpr int ROW_RESTART_Y = 3;      ///< Erste Zeile nach dem Umbruch, Original 2
        constexpr int MARKER_X = -7;          ///< Markerversatz zur Senderposition, Original -4. Mittig unter dem Zeiger
        constexpr int MARKER_Y = 10;          ///< Original 5, dort 1,5 Pixel über der Versalmitte. Hier exakt mittig zu den Versalien (Zeile +6 bis +22)
        constexpr int MARKER_W = 22;          ///< Original 14, gerade Breite hält den Marker mittig unter dem 6 Pixel breiten Zeiger
        constexpr int MARKER_H = 9;           ///< Original 6
        constexpr int NAME_X = 24;            ///< Textversatz zur Senderposition, Original 16
        constexpr int NAME_Y = -1;            ///< Original 0. Akzente auf Großbuchstaben wie Ć heben die Oberlänge von ComfortaaBold21 auf 24, der Versatz hält die Versalien bei +6 bis +22
        constexpr int UNDERLINE_H = 2;        ///< Unterstreichung der Favoriten, Original 1
        constexpr int LOGO_X = 24;            ///< Original 15
        constexpr int LOGO_Y = 45;            ///< Original 30
        constexpr int LOGO_W = 135;           ///< Original 90
        constexpr int LOGO_H = 79;            ///< Original 53
        constexpr int NEEDLE_STRIP = 8;       ///< Wiederhergestellter Streifen je Zeigerposition, Original 6
        constexpr int NEEDLE_RED_X = 1;       ///< Versatz des roten Zeigers im Streifen, Original 1
        constexpr int NEEDLE_RED_W = 6;       ///< Breite des roten Zeigers, Original 4
        constexpr int NEEDLE_MAX_X = WIDTH - NEEDLE_STRIP; ///< Original 794
        constexpr float NEEDLE_SPEED = 360.0f; ///< Höchstgeschwindigkeit in px/s, ganze Skala in etwa 3 s. Bei 64 Bildern/s des Displays rückt der Zeiger so unter 6 px je Bild und überlappt sich von Bild zu Bild. Schneller springt er sichtbar. Das Original fährt gleichförmig mit 166,6 px/s (nativ wären das 266,6)
        constexpr float NEEDLE_ACCEL = 2400.0f; ///< Anfahren und Bremsen in px/s², weich statt gleichförmig wie im Original. Volle Fahrt nach 0,15 s
        constexpr int NEEDLE_SUBSTEPS = 16;    ///< Teilschritte je Pixel: Während der Fahrt mischen die Randspalten der Nadel Rot und Hintergrund nach dem Bruchteil der Lage
        constexpr int NEEDLE_STAGING_W = 64;   ///< Größte Breite des Streifens aus alter und neuer Nadelstelle, die in einem Schritt direkt in die Panelzeilen geht, ohne Panelkopie über einen Zwischenpuffer dieser Breite. Breitere Sprünge gehen aus der Szene
        constexpr int TOUCH_RIGHT = STATION_STEP_X * MAX_STATIONS; ///< Rechte Grenze der Senderwahl, Original 700
    }

    /** @brief Infozeile mit Codec-Feld, Sender, Titel, Bitrate und Stereo-Symbol. */
    namespace Footer
    {
        constexpr int Y = 474;               ///< Original 316
        constexpr int HEIGHT = 48;           ///< Original 32
        constexpr int LINE = 2;              ///< Rahmenlinie oben und unten, Original 1
        constexpr int RESERVED = 144;        ///< Für Codec-Feld und Stereo-Symbol freigehaltene Breite, je Seite die Hälfte, Original 90
        constexpr int STEREO_W = 62;         ///< Original 41
        constexpr int STEREO_H = 38;         ///< Original 25
        constexpr int STEREO_X = WIDTH - STEREO_W - 2; ///< Original 800-41-1
        constexpr int STEREO_Y = 479;        ///< Original 319
        constexpr int STEREO_LEFT_X = 20;    ///< Mittelpunkt linker Ring, Original 13
        constexpr int STEREO_RIGHT_X = 42;   ///< Mittelpunkt rechter Ring, Original 28
        constexpr int STEREO_CY = 18;        ///< Original 12
        constexpr float STEREO_R = 16.5f;    ///< Original 11
        constexpr float STEREO_THICKNESS = 3.0f; ///< Original 2
        constexpr int CODEC_X = 2;           ///< Spiegelbildlich zum Stereo-Symbol, Original 1
        constexpr int CODEC_Y = STEREO_Y;    ///< Original 319
        constexpr int CODEC_W = 65;          ///< Original 43
        constexpr int CODEC_H = STEREO_H;    ///< Original 25
        constexpr int CODEC_BADGE_H = 27;    ///< Höhe der Marke wie im SD-Player, Original 18
        constexpr int CODEC_BADGE_PAD = 12;  ///< Zusatzbreite zum Text, Original 8
    }

    /** @brief Magisches Auge oben rechts in der Skala. */
    namespace MagicEye
    {
        constexpr int SIZE = 105;              ///< Breite und Höhe des Sprites, Original 70
        constexpr int X = 1140;                ///< Linke Kante. Der Mittelpunkt liegt im gleichen Breitenverhältnis wie im Original, Original 710
        constexpr int Y = 30;                  ///< Original 20
        constexpr float RING_THICKNESS = 7.5f; ///< Außenring EM11/EM34, Original 5
        constexpr float EM80_HALF_W = 33.0f;   ///< Original 22
        constexpr float EM80_HALF_H = 43.5f;   ///< Original 29
        constexpr float EM84_HALF_W = 18.75f;  ///< Original 12,5
        constexpr float EM84_HALF_H = 51.0f;   ///< Original 34
    }

    /** @brief Lautstärkedrehknopf mit Beschriftung darüber. */
    namespace Knob
    {
        constexpr int X = 0;                    ///< Original 0
        constexpr int Y = 559;                  ///< Original 373, ungerade Kantenlänge hält den Mittelpunkt auf einem Pixel
        constexpr int SIZE = 161;               ///< Original 107
        constexpr int MID = SIZE / 2;           ///< Mittelpunkt im Sprite, Original 53
        constexpr int LABEL_H = 19;             ///< Beschriftungsstreifen über dem Knopf, Original 13. Beginnt wie die Reglerbeschriftung bei y 540
        constexpr float RING_R = MID - 1.5f;    ///< Original MID-1
        constexpr float RING_THICKNESS = 4.5f;  ///< Original 3
        constexpr int MARKER_OFFSET = 30;       ///< Abstand der Markerbahn vom Rand, Original 20
        constexpr float MARKER_R = 19.5f;       ///< Original 13
        constexpr float MARKER_THICKNESS = 4.5f; ///< Original 3
        constexpr int VALUE_Y = 24;             ///< Oberkante der Zahl über der Mitte, Original 15. Ziffernmitte wie im Original knapp unter der Knopfmitte (+3,5 statt +2,5 px)
        constexpr int MUTE_ICON = 42;           ///< Stummsymbol, Original 28
        constexpr float MUTE_ICON_SCALE = 1.5f; ///< Faktor für IconShapes::draw
        constexpr int TOUCH_MUTE_R = 24;        ///< Stummschaltung bis zu diesem Radius, Original 16
        constexpr int TOUCH_RING_R = 30;        ///< Drehen ab diesem Radius, Original 20
        constexpr int TOUCH_MARGIN = 30;        ///< Trefferrand um den Knopf, Original 20
    }

    /** @brief Schieberegler für Balance, Tiefen, Mitten und Höhen in einem Sprite. */
    namespace Tracks
    {
        constexpr int X = 233;             ///< Original 130. Mittig zwischen Drehknopf und Visualizerfläche, je 72 bzw. 73 px Abstand (Original 23 und 21)
        constexpr int Y = 540;             ///< Original 360
        constexpr int WIDTH = 285;         ///< Original 190
        constexpr int HEIGHT = 180;        ///< Original 120
        constexpr int SLIDER_X0 = 8;       ///< Linke Kante des ersten Reglers, Original 5
        constexpr int SLIDER_STEP = 75;    ///< Original 50
        constexpr int TOUCH_W = 47;        ///< Treffer- und Nulllinienbreite, Original 31
        constexpr int CENTER_X = 23;       ///< Mitte von Bahn, Nulllinie und Knopf im Regler, Original 15
        constexpr int GROOVE_W = 5;        ///< Original 3
        constexpr int GROOVE_Y = 3;        ///< Original 2
        constexpr int GROOVE_H = 174;      ///< Original 116
        constexpr int PIXEL_MIN = 156;     ///< Knopfmitte bei -12 dB bzw. Balance links, Original 104
        constexpr int PIXEL_ZERO = 96;     ///< Original 64
        constexpr int PIXEL_MAX = 36;      ///< Original 24
        constexpr float KNOB_R = 19.5f;    ///< Original 13
        constexpr float KNOB_THICKNESS = 4.5f; ///< Original 3
        constexpr float KNOB_FILL_R = 15.0f;   ///< Original 10
    }

    /** @brief Rechter unterer Bereich für Visualizer, Uhren und Tasten: Originalbereich 341..800 x 354..480 1,5-fach, rechts unten bündig. */
    namespace Visual
    {
        constexpr int W = 689;          ///< Original 459
        constexpr int H = 189;          ///< Original 126
        constexpr int X = WIDTH - W;    ///< 591, Original 341
        constexpr int Y = HEIGHT - H;   ///< 531, Original 354
    }

    /** @brief Fünf Bedientasten rechts unten, gleich breit mit gleichen Abständen, rechte Taste am Rand. */
    namespace Buttons
    {
        constexpr int Y = 653;                 ///< Original 435
        constexpr int H = HEIGHT - Y;          ///< 67, Original 45
        constexpr int W = 68;                  ///< Original 45, gilt auch für Reset (Original 67 mit Text)
        constexpr int GAP = 15;                ///< Original 10
        constexpr int RADIUS = 6;              ///< Eckenradius, Original 4
        constexpr int BORDER = 2;              ///< Rahmen, Original 1
        constexpr int BORDER_PRESSED = 3;      ///< Rahmen gedrückt, Original 2
        constexpr int X4 = WIDTH - W;          ///< 1212, Haus (Hauptmenü und Sleep-Timer)
        constexpr int X3 = X4 - GAP - W;       ///< 1129, Reset
        constexpr int X2 = X3 - GAP - W;       ///< 1046, Weiter
        constexpr int X1 = X2 - GAP - W;       ///< 963, Zurück
        constexpr int X0 = X1 - GAP - W;       ///< 880, Favoriten
    }

    /** @brief Uhren im unteren Bereich, Lage über die Originalkanten auf die Visualizerfläche abgebildet. */
    namespace Clock
    {
        constexpr int DIGITAL_X = Visual::X + 21;  ///< Original 355
        constexpr int DIGITAL_Y = Buttons::Y;      ///< Original 435
        constexpr int DIGITAL_W = 225;             ///< Original 150
        constexpr int DIGITAL_H = Buttons::H;      ///< Original 45
        constexpr int ANALOG_X = Visual::X + 570;  ///< Original 721, im Goniometermodus
        constexpr int ANALOG_Y = Visual::Y;        ///< Original 354
        constexpr int ANALOG_SIZE = 105;           ///< Original 70
    }

    /** @brief Akkusymbol hochkant in der Zeile der Buttons, nur mit erkanntem Akku. Ohne Vorbild im Original. */
    namespace Battery
    {
        constexpr int W = 27;                                   ///< Breite des Symbols (BatteryIcon::Upright)
        constexpr int H = Buttons::H;                           ///< 67, so hoch wie ein Button
        constexpr int X = Buttons::X0 - Buttons::GAP - W;       ///< 838, links neben dem Favoriten-Button im Abstand der Buttons untereinander
        constexpr int Y = Buttons::Y;                           ///< 653
        constexpr int CLOCK_INSET = Clock::DIGITAL_X + Clock::DIGITAL_W + 10 - X; ///< 9, mit Akku rücken Ring und Restzeit des Sleep-Timers im Uhrsprite so weit nach links, dass bis zum Symbol 10 frei bleiben wie ohne Akku bis zum ersten Button
        constexpr int GONIO_LEFT = Visual::X + 246;             ///< 837, rechte Kante der Pegelsäulen des Goniometers (Original 505)
        constexpr int GONIO_X = (GONIO_LEFT + Buttons::X0 - W) / 2; ///< 845, im Goniometermodus mittig zwischen Pegelsäulen und erstem Button des Radios, je 8 Abstand
        static_assert(Clock::DIGITAL_X + Clock::DIGITAL_W <= X && GONIO_X + W < Buttons::X0, "Akkusymbol muss zwischen Digitaluhr und erstem Button liegen");
    }

    /** @brief Dialoge 1,5-fach, mittig auf dem Display wie im Original. Die Innenmaße stehen nativ in den Dialogklassen. */
    namespace Dialog
    {
        constexpr int PRESETS_W = 900, PRESETS_H = 660; ///< Favoriten, Original 600x440 bei (100,20)
        constexpr int PRESETS_X = (WIDTH - PRESETS_W) / 2, PRESETS_Y = (HEIGHT - PRESETS_H) / 2;
        constexpr int MESSAGE_W = 450, MESSAGE_H = 300; ///< Reset-Rückfrage, Original 300x200 bei (250,140)
        constexpr int MESSAGE_X = (WIDTH - MESSAGE_W) / 2, MESSAGE_Y = (HEIGHT - MESSAGE_H) / 2;
        constexpr int SLEEP_W = 450, SLEEP_H = 450; ///< Schlaftimer, Original 300x300 bei (250,90)
        constexpr int SLEEP_X = (WIDTH - SLEEP_W) / 2, SLEEP_Y = (HEIGHT - SLEEP_H) / 2;
    }
}
#endif
