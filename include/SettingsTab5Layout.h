#ifndef SETTINGS_TAB5_LAYOUT_H
#define SETTINGS_TAB5_LAYOUT_H

/**
 * @brief Native 1280x720-Geometrie der Einstellungen und ihrer Dialoge ohne Skalierung.
 *
 * Regel wie beim Radio (RadioTab5Layout.h): Größen, Höhen, Abstände und Schriften 1,5-fach, halbe Pixel aufgerundet. Das Menü verteilt sich über die volle Höhe und nutzt Comfortaa Bold 27, seine Breite folgt dem längsten Eintrag, die übrige Breite nimmt der Arbeitsbereich auf. Dialoge liegen 1,5-fach mittig wie im Original. Werte der Seiten sind relativ zur linken oberen Ecke des Arbeitsbereichs, Werte der Dialoge relativ zu deren linker oberer Ecke.
 */
namespace SettingsTab5Layout
{
    constexpr int WIDTH = 1280; ///< Szene deckungsgleich mit dem Display, Original 800
    constexpr int HEIGHT = 720; ///< Original 480

    /** @brief Gemeinsame Rahmen, Radien und Linien von Zeilen, Feldern und Tasten. */
    namespace Style
    {
        constexpr int BORDER = 2;           ///< Rahmen von Feldern, Kacheln und Tasten, Original 1
        constexpr int BORDER_PRESSED = 3;   ///< Rahmen gedrückter Tasten, Original 2
        constexpr int LINE = 2;             ///< Trennlinien, Original 1
        constexpr int FIELD_R = 12;         ///< Zeilen, Felder und Kacheln, Original 8
        constexpr int FIELD_INNER_R = 11;   ///< Original 7
        constexpr int BUTTON_R = 9;         ///< Tasten, Original 6
        constexpr int BUTTON_INNER_R = 8;   ///< Original 5
        constexpr int ACCENT_INSET = 12;    ///< Akzentstreifen oben und unten eingerückt, Original 8
    }

    /** @brief Grobaufteilung: Menü links, Scrollbalken, Arbeitsbereich rechts, Fußzeile unten. */
    namespace Frame
    {
        constexpr int FOOTER_H = 84;                          ///< Original 56
        constexpr int VIEWPORT_H = HEIGHT - FOOTER_H;         ///< 636, Original 424
        constexpr int FOOTER_Y = VIEWPORT_H;                  ///< Original 424
        constexpr int MENU_X = 3;                             ///< Original 2
        constexpr int MENU_W = 296;                           ///< Original 170, Einzug 15 + "Time & time server" in Comfortaa Bold 27 (266) + Rand 15
        constexpr int SECTION_ROW_H = 48;                     ///< Sektionskopf, Original 28, mit den zwölf Einträgen (elf wie im Original-RetroRadio und UKW) über die volle Höhe verteilt (2 x 48 + 12 x 52 = 720)
        constexpr int ITEM_ROW_H = 52;                        ///< Menüeintrag, Original 36, Comfortaa Bold 27 ist 27 px hoch (Aufstrich 21, Unterlänge 6), jeder Menütext in DE und EN bleibt einzeilig mit 25 px Luft
        constexpr int ITEM_ACCENT_W = 6;                      ///< Kategoriestreifen, Original 4
        constexpr int ITEM_TEXT_X = ITEM_ACCENT_W + 9;        ///< 15, Original 4+6
        constexpr int SCROLLBAR_X = MENU_X + MENU_W + 6;      ///< 305, Original 176 (4 nach dem Menü)
        constexpr int SCROLLBAR_W = 9;                        ///< Original 6
        constexpr int DIVIDER_X = SCROLLBAR_X + SCROLLBAR_W;  ///< 314, Original 182, Linie 2 statt 1 px
        constexpr int WORK_X = DIVIDER_X + 15;                ///< 329, Original 192 (10 nach dem Scrollbalken)
        constexpr int WORK_Y = 8;                             ///< Original 5
        constexpr int WORK_W = WIDTH - WORK_X - 15;           ///< 936, Original 598 (Rand 10), nimmt die übrige Breite auf
        constexpr int WORK_H = VIEWPORT_H - WORK_Y;           ///< 628, Original 419
        constexpr int WORK_R = 18;                            ///< Original 12
    }

