#ifndef AUDIO_LITE_DECODER_H
#define AUDIO_LITE_DECODER_H

/**
 * @file Decoder.h
 * @brief Gemeinsame Schnittstelle aller Decoder
 *
 * Jeder unterstützte Codec stellt eine Implementierung dieser Schnittstelle bereit. `Audio` kennt nur diese Schnittstelle und erzeugt die passende Implementierung, sobald das Format der Quelle feststeht.
 */

#include <cstdint>
#include <vector>

class Audio;

// ===================================================================
// Decoder
// ===================================================================

/**
 * @brief Abstrakte Schnittstelle eines Audio-Decoders
 *
 * Eine Implementierung wandelt kodierte Rahmen in PCM-Samples. Der übliche Ablauf ist `init()`, dann je Rahmen `findSyncWord()` gefolgt von `decode()`, und am Ende `clear()`.
 *
 * Implementierungen halten eine Referenz auf die `Audio`-Instanz, zu der sie gehören, und melden darüber Ereignisse. Deshalb gibt es keinen Standardkonstruktor: ein Decoder ohne zugehörige `Audio`-Instanz ist nicht sinnvoll.
 */
class Decoder
{
public:
    virtual ~Decoder() = default;

    Decoder(const Decoder &) = delete;
    Decoder &operator=(const Decoder &) = delete;
    Decoder(Decoder &&) = delete;
    Decoder &operator=(Decoder &&) = delete;

    // ===================================================================
    // Lebenszyklus
    // ===================================================================

    /**
     * @brief Allokiert die Arbeitspuffer des Decoders
     * @return true bei Erfolg
     *
     * Muss vor dem ersten `decode()` laufen.
     */
    virtual bool init() = 0;

    /**
     * @brief Gibt die Arbeitspuffer wieder frei
     */
    virtual void clear() = 0;

    /**
     * @brief Setzt den Decoder auf den Anfangszustand zurück
     *
     * Die Arbeitspuffer bleiben allokiert. Wird bei einem Sprung in der Quelle benutzt, damit der Decoder nicht auf halben Rahmen aufsetzt.
     */
    virtual void reset() = 0;

    /**
     * @brief Meldet, ob der Decoder betriebsbereit ist
     * @return true wenn `init()` erfolgreich war und die Puffer stehen
     */
    virtual bool isValid() = 0;

    // ===================================================================
    // Rahmensuche und Dekodierung
    // ===================================================================

    /**
     * @brief Sucht den Beginn des nächsten Rahmens
     * @param buf Zu durchsuchender Puffer
     * @param nBytes Anzahl der Bytes im Puffer
     * @return Abstand des Rahmenanfangs vom Pufferanfang, oder ein negativer Wert wenn kein Rahmen gefunden wurde
     *
     * Der Aufrufer verwirft die Bytes vor dem gefundenen Rahmen.
     */
    virtual int32_t findSyncWord(uint8_t *buf, int32_t nBytes) = 0;

    /**
     * @brief Dekodiert einen Rahmen
     * @param inbuf Eingangsdaten, beginnend am Rahmenanfang
     * @param bytesLeft Zeigt auf die Anzahl verfügbarer Bytes und wird um die verbrauchten verringert
     * @param outbuf1 Zielpuffer für die PCM-Samples
     * @return 0 bei Erfolg, sonst ein decoderspezifischer Fehlercode
     *
     * Wie viele Samples geschrieben wurden, meldet anschließend `getOutputSamples()`.
     */
    virtual int32_t decode(uint8_t *inbuf, int32_t *bytesLeft, int32_t *outbuf1) = 0;

    /**
     * @brief Anzahl der beim letzten `decode()` erzeugten Samples
     * @return Anzahl der Samples über alle Kanäle
     */
    virtual uint32_t getOutputSamples() = 0;

    // ===================================================================
    // Formatangaben
    // ===================================================================

    /**
     * @brief Anzahl der Kanäle
     * @return 1 für Mono, 2 für Stereo
     */
    virtual uint8_t getChannels() = 0;

    /**
     * @brief Abtastrate der Quelle
     * @return Abtastrate in Hertz
     */
    virtual uint32_t getSampleRate() = 0;

    /**
     * @brief Auflösung der Quelle
     * @return Bits je Sample
     */
    virtual uint8_t getBitsPerSample() = 0;

