#include "Tab5Battery.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>

namespace
{
    constexpr uint16_t MinMillivolts = 5500, MaxMillivolts = 8700;         ///< Plausibler Bereich eines 2S-Akkus vom Tiefentladeschutz bis über den Ladeschluss
    constexpr uint16_t MaxSpread = 1500;                                   ///< Größere Schwankung im Fenster in mV gilt als Ladebaustein ohne Akku (dort rund 4000). Das Anstecken von USB hebt die Spannung eines hochohmigen Akkus um einige hundert Millivolt und bleibt darunter
    constexpr uint8_t MaxToggles = 2;                                      ///< Mehr Wechsel des Ladestatus im Fenster gelten als Ladebaustein ohne Akku
    constexpr uint8_t Confirm = 5;                                         ///< So viele abweichende Urteile in Folge ändern die Erkennung
    constexpr int16_t ExternalAbove = -20, BatteryBelow = -60;             ///< Stromgrenzen in mA: darüber Strom von außen, darunter Akkubetrieb, dazwischen bleibt der Stand
    constexpr float SmoothShare = 1.0f / 16.0f;                            ///< Anteil einer Messung an der geglätteten Ruhespannung
    constexpr int PercentStep = 5, PercentHold = 4;                        ///< Anzeige in Fünferschritten, ein neuer Schritt erst ab dieser Abweichung
    constexpr uint8_t ChargeCap = 95;                                      ///< Höchste Anzeige beim Laden, 100 gibt es erst mit dem Ladeschluss
    constexpr uint16_t MinCapacity = 800, MaxCapacity = 6000;              ///< Grenzen einer Akkugröße in mAh
    constexpr uint16_t MinResistance = 100, MaxResistance = 900;           ///< Grenzen des Innenwiderstands in mΩ
    constexpr float LearnBelow = 20.0f;                                    ///< Nur eine Ladung, die höchstens so voll beginnt, liefert die Akkugröße
    constexpr uint32_t LearnBatterySeconds = 60;                           ///< So lange muss der Akkubetrieb vor dem Anstecken beobachtet sein, damit der Startwert aus der Spannung stammt
    constexpr uint16_t FullMillivolts = 8100, FullConfirm = 10;            ///< Meldet der Ladestatus das Ende, gilt ab dieser Spannung nach so vielen Messungen der Ladeschluss. Das Ladeteil des Tab5 hört bei rund 8175 mV auf
    constexpr uint16_t IdleMillivolts = 8300, IdleConfirm = 300;           ///< Bleibt der Ladestatus an, gilt versiegter Strom ab dieser Spannung nach so vielen Messungen als Ladeschluss
    constexpr int16_t FullMilliamps = 100;                                 ///< Darunter ist der Ladestrom versiegt
    constexpr float FollowSlow = 1.0f / 900.0f, FollowFast = 1.0f / 60.0f; ///< Anteil je Messung, mit dem die Ruhespannung den Zähler im Akkubetrieb nachführt
    constexpr float FollowFastBelow = 15.0f;                               ///< Unter diesem Stand laut Spannung gilt der schnelle Anteil, damit die Warnung rechtzeitig kommt
    constexpr uint8_t SettleSamples = 4;                                   ///< Messungen nach einem Wechsel der Stromquelle bis zur Auswertung des Innenwiderstands
    constexpr float MinStepMilliamps = 400.0f;                             ///< Kleinster Stromsprung, aus dem sich der Innenwiderstand rechnen lässt

    /** @brief Stützpunkt der Ruhespannungskurve. */
    struct CurvePoint
    {
        uint16_t millivolts; ///< Spannung des Packs
        uint8_t percent;     ///< Ladestand bei dieser Spannung
    };

    /** @brief Übliche Ruhespannung zweier Lithium-Ionen-Zellen in Reihe, absteigend. */
    constexpr CurvePoint Curve[] = {{8400, 100}, {8200, 92}, {8000, 82}, {7800, 70}, {7600, 55}, {7500, 45}, {7400, 35}, {7300, 25}, {7200, 15}, {7000, 8}, {6800, 4}, {6600, 1}, {6400, 0}};

    /** @brief Schritt der Vorführung. */
    struct DemoStep
    {
        uint8_t percent, seconds; ///< Gezeigter Ladestand und Dauer in Messungen
        bool external;            ///< Strom von außen
    };

    constexpr DemoStep Demo[] = {{100, 4, false}, {80, 4, false}, {60, 4, false}, {40, 4, false}, {20, 4, false}, {5, 8, false}, {30, 4, true}, {100, 4, true}};
}

