#ifndef RADIO_PRESET_DIALOG_H
#define RADIO_PRESET_DIALOG_H

#include <functional>

#include "UiButton.h"
#include "RadioDialogCanvas.h"

#define MAX_PRESETS 10 ///< Höchstzahl der angezeigten Favoriten

struct Station;

/**
 * @brief Favoriteneintrag mit Stationsreferenz
 *
 * @note Speichert nur den Zeiger auf den Stationsnamen, keine Kopie.
 *       Die Quelldaten müssen für die Dialogdauer gültig bleiben.
 */
struct PresetEntry
{
    const char *name; ///< Name des Presets (Zeiger auf Station-Name, nicht kopiert)
    int stationIndex; ///< Index der zugehörigen Station
};

/**
 * @brief Modaler Dialog zur Auswahl von Favoriten
 *
 * Zeigt bis zu 10 Favoriten in zwei Spalten à fünf Tasten. Die Auswahl erfolgt per Touch, den die Zentrale über RadioDialogCanvas zuteilt, mit visueller Rückmeldung, update() läuft im Oberflächentask.
 */
class RadioPresetDialog
{
public:
    /**
     * @brief Legt den Dialog an, er bleibt bis start() inaktiv
     * @param gfx Vorbereitete Zeichenfläche mit zentral zugeteiltem Touch, geliehen, nicht nullptr und bis zur Zerstörung gültig
     */
    explicit RadioPresetDialog(RadioDialogCanvas *gfx);

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
     * @param title Titeltext (der Zeiger muss für die Dialogdauer gültig bleiben), nullptr setzt "Favoriten"
     */
    void setTitle(const char *title);

    /**
     * @brief Registriert Callback für Auswahlereignisse
     * @param cb Funktion mit Parameter int (stationIndex oder -1 für Abbruch oder Timeout), wird einmal beim Beenden aufgerufen
     */
    void setCallback(std::function<void(int selectedIndex)> cb);

    /**
     * @brief Aktiviert automatisches Timeout
     * @param millis Timeout in Millisekunden (0 = deaktiviert)
     *
     * Der Timeout-Timer wird bei jeder Touch-Interaktion zurückgesetzt (Keep-alive), sodass der Dialog nur bei Inaktivität schließt.
     */
    void setTimeout(uint32_t millis);

    /**
     * @brief Startet den Dialog mit der Senderliste
     * @param stations Array mit count Stationen, bei count > 0 nicht nullptr
     * @param count Anzahl der Stationen im Array
     *
     * @note Die Stationsnamen werden nur als Zeiger gespeichert, nicht kopiert.
     *       Die Stationsdaten müssen für die gesamte Dialogdauer gültig bleiben.
     * @note Erfordert gültige Dialoggrenzen (setBounds mit w,h > 0).
     *       Bei ungültigen Grenzen wird nicht gestartet.
     * @note Es werden nur die ersten MAX_PRESETS Favoriten angezeigt.
     */
    void start(Station *stations, int count);

    /**
     * @brief Verarbeitet Touch und Timeout, ohne aktiven Dialog wirkungslos
     * @note Muss im Oberflächentask regelmäßig aufgerufen werden
     */
    void update();

    /**
     * @brief Prüft, ob der Dialog aktiv ist
     * @return true wenn aktiv, false sonst
     */
    bool isActive() const;

    /**
     * @brief Gibt den zuletzt ausgewählten Station-Index zurück
     * @return Station-Index oder -1 wenn abgebrochen/keine Auswahl
     */
    int getSelectedIndex() const;

    /** @brief Beendet den Dialog ohne Rückruf, der zuletzt ausgewählte Index bleibt erhalten. */
    void cancel()
    {
        callback = nullptr;
        state = Inactive;
        touchState = TouchState();
    }

private:
    /**
     * @brief Layoutmaße für 1280x720 (Originalwerte bei 800x480 jeweils angegeben)
     */
    struct UIConfig
    {
        static constexpr int PADDING = 30;          ///< Innenabstand, Original 20
        static constexpr int BORDER_RADIUS = 6;     ///< Eckenradius, Original 4
        static constexpr int BORDER = 2;            ///< Dialograhmen, Original 1
        static constexpr int TITLE_Y_OFFSET = 15;   ///< Original 10
        static constexpr int TITLE_Y_SHIFT = 12;    ///< Zusätzlicher Versatz der Titelzeile, Original 8
        static constexpr int SEPARATOR_Y = 72;      ///< Original 48
        static constexpr int SEPARATOR_HEIGHT = 3;  ///< Original 2
        static constexpr int CONTENT_START_Y = 105; ///< Original 70
        static constexpr int EMPTY_HINT_Y = 120;    ///< Hinweis ohne Favoriten unter CONTENT_START_Y, Original 80

