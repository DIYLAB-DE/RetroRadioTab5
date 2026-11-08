#include "Tab5Rtc.h"
#include <M5Unified.h>
#include <cstring>
#include <sys/time.h>

namespace
{
    constexpr uint8_t Address = 0x32, Flags = 0x1d, Control = 0x1e, Backup = 0x1f, Ram = 0x20, Offset = 0x30;
    constexpr uint8_t Lost = 0x02, Stopped = 0x40, InvalidStation = 0xff;
    constexpr uint32_t Frequency = 400000;

    /** @brief CRC5 über 27 Datenbits, Startwert kennzeichnet dieses Format und verwirft insbesondere vier Nullbytes. */
    uint8_t checksum(const uint8_t (&bytes)[4])
    {
        uint8_t crc = 0x15;
        for (unsigned bit = 0; bit < 27; ++bit)
        {
            const bool feedback = ((crc >> 4) ^ ((bytes[bit / 8] >> (bit % 8)) & 1)) & 1;
            crc = static_cast<uint8_t>((crc << 1) & 31);
            if (feedback)
                crc ^= 0x05;
        }
        return crc;
    }

    /** @brief Liest RX8130-Register über den vorhandenen, verriegelten internen M5-I2C-Bus. */
    bool readRtc(uint8_t reg, uint8_t *bytes, size_t count)
    {
        return M5.In_I2C.readRegister(Address, reg, bytes, count, Frequency);
    }

    /** @brief Schreibt RX8130-Register mit dem bestätigten I2C-Ergebnis des vorhandenen M5-Busses. */
    bool writeRtc(uint8_t reg, const uint8_t *bytes, size_t count)
    {
        return M5.In_I2C.writeRegister(Address, reg, bytes, count, Frequency);
    }
}

bool Tab5Rtc::encodeRadio(const RadioPreferences &value, uint8_t (&bytes)[4])
{
    if (value.station >= 70 || value.volume > 100 || value.beforeMute < 1 || value.beforeMute > 100 || value.mode >= 8)
        return false;
    for (auto style : value.styles)
        if (style > 1)
            return false;
    bytes[0] = value.station | (value.muted ? 0x80 : 0);
    // Bei Pegel 0 bleibt die zum Entstummen nötige Lautstärke erhalten. Bei positivem Pegel ist dieser der Rückkehrwert.
    bytes[1] = value.volume ? value.volume : static_cast<uint8_t>(100 + value.beforeMute);
    bytes[2] = 0;
    for (unsigned i = 0; i < 8; ++i)
        bytes[2] |= value.styles[i] << i;
    bytes[3] = value.mode;
    bytes[3] |= checksum(bytes) << 3;
    return true;
}

bool Tab5Rtc::decodeRadio(const uint8_t (&bytes)[4], RadioPreferences &value)
{
    if ((bytes[0] & 0x7f) >= 70 || bytes[1] < 1 || bytes[1] > 200 || (bytes[3] >> 3) != checksum(bytes))
        return false;
    value.station = bytes[0] & 0x7f;
    value.muted = bytes[0] & 0x80;
    value.volume = bytes[1] <= 100 ? bytes[1] : 0;
    value.beforeMute = bytes[1] <= 100 ? bytes[1] : bytes[1] - 100;
    for (unsigned i = 0; i < 8; ++i)
        value.styles[i] = (bytes[2] >> i) & 1;
    value.mode = bytes[3] & 7;
    return true;
}

bool Tab5Rtc::utcEpoch(const tm &utc, time_t &epoch)
{
    const int year = utc.tm_year + 1900;
    if (year < 2000 || year > 2099 || utc.tm_mon < 0 || utc.tm_mon > 11 || utc.tm_mday < 1 || utc.tm_hour < 0 || utc.tm_hour > 23 || utc.tm_min < 0 || utc.tm_min > 59 || utc.tm_sec < 0 || utc.tm_sec > 59)
        return false;
    constexpr uint8_t Days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool leap = year % 4 == 0;
    if (utc.tm_mday > Days[utc.tm_mon] + (utc.tm_mon == 1 && leap))
        return false;
    int days = 10957; // 1970-01-01 -> 2000-01-01.
    for (int y = 2000; y < year; ++y)
        days += 365 + (y % 4 == 0);
    for (int month = 0; month < utc.tm_mon; ++month)
        days += Days[month] + (month == 1 && leap);
    epoch = static_cast<time_t>(days + utc.tm_mday - 1) * 86400 + utc.tm_hour * 3600 + utc.tm_min * 60 + utc.tm_sec;
    return true;
}

bool Tab5Rtc::begin()
{
    uint8_t flags, offset, control;
    if (!readRtc(Flags, &flags, 1) || !readRtc(Offset, &offset, 1) || !readRtc(Control, &control, 1))
        return false;
    m_status.flags = flags;
    m_ramLost = flags & Lost;
    m_clockStopped = control & Stopped;
    // M5.begin darf keine ungeprüfte Zeit setzen. Der vorhandene Treiber wird deshalb erst hier aktiviert.
    if (!M5.Rtc.begin(&M5.In_I2C, M5.getBoard()))
        return false;
    if (!m_ramLost && offset && !writeRtc(Offset, &offset, 1))
        return false;
    uint8_t backup;
    if (!readRtc(Backup, &backup, 1) || (backup & 0x30) != 0x30)
        return false;
    if (m_ramLost && !writeRtc(Ram, &InvalidStation, 1))
        return false;
    m_status.available = true;
    m_status.restored = restoreSystemTime();
    return true;
}

