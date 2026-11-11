#ifndef TAB5_BATTERY_H
#define TAB5_BATTERY_H

#include <cstdint>

/** @brief Auswertung der Akkumessung des Tab5: Erkennung, Ladestand und Stromquelle aus Spannung, Strom und Ladestatus. Reine Rechenlogik ohne Hardwarezugriff. Die Messwerte liest der UI-Task über M5Unified (INA226 an 0x41, Ladestatus an E2.P6). */
namespace Tab5Battery
{
    constexpr uint8_t LowPercent = 10; ///< Unter diesem Ladestand gilt der Akku als fast leer, das rote Segment blinkt

    /** @brief Rohwerte einer Messung. */
    struct Sample
    {
        uint16_t millivolts = 0; ///< Spannung der Akkuleitung
        int16_t milliamps = 0;   ///< Strom, positiv beim Laden, negativ beim Entladen
        bool charging = false;   ///< Ladestatus des IP2326 (CHG_STAT)
    };

    /** @brief Kennwerte des angeschlossenen Akkus. Die Vorgaben gelten, bis das Gerät sie an einer Ladung selbst gemessen hat. */
    struct Calibration
    {
        uint16_t capacityMah = 2000;   ///< Nutzbare Ladung, Vorgabe für einen NP-F550
        uint16_t resistanceMohm = 350; ///< Innenwiderstand samt Schutzschaltung und Leitung
        bool learned = false;          ///< capacityMah stammt aus einer vollständigen Ladung statt aus der Vorgabe

        /** @brief Vergleicht alle Felder. */
        bool operator==(const Calibration &other) const
        {
            return capacityMah == other.capacityMah && resistanceMohm == other.resistanceMohm && learned == other.learned;
        }
    };

    /** @brief Ergebnis der Auswertung. Die ersten fünf Felder bestimmen die Anzeige, der Rest dient Diagnose und Info-Seite. */
    struct Status
    {
        bool present = false;     ///< Akku erkannt
        bool external = false;    ///< Strom von außen
        bool simulated = false;   ///< Anzeige stammt aus simulate() oder demo() statt aus der Messung
        uint8_t percent = 0;      ///< Ladestand 0..100 in Schritten von 5
        bool low = false;         ///< Fast leer nach dem ungerundeten Stand, die Anzeigestufe hinkt wegen ihrer Haltegrenze bis zu drei Punkte nach
        bool measured = false;    ///< Erkennung allein aus der Messung, auch während einer Simulation
        uint16_t millivolts = 0;  ///< Letzte Spannung
        uint16_t smoothed = 0;    ///< Geglättete Ruhespannung (Spannung abzüglich Strom mal Innenwiderstand), 0 ohne erkannten Akku
        int16_t milliamps = 0;    ///< Letzter Strom
        bool charging = false;    ///< Letzter Ladestatus
        uint8_t toggles = 0;      ///< Wechsel des Ladestatus im Beobachtungsfenster
        uint16_t spread = 0;      ///< Spannungsschwankung im Beobachtungsfenster in mV
        uint16_t level10 = 0;     ///< Mitgezählter Ladestand in Zehntelprozent, ungerundet
        uint16_t chargedMah = 0;  ///< Seit dem Anstecken geladene Ladung
        bool full = false;        ///< Ladeschluss erkannt
        bool learning = false;    ///< Die laufende Ladung begann fast leer im Akkubetrieb und kann die Akkugröße liefern
        Calibration calibration;  ///< Geltende Kennwerte des Akkus
    };

    /** @brief Ladestand aus der Ruhespannung eines 2S-Lithium-Akkus. @param millivolts Spannung des Packs ohne Stromfluss. @return 0..100, linear zwischen den Stützpunkten einer Ruhespannungskurve. */
    uint8_t percentFromMillivolts(uint16_t millivolts);

