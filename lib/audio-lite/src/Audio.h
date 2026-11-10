#ifndef AUDIO_LITE_AUDIO_H
#define AUDIO_LITE_AUDIO_H

/**
 * @file Audio.h
 * @brief Öffentliche Schnittstelle von audio-lite
 *
 * Arduino-freier Ersatz für ESP32-audioI2S 3.4.7. Die öffentliche Oberfläche enthält ausschließlich Methoden, die der Host tatsächlich aufruft. Namen, Signaturen und Aufrufkonventionen entsprechen dem Upstream, damit ein bestehender Host ohne Umbau dagegen übersetzt. Die einzige bewusste Abweichung ist `connecttoFS()`, das keinen `fs::FS`-Parameter mehr trägt.
 */

#include "AudioConfig.h"
#include "AudioBuffer.h"
#include "Decoder.h"
#include "audiolib_structs.hpp"
#include "psram_unique_ptr.hpp"
#include "NetSocket.h"
#include "TcpSocket.h"
#include "TlsSocket.h"

#include <driver/i2s_std.h>
#include <esp_dsp.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <esp_log.h>

#include <array>
#include <atomic>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

inline constexpr const char *AUDIO_LOG_TAG = "audio-lite"; ///< Log-Kennung der Library
class NetSocket;

// ===================================================================
// Audio
// ===================================================================

/**
 * @brief Audio-Wiedergabe von HTTP-Streams und lokalen Dateien über I2S
 *
 * Eine Instanz besitzt genau einen I2S-Kanal, einen Eingangspuffer, eine Byte-Quelle und einen Decoder. Die Klasse ist nicht kopierbar und nicht verschiebbar.
 *
 * Die Instanz gehört genau einem Task. Alle öffentlichen Methoden werden aus diesem Task aufgerufen, insbesondere `loop()`, das den gesamten Transport antreibt. Die Klasse ist nicht threadsicher und synchronisiert nichts. Ein Aufruf aus einem zweiten Task ist ein Fehler.
 */
class Audio
{
public:
    // ===================================================================
    // Konstruktion
    // ===================================================================

    /**
     * @brief Konstruktor
     * @param i2sPort Zu verwendender I2S-Port
     *
     * Legt nur die Instanz an. Der I2S-Kanal wird erst durch `setPinout()` konfiguriert.
     */
    explicit Audio(uint8_t i2sPort = I2S_NUM_0);

    /**
     * @brief Destruktor
     *
     * Beendet eine laufende Wiedergabe, gibt Decoder und Puffer frei und schließt den I2S-Kanal, in dieser Reihenfolge.
     */
    ~Audio();

    Audio(const Audio &) = delete;
    Audio &operator=(const Audio &) = delete;
    Audio(Audio &&) = delete;
    Audio &operator=(Audio &&) = delete;

    // ===================================================================
    // Events
    // ===================================================================

    /**
     * @brief Typ eines Ereignisses, das die Library an den Host meldet
     *
     * Die Reihenfolge und die Namen entsprechen dem Upstream und dürfen nicht verändert werden, auch wenn der Host nicht jeden Wert auswertet.
     */
    typedef enum
    {
        evt_info = 0,      ///< Allgemeine Betriebsmeldung
        evt_id3data,       ///< Rohdaten eines ID3-Tags
        evt_eof,           ///< Ende der Datei oder des Streams
        evt_name,          ///< Stationsname
        evt_icydescription,///< ICY-Beschreibung des Streams
        evt_streamtitle,   ///< Aktueller Titel im Stream
        evt_bitrate,       ///< Bitrate, numerisch zusätzlich in `msg_t::arg1`
        evt_icyurl,        ///< ICY-URL der Station
        evt_icylogo,       ///< URL des Stationslogos
        evt_genre,         ///< Genre laut ICY-Header
        evt_lasthost,      ///< Zuletzt verbundener Host
        evt_image,         ///< Cover-Bild, Positionen in `msg_t::vec`
        evt_lyrics,        ///< Liedtext
        evt_log,           ///< Log-Meldung der Library
    } event_t;

    /**
     * @brief Nutzlast eines Ereignisses
     *
     * Alle Felder werden von der Library gefüllt, auch wenn ein bestimmter Host nur einen Teil davon liest. Bei `evt_bitrate` steht der bereits geparste Zahlenwert in `arg1`, der Host muss den Text nicht erneut zerlegen. Bei `evt_image` enthält `vec` abwechselnd Position und Länge der eingebetteten Bilder.
     *
     * @warning `msg` und `s` zeigen auf Puffer der Library und sind nur für die Dauer des Callback-Aufrufs gültig. Wer den Inhalt behalten will, kopiert ihn.
     */
    typedef struct _msg
    {
        const char *msg = nullptr;      ///< Ereignistext
        const char *s = nullptr;        ///< Ereignisname, bei `evt_log` die Log-Stufe
        event_t e = (event_t)0;         ///< Ereignistyp
        int32_t i2s_num = 0;            ///< I2S-Port der meldenden Instanz
        int32_t arg1 = 0;               ///< Numerischer Zusatzwert, ereignisabhängig
        int32_t arg2 = 0;               ///< Zweiter numerischer Zusatzwert, ereignisabhängig
        std::vector<uint32_t> vec = {}; ///< Positions-/Längenpaare, nur bei `evt_image`
    } msg_t;

    /**
     * @brief Callback, über den die Library alle Ereignisse meldet
     *
     * Statisch, gilt also für alle Instanzen gemeinsam. Wird der Callback nicht gesetzt, gehen die Ereignisse verloren.
     *
     * @note Aufrufkontext: Ereignisse werden gesammelt und beim nächsten `loop()` abgearbeitet, der Callback läuft also im Task des Hosts und darf blockieren, ohne die Tonausgabe zu stören. Einzige Ausnahme ist `evt_log`, das unmittelbar an der Fundstelle gemeldet wird und deshalb aus beliebigem Kontext kommen kann.
     */
    inline static std::function<void(msg_t i)> audio_info_callback;

    // ===================================================================
    // Konfiguration
    // ===================================================================

    /**
     * @brief Konfiguriert die I2S-Anschlüsse und öffnet den Kanal
     * @param BCLK Bit-Clock
     * @param LRC Wortauswahl, Left/Right Clock
     * @param DOUT Datenausgang
     * @param MCLK Master-Clock, wird nur von wenigen DACs gebraucht
     * @return true bei Erfolg
     *
     * Muss vor der ersten Wiedergabe aufgerufen werden. Die Puffergeometrie stammt zu diesem Zeitpunkt aus `settings.DMA_DESC_NUM` und `settings.DMA_FRAME_NUM`, spätere Änderungen daran wirken erst beim nächsten Aufruf.
     *
     * @note Aufrufkontext: Task des Hosts, vor dem ersten `loop()`.
     */
    bool setPinout(uint8_t BCLK, uint8_t LRC, uint8_t DOUT, int8_t MCLK = I2S_GPIO_UNUSED);

