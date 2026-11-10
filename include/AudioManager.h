#ifndef AUDIO_MANAGER_H
#define AUDIO_MANAGER_H

#include <freertos/FreeRTOS.h>  // für portMUX_TYPE
#include <freertos/portmacro.h> // für portMUX_INITIALIZER_UNLOCKED
#include <atomic>               // für std::atomic
#include <stdint.h>             // für uint16_t
#include <freertos/queue.h>

#include "Audio.h"

/**
 * @brief Struktur für geparste Metadaten
 *
 * Speichert die extrahierten Metadaten aus ID3-Tags. Verwendet feste Puffer um dynamische Speicherverwaltung zu vermeiden.
 */
struct AudioMetadata
{
    char artist[128]; ///< Interpret/Künstler
    char title[128];  ///< Titel des Songs
    char album[128];  ///< Album-Name
    bool hasArtist;   ///< Flag: Artist wurde gefunden
    bool hasTitle;    ///< Flag: Title wurde gefunden
    bool hasAlbum;    ///< Flag: Album wurde gefunden

    /**
     * @brief Konstruktor - Initialisiert alle Felder
     */
    AudioMetadata() : hasArtist(false), hasTitle(false), hasAlbum(false)
    {
        artist[0] = '\0';
        title[0] = '\0';
        album[0] = '\0';
    }

    /**
     * @brief Setzt alle Metadaten zurück
     */
    void reset()
    {
        artist[0] = '\0';
        title[0] = '\0';
        album[0] = '\0';
        hasArtist = false;
        hasTitle = false;
        hasAlbum = false;
    }
};

/**
 * @brief Enumeration für verschiedene Audio-Event-Typen
 *
 * Diese Enumeration definiert alle möglichen Event-Typen, die vom AudioManager verarbeitet werden können.
 */
enum class AudioManagerEventType
{
    NOTHING,         ///< Kein Event verfügbar
    INFO,            ///< Allgemeine Audio-Informationen
    ID3_ARTIST,      ///< ID3-Tag: Artist/Interpret
    ID3_TITLE,       ///< ID3-Tag: Titel
    ID3_ALBUM,       ///< ID3-Tag: Album
    ID3DATA,         ///< ID3-Tag-Daten (generisch, für andere Tags)
    EOF_AUDIO,       ///< Ende einer Audio-Datei/eines Streams erreicht
    STATION_NAME,    ///< Radio-Stations-Name
    ICY_DESCRIPTION, ///< ICY-Stream-Beschreibung
    STREAM_TITLE,    ///< Stream-Titel-Information
    BITRATE,         ///< Bitrate-Information
    ICY_URL,         ///< ICY-URL-Information
    ICY_LOGO,        ///< ICY-Logo-Information
    LAST_HOST,       ///< Information über den letzten Host
    COVER_IMAGE,     ///< Coverpositionen verfügbar. Werden in einen eigenen begrenzten Ereignispuffer kopiert
    LYRICS           ///< Liedtext verfügbar
};

/**
 * @brief Audio-Manager-Klasse für die Verarbeitung von Audio-Daten und -Events
 *
 * Der AudioManager ist eine zentrale Klasse zur Verwaltung und Analyse von Audio-Streams. Er verarbeitet sowohl die eigentlichen Audio-Samples als auch verschiedene Metadaten und Events, die von der Audio-Library gesendet werden.
 *
 * Hauptfunktionen:
 * - Real-time Audio-Sample-Analyse (RMS, Peak, Mono-Erkennung)
 * - Begrenzte Text-Event-Queue, älteste Meldung wird bei Überlauf ersetzt
 * - Pufferung von Audio-Samples für weitere Verarbeitung
 * - Thread-sichere Datenübertragung zwischen Audio-Callbacks und Haupt-Thread
 *
 * Die Klasse arbeitet mit einem Producer/Consumer-Pattern, bei dem die Audio-Callbacks als Producer fungieren und der Haupt-Thread als Consumer.
 */
