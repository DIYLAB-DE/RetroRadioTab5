#ifndef TAB5_ORIENTATION_H
#define TAB5_ORIENTATION_H
#include <M5Unified.h>
#include <utility/imu/BMI270_Class.hpp>

/** @brief Lage des Tab5 aus dem eingebauten BMI270 (I2C 0x68) für die automatische Drehung des Querformats. Nur aus dem UI-Task aufrufen, der den internen Bus auch für Touch und Codec nutzt. */
class Tab5Orientation
{
public:
    /** @brief Startet ausschließlich den BMI270 an 0x68. Lädt dabei dessen Konfigurationsdatei (rund 8 KB, etwa 190 ms). Scheitert der Start, folgen eine Diagnosezeile im Log und genau ein zweiter Versuch nach eigenem Soft-Reset. @return false ohne startbaren BMI270. */
    bool begin();
    /** @brief Liest höchstens alle 100 ms die Beschleunigung und meldet eine seit einer Sekunde stabile Lage. @param now Millisekunden. @return 3 (normal), 1 (um 180 Grad gedreht) oder 0 (liegend, unklar oder kein Sensor). */
    uint8_t update(uint32_t now);

    /** @brief Liefert die zuletzt gelesenen Rohwerte (4096 je g) für die Diagnose. */
    void raw(int16_t &x, int16_t &y, int16_t &z) const
    {
        x = m_x;
        y = m_y;
        z = m_z;
    }

    /** @brief Meldet einen gestarteten Sensor. */
    bool ready() const
    {
        return m_ready;
    }

private:
    static constexpr uint8_t Address = 0x68;        ///< I2C-Adresse des BMI270 am internen Bus
    static constexpr uint32_t Frequency = 400000;   ///< Bustakt für den BMI270
    m5::BMI270_Class m_imu{Address, Frequency, &m5::In_I2C};
    bool m_ready = false;
    int16_t m_x = 0, m_y = 0, m_z = 0;
    uint8_t m_candidate = 0, m_stable = 0;
    uint32_t m_lastRead = 0, m_candidateSince = 0;
};
#endif
