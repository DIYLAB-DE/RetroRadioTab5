#ifndef STATIONMANAGER_H
#define STATIONMANAGER_H

#include <cstddef>
#include <cstdint>
#include <mutex>

// Größenkonstanten für Station-Strings

static constexpr size_t STATION_SHORTNAME_SIZE = 24;   ///< z.B. "Bayern 3", "SWR Aktuell"
static constexpr size_t STATION_NAME_SIZE = 64;   ///< Vollständiger Sendername
static constexpr size_t STATION_URL_SIZE = 256;  ///< Streaming-URLs

/**
 * @brief Struktur zur Repräsentation einer Radiostation (Laufzeit, im RAM)
 *
 * Diese Struktur verwendet feste Char-Arrays statt Heap-Allokation. Vorteile:
 * - Keine Heap-Fragmentierung
 * - Deterministischer Speicherverbrauch (~350 Bytes pro Station)
 * - Trivial kopierbar (POD-ähnlich)
 * - Kein manuelles Speichermanagement nötig
 */
struct Station
{
    int16_t x;                              ///< X-Position der Station in der Skala
    int16_t y;                              ///< Y-Position der Station in der Skala
    int16_t lastPos;                        ///< Letzte Zeigerposition auf der Skala in Pixeln
    char shortName[STATION_SHORTNAME_SIZE]; ///< Kurzer Name der Station
    char name[STATION_NAME_SIZE];           ///< Vollständiger Name der Station
    char url[STATION_URL_SIZE];             ///< Stream-URL der Station
    bool preset;                            ///< Flag ob Station als Preset gespeichert ist

    /**
     * @brief Konstruktor - initialisiert alle Felder mit Nullen/Leerstrings
     */
    Station()
        : x(0), y(0), lastPos(0), shortName{'\0'}, name{'\0'}, url{'\0'}, preset(false)
    {
    }
};

static constexpr int MAX_STATIONS = 70;  ///< Maximale Anzahl von Stationen

// Flash-Partition: Binärformat-Strukturen

/// Magic-Number zur Identifikation gültiger Stationsdaten: "STNS"
static constexpr uint32_t STATIONS_MAGIC = 0x534E5453;

/// Format-Version der Ablageplätze (StationsSlotHeader), bei Strukturänderungen erhöhen
static constexpr uint16_t STATIONS_FORMAT_VERSION = 2;

/// Format-Version des einfachen Formats an Offset 0 (StationsHeader), wird nur noch gelesen
static constexpr uint16_t STATIONS_LEGACY_VERSION = 1;

static constexpr uint32_t STATIONS_SLOT_SIZE = 0x8000; ///< Größe eines Ablageplatzes, Vielfaches der 4-KB-Flashsektoren
static constexpr uint32_t STATIONS_SLOT_COUNT = 2;     ///< Ablageplätze ab Offset 0, abwechselnd beschrieben

/**
 * @brief Kopf des einfachen Formats Version 1 an Offset 0, direkt gefolgt von count StationRecords
 *
 * Wird nur noch gelesen. Liegt im Bereich von Platz 0 und bleibt erhalten, bis Platz 1 eine neuere Liste trägt. Erst das folgende Speichern überschreibt es.
 */
struct StationsHeader
{
    uint32_t magic;          ///< Muss STATIONS_MAGIC sein
    uint16_t version;        ///< STATIONS_LEGACY_VERSION
    uint16_t count;          ///< Anzahl gespeicherter Stationen
    uint32_t crc32;          ///< CRC32 über alle StationRecord-Daten
    uint32_t reserved;       ///< Reserviert für spätere Erweiterungen
};

/**
 * @brief Kompakter Stationsdatensatz für die Flash-Partition
 *
 * Enthält nur die persistenten Felder ohne Laufzeitdaten (x, y, lastPos). sizeof(StationRecord) = 348 Bytes -> 70 Stationen = ~24 KB
 */
struct StationRecord
{
    char shortName[STATION_SHORTNAME_SIZE]; ///< Kurzname
    char name[STATION_NAME_SIZE];           ///< Vollständiger Name
    char url[STATION_URL_SIZE];             ///< Stream-URL
    uint8_t preset;                         ///< Favorit-Flag (0/1)
    uint8_t padding[3];                     ///< Alignment auf 4 Bytes
};

/**
 * @brief Kopf eines Ablageplatzes, direkt gefolgt von count StationRecords
 *
 * Wird nach den Daten geschrieben. Ein Platz gilt nur mit gültiger Kopf- und Daten-CRC, beim Laden zählt der gültige Platz mit dem neueren Folgezähler.
 */
