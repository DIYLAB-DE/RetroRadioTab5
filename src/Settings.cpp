#include "Settings.h"
#include "FtpProtocol.h"
#include "StringUtils.h"
#include "Thema.h"
#include "Tab5Render.h"
#include "SettingsTab5Layout.h"
#include "fonts/ComfortaaBold15.h"
#include "fonts/ComfortaaBold21.h"
#include "fonts/ComfortaaBold24.h"
#include "fonts/ComfortaaBold27.h"
#include "fonts/ComfortaaBold45.h"
#include "fonts/LeagueGothicCond51.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
// Geometrie, Zeichenpfade und Bedienung selektiv aus DIYLAB-DE/RetroRadio ea4229a34fb39faaff23619092cf09cafb8c51cf, Settings.cpp. GPL-3.0, siehe referenz/RetroRadio-LICENSE.
using OKey = SettingsTextKey;
#define STRF(dst, key, ...) snprintf(dst, sizeof(dst), ot(key), ##__VA_ARGS__)

namespace
{
    enum class MagicEyeStyle
    {
        Em11,
        Em34,
        Em80,
        Em84
    };
    namespace L = SettingsTab5Layout;
    // Bildschirm und Grobaufteilung, Maße mit Originalwerten in include/SettingsTab5Layout.h
    constexpr int SCREEN_W = L::WIDTH;                ///< Display-Breite
    constexpr int SCREEN_H = L::HEIGHT;               ///< Display-Höhe
    constexpr int FOOTER_H = L::Frame::FOOTER_H;      ///< Höhe der Fußzeile
    constexpr int VIEWPORT_H = L::Frame::VIEWPORT_H;  ///< Höhe über der Fußzeile
    constexpr int FOOTER_Y = L::Frame::FOOTER_Y;      ///< Obere Kante der Fußzeile
    constexpr int LINE = L::Style::LINE;              ///< Stärke der Trennlinien

    // Menü (links)
    constexpr int MENU_X = L::Frame::MENU_X;               ///< Linke Kante der Menüliste
    constexpr int MENU_W = L::Frame::MENU_W;               ///< Breite der Menüliste
    constexpr int SECTION_ROW_H = L::Frame::SECTION_ROW_H; ///< Zeilenhöhe einer Sektionsüberschrift
    constexpr int ITEM_ROW_H = L::Frame::ITEM_ROW_H;       ///< Zeilenhöhe eines Panel-Eintrags
    constexpr int ITEM_ACCENT_W = L::Frame::ITEM_ACCENT_W; ///< Breite des Kategorie-Farbstreifens
    constexpr int ITEM_TEXT_X = L::Frame::ITEM_TEXT_X;     ///< Texteinzug nach dem Akzent-Balken

    // Scrollbar (zwischen Menü und Arbeitsbereich)
    constexpr int SCROLLBAR_X = L::Frame::SCROLLBAR_X; ///< Linke Kante der Scrollbar
    constexpr int SCROLLBAR_W = L::Frame::SCROLLBAR_W; ///< Breite der Scrollbar
    constexpr int DIVIDER_X = L::Frame::DIVIDER_X;     ///< Grenze Menübereich Arbeitsbereich

    // Arbeitsbereich (rechts)
    constexpr int WORK_X = L::Frame::WORK_X; ///< Linke Kante des Arbeitsbereichs
    constexpr int WORK_Y = L::Frame::WORK_Y; ///< Obere Kante des Arbeitsbereichs
    constexpr int WORK_W = L::Frame::WORK_W; ///< Breite des Arbeitsbereichs
    constexpr int WORK_H = L::Frame::WORK_H; ///< Höhe des Arbeitsbereichs
    constexpr int WORK_R = L::Frame::WORK_R; ///< Eckradius des Arbeitsbereichs

    // Titelbox im Arbeitsbereich (sprite-relativ)
    constexpr int TITLE_BOX_X = L::Title::BOX_X;   ///< Linke Kante der Titelbox
    constexpr int TITLE_BOX_Y = L::Title::BOX_Y;   ///< Obere Kante der Titelbox
    constexpr int TITLE_BOX_W = L::Title::BOX_W;   ///< Breite der Titelbox
    constexpr int TITLE_BOX_H = L::Title::BOX_H;   ///< Höhe der Titelbox
    constexpr int TITLE_BOX_R = L::Title::BOX_R;   ///< Eckradius der Titelbox
    constexpr int TITLE_TEXT_X = L::Title::TEXT_X; ///< X-Einzug von Titel und Untertitel
    constexpr int TITLE_TEXT_Y = L::Title::TEXT_Y; ///< Y-Position des Titels
    constexpr int SUB_TEXT_Y = L::Title::SUB_Y;    ///< Y-Position des Untertitels

    // Inhaltsbereich unterhalb der Titelbox (sprite-relativ)
    constexpr int CONTENT_X = L::Content::X; ///< Linke Kante des Inhalts
    constexpr int CONTENT_Y = L::Content::Y; ///< Obere Kante des Inhalts
    constexpr int CONTENT_W = L::Content::W; ///< Breite des Inhalts
    constexpr int CONTENT_H = L::Content::H; ///< Höhe des Inhalts

    // Gemeinsame Rahmen und Radien
    constexpr int BORDER = L::Style::BORDER;                 ///< Rahmen von Feldern und Tasten
    constexpr int FIELD_R = L::Style::FIELD_R;               ///< Eckradius von Zeilen und Feldern
    constexpr int FIELD_INNER_R = L::Style::FIELD_INNER_R;   ///< Eckradius der Feldfüllung
    constexpr int BUTTON_INNER_R = L::Style::BUTTON_INNER_R; ///< Eckradius der Tastenfüllung
    constexpr int ACCENT_INSET = L::Style::ACCENT_INSET;     ///< Einzug des Akzentstreifens

    // WLAN-Profilzeilen
    constexpr int WIFI_ROW_H = L::Wifi::ROW_H;        ///< Höhe einer Profilzeile
    constexpr int WIFI_ROW_GAP = L::Wifi::ROW_GAP;    ///< Abstand zwischen Profilzeilen
    constexpr int WIFI_ACT_H = L::Wifi::ACTION_H;     ///< Höhe der Aktionsknöpfe
    constexpr int WIFI_ACT_GAP = L::Wifi::ACTION_GAP; ///< Abstand zwischen Aktionsknöpfen

    // Geräte-Netzwerkname (Feld am unteren Rand des Arbeitsbereichs)
    constexpr int HOSTNAME_FIELD_H = L::Wifi::HOST_H;            ///< Höhe des Netzwerknamen-Feldes
    constexpr int HOSTNAME_BOTTOM_GAP = L::Wifi::HOST_BOTTOM_GAP; ///< Abstand der Feld-Unterkante zur Unterkante des Arbeitsbereichs
    constexpr int HOSTNAME_PAD_X = L::Wifi::HOST_PAD_X;          ///< Innenabstand links/rechts im Feld
    constexpr int HOSTNAME_LINE_GAP = L::Wifi::HOST_LINE_GAP;    ///< Vertikaler Abstand zwischen Beschriftung und Name
    constexpr int HOSTNAME_EDIT_BTN_W = L::Wifi::EDIT_W;         ///< Breite des ändern-Knopfes im Feld
    constexpr int HOSTNAME_EDIT_BTN_H = L::Wifi::EDIT_H;         ///< Höhe des ändern-Knopfes im Feld

    // Umschalter mit Rahmen, dunkler Füllung und Statusfarbe
    constexpr int TOGGLE_W = L::Toggle::W;           ///< Breite des Umschalters
    constexpr int TOGGLE_H = L::Toggle::H;           ///< Höhe des Umschalters
    constexpr int TOGGLE_MARGIN = L::Toggle::MARGIN; ///< Abstand des Umschalters vom rechten Zeilenrand
    constexpr int TOGGLE_INSET = L::Toggle::INSET;   ///< Randbreite des Gehäuses
    constexpr int TOGGLE_KNOB_R = L::Toggle::KNOB_R; ///< Radius des Schaltknopfes

    // Sprachzeilen
    constexpr int LANG_ROW_H = L::Language::ROW_H;     ///< Höhe einer Sprachzeile
    constexpr int LANG_ROW_GAP = L::Language::ROW_GAP; ///< Abstand zwischen Sprachzeilen
    constexpr int LANG_ROWS_Y = CONTENT_Y + L::Language::HEADING_H;                                                ///< Beginn der Sprachzeilen unter der Rubrik Sprache
    constexpr int LOGS_HEADING_Y = LANG_ROWS_Y + 2 * LANG_ROW_H + LANG_ROW_GAP + L::Language::SECTION_GAP; ///< Beginn der Rubrik serielle Logs

    // Info-Knöpfe in der Fußzeile
    constexpr int FOOTER_ACTION_BTN_MIN_W = L::Footer::ACTION_MIN_W; ///< Mindestbreite eines Textknopfes im Footer
    constexpr int FOOTER_ACTION_BTN_GAP = L::Footer::ACTION_GAP;     ///< Abstand zwischen Textknöpfen im Footer

    // PIN-Punkte
    constexpr int PIN_DOT_R = L::Security::DOT_R;      ///< Radius eines PIN-Punktes
    constexpr int PIN_DOT_GAP = L::Security::DOT_STEP; ///< Abstand zwischen PIN-Punkten

    // Fußzeile und Buttons
    constexpr int FOOTER_CY = L::Footer::CY;         ///< Vertikale Mitte der Fußzeile
    constexpr int BTN_H = L::Footer::BUTTON_H;       ///< Standard-Button-Höhe
    constexpr int BTN_R = L::Style::BUTTON_R;        ///< Standard-Button-Eckradius
    constexpr int SAVE_BTN_W = L::Footer::SAVE_W;    ///< Breite des Speichern-Knopfes

    constexpr int EXIT_BTN_SIZE = L::Footer::EXIT;   ///< Kantenlänge des Exit-Knopfes
    constexpr int EXIT_BTN_CX = L::Footer::EXIT_CX;  ///< X-Mitte des Exit-Knopfes
    constexpr int EXIT_BTN_CY = FOOTER_CY;           ///< Y-Mitte des Exit-Knopfes

    constexpr int CLOCK_W = L::Footer::CLOCK_W;      ///< Uhr-Sprite-Breite
    constexpr int CLOCK_H = L::Footer::CLOCK_H;      ///< Uhr-Sprite-Höhe
    constexpr int CLOCK_PUSH_X = L::Footer::CLOCK_X; ///< X-Startposition der Uhr
    constexpr int CLOCK_PUSH_Y = L::Footer::CLOCK_Y; ///< Y-Startposition der Uhr

    // Zeit-Panel: drei Wahl-Zeilen oben, fünf Stepper-Spalten plus Set-Knopf unten
    constexpr int TIME_FIELD_H = L::Time::FIELD_H;           ///< Höhe einer Wahl-Zeile
    constexpr int TIME_FIELD_GAP = L::Time::FIELD_GAP;       ///< Abstand zwischen Wahl-Zeilen
    constexpr int TIME_FIELD_PAD_X = L::Time::FIELD_PAD_X;   ///< Innenabstand der Wahl-Zeile links/rechts
    constexpr int TIME_FIELD_LABEL_Y = L::Time::LABEL_Y;     ///< Y der Beschriftung relativ zum Zeilen-Anfang
    constexpr int TIME_FIELD_VALUE_Y = L::Time::VALUE_Y;     ///< Y des Wertes relativ zum Zeilen-Anfang
    constexpr int TIME_FIELD_COUNT = L::Time::FIELD_COUNT;   ///< Zeitzone, NTP-Server, Sync-Intervall

    constexpr int TIME_STEPPER_BLOCK_Y = L::Time::STEPPER_Y;  ///< Oberkante der Schrittgeber
    constexpr int TIME_STEPPER_COL_W = L::Time::COL_W;        ///< Breite einer Stepper-Spalte
    constexpr int TIME_STEPPER_COL_GAP = L::Time::COL_GAP;    ///< Abstand zwischen Stepper-Spalten
    constexpr int TIME_STEPPER_TOTAL_W = 5 * TIME_STEPPER_COL_W + 4 * TIME_STEPPER_COL_GAP;
    constexpr int TIME_STEPPER_BTN_H = L::Time::STEP_H;       ///< Höhe eines Stepper-Knopfes (oben/unten)
    constexpr int TIME_STEPPER_VAL_H = L::Time::VALUE_H;      ///< Höhe der Wert-Anzeige in der Mitte
    constexpr int TIME_STEPPER_VAL_GAP = L::Time::VALUE_GAP;  ///< Abstand zwischen Knopf und Wert
    constexpr int TIME_STEPPER_TOTAL_H = L::Time::TOTAL_H;    ///< Höhe einer Stepper-Spalte
    constexpr int TIME_SET_BTN_H = L::Time::SET_H;            ///< Höhe des Zeit-setzen-Knopfes
    constexpr int TIME_SET_BTN_W = L::Time::SET_W;            ///< Breite des Zeit-setzen-Knopfes

    // Zeitfenster-Panel: 7 Wochentage x 24 volle Stunden
    constexpr int TIME_WINDOW_DAYS = 7;   ///< Anzahl Wochentage
    constexpr int TIME_WINDOW_HOURS = 24; ///< Anzahl voller Stunden
    constexpr int TIME_WINDOW_DAY_LABEL_W = L::Window::DAY_LABEL_W; ///< Breite der Wochentags-Spalte
    constexpr int TIME_WINDOW_GRID_W = CONTENT_W - TIME_WINDOW_DAY_LABEL_W; ///< Breite der Stunden-Spalten
    constexpr int TIME_WINDOW_CELL_H = L::Window::CELL_H;           ///< Höhe einer Wochentags-Zeile
    constexpr int TIME_WINDOW_CHECK_SIZE = L::Window::CHECK;        ///< Kantenlänge einer Checkbox
    constexpr int TIME_WINDOW_HEADER_H = L::Window::HEADER_H;       ///< Höhe der Stunden-Kopfzeile
    constexpr int TIME_WINDOW_TOP = CONTENT_Y + L::Window::TOP;     ///< Obere Kante der Matrix
    constexpr int TIME_WINDOW_DAY_TEXT_X = L::Window::DAY_TEXT_X;   ///< Texteinzug der Wochentagskürzel
    constexpr int TIME_WINDOW_HOUR_LABEL_BUF = 3; ///< Puffergröße für zweistellige Stunden
    constexpr int TIME_WINDOW_FRAME_R = L::Window::FRAME_R;         ///< Eckradius des Matrix-Rahmens
    constexpr int TIME_WINDOW_INNER_R = L::Window::INNER_R;         ///< Eckradius der Innenfläche
    constexpr int TIME_WINDOW_FRAME_INSET = L::Window::FRAME_INSET; ///< Innenversatz der Matrix-Füllung
    constexpr int TIME_WINDOW_FRAME_PAD_Y = L::Window::FRAME_PAD_Y; ///< Vertikaler Außenabstand des Matrix-Rahmens
    constexpr int TIME_WINDOW_CHECK_LINE_W = L::Window::TICK_W;     ///< Strichbreite des Hakens
    constexpr int TIME_WINDOW_HINT_GAP_Y = L::Window::HINT_GAP_Y;   ///< Abstand der Erklärung unter der Matrix
    constexpr int TIME_WINDOW_HINT_LINE_H = L::Window::HINT_LINE_H; ///< Zeilenhöhe der Erklärung
    constexpr int TIME_WINDOW_HINT_X = CONTENT_X + L::Window::HINT_X; ///< Linke Textkante der Erklärung
    constexpr uint32_t TIME_WINDOW_FULL_MASK = 0x00FFFFFFUL; ///< Alle 24 Stunden aktiv

    // Klang-Panel: fünf Klangwerte und Lautsprecher-Freigabe, je Label links, dann Minus, Wert, Plus
    constexpr int AUDIO_ROW_COUNT = 5;       ///< Tiefen, Mitten, Höhen, Basisbreite, Loudness
    constexpr int AUDIO_WIDTH_ROW = 3;       ///< Zeile für die Basisbreite
    constexpr int AUDIO_ROW_H = L::Sound::ROW_H;          ///< Höhe einer Klang-Zeile
    constexpr int AUDIO_ROW_GAP = L::Sound::ROW_GAP;      ///< Abstand zwischen den Zeilen
    constexpr int AUDIO_ROW_FIRST_Y = L::Sound::FIRST_Y;  ///< Y der ersten Zeile relativ zum Inhalt
    constexpr int AUDIO_STEP_BTN_W = L::Sound::STEP_W;    ///< Breite eines Plus/Minus-Knopfes
    constexpr int AUDIO_STEP_VAL_W = L::Sound::VALUE_W;   ///< Breite der Wert-Anzeige in der Mitte
    constexpr int AUDIO_STEP_GAP = L::Sound::STEP_GAP;    ///< Abstand zwischen Knopf und Wert
    constexpr int AUDIO_FREQ_STEP_HZ = 100;  ///< Schrittweite eines Tastendrucks in Hz
    constexpr int AUDIO_WIDTH_STEP_PCT = 10; ///< Schrittweite der Basisbreite in Prozent
    constexpr int AUDIO_LOUDNESS_STEP_PCT = 10; ///< Schrittweite der Loudness in Prozent

    // Eckfrequenz-Grenzen je Band in Hz (konservativ)
    constexpr uint16_t AUDIO_LOW_MIN = 100; ///< Ab 100 Hz ergibt sich ein echtes Bass-Shelf, das Stimmen unangetastet lässt
    constexpr uint16_t AUDIO_LOW_MAX = 800;
    constexpr uint16_t AUDIO_PEAK_MIN = 1500;
    constexpr uint16_t AUDIO_PEAK_MAX = 6000;
    constexpr uint16_t AUDIO_HIGH_MIN = 4000;
    constexpr uint16_t AUDIO_HIGH_MAX = 12000;
    constexpr uint16_t AUDIO_WIDTH_MIN = 0;
    constexpr uint16_t AUDIO_WIDTH_MAX = 200;
    constexpr uint16_t AUDIO_LOUDNESS_MIN = 0;
    constexpr uint16_t AUDIO_LOUDNESS_MAX = 100;

    // Klang-Footer: Hörprobe-Knopf und Lautstärke (Play, Minus, DotMatrix, Plus), Block rechtsbündig vor der Uhr
    constexpr int AUDIO_FT_BTN_SIZE = L::SoundFooter::BUTTON;   ///< Quadratische Footer-Knöpfe
    constexpr int AUDIO_FT_BTN_R = BTN_R;                       ///< Eckradius der Footer-Knöpfe
    constexpr int AUDIO_FT_VOL_STEP = 5;     ///< Schrittweite der Lautstärke pro Tastendruck
    constexpr int AUDIO_FT_DISP_W = L::SoundFooter::DISP_W;     ///< Breite der DotMatrix-Fläche
    constexpr int AUDIO_FT_DISP_H = L::SoundFooter::DISP_H;     ///< Höhe der Zahl
    constexpr int AUDIO_FT_FRAME_W = L::SoundFooter::FRAME_W;   ///< Breite des Zahlenrahmens
    constexpr int AUDIO_FT_PLAY_CX = L::SoundFooter::PLAY_CX;   ///< Play-Knopf-Mitte
    constexpr int AUDIO_FT_VDOWN_CX = L::SoundFooter::DOWN_CX;  ///< Vol-Minus-Mitte
    constexpr int AUDIO_FT_FRAME_X = L::SoundFooter::FRAME_X;   ///< Zahlenrahmen linke Kante
    constexpr int AUDIO_FT_DISP_X = L::SoundFooter::DISP_X;     ///< DotMatrix linke Kante
    constexpr int AUDIO_FT_VUP_CX = L::SoundFooter::UP_CX;      ///< Vol-Plus-Mitte

    // Anzeige-Panel: zwei Abschnitte, oben die Röhrenwahl mit Vorschau, unten das VU-Meter.
    // Alle Y-Werte sind Abstände zur Oberkante des Inhaltsbereichs.
    constexpr int APPEARANCE_HINT_H = L::Appearance::HINT_H;           ///< Zeilenhöhe von ComfortaaBold24
    constexpr int APPEARANCE_SEC_LINE_DY = L::Appearance::SEC_LINE_DY; ///< Trennlinie unter dem Text der Überschrift
    constexpr int APPEARANCE_SEC1_Y = L::Appearance::SEC1_Y;           ///< Überschrift des ersten Abschnitts
    constexpr int APPEARANCE_TILE_Y = L::Appearance::TILE_Y;           ///< Obere Kante der Röhrenkacheln
    constexpr int APPEARANCE_TILE_W = L::Appearance::TILE_W;           ///< Breite einer Röhrenkachel
    constexpr int APPEARANCE_TILE_H = L::Appearance::TILE_H;           ///< Höhe einer Röhrenkachel
    constexpr int APPEARANCE_TILE_GAP = L::Appearance::TILE_GAP;       ///< Abstand zwischen den Kacheln
    constexpr int APPEARANCE_EYE_BOX = L::Appearance::EYE_BOX;         ///< Kantenlänge der Röhrenvorschau in Pixeln
    constexpr int APPEARANCE_HINT1_Y = L::Appearance::HINT1_Y;         ///< Hinweiszeile des ersten Abschnitts
    constexpr int APPEARANCE_SEC2_Y = L::Appearance::SEC2_Y;           ///< Überschrift des zweiten Abschnitts
    constexpr int APPEARANCE_ROW1_Y = L::Appearance::ROW1_Y;           ///< Zeile der Zeigerträgheit
    constexpr int APPEARANCE_ROW2_Y = L::Appearance::ROW2_Y;           ///< Zeile des Bezugspegels
    constexpr int APPEARANCE_ROW_H = L::Appearance::ROW_H;             ///< Höhe einer Wahlzeile
    constexpr int APPEARANCE_HINT_DY = L::Appearance::HINT_DY;         ///< Abstand der Hinweiszeile unter ihrer Wahlzeile

    // Der Inhaltsbereich endet ohnehin 15 Bildpunkte über der Panelkante. Endet die letzte
    // Hinweiszeile genau auf seiner Unterkante, stimmt dieser Abstand. Der Rest verteilt sich
    // auf die Lücken darüber. Wer eine Zeile verschiebt, muss die Rechnung wieder aufgehen lassen.
    static_assert(APPEARANCE_ROW2_Y + APPEARANCE_HINT_DY + APPEARANCE_HINT_H == CONTENT_H,
                  "Die letzte Hinweiszeile muss genau auf der Unterkante des Inhalts enden");
    constexpr int APPEARANCE_LABEL_W = L::Appearance::LABEL_W;  ///< Breite der Beschriftung links neben den Segmenten
    constexpr int APPEARANCE_SEG_GAP = L::Appearance::SEG_GAP;  ///< Abstand zwischen zwei Segmenten
    constexpr int APPEARANCE_LEVEL_W = L::Appearance::LEVEL_W;  ///< Breite der Pegelanzeige rechts in der Abschnittszeile
    constexpr unsigned long APPEARANCE_LEVEL_MS = 1000; ///< Auffrischung der Pegelanzeige
    constexpr float APPEARANCE_LEVEL_SMOOTH = 0.05f;    ///< Glättung je Durchlauf, ergibt rund eine Sekunde
    constexpr uint8_t APPEARANCE_EYE_MIN = 0;   ///< Kleinster Stilwert des magischen Auges (EM11)
    constexpr uint8_t APPEARANCE_EYE_MAX = 3;   ///< Größter Stilwert des magischen Auges (EM84), passend zur lokalen Aufzählung MagicEyeStyle
    constexpr uint8_t APPEARANCE_VU_MIN = 0;    ///< Kleinste Trägheit
    constexpr uint8_t APPEARANCE_VU_MAX = 2;    ///< Größte Trägheit, muss zur Aufzählung VuBallistics in include/Visualizer.h passen
    constexpr uint8_t APPEARANCE_SENS_MIN = 0;  ///< Kleinster Bezugspegel
    constexpr uint8_t APPEARANCE_SENS_MAX = 4;  ///< Größter Bezugspegel, muss zur Tabelle VU_ALIGNMENT_DB in src/Visualizer.cpp passen

    constexpr int MAX_PROFILES = 3; ///< Gleich Settings::ProfileCount

    // Bluetooth-Seite
    constexpr uint32_t BT_SEARCH_TIMEOUT_MS = 15000;  ///< Der Pico beendet eine Suche spätestens nach 9,96 s. Danach gilt sie als gescheitert
    constexpr uint32_t BT_CONNECT_TIMEOUT_MS = 25000; ///< Der Pico meldet einen Verbindungsversuch spätestens nach rund 20 s als gescheitert
    constexpr int16_t BT_RSSI_UNKNOWN = -100;         ///< Signalstärke in der Geräteliste, wenn die Suche keine lieferte. Erscheint als 0 %

    /** @brief Spalten des Schrittgeberblocks der Zeitseite in Konfigurationsreihenfolge. */
    enum
    {
        FIELD_YEAR,
        FIELD_MONTH,
        FIELD_DAY,
        FIELD_HOUR,
        FIELD_MINUTE,
        FIELD_COUNT
    };

    /** @brief Synchronisationsintervalle in ms, die Reihenfolge entspricht den Schlüsseln in intervalText. */
    constexpr uint32_t Intervals[] = {86400000, 43200000, 21600000, 10800000, 3600000, 1800000, 600000, 60000};

    /** @brief Kennungen der Touchziele. Mehrfachziele belegen einen Bereich ab ihrem Basiswert (Basis + Index), Nav + Seite entspricht Settings::Panel. */
    enum ActionId
    {
        Nav = 0,
        SelectProfile = 20,
        ToggleProfile = 30,
        Scan = 40,
        Password,
        Test,
        Delete,
        Host,
        Zone = 50,
        Ntp,
        Interval,
        Manual,
        AllActive = 110,
        LanguageDe = 120,
        LanguageEn,
        SerialLogsToggle,
        PinLockToggle = 130,
        Restart = 150,
        ResetDefaults,
        LoadLatest,
        HomeAction = 160,
        SaveAction,
        DateAccept = 225,
        DateCancel,
        Cell = 300,
        PinKey = 480,
        TimePlus = 500,
        TimeMinus = 510,
        AudioPlus = 520,
        AudioMinus = 530,
        Eye = 550,
        Ballistic = 560,
        Sensitivity = 570,
        Play = 580,
        VolumeDown,
        VolumeUp,
        ScreenMinus = 590,
        ScreenPlus,
        ScreenRotation,
        ScreenAuto = 594,
        ThemeSelect = 600,
        BtToggle = 610,
        BtSearch,
        BtTone,
        BtForget,
        UkwToggle = 620,
        UkwFreqMinus,
        UkwFreqPlus,
        UkwFreqEdit,
        UkwPowerMinus,
        UkwPowerPlus
    };

    /** @brief Liest eine Frequenz in MHz mit höchstens drei Vorkommastellen und höchstens einer Nachkommastelle hinter Komma oder Punkt, etwa „101,3“, „87.5“ oder „100“. @return false bei jedem anderen Text, khz ist dann unbestimmt. */
    bool parseMhz(const char *text, uint32_t &khz)
    {
        uint32_t whole = 0, digits = 0, tenth = 0;
        const char *p = text;
        for (; *p >= '0' && *p <= '9'; ++p)
        {
            if (++digits > 3)
                return false;
            whole = whole * 10 + static_cast<uint32_t>(*p - '0');
        }
        if (!digits)
            return false;
        if (*p == ',' || *p == '.')
        {
            ++p;
            if (*p < '0' || *p > '9')
                return false;
            tenth = static_cast<uint32_t>(*p++ - '0');
        }
        if (*p)
            return false;
        khz = whole * 1000 + tenth * 100;
        return true;
    }