    /**
     * @brief Setzt die Verbindungs-Zeitgrenzen
     * @param timeout_ms Zeitgrenze für unverschlüsselte Verbindungen in Millisekunden
     * @param timeout_ms_ssl Zeitgrenze für verschlüsselte Verbindungen in Millisekunden
     *
     * Der Wert 0 lässt die jeweilige Zeitgrenze unverändert. Welcher der beiden Werte gilt, entscheidet sich beim Verbindungsaufbau anhand des URL-Schemas.
     *
     * @note Aufrufkontext: Task des Hosts.
     */
    void setConnectionTimeout(uint16_t timeout_ms, uint16_t timeout_ms_ssl);

    /**
     * @brief Laufzeit-Einstellungen
     *
     * Die Feldnamen entsprechen dem Upstream, einschließlich der dortigen Schreibweise `BUFFER_TRESHOLD_HLS`. `VU_LEVEL` und `SPECTRUM` stehen fest auf false, der Host fragt beides nicht ab. Änderungen an den `FREQ_*`-Feldern wirken beim nächsten `setTone()`, Änderungen an den `DMA_*`-Feldern erst beim nächsten `setPinout()`.
     */
    struct audioSettings
    {
        uint16_t DMA_DESC_NUM = 32;                ///< Anzahl der I2S-DMA-Puffer
        uint16_t DMA_FRAME_NUM = 256;              ///< Frames je DMA-Puffer
        uint16_t FREQ_LS_HZ = 500;                 ///< Eckfrequenz des Low-Shelf-Filters
        uint16_t FREQ_PEAK_HZ = 1800;              ///< Mittenfrequenz des Peaking-EQ
        uint16_t FREQ_HS_HZ = 6000;                ///< Eckfrequenz des High-Shelf-Filters
        float QUALITY_SLOPE = 0.707f;              ///< Güte aller Shelf-Filter
        uint16_t PEAK_HOLD_SAMPLES = 2000;         ///< VU-Anzeige, Haltezeit der Spitze
        uint8_t PEAK_RELEASE = 1;                  ///< VU-Anzeige, Rückfallgeschwindigkeit
        bool VU_LEVEL = false;                     ///< VU-Anzeige, aus: der Host wertet sie nicht aus
        bool SPECTRUM = false;                     ///< Spektrum, aus: der Host rechnet seine FFT selbst
        bool IIR_FILTER = true;                    ///< Klangregelung aktiv
        bool VOLUME_CONTROL = true;                ///< Lautstärke- und Balanceregelung aktiv
        float VOL_FADING_SPEED = 50.0f;            ///< Überblendung der Lautstärke, 1.0 schnell bis 100.0 langsam
        uint32_t BUFFER_TRESHOLD_HLS = UINT16_MAX; ///< Füllstand, ab dem ein HLS-TS-Stream startet und nachlädt
    } settings;

    // ===================================================================
    // Verbindung und Quelle
    // ===================================================================

    /**
     * @brief Verbindet mit einem HTTP-Stream
     * @param host Vollständige URL, beginnend mit `http://`
     * @param user Benutzername für Basic-Authentifizierung, oder nullptr
     * @param pwd Passwort für Basic-Authentifizierung, oder nullptr
     * @return true wenn die Verbindung aufgebaut wurde
     *
     * Erkennt anhand der Antwort selbst, ob eine Playlist (M3U, PLS, M3U8) oder direkt ein Audio-Stream geliefert wird, und löst Playlists auf. Ein bereits laufender Stream wird zuvor beendet.
     *
     * `https://`-URLs laufen über eine TLS-Verbindung. Das Serverzertifikat wird gegen das im Framework eingebaute Zertifikatsbündel geprüft, ein Fehlschlag der Prüfung lässt den Aufruf mit `false` zurückkehren.
     *
     * @note Aufrufkontext: Task des Hosts. Der Rückgabewert meldet nur den Verbindungsaufbau. Ob tatsächlich Ton kommt, zeigt sich erst über die Ereignisse.
     */
    bool connecttohost(const char *host, const char *user = nullptr, const char *pwd = nullptr);

    /**
     * @brief Spielt eine lokale Datei ab
     * @param path Absoluter POSIX-Pfad im VFS, zum Beispiel `/sd/musik/titel.mp3`
     * @return true wenn die Datei geöffnet und ein Decoder gefunden wurde
     *
     * Anders als im Upstream nimmt die Methode **kein** Dateisystem-Objekt mehr entgegen, sondern öffnet den Pfad direkt über das VFS. Ein Host, der die Karte über Arduinos `SD.begin()` einhängt, findet sie unter `/sd` und muss dieses Präfix voranstellen.
     *
     * @note Aufrufkontext: Task des Hosts.
     */
    bool connecttoFS(const char *path);

    // ===================================================================
    // Wiedergabe
    // ===================================================================

    /**
     * @brief Treibt Transport, Decoder und Ereignis-Zustellung an
     *
     * Muss regelmäßig aufgerufen werden, solange die Instanz existiert, nicht nur während der Wiedergabe. Bleibt der Aufruf aus, verstummt der Ton und es werden keine Ereignisse mehr zugestellt.
     *
     * @note Aufrufkontext: Task des Hosts. Der Ereignis-Callback wird aus diesem Aufruf heraus ausgeführt.
     */
    void loop();

    /**
     * @brief Beendet die laufende Wiedergabe
     * @return Position in der Datei, an der gestoppt wurde, oder 0 bei einem Stream
     *
     * @note Aufrufkontext: Task des Hosts.
     */
    uint32_t stopSong();

    /**
     * @brief Schaltet zwischen Pause und Wiedergabe um
     * @return Zustand nach dem Umschalten, true wenn pausiert
     *
     * @note Aufrufkontext: Task des Hosts.
     */
    bool pauseResume();

    /**
     * @brief Meldet, ob gerade eine Quelle geöffnet ist
     * @return true solange eine Datei oder ein Stream läuft
     *
     * Bleibt auch im pausierten Zustand true. Wer Pause von Stillstand unterscheiden muss, führt den Pausenzustand selbst mit.
     *
     * @note Aufrufkontext: Task des Hosts.
     */
    bool isRunning();

    // ===================================================================
    // Lautstärke und Klang
    // ===================================================================