    /** @brief Titelbox oben im Arbeitsbereich. */
    namespace Title
    {
        constexpr int BOX_X = 15;                        ///< Original 10
        constexpr int BOX_Y = 15;                        ///< Original 10
        constexpr int BOX_W = Frame::WORK_W - 2 * BOX_X; ///< 906, Original 578
        constexpr int BOX_H = 104;                       ///< Original 69
        constexpr int BOX_R = 15;                        ///< Original 10
        constexpr int TEXT_X = 42;                       ///< Titel und Untertitel, Original 28
        constexpr int TEXT_Y = 27;                       ///< Original 18
        constexpr int SUB_Y = 75;                        ///< Original 50
    }

    /** @brief Inhaltsbereich unter der Titelbox. */
    namespace Content
    {
        constexpr int X = Title::BOX_X;                      ///< Original 10
        constexpr int Y = Title::BOX_Y + Title::BOX_H + 18;  ///< 137, Original 91
        constexpr int W = Frame::WORK_W - 2 * X;             ///< 906, Original 578
        constexpr int H = Frame::WORK_H - Y - 15;            ///< 476, Original 318
    }

    /** @brief WLAN: drei Profilzeilen, Tastenreihe und Netzwerknamenfeld. */
    namespace Wifi
    {
        constexpr int ROW_H = 69;           ///< Original 46
        constexpr int ROW_GAP = 12;         ///< Original 8
        constexpr int ROW_ACCENT_INSET = 9; ///< Original 6
        constexpr int DOT_X = 33;           ///< Statuspunkt, Original 22
        constexpr int DOT_R = 9;            ///< Original 6
        constexpr int TEXT_X = 60;          ///< Name und Status, Original 40
        constexpr int NAME_Y = 24;          ///< Mitte des Namens, Original 16
        constexpr int STATUS_DY = 21;       ///< Mitte des Status über der Zeilenunterkante, Original 14
        constexpr int ACTION_GAP_Y = 9;     ///< Tastenreihe unter den Profilen, Original 6
        constexpr int ACTION_H = 66;        ///< Original 44
        constexpr int ACTION_GAP = 15;      ///< Original 10
        constexpr int HOST_H = 96;          ///< Netzwerknamenfeld, Original 64
        constexpr int HOST_BOTTOM_GAP = 15; ///< Original 10
        constexpr int HOST_PAD_X = 21;      ///< Original 14
        constexpr int HOST_LINE_GAP = 6;    ///< Original 4
        constexpr int EDIT_W = 195;         ///< ändern-Knopf, Original 130
        constexpr int EDIT_H = 66;          ///< Original 44
    }

    /** @brief Bluetooth (ohne Vorbild im Original): eine Gerätezeile wie ein WLAN-Profil, darunter die Tastenreihe im Abstand der WLAN-Tasten zum letzten Profil und unten das Senderfeld anstelle des Netzwerknamens, alles mit den Maßen aus Wifi. */
    namespace Bluetooth
    {
        constexpr int ACTION_COUNT = 3;                           ///< suchen, test, vergessen
        constexpr int ACTION_SLOTS[ACTION_COUNT] = {0, 2, 3};     ///< Spalten im Raster der vier WLAN-Tasten: jede Taste steht unter ihrem WLAN-Gegenstück (scannen, test, löschen), die Spalte von „passwort“ bleibt frei und trennt die Suche nach neuen Geräten von den Tasten für das gespeicherte
        constexpr int ACTION_Y = Wifi::ROW_H + Wifi::ROW_GAP + Wifi::ACTION_GAP_Y; ///< 90 unter dem Inhaltsanfang, Abstand zur Zeile wie bei WLAN
    }

    /** @brief Umschalter in den Profilzeilen. */
    namespace Toggle
    {
        constexpr int W = 83;       ///< Original 55
        constexpr int H = 44;       ///< Original 29
        constexpr int MARGIN = 21;  ///< Abstand zum rechten Zeilenrand, Original 14
        constexpr int INSET = 3;    ///< Gehäuserand, Original 2
        constexpr int KNOB_R = 15;  ///< Original 10
        constexpr int KNOB_GAP = 2; ///< Abstand des Knopfes zum Innenrand, Original 1
    }