    using Rect = SettingsUi::Rect;
    using Phase = SettingsUi::TouchPhase;

    /** @brief Tönt eine Kategoriefarbe ab: auf dunklem Grund durch Skalieren der RGB565-Kanäle in Festkomma, mit MerkmalHellerGrund durch Mischen vom Grund zur Farbe. @param factor Anteil der Kategoriefarbe. */
    uint16_t scaleColor(uint16_t color, float factor)
    {
        if (themaAktiv().merkmale & MerkmalHellerGrund)
            return mische565(farbe(Rolle::GemGrund), color, static_cast<uint32_t>(std::max(0.0f, factor) * 255.0f + 0.5f));
        const uint32_t f = static_cast<uint32_t>(std::max(0.0f, factor) * 256.0f);
        const uint32_t r = (((color >> 11) & 31) * 256u * f) >> 16u;
        const uint32_t g = (((color >> 5) & 63) * 256u * f) >> 16u;
        const uint32_t b = ((color & 31) * 256u * f) >> 16u;
        return static_cast<uint16_t>((std::min<uint32_t>(r, 31) << 11) | (std::min<uint32_t>(g, 63) << 5) | std::min<uint32_t>(b, 31));
    }

    /** @brief Prüft, ob eine IPv4-Adresse als verbundene Adresse taugt: nicht 0.x, nicht ab 224 (Multicast), nicht Loopback und nicht Link-Local. */
    bool usableIp(const uint8_t *ip)
    {
        return ip[0] > 0 && ip[0] < 224 && ip[0] != 127 && !(ip[0] == 169 && ip[1] == 254);
    }

    /** @brief Prüft auf ASCII-Buchstaben oder -Ziffer. */
    bool alphaNumeric(unsigned char c)
    {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    }

    /** @brief Vergleicht Aktivierung, SSID und Passwort zweier Profile. */
    bool sameProfile(const Settings::Profile &a, const Settings::Profile &b)
    {
        return a.enabled == b.enabled && !strcmp(a.ssid, b.ssid) && !strcmp(a.password, b.password);
    }

    /** @brief Vergleicht die speicherbaren Klangwerte, Lautstärke und Wiedergabe gehören nicht dazu. */
    bool sameSound(const Settings::Sound &a, const Settings::Sound &b)
    {
        return a.eqLow == b.eqLow && a.eqPeak == b.eqPeak && a.eqHigh == b.eqHigh && a.width == b.width && a.loudness == b.loudness && a.speakers == b.speakers;
    }

    /** @brief Liefert die Tage des Monats m im Jahr y (2000 bis 2099), Schaltjahr bei Teilbarkeit durch 4. */
    int daysInMonth(int y, int m)
    {
        static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        return days[std::clamp(m, 1, 12) - 1] + (m == 2 && y % 4 == 0);
    }

    /** @brief Schreibt die Beschriftung eines Synchronisationsintervalls, unbekannte Werte erscheinen in Minuten. */
    void intervalText(uint32_t ms, LangIndex language, char *out, size_t size)
    {
        const OKey keys[] = {OKey::TIME_SYNC_24H, OKey::TIME_SYNC_12H, OKey::TIME_SYNC_6H, OKey::TIME_SYNC_3H, OKey::TIME_SYNC_1H, OKey::TIME_SYNC_30M, OKey::TIME_SYNC_10M, OKey::TIME_SYNC_1M};
        for (size_t i = 0; i < 8; ++i)
            if (Intervals[i] == ms)
            {
                snprintf(out, size, "%s", settingsUiText(keys[i], language));
                return;
            }
        snprintf(out, size, "%lu min", static_cast<unsigned long>(ms / 60000));
    }

    /** @brief Eine Zeile der Themenseite: gespeicherter Themenindex und übersetzter Name. */
    struct ThemeRow
    {
        uint8_t index; ///< Themenindex nach Thema.h
        OKey name;     ///< Textschlüssel des Anzeigenamens
    };

    /** @brief Zeilen der Themenseite von oben nach unten. Individuell (Index 1, intern Modern) steht als frei einstellbares Thema zuletzt, die Indizes bleiben unverändert. */
    constexpr ThemeRow ThemeRows[ThemenAnzahl] = {{ThemaRetroIndex, OKey::THEME_RETRO}, {ThemaEPaperIndex, OKey::THEME_EPAPER}, {ThemaModernIndex, OKey::THEME_CUSTOM}};

    /** @brief Zeichnet die Minivorschau eines Themas nur aus dessen Rollen: Grund mit Rahmen, links das Zifferblatt des MainScreens auf 10:10:35, rechts eine Fläche mit Schriftprobe, Akzentlinie und Pegelsegmenten. @param x,y Linke obere Ecke im Sprite. @param thema Gezeigtes Thema, unabhängig vom aktiven. */
    void drawThemePreview(UiCanvas &spr, int x, int y, const Thema &thema)
    {
        namespace T = L::Theme;
        constexpr float HOUR_MARK = 0.16f, MINOR_MARK = 0.09f;                                     ///< Markenlänge als Anteil des Radius
        constexpr float HOUR_HAND = 0.55f, MINUTE_HAND = 0.8f, SECOND_HAND = 0.92f, COUNTER = 0.22f; ///< Zeigerlängen als Anteil des Radius
        constexpr float HOUR_ANGLE = 305.0f, MINUTE_ANGLE = 60.0f, SECOND_ANGLE = 210.0f;           ///< 10:10:35 in Grad ab 12 Uhr
        constexpr int CARD_PAD = 9, ACCENT_Y = 42, ACCENT_H = 4, SEGMENTS = 5, SEGMENT_GAP = 3, SEGMENT_H = 18; ///< Innenmaße der Fläche
        const auto f = [&thema](Rolle rolle)
        {
            return farbe(thema, rolle);
        };
        spr.fillSmoothRoundRect(x, y, T::PREVIEW_W, T::PREVIEW_H, T::PREVIEW_R, f(Rolle::GemRahmen));
        spr.fillSmoothRoundRect(x + BORDER, y + BORDER, T::PREVIEW_W - 2 * BORDER, T::PREVIEW_H - 2 * BORDER, BUTTON_INNER_R, f(Rolle::GemGrund));

        const float cx = x + T::PREVIEW_H / 2.0f, cy = y + T::PREVIEW_H / 2.0f, r = T::CLOCK_R;
        const auto at = [cx, cy](float angle, float length)
        {
            const float rad = (angle - 90.0f) * 0.0174532925f;
            return std::pair<float, float>{cx + cosf(rad) * length, cy + sinf(rad) * length};
        };
        for (int hour = 0; hour < 12; ++hour)
        {
            const bool major = hour % 3 == 0;
            const auto outer = at(hour * 30.0f, r), inner = at(hour * 30.0f, r * (1.0f - (major ? HOUR_MARK : MINOR_MARK)));
            spr.drawWideLine(inner.first, inner.second, outer.first, outer.second, major ? 1.5f : 1.0f, f(major ? Rolle::MainUhrMarken : Rolle::MainUhrMarkenKlein));
        }
        const uint16_t zeiger = f(Rolle::MainUhrZeiger);
        const auto hourTip = at(HOUR_ANGLE, r * HOUR_HAND), minuteTip = at(MINUTE_ANGLE, r * MINUTE_HAND);
        spr.drawWideLine(cx, cy, hourTip.first, hourTip.second, 2.0f, zeiger);
        spr.drawWideLine(cx, cy, minuteTip.first, minuteTip.second, 1.5f, zeiger);
        spr.fillSmoothCircle(cx, cy, 4, f(Rolle::MainUhrAchse));
        const auto secondTip = at(SECOND_ANGLE, r * SECOND_HAND), counter = at(SECOND_ANGLE + 180.0f, r * COUNTER);
        spr.drawWedgeLine(counter.first, counter.second, secondTip.first, secondTip.second, 1.0f, 0.5f, f(Rolle::MainUhrSekunde));
        spr.fillSmoothCircle(cx, cy, 3, f(Rolle::MainUhrAchseAkzent));

        // Die Fläche beginnt rechts neben dem Zifferblatt und schließt oben und unten mit ihm ab.
        const int cardX = x + T::PREVIEW_H / 2 + T::CLOCK_R + T::CARD_INSET, cardY = y + T::CARD_INSET;
        const int cardW = x + T::PREVIEW_W - T::CARD_INSET - cardX, cardH = T::PREVIEW_H - 2 * T::CARD_INSET;
        spr.fillSmoothRoundRect(cardX, cardY, cardW, cardH, BUTTON_INNER_R, f(Rolle::GemFlaeche));
        spr.loadFont(ComfortaaBold24);
        spr.setTextDatum(TL_DATUM);
        spr.setTextColor(f(Rolle::GemText));
        spr.drawString("Aa", cardX + CARD_PAD, cardY + CARD_PAD);
        spr.unloadFont();
        const int innerW = cardW - 2 * CARD_PAD;
        spr.fillRect(cardX + CARD_PAD, cardY + ACCENT_Y, innerW, ACCENT_H, f(Rolle::GemAkzent));
        const Rolle pegel[SEGMENTS] = {Rolle::VisPegelGruen, Rolle::VisPegelGruen, Rolle::VisPegelGruen, Rolle::VisPegelOrange, Rolle::VisPegelRot};
        const int segmentW = (innerW - (SEGMENTS - 1) * SEGMENT_GAP) / SEGMENTS;
        for (int i = 0; i < SEGMENTS; ++i)
            spr.fillRect(cardX + CARD_PAD + i * (segmentW + SEGMENT_GAP), cardY + cardH - CARD_PAD - SEGMENT_H, segmentW, SEGMENT_H, f(pegel[i]));
    }
}

bool Settings::bounded(const Configuration &config)
{
    for (const auto &p : config.profiles)
        if (!SettingsUi::validText(p.ssid, sizeof(p.ssid)) || !SettingsUi::validText(p.password, sizeof(p.password)) || (p.enabled && !p.ssid[0]))
            return false;
    if (!SettingsUi::validText(config.hostname, sizeof(config.hostname)) || !SettingsUi::validText(config.ntpServer, sizeof(config.ntpServer)) ||
        !SettingsUi::validText(config.timezone, sizeof(config.timezone)) || !SettingsUi::validText(config.timezonePosix, sizeof(config.timezonePosix)))
        return false;
    if (config.language != LangIndex::DE && config.language != LangIndex::EN)
        return false;
    if (config.eyeStyle > 3 || config.vuBallistics > 2 || config.vuSensitivity > 4)
        return false;
    if (config.brightness < BrightnessMin || config.brightness > BrightnessMax || (config.rotation != RotationNormal && config.rotation != RotationFlipped))
        return false;
    if (config.theme >= ThemenAnzahl)
        return false;
    if (!BtProtocol::validUkwKhz(config.ukwKhz) || config.ukwPower > UkwMaxPower)
        return false;
    for (uint32_t day : config.timeWindows)
        if (day & ~DayMask)
            return false;
    return true;
}

bool Settings::setConfiguration(const Configuration &config, uint64_t revision)
{
    if (!bounded(config) || (m_loaded && revision < m_latestRevision))
        return false;
    if (m_loaded && revision == m_latestRevision)
        return true;
    // Wie im Original endet eine angefangene Bestätigung, wenn die Sperre anderswo umgeschaltet wurde.
    if (m_loaded && m_confirmPinDisable && config.pinLock != m_latest.pinLock)
    {
        m_confirmPinDisable = false;
        resetPinEntry();
    }
    m_latest = config;
    m_latestRevision = revision;
    if (m_loaded && (changedFields() || m_pendingId || m_modal != Modal::None))
    {
        m_conflict = true;
        m_notice = SettingsKey::RevisionChanged;
        redraw();
        return true;
    }
    m_baseline = m_draft = config;
    m_revision = revision;
    m_loaded = true;
    m_conflict = false;
    for (bool &tested : m_tested)
        tested = false;
    m_notice = SettingsKey::Unchanged;
    redraw();
    return true;
}

bool Settings::setStatus(const Status &status)
{
    if (m_haveStatus && status.revision <= m_status.revision)
        return false;
    if (!SettingsUi::validText(status.device, sizeof(status.device)) || !SettingsUi::validText(status.firmware, sizeof(status.firmware)) ||
        !SettingsUi::validText(status.connectedSsid, sizeof(status.connectedSsid)) || !SettingsUi::validText(status.mac, sizeof(status.mac)) ||
        !SettingsUi::validText(status.i2cInternalList, sizeof(status.i2cInternalList)) || !SettingsUi::validText(status.i2cPortAList, sizeof(status.i2cPortAList)))
        return false;
    if (status.timeValid && !validDate(status.localTime))
        return false;
    const bool changed = !m_haveStatus ||
                         // Laufzeit (minutengenau angezeigt) und Signalstärke stehen nur auf der Info-Seite, nativ kostet jedes Neuzeichnen der Seite spürbar Zeit.
                         (m_panel == Panel::Info && (status.uptimeSeconds / 60 != m_status.uptimeSeconds / 60 || status.rssi != m_status.rssi || status.ftpEnabled != m_status.ftpEnabled)) ||
                         status.timeValid != m_status.timeValid || status.ntpConfirmed != m_status.ntpConfirmed ||
                         status.localTime.year != m_status.localTime.year || status.localTime.month != m_status.localTime.month || status.localTime.day != m_status.localTime.day || status.localTime.hour != m_status.localTime.hour || status.localTime.minute != m_status.localTime.minute ||
                         status.wifiConnected != m_status.wifiConnected || status.signalValid != m_status.signalValid ||
                         status.pinKnown != m_status.pinKnown || status.pinSet != m_status.pinSet ||
                         memcmp(status.ipv4, m_status.ipv4, 4) || strcmp(status.connectedSsid, m_status.connectedSsid) || strcmp(status.mac, m_status.mac) ||
                         strcmp(status.device, m_status.device) || strcmp(status.firmware, m_status.firmware) || status.flashBytes != m_status.flashBytes || status.psramBytes != m_status.psramBytes ||
                         status.i2cInternal != m_status.i2cInternal || status.i2cPortA != m_status.i2cPortA || strcmp(status.i2cInternalList, m_status.i2cInternalList) || strcmp(status.i2cPortAList, m_status.i2cPortAList);
    if (!m_soundLoaded)
    {
        m_soundBaseline = m_soundDraft = status.sound;
        m_soundLoaded = true;
    }
    m_soundDraft.volume = status.sound.volume;
    m_soundDraft.playing = status.sound.playing;
    const bool clockChanged = !m_haveStatus || status.second != m_status.second || status.localTime.minute != m_status.localTime.minute;
    m_status = status;
    m_haveStatus = true;
    if (m_modal != Modal::None)
        return true;
    if (clockChanged || m_panel == Panel::Sound || m_panel == Panel::Appearance)
        invalidateFooter();
    if (changed && (m_panel == Panel::Info || m_panel == Panel::Time || m_panel == Panel::Security || m_panel == Panel::Wifi))
        redraw();
    return true;
}

uint32_t Settings::changedFields() const
{
    if (!m_loaded)
        return 0;
    uint32_t fields = 0;
    for (size_t i = 0; i < ProfileCount; ++i)
        if (!sameProfile(m_baseline.profiles[i], m_draft.profiles[i]))
            fields |= 1u << i;
    if (strcmp(m_baseline.hostname, m_draft.hostname))
        fields |= Hostname;
    if (strcmp(m_baseline.ntpServer, m_draft.ntpServer) || strcmp(m_baseline.timezone, m_draft.timezone) || strcmp(m_baseline.timezonePosix, m_draft.timezonePosix) || m_baseline.syncIntervalMs != m_draft.syncIntervalMs)
        fields |= Time;
    for (size_t i = 0; i < 7; ++i)
        if (m_baseline.timeWindows[i] != m_draft.timeWindows[i])
            fields |= Windows;
    if (m_baseline.language != m_draft.language)
        fields |= Language;
    if (m_baseline.eyeStyle != m_draft.eyeStyle || m_baseline.vuBallistics != m_draft.vuBallistics || m_baseline.vuSensitivity != m_draft.vuSensitivity)
        fields |= Appearance;
    if (m_baseline.brightness != m_draft.brightness || m_baseline.rotation != m_draft.rotation || m_baseline.autoRotate != m_draft.autoRotate)
        fields |= Screen;
    if (m_baseline.theme != m_draft.theme)
        fields |= Theme;
    if (m_baseline.ukwEnabled != m_draft.ukwEnabled || m_baseline.ukwKhz != m_draft.ukwKhz || m_baseline.ukwPower != m_draft.ukwPower)
        fields |= Ukw;
    if (m_baseline.serialLogs != m_draft.serialLogs)
        fields |= SerialLogs;
    if (m_baseline.pinLock != m_draft.pinLock)
        fields |= PinLock;
    if (pinPending())
        fields |= Pin;
    if (m_soundLoaded && !sameSound(m_soundDraft, m_soundBaseline))
        fields |= SoundField;
    return fields;
}

uint32_t Settings::nextId()
{
    if (++m_nextId == 0)
        ++m_nextId;
    return m_nextId;
}

bool Settings::enqueue(Request &request)
{
    if (m_queueCount == QueueSize)
    {
        m_notice = SettingsKey::QueueBusy;
        redraw();
        return false;
    }
    request.id = nextId();
    m_queue[(m_queueHead + m_queueCount) % QueueSize] = request;
    ++m_queueCount;
    return true;
}

bool Settings::takeRequest(Request &request)
{
    if (m_muteId)
    {
        request = Request{};
        request.type = RequestType::Mute;
        request.id = m_muteId;
        m_muteId = 0;
        return true;
    }
    if (!m_queueCount)
        return false;
    request = m_queue[m_queueHead];
    m_queue[m_queueHead] = Request{};
    m_queueHead = (m_queueHead + 1) % QueueSize;
    --m_queueCount;
    return true;
}

SettingsKey Settings::errorText(Error error)
{
    switch (error)
    {
    case Error::Unavailable:
        return SettingsKey::Missing;
    case Error::Busy:
        return SettingsKey::QueueBusy;
    case Error::Invalid:
        return SettingsKey::Invalid;
    case Error::Conflict:
        return SettingsKey::Conflict;
    case Error::Authentication:
        return SettingsKey::AuthError;
    case Error::NoNetwork:
        return SettingsKey::NoNetwork;
    case Error::NoDhcp:
        return SettingsKey::NoDhcp;
    case Error::Timeout:
        return SettingsKey::Timeout;
    case Error::Storage:
        return SettingsKey::StorageError;
    default:
        return SettingsKey::ServiceError;
    }
}

bool Settings::completeScan(uint32_t id, Outcome outcome, const WifiList::Network *networks, size_t count, bool more, Error error)
{
    if (!id || id != m_scanId || m_modal != Modal::Networks)
        return false;
    if (outcome == Outcome::Running)
        return true;
    m_scanId = 0;
    if (outcome == Outcome::Success)
    {
        if (!m_networks.setNetworks(networks, count, more))
            m_networks.fail(SettingsKey::Invalid);
    }
    else
        m_networks.fail(outcome == Outcome::Cancelled ? SettingsKey::Cancelled : errorText(error));
    redraw();
    return true;
}

bool Settings::complete(const Completion &completion)
{
    if (!completion.id)
        return false;
    if (completion.type >= RequestType::BtSearch && completion.type <= RequestType::BtTone)
        return completeBluetooth(completion);
    if (completion.id == m_previewId && completion.type == m_previewType)
    {
        m_previewId = 0;
        if (completion.outcome != Outcome::Success)
            notice(errorText(completion.error));
        return true;
    }
    const bool mute = completion.type == RequestType::Mute && completion.id == m_lastMuteId;
    const bool navigation = completion.type == m_lastNavigationType && completion.id == m_lastNavigationId;
    if (mute || navigation)
    {
        if (completion.outcome == Outcome::Running)
            return true;
        if (mute)
            m_lastMuteId = 0;
        else
            m_lastNavigationId = 0;
        if (completion.outcome == Outcome::Success)
            m_notice = mute ? SettingsKey::MuteConfirmed : SettingsKey::Ok;
        else
            m_notice = completion.outcome == Outcome::Cancelled ? SettingsKey::Cancelled : errorText(completion.error);
        // Das Ergebnis erscheint nur in der Fußzeile, damit es einen laufenden Test oder eine PIN-Eingabe nicht durch ein zweites Modal ersetzt
        invalidateFooter();
        return true;
    }
    if (completion.id != m_pendingId || completion.type != m_pendingType || completion.type == RequestType::Save)
        return false;
    if (completion.outcome == Outcome::Running)
        return true;
    const RequestType kind = m_pendingType;
    m_pendingId = 0;
    m_pendingType = RequestType::None;
    closeModal();
    if (completion.outcome != Outcome::Success)
    {
        notice(completion.outcome == Outcome::Cancelled ? SettingsKey::Cancelled : errorText(completion.error));
        return true;
    }
    if (kind == RequestType::TestProfile)
    {
        if (!usableIp(completion.ipv4))
        {
            notice(SettingsKey::NoDhcp);
            return true;
        }
        m_tested[m_profile] = true;
        m_draft.profiles[m_profile].enabled = true;
        edited();
        char ip[32];
        snprintf(ip, sizeof(ip), "%u.%u.%u.%u", completion.ipv4[0], completion.ipv4[1], completion.ipv4[2], completion.ipv4[3]);
        notice(SettingsKey::TestSuccess, ip);
    }
    else if (kind == RequestType::ChangePin || kind == RequestType::RemovePin)
    {
        m_status.pinKnown = true;
        m_status.pinSet = kind == RequestType::ChangePin;
        notice(SettingsKey::PinDone);
    }
    else if (kind == RequestType::SetTime)
        notice(SettingsKey::TimeSet);
    else
    {
        m_notice = SettingsKey::Ok;
        redraw();
    }
    return true;
}

bool Settings::completeSave(uint32_t id, bool success, const Configuration &stored, uint64_t revision, Error error, bool restartRequired)
{
    if (!id || id != m_pendingId || (m_pendingType != RequestType::Save && m_pendingType != RequestType::ResetDefaults))
        return false;
    const AfterSave after = m_afterSave;
    m_pendingId = 0;
    m_pendingType = RequestType::None;
    m_afterSave = AfterSave::None;
    if (!success || !bounded(stored) || revision <= m_revision)
    {
        if (error == Error::Conflict)
            m_conflict = true;
        // Der Log-Schalter zeigt wie im Original weiter den gespeicherten Zustand, der Logger wurde nicht umgeschaltet.
        if (m_savedFields == SerialLogs)
            m_draft.serialLogs = m_baseline.serialLogs;
        if (m_savedFields == PinLock)
        {
            m_draft.pinLock = m_baseline.pinLock;
            // Eine falsche PIN meldet wie im Original der Hinweis über den Punkten, die Seite wartet weiter auf die PIN.
            if (error == Error::Authentication)
            {
                m_confirmPinDisable = m_pinMismatch = true;
                redraw();
                return true;
            }
        }
        notice(success ? SettingsKey::StorageError : errorText(error), error == Error::Conflict ? tr(SettingsKey::ConflictHelp) : "");
        return true;
    }
    if (revision >= m_latestRevision)
    {
        m_latest = stored;
        m_latestRevision = revision;
    }
    if (m_savedFields & Pin)
        memset(m_pinPending, 0, sizeof(m_pinPending));
    if (m_savedFields & SoundField)
        m_soundBaseline = m_soundDraft;
    for (size_t i = 0; i < 3; ++i)
        if (m_savedFields & (1u << i))
            m_draft.profiles[i] = stored.profiles[i];
    m_baseline = m_latest;
    m_revision = m_latestRevision;
    m_conflict = false;
    m_notice = restartRequired ? SettingsKey::RestartRequired : SettingsKey::Saved;
    redraw();
    if (m_visible && after != AfterSave::None)
        action(after == AfterSave::Home ? RequestType::Home : RequestType::Restart);
    if (m_visible && after == AfterSave::None)
    {
        if (m_savedFields & 15)
            ask(Purpose::SavedWifi, SettingsKey::Saved, ot(OKey::SAVED_MSG));
        else if (m_savedFields != SerialLogs && m_savedFields != PinLock) // Log-Schalter und PIN-Sperre wirken still wie im Original, ihre Stellung zeigt das Ergebnis
            notice(SettingsKey::Saved);
    }
    return true;
}

bool Settings::normalizePassword(const char *input, char (&output)[65])
{
    if (!input)
        return false;
    const size_t length = strnlen(input, 65);
    if (length > 64)
        return false;
    if (!length)
    {
        output[0] = 0;
        return true;
    }
    if (length == 64)
    {
        for (size_t i = 0; i < length; ++i)
        {
            const char c = input[i];
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
                return false;
        }
        for (size_t i = 0; i < length; ++i)
            output[i] = input[i] >= 'A' && input[i] <= 'F' ? input[i] - 'A' + 'a' : input[i];
        output[length] = 0;
        return true;
    }
    if (length < 8)
        return false;
    for (size_t i = 0; i < length; ++i)
        if (static_cast<unsigned char>(input[i]) < 0x20 || static_cast<unsigned char>(input[i]) > 0x7e)
            return false;
    stringutils::safeCopy(output, input);
    return true;
}

bool Settings::validHostname(const char *name)
{
    if (!name)
        return false;
    const size_t length = strnlen(name, 33);
    if (!length || length > 32 || name[0] == '-' || name[length - 1] == '-')
        return false;
    for (size_t i = 0; i < length; ++i)
        if (!alphaNumeric(static_cast<unsigned char>(name[i])) && name[i] != '-')
            return false;
    return true;
}

bool Settings::validServer(const char *name)
{
    if (!name)
        return false;
    const size_t length = strnlen(name, 64);
    if (!length || length > 63 || name[0] == '.' || name[length - 1] == '.' || name[0] == '-' || name[length - 1] == '-')
        return false;
    for (size_t i = 0; i < length; ++i)
    {
        if (!alphaNumeric(static_cast<unsigned char>(name[i])) && name[i] != '-' && name[i] != '.')
            return false;
        if (i && name[i] == '.' && (name[i - 1] == '.' || name[i - 1] == '-'))
            return false;
        if (i && name[i] == '-' && name[i - 1] == '.')
            return false;
    }
    return true;
}

bool Settings::validDate(const LocalTime &time)
{
    if (time.year < 2000 || time.year > 2099 || time.month < 1 || time.month > 12 || time.day < 1 || time.hour > 23 || time.minute > 59)
        return false;
    const uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const uint8_t maxDay = static_cast<uint8_t>(days[time.month - 1] + (time.month == 2 && time.year % 4 == 0));
    return time.day <= maxDay;
}