class AudioManager
{
public:
    /** @brief Zustand des Audioworkers. */
    enum class State : uint8_t
    {
        Unavailable, ///< Worker nicht gestartet oder I2S-Initialisierung fehlgeschlagen
        Idle,        ///< Bereit, keine Quelle aktiv
        Starting,    ///< Quellwechsel läuft oder noch kein konsistenter Zustand
        Streaming,   ///< Quelle läuft
        Error,       ///< Start oder Betrieb fehlgeschlagen, Quelle gestoppt
        Paused,      ///< Datei pausiert
        Ended        ///< Datei regulär zu Ende gespielt
    };
    /** @brief Audioformat des aktiven Decoders, wie audio-lite es beim Anlegen meldet. */
    enum class Codec : uint8_t
    {
        None,   ///< Kein Decoder aktiv oder unbekannter Name
        Mp3,    ///< MP3
        Aac,    ///< AAC, HE-AAC und M4A
        Flac,   ///< FLAC
        Vorbis, ///< Ogg Vorbis
        Wav     ///< WAV
    };
    static constexpr uint8_t MaxVolume = 100;    ///< Größter Softwarepegel auf der audio-lite-Skala
    static constexpr uint8_t DefaultVolume = 10; ///< Startpegel
    static constexpr uint32_t PcmTimeoutMs = 15000; ///< Längste Zeit ohne PCM-Block in ms, danach beendet der Audioworker die Wiedergabe als Fehler

    /** @brief Coverpositionen einer Datei, wie audio-lite sie meldet. */
    struct CoverEvent
    {
        uint32_t requestId = 0;    ///< Wiedergabekennung der Datei
        uint32_t offsets[16]{};    ///< Positionen der Bilddaten in der Datei, paarweise, wie audio-lite sie meldet
        uint8_t count = 0;         ///< Anzahl gültiger Einträge in offsets, 0 ohne verwertbares Bild
    };

    /** @brief Übernimmt die letzte begrenzte Bildankündigung samt Audioauftrag ohne Datei-/Dekoderzugriff. Veraltete Kennungen prüft die Zentrale. @param event Ziel. @return true, wenn eine Ankündigung vorlag. */
    bool takeCover(CoverEvent &event);

    /** @brief Veröffentlichter Zustand für die Diagnose und die Bedienlogik. */
    struct Status
    {
        State state;                                                    ///< Zustand des Workers
        uint32_t pcmFrames, pcmAgeMs, sampleRate, bitRate, nonzeroBlocks; ///< Gelieferte PCM-Frames, Alter des letzten Blocks in ms (UINT32_MAX ohne), Abtastrate in Hz, Bitrate, Blöcke mit Signal
        int stationIndex;                                               ///< Index des Senders, -1 bei Dateien
        uint8_t volume;                                                 ///< Tatsächlich angewandter Softwarepegel
        uint32_t requestId = 0, controlRequestId = 0, positionSeconds = 0, durationSeconds = 0; ///< Kennung der Wiedergabe, des letzten Pause-/Suchbefehls, Position und Dauer der Datei in Sekunden
        bool file = false, fileConfirmed = false, commandFailed = false; ///< Quelle ist eine Datei, PCM der Datei wurde gesehen, letzter Pause-/Suchbefehl abgelehnt
        Codec codec = Codec::None;                                      ///< Aktiver Decoder
        uint32_t pcmGaps = 0, pcmGapMs = 0, inputBytes = 0;             ///< Lücken zwischen PCM-Blöcken seit dem Start des Geräts, deren Summe in ms und der Füllstand des Eingangspuffers in Byte
    };

    /** @brief Prüft frisches PCM der aktuellen Quelle vor dem Freigeben des Codecs. @param status Konsistentes Audioabbild. @param requestId Aktuell gewünschte Wiedergabe. @return Signal ist bereit. */
    static bool canOutput(const Status &status, uint32_t requestId);

