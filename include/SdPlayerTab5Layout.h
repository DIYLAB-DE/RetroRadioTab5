#ifndef SD_PLAYER_TAB5_LAYOUT_H
#define SD_PLAYER_TAB5_LAYOUT_H
#include "RadioTab5Layout.h"

/**
 * @brief Native 1280x720-Geometrie des SD-Players und seines Dateidialogs ohne Skalierung.
 *
 * Regel wie beim Radio (RadioTab5Layout.h): Größen, Höhen und Schriften 1,5-fach, halbe Pixel aufgerundet. Der untere Bereich mit Drehknopf, Reglern, Visualizer, Uhren und Tasten liegt exakt wie beim Radio (RadioTab5Layout). Oben behalten Kassette und Cover 1,5-fach ihre Lage am linken Rand. Die zusätzliche Breite der nativen Fläche nimmt die Playlist auf.
 */
namespace SdPlayerTab5Layout
{
    namespace RL = RadioTab5Layout;
    constexpr int WIDTH = RL::WIDTH;   ///< Breite der Szene, deckungsgleich mit dem Display
    constexpr int HEIGHT = RL::HEIGHT; ///< Höhe der Szene

    /** @brief Trennlinien, dunkle Spielerfläche und Lücke zur Playlist. */
    namespace Frame
    {
        constexpr int DIVIDER_H = RL::Scale::DIVIDER_HEIGHT;     ///< Original 3
        constexpr int TOP_DIVIDER_Y = RL::Scale::TOP_DIVIDER_Y;  ///< Original 0
        constexpr int BOTTOM_DIVIDER_Y = RL::Scale::BOTTOM_DIVIDER_Y; ///< Original 307
        constexpr int PANEL_Y = RL::Scale::Y;                    ///< Original 5
        constexpr int PANEL_H = RL::Scale::HEIGHT;               ///< Original 300
        constexpr int PANEL_W = 812;                             ///< Original 540, endet bündig mit dem Cover
        constexpr int GAP_W = 15;                                ///< Schwarze Lücke vor der Playlist, Original 10
    }

    /** @brief Kassette mit Fortschritt und Transporttasten darunter. */
    namespace Cassette
    {
        constexpr int X = 16;   ///< Original 10. Rand 1,6-fach wie die Senderskala des Radios
        constexpr int Y = 23;   ///< Original 15
        constexpr int W = 428;  ///< Original 285
        constexpr int H = 276;  ///< Original 184
        constexpr int WRAP_SPRITE = 76;     ///< Naben-Sprite, Original 48. Fasst den größten umgerechneten Deckradius 36,3
        constexpr int LABEL_MARGIN = 9;     ///< Textrand im Etikettstreifen, Original 6
    }

    /** @brief Fortschrittsbalken mit Zeitanzeige. */
    namespace Progress
    {
        constexpr int X = Cassette::X;   ///< Original 10
        constexpr int Y = 317;           ///< Original 211
        constexpr int W = Cassette::W;   ///< Original 285, bündig mit Kassette und Tastenreihe
        constexpr int H = 36;            ///< Original 24
        constexpr int TOUCH_Y = 305;     ///< Trefferfläche, Original 203
        constexpr int TOUCH_H = 60;      ///< Original 40
        constexpr int BAR_LINE_Y = 33;   ///< Rote Linie am unteren Rand, Original 22
        constexpr int BAR_LINE_H = 3;    ///< Original 2
        constexpr int TEXT_Y = 18;       ///< Textmitte, Original 12
        constexpr int CURRENT_X = 138;   ///< Linke Kante der Laufzeit, Original 92
        constexpr int SLASH_X = 213;     ///< Mitte des Schrägstrichs, Original 142
        constexpr int END_X = 228;       ///< Linke Kante der Gesamtzeit, Original 152
    }

    /** @brief Fünf Transporttasten unter der Kassette. */
    namespace Transport
    {
        constexpr int Y = 375;           ///< Original 250
        constexpr int SIZE = 68;         ///< Original 45
        constexpr int GAP = 22;          ///< Original 15, 22 statt 22,5, damit die Reihe wie im Original genau die Kassettenbreite füllt
        constexpr int X0 = Cassette::X;  ///< Original 10
        constexpr int STEP = SIZE + GAP; ///< Original 60
        constexpr int RADIUS = RL::Buttons::RADIUS;             ///< Original 4
        constexpr int BORDER = RL::Buttons::BORDER;             ///< Original 1
        constexpr int BORDER_PRESSED = RL::Buttons::BORDER_PRESSED; ///< Original 2
    }