bool Settings::validateDraft()
{
    const uint32_t fields = changedFields();
    if (!m_loaded || !m_capabilities.save)
    {
        notice(SettingsKey::Missing);
        return false;
    }
    if (m_conflict)
    {
        notice(SettingsKey::Conflict, tr(SettingsKey::ConflictHelp));
        return false;
    }
    if ((fields & 15u) && !m_capabilities.wifiProfiles)
    {
        notice(SettingsKey::Missing);
        return false;
    }
    if ((fields & Time) && !m_capabilities.time)
    {
        notice(SettingsKey::Missing);
        return false;
    }
    if ((fields & Windows) && !m_capabilities.timeWindows)
    {
        notice(SettingsKey::Missing);
        return false;
    }
    if ((fields & Language) && !m_capabilities.language)
    {
        notice(SettingsKey::Missing);
        return false;
    }
    if ((fields & Appearance) && !m_capabilities.appearance)
    {
        notice(SettingsKey::Missing);
        return false;
    }
    if ((fields & Screen) && !m_capabilities.screen)
    {
        notice(SettingsKey::Missing);
        return false;
    }
    if ((fields & Theme) && !m_capabilities.theme)
    {
        notice(SettingsKey::Missing);
        return false;
    }
    if ((fields & Pin) && !m_capabilities.pinChange)
    {
        notice(SettingsKey::Missing);
        return false;
    }
    if ((fields & SoundField) && !m_capabilities.sound)
    {
        notice(SettingsKey::Missing);
        return false;
    }
    for (size_t i = 0; i < ProfileCount; ++i)
        if (fields & (1u << i))
        {
            const auto &p = m_draft.profiles[i];
            char normalized[65] = {};
            if (!SettingsUi::validText(p.ssid, sizeof(p.ssid)) || (!p.ssid[0] && (p.enabled || p.password[0])))
            {
                notice(SettingsKey::InvalidSsid);
                return false;
            }
            const bool valid = normalizePassword(p.password, normalized);
            memset(normalized, 0, sizeof(normalized));
            if (!valid)
            {
                notice(SettingsKey::InvalidPassword);
                return false;
            }
        }
    if ((fields & Hostname) && !validHostname(m_draft.hostname))
    {
        notice(SettingsKey::InvalidHostname);
        return false;
    }
    if (fields & Time)
    {
        if (!validServer(m_draft.ntpServer))
        {
            notice(SettingsKey::InvalidNtp);
            return false;
        }
        if (!m_draft.timezone[0] || !m_draft.timezonePosix[0] || m_draft.syncIntervalMs < 60000 || m_draft.syncIntervalMs > 86400000)
        {
            notice(SettingsKey::Invalid);
            return false;
        }
    }
    return true;
}

void Settings::edited()
{
    m_notice = changedFields() ? SettingsKey::NotSaved : SettingsKey::Unchanged;
    m_contact.cancel();
    redraw();
}

void Settings::closeModal()
{
    m_modal = Modal::None;
    m_progress.finish();
    m_keyboard.close();
    m_message.close();
    m_contact.cancel();
    m_networks.cancelTouch();
    m_touchOwner = 0;
    redraw();
}

void Settings::notice(SettingsKey key, const char *detail)
{
    m_notice = key;
    ask(Purpose::Notice, key, tr(key), detail);
}

void Settings::save(AfterSave after, uint32_t only)
{
    if (m_pendingId)
        return;
    if (!changedFields())
    {
        if (after != AfterSave::None)
            action(after == AfterSave::Home ? RequestType::Home : RequestType::Restart);
        return;
    }
    if (!validateDraft())
        return;
    Request request;
    request.type = RequestType::Save;
    request.configuration = m_draft;
    request.changedFields = changedFields() & (after == AfterSave::None ? panelFields() : UINT32_MAX) & only;
    request.baselineRevision = m_revision;
    request.configuration = m_baseline;
    const auto fields = request.changedFields;
    for (size_t i = 0; i < 3; ++i)
        if (fields & (1u << i))
            request.configuration.profiles[i] = m_draft.profiles[i];
    if (fields & Hostname)
        stringutils::safeCopy(request.configuration.hostname, m_draft.hostname);
    if (fields & Time)
    {
        stringutils::safeCopy(request.configuration.ntpServer, m_draft.ntpServer);
        stringutils::safeCopy(request.configuration.timezone, m_draft.timezone);
        stringutils::safeCopy(request.configuration.timezonePosix, m_draft.timezonePosix);
        request.configuration.syncIntervalMs = m_draft.syncIntervalMs;
    }
    if (fields & Windows)
        memcpy(request.configuration.timeWindows, m_draft.timeWindows, sizeof(m_draft.timeWindows));
    if (fields & Language)
        request.configuration.language = m_draft.language;
    if (fields & Appearance)
    {
        request.configuration.eyeStyle = m_draft.eyeStyle;
        request.configuration.vuBallistics = m_draft.vuBallistics;
        request.configuration.vuSensitivity = m_draft.vuSensitivity;
    }
    if (fields & Screen)
    {
        request.configuration.brightness = m_draft.brightness;
        request.configuration.rotation = m_draft.rotation;
        request.configuration.autoRotate = m_draft.autoRotate;
    }
    if (fields & Theme)
        request.configuration.theme = m_draft.theme;
    if (fields & Ukw)
    {
        request.configuration.ukwEnabled = m_draft.ukwEnabled;
        request.configuration.ukwKhz = m_draft.ukwKhz;
        request.configuration.ukwPower = m_draft.ukwPower;
    }
    if (fields & SerialLogs)
        request.configuration.serialLogs = m_draft.serialLogs;
    if (fields & PinLock)
    {
        request.configuration.pinLock = m_draft.pinLock;
        if (!m_draft.pinLock)
            stringutils::safeCopy(request.currentPin, m_pinEntry);
    }
    stringutils::safeCopy(request.pin, m_pinPending);
    request.sound = m_soundDraft;
    m_savedFields = request.changedFields;
    if (!enqueue(request))
    {
        notice(SettingsKey::QueueBusy);
        return;
    }
    m_pendingId = request.id;
    m_pendingType = request.type;
    m_afterSave = after;
    closeModal();
    m_notice = SettingsKey::Working;
}

void Settings::discard()
{
    const bool unknownLatest = m_conflict && m_latestRevision <= m_revision;
    m_draft = m_baseline = m_latest;
    m_revision = m_latestRevision;
    m_conflict = unknownLatest;
    memset(m_pinPending, 0, sizeof(m_pinPending));
    resetPinEntry();
    m_confirmPinDisable = false;
    m_soundDraft = m_soundBaseline;
    preview(RequestType::PreviewSound);
    preview(RequestType::PreviewScreen);
    preview(RequestType::PreviewUkw);
    for (bool &tested : m_tested)
        tested = false;
    m_notice = m_conflict ? SettingsKey::Conflict : SettingsKey::Unchanged;
    redraw();
}

void Settings::leavePending()
{
    if (m_pendingType == RequestType::TestProfile)
    {
        Request cancel;
        cancel.type = RequestType::CancelTest;
        cancel.targetId = m_pendingId;
        if (!enqueue(cancel))
            return;
        m_pendingId = 0;
        m_pendingType = RequestType::None;
        closeModal();
    }
    else if (m_pendingType == RequestType::BtConnect)
    {
        btCommand(RequestType::BtDisconnect);
        m_pendingId = 0;
        m_pendingType = RequestType::None;
        closeModal();
    }
    action(RequestType::Home);
}

void Settings::action(RequestType type)
{
    if (type == RequestType::Mute)
    {
        if (!m_capabilities.mute)
            return;
        if (!m_muteId)
            m_muteId = nextId();
        m_lastMuteId = m_muteId;
        m_notice = SettingsKey::MutedRequested;
        invalidateFooter();
        return;
    }
    if (type == RequestType::Restart && !m_capabilities.restart)
    {
        notice(SettingsKey::Missing);
        return;
    }
    Request request;
    request.type = type;
    if (!enqueue(request))
    {
        notice(SettingsKey::QueueBusy);
        return;
    }
    if (type == RequestType::Restart)
    {
        m_pendingId = request.id;
        m_pendingType = type;
        m_notice = SettingsKey::Working;
    }
    else
    {
        m_lastNavigationId = request.id;
        m_lastNavigationType = type;
    }
    redraw();
}

void Settings::edit(Edit field)
{
    m_edit = field;
    SettingsKey title = SettingsKey::Invalid;
    const char *initial = "";
    size_t limit = 64;
    SettingsKeyboard::Mode mode = SettingsKeyboard::Mode::Text;
    char frequency[12];
    switch (field)
    {
    case Edit::UkwFrequency:
        snprintf(frequency, sizeof(frequency), "%lu%c%lu", static_cast<unsigned long>(m_draft.ukwKhz / 1000), m_language == LangIndex::EN ? '.' : ',', static_cast<unsigned long>(m_draft.ukwKhz % 1000 / 100));
        initial = frequency;
        limit = 6;
        break;
    case Edit::Ssid:
        title = SettingsKey::SsidName;
        initial = m_draft.profiles[m_profile].ssid;
        limit = 32;
        break;
    case Edit::Password:
        title = SettingsKey::Password;
        initial = m_draft.profiles[m_profile].password;
        mode = SettingsKeyboard::Mode::Password;
        break;
    case Edit::Hostname:
        title = SettingsKey::Hostname;
        initial = m_draft.hostname;
        limit = 32;
        break;
    case Edit::Ntp:
        title = SettingsKey::Ntp;
        initial = m_draft.ntpServer;
        limit = 63;
        break;
    default:
        return;
    }
    char dialogTitle[96];
    snprintf(dialogTitle, sizeof(dialogTitle), "%s", tr(title));
    if (field == Edit::Password)
        snprintf(dialogTitle, sizeof(dialogTitle), ot(OKey::WIFI_PW_TITLE_FMT), m_draft.profiles[m_profile].ssid);
    else if (field == Edit::Hostname)
        snprintf(dialogTitle, sizeof(dialogTitle), "%s", ot(OKey::WIFI_HOSTNAME_PROMPT));
    else if (field == Edit::Ntp)
        snprintf(dialogTitle, sizeof(dialogTitle), "%s", ot(OKey::TIME_NTP_PROMPT));
    else if (field == Edit::UkwFrequency)
        snprintf(dialogTitle, sizeof(dialogTitle), "%s", ot(OKey::UKW_FREQ_PROMPT));
    if (!m_keyboard.open(dialogTitle, initial, limit, mode, m_language))
    {
        notice(SettingsKey::Invalid);
        return;
    }
    m_modal = Modal::Keyboard;
    m_contact.cancel();
    m_touchOwner = 0;
    redraw();
}

void Settings::acceptInput()
{
    const char *value = m_keyboard.value();
    const Edit field = m_edit;
    const bool wifiField = field == Edit::Ssid || field == Edit::Password || field == Edit::Hostname;
    const bool timeField = field == Edit::Ntp;
    if ((wifiField && (!m_capabilities.wifiProfiles || !m_capabilities.save)) || (timeField && (!m_capabilities.time || !m_capabilities.save)) || (field == Edit::UkwFrequency && !ukwEditable()))
    {
        m_keyboard.setError(SettingsKey::Missing);
        return;
    }
    if (field == Edit::Ssid)
    {
        if (!value[0] || !SettingsUi::validText(value, 33))
        {
            m_keyboard.setError(SettingsKey::InvalidSsid);
            return;
        }
        auto &profile = m_draft.profiles[m_profile];
        if (strcmp(profile.ssid, value))
        {
            stringutils::safeCopy(profile.ssid, value);
            m_tested[m_profile] = false;
        }
    }
    else if (field == Edit::Password)
    {
        char normalized[65] = {};
        if (!normalizePassword(value, normalized))
        {
            m_keyboard.setError(SettingsKey::InvalidPassword);
            return;
        }
        stringutils::safeCopy(m_draft.profiles[m_profile].password, normalized);
        memset(normalized, 0, sizeof(normalized));
        m_tested[m_profile] = false;
    }
    else if (field == Edit::Hostname)
    {
        if (!validHostname(value))
        {
            m_keyboard.setError(SettingsKey::InvalidHostname);
            return;
        }
        stringutils::safeCopy(m_draft.hostname, value);
    }
    else if (field == Edit::Ntp)
    {
        if (!validServer(value))
        {
            m_keyboard.setError(SettingsKey::InvalidNtp);
            return;
        }
        stringutils::safeCopy(m_draft.ntpServer, value);
    }
    else if (field == Edit::UkwFrequency)
    {
        uint32_t khz = 0;
        if (!parseMhz(value, khz) || khz < UkwMinKhz || khz > UkwMaxKhz)
        {
            m_keyboard.setError(SettingsKey::Invalid);
            return;
        }
        m_draft.ukwKhz = khz;
        preview(RequestType::PreviewUkw);
    }
    closeModal();
    edited();
}

void Settings::startScan()
{
    if (!m_capabilities.wifiScan || !m_capabilities.wifiProfiles || !m_capabilities.save)
    {
        notice(SettingsKey::Missing);
        return;
    }
    if (m_scanId && !cancelScan())
        return;
    Request request;
    request.type = RequestType::Scan;
    if (!enqueue(request))
    {
        notice(SettingsKey::QueueBusy);
        return;
    }
    m_scanId = request.id;
    m_networks.open(m_language);
    m_modal = Modal::Networks;
    m_contact.cancel();
    m_touchOwner = 0;
    redraw();
}

bool Settings::cancelScan()
{
    if (!m_scanId)
        return true;
    Request request;
    request.type = RequestType::CancelScan;
    request.targetId = m_scanId;
    if (!enqueue(request))
    {
        m_notice = SettingsKey::QueueBusy;
        redraw();
        return false;
    }
    m_scanId = 0;
    return true;
}

void Settings::selectedNetwork(const WifiList::Result &result)
{
    if (result.action == WifiList::Action::None)
        return;
    if (result.action != WifiList::Action::Cancel && (!m_capabilities.wifiProfiles || !m_capabilities.save))
    {
        m_notice = SettingsKey::Missing;
        redraw();
        return;
    }
    if (result.action == WifiList::Action::Cancel)
    {
        if (cancelScan())
            closeModal();
        return;
    }
    if (result.action == WifiList::Action::Rescan)
    {
        startScan();
        return;
    }
    if (result.action == WifiList::Action::Manual)
    {
        if (cancelScan())
            edit(Edit::Ssid);
        return;
    }
    const auto *network = result.index >= 0 ? m_networks.network(static_cast<size_t>(result.index)) : nullptr;
    if (!network)
        return;
    auto &profile = m_draft.profiles[m_profile];
    if (strcmp(profile.ssid, network->ssid))
    {
        stringutils::safeCopy(profile.ssid, network->ssid);
        m_tested[m_profile] = false;
    }
    closeModal();
    edited();
}

size_t Settings::pickerCount() const
{
    switch (m_picker)
    {
    case Picker::Zone:
        return settingsTimezoneCount();
    case Picker::Interval:
        return sizeof(Intervals) / sizeof(Intervals[0]);
    case Picker::Eye:
        return 4;
    case Picker::Ballistics:
        return 3;
    default:
        return 5;
    }
}

void Settings::choose(size_t index)
{
    if (index >= pickerCount())
        return;
    if (!m_capabilities.save || ((m_picker == Picker::Zone || m_picker == Picker::Interval) ? !m_capabilities.time : !m_capabilities.appearance))
    {
        closeModal();
        notice(SettingsKey::Missing);
        return;
    }
    switch (m_picker)
    {
    case Picker::Zone:
        stringutils::safeCopy(m_draft.timezone, settingsTimezone(index)->region);
        stringutils::safeCopy(m_draft.timezonePosix, settingsTimezone(index)->posix);
        break;
    case Picker::Interval:
        m_draft.syncIntervalMs = Intervals[index];
        break;
    case Picker::Eye:
        m_draft.eyeStyle = static_cast<uint8_t>(index);
        break;
    case Picker::Ballistics:
        m_draft.vuBallistics = static_cast<uint8_t>(index);
        break;
    case Picker::Sensitivity:
        m_draft.vuSensitivity = static_cast<uint8_t>(index);
        break;
    }
    closeModal();
    edited();
}

Settings::Rect Settings::wifiRowRect(int idx) const
{
    const int y = CONTENT_Y + idx * (WIFI_ROW_H + WIFI_ROW_GAP);
    return {CONTENT_X, y, CONTENT_W, WIFI_ROW_H};
}

Settings::Rect Settings::wifiToggleRect(int idx) const
{
    const Rect r = wifiRowRect(idx);
    const int x = r.x + r.w - TOGGLE_W - TOGGLE_MARGIN;
    const int y = r.y + (r.h - TOGGLE_H) / 2;
    return {x, y, TOGGLE_W, TOGGLE_H};
}

Settings::Rect Settings::wifiActionRect(int slot) const
{
    const int y = CONTENT_Y + MAX_PROFILES * (WIFI_ROW_H + WIFI_ROW_GAP) + L::Wifi::ACTION_GAP_Y;
    const int w = (CONTENT_W - 3 * WIFI_ACT_GAP) / 4;
    const int x = CONTENT_X + slot * (w + WIFI_ACT_GAP);
    return {x, y, w, WIFI_ACT_H};
}

Settings::Rect Settings::hostnameFieldRect() const
{
    const int y = WORK_H - HOSTNAME_BOTTOM_GAP - HOSTNAME_FIELD_H;
    return {CONTENT_X, y, CONTENT_W, HOSTNAME_FIELD_H};
}

SettingsTextKey Settings::wifiStatusKey(int profileIndex) const
{
    if (!isProfileValid(profileIndex))
        return OKey::WIFI_STATUS_EMPTY;
    if (m_tested[profileIndex])
        return OKey::WIFI_STATUS_CONNECTED;
    if (m_draft.profiles[profileIndex].enabled)
        return OKey::WIFI_STATUS_ACTIVE;
    return OKey::WIFI_STATUS_INACTIVE;
}

uint16_t Settings::wifiStatusColor(int profileIndex) const
{
    if (!isProfileValid(profileIndex))
        return farbe(Rolle::EinStatusLeer);
    if (m_tested[profileIndex])
        return farbe(Rolle::EinSektionSystem);
    if (m_draft.profiles[profileIndex].enabled)
        return farbe(Rolle::EinSchalterAn);
    return farbe(Rolle::EinStatusInaktiv);
}

void Settings::drawWifiRow(UiCanvas *spr, int idx, const Rect &r, bool selected)
{
    char nameBuf[40];
    if (isProfileValid(idx))
        stringutils::safeCopy(nameBuf, m_draft.profiles[idx].ssid);
    else
        STRF(nameBuf, OKey::WIFI_PROFILE_FMT, idx + 1);
    drawProfileRow(spr, r, selected, nameBuf, isProfileValid(idx), ot(wifiStatusKey(idx)), wifiStatusColor(idx), m_draft.profiles[idx].enabled);
}

void Settings::drawProfileRow(UiCanvas *spr, const Rect &r, bool selected, const char *name, bool named, const char *status, uint16_t statusColor, bool on)
{
    const uint16_t rowBg = farbe(selected ? Rolle::EinZeileGewaehlt : Rolle::EinZeile);
    spr->fillSmoothRoundRect(r.x, r.y, r.w, r.h, FIELD_R, rowBg);
    if (selected)
        spr->fillRect(r.x, r.y + L::Wifi::ROW_ACCENT_INSET, ITEM_ACCENT_W, r.h - 2 * L::Wifi::ROW_ACCENT_INSET, farbe(Rolle::EinSektionVerbindung));

    spr->fillSmoothCircle(r.x + L::Wifi::DOT_X, r.y + r.h / 2, L::Wifi::DOT_R, statusColor);

    spr->loadFont(ComfortaaBold24);
    spr->setTextDatum(ML_DATUM);
    spr->setTextColor(farbe(named ? Rolle::GemTextHell : Rolle::EinTextNeben));
    spr->drawString(name, r.x + L::Wifi::TEXT_X, r.y + L::Wifi::NAME_Y);
    spr->unloadFont();

    spr->loadFont(ComfortaaBold21);
    spr->setTextColor(statusColor);
    spr->drawString(status, r.x + L::Wifi::TEXT_X, r.y + r.h - L::Wifi::STATUS_DY);
    spr->unloadFont();

    drawToggle(spr, {r.x + r.w - TOGGLE_W - TOGGLE_MARGIN, r.y + (r.h - TOGGLE_H) / 2, TOGGLE_W, TOGGLE_H}, on, rowBg);
}

void Settings::drawToggle(UiCanvas *spr, const Rect &t, bool on, uint16_t background)
{
    const uint16_t housing = farbe(on ? Rolle::GemRahmen : Rolle::EinSchalterGehaeuseAus);
    const uint16_t knobCol = farbe(on ? Rolle::EinSchalterAn : Rolle::GemWarnung);

    spr->fillSmoothRoundRect(t.x, t.y, t.w, t.h, t.h / 2, housing);
    spr->fillSmoothRoundRect(t.x + TOGGLE_INSET, t.y + TOGGLE_INSET,
                             t.w - 2 * TOGGLE_INSET, t.h - 2 * TOGGLE_INSET,
                             (t.h - 2 * TOGGLE_INSET) / 2, background);
    const int knobCx = on ? (t.x + t.w - TOGGLE_INSET - TOGGLE_KNOB_R - L::Toggle::KNOB_GAP)
                          : (t.x + TOGGLE_INSET + TOGGLE_KNOB_R + L::Toggle::KNOB_GAP);
    spr->fillSmoothCircle(knobCx, t.y + t.h / 2, TOGGLE_KNOB_R, knobCol);
}

void Settings::drawPanelWifi(UiCanvas *spr)
{
    for (int i = 0; i < MAX_PROFILES; ++i)
        drawWifiRow(spr, i, wifiRowRect(i), i == m_profile);

    const bool configured = isProfileValid(m_profile);
    UiButton *abtn[4] = {&m_btnScan, &m_btnPassword, &m_btnTest, &m_btnDelete};
    const SettingsTextKey labels[4] = {
        OKey::WIFI_ACT_SCAN,
        OKey::WIFI_ACT_PASSWORD,
        OKey::WIFI_ACT_TEST,
        OKey::WIFI_ACT_DELETE};
    // Scannen ist immer möglich. Passwort, Test und Löschen brauchen eine SSID
    const bool enabled[4] = {true, configured, configured, configured};

    for (int k = 0; k < 4; ++k)
    {
        abtn[k]->setLabelText(reinterpret_cast<const char *>(ot(labels[k])));
        if (enabled[k])
            abtn[k]->enable();
        else
            abtn[k]->disable();
        abtn[k]->drawButton();
    }

    const Rect hf = hostnameFieldRect();
    const uint16_t rahmen = farbe(Rolle::GemRahmen);
    drawLabelField(spr, hf, ot(OKey::WIFI_HOSTNAME_LABEL), m_draft.hostname);

    if (hostnamePending())
        spr->fillRect(hf.x + BORDER, hf.y + ACCENT_INSET, ITEM_ACCENT_W, hf.h - 2 * ACCENT_INSET, farbe(Rolle::EinSektionVerbindung));

    const int editX = hf.x + hf.w - HOSTNAME_PAD_X - HOSTNAME_EDIT_BTN_W;
    const int editY = hf.y + (hf.h - HOSTNAME_EDIT_BTN_H) / 2;

    spr->fillSmoothRoundRect(editX, editY, HOSTNAME_EDIT_BTN_W, HOSTNAME_EDIT_BTN_H, BTN_R, rahmen);
    spr->fillSmoothRoundRect(editX + BORDER, editY + BORDER, HOSTNAME_EDIT_BTN_W - 2 * BORDER, HOSTNAME_EDIT_BTN_H - 2 * BORDER, BUTTON_INNER_R, farbe(Rolle::GemTastenFlaeche));

    spr->loadFont(LeagueGothicCond51);
    spr->setTextDatum(MC_DATUM);
    spr->setTextColor(farbe(Rolle::EinTastenText));
    spr->drawString(ot(OKey::WIFI_HOSTNAME_EDIT),
                    editX + HOSTNAME_EDIT_BTN_W / 2, editY + HOSTNAME_EDIT_BTN_H / 2);
    spr->unloadFont();
}

void Settings::drawLabelField(UiCanvas *spr, const Rect &r, const char *label, const char *value)
{
    spr->fillSmoothRoundRect(r.x, r.y, r.w, r.h, FIELD_R, farbe(Rolle::GemRahmen));
    spr->fillSmoothRoundRect(r.x + BORDER, r.y + BORDER, r.w - 2 * BORDER, r.h - 2 * BORDER, FIELD_INNER_R, farbe(Rolle::EinZeile));
    spr->loadFont(ComfortaaBold24);
    const int lineH = spr->fontHeight();
    const int blockH = lineH * 2 + HOSTNAME_LINE_GAP;
    const int labelY = r.y + (r.h - blockH) / 2;
    const int valueY = labelY + lineH + HOSTNAME_LINE_GAP;
    spr->setTextDatum(TL_DATUM);
    spr->setTextColor(farbe(Rolle::EinTextNeben));
    spr->drawString(label, r.x + HOSTNAME_PAD_X, labelY);
    spr->setTextColor(farbe(Rolle::GemTextHell));
    spr->drawString(value, r.x + HOSTNAME_PAD_X, valueY);
    spr->unloadFont();
}

Settings::Rect Settings::btActionRect(int idx) const
{
    const Rect slot = wifiActionRect(L::Bluetooth::ACTION_SLOTS[std::clamp(idx, 0, L::Bluetooth::ACTION_COUNT - 1)]);
    return {slot.x, CONTENT_Y + L::Bluetooth::ACTION_Y, slot.w, slot.h};
}

bool Settings::btLinked() const
{
    return m_btPresent && (m_btLink.state == BtProtocol::LinkState::Connected || m_btLink.state == BtProtocol::LinkState::Connecting);
}

bool Settings::btActionEnabled(int idx) const
{
    using BtProtocol::LinkState;
    if (!m_btPresent)
        return false;
    // Suchen nimmt der Pico nur im Zustand STILL an, verbunden wird zuerst mit dem Schalter getrennt.
    if (idx == 0)
        return m_btLink.state == LinkState::Idle;
    if (idx == 1)
        return m_btLink.state == LinkState::Connected;
    return m_btLink.device.address[0] && m_btLink.state != LinkState::Searching;
}

bool Settings::btToggleEnabled() const
{
    return btLinked() || (m_btPresent && m_btLink.state == BtProtocol::LinkState::Idle && m_btLink.device.address[0]);
}

SettingsTextKey Settings::btStatusKey() const
{
    using BtProtocol::LinkState;
    if (!m_btPresent)
        return OKey::BT_STATUS_UNAVAILABLE;
    if (m_btLink.state == LinkState::Connected)
        return OKey::BT_STATUS_CONNECTED;
    if (m_btLink.state == LinkState::Connecting)
        return OKey::BT_STATUS_CONNECTING;
    if (m_btLink.state == LinkState::Searching)
        return OKey::BT_STATUS_SEARCHING;
    return m_btLink.device.address[0] ? OKey::BT_STATUS_DISCONNECTED : OKey::BT_STATUS_EMPTY;
}

uint16_t Settings::btStatusColor() const
{
    switch (btStatusKey())
    {
    case OKey::BT_STATUS_CONNECTED:
        return farbe(Rolle::EinSektionSystem);
    case OKey::BT_STATUS_CONNECTING:
    case OKey::BT_STATUS_SEARCHING:
        return farbe(Rolle::EinSchalterAn);
    case OKey::BT_STATUS_DISCONNECTED:
        return farbe(Rolle::EinStatusInaktiv);
    default:
        return farbe(Rolle::EinStatusLeer);
    }
}

