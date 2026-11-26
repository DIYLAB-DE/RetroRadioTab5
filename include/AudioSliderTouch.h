#ifndef AUDIO_SLIDER_TOUCH_H
#define AUDIO_SLIDER_TOUCH_H

#include <stdint.h>

/**
 * @brief Unterscheidet Tippen, Ziehen und Gedrückthalten an den vier Klangreglern von Radio und SD-Player.
 *
 * Koordinaten liegen in der Reglerfläche (RadioTab5Layout::Tracks, Ursprung links oben bei X/Y). Wer den Knopf eines Reglers 800 ms ruhig hält, stellt nur diesen Regler auf die Mittellinie (Balance 0 oder 0 dB). Ziehen schließt die Haltegeste bis zum Loslassen aus. Nur aus dem UI-Task aufrufen.
 */
class AudioSliderTouch
{
public:
    static constexpr int COUNT = 4;          ///< Balance, Tiefen, Mitten und Höhen
    static constexpr uint32_t HOLD_MS = 800; ///< Haltedauer bis zur Mittelstellung, wie im Original

    /** @brief Ergebnis einer Touchmeldung. */
    struct Target
    {
        int index = -1;      ///< Betroffener Regler (0 Balance, 1 bis 3 Klangbänder) oder -1 ohne Änderung
        int y = 0;           ///< Neue Knopfmitte in der Reglerfläche
        bool center = false; ///< Langer Druck hat die Mittelstellung ausgelöst
    };

    /**
     * @brief Wertet eine Touchmeldung aus und liefert je Berührung höchstens einen Sprung zur Mitte.
     * @param touched true bei Kontakt, false beim Loslassen
     * @param x X-Position in der Reglerfläche
     * @param y Y-Position in der Reglerfläche
     * @param positions Knopfmitten der vier Regler in der Reglerfläche, aus position()
     * @param now Zeit in Millisekunden
     * @return Ziel für Anzeige und Klang oder ein leeres Ziel. Ein kurzer Tipp auf einen Knopf liefert sein Ziel erst beim Loslassen.
     */
    Target update(bool touched, int x, int y, const int (&positions)[COUNT], uint32_t now);

    /** @brief Beendet die aktuelle Berührung ohne Ergebnis, etwa wenn ein Dialog sie übernimmt oder der Kontakt ohne Loslassen entzogen wird. Die nächste Meldung beginnt eine neue Berührung. */
    void reset()
    {
        m_phase = Phase::Idle;
    }

    /**
     * @brief Rechnet einen Reglerwert in die Knopfmitte um, wie die Zeichnung der Regler.
     * @param index 0 Balance (-16..16), 1 bis 3 Klangband (-12..12 dB)
     * @param value Reglerwert, wird auf seinen Bereich begrenzt
     * @return Y-Position in der Reglerfläche
     */
    static int position(int index, int value);

    /**
     * @brief Rechnet eine Knopfmitte in den Reglerwert um.
     * @param index 0 Balance, 1 bis 3 Klangband
     * @param y Y-Position in der Reglerfläche, wird auf die Bahn begrenzt
     * @return Balance -16..16 oder Verstärkung -12..12 dB
     */
    static int value(int index, int y);

private:
    /** @brief Zustand der aktuellen Berührung. */
    enum class Phase
    {
        Idle,    ///< Keine Berührung
        Holding, ///< Finger ruht auf einem Knopf
        Sliding, ///< Berührung kann nur noch tippen oder ziehen
        Consumed ///< Mittelstellung ausgelöst, bis zum Loslassen keine weitere Änderung
    };

    Phase m_phase = Phase::Idle; ///< Zustand der aktuellen Berührung
    int m_slider = 0;            ///< Regler, auf dessen Knopf der Druck begann
    int m_startX = 0;            ///< Anfangsposition der Haltegeste
    int m_startY = 0;            ///< Anfangsposition für Haltegeste und kurzen Tipp
    uint32_t m_startTime = 0;    ///< Beginn der Haltegeste in Millisekunden
};

#endif // AUDIO_SLIDER_TOUCH_H
