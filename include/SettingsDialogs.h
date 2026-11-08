#ifndef SETTINGS_DIALOGS_H
#define SETTINGS_DIALOGS_H

#include "Keyboard.h"
#include "WifiList.h"
#include "UiCanvas.h"
#include "SettingsTab5Layout.h"
#include "Thema.h"

/** @brief Tastendefinition aus RetroRadio/Keyboard.h. Text-, Aktions- und Symbolzeiger bleiben Eigentum des Aufrufers. */
struct SettingsKeyDef
{
    const char *label;      ///< Beschriftung, UTF-8
    const char *action;     ///< Zeichen oder Befehlsname wie "BACKSPACE", nullptr beendet die Zeile
    float width_units;      ///< Breite in Rasterbreiten (14 je Zeile)
    const uint16_t *icon;   ///< Symbol statt Beschriftung oder nullptr
    Rolle labelRolle;       ///< Rolle der Beschriftungsfarbe
    uint8_t labelDatum;     ///< Textausrichtung (MC_DATUM, ML_DATUM, MR_DATUM)
};

/** @brief Bildschirmtastatur mit zentral zugeführten Kontaktphasen und begrenztem UTF-8-Eingabepuffer. */
class SettingsKeyboard
{
public:
    using Mode = Keyboard::Mode;
    using Result = Keyboard::Result;

    /** @brief Überschreibt auch bei Zerstörung die temporäre Eingabe. */
    ~SettingsKeyboard()
    {
        close();
    }

    /** @brief Kopiert Titel und Eingabe nach dem Löschen der vorherigen Eingabe. @param title Gültiger UTF-8-Titel mit höchstens 95 Bytes. @param initial Gültiger UTF-8-Anfangswert innerhalb maxBytes. @param maxBytes Eingabegrenze 1..64 ohne Nullterminator. @param mode Text, Passwort, Zahlen oder PIN. append prüft Zeichenbeschränkungen numerischer Modi. @param language Tastatursprache. @return false bei ungültigen Zeigern, Texten oder Grenzen. Dialog bleibt geschlossen. */
    bool open(const char *title, const char *initial, size_t maxBytes, Mode mode, LangIndex language);
    static constexpr size_t MaxChanges = 4; ///< Größe des Empfangsfelds von drawChanges
    /** @brief Zeichnet Rahmen, Eingabe und Tasten vollständig, nativ in 1280x720. @param canvas Szene mit hergestelltem Hintergrund. Muss für Cursor-Treffertests bis close gültig bleiben. */
    void draw(UiCanvas &canvas);
    /** @brief Zeichnet seit dem letzten Zeichnen geänderte Teile (Tasten, Titel, Eingabezeile) über ihren eigenen Grund neu, ohne den Rahmen. @param canvas Szene mit vollständig gezeichneter Tastatur. @param changed Empfängt die geänderten Bereiche. @return Anzahl der Bereiche. */
    size_t drawChanges(UiCanvas &canvas, SettingsUi::Rect (&changed)[MaxChanges]);

    /** @brief Liefert die Außenmaße der Tastatur. */
    static SettingsUi::Rect bounds()
    {
        return {Layout::DEFAULT_X, Layout::DEFAULT_Y, Layout::KEYBOARD_WIDTH, Layout::TOTAL_HEIGHT};
    }

    /** @brief Führt Startupschutz, Cursorblinken und Auto-Repeat ohne Wartezeiten aus. @param now Zentraler Millisekundenzähler. @return true bei sichtbarer Änderung. */
    bool update(uint32_t now);
    /** @brief Verarbeitet Press nach 150 ms Startschutz und mindestens 30 ms seit Release. Move löst keine neue Taste aus. @param phase Zentrale Kontaktphase. @param x Logische X-Koordinate. @param y Logische Y-Koordinate. @return Accepted bei OK auf Press, Cancelled bei Schließen auf Press, sonst None. */
    Result touch(SettingsUi::TouchPhase phase, int x, int y);
    /** @brief Fügt vollständigen UTF-8-Text an der Cursorposition ein. @return true bei vollständiger Übernahme innerhalb der Bytegrenze. */
    bool append(const char *value);
    /** @brief Löscht ein vollständiges UTF-8-Zeichen vor dem Cursor. */
    void backspace();
    /** @brief Zeigt einen Dienstfehler im vorhandenen Titelbereich. */
    void setError(SettingsKey key);