    /**
     * @brief Setzt die Lautstärke
     * @param vol Wert von 0 bis zu der mit `setVolumeSteps()` gesetzten Stufenzahl
     * @param curve Kennlinie, 0 für linear, 1 für die gehörrichtige Kennlinie
     *
     * @note Aufrufkontext: Task des Hosts.
     */
    void setVolume(uint8_t vol, uint8_t curve = 0);

    /**
     * @brief Legt fest, in wie viele Stufen der Lautstärkebereich geteilt wird
     * @param steps Anzahl der Stufen
     *
     * Muss vor dem ersten `setVolume()` gesetzt werden, sonst gilt der Vorgabewert.
     *
     * @note Aufrufkontext: Task des Hosts.
     */
    void setVolumeSteps(uint8_t steps);

    /**
     * @brief Verschiebt die Balance zwischen den Kanälen
     * @param balance Wert von -16.0 (ganz links) über 0.0 (Mitte) bis +16.0 (ganz rechts), in dB
     *
     * Werte außerhalb des Bereichs werden begrenzt.
     *
     * @note Aufrufkontext: Task des Hosts.
     */
    void setBalance(float balance = 0.0f);

    /**
     * @brief Setzt die drei Verstärkungen der Klangregelung
     * @param gainLowPass Verstärkung des Low-Shelf-Filters in dB
     * @param gainBandPass Verstärkung des Peaking-EQ in dB
     * @param gainHighPass Verstärkung des High-Shelf-Filters in dB
     *
     * Die zugehörigen Eckfrequenzen stehen in `settings`. Wirkungslos, solange `settings.IIR_FILTER` false ist.
     *
     * @note Aufrufkontext: Task des Hosts.
     */
    void setTone(float gainLowPass, float gainBandPass, float gainHighPass);

    /**
     * @brief Setzt die Basisbreite des Stereobilds
     * @param width 0 für Mono, 100 für unverändert, bis maximal 200 für verbreitert
     *
     * Werte über 200 werden begrenzt.
     *
     * @note Aufrufkontext: Task des Hosts.
     */
    void setStereoWidth(uint8_t width);

    /**
     * @brief Erzwingt die Zusammenmischung auf Mono
     * @param m true mischt beide Kanäle zusammen
     *
     * @note Aufrufkontext: Task des Hosts.
     */
    void forceMono(bool m);

    // ===================================================================
    // Statusabfrage
    // ===================================================================

    /**
     * @brief Größe der laufenden Datei in Bytes
     * @return Dateigröße, oder 0 bei einem Stream
     *
     * @note Aufrufkontext: Task des Hosts.
     */
    uint32_t getFileSize();

    /**
     * @brief Aktuelle Leseposition in der laufenden Datei
     * @return Position in Bytes, oder 0 bei einem Stream
     *
     * Die Position bezieht sich auf die gelesenen Rohdaten, nicht auf den ausgegebenen Ton. Wegen der Pufferung läuft sie dem Hörbaren voraus.
     *
     * @note Aufrufkontext: Task des Hosts.
     */
    uint32_t getAudioFilePosition();

    /**
     * @brief Bereits abgespielte Zeit
     * @return Zeit in Sekunden seit Beginn der Datei, oder 0 bei einem Stream
     *
     * @note Aufrufkontext: Task des Hosts.
     */
    uint32_t getAudioCurrentTime();

    /**
     * @brief Gesamtdauer der laufenden Datei
     * @return Dauer in Sekunden, 0 solange sie noch nicht bestimmt werden konnte
     *
     * Bei Formaten ohne Längenangabe im Kopf wird die Dauer aus Dateigröße und Bitrate geschätzt und kann sich nachträglich noch ändern.
     *
     * @note Aufrufkontext: Task des Hosts.
     */
    uint32_t getAudioFileDuration();

    /**
     * @brief Abtastrate der dekodierten Quelle
     * @return Abtastrate in Hz
     *
     * Entspricht den Samples vor einem optionalen Ausgangs-Resampling.
     *
     * @note Im Sample-Callback lesen, um die Abtastrate zusammen mit dem zugehörigen Sample-Block zu übernehmen.
     */
    uint32_t getSampleRate();

    /**
     * @brief Bitrate der laufenden Quelle
     * @return Bitrate in Bit pro Sekunde, 0 solange sie unbekannt ist
     *
     * Bei Dateien mit variabler Bitrate ist der Wert ein Mittelwert.
     *
     * @note Aufrufkontext: Task des Hosts.
     */
    uint32_t getBitRate();

    /**
     * @brief Füllstand des Eingangspuffers
     * @return Anzahl der noch nicht dekodierten Bytes der laufenden Quelle
     *
     * @note Aufrufkontext: Task des Hosts.
     */
    uint32_t inBufferFilled();

    /**
     * @brief Spult in der laufenden Datei um die angegebenen Sekunden vor oder zurück
     * @param sec Sprungweite in Sekunden, negativ spult zurück
     * @return true, wenn der Sprung angenommen wurde
     *
     * Vor dem Dateianfang wird auf den Anfang begrenzt, hinter dem Dateiende stoppt die Wiedergabe. Bei MP3 mit Xing-Sprungtabelle ist die Zielposition exakt, sonst wird sie über die mittlere Bitrate genähert. Nur bei laufender Wiedergabe möglich, im Pause-Zustand wird der Aufruf abgelehnt.
     *
     * @note Aufrufkontext: Task des Hosts.
     */
    bool setTimeOffset(int sec);

  private:
    using VolumeCurveFn = std::function<float(float t)>; ///< Kennlinie der Lautstärkeregelung

    typedef enum : uint32_t {
        SR_ORIGIN = 0,
        SR_44100 = 44100,
        SR_48000 = 48000
    } OutputSR_t;

    static constexpr std::array<const char*, 14> EVENT_STR = {
        "info", "id3data", "eof", "station_name", "icy_description", "streamtitle",
        "bitrate (b/s)", "icy_url", "icy_logo", "genre", "lasthost", "cover_image",
        "lyrics", "log",
    }; ///< Ereignisnamen für msg_t::s, Reihenfolge wie event_t

    // Nicht Teil der öffentlichen Oberfläche: intern benutzt oder aus dem Upstream übernommen, damit die Maschinerie unverändert bleibt.
    audiolib::hwoe_t dismantleHost(const char* host);
    bool             getMute();
    bool             setAudioFilePosition(uint32_t pos);
    bool             setAudioPlayTime(uint16_t sec);
    const char*      getVersion();
    int32_t          getI2sPort();
    uint16_t         getVUlevel();
    uint32_t         getInBufferSize();
    uint32_t         inBufferFree();
    uint8_t          getBitsPerSample();
    uint8_t          getChannels();
    uint8_t          getVolume();
    uint8_t          getVolumeSteps();
    void             setI2sCommFmtLsb(bool commFMT);
    void             setMute(bool mute);
    void             setOutputSampleRate(OutputSR_t sr);
    void             setVolumeCurve(VolumeCurveFn curve);