void Settings::drawPanelBluetooth(UiCanvas *spr)
{
    // Die Gerätezeile steht an der Stelle des ersten WLAN-Profils und gilt wie das gewählte Profil als Ziel der Tasten.
    const auto &device = m_btLink.device;
    const bool stored = m_btPresent && device.address[0];
    drawProfileRow(spr, wifiRowRect(0), true, stored ? (device.name[0] ? device.name : device.address) : ot(OKey::BT_DEVICE), stored, ot(btStatusKey()), btStatusColor(), btLinked());

    UiButton *buttons[L::Bluetooth::ACTION_COUNT] = {&m_btnBtSearch, &m_btnBtTone, &m_btnBtForget};
    const SettingsTextKey labels[L::Bluetooth::ACTION_COUNT] = {OKey::BT_ACT_SEARCH, OKey::BT_ACT_TEST, OKey::BT_ACT_FORGET};
    for (int k = 0; k < L::Bluetooth::ACTION_COUNT; ++k)
    {
        buttons[k]->setLabelText(ot(labels[k]));
        if (btActionEnabled(k))
            buttons[k]->enable();
        else
            buttons[k]->disable();
        buttons[k]->drawButton();
    }

    drawLabelField(spr, hostnameFieldRect(), ot(OKey::BT_SENDER_LABEL), ot(m_btPresent ? OKey::BT_SENDER_READY : OKey::BT_SENDER_MISSING));
}

bool Settings::ukwEditable() const
{
    return m_capabilities.ukw && m_capabilities.save;
}

void Settings::formatMhz(char *text, size_t size, uint32_t khz) const
{
    snprintf(text, size, ot(OKey::UKW_FREQ_FMT), static_cast<unsigned long>(khz / 1000), static_cast<unsigned long>(khz % 1000 / 100));
}

void Settings::ukwStatusText(char *text, size_t size) const
{
    using BtProtocol::UkwState;
    const auto &ukw = m_btLink.ukw;
    if (!m_btPresent || !ukw.present())
        snprintf(text, size, "%s", ot(OKey::BT_SENDER_MISSING));
    else if (ukw.state == UkwState::Off)
        snprintf(text, size, "%s", ot(OKey::UKW_STATUS_OFF));
    else if (ukw.state == UkwState::Starting)
        snprintf(text, size, "%s", ot(OKey::UKW_STATUS_STARTING));
    else
    {
        char mhz[24];
        formatMhz(mhz, sizeof(mhz), ukw.khz);
        snprintf(text, size, ot(ukw.state == UkwState::On ? OKey::UKW_STATUS_ON_FMT : OKey::UKW_STATUS_MUTED_FMT), mhz);
    }
}

uint16_t Settings::ukwStatusColor() const
{
    using BtProtocol::UkwState;
    const auto &ukw = m_btLink.ukw;
    if (!m_btPresent || !ukw.present())
        return farbe(Rolle::EinStatusLeer);
    switch (ukw.state)
    {
    case UkwState::On:
        return farbe(Rolle::EinSektionSystem);
    case UkwState::Starting:
    case UkwState::Muted:
        return farbe(Rolle::EinSchalterAn);
    default:
        return farbe(Rolle::EinStatusInaktiv);
    }
}

Settings::Rect Settings::ukwStepRect(int row, bool plus) const
{
    const int blockW = AUDIO_STEP_BTN_W * 2 + AUDIO_STEP_VAL_W + AUDIO_STEP_GAP * 2;
    const int x = CONTENT_X + CONTENT_W - blockW + (plus ? AUDIO_STEP_BTN_W + AUDIO_STEP_GAP + AUDIO_STEP_VAL_W + AUDIO_STEP_GAP : 0);
    return Rect{x, CONTENT_Y + (row == 0 ? L::Ukw::ROW1_Y : L::Ukw::ROW2_Y), AUDIO_STEP_BTN_W, AUDIO_ROW_H};
}

Settings::Rect Settings::ukwValueRect(int row) const
{
    const Rect minus = ukwStepRect(row, false);
    return Rect{minus.x + AUDIO_STEP_BTN_W + AUDIO_STEP_GAP, minus.y, AUDIO_STEP_VAL_W, AUDIO_ROW_H};
}

void Settings::drawPanelUkw(UiCanvas *spr)
{
    // Die Senderzeile steht an der Stelle des ersten WLAN-Profils. Der Schalter zeigt den Entwurf, der Zustand darunter den Modulator laut Pico.
    char status[64];
    ukwStatusText(status, sizeof(status));
    drawProfileRow(spr, wifiRowRect(0), true, ot(OKey::UKW_SENDER), true, status, ukwStatusColor(), m_draft.ukwEnabled);

    // Frequenz und Leistung wie Klangzeilen: Beschriftung links, Minus, Wert und Plus rechtsbündig.
    const SettingsTextKey labels[2] = {OKey::UKW_FREQUENCY, OKey::UKW_POWER};
    const uint16_t rahmen = farbe(Rolle::GemRahmen), taste = farbe(Rolle::GemTastenFlaeche), tastenText = farbe(Rolle::EinTastenText);
    for (int row = 0; row < 2; ++row)
    {
        const Rect v = ukwValueRect(row);
        spr->loadFont(ComfortaaBold24);
        spr->setTextDatum(ML_DATUM);
        spr->setTextColor(farbe(Rolle::GemTextHell));
        spr->drawString(ot(labels[row]), CONTENT_X + L::Sound::LABEL_X, v.y + AUDIO_ROW_H / 2);
        spr->unloadFont();
        for (const bool plus : {false, true})
        {
            const Rect b = ukwStepRect(row, plus);
            spr->fillSmoothRoundRect(b.x, b.y, b.w, b.h, BTN_R, rahmen);
            spr->fillSmoothRoundRect(b.x + BORDER, b.y + BORDER, b.w - 2 * BORDER, b.h - 2 * BORDER, BUTTON_INNER_R, taste);
            spr->loadFont(ComfortaaBold45);
            spr->setTextDatum(MC_DATUM);
            spr->setTextColor(tastenText);
            spr->drawString(plus ? "+" : "-", b.x + b.w / 2, b.y + b.h / 2);
            spr->unloadFont();
        }
        char value[24];
        if (row == 0)
            formatMhz(value, sizeof(value), m_draft.ukwKhz);
        else
            snprintf(value, sizeof(value), "%u", static_cast<unsigned>(m_draft.ukwPower));
        spr->loadFont(ComfortaaBold45);
        spr->setTextDatum(MC_DATUM);
        spr->setTextColor(farbe(Rolle::EinSektionSystem));
        spr->drawString(value, v.x + v.w / 2, v.y + v.h / 2 + L::Sound::VALUE_OFFSET_Y);
        spr->unloadFont();
    }
    drawAppearanceHint(spr, L::Ukw::HINT1_Y, OKey::UKW_FREQ_HINT);
    drawAppearanceHint(spr, L::Ukw::HINT2_Y, OKey::UKW_POWER_HINT);
    drawAppearanceHint(spr, L::Ukw::HINT3_Y, OKey::UKW_POWER_HINT2);

    // Modulatorfeld: Chip, RDS und die Quellrate, die am Modulator anliegt.
    const auto &ukw = m_btLink.ukw;
    char modulator[64];
    if (!m_btPresent || !ukw.present())
        snprintf(modulator, sizeof(modulator), "%s", ot(OKey::BT_SENDER_MISSING));
    else
    {
        char rate[24];
        if (ukw.rate)
            snprintf(rate, sizeof(rate), ot(OKey::UKW_RATE_FMT), static_cast<unsigned long>(ukw.rate));
        else
            snprintf(rate, sizeof(rate), "%s", ot(OKey::UKW_RATE_BAD));
        snprintf(modulator, sizeof(modulator), "%s, %s, %s", ukw.chip, ot(strcmp(ukw.chip, "QN8007") == 0 ? OKey::UKW_RDS_YES : OKey::UKW_RDS_NO), rate);
    }
    drawLabelField(spr, hostnameFieldRect(), ot(OKey::UKW_MODULATOR_LABEL), modulator);
}

void Settings::setBluetooth(bool present, const BtProtocol::LinkView &link, uint32_t version)
{
    using BtProtocol::LinkState;
    const bool shown = present != m_btPresent || link.state != m_btLink.state || strcmp(link.device.address, m_btLink.device.address) || strcmp(link.device.name, m_btLink.device.name);
    const bool ukwShown = link.ukw.state != m_btLink.ukw.state || link.ukw.khz != m_btLink.ukw.khz || link.ukw.rate != m_btLink.ukw.rate || strcmp(link.ukw.chip, m_btLink.ukw.chip);
    m_btPresent = present;
    m_btLink = link;
    m_btVersion = version;
    // Die erste Änderung nach einem Auftrag ist sein Senden, das die Fehlerkennung löscht. Eine Kennung danach gehört zu ihm.
    const bool error = link.error[0] && version - m_btRequestVersion >= 2;
    if (m_btSearchId)
    {
        if (link.state == LinkState::Searching || !link.searchDone)
            m_btSearchStarted = true;
        if (!present)
            failBtSearch(SettingsKey::Missing);
        else if (error)
            failBtSearch(SettingsKey::QueueBusy);
        else if (m_btSearchStarted && link.searchDone)
            showBtHits();
    }
    if (m_pendingType == RequestType::BtConnect)
    {
        if (link.state == LinkState::Connecting)
            m_btConnectSeen = true;
        if (present && link.state == LinkState::Connected && !strcmp(link.device.address, m_btTarget))
            finishBtConnect(true);
        else if (!present)
        {
            char reason[64];
            snprintf(reason, sizeof(reason), "%s: %s", ot(OKey::BT_SENDER_LABEL), ot(OKey::BT_SENDER_MISSING));
            finishBtConnect(false, reason);
        }
        else if (error || (m_btConnectSeen && link.state == LinkState::Idle))
            finishBtConnect(false, ot(OKey::BT_CONNECT_FAIL_MSG));
    }
    if (((shown || ukwShown) && m_panel == Panel::Ukw && m_modal == Modal::None) || (shown && m_panel == Panel::Bluetooth && m_modal == Modal::None))
        redraw();
}

void Settings::startBtSearch()
{
    if (!btActionEnabled(0) || m_btSearchId)
        return;
    Request request;
    request.type = RequestType::BtSearch;
    if (!enqueue(request))
    {
        notice(SettingsKey::QueueBusy);
        return;
    }
    m_btSearchId = request.id;
    m_btSearchSince = m_now;
    m_btSearchStarted = false;
    m_btRequestVersion = m_btVersion;
    m_networks.open(m_language, SettingsWifiList::Kind::Devices);
    m_modal = Modal::Networks;
    m_contact.cancel();
    m_touchOwner = 0;
    redraw();
}

void Settings::stopBtSearch()
{
    if (!m_btSearchId)
        return;
    m_btSearchId = 0;
    btCommand(RequestType::BtDisconnect);
}

void Settings::failBtSearch(SettingsKey key)
{
    m_btSearchId = 0;
    if (m_modal == Modal::Networks)
    {
        m_networks.fail(key);
        redraw();
    }
}

void Settings::showBtHits()
{
    m_btSearchId = 0;
    if (m_modal != Modal::Networks)
        return;
    WifiList::Network list[BtProtocol::MaxHits];
    const size_t count = std::min<size_t>(m_btLink.hitCount, BtProtocol::MaxHits);
    for (size_t i = 0; i < count; ++i)
    {
        const auto &hit = m_btLink.hits[i];
        // Ohne Namen oder mit ungültigem UTF-8 erscheint die Adresse, damit jede Zeile einen Text hat.
        stringutils::safeCopy(list[i].ssid, hit.name[0] && SettingsUi::validText(hit.name, sizeof(hit.name)) ? hit.name : hit.address);
        list[i].rssi = hit.rssiKnown ? hit.rssi : BT_RSSI_UNKNOWN;
        list[i].encrypted = false;
        stringutils::safeCopy(m_btListAddress[i], hit.address);
    }
    if (!m_networks.setNetworks(list, count, false))
        m_networks.fail(SettingsKey::Invalid);
    redraw();
}

void Settings::selectedDevice(const WifiList::Result &result)
{
    if (result.action == WifiList::Action::Cancel)
    {
        stopBtSearch();
        closeModal();
    }
    else if (result.action == WifiList::Action::Rescan)
        startBtSearch();
    else if (result.action == WifiList::Action::Selected)
    {
        const int hit = result.index >= 0 ? m_networks.origin(static_cast<size_t>(result.index)) : -1;
        if (hit < 0 || hit >= static_cast<int>(BtProtocol::MaxHits))
            return;
        char address[BtProtocol::AddressSize];
        stringutils::safeCopy(address, m_btListAddress[hit]);
        closeModal();
        connectBt(address);
    }
}

void Settings::connectBt(const char *address)
{
    Request request;
    request.type = RequestType::BtConnect;
    stringutils::safeCopy(request.btAddress, address);
    if (!enqueue(request))
    {
        notice(SettingsKey::QueueBusy);
        return;
    }
    m_pendingId = request.id;
    m_pendingType = request.type;
    stringutils::safeCopy(m_btTarget, address);
    m_btConnectSeen = false;
    m_btRequestVersion = m_btVersion;
    m_progress.setTitle(ot(OKey::BT_CONNECT_TITLE));
    m_progress.start(ot(OKey::BT_CONNECT_RUNNING), 0);
    m_operationStart = m_now;
    m_modal = Modal::Progress;
    redraw();
}

void Settings::finishBtConnect(bool success, const char *reason)
{
    m_pendingId = 0;
    m_pendingType = RequestType::None;
    closeModal();
    if (!success)
    {
        ask(Purpose::BtFailed, SettingsKey::Ok, reason);
        return;
    }
    char text[96];
    const auto &device = m_btLink.device;
    snprintf(text, sizeof(text), ot(OKey::BT_CONNECT_OK_FMT), device.name[0] ? device.name : device.address);
    ask(Purpose::BtConnected, SettingsKey::Ok, text);
}

void Settings::btCommand(RequestType type)
{
    Request request;
    request.type = type;
    if (!enqueue(request))
        notice(SettingsKey::QueueBusy);
}

bool Settings::completeBluetooth(const Completion &completion)
{
    if (completion.outcome != Outcome::Failed)
        return true;
    if (completion.type == RequestType::BtSearch)
    {
        if (completion.id != m_btSearchId)
            return false;
        failBtSearch(errorText(completion.error));
        return true;
    }
    if (completion.type == RequestType::BtConnect)
    {
        if (completion.id != m_pendingId || m_pendingType != RequestType::BtConnect)
            return false;
        finishBtConnect(false, tr(errorText(completion.error)));
        return true;
    }
    if (completion.type != RequestType::BtDisconnect)
        notice(errorText(completion.error));
    return true;
}

Settings::Rect Settings::langRowRect(int idx) const
{
    const int y = LANG_ROWS_Y + idx * (LANG_ROW_H + LANG_ROW_GAP);
    return {CONTENT_X, y, CONTENT_W, LANG_ROW_H};
}

Settings::Rect Settings::serialLogsToggleRect() const
{
    return {CONTENT_X + CONTENT_W - TOGGLE_W, LOGS_HEADING_Y + L::Language::HEADING_H, TOGGLE_W, TOGGLE_H};
}

void Settings::drawPanelLanguage(UiCanvas *spr)
{
    const int selectedIdx = m_draft.language == LangIndex::EN ? 1 : 0;
    const int activeSysIdx = currentLangIndex();
    const SettingsTextKey names[2] = {OKey::LANG_NAME_DE, OKey::LANG_NAME_EN};
    const uint16_t zeile = farbe(Rolle::EinZeile), zeileGewaehlt = farbe(Rolle::EinZeileGewaehlt), sektionSystem = farbe(Rolle::EinSektionSystem);
    const uint16_t textHell = farbe(Rolle::GemTextHell), text = farbe(Rolle::EinText);

    // Rubriken wie im Original: Überschriften in der Sektionsfarbe, dazwischen eine Trennlinie
    spr->loadFont(ComfortaaBold24);
    spr->setTextDatum(ML_DATUM);
    spr->setTextColor(sektionSystem);
    spr->drawString(ot(OKey::LANG_SECTION), CONTENT_X, CONTENT_Y + L::Language::HEADING_H / 2);
    spr->drawString(ot(OKey::LOGS_SECTION), CONTENT_X, LOGS_HEADING_Y + L::Language::HEADING_H / 2);
    spr->fillRect(CONTENT_X, LOGS_HEADING_Y - L::Language::SECTION_GAP / 2 - LINE / 2, CONTENT_W, LINE, farbe(Rolle::EinTrenner));
    const Rect toggle = serialLogsToggleRect();
    drawToggle(spr, toggle, m_draft.serialLogs, farbe(Rolle::GemFlaeche));
    spr->setTextColor(text);
    spr->drawString(ot(OKey::LOGS_TOGGLE), CONTENT_X, toggle.y + toggle.h / 2);
    spr->unloadFont();

    for (int i = 0; i < 2; ++i)
    {
        const Rect r = langRowRect(i);
        const bool selected = (i == selectedIdx);
        spr->fillSmoothRoundRect(r.x, r.y, r.w, r.h, L::Language::ROW_R, selected ? zeileGewaehlt : zeile);
        if (selected)
            spr->fillRect(r.x, r.y + ACCENT_INSET, ITEM_ACCENT_W, r.h - 2 * ACCENT_INSET, sektionSystem);

        spr->loadFont(ComfortaaBold24);
        spr->setTextDatum(ML_DATUM);
        spr->setTextColor(selected ? textHell : text);
        spr->drawString(ot(names[i]), r.x + L::Language::TEXT_X, r.y + r.h / 2);
        spr->unloadFont();

        if (i == activeSysIdx)
        {
            spr->loadFont(ComfortaaBold21);
            spr->setTextDatum(MR_DATUM);
            spr->setTextColor(sektionSystem);
            spr->drawString(ot(OKey::LANG_ACTIVE), r.x + r.w - L::Language::ACTIVE_RIGHT, r.y + r.h / 2);
            spr->unloadFont();
        }
    }
}

Settings::Rect Settings::themeRowRect(int idx) const
{
    const int y = CONTENT_Y + idx * (L::Theme::ROW_H + L::Theme::ROW_GAP);
    return {CONTENT_X, y, CONTENT_W, L::Theme::ROW_H};
}

void Settings::drawPanelTheme(UiCanvas *spr)
{
    const uint16_t zeile = farbe(Rolle::EinZeile), zeileGewaehlt = farbe(Rolle::EinZeileGewaehlt), sektionSystem = farbe(Rolle::EinSektionSystem);
    const uint16_t textHell = farbe(Rolle::GemTextHell), text = farbe(Rolle::EinText);
    for (uint8_t row = 0; row < ThemenAnzahl; ++row)
    {
        const Thema &thema = themaNachIndex(ThemeRows[row].index);
        const Rect r = themeRowRect(row);
        const bool selected = ThemeRows[row].index == m_draft.theme;
        spr->fillSmoothRoundRect(r.x, r.y, r.w, r.h, L::Language::ROW_R, selected ? zeileGewaehlt : zeile);
        if (selected)
            spr->fillRect(r.x, r.y + ACCENT_INSET, ITEM_ACCENT_W, r.h - 2 * ACCENT_INSET, sektionSystem);
        drawThemePreview(*spr, r.x + L::Theme::PREVIEW_X, r.y + ACCENT_INSET, thema);

        spr->loadFont(ComfortaaBold24);
        spr->setTextDatum(ML_DATUM);
        spr->setTextColor(selected ? textHell : text);
        spr->drawString(ot(ThemeRows[row].name), r.x + L::Theme::TEXT_X, r.y + r.h / 2);
        spr->unloadFont();

        if (&thema == &themaAktiv())
        {
            spr->loadFont(ComfortaaBold21);
            spr->setTextDatum(MR_DATUM);
            spr->setTextColor(sektionSystem);
            spr->drawString(ot(OKey::LANG_ACTIVE), r.x + r.w - L::Language::ACTIVE_RIGHT, r.y + r.h / 2);
            spr->unloadFont();
        }
    }
}

Settings::Rect Settings::pinLockToggleRect() const
{
    return {CONTENT_X + CONTENT_W - TOGGLE_W, CONTENT_Y, TOGGLE_W, TOGGLE_H};
}

void Settings::drawPanelSecurity(UiCanvas *spr)
{
    if (!m_numpadLayoutValid)
    {
        m_numpad.computeLayout(CONTENT_W, CONTENT_H - Numpad::REQUIRED_H);
        m_numpadLayoutValid = true;
    }

    SettingsTextKey hintKey = OKey::SEC_PIN_HINT;
    uint16_t hintColor = farbe(Rolle::EinTextNeben);
    if (m_confirmPinDisable)
    {
        hintKey = m_pinMismatch ? OKey::SEC_WRONG_PIN : OKey::SEC_CONFIRM_OFF;
        if (m_pinMismatch)
            hintColor = farbe(Rolle::GemWarnung);
    }
    else if (m_pinConfirming)
    {
        hintKey = OKey::SEC_PIN_REPEAT;
    }
    else if (m_pinMismatch)
    {
        hintKey = OKey::SEC_PIN_MISMATCH;
        hintColor = farbe(Rolle::GemWarnung);
    }
    else if (pinPending() && m_pinLen == 0)
    {
        hintKey = OKey::SEC_PIN_READY;
        hintColor = farbe(Rolle::EinSektionSystem);
    }

    const Rect toggle = pinLockToggleRect();
    drawToggle(spr, toggle, m_draft.pinLock, farbe(Rolle::GemFlaeche));
    spr->loadFont(ComfortaaBold24);
    spr->setTextDatum(ML_DATUM);
    spr->setTextColor(farbe(Rolle::EinText));
    spr->drawString(ot(OKey::SEC_TOGGLE), CONTENT_X, toggle.y + toggle.h / 2);
    spr->setTextDatum(MC_DATUM);
    spr->setTextColor(hintColor);
    spr->drawString(ot(hintKey), CONTENT_X + CONTENT_W / 2, CONTENT_Y + L::Security::HINT_Y);
    spr->unloadFont();

    const int dotsY = CONTENT_Y + L::Security::DOTS_Y;
    const int totalW = 3 * PIN_DOT_GAP;
    const int startX = CONTENT_X + (CONTENT_W - totalW) / 2;
    const uint16_t punktVoll = farbe(Rolle::EinText), punktLeer = farbe(Rolle::EinPunktLeer);
    for (int i = 0; i < 4; ++i)
    {
        const int cx = startX + i * PIN_DOT_GAP;
        if (i < m_pinLen)
            spr->fillSmoothCircle(cx, dotsY, PIN_DOT_R, punktVoll);
        else
            spr->fillSmoothCircle(cx, dotsY, PIN_DOT_R, punktLeer);
    }

    m_numpad.draw(spr, CONTENT_X, CONTENT_Y, -1);
}

Settings::Rect Settings::timeFieldRect(int idx) const
{
    const int y = CONTENT_Y + idx * (TIME_FIELD_H + TIME_FIELD_GAP);
    return Rect{CONTENT_X, y, CONTENT_W, TIME_FIELD_H};
}

Settings::Rect Settings::timeStepperColumnRect(int col) const
{
    const int totalW = TIME_STEPPER_TOTAL_W;
    const int startX = CONTENT_X + (CONTENT_W - totalW) / 2;
    const int x = startX + col * (TIME_STEPPER_COL_W + TIME_STEPPER_COL_GAP);
    const int y = CONTENT_Y + TIME_STEPPER_BLOCK_Y;
    return Rect{x, y, TIME_STEPPER_COL_W, TIME_STEPPER_TOTAL_H};
}

Settings::Rect Settings::timeStepperUpRect(int col) const
{
    const Rect c = timeStepperColumnRect(col);
    return Rect{c.x, c.y, c.w, TIME_STEPPER_BTN_H};
}

Settings::Rect Settings::timeStepperDownRect(int col) const
{
    const Rect c = timeStepperColumnRect(col);
    return Rect{c.x, c.y + TIME_STEPPER_BTN_H + TIME_STEPPER_VAL_H + 2 * TIME_STEPPER_VAL_GAP, c.w, TIME_STEPPER_BTN_H};
}

Settings::Rect Settings::timeStepperValueRect(int col) const
{
    const Rect c = timeStepperColumnRect(col);
    return Rect{c.x, c.y + TIME_STEPPER_BTN_H + TIME_STEPPER_VAL_GAP, c.w, TIME_STEPPER_VAL_H};
}

Settings::Rect Settings::timeSetButtonRect() const
{
    const int x = CONTENT_X + (CONTENT_W - TIME_SET_BTN_W) / 2;
    const int y = CONTENT_Y + TIME_STEPPER_BLOCK_Y + TIME_STEPPER_TOTAL_H + L::Time::SET_GAP_Y;
    return Rect{x, y, TIME_SET_BTN_W, TIME_SET_BTN_H};
}

void Settings::clampManualDay()
{
    const int maxDay = daysInMonth(m_manualYear, m_manualMonth);
    if (m_manualDay > maxDay)
        m_manualDay = maxDay;
    if (m_manualDay < 1)
        m_manualDay = 1;
}

Settings::Rect Settings::timeWindowCellRect(int day, int hour) const
{
    const int startX = CONTENT_X;
    const int gridX = startX + TIME_WINDOW_DAY_LABEL_W;
    const int cellX = gridX + (hour * TIME_WINDOW_GRID_W) / TIME_WINDOW_HOURS;
    const int nextCellX = gridX + ((hour + 1) * TIME_WINDOW_GRID_W) / TIME_WINDOW_HOURS;
    const int rowY = TIME_WINDOW_TOP + TIME_WINDOW_HEADER_H + day * TIME_WINDOW_CELL_H;
    return Rect{cellX, rowY, nextCellX - cellX, TIME_WINDOW_CELL_H};
}