    /** @brief Liefert den eigenen nullterminierten Eingabepuffer, Inhalt kann sich bei Eingaben ändern und wird bei close gelöscht. */
    const char *value() const
    {
        return m_value;
    }

    /** @brief Schließt ohne Speicherung und überschreibt den Eingabepuffer. */
    void close();
    /** @brief Verwirft nur die aktuelle Berührung. */
    void cancelTouch();

    /** @brief Liefert die gedrückte Taste oder -1. */
    int pressed() const
    {
        return m_pressed;
    }

    /** @brief Meldet eine offene Eingabe. */
    bool active() const
    {
        return m_active;
    }

private:
    /** @brief Tastenebene. */
    enum class KeyboardState : uint8_t
    {
        LOWERCASE,         ///< Kleinbuchstaben
        UPPERCASE,         ///< Großbuchstaben
        SYMBOLS_LOWERCASE, ///< Ziffern und Zeichen
        SYMBOLS_UPPERCASE  ///< Weitere Zeichen
    };

    /** @brief Native Maße, Originalwerte in SettingsTab5Layout::Keyboard. */
    struct Layout
    {
        static constexpr int KEYBOARD_WIDTH = SettingsTab5Layout::Keyboard::W, KEYBOARD_ROWS = 5, MAX_COLS = 16;
        static constexpr int KEY_HEIGHT = SettingsTab5Layout::Keyboard::KEY_H, KEY_GAP = SettingsTab5Layout::Keyboard::KEY_GAP, KEY_RADIUS = SettingsTab5Layout::Keyboard::R, BORDER_RADIUS = SettingsTab5Layout::Keyboard::R, PADDING = SettingsTab5Layout::Keyboard::PADDING;
        static constexpr int LABEL_HEIGHT = SettingsTab5Layout::Keyboard::LABEL_H, INPUT_HEIGHT = SettingsTab5Layout::Keyboard::INPUT_H, INPUT_MARGIN = SettingsTab5Layout::Keyboard::INPUT_MARGIN;
        static constexpr float GRID_UNITS = 14.0f;
        static constexpr float UNIT_WIDTH = (KEYBOARD_WIDTH - 2 * PADDING - (int(GRID_UNITS) - 1) * KEY_GAP) / GRID_UNITS;
        static constexpr int KEYBOARD_PART_HEIGHT = KEYBOARD_ROWS * KEY_HEIGHT + (KEYBOARD_ROWS - 1) * KEY_GAP;
        static constexpr int INPUT_Y_REL = PADDING + LABEL_HEIGHT;
        static constexpr int KEYBOARD_Y_REL = INPUT_Y_REL + INPUT_HEIGHT + INPUT_MARGIN;
        static constexpr int TOTAL_HEIGHT = KEYBOARD_Y_REL + KEYBOARD_PART_HEIGHT + PADDING;
        static constexpr int DEFAULT_X = (SettingsTab5Layout::WIDTH - KEYBOARD_WIDTH) / 2, DEFAULT_Y = (SettingsTab5Layout::HEIGHT - TOTAL_HEIGHT) / 2;
    };

    /** @brief Berechnete Taste der aktuellen Ebene. */
    struct Key
    {
        const SettingsKeyDef *definition = nullptr; ///< Beschriftung und Aktion
        SettingsUi::Rect rect{};                    ///< Fläche in Displaypixeln
    };

