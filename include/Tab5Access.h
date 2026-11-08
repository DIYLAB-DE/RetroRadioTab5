#ifndef TAB5_ACCESS_H
#define TAB5_ACCESS_H

#include <M5GFX.h>
#include <time.h>

/** @brief PIN-Sperrbildschirm nativ in 1280x720 (Maße in MainScreenTab5Layout.h). Hardware, gespeicherte PIN und Navigationszustand gehören dem zentralen Aufrufer. */
class Tab5Access
{
public:
    enum class TouchPhase : uint8_t
    {
        Press,
        Move,
        Release,
        Cancel
    };
    using PinValidator = bool (*)(const char *);

    /** @brief Erstellt eine inaktive Sperransicht ohne Dienstezugriff. */
    Tab5Access() = default;
    /** @brief Gibt eigene Ressourcen frei und löscht die laufende PIN-Eingabe. */
    ~Tab5Access();
    Tab5Access(const Tab5Access &) = delete;
    Tab5Access &operator=(const Tab5Access &) = delete;

    /** @brief Reserviert Ziffernfeld und Schrift einmalig. @param display Physisches Display mit 1280x720 bei bereits eingestellter Rotation. @return false bei unpassender Größe oder Ressourcenfehler. */
    bool begin(lgfx::LGFX_Device &display);

    /** @brief Bindet die zentrale PIN-Prüfung an. @param validator Rückruf ohne Speicherung des kurzlebigen Eingabezeigers, nullptr lässt die Sperre geschlossen. */
    void setPinValidator(PinValidator validator)
    {
        m_validator = validator;
    }

    /** @brief Aktiviert die Logoansicht und verwirft jede Eingabe. @param now Monotoner Millisekundenwert. @param contactHeld Ein beim Aktivieren noch liegender Finger muss zuerst losgelassen werden. */
    void activate(uint32_t now, bool contactHeld = false);
    /** @brief Gibt die Sperre ohne PIN frei, wenn die PIN-Sperre ausgeschaltet wurde. Verwirft Eingabe, Kontakt und Pulse, der Aufrufer zeichnet danach den MainScreen. */
    void deactivate();

    /** @brief Meldet, ob die PIN-Sperre aktuell Eingaben und Anzeige besitzt. @return true bis zur erfolgreichen PIN-Prüfung. */
    bool active() const
    {
        return m_active;
    }

    /** @brief Aktualisiert Pulsphasen und das Ausblenden nach Inaktivität (Standard: 30 Sekunden). @param now Monotoner Millisekundenwert. */
    void update(uint32_t now);
    /** @brief Verarbeitet einen vom zentralen Besitzer gelieferten Kontakt. @param phase Kontaktphase. @param x Physische X-Koordinate. @param y Physische Y-Koordinate. @param now Monotoner Millisekundenwert. @return true ausschließlich beim erfolgreichen OK-Loslassen, der Aufrufer zeichnet danach den MainScreen. */
    bool touch(TouchPhase phase, int32_t x, int32_t y, uint32_t now);

    /** @brief Ändert den Inaktivitätszeitraum. @param timeoutMs Millisekunden, 0 deaktiviert das Ausblenden. */
    void setIdleTimeoutMs(uint32_t timeoutMs)
    {
        m_idleTimeoutMs = timeoutMs;
    }

private:
    static constexpr size_t MaxInputLength = 16; ///< Längste PIN-Eingabe in Zeichen
    lgfx::LGFX_Device *m_display = nullptr;
    lgfx::LGFX_Sprite m_keypad;
    lgfx::PointerWrapper m_fontData;
    lgfx::VLWfont m_font;
    PinValidator m_validator = nullptr;
    char m_input[MaxInputLength + 1]{};
    uint8_t m_inputLength = 0;
    bool m_ready = false, m_active = false, m_touchActive = false, m_keypadVisible = false;
    int8_t m_pressedKey = -1;
    bool m_buttonPulseActive = false;
    uint8_t m_buttonPulsePhase = 0; ///< Laufende Phase der zwei Rückmeldungspulse (je an/aus)
    uint16_t m_buttonPulseColor = 0;
    uint32_t m_buttonPulsePhaseStart = 0, m_lastActivityMs = 0, m_idleTimeoutMs = 30000;
    uint16_t *m_logoGrau = nullptr, *m_madeInGrau = nullptr; ///< Logo und Schriftzug als graue Kopie im PSRAM bei MerkmalGraustufen, sonst nullptr