    /** @brief Sprachzeilen. */
    namespace Language
    {
        constexpr int ROW_H = 81;        ///< Original 54
        constexpr int ROW_GAP = 18;      ///< Original 12
        constexpr int ROW_R = 15;        ///< Original 10
        constexpr int TEXT_X = 36;       ///< Original 24
        constexpr int ACTIVE_RIGHT = 30; ///< Original 20
        constexpr int HEADING_H = 48;    ///< Rubriküberschrift „Sprache“ bzw. „Serielle Logs“ auf der Seite Allgemein, Original 32
        constexpr int SECTION_GAP = 36;  ///< Abstand zwischen den Rubriken, die Trennlinie liegt in der Mitte, Original 24
    }

    /** @brief Themenseite (ohne Vorbild im Original): Zeilen wie auf der Sprachseite, so hoch, dass links eine Minivorschau im Seitenverhältnis des Displays Platz hat. */
    namespace Theme
    {
        constexpr int ROW_H = 138;                                  ///< Drei Zeilen und zwei Abstände füllen 450 der 476 Inhaltspixel
        constexpr int ROW_GAP = Language::ROW_GAP;                  ///< 18
        constexpr int PREVIEW_X = Language::TEXT_X;                 ///< 36, Einzug wie der Sprachname
        constexpr int PREVIEW_H = ROW_H - 2 * Style::ACCENT_INSET;  ///< 114, oben und unten bündig mit dem Akzentstreifen
        constexpr int PREVIEW_W = 203;                              ///< 16:9 wie das Display
        constexpr int PREVIEW_R = Style::BUTTON_R;                  ///< 9
        constexpr int TEXT_X = PREVIEW_X + PREVIEW_W + Language::TEXT_X; ///< 275, Name rechts der Vorschau
        constexpr int CLOCK_R = 45;                                 ///< Zifferblatt links in der Vorschau
        constexpr int CARD_INSET = 12;                              ///< Abstand der Fläche rechts in der Vorschau zum Rand
    }

    /** @brief Sicherheitsseite: Schalter der PIN-Sperre oben, darunter PIN-Hinweis, PIN-Punkte und Ziffernfeld. */
    namespace Security
    {
        constexpr int HINT_Y = Toggle::H + 48; ///< Mitte des Hinweises unter dem Schalter der PIN-Sperre, 92, Original TOGGLE_H + 32
        constexpr int DOTS_Y = HINT_Y + 54;    ///< Mitte der Punkte, 146, Original PIN_HINT_Y + 36
        constexpr int DOT_R = 20;              ///< Original 13
        constexpr int DOT_STEP = 54;           ///< Abstand zweier Punktmitten, die Punkte stehen einzeln, Original 36
        constexpr int PAD_GAP = 6;   ///< Ziffernfeld, Original 4
        constexpr int PAD_KEY_H = 41; ///< Original 27
        constexpr int PAD_R = 9;     ///< Original 6
        constexpr int PAD_TEXT_OY = 3; ///< Original 2
    }

    /** @brief Zeit: drei Auswahlzeilen, fünf Schrittgeber links, der Knopf zum Setzen rechts daneben, unten die Zeile des Sekundenzeigers. */
    namespace Time
    {
        constexpr int FIELD_H = 75;        ///< Original 50
        constexpr int FIELD_GAP = 9;       ///< Original 6
        constexpr int FIELD_PAD_X = 21;    ///< Original 14
        constexpr int LABEL_Y = 9;         ///< Original 6
        constexpr int VALUE_Y = 39;        ///< Original 26
        constexpr int FIELD_COUNT = 3;     ///< Zeitzone, NTP-Server, Intervall
        constexpr int COL_W = 105;         ///< Original 70
        constexpr int COL_GAP = 9;         ///< Original 6
        constexpr int STEP_H = 39;         ///< Plus und Minus, Original 26
        constexpr int VALUE_H = 48;        ///< Original 32
        constexpr int VALUE_GAP = 3;       ///< Zwischen Knopf und Wert, Original 2
        constexpr int TOTAL_H = 2 * STEP_H + VALUE_H + 2 * VALUE_GAP; ///< 132, Original 88
        constexpr int VALUE_OFFSET_Y = 5;  ///< Ziffern optisch mittig, Original 3
        constexpr int SECOND_Y = Content::H - FIELD_H; ///< 401, Zeile „Sekundenzeiger“ am unteren Rand des Inhalts, ohne Vorbild im Original
        constexpr int STEPPER_Y = (FIELD_COUNT * (FIELD_H + FIELD_GAP) - FIELD_GAP + SECOND_Y - TOTAL_H) / 2; ///< 256, mittig zwischen Auswahlzeilen und Sekundenzeile. Original 174 unter den Auswahlzeilen, die Schrittgeber dort mittig mit „zeit setzen“ darunter, hier linksbündig mit „zeit setzen“ rechts daneben
        constexpr int SET_W = 240;         ///< Original 160
        constexpr int SET_H = 68;          ///< Original 45
    }

