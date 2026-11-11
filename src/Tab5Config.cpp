#include "Tab5Config.h"
#include "BtProtocol.h"
#include "Logger.h"
#include "Thema.h"
#include <nvs.h>
#include <cstring>
#include <mbedtls/platform_util.h>

namespace
{
    constexpr size_t BlobSize = 576;              ///< Größe des Datensatzes config_v1 einschließlich CRC
    constexpr uint32_t Magic = 0x31434652;        ///< Kennung von config_v1
    constexpr size_t ExtendedSize = 608;          ///< Größe von config_v2: config_v1-Daten ab Byte 0, danach Kennung, Klang, PIN, Bildschirm, Thema, UKW, serielle Logs und CRC am Ende
    constexpr uint32_t ExtendedMagic = 0x32434652; ///< Kennung des Anhangs von config_v2 ab Byte 576
    constexpr char LogsKey[] = "logs_v1";          ///< Eigener Eintrag der seriellen Logs (1 aus, 2 an). Ein Umschalten schreibt so einen NVS-Eintrag statt der 608 Byte von config_v2, der NVS-Bereich muss seltener Flashsektoren löschen
    constexpr char SecondHandKey[] = "secHand_v1"; ///< Eigener Eintrag des Sekundenzeigers im MainScreen (1 Sekundenschritte, 2 schleichend). Fehlt er oder trägt er einen anderen Wert, schleicht der Zeiger
    constexpr char PinLockKey[] = "pinLockOn";     ///< Eigener Eintrag der PIN-Sperre mit dem Namen aus dem Original (0 aus, 1 an). Fehlt er oder trägt er einen anderen Wert, gilt die Sperre

    /** @brief CRC über den eigenen, explizit kodierten Datensatz. */
    uint32_t crc(const uint8_t *data, size_t count)
    {
        uint32_t value = UINT32_MAX;
        while (count--)
        {
            value ^= *data++;
            for (int i = 0; i < 8; ++i)
                value = (value >> 1) ^ (0xedb88320u & (0u - (value & 1u)));
        }
        return ~value;
    }

    /** @brief Kodiert Zahlen portabel in Little Endian. */
    void number(uint8_t *data, size_t &at, uint64_t value, size_t bytes)
    {
        for (size_t i = 0; i < bytes; ++i)
            data[at++] = static_cast<uint8_t>(value >> (8 * i));
    }

    /** @brief Dekodiert eine bereits größen- und CRC-geprüfte Zahl. */
    uint64_t number(const uint8_t *data, size_t &at, size_t bytes)
    {
        uint64_t value = 0;
        for (size_t i = 0; i < bytes; ++i)
            value |= static_cast<uint64_t>(data[at++]) << (8 * i);
        return value;
    }

    /** @brief Serialisiert ohne Strukturpadding oder uninitialisierte Stringreste. */
    void encode(const Settings::Configuration &c, uint64_t revision, uint8_t (&out)[BlobSize])
    {
        memset(out, 0, sizeof(out));
        size_t at = 0;
        number(out, at, Magic, 4);
        number(out, at, 1, 2);
        number(out, at, revision, 8);
        auto str = [&](const char *s, size_t n)
        {
            memcpy(out + at, s, strlen(s));
            at += n;
        };
        for (const auto &p : c.profiles)
        {
            str(p.ssid, 33);
            str(p.password, 65);
            number(out, at, p.enabled, 1);
        }
        str(c.hostname, 33);
        str(c.ntpServer, 64);
        str(c.timezone, 64);
        str(c.timezonePosix, 64);
        number(out, at, c.syncIntervalMs, 4);
        for (auto mask : c.timeWindows)
            number(out, at, mask, 4);
        number(out, at, static_cast<uint8_t>(c.language), 1);
        number(out, at, c.eyeStyle, 1);
        number(out, at, c.vuBallistics, 1);
        number(out, at, c.vuSensitivity, 1);
        number(out, at, crc(out, BlobSize - 4), 4);
    }

