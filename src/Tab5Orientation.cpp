#include "Tab5Orientation.h"
#include "Logger.h"
#include <cstdio>
#include <cstdlib>

namespace
{
    constexpr int16_t OneG = 4096;               ///< BMI270 nach der Konfiguration im Bereich ±8 g
    constexpr int16_t Threshold = OneG / 2;      ///< Mindestanteil der Schwerkraft entlang der senkrechten Bildachse: mindestens 30 Grad aus der Waagerechten aufgerichtet
    constexpr uint32_t ReadMs = 100;             ///< Abfrageabstand
    constexpr uint32_t StableMs = 1000;          ///< So lange muss eine neue Lage anliegen, bevor sie gilt
    constexpr int VerticalAxis = 0;              ///< Sensorachse entlang der senkrechten Bildachse (0 = x, 1 = y). Gemessen aufrecht auf dem Fuß: x = 3441, y = 80, z = -2221
    constexpr bool NormalPositive = true;        ///< Vorzeichen dieser Achse in Rotation 3 (normal)
    constexpr uint32_t ResetSettleMs = 5;        ///< Wartezeit nach dem eigenen Soft-Reset vor dem zweiten Start, der BMI270 braucht nach dem Reset mindestens 2 ms

    /** @brief Liest ein BMI270-Register für die Diagnosezeile. @param address I2C-Adresse. @param frequency Bustakt in Hz. @param reg Register. @param text Ziel für zwei Hexziffern oder „--“ ohne Antwort. */
    void diagnosticRegister(uint8_t address, uint32_t frequency, uint8_t reg, char (&text)[3])
    {
        uint8_t value = 0;
        if (m5::In_I2C.readRegister(address, reg, &value, 1, frequency))
            snprintf(text, sizeof(text), "%02X", value);
        else
            snprintf(text, sizeof(text), "--");
    }
}

bool Tab5Orientation::begin()
{
    m_ready = m_imu.begin(&m5::In_I2C) != m5::IMU_Base::imu_spec_none;
    if (m_ready)
        return true;
    // Der Sensor fehlte schon über mehrere Warmstarts trotz Soft-Reset in begin() (Ursache offen). Die Logzeile hält Chip-ID (24 erwartet), Status und Fehlerregister des gescheiterten Starts fest.
    char chip[3], status[3], error[3];
    diagnosticRegister(Address, Frequency, m5::BMI270_Class::CHIP_ID_ADDR, chip);
    diagnosticRegister(Address, Frequency, m5::BMI270_Class::INTERNAL_STATUS_ADDR, status);
    diagnosticRegister(Address, Frequency, m5::BMI270_Class::ERR_REG_ADDR, error);
    LOG_WARNING_F("LAGESENSOR erster Start gescheitert: Chip=%s Status=%s Fehler=%s, zweiter Versuch", chip, status, error);
    // Genau ein zweiter Versuch nach eigenem Soft-Reset mit ausreichender Wartezeit, begin() selbst wartet nach seinem Reset nur einen Systemtakt.
    m5::In_I2C.writeRegister8(Address, m5::BMI270_Class::CMD_REG_ADDR, m5::BMI270_Class::SOFT_RESET_CMD, Frequency);
    delay(ResetSettleMs);
    m_ready = m_imu.begin(&m5::In_I2C) != m5::IMU_Base::imu_spec_none;
    return m_ready;
}

uint8_t Tab5Orientation::update(uint32_t now)
{
    if (!m_ready)
        return 0;
    if (now - m_lastRead < ReadMs)
        return m_stable;
    m_lastRead = now;
    m5::IMU_Base::imu_raw_data_t data;
    if (m_imu.getImuRawData(&data) & m5::IMU_Base::imu_spec_accel)
    {
        m_x = data.accel.x;
        m_y = data.accel.y;
        m_z = data.accel.z;
    }
    const int vertical = VerticalAxis == 0 ? m_x : m_y, other = VerticalAxis == 0 ? m_y : m_x;
    // Steht das Tab5 deutlich auf einer Längskante, auch zurückgelehnt auf dem Fuß, entscheidet das Vorzeichen. Liegend oder hochkant bleibt die Lage offen.
    uint8_t candidate = 0;
    if (std::abs(vertical) >= Threshold && std::abs(vertical) > std::abs(other))
        candidate = (vertical > 0) == NormalPositive ? 3 : 1;
    if (candidate != m_candidate)
    {
        m_candidate = candidate;
        m_candidateSince = now;
    }
    if (now - m_candidateSince >= StableMs)
        m_stable = m_candidate;
    return m_stable;
}