    /** @brief Zeitfenster: sieben Wochentage mal 24 Stunden. */
    namespace Window
    {
        constexpr int DAY_LABEL_W = 42;  ///< Original 28, das Stundenraster nimmt die zusätzliche Breite auf
        constexpr int CELL_H = 41;       ///< Original 27
        constexpr int CHECK = 23;        ///< Kantenlänge der Checkbox, Original 15
        constexpr int CHECK_LINE = 2;    ///< Original 1
        constexpr int HEADER_H = 39;     ///< Original 26
        constexpr int TOP = 15;          ///< Abstand zum Inhalt, Original 10
        constexpr int DAY_TEXT_X = 12;   ///< Original 8
        constexpr int FRAME_R = 9;       ///< Original 6
        constexpr int INNER_R = 8;       ///< Original 5
        constexpr int FRAME_INSET = 2;   ///< Original 1
        constexpr int FRAME_PAD_Y = 4;   ///< Original 2, 4 statt 3, damit der Rahmen oben und unten wie seitlich 2 px stark bleibt
        constexpr int TICK_W = 3;        ///< Strich des Hakens, Original 2
        constexpr int HINT_GAP_Y = 24;   ///< Original 16
        constexpr int HINT_LINE_H = 29;  ///< Original 19
        constexpr int HINT_X = 6;        ///< Original 4
    }

    /** @brief Klang: sechs Zeilen mit Minus, Wert und Plus. */
    namespace Sound
    {
        constexpr int ROW_H = 66;         ///< Original 44
        constexpr int ROW_GAP = 8;        ///< Original 5
        constexpr int FIRST_Y = 6;        ///< Original 4
        constexpr int STEP_W = 87;        ///< Original 58
        constexpr int VALUE_W = 225;      ///< Original 150
        constexpr int STEP_GAP = 15;      ///< Original 10
        constexpr int LABEL_X = 6;        ///< Original 4
        constexpr int VALUE_OFFSET_Y = 5; ///< Original 3
    }

    /** @brief Anzeige: Röhrenkacheln und zwei Wahlzeilen des VU-Meters. */
    namespace Appearance
    {
        constexpr int HINT_H = 32;        ///< Zeilenhöhe von ComfortaaBold24 ohne die tiefere Unterlänge von Zeichen wie ą (Aufstrich 26, Unterlänge 6), Original 23
        constexpr int SEC_LINE_DY = 36;   ///< Linie unter der Überschrift, Original 24
        constexpr int SEC1_Y = 0;         ///< Original 0
        constexpr int TILE_Y = 44;        ///< Original 29
        constexpr int TILE_GAP = 21;      ///< Original 14
        constexpr int TILE_W = (Content::W - 3 * TILE_GAP) / 4; ///< 210, Original 134, die Kacheln füllen wie im Original die Inhaltsbreite
        constexpr int TILE_H = 141;       ///< Original 94
        constexpr int EYE_BOX = 105;      ///< Röhrenvorschau, Original 70
        constexpr int EYE_Y = 6;          ///< Original 4
        constexpr float RING = 7.5f;      ///< Original 5
        constexpr float EM80_HALF_W = 33.0f;  ///< Original 22
        constexpr float EM80_HALF_H = 43.5f;  ///< Original 29
        constexpr float EM84_HALF_W = 18.75f; ///< Original 12,5
        constexpr float EM84_HALF_H = 51.0f;  ///< Original 34
        constexpr int NAME_DY = 20;       ///< Mitte des Namens über der Kachelunterkante, Original 13
        constexpr int HINT1_Y = 192;      ///< Original 128
        constexpr int SEC2_Y = 239;       ///< Original 159
        constexpr int ROW1_Y = 284;       ///< Original 189
        constexpr int ROW2_Y = 384;       ///< Original 256
        constexpr int ROW_H = 54;         ///< Original 36
        constexpr int HINT_DY = 60;       ///< Original 39, 60 statt 59, damit die letzte Hinweiszeile wie im Original genau auf der Unterkante des Inhalts endet
        constexpr int LABEL_W = 225;      ///< Original 150
        constexpr int SEG_GAP = 9;        ///< Original 6
        constexpr int LEVEL_W = 330;      ///< Pegelanzeige, Original 220
        constexpr int LEVEL_GAP = 12;     ///< Zwischen Beschriftung und Pegel, Original 8
        constexpr int TEXT_X = 3;         ///< Überschriften und Hinweise, Original 2
    }

