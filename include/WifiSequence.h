#ifndef WIFI_SEQUENCE_H
#define WIFI_SEQUENCE_H
#include <cstddef>
#include <cstdint>

/** @brief Ablauf der Verbindung mit den gespeicherten WLAN-Profilen ohne eigenen WLAN-Zugriff. Gehört allein dem Dienstworker, der die gemeldeten Schritte ausführt und dabei nie wartet. */
class WifiSequence
{
public:
    static constexpr size_t MaxProfiles = 3;     ///< Anzahl der Profilplätze
    static constexpr uint32_t AttemptMs = 20000; ///< Zeitgrenze eines Verbindungsversuchs je Profil in Millisekunden

    /** @brief Nächster Schritt für den Dienstworker. */
    enum class Step : uint8_t
    {
        None,     ///< Nichts zu tun
        Connect,  ///< Bisherige Verbindung trennen und Profil profile() verbinden
        Connected ///< Neue Verbindung mit IPv4-Adresse: Zeitzone und NTP übernehmen
    };

    /** @brief Beginnt den Ablauf bei Profil 1, auch wenn gerade eine Verbindung besteht. Danach freigegebene Profile der Reihe nach, nach dem letzten wieder von vorn, bis eine Verbindung steht. */
    void start();
    /** @brief Hält den Ablauf an, solange der Worker selbst trennt und verbindet (Verbindungstest). */
    void interrupt();
    /** @brief Wertet einen beendeten Verbindungstest aus: Ohne Verbindung beginnt der Ablauf neu. @param connected Test hat verbunden. @param ssid Bei einem Test aus den Einstellungen das getestete Netz, das bis restore() als vorläufig gilt. nullptr bei USB-Tests, deren Verbindung bestehen bleibt. */
    void tested(bool connected, const char *ssid);
    /** @brief Kehrt nach dem Verlassen der Einstellungen zu den gespeicherten Profilen zurück, wenn das zuletzt vorläufig verbundene Netz nicht unter ihnen ist. @param ssids Freigegebene Profilnetze, nullptr für leere oder gesperrte Plätze. */
    void restore(const char *const (&ssids)[MaxProfiles]);
    /** @brief Liefert den nächsten Schritt, ohne zu warten. @param now Monotone Zeit in Millisekunden. @param connections Zähler der gemeldeten IPv4-Verbindungen. Jede Änderung gilt als neue Verbindung. @param ssids Freigegebene Profilnetze, nullptr für leere oder gesperrte Plätze. */
    Step poll(uint32_t now, uint32_t connections, const char *const (&ssids)[MaxProfiles]);

    /** @brief Liefert den Index des Profils, das nach Step::Connect zu verbinden ist. */
    size_t profile() const
    {
        return m_profile;
    }

    /** @brief Meldet, ob der Ablauf noch eine Verbindung sucht. */
    bool active() const
    {
        return m_active;
    }

private:
    /** @brief Wählt ab m_next das nächste freigegebene Profil und beendet den Ablauf, wenn keines freigegeben ist. */
    Step next(uint32_t now, const char *const (&ssids)[MaxProfiles]);

    char m_temporary[33]{};   ///< Vorläufig verbundenes Testnetz aus den Einstellungen, leer ohne solches
    bool m_active = false;    ///< Ablauf sucht eine Verbindung
    bool m_attempt = false;   ///< Ein Versuch läuft seit m_since
    size_t m_next = 0;        ///< Nächster zu prüfender Profilplatz
    size_t m_profile = 0;     ///< Profil des laufenden Versuchs
    uint32_t m_since = 0;     ///< Beginn des laufenden Versuchs in Millisekunden
    uint32_t m_connections = 0; ///< Zuletzt ausgewerteter Verbindungszähler
};
#endif