    /** @brief Prüft CRC, Kennung, Revision und Wertebereiche und dekodiert den Datensatz config_v1. @param in Gespeicherter Datensatz. @param c Ziel. Bei false nur teilweise beschrieben. @param revision Gespeicherte Revision. @return true bei gültigem Datensatz. */
    bool decode(const uint8_t (&in)[BlobSize], Settings::Configuration &c, uint64_t &revision)
    {
        size_t at = BlobSize - 4;
        if (number(in, at, 4) != crc(in, BlobSize - 4))
            return false;
        at = 0;
        if (number(in, at, 4) != Magic || number(in, at, 2) != 1)
            return false;
        revision = number(in, at, 8);
        if (!revision || revision == UINT64_MAX)
            return false;
        auto str = [&](char *s, size_t n)
        {
            memcpy(s, in + at, n);
            at += n;
        };
        for (auto &p : c.profiles)
        {
            str(p.ssid, 33);
            str(p.password, 65);
            const auto enabled = number(in, at, 1);
            if (enabled > 1)
                return false;
            p.enabled = enabled;
        }
        str(c.hostname, 33);
        str(c.ntpServer, 64);
        str(c.timezone, 64);
        str(c.timezonePosix, 64);
        c.syncIntervalMs = number(in, at, 4);
        for (auto &mask : c.timeWindows)
            mask = number(in, at, 4);
        c.language = static_cast<LangIndex>(number(in, at, 1));
        c.eyeStyle = number(in, at, 1);
        c.vuBallistics = number(in, at, 1);
        c.vuSensitivity = number(in, at, 1);
        return Tab5Config::valid(c);
    }
}

bool Tab5Config::valid(const Settings::Configuration &c)
{
    if (!SettingsUi::validText(c.hostname, sizeof(c.hostname)) || !c.hostname[0] || c.hostname[0] == '-' || c.hostname[strlen(c.hostname) - 1] == '-')
        return false;
    for (const char *p = c.hostname; *p; ++p)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '-'))
            return false;
    for (const auto &p : c.profiles)
    {
        if (!SettingsUi::validText(p.ssid, sizeof(p.ssid)) || !memchr(p.password, 0, sizeof(p.password)) || (p.enabled && !p.ssid[0]))
            return false;
        char normalized[65]{};
        const bool ok = Settings::normalizePassword(p.password, normalized);
        mbedtls_platform_zeroize(normalized, sizeof(normalized));
        if (!ok)
            return false;
    }
    if (!SettingsUi::validText(c.ntpServer, sizeof(c.ntpServer)) || !c.ntpServer[0] || !SettingsUi::validText(c.timezone, sizeof(c.timezone)) || !SettingsUi::validText(c.timezonePosix, sizeof(c.timezonePosix)) || !c.timezonePosix[0])
        return false;
    if (c.syncIntervalMs < 60000 || c.syncIntervalMs > 86400000 || (c.language != LangIndex::DE && c.language != LangIndex::EN) || c.eyeStyle > 3 || c.vuBallistics > 2 || c.vuSensitivity > 4)
        return false;
    if (c.brightness < Settings::BrightnessMin || c.brightness > Settings::BrightnessMax || (c.rotation != Settings::RotationNormal && c.rotation != Settings::RotationFlipped) || c.theme >= ThemenAnzahl)
        return false;
    if (!BtProtocol::validUkwKhz(c.ukwKhz) || c.ukwPower > Settings::UkwMaxPower)
        return false;
    for (auto mask : c.timeWindows)
        if (mask & ~Settings::DayMask)
            return false;
    return true;
}

bool Tab5Config::valid(const Extras &extras)
{
    const auto &s = extras.sound;
    if (s.eqLow < 100 || s.eqLow > 800 || s.eqPeak < 1500 || s.eqPeak > 6000 || s.eqHigh < 4000 || s.eqHigh > 12000 || s.width > 200 || s.loudness > 100 || s.volume > 100)
        return false;
    for (size_t i = 0; i < 4; ++i)
        if (extras.pin[i] < '0' || extras.pin[i] > '9')
            return false;
    return extras.pin[4] == 0;
}

void Tab5Config::defaults(Settings::Configuration &config, Extras &extras)
{
    config = {};
    extras = {};
    strcpy(config.hostname, "RetroRadioTab5");
    strcpy(config.ntpServer, "de.pool.ntp.org");
    strcpy(config.timezone, "Europe/Berlin");
    strcpy(config.timezonePosix, "CET-1CEST,M3.5.0,M10.5.0/3");
    config.syncIntervalMs = 3600000;
    for (auto &mask : config.timeWindows)
        mask = Settings::DayMask;
}