    // Private Bausteine
    bool i2sConfig();
    std::unique_ptr<Decoder> createDecoder(const char* type);
    void destroyDecoder();
    bool fsRange(uint32_t range);
    void latinToUTF8(ps_ptr<char>& buff, bool UTF8check = true);
    void htmlToUTF8(char* str);
    void setDefaults(); // gibt die Puffer frei und setzt den Anfangszustand
    int32_t audioFileRead();
    int32_t audioFileRead(uint16_t timeout_ms);
    int32_t audioFileRead(uint8_t* buff, size_t len);
    int32_t audioFileRead(uint8_t* buff, size_t len, uint16_t timeout_ms);
    int32_t audioFileSeek(uint32_t position, size_t len = 0);
    uint32_t xingTimeToFilePos(uint32_t sec);
    uint32_t xingFilePosToTime(uint32_t pos);
    void syncSampleCountToFilePos();
    void initInBuff();
    bool httpPrint(const char* host);
    bool httpRange(uint32_t range, uint32_t length = UINT32_MAX);
    void processLocalFile();
    void processWebStream();
    void processWebFile();
    void processWebStreamTS();
    void processWebStreamHLS();
    void playAudioData();
    bool readPlayListData();
    const char* parsePlaylistM3U();
    const char* parsePlaylistPLS();
    const char* parsePlaylistASX();
    ps_ptr<char> parsePlaylistM3U8();
    uint16_t accomplishM3u8Url();
    int16_t prepareFirstM3u8Url(ps_ptr<char>& playlistBuff);
    ps_ptr<char> m3u8Redirection(uint8_t* codec);
    void showCodecParams();
    int findNextSync(uint8_t* data, size_t len);
    uint32_t decodeError(int8_t res, uint8_t* data, int32_t bytesDecoded);
    uint32_t decodeContinue(int8_t res, uint8_t* data, int32_t bytesDecoded, int32_t* bytesLeft);
    int sendBytes(uint8_t* data, size_t len);
    void setDecoderItems();
    void calculateAudioTime(uint16_t bytesDecoderIn, uint16_t bytesDecoderOut);
    void showID3Tag(const char* tag, const char* val);
    size_t readAudioHeader(uint32_t bytes);
    int readWavHeader(uint8_t* data, size_t len);
    int readFlacHeader(uint8_t* data, size_t len);
    int readID3Header(uint8_t* data, size_t len);
    int readM4aHeader(uint8_t* data, size_t len);
    size_t processM3u8ID3Header(uint8_t* packet);
    bool setSampleRate(uint32_t hz);
    bool setBitsPerSample(int bits);
    bool setChannels(int channels);
    uint32_t resampleI2Soutput(audiolib::resampler_t& resampler, int32_t* input, uint32_t inputSamples, int32_t* output);
    void playChunk();
    void calculateVUlevel(int32_t* sample);
    void processSpectrum();
    void gainRamp();
    void calculateVolumeLimits();
    void applyVolume(int32_t* sample);
    void showstreamtitle(char* ml);
    bool parseContentType(ps_ptr<char> ct);
    bool parseHttpResponseHeader();
    bool parseHttpRangeHeader();
    bool initializeDecoder();
    esp_err_t i2sStart();
    esp_err_t i2sStop();
    void zeroI2Sbuff();
    void reconfigI2S();
    void stereo2mono(int32_t* buff, uint16_t validSamples);
    void applyStereoWidth(int32_t* buff, uint16_t validSamples);
    void iirCalculateCoefficients();
    void updateEqHeadroom();
    void iirFilter(int32_t* iir_in);
    uint32_t streamavail() {
        return m_client ? m_client->available() : 0;
    }
    bool tsParsePacket(uint8_t* packet, uint8_t* packetStart, uint8_t* packetLength);
    uint64_t getLastGranulePosition(uint8_t codec);

    // Audio-Task: dekodiert und gibt über I2S aus
  private:
    void setAudioTaskCore(uint8_t coreID);
    uint32_t getHighWatermark();

  private:
    void startAudioTask(); // startet den Task für Dekodierung und Ausgabe
    void stopAudioTask();  // beendet den Audio-Task
    static void audioTaskWrapper(void* param);
    void audioTask();
    void performAudioTask();

    // Hilfsfunktionen
    bool readMetadata(uint32_t b, uint16_t* readedBytes, bool first = false);
    int32_t getChunkSize(uint16_t* readedBytes, bool first = false);
    bool readID3V1Tag();
    int32_t newInBuffStart(int32_t resumeFilePos);
    bool streamDetection(uint32_t bytesAvail);
    size_t streamDecodeThreshold();
    bool adtsFrameComplete(const uint8_t* data, size_t len);
    uint32_t m4aCorrectResumeFilePos();
    int32_t oggCorrectResumeFilePos();
    int32_t flacCorrectResumeFilePos();
    int32_t mp3CorrectResumeFilePos();
    int32_t wavCorrectResumeFilePos();
    uint8_t determineCodec(uint8_t presumed_codec);
    bool getInfo();
    void trim(char* str);
    bool startsWith(const char* base, const char* str);
    int indexOf(const char* base, const char* str, int startIndex = 0);
    int indexOf(const char* base, char ch, int startIndex = 0);
    int specialIndexOf(uint8_t* base, const char* str, int baselen, bool exact = false);
    int32_t min3(int32_t a, int32_t b, int32_t c);
    uint64_t bigEndian(uint8_t* base, uint8_t numBytes, uint8_t shiftLeft = 8);
    bool b64encode(const char* source, uint16_t sourceLength, char* dest);
    void vectorClearAndShrink(std::vector<ps_ptr<char>>& vec);
    void dequeClearAndShrink(std::deque<ps_ptr<char>>& deq);
    uint32_t simpleHash(const char* str);
    ps_ptr<char> urlencode(const char* str, bool spacesOnly);
    audiolib::BiquadCoeffs makeButterworthLPF_Q31(float fs);