    /** @brief Tastenreihen je Ebene. Die LAYOUT_EN_-Tabellen gelten für Englisch, die übrigen für Deutsch. */
    static const SettingsKeyDef LAYOUT_LOWERCASE[Layout::KEYBOARD_ROWS][Layout::MAX_COLS];
    static const SettingsKeyDef LAYOUT_UPPERCASE[Layout::KEYBOARD_ROWS][Layout::MAX_COLS];
    static const SettingsKeyDef LAYOUT_SYMBOLS_LOWERCASE[Layout::KEYBOARD_ROWS][Layout::MAX_COLS];
    static const SettingsKeyDef LAYOUT_SYMBOLS_UPPERCASE[Layout::KEYBOARD_ROWS][Layout::MAX_COLS];
    static const SettingsKeyDef LAYOUT_EN_LOWERCASE[Layout::KEYBOARD_ROWS][Layout::MAX_COLS];
    static const SettingsKeyDef LAYOUT_EN_UPPERCASE[Layout::KEYBOARD_ROWS][Layout::MAX_COLS];
    static const SettingsKeyDef LAYOUT_EN_SYMBOLS_LOWERCASE[Layout::KEYBOARD_ROWS][Layout::MAX_COLS];
    static const SettingsKeyDef LAYOUT_EN_SYMBOLS_UPPERCASE[Layout::KEYBOARD_ROWS][Layout::MAX_COLS];
    char m_title[96]{}, m_value[65]{};                  ///< Titel, Eingabepuffer für höchstens 64 Bytes samt Terminator
    size_t m_limit = 0, m_cursor = 0, m_keyCount = 0;    ///< Maximale Länge in Bytes, Cursorposition als Byteversatz, Zahl der Tasten
    Key m_keys[Layout::KEYBOARD_ROWS * Layout::MAX_COLS]{}; ///< Tasten der aktuellen Ebene
    Mode m_mode = Mode::Text;                            ///< Eingabeart
    LangIndex m_language = LangIndex::DE;                ///< Sprache der Tastenbelegung
    KeyboardState m_state = KeyboardState::LOWERCASE;    ///< Aktuelle Ebene
    SettingsKey m_error = SettingsKey::InputHint;        ///< Hinweis oder Fehler in der Titelzeile
    bool m_active = false, m_capsLock = false, m_passwordVisible = false, m_contact = false, m_blink = true, m_changed = false, m_startPending = false; ///< Geöffnet. Dauergroßschreibung. Passwort sichtbar. Kontakt läuft. Cursor sichtbar. Änderung noch an update zu melden. Startzeit noch zu setzen
    bool m_keysDirty = false, m_inputDirty = false, m_titleDirty = false; ///< Seit dem letzten Zeichnen geänderte Teile
    int m_drawnPressed = -1;                             ///< Zuletzt gezeichnete gedrückte Taste
    uint32_t m_now = 0, m_startedAt = 0, m_pressedAt = 0, m_lastRepeat = 0, m_lastRelease = 0, m_lastBlink = 0; ///< Zeiten in ms: aktuell, Öffnen, Tastendruck, letzte Wiederholung, letztes Loslassen, letzter Cursorwechsel
    bool m_hasReleased = false;                          ///< Seit dem Öffnen wurde mindestens einmal losgelassen
    int m_pressed = -1;                                  ///< Gedrückte Taste oder -1
    const char *m_repeatAction = nullptr;                ///< Aktion der gehaltenen wiederholbaren Taste
    mutable UiCanvas *m_canvas = nullptr;                ///< Zuletzt verwendete Zeichenfläche für die Textbreite beim Treffertest des Cursors
    /** @brief Berechnet Tastenpositionen aus fünf Zeilen, 14 Breiteneinheiten und den jeweiligen Tastendefinitionen. */
    void buildLayout();
    /** @brief Verarbeitet eine Aktion ohne Speichern oder Dienstzugriff. */
    Result processKey(const char *action);
    /** @brief Bewegt den Cursor um einen vollständigen UTF-8-Codepunkt. */
    void moveCursor(int direction);
    /** @brief Bestimmt die nächste UTF-8-Cursorposition anhand der Schriftbreite in der zuletzt verwendeten Zeichenfläche. */
    size_t cursorFromTouch(int x) const;
    /** @brief Liefert das Eingabefeld mit Platz für die Sichtbarkeitstaste im Passwort- und PIN-Modus. */
    SettingsUi::Rect inputRect() const;
    /** @brief Liefert die Schließen-Taste. */
    static SettingsUi::Rect closeRect();
    /** @brief Liefert die Sichtbarkeitstaste im Passwort- und PIN-Modus. */
    SettingsUi::Rect eyeRect() const;
    /** @brief Zeichnet Titel- oder Fehlerzeile über ihren Grund. @return Bereich der Zeile. */
    SettingsUi::Rect drawTitle(UiCanvas &canvas) const;
    /** @brief Zeichnet Sichtbarkeitstaste, Eingabefeld, Text und Cursor über ihren Grund. @return Bereich der Eingabezeile. */
    SettingsUi::Rect drawInput(UiCanvas &canvas) const;
    /** @brief Zeichnet eine Taste, die Schließen-Taste (100) oder die Sichtbarkeitstaste (101) über ihren Grund. @return Bereich der Taste oder leeres Rechteck. */
    SettingsUi::Rect drawKey(UiCanvas &canvas, int index) const;
    /** @brief Liefert die RGB565-Farbe anhand von Tastenaktion, Shift und Caps Lock. */
    uint16_t keyColor(const char *action) const;
    /** @brief Meldet ein Sonderzeichenlayout. */
    bool symbolState() const;
};