    /** @brief Zeitüberschreitung ohne PCM im Audioworker. Der Abstand wird vorzeichenbehaftet ausgewertet, damit weder der Überlauf von millis() noch ein Zeitstempel, den der audio-lite-Task nach dem Lesen von now schreibt, einen Abbruch auslöst. @param now millis(), gelesen nach lastPcm. @param lastPcm Zeitstempel des letzten PCM-Blocks, 0 ohne Block seit Start oder Fortsetzen. @param started millis() beim Start oder Fortsetzen. @return true, wenn mehr als PcmTimeoutMs vergangen sind. */
    static constexpr bool pcmTimedOut(uint32_t now, uint32_t lastPcm, uint32_t started)
    {
        return static_cast<int32_t>(now - (lastPcm ? lastPcm : started)) > static_cast<int32_t>(PcmTimeoutMs);
    }

    /** @brief Startet den einzigen Besitzer von Audio/I2S. @return true bei angelegter Queue und Task. */
    bool begin();
    /** @brief Fordert einen Stream an. Der UI-Task muss den Codec vorher stummschalten. @param url Vollständige Stream-URL, wird kopiert. @param stationIndex Senderindex. @return true bei übernommener Anforderung. */
    bool requestStart(const char *url, int stationIndex, uint32_t requestId = 0);
    /** @brief Startet eine lokale Datei im einzigen Audioworker. @param path Geprüfter VFS-Pfad unter /sdcard/. @param requestId Wiedergabekennung. @return Auftrag kopiert. */
    bool requestFile(const char *path, uint32_t requestId);
    /** @brief Fordert Pause/Fortsetzung nur für die aktuelle Datei an. @param paused Gewünschter Zustand. @param requestId Befehlskennung. @return Angenommen. */
    bool requestPause(bool paused, uint32_t requestId);
    /** @brief Springt im Audioworker auf eine absolute Sekunde. @param seconds Ziel. @param requestId Befehlskennung. @return Angenommen. */
    bool requestSeek(uint32_t seconds, uint32_t requestId);

    /** @brief Begrenzt die Balance auf -16..16 und übergibt sie dem Audioworker. */
    void setBalance(int8_t value)
    {
        m_balance.store(value < -16 ? -16 : value > 16 ? 16
                                                       : value);
    }

    /** @brief Übergibt drei EQ-Werte, angewandt ausschließlich im Audioworker. @param low Tiefen. @param mid Mitten. @param high Höhen. Werte -12..12 dB. */
    void setTone(int8_t low, int8_t mid, int8_t high);

    /** @brief Klangparameter werden geschlossen an den einzigen Audioworker übergeben. */
    struct Tuning
    {
        uint16_t low = 500, mid = 1800, high = 6000; ///< Eckfrequenzen der Tiefen-, Mitten- und Höhenbänder in Hz
        uint8_t width = 100, loudness = 0;           ///< Basisbreite in Prozent (0..200, Vorgabe 100), Loudness-Stärke 0..100
    };

    /** @brief Prüft und übernimmt Eckfrequenzen, Basisbreite und Loudness ohne Decoderzugriff. @return Werte gültig. */
    bool setTuning(const Tuning &tuning);
    /** @brief Fordert Stopp an, der UI-Task schaltet den Codec zuvor sofort stumm. */
    void requestStop();
    /** @brief Liefert publizierte Diagnosedaten ohne Decoder-/Netzwerkzugriff. @return Aktueller Status. */
    Status getStatus() const;
    /** @brief Begrenzt den angeforderten Softwarepegel auf 0..100 und setzt den Bezugspegel der Loudness gleich. @param volume 0 bis 100 auf der audio-lite-Skala. */
    void setVolume(uint8_t volume);
    /** @brief Pegel 0..100, nach dem die Loudness Tiefen und Höhen anhebt, abweichend vom Softwarepegel. Nach setVolume() aufrufen. Geht der Ton an den Bluetooth-Kopfhörer, steht der Softwarepegel auf MaxVolume und der Drehknopf regelt den Kopfhörer. Die Loudness folgt dann dem Drehknopf. Bei 0 setzt die Messung die Pegelanzeige wie bei Stumm auf null. @param volume 0 bis 100. */
    void setLoudnessVolume(uint8_t volume);