  private:
    enum : int {
        APLL_AUTO = -1,
        APLL_ENABLE = 1,
        APLL_DISABLE = 0
    };
    enum : int {
        EXTERNAL_I2S = 0,
        INTERNAL_DAC = 1,
        INTERNAL_PDM = 2
    };
    enum : int {
        FORMAT_NONE = 0,
        FORMAT_M3U = 1,
        FORMAT_PLS = 2,
        FORMAT_ASX = 3,
        FORMAT_M3U8 = 4
    }; // Playlist-Formate
    const char* m_plsFmtStr[5] = {"NONE", "M3U", "PLS", "ASX", "M3U8"}; ///< Playlist-Format als Text
    enum : int {
        AUDIO_NONE,
        HTTP_RESPONSE_HEADER,
        HTTP_RANGE_HEADER,
        AUDIO_DATA,
        AUDIO_LOCALFILE,
        AUDIO_PLAYLISTINIT,
        AUDIO_PLAYLISTHEADER,
        AUDIO_PLAYLISTDATA
    };
    const char* m_dataModeStr[8] = {"AUDIO_NONE",      "HTTP_RESPONSE_HEADER", "HTTP_RANGE_HEADER",    "AUDIO_DATA",
                                  "AUDIO_LOCALFILE", "AUDIO_PLAYLISTINIT",   "AUDIO_PLAYLISTHEADER", "AUDIO_PLAYLISTDATA"};
    enum : int {
        FLAC_BEGIN = 0,
        FLAC_MAGIC = 1,
        FLAC_MBH = 2,
        FLAC_SINFO = 3,
        FLAC_PADDING = 4,
        FLAC_APP = 5,
        FLAC_SEEK = 6,
        FLAC_VORBIS = 7,
        FLAC_CUESHEET = 8,
        FLAC_PICTURE = 9,
        FLAC_OKAY = 100
    };
    enum : int {
        MP3_BEGIN = 0,
        MP3_ID3HEADER,
        MP3_NEXTID3,
        MP3_EXTHEADER,
        MP3_ID3FRAME,
        MP3_FRAMESIZE,
        MP3_SKIP,
        MP3_TAG,
        MP3_SYLT,
        MP3_ID3V22,
        MP3_LASTFRAMES,
        MP3_XING,
        MP3_OKAY = 100
    };
    enum : int {
        M4A_BEGIN = 0,
        M4A_FTYP = 1,
        M4A_CHK = 2,
        M4A_MOOV = 3,
        M4A_FREE = 4,
        M4A_TRAK = 5,
        M4A_MDAT = 6,
        M4A_ILST = 7,
        M4A_MP4A = 8,
        M4A_ESDS = 9,
        M4A_MDIA = 10,
        M4A_MINF = 11,
        M4A_STBL = 12,
        M4A_STSD = 13,
        M4A_UDTA = 14,
        M4A_STSZ = 15,
        M4A_META = 16,
        M4A_MDHD = 17,
        M4A_CHPL = 18,
        M4A_AMRDY = 99,
        M4A_OKAY = 100,
    };
    enum : int {
        CODEC_NONE = 0,
        CODEC_WAV = 1,
        CODEC_MP3 = 2,
        CODEC_AAC = 3,
        CODEC_M4A = 4,
        CODEC_FLAC = 5,
        CODEC_OPUS = 6,
        CODEC_VORBIS = 7,
        CODEC_OGG = 8
    };
    const char* m_codecName[10] = {"unknown", "WAV", "MP3", "AAC", "M4A", "FLAC", "OPUS", "VORBIS", "OGG"};
    enum : int {
        ST_NONE = 0,
        ST_WEBFILE = 1,
        ST_WEBSTREAM = 2
    };
    const char* m_streamTypeStr[3] = {"NONE", "WEBFILE", "WEBSTREAM"};
    typedef enum {
        LEFTCHANNEL = 0,
        RIGHTCHANNEL = 1
    } SampleIndex;
    typedef enum {
        LOWSHELF = 0,
        PEAKINGEQ = 1,
        HIFGSHELF = 2
    } FilterType;

  private:
    typedef struct _filter {
        float a0;
        float a1;
        float a2;
        float b1;
        float b2;
    } filter_t;

    typedef struct _pis_array {
        int number;
        int pids[4];
    } pid_array;

  private:
    FILE* m_audiofile = nullptr;      ///< offene lokale Datei
    ps_ptr<char> m_audioFilePath;     ///< Pfad der offenen Datei, FILE* kennt ihn nicht
    TcpSocket m_tcpSocket;            ///< unverschlüsselte Verbindung
    TlsSocket m_tlsSocket;            ///< verschlüsselte Verbindung
    NetSocket* m_client = nullptr;    ///< zeigt auf die aktive Verbindung
    AudioBuffer m_inBuff;               ///< Ringpuffer zwischen Quelle und Decoder
    std::mutex m_mutexInfo;            ///< schützt die Ereignis-Zustellung

    SemaphoreHandle_t m_mutexPlayAudioData;
    SemaphoreHandle_t m_mutexAudioTask;
    SemaphoreHandle_t m_mutexAudioTaskIsDecoding;
    TaskHandle_t m_audioTaskHandle = nullptr;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

    i2s_chan_handle_t m_i2sTxHandle = {};
    i2s_chan_config_t m_i2sChanCfg = {}; ///< Konfiguration des I2S-Kanals
    i2s_std_config_t m_i2sStdCfg = {};   ///< Konfiguration des I2S-Treibers

#pragma GCC diagnostic pop

    std::vector<ps_ptr<char>> m_playlistContent; ///< M3U8-Playlist, Zeilen aus der Antwort
    std::vector<ps_ptr<char>> m_playlistURL;     ///< Stream-URLs aus der M3U8-Playlist
    std::deque<ps_ptr<char>> m_linesWithURL;     ///< Auszug aus m_playlistContent, URL und MediaSequenceNumber
    std::vector<ps_ptr<char>> m_linesWithEXTINF; ///< Auszug aus m_playlistContent, Länge und Metadaten
    std::vector<ps_ptr<char>> m_syltLines;       ///< SYLT-Zeilentabelle
    std::vector<uint32_t> m_syltTimeStamp;       ///< SYLT-Zeittabelle

    static const uint8_t TS_PACKET_SIZE = 188; ///< Größe eines TS-Pakets in Bytes
    static const uint8_t TS_HEADER_SIZE = 4;   ///< Größe des TS-Paketkopfs in Bytes

