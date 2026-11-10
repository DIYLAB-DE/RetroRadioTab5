#include "Tab5Battery.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>

namespace
{
    constexpr uint16_t MinMillivolts = 5500, MaxMillivolts = 8700; ///< Plausibler Bereich eines 2S-Akkus vom Tiefentladeschutz bis über den Ladeschluss
    constexpr uint16_t MaxSpread = 500;                            ///< Größere Schwankung im Fenster in mV gilt als Ladebaustein ohne Akku
    constexpr uint8_t MaxToggles = 2;                              ///< Mehr Wechsel des Ladestatus im Fenster gelten als Ladebaustein ohne Akku
    constexpr uint8_t Confirm = 5;                                 ///< So viele abweichende Urteile in Folge ändern die Erkennung
    constexpr int16_t ExternalAbove = -20, BatteryBelow = -60;     ///< Stromgrenzen in mA: darüber Strom von außen, darunter Akkubetrieb, dazwischen bleibt der Stand
    constexpr float SmoothShare = 1.0f / 16.0f;                    ///< Anteil einer Messung an der geglätteten Spannung
    constexpr int PercentStep = 5, PercentHold = 4;                ///< Anzeige in Fünferschritten, ein neuer Schritt erst ab dieser Abweichung

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
        m_smooth = 0.0f;
        m_havePercent = false;
    }
    if (m_present)
    {
        if (sample.milliamps >= ExternalAbove)
            m_external = true;
        else if (sample.milliamps <= BatteryBelow)
            m_external = false;
        m_smooth = m_smooth <= 0.0f ? sample.millivolts : m_smooth + (sample.millivolts - m_smooth) * SmoothShare;
        const int percent = percentFromMillivolts(static_cast<uint16_t>(lroundf(m_smooth)));
        if (!m_havePercent || std::abs(percent - m_percent) >= PercentHold)
        {
            m_percent = static_cast<uint8_t>((percent + PercentStep / 2) / PercentStep * PercentStep);
            m_havePercent = true;
        }
    }
    else
        m_external = false;
    m_status.measured = m_present;
    m_status.millivolts = sample.millivolts;
    m_status.smoothed = static_cast<uint16_t>(lroundf(m_smooth));
    m_status.milliamps = sample.milliamps;
    m_status.charging = sample.charging;
    m_status.toggles = toggles;
    m_status.spread = spread;
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
    }
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
        return;
    }
    m_status.present = true;
    m_status.external = external;
    m_status.percent = static_cast<uint8_t>(std::min(percent, 100));
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
}
