#ifndef MAINSCREEN_TAB5_LAYOUT_H
#define MAINSCREEN_TAB5_LAYOUT_H

/**
 * @brief Native 1280x720-Geometrie von MainScreen, Sperrbildschirm und Eckengeste ohne Skalierung.
 *
 * Regel wie beim Radio (RadioTab5Layout.h): Größen, Abstände, Strichstärken und Schriften 1,5-fach, halbe Pixel aufgerundet. Das Symbolraster bleibt oben rechts bündig, die Uhr sitzt links am Rand. Die 80 Pixel Zusatzbreite liegen zwischen Uhr und Symbolen. Logo und Schriftzug der Sperre bleiben in Originalgröße (es gibt keine größere Vorlage, Vergrößern würde unscharf), nur ihr Abstand ist nativ. Werte der Uhr sind relativ zur linken oberen Ecke des Uhrsprites, Werte der Tasten relativ zum Tastenfeld.
 */
namespace MainScreenTab5Layout
{
    constexpr int WIDTH = 1280; ///< Ansicht deckungsgleich mit dem Display, Original 800
    constexpr int HEIGHT = 720; ///< Original 480

    /** @brief Raster der quadratischen Symbole oben rechts, nur die erste Zeile ist belegt. */
    namespace Buttons
    {
        constexpr int EDGE_MARGIN = 33;                                  ///< Abstand zum oberen und rechten Rand, Original 22
        constexpr int SIZE = 150;                                        ///< Original 100
        constexpr int SPACING = 18;                                      ///< Original 12
        constexpr int COLS = 3;                                          ///< wie im Original
        constexpr int ROWS = 4;                                          ///< wie im Original, bestimmt die Uhrgröße
        constexpr int GRID_W = COLS * SIZE + (COLS - 1) * SPACING;       ///< 486, Original 324
        constexpr int GRID_H = ROWS * SIZE + (ROWS - 1) * SPACING;       ///< 654, Original 436
        constexpr int START_X = WIDTH - EDGE_MARGIN - GRID_W;            ///< 761, Original 454
        constexpr int START_Y = EDGE_MARGIN;                             ///< 33, Original 22
        constexpr int STEP_X = SIZE + SPACING;                           ///< 168, Original 112
        constexpr int RADIUS = 15;                                       ///< Ecken, Original 10
        constexpr int BORDER = 2;                                        ///< Rahmen, Original 1
        constexpr int BORDER_PRESSED = 3;                                ///< Rahmen beim Drücken, Original 2
        constexpr int ICON_INSET = 15;                                   ///< Original 10
        constexpr float ICON_SCALE = 1.5f;                               ///< Originalformen nativ neu gerechnet (IconShapes::draw)
        constexpr int ICON_SIZE = 120;                                   ///< Original 80
    }

    /** @brief Analoguhr mit Datumszeile in einem gemeinsamen Sprite links. */
    namespace Clock
    {
        constexpr int MARK_INSET = 15;                                                 ///< Abstand der Markierungen vom Uhrrand, Original 10
        constexpr int DATE_H = 54;                                                     ///< Höhe der Datumszeile, Original 36
        constexpr int DATE_OFFSET = 15;                                                ///< Abstand Uhr -> Datum, Original 10
        constexpr int DIAMETER = Buttons::GRID_H + MARK_INSET - DATE_OFFSET - DATE_H;  ///< 600, Original 400
        constexpr float RADIUS = DIAMETER / 2.0f;                                      ///< 300, Original 200
        constexpr int SPRITE_W = DIAMETER + 45;                                        ///< 645, Original 430 (Durchmesser + 30)
        constexpr int SPRITE_H = DIAMETER + DATE_OFFSET + DATE_H;                      ///< 669, Original 446
        constexpr int SPRITE_X = 18;                                                   ///< Original 12. Links am Rand, 98 Pixel bis zu den Symbolen
        constexpr int SPRITE_Y = Buttons::START_Y - MARK_INSET;                        ///< 18, Original 12
        constexpr int CENTER_X = SPRITE_W / 2;                                         ///< 322, Original 215
        constexpr int CENTER_Y = DIAMETER / 2;                                         ///< 300. Oberkante des Kreises bei 0, Original 200
        constexpr int DATE_Y = DIAMETER + DATE_OFFSET;                                 ///< 615. Oberkante der Datumszeile, Original 410
        constexpr float MINOR_MARK_W = 1.5f;                                           ///< Minutenstriche, Original 1
        constexpr float HOUR_MARK_W = 3.0f;                                            ///< Stundenstriche, Original 2
        constexpr float MAJOR_MARK_W = 4.5f;                                           ///< Viertelstunden sowie NTP und DST, Original 3
        constexpr float SECOND_TIP = 0.75f;                                            ///< Spitze des Sekundenzeigers, Original 0,5
    }