bool Tab5Rtc::readTime(time_t &epoch)
{
    uint8_t control, flags;
    m_status.timeValid = false;
    if (!m_status.available || !readRtc(Flags, &flags, 1) || !readRtc(Control, &control, 1))
        return false;
    m_status.flags = flags;
    if (m_clockStopped || (flags & Lost) || (control & Stopped))
        return false;
    m5::rtc_datetime_t date;
    if (!M5.Rtc.getDateTime(&date))
        return false;
    tm utc{};
    utc.tm_year = date.date.year - 1900;
    utc.tm_mon = date.date.month - 1;
    utc.tm_mday = date.date.date;
    utc.tm_hour = date.time.hours;
    utc.tm_min = date.time.minutes;
    utc.tm_sec = date.time.seconds;
    if (!utcEpoch(utc, epoch))
        return false;
    m_status.utc = epoch;
    m_status.timeValid = true;
    return true;
}

bool Tab5Rtc::restoreSystemTime()
{
    time_t epoch;
    if (!readTime(epoch))
        return false;
    timeval value{epoch, 0};
    if (settimeofday(&value, nullptr) != 0)
        return false;
    return true;
}

bool Tab5Rtc::syncFromSystem()
{
    const time_t epoch = time(nullptr);
    tm utc{};
    time_t checked;
    if (!m_status.available || !gmtime_r(&epoch, &utc) || !utcEpoch(utc, checked))
        return false;
    if (!M5.In_I2C.bitOff(Address, Control, Stopped, Frequency))
        return false;
    const auto bcd = [](int value)
    {
        return static_cast<uint8_t>((value / 10) * 16 + value % 10);
    };
    const uint8_t clock[] = {bcd(utc.tm_sec), bcd(utc.tm_min), bcd(utc.tm_hour), static_cast<uint8_t>(1u << utc.tm_wday), bcd(utc.tm_mday), bcd(utc.tm_mon + 1), bcd((utc.tm_year + 1900) % 100)};
    if (!writeRtc(0x10, clock, sizeof(clock)))
        return false;
    // VLF erst nach bestätigter Rücklesung löschen, Schreiben einer 1 erhält die anderen W0C-Ereignisflags.
    m5::rtc_datetime_t date;
    if (!M5.Rtc.getDateTime(&date))
        return false;
    tm readBack{};
    readBack.tm_year = date.date.year - 1900;
    readBack.tm_mon = date.date.month - 1;
    readBack.tm_mday = date.date.date;
    readBack.tm_hour = date.time.hours;
    readBack.tm_min = date.time.minutes;
    readBack.tm_sec = date.time.seconds;
    time_t actual;
    if (!utcEpoch(readBack, actual) || actual < epoch || actual - epoch > 2)
        return false;
    constexpr uint8_t ClearLost = 0xfd;
    if (!writeRtc(Flags, &ClearLost, 1))
        return false;
    m_clockStopped = false;
    if (!readTime(actual))
        return false;
    m_ramLost = false;
    ++m_status.timeWrites;
    return true;
}

bool Tab5Rtc::loadRadio(RadioPreferences &value, uint8_t quietVolume)
{
    uint8_t bytes[4];
    if (!m_status.available || m_ramLost || !readRtc(Ram, bytes, 4) || !decodeRadio(bytes, value))
    {
        // Bei verfügbarer RTC schreibt der Dienst radio_v1 nur nach Balance-/EQ-Änderungen, dessen Pegel kann deshalb weit über dem zuletzt gehörten liegen.
        if (value.volume > quietVolume)
            value.volume = quietVolume;
        if (value.beforeMute > quietVolume)
            value.beforeMute = quietVolume;
        return false;
    }
    memcpy(m_radio, bytes, 4);
    m_haveRadio = true;
    return true;
}

bool Tab5Rtc::saveRadio(const RadioPreferences &value)
{
    uint8_t bytes[4], confirmed[4];
    if (!m_status.available || !encodeRadio(value, bytes))
        return false;
    if (m_haveRadio && !memcmp(bytes, m_radio, 4))
        return true;
    // Den Sender zuerst ungültig machen, die restlichen drei Bytes schreiben und erst zuletzt freigeben.
    if (!writeRtc(Ram, &InvalidStation, 1) || !writeRtc(Ram + 1, bytes + 1, 3) || !writeRtc(Ram, bytes, 1) || !readRtc(Ram, confirmed, 4) || memcmp(bytes, confirmed, 4))
    {
        m_haveRadio = false;
        return false;
    }
    memcpy(m_radio, bytes, 4);
    m_haveRadio = true;
    ++m_status.ramWrites;
    return true;
}