    /** @brief Übernimmt den tatsächlichen Hardware-Stummzustand für die Pegelanzeige. @param muted true für stumm. */
    void setMuted(bool muted)
    {
        m_muted.store(muted);
    }

    /** @brief Wählt zwischen der Monomischung für den eingebauten Lautsprecher und Stereo für die 3,5-mm-Buchse. Der Audioworker übernimmt die Wahl im nächsten Durchlauf. @param stereo true bei gestecktem Stecker. */
    void setStereoOutput(bool stereo)
    {
        m_stereoOutput.store(stereo);
    }

    /**
     * @brief Maximale Länge für Event-Strings in der Queue
     */
    static constexpr size_t MAX_EVENT_STRING_LENGTH = 256;

    // Konstruktor & Initialisierung

    /**
     * @brief Konstruktor - Initialisiert alle Member-Variablen
     *
     * Setzt alle Puffer auf Null und initialisiert die Analyse-Parameter mit Standardwerten.
     */
    AudioManager();

    /**
     * @brief Initialisiert den AudioManager und registriert Callbacks
     * @param audio Referenz auf das Audio-Objekt
     *
     * Diese Funktion muss nach der Erstellung des Audio-Objekts aufgerufen werden, um den zentralen Callback zu registrieren.
     */
    void init(Audio &audio);

    // Event-Queue (begrenzter Ringpuffer)

    /**
     * @brief Holt das älteste Event aus der Queue
     * @param type (out) Event-Typ des dequeued Events
     * @param out (out) Zielpuffer für Event-Text/Payload (kann nullptr sein)
     * @param len Größe des Ausgabepuffers einschließlich Nullterminator
     * @param requestId Optionales Ziel für die Wiedergabekennung des entnommenen Events
     * @return true wenn ein Text-Event vorhanden war, false wenn die Queue leer ist.
     * @note Dateicover werden begrenzt kopiert und im zentralen Dateidienst dekodiert.
     * @note Die Funktion ist non-blocking und gibt sofort false zurück, wenn keine Events verfügbar sind.
     */
    bool dequeueEvent(AudioManagerEventType &type, char *out, size_t len, uint32_t *requestId = nullptr);

    // Metadaten-Zugriff

    /**
     * @brief Liefert die aktuellen Metadaten
     * @return Geliehene Referenz auf die Metadaten-Struktur, kein synchronisierter Snapshot
     *
     * Die Metadaten-Struktur enthält Artist, Title und Album-Informationen, die aus ID3-Tags extrahiert wurden. ID3DATA-Events können die Struktur im Audioworker ändern. Extern nur bei angehaltener Verarbeitung lesen, im UI die kopierten Events verwenden.
     */
    const AudioMetadata &getMetadata() const
    {
        return m_metadata;
    }

    /**
     * @brief Setzt die Metadaten zurück
     *
     * Sollte aufgerufen werden, wenn ein neuer Track startet oder die Metadaten ungültig werden.
     */
    void resetMetadata()
    {
        m_metadata.reset();
    }

    // Audio-Callback (wird von Audio-Library aufgerufen)

    /**
     * @brief Zentraler Callback für alle Audio-Events
     * @param msg Die Message-Struktur mit Event-Daten
     *
     * Diese Funktion wird von der Audio-Library für alle Events aufgerufen und verteilt sie an die entsprechenden internen Handler.
     *
     * @note Diese Funktion wird aus einem Callback-Kontext aufgerufen.
     */
    void handleAudioCallback(const Audio::msg_t &msg);

