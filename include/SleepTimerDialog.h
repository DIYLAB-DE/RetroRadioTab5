#ifndef SLEEP_TIMER_DIALOG_H
#define SLEEP_TIMER_DIALOG_H

#include <functional>

#include "UiButton.h"
#include "RadioDialogCanvas.h"

#define MAX_TIMER_OPTIONS 6 ///< Höchstzahl der Timer-Optionen, zwei Zeilen à drei Tasten

/**
 * @brief Modaler Sleep-Timer-Dialog samt Countdown
 *
 * Zeigt bis zu sechs Zeitoptionen sowie Abbruch und, bei laufendem Countdown, Stopp. Der Countdown läuft über das Schließen des Dialogs hinaus weiter, solange update() aufgerufen wird. Touch teilt die Zentrale über RadioDialogCanvas zu, update() läuft im Oberflächentask.
 */
class SleepTimerDialog
{
public:
    /**
     * @brief Legt den Dialog an, er bleibt bis start() inaktiv
     * @param gfx Vorbereitete Zeichenfläche mit zentral zugeteiltem Touch, geliehen, nicht nullptr und bis zur Zerstörung gültig
     */
    explicit SleepTimerDialog(RadioDialogCanvas *gfx);

    /**
     * @brief Definiert Position und Größe des Dialogs
     * @param x X-Position (linke obere Ecke)
     * @param y Y-Position (linke obere Ecke)
     * @param w Breite in Pixeln
     * @param h Höhe in Pixeln
     */
    void setBounds(int x, int y, int w, int h);

    /**
     * @brief Setzt den Dialog-Titel
     * @param title Geliehener Titeltext, bis zur nächsten Änderung oder Zerstörung gültig, nullptr setzt "Sleep-Timer"
     */
    void setTitle(const char *title);

    /**
     * @brief Setzt den Beschreibungstext
     * @param message Geliehener Nachrichtentext, bis zur nächsten Änderung oder Zerstörung gültig, nullptr verwendet einen Leerstring
     */
    void setMessage(const char *message);

    /**
     * @brief Setzt das Label des Abbruch-Buttons
     * @param label Neuer Text. Bei nullptr bleibt der bisherige Wert erhalten.
     *
     * Das Label wird intern kopiert, der Aufrufer darf einen Stack-Puffer übergeben. Default ist "abbruch", solange der Setter nicht aufgerufen wurde.
     */
    void setCancelLabel(const char *label);

    /**
     * @brief Setzt das Label des Stopp-Buttons (sichtbar bei laufendem Sleep-Countdown)
     * @param label Neuer Text. Bei nullptr bleibt der bisherige Wert erhalten.
     *
     * Das Label wird intern kopiert, der Aufrufer darf einen Stack-Puffer übergeben. Default ist "stopp", solange der Setter nicht aufgerufen wurde.
     */
    void setStopLabel(const char *label);

    /**
     * @brief Konfiguriert die verfügbaren Timer-Optionen
     * @param labels Array mit Beschriftungen (nicht nullptr), die Texte werden nur referenziert und müssen bis zur nächsten Änderung oder Zerstörung gültig bleiben
     * @param values Array mit Minutenwerten (nicht nullptr), die Werte werden kopiert
     * @param count Anzahl der Optionen (max. MAX_TIMER_OPTIONS)
     *
     * Bei ungültigen Eingaben (nullptr oder count <= 0) wird buttonCount auf 0 gesetzt. Einzelne nullptr-Labels werden durch Leerstrings ersetzt.
     */
    void setOptions(const char *labels[], const int values[], int count);

    /**
     * @brief Registriert Callback für Timer-Auswahl
     * @param cb Funktion mit Parameter int: Minutenwert der gewählten Option, -1 Abbruch oder Timeout, -2 Countdown gestoppt, wird einmal beim Schließen aufgerufen
     *
     * @note std::function kopiert das Callable und kann dafür Speicher reservieren, referenzierte Objekte müssen bis zum Rückruf oder Abbruch gültig bleiben.
     */
    void setCallback(std::function<void(int minutes)> cb);

    /**
     * @brief Setzt die maximale Dialogdauer ab start, Touch verlängert diese Frist nicht
     * @param millis Timeout in Millisekunden (0 = deaktiviert)
     */
    void setTimeout(uint32_t millis);

