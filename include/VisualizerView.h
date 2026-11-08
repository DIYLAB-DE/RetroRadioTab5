#ifndef VISUALIZER_VIEW_H
#define VISUALIZER_VIEW_H

#include "AudioManager.h"
#include "Visualizer.h"
#include "RadioPreferences.h"

/** @brief Gemeinsamer Visualizer für Radio und SD-Player: eine Instanz in 1,5-facher Geometrie, die der aktive Screen an seine Stelle legt. Nur der aktive Screen konsumiert PCM über update(). Läuft im Oberflächentask. */
class VisualizerView
{
public:
    static constexpr int RadioWidth = 689, RadioHeight = 189; ///< Breite und Höhe des Visualizerbereichs in Displaypixeln
    /** @brief Reserviert die Zeichenfläche und bereitet alle acht Darstellungsmodi vor der Wiedergabe vor. @param display Zentrales Display. @return false bei Allokationsfehler, dann ist alles wieder freigegeben. true auch bei wiederholtem Aufruf mit demselben Display. */
    bool begin(lgfx::LGFX_Device &display);
    /** @brief Legt den Ausgabeort fest, ohne zu skalieren oder zu allokieren, und verwirft bei Änderung alte Signalverläufe. @param x Linke Kante auf 1280x720. @param y Obere Kante auf 1280x720. @param width Muss RadioWidth sein. @param height Muss RadioHeight sein. @return false vor begin(), bei anderer Größe oder wenn der Bereich über das Display hinausragt. Die bisherige Lage bleibt dann erhalten. */
    bool setBounds(int x, int y, int width, int height);
    /** @brief Erzwingt, dass das nächste update() alle Teilflächen vollständig überträgt, etwa nach einem Modal oder Bildschirmwechsel. */
    void invalidate();
    /** @brief Übernimmt PCM samt Quellrate aus dem AudioManager, zeichnet und überträgt die geänderten Teilflächen ohne Skalierung. @param now Monotone Zeit in ms. @param audio Zentraler AudioManager. @param silent Ton verwerfen und Ruhe zeigen, etwa während der Senderwahl. */
    void update(uint32_t now, AudioManager &audio, bool silent = false);
    /** @brief Wechselt den Modus im Kreis. @param direction Schrittzahl, positiv vorwärts, negativ rückwärts. */
    void nextMode(int direction);
    /** @brief Wechselt den Stil des aktuellen Modus. */
    void nextStyle();
    /** @brief Übernimmt die zentrale Sprache. @param lang Gewünschte Sprache. */
    void setLanguage(LangIndex lang);
    /** @brief Setzt Ballistik und Empfindlichkeit der VU-Instrumente. @param ballistics Wert aus VuBallistics. @param sensitivity Stufe 0..4. */
    void setVuSettings(uint8_t ballistics, uint8_t sensitivity);
    /** @brief Setzt den Modus, ein ungültiger Wert wird ignoriert. @param mode Visualizer-Modus. */
    void setMode(VisMode mode);
    /** @brief Verwirft alte Wellenform- und FFT-Verläufe beim Quellenwechsel, ohne Puffer neu anzulegen. */
    void resetSignal();

    /** @brief Aktueller Modus. */
    VisMode mode() const
    {
        return m_active ? m_active->visualizer.getMode() : m_radio.visualizer.getMode();
    }

    /** @brief Reicht einen Touchpunkt relativ zur linken oberen Ecke des Bereichs an die Wisch- und Halteerkennung weiter. @param touched Kontakt gedrückt. */
    void handleTouch(int x, int y, bool touched);

    /** @brief Kopiert Modus und die acht Stile in den speicherbaren Zustand. */
    void preferences(RadioPreferences &value) const
    {
        (m_active ? m_active : &m_radio)->visualizer.preferences(value);
    }

    /** @brief Stellt Modus und Stile aus dem gespeicherten Zustand wieder her. */
    void applyPreferences(const RadioPreferences &value)
    {
        m_radio.visualizer.applyPreferences(value);
        invalidate();
    }

    /** @brief Verwirft eine zentral abgebrochene Geste ohne Moduswechsel. */
    void cancelTouch()
    {
        m_radio.visualizer.cancelTouch();
    }

    /** @brief begin() war erfolgreich. */
    bool ready() const
    {
        return m_ready;
    }

private:
    /** @brief Zeichenfläche mit zugehörigem Visualizer. */
    struct Bank
    {
        Gfx canvas;            ///< Zeichenfläche in der Größe des Bereichs
        Visualizer visualizer; ///< Darstellungsmodi und Zeichenlogik
    };

    Bank m_radio;                           ///< Die einzige Instanz, genutzt von Radio und SD-Player
    Bank *m_active = nullptr;               ///< Aktive Instanz, nullptr vor begin()
    lgfx::LGFX_Device *m_display = nullptr; ///< Ausgabedisplay
    int m_x = 0, m_y = 0;                   ///< Linke obere Ecke des Bereichs auf dem Display
    bool m_ready = false, m_force = true, m_tailWasGonio = false; ///< begin() erfolgreich, nächste Übertragung vollständig, der Goniometerfuß unter dem Streifen war zuletzt sichtbar
    static constexpr uint32_t RefreshMs = 1000; ///< Abstand der vollständigen Auffrischung der eigenen Flächen samt Zwischenzeile bis zur Tastenreihe, unabhängig vom Zeilenvergleich
    uint32_t m_lastRefresh = 0;                 ///< Zeitpunkt der letzten vollständigen Auffrischung
};
#endif