    /** @brief Bildschirmseite (ohne Vorbild im Original): Abschnitte im Raster der Anzeige-Seite, Helligkeit als Zeile wie auf der Klangseite. */
    namespace Screen
    {
        constexpr int ROW1_Y = Appearance::TILE_Y;              ///< Helligkeitszeile, so hoch wie eine Klangzeile
        constexpr int HINT1_Y = ROW1_Y + Sound::ROW_H + 9;      ///< Hinweis unter der Helligkeit
        constexpr int SEC2_Y = Appearance::SEC2_Y;              ///< Abschnitt Ausrichtung
        constexpr int ROW2_Y = Appearance::ROW1_Y;              ///< Wahlzeile der Automatik
        constexpr int HINT2_Y = ROW2_Y + Appearance::HINT_DY;   ///< Hinweis darunter
        constexpr int ROW3_Y = Appearance::ROW2_Y;              ///< Wahlzeile der festen Ausrichtung
        constexpr int HINT3_Y = ROW3_Y + Appearance::HINT_DY;   ///< Hinweis darunter, endet wie auf der Anzeige-Seite auf der Inhaltskante
    }

    /** @brief UKW (ohne Vorbild im Original): eine Senderzeile wie ein WLAN-Profil mit Schalter, darunter Frequenz und Leistung als Klangzeilen (Beschriftung, Minus, Wert, Plus) mit je einem Hinweis, unten das Modulatorfeld anstelle des Netzwerknamens. */
    namespace Ukw
    {
        constexpr int ROW1_Y = Bluetooth::ACTION_Y;                     ///< Frequenzzeile, 90 unter dem Inhaltsanfang wie die Bluetooth-Tasten
        constexpr int HINT1_Y = ROW1_Y + Sound::ROW_H + 9;              ///< 165: Hinweis zur Frequenz
        constexpr int ROW2_Y = HINT1_Y + Appearance::HINT_H + 18;       ///< 215: Leistungszeile
        constexpr int HINT2_Y = ROW2_Y + Sound::ROW_H + 9;              ///< 290: erster Hinweis zur Leistung
        constexpr int HINT3_Y = HINT2_Y + Appearance::HINT_H;           ///< 322: zweiter Hinweis, endet vor dem Modulatorfeld (ab 380)
    }

    /** @brief Info: zwei Spalten mit Kopf, Zeilen und I2C-Zeile. */
    namespace Info
    {
        constexpr int COL_GAP = 21;        ///< Original 14
        constexpr int COL_W = (Content::W - COL_GAP) / 2; ///< 442, Original 282
        constexpr int HDR_H = 39;          ///< Original 26
        constexpr int HDR_PAD_X = 15;      ///< Original 10
        constexpr int ROWS_GAP = 6;        ///< Erste Zeile unter dem Kopf, Original 4
        constexpr int ROW_H = 38;          ///< Original 28 (1,5-fach wären 42), für zehn Zeilen links (mit FTP) über dem I2C-Streifen 38
        constexpr int ROW_PAD_X = 12;      ///< Original 8
        constexpr int I2C_LABEL_GAP = 15;  ///< Original 10
        constexpr int I2C_BOTTOM_GAP = 15; ///< Original 10
        constexpr int I2C_BADGE_Y = 2;     ///< Original 1
        constexpr int SIGNAL_W = 120;      ///< Original 80
        constexpr int SIGNAL_H = 12;       ///< Original 8
        constexpr int SIGNAL_R = 3;        ///< Original 2
        constexpr int SIGNAL_GAP = 9;      ///< Text vor dem Balken, Original 6
    }