    /** @brief Gibt nur eigene Ressourcen frei. */
    void release();
    /** @brief Löscht den Eingabepuffer auch vor Compileroptimierung geschützt. */
    void clearInput();
    /** @brief Zeichnet die Logos und optional das Keypad. */
    void drawScreen();
    /** @brief Zeichnet Ziffernfeld, Druckfüllung und Pulsrahmen. */
    void renderKeypad(int8_t pressedKey = -1, bool pulseFrames = false, uint16_t pulseColor = 0);
    /** @brief Prüft die inklusiven Tastengrenzen und liefert den Tastenindex, sonst -1. */
    static int8_t keyAt(int32_t x, int32_t y);
    /** @brief Verarbeitet die beim Loslassen abgeschlossene Taste. */
    bool handleKeyPress(int8_t keyIndex, uint32_t now);
    /** @brief Verwirft den Kontakt ohne eine Taste zu bestätigen. */
    void cancelTouch();
    /** @brief Startet zwei Rückmeldungspulse. */
    void startButtonPulse(uint16_t color, uint32_t now);
    /** @brief Beendet jede Pulsphase. */
    void cancelButtonPulse();
};

/** @brief Diagonale Sperrgeste an den beiden linken Ecken des Displays. */
class Tab5CornerGesture
{
public:
    /** @brief Prüft genau einen zentral zugeordneten Kontakt. @param phase Kontaktphase. @param x Physische X-Koordinate. @param y Physische Y-Koordinate. @return true bei abgeschlossener Einwärts-/Auswärtsgeste, der Aufrufer aktiviert die Sperre. */
    bool touch(Tab5Access::TouchPhase phase, int32_t x, int32_t y);

    /** @brief Meldet die laufende Eckengeste, während der MainScreen-Tasten gesperrt bleiben. @return true nach Eintritt in eine der beiden Startzonen. */
    bool tracking() const
    {
        return m_tracking;
    }

    /** @brief Verwirft eine Geste beim Loslassen, Abbruch oder Ansichtswechsel. */
    void reset();

private:
    bool m_tracking = false, m_upper = false, m_startedInCorner = false;
    int32_t m_startX = 0, m_startY = 0;
};

/** @brief Reine Zeitfenster-Zustandsmaschine aus RetroRadio. Der Aufrufer übernimmt Sperre, Audiostopp und Hintergrundbeleuchtung. */
class Tab5TimeWindow
{
public:
    enum class Event : uint8_t
    {
        None,
        Sleep,
        Wake
    };
    /** @brief Wertet den aktuellen lokalen Slot gegen die 24 Stundenbits pro Tag aus. @param timeValid Zentral geprüfte Zeitgültigkeit. @param local Lokale Zeit mit tm_wday Sonntag=0. @param masks Montag..Sonntag, Bit 0..23 aktiviert die jeweilige Stunde. @return Übergang zum Schlafen oder Aufwecken, sonst None. Bei Wake erneut sperren. */
    Event evaluate(bool timeValid, const tm &local, const uint32_t (&masks)[7]);
    /** @brief Liefert den aktuellen Berührungszustand auch ohne Flanke. @param touched true bei liegendem Finger, false bei unberührtem Display. Nach Sleep erst false liefern. @return Wake beim folgenden Kontakt, gültig als manuelle Ausnahme nur für den aktuellen inaktiven Slot. Der Aufweckkontakt darf keine PIN-Taste auslösen. */
    Event observeTouch(bool touched);

    /** @brief Versetzt das Gerät nach abgelaufenem Radiotimer bis zum nächsten freigegebenen Kontakt in Schlaf. @return Sleep. */
    Event sleepNow()
    {
        m_timerSleep = true;
        m_sleeping = m_requireRelease = true;
        return Event::Sleep;
    }

    /** @brief Meldet den eigenen Zeitfenster-Schlafzustand. @return true nach Sleep bis Wake. */
    bool sleeping() const
    {
        return m_sleeping;
    }

private:
    static constexpr uint8_t InvalidSlot = 0xFF;
    uint8_t m_day = InvalidSlot, m_hour = InvalidSlot;
    uint8_t m_manualDay = InvalidSlot, m_manualHour = InvalidSlot;
    bool m_timerSleep = false;
    bool m_sleeping = false, m_requireRelease = false, m_enabled = true;
};

#endif