    /**
     * @brief Bitrate laut Decoder
     * @return Bitrate in Bit pro Sekunde
     *
     * Gehört zur Schnittstelle, wird von `Audio` aber nicht abgefragt: `Audio::getBitRate()` meldet die selbst ermittelte nominale beziehungsweise mittlere Bitrate. Wer hier einen Wert sucht, schaut an der falschen Stelle.
     */
    virtual uint32_t getBitRate() = 0;

    /**
     * @brief Abstand der ersten Audiodaten vom Anfang der Quelle
     * @return Abstand in Bytes
     *
     * Überspringt Kopfdaten und Metadaten, etwa einen ID3-Block.
     */
    virtual uint32_t getAudioDataStart() = 0;

    /**
     * @brief Gesamtdauer laut Decoder
     * @return Dauer in Sekunden, 0 wenn nicht bestimmbar
     */
    virtual uint32_t getAudioFileDuration() = 0;

    /**
     * @brief Übergibt Formatangaben, die nicht im Datenstrom selbst stehen
     * @param param1 Anzahl der Kanäle, bei AAC die Kanalkonfiguration
     * @param param2 Abtastrate in Hertz
     * @param param3 Bits je Sample, bei AAC ungenutzt
     * @param param4 Gesamtzahl der Samples, bei AAC der Object Type
     * @param param5 Größe der Audiodaten in Bytes, bei AAC ungenutzt
     *
     * Nötig für Formate, deren Rahmen keine vollständige Formatbeschreibung tragen. Die Angaben stammen dann aus dem Container, etwa dem WAV-Kopf oder den M4A-Boxen. Die Bedeutung der letzten drei Parameter hängt vom Decoder ab.
     */
    virtual void setRawBlockParams(uint8_t param1, uint32_t param2, uint8_t param3, uint32_t param4, uint32_t param5) = 0;

    // ===================================================================
    // Metadaten
    // ===================================================================

    /**
     * @brief Zuletzt im Datenstrom gefundener Titel
     * @return Titeltext, oder nullptr wenn keiner vorliegt
     *
     * @warning Der Zeiger gehört dem Decoder und gilt nur bis zum nächsten `decode()`.
     */
    virtual const char *getStreamTitle() = 0;

    /**
     * @brief Positionen eingebetteter Bilder
     * @return Abwechselnd Position und Länge je Bild, leer wenn keines vorliegt
     */
    virtual std::vector<uint32_t> getMetadataBlockPicture() = 0;

    // ===================================================================
    // Decoderspezifische Angaben
    // ===================================================================

    /**
     * @brief Name des Decoders
     * @return Kurzbezeichnung, etwa für Protokollausgaben
     */
    virtual const char *whoIsIt() = 0;

    /**
     * @brief Erste decoderspezifische Beschreibung
     * @return Beschreibungstext, oder nullptr wenn der Decoder nichts zu melden hat
     *
     * Wird unverändert als `evt_info` an den Host weitergereicht, etwa die MPEG-Version bei MP3 oder die Betriebsart bei Opus. Der Aufrufer muss auf nullptr prüfen, mehrere Decoder liefern das.
     */
    virtual const char *arg1() = 0;

    /**
     * @brief Zweite decoderspezifische Beschreibung
     * @return Beschreibungstext, oder nullptr wenn der Decoder nichts zu melden hat
     *
     * Bei AAC die erkannte Formatvariante.
     */
    virtual const char *arg2() = 0;

    /**
     * @brief Erster decoderspezifischer Zahlenwert
     * @return Wert, 0 wenn der Decoder nichts zu melden hat
     *
     * Bei AAC und Opus zeigt ein Wert ungleich 0 Parametric Stereo an.
     */
    virtual int32_t val1() = 0;

    /**
     * @brief Zweiter decoderspezifischer Zahlenwert
     * @return Wert, 0 wenn der Decoder nichts zu melden hat
     *
     * Bei AAC zeigt ein Wert ungleich 0 Spectral Band Replication an.
     */
    virtual int32_t val2() = 0;

protected:
    /**
     * @brief Konstruktor
     * @param audioRef Die `Audio`-Instanz, zu der dieser Decoder gehört
     */
    explicit Decoder(Audio &audioRef) : audio(audioRef) {}

    Audio &audio; ///< Zugehörige Audio-Instanz, für Ereignismeldungen der Implementierungen

private:
    Decoder() = delete;
};

#endif // AUDIO_LITE_DECODER_H