void Tab5Config::load(Settings::Configuration &config, uint64_t &revision, Extras *extras, bool report)
{
    Extras candidateExtras;
    defaults(config, candidateExtras);
    revision = 1;
    if (extras)
        *extras = candidateExtras;
    nvs_handle_t nvs;
    if (nvs_open("rr_tab5", NVS_READONLY, &nvs) != ESP_OK)
        return;
    uint8_t blob[ExtendedSize]{};
    size_t size = sizeof(blob);
    Settings::Configuration candidate;
    uint64_t storedRevision;
    bool loaded = false;
    if (nvs_get_blob(nvs, "config_v2", blob, &size) == ESP_OK && size == sizeof(blob))
    {
        size_t at = ExtendedSize - 4;
        if (number(blob, at, 4) == crc(blob, ExtendedSize - 4))
        {
            uint8_t legacy[BlobSize];
            memcpy(legacy, blob, BlobSize);
            at = BlobSize;
            if (number(blob, at, 4) == ExtendedMagic && decode(legacy, candidate, storedRevision))
            {
                auto &sound = candidateExtras.sound;
                sound.eqLow = number(blob, at, 2);
                sound.eqPeak = number(blob, at, 2);
                sound.eqHigh = number(blob, at, 2);
                sound.width = number(blob, at, 1);
                sound.loudness = number(blob, at, 1);
                sound.volume = number(blob, at, 1);
                const auto speakers = number(blob, at, 1);
                sound.speakers = speakers == 1;
                sound.playing = false;
                memcpy(candidateExtras.pin, blob + at, 4);
                candidateExtras.pin[4] = 0;
                at += 4;
                // Bildschirmwerte in den Bytes 594 (Helligkeit) und 595 (Rotation), ältere Datensätze tragen dort 0 und behalten die Vorgaben.
                const auto brightness = number(blob, at, 1), rotation = number(blob, at, 1), automatic = number(blob, at, 1);
                if (brightness >= Settings::BrightnessMin && brightness <= Settings::BrightnessMax)
                    candidate.brightness = brightness;
                if (rotation == Settings::RotationNormal || rotation == Settings::RotationFlipped)
                    candidate.rotation = rotation;
                // Byte 596: 1 Automatik aus, 2 ein, 0 (ältere Datensätze) behält die Vorgabe ein.
                if (automatic == 1 || automatic == 2)
                    candidate.autoRotate = automatic == 2;
                // Byte 597: Themenindex. Ältere Datensätze tragen 0 (Retro), ein unbekannter Index ergibt ebenfalls Retro, ohne den Datensatz zu verwerfen.
                const auto theme = number(blob, at, 1);
                candidate.theme = theme < ThemenAnzahl ? static_cast<uint8_t>(theme) : ThemaRetroIndex;
                // Bytes 598 bis 601: UKW-Schalter (1 aus, 2 an), Frequenz in Zehntel-kHz-Schritten von 10 (7600..10800) und Leistungsstufe plus 1. Ältere Datensätze tragen 0 und behalten die Vorgaben, ungültige Werte ebenso.
                const auto ukwOn = number(blob, at, 1), ukwKhz10 = number(blob, at, 2), ukwPower = number(blob, at, 1);
                if (ukwOn == 1 || ukwOn == 2)
                    candidate.ukwEnabled = ukwOn == 2;
                if (BtProtocol::validUkwKhz(ukwKhz10 * 10))
                    candidate.ukwKhz = ukwKhz10 * 10;
                if (ukwPower >= 1 && ukwPower <= Settings::UkwMaxPower + 1u)
                    candidate.ukwPower = static_cast<uint8_t>(ukwPower - 1);
                // Byte 602: serielle Logs (1 aus, 2 an). Ältere Datensätze tragen 0 und bleiben wie ungültige Werte bei der Vorgabe aus.
                const auto serialLogs = number(blob, at, 1);
                if (serialLogs == 1 || serialLogs == 2)
                    candidate.serialLogs = serialLogs == 2;
                loaded = speakers <= 1 && valid(candidateExtras);
            }
            mbedtls_platform_zeroize(legacy, sizeof(legacy));
        }
    }
    if (!loaded)
    {
        uint8_t legacy[BlobSize]{};
        size = sizeof(legacy);
        loaded = nvs_get_blob(nvs, "config_v1", legacy, &size) == ESP_OK && size == sizeof(legacy) && decode(legacy, candidate, storedRevision);
        candidateExtras = {};
        mbedtls_platform_zeroize(legacy, sizeof(legacy));
    }
    if (loaded)
    {
        config = candidate;
        revision = storedRevision;
        if (extras)
            *extras = candidateExtras;
        if (report)
            LOG_INFO("Eigene Tab5-Einstellungen geladen.");
    }
    else if (report)
        LOG_WARNING("Eigene Tab5-Konfiguration fehlt oder ist ungültig. Nur RAM-Defaults, keine Daten gelöscht.");
    // Byte 602 gilt nur, solange logs_v1 fehlt.
    uint8_t logs = 0;
    if (nvs_get_u8(nvs, LogsKey, &logs) == ESP_OK && (logs == 1 || logs == 2))
        config.serialLogs = logs == 2;
    uint8_t pinLock = 1;
    if (nvs_get_u8(nvs, PinLockKey, &pinLock) == ESP_OK && pinLock == 0)
        config.pinLock = false;
    uint8_t secondHand = 2;
    if (nvs_get_u8(nvs, SecondHandKey, &secondHand) == ESP_OK && secondHand == 1)
        config.secondSweep = false;
    nvs_close(nvs);
    mbedtls_platform_zeroize(blob, sizeof(blob));
    mbedtls_platform_zeroize(&candidate, sizeof(candidate));
    mbedtls_platform_zeroize(&candidateExtras, sizeof(candidateExtras));
}