    /**
     * @brief Verarbeitet Audio-Samples in Echtzeit
     * @param outBuff Zeiger auf den Audio-Sample-Puffer (Stereo, 32-bit)
     * @param validSamples Anzahl der gültigen Stereo-Sample-Paare
     * @param continueI2S Zeiger auf Flag für I2S-Fortsetzung
     *
     * Diese Funktion analysiert die Audio-Samples und berechnet RMS- und Peak-Werte sowie die Mono-Erkennung. Die Samples werden in internen Puffern zur weiteren Verarbeitung gespeichert.
     *
     * @note Diese Funktion wird aus dem Decoder-Task aufgerufen.
     */
    void task_processAudioSamples32(int32_t *outBuff, int16_t validSamples, bool *continueI2S);

    // Zugriff auf analysierte Audio-Daten

    /**
     * @brief Prüft, ob neue Audio-Daten zur Verarbeitung bereitstehen
     * @return true wenn neue Daten verfügbar sind, false sonst
     *
     * Diese Funktion sollte vor dem Zugriff auf die Audio-Analyse-Daten aufgerufen werden, um sicherzustellen, dass aktuelle Daten vorliegen.
     */
    bool isAudioDataReady() const;

    /**
     * @brief Liefert den RMS-Wert des linken Kanals
     * @return RMS-Wert als skalierter Float-Wert
     *
     * Der RMS (Root Mean Square) Wert gibt die durchschnittliche Signalstärke über einen Zeitraum an.
     */
    float getRMSLeft() const;

    /**
     * @brief Liefert den RMS-Wert des rechten Kanals
     * @return RMS-Wert als skalierter Float-Wert
     */
    float getRMSRight() const;

    /**
     * @brief Liefert den Peak-Wert des linken Kanals
     * @return Peak-Wert als skalierter Float-Wert
     *
     * Der Peak-Wert repräsentiert die maximale Amplitude im analysierten Zeitfenster.
     */
    float getPeakLeft() const;

    /**
     * @brief Liefert den Peak-Wert des rechten Kanals
     * @return Peak-Wert als skalierter Float-Wert
     */
    float getPeakRight() const;

    /**
     * @brief Prüft, ob das aktuelle Signal als Mono erkannt wurde
     * @return true wenn Mono-Signal erkannt, false bei Stereo
     *
     * Die Mono-Erkennung basiert auf einem Toleranz-Algorithmus, der kleine Unterschiede zwischen den Kanälen ignoriert.
     */
    bool isMono() const;

    /**
     * @brief Liefert die Anzahl der gültigen Samples im Puffer
     * @return Anzahl der Samples pro Kanal
     */
    uint16_t getValidSamplesCount() const;

    /**
     * @brief Liefert die Abtastrate des bereitstehenden Sample-Blocks
     * @return Abtastrate in Hz, 0 wenn unbekannt
     * @note Nach isAudioDataReady() und vor consumeAudioData() zusammen mit den Samples lesen.
     */
    uint32_t getSampleRate() const;

    /**
     * @brief Liefert Zeiger auf den Sample-Puffer des linken Kanals
     * @return Konstanter Zeiger auf den internen Sample-Puffer
     *
     * @warning Der Zeiger ist nur gültig, solange isAudioDataReady() true zurückgibt und consumeAudioData() noch nicht aufgerufen wurde.
     */
    const int16_t *getSampleBufferLeft() const;

    /**
     * @brief Liefert Zeiger auf den Sample-Puffer des rechten Kanals
     * @return Konstanter Zeiger auf den internen Sample-Puffer
     *
     * @warning Der Zeiger ist nur gültig, solange isAudioDataReady() true zurückgibt und consumeAudioData() noch nicht aufgerufen wurde.
     */
    const int16_t *getSampleBufferRight() const;

