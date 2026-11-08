#ifndef SD_CARD_WATCH_H
#define SD_CARD_WATCH_H
#include <cstdint>

/** @brief Überwachung der eingebundenen SD-Karte ohne eigenen Kartenzugriff. Gehört allein dem Dienstworker, der die gemeldeten Schritte ausführt. Antwortet die Karte nicht mehr, steigt die Kartengeneration, damit Aufträge und Titellisten der alten Karte ungültig werden. Neu eingebunden wird nur auf Anforderung. */
class SdCardWatch
{
public:
    static constexpr uint32_t ProbeMs = 2000; ///< Abstand der Statusabfrage bei eingebundener Karte in Millisekunden

    /** @brief Nächster Schritt für den Dienstworker. */
    enum class Step : uint8_t
    {
        None,    ///< Nichts zu tun
        Probe,   ///< Statusbefehl an die Karte senden und probed() melden
        Unmount, ///< Verlorene Karte aushängen und unmounted() melden
        Mount    ///< Karte einbinden und mounted() melden
    };

    /** @brief Meldet das Ergebnis eines Einbindeversuchs. @param ready Karte eingebunden. @param now Monotone Zeit in Millisekunden. */
    void mounted(bool ready, uint32_t now);
    /** @brief Wertet den Statusbefehl aus: Ohne Antwort gilt die Karte als verloren, die Generation steigt und das Aushängen steht an. @param answered Karte hat geantwortet. @return true, wenn die Karte damit als verloren gilt. */
    bool probed(bool answered);
    /** @brief Meldet die ausgehängte verlorene Karte, erst danach kann neu eingebunden werden. */
    void unmounted();
    /** @brief Liefert den nächsten Schritt, ohne zu warten. @param now Monotone Zeit in Millisekunden. @param audioBusy Der Audioworker kann eine Datei der Karte offen haben. Das Aushängen wartet dann. */
    Step poll(uint32_t now, bool audioBusy);

    /** @brief Fordert erneutes Einbinden an, wirkt nur ohne bereite Karte und wird nach dem Aushängen einer verlorenen Karte ausgeführt. */
    void retry()
    {
        if (!m_ready)
            m_retry = true;
    }

    /** @brief Lässt nach einem gescheiterten Kartenauftrag beim nächsten poll() sofort prüfen. */
    void failed()
    {
        m_probeNow = m_ready;
    }

    /** @brief Meldet eine eingebundene Karte, die zuletzt geantwortet hat. */
    bool ready() const
    {
        return m_ready;
    }

    /** @brief Liefert die Kartengeneration, nie 0. */
    uint32_t generation() const
    {
        return m_generation;
    }

private:
    bool m_ready = false;     ///< Karte eingebunden und zuletzt erreichbar
    bool m_mounted = false;   ///< Datenträger eingebunden, auch eine verlorene Karte bis zum Aushängen
    bool m_retry = false;     ///< Einbinden angefordert
    bool m_probeNow = false;  ///< Nach einem Fehlschlag ohne Wartezeit prüfen
    uint32_t m_generation = 1; ///< Kartengeneration
    uint32_t m_probeAt = 0;   ///< Zeit der letzten Statusabfrage oder des Einbindens in Millisekunden
};
#endif