uint8_t Tab5Config::loadTheme()
{
    Settings::Configuration config;
    uint64_t revision;
    load(config, revision, nullptr, false);
    const uint8_t theme = config.theme;
    mbedtls_platform_zeroize(&config, sizeof(config));
    return theme;
}

bool Tab5Config::loadSerialLogs()
{
    Settings::Configuration config;
    uint64_t revision;
    load(config, revision, nullptr, false);
    const bool enabled = config.serialLogs;
    mbedtls_platform_zeroize(&config, sizeof(config));
    return enabled;
}

bool Tab5Config::saveSerialLogs(bool enabled)
{
    nvs_handle_t nvs;
    esp_err_t result = nvs_open("rr_tab5", NVS_READWRITE, &nvs);
    if (result == ESP_OK)
    {
        result = nvs_set_u8(nvs, LogsKey, enabled ? 2 : 1);
        if (result == ESP_OK)
            result = nvs_commit(nvs);
        nvs_close(nvs);
    }
    return result == ESP_OK;
}

bool Tab5Config::savePinLock(bool enabled)
{
    nvs_handle_t nvs;
    esp_err_t result = nvs_open("rr_tab5", NVS_READWRITE, &nvs);
    if (result == ESP_OK)
    {
        result = nvs_set_u8(nvs, PinLockKey, enabled ? 1 : 0);
        if (result == ESP_OK)
            result = nvs_commit(nvs);
        nvs_close(nvs);
    }
    return result == ESP_OK;
}

bool Tab5Config::saveSecondHand(bool sweep)
{
    nvs_handle_t nvs;
    esp_err_t result = nvs_open("rr_tab5", NVS_READWRITE, &nvs);
    if (result == ESP_OK)
    {
        result = nvs_set_u8(nvs, SecondHandKey, sweep ? 2 : 1);
        if (result == ESP_OK)
            result = nvs_commit(nvs);
        nvs_close(nvs);
    }
    return result == ESP_OK;
}