    /** @brief Fußzeile mit Speichern, Textknöpfen, Klangfeld, Uhr und Haus-Taste. */
    namespace Footer
    {
        constexpr int CY = Frame::FOOTER_Y + Frame::FOOTER_H / 2; ///< 678, Original 452
        constexpr int BUTTON_Y = Frame::FOOTER_Y + 9;  ///< Original +6
        constexpr int BUTTON_H = 66;                   ///< Original 44
        constexpr int SAVE_W = 225;                    ///< Original 150
        constexpr int ACTION_MIN_W = 180;              ///< Textknöpfe, Original 120
        constexpr int ACTION_PAD = 72;                 ///< Zusatzbreite zum Text, Original 48
        constexpr int ACTION_GAP = 18;                 ///< Original 12
        constexpr int EXIT = 66;                       ///< Haus-Taste, Original 44
        constexpr int EXIT_CX = WIDTH - EXIT / 2 - 15; ///< 1232, Original 768 (Rand 10)
        constexpr int CLOCK_W = 140;                   ///< "88:88" in Font7 unskaliert wie die Uhren von Radio und SD-Player, Original 104 bei Textgröße 0,75
        constexpr int CLOCK_H = 68;                    ///< Original 45
        constexpr int CLOCK_X = EXIT_CX - EXIT / 2 - 21 - CLOCK_W; ///< 1038, Original 650 (14 vor der Haus-Taste)
        constexpr int CLOCK_Y = Frame::FOOTER_Y + (Frame::FOOTER_H - CLOCK_H) / 2 + 2; ///< 646, Original 430
        constexpr int CLOCK_GAP = 24;                  ///< Mindestabstand von Textknöpfen und Klangfeld zur Uhr, Original 16
        constexpr int CLOCK_DIGIT_H = 46;              ///< Höhe der Ziffern von Font7
        constexpr int BATTERY_SYMBOL_W = 50;             ///< Akkusymbol liegend (BatteryIcon::Small), nur mit erkanntem Akku, ohne Vorbild im Original
        constexpr int BATTERY_SYMBOL_H = 24;             ///< Höhe des Symbols
        constexpr int BATTERY_W = 56;                  ///< Breite von "100 %" in ComfortaaBold21, das Symbol steht mittig darunter
        constexpr int BATTERY_GAP = 21;                ///< Abstand zur Uhr wie zwischen Uhr und Haus-Button
        constexpr int BATTERY_X = CLOCK_X - BATTERY_GAP - BATTERY_W; ///< 961
        constexpr int BATTERY_CAP_H = 17;              ///< Höhe der Ziffern von ComfortaaBold21
        constexpr int BATTERY_TEXT_Y = CLOCK_Y + (CLOCK_H - CLOCK_DIGIT_H) / 2; ///< 657, Oberkante der Zahl auf der Oberkante der Uhrziffern
        constexpr int BATTERY_SYMBOL_Y = BATTERY_TEXT_Y + CLOCK_DIGIT_H - BATTERY_SYMBOL_H; ///< 679, Unterkante des Symbols auf der Unterkante der Uhrziffern
        constexpr int BATTERY_SHIFT = BATTERY_W + BATTERY_GAP; ///< 77, um so viel rücken Textknöpfe und Klangfeld mit Akku nach links
    }