uint8_t Tab5Battery::percentFromMillivolts(uint16_t millivolts)
{
    if (millivolts >= Curve[0].millivolts)
        return 100;
    for (size_t i = 1; i < sizeof(Curve) / sizeof(Curve[0]); ++i)
        if (millivolts >= Curve[i].millivolts)
        {
            const auto &high = Curve[i - 1], &low = Curve[i];
            return static_cast<uint8_t>(low.percent + (static_cast<uint32_t>(millivolts - low.millivolts) * (high.percent - low.percent) + (high.millivolts - low.millivolts) / 2) / (high.millivolts - low.millivolts));
        }
    return 0;
}

void Tab5Battery::Monitor::feed(const Sample &sample)
{
    m_volts[m_index] = sample.millivolts;
    m_amps[m_index] = sample.milliamps;
    m_flags[m_index] = sample.charging;
    m_index = (m_index + 1) % Window;
    if (m_count < Window)
        ++m_count;
    // Die ältesten Messungen stehen ab der Schreibstelle, sobald das Fenster gefüllt ist.
    const int first = m_count == Window ? m_index : 0;
    uint16_t low = UINT16_MAX, high = 0;
    uint8_t toggles = 0;
    for (int i = 0; i < m_count; ++i)
    {
        const int at = (first + i) % Window;
        low = std::min(low, m_volts[at]);
        high = std::max(high, m_volts[at]);
        if (i && m_flags[at] != m_flags[(first + i - 1) % Window])
            ++toggles;
    }
    const uint16_t spread = high - low;
    const bool candidate = m_count >= Confirm && sample.millivolts >= MinMillivolts && sample.millivolts <= MaxMillivolts && spread <= MaxSpread && toggles <= MaxToggles;
    if (candidate == m_present)
        m_streak = 0;
    else if (++m_streak >= Confirm)
    {
        m_present = candidate;
        m_streak = 0;
        forget();
    }
    if (m_present)
    {
        const bool before = m_external;
        if (sample.milliamps >= ExternalAbove)
            m_external = true;
        else if (sample.milliamps <= BatteryBelow)
            m_external = false;
        track(sample, m_haveLevel && before != m_external);
    }
    m_status.measured = m_present;
    m_status.millivolts = sample.millivolts;
    m_status.smoothed = static_cast<uint16_t>(std::max(0L, lroundf(m_smooth)));
    m_status.milliamps = sample.milliamps;
    m_status.charging = sample.charging;
    m_status.toggles = toggles;
    m_status.spread = spread;
    m_status.level10 = m_present ? static_cast<uint16_t>(lroundf(m_level * 10.0f)) : 0;
    m_status.chargedMah = static_cast<uint16_t>(std::min(65535L, lroundf(m_chargedMah)));
    m_status.full = m_full;
    m_status.learning = m_learning;
    m_status.calibration = m_calibration;
    if (m_demo)
    {
        if (++m_demoTicks >= Demo[m_demoStep].seconds)
        {
            m_demoTicks = 0;
            m_demoStep = (m_demoStep + 1) % (sizeof(Demo) / sizeof(Demo[0]));
        }
        applyDemo();
    }
    else if (!m_status.simulated)
    {
        m_status.present = m_present;
        m_status.external = m_external;
        m_status.percent = m_present ? m_percent : 0;
        m_status.low = m_present && m_level < LowPercent;
    }
}