    /**
     * @brief Markiert die aktuellen Audio-Daten als verarbeitet
     *
     * Diese Funktion muss nach der Verarbeitung der Audio-Daten aufgerufen werden, um den AudioManager für neue Daten freizugeben.
     *
     * @note Nach dem Aufruf sind die Sample-Puffer-Zeiger ungültig
     */
    void consumeAudioData();

private:
    /** @brief Quellwechsel für den Audioworker, ohne start bedeutet er Stopp. */
    struct Command
    {
        char url[320];               ///< Stream-URL oder Dateipfad unter /sdcard/
        int stationIndex;            ///< Senderindex, -1 bei Dateien
        uint32_t generation, requestId; ///< Fortlaufende Nummer des Befehls (neuere überholen ältere) und Wiedergabekennung
        bool start, file;            ///< Quelle starten statt stoppen, Quelle ist eine Datei
    };

    /** @brief Pause- oder Suchbefehl für die laufende Datei. */
    struct Control
    {
        uint32_t generation, requestId, seconds; ///< Gültige Quellnummer, Befehlskennung, Zielsekunde bei seek
        bool seek, paused;                       ///< Springen statt pausieren, gewünschter Pausezustand
    };

    QueueHandle_t m_controls = nullptr, m_covers = nullptr; ///< Pause-/Suchbefehle (Tiefe 4), letzte Bildankündigung (Tiefe 1)
    std::atomic<uint32_t> m_requestId{0}, m_controlId{0}, m_position{0}, m_duration{0};
    std::atomic<bool> m_file{false}, m_fileConfirmed{false}, m_commandFailed{false}, m_eof{false}, m_decoderError{false};
    std::atomic<Codec> m_codec{Codec::None}; ///< Aktiver Decoder, schreibt der Audio-Task aus den Decodermeldungen
    std::atomic<int8_t> m_balance{0};
    std::atomic<int8_t> m_tone[3]{};
    portMUX_TYPE m_tuningLock = portMUX_INITIALIZER_UNLOCKED;
    Tuning m_tuning;
    QueueHandle_t m_commands = nullptr; ///< Quellwechsel, ein neuer Befehl überschreibt einen noch nicht abgeholten
    TaskHandle_t m_task = nullptr;
    std::atomic<State> m_state{State::Unavailable};
    std::atomic<uint32_t> m_generation{0}, m_activeGeneration{0};
    std::atomic<uint32_t> m_pcmFrames{0}, m_lastPcm{0}, m_nonzeroBlocks{0};
    std::atomic<uint32_t> m_pcmGaps{0}, m_pcmGapMs{0}, m_inputBytes{0}; ///< Zahl und Summe der PCM-Lücken, Füllstand des Eingangspuffers. Nur Diagnose
    uint32_t m_blockMs = 0, m_startBlocks = 0;                         ///< Dauer des zuletzt gelieferten PCM-Blocks in ms und Zahl der Blöcke seit dem Start, nur der audio-lite-Task greift zu
    std::atomic<uint32_t> m_rate{0}, m_bitRate{0};
    std::atomic<int> m_stationIndex{-1};
    std::atomic<uint8_t> m_volume{DefaultVolume}, m_appliedVolume{0};
    std::atomic<uint8_t> m_loudnessVolume{DefaultVolume}; ///< Pegel des Drehknopfs: Bezugspegel der Loudness und bei 0 stumme Pegelanzeige, siehe setLoudnessVolume()
    std::atomic<bool> m_muted{true};
    std::atomic<bool> m_stereoOutput{false}; ///< Stereo an der Buchse statt Monomischung für den Lautsprecher
    /** @brief Verarbeitet ausschließlich im Audioworker Streambefehle und Decoder-Schleife. @param context AudioManager-Instanz mit Firmware-Lebensdauer. */
    static void radioTask(void *context);

    /**
     * @brief Struktur für ein Event in der Queue
     *
     * Enthält ausschließlich feste Textpuffer ohne Heap-Allokation.
     */
    struct QueuedEvent
    {
        uint32_t requestId;                 ///< Ursprung der Verbindung/Datei.
        AudioManagerEventType type;         ///< Event-Typ
        char text[MAX_EVENT_STRING_LENGTH]; ///< Event-Text/Payload
    };