    /**
     * @brief Startet einen Sleep-Countdown
     * @param minutes Dauer in Minuten, Aufrufer begrenzt auf 1..71582, damit Minuten * 60000 in uint32_t passt
     * @param onTimeout Callback-Funktion bei Timer-Ablauf, wird von update() einmal aufgerufen
     *
     * Bei ungültigen Werten (minutes <= 0) wird ein eventuell laufender Timer abgebrochen und kein neuer gestartet.
     *
     * @note std::function kopiert das Callable und kann dafür Speicher reservieren, referenzierte Objekte müssen bis zum Rückruf oder Abbruch gültig bleiben.
     */
    void startSleepCountdown(int minutes, std::function<void()> onTimeout);

    /**
     * @brief Zeigt den Dialog an
     */
    void start();

    /**
     * @brief Überwacht den Countdown und verarbeitet bei aktivem Dialog Touch und Dialog-Timeout
     * @note Muss im Oberflächentask regelmäßig aufgerufen werden, auch bei geschlossenem Dialog, damit der Countdown abläuft
     */
    void update();

    /**
     * @brief Stoppt den aktiven Sleep-Timer
     */
    void cancelSleepTimer();

    /**
     * @brief Prüft, ob der Dialog aktiv ist
     * @return true wenn aktiv, false sonst
     */
    bool isActive() const;

    /**
     * @brief Prüft, ob ein Sleep-Timer läuft
     * @return true wenn Timer aktiv, false sonst
     */
    bool isTimerActive() const;

    /**
     * @brief Gibt die verbleibende Zeit zurück
     * @param[out] minutes Verbleibende Minuten (-1 wenn inaktiv)
     * @param[out] seconds Verbleibende Sekunden (-1 wenn inaktiv)
     */
    void getRemainingTime(int &minutes, int &seconds) const;

    /** @brief Beendet Dialog und laufenden Countdown ohne Auswahl- oder Ablaufcallback. */
    void cancel()
    {
        callback = nullptr;
        state = Inactive;
        touchState = TouchState();
        cancelSleepTimer();
    }

private:
    /**
     * @brief Layoutmaße für 1280x720 (Originalwerte bei 800x480 jeweils angegeben)
     */
    struct UIConfig
    {
        static constexpr int PADDING = 30;         ///< Innenabstand, Original 20
        static constexpr int BORDER_RADIUS = 6;    ///< Eckenradius, Original 4
        static constexpr int BORDER = 2;           ///< Dialograhmen, Original 1
        static constexpr int TITLE_Y_OFFSET = 15;  ///< Original 10
        static constexpr int TITLE_Y_SHIFT = 12;   ///< Zusätzlicher Versatz der Titelzeile, Original 8
        static constexpr int SEPARATOR_Y = 72;     ///< Original 48
        static constexpr int SEPARATOR_HEIGHT = 3; ///< Original 2
        static constexpr int MESSAGE_Y = 98;       ///< Original 65

        // Tasten
        static constexpr int BUTTON_HEIGHT = 68;        ///< Original 45
        static constexpr int BUTTON_SPACING = 22;       ///< Original 15, abgerundet von 22,5, damit das Raster 68 + 22 = 90 genau 1,5 x 60 bleibt
        static constexpr int BUTTON_BORDER = 2;         ///< Original 1
        static constexpr int BUTTON_BORDER_PRESSED = 3; ///< Original 2
        static constexpr int MATRIX_ROWS = 2;           ///< Zeilen der Optionstasten
        static constexpr int MATRIX_COLS = 3;           ///< Spalten der Optionstasten
    };

    /**
     * @brief Zustand des Dialogs
     */
    enum State
    {
        Inactive, ///< Dialog nicht aktiv
        Active    ///< Dialog aktiv und wartet auf Eingabe
    };

    RadioDialogCanvas *m_tft; ///< Geliehene Zeichenfläche, kein Hardwarebesitz
    UiButton buttons[MAX_TIMER_OPTIONS]; ///< Optionstasten
    UiButton btnCancel;                  ///< Abbruchtaste
    UiButton btnStopTimer;               ///< Stopptaste, nur bei laufendem Countdown