    std::unique_ptr<Decoder> m_decoder = {};
    ps_ptr<int32_t> m_outBuff;       ///< Ausgangspuffer, links/rechts verschränkt
    ps_ptr<int32_t> m_resamplesBuff; ///< Resampler-Puffer, links/rechts verschränkt
    ps_ptr<char> m_metadataBuff;     ///< ICY-Metadaten, höchstens 16 * 256 + 1 Bytes
    ps_ptr<char> m_httpRespHdrBuff;  ///< HTTP-Antwort-Header
    ps_ptr<char> m_ibuff;            ///< Textpuffer der Ereignis-Formatierung
    ps_ptr<char> m_lastHost;         ///< zuletzt verlangte Stream-URL
    ps_ptr<char> m_currentHost;      ///< aktuelle URL, kann durch Umleitung oder Playlist wechseln
    ps_ptr<char> m_m3u8Host;
    ps_ptr<char> m_speechtxt;   ///< Text der Sprachausgabe
    ps_ptr<char> m_streamTitle; ///< zuletzt gemeldeter StreamTitle
    ps_ptr<char> m_streamURL;   ///< zuletzt gemeldete StreamURL
    ps_ptr<char> m_playlistBuff;
    VolumeCurveFn m_volumeCurve = nullptr;

    const uint16_t m_plsBuffEntryLen = 256; ///< Länge eines Eintrags in m_playlistBuff
    int m_LFcount = 0;                      ///< Zeilenumbruch-Zähler, erkennt das Header-Ende
    uint32_t m_avrBitrate = 0;             ///< mittlere Bitrate, bei VBR fortlaufend gemittelt
    uint32_t m_nominalBitrate = 0;         ///< Bitrate laut Header
    uint32_t m_audioFilePosition = 0;       ///< aktuelle Position, zählt jedes gelesene Byte
    uint32_t m_audioDataReadPtr = 0;        ///< Leseposition von playAudioData()
    uint32_t m_audioFileSize = 0;           ///< Dateigröße, lokal wie Web-Datei
    int m_readbytes = 0;                    ///< gelesene Bytes
    uint32_t m_metacount = 0;               ///< Bytes bis zum nächsten Metadaten-Block
    int m_controlCounter = 0;               ///< Schrittzähler der Header-Parser
    uint8_t m_timeoutCounter = 0;           ///< Zeitüberschreitungs-Zähler
    uint8_t m_bitsPerSample = 16;           ///< Bits je Sample
    uint8_t m_channels = 2;                 ///< Anzahl der Kanäle
    uint8_t m_playlistFormat = 0;           ///< M3U, PLS, ASX
    uint8_t m_codec = CODEC_NONE;           ///< aktiver Codec
    uint8_t m_m3u8Codec = CODEC_AAC;        ///< Codec des M3U8-Streams
    uint8_t m_expectedCodec = CODEC_NONE;   ///< aus der URL-Endung erwarteter Codec, gesetzt in connecttohost()
    uint8_t m_expectedPlsFmt = FORMAT_NONE; ///< aus der URL-Endung erwartetes Playlist-Format, gesetzt in connecttohost()
    uint8_t m_streamType = ST_NONE;         ///< Web-Datei oder Web-Stream
    uint8_t m_ID3Size = 0;                  ///< Länge des ID3-Rahmens ohne ID3-Header
    uint8_t m_audioTaskCoreId = 0;          ///< Kern, auf dem der Audio-Task läuft
    uint8_t m_m4aObjectType = 0;           ///< gesetzt in readM4aHeader()
    uint8_t m_m4aChConfig = 0;             ///< gesetzt in readM4aHeader()
    uint16_t m_m4aSampleRate = 0;          ///< gesetzt in readM4aHeader()
    int16_t m_validSamples = 0;             ///< noch auszugebende Stereo-Sample-Paare
    bool m_blockPrepared = false;           ///< Ausgabeblock ist verarbeitet, auch wenn I2S noch kein Frame übernommen hat
    int16_t m_curSample = 0;                ///< aktuelles Sample innerhalb des Ausgangspuffers
    uint16_t m_dataMode = 0;                ///< Zustand der Verarbeitungs-Zustandsmaschine
    uint16_t m_streamTitleHash = 0;         ///< Streuwert des letzten Titels, unterdrückt Wiederholungen
    uint16_t m_timeoutMs = 250;            ///< Zeitgrenze unverschlüsselter Verbindungen
    uint16_t m_timeoutMsSsl = 2700;       ///< Zeitgrenze verschlüsselter Verbindungen
    uint32_t m_metaint = 0;                 ///< Datenbytes zwischen zwei Metadaten-Blöcken
    uint32_t m_chunkcount = 0;              ///< Zähler des Chunked-Transfers
    uint32_t m_t0 = 0;                      ///< Zeitstempel für kurze Wartezeiten
    uint32_t m_bytesNotConsumed = 0;        ///< Bilder oder andere Beigaben im Datenstrom
    uint64_t m_lastGranulePosition = 0;     ///< für die Dauerberechnung bei OPUS und VORBIS
    int32_t m_resumeFilePos = -1;           ///< Rückgabewert von stopSong(), -1 bedeutet Leerlauf
    int32_t m_fileStartTime = -1;           ///< Startzeit, gesetzt in connecttoFS()
    uint16_t m_m3u8TargetDuration = 10;    ///< Zielblockdauer des M3U8-Streams in Sekunden
    uint32_t m_stszNumEntries = 0;         ///< Anzahl der Einträge im stsz-Atom
    uint32_t m_stszPosition = 0;           ///< Position des stsz-Atoms in der Datei
    uint32_t m_haveNewFilePos = 0;          ///< vom Host verlangte neue Dateiposition
    bool m_fAltUserAgent = false;        ///< alternativen User-Agent senden
    bool m_fI2sInit = false;              ///< I2S-Kanal ist konfiguriert
    bool m_fUnsync = false;                ///< Unsync-Bit aus dem ID3-Tag, wird nicht ausgewertet
    bool m_fExthdr = false;                ///< ID3-Tag hat einen erweiterten Header
    bool m_fSsl = false;                   ///< Verbindung läuft verschlüsselt
    bool m_fRunning = false;               ///< Quelle ist geöffnet
    bool m_fFirstCall = false;             ///< Startsequenz von processWebStream() und processLocalFile()
    bool m_fFirstLoop = false;             ///< Startsequenz von loop()
    bool m_fFirstPlayCall = false;         ///< Startsequenz von playAudioData()
    bool m_fID3v1TagFound = false;         ///< ID3v1-Tag gefunden
    bool m_fChunked = false;               ///< Station liefert Chunked-Transfer
    bool m_fFirstMetaByte = false;         ///< erstes Byte eines Metadaten-Blocks
    bool m_fPlaying = false;               ///< gültiger Datenstrom erkannt, Rahmen werden dekodiert
    bool m_fTts = false;                   ///< Sprachausgabe läuft
    bool m_fOgg = false;                   ///< OGG-Stream
    bool m_fForceMono = false;             ///< beide Kanäle zusammenmischen
    uint8_t m_stereoWidthPct = 100;         ///< Stereobreite in Prozent, 100 lässt das Signal unverändert
    bool m_fRtsp = false;                  ///< RTSP wird benutzt (M3U8-Stream)
    bool m_fM3u8Data = false;              ///< M3U8-Daten werden verarbeitet
    bool m_fContinue = false;              ///< nächster M3U8-Block liegt bereit
    bool m_fTs = true;                     ///< Transport-Stream
    bool m_fM4aID3DataAreRead = false;     ///< M4A-Metadaten sind bereits gelesen
    bool m_fPsramFound = false;            ///< PSRAM vorhanden, im Konstruktor ermittelt
    bool m_fTimeout = false;               ///< Zeitgrenze wurde überschritten
    bool m_fAudioTaskIsRunning = false;    ///< Audio-Task läuft
    bool m_fAllDataReceived = false;       ///< Quelle vollständig gelesen
    bool m_fStream = false;                ///< Stream ist bereit für die Ausgabe
    bool m_fRebuffering = false;           ///< Vorrat eines Web-Streams ist aufgebraucht, die Dekodierung wartet auf den Startfüllstand
    bool m_fDecodeReady = false;          ///< Daten für den Decoder liegen bereit
    bool m_fEof = false;                   ///< Ende der Datei oder des Streams
    bool m_fLockInBuffer = false;          ///< sperrt den Eingangspuffer während einer Umbaumaßnahme
    bool m_fAudioTaskIsDecoding = false;   ///< Audio-Task dekodiert gerade
    bool m_fAcceptRanges = false;          ///< Server unterstützt Range-Anfragen
    bool m_fResetM3u8Codec = true;        ///< Codec des M3U8-Streams neu bestimmen
    bool m_fConnectionClose = false;       ///< Server schließt die Verbindung, gesetzt in parseHttpResponseHeader()
    bool m_fI2sChannelEnabled = false;   ///< I2S-Kanal ist eingeschaltet
#if AUDIO_LITE_DIAG
    uint32_t m_diagDecOk = 0;               ///< erfolgreiche decode()-Aufrufe seit der letzten Diagnosezeile
    uint32_t m_diagDecFail = 0;             ///< decode()-Fehler seit der letzten Diagnosezeile
    uint32_t m_diagDecMore = 0;             ///< decode() wollte mehr Daten
    uint32_t m_diagI2sBytes = 0;            ///< an I2S übergebene Bytes
    uint32_t m_diagI2sTimeout = 0;          ///< i2s_channel_write-Timeouts
#endif
    uint32_t m_audioFileDuration = 0;       ///< Gesamtdauer in Sekunden
    uint32_t m_audioCurrentTime = 0;        ///< abgespielte Zeit in Sekunden
    uint32_t m_audioDataStart = 0;          ///< Beginn der Audiodaten in Bytes
    OutputSR_t m_outputSr = SR_ORIGIN;     ///< Abtastrate am Ausgang
    size_t m_audioDataSize = 0;             ///< Größe der Audiodaten
    size_t m_ibuffSize = 0;                 ///< Größe des Textpuffers der Ereignis-Formatierung
    size_t m_i2sBytesWritten = 0;          ///< von i2s_channel_write gemeldete Bytes, wird nicht ausgewertet