        // Tasten
        static constexpr int BUTTON_HEIGHT = 68;        ///< Original 45
        static constexpr int BUTTON_SPACING = 22;       ///< Original 15, abgerundet von 22,5, damit das Raster 68 + 22 = 90 genau 1,5 x 60 bleibt
        static constexpr int CANCEL_BUTTON_HEIGHT = 68; ///< Original 45
        static constexpr int NUMBER_CIRCLE_SIZE = 60;   ///< Original 40
        static constexpr int BUTTON_BORDER = 2;         ///< Original 1
        static constexpr int BUTTON_BORDER_PRESSED = 3; ///< Original 2
        static constexpr int BUTTON_INSET = 8;          ///< Abstand Zahlenkreis zur Kante, Original 5
        static constexpr int BUTTON_ICON_GAP = 15;      ///< Abstand Zahlenkreis zum Namen, Original 10
        static constexpr int BUTTONS_PER_COLUMN = 5;    ///< Tasten je Spalte
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
    UiButton presetButtons[MAX_PRESETS]; ///< Favoritentasten
    UiButton btnCancel;                  ///< Abbruchtaste

    PresetEntry presetEntries[MAX_PRESETS]; ///< Angezeigte Favoriten
    int presetCount = 0;                    ///< Anzahl in presetEntries

    const char *titleText = "Favoriten"; ///< Titel (Zeiger, nicht kopiert)

    int dialogX = 0; ///< Linke Kante
    int dialogY = 0; ///< Obere Kante
    int dialogW = 0; ///< Breite
    int dialogH = 0; ///< Höhe

    State state = Inactive;                    ///< Zustand des Dialogs
    int selectedIndex = -1;                    ///< Zuletzt gewählter Senderindex, -1 bei Abbruch oder ohne Auswahl
    std::function<void(int)> callback = nullptr; ///< Rückruf beim Beenden

    uint32_t timeoutMs = 0; ///< Timeout in ms, 0 = deaktiviert
    uint32_t startTime = 0; ///< Zeitpunkt der letzten Aktivität in ms

    /**
     * @brief Verfolgt den aktuellen Touchkontakt
     */
    struct TouchState
    {
        bool isTouching = false; ///< Kontakt liegt an
        int activeButton = -1;   ///< -1 keine Taste, 0..9 Favorit, 99 Abbruch
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
     */
    void drawHeader();

    /**
     * @brief Zeichnet die Trennlinie
     */
    void drawSeparator();

    /**
     * @brief Zeichnet alle Preset-Buttons oder Hinweis bei leerer Liste
     */
    void drawPresetButtons();

    /**
     * @brief Zeichnet die Abbruchtaste
     */
    void drawCancelButton();

    /**
     * @brief Verarbeitet Touch-Eingaben, die Aktion gilt beim Loslassen auf der gedrückten Taste
     *
     * Setzt bei Touch-Aktivität den Timeout-Timer zurück (Keep-alive).
     */
    void processTouchInput();

    /**
     * @brief Prüft Timeout und führt ggf. Abbruch aus
     *
     * Timeout greift nur bei Inaktivität - Touch-Interaktion setzt den Timer in processTouchInput() zurück.
     */
    void checkTimeout();

    /**
     * @brief Findet die Taste an einer Touchposition
     * @return Presetindex 0..presetCount-1, 99 für Abbruch oder -1
     */
    int findButtonAtPosition(int x, int y);

    /**
     * @brief Aktualisiert die Hervorhebung der gedrückten Taste
     * @param newActiveButton Index der neu aktiven Taste (0..9, 99 Abbruch, -1 keine)
     *
     * Zeichnet nur die tatsächlich geänderten Buttons neu (max. 2), nicht alle. Nutzt touchState.activeButton als vorherigen Zustand.
     */
    void updateButtonHighlight(int newActiveButton);

    /**
     * @brief Beendet den Dialog und ruft den Rückruf einmal auf
     * @param result Senderindex oder -1 bei Abbruch
     */
    void finish(int result);
};

#endif // RADIO_PRESET_DIALOG_H
