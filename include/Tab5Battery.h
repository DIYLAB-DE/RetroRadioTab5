#ifndef TAB5_BATTERY_H
#define TAB5_BATTERY_H

#include <cstdint>

/** @brief Auswertung der Akkumessung des Tab5: Erkennung, Ladestand und Stromquelle aus Spannung, Strom und Ladestatus. Reine Rechenlogik ohne Hardwarezugriff. Die Messwerte liest der UI-Task über M5Unified (INA226 an 0x41, Ladestatus an E2.P6). */
namespace Tab5Battery
{
    /** @brief Rohwerte einer Messung. */
    struct Sample
    {
        uint16_t millivolts = 0; ///< Spannung der Akkuleitung
        int16_t milliamps = 0;   ///< Strom, positiv beim Laden, negativ beim Entladen
        bool charging = false;   ///< Ladestatus des IP2326 (CHG_STAT)
    };

    /** @brief Ergebnis der Auswertung. Die ersten vier Felder bestimmen die Anzeige, der Rest dient der Diagnose. */
    struct Status
    {
        bool present = false;    ///< Akku erkannt
        bool external = false;   ///< Strom von außen
        bool simulated = false;  ///< Anzeige stammt aus simulate() oder demo() statt aus der Messung
        uint8_t percent = 0;     ///< Ladestand 0..100 in Schritten von 5
        bool measured = false;   ///< Erkennung allein aus der Messung, auch während einer Simulation
        uint16_t millivolts = 0; ///< Letzte Spannung
        uint16_t smoothed = 0;   ///< Geglättete Spannung, 0 ohne erkannten Akku
        int16_t milliamps = 0;   ///< Letzter Strom
        bool charging = false;   ///< Letzter Ladestatus
        uint8_t toggles = 0;     ///< Wechsel des Ladestatus im Beobachtungsfenster
        uint16_t spread = 0;     ///< Spannungsschwankung im Beobachtungsfenster in mV
    };

    /** @brief Ladestand aus der Spannung eines 2S-Lithium-Akkus. @param millivolts Spannung des Packs. @return 0..100, linear zwischen den Stützpunkten einer Ruhespannungskurve. Unter Last liest der Wert etwas zu tief, beim Laden zu hoch. */
    uint8_t percentFromMillivolts(uint16_t millivolts);

    /** @brief Beobachtet die Messwerte über ein Fenster und leitet daraus den Anzeigezustand ab. Alle Aufrufe aus demselben Task. */
    class Monitor
    {
    public:
        /** @brief Nimmt eine Messung auf. Fenster, Glättung und Vorführung rechnen mit einem Aufruf je Sekunde. */
        void feed(const Sample &sample);
        /** @brief Ersetzt die Anzeige durch feste Werte, damit sich die Ansichten an einem Gerät ohne Akku prüfen lassen. @param percent 0..100, ein negativer Wert beendet Simulation und Vorführung. @param external Strom von außen. */
        void simulate(int percent, bool external);
        /** @brief Startet die Vorführung: Die Anzeige durchläuft im Abstand weniger Sekunden alle Zustände von voll bis blinkend und danach das Laden, bis simulate() sie beendet. */
        void demo();

        /** @brief Liefert den aktuellen Stand. */
        const Status &status() const
        {
            return m_status;
        }

    private:
        static constexpr int Window = 16; ///< Beobachtungsfenster in Messungen

        Status m_status{};
        uint16_t m_volts[Window]{};  ///< Ringpuffer der Spannungen
        bool m_flags[Window]{};      ///< Ringpuffer des Ladestatus
        uint8_t m_index = 0, m_count = 0, m_streak = 0; ///< Schreibstelle, Füllstand, abweichende Urteile in Folge
        bool m_present = false, m_external = false, m_demo = false; ///< Gemessene Erkennung und Stromquelle, Vorführung läuft
        float m_smooth = 0.0f;       ///< Geglättete Spannung in mV, 0 ohne Akku
        uint8_t m_percent = 0;       ///< Angezeigter Ladestand aus der Messung
        bool m_havePercent = false;  ///< m_percent ist seit dem Erkennen gesetzt
        uint8_t m_demoStep = 0, m_demoTicks = 0; ///< Schritt der Vorführung und Messungen in diesem Schritt

        /** @brief Überträgt den Schritt der Vorführung in die Anzeige. */
        void applyDemo();
    };
}

#endif