bool Tab5Config::save(const Settings::Configuration &config, uint64_t revision, const Extras &extras)
{
    if (!valid(config) || !valid(extras) || !revision || revision == UINT64_MAX)
        return false;
    uint8_t blob[ExtendedSize]{};
    uint8_t legacy[BlobSize];
    encode(config, revision, legacy);
    memcpy(blob, legacy, sizeof(legacy));
    size_t at = BlobSize;
    number(blob, at, ExtendedMagic, 4);
    const auto &sound = extras.sound;
    number(blob, at, sound.eqLow, 2);
    number(blob, at, sound.eqPeak, 2);
    number(blob, at, sound.eqHigh, 2);
    number(blob, at, sound.width, 1);
    number(blob, at, sound.loudness, 1);
    number(blob, at, sound.volume, 1);
    number(blob, at, sound.speakers, 1);
    memcpy(blob + at, extras.pin, 4);
    at += 4;
    number(blob, at, config.brightness, 1);
    number(blob, at, config.rotation, 1);
    number(blob, at, config.autoRotate ? 2 : 1, 1);
    number(blob, at, config.theme, 1);
    number(blob, at, config.ukwEnabled ? 2 : 1, 1);
    number(blob, at, config.ukwKhz / 10, 2);
    number(blob, at, config.ukwPower + 1u, 1);
    number(blob, at, config.serialLogs ? 2 : 1, 1);
    at = ExtendedSize - 4;
    number(blob, at, crc(blob, ExtendedSize - 4), 4);
    nvs_handle_t nvs;
    esp_err_t result = nvs_open("rr_tab5", NVS_READWRITE, &nvs);
    if (result == ESP_OK)
    {
        result = nvs_set_blob(nvs, "config_v2", blob, sizeof(blob));
        if (result == ESP_OK)
            result = nvs_set_u8(nvs, LogsKey, config.serialLogs ? 2 : 1);
        if (result == ESP_OK)
            result = nvs_set_u8(nvs, PinLockKey, config.pinLock ? 1 : 0);
        if (result == ESP_OK)
            result = nvs_set_u8(nvs, SecondHandKey, config.secondSweep ? 2 : 1);
        if (result == ESP_OK)
            result = nvs_commit(nvs);
        nvs_close(nvs);
    }
    mbedtls_platform_zeroize(blob, sizeof(blob));
    mbedtls_platform_zeroize(legacy, sizeof(legacy));
    return result == ESP_OK;
}

namespace
{
    /** @brief Prüft alle Regler- und Stilgrenzen vor NVS oder Geräteübernahme. */
    bool validRadio(const RadioPreferences &value)
    {
        if (value.mode >= 8 || value.volume > 100 || value.beforeMute < 1 || value.beforeMute > 100 || value.balance < -16 || value.balance > 16)
            return false;
        for (auto style : value.styles)
            if (style > 1)
                return false;
        for (auto gain : value.tone)
            if (gain < -12 || gain > 12)
                return false;
        return true;
    }
}

RadioPreferences Tab5Config::loadRadio()
{
    RadioPreferences value;
    nvs_handle_t handle;
    if (nvs_open("rr_tab5", NVS_READONLY, &handle) != ESP_OK)
        return value;
    uint8_t blob[24]{};
    size_t size = sizeof(blob);
    const bool found = nvs_get_blob(handle, "radio_v1", blob, &size) == ESP_OK;
    nvs_close(handle);
    size_t at = 20;
    if (!found || size != sizeof(blob) || number(blob, at, 4) != crc(blob, 20))
        return value;
    at = 0;
    if (number(blob, at, 4) != 0x31505252)
        return value;
    value.mode = blob[4];
    memcpy(value.styles, blob + 5, 8);
    value.volume = blob[13];
    memcpy(value.tone, blob + 14, 3);
    value.balance = static_cast<int8_t>(blob[17]);
    if (blob[18] > 1)
        return RadioPreferences{};
    value.muted = blob[18];
    value.beforeMute = blob[19] ? blob[19] : 10;
    return validRadio(value) ? value : RadioPreferences{};
}

bool Tab5Config::saveRadio(const RadioPreferences &value)
{
    if (!validRadio(value))
        return false;
    uint8_t blob[24]{};
    size_t at = 0;
    number(blob, at, 0x31505252, 4);
    blob[4] = value.mode;
    memcpy(blob + 5, value.styles, 8);
    blob[13] = value.volume;
    memcpy(blob + 14, value.tone, 3);
    blob[17] = static_cast<uint8_t>(value.balance);
    blob[18] = value.muted;
    blob[19] = value.beforeMute;
    at = 20;
    number(blob, at, crc(blob, 20), 4);
    nvs_handle_t handle;
    if (nvs_open("rr_tab5", NVS_READWRITE, &handle) != ESP_OK)
        return false;
    const bool ok = nvs_set_blob(handle, "radio_v1", blob, sizeof(blob)) == ESP_OK && nvs_commit(handle) == ESP_OK;
    nvs_close(handle);
    return ok;
}

namespace
{
    constexpr uint32_t BatteryMagic = 0x31415252; ///< Kennung von akku_v1, als Bytes „RRA1“
}

