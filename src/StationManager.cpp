#include "StationManager.h"
#include "StationsDefault.h"
#include "StringUtils.h"
#include "Logger.h"

#include <esp_partition.h>
#include <esp_heap_caps.h>
#include <mutex>
#include <cstddef>
#include <cstring>

// Partitionsname (muss mit partitions_tab5.csv übereinstimmen)
static constexpr const char *PARTITION_LABEL = "stations";
static constexpr esp_partition_subtype_t PARTITION_SUBTYPE =
    static_cast<esp_partition_subtype_t>(0x80);

namespace
{
    constexpr size_t RECORDS_SIZE = MAX_STATIONS * sizeof(StationRecord); ///< Puffergröße für die volle Liste

    /** @brief Geprüfte Senderliste auf dem Flash. */
    struct StoredList
    {
        bool valid = false;    ///< Kopf und Daten-CRC geprüft
        bool legacy = false;   ///< Altes Format Version 1 an Offset 0, älter als jeder gültige Platz
        uint32_t slot = 0;     ///< Ablageplatz 0 oder 1
        uint32_t sequence = 0; ///< Folgezähler, beim alten Format 0
        uint16_t count = 0;    ///< Anzahl Stationen
        uint32_t dataCrc = 0;  ///< CRC32 über die StationRecords
    };

    /** @brief CRC32 mit Standardpolynom 0xEDB88320, wie zlib. Gleiche Werte wie im alten Format. */
    uint32_t crc32(const void *data, size_t length)
    {
        const auto *bytes = static_cast<const uint8_t *>(data);
        uint32_t crc = 0xFFFFFFFF;

        for (size_t i = 0; i < length; i++)
        {
            crc ^= bytes[i];
            for (int bit = 0; bit < 8; bit++)
                if (crc & 1)
                    crc = (crc >> 1) ^ 0xEDB88320;
                else
                    crc >>= 1;
        }

        return ~crc;
    }

    /** @brief CRC32 über den Platzkopf ohne das Feld headerCrc. */
    uint32_t headerCrc(const StationsSlotHeader &header)
    {
        return crc32(&header, offsetof(StationsSlotHeader, headerCrc));
    }

    /** @brief Puffer für MAX_STATIONS Datensätze, bevorzugt im PSRAM. nullptr ohne Speicher, Freigabe mit free(). */
    StationRecord *allocRecords()
    {
        void *buffer = heap_caps_malloc(RECORDS_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        return static_cast<StationRecord *>(buffer ? buffer : malloc(RECORDS_SIZE));
    }

    /**
     * @brief Liest einen Ablageplatz und prüft Kopf, Länge und Daten-CRC
     *
     * Platz 0 darf statt eines Platzkopfs das alte Format Version 1 tragen. Ein gelöschter Platz ist ohne Logzeile ungültig.
     * @param records Puffer mit RECORDS_SIZE Bytes, danach mit den Daten des Platzes gefüllt
     * @param list Ergebnis, bei ungültigem Platz zurückgesetzt
     * @return true bei gültigem Platz
     */
    bool readSlot(const esp_partition_t *part, uint32_t slot, StationRecord *records, StoredList &list)
    {
        list = StoredList{};
        const uint32_t offset = slot * STATIONS_SLOT_SIZE;
        if (part->size < offset + STATIONS_SLOT_SIZE)
            return false;

        StationsSlotHeader header;
        esp_err_t err = esp_partition_read(part, offset, &header, sizeof(header));
        if (err != ESP_OK)
        {
            LOG_ERROR_F("Senderplatz %lu nicht lesbar: %s", static_cast<unsigned long>(slot), esp_err_to_name(err));
            return false;
        }
        if (header.magic != STATIONS_MAGIC)
            return false;

        uint32_t dataOffset = 0;
        if (header.version == STATIONS_FORMAT_VERSION)
        {
            if (header.headerCrc != headerCrc(header) || header.count > MAX_STATIONS || header.length != header.count * sizeof(StationRecord))
            {
                LOG_WARNING_F("Senderplatz %lu: Kopf ungültig, Platz wird ignoriert.", static_cast<unsigned long>(slot));
                return false;
            }
            dataOffset = offset + sizeof(header);
            list.sequence = header.sequence;
            list.dataCrc = header.dataCrc;
        }
        else if (slot == 0 && header.version == STATIONS_LEGACY_VERSION)
        {
            StationsHeader old;
            memcpy(&old, &header, sizeof(old));
            if (old.count > MAX_STATIONS)
            {
                LOG_WARNING_F("Ungültige Stationsanzahl im alten Format: %u", static_cast<unsigned>(old.count));
                return false;
            }
            dataOffset = sizeof(old);
            list.legacy = true;
            list.dataCrc = old.crc32;
        }
        else
        {
            LOG_WARNING_F("Senderplatz %lu: unbekannte Format-Version %u, Platz wird ignoriert.", static_cast<unsigned long>(slot), static_cast<unsigned>(header.version));
            return false;
        }

        list.count = header.count;
        const size_t length = list.count * sizeof(StationRecord);
        if (length && (err = esp_partition_read(part, dataOffset, records, length)) != ESP_OK)
        {
            LOG_ERROR_F("Senderplatz %lu: Daten nicht lesbar: %s", static_cast<unsigned long>(slot), esp_err_to_name(err));
            list = StoredList{};
            return false;
        }
        const uint32_t crc = crc32(records, length);
        if (crc != list.dataCrc)
        {
            LOG_WARNING_F("Senderplatz %lu: CRC32-Prüfung fehlgeschlagen (gespeichert 0x%08lX, berechnet 0x%08lX), Platz wird ignoriert.", static_cast<unsigned long>(slot), static_cast<unsigned long>(list.dataCrc), static_cast<unsigned long>(crc));
            list = StoredList{};
            return false;
        }
        list.slot = slot;
        list.valid = true;
        return true;
    }

    /**
     * @brief Sucht die aktuelle Senderliste: den gültigen Platz mit dem neueren Folgezähler, ohne gültigen Platz das alte Format
     * @param records Arbeitspuffer mit RECORDS_SIZE Bytes, enthält danach nicht zwingend die Daten des Ergebnisses
     */
    StoredList findCurrent(const esp_partition_t *part, StationRecord *records)
    {
        StoredList current;
        for (uint32_t slot = 0; slot < STATIONS_SLOT_COUNT; ++slot)
        {
            StoredList list;
            // Vorzeichenbehaftete Differenz: Nach dem Überlauf ist 0 neuer als 0xFFFFFFFF.
            if (readSlot(part, slot, records, list) && (!current.valid || current.legacy || static_cast<int32_t>(list.sequence - current.sequence) > 0))
                current = list;
        }
        return current;
    }
}

StationManager::StationManager() : m_stationCount(0)
{
}

bool StationManager::loadStations()
{
    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, PARTITION_SUBTYPE, PARTITION_LABEL);