    static constexpr uint8_t EVENT_Q_CAP = 12;            ///< Kapazität der Event-Queue
    std::atomic<uint8_t> m_qHead{0};                      ///< Schreib-Index (Producer)
    std::atomic<uint8_t> m_qTail{0};                      ///< Lese-Index (Consumer)
    QueuedEvent m_evtQ[EVENT_Q_CAP];                      ///< Event-Queue-Puffer
    portMUX_TYPE m_evtMux = portMUX_INITIALIZER_UNLOCKED; ///< Mutex für thread-sichere Queue-Operationen

    /**
     * @brief Legt ein Text-Event in die Queue (thread-sicher)
     * @param type Event-Typ
     * @param text Event-Text (null-terminiert)
     */
    inline void enqueueEvent_(AudioManagerEventType type, const char *text);

    /**
     * @brief Prüft ob Queue leer ist (nur intern, nicht thread-sicher)
     */
    inline bool qIsEmpty_() const
    {
        return m_qHead.load(std::memory_order_relaxed) == m_qTail.load(std::memory_order_relaxed);
    }

    /**
     * @brief Prüft ob Queue voll ist (nur intern, nicht thread-sicher)
     */
    inline bool qIsFull_() const
    {
        return (m_qHead.load(std::memory_order_relaxed) + 1) % EVENT_Q_CAP == m_qTail.load(std::memory_order_relaxed);
    }

    // Metadaten-Parsing

    AudioMetadata m_metadata; ///< Geparste Metadaten aus ID3-Tags

    /**
     * @brief Parst ID3-Daten und extrahiert Metadaten
     * @param id3String Der rohe ID3-String von der Audio-Library
     * @return true wenn Metadaten gefunden wurden, false sonst
     */
    bool parseId3Metadata(const char *id3String);

    /**
     * @brief Case-insensitive String-Vergleich am Anfang
     * @param str Der zu prüfende String
     * @param prefix Der Präfix nach dem gesucht wird
     * @return true wenn str mit prefix beginnt (case-insensitive)
     */
    bool startsWithIgnoreCase(const char *str, const char *prefix) const;

    /**
     * @brief Findet ein Tag-Muster im String
     * @param str Der zu durchsuchende String
     * @param patterns Array von Suchmustern
     * @param patternCount Anzahl der Suchmuster
     * @return Zeiger auf den Wert nach dem Tag, oder nullptr wenn nicht gefunden
     */
    const char *findTagValue(const char *str, const char **patterns, int patternCount) const;

    /**
     * @brief Prüft ob Metadaten-String gültig ist
     * @param str Der zu prüfende String
     * @return true wenn String gültig erscheint, false wenn verstümmelt
     */
    bool isValidMetadataString(const char *str) const;

    /**
     * @brief Bereinigt ID3-Strings und erkennt UTF-16
     * @param input Eingabe-String (möglicherweise verstümmelt)
     * @param output Ausgabepuffer für den bereinigten String
     * @param maxLen Größe des Ausgabepuffers
     * @return true wenn String gültig, false wenn leer/ungültig
     */
    bool cleanId3String(const char *input, char *output, size_t maxLen);

    // Event-Handler (werden vom zentralen Callback aufgerufen und reihen den Text als Event ein)