/** @brief WLAN-Auswahl mit fünf sichtbaren Zeilen, Scan und Speicherung erfolgen außerhalb des Dialogs. */
class SettingsWifiList
{
public:
    using Network = WifiList::Network;
    using State = WifiList::State;
    using Action = WifiList::Action;
    using Result = WifiList::Result;
    static constexpr size_t MaxNetworks = 24, PageSize = 5;
    /** @brief Was die Liste zeigt, bestimmt Titel und die Texte für Suche und leeres Ergebnis. */
    enum class Kind : uint8_t
    {
        Networks, ///< WLAN-Netze
        Devices   ///< Bluetooth-Geräte, Name im Feld ssid, ohne Schloss
    };
    /** @brief Setzt die Auswahl (nativ 750x450 mittig, Original 500x300 bei 150,90) auf den Zustand Scanning. Der Aufrufer startet die Suche. */
    void open(LangIndex language, Kind kind = Kind::Networks);
    /** @brief Kopiert bis zu 24 Einträge und sortiert stabil nach absteigendem RSSI. Gleich starke behalten ihre Reihenfolge. @param networks Array mit count Einträgen und gültigen, nicht leeren UTF-8-SSIDs. @param count Anzahl 0..24. Bei 0 darf networks nullptr sein. @param more Kennzeichnet weitere nicht übergebene Scanergebnisse. @return false bei ungültiger Anzahl, fehlendem Array oder ungültiger SSID. Bisherige Liste bleibt erhalten. */
    bool setNetworks(const Network *networks, size_t count, bool more);

    /** @brief Liefert zur Listenzeile index die Stelle des Eintrags im Array, das setNetworks() erhielt, oder -1. */
    int origin(size_t index) const
    {
        return index < m_count ? m_origin[index] : -1;
    }

    /** @brief Übernimmt einen zentral gemeldeten Suchfehler. */
    void fail(SettingsKey key);
    /** @brief Zeichnet Rahmen und Inhalt der WLAN-Auswahl. */
    void draw(UiCanvas &canvas) const;
    /** @brief Zeichnet Titel, Tasten, aus RSSI abgeleitete Prozentwerte, Schlosssymbole und Scrollbalken über den eigenen Grund, ohne die abgerundeten Ecken. */
    void drawContent(UiCanvas &canvas) const;

    /** @brief Liefert die Außenmaße der WLAN-Auswahl. */
    static SettingsUi::Rect bounds()
    {
        return {SettingsTab5Layout::Networks::X, SettingsTab5Layout::Networks::Y, SettingsTab5Layout::Networks::W, SettingsTab5Layout::Networks::H};
    }

    /** @brief Wählt eine Listenzeile auf Press. OK bestätigt auf Press den gewählten Index. Move scrollt nach mehr als acht Pixeln Bewegung in 54-Pixel-Zeilenschritten. @param phase Zentrale Kontaktphase nach update. @param x Displaykoordinate X. @param y Displaykoordinate Y. @return Auswahl, Abbruch oder erneute Scananforderung. Sonst keine Aktion. */
    Result touch(SettingsUi::TouchPhase phase, int x, int y);
    /** @brief Aktualisiert die Zeitbasis für Startupschutz und Scan-Sperrzeit. */
    void update(uint32_t now);

    /** @brief Liefert einen übernommenen, sortierten Eintrag oder nullptr. */
    const Network *network(size_t index) const
    {
        return index < m_count ? &m_networks[index] : nullptr;
    }