bool Tab5Config::loadBattery(Tab5Battery::Calibration &value)
{
    nvs_handle_t handle;
    if (nvs_open("rr_tab5", NVS_READONLY, &handle) != ESP_OK)
        return false;
    uint8_t blob[12]{};
    size_t size = sizeof(blob);
    const bool found = nvs_get_blob(handle, "akku_v1", blob, &size) == ESP_OK;
    nvs_close(handle);
    size_t at = 8;
    if (!found || size != sizeof(blob) || number(blob, at, 4) != crc(blob, 8))
        return false;
    at = 0;
    if (number(blob, at, 4) != BatteryMagic)
        return false;
    value.capacityMah = static_cast<uint16_t>(number(blob, at, 2));
    value.resistanceMohm = static_cast<uint16_t>(number(blob, at, 2));
    value.learned = true;
    return true;
}

bool Tab5Config::saveBattery(const Tab5Battery::Calibration &value)
{
    uint8_t blob[12]{};
    size_t at = 0;
    number(blob, at, BatteryMagic, 4);
    number(blob, at, value.capacityMah, 2);
    number(blob, at, value.resistanceMohm, 2);
    number(blob, at, crc(blob, 8), 4);
    nvs_handle_t handle;
    if (nvs_open("rr_tab5", NVS_READWRITE, &handle) != ESP_OK)
        return false;
    const bool ok = nvs_set_blob(handle, "akku_v1", blob, sizeof(blob)) == ESP_OK && nvs_commit(handle) == ESP_OK;
    nvs_close(handle);
    return ok;
}

namespace
{
    constexpr uint32_t ModernMagic = 0x314D5252;                               ///< Kennung von modern_v1, als Bytes „RRM1“
    constexpr uint16_t ModernVersion = 1;                                      ///< Version von modern_v1
    constexpr size_t ModernSize = 4 + 2 + 1 + 3 * ModernBearbeitbarAnzahl + 4; ///< Größe von modern_v1: Kennung, Version, Anzahl, je bearbeitbare Grundfarbe R, G, B und CRC am Ende
}

bool Tab5Config::validModern(const ModernFarben &farben)
{
    for (size_t i = ModernErsteBearbeitbare; i < ModernFarbenAnzahl; ++i)
        if (farben.rgb[i] > 0xFFFFFF)
            return false;
    return true;
}

bool Tab5Config::loadModern(ModernFarben &farben, bool report)
{
    farben = ModernVorgabe;
    nvs_handle_t nvs;
    if (nvs_open("rr_tab5", NVS_READONLY, &nvs) != ESP_OK)
        return false;
    // Ein Byte mehr als nötig, damit ein zu langer Eintrag als ungültig auffällt statt als Lesefehler.
    uint8_t blob[ModernSize + 1]{};
    size_t size = sizeof(blob);
    const esp_err_t result = nvs_get_blob(nvs, "modern_v1", blob, &size);
    nvs_close(nvs);
    if (result == ESP_ERR_NVS_NOT_FOUND)
        return false;
    size_t at = ModernSize - 4;
    bool valid = result == ESP_OK && size == ModernSize && number(blob, at, 4) == crc(blob, ModernSize - 4);
    if (valid)
    {
        at = 0;
        valid = number(blob, at, 4) == ModernMagic && number(blob, at, 2) == ModernVersion && number(blob, at, 1) == ModernBearbeitbarAnzahl;
    }
    if (!valid)
    {
        if (report)
            LOG_WARNING("Modern-Farben in rr_tab5/modern_v1 ungültig. Vorgaben gelten, nichts gelöscht.");
        return false;
    }
    for (size_t i = ModernErsteBearbeitbare; i < ModernFarbenAnzahl; ++i, at += 3)
        farben.rgb[i] = static_cast<uint32_t>(blob[at]) << 16 | static_cast<uint32_t>(blob[at + 1]) << 8 | blob[at + 2];
    return true;
}

bool Tab5Config::saveModern(const ModernFarben &farben)
{
    if (!validModern(farben))
        return false;
    uint8_t blob[ModernSize]{};
    size_t at = 0;
    number(blob, at, ModernMagic, 4);
    number(blob, at, ModernVersion, 2);
    number(blob, at, ModernBearbeitbarAnzahl, 1);
    for (size_t i = ModernErsteBearbeitbare; i < ModernFarbenAnzahl; ++i)
    {
        blob[at++] = static_cast<uint8_t>(farben.rgb[i] >> 16);
        blob[at++] = static_cast<uint8_t>(farben.rgb[i] >> 8);
        blob[at++] = static_cast<uint8_t>(farben.rgb[i]);
    }
    number(blob, at, crc(blob, ModernSize - 4), 4);
    nvs_handle_t nvs;
    if (nvs_open("rr_tab5", NVS_READWRITE, &nvs) != ESP_OK)
        return false;
    const bool ok = nvs_set_blob(nvs, "modern_v1", blob, sizeof(blob)) == ESP_OK && nvs_commit(nvs) == ESP_OK;
    nvs_close(nvs);
    return ok;
}