    if (!part)
    {
        LOG_ERROR("Stations-Partition nicht gefunden! Lade Defaults...");
        loadDefaults();
        return true;
    }

    LOG_INFO_F("Stations-Partition gefunden: %s (%lu KB)",
               part->label, static_cast<unsigned long>(part->size / 1024));

    StationRecord *records = allocRecords();
    if (!records)
    {
        LOG_ERROR("Kein Speicher für Station-Records! Lade Defaults...");
        loadDefaults();
        return true;
    }

    StoredList current = findCurrent(part, records);
    // Der Puffer enthält den zuletzt geprüften Platz, daher den gewählten erneut lesen.
    if (current.valid && !readSlot(part, current.slot, records, current))
        current = StoredList{};

    if (!current.valid)
    {
        free(records);
        LOG_WARNING("Keine gültige Senderliste in der Partition. Lade Defaults nur ins RAM...");
        loadDefaults();
        return true;
    }

    // Daten ins RAM-Array übertragen
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        for (int i = 0; i < static_cast<int>(current.count); i++)
        {
            m_stations[i] = Station();  // Laufzeitfelder zurücksetzen

            stringutils::safeCopy(m_stations[i].shortName, records[i].shortName);
            stringutils::safeCopy(m_stations[i].name, records[i].name);
            stringutils::safeCopy(m_stations[i].url, records[i].url);
            m_stations[i].preset = (records[i].preset != 0);
        }

        // Restliche Slots auffüllen (festes 70-Slot-Modell)
        for (int i = current.count; i < MAX_STATIONS; i++)
            m_stations[i] = Station();
        m_stationCount = MAX_STATIONS;
    }

    free(records);
    if (current.legacy)
        LOG_SUCCESS_F("%d Stationen im alten Format geladen (CRC32 OK), Umstellung beim nächsten Speichern", current.count);
    else
        LOG_SUCCESS_F("%d Stationen aus Platz %lu geladen (Folge %lu, CRC32 OK)", current.count,
                      static_cast<unsigned long>(current.slot), static_cast<unsigned long>(current.sequence));
    return true;
}