struct StationsSlotHeader
{
    uint32_t magic;     ///< Muss STATIONS_MAGIC sein
    uint16_t version;   ///< STATIONS_FORMAT_VERSION
    uint16_t count;     ///< Anzahl gespeicherter Stationen
    uint32_t sequence;  ///< Folgezähler, je Speichern um 1 erhöht, vorzeichenbehaftet über den Überlauf hinweg verglichen
    uint32_t length;    ///< Datenlänge in Bytes, count * sizeof(StationRecord)
    uint32_t dataCrc;   ///< CRC32 über die StationRecords
    uint32_t headerCrc; ///< CRC32 über die vorherigen Felder des Kopfes, erkennt einen unvollständig geschriebenen Kopf
};

static_assert(sizeof(StationRecord) == 348 && sizeof(StationsSlotHeader) == 24, "Flashformat der Senderliste");
static_assert(sizeof(StationsSlotHeader) + MAX_STATIONS * sizeof(StationRecord) <= STATIONS_SLOT_SIZE, "Volle Senderliste passt in einen Platz");

/**
 * @brief Manager-Klasse für Radiostationen
 *
 * Verwaltet ein Array von Station-Objekten und bietet Funktionen zum Laden, Speichern und Manipulieren der Stationsliste.
 *
 * Speicherung: Dedizierte Flash-Partition ("stations") statt LittleFS, darin zwei abwechselnd beschriebene Ablageplätze, damit ein Abbruch beim Speichern die vorherige Liste nicht zerstört. Fallback: PROGMEM-Defaults aus StationsDefault.h, wenn kein Platz gültig ist.
 *
 * Speicherverbrauch RAM: ~70 × 350 Bytes ≈ 24 KB (statisch, kein Heap)
 */
class StationManager
{
public:
    /**
     * @brief Konstruktor - initialisiert den StationManager
     */
    StationManager();

    /**
     * @brief Destruktor
     */
    ~StationManager() = default;

    /**
     * @brief Lädt Stationen aus der Flash-Partition
     *
     * Ablauf:
     * 1. Partition "stations" suchen
     * 2. Beide Ablageplätze prüfen (Magic, Version, Kopf-CRC, Länge, Daten-CRC). Platz 0 darf das alte Format Version 1 tragen
     * 3. Den gültigen Platz mit dem neueren Folgezähler laden, das alte Format nur ohne gültigen Platz
     * 4. Ohne gültigen Platz: PROGMEM-Defaults nur ins RAM laden
     *
     * Schreibt nie in die Partition.
     *
     * @return true wenn erfolgreich geladen (Partition oder Defaults)
     */
    bool loadStations();

    /**
     * @brief Speichert Stationen in die Flash-Partition
     *
     * Schreibt in den Ablageplatz, der nicht die aktuelle Liste trägt: Platz löschen, StationRecords schreiben, Kopf mit um 1 erhöhtem Folgezähler zuletzt, danach zurücklesen und prüfen. Ein Abbruch oder Fehler lässt die vorherige Liste gültig. Das alte Format liegt in Platz 0, das erste Speichern schreibt daher nach Platz 1.
     *
     * @param replacement Optional vollständiges Array mit MAX_STATIONS Einträgen, RAM erst nach erfolgreichem Schreiben ersetzen.
     * @return true wenn erfolgreich gespeichert
     */
    bool saveStations(const Station *replacement = nullptr);

    /**
     * @brief Lädt die PROGMEM-Defaults in das RAM-Array
     *
     * Wird aufgerufen, wenn die Partition leer oder korrupt ist.
     *
     * @note Ruft NICHT automatisch saveStations() auf!
     */
    void loadDefaults();

    /**
     * @brief Gibt die Anzahl der geladenen Stationen zurück
     * @return Anzahl der Stationen
     */
    int getStationCount() const;

    /**
     * @brief Gibt eine einzelne Station zurück (thread-safe Kopie)
     * @param index Index der gewünschten Station
     * @param outStation Ausgabe-Referenz für die Station
     * @return true wenn Station existiert, false bei ungültigem Index
     * @note Erstellt eine Kopie - sicher für Multithreading
     */
    bool getStation(int index, Station &outStation) const;

    /**
     * @brief Führt eine Funktion mit Lock auf dem Stations-Array aus
     * @tparam Func Callback-Typ: void(const Station*, int count)
     * @param callback Funktion die mit dem Array aufgerufen wird
     * @note Das Array ist während des Callbacks vollständig geschützt. Ideal für Webinterface, Serialisierung, UI-Listen etc.
     *
     * Beispiel:
     * @code
     * manager.withStations([](const Station* stations, int count) {
     *     for (int i = 0; i < count; i++) {
     *         Serial.println(stations[i].name);
     *     }
     * });
     * @endcode
     */
    template <typename Func>
    void withStations(Func &&callback) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        callback(m_stations, m_stationCount);
    }

private:
    Station m_stations[MAX_STATIONS];  ///< Array der Stationen (~24 KB)
    int m_stationCount;                ///< Aktuelle Anzahl der Stationen
    mutable std::mutex m_mutex;        ///< Schützt Zugriff bei Multithreading
};

#endif  // STATIONMANAGER_H