    /** @brief Verwirft den aktuellen Kontakt. */
    void cancelTouch();
    /** @brief Meldet Taste oder sichtbaren Listenstatus für die zentrale Neuzeichnung. */
    int pressed() const;

    /** @brief Meldet einen ausstehenden zentralen Scan. */
    bool scanning() const
    {
        return m_state == State::Scanning;
    }

private:
    Network m_networks[MaxNetworks]{};                   ///< Übernommene Einträge nach RSSI absteigend
    uint8_t m_origin[MaxNetworks]{};                     ///< Je Zeile die Stelle des Eintrags im Array von setNetworks()
    size_t m_count = 0;                                  ///< Zahl der Einträge
    Kind m_kind = Kind::Networks;                        ///< Inhalt der Liste
    int m_selected = -1, m_scroll = 0, m_startScroll = 0, m_startY = 0, m_pressed = -1; ///< Gewählte Zeile, erste sichtbare Zeile. Deren Wert und Berührpunkt beim Beginn des Wischens, gedrückte Taste oder Zeile
    bool m_more = false, m_contact = false, m_listContact = false, m_scrolling = false, m_startPending = true; ///< Weitere Treffer verworfen. Kontakt läuft. Kontakt begann in der Liste. Wischen läuft. Startzeit noch zu setzen
    uint32_t m_now = 0, m_startedAt = 0, m_lastScan = 0; ///< Zeiten in ms: aktuell, Öffnen, letzte Suchanforderung
    State m_state = State::Scanning;                     ///< Suche läuft, Liste bereit oder Fehler
    SettingsKey m_error = SettingsKey::ServiceError;     ///< Fehlertext im Zustand Fehler
    LangIndex m_language = LangIndex::DE;                ///< Sprache der Texte
};

/** @brief Meldungsdialog mit eigenen begrenzten Textpuffern und expliziten Kontaktphasen. */
class SettingsMessageBox
{
public:
    /** @brief Kopiert Titel, zwei Textabsätze und ein bis drei Tastenbeschriftungen in begrenzte Puffer. Zu lange Texte werden gekürzt. @param title Titel, nicht nullptr. @param line1 Erster Text, nicht nullptr. @param line2 Zweiter Text oder Leerstring, nicht nullptr. @param buttons Array mit count Beschriftungen. @param count Anzahl 1..3. Ungültige Zeiger oder Anzahl lassen den Dialog geschlossen. */
    void open(const char *title, const char *line1, const char *line2, const char *const *buttons, size_t count);
    /** @brief Zeichnet Rahmen, Titel, Textumbruch und Tasten. Bereitet dabei die Trefferflächen für touch vor. */
    void draw(UiCanvas &canvas) const;
    /** @brief Bestätigt auf Release die zuletzt durch Press/Move getroffene Taste, Release-Koordinaten ändern die Auswahl nicht. @param phase Zentrale Kontaktphase, Cancel verwirft die Auswahl. @param x Logische X-Koordinate. @param y Logische Y-Koordinate. @return Tastenindex oder -1, vor dem ersten draw und während Pending immer -1. */
    int touch(SettingsUi::TouchPhase phase, int x, int y);

    /** @brief Verwirft den aktuellen Kontakt ohne Ergebnis. */
    void cancelTouch()
    {
        m_contact = false;
        m_pressed = -1;
    }

    /** @brief Liefert die gedrückte Taste oder -1. */
    int pressed() const
    {
        return m_pressed;
    }

    /** @brief Schließt die Meldung ohne zusätzliche Aktion. */
    void close();

    /** @brief Meldet einen aktiven Dialog. */
    bool active() const
    {
        return m_active;
    }

    /** @brief Liefert die aktuellen Außenmaße. */
    SettingsUi::Rect bounds() const
    {
        return m_bounds;
    }