bool StationManager::saveStations(const Station *replacement)
{
    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, PARTITION_SUBTYPE, PARTITION_LABEL);

    if (!part)
    {
        LOG_ERROR("Stations-Partition nicht gefunden! Speichern nicht möglich.");
        return false;
    }

    if (part->size < STATIONS_SLOT_COUNT * STATIONS_SLOT_SIZE)
    {
        LOG_ERROR("Stations-Partition zu klein, keine Daten geschrieben.");
        return false;
    }

    StationRecord *records = allocRecords();
    if (!records)
    {
        LOG_ERROR("Kein Speicher für Station-Records beim Speichern!");
        return false;
    }

    // Der Platz mit der aktuellen Liste bleibt unberührt, das alte Format liegt in Platz 0.
    const StoredList current = findCurrent(part, records);
    const uint32_t slot = current.valid && current.slot == 0 ? 1 : 0;
    const uint32_t offset = slot * STATIONS_SLOT_SIZE;

    memset(records, 0, RECORDS_SIZE);  // Sauberer Ausgangszustand
    int actualCount = 0;

    // Records unter Lock aus RAM kopieren
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        actualCount = replacement ? MAX_STATIONS : m_stationCount;

        for (int i = 0; i < actualCount; i++)
        {
            const auto &station = replacement ? replacement[i] : m_stations[i];
            stringutils::safeCopy(records[i].shortName, station.shortName);
            stringutils::safeCopy(records[i].name, station.name);
            stringutils::safeCopy(records[i].url, station.url);
            records[i].preset = station.preset ? 1 : 0;
        }
    }
    // Lock freigegeben - Flash-I/O ohne Lock

    StationsSlotHeader header;
    memset(&header, 0, sizeof(header));
    header.magic = STATIONS_MAGIC;
    header.version = STATIONS_FORMAT_VERSION;
    header.count = static_cast<uint16_t>(actualCount);
    header.sequence = current.sequence + 1;
    header.length = actualCount * sizeof(StationRecord);
    header.dataCrc = crc32(records, header.length);
    header.headerCrc = headerCrc(header);

    // Kopf zuletzt: Bis er vollständig steht, ist der Zielplatz ungültig und das Laden nimmt die vorherige Liste.
    esp_err_t err = esp_partition_erase_range(part, offset, STATIONS_SLOT_SIZE);
    if (err == ESP_OK)
        err = esp_partition_write(part, offset + sizeof(header), records, header.length);
    if (err == ESP_OK)
        err = esp_partition_write(part, offset, &header, sizeof(header));
    if (err != ESP_OK)
    {
        LOG_ERROR_F("Senderplatz %lu nicht geschrieben: %s. Vorherige Liste bleibt gültig.", static_cast<unsigned long>(slot), esp_err_to_name(err));
        free(records);
        return false;
    }

    StoredList written;
    if (!readSlot(part, slot, records, written) || written.sequence != header.sequence || written.dataCrc != header.dataCrc)
    {
        LOG_ERROR_F("Senderplatz %lu: Prüflesen fehlgeschlagen, vorherige Liste bleibt gültig.", static_cast<unsigned long>(slot));
        free(records);
        return false;
    }

    if (replacement)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (int i = 0; i < actualCount; ++i)
        {
            stringutils::safeCopy(m_stations[i].shortName, replacement[i].shortName);
            stringutils::safeCopy(m_stations[i].name, replacement[i].name);
            stringutils::safeCopy(m_stations[i].url, replacement[i].url);
            m_stations[i].preset = replacement[i].preset;
        }
        m_stationCount = actualCount;
    }
    free(records);
    LOG_SUCCESS_F("%d Stationen in Platz %lu gespeichert (Folge %lu, CRC32: 0x%08lX)", actualCount, static_cast<unsigned long>(slot),
                  static_cast<unsigned long>(header.sequence), static_cast<unsigned long>(header.dataCrc));
    return true;
}

void StationManager::loadDefaults()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // Array zurücksetzen
    for (int i = 0; i < MAX_STATIONS; i++)
        m_stations[i] = Station();

    // Defaults aus PROGMEM kopieren
    const int count = (DEFAULT_STATION_COUNT < MAX_STATIONS)
                          ? DEFAULT_STATION_COUNT
                          : MAX_STATIONS;

    for (int i = 0; i < count; i++)
    {
        stringutils::safeCopy(m_stations[i].shortName, DEFAULT_STATIONS[i].shortName);
        stringutils::safeCopy(m_stations[i].name, DEFAULT_STATIONS[i].name);
        stringutils::safeCopy(m_stations[i].url, DEFAULT_STATIONS[i].url);
        m_stations[i].preset = DEFAULT_STATIONS[i].preset;
    }

    m_stationCount = MAX_STATIONS;

    LOG_INFO_F("Default-Senderliste geladen (%d Sender aus PROGMEM)", count);
}

int StationManager::getStationCount() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_stationCount;
}

bool StationManager::getStation(int index, Station &outStation) const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (index < 0 || index >= m_stationCount)
        return false;

    // Sichere Kopie erstellen (Station ist trivial kopierbar)
    outStation = m_stations[index];
    return true;
}