    /** @brief Klangfeld in der Fußzeile: Hörprobe, leiser, Lautstärke, lauter. */
    namespace SoundFooter
    {
        constexpr int BUTTON = 66;        ///< Original 44
        constexpr int GAP = 12;           ///< Original 8
        constexpr int DOT = 4;            ///< Original 3, mit Abstand 2 wird die Ziffer 28x40, genau 1,5-fach (Original 19x27)
        constexpr int DOT_100 = 3;        ///< Schmalere Punkte für 100, Original 2
        constexpr int DOT_SPACE = 2;      ///< Original 1
        constexpr int CHAR_SPACE = 8;     ///< Original 5
        constexpr int CHAR_W = 5 * DOT + 4 * DOT_SPACE;            ///< 28, Original 19
        constexpr int DIGITS_W = 2 * CHAR_W + CHAR_SPACE;          ///< 64, Original 43
        constexpr int DISP_W = 3 * (5 * DOT_100 + 4 * DOT_SPACE);  ///< 69 für 100, Original 43
        constexpr int DISP_H = 7 * DOT + 6 * DOT_SPACE;            ///< 40, Original 27
        constexpr int FRAME_PAD = 10;     ///< Original 8, 10 statt 12 hält den Rahmen 1,5-fach
        constexpr int FRAME_W = DISP_W + 2 * FRAME_PAD;            ///< 89, Original 59
        constexpr int BLOCK_W = BUTTON * 3 + FRAME_W + GAP * 3;    ///< 323, Original 215
        constexpr int BLOCK_RIGHT = Footer::CLOCK_X - Footer::CLOCK_GAP; ///< Original 634
        constexpr int PLAY_CX = BLOCK_RIGHT - BLOCK_W + BUTTON / 2;
        constexpr int DOWN_CX = PLAY_CX + BUTTON + GAP;
        constexpr int FRAME_X = DOWN_CX + BUTTON / 2 + GAP;
        constexpr int DISP_X = FRAME_X + FRAME_PAD;
        constexpr int UP_CX = FRAME_X + FRAME_W + GAP + BUTTON / 2;
    }

    /** @brief Auswahlliste für Zeitzone und Intervall, 1,5-fach mittig. */
    namespace Picker
    {
        constexpr int W = 690;            ///< Original 460
        constexpr int H = 600;            ///< Original 400
        constexpr int X = (WIDTH - W) / 2;  ///< 295, Original 170
        constexpr int Y = (HEIGHT - H) / 2; ///< 60, Original 40
        constexpr int BORDER = 3;         ///< Original 2
        constexpr int R = 18;             ///< Original 12
        constexpr int INNER_R = 15;       ///< Original 10
        constexpr int TITLE_Y = 51;       ///< Mitte des Titels, Original 34
        constexpr int LIST_X = 24;        ///< Original 16
        constexpr int LIST_Y = 90;        ///< Original 60
        constexpr int LIST_WIDTH = 642;   ///< Original 428
        constexpr int LIST_HEIGHT = 402;  ///< Original 268
        constexpr int ROW_STEP = 57;      ///< Original 38
        constexpr int ROW_H = 54;         ///< Original 36
        constexpr int ROW_R = 8;          ///< Original 5
        constexpr int TEXT_X = 21;        ///< Original 14
        constexpr int END_GAP = 3;        ///< Original 2
        constexpr int SELECT_Y = 189;     ///< Abstand des gewählten Eintrags zur Listenoberkante beim Öffnen, Original 126
        constexpr int DRAG = 8;           ///< Ziehschwelle, Original 5
        constexpr int CANCEL_X = 225;     ///< Original 150
        constexpr int CANCEL_Y = 510;     ///< Original 340
        constexpr int CANCEL_W = 240;     ///< Original 160
        constexpr int CANCEL_H = 66;      ///< Original 44
    }

    /** @brief Meldungen, die Innenmaße stehen nativ in SettingsMessageBox. */
    namespace Message
    {
        constexpr int X = 340;       ///< Original 200
        constexpr int Y = 210;       ///< Original 140
        constexpr int W = 600;       ///< Original 400
        constexpr int H = 300;       ///< Original 200
        constexpr int TALL_H = 330;  ///< Meldungen mit Neustartfrage, Original 220
        constexpr int WIDE_W = 630;  ///< Werkseinstellungen, Original 420
        constexpr int WIDE_Y = 195;  ///< Original 130
    }

    /** @brief Fortschrittsdialog des WLAN-Tests, mittig. */
    namespace Progress
    {
        constexpr int W = 600;          ///< Original 400
        constexpr int H = 300;          ///< Original 200
        constexpr int BORDER = 2;       ///< Original 1
        constexpr int R = 6;            ///< Original 4
        constexpr int MARGIN = 30;      ///< Original 20
        constexpr int TITLE_Y = 27;     ///< Original 18
        constexpr int LINE_Y = 72;      ///< Original 48
        constexpr int LINE_H = 3;       ///< Original 2
        constexpr int TEXT_Y = 98;      ///< Original 65
        constexpr int TEXT_LEADING = 3; ///< Original 2, ComfortaaBold21 ist 30 hoch, Zeilenraster 33 (Original 22)
        constexpr int BAR_DY = 98;      ///< Balkenoberkante über der Unterkante, Original 65
        constexpr int BAR_H = 60;       ///< Original 40
        constexpr int SEGMENT = 2;      ///< Original 1
        constexpr int SEGMENT_GAP = 3;  ///< Original 2
    }