void Tab5Battery::Monitor::track(const Sample &sample, bool flipped)
{
    const float amps = sample.milliamps;
    // Die letzte Messung steht eine Stelle vor der Schreibstelle.
    auto mean = [this](int newest, int count, float &volts, float &current)
    {
        volts = current = 0.0f;
        for (int i = 0; i < count; ++i)
        {
            const int at = (m_index + 2 * Window - 1 - newest - i) % Window;
            volts += m_volts[at];
            current += m_amps[at];
        }
        volts /= count;
        current /= count;
    };
    // Innenwiderstand: Spannungssprung durch Stromsprung beim An- oder Abstecken, gemessen vor dem Wechsel und nach dem Einschwingen.
    if (flipped)
    {
        m_settle = 0;
        if (m_count > SettleSamples)
        {
            mean(1, SettleSamples, m_beforeVolts, m_beforeAmps);
            m_settle = SettleSamples;
        }
    }
    else if (m_settle && --m_settle == 0)
    {
        float volts, current;
        mean(0, 2, volts, current);
        const float step = current - m_beforeAmps;
        if (std::fabs(step) >= MinStepMilliamps)
        {
            const float resistance = (volts - m_beforeVolts) / step * 1000.0f;
            if (resistance >= MinResistance && resistance <= MaxResistance)
                m_calibration.resistanceMohm = static_cast<uint16_t>(lroundf((m_calibration.resistanceMohm + resistance) / 2.0f));
        }
    }
    // Ruhespannung: Der Strom hebt die Spannung beim Laden und senkt sie beim Entladen um Strom mal Innenwiderstand.
    const float rest = sample.millivolts - amps * m_calibration.resistanceMohm / 1000.0f;
    m_smooth = m_smooth <= 0.0f ? rest : m_smooth + (rest - m_smooth) * SmoothShare;
    const float byVoltage = percentFromMillivolts(static_cast<uint16_t>(std::min(65535L, std::max(0L, lroundf(m_smooth)))));
    if (!m_haveLevel)
    {
        m_level = byVoltage;
        m_haveLevel = true;
    }
    if (flipped)
    {
        // Angesteckt beginnt eine neue Ladung, abgezogen endet sie ohne Ergebnis.
        m_startLevel = m_level;
        m_chargedMah = 0.0f;
        m_learning = m_external && m_batterySeconds >= LearnBatterySeconds && m_level <= LearnBelow;
        m_full = false;
        m_fullStreak = m_idleStreak = 0;
        m_batterySeconds = 0;
    }
    // Zähler: mA mal 1 s, bezogen auf mAh mal 3600 s, in Prozent.
    m_level += amps / (m_calibration.capacityMah * 36.0f);
    if (!m_external)
    {
        ++m_batterySeconds;
        m_level += (byVoltage - m_level) * (byVoltage < FollowFastBelow ? FollowFast : FollowSlow);
    }
    else
    {
        if (amps > 0.0f)
            m_chargedMah += amps / 3600.0f;
        const bool idle = sample.milliamps < FullMilliamps;
        m_fullStreak = idle && !sample.charging && sample.millivolts >= FullMillivolts ? m_fullStreak + 1 : 0;
        m_idleStreak = idle && sample.millivolts >= IdleMillivolts ? m_idleStreak + 1 : 0;
        if (!m_full && (m_fullStreak >= FullConfirm || m_idleStreak >= IdleConfirm))
        {
            m_full = true;
            if (m_learning)
            {
                const float capacity = m_chargedMah / (1.0f - m_startLevel / 100.0f);
                if (capacity >= MinCapacity && capacity <= MaxCapacity)
                {
                    m_calibration.capacityMah = static_cast<uint16_t>(lroundf(capacity));
                    m_calibration.learned = true;
                    m_changed = true;
                }
            }
            m_learning = false;
        }
        m_level = m_full ? 100.0f : std::min(m_level, 99.0f);
    }
    m_level = std::min(100.0f, std::max(0.0f, m_level));
    const int target = static_cast<int>(lroundf(m_level));
    if (!m_havePercent || std::abs(target - m_percent) >= PercentHold)
    {
        m_percent = static_cast<uint8_t>((target + PercentStep / 2) / PercentStep * PercentStep);
        m_havePercent = true;
    }
    if (m_full)
        m_percent = 100;
    else if (m_external)
        m_percent = std::min(m_percent, ChargeCap);
}

void Tab5Battery::Monitor::forget()
{
    m_external = m_haveLevel = m_havePercent = m_full = m_learning = false;
    m_smooth = m_level = m_chargedMah = m_startLevel = 0.0f;
    m_percent = 0;
    m_fullStreak = m_idleStreak = 0;
    m_batterySeconds = 0;
    m_settle = 0;
}

void Tab5Battery::Monitor::simulate(int percent, bool external)
{
    m_demo = false;
    m_status.simulated = percent >= 0;
    if (percent < 0)
    {
        m_status.present = m_present;
        m_status.external = m_external;
        m_status.percent = m_present ? m_percent : 0;
        m_status.low = m_present && m_level < LowPercent;
        return;
    }
    m_status.present = true;
    m_status.external = external;
    m_status.percent = static_cast<uint8_t>(std::min(percent, 100));
    m_status.low = m_status.percent < LowPercent;
}

void Tab5Battery::Monitor::demo()
{
    m_demo = true;
    m_demoStep = m_demoTicks = 0;
    applyDemo();
}

void Tab5Battery::Monitor::applyDemo()
{
    m_status.simulated = m_status.present = true;
    m_status.percent = Demo[m_demoStep].percent;
    m_status.external = Demo[m_demoStep].external;
    m_status.low = m_status.percent < LowPercent;
}

void Tab5Battery::Monitor::setCalibration(const Calibration &calibration)
{
    m_calibration = calibration;
    if (m_calibration.capacityMah < MinCapacity || m_calibration.capacityMah > MaxCapacity)
        m_calibration = Calibration{};
    if (m_calibration.resistanceMohm < MinResistance || m_calibration.resistanceMohm > MaxResistance)
        m_calibration.resistanceMohm = Calibration{}.resistanceMohm;
    m_status.calibration = m_calibration;
}

bool Tab5Battery::Monitor::takeCalibration(Calibration &calibration)
{
    if (!m_changed)
        return false;
    m_changed = false;
    calibration = m_calibration;
    return true;
}