    pid_array m_pidsOfPMT;
    int16_t m_pidOfAAC;
    uint8_t m_packetBuff[TS_PACKET_SIZE];
    int16_t m_pesDataLength = 0;

    // Zustandsblöcke aus audiolib_structs.hpp
    audiolib::ID3Hdr_t m_ID3Hdr;
    audiolib::pwsHLS_t m_pwsHLS;
    audiolib::pplM3u8_t m_pplM3U8;
    audiolib::m4aHdr_t m_m4aHdr;
    audiolib::plCh_t m_plCh;
    audiolib::lVar_t m_lVar;
    audiolib::prlf_t m_prlf;
    audiolib::cat_t m_cat;
    audiolib::xing_t m_xing;
    audiolib::ifCh_t m_ifCh;
    audiolib::tspp_t m_tspp;
    audiolib::pwst_t m_pwst;
    audiolib::gchs_t m_gchs;
    audiolib::pwf_t m_pwf;
    audiolib::pad_t m_pad;
    audiolib::sbyt_t m_sbyt;
    audiolib::rmet_t m_rmet;
    audiolib::pwsts_t m_pwsst;
    audiolib::rwh_t m_rwh;
    audiolib::rflh_t m_rflh;
    audiolib::phreh_t m_phreh;
    audiolib::phrah_t m_phrah;
    audiolib::sdet_t m_sdet;
    audiolib::fnsy_t m_fnsy;
    audiolib::audioItems_t m_audioItems;
    audiolib::vu_items_t m_vuItems;
    audiolib::fft_items_t m_fftItems;
    audiolib::i2s_items_t m_i2sItems;
    audiolib::resampler_t m_resampler;
    audiolib::info_queue_t m_infoQueue;
    audiolib::icy_items_t m_icyItems;

    // ===================================================================
  public:
    template <typename... Args> static bool info(Audio& instance, event_t e, const char* fmt, Args&&... args) {
        std::lock_guard<std::mutex> lock(instance.m_mutexInfo);
        if (!fmt)
            return false;
        if (!audio_info_callback)
            return false;

        ps_ptr<char> result;
        result.assignf(fmt, std::forward<Args>(args)...);
        if (!result.get())
            return false;

        auto extract_last_number = [](std::string_view s) -> std::optional<int32_t> {
            auto is_space = [](char c) {
                return std::isspace(static_cast<unsigned char>(c));
            };
            auto is_digit = [](char c) {
                return std::isdigit(static_cast<unsigned char>(c));
            };

            auto it = s.end();
            // Leerraum am Ende überspringen
            while (it != s.begin() && is_space(*(it - 1))) {
                --it;
            }
            auto end = it;
            // Ziffern rückwärts einsammeln
            while (it != s.begin() && is_digit(*(it - 1))) {
                --it;
            }
            // optionales Vorzeichen
            if (it != s.begin()) {
                char c = *(it - 1);
                if (c == '+' || c == '-') {
                    --it;
                }
            }

            if (it == end) {
                return std::nullopt;
            }
            // vor der Zahl muss ein Leerzeichen stehen, sonst ist sie Teil eines Wortes
            if (it != s.begin() && !is_space(*(it - 1))) {
                return std::nullopt;
            }
            int32_t value{};
            auto [ptr, ec] = std::from_chars(it, end, value);

            // nur eine vollständig geparste Zahl zählt
            if (ec == std::errc{} && ptr == end) {
                return value;
            }
            return std::nullopt;
        };

        std::vector<uint32_t> v;
        v.push_back(0);
        instance.m_infoQueue.msg.emplace_front(result);
        instance.m_infoQueue.s.emplace_front(EVENT_STR[e]);
        instance.m_infoQueue.arg1.emplace_front(extract_last_number(result.c_get()).value_or(0));
        instance.m_infoQueue.arg2.emplace_front(0);
        instance.m_infoQueue.vec.emplace_front(v);
        instance.m_infoQueue.e.emplace_front((uint8_t)e);
        result.reset();
        return true;
    }