void Settings::drawPanelTimeWindow(UiCanvas *spr)
{
    if (!spr)
        return;

    const char *daysDe[TIME_WINDOW_DAYS] = {"Mo", "Di", "Mi", "Do", "Fr", "Sa", "So"};
    const char *daysEn[TIME_WINDOW_DAYS] = {"Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"};
    const char **days = (m_language == LangIndex::EN) ? daysEn : daysDe;

    const int totalW = TIME_WINDOW_DAY_LABEL_W + TIME_WINDOW_GRID_W;
    const int startX = CONTENT_X;
    const int gridX = startX + TIME_WINDOW_DAY_LABEL_W;
    const int headerY = TIME_WINDOW_TOP;
    const int rowsY = TIME_WINDOW_TOP + TIME_WINDOW_HEADER_H;
    const int rowsH = TIME_WINDOW_DAYS * TIME_WINDOW_CELL_H;
    const uint16_t rahmen = farbe(Rolle::GemRahmen), haken = farbe(Rolle::EinSektionSystem), trenner = farbe(Rolle::EinTrenner);
    const uint16_t textNeben = farbe(Rolle::EinTextNeben), textHell = farbe(Rolle::GemTextHell);

    spr->fillSmoothRoundRect(startX, rowsY - TIME_WINDOW_FRAME_PAD_Y,
                             totalW, rowsH + 2 * TIME_WINDOW_FRAME_PAD_Y,
                             TIME_WINDOW_FRAME_R, rahmen);
    spr->fillSmoothRoundRect(startX + TIME_WINDOW_FRAME_INSET, rowsY - TIME_WINDOW_FRAME_INSET,
                             totalW - 2 * TIME_WINDOW_FRAME_INSET, rowsH + 2 * TIME_WINDOW_FRAME_INSET,
                             TIME_WINDOW_INNER_R, farbe(Rolle::EinZeile));

    spr->loadFont(ComfortaaBold15);
    spr->setTextDatum(MC_DATUM);
    spr->setTextColor(textNeben);
    for (int hour = 0; hour < TIME_WINDOW_HOURS; ++hour)
    {
        char label[TIME_WINDOW_HOUR_LABEL_BUF];
        stringutils::safeFormat(label, "%02d", hour);
        const Rect cell = timeWindowCellRect(0, hour);
        spr->drawString(label,
                        cell.x + cell.w / 2,
                        headerY + TIME_WINDOW_HEADER_H / 2);
    }

    for (int day = 0; day < TIME_WINDOW_DAYS; ++day)
    {
        const int rowY = rowsY + day * TIME_WINDOW_CELL_H;

        spr->setTextDatum(ML_DATUM);
        spr->setTextColor(textHell);
        spr->drawString(days[day], startX + TIME_WINDOW_DAY_TEXT_X, rowY + TIME_WINDOW_CELL_H / 2);

        for (int hour = 0; hour < TIME_WINDOW_HOURS; ++hour)
        {
            const Rect cell = timeWindowCellRect(day, hour);
            const int boxX = cell.x + (cell.w - TIME_WINDOW_CHECK_SIZE) / 2;
            const int boxY = cell.y + (cell.h - TIME_WINDOW_CHECK_SIZE) / 2;
            const bool checked = ((m_draft.timeWindows[day] & (1UL << hour)) != 0);
            const uint16_t color = checked ? haken : rahmen;

            for (int line = 0; line < L::Window::CHECK_LINE; ++line)
                spr->drawRect(boxX + line, boxY + line, TIME_WINDOW_CHECK_SIZE - 2 * line, TIME_WINDOW_CHECK_SIZE - 2 * line, color);
            if (checked)
            {
                // Haken des Originals: (-3,-1) (-1,2) (4,-4)
                const int cx = boxX + TIME_WINDOW_CHECK_SIZE / 2;
                const int cy = boxY + TIME_WINDOW_CHECK_SIZE / 2;
                spr->drawWideLine(cx - 5, cy - 2, cx - 2, cy + 3, TIME_WINDOW_CHECK_LINE_W, color);
                spr->drawWideLine(cx - 2, cy + 3, cx + 6, cy - 6, TIME_WINDOW_CHECK_LINE_W, color);
            }
        }

        if (day + 1 < TIME_WINDOW_DAYS)
            spr->fillRect(startX + TIME_WINDOW_FRAME_INSET,
                          rowY + TIME_WINDOW_CELL_H - TIME_WINDOW_FRAME_INSET,
                          totalW - 2 * TIME_WINDOW_FRAME_INSET, LINE,
                          trenner);
    }

    for (int hour = 0; hour <= TIME_WINDOW_HOURS; ++hour)
    {
        const int x = gridX + (hour * TIME_WINDOW_GRID_W) / TIME_WINDOW_HOURS;
        spr->fillRect(x, rowsY, LINE, rowsH, trenner);
    }
    spr->unloadFont();

    const int hintY = rowsY + rowsH + TIME_WINDOW_HINT_GAP_Y;
    spr->loadFont(ComfortaaBold21);
    spr->setTextDatum(TL_DATUM);
    spr->setTextColor(textNeben);
    spr->drawString(ot(OKey::TIME_WINDOW_HINT_ACTIVE), TIME_WINDOW_HINT_X, hintY);
    spr->drawString(ot(OKey::TIME_WINDOW_HINT_SLEEP), TIME_WINDOW_HINT_X, hintY + TIME_WINDOW_HINT_LINE_H);
    spr->unloadFont();
}

Settings::Rect Settings::audioStepperColumnRect(int row) const
{
    const int y = CONTENT_Y + AUDIO_ROW_FIRST_Y + row * (AUDIO_ROW_H + AUDIO_ROW_GAP);
    return Rect{CONTENT_X, y, CONTENT_W, AUDIO_ROW_H};
}

Settings::Rect Settings::audioStepperDownRect(int row) const
{
    const Rect c = audioStepperColumnRect(row);
    const int blockW = AUDIO_STEP_BTN_W * 2 + AUDIO_STEP_VAL_W + AUDIO_STEP_GAP * 2;
    const int x = c.x + c.w - blockW;
    return Rect{x, c.y, AUDIO_STEP_BTN_W, AUDIO_ROW_H};
}

Settings::Rect Settings::audioStepperValueRect(int row) const
{
    const Rect down = audioStepperDownRect(row);
    return Rect{down.x + AUDIO_STEP_BTN_W + AUDIO_STEP_GAP, down.y, AUDIO_STEP_VAL_W, AUDIO_ROW_H};
}

Settings::Rect Settings::audioStepperUpRect(int row) const
{
    const Rect v = audioStepperValueRect(row);
    return Rect{v.x + AUDIO_STEP_VAL_W + AUDIO_STEP_GAP, v.y, AUDIO_STEP_BTN_W, AUDIO_ROW_H};
}

void Settings::drawPanelAudio(UiCanvas *spr)
{
    const SettingsTextKey labels[AUDIO_ROW_COUNT + 1] = {
        OKey::AUDIO_BASS,
        OKey::AUDIO_MID,
        OKey::AUDIO_HIGH,
        OKey::AUDIO_WIDTH,
        OKey::AUDIO_LOUDNESS,
        OKey::AUDIO_SPEAKERS};
    const SettingsTextKey valueFormats[AUDIO_ROW_COUNT] = {
        OKey::AUDIO_HZ_FMT,
        OKey::AUDIO_HZ_FMT,
        OKey::AUDIO_HZ_FMT,
        OKey::AUDIO_PCT_FMT,
        OKey::AUDIO_PCT_FMT};
    const uint16_t values[AUDIO_ROW_COUNT] = {m_soundDraft.eqLow, m_soundDraft.eqPeak, m_soundDraft.eqHigh, m_soundDraft.width, m_soundDraft.loudness};
    const uint16_t textHell = farbe(Rolle::GemTextHell), rahmen = farbe(Rolle::GemRahmen), taste = farbe(Rolle::GemTastenFlaeche);
    const uint16_t tastenText = farbe(Rolle::EinTastenText), wert = farbe(Rolle::EinSektionSystem);

    for (int row = 0; row <= AUDIO_ROW_COUNT; ++row)
    {
        const Rect c = audioStepperColumnRect(row);

        spr->loadFont(ComfortaaBold24);
        spr->setTextDatum(ML_DATUM);
        spr->setTextColor(textHell);
        spr->drawString(ot(labels[row]), c.x + L::Sound::LABEL_X, c.y + AUDIO_ROW_H / 2);
        spr->unloadFont();

        const Rect down = audioStepperDownRect(row);
        spr->fillSmoothRoundRect(down.x, down.y, down.w, down.h, BTN_R, rahmen);
        spr->fillSmoothRoundRect(down.x + BORDER, down.y + BORDER, down.w - 2 * BORDER, down.h - 2 * BORDER, BUTTON_INNER_R, taste);
        spr->loadFont(ComfortaaBold45);
        spr->setTextDatum(MC_DATUM);
        spr->setTextColor(tastenText);
        spr->drawString("-", down.x + down.w / 2, down.y + down.h / 2);
        spr->unloadFont();

        const Rect up = audioStepperUpRect(row);
        spr->fillSmoothRoundRect(up.x, up.y, up.w, up.h, BTN_R, rahmen);
        spr->fillSmoothRoundRect(up.x + BORDER, up.y + BORDER, up.w - 2 * BORDER, up.h - 2 * BORDER, BUTTON_INNER_R, taste);
        spr->loadFont(ComfortaaBold45);
        spr->setTextDatum(MC_DATUM);
        spr->setTextColor(tastenText);
        spr->drawString("+", up.x + up.w / 2, up.y + up.h / 2);
        spr->unloadFont();

        const Rect v = audioStepperValueRect(row);
        char buf[16];
        if (row == AUDIO_ROW_COUNT)
            STRF(buf, m_soundDraft.speakers ? OKey::AUDIO_SPEAKERS_ON : OKey::AUDIO_SPEAKERS_OFF);
        else
            STRF(buf, valueFormats[row], static_cast<int>(values[row]));
        spr->loadFont(ComfortaaBold45);
        spr->setTextDatum(MC_DATUM);
        spr->setTextColor(wert);
        spr->drawString(buf, v.x + v.w / 2, v.y + v.h / 2 + L::Sound::VALUE_OFFSET_Y);
        spr->unloadFont();
    }
}

Settings::Rect Settings::appearanceTileRect(int index) const
{
    const int x = CONTENT_X + index * (APPEARANCE_TILE_W + APPEARANCE_TILE_GAP);
    return Rect{x, CONTENT_Y + APPEARANCE_TILE_Y, APPEARANCE_TILE_W, APPEARANCE_TILE_H};
}

Settings::Rect Settings::appearanceSegmentRect(int rowY, int index, int count) const
{
    const int x0 = CONTENT_X + APPEARANCE_LABEL_W;
    const int platz = CONTENT_W - APPEARANCE_LABEL_W;
    const int w = (platz - (count - 1) * APPEARANCE_SEG_GAP) / count;
    return Rect{x0 + index * (w + APPEARANCE_SEG_GAP), CONTENT_Y + rowY, w, APPEARANCE_ROW_H};
}

void Settings::drawAppearanceSectionTitle(UiCanvas *spr, int y, SettingsTextKey titleKey)
{
    spr->loadFont(ComfortaaBold24);
    spr->setTextDatum(TL_DATUM);
    spr->setTextColor(farbe(Rolle::EinSektionSystem));
    spr->drawString(ot(titleKey), CONTENT_X + L::Appearance::TEXT_X, CONTENT_Y + y);
    spr->unloadFont();

    spr->fillRect(CONTENT_X, CONTENT_Y + y + APPEARANCE_SEC_LINE_DY, CONTENT_W, LINE, farbe(Rolle::GemRahmen));
}

void Settings::drawAppearanceHint(UiCanvas *spr, int y, SettingsTextKey key)
{
    spr->loadFont(ComfortaaBold24);
    spr->setTextDatum(TL_DATUM);
    spr->setTextColor(farbe(Rolle::EinTextNeben));
    spr->drawString(ot(key), CONTENT_X + L::Appearance::TEXT_X, CONTENT_Y + y);
    spr->unloadFont();
}

void Settings::drawAppearanceEyeTiles(UiCanvas *spr)
{
    static const SettingsTextKey NAMEN[] = {
        OKey::APPEARANCE_EYE_EM11,
        OKey::APPEARANCE_EYE_EM34,
        OKey::APPEARANCE_EYE_EM80,
        OKey::APPEARANCE_EYE_EM84,
    };
    static_assert(sizeof(NAMEN) / sizeof(NAMEN[0]) == APPEARANCE_EYE_MAX + 1,
                  "Zu jeder Röhre muss eine Beschriftung gehören");

    constexpr float VORSCHAU_PEGEL = 0.55f;
    constexpr float RING_DICKE = L::Appearance::RING;
    const uint16_t zeile = farbe(Rolle::EinZeile), zeileGewaehlt = farbe(Rolle::EinZeileGewaehlt), aktivRahmen = farbe(Rolle::EinSektionSystem), rahmen = farbe(Rolle::GemRahmen);
    const uint16_t ring = farbe(Rolle::GemAugenRing), textHell = farbe(Rolle::GemTextHell), text = farbe(Rolle::EinText);

    for (int i = 0; i <= APPEARANCE_EYE_MAX; ++i)
    {
        const Rect t = appearanceTileRect(i);
        const bool aktiv = (i == m_draft.eyeStyle);
        const uint16_t grund = aktiv ? zeileGewaehlt : zeile;

        spr->fillSmoothRoundRect(t.x, t.y, t.w, t.h, FIELD_R, aktiv ? aktivRahmen : rahmen);
        spr->fillSmoothRoundRect(t.x + BORDER, t.y + BORDER, t.w - 2 * BORDER, t.h - 2 * BORDER, FIELD_INNER_R, grund);

        const int cx = t.x + t.w / 2;
        const int cy = t.y + L::Appearance::EYE_Y + APPEARANCE_EYE_BOX / 2;

        switch (static_cast<MagicEyeStyle>(i))
        {
        case MagicEyeStyle::Em80:
            spr->drawEm80MagicEye(cx, cy, L::Appearance::EM80_HALF_W,
                                  L::Appearance::EM80_HALF_H,
                                  VORSCHAU_PEGEL, ring, grund);
            break;

        case MagicEyeStyle::Em84:
            spr->drawEm84MagicEye(cx, cy, L::Appearance::EM84_HALF_W,
                                  L::Appearance::EM84_HALF_H,
                                  VORSCHAU_PEGEL, ring, grund);
            break;

        default:
        {
            const float r = APPEARANCE_EYE_BOX / 2.0f - 1.5f; // Original 70/2-1
            spr->drawSmoothCircle(cx, cy, r, RING_DICKE, ring, grund);

            if (static_cast<MagicEyeStyle>(i) == MagicEyeStyle::Em34)
                spr->drawEm34MagicEye(cx, cy, r - RING_DICKE, VORSCHAU_PEGEL, grund);
            else
                spr->drawEm11MagicEye(cx, cy, r - RING_DICKE, VORSCHAU_PEGEL, grund);
            break;
        }
        }

        spr->loadFont(ComfortaaBold24);
        spr->setTextDatum(MC_DATUM);
        spr->setTextColor(aktiv ? textHell : text);
        spr->drawString(ot(NAMEN[i]), cx, t.y + t.h - L::Appearance::NAME_DY);
        spr->unloadFont();
    }
}

void Settings::drawAppearanceSegments(UiCanvas *spr, int rowY, SettingsTextKey labelKey,
                                      const char *const *values, int count, int selected)
{
    const uint16_t textHell = farbe(Rolle::GemTextHell), aktivRahmen = farbe(Rolle::EinSektionSystem), rahmen = farbe(Rolle::GemRahmen);
    const uint16_t auswahl = farbe(Rolle::GemAuswahl), taste = farbe(Rolle::GemTastenFlaeche), tastenText = farbe(Rolle::EinTastenText);
    spr->loadFont(ComfortaaBold24);
    spr->setTextDatum(ML_DATUM);
    spr->setTextColor(textHell);
    spr->drawString(ot(labelKey), CONTENT_X + L::Appearance::TEXT_X, CONTENT_Y + rowY + APPEARANCE_ROW_H / 2);
    spr->unloadFont();

    for (int i = 0; i < count; ++i)
    {
        const Rect r = appearanceSegmentRect(rowY, i, count);
        const bool aktiv = (i == selected);

        spr->fillSmoothRoundRect(r.x, r.y, r.w, r.h, BTN_R, aktiv ? aktivRahmen : rahmen);
        spr->fillSmoothRoundRect(r.x + BORDER, r.y + BORDER, r.w - 2 * BORDER, r.h - 2 * BORDER, BUTTON_INNER_R, aktiv ? auswahl : taste);

        spr->loadFont(ComfortaaBold24);
        spr->setTextDatum(MC_DATUM);
        spr->setTextColor(aktiv ? textHell : tastenText);
        spr->drawString(values[i], r.x + r.w / 2, r.y + r.h / 2);
        spr->unloadFont();
    }
}

void Settings::drawPanelAppearance(UiCanvas *spr)
{
    drawAppearanceSectionTitle(spr, APPEARANCE_SEC1_Y, OKey::APPEARANCE_MAGIC_EYE);
    drawAppearanceEyeTiles(spr);
    drawAppearanceHint(spr, APPEARANCE_HINT1_Y, OKey::APPEARANCE_HINT);

    drawAppearanceSectionTitle(spr, APPEARANCE_SEC2_Y, OKey::APPEARANCE_SECTION_VU);

    // Angegeben ist die Einschwingzeit bis 99 Prozent, Reihenfolge wie VuBallistics.
    static const char *const BALLISTIK[] = {"150 ms", "300 ms", "600 ms"};
    static_assert(sizeof(BALLISTIK) / sizeof(BALLISTIK[0]) == APPEARANCE_VU_MAX + 1,
                  "Zu jeder Trägheit muss eine Zeitangabe gehören");

    drawAppearanceSegments(spr, APPEARANCE_ROW1_Y, OKey::APPEARANCE_VU_BALLISTICS,
                           BALLISTIK, APPEARANCE_VU_MAX + 1, m_draft.vuBallistics);
    drawAppearanceHint(spr, APPEARANCE_ROW1_Y + APPEARANCE_HINT_DY,
                       OKey::APPEARANCE_BALLISTICS_HINT);

    // Pegel unter Vollaussteuerung, bei dem der Zeiger 0 VU erreicht. Reihenfolge wie VU_ALIGNMENT_DB.
    static const char *const PEGEL[] = {"-2 dB", "-6 dB", "-10 dB", "-14 dB", "-18 dB"};
    static_assert(sizeof(PEGEL) / sizeof(PEGEL[0]) == APPEARANCE_SENS_MAX + 1,
                  "Zu jedem Bezugspegel muss eine Beschriftung gehören");

    drawAppearanceSegments(spr, APPEARANCE_ROW2_Y, OKey::APPEARANCE_VU_LEVEL,
                           PEGEL, APPEARANCE_SENS_MAX + 1, m_draft.vuSensitivity);
    drawAppearanceHint(spr, APPEARANCE_ROW2_Y + APPEARANCE_HINT_DY,
                       OKey::APPEARANCE_LEVEL_HINT);
}

Settings::Rect Settings::screenStepRect(bool plus) const
{
    const int blockW = AUDIO_STEP_BTN_W * 2 + AUDIO_STEP_VAL_W + AUDIO_STEP_GAP * 2;
    const int x = CONTENT_X + CONTENT_W - blockW + (plus ? AUDIO_STEP_BTN_W + AUDIO_STEP_GAP + AUDIO_STEP_VAL_W + AUDIO_STEP_GAP : 0);
    return Rect{x, CONTENT_Y + L::Screen::ROW1_Y, AUDIO_STEP_BTN_W, AUDIO_ROW_H};
}

Settings::Rect Settings::screenValueRect() const
{
    const Rect minus = screenStepRect(false);
    return Rect{minus.x + AUDIO_STEP_BTN_W + AUDIO_STEP_GAP, minus.y, AUDIO_STEP_VAL_W, AUDIO_ROW_H};
}

void Settings::drawPanelScreen(UiCanvas *spr)
{
    drawAppearanceSectionTitle(spr, APPEARANCE_SEC1_Y, OKey::SCREEN_SECTION_BRIGHTNESS);

    // Aufbau wie eine Klangzeile: Beschriftung links, Minus, Wert und Plus rechtsbündig
    spr->loadFont(ComfortaaBold24);
    spr->setTextDatum(ML_DATUM);
    spr->setTextColor(farbe(Rolle::GemTextHell));
    spr->drawString(ot(OKey::SCREEN_BRIGHTNESS), CONTENT_X + L::Sound::LABEL_X, CONTENT_Y + L::Screen::ROW1_Y + AUDIO_ROW_H / 2);
    spr->unloadFont();
    const uint16_t rahmen = farbe(Rolle::GemRahmen), taste = farbe(Rolle::GemTastenFlaeche), tastenText = farbe(Rolle::EinTastenText);
    for (const bool plus : {false, true})
    {
        const Rect b = screenStepRect(plus);
        spr->fillSmoothRoundRect(b.x, b.y, b.w, b.h, BTN_R, rahmen);
        spr->fillSmoothRoundRect(b.x + BORDER, b.y + BORDER, b.w - 2 * BORDER, b.h - 2 * BORDER, BUTTON_INNER_R, taste);
        spr->loadFont(ComfortaaBold45);
        spr->setTextDatum(MC_DATUM);
        spr->setTextColor(tastenText);
        spr->drawString(plus ? "+" : "-", b.x + b.w / 2, b.y + b.h / 2);
        spr->unloadFont();
    }
    const Rect v = screenValueRect();
    char value[16];
    STRF(value, OKey::AUDIO_PCT_FMT, static_cast<int>(m_draft.brightness));
    spr->loadFont(ComfortaaBold45);
    spr->setTextDatum(MC_DATUM);
    spr->setTextColor(farbe(Rolle::EinSektionSystem));
    spr->drawString(value, v.x + v.w / 2, v.y + v.h / 2 + L::Sound::VALUE_OFFSET_Y);
    spr->unloadFont();
    drawAppearanceHint(spr, L::Screen::HINT1_Y, OKey::SCREEN_BRIGHTNESS_HINT);

    drawAppearanceSectionTitle(spr, L::Screen::SEC2_Y, OKey::SCREEN_SECTION_ROTATION);
    const char *const automatic[] = {ot(OKey::SCREEN_AUTO_ON), ot(OKey::SCREEN_AUTO_OFF)};
    drawAppearanceSegments(spr, L::Screen::ROW2_Y, OKey::SCREEN_AUTO, automatic, 2, m_draft.autoRotate ? 0 : 1);
    drawAppearanceHint(spr, L::Screen::HINT2_Y, OKey::SCREEN_AUTO_HINT);
    // Mit Automatik ist keine feste Ausrichtung gewählt.
    const char *const rotations[] = {ot(OKey::SCREEN_ROTATION_NORMAL), ot(OKey::SCREEN_ROTATION_FLIPPED)};
    drawAppearanceSegments(spr, L::Screen::ROW3_Y, OKey::SCREEN_ROTATION, rotations, 2, m_draft.autoRotate ? -1 : m_draft.rotation == RotationFlipped ? 1
                                                                                                                                                      : 0);
    drawAppearanceHint(spr, L::Screen::HINT3_Y, OKey::SCREEN_ROTATION_HINT);
}

void Settings::drawPanelTime(UiCanvas *spr)
{
    if (!spr)
        return;

    if (!m_manualInit)
        initManualFromRtc();

    const char *fieldLabels[TIME_FIELD_COUNT] = {
        reinterpret_cast<const char *>(ot(OKey::TIME_TZ_LABEL)),
        reinterpret_cast<const char *>(ot(OKey::TIME_NTP_LABEL)),
        reinterpret_cast<const char *>(ot(OKey::TIME_SYNC_LABEL)),
    };

    char tzValue[64];
    stringutils::safeCopy(tzValue, m_draft.timezone);
    char ntpValue[64];
    stringutils::safeCopy(ntpValue, m_draft.ntpServer);
    char syncValue[40];
    intervalText(m_draft.syncIntervalMs, m_language, syncValue, sizeof(syncValue));
    const char *fieldValues[TIME_FIELD_COUNT] = {tzValue, ntpValue, syncValue};
    const SettingsTextKey notSetKeys[TIME_FIELD_COUNT] = {
        OKey::TIME_TZ_NOT_SET,
        OKey::TIME_NTP_NOT_SET,
        OKey::TIME_TZ_NOT_SET, // Das Intervall hat immer einen Wert, dieser Schlüssel greift nie
    };

    const bool tzDirty = strcmp(m_draft.timezone, m_baseline.timezone) != 0;
    const bool ntpDirty = strcmp(m_draft.ntpServer, m_baseline.ntpServer) != 0;
    const bool syncDirty = (m_draft.syncIntervalMs != m_baseline.syncIntervalMs);
    const bool dirty[TIME_FIELD_COUNT] = {tzDirty, ntpDirty, syncDirty};
    const uint16_t rahmen = farbe(Rolle::GemRahmen), zeile = farbe(Rolle::EinZeile), verbindung = farbe(Rolle::EinSektionVerbindung);
    const uint16_t textNeben = farbe(Rolle::EinTextNeben), text = farbe(Rolle::EinText), textHell = farbe(Rolle::GemTextHell);
    const uint16_t taste = farbe(Rolle::GemTastenFlaeche), tastenText = farbe(Rolle::EinTastenText);

    for (int i = 0; i < TIME_FIELD_COUNT; ++i)
    {
        const Rect r = timeFieldRect(i);
        spr->fillSmoothRoundRect(r.x, r.y, r.w, r.h, FIELD_R, rahmen);
        spr->fillSmoothRoundRect(r.x + BORDER, r.y + BORDER, r.w - 2 * BORDER, r.h - 2 * BORDER, FIELD_INNER_R, zeile);
        if (dirty[i])
            spr->fillRect(r.x + BORDER, r.y + ACCENT_INSET, ITEM_ACCENT_W, r.h - 2 * ACCENT_INSET, verbindung);

        spr->loadFont(ComfortaaBold24);
        spr->setTextDatum(TL_DATUM);
        spr->setTextColor(textNeben);
        spr->drawString(fieldLabels[i], r.x + TIME_FIELD_PAD_X, r.y + TIME_FIELD_LABEL_Y);

        const bool hasValue = fieldValues[i][0] != '\0';
        spr->setTextColor(hasValue ? text : textNeben);
        const char *valueText = hasValue
                                    ? fieldValues[i]
                                    : reinterpret_cast<const char *>(ot(notSetKeys[i]));
        spr->drawString(valueText, r.x + TIME_FIELD_PAD_X, r.y + TIME_FIELD_VALUE_Y);
        spr->unloadFont();
    }

    // Schrittgeberblock mit fünf Spalten in sprachabhängiger Datums- und Zeitreihenfolge
    clampManualDay();
    const int manualValues[FIELD_COUNT] = {
        m_manualYear, m_manualMonth, m_manualDay, m_manualHour, m_manualMinute};

    spr->loadFont(ComfortaaBold24);
    for (int col = 0; col < 5; ++col)
    {
        const Rect up = timeStepperUpRect(col);
        spr->fillSmoothRoundRect(up.x, up.y, up.w, up.h, BTN_R, rahmen);
        spr->fillSmoothRoundRect(up.x + BORDER, up.y + BORDER, up.w - 2 * BORDER, up.h - 2 * BORDER, BUTTON_INNER_R, taste);
        spr->setTextDatum(MC_DATUM);
        spr->setTextColor(tastenText);
        spr->drawString("+", up.x + up.w / 2, up.y + up.h / 2);

        const Rect down = timeStepperDownRect(col);
        spr->fillSmoothRoundRect(down.x, down.y, down.w, down.h, BTN_R, rahmen);
        spr->fillSmoothRoundRect(down.x + BORDER, down.y + BORDER, down.w - 2 * BORDER, down.h - 2 * BORDER, BUTTON_INNER_R, taste);
        spr->setTextDatum(MC_DATUM);
        spr->setTextColor(tastenText);
        spr->drawString("-", down.x + down.w / 2, down.y + down.h / 2);
    }
    spr->unloadFont();

    // Wert-Anzeigen: Jahr vierstellig, Rest zweistellig mit führender Null. Datum (Spalten links) hell, Uhrzeit (Spalten rechts) in Petrol, damit beide Gruppen visuell auseinanderzuhalten sind. Y-Offset gleicht die optische Asymmetrie der Ziffern aus (keine Unterlängen).
    constexpr int VALUE_Y_OFFSET = L::Time::VALUE_OFFSET_Y;
    spr->loadFont(ComfortaaBold45);
    spr->setTextDatum(MC_DATUM);
    for (int col = 0; col < 5; ++col)
    {
        const int field = stepperColToField(col);
        const Rect v = timeStepperValueRect(col);

        char buf[8];
        if (field == FIELD_YEAR)
            stringutils::safeFormat(buf, "%d", manualValues[field]);
        else
            stringutils::safeFormat(buf, "%02d", manualValues[field]);

        const bool isTime = (field == FIELD_HOUR) || (field == FIELD_MINUTE);
        spr->setTextColor(isTime ? verbindung : textHell);
        spr->drawString(buf, v.x + v.w / 2, v.y + v.h / 2 + VALUE_Y_OFFSET);
    }
    spr->unloadFont();

    // "Zeit setzen"-Knopf
    const Rect btn = timeSetButtonRect();
    spr->fillSmoothRoundRect(btn.x, btn.y, btn.w, btn.h, BTN_R, rahmen);
    spr->fillSmoothRoundRect(btn.x + BORDER, btn.y + BORDER, btn.w - 2 * BORDER, btn.h - 2 * BORDER, BUTTON_INNER_R, taste);
    spr->loadFont(LeagueGothicCond51);
    spr->setTextDatum(MC_DATUM);
    spr->setTextColor(tastenText);
    spr->drawString(ot(OKey::TIME_MANUAL_BTN),
                    btn.x + btn.w / 2, btn.y + btn.h / 2);
    spr->unloadFont();
}