    /** @brief Cover und Infofeld darunter, bündig mit dem Ende der Spielerfläche. */
    namespace Cover
    {
        constexpr int W = 345;                    ///< Original 230
        constexpr int H = 330;                    ///< Original 220
        constexpr int X = Frame::PANEL_W - W;     ///< 467, Original 310 = 540-230
        constexpr int Y = Cassette::Y;            ///< Original 15
        constexpr int LINE = 2;                   ///< Seitenlinien, Original 1
        constexpr int INITIALS_XY = 23;           ///< Initialen, Original 15
        constexpr int TEXT_MARGIN = 23;           ///< Abstand der Textzeile vom Rand, Original 15
        constexpr int DETAILS_Y = Transport::Y;   ///< Infofeld, Original 250
        constexpr int DETAILS_H = Transport::SIZE; ///< Original 45
    }

    /** @brief Infofeld unter dem Cover mit Kapazität, Album, Titelnummer und Formatmarken. */
    namespace Details
    {
        constexpr int TEXT_X = 12;        ///< Original 8
        constexpr int LINE1_Y = 9;        ///< Original 6
        constexpr int LINE2_Y = 39;       ///< Original 26
        constexpr int TEXT_W = 321;       ///< Original 214
        constexpr int TEXT_W_PLAYING = 318; ///< Original 212
        constexpr int BADGE_Y = 35;       ///< Original 23
        constexpr int BADGE_H = 27;       ///< Original 18
        constexpr int BADGE_PAD = 12;     ///< Original 8
        constexpr int BADGE_GAP = 3;      ///< Original 2
        constexpr int BADGE_RIGHT = 8;    ///< Abstand zum rechten Rand, Original 5
        constexpr int TRACK_GAP = 8;      ///< Abstand der Titelnummer zu den Marken, Original 5
    }

    /** @brief Playlist rechts mit Scrollbalken am Displayrand. */
    namespace Playlist
    {
        constexpr int SCROLLBAR_W = 8;                                   ///< Original 5
        constexpr int X = Frame::PANEL_W + Frame::GAP_W;                 ///< 827, Original 550
        constexpr int Y = Frame::PANEL_Y;                                ///< Original 5
        constexpr int W = WIDTH - SCROLLBAR_W - X;                       ///< 445. Original 245, nimmt die zusätzliche Breite auf
        constexpr int H = Frame::PANEL_H;                                ///< Original 300
        constexpr int ROW_H = 41;                                        ///< Original 27
        constexpr int SCROLLBAR_X = WIDTH - SCROLLBAR_W;                 ///< Original 795
        constexpr int SCROLLBAR_MIN_H = 30;                              ///< Original 20
        constexpr int NUMBER_X = 15;                                     ///< Original 10
        constexpr int NAME_GAP = 23;                                     ///< Abstand nach "999.", Original 15
        constexpr int NAME_MARGIN = 15;                                  ///< Rechter Textrand, Original 10
        constexpr int OVERSCROLL = 75;                                   ///< Gedämpfter Überlauf beim Ziehen, Original 50
        constexpr int DRAG_THRESHOLD = 15;                               ///< Ab hier gilt der Kontakt als Ziehen, Original 10
        constexpr int EMPTY_ICON_SCALE = 6;                              ///< Ordnersymbol, Original 4
        constexpr int EMPTY_ICON_Y = 117;                                ///< Original 78
        constexpr int EMPTY_TITLE_Y = 252;                               ///< Original 168
        constexpr int EMPTY_HINT1_Y = 294;                               ///< Original 196
        constexpr int EMPTY_HINT2_Y = 324;                               ///< Original 216
        constexpr int EMPTY_HINT_MARGIN = 15;                            ///< Original 10
    }

    /** @brief Trefferfläche für Wisch- und Haltegesten auf dem Visualizer, ohne Uhren- und Tastenzeile. */
    namespace Visualizer
    {
        constexpr int TOUCH_H = 107; ///< Original 71
    }

    /** @brief Statuszeile wie die Infozeile des Radios. */
    namespace Footer
    {
        constexpr int Y = RL::Footer::Y;           ///< Original 316
        constexpr int HEIGHT = RL::Footer::HEIGHT; ///< Original 32
        constexpr int LINE = RL::Footer::LINE;     ///< Original 1
    }