    /** @brief Verarbeitet allgemeine Audio-Informationen. */
    void handleInfo(const Audio::msg_t &msg);
    /** @brief Verarbeitet ID3-Tag-Daten und reiht erkannte Interpreten, Titel und Alben als Events ein. */
    void handleId3Data(const Audio::msg_t &msg);
    /** @brief Behandelt das Ende einer Audio-Datei oder eines Streams und merkt es für den Worker vor. */
    void handleEof(const Audio::msg_t &msg);
    /** @brief Verarbeitet den Sendernamen. */
    void handleStationName(const Audio::msg_t &msg);
    /** @brief Verarbeitet die ICY-Stream-Beschreibung. */
    void handleIcyDescription(const Audio::msg_t &msg);
    /** @brief Verarbeitet den Stream-Titel. */
    void handleStreamTitle(const Audio::msg_t &msg);
    /** @brief Verarbeitet die Bitrate-Meldung. */
    void handleBitrate(const Audio::msg_t &msg);
    /** @brief Verarbeitet die ICY-URL. */
    void handleIcyUrl(const Audio::msg_t &msg);
    /** @brief Verarbeitet die ICY-Logo-Meldung. */
    void handleIcyLogo(const Audio::msg_t &msg);
    /** @brief Verarbeitet die Meldung über den zuletzt verbundenen Host. */
    void handleLastHost(const Audio::msg_t &msg);
    /** @brief Verarbeitet Liedtext-Daten. */
    void handleLyrics(const Audio::msg_t &msg);

    // Audio-Sample-Verarbeitung

    Audio *m_audio = nullptr;  ///< Audio-Quelle für die Abtastrate im Sample-Callback
    uint32_t m_sampleRate = 0; ///< Abtastrate des gepufferten Sample-Blocks in Hz

    int16_t m_sampleBufferLeft[2048];  ///< Puffer für Audio-Samples des linken Kanals
    int16_t m_sampleBufferRight[2048]; ///< Puffer für Audio-Samples des rechten Kanals

    float m_rmsLeft;   ///< RMS-Wert des linken Kanals (skaliert)
    float m_rmsRight;  ///< RMS-Wert des rechten Kanals (skaliert)
    float m_peakLeft;  ///< Peak-Wert des linken Kanals (skaliert)
    float m_peakRight; ///< Peak-Wert des rechten Kanals (skaliert)

    bool m_monoSignal;          ///< Flag für Mono-Signal-Erkennung
    uint16_t m_numValidSamples; ///< Anzahl der gültigen Samples in den Puffern

    /**
     * @brief SPSC-Handshake-Flag für Audio-Daten (Dual-Core-Safe)
     *
     * Einziges Synchronisationssignal zwischen Producer (PCM-Callback) und Consumer (UI-Loop):
     * - false: Producer darf schreiben, Consumer hat nichts zu lesen
     * - true:  Producer hat geschrieben, Consumer darf lesen
     *
     * acquire/release garantiert, dass alle Daten-Schreiboperationen vor dem store(true) für den Consumer nach dem load() sichtbar sind und umgekehrt.
     */
    std::atomic<bool> m_dataReady{false};

    static constexpr float NORM_FACTOR = 1.0f / 32767.0f;    ///< Normalisierungs-Faktor für 16-bit Samples
    static constexpr float RMS_SCALE_FACTOR = NORM_FACTOR;   ///< 1,0 entspricht Vollausschlag (0 dBFS)
    static constexpr float PEAK_SCALE_FACTOR = NORM_FACTOR;  ///< 1,0 entspricht Vollausschlag (0 dBFS)
    static constexpr float MONO_THRESHOLD_FACTOR = 0.99f;    ///< Schwellenwert für Mono-Erkennung
};

/**
 * @brief Globale AudioManager-Instanz
 *
 * Die Zentrale verwendet die Auftrags- und Statusschnittstellen, Decoder und I2S gehören ausschließlich dem Audioworker.
 */
extern AudioManager g_audioManager;

/**
 * @brief Callback für Audio-Sample-Verarbeitung
 * @param outBuff Zeiger auf den Audio-Sample-Puffer
 * @param validSamples Anzahl der gültigen Samples
 * @param continueI2S Zeiger auf I2S-Fortsetzungs-Flag
 *
 * Wird von der Audio-Library aufgerufen und leitet an den AudioManager weiter.
 */
void audio_visualizer_process_i2s(int32_t *outBuff, int16_t validSamples, bool *continueI2S);

#endif // AUDIO_MANAGER_H