void Settings::drawPanelInfo(UiCanvas *spr)
{
    constexpr int COL_GAP = L::Info::COL_GAP;
    constexpr int COL_W = L::Info::COL_W;
    constexpr int LEFT_X = CONTENT_X;
    constexpr int RIGHT_X = CONTENT_X + COL_W + COL_GAP;
    constexpr int HDR_H = L::Info::HDR_H;
    constexpr int HDR_PAD_X = L::Info::HDR_PAD_X;
    constexpr int ROW_H = L::Info::ROW_H;
    constexpr int ROW_PAD_X = L::Info::ROW_PAD_X;
    constexpr int I2C_LABEL_GAP = L::Info::I2C_LABEL_GAP; ///< Abstand zwischen I2C-Badge und Adressliste
    constexpr int I2C_BOTTOM_GAP = L::Info::I2C_BOTTOM_GAP; ///< Abstand der I2C-Zeile zum unteren Rand
    constexpr float I2C_BADGE_COLOR_SCALE = 0.35f; ///< Abdunkelung der I2C-Badge-Farbe
    constexpr int SIGNAL_BAR_W = L::Info::SIGNAL_W;
    constexpr int SIGNAL_BAR_H = L::Info::SIGNAL_H;

    char ssidBuf[40] = {0};
    char hostBuf[40] = {0};
    char ipBuf[24] = {0};
    char gatewayBuf[24] = {0};
    char macBuf[24] = {0};
    char channelBuf[24] = {0};
    char ntpBuf[40] = {0};
    char syncBuf[32] = {0};
    char fwBuf[32] = {0};
    char fwSizeBuf[24] = {0};
    char mcuBuf[40] = {0};
    char hardwareBuf[40] = {0};
    char heapBuf[40] = {0};
    char psramFreeBuf[32] = {0};
    char uptimeBuf[32] = {0};
    char uartBuf[24] = {0};
    char chipTempBuf[32] = {0};
    char ftpBuf[48] = {0};
    char i2cBuf[128] = {0};

    const bool wifiOn = m_status.wifiConnected;
    const char *notConn = ot(OKey::INFO_NOT_CONNECTED);
    stringutils::safeCopy(ssidBuf, wifiOn ? m_status.connectedSsid : notConn);
    stringutils::safeCopy(hostBuf, m_baseline.hostname);
    if (wifiOn)
    {
        snprintf(ipBuf, sizeof(ipBuf), "%u.%u.%u.%u", m_status.ipv4[0], m_status.ipv4[1], m_status.ipv4[2], m_status.ipv4[3]);
        snprintf(gatewayBuf, sizeof(gatewayBuf), "%u.%u.%u.%u", m_status.gateway[0], m_status.gateway[1], m_status.gateway[2], m_status.gateway[3]);
        STRF(channelBuf, OKey::INFO_CHANNEL_FMT, m_status.channel);
    }
    else
    {
        stringutils::safeCopy(ipBuf, notConn);
        stringutils::safeCopy(gatewayBuf, notConn);
        stringutils::safeCopy(channelBuf, notConn);
    }
    stringutils::safeCopy(macBuf, m_status.mac[0] ? m_status.mac : tr(SettingsKey::Unavailable));
    stringutils::safeCopy(ntpBuf, m_baseline.ntpServer);
    stringutils::safeCopy(syncBuf, m_status.ntpConfirmed ? ot(OKey::INFO_SYNC_OK) : ot(OKey::INFO_NEVER));
    stringutils::safeCopy(fwBuf, m_status.firmware);
    snprintf(fwSizeBuf, sizeof(fwSizeBuf), "%lu KB", static_cast<unsigned long>(m_status.firmwareBytes / 1024));
    stringutils::safeCopy(mcuBuf, "ESP32-P4");
    STRF(hardwareBuf, OKey::INFO_HARDWARE_FMT, static_cast<int>(m_status.flashBytes / (1024UL * 1024UL)), static_cast<int>(m_status.psramBytes / (1024UL * 1024UL)));
    snprintf(heapBuf, sizeof(heapBuf), "%lu KB", static_cast<unsigned long>(m_status.heapFree / 1024));
    // Wie im Webportal: freier PSRAM in KB und Anteil frei / gesamt, beides abgerundet
    if (m_status.psramBytes)
        STRF(psramFreeBuf, OKey::INFO_PSRAM_FREE_FMT, static_cast<unsigned long>(m_status.psramFree / 1024), static_cast<unsigned long>(static_cast<uint64_t>(m_status.psramFree) * 100 / m_status.psramBytes));
    else
        snprintf(psramFreeBuf, sizeof(psramFreeBuf), "%lu KB", static_cast<unsigned long>(m_status.psramFree / 1024));
    snprintf(uptimeBuf, sizeof(uptimeBuf), "%lud %02lu:%02lu", static_cast<unsigned long>(m_status.uptimeSeconds / 86400), static_cast<unsigned long>(m_status.uptimeSeconds / 3600 % 24), static_cast<unsigned long>(m_status.uptimeSeconds / 60 % 60));
    stringutils::safeCopy(uartBuf, "USB JTAG/Serial");
    // Temperatur des P4-Chips, sie liegt durch Eigenwärme über der Raumtemperatur.
    if (m_status.chipTempValid)
    {
        const int abs10 = std::abs(static_cast<int>(m_status.chipTempC10));
        STRF(chipTempBuf, OKey::INFO_CHIP_TEMP_FMT, m_status.chipTempC10 < 0 ? "-" : "", abs10 / 10, abs10 % 10);
    }
    else
        stringutils::safeCopy(chipTempBuf, ot(OKey::INFO_CHIP_TEMP_NOT_CHECKED));
    // Adresse mit Benutzer als FTP-URL, das Passwort gibt es nur im Webportal hinter der PIN.
    if (m_status.ftpEnabled && wifiOn)
        snprintf(ftpBuf, sizeof(ftpBuf), "%s, ftp://%s@%u.%u.%u.%u", ot(OKey::INFO_FTP_ON), FtpProtocol::User, m_status.ipv4[0], m_status.ipv4[1], m_status.ipv4[2], m_status.ipv4[3]);
    else
        stringutils::safeCopy(ftpBuf, ot(m_status.ftpEnabled ? OKey::INFO_FTP_ON : OKey::INFO_FTP_OFF));
    // Das Ergebnis des einmaligen Startscans steht getrennt für den internen Bus und Port A
    const auto busText = [&](BusScan state, const char *list)
    {
        return state == BusScan::NotChecked ? ot(OKey::INFO_I2C_NOT_CHECKED) : state == BusScan::Blocked ? ot(OKey::INFO_I2C_BUS_BLOCKED)
                                                                           : list[0]                     ? list
                                                                                                         : ot(OKey::INFO_I2C_NONE);
    };
    if (m_status.i2cPortA == BusScan::Done && !m_status.i2cPortAList[0])
        snprintf(i2cBuf, sizeof(i2cBuf), "%s, %s", busText(m_status.i2cInternal, m_status.i2cInternalList), ot(OKey::INFO_I2C_PORT_A_FREE));
    else
        snprintf(i2cBuf, sizeof(i2cBuf), "%s, Port A: %s", busText(m_status.i2cInternal, m_status.i2cInternalList), busText(m_status.i2cPortA, m_status.i2cPortAList));

    // Standardzeilen, die Signalzeile mit den Balken wird separat gezeichnet
    struct InfoRow
    {
        SettingsTextKey labelKey;
        const char *value;
    };

    // Linke Spalte: SSID, Host, IP, Gateway, MAC, Kanal, [Signal], NTP, Sync, FTP
    const InfoRow leftRowsTop[] = {
        {OKey::INFO_SSID, ssidBuf},
        {OKey::INFO_HOSTNAME, hostBuf},
        {OKey::INFO_IP, ipBuf},
        {OKey::INFO_GATEWAY, gatewayBuf},
        {OKey::INFO_MAC, macBuf},
        {OKey::INFO_CHANNEL, channelBuf},
    };
    const InfoRow leftRowsBot[] = {
        {OKey::INFO_NTP_SERVER, ntpBuf},
        {OKey::INFO_SYNC_STATUS, syncBuf},
        {OKey::INFO_FTP, ftpBuf},
    };

    // Rechte Spalte: Firmware-Version, FW-Belegung, MCU, RAM, UART, Chiptemperatur, Heap, PSRAM, Uptime. Flash gesamt fehlt, weil die Firmware-Belegung in KB den wichtigeren Diagnosehinweis liefert.
    const InfoRow rightRows[] = {
        {OKey::INFO_FIRMWARE, fwBuf},
        {OKey::INFO_FIRMWARE_SIZE, fwSizeBuf},
        {OKey::INFO_MCU, mcuBuf},
        {OKey::INFO_HARDWARE, hardwareBuf},
        {OKey::INFO_UART, uartBuf},
        {OKey::INFO_CHIP_TEMP, chipTempBuf},
        {OKey::INFO_HEAP_FREE, heapBuf},
        {OKey::INFO_PSRAM_FREE, psramFreeBuf},
        {OKey::INFO_UPTIME, uptimeBuf},
    };

    const uint16_t textNeben = farbe(Rolle::EinTextNeben), textHell = farbe(Rolle::GemTextHell), trenner = farbe(Rolle::EinTrenner);
    auto drawInfoRow = [&](int colX, int colW, int yTop, const InfoRow &row)
    {
        spr->setTextDatum(ML_DATUM);
        spr->setTextColor(textNeben);
        spr->drawString(ot(row.labelKey), colX + ROW_PAD_X, yTop + ROW_H / 2);
        spr->setTextDatum(MR_DATUM);
        spr->setTextColor(textHell);
        spr->drawString(row.value, colX + colW - ROW_PAD_X, yTop + ROW_H / 2);
        spr->fillRect(colX + ROW_PAD_X, yTop + ROW_H - LINE,
                      colW - 2 * ROW_PAD_X, LINE, trenner);
    };

    spr->loadFont(ComfortaaBold24);

    // Gedämpfte Kopffarben, getrennt von den hellen Akzentstreifen im Menü, weil eine vollflächige Sättigung dort grell wirkt.
    spr->fillSmoothRoundRect(LEFT_X, CONTENT_Y, COL_W, HDR_H, BTN_R, farbe(Rolle::EinInfoKopfNetz));
    spr->setTextDatum(ML_DATUM);
    spr->setTextColor(textHell);
    spr->drawString(ot(OKey::INFO_SECTION_NETWORK),
                    LEFT_X + HDR_PAD_X, CONTENT_Y + HDR_H / 2);

    spr->fillSmoothRoundRect(RIGHT_X, CONTENT_Y, COL_W, HDR_H, BTN_R, farbe(Rolle::EinInfoKopfSystem));
    spr->setTextDatum(ML_DATUM);
    spr->setTextColor(textHell);
    spr->drawString(ot(OKey::INFO_SECTION_SYSTEM),
                    RIGHT_X + HDR_PAD_X, CONTENT_Y + HDR_H / 2);

    // Linke Spalte: obere Standard-Zeilen
    int leftY = CONTENT_Y + HDR_H + L::Info::ROWS_GAP;
    for (const InfoRow &row : leftRowsTop)
    {
        drawInfoRow(LEFT_X, COL_W, leftY, row);
        leftY += ROW_H;
    }

    // Signal-Zeile mit Balken
    spr->setTextDatum(ML_DATUM);
    spr->setTextColor(textNeben);
    spr->drawString(ot(OKey::INFO_SIGNAL),
                    LEFT_X + ROW_PAD_X, leftY + ROW_H / 2);

    if (wifiOn)
    {
        const int32_t rssi = m_status.rssi;
        int percent = 0;
        if (rssi >= -50)
            percent = 100;
        else if (rssi <= -90)
            percent = 0;
        else
            percent = (rssi + 90) * 100 / 40;
        if (percent < 0)
            percent = 0;
        if (percent > 100)
            percent = 100;

        uint16_t barColor;
        if (percent >= 60)
            barColor = farbe(Rolle::EinSektionSystem);
        else if (percent >= 30)
            barColor = farbe(Rolle::EinSignalMittel);
        else
            barColor = farbe(Rolle::EinSignalSchwach);

        const int textRightX = LEFT_X + COL_W - ROW_PAD_X;
        const int barX = textRightX - SIGNAL_BAR_W;
        const int barY = leftY + ROW_H / 2 - SIGNAL_BAR_H / 2;
        spr->fillRoundRect(barX, barY, SIGNAL_BAR_W, SIGNAL_BAR_H, L::Info::SIGNAL_R, farbe(Rolle::GemRahmen));
        const int fillW = SIGNAL_BAR_W * percent / 100;
        if (fillW > 0)
            spr->fillRoundRect(barX, barY, fillW, SIGNAL_BAR_H, L::Info::SIGNAL_R, barColor);

        char sigText[20];
        stringutils::safeFormat(sigText, "%d dB (%d%%)",
                                static_cast<int>(rssi), percent);
        spr->setTextDatum(MR_DATUM);
        spr->setTextColor(textHell);
        spr->drawString(sigText, barX - L::Info::SIGNAL_GAP, leftY + ROW_H / 2);
    }
    else
    {
        spr->setTextDatum(MR_DATUM);
        spr->setTextColor(textNeben);
        spr->drawString(ot(OKey::INFO_NOT_CONNECTED),
                        LEFT_X + COL_W - ROW_PAD_X, leftY + ROW_H / 2);
    }
    spr->fillRect(LEFT_X + ROW_PAD_X, leftY + ROW_H - LINE,
                  COL_W - 2 * ROW_PAD_X, LINE, trenner);
    leftY += ROW_H;

    // Letzte drei Zeilen links: NTP-Server, Sync und FTP
    for (const InfoRow &row : leftRowsBot)
    {
        drawInfoRow(LEFT_X, COL_W, leftY, row);
        leftY += ROW_H;
    }

    // Rechte Spalte: System-Werte untereinander
    int rightY = CONTENT_Y + HDR_H + L::Info::ROWS_GAP;
    for (const InfoRow &row : rightRows)
    {
        drawInfoRow(RIGHT_X, COL_W, rightY, row);
        rightY += ROW_H;
    }

    const int i2cY = WORK_H - I2C_BOTTOM_GAP - ROW_H;
    const char *i2cLabel = reinterpret_cast<const char *>(ot(OKey::INFO_I2C));
    const int i2cLabelW = spr->textWidth(i2cLabel) + 2 * HDR_PAD_X;
    spr->fillSmoothRoundRect(CONTENT_X, i2cY + L::Info::I2C_BADGE_Y, i2cLabelW, HDR_H, BTN_R, scaleColor(farbe(Rolle::EinSektionVerbindung), I2C_BADGE_COLOR_SCALE));
    spr->setTextDatum(ML_DATUM);
    spr->setTextColor(textHell);
    spr->drawString(i2cLabel, CONTENT_X + HDR_PAD_X, i2cY + L::Info::I2C_BADGE_Y + HDR_H / 2);
    // Passt die Liste nicht in die Zeile, entfallen die Leerzeichen nach den Kommas. Rest wird am Zeilenende abgeschnitten.
    const int i2cX = CONTENT_X + i2cLabelW + I2C_LABEL_GAP, i2cW = CONTENT_X + CONTENT_W - ROW_PAD_X - i2cX;
    if (spr->textWidth(i2cBuf) > i2cW)
    {
        size_t out = 0;
        for (size_t in = 0; i2cBuf[in]; ++in)
            if (!(i2cBuf[in] == ' ' && in > 0 && i2cBuf[in - 1] == ','))
                i2cBuf[out++] = i2cBuf[in];
        i2cBuf[out] = '\0';
    }
    spr->setClipRect(i2cX, i2cY, i2cW, ROW_H);
    spr->drawString(i2cBuf, i2cX, i2cY + ROW_H / 2);
    spr->clearClipRect();
    spr->fillRect(CONTENT_X + ROW_PAD_X, i2cY + ROW_H - LINE, CONTENT_W - 2 * ROW_PAD_X, LINE, trenner);

    spr->unloadFont();
}

void Settings::resetPinEntry()
{
    m_pinLen = 0;
    m_pinEntry[0] = '\0';
    m_pinConfirming = false;
    m_pinFirst[0] = '\0';
    m_pinMismatch = false;
}

void Settings::handlePinKey(char action)
{
    if (action >= '0' && action <= '9')
    {
        m_pinMismatch = false;
        if (m_pinLen < 4)
        {
            m_pinEntry[m_pinLen++] = action;
            m_pinEntry[m_pinLen] = '\0';
        }
        if (m_pinLen < 4)
            return;

        if (m_confirmPinDisable)
        {
            changePinLock(false);
            return;
        }

        if (!m_pinConfirming)
        {
            stringutils::safeCopy(m_pinFirst, m_pinEntry);
            m_pinConfirming = true;
            m_pinLen = 0;
            m_pinEntry[0] = '\0';
            return;
        }

        if (strcmp(m_pinEntry, m_pinFirst) == 0)
            stringutils::safeCopy(m_pinPending, m_pinEntry);
        else
            m_pinMismatch = true;

        m_pinConfirming = false;
        m_pinFirst[0] = '\0';
        m_pinLen = 0;
        m_pinEntry[0] = '\0';
        edited();
        return;
    }

    if (action == 'B')
    {
        if (m_pinLen > 0)
        {
            m_pinLen--;
            m_pinEntry[m_pinLen] = '\0';
        }
        return;
    }

    if (action == 'C')
        resetPinEntry();
}

void Settings::changePinLock(bool enabled)
{
    m_draft.pinLock = enabled;
    save(AfterSave::None, PinLock);
    if (!m_pendingId)
        m_draft.pinLock = m_baseline.pinLock;
    // Die PIN zum Ausschalten steht jetzt im Auftrag.
    m_confirmPinDisable = false;
    resetPinEntry();
}

namespace
{
    /** @brief Kopiert einen Sprite zeilenweise per memcpy in die Szene, begrenzt auf Szene und Ausschnitt. Ersetzt das teure pushSprite zwischen Sprites. */
    void blit(UiCanvas &scene, UiCanvas &sprite, int x, int y, const SettingsUi::Rect &clip)
    {
        const int sceneW = scene.width(), sceneH = scene.height(), spriteW = sprite.width(), spriteH = sprite.height();
        const int left = std::max({x, clip.x, 0}), top = std::max({y, clip.y, 0});
        const int right = std::min({x + spriteW, clip.x + clip.w, sceneW}), bottom = std::min({y + spriteH, clip.y + clip.h, sceneH});
        if (right <= left || bottom <= top)
            return;
        auto *target = static_cast<uint16_t *>(scene.getBuffer());
        const auto *source = static_cast<const uint16_t *>(sprite.getBuffer());
        for (int row = top; row < bottom; ++row)
            memcpy(target + row * sceneW + left, source + (row - y) * spriteW + left - x, (right - left) * sizeof(uint16_t));
    }

    /** @brief Liefert die Schnittfläche zweier Rechtecke, bei fehlender Überlappung mit Breite und Höhe 0. */
    SettingsUi::Rect intersection(const SettingsUi::Rect &a, const SettingsUi::Rect &b)
    {
        const int left = std::max(a.x, b.x), top = std::max(a.y, b.y), right = std::min(a.x + a.w, b.x + b.w), bottom = std::min(a.y + a.h, b.y + b.h);
        return right > left && bottom > top ? SettingsUi::Rect{left, top, right - left, bottom - top} : SettingsUi::Rect{0, 0, 0, 0};
    }
}

bool Settings::begin(lgfx::LGFX_Device &display)
{
    if (m_ready)
        return m_display == &display;
    if (display.width() != SCREEN_W || display.height() != SCREEN_H)
        return false;
    m_display = &display;
    if (!m_painter.prepare(SCREEN_W, SCREEN_H) || !m_work.prepare(WORK_W, WORK_H) || !m_clock.prepare(CLOCK_W, CLOCK_H) || !m_audioVolume.prepare(AUDIO_FT_DISP_W, AUDIO_FT_DISP_H))
    {
        end();
        return false;
    }
    UiButton *buttons[] = {&m_btnScan, &m_btnPassword, &m_btnTest, &m_btnDelete};
    const uint16_t rahmen = farbe(Rolle::GemRahmen), taste = farbe(Rolle::GemTastenFlaeche), tastenText = farbe(Rolle::EinTastenText);
    for (int i = 0; i < 4; ++i)
    {
        const Rect r = wifiActionRect(i);
        buttons[i]->initButton(&m_work, r.x + r.w / 2, r.y + r.h / 2, r.w, r.h, BTN_R, rahmen, taste, tastenText, "", nullptr);
        buttons[i]->setFont(LeagueGothicCond51);
        buttons[i]->setBorderWidths(BORDER, L::Style::BORDER_PRESSED);
    }
    UiButton *btButtons[] = {&m_btnBtSearch, &m_btnBtTone, &m_btnBtForget};
    for (int i = 0; i < L::Bluetooth::ACTION_COUNT; ++i)
    {
        const Rect r = btActionRect(i);
        btButtons[i]->initButton(&m_work, r.x + r.w / 2, r.y + r.h / 2, r.w, r.h, BTN_R, rahmen, taste, tastenText, "", nullptr);
        btButtons[i]->setFont(LeagueGothicCond51);
        btButtons[i]->setBorderWidths(BORDER, L::Style::BORDER_PRESSED);
    }
    m_audioVolDots.init(L::SoundFooter::DOT, L::SoundFooter::DOT, L::SoundFooter::DOT_SPACE, L::SoundFooter::CHAR_SPACE);
    // Grund und Schatten der Punkte sind der Grund des Lautstärkefelds in der Fußzeile
    m_audioVolDots.setOnColor(farbe(Rolle::EinPunkteAn), taste);
    m_audioVolDots.setOffColor(farbe(Rolle::EinPunkteAus), taste);
    m_audioVolDots.setBackgroundColor(taste);
    m_audioVolDots.setShowOffDots(true);
    m_audioVolDots.setGradientEnabled(false);
    m_audioVolDots.setDotShape(DotMatrix::DotShape::SQUARE);
    m_audioVolDots.reserveText(4);
    m_ready = true;
    return true;
}

void Settings::end()
{
    m_keyboard.close();
    m_message.close();
    for (auto *sprite : {&m_painter, &m_work, &m_clock, &m_audioVolume})
        sprite->deleteSprite();
    m_display = nullptr;
    m_ready = m_visible = false;
    m_contact.cancel();
    m_queueHead = m_queueCount = 0;
    m_dirtyCount = 0;
    for (auto &r : m_queue)
        r = Request{};
    memset(m_pinPending, 0, sizeof(m_pinPending));
    memset(m_pinEntry, 0, sizeof(m_pinEntry));
    memset(m_pinFirst, 0, sizeof(m_pinFirst));
}

void Settings::show(LangIndex language)
{
    m_language = language;
    m_visible = m_ready;
    m_fullPresent = true;
    m_contact.cancel();
    m_touchOwner = 0;
    m_manualInit = false;
    // Beim Verlassen stellt der Aufrufer die gespeicherten Bildschirmwerte her, ein offener Entwurf gilt beim Wiedereintritt wieder als Vorschau.
    if (m_ready && (changedFields() & Screen))
        preview(RequestType::PreviewScreen);
    if (m_ready && (changedFields() & Ukw))
        preview(RequestType::PreviewUkw);
    redraw();
}

void Settings::hide()
{
    if (m_scanId)
        cancelScan();
    stopBtSearch();
    m_visible = false;
    m_contact.cancel();
    m_touchOwner = 0;
    m_keyboard.close();
    m_message.close();
    m_modal = Modal::None;
    m_afterSave = AfterSave::None;
}

void Settings::invalidate()
{
    m_fullPresent = true;
    redraw();
}

void Settings::redraw()
{
    m_dirty = true;
    m_footerOnly = m_modalOnly = false;
}

void Settings::redrawModal()
{
    if (!m_dirty)
        m_modalOnly = true;
    m_dirty = true;
}

void Settings::invalidateFooter()
{
    if (!m_dirty)
        m_footerOnly = true;
    m_dirty = true;
}

bool Settings::modalActive() const
{
    return m_modal != Modal::None;
}

void Settings::setCapabilities(const Capabilities &caps)
{
    m_capabilities = caps;
    redraw();
}

void Settings::update(uint32_t now)
{
    if (!m_visible || !m_ready)
        return;
    m_now = now;
    if (m_modal == Modal::Progress)
    {
        const auto progress = std::min(100UL, static_cast<unsigned long>(now - m_operationStart) / 200);
        if (now / 100 != m_lastSecond)
        {
            m_lastSecond = now / 100;
            m_progress.updateProgress(progress);
            redrawModal();
        }
    }
    if (m_modal == Modal::Keyboard && m_keyboard.update(now))
        redrawModal();
    if (m_modal == Modal::Networks)
        m_networks.update(now);
    if (m_btSearchId && now - m_btSearchSince > BT_SEARCH_TIMEOUT_MS)
    {
        stopBtSearch();
        failBtSearch(SettingsKey::Timeout);
    }
    if (m_pendingType == RequestType::BtConnect && now - m_operationStart > BT_CONNECT_TIMEOUT_MS)
        finishBtConnect(false, tr(SettingsKey::Timeout));
    if (m_dirty)
    {
        draw();
        m_dirty = m_footerOnly = m_modalOnly = false;
    }
}

uint32_t Settings::panelFields() const
{
    static constexpr uint32_t fields[] = {15, 0, Ukw, Time, Windows, Language | SerialLogs, Pin | PinLock, SoundField, Appearance, Theme, Screen, 0};
    static_assert(sizeof(fields) / sizeof(fields[0]) == PanelCount, "Eine Feldmaske je Seite in der Reihenfolge von Panel");
    return fields[static_cast<unsigned>(m_panel)];
}

void Settings::target(Rect r, int action, bool enabled, bool work)
{
    if (m_hitCount == HitCount)
        return;
    if (work)
    {
        r.x += WORK_X;
        r.y += WORK_Y;
    }
    m_hits[m_hitCount++] = {r, action, enabled};
}

int Settings::hit(int x, int y) const
{
    for (size_t i = m_hitCount; i > 0; --i)
        if (m_hits[i - 1].enabled && m_hits[i - 1].rect.contains(x, y))
            return m_hits[i - 1].action;
    return -1;
}

void Settings::drawButton(UiCanvas &canvas, Rect r, const char *label, bool enabled, bool pressed, bool selected)
{
    UiButton b;
    b.initButton(&canvas, r.x + r.w / 2, r.y + r.h / 2, r.w, r.h, BTN_R, farbe(selected ? Rolle::GemAuswahl : Rolle::GemRahmen), farbe(selected ? Rolle::GemAuswahl : Rolle::GemTastenFlaeche), farbe(selected ? Rolle::GemTextHell : Rolle::EinTastenText), label, nullptr);
    b.setFont(LeagueGothicCond51);
    b.setBorderWidths(BORDER, L::Style::BORDER_PRESSED);
    if (!enabled)
        b.disable();
    b.drawButton(pressed);
}