    /** @brief Akkuanzeige in Höhe der Datumszeile: liegendes Symbol mit Prozentzahl in der Datumsschrift, nur mit erkanntem Akku. Auf dem MainScreen rechtsbündig mit den Symbolen, auf dem Sperrbildschirm mittig. Ohne Vorbild im Original. */
    namespace Battery
    {
        constexpr int ICON_W = 82;                            ///< Breite des Symbols (BatteryIcon::Large)
        constexpr int ICON_H = 42;                            ///< Höhe des Symbols
        constexpr int GAP = 16;                               ///< Abstand Symbol -> Zahl
        constexpr int TEXT_W = 122;                           ///< Breite von "100 %" in ComfortaaBold45
        constexpr int W = ICON_W + GAP + TEXT_W;              ///< 220, Sprite für Symbol und Zahl
        constexpr int H = Clock::DATE_H;                      ///< 54, die Zahl steht auf der Grundlinie des Datums
        constexpr int X = WIDTH - Buttons::EDGE_MARGIN - W;   ///< 1027, rechte Kante 1247 wie die Symbole
        constexpr int Y = Clock::SPRITE_Y + Clock::DATE_Y;    ///< 633, Oberkante der Datumszeile
        constexpr int CAP_H = 35;                             ///< Höhe der Ziffern von ComfortaaBold45
        constexpr int DESCENT = 11;                           ///< Unterlänge von ComfortaaBold45
        constexpr int ICON_Y = H - DESCENT - CAP_H + (CAP_H - ICON_H) / 2; ///< 5, Symbol und Ziffern auf einer Mittellinie
        constexpr int LOCK_X = (WIDTH - W) / 2;               ///< 530, Sperrbildschirm mittig
    }

    /** @brief WLAN-Anzeige und gedrehte NTP/DST-Beschriftungen im Zifferblatt. */
    namespace Indicators
    {
        constexpr int SIGNAL_BAR_COUNT = 5;                         ///< wie im Original
        constexpr int SIGNAL_BAR_W = 6;                             ///< Original 4
        constexpr int SIGNAL_BAR_GAP = 6;                           ///< Original 4
        constexpr int SIGNAL_BAR_H[SIGNAL_BAR_COUNT] = {9, 17, 24, 32, 39}; ///< Original 6, 11, 16, 21, 26 (Stufe 5)
        constexpr float SIGNAL_BAR_RADIAL_POS = 0.36f;              ///< Mitte der Balken relativ zum Radius, wie im Original
        constexpr int SSID_OFFSET_Y = 9;                            ///< Abstand Balken -> SSID, Original 6
        constexpr int IP_OFFSET_Y = 20;                             ///< Abstand SSID-Oberkante -> IP, Original 13
        constexpr int SSID_MAX_W = 180;                             ///< Original 120
        constexpr int LABEL_W = 63;                                 ///< NTP/DST-Beschriftung, Original 42
        constexpr int LABEL_H = 27;                                 ///< Original 18
        constexpr float LABEL_RADIAL_POS = 0.68f;                   ///< wie im Original
    }

    /** @brief Sperrbildschirm mit Logo, Schriftzug und PIN-Tastenfeld. */
    namespace Lock
    {
        constexpr int LOGO_W = 450;                                           ///< Originalgröße (logo_450)
        constexpr int LOGO_H = 263;                                           ///< Originalgröße
        constexpr int MADEIN_W = 212;                                         ///< Originalgröße (made_in_germany)
        constexpr int MADEIN_H = 40;                                          ///< Originalgröße
        constexpr int LOGO_GAP = 44;                                          ///< Original 29
        constexpr int LOGO_X = (WIDTH - LOGO_W) / 2;                          ///< 415, mittig wie im Original
        constexpr int LOGO_Y = (HEIGHT - LOGO_H - LOGO_GAP - MADEIN_H) / 2;   ///< 186, beide Bilder zusammen senkrecht mittig
        constexpr int MADEIN_X = (WIDTH - MADEIN_W) / 2;                      ///< 534
        constexpr int MADEIN_Y = LOGO_Y + LOGO_H + LOGO_GAP;                  ///< 493
        constexpr int KEY_W = 98;                                             ///< Original 65
        constexpr int KEY_H = 105;                                            ///< Original 70
        constexpr int KEY_GAP = 9;                                            ///< Original 6
        constexpr int KEY_COLS = 2;                                           ///< wie im Original
        constexpr int KEY_ROWS = 6;                                           ///< wie im Original
        constexpr int KEY_RADIUS = 6;                                         ///< Original 4, eingerückte Rahmenlinien konzentrisch je Pixel um 1 kleiner
        constexpr int KEY_BORDER = 2;                                         ///< Original 1
        constexpr int PULSE_FRAME = 5;                                        ///< Pulsrahmen einschließlich Tastenrand, Original 3
        constexpr int KEYPAD_W = KEY_COLS * KEY_W + (KEY_COLS - 1) * KEY_GAP; ///< 205, Original 136
        constexpr int KEYPAD_H = KEY_ROWS * KEY_H + (KEY_ROWS - 1) * KEY_GAP; ///< 675, Original 450
        constexpr int KEYPAD_MARGIN = 23;                                     ///< Abstand zum oberen und rechten Rand, Original 15. Unten bleiben 22
        constexpr int KEYPAD_X = WIDTH - KEYPAD_MARGIN - KEYPAD_W;            ///< 1052, Original 649
        constexpr int KEYPAD_Y = KEYPAD_MARGIN;                               ///< 23, Original 15
    }

    /** @brief Diagonale Sperrgeste an den beiden linken Ecken des Displays. */
    namespace Corner
    {
        constexpr int START_ZONE = 225; ///< Startzonen an beiden linken Ecken, Original 150
        constexpr int CORNER = 48;      ///< Zielecken, Original 32
        constexpr int SWIPE = 75;       ///< Mindestweg je Achse, Original 50
    }
}

#endif