    /** @brief Übernimmt Grenzen innerhalb von 1280x720 bei mindestens 180x180 Pixeln, ungültige Werte ändern die bisherigen Grenzen nicht. */
    void setBounds(int x, int y, int w, int h);
    /** @brief Kopiert einen Wartetext und sperrt Tasten bis zum nächsten open oder close. */
    void setPending(const char *text);

private:
    char m_title[96]{}, m_message[384]{}, m_buttons[3][48]{}, m_loading[96]{}; ///< Titel, beide Absätze, Tastentexte, Wartetext
    size_t m_count = 0;                                  ///< Zahl der Tasten
    SettingsUi::Rect m_bounds{340, 210, 600, 300}; ///< Nativ mittig, Original 200,140,400,200
    mutable SettingsUi::Rect m_buttonRects[3]{};         ///< Tastenflächen, von draw berechnet
    mutable bool m_layoutValid = false;                  ///< m_buttonRects ist berechnet
    bool m_active = false, m_contact = false, m_pending = false; ///< Geöffnet. Kontakt läuft. Wartetext gesetzt, Tasten gesperrt
    int m_pressed = -1;                                  ///< Gedrückte Taste oder -1
};

/** @brief Segmentierter Fortschrittsdialog ohne eigenen Task, Wartezeiten oder Gerätezustände. */
class SettingsProgressBox
{
public:
    /** @brief Kopiert den Titel in den eigenen 64-Byte-Puffer, überlange Texte werden gekürzt. */
    void setTitle(const char *title);
    /** @brief Setzt den kopierten Nachrichtentext. */
    void setMessage(const char *message);
    /** @brief Setzt Grenzen innerhalb von 1280x720. */
    void setBounds(int x, int y, int w, int h);
    /** @brief Zentriert die Box in 1280x720 Pixeln. */
    void setAutoCenter(bool center = true);
    /** @brief Öffnet den Dialog mit 0..100 Prozent oder -1 für unbestimmten Fortschritt. */
    void start(const char *message = nullptr, int progress = 0);
    /** @brief Übernimmt einen zentral gemessenen Fortschritt. */
    void updateProgress(int progress);

    /** @brief Übernimmt eine neue Nachricht. */
    void updateMessage(const char *message)
    {
        setMessage(message);
    }

    /** @brief Übernimmt Nachricht und Fortschritt gemeinsam. */
    void update(const char *message, int progress)
    {
        setMessage(message);
        updateProgress(progress);
    }

    /** @brief Schaltet die unbestimmte Fortschrittsanimation frühestens nach 50 ms weiter. @param now Monotone Zeit in Millisekunden. @return true bei geändertem Animationsstand. */
    bool updateAnimation(uint32_t now);
    /** @brief Zeichnet Text und segmentierten Fortschrittsbalken auf die vorbereitete Zeichenfläche. */
    void draw(UiCanvas &canvas) const;
    /** @brief Zeichnet nur den Fortschrittsbalken neu. */
    void drawBar(UiCanvas &canvas) const;
    /** @brief Liefert den Bereich des Fortschrittsbalkens. */
    SettingsUi::Rect barRect() const;

    /** @brief Liefert die Außenmaße. */
    SettingsUi::Rect bounds() const
    {
        return m_bounds;
    }

    /** @brief Schließt ohne Nebenwirkung, die zentrale Ansicht zeichnet den Hintergrund neu. */
    void finish()
    {
        m_active = false;
    }

    /** @brief Meldet den aktiven Zustand. */
    bool isActive() const
    {
        return m_active;
    }

    /** @brief Setzt Segment- und Lückenbreite in Pixeln, begrenzt das Segment auf mindestens 1 und die Lücke auf mindestens 0. */
    void setBarStyle(int segmentWidth, int gapWidth);

private:
    char m_title[64]{}, m_message[128]{};                ///< Titel und Nachricht
    SettingsUi::Rect m_bounds{(SettingsTab5Layout::WIDTH - SettingsTab5Layout::Progress::W) / 2, (SettingsTab5Layout::HEIGHT - SettingsTab5Layout::Progress::H) / 2, SettingsTab5Layout::Progress::W, SettingsTab5Layout::Progress::H}; ///< Original 200,140,400,200
    bool m_active = false, m_autoCenter = true;          ///< Geöffnet, mittig in der Szene
    int m_progress = 0, m_animation = 0, m_segmentWidth = SettingsTab5Layout::Progress::SEGMENT, m_gapWidth = SettingsTab5Layout::Progress::SEGMENT_GAP; ///< Fortschritt 0..100 oder -1 unbestimmt. Animationsstand 0..100. Segment- und Lückenbreite in Pixeln
    uint32_t m_animationTime = 0;                        ///< Zeit des letzten Animationsschritts in ms
};

#endif