    static bool info(Audio& instance, event_t e, std::vector<uint32_t>& v) {
        if (!audio_info_callback)
            return false;
        std::lock_guard<std::mutex> lock(instance.m_mutexInfo);
        ps_ptr<char> apic;
        apic.assignf("APIC found at pos {}", v[0]);
        instance.m_infoQueue.msg.emplace_front(apic);
        instance.m_infoQueue.s.emplace_front(EVENT_STR[e]);
        instance.m_infoQueue.arg1.emplace_front(0);
        instance.m_infoQueue.arg2.emplace_front(0);
        instance.m_infoQueue.vec.emplace_front(v);
        instance.m_infoQueue.e.emplace_front((uint8_t)e);
        return true;
    }
    // ===================================================================

    template <typename... Args> static void AUDIO_LOG_IMPL(uint8_t level, const char* path, int line, const char* func, const char* fmt, Args&&... args) {

#define ANSI_ESC_RESET "\033[0m"
#define ANSI_ESC_BLACK "\033[30m"
#define ANSI_ESC_RED "\033[31m"
#define ANSI_ESC_GREEN "\033[32m"
#define ANSI_ESC_YELLOW "\033[33m"
#define ANSI_ESC_BLUE "\033[34m"
#define ANSI_ESC_MAGENTA "\033[35m"
#define ANSI_ESC_CYAN "\033[36m"
#define ANSI_ESC_WHITE "\033[37m"

        // Früh aussteigen, VOR jeder Allokation. Ohne das allokiert jeder Log-Aufruf PSRAM und formatiert Text, nur um am Ende zu verwerfen. Im Audio-Task passiert das fortlaufend. Fehler (1) und Warnungen (2) kommen immer durch, ESP_LOG filtert selbst zur Laufzeit.
        if (level >= 3) {
            const int needed = (level == 5) ? 4 : (int)level;
            if (CORE_DEBUG_LEVEL < needed)
                return;
        }

        ps_ptr<char> logStr = path;
        while (logStr.contains("/")) {
            logStr.remove_before('/', false);
        }
        logStr.appendf(":{} {}] ", line, func ? func : "");
        logStr.insert("[", 0);

        if (level == 1)
            logStr.append(ANSI_ESC_RED);
        else if (level == 2)
            logStr.append(ANSI_ESC_YELLOW);
        else if (level == 3)
            logStr.append(ANSI_ESC_GREEN);
        else if (level == 4)
            logStr.append(ANSI_ESC_CYAN);
        else
            logStr.append(ANSI_ESC_WHITE);

        int add_len = std::snprintf(nullptr, 0, fmt, std::forward<Args>(args)...);
        if (add_len > 0) {
            logStr.appendf(fmt, std::forward<Args>(args)...);
        }
        logStr.append(ANSI_ESC_RESET);

        msg_t msg;
        msg.msg = logStr.get();
        const char* tag[7] = {"", "LOGE", "LOGW", "LOGI", "LOGD", "LOGV", ""};
        msg.s = tag[level];
        msg.e = evt_log;

        // Fehler und Warnungen gehen IMMER zusätzlich auf den seriellen Kanal, per printf statt ESP_LOG: das vorgebaute Framework hat CONFIG_LOG_MAXIMUM_LEVEL=1, ESP_LOGW ist dort wegkompiliert. Ein Host, der evt_log verwirft, sähe sonst keine Fehler der Library.
        if (level == 1)
            printf("E audio-lite %s\n", logStr.c_get());
        else if (level == 2)
            printf("W audio-lite %s\n", logStr.c_get());

        if (audio_info_callback)
            audio_info_callback(msg);
        else {
            if (level == 3)
                ESP_LOGI(AUDIO_LOG_TAG, "%s", logStr.c_get());
            else if (level == 4)
                ESP_LOGD(AUDIO_LOG_TAG, "%s", logStr.c_get());
            else if (level >= 5)
                ESP_LOGV(AUDIO_LOG_TAG, "%s", logStr.c_get());
        }
        logStr.reset();
    }

// Kurzformen für die Log-Aufrufe
#define AUDIO_LOG_ERROR(fmt, ...) AUDIO_LOG_IMPL(1, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#define AUDIO_LOG_WARN(fmt, ...) AUDIO_LOG_IMPL(2, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#define AUDIO_LOG_INFO(fmt, ...) AUDIO_LOG_IMPL(3, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#define AUDIO_LOG_DEBUG(fmt, ...) AUDIO_LOG_IMPL(4, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
};

// ===================================================================
// Abgriff für die Visualisierung
// ===================================================================

/**
 * @brief Abgriff der fertigen PCM-Samples, unmittelbar vor der I2S-Ausgabe
 * @param outBuff Sample-Puffer, stereo verschränkt, 32 Bit je Kanal
 * @param validSamples Anzahl gültiger Stereo-Sample-Paare
 * @param continueI2S Wird auf false gesetzt, wenn die Ausgabe unterbleiben soll
 *
 * Die Funktion ist bewusst **nicht** als `weak` deklariert und die Library liefert **keine** Vorgabe-Implementierung. Dadurch bleibt an der Aufrufstelle eine echte undefinierte Referenz, die der Linker nur aus dem Anwendungsprojekt befriedigen kann. Das Binden ist damit unabhängig von Linkreihenfolge, LTO und `--gc-sections` garantiert. Ein Anwendungsprojekt **muss** diese Funktion definieren, sonst schlägt das Linken fehl.
 *
 * @warning Aufrufkontext: zeitkritischer Ausgabepfad, einmal je Chunk. Hier wird nicht allokiert, nicht geloggt und nicht blockiert.
 */
// Wird einmal je Ausgabeblock gerufen, und zwar vor Lautstärke, Klangregelung und Resampler.
// Die Samples tragen damit den Programmpegel. Wer den Lautsprecherpegel braucht, muss selbst
// mit der Lautstärkeeinstellung multiplizieren.
void audio_visualizer_process_i2s(int32_t *outBuff, int16_t validSamples, bool *continueI2S);

#endif // AUDIO_LITE_AUDIO_H
