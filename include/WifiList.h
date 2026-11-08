#ifndef WIFI_LIST_H
#define WIFI_LIST_H
#include "MessageBox.h"

/** @brief Begrenzte Netzauswahl aus zentral gelieferten Scanergebnissen. */
class WifiList
{
public:
    static constexpr size_t MaxNetworks = 24, PageSize = 5;

    /** @brief Ein gefundenes Netz. */
    struct Network
    {
        char ssid[33] = {};    ///< Name, höchstens 32 Bytes
        int16_t rssi = 0;      ///< Empfangsstärke in dBm
        bool encrypted = true; ///< Verschlüsseltes Netz
    };
    /** @brief Zustand der Suche. */
    enum class State : uint8_t
    {
        Scanning, ///< Suche läuft
        Ready,    ///< Ergebnis liegt vor
        Failed    ///< Suche fehlgeschlagen, Text in m_error
    };
    /** @brief Ergebnis einer Bedienung. */
    enum class Action : uint8_t
    {
        None,     ///< Nichts zu tun
        Selected, ///< Netz gewählt, siehe Result::index
        Cancel,   ///< Abbruch
        Rescan,   ///< Neue Suche gewünscht
        Manual    ///< Manuelle Eingabe gewünscht
    };

    /** @brief Rückgabe von touch(). */
    struct Result
    {
        Action action = Action::None; ///< Gewünschte Aktion
        int index = -1;               ///< Index des gewählten Netzes bei Selected, sonst -1
    };

    /** @brief Öffnet eine leere Scanansicht. @param language Sprache. */
    void open(LangIndex language);
    /** @brief Kopiert und sortiert ein vollständiges begrenztes Ergebnis. @param networks Ergebnisse. @param count Anzahl bis MaxNetworks. @param more Weitere Ergebnisse verworfen. @return false bei ungültigen Daten. */
    bool setNetworks(const Network *networks, size_t count, bool more);

    /** @brief Zeigt einen gemeldeten Scanfehler. @param key Fehlertext. */
    void fail(SettingsKey key)
    {
        m_state = State::Failed;
        m_error = key;
        m_count = 0;
        m_contact.cancel();
    }

    /** @brief Verarbeitet einen Kontakt. @param phase Kontaktphase. @param x Physische X-Koordinate. @param y Physische Y-Koordinate. @return Auswahl oder Aktion beim Loslassen auf einem Ziel, sonst None. */
    Result touch(SettingsUi::TouchPhase phase, int x, int y);

    /** @brief Liest einen kopierten Netzeintrag. @param index Index. @return Gültiger Eintrag oder nullptr. */
    const Network *network(size_t index) const
    {
        return index < m_count ? &m_networks[index] : nullptr;
    }

    /** @brief Bricht den Kontakt ab. */
    void cancelTouch()
    {
        m_contact.cancel();
    }

    /** @brief Liefert das gedrückte Ziel für gezielte Neuzeichnung. @return Ziel oder -1. */
    int pressed() const
    {
        return m_contact.pressed();
    }

    /** @brief Meldet eine noch laufende Suche. @return Suchstatus. */
    bool scanning() const
    {
        return m_state == State::Scanning;
    }

private:
    Network m_networks[MaxNetworks] = {}; ///< Kopierte Ergebnisse, nach Empfangsstärke sortiert
    size_t m_count = 0, m_page = 0;       ///< Anzahl der Netze, gezeigte Seite
    bool m_more = false;                  ///< Weitere Ergebnisse wurden verworfen
    State m_state = State::Scanning;
    SettingsKey m_error = SettingsKey::ServiceError;
    LangIndex m_language = LangIndex::DE;
    SettingsUi::Contact m_contact;
};
#endif