    const char *buttonLabels[MAX_TIMER_OPTIONS]; ///< Beschriftungen der Optionen (Zeiger, nicht kopiert)
    int buttonValues[MAX_TIMER_OPTIONS];         ///< Minutenwerte der Optionen
    int buttonCount = 0;                         ///< Anzahl der Optionen

    const char *titleText = "Sleep-Timer"; ///< Titel (Zeiger, nicht kopiert)
    const char *messageText = "";          ///< Beschreibungstext (Zeiger, nicht kopiert)
    char cancelLabel[16] = "abbruch"; ///< Label des Abbruch-Buttons, durch setCancelLabel() überschreibbar
    char stopLabel[16] = "stopp";     ///< Label des Stopp-Buttons, durch setStopLabel() überschreibbar

    int dialogX = 0; ///< Linke Kante
    int dialogY = 0; ///< Obere Kante
    int dialogW = 0; ///< Breite
    int dialogH = 0; ///< Höhe

    State state = Inactive;                      ///< Zustand des Dialogs
    std::function<void(int)> callback = nullptr; ///< Rückruf beim Schließen
    uint32_t timeoutMs = 0;                      ///< Dialog-Timeout in ms, 0 = deaktiviert
    uint32_t dialogStartTime = 0;                ///< Zeitpunkt von start() in ms

    bool sleepTimerActive = false;                  ///< Countdown läuft
    uint32_t sleepTimerStart = 0;                   ///< Beginn des Countdowns in ms
    uint32_t sleepTimerDuration = 0;                ///< Dauer des Countdowns in ms
    std::function<void()> timeoutCallback = nullptr; ///< Rückruf bei Ablauf des Countdowns

    /**
     * @brief Verfolgt den aktuellen Touchkontakt
     */
    struct TouchState
    {
        bool isTouching = false; ///< Kontakt liegt an
        int activeButton = -1;   ///< -1 keine Taste, 0..5 Option, 98 Stopp, 99 Abbruch
    } touchState;

    /**
     * @brief Zeichnet den kompletten Dialog
     */
    void draw();

    /**
     * @brief Zeichnet Rahmen und Hintergrund
     */
    void drawFrame();

    /**
     * @brief Zeichnet den Titel
     *
     * Setzt TextDatum auf TL_DATUM, um unabhängig vom externen Zustand korrekt zu zeichnen.
     */
    void drawHeader();

    /**
     * @brief Zeichnet die Trennlinie
     */
    void drawSeparator();

    /**
     * @brief Zeichnet den Beschreibungstext
     *
     * Setzt TextDatum auf TL_DATUM, um unabhängig vom externen Zustand korrekt zu zeichnen.
     */
    void drawMessage();

    /**
     * @brief Zeichnet die Optionstasten in zwei Zeilen über den Steuertasten
     */
    void drawTimerButtons();

    /**
     * @brief Zeichnet Abbruch- und bei laufendem Countdown zusätzlich die Stopptaste
     */
    void drawControlButtons();

    /**
     * @brief Verarbeitet Touch-Eingaben, die Aktion gilt beim Loslassen auf der gedrückten Taste
     */
    void processTouchInput();

    /**
     * @brief Schließt den Dialog mit Abbruch, wenn der Dialog-Timeout abgelaufen ist
     */
    void checkDialogTimeout();

    /**
     * @brief Prüft Sleep-Timer und führt ggf. Callback aus
     */
    void checkSleepTimer();

    /**
     * @brief Findet die Taste an einer Touchposition
     * @return Optionsindex 0..buttonCount-1, 98 für Stopp, 99 für Abbruch oder -1
     */
    int findButtonAtPosition(int x, int y);

    /**
     * @brief Aktualisiert die Hervorhebung der gedrückten Taste
     * @param newActiveButton Index der neu aktiven Taste (0..5, 98 Stopp, 99 Abbruch, -1 keine)
     *
     * Zeichnet nur die zuvor und die jetzt aktive Schaltfläche in den Canvas, Displaytransfer und Hintergrundwiederherstellung übernimmt der Aufrufer.
     */
    void updateButtonHighlight(int newActiveButton);

    /**
     * @brief Beendet den Dialog und ruft den Rückruf einmal auf
     * @param result Minutenwert der Option, -1 Abbruch oder Timeout, -2 Countdown gestoppt
     */
    void closeDialog(int result);
};

#endif // SLEEP_TIMER_DIALOG_H