    /** @brief Vier Symboltasten rechts unten im Raster der Radiotasten X1 bis X4, alle so breit wie das Haus. */
    namespace Buttons
    {
        constexpr int Y = RL::Buttons::Y;         ///< Original 435
        constexpr int H = RL::Buttons::H;         ///< Original 45
        constexpr int W = RL::Buttons::W;         ///< 68, Original 45 bis 75 je nach Text
        constexpr int POWER_X = RL::Buttons::X4;  ///< 1212, Original 755
        constexpr int RESET_X = RL::Buttons::X3;  ///< 1129, Original 678
        constexpr int CLEAR_X = RL::Buttons::X2;  ///< 1046, Original 598
        constexpr int BROWSE_X = RL::Buttons::X1; ///< 963, Original 513
    }

    /** @brief Akkusymbol hochkant links neben dem Ordner-Button im Abstand der Buttons untereinander, in jeder Anzeigeart an derselben Stelle. Nur mit erkanntem Akku, ohne Vorbild im Original. */
    namespace Battery
    {
        constexpr int X = Buttons::BROWSE_X - RL::Buttons::GAP - RL::Battery::W; ///< 921
        constexpr int Y = RL::Battery::Y;                                        ///< 653
    }

    /** @brief Dateidialog 1,5-fach mittig. Alle Innenmaße relativ zur linken oberen Ecke. */
    namespace Explorer
    {
        constexpr int W = 825;                    ///< Original 550
        constexpr int H = 525;                    ///< Original 350
        constexpr int X = (WIDTH - W + 1) / 2;    ///< 228, Original 125
        constexpr int Y = (HEIGHT - H + 1) / 2;   ///< 98, Original 65
        constexpr int BORDER = 2;                 ///< Original 1
        constexpr int RADIUS = 6;                 ///< Original 4
        constexpr int TEXT_X = 23;                ///< Titel, Pfad, Linien und Status, Original 15
        constexpr int TITLE_Y = 23;               ///< Original 15
        constexpr int PATH_Y = 53;                ///< Original 35
        constexpr int PATH_W = 780;               ///< Original 520
        constexpr int LINE_W = 780;               ///< Original 520
        constexpr int LINE_H = 2;                 ///< Original 1
        constexpr int LINE1_Y = 90;               ///< Original 60
        constexpr int LINE2_Y = 138;              ///< Original 92
        constexpr int HEADER_Y = 116;             ///< Mitte der Spaltenköpfe, Original 77
        constexpr int HEADER_NAME_X = 27;         ///< Original 18
        constexpr int HEADER_TYPE_X = 579;        ///< Original 386
        constexpr int HEADER_SIZE_X = 789;        ///< Original 526
        constexpr int LIST_X = 5;                 ///< Original 3
        constexpr int LIST_Y = 141;               ///< Original 94
        constexpr int LIST_WIDTH = 807;           ///< Original 538
        constexpr int LIST_HEIGHT = 287;          ///< Original 191
        constexpr int ROW_H = 47;                 ///< Original 31
        constexpr int ROW_TEXT_Y = 23;            ///< Original 15
        constexpr int ROW_LEFT = 23;              ///< Linker Rand der Zeile, Original 15
        constexpr int ICON_GAP = 36;              ///< Textversatz nach Ordnersymbol, Original 24
        constexpr int ICON_Y = 9;                 ///< Ordnersymbol, Original 6
        constexpr int ICON_UP_Y = 14;             ///< Symbol für den übergeordneten Ordner, Original 9
        constexpr float ICON_SCALE = 1.5f;        ///< Faktor für IconShapes::draw
        constexpr int TYPE_X = 575;               ///< Mitte der Typspalte in der Liste, Original 383
        constexpr int SIZE_RIGHT = 785;           ///< Rechte Kante der Größenspalte, Original 523
        constexpr int NAME_RESERVE = 75;          ///< Abstand des Namens vor der Typspalte, Original 50
        constexpr int SCROLLBAR_X = 813;          ///< Original 542
        constexpr int SCROLLBAR_W = 8;            ///< Original 5
        constexpr int SCROLLBAR_MIN_H = 30;       ///< Original 20
        constexpr int STATUS1_Y = 438;            ///< Original 292
        constexpr int STATUS2_Y = 465;            ///< Original 310
        constexpr int BUTTON_W = 135;             ///< Original 90
        constexpr int BUTTON_H = 68;              ///< Original 45
        constexpr int OK_CX = 578;                ///< Mitte "ok", Original 385
        constexpr int CANCEL_CX = 735;            ///< Mitte "abbruch", Original 490
        constexpr int BUTTON_CY = 470;            ///< Original 313
        constexpr int DRAG_THRESHOLD = 15;        ///< Original 10
        constexpr int OVERSCROLL = 120;           ///< Original 80
    }
}
#endif