void Settings::markDirty(Rect r)
{
    r = intersection(r, m_clip);
    if (!r.w)
        return;
    const auto area = [](const Rect &a)
    {
        return static_cast<int32_t>(a.w) * a.h;
    };
    for (size_t i = 0; i < m_dirtyCount; ++i)
    {
        // Benachbarte Flächen zusammenfassen, solange die gemeinsame Hülle kaum Unverändertes mitüberträgt.
        Rect &d = m_dirtyRects[i];
        const int left = std::min(d.x, r.x), top = std::min(d.y, r.y), right = std::max(d.x + d.w, r.x + r.w), bottom = std::max(d.y + d.h, r.y + r.h);
        const Rect hull{left, top, right - left, bottom - top};
        if (area(hull) * 4 <= (area(d) + area(r)) * 5 || i + 1 == DirtyCount)
        {
            d = hull;
            return;
        }
    }
    m_dirtyRects[m_dirtyCount++] = r;
}

void Settings::markWork(Rect r)
{
    if (!m_workInScene)
    {
        m_workDirty = m_workDirty.w ? Rect{std::min(m_workDirty.x, r.x), std::min(m_workDirty.y, r.y), std::max(m_workDirty.x + m_workDirty.w, r.x + r.w) - std::min(m_workDirty.x, r.x), std::max(m_workDirty.y + m_workDirty.h, r.y + r.h) - std::min(m_workDirty.y, r.y)} : r;
        return;
    }
    const Rect screen{WORK_X + r.x, WORK_Y + r.y, r.w, r.h};
    blit(m_painter, m_work, WORK_X, WORK_Y, intersection(screen, m_clip));
    markDirty(screen);
}

void Settings::ensureWorkInScene()
{
    if (m_workInScene)
        return;
    blit(m_painter, m_work, WORK_X, WORK_Y, {0, 0, SCREEN_W, SCREEN_H});
    m_workInScene = true;
    if (m_workDirty.w)
    {
        const Rect r{WORK_X + m_workDirty.x, WORK_Y + m_workDirty.y, m_workDirty.w, m_workDirty.h};
        m_workDirty = {0, 0, 0, 0};
        markDirty(r);
    }
}

void Settings::present()
{
    // Szenenbereiche über dem Arbeitsbereich setzen dessen aktuellen Inhalt in der Szene voraus.
    if (!m_workInScene)
        for (size_t i = 0; i < m_dirtyCount; ++i)
            if (intersection(m_dirtyRects[i], {WORK_X, WORK_Y, WORK_W, WORK_H}).w)
            {
                ensureWorkInScene();
                break;
            }
    if (m_workDirty.w)
    {
        const auto *work = static_cast<const uint16_t *>(m_work.getBuffer());
        const Rect &r = m_workDirty;
        Tab5Render::pushRegion(*m_display, work + r.y * WORK_W + r.x, WORK_W, WORK_X + r.x, WORK_Y + r.y, r.w, r.h);
        m_workDirty = {0, 0, 0, 0};
    }
    auto *pixels = static_cast<uint16_t *>(m_painter.getBuffer());
    for (size_t i = 0; i < m_dirtyCount; ++i)
    {
        const Rect &r = m_dirtyRects[i];
        Tab5Render::pushRegion(*m_display, pixels + r.y * SCREEN_W + r.x, SCREEN_W, r.x, r.y, r.w, r.h);
    }
    m_dirtyCount = 0;
}

void Settings::drawFrame(bool paint)
{
    const uint16_t grund = farbe(Rolle::GemGrund), sektionVerbindung = farbe(Rolle::EinSektionVerbindung), sektionSystem = farbe(Rolle::EinSektionSystem);
    const uint16_t kopfText = farbe(Rolle::EinSektionKopfText), textHell = farbe(Rolle::GemTextHell), text = farbe(Rolle::EinText), trenner = farbe(Rolle::EinTrenner);
    if (paint)
    {
        // Menüspalte und die schmalen Ränder um den Arbeitsbereich, Arbeitsbereich und Fußzeile decken den Rest.
        m_painter.fillRect(0, 0, DIVIDER_X + LINE, SCREEN_H, grund);
        m_painter.fillRect(DIVIDER_X + LINE, 0, WORK_X - DIVIDER_X - LINE, VIEWPORT_H, grund);
        m_painter.fillRect(WORK_X, 0, SCREEN_W - WORK_X, WORK_Y, grund);
        m_painter.fillRect(WORK_X + WORK_W, WORK_Y, SCREEN_W - WORK_X - WORK_W, WORK_H, grund);
        m_painter.loadFont(ComfortaaBold27); // Eine Stufe größer als das 1,5-fache der Originalschrift 16
    }
    const OKey labels[] = {OKey::MENU_WIFI, OKey::MENU_BLUETOOTH, OKey::MENU_UKW, OKey::MENU_TIME, OKey::MENU_TIME_WINDOW, OKey::MENU_LANGUAGE, OKey::MENU_SECURITY, OKey::MENU_AUDIO, OKey::MENU_APPEARANCE, OKey::MENU_THEME, OKey::MENU_SCREEN, OKey::MENU_INFO};
    static_assert(sizeof(labels) / sizeof(labels[0]) == PanelCount, "Ein Menütext je Seite");
    constexpr int systemPanel = static_cast<int>(Panel::Language); ///< Erste Seite des Abschnitts System
    int y = 0;
    for (int panel = 0; panel < PanelCount; ++panel)
    {
        const uint16_t cat = panel < systemPanel ? sektionVerbindung : sektionSystem;
        if (panel == 0 || panel == systemPanel)
        {
            if (paint)
            {
                const auto tint = scaleColor(cat, 0.45f);
                m_painter.fillRect(MENU_X, y, MENU_W, SECTION_ROW_H, tint);
                m_painter.fillRect(MENU_X, y, ITEM_ACCENT_W, SECTION_ROW_H, cat);
                m_painter.setTextDatum(ML_DATUM);
                m_painter.setTextColor(kopfText, tint);
                m_painter.drawString(ot(panel == 0 ? OKey::SEC_CONNECTION : OKey::SEC_SYSTEM), MENU_X + ITEM_TEXT_X, y + SECTION_ROW_H / 2);
            }
            y += SECTION_ROW_H;
        }
        if (paint)
        {
            const bool selected = panel == static_cast<int>(m_panel);
            const auto bg = selected ? scaleColor(cat, 0.25f) : grund;
            if (selected)
                m_painter.fillRect(MENU_X, y, MENU_W, ITEM_ROW_H, bg);
            m_painter.fillRect(MENU_X, y, ITEM_ACCENT_W, ITEM_ROW_H, selected ? cat : scaleColor(cat, 0.45f));
            m_painter.setTextColor(selected ? textHell : text, bg);
            m_painter.setTextDatum(ML_DATUM);
            m_painter.drawString(ot(labels[panel]), MENU_X + ITEM_TEXT_X, y + ITEM_ROW_H / 2);
            m_painter.fillRect(MENU_X + ITEM_ACCENT_W, y + ITEM_ROW_H - LINE, MENU_W - ITEM_ACCENT_W, LINE, trenner);
        }
        target({MENU_X, y, MENU_W, ITEM_ROW_H}, Nav + panel, true, false);
        y += ITEM_ROW_H;
    }
    if (!paint)
        return;
    m_painter.unloadFont();
    m_painter.fillRect(SCROLLBAR_X, 0, SCROLLBAR_W, SCREEN_H, farbe(Rolle::GemScrollSpur));
    m_painter.fillRect(DIVIDER_X, 0, LINE, SCREEN_H, trenner);
    markDirty({0, 0, WORK_X, SCREEN_H});
    markDirty({WORK_X, 0, SCREEN_W - WORK_X, WORK_Y});
    markDirty({WORK_X + WORK_W, WORK_Y, SCREEN_W - WORK_X - WORK_W, WORK_H});
}

void Settings::drawPanel()
{
    const OKey titles[] = {OKey::WIFI_TITLE, OKey::BT_TITLE, OKey::UKW_TITLE, OKey::TIME_TITLE, OKey::TIME_WINDOW_TITLE, OKey::LANG_TITLE, OKey::SEC_TITLE, OKey::AUDIO_TITLE, OKey::APPEARANCE_TITLE, OKey::THEME_TITLE, OKey::SCREEN_TITLE, OKey::INFO_TITLE};
    const OKey subtitles[] = {OKey::WIFI_SUBTITLE, OKey::BT_SUBTITLE, OKey::UKW_SUBTITLE, OKey::TIME_SUBTITLE, OKey::TIME_WINDOW_SUBTITLE, OKey::LANG_SUBTITLE, OKey::SEC_SUBTITLE, OKey::AUDIO_SUBTITLE, OKey::APPEARANCE_SUBTITLE, OKey::THEME_SUBTITLE, OKey::SCREEN_SUBTITLE, OKey::INFO_SUBTITLE};
    static_assert(sizeof(titles) / sizeof(titles[0]) == PanelCount && sizeof(subtitles) / sizeof(subtitles[0]) == PanelCount, "Ein Titel und ein Untertitel je Seite");
    const auto panel = static_cast<unsigned>(m_panel);
    // Nur die Ecken außerhalb der abgerundeten Fläche brauchen den Grund, ein ganzflächiges Füllen kostet nativ rund 25 ms.
    const uint16_t grund = farbe(Rolle::GemGrund);
    for (const int x : {0, WORK_W - WORK_R})
        for (const int y : {0, WORK_H - WORK_R})
            m_work.fillRect(x, y, WORK_R, WORK_R, grund);
    m_work.fillSmoothRoundRect(0, 0, WORK_W, WORK_H, WORK_R, farbe(Rolle::GemFlaeche));
    m_work.fillSmoothRoundRect(TITLE_BOX_X, TITLE_BOX_Y, TITLE_BOX_W, TITLE_BOX_H, TITLE_BOX_R, farbe(Rolle::EinTitelFeld));
    m_work.setTextDatum(TL_DATUM);
    m_work.loadFont(ComfortaaBold45);
    m_work.setTextColor(farbe(Rolle::EinPanelTitel));
    m_work.drawString(ot(titles[panel]), TITLE_TEXT_X, TITLE_TEXT_Y);
    m_work.unloadFont();
    m_work.loadFont(ComfortaaBold24);
    m_work.setTextColor(farbe(Rolle::EinTextNeben));
    m_work.drawString(ot(subtitles[panel]), TITLE_TEXT_X, SUB_TEXT_Y);
    m_work.unloadFont();
    switch (m_panel)
    {
    case Panel::Wifi:
        drawPanelWifi(&m_work);
        for (int i = 0; i < 3; ++i)
        {
            target(wifiRowRect(i), SelectProfile + i);
            target(wifiToggleRect(i), ToggleProfile + i, m_capabilities.wifiProfiles && isProfileValid(i));
        }
        for (int i = 0; i < 4; ++i)
            target(wifiActionRect(i), Scan + i, i == 0 ? m_capabilities.wifiScan : isProfileValid(m_profile) && m_capabilities.wifiProfiles);
        target(hostnameFieldRect(), Host, m_capabilities.wifiProfiles);
        break;
    case Panel::Bluetooth:
        drawPanelBluetooth(&m_work);
        target(wifiToggleRect(0), BtToggle, btToggleEnabled());
        for (int i = 0; i < L::Bluetooth::ACTION_COUNT; ++i)
            target(btActionRect(i), BtSearch + i, btActionEnabled(i));
        break;
    case Panel::Ukw:
        drawPanelUkw(&m_work);
        target(wifiToggleRect(0), UkwToggle, ukwEditable());
        target(ukwStepRect(0, false), UkwFreqMinus, ukwEditable());
        target(ukwStepRect(0, true), UkwFreqPlus, ukwEditable());
        target(ukwValueRect(0), UkwFreqEdit, ukwEditable());
        target(ukwStepRect(1, false), UkwPowerMinus, ukwEditable());
        target(ukwStepRect(1, true), UkwPowerPlus, ukwEditable());
        break;
    case Panel::Time:
        drawPanelTime(&m_work);
        target(timeFieldRect(0), Zone, m_capabilities.time);
        target(timeFieldRect(1), Ntp, m_capabilities.time);
        target(timeFieldRect(2), Interval, m_capabilities.time);
        for (int i = 0; i < 5; ++i)
        {
            target(timeStepperUpRect(i), TimePlus + i, m_capabilities.manualTime);
            target(timeStepperDownRect(i), TimeMinus + i, m_capabilities.manualTime);
        }
        target(timeSetButtonRect(), DateAccept, m_capabilities.manualTime);
        break;
    case Panel::Window:
        drawPanelTimeWindow(&m_work);
        for (int day = 0; day < 7; ++day)
            for (int hour = 0; hour < 24; ++hour)
                target(timeWindowCellRect(day, hour), Cell + day * 24 + hour, m_capabilities.timeWindows);
        break;
    case Panel::Language:
        drawPanelLanguage(&m_work);
        target(langRowRect(0), LanguageDe, m_capabilities.language);
        target(langRowRect(1), LanguageEn, m_capabilities.language);
        target({CONTENT_X, serialLogsToggleRect().y, CONTENT_W, TOGGLE_H}, SerialLogsToggle, true);
        break;
    case Panel::Security:
        drawPanelSecurity(&m_work);
        target({CONTENT_X, pinLockToggleRect().y, CONTENT_W, TOGGLE_H}, PinLockToggle, m_capabilities.pinChange);
        break;
    case Panel::Sound:
        drawPanelAudio(&m_work);
        for (int row = 0; row < 6; ++row)
        {
            target(audioStepperDownRect(row), AudioMinus + row, m_capabilities.sound);
            target(audioStepperUpRect(row), AudioPlus + row, m_capabilities.sound);
        }
        break;
    case Panel::Appearance:
        drawPanelAppearance(&m_work);
        for (int i = 0; i < 4; ++i)
            target(appearanceTileRect(i), Eye + i, m_capabilities.appearance);
        for (int i = 0; i < 3; ++i)
            target(appearanceSegmentRect(APPEARANCE_ROW1_Y, i, 3), Ballistic + i, m_capabilities.appearance);
        for (int i = 0; i < 5; ++i)
            target(appearanceSegmentRect(APPEARANCE_ROW2_Y, i, 5), Sensitivity + i, m_capabilities.appearance);
        drawAppearanceLevel(true);
        break;
    case Panel::Theme:
        drawPanelTheme(&m_work);
        for (uint8_t row = 0; row < ThemenAnzahl; ++row)
            target(themeRowRect(row), ThemeSelect + ThemeRows[row].index, m_capabilities.theme);
        break;
    case Panel::Screen:
        drawPanelScreen(&m_work);
        target(screenStepRect(false), ScreenMinus, m_capabilities.screen);
        target(screenStepRect(true), ScreenPlus, m_capabilities.screen);
        for (int i = 0; i < 2; ++i)
        {
            target(appearanceSegmentRect(L::Screen::ROW2_Y, i, 2), ScreenAuto + i, m_capabilities.screen);
            target(appearanceSegmentRect(L::Screen::ROW3_Y, i, 2), ScreenRotation + i, m_capabilities.screen);
        }
        break;
    case Panel::Info:
        drawPanelInfo(&m_work);
        break;
    }
    // Ohne Dialog geht der Arbeitsbereich direkt aus seinem Sprite ins Display, die Kopie in die Szene (nativ rund 40 ms) entsteht erst für einen Dialog.
    m_workInScene = false;
    markWork({0, 0, WORK_W, WORK_H});
}

bool Settings::drawAppearanceLevel(bool force)
{
    char level[12];
    if (m_status.rmsValid)
        snprintf(level, sizeof(level), "%d dB", static_cast<int>(lroundf(m_status.rmsDb)));
    else
        strcpy(level, "--");
    if (!force && !strcmp(level, m_levelText))
        return false;
    stringutils::safeCopy(m_levelText, level);
    // Nur der Streifen der Pegelanzeige wird neu gezeichnet, damit die Röhrenvorschauen nicht jede Sekunde neu entstehen.
    const int x = CONTENT_X + CONTENT_W, y = CONTENT_Y + APPEARANCE_SEC2_Y;
    const uint16_t flaeche = farbe(Rolle::GemFlaeche);
    m_work.fillRect(x - APPEARANCE_LEVEL_W, y, APPEARANCE_LEVEL_W, APPEARANCE_HINT_H, flaeche);
    m_work.loadFont(ComfortaaBold24);
    m_work.setTextDatum(TR_DATUM);
    m_work.setTextColor(farbe(Rolle::GemTextHell), flaeche);
    m_work.drawString(level, x, y);
    m_work.setTextColor(farbe(Rolle::EinTextNeben), flaeche);
    m_work.drawString(ot(OKey::APPEARANCE_LEVEL_NOW), x - m_work.textWidth(level) - L::Appearance::LEVEL_GAP, y);
    m_work.unloadFont();
    return true;
}

void Settings::drawClock()
{
    // Font7 unskaliert wie die Uhren von Radio und SD-Player. Das Original skaliert mit Textgröße 0,75
    const uint16_t ziffern = farbe(Rolle::UhrDigitalZiffern), spur = farbe(Rolle::UhrDigitalSpur);
    m_clock.fillSprite(farbe(Rolle::GemGrund));
    m_clock.setFont(&fonts::Font7);
    m_clock.setTextSize(1);
    m_clock.setTextDatum(ML_DATUM);
    m_clock.setTextColor(spur);
    m_clock.drawString("88:88", 0, CLOCK_H / 2.0f);
    if (m_status.timeValid && m_status.localTime.year >= 2024)
    {
        char value[8];
        snprintf(value, sizeof(value), "%02u %02u", m_status.localTime.hour, m_status.localTime.minute);
        m_clock.setTextColor(ziffern);
        m_clock.drawString(value, 0, CLOCK_H / 2.0f);
        m_clock.setTextColor(m_status.second % 2 == 0 ? ziffern : spur);
        m_clock.drawString(":", m_clock.textWidth("88"), CLOCK_H / 2.0f);
    }
    blit(m_painter, m_clock, CLOCK_PUSH_X, CLOCK_PUSH_Y, m_clip);
    m_clock.setFont(nullptr);
}

void Settings::drawFooter()
{
    const Rect area{DIVIDER_X + LINE, FOOTER_Y, SCREEN_W - DIVIDER_X - LINE, FOOTER_H};
    m_painter.fillRect(area.x, area.y, area.w, area.h, farbe(Rolle::GemGrund));
    const Rect exit{EXIT_BTN_CX - EXIT_BTN_SIZE / 2, EXIT_BTN_CY - EXIT_BTN_SIZE / 2, EXIT_BTN_SIZE, EXIT_BTN_SIZE};
    // Die Symbolfarbe steht als RGB565-Hexzahl im Etikett
    char symbol[24];
    snprintf(symbol, sizeof(symbol), "^HOME:48:%04X^", farbe(Rolle::GemHausSymbol));
    drawButton(m_painter, exit, symbol, true, m_contact.pressed() == HomeAction);
    target(exit, HomeAction, true, false);
    // Log-Schalter und PIN-Sperre speichern sich selbst, ihr laufender Auftrag zeigt keine Speichern-Taste.
    const bool pending = (changedFields() & panelFields() & ~(SerialLogs | PinLock)) != 0;
    if (pending || m_panel == Panel::Window)
    {
        const Rect r{WORK_X, L::Footer::BUTTON_Y, SAVE_BTN_W, BTN_H};
        drawButton(m_painter, r, ot(OKey::FOOTER_SAVE), pending && m_capabilities.save, false, true);
        target(r, SaveAction, pending && m_capabilities.save, false);
    }
    if (m_panel == Panel::Info || m_panel == Panel::Window)
    {
        m_painter.loadFont(LeagueGothicCond51);
        int width = std::max(FOOTER_ACTION_BTN_MIN_W, static_cast<int>(m_painter.textWidth(ot(OKey::INFO_RESET_BTN))) + L::Footer::ACTION_PAD);
        if (m_panel == Panel::Info)
            width = std::max(width, static_cast<int>(m_painter.textWidth(ot(OKey::INFO_RESTART_BTN))) + L::Footer::ACTION_PAD);
        width = std::min(width, (CLOCK_PUSH_X - L::Footer::CLOCK_GAP - WORK_X - FOOTER_ACTION_BTN_GAP) / 2);
        m_painter.unloadFont();
        if (m_panel == Panel::Info)
        {
            Rect r{WORK_X, L::Footer::BUTTON_Y, width, BTN_H};
            drawButton(m_painter, r, ot(OKey::INFO_RESTART_BTN));
            target(r, Restart, m_capabilities.restart, false);
            r.x += width + FOOTER_ACTION_BTN_GAP;
            drawButton(m_painter, r, ot(OKey::INFO_RESET_BTN));
            target(r, ResetDefaults, m_capabilities.save, false);
        }
        else
        {
            Rect r{WORK_X + SAVE_BTN_W + FOOTER_ACTION_BTN_GAP, L::Footer::BUTTON_Y, width, BTN_H};
            drawButton(m_painter, r, ot(OKey::INFO_RESET_BTN));
            target(r, AllActive, m_capabilities.timeWindows, false);
        }
    }
    if (m_panel == Panel::Sound)
    {
        constexpr int half = AUDIO_FT_BTN_SIZE / 2, top = FOOTER_CY - half;
        const Rect play{AUDIO_FT_PLAY_CX - half, top, AUDIO_FT_BTN_SIZE, AUDIO_FT_BTN_SIZE}, down{AUDIO_FT_VDOWN_CX - half, top, AUDIO_FT_BTN_SIZE, AUDIO_FT_BTN_SIZE}, up{AUDIO_FT_VUP_CX - half, top, AUDIO_FT_BTN_SIZE, AUDIO_FT_BTN_SIZE};
        if (m_soundDraft.playing)
            snprintf(symbol, sizeof(symbol), "^STOP:48:%04X^", farbe(Rolle::EinStopSymbol));
        else
            snprintf(symbol, sizeof(symbol), "^PLAY:48:%04X^", farbe(Rolle::EinPlaySymbol));
        drawButton(m_painter, play, symbol);
        drawButton(m_painter, down, "-");
        drawButton(m_painter, up, "+");
        target(play, Play, m_capabilities.sound, false);
        target(down, VolumeDown, m_capabilities.sound, false);
        target(up, VolumeUp, m_capabilities.sound, false);
        const uint16_t feld = farbe(Rolle::GemTastenFlaeche);
        m_painter.fillSmoothRoundRect(AUDIO_FT_FRAME_X, top, AUDIO_FT_FRAME_W, AUDIO_FT_BTN_SIZE, AUDIO_FT_BTN_R, farbe(Rolle::GemRahmen));
        m_painter.fillSmoothRoundRect(AUDIO_FT_FRAME_X + BORDER, top + BORDER, AUDIO_FT_FRAME_W - 2 * BORDER, AUDIO_FT_BTN_SIZE - 2 * BORDER, AUDIO_FT_BTN_R, feld);
        // Nur 100 benötigt drei schmalere Ziffern im festen Lautstärkefeld, zwei Ziffern stehen mittig darin.
        const bool full = m_soundDraft.volume == 100;
        m_audioVolDots.init(full ? L::SoundFooter::DOT_100 : L::SoundFooter::DOT, L::SoundFooter::DOT, L::SoundFooter::DOT_SPACE, full ? 0 : L::SoundFooter::CHAR_SPACE);
        char volume[4];
        snprintf(volume, sizeof(volume), "%2u", m_soundDraft.volume);
        m_audioVolDots.setText(volume);
        m_audioVolume.fillSprite(feld);
        m_audioVolDots.draw(&m_audioVolume, full ? 0 : (AUDIO_FT_DISP_W - L::SoundFooter::DIGITS_W) / 2, 0);
        blit(m_painter, m_audioVolume, AUDIO_FT_DISP_X, FOOTER_CY - AUDIO_FT_DISP_H / 2, m_clip);
    }
    drawClock();
    markDirty(area);
}

void Settings::drawModal()
{
    if (m_modal == Modal::None)
        return;
    ensureWorkInScene();
    Rect r{0, 0, 0, 0};
    if (m_modal == Modal::Keyboard)
    {
        m_keyboard.draw(m_painter);
        r = m_keyboard.bounds();
    }
    else if (m_modal == Modal::Networks)
    {
        m_networks.draw(m_painter);
        r = SettingsWifiList::bounds();
    }
    else if (m_modal == Modal::Message)
    {
        m_message.draw(m_painter);
        r = m_message.bounds();
    }
    else if (m_modal == Modal::Picker)
    {
        drawPicker(true);
        r = {L::Picker::X, L::Picker::Y, L::Picker::W, L::Picker::H};
    }
    else if (m_modal == Modal::Progress)
    {
        m_progress.draw(m_painter);
        r = m_progress.bounds();
    }
    if (r.w > 0)
    {
        m_modalRect = r;
        markDirty(r);
    }
}

void Settings::drawModalChanges()
{
    // Nur das Geänderte: Tasten und Eingabefeld der Tastatur, Listeninhalte und Fortschrittsbalken ohne erneuten Rahmen.
    if (m_modal == Modal::Keyboard)
    {
        Rect changed[SettingsKeyboard::MaxChanges];
        const size_t count = m_keyboard.drawChanges(m_painter, changed);
        for (size_t i = 0; i < count; ++i)
            markDirty(changed[i]);
    }
    else if (m_modal == Modal::Networks)
    {
        m_networks.drawContent(m_painter);
        markDirty(SettingsWifiList::bounds());
    }
    else if (m_modal == Modal::Message)
    {
        restoreUnder(m_modalRect);
        m_message.draw(m_painter);
        m_modalRect = m_message.bounds();
        markDirty(m_modalRect);
    }
    else if (m_modal == Modal::Picker)
    {
        drawPicker(false);
        markDirty({L::Picker::X + L::Picker::LIST_X, L::Picker::Y + L::Picker::LIST_Y, L::Picker::LIST_WIDTH, L::Picker::LIST_HEIGHT});
    }
    else if (m_modal == Modal::Progress)
    {
        m_progress.drawBar(m_painter);
        markDirty(m_progress.barRect());
    }
}

void Settings::restoreUnder(const Rect &area)
{
    // Weiche Dialogkanten mischen sich mit dem Untergrund, vor erneutem Zeichnen deshalb den Hintergrund unter dem Dialog herstellen.
    const size_t hits = m_hitCount;
    m_clip = area;
    m_painter.setClipRect(area.x, area.y, area.w, area.h);
    drawFrame(true);
    blit(m_painter, m_work, WORK_X, WORK_Y, m_clip);
    drawFooter();
    m_painter.clearClipRect();
    m_clip = {0, 0, SCREEN_W, SCREEN_H};
    m_hitCount = hits;
}