    /** @brief WLAN-Auswahl mit fünf sichtbaren Zeilen, mittig. */
    namespace Networks
    {
        constexpr int W = 750;              ///< Original 500
        constexpr int H = 450;              ///< Original 300
        constexpr int X = (WIDTH - W) / 2;  ///< 265, Original 150
        constexpr int Y = (HEIGHT - H) / 2; ///< 135, Original 90
        constexpr int BORDER = 2;           ///< Original 1
        constexpr int R = 6;                ///< Original 4
        constexpr int TITLE_X = 15;         ///< Original 10
        constexpr int TITLE_Y = 38;         ///< Titelmitte, Original 25
        constexpr int CLOSE = 45;           ///< Schließen-Taste, Original 30
        constexpr int CLOSE_RIGHT = 60;     ///< Linke Kante vor dem rechten Rand, Original 40
        constexpr int CLOSE_Y = 15;         ///< Original 10
        constexpr int LIST_X = 15;          ///< Original 10
        constexpr int LIST_Y = 75;          ///< Original 50
        constexpr int LIST_WIDTH = W - 2 * LIST_X; ///< 720, Original 480
        constexpr int LIST_HEIGHT = 278;    ///< Original 185
        constexpr int ROW_H = 54;           ///< Original 36
        constexpr int ROW_TEXT_X = 15;      ///< Original 10
        constexpr int SIGNAL_RIGHT = 15;    ///< Original 10
        constexpr int LOCK_RIGHT = 102;     ///< Schlossmitte vor dem rechten Listenrand, Original 68
        constexpr int LOCK_Y = 24;          ///< Original 16
        constexpr int SEP_X = 8;            ///< Trennlinie eingerückt, Original 5
        constexpr int BUTTON_W = 120;       ///< Original 80
        constexpr int BUTTON_H = 68;        ///< Original 45
        constexpr int OK_RIGHT = 135;       ///< Linke Kante vor dem rechten Rand, Original 90
        constexpr int SCAN_RIGHT = 270;     ///< Original 180
        constexpr int BUTTON_DY = 83;       ///< Oberkante über der Unterkante, Original 55
        constexpr int SCROLL_RIGHT = 27;    ///< Original 18
        constexpr int SCROLL_W = 12;        ///< Original 8
        constexpr int SCROLL_MIN_H = 30;    ///< Original 20
        constexpr int SCROLL_R = 5;         ///< Original 3
        constexpr int DRAG = 8;             ///< Ziehschwelle, Original 5
    }

    /** @brief Bildschirmtastatur, mittig. */
    namespace Keyboard
    {
        constexpr int W = 1155;           ///< Original 770
        constexpr int KEY_H = 68;         ///< Original 45
        constexpr int KEY_GAP = 6;        ///< Original 4
        constexpr int R = 6;              ///< Rahmen, Eingabefeld und Tasten, Original 4
        constexpr int BORDER = 2;         ///< Original 1
        constexpr int PADDING = 15;       ///< Original 10
        constexpr int LABEL_H = 60;       ///< Original 40
        constexpr int INPUT_H = 54;       ///< Original 36
        constexpr int INPUT_MARGIN = 12;  ///< Original 8
        constexpr int TITLE_Y = 38;       ///< Titelmitte, Original 25
        constexpr int CLOSE = 45;         ///< Schließen-Taste, Original 30
        constexpr int CLOSE_RIGHT = 60;   ///< Original 40
        constexpr int CLOSE_Y = 15;       ///< Original 10
        constexpr int TEXT_X = 12;        ///< Texteinzug im Eingabefeld, Original 8
        constexpr int HIDDEN_DY = 6;      ///< Sternchen tiefer gesetzt, Original 4
        constexpr int CURSOR_INSET = 9;   ///< Original 6
        constexpr int CURSOR_W = 2;       ///< Original 1
        constexpr int LABEL_OFFSET = 23;  ///< Einzug links oder rechts ausgerichteter Tastentexte, Original 15
    }
}
#endif
