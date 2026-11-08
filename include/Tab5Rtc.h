#ifndef TAB5_RTC_H
#define TAB5_RTC_H
#include "RadioPreferences.h"
#include <ctime>
#include <stdint.h>

/** @brief Bindet die vorhandene M5-RTC an UTC-Systemzeit und vier geprüfte RAM-Bytes an, I/O nur bei Dienststart und im Dienstworker. */
class Tab5Rtc
{
public:
    /** @brief Befund der RTC für die Diagnose. */
    struct Status
    {
        bool available = false, timeValid = false, restored = false; ///< RTC erreichbar, Zeit gültig, Systemzeit aus der RTC gesetzt
        uint8_t flags = 0;                                          ///< Ereignisflags des RX8130
        uint32_t ramWrites = 0, timeWrites = 0;                     ///< Bestätigte Schreibvorgänge in RTC-RAM und Uhrzeit
        time_t utc = 0;                                             ///< Zuletzt gelesene RTC-Zeit
    };

    /** @brief Initialisiert den vorhandenen RX8130-Treiber und verwirft RAM nach belegtem Datenverlust. @return RTC erreichbar und initialisiert. */
    bool begin();
    /** @brief Übernimmt ausschließlich plausible, nicht als verloren markierte UTC-Zeit. @return Systemzeit gesetzt. */
    bool restoreSystemTime();
    /** @brief Schreibt die gültige Systemzeit als UTC und bestätigt Register und VLF. @return RTC-Zeit bestätigt. */
    bool syncFromSystem();
    /** @brief Überlagert häufige Radiowerte, erhält Balance/EQ aus NVS. Ohne erreichbare RTC oder gültigen RAM bleiben die NVS-Ausgangswerte, Pegel und Rückkehrpegel aber höchstens quietVolume, weil der NVS-Pegel veraltet sein kann. @param value NVS-Ausgangswerte. @param quietVolume Leiser Startpegel 1..100. @return RAM gültig. */
    bool loadRadio(RadioPreferences &value, uint8_t quietVolume);
    /** @brief Schreibt nur veränderte RAM-Daten mit ungültigem Sender als Unterbrechungsmarker. @param value Gemeinsamer Radiozustand. @return Daten unverändert oder Rücklesung bestätigt. */
    bool saveRadio(const RadioPreferences &value);

    /** @brief Kopiert den zuletzt im Dienst ermittelten Befund ohne I2C. @return Letzte Befunde und bestätigte Schreibzähler. */
    Status status() const
    {
        return m_status;
    }

    /** @brief Kodiert Sender/Stumm, Pegel/Rückkehrwert, acht Stile sowie Modus/CRC5 in genau vier Bytes. @param value Zu kodierender Zustand. @param bytes Vier Zielbytes, bei ungültigen Werten unverändert. @return Werte kodierbar. */
    static bool encodeRadio(const RadioPreferences &value, uint8_t (&bytes)[4]);
    /** @brief Prüft alle vier Bytes vor Übernahme, übrige Felder bleiben erhalten. @param bytes Vier RTC-RAM-Bytes. @param value Ausgangszustand und Ziel der schnellen Felder. @return Daten gültig. */
    static bool decodeRadio(const uint8_t (&bytes)[4], RadioPreferences &value);
    /** @brief Wandelt einen geprüften UTC-Kalender 2000..2099 ohne Änderung der globalen Zeitzone um. @param utc Kalender in UTC. @param epoch Ziel in Sekunden seit 1970, bei ungültigem Kalender unverändert. @return Kalender gültig. */
    static bool utcEpoch(const tm &utc, time_t &epoch);

private:
    Status m_status;
    bool m_ramLost = true, m_haveRadio = false;
    bool m_clockStopped = false;
    uint8_t m_radio[4]{};
    /** @brief Liest die RTC-Uhrzeit als UTC, solange weder Verlust- noch Stoppflag gesetzt sind. Setzt dabei Status::timeValid. @param epoch Ziel in Sekunden seit 1970. @return Zeit gelesen und gültig. */
    bool readTime(time_t &epoch);
};
#endif