void Settings::draw()
{
    if (m_fullPresent)
    {
        m_menuDirty = true;
        m_modalRect = {0, 0, 0, 0};
    }
    if (m_modalOnly && m_modal != Modal::None)
        drawModalChanges();
    else if (m_footerOnly && m_modal == Modal::None)
    {
        m_hitCount = m_panelHitCount;
        if (m_panel == Panel::Appearance && drawAppearanceLevel(false))
            markWork({CONTENT_X + CONTENT_W - APPEARANCE_LEVEL_W, CONTENT_Y + APPEARANCE_SEC2_Y, APPEARANCE_LEVEL_W, APPEARANCE_HINT_H});
        drawFooter();
    }
    else
    {
        // Ein zuvor gezeichneter Dialog verdeckte Teile von Menü und Rändern, Arbeitsbereich und Fußzeile entstehen ohnehin vollständig neu.
        if (m_modalRect.w > 0)
        {
            if (m_modalRect.x < WORK_X || m_modalRect.y < WORK_Y || m_modalRect.x + m_modalRect.w > WORK_X + WORK_W)
                m_menuDirty = true;
            m_modalRect = {0, 0, 0, 0};
        }
        m_hitCount = 0;
        drawFrame(m_menuDirty);
        m_menuDirty = false;
        drawPanel();
        m_panelHitCount = m_hitCount;
        drawFooter();
        drawModal();
    }
    present();
    m_fullPresent = false;
}

void Settings::initManualFromRtc()
{
    if (m_status.timeValid)
    {
        m_manualYear = m_status.localTime.year;
        m_manualMonth = m_status.localTime.month;
        m_manualDay = m_status.localTime.day;
        m_manualHour = m_status.localTime.hour;
        m_manualMinute = m_status.localTime.minute;
    }
    m_manualInit = true;
    clampManualDay();
}

int Settings::stepperColToField(int col) const
{
    static constexpr int de[] = {FIELD_DAY, FIELD_MONTH, FIELD_YEAR, FIELD_HOUR, FIELD_MINUTE}, en[] = {FIELD_MONTH, FIELD_DAY, FIELD_YEAR, FIELD_HOUR, FIELD_MINUTE};
    return (m_language == LangIndex::EN ? en : de)[col];
}

void Settings::preview(RequestType type)
{
    // Nur Klang und Bildschirmwerte übergeben, der Entwurf mit WLAN-Passwörtern bleibt in der Ansicht.
    Request r;
    r.type = type;
    r.sound = m_soundDraft;
    r.configuration.brightness = m_draft.brightness;
    r.configuration.rotation = m_draft.rotation;
    r.configuration.autoRotate = m_draft.autoRotate;
    r.configuration.ukwEnabled = m_draft.ukwEnabled;
    r.configuration.ukwKhz = m_draft.ukwKhz;
    r.configuration.ukwPower = m_draft.ukwPower;
    if (!enqueue(r))
        notice(SettingsKey::QueueBusy);
    else
    {
        m_previewId = r.id;
        m_previewType = type;
    }
}

void Settings::openPicker(Picker picker)
{
    namespace P = L::Picker;
    m_picker = picker;
    m_modal = Modal::Picker;
    m_pickerDragging = false;
    m_touchOwner = -1;
    size_t selected = 0;
    for (size_t i = 0; i < pickerCount(); ++i)
        if (picker == Picker::Zone ? strcmp(settingsTimezone(i)->region, m_draft.timezone) == 0 : Intervals[i] == m_draft.syncIntervalMs)
        {
            selected = i;
            break;
        }
    m_pickerScroll = std::clamp(static_cast<int>(selected) * P::ROW_STEP - P::SELECT_Y, 0, std::max(0, static_cast<int>(pickerCount()) * P::ROW_STEP - P::END_GAP - P::LIST_HEIGHT));
    redraw();
}

void Settings::drawPicker(bool full)
{
    namespace P = L::Picker;
    constexpr Rect list{P::X + P::LIST_X, P::Y + P::LIST_Y, P::LIST_WIDTH, P::LIST_HEIGHT}, cancel{P::X + P::CANCEL_X, P::Y + P::CANCEL_Y, P::CANCEL_W, P::CANCEL_H};
    const uint16_t zeile = farbe(Rolle::EinZeile), zeileGewaehlt = farbe(Rolle::EinZeileGewaehlt), grund = farbe(Rolle::GemGrund);
    const uint16_t text = farbe(Rolle::EinText), textHell = farbe(Rolle::GemTextHell);
    if (full)
    {
        m_painter.fillSmoothRoundRect(P::X, P::Y, P::W, P::H, P::R, farbe(Rolle::GemRahmen));
        m_painter.fillSmoothRoundRect(P::X + P::BORDER, P::Y + P::BORDER, P::W - 2 * P::BORDER, P::H - 2 * P::BORDER, P::INNER_R, zeile);
        m_painter.loadFont(ComfortaaBold24);
        m_painter.setTextDatum(MC_DATUM);
        m_painter.setTextColor(text, zeile);
        m_painter.drawString(ot(m_picker == Picker::Zone ? OKey::TIME_TZ_PICK_TITLE : OKey::TIME_SYNC_PICK_TITLE), P::X + P::W / 2, P::Y + P::TITLE_Y);
        drawButton(m_painter, cancel, m_language == LangIndex::EN ? "cancel" : "abbrechen");
    }
    else
        m_painter.fillRect(list.x, list.y, list.w, list.h, zeile);
    m_painter.setClipRect(list.x, list.y, list.w, list.h);
    m_painter.loadFont(ComfortaaBold24);
    m_painter.setTextDatum(ML_DATUM);
    const OKey labels[] = {OKey::TIME_SYNC_24H, OKey::TIME_SYNC_12H, OKey::TIME_SYNC_6H, OKey::TIME_SYNC_3H, OKey::TIME_SYNC_1H, OKey::TIME_SYNC_30M, OKey::TIME_SYNC_10M, OKey::TIME_SYNC_1M};
    for (int i = m_pickerScroll / P::ROW_STEP; i < static_cast<int>(pickerCount()) && i * P::ROW_STEP - m_pickerScroll < list.h; ++i)
    {
        const int y = list.y + i * P::ROW_STEP - m_pickerScroll;
        const bool selected = m_picker == Picker::Zone ? strcmp(settingsTimezone(i)->region, m_draft.timezone) == 0 : Intervals[i] == m_draft.syncIntervalMs;
        m_painter.fillSmoothRoundRect(list.x, y, list.w, P::ROW_H, P::ROW_R, selected ? zeileGewaehlt : grund);
        m_painter.setTextColor(selected ? textHell : text);
        m_painter.drawString(m_picker == Picker::Zone ? settingsTimezone(i)->region : ot(labels[i]), list.x + P::TEXT_X, y + P::ROW_H / 2);
    }
    m_painter.clearClipRect();
    m_painter.unloadFont();
}

void Settings::ask(Purpose purpose, SettingsKey title, const char *line1, const char *line2)
{
    m_purpose = purpose;
    m_message.setBounds(L::Message::X, L::Message::Y, L::Message::W, L::Message::H);
    const char *heading = tr(title), *body = line1;
    char formatted[192]{};
    const char *buttons[] = {"ok", m_language == LangIndex::EN ? "cancel" : "abbrechen", nullptr};
    size_t count = 2;
    switch (purpose)
    {
    case Purpose::Notice:
        count = 1;
        if (title == SettingsKey::Saved)
        {
            heading = ot(m_savedFields & Pin ? OKey::SEC_SAVED_TITLE : OKey::SAVED_TITLE);
            if (m_savedFields & Pin)
                body = ot(OKey::SEC_SAVED_MSG);
            else if (m_savedFields & SoundField)
                body = ot(OKey::AUDIO_SAVED_MSG);
            else if (m_savedFields & Appearance)
                body = ot(OKey::APPEARANCE_SAVED_MSG);
            else if (m_savedFields & Screen)
                body = ot(OKey::SCREEN_SAVED_MSG);
            else if (m_savedFields & Windows)
                body = ot(OKey::TIME_WINDOW_SAVED_MSG);
            else if (m_savedFields & Time)
                body = ot(OKey::TIME_SAVED_MSG);
        }
        else if (title == SettingsKey::TestSuccess)
        {
            heading = ot(OKey::WIFI_TEST_OK_TITLE);
            snprintf(formatted, sizeof(formatted), ot(OKey::WIFI_TEST_OK_FMT), line2, static_cast<int>(m_status.rssi));
            body = formatted;
            line2 = "";
        }
        else if (title == SettingsKey::TimeSet)
        {
            heading = ot(OKey::SAVED_TITLE);
            body = ot(OKey::TIME_MANUAL_OK);
        }
        break;
    case Purpose::Leave:
        heading = ot(OKey::EXIT_TITLE);
        body = ot(OKey::EXIT_MSG);
        buttons[0] = ot(OKey::FOOTER_SAVE);
        buttons[1] = ot(OKey::EXIT_DISCARD_BTN);
        break;
    case Purpose::DeleteProfile:
        heading = ot(OKey::WIFI_DELETE_TITLE);
        body = ot(OKey::WIFI_DELETE_MSG);
        break;
    case Purpose::Restart:
        heading = ot(OKey::INFO_RESTART_TITLE);
        body = ot(OKey::INFO_RESTART_MSG);
        buttons[0] = ot(OKey::INFO_RESTART_BTN);
        break;
    case Purpose::ResetDefaults:
        heading = ot(OKey::INFO_RESET_TITLE);
        body = ot(OKey::INFO_RESET_MSG);
        buttons[0] = ot(OKey::INFO_RESET_BTN);
        m_message.setBounds(L::Message::X, L::Message::WIDE_Y, L::Message::WIDE_W, L::Message::TALL_H);
        break;
    case Purpose::AllActive:
        heading = ot(OKey::TIME_WINDOW_RESET_TITLE);
        body = ot(OKey::TIME_WINDOW_RESET_MSG);
        break;
    case Purpose::LanguageSave:
        heading = ot(OKey::LANG_RESTART_TITLE);
        body = ot(OKey::LANG_RESTART_MSG);
        break;
    case Purpose::ThemeSave:
        heading = ot(OKey::THEME_RESTART_TITLE);
        body = ot(OKey::THEME_RESTART_MSG);
        break;
    case Purpose::SavedWifi:
        heading = ot(OKey::SAVED_TITLE);
        body = ot(OKey::SAVED_MSG);
        buttons[0] = ot(OKey::SAVED_RESTART_BTN);
        buttons[1] = ot(OKey::SAVED_LATER_BTN);
        m_message.setBounds(L::Message::X, L::Message::Y, L::Message::W, L::Message::TALL_H);
        break;
    case Purpose::SetTime:
        heading = ot(OKey::TIME_MANUAL_CONFIRM_TITLE);
        body = ot(OKey::TIME_MANUAL_CONFIRM_MSG);
        break;
    case Purpose::BtForget:
        heading = ot(OKey::BT_FORGET_TITLE);
        body = ot(OKey::BT_FORGET_MSG);
        break;
    case Purpose::BtConnected:
        count = 1;
        heading = ot(OKey::BT_CONNECT_OK_TITLE);
        break;
    case Purpose::BtFailed:
        count = 1;
        heading = ot(OKey::BT_CONNECT_FAIL_TITLE);
        break;
    default:
        break;
    }
    m_message.open(heading, body, line2, buttons, count);
    m_modal = Modal::Message;
    m_contact.cancel();
    m_touchOwner = m_touchOwner ? -1 : 0;
    redraw();
}

void Settings::confirmed(int choice)
{
    const Purpose purpose = m_purpose;
    closeModal();
    if (purpose == Purpose::Leave)
    {
        if (choice == 0)
            save((changedFields() & (Language | Theme)) ? AfterSave::Restart : AfterSave::Home);
        else
        {
            discard();
            action(RequestType::Home);
        }
        return;
    }
    if (choice != 0)
        return;
    if (purpose == Purpose::BtForget)
        btCommand(RequestType::BtForget);
    else if (purpose == Purpose::DeleteProfile)
    {
        m_draft.profiles[m_profile] = {};
        m_tested[m_profile] = false;
        edited();
    }
    else if (purpose == Purpose::Restart || purpose == Purpose::SavedWifi)
        action(RequestType::Restart);
    else if (purpose == Purpose::LanguageSave || purpose == Purpose::ThemeSave)
        save(AfterSave::Restart);
    else if (purpose == Purpose::AllActive)
    {
        for (auto &mask : m_draft.timeWindows)
            mask = DayMask;
        edited();
        save(AfterSave::None);
    }
    else if (purpose == Purpose::SetTime)
    {
        Request r;
        r.type = RequestType::SetTime;
        r.localTime = {static_cast<uint16_t>(m_manualYear), static_cast<uint8_t>(m_manualMonth), static_cast<uint8_t>(m_manualDay), static_cast<uint8_t>(m_manualHour), static_cast<uint8_t>(m_manualMinute)};
        if (!validDate(r.localTime))
        {
            notice(SettingsKey::BadDate);
            return;
        }
        if (enqueue(r))
        {
            m_pendingId = r.id;
            m_pendingType = r.type;
        }
        else
            notice(SettingsKey::QueueBusy);
    }
    else if (purpose == Purpose::ResetDefaults)
    {
        Request r;
        r.type = RequestType::ResetDefaults;
        r.baselineRevision = m_revision;
        m_savedFields = UINT32_MAX;
        if (enqueue(r))
        {
            m_pendingId = r.id;
            m_pendingType = r.type;
            m_afterSave = AfterSave::Restart;
        }
        else
            notice(SettingsKey::QueueBusy);
    }
}

void Settings::activate(int id)
{
    if (id >= Nav && id < PanelCount)
    {
        m_panel = static_cast<Panel>(id);
        m_manualInit = false;
        m_menuDirty = true;
        // Eine angefangene Bestätigung zum Ausschalten der PIN-Sperre endet wie im Original mit dem Seitenwechsel.
        if (m_confirmPinDisable)
        {
            m_confirmPinDisable = false;
            resetPinEntry();
        }
        redraw();
        return;
    }
    if (id == ScreenMinus || id == ScreenPlus)
    {
        m_draft.brightness = static_cast<uint8_t>(std::clamp(m_draft.brightness + (id == ScreenPlus ? BrightnessStep : -BrightnessStep), static_cast<int>(BrightnessMin), static_cast<int>(BrightnessMax)));
        preview(RequestType::PreviewScreen);
        edited();
        return;
    }
    // Eine feste Ausrichtung schaltet die Automatik aus.
    if (id == ScreenRotation || id == ScreenRotation + 1)
    {
        m_draft.rotation = id == ScreenRotation ? RotationNormal : RotationFlipped;
        m_draft.autoRotate = false;
        preview(RequestType::PreviewScreen);
        edited();
        return;
    }
    if (id == ScreenAuto || id == ScreenAuto + 1)
    {
        m_draft.autoRotate = id == ScreenAuto;
        preview(RequestType::PreviewScreen);
        edited();
        return;
    }
    if (id == BtToggle)
    {
        if (btLinked())
            btCommand(RequestType::BtDisconnect);
        else if (btToggleEnabled())
            connectBt(m_btLink.device.address);
        return;
    }
    if (id == UkwToggle || id == UkwFreqMinus || id == UkwFreqPlus || id == UkwPowerMinus || id == UkwPowerPlus)
    {
        if (id == UkwToggle)
            m_draft.ukwEnabled = !m_draft.ukwEnabled;
        else if (id == UkwFreqMinus || id == UkwFreqPlus)
            m_draft.ukwKhz = static_cast<uint32_t>(std::clamp<int32_t>(static_cast<int32_t>(m_draft.ukwKhz) + (id == UkwFreqPlus ? static_cast<int32_t>(UkwStepKhz) : -static_cast<int32_t>(UkwStepKhz)), static_cast<int32_t>(UkwMinKhz), static_cast<int32_t>(UkwMaxKhz)));
        else
            m_draft.ukwPower = static_cast<uint8_t>(std::clamp<int>(m_draft.ukwPower + (id == UkwPowerPlus ? 1 : -1), 0, UkwMaxPower));
        preview(RequestType::PreviewUkw);
        edited();
        return;
    }
    if (id == UkwFreqEdit)
    {
        edit(Edit::UkwFrequency);
        return;
    }
    if (id >= SelectProfile && id < SelectProfile + 3)
    {
        m_profile = id - SelectProfile;
        redraw();
        return;
    }
    if (id >= ToggleProfile && id < ToggleProfile + 3)
    {
        auto &p = m_draft.profiles[id - ToggleProfile];
        p.enabled = !p.enabled;
        edited();
        return;
    }
    if (id >= Cell && id < Cell + 168)
    {
        const int index = id - Cell;
        m_draft.timeWindows[index / 24] ^= 1UL << (index % 24);
        edited();
        return;
    }
    if (id >= Eye && id < Eye + 4)
    {
        m_draft.eyeStyle = id - Eye;
        edited();
        return;
    }
    if (id >= Ballistic && id < Ballistic + 3)
    {
        m_draft.vuBallistics = id - Ballistic;
        edited();
        return;
    }
    if (id >= Sensitivity && id < Sensitivity + 5)
    {
        m_draft.vuSensitivity = id - Sensitivity;
        edited();
        return;
    }
    if (id >= ThemeSelect && id < ThemeSelect + ThemenAnzahl)
    {
        m_draft.theme = static_cast<uint8_t>(id - ThemeSelect);
        edited();
        return;
    }
    if ((id >= TimePlus && id < TimePlus + 5) || (id >= TimeMinus && id < TimeMinus + 5))
    {
        const bool plus = id < TimeMinus;
        const int field = stepperColToField(id - (plus ? TimePlus : TimeMinus));
        int *values[] = {&m_manualYear, &m_manualMonth, &m_manualDay, &m_manualHour, &m_manualMinute};
        const int minimum[] = {2000, 1, 1, 0, 0}, maximum[] = {2099, 12, daysInMonth(m_manualYear, m_manualMonth), 23, 59};
        *values[field] = std::clamp(*values[field] + (plus ? 1 : -1), minimum[field], maximum[field]);
        clampManualDay();
        redraw();
        return;
    }
    if ((id >= AudioPlus && id < AudioPlus + 6) || (id >= AudioMinus && id < AudioMinus + 6))
    {
        const bool plus = id < AudioMinus;
        const int row = id - (plus ? AudioPlus : AudioMinus);
        if (row == 5)
            m_soundDraft.speakers = plus;
        else
        {
            const int minimum[] = {100, 1500, 4000, 0, 0}, maximum[] = {800, 6000, 12000, 200, 100};
            const int values[] = {m_soundDraft.eqLow, m_soundDraft.eqPeak, m_soundDraft.eqHigh, m_soundDraft.width, m_soundDraft.loudness};
            const int value = std::clamp(values[row] + (plus ? 1 : -1) * (row < 3 ? 100 : 10), minimum[row], maximum[row]);
            if (row == 0)
                m_soundDraft.eqLow = value;
            else if (row == 1)
                m_soundDraft.eqPeak = value;
            else if (row == 2)
                m_soundDraft.eqHigh = value;
            else if (row == 3)
                m_soundDraft.width = value;
            else
                m_soundDraft.loudness = value;
        }
        preview(RequestType::PreviewSound);
        edited();
        return;
    }
    switch (id)
    {
    case Scan:
        startScan();
        break;
    case Password:
        edit(Edit::Password);
        break;
    case Host:
        edit(Edit::Hostname);
        break;
    case Ntp:
        edit(Edit::Ntp);
        break;
    case Zone:
        openPicker(Picker::Zone);
        break;
    case Interval:
        openPicker(Picker::Interval);
        break;
    case Delete:
        ask(Purpose::DeleteProfile, SettingsKey::DeleteProfile, "");
        break;
    case BtSearch:
        startBtSearch();
        break;
    case BtTone:
        btCommand(RequestType::BtTone);
        break;
    case BtForget:
        ask(Purpose::BtForget, SettingsKey::Delete, "");
        break;
    case LanguageDe:
    case LanguageEn:
        m_draft.language = id == LanguageDe ? LangIndex::DE : LangIndex::EN;
        edited();
        break;
    case SerialLogsToggle:
        // Wirkt wie im Original sofort und wird ohne „speichern“ gesichert. Eine offene Sprachwahl bleibt Entwurf. Kommt kein Speicherauftrag zustande, bleibt der gespeicherte Zustand stehen.
        if (m_pendingId)
            break;
        m_draft.serialLogs = !m_draft.serialLogs;
        save(AfterSave::None, SerialLogs);
        if (!m_pendingId)
            m_draft.serialLogs = m_baseline.serialLogs;
        redraw();
        break;
    case PinLockToggle:
        // Wirkt wie im Original sofort. Zum Ausschalten nimmt der Nummernblock erst die aktuelle PIN an, ein zweiter Tipp bricht das ab.
        if (m_pendingId)
            break;
        resetPinEntry();
        if (m_draft.pinLock)
            m_confirmPinDisable = !m_confirmPinDisable;
        else
            changePinLock(true);
        redraw();
        break;
    case SaveAction:
        if (m_panel == Panel::Language && (changedFields() & Language))
            ask(Purpose::LanguageSave, SettingsKey::Language, "");
        else if (m_panel == Panel::Theme)
            ask(Purpose::ThemeSave, SettingsKey::Restart, "");
        else
            save(AfterSave::None);
        break;
    case HomeAction:
        if (changedFields())
            ask(Purpose::Leave, SettingsKey::Home, "");
        else
            action(RequestType::Home);
        break;
    case Restart:
        ask(Purpose::Restart, SettingsKey::Restart, "");
        break;
    case ResetDefaults:
        ask(Purpose::ResetDefaults, SettingsKey::Restart, "");
        break;
    case AllActive:
        ask(Purpose::AllActive, SettingsKey::AllActive, "");
        break;
    case DateAccept:
        ask(Purpose::SetTime, SettingsKey::SetTime, "");
        break;
    case Play:
        m_soundDraft.playing = !m_soundDraft.playing;
        preview(RequestType::PreviewPlayback);
        redraw();
        break;
    case VolumeDown:
    case VolumeUp:
        m_soundDraft.volume = std::clamp(static_cast<int>(m_soundDraft.volume) + (id == VolumeUp ? 5 : -5), 0, static_cast<int>(m_capabilities.maxVolume));
        preview(RequestType::SetVolume);
        invalidateFooter();
        break;
    case Test:
    {
        Request r;
        r.type = RequestType::TestProfile;
        r.profile = m_draft.profiles[m_profile];
        r.profileIndex = m_profile;
        if (enqueue(r))
        {
            m_pendingId = r.id;
            m_pendingType = r.type;
            m_progress.setTitle(ot(OKey::WIFI_TEST_BOX_TITLE));
            m_progress.start(ot(OKey::WIFI_TEST_RUNNING), 0);
            m_operationStart = m_now;
            m_modal = Modal::Progress;
            redraw();
        }
        else
            notice(SettingsKey::QueueBusy);
        break;
    }
    default:
        break;
    }
}

bool Settings::touch(Phase phase, int x, int y)
{
    if (!m_visible || !m_ready)
        return false;
    const bool inside = x >= 0 && x < SCREEN_W && y >= 0 && y < SCREEN_H;
    if (m_touchOwner == -1)
    {
        if (phase == Phase::Release || phase == Phase::Cancel)
            m_touchOwner = 0;
        return true;
    }
    // Bei offenem Auftrag bleibt nur die Haus-Taste bedienbar.
    if (m_pendingId)
    {
        const bool exitPressed = m_contact.pressed() == HomeAction;
        if (m_contact.process(phase, inside && hit(x, y) == HomeAction ? HomeAction : -1) == HomeAction)
            leavePending();
        else if (exitPressed != (m_contact.pressed() == HomeAction))
            invalidateFooter();
        return true;
    }
    if (phase == Phase::Press && !inside)
    {
        m_touchOwner = -1;
        return true;
    }
    if (m_modal == Modal::Keyboard)
    {
        const int before = m_keyboard.pressed();
        const auto result = m_keyboard.touch(phase, x, y);
        if (result == SettingsKeyboard::Result::Accepted)
        {
            acceptInput();
            m_touchOwner = -1;
        }
        else if (result == SettingsKeyboard::Result::Cancelled)
        {
            closeModal();
            m_touchOwner = -1;
        }
        if (before != m_keyboard.pressed() || phase == Phase::Press)
            redrawModal();
        return true;
    }
    if (m_modal == Modal::Networks)
    {
        const int before = m_networks.pressed();
        const auto result = m_networks.touch(phase, x, y);
        if (m_panel == Panel::Bluetooth)
            selectedDevice(result);
        else
            selectedNetwork(result);
        if (result.action != WifiList::Action::None && phase == Phase::Press)
            m_touchOwner = -1;
        if (before != m_networks.pressed())
            redrawModal();
        return true;
    }
    if (m_modal == Modal::Message)
    {
        const int before = m_message.pressed();
        const int choice = m_message.touch(phase, x, y);
        if (choice >= 0)
            confirmed(choice);
        if (before != m_message.pressed())
            redrawModal();
        return true;
    }
    if (m_modal == Modal::Picker)
    {
        namespace P = L::Picker;
        constexpr Rect list{P::X + P::LIST_X, P::Y + P::LIST_Y, P::LIST_WIDTH, P::LIST_HEIGHT}, cancel{P::X + P::CANCEL_X, P::Y + P::CANCEL_Y, P::CANCEL_W, P::CANCEL_H};
        if (phase == Phase::Press)
        {
            m_touchOwner = 1;
            m_pickerTouchY = y;
            m_pickerStart = m_pickerScroll;
            m_pickerDragging = false;
        }
        else if (phase == Phase::Move && m_touchOwner == 1 && std::abs(y - m_pickerTouchY) > P::DRAG)
        {
            const int scroll = std::clamp(m_pickerStart - (y - m_pickerTouchY), 0, std::max(0, static_cast<int>(pickerCount()) * P::ROW_STEP - P::END_GAP - list.h));
            m_pickerDragging = true;
            if (scroll != m_pickerScroll)
            {
                m_pickerScroll = scroll;
                redrawModal();
            }
        }
        else if (phase == Phase::Release && m_touchOwner == 1)
        {
            m_touchOwner = 0;
            if (!m_pickerDragging)
            {
                if (list.contains(x, y))
                    choose((y - list.y + m_pickerScroll) / P::ROW_STEP);
                else if (cancel.contains(x, y))
                    closeModal();
            }
        }
        else if (phase == Phase::Cancel)
            m_touchOwner = 0;
        return true;
    }
    if (phase == Phase::Cancel)
    {
        m_touchOwner = 0;
        if (m_contact.pressed() == HomeAction)
            invalidateFooter();
        m_contact.cancel();
        return true;
    }
    if (phase == Phase::Press)
    {
        m_touchOwner = 1;
        if (m_panel == Panel::Security && m_capabilities.pinChange && inside)
        {
            const int key = m_numpad.hitTest(x - WORK_X - CONTENT_X, y - WORK_Y - CONTENT_Y);
            if (key >= 0)
            {
                handlePinKey(Numpad::keyAction(key));
                redraw();
                return true;
            }
        }
        const int id = inside ? hit(x, y) : -1;
        m_contact.process(phase, id);
        // Nur die Haus-Taste zeigt einen Druckzustand, leere Kontakte und das Loslassen zeichnen nicht die ganze Seite neu.
        if (id >= 0 && id != HomeAction)
        {
            activate(id);
            if (m_modal != Modal::None)
                m_touchOwner = -1;
        }
        else if (id == HomeAction)
            invalidateFooter();
    }
    else if (phase == Phase::Release)
    {
        const bool exitPressed = m_contact.pressed() == HomeAction;
        const int id = m_contact.process(phase, inside ? hit(x, y) : -1);
        m_touchOwner = 0;
        if (id == HomeAction)
            activate(id);
        else if (exitPressed)
            invalidateFooter();
    }
    return true;
}