    /**
     * @brief Beobachtet die Messwerte über ein Fenster und leitet daraus den Anzeigezustand ab. Alle Aufrufe aus demselben Task.
     *
     * Der Ladestand wird mitgezählt (Strom mal Zeit, bezogen auf die Akkugröße). Im Akkubetrieb führt ihn die Ruhespannung langsam nach, fast leer schnell. Beim Laden zählt allein der Strom, weil die Spannung dann nichts über den Füllstand sagt. Der Ladeschluss setzt ihn auf 100 %. Eine Ladung von höchstens 20 % bis zum Ladeschluss liefert die Akkugröße, jeder Wechsel der Stromquelle den Innenwiderstand.
     */
    class Monitor
    {
    public:
        /** @brief Nimmt eine Messung auf. Fenster, Zähler, Glättung und Vorführung rechnen mit einem Aufruf je Sekunde. */
        void feed(const Sample &sample);
        /** @brief Ersetzt die Anzeige durch feste Werte, damit sich die Ansichten an einem Gerät ohne Akku prüfen lassen. @param percent 0..100, ein negativer Wert beendet Simulation und Vorführung. @param external Strom von außen. */
        void simulate(int percent, bool external);
        /** @brief Startet die Vorführung: Die Anzeige durchläuft im Abstand weniger Sekunden alle Zustände von voll bis blinkend und danach das Laden, bis simulate() sie beendet. */
        void demo();
        /** @brief Übernimmt gespeicherte Kennwerte des Akkus, gedacht für den Start. Werte außerhalb der Grenzen fallen auf die Vorgabe zurück. */
        void setCalibration(const Calibration &calibration);
        /** @brief Meldet genau einmal, dass eine Ladung neue Kennwerte geliefert hat, die der Aufrufer speichern soll. @param calibration Erhält die neuen Kennwerte. @return true, wenn es neue gibt. */
        bool takeCalibration(Calibration &calibration);

        /** @brief Liefert den aktuellen Stand. */
        const Status &status() const
        {
            return m_status;
        }

    private:
        static constexpr int Window = 16; ///< Beobachtungsfenster in Messungen

        Status m_status{};
        Calibration m_calibration{};                                ///< Geltende Kennwerte
        uint16_t m_volts[Window]{};                                 ///< Ringpuffer der Spannungen
        int16_t m_amps[Window]{};                                   ///< Ringpuffer der Ströme
        bool m_flags[Window]{};                                     ///< Ringpuffer des Ladestatus
        uint8_t m_index = 0, m_count = 0, m_streak = 0;             ///< Schreibstelle, Füllstand, abweichende Urteile in Folge
        bool m_present = false, m_external = false, m_demo = false; ///< Gemessene Erkennung und Stromquelle, Vorführung läuft
        float m_smooth = 0.0f;                                      ///< Geglättete Ruhespannung in mV, 0 ohne Akku
        float m_level = 0.0f;                                       ///< Mitgezählter Ladestand in Prozent
        bool m_haveLevel = false;                                   ///< m_level ist seit dem Erkennen gesetzt
        uint8_t m_percent = 0;                                      ///< Angezeigter Ladestand aus der Messung
        bool m_havePercent = false;                                 ///< m_percent ist seit dem Erkennen gesetzt
        bool m_full = false, m_learning = false, m_changed = false; ///< Ladeschluss erkannt, Ladung taugt zum Lernen der Akkugröße, neue Kennwerte liegen zum Speichern bereit
        uint16_t m_fullStreak = 0, m_idleStreak = 0;                ///< Messungen in Folge mit Ladeschluss laut Ladestatus und mit versiegtem Ladestrom
        uint32_t m_batterySeconds = 0;                              ///< Messungen im Akkubetrieb seit dem Erkennen, als Beleg für einen brauchbaren Startwert
        float m_chargedMah = 0.0f, m_startLevel = 0.0f;             ///< Ladung seit dem Anstecken und Ladestand in diesem Moment
        uint8_t m_settle = 0;                                       ///< Messungen bis zur Auswertung des Innenwiderstands nach einem Wechsel der Stromquelle, 0 = keine offen
        float m_beforeVolts = 0.0f, m_beforeAmps = 0.0f;            ///< Mittel von Spannung und Strom vor dem Wechsel der Stromquelle
        uint8_t m_demoStep = 0, m_demoTicks = 0;                    ///< Schritt der Vorführung und Messungen in diesem Schritt

        /** @brief Überträgt den Schritt der Vorführung in die Anzeige. */
        void applyDemo();
        /** @brief Führt Zähler, Ladeschluss, Lernen und Anzeigestufe für eine Messung mit erkanntem Akku fort. @param flipped Die Stromquelle hat mit dieser Messung gewechselt. */
        void track(const Sample &sample, bool flipped);
        /** @brief Setzt alles zurück, was am erkannten Akku hängt. */
        void forget();
    };
}

#endif