namespace
{
    constexpr uint32_t FtpMagic = 0x31465252;                                   ///< Kennung von ftp_v1, als Bytes „RRF1“
    constexpr uint16_t FtpVersion = 1;                                          ///< Version von ftp_v1
    constexpr size_t FtpChars = FtpProtocol::PasswordCapacity - 1;              ///< Zeichenplätze in ftp_v1, ungenutzte sind 0
    constexpr size_t FtpSize = 4 + 2 + 1 + FtpChars + 4;                        ///< Größe von ftp_v1: Kennung, Version, Länge, Zeichen und CRC am Ende
}

bool Tab5Config::loadFtpPassword(char (&out)[FtpProtocol::PasswordCapacity])
{
    memset(out, 0, sizeof(out));
    nvs_handle_t nvs;
    if (nvs_open("rr_tab5", NVS_READONLY, &nvs) != ESP_OK)
        return false;
    // Ein Byte mehr als nötig, damit ein zu langer Eintrag als ungültig auffällt statt als Lesefehler.
    uint8_t blob[FtpSize + 1]{};
    size_t size = sizeof(blob);
    const esp_err_t result = nvs_get_blob(nvs, "ftp_v1", blob, &size);
    nvs_close(nvs);
    size_t at = FtpSize - 4;
    bool valid = result == ESP_OK && size == FtpSize && number(blob, at, 4) == crc(blob, FtpSize - 4);
    if (valid)
    {
        at = 0;
        valid = number(blob, at, 4) == FtpMagic && number(blob, at, 2) == FtpVersion;
        const size_t length = blob[at++];
        valid = valid && length <= FtpChars;
        // Ungenutzte Zeichenplätze sind 0, ein Eintrag mit anderem Inhalt dort gilt als beschädigt.
        for (size_t i = length; valid && i < FtpChars; ++i)
            valid = blob[at + i] == 0;
        if (valid)
        {
            memcpy(out, blob + at, length);
            out[length] = 0;
            valid = strlen(out) == length && FtpProtocol::validPassword(out);
        }
    }
    if (!valid)
    {
        if (result != ESP_ERR_NVS_NOT_FOUND)
            LOG_WARNING("FTP-Passwort in rr_tab5/ftp_v1 ungültig. Erst das nächste Einschalten von FTP ersetzt es durch ein neues.");
        memset(out, 0, sizeof(out));
    }
    mbedtls_platform_zeroize(blob, sizeof(blob));
    return valid;
}

bool Tab5Config::saveFtpPassword(const char *password)
{
    if (!FtpProtocol::validPassword(password))
        return false;
    uint8_t blob[FtpSize]{};
    size_t at = 0;
    number(blob, at, FtpMagic, 4);
    number(blob, at, FtpVersion, 2);
    const size_t length = strlen(password);
    blob[at++] = static_cast<uint8_t>(length);
    memcpy(blob + at, password, length);
    at = FtpSize - 4;
    number(blob, at, crc(blob, FtpSize - 4), 4);
    nvs_handle_t nvs;
    bool ok = nvs_open("rr_tab5", NVS_READWRITE, &nvs) == ESP_OK;
    if (ok)
    {
        ok = nvs_set_blob(nvs, "ftp_v1", blob, sizeof(blob)) == ESP_OK && nvs_commit(nvs) == ESP_OK;
        nvs_close(nvs);
    }
    mbedtls_platform_zeroize(blob, sizeof(blob));
    return ok;
}

bool Tab5Config::resetModern()
{
    nvs_handle_t nvs;
    if (nvs_open("rr_tab5", NVS_READWRITE, &nvs) != ESP_OK)
        return false;
    const esp_err_t result = nvs_erase_key(nvs, "modern_v1");
    const bool ok = (result == ESP_OK || result == ESP_ERR_NVS_NOT_FOUND) && nvs_commit(nvs) == ESP_OK;
    nvs_close(nvs);
    return ok;
}
