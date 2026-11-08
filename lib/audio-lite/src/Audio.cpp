
/*****************************************************************************************************************************************************
    audio.cpp

    Created on: 28.10.2018                                                                                                  */
char audioI2SVers[] = "\
    Version 3.4.7                                                                                                                            ";
/*  Updated on: Jul 08, 2026

    Author: Wolle (schreibfaul1)
    Audio library for ESP32, ESP32-S3 or ESP32-P4
    Arduino Vers. V3 is mandatory
    PSRAM is mandatory
    external DAC is mandatory

*****************************************************************************************************************************************************/

#include "Audio.h"
#include "psram_unique_ptr.hpp"
#include "AudioTime.h"

#include <cctype>

namespace {
    // Länge der Base64-Ausgabe für sourceLength Bytes, ohne Nullterminierung. Kommt im Original aus Arduinos libb64.
    constexpr size_t base64_encode_expected_len(size_t sourceLength) {
        return ((sourceLength + 2) / 3) * 4;
    }
}

#if AUDIO_CODEC_AAC
    #include "aac_decoder/aac_decoder.h"
#endif
#if AUDIO_CODEC_FLAC
    #include "flac_decoder/flac_decoder.h"
#endif
#if AUDIO_CODEC_MP3
    #include "mp3_decoder/mp3_decoder.h"
#endif
#if AUDIO_CODEC_OPUS
    #include "opus_decoder/opus_decoder.h"
#endif
#if AUDIO_CODEC_VORBIS
    #include "vorbis_decoder/vorbis_decoder.h"
#endif
#if AUDIO_CODEC_WAV
    #include "wav_decoder/wav_decoder.h"
#endif

// Rahmengrößen je Codec: die größte Datenmenge, die der Decoder für einen Rahmen am Stück braucht
constexpr size_t FRAME_SIZE_WAV = 4096;
constexpr size_t FRAME_SIZE_MP3 = 18000;         ///< mehr als ein icy-metaint
constexpr size_t FRAME_SIZE_AAC = 18000;         ///< mehr als ein icy-metaint
constexpr size_t FRAME_SIZE_FLAC = UINT16_MAX;   ///< größte OGG-Seite
constexpr size_t FRAME_SIZE_OPUS = UINT16_MAX;   ///< größte OGG-Seite
constexpr size_t FRAME_SIZE_VORBIS = UINT16_MAX; ///< OGG-Seiten sind meist 4080 Bytes, im Metadaten-Block bis 64 KB
constexpr size_t OUT_BUFF_SIZE = 4608 * 2;
constexpr size_t RESAMPLES_BUFF_SIZE = OUT_BUFF_SIZE * 8; ///< kleinste Abtastrate 6 kHz auf größte 48 kHz

constexpr size_t AUDIO_STACK_SIZE = 4000;

// statische Allokationen für den Audio-Task
StaticTask_t __attribute__((unused)) xAudioTaskBuffer;
StackType_t __attribute__((unused)) xAudioStack[AUDIO_STACK_SIZE];

// ===================================================================
// Konstruktion und Decoder-Verwaltung
// ===================================================================

/**
 * @brief Konstruktor, legt nur die Semaphoren und den I2S-Port fest
 */
Audio::Audio(uint8_t i2sPort) {

    m_fI2sInit = false;
    m_mutexPlayAudioData = xSemaphoreCreateMutex();
    m_mutexAudioTask = xSemaphoreCreateMutex();
    m_mutexAudioTaskIsDecoding = xSemaphoreCreateMutex();

    m_i2sItems.i2s_num = i2sPort;
}

/**
 * @brief Destruktor, beendet Wiedergabe, Task und I2S-Kanal
 */
Audio::~Audio() {
    stopSong();
    setDefaults();

    i2s_channel_disable(m_i2sTxHandle);
    i2s_del_channel(m_i2sTxHandle);
    stopAudioTask();
    vSemaphoreDelete(m_mutexPlayAudioData);
    vSemaphoreDelete(m_mutexAudioTask);
    vSemaphoreDelete(m_mutexAudioTaskIsDecoding);
}

/**
 * @brief Gibt den aktiven Decoder frei
 */
void Audio::destroyDecoder() {
    if (m_decoder && m_decoder->isValid()) {
        info(*this, evt_info, "{}Decoder has been destroyed", m_decoder->whoIsIt());
        m_decoder->reset();
        m_decoder.reset();
    }
}

/**
 * @brief Erzeugt den zum Format passenden Decoder
 */
std::unique_ptr<Decoder> Audio::createDecoder(const char* type) {
    destroyDecoder();
#if AUDIO_CODEC_MP3
    if (strcmp(type, "MP3") == 0)
        return std::make_unique<MP3Decoder>(*this);
#endif
#if AUDIO_CODEC_FLAC
    if (strcmp(type, "FLAC") == 0)
        return std::make_unique<FlacDecoder>(*this);
#endif
#if AUDIO_CODEC_OPUS
    if (strcmp(type, "OPUS") == 0)
        return std::make_unique<OpusDecoder>(*this);
#endif
#if AUDIO_CODEC_AAC
    if (strcmp(type, "AAC") == 0)
        return std::make_unique<AACDecoder>(*this);
#endif
#if AUDIO_CODEC_VORBIS
    if (strcmp(type, "VORBIS") == 0)
        return std::make_unique<VorbisDecoder>(*this);
#endif
#if AUDIO_CODEC_WAV
    if (strcmp(type, "WAV") == 0)
        return std::make_unique<WavDecoder>(*this);
#endif
    return nullptr;
}

/**
 * @brief Allokiert den Eingangspuffer, falls noch nicht geschehen
 */
void Audio::initInBuff() {
    if (!m_inBuff.isInitialized()) {
        size_t size = m_inBuff.init();
        if (size > 0) {
            info(*this, evt_info, "inputBufferSize: {} bytes", size - 1);
        }
    }
}

// ===================================================================
// I2S-Kanal
// ===================================================================

/**
 * @brief Schaltet den I2S-Kanal ein
 */
esp_err_t Audio::i2sStart() {
    zeroI2Sbuff();
    esp_err_t err = ESP_FAIL;
    if (!m_fI2sChannelEnabled) {
        err = i2s_channel_enable(m_i2sTxHandle);
        if (err == ESP_OK)
            m_fI2sChannelEnabled = true;
    }
    return err;
}

/**
 * @brief Schaltet den I2S-Kanal aus und leert die Ausgangspuffer
 */
esp_err_t Audio::i2sStop() {
    m_outBuff.clear();
    m_resamplesBuff.clear();
    esp_err_t err = ESP_FAIL;
    if (m_fI2sChannelEnabled)
        err = i2s_channel_disable(m_i2sTxHandle);
    m_fI2sChannelEnabled = false;
    return err;
}

/**
 * @brief Setzt den I2S-DMA-Puffer auf null
 */
void Audio::zeroI2Sbuff() {
    // Seit IDF 5 gibt es zero_dma_buff() nicht mehr, das Vorladen weniger Null-Bytes setzt stattdessen den gesamten Puffer zurück.
    uint8_t buff[2] = {0, 0};
    size_t bytes_loaded = 0;
    i2s_channel_preload_data(m_i2sTxHandle, buff, 2, &bytes_loaded);
}

// ===================================================================
// Grundzustand und Verbindung
// ===================================================================

/**
 * @brief Beendet die Wiedergabe und setzt alle Zustandsfelder auf die Anfangswerte
 */
void Audio::setDefaults() {
    stopSong();
    initInBuff(); // allokiert den Eingangspuffer, falls noch nicht geschehen

    m_inBuff.reset();
    m_streamTitle.reset();
    m_streamURL.reset();
    m_playlistBuff.reset();
    m_m3u8Host.reset();

    m_outBuff.clear();
    m_resamplesBuff.clear();
    m_syltTimeStamp.clear();

    vectorClearAndShrink(m_playlistURL);
    vectorClearAndShrink(m_playlistContent);
    vectorClearAndShrink(m_syltLines);

    m_tcpSocket.stop();
    m_tlsSocket.stop();
    m_client = &m_tcpSocket;

    m_fTimeout = false;
    m_fChunked = false;
    m_fFirstMetaByte = false;
    m_fPlaying = false;
    m_fTts = false;
    m_fFirstCall = true;       // Startsequenz von processWebStream() und processLocalFile()
    m_cat.firstCall = true;     // Startsequenz von calculateAudioTime()
    m_pplM3U8.firstCall = true; // Startsequenz von parsePlaylistM3U8()
    m_fFirstPlayCall = true;   // Startsequenz von playAudioData()
    m_fFirstLoop = true;
    m_fUnsync = false;
    m_fExthdr = false;
    m_fRtsp = false;
    m_fM3u8Data = false;
    m_fContinue = false;
    m_fTs = false;
    m_fOgg = false;
    m_fM4aID3DataAreRead = false;
    m_fStream = false;
    m_fDecodeReady = false;
    m_fEof = false;
    m_fID3v1TagFound = false;
    m_fLockInBuffer = false;
    m_fAcceptRanges = false;
    m_fConnectionClose = false;
    m_fAllDataReceived = false;

    m_codec = CODEC_NONE;
    m_dataMode = AUDIO_NONE;
    m_streamType = ST_NONE;
    m_playlistFormat = FORMAT_NONE;
    m_m3u8Codec = CODEC_AAC;

    m_validSamples = 0;
    m_audioCurrentTime = 0;
    m_audioFileDuration = 0;
    m_resumeFilePos = -1;
    m_audioDataStart = 0;
    m_audioDataSize = 0;
    m_audioFileSize = 0;
    m_avrBitrate = 0;
    m_nominalBitrate = 0;
    m_bytesNotConsumed = 0;
    m_chunkcount = 0;
    m_curSample = 0;
    m_LFcount = 0;
    m_controlCounter = 0;
    m_channels = 2; // Stereo annehmen, bis der Decoder es besser weiß
    m_ID3Size = 0;
    m_haveNewFilePos = 0;
    m_validSamples = 0;
    m_m4aChConfig = 0;
    m_m4aObjectType = 0;
    m_m4aSampleRate = 0;
    m_lastGranulePosition = 0;
    m_validSamples = 0;
    m_blockPrepared = false;
    m_xing.reset();
}

/**
 * @brief Setzt die Verbindungs-Zeitgrenzen, 0 lässt den jeweiligen Wert unverändert
 */
void Audio::setConnectionTimeout(uint16_t timeout_ms, uint16_t timeout_ms_ssl) {
    if (timeout_ms)
        m_timeoutMs = timeout_ms;
    if (timeout_ms_ssl)
        m_timeoutMsSsl = timeout_ms_ssl;
}

/**
 * @brief Zerlegt eine URL in Schema, Host, Port, Pfad und Query
 *
 * Aufteilung am Beispiel, die Bezeichner entsprechen den Feldern von hwoe_t:
 *
 * https://edge.live.mp3.mdn.newmedia.nacamar.net:8000/ps-charivariwb/livestream.mp3;?user=ps-charivariwb;&pwd=ps-charivariwb
 *   ssl   |<--------------- hwoe --------------->|port|<-------- extension --------->|<------------ query_string ---------->
 *                                                     |<------------------------------- path ------------------------------>
 */
audiolib::hwoe_t Audio::dismantleHost(const char* host) {
    if (!host)
        return {};

    audiolib::hwoe_t result;

    const char* p = host;

    // Schema bestimmt Verschlüsselung, ohne Schema wird http angenommen
    if (strncmp(p, "https://", 8) == 0) {
        result.ssl = true;
        p += 8;
    } else if (strncmp(p, "http://", 7) == 0) {
        result.ssl = false;
        p += 7;
    } else {
        result.ssl = false;
    }

    // Host reicht von p bis ':', '/' oder '?'
    const char* host_start = p;
    const char* port_sep = strchr(p, ':');
    const char* path_sep = strchr(p, '/');
    const char* query_sep = strchr(p, '?');

    const char* host_end = p + strlen(p); // Vorgabe: Ende der Zeichenkette
    if (port_sep && port_sep < host_end)
        host_end = port_sep;
    if (path_sep && path_sep < host_end)
        host_end = path_sep;
    if (query_sep && query_sep < host_end)
        host_end = query_sep;
    result.hwoe.copy_from(host_start, host_end - host_start);
    result.rqh_host.clone_from(result.hwoe);

    // Port, ohne Angabe der Schema-Standardport
    result.port = result.ssl ? 443 : 80;
    if (port_sep && (!path_sep || port_sep < path_sep)) {
        result.port = atoi(port_sep + 1);
        result.rqh_host.appendf(":{}", result.port);
    }

    // Pfadanteil
    if (path_sep) {
        const char* path_start = path_sep + 1;
        const char* path_end = query_sep ? query_sep : host + strlen(host);
        result.extension.copy_from(path_start, path_end - path_start);
    }

    // Query-String
    if (query_sep) {
        result.query_string.assign(query_sep + 1);
    }

    return result;
}

/**
 * @brief Verbindet mit einem HTTP- oder HTTPS-Stream, user und pwd nur für Basic-Authentifizierung
 */
bool Audio::connecttohost(const char* host, const char* user, const char* pwd) {

    ps_ptr<char> c_host = host; // eigene Kopien, die Parameter können Zeiger in fremde Puffer sein
    ps_ptr<char> c_user = user;
    ps_ptr<char> c_pwd = pwd;

    if (!c_host.valid()) {
        AUDIO_LOG_ERROR("Hostaddress is empty");
        stopSong();
        return false;
    }

    c_host.trim();

    if (c_host.strlen() < 8) {
        AUDIO_LOG_ERROR("Hostaddress is too short");
        stopSong();
        return false;
    }

    if (c_host.strlen() > 2048) { // größte URL-Länge, die Chrome zulässt
        AUDIO_LOG_ERROR("Hostaddress is too long");
        stopSong();
        return false;
    }

    const char* user_agent_0 = "Mozilla/5.0 (X11; Linux x86_64) Chrome/146.0.0.0 Safari/537.36";
    const char* user_agent_1 = "VLC/3.0.21 LibVLC/3.0.21 AppleWebKit/537.36 (KHTML, like Gecko)";

    bool res = false;
    uint16_t port = 0;
    uint16_t authLen = 0;   // Länge der Zugangsdaten
    uint32_t timestamp = 0; // für die Dauer des Verbindungsaufbaus

    ps_ptr<char> hwoe;         // Host ohne Pfadanteil
    ps_ptr<char> rqh_host;     // Host für den Request-Header
    ps_ptr<char> extension;    // Pfadanteil
    ps_ptr<char> query_string; // Parameter
    ps_ptr<char> path;         // Pfadanteil + '?' + Parameter
    ps_ptr<char> rqh;          // Request-Header

    xSemaphoreTakeRecursive(m_mutexPlayAudioData, 0.3 * configTICK_RATE_HZ);

    auto dismantledHost = dismantleHost(c_host.get());

    m_fSsl = dismantledHost.ssl;
    port = dismantledHost.port;
    hwoe = dismantledHost.hwoe.c_get();
    rqh_host = dismantledHost.rqh_host.c_get();
    extension = dismantledHost.extension.c_get();
    query_string = dismantledHost.query_string.c_get();

    if (query_string.strlen())
        extension.appendf("?{}", query_string);
    path = urlencode(extension.get(), true);

    // optionale Basic-Authentifizierung
    if (c_user.valid() && c_pwd.valid())
        authLen = c_user.strlen() + c_pwd.strlen();
    ps_ptr<char> authorization;
    ps_ptr<char> toEncode;
    authorization.calloc(base64_encode_expected_len(authLen + 1) + 1, "authorization");
    if (authLen > 0) {
        toEncode.assignf("{}:{}", c_user, c_pwd);
        b64encode((const char*)toEncode.get(), toEncode.strlen(), authorization.get());
    }

    setDefaults();

    rqh.assignf("GET /{}", path);
    rqh.append(" HTTP/1.1\r\n");
    rqh.appendf("Host: {}\r\n", rqh_host);
#if !AUDIO_LITE_NO_ICY
    rqh.append("Icy-MetaData:1\r\n");
#endif
    rqh.append("Pragma: no-cache\r\n");
    rqh.append("Cache-Control: no-cache\r\n");
    rqh.append("Range: bytes=0-\r\n");
    rqh.append("Accept: */*\r\n");
    rqh.appendf("User-Agent: {}\r\n", m_fAltUserAgent ? user_agent_0 : user_agent_1);
    if (authLen > 0) {
        rqh.append("Authorization: Basic ");
        rqh.append(authorization);
        rqh.append("\r\n");
    }
    rqh.append("Accept-Encoding: identity;q=1,*;q=0\r\n");
    rqh.append("Connection: keep-alive\r\n\r\n");

    if (m_fSsl) {
        m_client = &m_tlsSocket;
        if (port == 80)
            port = 443;
    } else {
        m_client = &m_tcpSocket;
    }

    timestamp = audio_millis();
    m_client->setTimeout(m_fSsl ? m_timeoutMsSsl : m_timeoutMs);

    info(*this, evt_info, "connect to: \"{}\" on port {} path \"/{}\"", hwoe.get(), port, path.get());
    res = m_client->connect(hwoe.get(), port);

    m_expectedCodec = CODEC_NONE;
    m_expectedPlsFmt = FORMAT_NONE;

    if (res) {
        uint32_t dt = audio_millis() - timestamp;
        info(*this, evt_info, "{} has been established in {} ms", m_fSsl ? "SSL" : "Connection", dt);
        m_fRunning = true;
        m_client->print(rqh.get());
        if (extension.ends_with_icase(".mp3"))
            m_expectedCodec = CODEC_MP3;
        if (extension.ends_with_icase(".aac"))
            m_expectedCodec = CODEC_AAC;
        if (extension.ends_with_icase(".wav"))
            m_expectedCodec = CODEC_WAV;
        if (extension.ends_with_icase(".m4a"))
            m_expectedCodec = CODEC_M4A;
        if (extension.ends_with_icase(".ogg"))
            m_expectedCodec = CODEC_OGG;
        if (extension.ends_with_icase(".flac"))
            m_expectedCodec = CODEC_FLAC;
        if (extension.ends_with_icase("-flac"))
            m_expectedCodec = CODEC_FLAC;
        if (extension.ends_with_icase(".opus"))
            m_expectedCodec = CODEC_OPUS;
        if (extension.ends_with_icase("/opus"))
            m_expectedCodec = CODEC_OPUS;
        if (extension.ends_with_icase(".asx"))
            m_expectedPlsFmt = FORMAT_ASX;
        if (extension.ends_with_icase(".m3u"))
            m_expectedPlsFmt = FORMAT_M3U;
        if (extension.ends_with_icase(".pls"))
            m_expectedPlsFmt = FORMAT_PLS;
        if (extension.contains(".m3u8"))
            m_expectedPlsFmt = FORMAT_M3U8;

        m_currentHost = c_host;
        m_lastHost = c_host;
        info(*this, evt_lasthost, "{}", m_lastHost.c_get());
        m_dataMode = HTTP_RESPONSE_HEADER; // als Nächstes wird der Antwort-Header gelesen
        m_streamType = ST_WEBSTREAM;
    } else {
        AUDIO_LOG_ERROR("Request \"{}\" failed!", c_host.get());
        m_fRunning = false;
    }
    xSemaphoreGiveRecursive(m_mutexPlayAudioData);
    return res;
}

/**
 * @brief Schickt eine neue GET-Anfrage über die bestehende oder eine neue Verbindung, etwa beim Wechsel auf eine Playlist-URL
 */
bool Audio::httpPrint(const char* host) {

    ps_ptr<char> c_host = host; // eigene Kopie

    if (!c_host.valid()) {
        AUDIO_LOG_ERROR("Hostaddress is empty");
        stopSong();
        return false;
    }

    uint16_t port = 0;
    ps_ptr<char> hwoe;         // Host ohne Pfadanteil
    ps_ptr<char> rqh_host;     // Host für den Request-Header
    ps_ptr<char> extension;    // Pfadanteil
    ps_ptr<char> query_string; // Parameter
    ps_ptr<char> path;         // Pfadanteil + '?' + Parameter
    ps_ptr<char> rqh;          // Request-Header
    ps_ptr<char> cur_hwoe;     // m_currentHost ohne Pfadanteil

    c_host.trim();
    auto dismantledHost = dismantleHost(c_host.get());

    m_fSsl = dismantledHost.ssl;
    port = dismantledHost.port;
    hwoe = dismantledHost.hwoe;
    rqh_host = dismantledHost.rqh_host;
    extension = dismantledHost.extension;
    query_string = dismantledHost.query_string;

    if (query_string.strlen())
        extension.appendf("?{}", query_string);
    path = urlencode(extension.get(), true);

    if (!m_currentHost.valid())
        m_currentHost.assign("");
    auto dismantledLastHost = dismantleHost(m_currentHost.get());
    cur_hwoe = dismantledLastHost.hwoe;

    bool f_equal = true;
    if (hwoe == cur_hwoe && port == dismantledLastHost.port) {
        f_equal = true;
    } else {
        f_equal = false;
    }

    rqh.assignf("GET /{}", path);
    rqh.append(" HTTP/1.1\r\n");
    rqh.appendf("Host: {}\r\n", rqh_host);
#if !AUDIO_LITE_NO_ICY
    rqh.append("Icy-MetaData:1\r\n");
#endif
    rqh.append("Accept:*/*\r\n");
    rqh.append("User-Agent: VLC/3.0.21 LibVLC/3.0.21 AppleWebKit/537.36 (KHTML, like Gecko)\r\n");
    rqh.append("Accept-Encoding: identity;q=1,*;q=0\r\n");
    rqh.append("Connection: keep-alive\r\n\r\n");

    info(*this, evt_info, "next URL: \"{}\"", c_host.get());

    if (f_equal == false) {
        if (m_client->connected())
            m_client->stop();
    }
    if (!m_client->connected()) {
        if (m_fSsl) {
            m_client = &m_tlsSocket;
            if (m_fSsl && port == 80)
                port = 443;
        } else {
            m_client = &m_tcpSocket;
        }
        if (f_equal)
            info(*this, evt_info, "The host has disconnected, reconnecting");

        if (!m_client->connect(hwoe.get(), port)) {
            AUDIO_LOG_ERROR("connection lost {}", c_host.c_get());
            stopSong();
            return false;
        }
    }
    m_currentHost = c_host;
    m_client->print(rqh.get());

    if (extension.ends_with_icase(".mp3"))
        m_expectedCodec = CODEC_MP3;
    else if (extension.ends_with_icase(".aac"))
        m_expectedCodec = CODEC_AAC;
    else if (extension.ends_with_icase(".wav"))
        m_expectedCodec = CODEC_WAV;
    else if (extension.ends_with_icase(".m4a"))
        m_expectedCodec = CODEC_M4A;
    else if (extension.ends_with_icase(".flac"))
        m_expectedCodec = CODEC_FLAC;
    else if (extension.ends_with_icase(".m4s")) { // MP4-Container
        stopSong();
        AUDIO_LOG_WARN("MP4-Container not supported");
        return false;
    } else
        m_expectedCodec = CODEC_NONE;

    if (extension.ends_with_icase(".asx"))
        m_expectedPlsFmt = FORMAT_ASX;
    else if (extension.ends_with_icase(".m3u"))
        m_expectedPlsFmt = FORMAT_M3U;
    else if (extension.contains(".m3u8"))
        m_expectedPlsFmt = FORMAT_M3U8;
    else if (extension.ends_with_icase(".pls"))
        m_expectedPlsFmt = FORMAT_PLS;
    else
        m_expectedPlsFmt = FORMAT_NONE;

    m_audioFileSize = 0;
    m_dataMode = HTTP_RESPONSE_HEADER; // als Nächstes wird der Antwort-Header gelesen
    m_streamType = ST_WEBSTREAM;
    m_fChunked = false;
    AUDIO_LOG_DEBUG("playlistFormat {}, dataMode {}, streamType: {}", m_plsFmtStr[m_playlistFormat], m_dataModeStr[m_dataMode], m_streamTypeStr[m_streamType]);
    return true;
}

/**
 * @brief Fordert per Range-Anfrage einen Dateiausschnitt ab der Position seek an
 */
bool Audio::httpRange(uint32_t seek, uint32_t length) {

    if (!m_fRunning)
        return false;

    uint16_t port = 0;
    ps_ptr<char> c_host;       // eigene Kopie des Hosts
    ps_ptr<char> hwoe;         // Host ohne Pfadanteil
    ps_ptr<char> rqh_host;     // Host für den Request-Header
    ps_ptr<char> extension;    // Pfadanteil
    ps_ptr<char> query_string; // Parameter
    ps_ptr<char> path;         // Pfadanteil + '?' + Parameter
    ps_ptr<char> rqh;          // Request-Header
    ps_ptr<char> cur_hwoe;     // m_currentHost ohne Pfadanteil
    ps_ptr<char> range;        // zum Beispiel "Range: bytes=124-"

    c_host.clone_from(m_currentHost);
    c_host.trim();
    auto dismantledHost = dismantleHost(c_host.get());

    m_fSsl = dismantledHost.ssl;
    port = dismantledHost.port;
    if (dismantledHost.hwoe.valid())
        hwoe.clone_from(dismantledHost.hwoe);
    if (dismantledHost.rqh_host.valid())
        rqh_host.clone_from(dismantledHost.rqh_host);
    if (dismantledHost.extension.valid())
        extension.clone_from(dismantledHost.extension);
    if (dismantledHost.query_string.valid())
        query_string.clone_from(dismantledHost.query_string);

    if (extension.valid())
        path.assign(extension.get());
    if (query_string.valid()) {
        path.append("?");
        path.append(query_string.get());
    }
    if (!hwoe.valid())
        hwoe.assign("");
    if (!extension.valid())
        extension.assign("");
    if (!path.valid())
        path.assign("");

    path = urlencode(path.get(), true);

    if (!m_currentHost.valid())
        m_currentHost.assign("");
    auto dismantledLastHost = dismantleHost(m_currentHost.get());
    cur_hwoe.clone_from(dismantledLastHost.hwoe);

    if (length == UINT32_MAX)
        range.assignf("Range: bytes={}-\r\n", seek);
    else
        range.assignf("Range: bytes={}-{}\r\n", seek, seek + length);

    rqh.assignf("GET /{} HTTP/1.1\r\n", path.get());
    rqh.appendf("Host: {}\r\n", rqh_host.get());
    rqh.append("Accept: */*\r\n");
    rqh.append("Accept-Encoding: identity;q=1,*;q=0\r\n");
    rqh.append("Cache-Control: no-cache\r\n");
    rqh.append("Connection: keep-alive\r\n");
    rqh.appendf(range.c_get());
    rqh.appendf("Referer: {}\r\n", m_currentHost.c_get());
    rqh.append("Sec-GPC: 1\r\n");
    rqh.append("User-Agent: VLC/3.0.21 LibVLC/3.0.21 AppleWebKit/537.36 (KHTML, like Gecko)\r\n\r\n");

    if (m_client->connected()) {
        m_client->stop();
    }
    if (m_fSsl) {
        m_client = &m_tlsSocket;
        if (m_fSsl && port == 80)
            port = 443;
    } else {
        m_client = &m_tcpSocket;
    }

    if (!m_client->connect(hwoe.get(), port)) {
        AUDIO_LOG_ERROR("connection lost {}", c_host.c_get());
        stopSong();
        return false;
    }

    m_client->print(rqh.c_get());
    m_dataMode = HTTP_RANGE_HEADER;
    return true;
}

/**
 * @brief Öffnet eine lokale Datei über den absoluten VFS-Pfad und bestimmt den Codec aus der Endung
 */
bool Audio::connecttoFS(const char* path) {

    xSemaphoreTakeRecursive(m_mutexPlayAudioData, 0.3 * configTICK_RATE_HZ);
    ps_ptr<char> c_path;
    ps_ptr<char> audioPath;
    bool res = false;
    m_fileStartTime = -1;
    m_codec = CODEC_NONE;

    if (!path) {
        AUDIO_LOG_ERROR("file path is not set");
        goto exit;
    }
    c_path.copy_from(path);
    c_path.trim();
    if (!c_path.contains(".")) {
        AUDIO_LOG_ERROR("No file extension found");
        goto exit;
    }
    setDefaults();

    if (c_path.ends_with_icase(".mp3"))
        m_codec = CODEC_MP3;
    if (c_path.ends_with_icase(".m4a"))
        m_codec = CODEC_M4A;
    if (c_path.ends_with_icase(".aac"))
        m_codec = CODEC_AAC;
    if (c_path.ends_with_icase(".wav"))
        m_codec = CODEC_WAV;
    if (c_path.ends_with_icase(".flac"))
        m_codec = CODEC_FLAC;
    if (c_path.ends_with_icase(".opus"))
        m_codec = CODEC_OGG;
    if (c_path.ends_with_icase(".ogg"))
        m_codec = CODEC_OGG;
    if (c_path.ends_with_icase(".oga"))
        m_codec = CODEC_OGG;

    if (m_codec == CODEC_OGG)
        m_fOgg = true;
    if (m_codec == CODEC_NONE) {
        int dotPos = c_path.last_index_of('.');
        AUDIO_LOG_WARN("The {} format is not supported", path + dotPos);
        goto exit;
    }

    if (!c_path.starts_with("/"))
        c_path.insert("/", 0);

    // Öffnen statt vorher zu prüfen: ein Syscall weniger, und zwischen Prüfung und Öffnen kann die Datei nicht verschwinden.
    m_audioFilePath.copy_from(c_path.get());
    m_audiofile = fopen(c_path.get(), "rb");
    if (!m_audiofile) {
        AUDIO_LOG_WARN("file not found: {}", c_path.get());
        goto exit;
    }
    info(*this, evt_info, "Reading file: \"{}\"", c_path.get());
    m_dataMode = AUDIO_LOCALFILE;
    m_audioFileSize = 0;
    if (fseek(m_audiofile, 0, SEEK_END) == 0) {
        const long fileSize = ftell(m_audiofile);
        if (fileSize > 0)
            m_audioFileSize = static_cast<uint32_t>(fileSize);
    }
    rewind(m_audiofile);
    m_fRunning = true;
    res = true;

exit:
    xSemaphoreGiveRecursive(m_mutexPlayAudioData);
    return res;
}

// ===================================================================
// Metadaten
// ===================================================================

/**
 * @brief Meldet einen ID3-Rahmen mit lesbarer Beschreibung als Ereignis an den Host
 */
void Audio::showID3Tag(const char* tag, const char* value) {
    ps_ptr<char> id3tag;
    id3tag.set_name("id3tag");
    // ID3 Version 2.2
    if (!strcmp(tag, "CNT"))
        id3tag.assignf("Play counter: {}", value);
    if (!strcmp(tag, "COM"))
        id3tag.assignf("Comments: {}", value);
    if (!strcmp(tag, "CRA"))
        id3tag.assignf("Audio encryption: {}", value);
    if (!strcmp(tag, "CRM"))
        id3tag.assignf("Encrypted meta frame: {}", value);
    if (!strcmp(tag, "ETC"))
        id3tag.assignf("Event timing codes: {}", value);
    if (!strcmp(tag, "EQU"))
        id3tag.assignf("Equalization: {}", value);
    if (!strcmp(tag, "IPL"))
        id3tag.assignf("Involved people list: {}", value);
    if (!strcmp(tag, "PIC"))
        id3tag.assignf("Attached picture: {}", value);
    if (!strcmp(tag, "SLT"))
        id3tag.assignf("Synchronized lyric/text: {}", value);
    if (!strcmp(tag, "TAL"))
        id3tag.assignf("Album/Movie/Show title: {}", value);
    if (!strcmp(tag, "TBP"))
        id3tag.assignf("BPM (Beats Per Minute): {}", value);
    if (!strcmp(tag, "TCM"))
        id3tag.assignf("Composer: {}", value);
    if (!strcmp(tag, "TCO"))
        id3tag.assignf("Content type: {}", value);
    if (!strcmp(tag, "TCR"))
        id3tag.assignf("Copyright message: {}", value);
    if (!strcmp(tag, "TDA"))
        id3tag.assignf("Date: {}", value);
    if (!strcmp(tag, "TDY"))
        id3tag.assignf("Playlist delay: {}", value);
    if (!strcmp(tag, "TEN"))
        id3tag.assignf("Encoded by: {}", value);
    if (!strcmp(tag, "TFT"))
        id3tag.assignf("File type: {}", value);
    if (!strcmp(tag, "TIM"))
        id3tag.assignf("Time: {}", value);
    if (!strcmp(tag, "TKE"))
        id3tag.assignf("Initial key: {}", value);
    if (!strcmp(tag, "TLA"))
        id3tag.assignf("Language(s): {}", value);
    if (!strcmp(tag, "TLE"))
        id3tag.assignf("Length: {}", value);
    if (!strcmp(tag, "TMT"))
        id3tag.assignf("Media type: {}", value);
    if (!strcmp(tag, "TOA"))
        id3tag.assignf("Original artist(s)/performer(s): {}", value);
    if (!strcmp(tag, "TOF"))
        id3tag.assignf("Original filename: {}", value);
    if (!strcmp(tag, "TOL"))
        id3tag.assignf("Original Lyricist(s)/text writer(s): {}", value);
    if (!strcmp(tag, "TOR"))
        id3tag.assignf("Original release year: {}", value);
    if (!strcmp(tag, "TOT"))
        id3tag.assignf("Original album/Movie/Show title: {}", value);
    if (!strcmp(tag, "TP1"))
        id3tag.assignf("Lead artist(s)/Lead performer(s)/Soloist(s)/Performing group: {}", value);
    if (!strcmp(tag, "TP2"))
        id3tag.assignf("Band/Orchestra/Accompaniment: {}", value);
    if (!strcmp(tag, "TP3"))
        id3tag.assignf("Conductor/Performer refinement: {}", value);
    if (!strcmp(tag, "TP4"))
        id3tag.assignf("Interpreted, remixed, or otherwise modified by: {}", value);
    if (!strcmp(tag, "TPA"))
        id3tag.assignf("Part of a set: {}", value);
    if (!strcmp(tag, "TPB"))
        id3tag.assignf("Publisher: {}", value);
    if (!strcmp(tag, "TRC"))
        id3tag.assignf("ISRC (International Standard Recording Code): {}", value);
    if (!strcmp(tag, "TRD"))
        id3tag.assignf("Recording dates: {}", value);
    if (!strcmp(tag, "TRK"))
        id3tag.assignf("Track number/Position in set: {}", value);
    if (!strcmp(tag, "TSI"))
        id3tag.assignf("Size: {}", value);
    if (!strcmp(tag, "TSS"))
        id3tag.assignf("Software/hardware and settings used for encoding: {}", value);
    if (!strcmp(tag, "TT1"))
        id3tag.assignf("Content group description: {}", value);
    if (!strcmp(tag, "TT2"))
        id3tag.assignf("Title/Songname/Content description: {}", value);
    if (!strcmp(tag, "TT3"))
        id3tag.assignf("Subtitle/Description refinement: {}", value);
    if (!strcmp(tag, "TXT"))
        id3tag.assignf("Lyricist/text writer: {}", value);
    if (!strcmp(tag, "TXX"))
        id3tag.assignf("User defined text information frame: {}", value);
    if (!strcmp(tag, "TYE"))
        id3tag.assignf("Year: {}", value);
    if (!strcmp(tag, "UFI"))
        id3tag.assignf("Unique file identifier: {}", value);
    if (!strcmp(tag, "ULT"))
        id3tag.assignf("Unsychronized lyric/text transcription: {}", value);
    if (!strcmp(tag, "WAF"))
        id3tag.assignf("Official audio file webpage: {}", value);
    if (!strcmp(tag, "WAR"))
        id3tag.assignf("Official artist/performer webpage: {}", value);
    if (!strcmp(tag, "WAS"))
        id3tag.assignf("Official audio source webpage: {}", value);
    if (!strcmp(tag, "WCM"))
        id3tag.assignf("Commercial information: {}", value);
    if (!strcmp(tag, "WCP"))
        id3tag.assignf("Copyright/Legal information: {}", value);
    if (!strcmp(tag, "WPB"))
        id3tag.assignf("Publishers official webpage: {}", value);
    if (!strcmp(tag, "WXX"))
        id3tag.assignf("User defined URL link frame: {}", value);

    // ID3 Version 2.3 und 2.4
    if (!strcmp(tag, "COMM"))
        id3tag.assignf("Comment: {}", value);
    if (!strcmp(tag, "OWNE"))
        id3tag.assignf("Ownership: {}", value);
    if (!strcmp(tag, "PRIV"))
        id3tag.assignf("Private: {}", value);
    if (!strcmp(tag, "SYLT"))
        id3tag.assignf("SynLyrics: {}", value);
    if (!strcmp(tag, "TALB"))
        id3tag.assignf("Album: {}", value);
    if (!strcmp(tag, "TBPM"))
        id3tag.assignf("BeatsPerMinute: {}", value);
    if (!strcmp(tag, "TCAT"))
        id3tag.assignf("PodcastCategory: {}", value);
    if (!strcmp(tag, "TCMP"))
        id3tag.assignf("Compilation: {}", value);
    if (!strcmp(tag, "TCOM"))
        id3tag.assignf("Composer: {}", value);
    if (!strcmp(tag, "TCON"))
        id3tag.assignf("ContentType: {}", value);
    if (!strcmp(tag, "TCOP"))
        id3tag.assignf("Copyright: {}", value);
    if (!strcmp(tag, "TDAT"))
        id3tag.assignf("Date: {}", value);
    if (!strcmp(tag, "TDES"))
        id3tag.assignf("PodcastDescription: {}", value);
    if (!strcmp(tag, "TDOR"))
        id3tag.assignf("Original Release Year: {}", value);
    if (!strcmp(tag, "TDRC"))
        id3tag.assignf("Publication date: {}", value);
    if (!strcmp(tag, "TDRL"))
        id3tag.assignf("ReleaseTime: {}", value);
    if (!strcmp(tag, "TENC"))
        id3tag.assignf("Encoded by: {}", value);
    if (!strcmp(tag, "TEXT")) {} // wird nicht gemeldet
    if (!strcmp(tag, "TGID"))
        id3tag.assignf("PodcastID: {}", value);
    if (!strcmp(tag, "TIME"))
        id3tag.assignf("Time: {}", value);
    if (!strcmp(tag, "TIT1"))
        id3tag.assignf("Grouping: {}", value);
    if (!strcmp(tag, "TIT2"))
        id3tag.assignf("Title: {}", value);
    if (!strcmp(tag, "TIT3"))
        id3tag.assignf("Subtitle: {}", value);
    if (!strcmp(tag, "TLAN"))
        id3tag.assignf("Language: {}", value);
    if (!strcmp(tag, "TLEN"))
        id3tag.assignf("Length (ms): {}", value);
    if (!strcmp(tag, "TMED"))
        id3tag.assignf("Media: {}", value);
    if (!strcmp(tag, "TOAL"))
        id3tag.assignf("OriginalAlbum: {}", value);
    if (!strcmp(tag, "TOPE"))
        id3tag.assignf("OriginalArtist: {}", value);
    if (!strcmp(tag, "TOLY"))
        id3tag.assignf("OriginalLyricist: {}", value);
    if (!strcmp(tag, "TORY"))
        id3tag.assignf("OriginalReleaseYear: {}", value);
    if (!strcmp(tag, "TPE1"))
        id3tag.assignf("Artist: {}", value);
    if (!strcmp(tag, "TPE2"))
        id3tag.assignf("Band: {}", value);
    if (!strcmp(tag, "TPE3"))
        id3tag.assignf("Conductor: {}", value);
    if (!strcmp(tag, "TPE4"))
        id3tag.assignf("InterpretedBy: {}", value);
    if (!strcmp(tag, "TPOS"))
        id3tag.assignf("PartOfSet: {}", value);
    if (!strcmp(tag, "TPUB"))
        id3tag.assignf("Publisher: {}", value);
    if (!strcmp(tag, "TRCK"))
        id3tag.assignf("Track: {}", value);
    if (!strcmp(tag, "TRSN"))
        id3tag.assignf("InternetRadioStationName: {}", value);
    if (!strcmp(tag, "TSSE"))
        id3tag.assignf("SettingsForEncoding: {}", value);
    if (!strcmp(tag, "TRDA"))
        id3tag.assignf("RecordingDates: {}", value);
    if (!strcmp(tag, "TSO2"))
        id3tag.assignf("AlbumArtistSortOrder: {}", value);
    if (!strcmp(tag, "TSOA"))
        id3tag.assignf("AlbumSortOrder: {}", value);
    if (!strcmp(tag, "TSOP"))
        id3tag.assignf("PerformerSortOrder: {}", value);
    if (!strcmp(tag, "TSOC"))
        id3tag.assignf("ComposerSortOrder: {}", value);
    if (!strcmp(tag, "TSOT"))
        id3tag.assignf("TitleSortOrder: {}", value);
    if (!strcmp(tag, "TXXX"))
        id3tag.assignf("UserDefinedText: {}", value);
    if (!strcmp(tag, "TYER"))
        id3tag.assignf("Year: {}", value);
    if (!strcmp(tag, "USER"))
        id3tag.assignf("TermsOfUse: {}", value);
    if (!strcmp(tag, "USLT"))
        id3tag.assignf("Lyrics: {}", value);
    if (!strcmp(tag, "WCOM"))
        id3tag.assignf("Commercial URL: {}", value);
    if (!strcmp(tag, "WFED"))
        id3tag.assignf("Podcast URL: {}", value);
    if (!strcmp(tag, "WOAF"))
        id3tag.assignf("File URL: {}", value);
    if (!strcmp(tag, "WOAR"))
        id3tag.assignf("OfficialArtistWebpage: {}", value);
    if (!strcmp(tag, "WOAS"))
        id3tag.assignf("Source URL: {}", value);
    if (!strcmp(tag, "WORS"))
        id3tag.assignf("InternetRadioStationURL: {}", value);
    if (!strcmp(tag, "WXXX"))
        id3tag.assignf("User defined URL link frame: {}", value);
    if (!strcmp(tag, "XDOR"))
        id3tag.assignf("OriginalReleaseTime: {}", value);

    if (!id3tag.valid()) {
        AUDIO_LOG_DEBUG("unknown tag: {}", tag);
        return;
    }

    latinToUTF8(id3tag);

    if (id3tag.contains("?xml")) {
        showstreamtitle(id3tag.get());
        return;
    }

    if (id3tag.starts_with("Grouping")) {
        showstreamtitle(id3tag.get());
        return;
    }
    if (id3tag.strlen()) {
        info(*this, evt_id3data, "{}", id3tag.get());
    }
}
/**
 * @brief Wandelt Latin-1-Texte nach UTF-8, bereits gültiges UTF-8 bleibt unverändert
 *
 * Die meisten Stationen senden UTF-8, einige Latin-1. Die Umwandlung verlängert den Text, der Puffer wird bei Bedarf vergrößert.
 */
void Audio::latinToUTF8(ps_ptr<char>& buff, bool UTF8check) {
    uint16_t pos = 0;
    uint16_t in = 0;
    uint16_t out = 0;
    bool isUTF8 = true;

    // Ob ein Text UTF-8 ist, lässt sich nicht beweisen: jede UTF-8-Bytefolge ist auch gültiges Latin-1. Umgekehrt gilt das nicht, ungültige UTF-8-Folgen schließen UTF-8 sicher aus, und genau das wird geprüft.
    if (UTF8check) {
        const size_t strLen = buff.strlen();
        while (pos < strLen) {
            if ((uint8_t)buff[pos] <= 0x7F) { // 0xxxxxxx: ASCII
                pos++;
            } else if ((buff[pos] & 0xE0) == 0xC0) {
                // 110xxxxx 10xxxxxx: Zwei-Byte-Folge
                if (pos + 1 >= strLen || (uint8_t)buff[pos] < 0xC2 || ((uint8_t)buff[pos + 1] & 0xC0) != 0x80) {
                    isUTF8 = false;
                    break;
                }
                pos += 2;
            } else if ((buff[pos] & 0xF0) == 0xE0) {
                // 1110xxxx 10xxxxxx 10xxxxxx: Drei-Byte-Folge
                if (pos + 2 >= strLen || ((uint8_t)buff[pos + 1] & 0xC0) != 0x80 || ((uint8_t)buff[pos + 2] & 0xC0) != 0x80) {
                    isUTF8 = false;
                    break;
                }
                if ((uint8_t)buff[pos] == 0xE0 && (uint8_t)buff[pos + 1] < 0xA0) {
                    isUTF8 = false;
                    break;
                } // überlange Kodierung
                if ((uint8_t)buff[pos] == 0xED && (uint8_t)buff[pos + 1] >= 0xA0) {
                    isUTF8 = false;
                    break;
                } // UTF-16-Surrogat
                pos += 3;
            } else if ((buff[pos] & 0xF8) == 0xF0) {
                // 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx: Vier-Byte-Folge
                if (pos + 3 >= strLen || ((uint8_t)buff[pos + 1] & 0xC0) != 0x80 || ((uint8_t)buff[pos + 2] & 0xC0) != 0x80 || ((uint8_t)buff[pos + 3] & 0xC0) != 0x80) {
                    isUTF8 = false;
                    break;
                }
                if ((uint8_t)buff[pos] == 0xF0 && (uint8_t)buff[pos + 1] < 0x90) {
                    isUTF8 = false;
                    break;
                } // überlange Kodierung
                if ((uint8_t)buff[pos] > 0xF4 || ((uint8_t)buff[pos] == 0xF4 && (uint8_t)buff[pos + 1] > 0x8F)) {
                    isUTF8 = false;
                    break;
                } // über U+10FFFF hinaus
                pos += 4;
            } else {
                isUTF8 = false;
                break; // ungültiges Startbyte (Folgebyte oder 0xF8 bis 0xFF)
            }
        }
        if (isUTF8)
            return; // ist UTF-8, nichts zu tun
    }
    ps_ptr<char> iso8859_1;
    iso8859_1.assign(buff.get());

    // schlimmster Fall: jedes Latin-1-Zeichen über 0x7F wird zu zwei Bytes, also doppelte Länge plus Nullterminierung
    std::size_t requiredSize = strlen(iso8859_1.get()) * 2 + 1;

    if (buff.size() < requiredSize) {
        buff.realloc(requiredSize);
        if (buff.size() < requiredSize) {
            AUDIO_LOG_ERROR("latinToUTF8: realloc failed (need {} bytes, have {})", requiredSize, buff.size());
            return; // Original behalten, kein Pufferüberlauf
        }
    }

    // nach UTF-8 wandeln
    while (iso8859_1[in] != '\0') {
        if (iso8859_1[in] < 0x80) {
            buff[out++] = iso8859_1[in++];
        } else {
            buff[out++] = (0xC0 | (iso8859_1[in] >> 6));
            buff[out++] = (0x80 | (iso8859_1[in] & 0x3F));
            in++;
        }
    }
    buff[out] = '\0';
}
/**
 * @brief Ersetzt HTML-Entities in der Zeichenkette am Platz durch ihre UTF-8-Zeichen
 */
void Audio::htmlToUTF8(char* str) {

    typedef struct {
        const char* name;
        uint32_t codepoint;
    } EntityMap;

    const EntityMap entities[] = {
        {"amp", 0x0026},    // &
        {"lt", 0x003C},     // <
        {"gt", 0x003E},     // >
        {"quot", 0x0022},   // "
        {"apos", 0x0027},   // '
        {"nbsp", 0x00A0},   // geschütztes Leerzeichen
        {"euro", 0x20AC},   // Eurozeichen
        {"copy", 0x00A9},   // Copyright-Zeichen
        {"reg", 0x00AE},    // Marken-Zeichen
        {"trade", 0x2122},  // Trademark-Zeichen
        {"hellip", 0x2026}, // Auslassungspunkte
        {"ndash", 0x2013},  // Halbgeviertstrich
        {"mdash", 0x2014},  // Geviertstrich
        {"sect", 0x00A7},   // Paragrafenzeichen
        {"para", 0x00B6}    // Absatzzeichen
    };

    auto find_entity = [&](const char* p, uint32_t* codepoint, int* entity_len) {
        for (size_t i = 0; i < sizeof(entities) / sizeof(entities[0]); i++) {
            const char* name = entities[i].name;
            size_t len = strlen(name);
            if (strncmp(p + 1, name, len) == 0 && p[len + 1] == ';') {
                *codepoint = entities[i].codepoint;
                *entity_len = (int)(len + 2); // &name;
                return 1;
            }
        }
        return 0;
    };

    // schreibt den Unicode-Codepoint als UTF-8 nach dst und liefert die Byteanzahl
    auto codepoint_to_utf8 = [&](uint32_t cp, char* dst) {
        if (cp <= 0x7F) {
            dst[0] = cp;
            return 1;
        } else if (cp <= 0x7FF) {
            dst[0] = 0xC0 | (cp >> 6);
            dst[1] = 0x80 | (cp & 0x3F);
            return 2;
        } else if (cp <= 0xFFFF) {
            dst[0] = 0xE0 | (cp >> 12);
            dst[1] = 0x80 | ((cp >> 6) & 0x3F);
            dst[2] = 0x80 | (cp & 0x3F);
            return 3;
        } else if (cp <= 0x10FFFF) {
            dst[0] = 0xF0 | (cp >> 18);
            dst[1] = 0x80 | ((cp >> 12) & 0x3F);
            dst[2] = 0x80 | ((cp >> 6) & 0x3F);
            dst[3] = 0x80 | (cp & 0x3F);
            return 4;
        }
        return -1; // ungültiger Codepoint
    };

    char* p = str;
    while (*p != '\0') {
        if (p[0] == '&') {
            uint32_t cp;
            int consumed;
            if (find_entity(p, &cp, &consumed)) { // benannte Entity wie &copy;
                char utf8[5] = {0};
                int len = codepoint_to_utf8(cp, utf8);
                if (len > 0) {
                    size_t tail_len = strlen(p + consumed);
                    memmove(p + len, p + consumed, tail_len + 1);
                    memcpy(p, utf8, len);
                    p += len;
                    continue;
                }
            }
        }
        if (p[0] == '&' && p[1] == '#') {
            char* endptr;
            uint32_t codepoint = strtol(p + 2, &endptr, 10);

            if (*endptr == ';' && codepoint <= 0x10FFFF) {
                char utf8[5] = {0};
                int utf8_len = codepoint_to_utf8(codepoint, utf8);
                if (utf8_len > 0) {
                    size_t tail_len = strlen(endptr + 1);

                    // Rest nach links schieben, +1 wegen der Nullterminierung
                    memmove(p + utf8_len, endptr + 1, tail_len + 1);

                    memcpy(p, utf8, utf8_len);

                    p += utf8_len;
                    continue;
                }
            }
        }
        p++;
    }
}

// ===================================================================
// Header-Parser
// ===================================================================

/**
 * @brief Reicht die Kopfdaten an den Parser des aktiven Codecs weiter
 */
size_t Audio::readAudioHeader(uint32_t bytes) {
    size_t bytesReaded = 0;
    if (m_codec == CODEC_WAV) {
        int res = readWavHeader(m_inBuff.getReadPtr(), bytes);
        if (res >= 0)
            bytesReaded = res;
        else { // Fehler, Header überspringen
            m_controlCounter = 100;
        }
    }
    if (m_codec == CODEC_MP3) {
        int res = readID3Header(m_inBuff.getReadPtr(), bytes);
        if (res > 0 || m_controlCounter < 100)
            bytesReaded = res;
        else { // Fehler, Header überspringen
            m_controlCounter = 100;
        }
    }
    if (m_codec == CODEC_M4A) {
        int res = readM4aHeader(m_inBuff.getReadPtr(), bytes);
        if (res >= 0)
            bytesReaded = res;
        else { // Fehler, Header überspringen
            m_controlCounter = 100;
        }
    }
    if (m_codec == CODEC_AAC) {
        // reiner Stream, kein Header
        m_audioDataSize = m_audioFileSize;
        m_controlCounter = 100;
    }
    if (m_codec == CODEC_FLAC) {
        int res = readFlacHeader(m_inBuff.getReadPtr(), bytes);
        if (res >= 0)
            bytesReaded = res;
        else {
            stopSong();
        }
    }
    if (m_codec == CODEC_OPUS) {
        m_controlCounter = 100;
    }
    if (m_codec == CODEC_VORBIS) {
        m_controlCounter = 100;
    }
    if (m_codec == CODEC_OGG) {
        m_controlCounter = 100;
    }
    if (!isRunning()) {
        AUDIO_LOG_ERROR("Processing stopped due to invalid audio header");
        return 0;
    }
    return bytesReaded;
}

/**
 * @brief Liest den WAV-Kopf schrittweise, m_controlCounter zählt die Schritte
 */
int Audio::readWavHeader(uint8_t* data, size_t len) {

    if (m_controlCounter == 0) {
        m_rwh.cs = 0;
        m_rwh.bts = 0;
        m_controlCounter++;
        if ((*data != 'R') || (*(data + 1) != 'I') || (*(data + 2) != 'F') || (*(data + 3) != 'F')) {
            AUDIO_LOG_ERROR("file has no RIFF tag");
            m_rwh.headerSize = 0;
            return -1;
        } else {
            m_rwh.headerSize = 4;
            return 4; // ok
        }
    }

    if (m_controlCounter == 1) {
        m_controlCounter++;
        m_rwh.cs = (uint32_t)(*data + (*(data + 1) << 8) + (*(data + 2) << 16) + (*(data + 3) << 24) - 8);
        m_rwh.headerSize += 4;
        return 4; // ok
    }

    if (m_controlCounter == 2) {
        m_controlCounter++;
        if ((*data != 'W') || (*(data + 1) != 'A') || (*(data + 2) != 'V') || (*(data + 3) != 'E')) {
            AUDIO_LOG_ERROR("format tag is not WAVE");
            return -1;
        } else {
            m_rwh.headerSize += 4;
            return 4;
        }
    }

    if (m_controlCounter == 3) {
        if ((*data == 'f') && (*(data + 1) == 'm') && (*(data + 2) == 't')) {
            m_controlCounter++;
            m_rwh.headerSize += 4;
            return 4;
        } else {
            m_rwh.headerSize += 4;
            return 4;
        }
    }

    if (m_controlCounter == 4) {
        m_controlCounter++;
        m_rwh.cs = (uint32_t)(*data + (*(data + 1) << 8));
        if (m_rwh.cs > 40)
            return -1;             // unplausible Blockgröße
        m_rwh.bts = m_rwh.cs - 16; // zu überspringende Bytes, wenn der fmt-Block größer als 16 ist
        m_rwh.headerSize += 4;
        return 4;
    }

    if (m_controlCounter == 5) {
        m_controlCounter++;
        uint16_t fc = (uint16_t)(*(data + 0) + (*(data + 1) << 8));                                               // Formatcode
        uint16_t ch = (uint16_t)(*(data + 2) + (*(data + 3) << 8));                                               // Anzahl der verschränkten Kanäle
        uint32_t sr = (uint32_t)(*(data + 4) + (*(data + 5) << 8) + (*(data + 6) << 16) + (*(data + 7) << 24));   // Abtastrate
        uint32_t dr = (uint32_t)(*(data + 8) + (*(data + 9) << 8) + (*(data + 10) << 16) + (*(data + 11) << 24)); // Datenrate
        uint16_t dbs = (uint16_t)(*(data + 12) + (*(data + 13) << 8));                                            // Datenblockgröße
        uint16_t bps = (uint16_t)(*(data + 14) + (*(data + 15) << 8));                                            // Bits je Sample

        info(*this, evt_info, "FormatCode: {}", fc);
        info(*this, evt_info, "DataRate: {}", dr);
        info(*this, evt_info, "DataBlockSize: {}", dbs);
        info(*this, evt_info, "BitsPerSample: {}", bps);

        if ((bps != 8) && (bps != 16) && (bps != 24) && (bps != 32)) {
            info(*this, evt_info, "BitsPerSample is {},  must be 8, 16, 24 or 32", bps);
            stopSong();
            return -1;
        }
        if ((ch != 1) && (ch != 2)) {
            info(*this, evt_info, "num channels is {},  must be 1 or 2", ch);
            stopSong();
            return -1;
        }
        if (fc != 1) {
            AUDIO_LOG_ERROR("format code is not 1 (PCM)");
            stopSong();
            return -1;
        }
        m_decoder->setRawBlockParams(ch, sr, bps, 0, 0);
        m_nominalBitrate = ch * sr * bps;
        m_rwh.headerSize += 16;
        return 16;
    }

    if (m_controlCounter == 6) {
        m_controlCounter++;
        m_rwh.headerSize += m_rwh.bts;
        return m_rwh.bts; // Rest des fmt-Blocks überspringen
    }

    if (m_controlCounter == 7) {
        if ((*(data + 0) == 'd') && (*(data + 1) == 'a') && (*(data + 2) == 't') && (*(data + 3) == 'a')) {
            m_controlCounter++;
            m_rwh.headerSize += 4;
            return 4;
        } else {
            m_rwh.headerSize++;
            return 1;
        }
    }

    if (m_controlCounter == 8) {
        m_controlCounter++;
        size_t cs = *(data + 0) + (*(data + 1) << 8) + (*(data + 2) << 16) + (*(data + 3) << 24); // Blockgröße
        m_rwh.headerSize += 4;
        if (cs) {
            m_audioDataSize = cs - 44;
        } else { // manche Dateien lassen die Größe leer
            m_audioDataSize = m_audioFileSize - m_rwh.headerSize;
        }

        m_audioFileDuration = m_audioDataSize / (getSampleRate() * getChannels());
        if (getBitsPerSample() == 16)
            m_audioFileDuration /= 2;
        info(*this, evt_info, "Duration (s): {}", m_audioFileDuration);
        return 4;
    }
    m_controlCounter = 100; // Header vollständig gelesen
    m_audioDataStart = m_rwh.headerSize;
    return 0;
}

/**
 * @brief Liest die FLAC-Metadatenblöcke schrittweise, m_controlCounter hält den Zustand
 */
int Audio::readFlacHeader(uint8_t* data, size_t len) {

    if (m_rflh.retvalue && m_controlCounter != 0) {
        if (m_rflh.retvalue > len) { // mehr zu überspringen, als der Puffer hergibt
            if (len > m_inBuff.getMaxBlockSize())
                len = m_inBuff.getMaxBlockSize();
            m_rflh.retvalue -= len; // Rest beim nächsten Aufruf
            return len;
        } else {
            size_t tmp = m_rflh.retvalue;
            m_rflh.retvalue = 0;
            return tmp;
        }
        return 0;
    }
    if (m_controlCounter == FLAC_BEGIN) {
        m_rflh.reset();
        m_controlCounter = FLAC_MAGIC;
        m_rflh.picVec.clear();
        return 0;
    }
    if (m_controlCounter == FLAC_MAGIC) {
        if (specialIndexOf(data, "OggS", 10) == 0) { // OGG-Container, keine nackten FLAC-Blöcke
            m_rflh.headerSize = 0;
            m_rflh.retvalue = 0;
            m_controlCounter = FLAC_OKAY;
            return 0;
        }
        if (specialIndexOf(data, "fLaC", 10) != 0) {
            if (specialIndexOf(data, "ID3", 10) == 0) { // ID3-Tag vor dem fLaC-Kennzeichen
                // Synchsafe-Größe
                uint32_t size = (data[6] << 21) | (data[7] << 14) | (data[8] << 7) | data[9];
                size += 10;             // Header + Inhalt
                m_rflh.retvalue = size; // ID3-Header samt Inhalt überspringen
                return 0;
            }
            AUDIO_LOG_ERROR("Magic String 'fLaC' not found in header");
            stopSong();
            return -1;
        }

        m_controlCounter = FLAC_MBH; // METADATA_BLOCK_HEADER
        m_rflh.headerSize = 4;
        m_rflh.retvalue = 4;
        return 0;
    }
    if (m_controlCounter == FLAC_MBH) { /* METADATA_BLOCK_HEADER */
        uint8_t blockType = *data;

        if (!m_rflh.f_lastMetaBlock) {
            if ((blockType & 127) == 0)
                m_controlCounter = FLAC_SINFO;
            if ((blockType & 127) == 1)
                m_controlCounter = FLAC_PADDING;
            if ((blockType & 127) == 2)
                m_controlCounter = FLAC_APP;
            if ((blockType & 127) == 3)
                m_controlCounter = FLAC_SEEK;
            if ((blockType & 127) == 4)
                m_controlCounter = FLAC_VORBIS;
            if ((blockType & 127) == 5)
                m_controlCounter = FLAC_CUESHEET;
            if ((blockType & 127) == 6)
                m_controlCounter = FLAC_PICTURE;
            if ((blockType & 128)) {
                m_rflh.f_lastMetaBlock = true;
            }
            m_rflh.headerSize += 1;
            m_rflh.retvalue = 1;
            return 0;
        }

        m_controlCounter = FLAC_OKAY;

        m_audioDataStart = m_rflh.headerSize;
        m_audioDataSize = m_audioFileSize - m_audioDataStart;
        m_decoder->setRawBlockParams(m_rflh.numChannels, m_rflh.sampleRate, m_rflh.bitsPerSample, m_rflh.totalSamplesInStream, (uint32_t)m_audioDataSize);
        if (m_rflh.picLen) {
            size_t pos = m_audioDataStart;
            info(*this, evt_image, m_rflh.picVec);
        }

        if (m_rflh.duration) {
            m_rflh.nominalBitrate = (m_audioDataSize * 8) / m_rflh.duration;
            m_nominalBitrate = m_rflh.nominalBitrate;
            m_audioFileDuration = m_rflh.duration;
            info(*this, evt_info, "Duration (s): {}", m_rflh.duration);
        }
        m_rflh.retvalue = 0;
        return 0;
    }
    if (m_controlCounter == FLAC_SINFO) { /* Streaminfo-Block */
        size_t l = bigEndian(data, 3);
        vTaskDelay(2);
        m_rflh.maxBlockSize = bigEndian(data + 5, 2);
        info(*this, evt_info, "FLAC maxBlockSize: {}", m_rflh.maxBlockSize);
        vTaskDelay(2);
        m_rflh.maxFrameSize = bigEndian(data + 10, 3);
        if (m_rflh.maxFrameSize) {
            info(*this, evt_info, "FLAC maxFrameSize: {}", m_rflh.maxFrameSize);
        } else {
            info(*this, evt_info, "FLAC maxFrameSize: N/A");
        }
        if (m_rflh.maxFrameSize > m_inBuff.getMaxBlockSize()) {
            AUDIO_LOG_ERROR("FLAC maxFrameSize too large!");
            stopSong();
            return -1;
        }
        vTaskDelay(2);
        uint32_t nextval = bigEndian(data + 13, 3);
        m_rflh.sampleRate = nextval >> 4;
        info(*this, evt_info, "FLAC sampleRate (Hz): {}", m_rflh.sampleRate);
        vTaskDelay(2);
        m_rflh.numChannels = ((nextval & 0x06) >> 1) + 1;
        info(*this, evt_info, "FLAC numChannels: {}", m_rflh.numChannels);
        vTaskDelay(2);
        uint8_t bps = (nextval & 0x01) << 4;
        bps += (*(data + 16) >> 4) + 1;
        m_rflh.bitsPerSample = bps;
        if ((bps != 8) && (bps != 16) && (bps != 24)) {
            AUDIO_LOG_ERROR("bits per sample must be 8, 16 or 24, is {}", bps);
            stopSong();
            return -1;
        }
        info(*this, evt_info, "FLAC bitsPerSample: {}", m_rflh.bitsPerSample);
        m_rflh.totalSamplesInStream = bigEndian(data + 17, 4);
        if (m_rflh.totalSamplesInStream) {
            info(*this, evt_info, "total samples in stream: {}", m_rflh.totalSamplesInStream);
        } else {
            info(*this, evt_info, "total samples in stream: N/A");
        }
        if (bps != 0 && m_rflh.totalSamplesInStream && m_rflh.sampleRate) {
            m_rflh.duration = m_rflh.totalSamplesInStream / m_rflh.sampleRate;
        }
        m_controlCounter = FLAC_MBH; // METADATA_BLOCK_HEADER
        m_rflh.retvalue = l + 3;
        m_rflh.headerSize += m_rflh.retvalue;
        return 0;
    }
    if (m_controlCounter == FLAC_PADDING) { /* PADDING */
        size_t l = bigEndian(data, 3);
        m_controlCounter = FLAC_MBH;
        m_rflh.retvalue = l + 3;
        m_rflh.headerSize += m_rflh.retvalue;
        return 0;
    }
    if (m_controlCounter == FLAC_APP) { /* APPLICATION */
        size_t l = bigEndian(data, 3);
        m_controlCounter = FLAC_MBH;
        m_rflh.retvalue = l + 3;
        m_rflh.headerSize += m_rflh.retvalue;
        return 0;
    }
    if (m_controlCounter == FLAC_SEEK) { /* SEEKTABLE */
        size_t l = bigEndian(data, 3);
        m_controlCounter = FLAC_MBH;
        m_rflh.retvalue = l + 3;
        m_rflh.headerSize += m_rflh.retvalue;
        return 0;
    }
    if (m_controlCounter == FLAC_VORBIS) { /* VORBIS COMMENT */

        auto parse_flac_timestamp = [&](const char* s) {
            if (!s || *s != '[')
                return -1;
            int mm = 0, ss = 0, hh = 0;
            if (sscanf(s, "[%d:%d.%d]", &mm, &ss, &hh) == 3) {
                return mm * 60000 + ss * 1000 + hh * 10;
            }
            return -1;
        };

        ps_ptr<char> vendorString;
        ps_ptr<char> commentString;
        ps_ptr<char> lyricsBuffer;
        size_t vendorLength = bigEndian(data, 3);
        size_t idx = 0;
        data += 3;
        idx += 3;
        size_t vendorStringLength = data[0] + (data[1] << 8) + (data[2] << 16) + (data[3] << 24);
        if (vendorStringLength) {
            data += 4;
            idx += 4;
        }
        if (vendorStringLength > 495)
            vendorStringLength = 495;
        vendorString.assign((const char*)data, vendorStringLength);
        vendorString.insert("VENDOR_STRING: ", 0);
        info(*this, evt_id3data, "{}", vendorString.get());
        data += vendorStringLength;
        idx += vendorStringLength;
        size_t commentListLength = data[0] + (data[1] << 8) + (data[2] << 16) + (data[3] << 24);
        data += 4;
        idx += 4;

        for (int i = 0; i < commentListLength; i++) {
            (void)i;
            size_t commentLength = data[0] + (data[1] << 8) + (data[2] << 16) + (data[3] << 24);
            data += 4;
            idx += 4;
            if (commentLength) {
                commentString.assign((const char*)data, commentLength);
                if (commentString.starts_with("LYRICS="))
                    lyricsBuffer.clone_from(commentString);
                else
                    info(*this, evt_id3data, "{}", commentString.get());
            }
            data += commentLength;
            idx += commentLength;
            if (idx > vendorLength + 3) {
                AUDIO_LOG_ERROR("VORBIS COMMENT section is too long");
            }
        }
        ps_ptr<char> tmp;
        ps_ptr<char> timestamp;
        timestamp.alloc(12, "timestamp");
        if (lyricsBuffer.valid()) {
            lyricsBuffer.remove_prefix("LYRICS=");
            idx = 0;
            while (idx < lyricsBuffer.size()) {
                int pos = lyricsBuffer.index_of('\n', idx);
                if (pos == -1)
                    break;
                int len = pos - idx + 1;
                tmp.copy_from(lyricsBuffer.get() + idx, len);
                idx += len;
                if (tmp.ends_with("\n"))
                    tmp.truncate_at('\n');
                // tmp content e.g.: [00:27.07]Jetzt kommt die Zeit
                //                   [00:28.84]Auf die ihr euch freut
                timestamp.copy_from(tmp.get(), 10);

                int ms = parse_flac_timestamp(timestamp.c_get());

                tmp.remove_before(10, true);
                if (tmp.strlen() > 0) {
                    m_syltLines.push_back(std::move(tmp));
                    m_syltTimeStamp.push_back(ms);
                }
            }
            info(*this, evt_info, "audiofile contains synchronized lyrics");
        }
        m_controlCounter = FLAC_MBH;
        m_rflh.retvalue = vendorLength + 3;
        m_rflh.headerSize += m_rflh.retvalue;
        return 0;
    }
    if (m_controlCounter == FLAC_CUESHEET) { /* CUESHEET */
        size_t l = bigEndian(data, 3);
        m_controlCounter = FLAC_MBH;
        m_rflh.retvalue = l + 3;
        m_rflh.headerSize += m_rflh.retvalue;
        return 0;
    }
    if (m_controlCounter == FLAC_PICTURE) { /* PICTURE */
        m_rflh.picLen = bigEndian(data, 3);
        m_rflh.picPos = m_rflh.headerSize + 3;
        m_rflh.picVec.push_back(m_rflh.picPos);
        m_rflh.picVec.push_back(m_rflh.picLen);
        AUDIO_LOG_DEBUG("FLAC PICTURE, pos {}, size {}", m_rflh.picPos, m_rflh.picLen);
        m_controlCounter = FLAC_MBH;
        m_rflh.retvalue = m_rflh.picLen + 3;
        m_rflh.headerSize += m_rflh.retvalue;
        return 0;
    }
    return 0;
}

/**
 * @brief Liest die ID3-Tags schrittweise, m_controlCounter hält den Zustand
 */
int Audio::readID3Header(uint8_t* data, size_t len) {
    if (m_controlCounter == MP3_BEGIN) { /* ID3-Kennung und Header-Größe lesen */
        m_controlCounter = MP3_ID3HEADER;
        m_ID3Hdr.reset(); // alles zurücksetzen
        m_ID3Hdr.iBuffSize = 4096;
        m_ID3Hdr.iBuff.alloc(m_ID3Hdr.iBuffSize + 10, "m_ID3Hdr.iBuff");
        memset(m_ID3Hdr.tag, 0, sizeof(m_ID3Hdr.tag));
        return 0;
    }
    if (m_controlCounter == MP3_NEXTID3) {
        m_controlCounter = MP3_ID3HEADER;
        // gezielt zurücksetzen
        m_ID3Hdr.id3Size = 0;
        m_ID3Hdr.remainingHeaderBytes = 0;
        m_ID3Hdr.v22_tag_length = 0;
        m_ID3Hdr.ID3version = 0;
        m_ID3Hdr.ehsz = 0;
        m_ID3Hdr.framesize = 0;
        m_ID3Hdr.compressed = false;
        m_ID3Hdr.SYLT.size = 0;
        m_ID3Hdr.SYLT.pos = 0;
        m_ID3Hdr.iBuffSize = 4096;
        m_ID3Hdr.iBuff.alloc(m_ID3Hdr.iBuffSize + 10, "m_ID3Hdr.iBuff");
        memset(m_ID3Hdr.tag, 0, sizeof(m_ID3Hdr.tag));
        return 0;
    }

    if (m_controlCounter == MP3_ID3HEADER) {
        m_controlCounter = MP3_EXTHEADER;
        int retval = 0;
        if (!m_fM3u8Data)
            info(*this, evt_info, "File-Size: {}", m_audioFileSize);

        m_ID3Hdr.remainingHeaderBytes = 0;
        m_ID3Hdr.ehsz = 0;
        if (specialIndexOf(data, "ID3", 4) != 0) { // kein ID3-Tag
            if (!m_fM3u8Data) {
                info(*this, evt_info, "file has no ID3 tag, skip metadata");
            }
            m_audioDataSize = m_audioFileSize;
            m_controlCounter = MP3_XING; // gibt es einen Xing-Header?
            return 0;                    // keine ID3-Kennung gefunden
        }
        m_ID3Hdr.ID3version = *(data + 3);
        switch (m_ID3Hdr.ID3version) {
        case 2:
            m_fUnsync = (*(data + 5) & 0x80);
            m_fExthdr = false;
            break;
        case 3:
        case 4:
            m_fUnsync = (*(data + 5) & 0x80); // Bit 7
            m_fExthdr = (*(data + 5) & 0x40); // Bit 6: erweiterter Header
            break;
        };
        m_ID3Hdr.id3Size = bigEndian(data + 6, 4, 7); //  ID3v2 size  4 * %0xxxxxxx (shift left seven times!!)
        m_ID3Hdr.id3Size += 10;

        retval = 10;

        // ab hier kann jeder Lesevorgang Unsync-Bytes enthalten
        if (!m_fM3u8Data)
            info(*this, evt_info, "ID3 framesSize: {}", m_ID3Hdr.id3Size);
        if (!m_fM3u8Data)
            info(*this, evt_info, "ID3 version: 2.{}", m_ID3Hdr.ID3version);

        if (m_ID3Hdr.ID3version == 2) {
            m_controlCounter = MP3_ID3V22;
        }
        m_ID3Hdr.remainingHeaderBytes = m_ID3Hdr.id3Size;
        m_ID3Size = m_ID3Hdr.id3Size;
        m_ID3Hdr.remainingHeaderBytes -= 10;

        if (m_fExthdr) {
            m_ID3Hdr.ehsz = bigEndian(data + 10, 4, 7); // synchsafe kodiert
            info(*this, evt_info, "ID3 extended header, size: {}", m_ID3Hdr.ehsz);
            m_ID3Hdr.ehsz -= 4;
            m_ID3Hdr.remainingHeaderBytes -= 4;
            m_ID3Hdr.id3Size += 4;
            retval += 4;
        }
        return retval;
    }
    if (m_controlCounter == MP3_EXTHEADER) { // erweiterten Header überspringen, falls vorhanden
        if (m_ID3Hdr.ehsz > len) {
            m_ID3Hdr.ehsz -= len;
            m_ID3Hdr.remainingHeaderBytes -= len;
            return len;
        } // verwerfen
        else {
            m_controlCounter = MP3_ID3FRAME;
            m_ID3Hdr.remainingHeaderBytes -= m_ID3Hdr.ehsz;
            return m_ID3Hdr.ehsz;
        } // verwerfen
    }
    if (m_controlCounter == MP3_ID3FRAME) { // ID3-Rahmen lesen, die Kennung holen
        if (m_ID3Hdr.remainingHeaderBytes == 0) {
            m_controlCounter = MP3_XING;
            return 0;
        }
        m_controlCounter = MP3_FRAMESIZE;
        m_ID3Hdr.frameid[0] = *(data + 0);
        m_ID3Hdr.frameid[1] = *(data + 1);
        m_ID3Hdr.frameid[2] = *(data + 2);
        m_ID3Hdr.frameid[3] = *(data + 3);
        m_ID3Hdr.frameid[4] = 0;
        for (uint8_t i = 0; i < 4; i++)
            m_ID3Hdr.tag[i] = m_ID3Hdr.frameid[i]; // die Kennung ist die Rahmen-ID
        if (m_ID3Hdr.frameid[0] == 0 && m_ID3Hdr.frameid[1] == 0 && m_ID3Hdr.frameid[2] == 0 && m_ID3Hdr.frameid[3] == 0) {
            // wir stehen im Füllbereich
            m_controlCounter = MP3_LASTFRAMES; // alle ID3-Metadaten verarbeitet
        }
        m_ID3Hdr.remainingHeaderBytes -= 4;
        return 4;
    }
    if (m_controlCounter == MP3_FRAMESIZE) { // Rahmengröße lesen
        m_controlCounter = MP3_TAG;

        if (m_ID3Hdr.ID3version == 4) {
            m_ID3Hdr.framesize = bigEndian(data, 4, 7); // << 7
        } else {
            m_ID3Hdr.framesize = bigEndian(data, 4); // << 8
        }

        uint8_t frameFlag_0 = *(data + 4);
        uint8_t frameFlag_1 = *(data + 5);
        (void)frameFlag_0;
        m_ID3Hdr.compressed = frameFlag_1 & 0x80; // Rahmen ist zlib-komprimiert, 4 Bytes für die entpackte Größe
        uint32_t decompsize = 0;
        if (m_ID3Hdr.compressed) {
            decompsize = bigEndian(data + 6, 4);
            (void)decompsize;
            m_ID3Hdr.remainingHeaderBytes -= 6 + 4;
            return 6 + 4;
        }
        m_ID3Hdr.remainingHeaderBytes -= 6;
        return 6;
    }
    if (m_controlCounter == MP3_SKIP) { // ist der Rahmen größer als m_ID3Hdr.framesize, den Rest überspringen
        if (m_ID3Hdr.framesize > len) {
            m_ID3Hdr.framesize -= len;
            m_ID3Hdr.remainingHeaderBytes -= len;
            return len;
        } else {
            m_controlCounter = MP3_ID3FRAME; // nächsten Rahmen prüfen
            m_ID3Hdr.remainingHeaderBytes -= m_ID3Hdr.framesize;
            return m_ID3Hdr.framesize;
        }
    }
    if (m_controlCounter == MP3_TAG) { // den Wert lesen
        // Der Frame-Inhalt wird unten aus data[] kopiert, begrenzt auf iBuffSize. Liegt dieser Teil noch nicht vollständig im zusammenhängenden Lesefenster, auf mehr Daten warten: sonst liest die Kopie hinter das Fenster und meldet mehr verbrauchte Bytes, als der Aufrufer anbot. Der Ringpuffer verweigert eine solche Buchung ohne Rollback, und die Statemachine wäre dauerhaft gegen den Datenstrom versetzt.
        {
            size_t needed = m_ID3Hdr.framesize;
            if (needed > m_ID3Hdr.iBuffSize)
                needed = m_ID3Hdr.iBuffSize;
            if (needed > len)
                return 0;
        }
        m_controlCounter = MP3_SKIP;   // nur 256 Bytes lesen

        uint8_t textEncodingByte = *(data + 0); // ID3v2-Text-Encoding-Byte
        // 0x00: ISO-8859-1 (Latin-1, unter 0x80 identisch mit ASCII)
        // 0x01: UCS-2 mit BOM, in ID3v2.2 und ID3v2.3
        // 0x02: UTF-16BE ohne BOM, in ID3v2.4
        // 0x03: UTF-8, in ID3v2.4

        if (startsWith(m_ID3Hdr.tag, "APIC")) { // eingebettetes Bild, seine Lage wird gemeldet
            m_ID3Hdr.APIC_vec.push_back(m_ID3Hdr.totalId3Size + m_ID3Hdr.id3Size - m_ID3Hdr.remainingHeaderBytes);
            m_ID3Hdr.APIC_vec.push_back(m_ID3Hdr.framesize);
            AUDIO_LOG_DEBUG("APIC_pos {}, APIC_size {}", m_ID3Hdr.APIC_vec[2 * m_ID3Hdr.numID3Header], m_ID3Hdr.APIC_vec[2 * m_ID3Hdr.numID3Header + 1]);
            return 0;
        }

        if (startsWith(m_ID3Hdr.tag, "SYLT") || startsWith(m_ID3Hdr.tag, "USLT")) { // eingebetteter Liedtext, wird ausgewertet
            m_controlCounter = MP3_SYLT;
            return 0;
        }

        if ( // herstellereigene Rahmen ohne Standardbedeutung
            startsWith(m_ID3Hdr.tag, "PRIV")) {
            ; // AUDIO_LOG_ERROR("PRIV");
            return 0;
        }
        if (startsWith(m_ID3Hdr.tag, "TSIZ") || startsWith(m_ID3Hdr.tag, "XTRA")) { // Rahmen mit ungültiger Länge, alles überspringen und das erste Synchronwort suchen
            m_ID3Hdr.framesize = 0;
            size_t tmp = m_ID3Hdr.remainingHeaderBytes;
            m_ID3Hdr.remainingHeaderBytes = 0;
            return tmp;
        }

        if (m_ID3Hdr.framesize == 0)
            return 0;

        ps_ptr<char> tmp;
        tmp.set_name("tmp");
        ps_ptr<char> content_descriptor;
        content_descriptor.set_name("content_descriptor");
        size_t fs = m_ID3Hdr.framesize; // Größe des Datenfelds laut Rahmenkopf
        size_t bytesToCopy = fs;
        size_t textDataLength = 0;
        uint16_t idx = 0;

        if (bytesToCopy >= m_ID3Hdr.iBuffSize) {
            bytesToCopy = m_ID3Hdr.iBuffSize - 1;
        } // Platz für die Nullterminierung sicherstellen
        if (bytesToCopy > 0) {
            textDataLength = bytesToCopy - 1;
        } // nur wenn es Daten zum Kürzen gibt
        for (int i = 0; i < textDataLength; i++) {
            m_ID3Hdr.iBuff[i] = *(data + i + 1); // das erste Byte (Encoding) wird übersprungen
        }

        if (textEncodingByte == 1 || textEncodingByte == 2) { // UTF-16LE oder UTF-16BE
            m_ID3Hdr.iBuff[textDataLength] = 0;               // UTF-16 braucht eine doppelte Nullterminierung
            m_ID3Hdr.iBuff[textDataLength + 1] = 0;           // zweites '\0' für UTF-16
        } else {
            m_ID3Hdr.iBuff[textDataLength] = 0; // ein '\0' für ISO-8859-1 oder UTF-8
        }

        m_ID3Hdr.framesize -= fs;

        uint16_t dataLength = fs - 1;
        bool isBigEndian = (textEncodingByte == 2);

        char encodingTab[4][12] = {"ISO-8859-1", "UTF-16", "UTF-16BE", "UTF-8"};

        if (startsWith(m_ID3Hdr.tag, "COMM")) { // Sprachcode
            m_ID3Hdr.lang[0] = m_ID3Hdr.iBuff[0];
            m_ID3Hdr.lang[1] = m_ID3Hdr.iBuff[1];
            m_ID3Hdr.lang[2] = m_ID3Hdr.iBuff[2];
            m_ID3Hdr.lang[3] = '\0';

            idx = 4;
            if (textEncodingByte == 1 || textEncodingByte == 2)
                idx++;

            uint16_t cd_len = 0;
            if (textEncodingByte == 0)
                cd_len = 1 + content_descriptor.copy_from_iso8859_1((const uint8_t*)(m_ID3Hdr.iBuff.get() + idx)); // iso8859_1
            else if (textEncodingByte == 3)
                cd_len = 1 + content_descriptor.copy_from((const char*)(m_ID3Hdr.iBuff.get() + idx)); // utf-8
            else
                cd_len = 2 + content_descriptor.copy_from_utf16((const uint8_t*)(m_ID3Hdr.iBuff.get() + idx), isBigEndian); // utf-16
            if (cd_len > 2)
                info(*this, evt_info, "Comment: text encoding: {}, language {}, content_descriptor: {}", encodingTab[textEncodingByte], m_ID3Hdr.lang, content_descriptor.c_get());

            idx += cd_len;
        } else {
            if (textEncodingByte == 0) {
                tmp.copy_from_iso8859_1((const uint8_t*)m_ID3Hdr.iBuff.get() + idx);
            } // ISO-8859-1
            else if (textEncodingByte == 1 || textEncodingByte == 2) {
                tmp.copy_from_utf16((const uint8_t*)m_ID3Hdr.iBuff.get() + idx, isBigEndian);
            } // UTF-16LE oder UTF-16BE
            else if (textEncodingByte == 3) {
                tmp.copy_from(m_ID3Hdr.iBuff.get() + idx);
            } // UTF-8 direkt kopieren, keine Umwandlung nötig
        }
        showID3Tag(m_ID3Hdr.tag, tmp.c_get());
        m_ID3Hdr.remainingHeaderBytes -= fs;
        return fs;
    }
    if (m_controlCounter == MP3_SYLT) { // SYLT
        m_controlCounter = MP3_SKIP;
        if (m_dataMode == AUDIO_LOCALFILE || (m_streamType == ST_WEBFILE)) {
            ps_ptr<char> tmp;
            ps_ptr<char> content_descriptor;
            ps_ptr<char> syltBuff;
            bool isBigEndian = true;
            size_t len = 0;
            int idx = 0;
            m_ID3Hdr.SYLT.pos = m_ID3Hdr.id3Size - m_ID3Hdr.remainingHeaderBytes;
            m_ID3Hdr.SYLT.size = m_ID3Hdr.framesize;
            if (m_ID3Hdr.SYLT.size < len)
                return 0;
            syltBuff.copy_from((const char*)data, m_ID3Hdr.SYLT.size);
            m_ID3Hdr.SYLT.text_encoding = syltBuff[0]; // 0=ISO-8859-1, 1=UTF-16, 2=UTF-16BE, 3=UTF-8
            if (m_ID3Hdr.SYLT.text_encoding == 1)
                isBigEndian = false;
            if (m_ID3Hdr.SYLT.text_encoding > 3) {
                AUDIO_LOG_ERROR("unknown text encoding: {}", m_ID3Hdr.SYLT.text_encoding);
                m_ID3Hdr.SYLT.text_encoding = 0;
            }
            char encodingTab[4][12] = {"ISO-8859-1", "UTF-16", "UTF-16BE", "UTF-8"};
            memcpy(m_ID3Hdr.SYLT.lang, syltBuff.get() + 1, 3);
            m_ID3Hdr.SYLT.lang[3] = '\0';
            info(*this, evt_info, "Lyrics: text_encoding: {}, language: {}, size {}", encodingTab[m_ID3Hdr.SYLT.text_encoding], m_ID3Hdr.SYLT.lang, m_ID3Hdr.SYLT.size);
            m_ID3Hdr.SYLT.time_stamp_format = syltBuff[4];
            m_ID3Hdr.SYLT.content_type = syltBuff[5];
            idx = 6;
            uint16_t cd_len = 0;
            if (m_ID3Hdr.SYLT.text_encoding == 0)
                cd_len = 1 + content_descriptor.copy_from_iso8859_1((const uint8_t*)(syltBuff.get() + idx)); // iso8859_1
            else if (m_ID3Hdr.SYLT.text_encoding == 3)
                cd_len = 1 + content_descriptor.copy_from((const char*)(syltBuff.get() + idx)); // utf-8
            else
                cd_len = 2 + content_descriptor.copy_from_utf16((const uint8_t*)(syltBuff.get() + idx), isBigEndian); // utf-16
            if (cd_len > 2)
                info(*this, evt_info, "Lyrics: content_descriptor: {}", content_descriptor.c_get());

            idx += cd_len;
            while (idx < m_ID3Hdr.SYLT.size) {
                if (m_ID3Hdr.SYLT.text_encoding == 0)
                    idx += 1 + tmp.copy_from_iso8859_1((const uint8_t*)syltBuff.get() + idx); // ISO8859_1
                else if (m_ID3Hdr.SYLT.text_encoding == 3)
                    idx += 1 + tmp.copy_from((const char*)syltBuff.get() + idx); // UTF-8
                else
                    idx += 2 + tmp.copy_from_utf16((const uint8_t*)(syltBuff.get() + idx), isBigEndian); // UTF-16LE, UTF-16BE

                if (idx + 4 <= m_ID3Hdr.SYLT.size) {
                    if (tmp.starts_with("\n")) {
                        tmp.remove_before(1);
                    }
                    uint32_t timestamp = bigEndian((uint8_t*)syltBuff.get() + idx, 4);
                    m_syltLines.push_back(std::move(tmp));
                    m_syltTimeStamp.push_back(timestamp);
                }
                idx += 4;
            }
            info(*this, evt_info, "audiofile contains synchronized lyrics");
        }
        return 0;
    }

    // ab hier nur ID3 Version 2.2, höhere Versionen stehen oben
    // Rahmenaufbau nach https://mutagen-specs.readthedocs.io/en/latest/id3/id3v2.2.html
    if (m_controlCounter == MP3_ID3V22) { // Rahmen in Version 2.2: 3 Bytes Kennung, 3 Bytes Größe

        if (m_ID3Hdr.v22_tag_length > 0) {
            if (m_ID3Hdr.v22_tag_length > len) {
                m_ID3Hdr.v22_tag_length -= len;
                return len;
            } // verwerfen
            else {
                uint32_t t = m_ID3Hdr.v22_tag_length;
                m_ID3Hdr.v22_tag_length = 0;
                return t;
            } // verwerfen
        }

        if (m_ID3Hdr.remainingHeaderBytes == 0) {
            m_controlCounter = MP3_LASTFRAMES;
            return 0;
        }

        ps_ptr<char> tag;
        tag.set_name("tag");
        ps_ptr<char> value;
        value.set_name("value");
        if (data[0] == 0) { // wir stehen im Füllbereich
            m_controlCounter = MP3_LASTFRAMES;
            uint16_t padding = m_ID3Hdr.remainingHeaderBytes;
            m_ID3Hdr.remainingHeaderBytes = 0;
            return padding;
        }
        tag.copy_from((const char*)data, 3);
        m_ID3Hdr.v22_tag_length = bigEndian(data + 3, 3) + 6;
        value.copy_from((const char*)data + 7, min(m_ID3Hdr.v22_tag_length - 7, (size_t)1024));

        if (tag.starts_with("PIC")) { // eingebettetes Bild im Header
            size_t pic_len = bigEndian(data + 3, 3);
            uint32_t pic_start = m_ID3Hdr.totalId3Size + m_ID3Hdr.id3Size - m_ID3Hdr.remainingHeaderBytes;

            m_ID3Hdr.APIC_vec.push_back(pic_start + 6);
            m_ID3Hdr.APIC_vec.push_back(pic_len);
        } else if (startsWith(m_ID3Hdr.tag, "SLT")) { // eingebetteter Liedtext im Header
            if (m_dataMode == AUDIO_LOCALFILE) {

                ps_ptr<char> tmp;
                ps_ptr<char> content_descriptor;
                ps_ptr<char> syltBuff;
                bool isBigEndian = true;
                size_t len = 0;
                int idx = 0;

                m_ID3Hdr.SYLT.pos = m_ID3Hdr.id3Size - m_ID3Hdr.remainingHeaderBytes;
                m_ID3Hdr.SYLT.size = m_ID3Hdr.v22_tag_length;
                if (m_ID3Hdr.SYLT.size < len)
                    return 0;
                syltBuff.copy_from((const char*)data, m_ID3Hdr.SYLT.size);
                m_ID3Hdr.SYLT.text_encoding = syltBuff[0];
                memcpy(m_ID3Hdr.SYLT.lang, syltBuff.get() + 1, 3);
                m_ID3Hdr.SYLT.lang[3] = '\0';
                info(*this, evt_info, "Lyrics: text_encoding: {}, language: {}, size {}",
                     m_ID3Hdr.SYLT.text_encoding == 0   ? "ASCII"
                     : m_ID3Hdr.SYLT.text_encoding == 3 ? "UTF-8"
                                                        : "?",
                     m_ID3Hdr.SYLT.lang, m_ID3Hdr.SYLT.size);
                m_ID3Hdr.SYLT.time_stamp_format = syltBuff[4];
                m_ID3Hdr.SYLT.content_type = syltBuff[5];

                idx = 6;

                if (m_ID3Hdr.SYLT.text_encoding == 0 || m_ID3Hdr.SYLT.text_encoding == 3) { // utf-8
                    len = content_descriptor.copy_from((const char*)(syltBuff.get() + idx));
                } else { // utf-16
                    len = content_descriptor.copy_from_utf16((const uint8_t*)(syltBuff.get() + idx), isBigEndian);
                }
                if (len > 2)
                    info(*this, evt_info, "Lyrics: content_descriptor: {}", content_descriptor.c_get());
                idx += len;

                while (idx < m_ID3Hdr.SYLT.size) {
                    // UTF-16LE, UTF-16BE
                    if (m_ID3Hdr.SYLT.text_encoding == 1 || m_ID3Hdr.SYLT.text_encoding == 2) {
                        idx += tmp.copy_from_utf16((const uint8_t*)(syltBuff.get() + idx), isBigEndian);
                    } else {
                        // ISO-8859-1 / UTF-8
                        idx += tmp.copy_from((const char*)syltBuff.get() + idx);
                    }
                    if (idx + 4 <= m_ID3Hdr.SYLT.size) {
                        if (tmp.starts_with("\n"))
                            tmp.remove_before(1);
                        uint32_t timestamp = bigEndian((uint8_t*)syltBuff.get() + idx, 4);
                        m_syltLines.push_back(std::move(tmp));
                        m_syltTimeStamp.push_back(timestamp);
                    }

                    idx += 4;
                }
                info(*this, evt_info, "audiofile contains synchronized lyrics");
            }
        } else {
            showID3Tag(tag.c_get(), value.c_get());
        }

        m_ID3Hdr.remainingHeaderBytes -= m_ID3Hdr.v22_tag_length;

        return 0;
    }
    // Ende des Abschnitts für ID3 Version 2.2

    if (m_controlCounter == MP3_LASTFRAMES) { // restliche ID3-Metadaten überspringen, meist Füllbytes
        if (m_ID3Hdr.remainingHeaderBytes > len) {
            m_ID3Hdr.remainingHeaderBytes -= len;
            return len;
        } // verwerfen
        else {
            m_controlCounter = MP3_XING;
            return m_ID3Hdr.remainingHeaderBytes;
        } // verwerfen
    }
    if (m_controlCounter == MP3_XING) { //  exist another ID3tag?

        static const int samplerate_table[4][3] = {
            {11025, 12000, 8000},  // MPEG 2.5
            {0, 0, 0},             // reserviert
            {22050, 24000, 16000}, // MPEG 2
            {44100, 48000, 32000}  // MPEG 1
        };

        static const int samples_per_frame[4][4] = {// Layer:   0    1      2      3
                                                    /* V2.5 */ {0, 576, 1152, 384},
                                                    /* res. */ {0, 0, 0, 0},
                                                    /* V2   */ {0, 576, 1152, 384},
                                                    /* V1   */ {0, 1152, 1152, 384}};

        m_audioDataStart += m_ID3Hdr.id3Size;
        m_ID3Hdr.totalId3Size += m_ID3Hdr.id3Size;
        m_ID3Hdr.id3Size = 0;

        if ((*(data + 0) == 'I') && (*(data + 1) == 'D') && (*(data + 2) == '3')) {
            m_controlCounter = MP3_NEXTID3;
            m_ID3Hdr.numID3Header++;
            return 0;
        } else {
            // padding after ID3 body?
            uint32_t padding_counter = 0;
            while (padding_counter < len && data[padding_counter] == 0) {
                padding_counter++;
                m_audioDataStart++;
                m_audioDataSize--;
            }
            if (padding_counter)
                return padding_counter;

            m_controlCounter = MP3_OKAY; // 100 -> ok
            m_audioDataSize = m_audioFileSize - m_audioDataStart;

            // Kopf des ersten MP3-Rahmens: trägt er eine Xing/Info/VBRI-Kennung, liefern Rahmenzahl und Streamlänge die exakte Dauer, bei VBR zusätzlich die Sprungtabelle für positionsgenaues Spulen
            uint32_t hdr = bigEndian(data, 4);
            bool frameValid = ((hdr & 0xFFE00000) == 0xFFE00000);
            if (!frameValid)
                AUDIO_LOG_ERROR("Syncword not found");
            int versionID = (hdr >> 19) & 0x3;  // 0=MPEG2.5, 2=MPEG2, 3=MPEG1
            int layerIndex = (hdr >> 17) & 0x3; // 1=Layer3
            bool hasCrc = ((hdr >> 16) & 0x1) == 0;
            int bitrateIdx = (hdr >> 12) & 0xF;
            int samplerateIdx = (hdr >> 10) & 0x3;
            int paddingBit = (hdr >> 9) & 0x1;
            int mode = (hdr >> 6) & 0x3; // 0=stereo,3=mono
            if (versionID == 1 || layerIndex != 1 || samplerateIdx == 3)
                frameValid = false; // reservierte Werte, die Tabellenzugriffe wären ungültig

            uint32_t consumedFrameLen = 0;
            if (frameValid) {
                int samplerate = samplerate_table[versionID][samplerateIdx];
                int spf = samples_per_frame[versionID][layerIndex];

                // die Kennung sitzt hinter Rahmenkopf, CRC und Seiteninformationen
                uint32_t sideInfoLen;
                if (versionID == 3)
                    sideInfoLen = (mode == 3) ? 17 : 32; // MPEG1
                else
                    sideInfoLen = (mode == 3) ? 9 : 17; // MPEG2/2.5
                uint32_t tagPos = 4 + (hasCrc ? 2 : 0) + sideInfoLen;

                uint32_t frames = 0;
                uint32_t bytes = 0;
                bool tagFound = false;
                if (tagPos + 8 <= len && (memcmp(data + tagPos, "Xing", 4) == 0 || memcmp(data + tagPos, "Info", 4) == 0)) {
                    tagFound = true;
                    uint32_t flags = bigEndian(data + tagPos + 4, 4); // 1=Rahmenzahl, 2=Streamlänge, 4=Sprungtabelle, 8=Qualität
                    uint32_t off = tagPos + 8;
                    if ((flags & 0x01) && off + 4 <= len) {
                        frames = bigEndian(data + off, 4);
                        off += 4;
                    }
                    if ((flags & 0x02) && off + 4 <= len) {
                        bytes = bigEndian(data + off, 4);
                        off += 4;
                    }
                    if ((flags & 0x04) && off + 100 <= len) {
                        memcpy(m_xing.toc, data + off, 100);
                        m_xing.tocBase = m_audioDataStart; // der Kennungsrahmen zählt zum Stream, sein Anfang ist der Bezugspunkt
                        m_xing.tocBytes = bytes ? bytes : m_audioDataSize;
                        m_xing.hasToc = true;
                    }
                } else if (versionID == 3 && 4 + 32 + 18 <= (int)len && memcmp(data + 4 + 32, "VBRI", 4) == 0) {
                    // VBRI (Fraunhofer) sitzt fest 32 Bytes hinter dem Rahmenkopf, ohne Sprungtabelle in Xing-Form
                    tagFound = true;
                    bytes = bigEndian(data + 4 + 32 + 10, 4);
                    frames = bigEndian(data + 4 + 32 + 14, 4);
                }

                if (tagFound && frames) {
                    float durationSec = (float)frames * spf / samplerate;
                    m_audioFileDuration = round(durationSec);
                    info(*this, evt_info, "Duration (s): {}", m_audioFileDuration);
                    if (!bytes)
                        bytes = m_audioDataSize;
                    m_nominalBitrate = round((float)bytes * 8 / durationSec);
                    info(*this, evt_info, "Bitrate (b/s): {}", m_nominalBitrate);
                }

                // der Kennungsrahmen ist stumm und geht nicht an den Decoder
                if (tagFound && bitrateIdx > 0 && bitrateIdx < 15) {
                    static const uint16_t bitrate_table[2][16] = {
                        {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0}, // MPEG1 Layer III
                        {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0}};    // MPEG2/2.5 Layer III
                    uint32_t br = (uint32_t)bitrate_table[versionID == 3 ? 0 : 1][bitrateIdx] * 1000;
                    uint32_t frameLen = (uint32_t)spf / 8 * br / samplerate + paddingBit;
                    if (frameLen <= len)
                        consumedFrameLen = frameLen;
                }
            }

            bool coverSeek = false;
            if (m_ID3Hdr.APIC_vec.size()) { // eingebettete Bilder melden
                info(*this, evt_image, m_ID3Hdr.APIC_vec);
                m_ID3Hdr.APIC_vec.clear();
                AUDIO_LOG_DEBUG("m_ID3Hdr.totalId3Size {}, m_audioDataStart {}", m_ID3Hdr.totalId3Size, m_audioDataStart);
                if (m_ID3Hdr.totalId3Size != m_audioDataStart) {
                    audioFileSeek(m_audioDataStart);
                    coverSeek = true;
                }
            }
            m_ID3Hdr.numID3Header = 0;
            m_ID3Hdr.totalId3Size = 0;
            m_ID3Hdr.iBuff.reset();

            // nach einer Positions-Korrektur stimmen Puffer und Dateiposition nicht mehr überein, dann bleibt der Kennungsrahmen im Datenstrom
            if (coverSeek)
                return 0;
            m_audioDataStart += consumedFrameLen;
            m_audioDataSize -= consumedFrameLen;
            return consumedFrameLen;
        }
    }
    return 0;
}

/**
 * @brief Liest die M4A-Boxenstruktur schrittweise, m_controlCounter hält den Zustand
 */
int Audio::readM4aHeader(uint8_t* data, size_t len) {
    bool atom_struct = false;
    ps_ptr<char> atom_name;
    ps_ptr<char> atom_size;
    uint32_t idx = 0;

    /*
         ftyp
           | - moov  -> trak -> ... -> mp4a contains raw block parameters
           |    L... -> ilst  contains artist, composer ....
         free (optional) // Sprung zu weiteren Atomen hinter mdat
           |
         mdat contains the audio data                                                      */

    if (m_m4aHdr.retvalue) {
        if (m_m4aHdr.retvalue > UINT16_MAX) {
            int res = audioFileSeek(m_m4aHdr.headerSize);
            if (res >= 0) {
                AUDIO_LOG_INFO("skip {} bytes", m_m4aHdr.retvalue);
                m_inBuff.reset();
                m_m4aHdr.retvalue = 0;
                return 0;
            }
        }
        if (m_m4aHdr.retvalue > len) {
            m_m4aHdr.retvalue -= len;
            m_m4aHdr.cnt += len;
            return len;
        } else {
            size_t tmp = m_m4aHdr.retvalue;
            m_m4aHdr.retvalue = 0;
            m_m4aHdr.cnt += tmp;
            return tmp;
        }
    }
    if (m_controlCounter == M4A_BEGIN) {
        memset(&m_m4aHdr, 0, sizeof(audiolib::m4aHdr_t));

        atom_size.big_endian(data, 4);
        atom_name.copy_from((const char*)data + 4, 4);

        if (atom_name.equals("ftyp")) {
            if (atom_struct) {
                AUDIO_LOG_WARN("atom {} @ {}, size: {}, ends @ {}", atom_name.c_get(), m_m4aHdr.headerSize, (uint32_t)atom_size.to_uint32(16),
                               m_m4aHdr.headerSize + (size_t)atom_size.to_uint32(16));
                m_m4aHdr.sizeof_ftyp = atom_size.to_uint32(16);
            }
            m_controlCounter = M4A_FTYP;
        } else {
            AUDIO_LOG_ERROR("begin: ftyp not found");
            stopSong();
            return -1;
        }
    }
    if (m_controlCounter == M4A_FTYP) { /* M4A-Kennung prüfen */

        int m4a = specialIndexOf(data, "M4A ", 20);
        int isom = specialIndexOf(data, "isom", 20);
        int mp42 = specialIndexOf(data, "mp42", 20);

        if ((m4a != 8) && (isom != 8) && (mp42 != 8)) {
            AUDIO_LOG_ERROR("subtype 'MA4 ', 'isom' or 'mp42' expected, but found '{} '", (data + 8));
            stopSong();
            return -1;
        }

        m_m4aHdr.retvalue += m_m4aHdr.sizeof_ftyp;
        m_m4aHdr.headerSize += m_m4aHdr.sizeof_ftyp;
        m_controlCounter = M4A_CHK;
        return 0;
    }
    if (m_controlCounter == M4A_CHK) { /* Kennung prüfen */
        atom_size.big_endian(data, 4);
        atom_name.copy_from((const char*)data + 4, 4);

        if (atom_name.equals("moov")) {
            if (!m_m4aHdr.mdat_seen)
                m_m4aHdr.progressive = true; // moov liegt vor mdat
            if (atom_struct) {
                AUDIO_LOG_WARN("atom {} @ {}, size: {}, ends @ {}", atom_name.c_get(), m_m4aHdr.headerSize, atom_size.to_uint32(16), m_m4aHdr.headerSize + atom_size.to_uint32(16));
            }
            m_m4aHdr.sizeof_moov = atom_size.to_uint32(16) - 8;
            m_m4aHdr.retvalue += 8;
            m_m4aHdr.headerSize += 8;
            m_controlCounter = M4A_MOOV;
            return 0;
        }

        if (atom_name.equals("mdat")) {
            m_m4aHdr.mdat_seen = true;
            if (!m_m4aHdr.progressive) {
                m_m4aHdr.mdat_startPos = m_m4aHdr.headerSize + 8;
                m_m4aHdr.sizeof_mdat = atom_size.to_uint32(16);
                if (atom_struct) {
                    AUDIO_LOG_WARN("atom {} @ {}, size: {}, ends @ {}", atom_name.c_get(), m_m4aHdr.mdat_startPos, m_m4aHdr.sizeof_mdat,
                                   m_m4aHdr.mdat_startPos + m_m4aHdr.sizeof_mdat);
                }
                info(*this, evt_info, "Audiofile is non progressive");
                m_m4aHdr.retvalue += m_m4aHdr.sizeof_mdat;
                m_m4aHdr.headerSize += m_m4aHdr.sizeof_mdat;
                return 0;
                stopSong();
                return -1;
            }
            if (atom_struct) {
                AUDIO_LOG_WARN("atom {} @ {}, size: {}, ends @ {}", atom_name.c_get(), m_m4aHdr.headerSize, atom_size.to_uint32(16), m_m4aHdr.headerSize + atom_size.to_uint32(16));
            }
            m_m4aHdr.sizeof_mdat = atom_size.to_uint32(16) - 8;
            m_m4aHdr.retvalue += 8;
            m_m4aHdr.headerSize += 8;
            m_controlCounter = M4A_MDAT;
            return 0;
        }

        if (atom_struct) {
            AUDIO_LOG_WARN("atom {} @ {}, size: {}, ends @ {}", atom_name.c_get(), m_m4aHdr.headerSize, atom_size.to_uint32(16), m_m4aHdr.headerSize + atom_size.to_uint32(16));
        }
        m_m4aHdr.retvalue += atom_size.to_uint32(16);
        m_m4aHdr.headerSize += atom_size.to_uint32(16);
        m_m4aHdr.sizeof_moov -= atom_size.to_uint32(16);
        return 0;
    }
    if (m_controlCounter == M4A_MOOV) { // moov
        if (atom_struct) {
            AUDIO_LOG_WARN("moov size remain {}", m_m4aHdr.sizeof_moov);
        }
        if (m_m4aHdr.sizeof_moov == 0) {
            if (m_m4aHdr.progressive) {
                m_controlCounter = M4A_CHK;
                AUDIO_LOG_DEBUG("goto CHK");
            } else {
                audioFileSeek(0); // nicht progressiv, zurück zu mdat
                m_inBuff.reset();
                m_m4aHdr.headerSize = m_m4aHdr.mdat_startPos;
                m_m4aHdr.retvalue = m_m4aHdr.mdat_startPos; // Lesezeiger auf den Anfang der Audiodaten setzen
                if (atom_struct)
                    AUDIO_LOG_WARN("goto MDAT at {}", m_m4aHdr.mdat_startPos + 8);
                m_controlCounter = M4A_MDAT;
            }
            return 0;
        } // zurückgehen

        atom_name.copy_from((const char*)data + 4, 4);
        atom_size.big_endian(data, 4);
        m_m4aHdr.sizeof_moov -= atom_size.to_uint32(16);

        if (atom_name.equals("trak")) {
            if (atom_struct) {
                AUDIO_LOG_WARN("atom {} @ {}, size: {}, ends @ {}", atom_name.c_get(), m_m4aHdr.headerSize, atom_size.to_uint32(16), m_m4aHdr.headerSize + atom_size.to_uint32(16));
            }
            m_m4aHdr.sizeof_trak = atom_size.to_uint32(16) - 8;
            m_m4aHdr.retvalue += 8;
            m_m4aHdr.headerSize += 8;
            m_controlCounter = M4A_TRAK;
            return 0;
        }

        if (atom_name.equals("udta")) {
            if (atom_struct) {
                AUDIO_LOG_WARN("atom {} @ {}, size: {}, ends @ {}", atom_name.c_get(), m_m4aHdr.headerSize, atom_size.to_uint32(16), m_m4aHdr.headerSize + atom_size.to_uint32(16));
            }
            m_m4aHdr.sizeof_udta = atom_size.to_uint32(16) - 8;
            m_m4aHdr.retvalue += 8;
            m_m4aHdr.headerSize += 8;
            m_controlCounter = M4A_UDTA;
            return 0;
        }

        if (atom_struct) {
            AUDIO_LOG_WARN("atom {} @ {}, size: {}, ends @ {}", atom_name.c_get(), m_m4aHdr.headerSize, atom_size.to_uint32(16), m_m4aHdr.headerSize + atom_size.to_uint32(16));
        }
        m_m4aHdr.retvalue += atom_size.to_uint32(16);
        m_m4aHdr.headerSize += atom_size.to_uint32(16);
        return 0;
    }
    if (m_controlCounter == M4A_TRAK) { // trak
        if (atom_struct) {
            AUDIO_LOG_WARN("trak size remain {}", m_m4aHdr.sizeof_trak);
        }
        if (m_m4aHdr.sizeof_trak == 0) {
            m_controlCounter = M4A_MOOV;
            return 0;
        } // zurückgehen

        atom_name.copy_from((const char*)data + 4, 4);
        atom_size.big_endian(data, 4);
        m_m4aHdr.sizeof_trak -= atom_size.to_uint32(16);

        if (atom_name.equals("mdia")) {
            if (atom_struct) {
                AUDIO_LOG_WARN("atom {} @ {}, size: {}, ends @ {}", atom_name.c_get(), m_m4aHdr.headerSize, atom_size.to_uint32(16), m_m4aHdr.headerSize + atom_size.to_uint32(16));
            }
            m_m4aHdr.sizeof_mdia = atom_size.to_uint32(16) - 8;
            m_m4aHdr.retvalue += 8;
            m_m4aHdr.headerSize += 8;
            m_controlCounter = M4A_MDIA;
            return 0;
        }

        if (atom_struct) {
            AUDIO_LOG_WARN("atom {} @ {}, size: {}, ends @ {}", atom_name.c_get(), m_m4aHdr.headerSize, atom_size.to_uint32(16), m_m4aHdr.headerSize + atom_size.to_uint32(16));
        }
        m_m4aHdr.retvalue += atom_size.to_uint32(16);
        m_m4aHdr.headerSize += atom_size.to_uint32(16);
        return 0;
    }
    if (m_controlCounter == M4A_MDIA) { // mdia
        if (atom_struct) {
            AUDIO_LOG_WARN("mdia size remain {}", m_m4aHdr.sizeof_mdia);
        }
        if (m_m4aHdr.sizeof_mdia == 0) {
            m_controlCounter = M4A_TRAK;
            return 0;
        } // zurückgehen

        atom_name.copy_from((const char*)data + 4, 4);
        atom_size.big_endian(data, 4);
        m_m4aHdr.sizeof_mdia -= atom_size.to_uint32(16);

        if (atom_name.equals("mdhd")) {
            if (atom_struct) {
                AUDIO_LOG_WARN("atom {} @ {}, size: {}, ends @ {}", atom_name.c_get(), m_m4aHdr.headerSize, atom_size.to_uint32(16), m_m4aHdr.headerSize + atom_size.to_uint32(16));
            }
            m_m4aHdr.sizeof_mdhd = atom_size.to_uint32(16) - 8;
            m_m4aHdr.retvalue += 8;
            m_m4aHdr.headerSize += 8;
            m_controlCounter = M4A_MDHD;
            return 0;
        }

        if (atom_name.equals("minf")) {
            if (atom_struct) {
                AUDIO_LOG_WARN("atom {} @ {}, size: {}, ends @ {}", atom_name.c_get(), m_m4aHdr.headerSize, atom_size.to_uint32(16), m_m4aHdr.headerSize + atom_size.to_uint32(16));
            }
            m_m4aHdr.sizeof_minf = atom_size.to_uint32(16) - 8;
            m_m4aHdr.retvalue += 8;
            m_m4aHdr.headerSize += 8;
            m_controlCounter = M4A_MINF;
            return 0;
        }

        if (atom_struct) {
            AUDIO_LOG_WARN("atom {} @ {}, size: {}, ends @ {}", atom_name.c_get(), m_m4aHdr.headerSize, atom_size.to_uint32(16), m_m4aHdr.headerSize + atom_size.to_uint32(16));
        }
        m_m4aHdr.retvalue += atom_size.to_uint32(16);
        m_m4aHdr.headerSize += atom_size.to_uint32(16);
        return 0;
    }
    if (m_controlCounter == M4A_MINF) { // minf
        if (atom_struct) {
            AUDIO_LOG_WARN("minf size remain {}", m_m4aHdr.sizeof_minf);
        }
        if (m_m4aHdr.sizeof_minf == 0) {
            m_controlCounter = M4A_MDIA;
            return 0;
        } // zurückgehen

        atom_name.copy_from((const char*)data + 4, 4);
        atom_size.big_endian(data, 4);
        m_m4aHdr.sizeof_minf -= atom_size.to_uint32(16);

        if (atom_name.equals("stbl")) {
            if (atom_struct) {
                AUDIO_LOG_WARN("atom {} @ {}, size: {}, ends @ {}", atom_name.c_get(), m_m4aHdr.headerSize, atom_size.to_uint32(16), m_m4aHdr.headerSize + atom_size.to_uint32(16));
            }
            m_m4aHdr.sizeof_stbl = atom_size.to_uint32(16) - 8;
            m_m4aHdr.retvalue += 8;
            m_m4aHdr.headerSize += 8;
            m_controlCounter = M4A_STBL;
            return 0;
        }

        if (atom_struct) {
            AUDIO_LOG_WARN("atom {} @ {}, size: {}, ends @ {}", atom_name.c_get(), m_m4aHdr.headerSize, atom_size.to_uint32(16), m_m4aHdr.headerSize + atom_size.to_uint32(16));
        }
        m_m4aHdr.retvalue += atom_size.to_uint32(16);
        m_m4aHdr.headerSize += atom_size.to_uint32(16);
        return 0;
    }
    if (m_controlCounter == M4A_MDHD) { // mdhd
        if (atom_struct) {
            AUDIO_LOG_WARN("mdhd size remain {}", m_m4aHdr.sizeof_mdhd);
        }
        if (m_m4aHdr.sizeof_mdhd == 0) {
            m_controlCounter = M4A_MDIA;
            return 0;
        } // zurückgehen

        ps_ptr<char> mdhd_buffer;                                 // Box-Inhalt in einen Puffer lesen
        mdhd_buffer.copy_from((char*)data, m_m4aHdr.sizeof_mdhd); // mdhd-Inhalt ohne Kopf lesen
                                                                  /* version;           1 Byte  offset 0   0 -> 32 bit, 1 -> 64 bit
                                                                     flags[3];          3 Bytes offset 1
                                                                     creation_time;     4 Bytes offset 4
                                                                     modification_time, 4 Bytes offset 8
                                                                     timescale;         4 Bytes offset 12
                                                                     duration;          4 Bytes offset 16
                                                                     language;          2 Bytes offset 20
                                                                     pre_defined;       2 Bytes offset 22 */
        m_m4aHdr.timescale = bigEndian((uint8_t*)mdhd_buffer.get() + 12, 4);
        m_m4aHdr.duration = bigEndian((uint8_t*)mdhd_buffer.get() + 16, 4);
        if (m_m4aHdr.timescale) {
            m_audioFileDuration = m_m4aHdr.duration / m_m4aHdr.timescale;
            info(*this, evt_info, "Duration (s): {}", m_audioFileDuration);
        }
        m_m4aHdr.retvalue += m_m4aHdr.sizeof_mdhd;
        m_m4aHdr.headerSize += m_m4aHdr.sizeof_mdhd;
        m_controlCounter = M4A_MDIA;
        return 0;
    }
    if (m_controlCounter == M4A_STBL) { // minf
        if (atom_struct) {
            AUDIO_LOG_WARN("stbl size remain {}", m_m4aHdr.sizeof_stbl);
        }
        if (m_m4aHdr.sizeof_stbl == 0) {
            m_controlCounter = M4A_MINF;
            return 0;
        } // zurückgehen

        atom_size.big_endian(data, 4);
        atom_name.copy_from((const char*)data + 4, 4);
        m_m4aHdr.sizeof_stbl -= atom_size.to_uint32(16);

        if (atom_name.equals("stsd")) {
            if (atom_struct) {
                AUDIO_LOG_WARN("atom {} @ {}, size: {}, ends @ {}", atom_name.c_get(), m_m4aHdr.headerSize, atom_size.to_uint32(16), m_m4aHdr.headerSize + atom_size.to_uint32(16));
            }
            uint8_t header_size = 8;
            m_m4aHdr.sizeof_stsd = atom_size.to_uint32(16) - 8 - header_size;
            m_m4aHdr.retvalue += 8 + header_size;
            m_m4aHdr.headerSize += 8 + header_size;
            m_controlCounter = M4A_STSD;
            return 0;
        }

        if (atom_name.equals("stsz")) {
            if (atom_struct) {
                AUDIO_LOG_WARN("atom {} @ {}, size: {}, ends @ {}", atom_name.c_get(), m_m4aHdr.headerSize, atom_size.to_uint32(16), m_m4aHdr.headerSize + atom_size.to_uint32(16));
            }
            uint8_t header_size = 8;
            m_m4aHdr.sizeof_stsz = atom_size.to_uint32(16) - 8 - header_size;
            m_m4aHdr.retvalue += 8;
            m_m4aHdr.headerSize += 8;
            m_controlCounter = M4A_STSZ;
            return 0;
        }

        if (atom_struct) {
            AUDIO_LOG_WARN("atom {} @ {}, size: {}, ends @ {}", atom_name.c_get(), m_m4aHdr.headerSize, atom_size.to_uint32(16), m_m4aHdr.headerSize + atom_size.to_uint32(16));
        }
        m_m4aHdr.retvalue += atom_size.to_uint32(16);
        m_m4aHdr.headerSize += atom_size.to_uint32(16);
        return 0;
    }
    if (m_controlCounter == M4A_STSD) {
        if (atom_struct) {
            AUDIO_LOG_WARN("stsd size remain {}", m_m4aHdr.sizeof_stsd);
        }
        if (m_m4aHdr.sizeof_stsd == 0) {
            m_controlCounter = M4A_STBL;
            return 0;
        } // zurückgehen

        atom_size.big_endian(data, 4);
        atom_name.copy_from((const char*)data + 4, 4);
        m_m4aHdr.sizeof_stsd -= atom_size.to_uint32(16);

        if (atom_name.equals("mp4a")) {
            if (atom_struct) {
                AUDIO_LOG_WARN("atom {} @ {}, size: {}, ends @ {}", atom_name.c_get(), m_m4aHdr.headerSize, atom_size.to_uint32(16), m_m4aHdr.headerSize + atom_size.to_uint32(16));
            }
            m_m4aHdr.sizeof_mp4a = atom_size.to_uint32(16) - 8;
            m_m4aHdr.retvalue += 8;
            m_m4aHdr.headerSize += 8;
            m_controlCounter = M4A_MP4A;
            return 0;
        }

        if (atom_name.equals("alac")) {
            AUDIO_LOG_ERROR("Apple loseless audio codec (ALAC) not supported");
            stopSong();
            return 0;
        }

        if (atom_name.equals("ac-3")) {
            AUDIO_LOG_ERROR("Dolby Digital not supported");
            stopSong();
            return 0;
        }

        if (atom_name.equals("ec-3")) {
            AUDIO_LOG_ERROR("Dolby Digital Plud not supported");
            stopSong();
            return 0;
        }

        if (atom_name.equals("Opus")) {
           
            stopSong();
            return 0;
        }

        if (atom_name.equals("mp3 ")) {
           
            stopSong();
            return 0;
        }

        if (atom_struct) {
            AUDIO_LOG_WARN("atom {} @ {}, size: {}, ends @ {}", atom_name.c_get(), m_m4aHdr.headerSize, atom_size.to_uint32(16), m_m4aHdr.headerSize + atom_size.to_uint32(16));
        }
        m_m4aHdr.retvalue += atom_size.to_uint32(16);
        m_m4aHdr.headerSize += atom_size.to_uint32(16);
        return 0;
    }
    if (m_controlCounter == M4A_CHPL) {
        if (atom_struct) {
            AUDIO_LOG_WARN("chpl size remain {}", m_m4aHdr.sizeof_chpl);
        }
        if (m_m4aHdr.sizeof_chpl == 0) {
            m_controlCounter = M4A_UDTA;
            return 0;
        } // zurückgehen

        ps_ptr<char> title_name;
        // data[0]: Version
        // data[1]: 4 Flag-Bytes
        uint32_t nr_of_chapters = bigEndian(data + 5, 4); // Anzahl der Kapitel
        // data[9]: uint64-Zeitstempel
        uint8_t title_length = data[17];
        title_name.copy_from((char*)data + 18, title_length);

        info(*this, evt_info, "Number of chapters: {}", nr_of_chapters);
        info(*this, evt_info, "Chapter name: {}", title_name.c_get());

        m_m4aHdr.retvalue += m_m4aHdr.sizeof_chpl;
        m_m4aHdr.headerSize += m_m4aHdr.sizeof_chpl;
        m_m4aHdr.sizeof_chpl = 0; // keine Unter-Atome
        return 0;
    }
    if (m_controlCounter == M4A_MP4A) {
        if (atom_struct) {
            AUDIO_LOG_WARN("mp4a size remain {}", m_m4aHdr.sizeof_mp4a);
        }
        if (m_m4aHdr.sizeof_mp4a == 0) {
            m_controlCounter = M4A_STSD;
            return 0;
        } // zurückgehen

        if (!m_m4aHdr.sample_rate) { // erst den Kopf lesen
            // data[0] bis data[5]: reserviert
            // data[6] und data[7]: Data-Reference-Index
            // data[8] bis data[15]: reserviert
            m_m4aHdr.channel_count = data[16] * 256 + data[17];
            AUDIO_LOG_DEBUG("channels {}", m_m4aHdr.channel_count);
            m_m4aHdr.sample_size = data[18] * 256 + data[19];
            AUDIO_LOG_DEBUG("bit per sample {}", m_m4aHdr.sample_size);
            // data[20] bis data[23]: reserviert
            m_m4aHdr.sample_rate = data[24] * 256 + data[25];
            AUDIO_LOG_DEBUG("sample rate {}", m_m4aHdr.sample_rate);
            // data[26] und data[27]: Festkomma-Anteil der Abtastrate, immer 0x00 0x00
            m_m4aHdr.offset = 28;
            m_m4aHdr.retvalue += m_m4aHdr.offset;
            m_m4aHdr.headerSize += m_m4aHdr.offset;
            m_m4aHdr.sizeof_mp4a -= m_m4aHdr.offset;
            return 0;
        }
        atom_size.big_endian(data, 4);
        atom_name.copy_from((const char*)data + 4, 4);
        m_m4aHdr.sizeof_mp4a -= atom_size.to_uint32(16);

        if (atom_name.equals("esds")) {
            if (atom_struct) {
                AUDIO_LOG_WARN("atom {} @ {}, size: {}, ends @ {}", atom_name.c_get(), m_m4aHdr.headerSize, atom_size.to_uint32(16), m_m4aHdr.headerSize + atom_size.to_uint32(16));
            }
            m_m4aHdr.sizeof_esds = atom_size.to_uint32(16) - 8;
            m_m4aHdr.retvalue += 8;
            m_m4aHdr.headerSize += 8;
            m_controlCounter = M4A_ESDS;
            return 0;
        }

        if (atom_struct) {
            AUDIO_LOG_WARN("atom {} @ {}, size: {}, ends @ {}", atom_name.c_get(), m_m4aHdr.headerSize, atom_size.to_uint32(16), m_m4aHdr.headerSize + atom_size.to_uint32(16));
        }
        m_m4aHdr.retvalue += atom_size.to_uint32(16);
        m_m4aHdr.headerSize += atom_size.to_uint32(16);
        return 0;
    }
    if (m_controlCounter == M4A_UDTA) { // udta
        if (atom_struct) {
            AUDIO_LOG_WARN("udta size remain {}", m_m4aHdr.sizeof_udta);
        }
        if (m_m4aHdr.sizeof_udta == 0) {
            m_controlCounter = M4A_MOOV;
            return 0;
        } // zurückgehen

        atom_size.big_endian(data, 4);
        atom_name.copy_from((const char*)data + 4, 4);
        m_m4aHdr.sizeof_udta -= atom_size.to_uint32(16);

        if (atom_name.equals("meta")) {
            if (atom_struct) {
                AUDIO_LOG_WARN("atom {} @ {}, size: {}, ends @ {}", atom_name.c_get(), m_m4aHdr.headerSize, atom_size.to_uint32(16), m_m4aHdr.headerSize + atom_size.to_uint32(16));
            }
            m_m4aHdr.sizeof_meta = atom_size.to_uint32(16) - 8;
            m_m4aHdr.retvalue += 8;
            m_m4aHdr.headerSize += 8;
            m_controlCounter = M4A_META;
            return 0;
        }

        if (atom_name.equals("chpl")) {
            if (atom_struct) {
                AUDIO_LOG_WARN("atom {} @ {}, size: {}, ends @ {}", atom_name.c_get(), m_m4aHdr.headerSize, atom_size.to_uint32(16), m_m4aHdr.headerSize + atom_size.to_uint32(16));
            }
            m_m4aHdr.sizeof_chpl = atom_size.to_uint32(16) - 8;
            m_m4aHdr.retvalue += 8;
            m_m4aHdr.headerSize += 8;
            m_controlCounter = M4A_CHPL;
            return 0;
        }

        if (atom_struct) {
            AUDIO_LOG_WARN("atom {} @ {}, size: {}, ends @ {}", atom_name.c_get(), m_m4aHdr.headerSize, atom_size.to_uint32(16), m_m4aHdr.headerSize + atom_size.to_uint32(16));
        }
        m_m4aHdr.retvalue += atom_size.to_uint32(16);
        m_m4aHdr.headerSize += atom_size.to_uint32(16);
        return 0;
    }
    if (m_controlCounter == M4A_META) { // meta
        if (atom_struct) {
            AUDIO_LOG_WARN("meta size remain {}", m_m4aHdr.sizeof_meta);
        }
        if (m_m4aHdr.sizeof_meta == 0) {
            m_controlCounter = M4A_UDTA;
            return 0;
        } // zurückgehen, 4 Bytes Typ + 4 Bytes Größe

        if (!m_m4aHdr.version_flags) {
            // 0x00, 0x00, 0x00, 0x94, 0x6D, 0x65, 0x74, 0x61, 0x00, 0x00, 0x00, 0x00,
            //  Größe                |   m    e     t     a  |  Flags
            m_m4aHdr.version_flags = true;
            m_m4aHdr.sizeof_meta -= 4;
            m_m4aHdr.retvalue += 4;
            m_m4aHdr.headerSize += 4;
            idx += 4;
        }
        atom_size.big_endian(data + idx, 4);
        atom_name.copy_from((const char*)data + 4 + idx, 4);
        m_m4aHdr.sizeof_meta -= atom_size.to_uint32(16);

        if (atom_name.equals("ilst")) {
            if (atom_struct) {
                AUDIO_LOG_WARN("atom {} @ {}, size: {}, ends @ {}", atom_name.c_get(), m_m4aHdr.headerSize, atom_size.to_uint32(16), m_m4aHdr.headerSize + atom_size.to_uint32(16));
            }
            m_m4aHdr.sizeof_ilst = atom_size.to_uint32(16) - 8;
            m_m4aHdr.retvalue += 8;
            m_m4aHdr.headerSize += 8;
            m_controlCounter = M4A_ILST;
            return 0;
        }

        if (atom_struct) {
            AUDIO_LOG_WARN("atom {} @ {}, size: {}, ends @ {}", atom_name.c_get(), m_m4aHdr.headerSize, atom_size.to_uint32(16), m_m4aHdr.headerSize + atom_size.to_uint32(16));
        }
        m_m4aHdr.retvalue += atom_size.to_uint32(16);
        m_m4aHdr.headerSize += atom_size.to_uint32(16);
        return 0;
    }
    if (m_controlCounter == M4A_ESDS) {                           // Elementary Stream Descriptor (esds)
        ps_ptr<char> esds_buffer;                                 // Box-Inhalt in einen Puffer lesen
        esds_buffer.copy_from((char*)data, m_m4aHdr.sizeof_esds); // esds-Inhalt ohne Kopf lesen

        // decoderConfigDescriptor suchen (Kennung 0x04)
        int32_t dec_config_descriptor_offset = esds_buffer.special_index_of("\x04\x80\x80\x80", 4, (uint32_t)m_m4aHdr.sizeof_esds);
        if (dec_config_descriptor_offset > 0) {                                                                     // decoderConfigDescriptor gefunden
            uint8_t dec_config_descriptor_length = ((uint8_t*)esds_buffer.get())[dec_config_descriptor_offset + 4]; // Length after Tag + 3 Extended Length Bytes
            m_m4aHdr.objectTypeIndicator = ((uint8_t*)esds_buffer.get())[dec_config_descriptor_offset + 5];         // 0x40 (AAC)
            m_m4aHdr.streamType = ((uint8_t*)esds_buffer.get())[dec_config_descriptor_offset + 6];                  // 0x05 (Audio)
            m_m4aHdr.bufferSizeDB = bigEndian((uint8_t*)esds_buffer.get() + dec_config_descriptor_offset + 7, 3);   // 24 bit
            m_m4aHdr.maxBitrate = bigEndian((uint8_t*)esds_buffer.get() + dec_config_descriptor_offset + 10, 4);    // 32 bit
            m_m4aHdr.nomBitrate = bigEndian((uint8_t*)esds_buffer.get() + dec_config_descriptor_offset + 14, 4);    // 32 bit
        }

        // decoderSpecificInfo suchen (Kennung 0x05)
        int32_t dec_specific_offset = esds_buffer.special_index_of("\x05\x80\x80\x80", 4, m_m4aHdr.sizeof_esds);
        if (dec_specific_offset >= 0) {                                                           // decoderSpecificInfo gefunden
            uint8_t dec_specific_length = ((uint8_t*)esds_buffer.get())[dec_specific_offset + 4]; // Länge nach Tag + 3 Extended Length Bytes
            AUDIO_LOG_DEBUG("DecoderSpecificInfo found at offset {}, length: {}", dec_specific_offset, dec_specific_length);

            // Inhalt der decoderSpecificInfo auslesen
            ps_ptr<char> dec_specific_data;
            dec_specific_data.alloc(dec_specific_length, "dec_specific_data");
            memcpy(dec_specific_data.get(), (uint8_t*)esds_buffer.get() + dec_specific_offset + 5, dec_specific_length);
            m_m4aHdr.aac_profile = (uint8_t)dec_specific_data.get()[0] >> 3;
            if (m_m4aHdr.aac_profile > 4) {
                AUDIO_LOG_ERROR("unsupported AAC Profile {}", m_m4aHdr.aac_profile);
                stopSong();
                return 0;
            }
            AUDIO_LOG_DEBUG("DecoderSpecificInfo data: 0x{:02X} 0x{:02X}", (uint8_t)dec_specific_data.get()[0], (uint8_t)dec_specific_data.get()[1]);
            char profile[5][33] = {"unknown", "AAC Main", "AAC LC (Low Complexity)", "AAC SSR (Scalable Sample Rate)", "AAC LTP (Long Term Prediction)"};
            info(*this, evt_info, "AAC Profile: {}", profile[m_m4aHdr.aac_profile]);
            info(*this, evt_info, "AAC Channels: {}", m_m4aHdr.channel_count);
            info(*this, evt_info, "AAC Sample Rate: {}", m_m4aHdr.sample_rate);
            info(*this, evt_info, "AAC Bits Per Sample: {}", m_m4aHdr.sample_size);
            m_m4aChConfig = m_m4aHdr.channel_count;
            m_m4aSampleRate = m_m4aHdr.sample_rate;
            m_m4aObjectType = m_m4aHdr.aac_profile;
        } else {
            AUDIO_LOG_WARN("No DecoderSpecificInfo found in esds");
        }
        m_m4aHdr.retvalue += m_m4aHdr.sizeof_esds;
        m_m4aHdr.headerSize += m_m4aHdr.sizeof_esds;
        m_controlCounter = M4A_MP4A;
        return 0;
    }
    if (m_controlCounter == M4A_ILST) {

        auto parse_m4a_timestamp = [&](const char* s) {
            if (!s || *s != '[')
                return -1;
            int mm = 0, ss = 0, hh = 0;
            if (sscanf(s, "[%d:%d.%d]", &mm, &ss, &hh) == 3) {
                return mm * 60000 + ss * 1000 + hh * 10;
            }
            return -1;
        };

        struct TagInfo { // Zuordnung für Metadaten im iTunes-Stil
            const uint8_t tag[4];
            const char* name;
            const char* descr;
        };
        const TagInfo tags[] = {
            // alle üblichen Kennungen
            {{0xA9, 0x6E, 0x61, 0x6D}, "©nam", "Title"},      {{0xA9, 0x41, 0x52, 0x54}, "©ART", "Artist"},           {{0xA9, 0x61, 0x72, 0x74}, "©art", "Artist"},
            {{0xA9, 0x61, 0x6C, 0x62}, "©alb", "Album"},      {{0xA9, 0x74, 0x6F, 0x6F}, "©too", "Encoder"},          {{0xA9, 0x63, 0x6D, 0x74}, "©cmt", "Comment"},
            {{0xA9, 0x77, 0x72, 0x74}, "©wrt", "Composer"},   {{0x74, 0x6D, 0x70, 0x6F}, "tmpo", "Tempo (BPM)"},      {{0x74, 0x72, 0x6B, 0x6E}, "trkn", "Track-Number"},
            {{0xA9, 0x64, 0x61, 0x79}, "©day", "Year"},       {{0x63, 0x70, 0x69, 0x6C}, "cpil", "Compilation-Flag"}, {{0x61, 0x41, 0x52, 0x54}, "aART", "Album Artist"},
            {{0xA9, 0x67, 0x65, 0x6E}, "©gen", "Genre"},      {{0x63, 0x6F, 0x76, 0x72}, "covr", "Cover Art"},        {{0x64, 0x69, 0x73, 0x6B}, "disk", "Disk-Nummer"},
            {{0xA9, 0x6C, 0x79, 0x72}, "©lyr", "Songtext"},   {{0xA9, 0x70, 0x72, 0x74}, "cprt", "Copyright"},        {{0x67, 0x6E, 0x72, 0x65}, "gnre", "Genre-ID"},
            {{0x72, 0x74, 0x6E, 0x67}, "rtng", "Evaluation"}, {{0x70, 0x67, 0x61, 0x70}, "pgap", "Gapless Playback"},
        };
        const size_t tags_count = sizeof(tags) / sizeof(tags[0]); // Anzahl der Kennungen

        ps_ptr<char> id3tag;
        ps_ptr<char> lyricsBuffer;
        uint32_t pos = m_m4aHdr.headerSize;
        uint32_t consumed = 0;
        while (true) {
            atom_size.big_endian(data + consumed, 4);
            atom_name.copy_from((const char*)data + 4 + consumed, 4);
            uint32_t as = atom_size.to_uint32(16);
            AUDIO_LOG_DEBUG("atom {} @ {}, size: {}, ends @ {}", atom_name.c_get(), pos, as, pos + as);
            m_m4aHdr.ilst_pos += as;
            pos += as;
            consumed += as;

            // 0x00, 0x00, 0x00, 0x1C, 0xA9, 0x64, 0x61, 0x79, 0x00, 0x00, 0x00, 0x14, 0x64, 0x61, 0x74, 0x61, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x32, 0x30, 0x32, 0x32,
            //     Unter-Atom-Länge  |   ©    d     a     y  |  Unter-Unter-Atom-Länge  |  d     a     t     a  |  Datentyp 1 ist UTF-8 |  reserviert           |  2     0     2     2

            ps_ptr<char> sa; // Unter-Atom
            sa.copy_from((const char*)data + consumed - as, min(as, (uint32_t)1024));

            char san[5] = {0};  // Name des Unter-Atoms
            char ssan[5] = {0}; // Name des Unter-Unter-Atoms, erwartet wird 'data'
            strncpy(san, &sa[4], 4);
            strncpy(ssan, &sa[12], 4);
            uint32_t ssal = bigEndian((uint8_t*)&sa[8], 4); // Länge des Unter-Unter-Atoms
            uint32_t dty = bigEndian((uint8_t*)&sa[16], 4); // Datentyp 1 ist UTF-8
            if (strncmp(ssan, "data", 4) == 0) {
                for (int i = 0; i < tags_count; i++) {
                    if (memcmp(san, tags[i].tag, 4) == 0) {
                        id3tag.reset();
                        if (dty == 0 && strlen(&sa[24]) > 0) {
                            if (strcmp(san, "covr") == 0) {
                                m_m4aHdr.picLen = as - 24;
                                m_m4aHdr.picPos = pos + 24 - as;
                                AUDIO_LOG_DEBUG("cover jpeg start: {}, len {}", m_m4aHdr.picPos, m_m4aHdr.picLen);
                            }
                            id3tag.assignf("{}: {}", tags[i].descr, &sa[24]); // binär
                        } else if (dty == 1 && strlen(&sa[24]) > 0)
                            id3tag.assignf("{}: {}", tags[i].descr, &sa[24]); // UTF-8-Text
                        else if (dty == 0x0D) {
                            m_m4aHdr.picLen = as - 24;
                            m_m4aHdr.picPos = pos + 24 - as;
                            AUDIO_LOG_DEBUG("cover jpeg start: {}, len {}", m_m4aHdr.picPos, m_m4aHdr.picLen);
                        } // JPEG
                        else if (dty == 0x0E) {
                            m_m4aHdr.picLen = as - 24;
                            m_m4aHdr.picPos = pos + 24 - as;
                            AUDIO_LOG_WARN("cover png start: {}, len {}", m_m4aHdr.picPos, m_m4aHdr.picLen);
                        } // PNG
                        else if (dty == 21) {
                            if (memcmp(tags[i].tag, "cpil", 4) == 0) {
                                break;
                            } // Compilation-Kennzeichen
                            if (memcmp(tags[i].tag, "pgap", 4) == 0) {
                                break;
                            } // lückenlose Wiedergabe
                            if (memcmp(tags[i].tag, "rtng", 4) == 0) {
                                break;
                            } // Auswertung
                        }
                        if (id3tag.valid()) {
                            info(*this, evt_id3data, "{}", id3tag.get());
                        }
                        if (strcmp(tags[i].descr, "Songtext") == 0) {
                            lyricsBuffer.copy_from(&sa[24]);
                        }
                        break;
                    }
                }
            } else
                AUDIO_LOG_DEBUG("{} tag not supported", san);

            if (consumed > len + m_m4aHdr.ilst_already_consumed) { // die ilst-Box kann größer als len sein
                m_m4aHdr.ilst_already_consumed += len;
                m_m4aHdr.retvalue = consumed;
                m_m4aHdr.headerSize += consumed;
                m_controlCounter = M4A_ILST;
                return 0;
            }

            if (m_m4aHdr.sizeof_ilst <= m_m4aHdr.ilst_pos) {

                ps_ptr<char> tmp; // zum Schluss den Liedtext auswerten, falls vorhanden
                ps_ptr<char> timestamp;
                timestamp.alloc(12, "timestamp");
                if (lyricsBuffer.valid()) {
                    lyricsBuffer.remove_prefix("LYRICS=");
                    idx = 0;
                    while (idx < lyricsBuffer.size()) {
                        int pos = lyricsBuffer.index_of('\n', idx);
                        if (pos == -1)
                            break;
                        int len = pos - idx + 1;
                        tmp.copy_from(lyricsBuffer.get() + idx, len);
                        idx += len;
                        if (tmp.ends_with("\n"))
                            tmp.truncate_at('\n');
                        // tmp content e.g.: [00:27.07]Jetzt kommt die Zeit
                        //                   [00:28.84]Auf die ihr euch freut
                        timestamp.copy_from(tmp.get(), 10);

                        int ms = parse_m4a_timestamp(timestamp.c_get());

                        tmp.remove_before(10, true);
                        if (tmp.strlen() > 0) {
                            m_syltLines.push_back(std::move(tmp));
                            m_syltTimeStamp.push_back(ms);
                        }
                    }
                    info(*this, evt_info, "audiofile contains synchronized lyrics");
                }
                m_m4aHdr.retvalue = consumed;
                m_m4aHdr.headerSize += consumed;
                m_controlCounter = M4A_META;
                break;
            }
        }
        return 0;
    }
    if (m_controlCounter == M4A_STSZ) {
        uint8_t version = data[0];
        (void)version;
        uint32_t flags = bigEndian(data + 1, 3);
        (void)flags;
        uint32_t sample_size = bigEndian(data + 4, 4);
        (void)sample_size;
        m_m4aHdr.stsz_num_entries = bigEndian(data + 8, 4);
        m_m4aHdr.stsz_table_pos = m_m4aHdr.headerSize + 12;
        m_m4aHdr.retvalue += m_m4aHdr.sizeof_stsz + 8;
        m_m4aHdr.headerSize += m_m4aHdr.sizeof_stsz + 8;
        m_controlCounter = M4A_STBL;
        return 0;
    }
    uint8_t extLen = 0;
    if (m_controlCounter == M4A_MDAT) { // mdat
        m_audioDataSize = m_m4aHdr.sizeof_mdat; // Länge dieses Atoms

        // erweiterte Größenangabe
        // 00 00 00 01 6D 64 61 74 00 00 00 00 00 00 16 64
        //        0001  m  d  a  t                    5732

        if (m_audioDataSize == 1) { // erweiterte Größenangabe
            m_audioDataSize = bigEndian(data + 8, 8);
            m_audioDataSize -= 16;
            extLen = 8;
        } else {
            m_audioDataSize -= 8;
        }
        m_m4aHdr.retvalue = extLen;
        m_m4aHdr.headerSize += extLen;
        m_controlCounter = M4A_AMRDY; // letzter Schritt vor dem Start der Wiedergabe
        return 0;
    }

    if (m_controlCounter == M4A_AMRDY) { // fast fertig
        m_audioDataStart = m_m4aHdr.headerSize;
        if (m_m4aHdr.picLen) {
            size_t pos = m_audioFilePosition;
            std::vector<uint32_t> vec;
            vec.push_back(m_m4aHdr.picPos);
            vec.push_back(m_m4aHdr.picLen);
            info(*this, evt_image, vec);
            vec.clear();
            audioFileSeek(pos); // der Host kann die Dateiposition verstellt haben, zurücksetzen
        }
        m_stszNumEntries = m_m4aHdr.stsz_num_entries;
        m_stszPosition = m_m4aHdr.stsz_table_pos;
        if (m_audioFileDuration) {
            m_nominalBitrate = (m_audioDataSize * 8) / m_audioFileDuration;
            info(*this, evt_info, "Duration (s): {}", m_audioFileDuration);
        }

        m_controlCounter = M4A_OKAY; // fertig
        return 0;
    }
    // diese Stelle darf nie erreicht werden
    AUDIO_LOG_ERROR("error");
    return 0;
}

/**
 * @brief Wertet den ID3-Header am Anfang eines M3U8-Segments aus
 */
size_t Audio::processM3u8ID3Header(uint8_t* packet) {
    uint8_t ID3version;
    size_t id3Size;
    bool m_fUnsync = false, m_fExthdr = false;
    uint64_t current_timestamp = 0;

    (void)m_fUnsync;        // unterdrückt -Wunused-variable
    (void)current_timestamp; // unterdrückt -Wunused-variable

    if (specialIndexOf(packet, "ID3", 4) != 0) { // kein ID3-Tag
        return 0; // keine ID3-Kennung gefunden
    }
    ID3version = *(packet + 3);
    switch (ID3version) {
    case 2:
        m_fUnsync = (*(packet + 5) & 0x80);
        m_fExthdr = false;
        break;
    case 3:
    case 4:
        m_fUnsync = (*(packet + 5) & 0x80); // Bit 7
        m_fExthdr = (*(packet + 5) & 0x40); // Bit 6: erweiterter Header
        break;
    };
    id3Size = bigEndian(&packet[6], 4, 7); //  ID3v2 size  4 * %0xxxxxxx (shift left seven times!!)
    id3Size += 10;

    if (m_fExthdr) {
        AUDIO_LOG_ERROR("ID3 extended header in m3u8 files not supported");
        return 0;
    }

    if (specialIndexOf(&packet[10], "PRIV", 5) != 0) { // Rahmen PRIV nicht gefunden
        AUDIO_LOG_ERROR("tag PRIV in m3u8 Id3 Header not found");
        return 0;
    }
    // existiert der Rahmen PRIV, wird als Inhalt "com.apple.streaming.transportStreamTimestamp" angenommen
    // im Header wird dann ein Zeitstempel erwartet

    current_timestamp = (double)bigEndian(&packet[69], 4) / 90000; // Sekunden

    return id3Size;
}

/**
 * @brief Beendet die laufende Wiedergabe und meldet die Stopp-Position in der Datei
 */
uint32_t Audio::stopSong() {
    m_fLockInBuffer = true; // warten, bis die Dekodierung abgeschlossen ist
    uint8_t maxWait = 0;
    uint32_t currTime = getAudioCurrentTime();

    xSemaphoreTake(m_mutexAudioTaskIsDecoding, 1 * configTICK_RATE_HZ); // warten, bis der Audio-Task bereit ist
    {
        if (m_fRunning) {
            m_fRunning = false;
            if (m_client->connected()) {
                if (m_streamType == ST_WEBSTREAM) {
                    info(*this, evt_info, "Closing web stream \"{}\"", m_lastHost.c_get());
                }
                if (m_streamType == ST_WEBFILE) {
                    info(*this, evt_info, "Closing web file \"{}\"", m_lastHost.c_get());
                }
                m_client->stop();
            }
            if (m_audiofile) {
                info(*this, evt_info, "Closing audio file \"{}\"", m_audioFilePath.c_get());
                fclose(m_audiofile);
                m_audiofile = nullptr;
            }
        }
        destroyDecoder();
        m_fLockInBuffer = false;
    }
    xSemaphoreGive(m_mutexAudioTaskIsDecoding);
    return currTime;
}

/**
 * @brief Schaltet zwischen Pause und Wiedergabe um
 */
bool Audio::pauseResume() {
    xSemaphoreTake(m_mutexAudioTask, 0.3 * configTICK_RATE_HZ);
    bool retVal = false;
    if (m_dataMode == AUDIO_LOCALFILE || m_streamType == ST_WEBSTREAM || m_streamType == ST_WEBFILE) {
        m_fRunning = !m_fRunning;
        retVal = true;
        if (!m_fRunning) {
            m_outBuff.clear();
            memset(m_resamplesBuff.get(), 0, RESAMPLES_BUFF_SIZE * sizeof(int16_t)); // Sample-Puffer leeren
            m_validSamples = 0;
            m_plCh.count = 0; // angefangener Block ist mit dem Puffer verschwunden, siehe sendBytes()
            m_blockPrepared = false;
        }
    }
    xSemaphoreGive(m_mutexAudioTask);
    return retVal;
}

/**
 * @brief Berechnet die Butterworth-Tiefpasskoeffizienten des Resamplers im Q31-Format
 */
audiolib::BiquadCoeffs Audio::makeButterworthLPF_Q31(float fs) {

    float fc = 0.45f * fs;
    if (fc > 20000.0f)
        fc = 20000.0f; // absolute Obergrenze
    if (fc < 3000.0f)
        fc = 3000.0f; // absolute Untergrenze, verhindert einen ausgedünnten Klang

    constexpr float Q = 0.70710678f;

    float w0 = 2.0f * M_PI * fc / fs;
    float cw = cosf(w0);
    float sw = sinf(w0);
    float alpha = sw / (2.0f * Q);

    float b0 = (1.0f - cw) * 0.5f;
    float b1 = 1.0f - cw;
    float b2 = (1.0f - cw) * 0.5f;
    float a0 = 1.0f + alpha;
    float a1 = -2.0f * cw;
    float a2 = 1.0f - alpha;

    // normalisieren
    b0 /= a0;
    b1 /= a0;
    b2 /= a0;
    a1 /= a0;
    a2 /= a0;

    // convert to Q31
    constexpr float Q31 = 2147483648.0f; // 2^31

    audiolib::BiquadCoeffs c;
    c.b0 = (int32_t)lrintf(b0 * Q31);
    c.b1 = (int32_t)lrintf(b1 * Q31);
    c.b2 = (int32_t)lrintf(b2 * Q31);
    c.a1 = (int32_t)lrintf(a1 * Q31);
    c.a2 = (int32_t)lrintf(a2 * Q31);

    return c;
}

/**
 * @brief Rechnet die Samples auf die Ausgangs-Abtastrate um
 */
uint32_t Audio::resampleI2Soutput(audiolib::resampler_t& rs, int32_t* input, uint32_t inputSamples, int32_t* output) {

    auto lerp_q32 = [&](int32_t a, int32_t b, uint32_t frac) -> int32_t {
        return a + (int32_t)(((int64_t)(b - a) * frac) >> 32);
    };
    auto biquadProcess = [&](audiolib::Biquad& s, const audiolib::BiquadCoeffs& c, int32_t x) -> int32_t {
        // Q31-Signal mal Q31-Koeffizienten ergibt Q62-Akkumulator
        int64_t acc = (int64_t)c.b0 * x + s.z1;
        s.z1 = (int64_t)c.b1 * x - (int64_t)c.a1 * (acc >> 31) + s.z2;
        s.z2 = (int64_t)c.b2 * x - (int64_t)c.a2 * (acc >> 31);
        int64_t y = acc >> 31;
        if (y > INT32_MAX)
            return INT32_MAX;
        if (y < INT32_MIN)
            return INT32_MIN;
        return (int32_t)y;
    };

    uint32_t outFrames = 0;
    uint32_t i = 0;

    // mit dem letzten Sample des vorigen Blocks beginnen, falls vorhanden
    if (rs.hasLast && inputSamples > 0) {
        int32_t l0 = rs.lastL;
        int32_t r0 = rs.lastR;
        int32_t l1 = input[0];
        int32_t r1 = input[1];

        // mit dem alten Phasenwert weiterrechnen
        while ((rs.phase >> 32) == 0) {
            uint32_t frac = (uint32_t)rs.phase;
            int32_t l = lerp_q32(l0, l1, frac);
            int32_t r = lerp_q32(r0, r1, frac);

            l = biquadProcess(rs.lpLeft, rs.g_lpCoeffs, l);
            r = biquadProcess(rs.lpRight, rs.g_lpCoeffs, r);

            output[outFrames * 2] = l;
            output[outFrames * 2 + 1] = r;
            ++outFrames;

            rs.phase += rs.phaseStep;
        }
        rs.phase -= (1ULL << 32);
        // i bleibt 0, input[0] ist noch nicht als linker Startwert verbraucht
    }

    // Rest des Blocks wie üblich
    for (; i + 1 < inputSamples; ++i) {
        int32_t l0 = input[i * 2];
        int32_t r0 = input[i * 2 + 1];
        int32_t l1 = input[(i + 1) * 2];
        int32_t r1 = input[(i + 1) * 2 + 1];

        while ((rs.phase >> 32) == 0) {
            uint32_t frac = (uint32_t)rs.phase;
            int32_t l = lerp_q32(l0, l1, frac);
            int32_t r = lerp_q32(r0, r1, frac);

            l = biquadProcess(rs.lpLeft, rs.g_lpCoeffs, l);
            r = biquadProcess(rs.lpRight, rs.g_lpCoeffs, r);

            output[outFrames * 2] = l;
            output[outFrames * 2 + 1] = r;
            ++outFrames;

            rs.phase += rs.phaseStep;
        }
        rs.phase -= (1ULL << 32);
    }

    // letztes Sample für den nächsten Block aufheben
    if (inputSamples > 0) {
        rs.lastL = input[(inputSamples - 1) * 2];
        rs.lastR = input[(inputSamples - 1) * 2 + 1];
        rs.hasLast = true;
    }

    return outFrames;
}

/**
 * @brief Gibt die dekodierten Samples über I2S aus, läuft im zeitkritischen Ausgabepfad
 */
void IRAM_ATTR Audio::playChunk() {
    if (m_validSamples == 0)
        return; // nichts zu tun

    bool continueI2S = true;
    m_plCh.i2s_bytesConsumed = 0;
    m_plCh.err = ESP_OK;

    constexpr int BYTES_PER_SAMPLE = sizeof(int32_t);
    constexpr int BYTES_PER_STEREOFRAME = 2 * BYTES_PER_SAMPLE;

    // Ein Schreibversuch kann null Frames übernehmen, wenn alle DMA-Puffer belegt bleiben. Der Block darf dann kein zweites Mal durch Anzeige, Lautstärke, Klangregelung und Stereo-Breite laufen.
    if (m_blockPrepared)
        goto i2swrite;

    // Die Anzeigen des Anwendungscodes bekommen die Samples, bevor Lautstärke und Klangregelung
    // wirken. Ein VU-Instrument zeigt den Programmpegel, nicht den Lautsprecherpegel. Hinge sein
    // Ausschlag am Lautstärkeregler, verlöre der Bezugspunkt 0 VU seine Bedeutung.
    audio_visualizer_process_i2s(m_outBuff.get(), m_validSamples, &continueI2S);
    if (!continueI2S) {
        m_validSamples = 0;
        m_plCh.count = 0;
        return;
    }
    {
        const bool applyGain = settings.VOLUME_CONTROL && (m_audioItems.limiter[LEFTCHANNEL] != 1.0f || m_audioItems.limiter[RIGHTCHANNEL] != 1.0f);
        for (int i = 0; i < m_validSamples; i++) {
#if AUDIO_FEATURE_VU || AUDIO_FEATURE_SPECTRUM
            if (settings.VU_LEVEL || settings.SPECTRUM)
                calculateVUlevel(&m_outBuff[i * 2]);
#endif
            if (applyGain)
                applyVolume(&m_outBuff[i * 2]);
            if (settings.IIR_FILTER)
                iirFilter(&m_outBuff[i * 2]);
        }
    }
#if AUDIO_FEATURE_SPECTRUM
    if (settings.SPECTRUM)
        processSpectrum();
#endif
    if (!m_fForceMono && m_stereoWidthPct != 100)
        applyStereoWidth(m_outBuff.get(), m_validSamples);
    if (m_fForceMono)
        stereo2mono(m_outBuff.get(), m_validSamples);
    if (m_outputSr && m_outputSr != m_i2sItems.sampleRate)
        m_validSamples = resampleI2Soutput(m_resampler, m_outBuff.get(), m_validSamples, m_resamplesBuff.get()); // neue Sample-Anzahl nach dem Resampling
    m_blockPrepared = true;

i2swrite:
    if (m_outputSr && m_outputSr != m_i2sItems.sampleRate) { // mit Resampler
        m_plCh.err = i2s_channel_write(m_i2sTxHandle, m_resamplesBuff.get() + m_plCh.count, m_validSamples * BYTES_PER_STEREOFRAME, &m_plCh.i2s_bytesConsumed, 5);
    } else {                                                                                                                                                   // ohne Resampler
        m_plCh.err = i2s_channel_write(m_i2sTxHandle, m_outBuff.get() + m_plCh.count, m_validSamples * BYTES_PER_STEREOFRAME, &m_plCh.i2s_bytesConsumed, 5); //
    }



    if (m_plCh.err == ESP_ERR_INVALID_ARG)
        AUDIO_LOG_ERROR("NULL pointer or this handle is not tx handle");
    if (m_plCh.err == ESP_ERR_INVALID_STATE)
        AUDIO_LOG_ERROR("I2S is not ready to write");

#if AUDIO_LITE_DIAG
    m_diagI2sBytes += m_plCh.i2s_bytesConsumed;
    if (m_plCh.err == ESP_ERR_TIMEOUT)
        m_diagI2sTimeout++;
#endif
    m_validSamples -= m_plCh.i2s_bytesConsumed / BYTES_PER_STEREOFRAME;

    if (m_validSamples < 0) {
        AUDIO_LOG_ERROR("valid samples counter is negative: {}", m_validSamples);
        m_validSamples = 0;
    }

    if (m_validSamples)
        AUDIO_LOG_DEBUG("m_validSamples {}", m_validSamples);

    m_plCh.count += m_plCh.i2s_bytesConsumed / BYTES_PER_SAMPLE;

    if (m_validSamples == 0) {
        m_plCh.count = 0;
        m_blockPrepared = false;
    }

    return;
}

/**
 * @brief Treibt Transport, Zustandsmaschine und Ereignis-Zustellung an, wird vom Host regelmäßig aufgerufen
 */
void Audio::loop() {
#if AUDIO_LITE_DIAG
    // Statuszeile alle 500 ms: zeigt, an welcher Stelle die Pipeline steht. printf statt ESP_LOGW, weil das vorgebaute Framework mit CONFIG_LOG_MAXIMUM_LEVEL=1 alles oberhalb von ESP_LOGE wegkompiliert.
    {
        static uint32_t s_nextDiag = 0;
        const uint32_t now = audio_millis();
        if (now >= s_nextDiag) {
            s_nextDiag = now + 500;
            printf("[diag] mode=%d run=%d stream=%d dec=%d i2s=%d filled=%u wspace=%u avail=%d vs=%d sr=%u eof=%d\n",
                   (int)m_dataMode, (int)m_fRunning, (int)m_fStream, (int)(m_decoder != nullptr), (int)m_fI2sInit,
                   (unsigned)m_inBuff.bufferFilled(), (unsigned)m_inBuff.writeSpace(), m_client ? m_client->available() : -1,
                   (int)m_validSamples, (unsigned)getSampleRate(), (int)m_fEof);
            printf("[diag2] decOk=%u decFail=%u decMore=%u i2sKB=%u i2sTmo=%u\n",
                   (unsigned)m_diagDecOk, (unsigned)m_diagDecFail, (unsigned)m_diagDecMore,
                   (unsigned)(m_diagI2sBytes / 1024), (unsigned)m_diagI2sTimeout);
            m_diagDecOk = 0; m_diagDecFail = 0; m_diagDecMore = 0; m_diagI2sBytes = 0; m_diagI2sTimeout = 0;
        }
    }
#endif
    getInfo();
    if (!m_fRunning)
        return;

    if (m_fFirstLoop) {
        m_fFirstLoop = false;
        memset(&m_lVar, 0, sizeof(m_lVar));
    }

    if (m_playlistFormat != FORMAT_M3U8) { // normaler Ablauf
        switch (m_dataMode) {
        case AUDIO_LOCALFILE:
            processLocalFile();
            break;
        case HTTP_RESPONSE_HEADER:
            if (!parseHttpResponseHeader()) {
                if (m_fTimeout && m_lVar.count < 3) {
                    m_fTimeout = false;
                    m_lVar.count++;
                    connecttohost(m_lastHost.get());
                }
            } else {
                m_lVar.count = 0;
            }
            break;
        case AUDIO_PLAYLISTINIT:
            readPlayListData();
            break;
        case AUDIO_PLAYLISTDATA:
            if (m_playlistFormat == FORMAT_M3U)
                httpPrint(parsePlaylistM3U());
            if (m_playlistFormat == FORMAT_PLS)
                httpPrint(parsePlaylistPLS());
            if (m_playlistFormat == FORMAT_ASX)
                httpPrint(parsePlaylistASX());
            break;
        case AUDIO_DATA:
            if (m_streamType == ST_WEBSTREAM)
                processWebStream();
            if (m_streamType == ST_WEBFILE)
                processWebFile();
            break;
        }
    } else { // reiner M3U8-Datenstrom
        ps_ptr<char> host;
        switch (m_dataMode) {
        case HTTP_RESPONSE_HEADER:
            if (!parseHttpResponseHeader()) {
                if (m_lVar.count < 3) {
                    m_lVar.count++;
                    connecttohost(m_lastHost.get());
                } else {
                    stopSong();
                }
            } else {
                m_lVar.count = 0;
                m_fFirstCall = true; // ID3-Header wird in processWebStreamHLS/TS() gelesen
            }
            break;
        case AUDIO_PLAYLISTINIT:
            if (readPlayListData())
                break;
            else { // readPlayListData() == false bedeutet: mit der M3U8-URL verbinden
                httpPrint(m_m3u8Host.get());
                m_dataMode = HTTP_RESPONSE_HEADER; // jetzt liegt eine neue Playlist an
                break;
            }
        case AUDIO_PLAYLISTDATA:
            host = parsePlaylistM3U8();
            if (host.valid()) { // host enthält die nächste Playlist-URL
                httpPrint(host.get());
                m_dataMode = HTTP_RESPONSE_HEADER;
            } else { // host == NULL bedeutet: mit der M3U8-URL verbinden
                if (m_lVar.no_host_timer > audio_millis()) {
                    break;
                }
                if (m_fStream)
                    m_lVar.no_host_timer = audio_millis() + 10000;
                httpPrint(m_m3u8Host.get());
                m_dataMode = HTTP_RESPONSE_HEADER; // jetzt liegt eine neue Playlist an
            }
            break;
        case AUDIO_DATA:
            if (m_fTs) {
                processWebStreamTS(); // AAC oder MP3 in TS-Paketen
            } else {
                processWebStreamHLS(); // AAC oder MP3 ohne TS-Verpackung
            }

            if (m_fContinue) { // m_fContinue bedeutet, die Verarbeitung braucht mehr Daten
                m_dataMode = AUDIO_PLAYLISTDATA;
                m_fContinue = false;
            }
            break;
        }
    }
}

/**
 * @brief Liest die Playlist-Zeilen aus der Antwort in m_playlistContent
 */
bool Audio::readPlayListData() {

    int32_t chunkLen = 0;
    uint16_t readedBytes = 0;
    ps_ptr<char> pl;
    uint32_t ctl = 0;
    size_t plSize = 0;

    auto detectTimeout = [&]() -> bool {
        uint32_t t = audio_millis();
        while (!m_client->available()) {
            vTaskDelay(2);
            if (t + 2000 < audio_millis()) {
                AUDIO_LOG_WARN("Playlist is incomplete, fetch again");
                if (m_fChunked)
                    getChunkSize(0, true);
                return true;
            }
        }
        return false;
    };

    if (m_dataMode != AUDIO_PLAYLISTINIT) {
        AUDIO_LOG_ERROR("wrong datamode {}", m_dataModeStr[m_dataMode]);
        goto exit;
    }

    if (m_fChunked) {
        getChunkSize(0, true);
        chunkLen = getChunkSize(&readedBytes);
        if (chunkLen <= 0) {
            AUDIO_LOG_ERROR("chunked datatransfer but chunkLen is invalid");
            goto exit;
        }
        plSize = chunkLen;
    } else {
        plSize = m_audioFileSize;
    }

    if (!plSize) { // Playlist womöglich ohne Content-Length und ohne Chunk-Größe
        AUDIO_LOG_ERROR("file size is not given");
        goto exit;
    }

    pl.alloc(2048, "pl");
    // m_playlistContent vollständig leeren
    if (m_playlistFormat == FORMAT_M3U8 && !psram_found()) {
        AUDIO_LOG_ERROR("m3u8 playlists requires PSRAM enabled!");
    }
    vectorClearAndShrink(m_playlistContent);

    while (true) { // äußere Schleife
        uint32_t ctime = audio_millis();
        uint32_t timeout = 2000; // ms

        pl.clear(); // Playlist-Zeile

        while (true) { // innere Schleife
            uint16_t pos = 0;
            while (true) { // innerste Schleife
                uint32_t t = audio_millis();
                if (ctl == plSize)
                    break;
                if (detectTimeout())
                    goto exit;
                pl[pos] = audioFileRead();
                ctl++;
                if (pl[pos] == '\n') {
                    pl[pos] = '\0';
                    pos++;
                    break;
                }
                if (pl[pos] == '\r') {
                    pl[pos] = '\0';
                    pos++;
                    continue;
                }
                pos++;
                if (pos == 2044) {
                    pos--;
                    continue;
                }
                if (ctl == plSize) {
                    pl[pos] = '\0';
                    break;
                }
            }
            if (pos) {
                pl[pos] = '\0';
                break;
            }
            if (ctl == plSize)
                break;
            if (detectTimeout())
                goto exit;
        } // innere Schleife
        AUDIO_LOG_DEBUG("PL: {}", pl.c_get());
        if (pl.starts_with_icase("<!DOCTYPE")) {
            AUDIO_LOG_WARN("url is a webpage!");
            goto exit;
        }
        if (pl.starts_with_icase("<html")) {
            AUDIO_LOG_WARN("url is a webpage!");
            goto exit;
        }

        if (pl.starts_with_icase("#EXT-X-SERVER-CONTROL")) {
            /* The parameter "?_HLS_skip=YES" is an HLS delivery directive that allows a client to request a playlist delta update from the server in order to optimise data transmission.
               Functionality: The server replaces outdated media segments that lie beyond the "skip boundary" with an #EXT-X-SKIP tag, rather than resending the entire playlist content.
               Prerequisites: The playlist must contain an #EXT-X-SERVER-CONTROL tag with the CAN-SKIP-UNTIL attribute and must not be an #EXT-X-ENDLIST tag (i.e. a live stream).
               Effect: This drastically reduces the size of the playlist file and speeds up parsing, which is particularly critical for low-latency HLS with short segments and large DVR windows
               in order to save bandwidth and avoid playback disruptions.
            */
            if (m_playlistFormat == FORMAT_M3U8) {
                if (!m_m3u8Host.ends_with("HLS_skip=YES")) {
                    AUDIO_LOG_DEBUG("#EXT-X-SERVER-CONTROL found");
                    m_m3u8Host.append("?_HLS_skip=YES");
                    m_client->stop();
                    httpPrint(m_m3u8Host.c_get());
                    return true;
                }
            }
        }

        if (pl.size() > 0)
            m_playlistContent.emplace_back(pl);

        // Abbruchbedingungen:
        // 1. Der Antwort-Header nennt eine Content-Length: Zeichen bis zur angegebenen Länge lesen
        // 2. keine Content-Length, aber Transfer-Encoding chunked: Blockgröße bestimmen und bis dahin lesen
        // 3. weder Blockgröße noch Content-Length, aber Connection close: alles lesen, was kommt
        if (ctl == plSize) {
            if (m_fChunked) {
                chunkLen = getChunkSize(&readedBytes); // am Ende wird "\r\n\0\r\n\r\n" erwartet
                if (chunkLen > 0) {
                    plSize += chunkLen;
                    continue; // nächste Runde
                }
            }
            break;
        }
    } // äußere Schleife

    m_dataMode = AUDIO_PLAYLISTDATA;
    return true;

exit:
    vectorClearAndShrink(m_playlistContent);
    getChunkSize(0, true);
    return false;
}

/**
 * @brief Wertet eine M3U-Playlist aus und liefert die nächste URL
 */
const char* Audio::parsePlaylistM3U() {

    uint8_t lines = m_playlistContent.size();
    int pos = 0;
    char* host = nullptr;

    for (int i = 0; i < lines; i++) {
        if (m_playlistContent[i].contains("#EXTINF:")) { // Info-Zeile?
            pos = m_playlistContent[i].index_of(",");    // Komma in dieser Zeile?
            if (pos > 0) {
                // Interpret und Titel aus den Metadaten melden, falls vorhanden
                info(*this, evt_id3data, "{}", m_playlistContent[i].get() + pos + 1);
            }
            continue;
        }
        if (m_playlistContent[i].starts_with("#")) { // Kommentarzeile?
            continue;
        }

        pos = m_playlistContent[i].index_of("http://:@", 0); // Artefakt "http://:@" entfernen
        if (pos >= 0) {
            AUDIO_LOG_INFO("Entry in playlist found: {}", (m_playlistContent[i].get() + pos + 9));
            host = m_playlistContent[i].get() + pos + 9;
            break;
        }
        pos = m_playlistContent[i].index_of("http", 0); // nach "http" suchen
        if (pos >= 0) {                                 // enthält die URL "http://"?
            host = m_playlistContent[i].get() + pos;    // ja, neuen Host übernehmen
            break;
        }
    }
    return host;
}

/**
 * @brief Wertet eine PLS-Playlist aus und liefert die nächste URL
 */
const char* Audio::parsePlaylistPLS() {
    uint8_t lines = m_playlistContent.size();
    int pos = 0;
    char* host = nullptr;

    for (int i = 0; i < lines; i++) {
        if (i == 0) {
            if (m_playlistContent[0].strlen() == 0)
                goto exit;                                         // Leerzeile
            if (!m_playlistContent[0].starts_with("[playlist]")) { // erste Zeile einer gültigen PLS-Playlist
                m_dataMode = HTTP_RESPONSE_HEADER;                 // PLS ist ungültig
                AUDIO_LOG_INFO("Playlist is not valid, switch to HTTP_RESPONSE_HEADER");
                goto exit;
            }
            continue;
        }
        if (m_playlistContent[i].starts_with("File1")) {
            if (host)
                continue;                                   // URL ist schon gefunden
            pos = m_playlistContent[i].index_of("http", 0); // File1=http://streamplus30.leonex.de:14840/;
            if (pos >= 0) {                                 // enthält die Zeile eine URL?
                host = m_playlistContent[i].get() + pos;    // host enthält jetzt eine Stream-URL
            }
            continue;
        }
        if (m_playlistContent[i].starts_with("Title1")) { // Title1=Antenne Tirol
            const char* plsStationName = (m_playlistContent[i].get() + 7);
            info(*this, evt_name, "{}", plsStationName);
            continue;
        }
        if (m_playlistContent[i].starts_with("Length1")) {
            continue;
        }
        if (m_playlistContent[i].contains("Invalid username")) { // Beispiel einer Fehlerzeile: Unable to access account
            goto exit;                                           // Benutzername oder Passwort ungültig
        }
    }
    return host;

exit:
    m_fRunning = false;
    stopSong();
    vectorClearAndShrink(m_playlistContent);
    m_dataMode = AUDIO_NONE;
    return nullptr;
}

/**
 * @brief Wertet eine ASX-Playlist (Advanced Stream Redirector) aus und liefert die nächste URL
 */
const char* Audio::parsePlaylistASX() {
    uint8_t lines = m_playlistContent.size();
    bool f_entry = false;
    int pos = 0;
    char* host = nullptr;

    for (int i = 0; i < lines; i++) {
        int p1 = m_playlistContent[i].index_of("<", 0);
        int p2 = m_playlistContent[i].index_of(">", 1);
        if (p1 >= 0 && p2 > p1) { // alles zwischen < und > auf Kleinbuchstaben normalisieren
            for (uint8_t j = p1; j < p2; j++) {
                m_playlistContent[i][j] = static_cast<char>(tolower(static_cast<unsigned char>(m_playlistContent[i][j])));
            }
        }
        if (m_playlistContent[i].contains("<entry>"))
            f_entry = true; // entry-Tag gefunden
        if (f_entry) {
            if (m_playlistContent[i].contains("ref href")) { //  <ref href="http://87.98.217.63:24112/stream" />
                pos = m_playlistContent[i].index_of("http", 0);
                if (pos > 0) {
                    host = (m_playlistContent[i].get() + pos); // http://87.98.217.63:24112/stream" />
                    int pos1 = indexOf(host, "\"", 0);         // http://87.98.217.63:24112/stream
                    if (pos1 > 0)
                        host[pos1] = '\0'; // host enthält jetzt eine Stream-URL
                }
            }
        }
        pos = m_playlistContent[i].index_of("<title>", 0);
        if (pos >= 0) {
            char* plsStationName = (m_playlistContent[i].get() + pos + 7); // <Title> entfernen
            pos = indexOf(plsStationName, "</", 0);
            if (pos >= 0) {
                *(plsStationName + pos) = 0; // </Title> entfernen
            }
            info(*this, evt_name, "{}", plsStationName);
        }

        if (m_playlistContent[i].starts_with("http") && !f_entry) { // nackte URL in der ASX
            host = m_playlistContent[i].get();
        }
    }
    return host;
}

/**
 * @brief Vervollständigt relative M3U8-URLs zu absoluten
 */
uint16_t Audio::accomplishM3u8Url() {

    for (uint16_t i = 0; i < m_linesWithURL.size(); i++) {

        ps_ptr<char> tmp;

        if (!m_linesWithURL[i].starts_with("http")) { //  playlist:   http://station.com/aaa/bbb/xxx.m3u8
                                                      //  chunklist:  http://station.com/aaa/bbb/ddd.aac
                                                      //  result:     http://station.com/aaa/bbb/ddd.aac
            tmp = m_m3u8Host;
            if (m_linesWithURL[i][0] != '/') { //  playlist:   http://station.com/aaa/bbb/xxx.m3u8 
                                               //  chunklist:  ddd.aac                              // m_linesWithURL[i]
                                               //  result:     http://station.com/aaa/bbb/ddd.aac   // m_linesWithURL[i]
                int idx = tmp.last_index_of('/');
                tmp[idx + 1] = '\0';
                tmp.append(m_linesWithURL[i].get());
                m_linesWithURL[i].clone_from(tmp);
                continue;
            } else { //  playlist:   http://station.com/aaa/bbb/xxx.m3u8
                     //  chunklist:  /aaa/bbb/ddd.aac
                     //  result:     http://station.com/aaa/bbb/ddd.aac
                int idx = tmp.index_of('/', 8);
                tmp[idx] = '\0';
                tmp.append(m_linesWithURL[i].get());
                m_linesWithURL[i].clone_from(tmp);
                continue;
            }
        } else { /* nichts zu tun */
            continue;
        }
    }
    return 0;
}

/**
 * @brief Bereitet die erste M3U8-URL aus der Master-Playlist vor
 */
int16_t Audio::prepareFirstM3u8Url(ps_ptr<char>& playlistBuff) {
    // sucht in m_linesWithURL die erste noch nicht gespielte URL.
    // Bereits benutzte URLs werden aus der Deque entfernt.
    // Sind alle URLs neu, ist nichts zu tun und 0 wird geliefert.
    // Wird keine neue URL gefunden, wird -1 geliefert.
    int idx = -1;
    for (int i = 0; i < m_linesWithURL.size(); i++) {
        if (playlistBuff.equals(m_linesWithURL[i].get()))
            idx = i;
    }
    if (idx == -1) {
        AUDIO_LOG_DEBUG("nothing found, all entries are new");
        return 0;
    }
    if (idx == m_linesWithURL.size()) {
        AUDIO_LOG_DEBUG("only last entry found, nothing is new");
        return -1;
    }
    for (int j = 0; j < idx + 1; j++) {
        m_linesWithURL.pop_front();
    }
    AUDIO_LOG_DEBUG("{} entries are known and removed", idx + 1);
    return idx + 1;
}

/**
 * @brief Wertet eine M3U8-Playlist aus und liefert die URL des nächsten Segments
 */
ps_ptr<char> Audio::parsePlaylistM3U8() {
    // Beispiel: Audio-Blöcke
    // #EXTM3U
    // #EXT-X-TARGETDURATION:10
    // #EXT-X-MEDIA-SEQUENCE:163374040
    // #EXT-X-DISCONTINUITY
    // #EXTINF:10,title="text=\"Spot Block End\" amgTrackId=\"9876543\"",artist=" ",url="length=\"00:00:00\""
    // http://n3fa-e2.revma.ihrhls.com/zc7729/63_sdtszizjcjbz02/main/163374038.aac
    // #EXTINF:10,title="text=\"Spot Block End\" amgTrackId=\"9876543\"",artist=" ",url="length=\"00:00:00\""
    // http://n3fa-e2.revma.ihrhls.com/zc7729/63_sdtszizjcjbz02/main/163374039.aac

    // #EXTM3U
    // #EXT-X-VERSION:3
    // #EXT-X-MEDIA-SEQUENCE:0
    // #EXT-X-TARGETDURATION:4
    // #EXTINF:3.997,90s90s - Rock
    // #EXT-X-PROGRAM-DATE-TIME:2025-08-17T14:08:21.088877044Z
    // https://hz71.streamabc.net/hls/d2gu4l4peavc72tgotd0/regc-90s90srock1436287-mp3-192-2191420/0.mp3
    // #EXTINF:3.997,90s90s - Rock
    // #EXT-X-PROGRAM-DATE-TIME:2025-08-17T14:08:24.088877044Z
    // https://hz71.streamabc.net/hls/d2gu4l4peavc72tgotd0/regc-90s90srock1436287-mp3-192-2191420/1.mp3
    // #EXTINF:3.997,90s90s - Rock
    // #EXT-X-PROGRAM-DATE-TIME:2025-08-17T14:08:28.088877044Z
    // https://hz71.streamabc.net/hls/d2gu4l4peavc72tgotd0/regc-90s90srock1436287-mp3-192-2191420/2.mp3
    // #EXTINF:3.997,90s90s - Rock
    // #EXT-X-PROGRAM-DATE-TIME:2025-08-17T14:08:32.088877044Z
    // https://hz71.streamabc.net/hls/d2gu4l4peavc72tgotd0/regc-90s90srock1436287-mp3-192-2191420/3.mp3

    if (!m_lastHost.valid()) {
        AUDIO_LOG_ERROR("m_lastHost is NULL");
        return {};
    }

    uint32_t lines = m_playlistContent.size();
    bool f_haveRedirection = false;

    if (lines) {
        bool addNextLine = false;
        dequeClearAndShrink(m_linesWithURL);
        vectorClearAndShrink(m_linesWithEXTINF);
        for (uint32_t i = 0; i < lines; i++) {
            if (m_playlistContent[i].starts_with("#EXT-X-STREAM-INF:")) {
                f_haveRedirection = true;
            }
            if (addNextLine) {
                if (m_playlistContent[i].starts_with("#EXT-X-PROGRAM-DATE-TIME:"))
                    continue; // Zeile überspringen
                addNextLine = false;
                m_linesWithURL.emplace_back(m_playlistContent[i]);
            }
            if (m_playlistContent[i].starts_with("#EXTINF:")) {
                m_linesWithEXTINF.emplace_back(m_playlistContent[i]);
                addNextLine = true;
            }
        }
        if (!f_haveRedirection) {
            accomplishM3u8Url();
            prepareFirstM3u8Url(m_playlistBuff);
            vectorClearAndShrink(m_playlistContent);
        }
    }

    for (int i = 0; i < m_linesWithURL.size(); i++) { /*AUDIO_LOG_INFO("{}", m_linesWithURL[i].get())*/
        ;
    }
    for (int i = 0; i < m_linesWithEXTINF.size(); i++) { /*AUDIO_LOG_INFO("{}", m_linesWithEXTINF[i].get());*/
        showstreamtitle(m_linesWithEXTINF[i].get());
    }

    if (f_haveRedirection) {
        m_m3u8Host = m3u8Redirection(&m_m3u8Codec);
        vectorClearAndShrink(m_playlistContent);
        return {};
    }

    if (m_codec == CODEC_NONE) {
        m_codec = CODEC_AAC;
        if (m_m3u8Codec == CODEC_MP3)
            m_codec = CODEC_MP3;
    } // keine Umleitung vorhanden

    if (m_linesWithURL.size() > 0) {
        ps_ptr<char> playlistBuff;
        if (m_linesWithURL[0].valid()) {
            playlistBuff = m_linesWithURL[0];
            m_linesWithURL.pop_front();
            m_linesWithURL.shrink_to_fit();
        }
        AUDIO_LOG_DEBUG("now playing {}", playlistBuff.get());
        if (playlistBuff.ends_with("ts"))
            m_fTs = true;
        if (playlistBuff.contains(".ts?") > 0)
            m_fTs = true;
        m_playlistBuff = playlistBuff;
        return playlistBuff;
    }
    return {};
}

/**
 * @brief Folgt der Umleitung einer M3U8-Master-Playlist zur Medien-Playlist
 */
ps_ptr<char> Audio::m3u8Redirection(uint8_t* codec) {
    // Beispiel: Umleitung
    // #EXTM3U
    // #EXT-X-STREAM-INF:BANDWIDTH=117500,AVERAGE-BANDWIDTH=117000,CODECS="mp4a.40.2"
    // 112/playlist.m3u8?hlssid=7562d0e101b84aeea0fa35f8b963a174
    // #EXT-X-STREAM-INF:BANDWIDTH=69500,AVERAGE-BANDWIDTH=69000,CODECS="mp4a.40.5"
    // 64/playlist.m3u8?hlssid=7562d0e101b84aeea0fa35f8b963a174
    // #EXT-X-STREAM-INF:BANDWIDTH=37500,AVERAGE-BANDWIDTH=37000,CODECS="mp4a.40.29"
    // 32/playlist.m3u8?hlssid=7562d0e101b84aeea0fa35f8b963a174

    if (!m_lastHost.valid()) {
        AUDIO_LOG_ERROR("m_lastHost is empty");
        return {};
    }
    const char codecString[9][11] = {
        "mp4a.40.34", // MP3-Stream
        "mp4a.40.01", // AAC Main
        "mp4a.40.2",  // MPEG-4 AAC LC
        "mp4a.40.02", // MPEG-4 AAC LC, führende 0 für Aud-OTI-Kompatibilität
        "mp4a.40.29", // MPEG-4 HE-AAC v2 (AAC LC + SBR + PS)
        "mp4a.40.42", // xHE-AAC
        "mp4a.40.5",  // MPEG-4 HE-AAC v1 (AAC LC + SBR)
        "mp4a.40.05", // MPEG-4 HE-AAC v1 (AAC LC + SBR), führende 0 für Aud-OTI-Kompatibilität
        "mp4a.67",    // MPEG-2 AAC LC
    };

    uint16_t choosenLine = 0;
    uint16_t plcSize = m_playlistContent.size();
    int8_t cS = 100;

    for (uint16_t i = 0; i < plcSize; i++) { // niedrigste Codec-Kennung suchen
        if (m_playlistContent[i].contains("CODECS=\"mp4a")) {
            for (uint8_t j = 0; j < 9; j++) {
                if (m_playlistContent[i].contains(codecString[j])) {
                    if (j < cS) {
                        cS = j;
                        choosenLine = i;
                    }
                }
            }
        }
    }
    if (cS == 0)
        *codec = CODEC_MP3;
    else if (cS < 100)
        *codec = CODEC_AAC;

    if (cS == 100) {                             // "mp4a.xx.xx" nicht gefunden
        *codec = CODEC_AAC;                      // assume AAC
        for (uint16_t i = 0; i < plcSize; i++) { // keine Codec-Kennung vorhanden, nach "http" suchen
            if (m_playlistContent[i].contains("#EXT-X-STREAM-INF")) {
                choosenLine = i;
            }
        }
    }

    choosenLine++; // die nächste Zeile ist die Umleitungs-URL

    ps_ptr<char> result;
    ps_ptr<char> line;
    line.clone_from(m_playlistContent[choosenLine]);

    if (line.starts_with("../")) {
        // ../../2093120-b/RISMI/stream01/streamPlaylist.m3u8
        while (line.starts_with("../")) {
            line.remove_prefix("../");
        }
        result.clone_from(m_currentHost);
        int idx = result.last_index_of('/');
        if (idx > 0 && (size_t)idx != result.strlen() - 1)
            result.truncate_at((size_t)idx + 1);
        result.append(line.get());
    } else if (!line.starts_with_icase("http")) {

        // http://arcast.com.ar:1935/radio/radionar.stream/playlist.m3u8               m_lastHost
        // chunklist_w789468022.m3u8                                                   line
        // http://arcast.com.ar:1935/radio/radionar.stream/chunklist_w789468022.m3u8   --> result

        result.clone_from(m_currentHost);
        int pos = m_currentHost.last_index_of('/');
        if (pos > 0) {
            result.truncate_at((size_t)pos + 1);
            result.append(line.get());
        }
    } else {
        result.clone_from(line);
    }

    return result; // Umleitung auf eine neue M3U8-Playlist
}
/*
    Lage der Positionszeiger innerhalb einer Audiodatei:

                              m_audioFilePosition
                                      |
    |-------------------------------- v -----------------------------------------------------------------------------------|
    |                                                                                                                      |
    |<------------------------------------------------- m_audioFileSize -------------------------------------------------->|
    |        m_audioDataStart                                                                                              |
    |               |                                                                                                      |
    |               v                                                                                                      |
    |               |<--------------------------------- m_audioDataSize ------------------------------------->|            |
    v               v                                                                                         v            v
    +----------------------------------------------------------------------------------------------------------------------+
    | Audio-Header  |                           Audiodaten-Block                                              | Sonstiges  |
    +----------------------------------------------------------------------------------------------------------------------+
                    ^                                                                                         ^
                    |--------------------------------------- ^ -----------------------------------------------|
                                                             |
                                                      m_audioDataReadPtr

    m_audioFilePosition ist die Position des Lese-/Schreibzeigers in der gesamten Datei.
    m_audioFileSize ist die Gesamtlänge der Datei.
    m_audioDataStart ist der Anfang des Audiodaten-Blocks und zugleich die Größe des Audio-Headers.
    m_audioDataSize ist die Länge des Audiodaten-Blocks.
    m_audioDataReadPtr ist der Lesezeiger innerhalb des Audiodaten-Blocks.
*/

/**
 * @brief Transportiert Daten einer lokalen Datei in den Eingangspuffer
 */
void Audio::processLocalFile() {
    if (!(m_audiofile && m_fRunning && m_dataMode == AUDIO_LOCALFILE))
        return;

    m_prlf.availableBytes = 0;
    m_prlf.bytesAddedToBuffer = 0;

    if (m_fFirstCall) { // läuft je Verbindung genau einmal, bereitet den Start vor
        m_fFirstCall = false;
        m_fStream = false;
        m_prlf.audioHeaderFound = false;
        m_prlf.newFilePos = 0;
        m_prlf.ctime = audio_millis();
        m_audioFilePosition = 0;
        m_audioDataSize = 0;
        m_audioDataStart = 0;
        m_fAllDataReceived = false;
        m_prlf.timeout = 8000; // ms
    }

    if (m_resumeFilePos >= 0) { // es gibt eine Wiederaufnahme-Position
        m_prlf.newFilePos = newInBuffStart(m_resumeFilePos);
        if (m_prlf.newFilePos < 0)
            AUDIO_LOG_WARN("skip to new position was not successful");
        m_haveNewFilePos = m_prlf.newFilePos;
        m_resumeFilePos = -1;
        m_fAllDataReceived = false;
        return;
    }

    // Hoechstmenge je Aufruf. fread blockiert den Aufrufer, bis die Karte geliefert hat. Bei den
    // vollen 64 KB sind das rund 87 ms, in denen die Hauptschleife und damit jede Anzeige steht.
    // In kleineren Portionen bleibt die Wartezeit unter 12 ms, ohne dass der Nachschub knapp
    // wird: die Schleife kommt etwa 190-mal je Sekunde vorbei, das sind ueber 1,5 MB/s. Selbst
    // eine auf 60 Bilder eingebrochene Anzeige haelt damit noch das Vierfache dessen bereit,
    // was ein FLAC-Datenstrom braucht.
    constexpr uint32_t MAX_NACHFUELLUNG = 8192;

    m_prlf.availableBytes = min(m_inBuff.writeSpace(), (size_t)(m_audioFileSize - m_audioFilePosition));
    m_prlf.bytesAddedToBuffer = audioFileRead(m_inBuff.getWritePtr(), min(m_prlf.availableBytes, MAX_NACHFUELLUNG));
    if (m_prlf.bytesAddedToBuffer > 0) {
        m_inBuff.bytesWritten(m_prlf.bytesAddedToBuffer);
    }
    if (m_audioDataSize && m_audioFilePosition >= m_audioDataSize) {
        if (!m_fAllDataReceived)
            m_fAllDataReceived = true;
    }
    if (!m_audioDataSize && m_audioFilePosition == m_audioFileSize) {
        if (!m_fAllDataReceived)
            m_fAllDataReceived = true;
    }

    if (!m_decoder && m_inBuff.bufferFilled() > 127) {
        if (!initializeDecoder())
            return;
        m_prlf.maxFrameSize = m_inBuff.getMaxBlockSize();
    }

    if (!m_fStream) {
        if (m_controlCounter != 100) {
            if (m_fOgg) {
                m_controlCounter = 100;
            }
            if ((audio_millis() - m_prlf.ctime) > m_prlf.timeout) {
                AUDIO_LOG_ERROR("audioHeader reading timeout");
                m_fRunning = false;
                goto exit;
            }
            if (m_inBuff.bufferFilled() > m_prlf.maxFrameSize || (m_inBuff.bufferFilled() == m_audioFileSize) ||
                m_fAllDataReceived) { // mindestens ein vollständiger Rahmen, oder die Datei ist kleiner
                m_inBuff.bytesWasRead(readAudioHeader(m_inBuff.readSpace()));
            }
            if (m_controlCounter == 100) {
                if (m_audioDataStart > 0) {
                    m_prlf.audioHeaderFound = true;
                }
                if (!m_audioDataSize)
                    m_audioDataSize = m_audioFileSize;
            }
            return;
        } else {
            m_fStream = true;
            info(*this, evt_info, "stream ready");
        }
    }

    if (m_fileStartTime > 0 && m_nominalBitrate) {
        if (getBitRate() > 0)
            setAudioPlayTime(m_fileStartTime);
        else
            info(*this, evt_info, "can't set audio play time directly");
        m_fileStartTime = -1;
    }

    // Dateiende erreicht?
    if (m_fEof) { // m_fEof und m_fID3v1TagFound setzt playAudioData()
        if (m_fID3v1TagFound)
            readID3V1Tag();
    exit:
        ps_ptr<char> afn; // Name der Audiodatei
        if (m_audiofile)
            afn.assign(m_audioFilePath.c_get()); // Namen zwischenspeichern
        m_audioCurrentTime = 0;
        m_audioFileDuration = 0;
        m_resumeFilePos = -1;
        m_haveNewFilePos = 0;
        m_codec = CODEC_NONE;
        stopSong();

        if (afn.valid()) {
            info(*this, evt_eof, "{}", afn.c_get());
        }
        return;
    }
}

/**
 * @brief Transportiert Daten eines Endlos-Streams in den Eingangspuffer
 */
void Audio::processWebStream() {
    if (m_dataMode != AUDIO_DATA)
        return;
    uint16_t readedBytes = 0;

    m_pwst.availableBytes = 0;
    m_pwst.f_clientIsConnected = m_client->connected();
    m_pwst.writeSpace = UINT16_MAX;

    // erster Aufruf, Anfangswerte setzen
    if (m_fFirstCall) { // läuft je Verbindung genau einmal, bereitet den Start vor
        m_fFirstCall = false;
        m_fStream = false;
        m_pwst.chunkSize = 0;
        m_metacount = m_metaint;
        m_fAllDataReceived = false;
        readMetadata(0, &readedBytes, true);
        getChunkSize(0, true);
        m_audioFilePosition = 0;
    }

    m_pwst.availableBytes = m_client->available(); // im Stream verfügbare Bytes
    // Chunked-Transfer
    if (m_fChunked && m_pwst.availableBytes) {
        if (m_pwst.chunkSize == 0) {
            int chunkLen = getChunkSize(&m_pwst.readedBytes);
            if (chunkLen == -1) { // mehr Daten nötig
                vTaskDelay(10);
                return;
            }
            if (chunkLen == -100) { // Fehler
                stopSong();
                return;
            }
            if (chunkLen == 0) {
                m_fAllDataReceived = true;
            }
            m_pwst.chunkSize = chunkLen;
            m_pwst.readedBytes = 0; // readedBytes zählt nicht zur Blockgröße
        }
        m_pwst.writeSpace = min(m_pwst.writeSpace, m_pwst.chunkSize);
    }

    // Metadaten liegen an
    if ((m_metaint) && (m_metacount == 0)) {
        if (!m_pwst.availableBytes)
            return;
        readedBytes = 0;
        bool res = false;
        if (m_fChunked) {
            res = readMetadata(min(m_pwst.availableBytes, m_pwst.chunkSize), &readedBytes);
        } else {
            res = readMetadata(m_pwst.availableBytes, &readedBytes);
        }
        m_pwst.readedBytes += readedBytes;
        if (m_fChunked)
            m_pwst.chunkSize -= readedBytes; // Blockgröße um die Metadaten-Länge verringern
        if (res == false)
            return;
        m_metacount = m_metaint;
        return;
    }
    if (m_metaint)
        m_pwst.writeSpace = min(m_pwst.writeSpace, m_metacount);

    // Puffer füllen- -
    if (m_pwst.availableBytes) {
        m_pwst.writeSpace = min(m_pwst.writeSpace, (uint32_t)m_inBuff.writeSpace());
        int32_t bytesAddedToBuffer = audioFileRead(m_inBuff.getWritePtr(), min(m_pwst.writeSpace, (uint32_t)UINT16_MAX));

        if (bytesAddedToBuffer > 0) {
            m_pwst.writeSpace -= bytesAddedToBuffer;
            if (m_metaint)
                m_metacount -= bytesAddedToBuffer;
            if (m_fChunked)
                m_pwst.chunkSize -= bytesAddedToBuffer;
            m_inBuff.bytesWritten(bytesAddedToBuffer);
        }
    }

    // bei oft fast leerem Puffer eine Warnung ausgeben
    if (m_fStream) {
        if (!m_fAllDataReceived)
            streamDetection(m_pwst.availableBytes);
        if (!m_pwst.f_clientIsConnected) {
            if (m_fTts && !m_fAllDataReceived)
                m_fAllDataReceived = true;
        } // Gegenstelle hat geschlossen, kommt bei Sprachdiensten vor
    }

    if (!m_decoder && m_inBuff.bufferFilled() > 127) {
        if (initializeDecoder())
            m_pwst.maxFrameSize = m_inBuff.getMaxBlockSize();
        else
            return;
    }

    // Dekodierung anstoßen -
    if (((m_inBuff.bufferFilled() > m_pwst.maxFrameSize) || (m_fAllDataReceived)) && !m_fStream) { // auf gefüllten Puffer warten
        info(*this, evt_info, "stream ready");
        m_fStream = true; // bereit für die Wiedergabe
    }

    if (m_fEof) {
        info(*this, evt_eof, "{}", m_lastHost.c_get());
        stopSong();
    }
}

/**
 * @brief Transportiert Daten einer Web-Datei in den Eingangspuffer
 */
void Audio::processWebFile() {

    if (!m_lastHost.valid()) {
        AUDIO_LOG_ERROR("m_lastHost is empty");
        return;
    }
    constexpr size_t OGG_PROBE_BYTES = 128; ///< Mindestdatenmenge für die vorhandene OGG-Codec-Erkennung

    if (m_fFirstCall) { // läuft je Verbindung genau einmal, bereitet den Start vor
        m_fFirstCall = false;
        m_fStream = false;
        m_controlCounter = 0;
        m_pwf.audioHeaderFound = false;
        m_pwf.newFilePos = 0;
        m_pwf.ctime = audio_millis();
        m_audioFilePosition = 0;
        m_audioDataSize = 0;
        m_audioDataStart = 0;
        m_fAllDataReceived = false;
        m_pwf.timeout = 8000; // ms
        if (m_codec != CODEC_OGG && !initializeDecoder())
            return;
        m_pwf.maxFrameSize = m_inBuff.getMaxBlockSize(); // kein Rahmen ist größer
    }

    if (m_resumeFilePos >= 0) { // es gibt eine Wiederaufnahme-Position
        m_pwf.newFilePos = newInBuffStart(m_resumeFilePos);
        if (m_pwf.newFilePos < 0)
            AUDIO_LOG_WARN("skip to new position was not successful");
        m_haveNewFilePos = m_pwf.newFilePos;
        m_resumeFilePos = -1;
        m_fAllDataReceived = false;
        return;
    }

    // Ein Socket-Leseschritt liefert höchstens 1.436 Bytes, für unkomprimiertes WAV reicht ein Schritt je Aufruf nicht immer.
    constexpr uint32_t WAV_REFILL_BYTES = 8192; ///< Höchstens nachgefüllte WAV-Bytes pro Aufruf
    const bool refillWav = m_codec == CODEC_WAV && !m_fSsl;
    uint32_t remaining = refillWav ? WAV_REFILL_BYTES : UINT16_MAX;
    do {
        m_pwf.availableBytes = min(m_client->available(), (int)m_inBuff.writeSpace());
        m_pwf.bytesAddedToBuffer = audioFileRead(m_inBuff.getWritePtr(), min(m_pwf.availableBytes, remaining));
        if (m_pwf.bytesAddedToBuffer <= 0)
            break;
        m_inBuff.bytesWritten(m_pwf.bytesAddedToBuffer);
        remaining -= m_pwf.bytesAddedToBuffer;
    } while (refillWav && remaining);
    if (m_audioDataSize && m_audioFilePosition >= m_audioDataSize) {
        if (!m_fAllDataReceived)
            m_fAllDataReceived = true;
    }
    if (!m_audioDataSize && m_audioFilePosition == m_audioFileSize) {
        if (!m_fAllDataReceived)
            m_fAllDataReceived = true;
    }
    if (!m_decoder) {
        if (m_inBuff.bufferFilled() < OGG_PROBE_BYTES) {
            if (m_fAllDataReceived || (audio_millis() - m_pwf.ctime) > m_pwf.timeout) {
                AUDIO_LOG_ERROR("OGG header incomplete");
                goto exit;
            }
            return;
        }
        if (!initializeDecoder())
            return;
        m_pwf.maxFrameSize = m_inBuff.getMaxBlockSize();
    }

    if (!m_fStream) {
        if (m_controlCounter != 100) {
            if (m_fOgg) {
                m_controlCounter = 100;
            }
            if ((audio_millis() - m_pwf.ctime) > m_pwf.timeout) {
                AUDIO_LOG_ERROR("audioHeader reading timeout");
                m_fRunning = false;
                goto exit;
            }
            if (m_inBuff.bufferFilled() > m_pwf.maxFrameSize || (m_inBuff.bufferFilled() == m_audioFileSize) ||
                m_fAllDataReceived) { // mindestens ein vollständiger Rahmen, oder die Datei ist kleiner
                m_inBuff.bytesWasRead(readAudioHeader(m_inBuff.readSpace()));
            }
            if (m_controlCounter == 100) {
                if (m_audioDataStart > 0) {
                    m_pwf.audioHeaderFound = true;
                }
                if (!m_audioDataSize)
                    m_audioDataSize = m_audioFileSize;
            }
            return;
        } else {
            if (m_resumeFilePos == -1) {
                if (m_inBuff.bufferFilled() > 2 * m_inBuff.getMaxBlockSize() || (m_fOgg && m_fAllDataReceived)) {
                    m_fStream = true;
                    info(*this, evt_info, "stream ready");
                }
            }
        }
    }

    if (m_fileStartTime > 0 && m_nominalBitrate) {
        if (getBitRate() > 0)
            setAudioPlayTime(m_fileStartTime);
        else
            info(*this, evt_info, "can't set audio play time directly");
        m_fileStartTime = -1;
    }

    // Dateiende erreicht?
    if (m_fEof) { // m_fEof und m_fID3v1TagFound setzt playAudioData()
        if (m_fID3v1TagFound)
            readID3V1Tag();
    exit:
        stopSong();
        info(*this, evt_eof, "{}", m_lastHost.c_get());

        m_audioCurrentTime = 0;
        m_audioFileDuration = 0;
        m_resumeFilePos = -1;
        m_haveNewFilePos = 0;
        m_codec = CODEC_NONE;
        return;
    }
}

/**
 * @brief Transportiert einen Transport-Stream (HLS) und entpackt die TS-Pakete
 */
void Audio::processWebStreamTS() {

    // erster Aufruf, Anfangswerte setzen
    if (m_fFirstCall) { // läuft je Verbindung genau einmal, bereitet den Start vor
        m_fFirstCall = false;
        m_fM3u8Data = true;
        m_audioFilePosition = 0;
        m_pwsst.f_firstPacket = true;
        m_pwsst.f_chunkFinished = false;
        m_pwsst.f_nextRound = false;
        m_pwsst.byteCounter = 0;
        m_t0 = audio_millis();
        if (!m_pwsst.ts_packet.valid())
            m_pwsst.ts_packet.alloc_array(m_pwsst.ts_packetsize, "m_pwsst.ts_packet"); // einmalige Anlage
        if (!m_decoder) {                                                              // einmalige Anlage
            getChunkSize(0, true);
            m_pwsst.chunkSize = 0;
            tsParsePacket(0, 0, 0);
            m_pwsst.ts_packetPtr = 0;
            if (!initializeDecoder())
                return;
        }
    } // -------------------------------------------------------------------------

    m_pwsst.ts_packetStart = 0;
    m_pwsst.ts_packetLength = 0;
nextRound:
    uint32_t availableBytes = m_client->available(); // im Stream verfügbare Bytes

    if (availableBytes) {
        /* If the m3u8 stream uses 'chunked data transfer' no content length is supplied. Then the chunk size determines the audio data to be processed.
           However, the chunk size in some streams is limited to 32768 bytes, although the chunk can be larger. Then the chunk size is
           calculated again. The data used to calculate (here readedBytes) the chunk size is not part of it.
        */
        uint16_t readedBytes = 0;
        uint32_t minAvBytes = 0;
        if (m_pwsst.f_chunkFinished)
            goto chunkFinished;
        if (m_fChunked && m_pwsst.chunkSize == m_pwsst.byteCounter) {
            int chunkLen = getChunkSize(&readedBytes);
            if (chunkLen == -1) { // mehr Daten nötig
                vTaskDelay(10);
                return;
            }
            if (chunkLen == -100) { // Fehler
                stopSong();
                return;
            }
            m_pwsst.chunkSize += chunkLen;
            AUDIO_LOG_DEBUG("chunkLen {}, rb {}", chunkLen, readedBytes);
            if (chunkLen == 0) {
                m_pwsst.f_chunkFinished = true;
                m_pwsst.chunkSize = 0;
                m_pwsst.byteCounter = 0;
                goto chunkFinished;
            }
        }
        if (m_pwsst.chunkSize)
            minAvBytes = min3(availableBytes, m_pwsst.ts_packetsize - m_pwsst.ts_packetPtr, m_pwsst.chunkSize - m_pwsst.byteCounter);
        else
            minAvBytes = min(availableBytes, (uint32_t)(m_pwsst.ts_packetsize - m_pwsst.ts_packetPtr));

        int res = audioFileRead(m_pwsst.ts_packet.get() + m_pwsst.ts_packetPtr, minAvBytes);
        if (res > 0) {
            m_pwsst.ts_packetPtr += res;
            m_pwsst.byteCounter += res;
            if (m_pwsst.ts_packetPtr < m_pwsst.ts_packetsize)
                return; // noch nicht genug Daten, unter der Paketgröße von 188 Bytes wird die Runde wiederholt
            m_pwsst.ts_packetPtr = 0;
            if (m_pwsst.f_firstPacket) { // im ersten Paket den ID3-Header suchen
                m_pwsst.f_firstPacket = false;
                uint8_t ID3_HeaderSize = processM3u8ID3Header(m_pwsst.ts_packet.get());
                if (ID3_HeaderSize > m_pwsst.ts_packetsize) {
                    AUDIO_LOG_ERROR("ID3 Header is too big");
                    stopSong();
                    return;
                }
                if (ID3_HeaderSize) {
                    memcpy(m_pwsst.ts_packet.get(), &m_pwsst.ts_packet.get()[ID3_HeaderSize], m_pwsst.ts_packetsize - ID3_HeaderSize);
                    m_pwsst.ts_packetPtr = m_pwsst.ts_packetsize - ID3_HeaderSize;
                    return;
                }
            }
            if (!tsParsePacket(&m_pwsst.ts_packet.get()[0], &m_pwsst.ts_packetStart, &m_pwsst.ts_packetLength)) {
                stopSong();
                AUDIO_LOG_ERROR("song stopped");
                return;
            };

            if (m_pwsst.ts_packetLength) {
                size_t ws = m_inBuff.writeSpace();
                if (ws >= m_pwsst.ts_packetLength) {
                    memcpy(m_inBuff.getWritePtr(), m_pwsst.ts_packet.get() + m_pwsst.ts_packetStart, m_pwsst.ts_packetLength);
                    m_inBuff.bytesWritten(m_pwsst.ts_packetLength);
                    m_pwsst.f_nextRound = true;
                } else {
                    int t = 0;
                    memcpy(m_inBuff.getWritePtr(), m_pwsst.ts_packet.get() + m_pwsst.ts_packetStart, ws); // alles schreiben, was in den Puffer passt
                    m_inBuff.bytesWritten(ws);
                    while (true) {
                        if (m_inBuff.writeSpace() >= m_pwsst.ts_packetLength - ws) {
                            memcpy(m_inBuff.getWritePtr(), &m_pwsst.ts_packet.get()[ws + m_pwsst.ts_packetStart], m_pwsst.ts_packetLength - ws); // den Rest schreiben
                            m_inBuff.bytesWritten(m_pwsst.ts_packetLength - ws);
                            break;
                        }
                        t++;
                        vTaskDelay(10); // warten, bis der Puffer Platz hat
                        if (t == 10) {
                            AUDIO_LOG_ERROR("m_inBuff is full");
                            break;
                        }
                    }
                }
            }
            if (m_audioFileSize && m_pwsst.byteCounter > m_audioFileSize) {
                AUDIO_LOG_ERROR("byteCounter overflow, byteCounter: {}, contentlength: {}", m_pwsst.byteCounter, m_audioFileSize);
                return;
            }
            if (m_pwsst.chunkSize && m_pwsst.byteCounter > m_pwsst.chunkSize) {
                AUDIO_LOG_ERROR("byteCounter overflow, byteCounter: {}, chunkSize: {}", m_pwsst.byteCounter, m_pwsst.chunkSize);
                return;
            }
        }
    }
    if (m_audioFileSize && m_pwsst.byteCounter == m_audioFileSize) {
        if (m_inBuff.bufferFilled() < settings.BUFFER_TRESHOLD_HLS * 2) {
            m_fContinue = true;
            m_pwsst.byteCounter = 0;
            m_pwsst.ts_packetPtr = 0;
        }
        m_pwsst.f_nextRound = false;
        goto exit;
    }

chunkFinished:
    if (m_pwsst.f_chunkFinished) {
        if (m_inBuff.bufferFilled() < settings.BUFFER_TRESHOLD_HLS * 2) {
            m_pwsst.f_chunkFinished = false;
            m_fContinue = true;
            m_pwsst.byteCounter = 0;
            m_pwsst.chunkSize = 0;
            m_pwsst.ts_packetPtr = 0;
        }
        goto exit;
    }

    // bei oft fast leerem Puffer eine Warnung ausgeben
    if (m_fStream) {
        streamDetection(availableBytes);
    }

    // Puffer füllen -
    {
        if (m_inBuff.bufferFilled() > settings.BUFFER_TRESHOLD_HLS && !m_fStream) { // auf gefüllten Puffer warten
            m_fStream = true;                                                     // bereit für die Wiedergabe
            uint16_t filltime = audio_millis() - m_t0;
            info(*this, evt_info, "stream ready");
            info(*this, evt_info, "buffer filled in {} ms", filltime);
        }
    }
    if (m_pwsst.f_nextRound) {
        goto nextRound;
    }
exit:
    return;
}

/**
 * @brief Transportiert einen HLS-Stream ohne TS-Verpackung
 */
void Audio::processWebStreamHLS() {

    // erster Aufruf, Anfangswerte setzen
    if (m_fFirstCall) { // läuft je Verbindung genau einmal, bereitet den Start vor
        m_fFirstCall = false;
        m_fM3u8Data = true;
        m_t0 = audio_millis();
        m_controlCounter = 0;
        m_audioFilePosition = 0;
        m_pwsHLS.ID3BuffSize = 4096;
        m_pwsHLS.availableBytes = 0;
        m_pwsHLS.firstBytes = true;
        m_pwsHLS.f_chunkFinished = false;
        m_pwsHLS.byteCounter = 0;
        m_pwsHLS.chunkSize = 0;
        m_pwsHLS.ID3WritePtr = 0;
        m_pwsHLS.ID3ReadPtr = 0;
        m_pwsHLS.ID3Buff.alloc(m_pwsHLS.ID3BuffSize, "m_pwsHLS.ID3Buff");
        getChunkSize(0, true);
        if (!m_decoder && !initializeDecoder())
            return;
        m_pwsHLS.maxFrameSize = m_inBuff.getMaxBlockSize(); // kein MP3- oder AAC-Rahmen ist größer
    }

    if (m_dataMode != AUDIO_DATA)
        return;

    m_pwsHLS.availableBytes = m_client->available();
    if (m_pwsHLS.availableBytes) { // an ID3 header could come here
        uint16_t readedBytes = 0;

        if (m_fChunked && !m_pwsHLS.chunkSize) {
            m_pwsHLS.chunkSize = getChunkSize(&readedBytes);
            if (m_pwsHLS.chunkSize == -1) { // mehr Daten nötig
                vTaskDelay(10);
                return;
            }
            if (m_pwsHLS.chunkSize == -100) { // Fehler
                stopSong();
                return;
            }
            m_pwsHLS.byteCounter += readedBytes;
        }

        if (m_pwsHLS.firstBytes) {
            if (m_pwsHLS.ID3WritePtr < m_pwsHLS.ID3BuffSize) {
                m_pwsHLS.ID3WritePtr += audioFileRead(&m_pwsHLS.ID3Buff[m_pwsHLS.ID3WritePtr], m_pwsHLS.ID3BuffSize - m_pwsHLS.ID3WritePtr);
                return;
            }
            if (m_controlCounter < 100) {
                int res = readID3Header(&m_pwsHLS.ID3Buff[m_pwsHLS.ID3ReadPtr], m_pwsHLS.ID3BuffSize - m_pwsHLS.ID3ReadPtr);
                if (res >= 0)
                    m_pwsHLS.ID3ReadPtr += res;
                if (m_pwsHLS.ID3ReadPtr > m_pwsHLS.ID3BuffSize) {
                    AUDIO_LOG_ERROR("buffer overflow");
                    stopSong();
                    return;
                }
                return;
            }
            if (m_controlCounter != 100)
                return;

            size_t ws = m_inBuff.writeSpace();
            if (ws >= m_pwsHLS.ID3BuffSize - m_pwsHLS.ID3ReadPtr) {
                memcpy(m_inBuff.getWritePtr(), &m_pwsHLS.ID3Buff[m_pwsHLS.ID3ReadPtr], m_pwsHLS.ID3BuffSize - m_pwsHLS.ID3ReadPtr);
                m_inBuff.bytesWritten(m_pwsHLS.ID3BuffSize - m_pwsHLS.ID3ReadPtr);
            } else {
                int t = 0;
                memcpy(m_inBuff.getWritePtr(), &m_pwsHLS.ID3Buff[m_pwsHLS.ID3ReadPtr], ws);
                m_inBuff.bytesWritten(ws);
                while (true) {
                    if (m_inBuff.writeSpace() >= m_pwsHLS.ID3BuffSize - (m_pwsHLS.ID3ReadPtr + ws)) {
                        memcpy(m_inBuff.getWritePtr(), &m_pwsHLS.ID3Buff[ws + m_pwsHLS.ID3ReadPtr],
                               m_pwsHLS.ID3BuffSize - (m_pwsHLS.ID3ReadPtr + ws)); // alles schreiben, was in den Puffer passt
                        m_inBuff.bytesWritten(m_pwsHLS.ID3BuffSize - (m_pwsHLS.ID3ReadPtr + ws));
                        break;
                    }
                    t++;
                    vTaskDelay(10); // warten, bis der Puffer Platz hat
                    if (t == 10) {
                        AUDIO_LOG_ERROR("m_inBuff is full");
                        break;
                    }
                }
            }
            m_pwsHLS.ID3Buff.reset();
            m_pwsHLS.byteCounter += m_pwsHLS.ID3BuffSize;
            m_pwsHLS.firstBytes = false;
        }

        size_t bytesWasWritten = 0;
        if (m_inBuff.writeSpace() >= m_pwsHLS.availableBytes) {
            bytesWasWritten = audioFileRead(m_inBuff.getWritePtr(), m_pwsHLS.availableBytes);
        } else {
            bytesWasWritten = audioFileRead(m_inBuff.getWritePtr(), m_inBuff.writeSpace());
        }
        m_inBuff.bytesWritten(bytesWasWritten);

        m_pwsHLS.byteCounter += bytesWasWritten;

        if (m_pwsHLS.byteCounter == m_audioFileSize || m_pwsHLS.byteCounter == m_pwsHLS.chunkSize) {
            m_pwsHLS.f_chunkFinished = true;
            m_pwsHLS.byteCounter = 0;
        }
    }

    if (m_pwsHLS.f_chunkFinished) {
        if (m_inBuff.bufferFilled() < settings.BUFFER_TRESHOLD_HLS * 2) {
            m_pwsHLS.f_chunkFinished = false;
            m_fContinue = true;
        }
    }

    // bei oft fast leerem Puffer warnen oder die Verbindung neu aufbauen
    if (m_fStream) {
        streamDetection(m_pwsHLS.availableBytes);
    }

    if (m_inBuff.bufferFilled() > settings.BUFFER_TRESHOLD_HLS && !m_fStream) { // auf gefüllten Puffer warten
        m_fStream = true;                                                     // bereit für die Wiedergabe
        info(*this, evt_info, "stream ready");
    }
    return;
}

/**
 * @brief Reicht Daten aus dem Eingangspuffer blockweise an den Decoder weiter, läuft im Audio-Task
 */
void Audio::playAudioData() {

    if (m_fEof || m_fLockInBuffer || !m_fStream) {
        vTaskDelay(1);
        return;
    } // Stream nicht bereit, Ende erreicht, Puffer gesperrt oder nichts geöffnet
    if (m_validSamples) {
        vTaskDelay(5);
        playChunk();
        return;
    } // erst die anstehenden Samples ausgeben
    m_pad.count = 0;
    m_pad.bytesToDecode = m_inBuff.readSpace();
    m_pad.bytesDecoded = 0;

    if (m_fFirstPlayCall) {
        m_fFirstPlayCall = false;
        m_audioDataReadPtr = 0;
        m_pad.count = 0;
        m_pad.oldAudioDataSize = 0;
        m_bytesNotConsumed = 0;
        m_pad.lastFrames = false;
        m_fEof = false;
    }

    bool isFile = false;
    bool isStream = false;

    if (m_dataMode == AUDIO_LOCALFILE)
        isFile = true;
    if (m_streamType == ST_WEBFILE && m_playlistFormat != FORMAT_M3U8)
        isFile = true; // lokale oder Web-Datei, keine M3U8
    if (m_streamType == ST_WEBSTREAM || m_playlistFormat == FORMAT_M3U8)
        isStream = true;
    if (!isFile && !isStream)
        return;

    xSemaphoreTake(m_mutexAudioTaskIsDecoding, 1 * configTICK_RATE_HZ);
    {
        m_pad.bytesDecoded = 0;
        if (isFile) {
            if (!m_audioDataSize)
                goto exit; // bei Dateigröße 0 gibt es nichts zu dekodieren
            if (m_audioDataSize - m_audioDataReadPtr == 128) {
                m_fID3v1TagFound = true;
                m_fEof = true;
                goto exit;
            }
            if (m_audioDataSize <= m_audioDataReadPtr) {
                m_fEof = true;
                goto exit;
            }

            if (m_audioDataStart + m_audioDataSize >= m_audioFilePosition)
                m_fAllDataReceived = true;
            if (m_audioDataSize - m_audioDataReadPtr <= m_inBuff.getMaxBlockSize())
                m_pad.lastFrames = true;

            if (m_pad.lastFrames) {
                m_pad.bytesToDecode = min(m_inBuff.readSpace(), (size_t)(m_audioDataSize - m_audioDataReadPtr));
                m_pad.bytesDecoded = sendBytes(m_inBuff.getReadPtr(), m_pad.bytesToDecode);
            } else {
                m_pad.bytesToDecode = m_inBuff.readSpace();
                if (m_pad.bytesToDecode >= m_inBuff.getMaxBlockSize()) {
                    m_pad.bytesDecoded = sendBytes(m_inBuff.getReadPtr(), m_pad.bytesToDecode);
                }
            }
        }

        if (isStream) {
            if (m_fAllDataReceived) { // Sprachdienste senden so
                m_pad.lastFrames = true;
                if (m_fTts && !m_inBuff.bufferFilled()) {
                    m_fEof = true;
                    goto exit;
                }
            }
            if (m_pad.lastFrames) {
                if (m_fChunked) {
                    m_pad.bytesToDecode = m_inBuff.readSpace();
                } else {
                    m_pad.bytesToDecode = min(m_inBuff.readSpace(), (size_t)(m_audioDataStart + m_audioDataSize - m_audioDataReadPtr));
                }
                m_pad.bytesDecoded = sendBytes(m_inBuff.getReadPtr(), m_pad.bytesToDecode);
            } else {
                m_pad.bytesToDecode = m_inBuff.readSpace();
                if (m_pad.bytesToDecode >= m_inBuff.getMaxBlockSize()) {
                    m_pad.bytesDecoded = sendBytes(m_inBuff.getReadPtr(), m_pad.bytesToDecode);
                }
            }
        }

        if (m_pad.bytesDecoded > 0) {
            m_inBuff.bytesWasRead(m_pad.bytesDecoded);
            m_audioDataReadPtr += m_pad.bytesDecoded;
        }
    }
exit:
    xSemaphoreGive(m_mutexAudioTaskIsDecoding);

    AUDIO_LOG_DEBUG("m_audioDataReadPtr {}, m_audioDataSize {}", m_audioDataReadPtr, m_audioDataSize);
    return;
}

/**
 * @brief Wertet den HTTP-Antwort-Header einer GET-Anfrage aus
 */
bool Audio::parseHttpResponseHeader() {

    if (m_dataMode != HTTP_RESPONSE_HEADER)
        return false;
    m_icyItems.reset();
    m_metaint = 0; // noch kein Metadaten-Abstand bekannt

    m_phreh.reset();
    m_phreh.ctime = audio_millis();
    m_phreh.timeout = 5000; // ms
    m_httpRespHdrBuff.clear();
    uint16_t pos = 0;

    while (true) { // erst den Kopf lesen and store it in m_httpRespHdrBuff
        if (m_client->available()) {
            m_httpRespHdrBuff[pos++] = audioFileRead();
        }
        if (m_httpRespHdrBuff.ends_with("\r\n\r\n"))
            break;
        if (m_httpRespHdrBuff.ends_with("\n\n"))
            break;
        if (pos == m_httpRespHdrBuff.size()) {
            AUDIO_LOG_WARN("responseHeaderline overflow");
            m_httpRespHdrBuff[pos - 1] = '\0';
            break;
        }
        if ((audio_millis() - m_phreh.ctime) > m_phreh.timeout) {
            AUDIO_LOG_ERROR("timeout");
            m_phreh.f_time = false;
            // Abweichung vom Upstream: ohne stopSong() bleibt der Zustand HTTP_RESPONSE_HEADER
            // stehen, loop() ruft diese Funktion sofort wieder auf, und ein Server, der nie
            // antwortet, blockiert den aufrufenden Task in endlosen 5-Sekunden-Runden.
            stopSong();
            return false;
        }
        if (!m_client->available())
            vTaskDelay(1); // Warten ohne Heißlauf: der aufrufende Task rendert auch die GUI
    }

    ps_ptr<char> rhl;
    bool ct_seen = false;


    pos = 0;
    while (true) { // den Header Zeile für Zeile lesen
        int idx = m_httpRespHdrBuff.index_of('\n', pos);
        if (idx > pos) {
            rhl = m_httpRespHdrBuff.substr(pos, idx - pos);
            rhl.set_name("rhl");
            rhl.remove_chars("\r");
            pos = idx + 1;
            if (rhl.strlen() == 0)
                continue;
        } else { // fertig?
            if (ct_seen)
                goto lastToDo;
            else {
                goto exit;
            }
        }


        if (rhl.starts_with_icase("icy-")) { // ICY-Headerzeilen
            m_phreh.f_icy_data = true;

            if (rhl.starts_with_icase("icy-genre:")) {
                m_icyItems.icy_genre.copy_from(rhl.get() + 10); // Ambient, Rock und so weiter
                m_icyItems.icy_genre.trim();
                latinToUTF8(m_icyItems.icy_genre);
            }

            if (rhl.starts_with_icase("icy-logo:")) {
                m_icyItems.icy_logo.copy_from(rhl.get() + 9); // https://www.0nradio.com/logos/0n-70s_600x600.jpg
                m_icyItems.icy_logo.trim();
                latinToUTF8(m_icyItems.icy_logo);
            }

            if (rhl.starts_with_icase("icy-name:")) {
                m_icyItems.icy_name.copy_from(rhl.get() + 9);
                m_icyItems.icy_name.trim();
                latinToUTF8(m_icyItems.icy_name);
            }

            if (rhl.starts_with_icase("icy-url:")) {
                m_icyItems.icy_url.copy_from(rhl.get() + 8);
                m_icyItems.icy_url.trim();
            }

            if (rhl.starts_with_icase("icy-description:")) {
                m_icyItems.icy_description.copy_from(rhl.get() + 16);
                m_icyItems.icy_description.trim();
                latinToUTF8(m_icyItems.icy_description);
            }

            if (rhl.starts_with_icase("icy-metaint:")) {
                m_icyItems.icy_metaint.copy_from(rhl.get() + 12);
                m_icyItems.icy_metaint.trim();
            }

            if (rhl.starts_with_icase("icy-br:")) {
                m_icyItems.icy_br.copy_from(rhl.get() + 7);
                m_icyItems.icy_br.trim();
            }
        } // Ende der ICY-Headerzeilen

        if (rhl.starts_with_icase("HTTP/")) { // HTTP status error code
            char statusCode[5];
            statusCode[0] = rhl[9];
            statusCode[1] = rhl[10];
            statusCode[2] = rhl[11];
            statusCode[3] = '\0';
            int sc = atoi(statusCode);
            if (sc == 403 && !m_fAltUserAgent) { // HTTP/1.1 403 Forbidden
                m_fAltUserAgent = true;
                AUDIO_LOG_WARN("403 Forbidden, test alternative user agent");
                connecttohost(m_lastHost.c_get());
                return true;
            }
            m_fAltUserAgent = false;
            if (sc > 310) { // e.g. HTTP/1.1 301 Moved Permanently
                info(*this, evt_streamtitle, "{}", rhl.get());
                goto exit;
            }
        } else if (rhl.starts_with_icase("content-type:")) { // content-type: text/html, charset=UTF-8
            int idx = rhl.index_of(';', 13);
            if (idx > 0)
                rhl[idx] = '\0';
            if (parseContentType(rhl.get() + 13))
                ct_seen = true;
            else {
                AUDIO_LOG_WARN("unknown contentType {}", rhl.get() + 13);
                goto exit;
            }
        }

        else if (rhl.starts_with_icase("location:")) {
            int pos = rhl.index_of_icase("http", 0);
            if (pos == -1) {
                int doubleSlash = rhl.index_of("//"); // e.g. location: //frontend.streamonkey.net/...  host: http://webstream.radiof.de/
                if (doubleSlash >= 9)
                    rhl.insert("http:", doubleSlash); // ==> http://frontend.streamonkey.net/fhn-radiof945/stream/mp3?aggregator=fh-tinyurl
                pos = rhl.index_of_icase("http", 0);
            }
            if (pos >= 0) {
                const char* c_host = (rhl.get() + pos);
                if (!m_currentHost.equals(c_host)) { // Endlosschleife verhindern
                    int pos_slash = indexOf(c_host, "/", 9);
                    if (pos_slash > 9) {
                        if (!strncmp(c_host, m_currentHost.get(), pos_slash)) {
                            info(*this, evt_info, "redirect to new extension at existing host \"{}\"", c_host);
                            if (m_playlistFormat == FORMAT_M3U8) {
                                //    m_lastHost.assign(c_host);
                                m_fM3u8Data = true;
                            }
                            httpPrint(c_host);
                            while (m_client->available())
                                audioFileRead(); // Empfangspuffer leeren
                            return true;
                        }
                    }
                    info(*this, evt_info, "redirect to new host \"{}\"", c_host);
                    httpPrint(c_host);
                    return true;
                }
            } else {
                AUDIO_LOG_WARN("unknown redirection: {}", rhl.c_get());
            }
        }

        else if (rhl.starts_with_icase("content-encoding:")) {
            info(*this, evt_info, "{}", rhl.get());
            if (rhl.contains("gzip")) {
                AUDIO_LOG_ERROR("can't extract gzip");
                goto exit;
            }
        }

        else if (rhl.starts_with_icase("content-disposition:")) { // Beispielzeile: content-disposition: attachment, filename=stream.asx
            int idx = rhl.index_of_icase("filename=");
            if (idx >= 0) {
                ps_ptr<char> fn;
                fn.assign(rhl.get() + idx + 9); // Position directly after "filename="
                fn.replace("\"", "");           // Anführungszeichen um den Dateinamen entfernen, falls vorhanden
                info(*this, evt_info, "Filename is {}", fn.get());
            }
        } else if (rhl.starts_with_icase("connection:")) {
            if (rhl.contains_with_icase("close")) {
                m_fConnectionClose = true;
            } // endet, sobald die letzte OGG-Seite markiert ist
        }

        else if (rhl.starts_with_icase("content-length:")) {
            const char* c_cl = (rhl.get() + 15);
            int32_t i_cl = atoi(c_cl);
            m_audioFileSize = i_cl;
        }

        else if (rhl.starts_with_icase("transfer-encoding:")) {
            if (rhl.ends_with_icase("chunked")) { // Station liefert Chunked-Transfer
                m_fChunked = true;
                info(*this, evt_info, "chunked data transfer");
                m_chunkcount = 0; // im Datenteil wird die Blockgröße erwartet
            }
        }

        else if (rhl.starts_with_icase("accept-ranges:")) {
            if (rhl.ends_with_icase("bytes"))
                m_fAcceptRanges = true;
        }

        else if (rhl.starts_with_icase("content-range:")) {
            AUDIO_LOG_INFO("{}", rhl.c_get());
        }

        else if (rhl.starts_with_icase("www-authenticate:")) {
            AUDIO_LOG_WARN("authentification failed, wrong credentials?");
            goto exit;
        } else {
            ;
        }
    } // äußere Schleife

exit: // Abbruchbedingung
    m_fAltUserAgent = false;
    m_dataMode = AUDIO_NONE;
    stopSong();
    return false;

lastToDo:
    m_fAltUserAgent = false;
    m_streamType = ST_WEBSTREAM;
    if (m_audioFileSize > 0)
        m_streamType = ST_WEBFILE; // Content-Length vorhanden, also eine Datei
    if (m_phreh.f_icy_data)
        m_streamType = ST_WEBSTREAM;
    if (m_fTts)
        m_streamType = ST_WEBSTREAM; // kommt so von Sprachdiensten ohne Content-Length

    if (ct_seen && m_playlistFormat == FORMAT_M3U8 && !m_m3u8Host.valid()) {
        m_m3u8Host = m_lastHost;
    }

    if (m_codec != CODEC_NONE) {
        m_dataMode = AUDIO_DATA; // jetzt werden Daten erwartet
    } else if (m_playlistFormat != FORMAT_NONE) {
        m_dataMode = AUDIO_PLAYLISTINIT; // eine Playlist wird erwartet
    } else {
        AUDIO_LOG_INFO("unknown content found at: {}", m_currentHost.c_get());
        goto exit;
    }

    if (m_phreh.f_icy_data) {
        if (m_icyItems.icy_description.valid())
            info(*this, evt_icydescription, "{}", m_icyItems.icy_description);
        if (m_icyItems.icy_genre.valid())
            info(*this, evt_genre, "{}", m_icyItems.icy_genre);
        if (m_icyItems.icy_logo.valid())
            info(*this, evt_icylogo, "{}", m_icyItems.icy_logo);
        if (m_icyItems.icy_name.valid())
            info(*this, evt_name, "{}", m_icyItems.icy_name);
        if (m_icyItems.icy_url.valid())
            info(*this, evt_icyurl, "{}", m_icyItems.icy_url);
        if (m_icyItems.icy_metaint.valid())
            m_metaint = m_icyItems.icy_metaint.to_uint32();
        if (m_icyItems.icy_br.valid())
            m_nominalBitrate = m_icyItems.icy_br.to_uint32() * 1000;
    }

    AUDIO_LOG_DEBUG("playlistFormat {}, dataMode {}, streamType: {}", m_plsFmtStr[m_playlistFormat], m_dataModeStr[m_dataMode], m_streamTypeStr[m_streamType]);
    return true;
}

/**
 * @brief Wertet den HTTP-Antwort-Header einer Range-Anfrage aus
 */
bool Audio::parseHttpRangeHeader() {

    ps_ptr<char> rhl;
    rhl.alloc(1024, "rhl"); // responseHeaderline
    bool ct_seen = false;

    if (m_dataMode != HTTP_RANGE_HEADER) {
        AUDIO_LOG_ERROR("wrong datamode {}", m_dataMode);
        goto exit;
    }

    m_phrah.ctime = audio_millis();
    m_phrah.timeout = 4500; // ms

    if (m_client->available() == 0) {
        if (!m_phrah.f_time) {
            m_phrah.stime = audio_millis();
            m_phrah.f_time = true;
        }
        if ((audio_millis() - m_phrah.stime) > m_phrah.timeout) {
            AUDIO_LOG_ERROR("timeout");
            m_phrah.f_time = false;
            return false;
        }
    }
    m_phrah.f_time = false;

    rhl.clear();

    while (true) { // äußere Schleife
        uint16_t pos = 0;
        if ((audio_millis() - m_phrah.ctime) > m_phrah.timeout) {
            AUDIO_LOG_ERROR("timeout");
            m_fTimeout = true;
            goto exit;
        }
        while (m_client->available()) {
            uint8_t b = audioFileRead();
            if (b == '\n') {
                if (!pos) { // Leerzeile empfangen, das ist die letzte Zeile des Antwort-Headers
                    goto lastToDo;
                }
                break;
            }
            if (b == '\r')
                rhl[pos] = 0;
            if (b < 0x20)
                continue;
            rhl[pos] = b;
            pos++;
            if (pos == 1023) {
                pos = 1022;
                continue;
            }
            if (pos == 1022) {
                rhl[pos] = '\0';
                AUDIO_LOG_WARN("responseHeaderline overflow");
            }
        } // innere Schleife
        if (!pos) {
            vTaskDelay(5);
            continue;
        }
        if (rhl.starts_with_icase("HTTP/")) { // HTTP status error code
            char statusCode[5];
            statusCode[0] = rhl[9];
            statusCode[1] = rhl[10];
            statusCode[2] = rhl[11];
            statusCode[3] = '\0';
            int sc = atoi(statusCode);
            if (sc > 310) { // e.g. HTTP/1.1 301 Moved Permanently
                info(*this, evt_name, "{}", rhl.get());
                goto exit;
            }
        }
        if (rhl.starts_with_icase("Server:")) {
            info(*this, evt_info, "{}", rhl.c_get());
        }
        if (rhl.starts_with_icase("Content-Length:")) {
        }
        if (rhl.starts_with_icase("Content-Range:")) {
            info(*this, evt_info, "{}", rhl.c_get());
        }
        if (rhl.starts_with_icase("Content-Type:")) {
        }
    }
exit:
    return false;
lastToDo:
    m_dataMode = AUDIO_DATA;
    return true;
}

/**
 * @brief Erzeugt und initialisiert den Decoder für den erkannten Codec
 */
bool Audio::initializeDecoder() {
    if (m_decoder) {
        AUDIO_LOG_WARN("stopSong first");
        return true;
    }
    m_codec = determineCodec(m_codec); // OGG kann VORBIS, FLAC oder OPUS enthalten, der Content-Type stimmt nicht immer

    const char* type = nullptr;
    switch (m_codec) {
#if AUDIO_CODEC_MP3
    case CODEC_MP3:
        type = "MP3";
        m_inBuff.setMaxBlocksize(FRAME_SIZE_MP3);
        break;
#endif
#if AUDIO_CODEC_AAC
    case CODEC_AAC:
        type = "AAC";
        m_inBuff.setMaxBlocksize(FRAME_SIZE_AAC);
        break;
#endif
#if AUDIO_CODEC_M4A && AUDIO_CODEC_AAC
    case CODEC_M4A:
        type = "AAC";
        m_inBuff.setMaxBlocksize(FRAME_SIZE_AAC);
        break;
#endif
#if AUDIO_CODEC_FLAC
    case CODEC_FLAC:
        type = "FLAC";
        m_inBuff.setMaxBlocksize(FRAME_SIZE_FLAC);
        break;
#endif
#if AUDIO_CODEC_OPUS
    case CODEC_OPUS:
        type = "OPUS";
        m_inBuff.setMaxBlocksize(FRAME_SIZE_OPUS);
        break;
#endif
#if AUDIO_CODEC_VORBIS
    case CODEC_VORBIS:
        type = "VORBIS";
        m_inBuff.setMaxBlocksize(FRAME_SIZE_VORBIS);
        break;
#endif
#if AUDIO_CODEC_WAV
    case CODEC_WAV:
        type = "WAV";
        m_inBuff.setMaxBlocksize(FRAME_SIZE_WAV);
        break;
#endif
    default:
        AUDIO_LOG_ERROR("unknown decoder");
        stopSong();
        return false;
        break;
    }

    if (type) {
        m_decoder = createDecoder(type);
        if (!m_decoder || !m_decoder->init()) {
            AUDIO_LOG_ERROR("The {Decoder could not be initialized", type);
            stopSong();
            return false;
        }
    }
    info(*this, evt_info, "{}Decoder has been initialized", m_decoder->whoIsIt());
    return true;
}

/**
 * @brief Bestimmt Codec oder Playlist-Format aus dem Content-Type der Antwort
 */
bool Audio::parseContentType(ps_ptr<char> ct) {
    enum : int {
        CT_NONE,
        CT_MP3,
        CT_AAC,
        CT_M4A,
        CT_WAV,
        CT_FLAC,
        CT_PLS,
        CT_M3U,
        CT_ASX,
        CT_M3U8,
        CT_TXT,
        CT_AACP,
        CT_OPUS,
        CT_OGG,
        CT_VORBIS
    };

    // MIME-Typen und ihre CT_Val-Werte
    static const struct {
        const char* mime_type;
        int ct_val;
    } mime_map[] = {
        {"audio/mpeg", CT_MP3},
        {"audio/mpeg3", CT_MP3},
        {"audio/x-mpeg", CT_MP3},
        {"audio/x-mpeg-3", CT_MP3},
        {"audio/mp3", CT_MP3},
        {"audio/aac", CT_AAC},
        {"audio/x-aac", CT_AAC},
        {"audio/aacp", CT_AAC},
        {"video/mp2t", CT_AAC},
        {"audio/mp2t", CT_AAC},
        {"video/m2ts", CT_AAC},
        {"audio/mp4", CT_M4A},
        {"audio/m4a", CT_M4A},
        {"audio/x-m4a", CT_M4A},
        {"audio/wav", CT_WAV},
        {"audio/x-wav", CT_WAV},
        {"audio/flac", CT_FLAC},
        {"audio/x-flac", CT_FLAC},
        {"audio/scpls", CT_PLS},
        {"audio/x-scpls", CT_PLS},
        {"application/pls+xml", CT_PLS},
        {"audio/mpegurl", CT_M3U},
        {"audio/x-mpegurl", CT_M3U},
        {"audio/ms-asf", CT_ASX},
        {"video/x-ms-asf", CT_ASX},
        {"audio/x-ms-asx", CT_ASX},
        {"application/ogg", CT_OGG},
        {"audio/ogg", CT_OGG},
        {"application/vnd.apple.mpegurl", CT_M3U8},
        {"application/hls+xml", CT_M3U8},
        {"application/x-mpegurl", CT_M3U8},
        {"application/octet-stream", CT_TXT},
        {"text/html", CT_TXT},
        {"text/plain", CT_TXT},
        {"application/json", CT_TXT},
        {NULL, CT_NONE} // Abschlussmarke
    };

    ct.trim();

    m_codec = CODEC_NONE;
    int ct_val = CT_NONE;

    // in der Zuordnungstabelle suchen
    for (int i = 0; mime_map[i].mime_type != NULL; i++) {
        ct.toLowerCase();
        if (ct.equals(mime_map[i].mime_type)) {
            ct_val = mime_map[i].ct_val;
            break;
        }
    }

    // Sonderfälle video/mp2t und audio/mp2t
    if (ct_val == CT_AAC && (ct == "video/mp2t" || ct == "audio/mp2t") && m_m3u8Codec == CODEC_MP3) {
        ct_val = CT_MP3;
    }
    // Sonderfall audio/mpegurl
    if (ct_val == CT_M3U && ct == "audio/mpegurl" && m_expectedPlsFmt == FORMAT_M3U8) {
        ct_val = CT_M3U8;
    }
    // Sonderfall application/json
    if (ct_val == CT_TXT && ct == "application/json" && m_expectedPlsFmt == FORMAT_M3U8) {
        ct_val = CT_M3U8;
    }

    // ungültige Content-Types abfangen
    if (ct_val == CT_NONE) {
        AUDIO_LOG_WARN("ContentType {} not supported", ct);
        return false;
    }

    // Zuordnung von m_codec und m_playlistFormat
    switch (ct_val) {
    case CT_MP3:
        m_codec = CODEC_MP3;
        break;
    case CT_AAC:
        m_codec = CODEC_AAC;
        break;
    case CT_M4A:
        m_codec = CODEC_M4A;
        break;
    case CT_FLAC:
        m_codec = CODEC_FLAC;
        break;
    case CT_OPUS:
        m_codec = CODEC_OPUS;
        m_fOgg = true;
        break;
    case CT_VORBIS:
        m_codec = CODEC_VORBIS;
        m_fOgg = true;
        break;
    case CT_WAV:
        m_codec = CODEC_WAV;
        break;
    case CT_OGG:
        m_codec = CODEC_OGG;
        m_fOgg = true;
        break;
    case CT_PLS:
        m_playlistFormat = FORMAT_PLS;
        break;
    case CT_M3U:
        m_playlistFormat = FORMAT_M3U;
        break;
    case CT_ASX:
        m_playlistFormat = FORMAT_ASX;
        break;
    case CT_M3U8:
        m_playlistFormat = FORMAT_M3U8;
        break;
    case CT_TXT:
        if (m_expectedCodec == CODEC_AAC)
            m_codec = CODEC_AAC;
        if (m_expectedCodec == CODEC_MP3)
            m_codec = CODEC_MP3;
        if (m_expectedPlsFmt == FORMAT_ASX)
            m_playlistFormat = FORMAT_ASX;
        if (m_expectedPlsFmt == FORMAT_M3U)
            m_playlistFormat = FORMAT_M3U;
        if (m_expectedPlsFmt == FORMAT_M3U8)
            m_playlistFormat = FORMAT_M3U8;
        if (m_expectedPlsFmt == FORMAT_PLS)
            m_playlistFormat = FORMAT_PLS;
        break;
    default:
        AUDIO_LOG_ERROR("{}, unsupported audio format", ct);
        return false;
    }

    return true;
}

/**
 * @brief Wertet StreamTitle und StreamUrl aus den ICY-Metadaten aus und meldet sie
 */
void Audio::showstreamtitle(char* st) {
    if (!st)
        return;
    // Beispiele für ml:
    // StreamTitle='Oliver Frank - Mega Hitmix';StreamUrl='www.radio-welle-woerthersee.at';
    // oder adw_ad='true';durationMilliseconds='10135';adId='34254';insertionType='preroll';
    // aus HTML-Entities werden Zeichen: 'Bielszy odcie&#324, bluesa' wird zu 'Bielszy odcień bluesa'

    ps_ptr<char> ml;
    ps_ptr<char> title;
    ps_ptr<char> artist;
    ps_ptr<char> streamTitle;
    ps_ptr<char> sUrl;
    ml.htmlToUTF8(st); // nach UTF-8 wandeln

    int16_t idx1 = 0, idx2, idx4, idx5, idx6, idx7, titleLen = 0, artistLen = 0, titleStart = 0, artistStart = 0;


    if (ml.index_of("StreamTitle='<?xml version=", 0) == 0) {
        /* e.g. xmlStreamTitle
              StreamTitle='<?xml version="1.0" encoding="utf-8"?><RadioInfo><Table><DB_ALBUM_ID>37364</DB_ALBUM_ID>
              <DB_ALBUM_IMAGE>00000037364.jpg</DB_ALBUM_IMAGE><DB_ALBUM_NAME>Boyfriend</DB_ALBUM_NAME>
              <DB_ALBUM_TYPE>Single</DB_ALBUM_TYPE><DB_DALET_ARTIST_NAME>DOVE CAMERON</DB_DALET_ARTIST_NAME>
              <DB_DALET_ITEM_CODE>CD4161</DB_DALET_ITEM_CODE><DB_DALET_TITLE_NAME>BOYFRIEND</DB_DALET_TITLE_NAME>
              <DB_FK_SITE_ID>2</DB_FK_SITE_ID><DB_IS_MUSIC>1</DB_IS_MUSIC><DB_LEAD_ARTIST_ID>26303</DB_LEAD_ARTIST_ID>
              <DB_LEAD_ARTIST_NAME>Dove Cameron</DB_LEAD_ARTIST_NAME><DB_RADIO_IMAGE>cidadefm.jpg</DB_RADIO_IMAGE>
              <DB_RADIO_NAME>Cidade</DB_RADIO_NAME><DB_SONG_ID>120126</DB_SONG_ID><DB_SONG_LYRIC>60981</DB_SONG_LYRIC>
              <DB_SONG_NAME>Boyfriend</DB_SONG_NAME></Table><AnimadorInfo><TITLE>Cidade</TITLE>
              <START_TIME_UTC>2022-11-15T22:00:00+00:00</START_TIME_UTC><END_TIME_UTC>2022-11-16T06:59:59+00:00
              </END_TIME_UTC><SHOW_NAME>Cidade</SHOW_NAME><SHOW_HOURS>22h às 07h</SHOW_HOURS><SHOW_PANEL>0</SHOW_PANEL>
              </AnimadorInfo></RadioInfo>';StreamUrl='';
        */
        idx4 = ml.index_of("<DB_DALET_TITLE_NAME>");
        idx5 = ml.index_of("</DB_DALET_TITLE_NAME>");
        idx6 = ml.index_of("<DB_LEAD_ARTIST_NAME>");
        idx7 = ml.index_of("</DB_LEAD_ARTIST_NAME>");
        if (idx4 == -1 || idx5 == -1)
            return;
        titleStart = idx4 + 21; // <DB_DALET_TITLE_NAME>
        titleLen = idx5 - titleStart;

        if (idx6 != -1 && idx7 != -1) {
            artistStart = idx6 + 21; // <DB_LEAD_ARTIST_NAME>
            artistLen = idx7 - artistStart;
        }
        if (titleLen)
            title.assign(ml.get() + titleStart, titleLen);
        if (artistLen)
            artist.assign(ml.get() + artistStart, artistLen);

        if (title.valid() && artist.valid()) {
            streamTitle.assign(title.get());
            streamTitle.append(" - ");
            streamTitle.append(artist.get());
        } else if (title.valid()) {
            streamTitle.assign(title.get());
        } else if (artist.valid()) {
            streamTitle.assign(artist.get());
        }
    }

    else if (ml.index_of("StreamTitle='") == 0) {
        int start = 13;
        int end = ml.index_of("';", start);
        if (end < 0)
            end = ml.index_of("'", start); // Rückfall, wenn die Station fehlerhaft sendet
        if (end > start) {
            int len = end - start;
            streamTitle.assign(ml.get() + start, len);
        }
    }

    else if (ml.starts_with("Grouping:")) {
        streamTitle.assign(ml.get() + 9);
    }

    else if (ml.starts_with("#EXTINF")) {
        // StreamTitle aus einer M3U-#EXTINF-Zeile ins ICY-Format bringen
        // orig: #EXTINF:10,title="text="TitleName",artist="ArtistName"
        // conv: StreamTitle=TitleName - ArtistName
        // orig: #EXTINF:10,title="text=\"Spot Block End\" amgTrackId=\"9876543\"",artist=" ",url="length=\"00:00:00\""
        // conv: StreamTitle=text=\"Spot Block End\" amgTrackId=\"9876543\" -

        idx1 = ml.index_of("title=\"");
        idx2 = ml.index_of("artist=\"");
        if (idx1 > 0) {
            int titleStart = idx1 + 7;
            int idx3 = ml.index_of("\"", titleStart);
            if (idx3 > titleStart) {
                int titleLength = idx3 - titleStart;
                title.assign(ml.get() + titleStart, titleLength);
                if (title.starts_with("text=\\")) { // #EXTINF:10,title="text=\"Spot Block End\"",artist=" ",
                    titleStart += 7;
                    idx3 = ml.index_of("\\", titleStart);
                    if (idx3 > titleStart) {
                        int titleLength = idx3 - titleStart;
                        title.assign(ml.get() + titleStart, titleLength);
                    }
                }
            }
        }
        if (idx2 > 0) {
            int artistStart = idx2 + 8;
            int idx3 = ml.index_of("\"", artistStart);
            if (idx3 > artistStart) {
                int artistLength = idx3 - artistStart;
                artist.assign(ml.get() + artistStart, artistLength);
                if (strcmp(artist.get(), " ") == 0)
                    artist.reset();
            }
        }
        if (title.valid() && artist.valid()) {
            streamTitle.assign(title.get());
            streamTitle.append(" - ");
            streamTitle.append(artist.get());
        } else if (title.valid()) {
            streamTitle.assign(title.get());
        } else if (artist.valid()) {
            streamTitle.assign(artist.get());
        }
    } else {
        ;
    }

    if (ml.index_of("StreamUrl=", 0) > 0) {
        idx1 = ml.index_of("StreamUrl=", 0);
        idx2 = ml.index_of(";", idx1);
        if (idx1 >= 0 && idx2 > idx1) { // StreamURL gefunden
            uint16_t len = idx2 - idx1;
            sUrl.assign(ml.get() + idx1, len);
            if (!sUrl.equals(m_streamURL.c_get())) {
                info(*this, evt_info, "Stream URL: {}", sUrl.c_get()); // zum Beispiel StreamUrl='http://myUrl.com'
                m_streamURL = sUrl;
            }
        }
    }

    if (ml.index_of("adw_ad=", 0) > 0) {
        idx1 = ml.index_of("adw_ad=", 0);
        if (idx1 >= 0) { // Werbeeinblendung gefunden
            idx1 = ml.index_of("durationMilliseconds=", 0);
            idx2 = ml.index_of(";", idx1);
            if (idx1 >= 0 && idx2 > idx1) {
                uint16_t len = idx2 - idx1;
                ps_ptr<char> sAdv;
                sAdv.assign(ml.get() + idx1, len + 1);
                uint8_t pos = 21; // remove "StreamTitle="
                if (sAdv[pos] == '\'')
                    pos++; // führendes Anführungszeichen entfernen
                if (sAdv[strlen(sAdv.get()) - 1] == '\'')
                    sAdv[strlen(sAdv.get()) - 1] = '\0'; // schließendes Anführungszeichen entfernen
                info(*this, evt_info, "Advertisement: {}", sAdv.get() + pos);
            }
        }
        return;
    }
    if (!streamTitle.valid())
        return;

    if (!m_streamTitle.equals(streamTitle)) {
        m_streamTitle.clone_from(streamTitle);
    } else {
        return; // unverändert
    }

    if (m_streamTitle.starts_with("{\"")) { // womöglich eine JSON-Antwort
        // "{\"status\":0,\"error\":{\"info\":\"\\u042d\\u0442\\u043e \\u0440\\u0435\\u043a\\u043b\\u0430\\u043c\\u0430 \\u0438\\u043b\\u0438
        // \\u0434\\u0436\\u0438\\u043d\\u0433\\u043b\",\"code\":201},\"result\":\"\\u042d\\u0442\\u043e \\u0440\\u0435\\u043a\\u043b\\u0430\\u043c\\u0430 \\u0438\\u043b\\u0438
        // \\u0434\\u0436\\u0438\\u043d\\u0433\\u043b\"}\n0";
        ps_ptr<char> jsonIn;
        jsonIn.clone_from(m_streamTitle);
        jsonIn.truncate_at('\n'); // Beispielinhalt: '{"status":1,...}' mit angehängter 0
        if (!jsonIn.isJson())
            return;
        int idx = jsonIn.index_of_icase("\"result\"");
        if (idx < 0)
            return;
        jsonIn.remove_before(idx + 10); // "result": entfernen
        jsonIn.truncate_at('"');        // alles ab dem Anführungszeichen entfernen: Ok","result":... wird zu Ok
        m_streamTitle.unicodeToUTF8(jsonIn.c_get());
    }

    info(*this, evt_streamtitle, "{}", m_streamTitle.c_get());
}

/**
 * @brief Meldet die Parameter des erkannten Formats an den Host
 */
void Audio::showCodecParams() {

    info(*this, evt_info, "Channels: {}", getChannels());
    info(*this, evt_info, "SampleRate (Hz): {}", getSampleRate());
    info(*this, evt_info, "BitsPerSample: {}", getBitsPerSample());

    if (m_codec == CODEC_AAC) {
        info(*this, evt_info, "{}", m_decoder->arg2()); // AAC Format
        uint8_t answ = m_decoder->val2();               // Spectral Band Replication
        if (answ > 0 && answ < 4) {
            const char sbr[4][50] = {"without SBR", "upsampled SBR", "downsampled SBR", "no SBR used, but file is upsampled by a factor 2"};
            info(*this, evt_info, "Spectral band replication: {}", sbr[answ]);
        }
    }
}

/**
 * @brief Sucht das nächste Synchronwort im Datenstrom
 */
int Audio::findNextSync(uint8_t* data, size_t len) {
    // MP3- und AAC-Daten sind in Rahmen unterteilt, an deren Anfang je ein Synchronwort steht.
    // Das Synchronwort ist 0xFFF, danach folgen Angaben zum Aufbau des Rahmens.
    // WAV-Dateien haben keine Rahmen.
    // Rückgabe: 0, wenn das Synchronwort an Position 0 steht,
    //           größer 0 als Abstand zum nächsten Synchronwort,
    //           -1, wenn im Block der Länge len kein Synchronwort liegt

    m_fnsy.nextSync = 0;

    if (m_codec == CODEC_WAV) {
        m_fPlaying = true;
        m_fnsy.nextSync = 0;
    }
    if (m_codec == CODEC_MP3) {
        m_fnsy.nextSync = m_decoder->findSyncWord(data, (int32_t)len);
        if (m_fnsy.nextSync == -1)
            return len; // kein Synchronwort, im nächsten Block weitersuchen
        m_decoder->clear();
    }
    if (m_codec == CODEC_AAC) {
        m_fnsy.nextSync = m_decoder->findSyncWord(data, (int32_t)len);
    }
    if (m_codec == CODEC_M4A) {
        if (!m_m4aChConfig)
            m_m4aChConfig = 2;
        if (!m_m4aSampleRate)
            m_m4aSampleRate = 44100;
        if (!m_m4aObjectType)
            m_m4aObjectType = 2;
        m_decoder->setRawBlockParams(m_m4aChConfig, m_m4aSampleRate, 0, m_m4aObjectType, 0);
        m_fPlaying = true;
        m_fnsy.nextSync = 0;
    }
    if (m_codec == CODEC_FLAC) {
        m_fnsy.nextSync = m_decoder->findSyncWord(data, (int32_t)len);
        if (m_fnsy.nextSync == -1)
            return len; // kein OggS, im nächsten Block weitersuchen
    }
    if (m_codec == CODEC_OPUS) {
        m_fnsy.nextSync = m_decoder->findSyncWord(data, (int32_t)len);
        if (m_fnsy.nextSync == -1)
            return len; // kein OggS, im nächsten Block weitersuchen
    }
    if (m_codec == CODEC_VORBIS) {
        m_fnsy.nextSync = m_decoder->findSyncWord(data, len);
        if (m_fnsy.nextSync == -1)
            return len; // kein OggS, im nächsten Block weitersuchen
    }
    if (m_fnsy.nextSync == -1) {
        if (m_fnsy.swnf == 0)
            info(*this, evt_info, "syncword not found");
        else {
            m_fnsy.swnf++; // Zähler der Fehlversuche, kann auf Fremddaten hindeuten
        }
    }
    if (m_fnsy.nextSync == 0) {
        if (m_fnsy.swnf) {
            info(*this, evt_info, "syncword not found {} times", m_fnsy.swnf);
            m_fnsy.swnf = 0;
        } else {
            info(*this, evt_info, "syncword found at pos 0");
            m_fDecodeReady = true;
        }
    }
    if (m_fnsy.nextSync > 0) {
        info(*this, evt_info, "syncword found at pos {}", m_fnsy.nextSync);
    }
    return m_fnsy.nextSync;
}

/**
 * @brief Übernimmt Kanäle, Abtastrate und Auflösung vom Decoder
 */
void Audio::setDecoderItems() {
    setChannels(m_decoder->getChannels());
    setSampleRate(m_decoder->getSampleRate());
    setBitsPerSample(m_decoder->getBitsPerSample());
    if (m_decoder->arg1())
        info(*this, evt_info, "{}", m_decoder->arg1());
    if (m_decoder->getAudioDataStart() > 0) { // OGG-Decoder melden den Datenbeginn erst beim Dekodieren
        m_audioDataStart = m_decoder->getAudioDataStart();
    }
    if (m_audioDataStart && m_audioDataSize == m_audioFileSize) {
        if (m_codec == CODEC_VORBIS && m_audioFileSize && m_audioDataReadPtr >= m_audioDataStart)
            m_audioDataReadPtr -= m_audioDataStart;
        m_audioDataSize = m_audioFileSize - m_audioDataStart;
    }

    info(*this, evt_info, "Audio-Data-Start: {}", m_audioDataStart);
    info(*this, evt_info, "Audio-Length: {}", m_audioDataSize);

    if (m_lastGranulePosition && m_audioFileSize && m_i2sItems.sampleRate) {
        m_audioFileDuration = (uint32_t)(m_lastGranulePosition / m_i2sItems.sampleRate);
        if (m_audioFileDuration)
            m_nominalBitrate = (m_audioFileSize - m_audioDataStart) * 8 / m_audioFileDuration;
        AUDIO_LOG_DEBUG("m_nominalBitrate {}, m_lastGranulePosition {}", m_nominalBitrate, m_lastGranulePosition);
        info(*this, evt_info, "Duration (s): {}", m_audioFileDuration);
    }

    if (getBitsPerSample() != 8 && getBitsPerSample() != 16 && getBitsPerSample() != 24 && getBitsPerSample() != 32) {
        AUDIO_LOG_ERROR("Bits per sample must be 8, 16, 24 or 32 found {}", getBitsPerSample());
        stopSong();
    }

    if (getChannels() != 1 && getChannels() != 2) {
        AUDIO_LOG_ERROR("Num of channels must be 1 or 2, found {}", getChannels());
        stopSong();
    }
    showCodecParams();
}

/**
 * @brief Behandelt einen Decoder-Fehler und bestimmt, wie viele Bytes verworfen werden
 */
uint32_t Audio::decodeError(int8_t res, uint8_t* data, int32_t bytesDecoded) {
#if AUDIO_LITE_DIAG
    // Fehlercode und die Bytes an der Fehlstelle: valides MP3 (FF Ex/FF Fx) deutet auf ein
    // Decoder-Problem, Text- oder Zufallsbytes auf eine Verfälschung des Datenstroms.
    printf("[decErr] res=%d bd=%ld pos=%lu data:", (int)res, (long)bytesDecoded, (unsigned long)m_audioFilePosition);
    for (int i = 0; i < 16; i++)
        printf(" %02X", data[i]);
    printf("\n");
#endif
    if (res == -100) {
        stopSong();
        return bytesDecoded;
    } // schwerer Fehler, etwa wenn der Decoder nicht initialisiert werden konnte
    if (m_codec == CODEC_AAC && res == -21) { // Wechsel zwischen Mono und Stereo
        // Laut Spezifikation steht die Kanalkonfiguration im ersten ADTS-Header und ändert sich im Stream nicht mehr. Manche Streams senden trotzdem kurze Mono-Blöcke im Stereo-Stream, das löst Fehler -21 aus, weil der faad2-Decoder nicht selbst umschalten kann.
        m_sbyt.channels = 0;
        if ((data[0] == 0xFF) || ((data[1] & 0xF0) == 0xF0)) {
            int channel_config = ((data[2] & 0x01) << 2) | ((data[3] & 0xC0) >> 6);
            if (channel_config != m_sbyt.channels) {
                m_sbyt.channels = channel_config;
                info(*this, evt_info, "AAC channel config changed to {}", m_sbyt.channels);
            }
        }
        m_decoder->reset();
        m_decoder->init();
        return 0;
    }
#if AUDIO_CODEC_MP3
    if (m_codec == CODEC_MP3) {
        if (res == MP3Decoder::MP3_NEED_RESTART) {
#if AUDIO_LITE_DIAG
            printf("[decRestart] MP3_NEED_RESTART -> Reconnect!\n");
#endif
            info(*this, evt_info, "" ANSI_ESC_RED "Network error" ANSI_ESC_RESET "");
            connecttohost(m_lastHost.get());
            return 0;
        }
    }
#endif
    m_fPlaying = false; // neues Synchronwort suchen
    if (bytesDecoded == 0)
        return 1; // ein Byte überspringen und das nächste Synchronwort suchen
    return bytesDecoded;
}

/**
 * @brief Behandelt Decoder-Ergebnisse, die mehr Daten anfordern
 */
uint32_t Audio::decodeContinue(int8_t res, uint8_t* data, int32_t bytesDecoded, int32_t* bytesLeft) {
#if AUDIO_CODEC_AAC
    if (m_codec == CODEC_AAC) {
        if (res == AACDecoder::AAC_ID3_HDR) {
            uint32_t size =
                ((data[6 + bytesDecoded] & 0x7F) << 21) | ((data[7 + bytesDecoded] & 0x7F) << 14) | ((data[8 + bytesDecoded] & 0x7F) << 7) | (data[9 + bytesDecoded] & 0x7F);
            size += 10; // Nutzdaten überspringen
            return bytesDecoded + size;
            if (bytesDecoded > *bytesLeft)
                AUDIO_LOG_ERROR("AAC input data too small bytesDecoded {} > bytesLeft {}", bytesDecoded, *bytesLeft);
        }
    }
#endif

#if AUDIO_CODEC_FLAC
    if (m_codec == CODEC_FLAC) {
        if (res == FlacDecoder::FLAC_PARSE_OGG_DONE)
            return bytesDecoded;
        if (res == FlacDecoder::FLAC_DECODE_FRAMES_LOOP)
            return bytesDecoded;
    } // nichts abzuspielen
#endif
#if AUDIO_CODEC_OPUS
    if (m_codec == CODEC_OPUS) {
        if (res == OpusDecoder::OPUS_PARSE_OGG_DONE)
            return bytesDecoded;
        if (res == OpusDecoder::OPUS_END)
            return bytesDecoded;
    } // nichts abzuspielen
#endif
#if AUDIO_CODEC_VORBIS
    if (m_codec == CODEC_VORBIS) {
        if (res == VorbisDecoder::VORBIS_PARSE_OGG_DONE)
            return bytesDecoded;
        if (res == VorbisDecoder::VORBIS_COMMENT_DONE)
            return bytesDecoded;
        if (res == VorbisDecoder::VORBIS_COMMENT_NEED_MORE)
            return bytesDecoded;
    } // nichts abzuspielen
#endif
#if AUDIO_CODEC_MP3
    if (m_codec == CODEC_MP3) {
        if (res == MP3Decoder::MP3_NEXT_FRAME)
            return bytesDecoded;
    }
#endif
    return 0;
}

/**
 * @brief Dekodiert einen Rahmen und stellt die Samples zur Ausgabe bereit
 */
int Audio::sendBytes(uint8_t* data, size_t len) {
    if (!m_fRunning)
        return 0;
    if (!m_decoder)
        return 0;

    int res = 0;
    int bytesDecoded = 0;
    const char* st = NULL;
    std::vector<uint32_t> vec;
    uint16_t samples_out = 0;
    if (m_validSamples) {
        goto exit;
    } // nichts zu dekodieren, nächste Runde

    m_sbyt.bytesLeft = 0;
    m_sbyt.nextSync = 0;

    if (!m_fPlaying) {
        m_sbyt.channels = 2; // bei AAC wird zunächst Stereo angenommen
        m_sbyt.isPS = 0;
        m_sbyt.f_setDecodeParamsOnce = true;
        m_sbyt.nextSync = findNextSync(data, len);
        if (m_sbyt.nextSync < 0)
            return len; // kein Synchronwort gefunden
        if (m_sbyt.nextSync == 0) {
            m_fPlaying = true;
        }
        if (m_sbyt.nextSync > 0)
            return m_sbyt.nextSync;
    }
    // an dieser Stelle ist m_fPlaying gesetzt

    m_sbyt.bytesLeft = len;

    if (m_codec == CODEC_NONE && m_playlistFormat == FORMAT_M3U8)
        return 0; // kann beim Laden der M3U8-Playlist vorkommen
    if (!m_fDecodeReady)
        return 0; // erst das Synchronwort finden

    res = m_decoder->decode(data, &m_sbyt.bytesLeft, m_outBuff.get());
    bytesDecoded = len - m_sbyt.bytesLeft;

    // mögliche Werte von res:
    //          0: in Ordnung, kein Fehler
    //          ab 100: der Decoder braucht mehr Daten
    //          unter 0: ein Fehler ist aufgetreten
    //          -100: schwerer Fehler, Wiedergabe beenden

    if (res < 0) {
#if AUDIO_LITE_DIAG
        m_diagDecFail++;
#endif
        return decodeError(res, data, bytesDecoded);
    } // Fehler, den Rahmen überspringen
    if (res > 99) {
#if AUDIO_LITE_DIAG
        m_diagDecMore++;
#endif
        return decodeContinue(res, data, bytesDecoded, &m_sbyt.bytesLeft);
    } // der Decoder braucht mehr Daten

    if ((bytesDecoded == 0) && (m_codec != CODEC_VORBIS && m_codec != CODEC_FLAC)) { // unlikely framesize, exept VORBIS decodes lastSegmentTable
#if AUDIO_LITE_DIAG
        printf("[decZero] pos=%lu data: %02X %02X %02X %02X %02X %02X %02X %02X\n", (unsigned long)m_audioFilePosition,
               data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7]);
#endif
        info(*this, evt_info, "framesize is 0, start decoding again");
        m_fPlaying = false; // neues Synchronwort suchen
        // das Synchronwort war falsch, also ein Byte überspringen und das nächste suchen
        return 1;
    }
    // Stand hier: bytesDecoded > 0 und res >= 0
#if AUDIO_LITE_DIAG
    m_diagDecOk++;
#endif

    switch (m_codec) {
#if AUDIO_CODEC_WAV
    case CODEC_WAV:
        m_validSamples = m_decoder->getOutputSamples();
        break;
#endif
#if AUDIO_CODEC_MP3
    case CODEC_MP3:
        m_validSamples = m_decoder->getOutputSamples();
        break;
#endif
#if AUDIO_CODEC_AAC
    case CODEC_AAC:
        m_validSamples = m_decoder->getOutputSamples() / getChannels();
        if (!m_sbyt.isPS && m_decoder->val1()) { // nur der Wechsel von 0 auf 1 wird gemeldet
            m_sbyt.isPS = 1;
            info(*this, evt_info, "Parametric Stereo");
        } else
            m_sbyt.isPS = m_decoder->val1();
        break;
#endif
#if AUDIO_CODEC_M4A && AUDIO_CODEC_AAC
    case CODEC_M4A:
        m_validSamples = m_decoder->getOutputSamples() / getChannels();
        break;
#endif
#if AUDIO_CODEC_FLAC
    case CODEC_FLAC:
        m_validSamples = m_decoder->getOutputSamples();
        st = m_decoder->getStreamTitle();
        if (st) {
            info(*this, evt_streamtitle, "{}", st);
        }
        vec = m_decoder->getMetadataBlockPicture();
        if (vec.size() > 0) { // Positionen des eingebetteten Bilds holen
            info(*this, evt_image, vec);
        }
        break;
#endif
#if AUDIO_CODEC_OPUS
    case CODEC_OPUS:
        m_validSamples = m_decoder->getOutputSamples();
        st = m_decoder->getStreamTitle();
        if (st) {
            info(*this, evt_streamtitle, st);
        }
        vec = m_decoder->getMetadataBlockPicture();
        if (vec.size() > 0) { // Positionen des eingebetteten Bilds holen
            info(*this, evt_image, vec);
        }
        if (m_decoder->arg1()) {
            if (m_sbyt.opus_mode != m_decoder->arg1()) {
                info(*this, evt_info, m_decoder->arg1());
                m_sbyt.opus_mode = m_decoder->arg1();
            }
        }
        break;
#endif

#if AUDIO_CODEC_VORBIS
    case CODEC_VORBIS:
        m_validSamples = m_decoder->getOutputSamples();
        st = m_decoder->getStreamTitle();
        if (st) {
            info(*this, evt_streamtitle, st);
        }
        vec = m_decoder->getMetadataBlockPicture();
        if (vec.size() > 0) { // Positionen des eingebetteten Bilds holen
            info(*this, evt_image, vec);
        }
        break;
#endif
    }
    // Ein frisch dekodierter Block bedeutet, dass der vorige vollstaendig an I2S ging:
    // playAudioData() dekodiert erst, wenn m_validSamples auf null steht. Der Rest-Zaehler
    // gehoert also zu keinem Block mehr und muss verfallen. Wird er von aussen genullt -
    // bei Pause, beim Spulen, beim Zuruecksetzen - bleibt er sonst stehen, und playChunk()
    // springt beim naechsten Aufruf ueber die gesamte Verarbeitung hinweg direkt zur Ausgabe.
    // Der erste Block danach ginge dann ohne Lautstaerkeregelung in voller Aussteuerung an
    // den Verstaerker, hoerbar als kurzes lautes Aufschrecken.
    m_plCh.count = 0;
    m_blockPrepared = false;

    if (m_sbyt.f_setDecodeParamsOnce && m_validSamples) {
        m_sbyt.f_setDecodeParamsOnce = false;
        setDecoderItems();
    }
    // WAV und FLAC erhalten die Pegelreserve von rund 3,01 dB des MP3-Decoders hier genau einmal je frisch dekodiertem Block, vor Anzeige, Lautstärke und Klangregelung. AAC und Vorbis senken im Decoder ab.
    if (m_codec == CODEC_WAV || m_codec == CODEC_FLAC) {
        constexpr int64_t PCM_HEADROOM_NUMERATOR = 181;   ///< Zähler der Pegelreserve
        constexpr int64_t PCM_HEADROOM_DENOMINATOR = 256; ///< Nenner der Pegelreserve
        int32_t* samples = m_outBuff.get();
        for (int i = 0; i < m_validSamples * 2; ++i) {
            int64_t scaled = static_cast<int64_t>(samples[i]) * PCM_HEADROOM_NUMERATOR;
            scaled += scaled >= 0 ? PCM_HEADROOM_DENOMINATOR / 2 : -(PCM_HEADROOM_DENOMINATOR / 2);
            samples[i] = static_cast<int32_t>(scaled / PCM_HEADROOM_DENOMINATOR);
        }
    }
    samples_out = m_validSamples;

exit:
    m_curSample = 0;
    if (m_validSamples) {
        calculateAudioTime(bytesDecoded, samples_out);
        playChunk();
    }
    return bytesDecoded;
}

/**
 * @brief Führt Spielzeit und mittlere Bitrate aus dem Decoder-Durchsatz nach
 */
void Audio::calculateAudioTime(uint16_t bytesDecoderIn, uint16_t samples_decoder_out) {


    float audioCurrentTime = 0.0;

    if (m_cat.firstCall) { // erster Aufruf
        m_cat.firstCall = false;
        m_cat.reset();

        if (m_codec == CODEC_FLAC && m_decoder->getAudioFileDuration()) { // BITSTREAMINFO FLAC/OGG
            m_audioFileDuration = m_decoder->getAudioFileDuration();
            m_cat.nominalBitRate = (m_audioDataSize / m_decoder->getAudioFileDuration()) * 8;
        }

        if (m_nominalBitrate) {
            info(*this, evt_bitrate, "{}", m_nominalBitrate);
            m_cat.nominalBitRate = m_nominalBitrate;
            if (!m_audioFileDuration) // eine exakt bekannte Dauer, etwa aus dem Xing-Kopf, hat Vorrang vor der Bitraten-Rechnung
                m_audioFileDuration = round(((float)m_audioDataSize * 8 / m_cat.nominalBitRate));
            if (m_lastGranulePosition)
                m_cat.tota_samples = m_lastGranulePosition;
            else
                m_cat.tota_samples = m_audioFileDuration * m_i2sItems.sampleRate;
        }
    }

    m_cat.sumBytesIn += bytesDecoderIn;
    m_cat.deltaBytesIn += bytesDecoderIn;
    m_cat.sum_samples += samples_decoder_out;

    if (m_cat.timeStamp + 50 < audio_millis()) {
        uint32_t t = audio_millis();                  // Zeitverfolgung
        uint32_t delta_t = t - m_cat.timeStamp; //    ---"---
        m_cat.timeStamp = t;                    //    ---"---

        if (m_cat.nominalBitRate) {
            audioCurrentTime = (uint32_t)(m_cat.sum_samples / m_i2sItems.sampleRate);
        } else {
            double instBitRate = (m_cat.deltaBytesIn * 8000.0) / delta_t;
            m_cat.counter++;
            m_cat.avrBitRate += (instBitRate - m_cat.avrBitRate) / m_cat.counter;
            if ((abs(m_cat.avrBitRate - m_cat.oldAvrBitrate < 50)) && !m_cat.avrBitrateStable && m_cat.avrBitRate > 1000) {
                m_cat.brCounter++;
                if (m_cat.brCounter > 6) {
                    m_cat.avrBitrateStable = m_cat.avrBitRate;
                    info(*this, evt_bitrate, "{}", m_cat.avrBitrateStable); // geschätzt
                }
            }
            m_avrBitrate = m_cat.avrBitRate;
            audioCurrentTime = (float)m_cat.sumBytesIn * 8 / m_cat.avrBitRate;
            m_audioFileDuration = round(((float)m_audioDataSize * 8 / m_cat.avrBitRate));
            m_cat.oldAvrBitrate = m_avrBitrate;
        }
        m_cat.deltaBytesIn = 0;
        m_audioCurrentTime = round(audioCurrentTime);
    }

    if (m_haveNewFilePos && (m_cat.avrBitRate || m_cat.nominalBitRate)) {
        uint32_t posWhithinAudioBlock = m_haveNewFilePos - m_audioDataStart;
        float newTime = 0;
        if (m_xing.hasToc) {
            newTime = xingFilePosToTime(m_haveNewFilePos);
        } else if (m_cat.nominalBitRate) {
            newTime = (float)posWhithinAudioBlock / (m_cat.nominalBitRate / 8);
        } else {
            newTime = (float)posWhithinAudioBlock / (m_cat.avrBitRate / 8);
            m_avrBitrate = m_cat.avrBitRate;
        }
        m_audioCurrentTime = round(newTime);
        m_cat.sumBytesIn = posWhithinAudioBlock;
        m_haveNewFilePos = 0;
        m_cat.syltIdx = 0;
        if (m_syltLines.size()) {
            while (m_cat.syltIdx < m_syltLines.size()) {
                if (m_audioCurrentTime * 1000 < m_syltTimeStamp[m_cat.syltIdx])
                    break;
                m_cat.syltIdx++;
            }
            if (m_cat.syltIdx)
                m_cat.syltIdx--;
        }
    }

    if (m_syltLines.size()) {
        if (m_cat.syltIdx >= m_syltLines.size())
            return;
        if (m_audioCurrentTime * 1000 > m_syltTimeStamp[m_cat.syltIdx]) {
            info(*this, evt_lyrics, "{}", m_syltLines[m_cat.syltIdx].c_get());
            m_cat.syltIdx++;
        }
    }
}

/**
 * @brief Konfiguriert den I2S-Kanal mit den aktuellen Einstellungen
 */
bool Audio::i2sConfig() {
    esp_err_t result = ESP_OK;
    // I2S-Konfiguration
    memset(&m_i2sChanCfg, 0, sizeof(i2s_chan_config_t));
#if (ESP_IDF_VERSION_MAJOR < 6)
    m_i2sChanCfg.id = (i2s_port_t)m_i2sItems.i2s_num; // I2S_NUM_AUTO, I2S_NUM_0, I2S_NUM_1
#else
    m_i2sChanCfg.id = m_i2sItems.i2s_num; // I2S_NUM_AUTO, I2S_NUM_0, I2S_NUM_1
#endif
    m_i2sChanCfg.role = I2S_ROLE_MASTER;                 // I2S als Master, BCLK und LRC sind Ausgänge
    m_i2sChanCfg.dma_desc_num = settings.DMA_DESC_NUM;   // Anzahl der DMA-Puffer
    m_i2sChanCfg.dma_frame_num = settings.DMA_FRAME_NUM; // I2S frame number in one DMA buffer.
    m_i2sChanCfg.auto_clear = true;                      // ohne anstehende Daten sendet I2S selbsttätig Nullen
    m_i2sChanCfg.allow_pd = false;
    m_i2sChanCfg.intr_priority = 2;
    result = i2s_new_channel(&m_i2sChanCfg, &m_i2sTxHandle, NULL);
    if (result != ESP_OK) { // ESP_ERR_INVALID_ARG?
        AUDIO_LOG_ERROR("I2S channel: invalid argument");
        return false;
    }

    memset(&m_i2sStdCfg, 0, sizeof(i2s_std_config_t));
    m_i2sStdCfg.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO); // schaltet die Bit-Verschiebung des Philips-Modus ein
    m_i2sStdCfg.gpio_cfg.bclk = I2S_GPIO_UNUSED;                                                                // BCLK, Zuweisung in setPinout()
    m_i2sStdCfg.gpio_cfg.din = I2S_GPIO_UNUSED;                                                                 // nicht benutzt
    m_i2sStdCfg.gpio_cfg.dout = I2S_GPIO_UNUSED;                                                                // DOUT, Zuweisung in setPinout()
    m_i2sStdCfg.gpio_cfg.mclk = I2S_GPIO_UNUSED;                                                                // MCLK, Zuweisung in setPinout()
    m_i2sStdCfg.gpio_cfg.ws = I2S_GPIO_UNUSED;                                                                  // LRC, Zuweisung in setPinout()
    m_i2sStdCfg.gpio_cfg.invert_flags.mclk_inv = false;
    m_i2sStdCfg.gpio_cfg.invert_flags.bclk_inv = false;
    m_i2sStdCfg.gpio_cfg.invert_flags.ws_inv = false;
    m_i2sStdCfg.clk_cfg.sample_rate_hz = m_i2sItems.sampleRate;
    m_i2sStdCfg.clk_cfg.clk_src = I2S_CLK_SRC_DEFAULT;
    m_i2sStdCfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    result = i2s_channel_init_std_mode(m_i2sTxHandle, &m_i2sStdCfg);
    if (result != ESP_OK) {
        if (result == ESP_ERR_INVALID_ARG)
            AUDIO_LOG_ERROR("invalid i2s configuration or i2s not standard mode");
        if (result == ESP_ERR_INVALID_STATE)
            AUDIO_LOG_ERROR("This i2s channel is not initialized or not stopped");
        return false;
    }
    return true;
}

/**
 * @brief Konfiguriert die I2S-Anschlüsse und öffnet den Kanal
 */
bool Audio::setPinout(uint8_t BCLK, uint8_t LRC, uint8_t DOUT, int8_t MCLK) {

    i2s_std_gpio_config_t gpio_cfg = {};
    bool result = i2sConfig();

    gpio_cfg.bclk = (gpio_num_t)BCLK;
    gpio_cfg.din = (gpio_num_t)I2S_GPIO_UNUSED;
    gpio_cfg.dout = (gpio_num_t)DOUT;
    gpio_cfg.mclk = (gpio_num_t)MCLK;
    gpio_cfg.ws = (gpio_num_t)LRC;

    m_fPsramFound = psram_found();
    if (!m_fPsramFound) {
        AUDIO_LOG_ERROR("PSRAM not found");
        result = false;
        goto exit;
    }

    m_outBuff.alloc_array(OUT_BUFF_SIZE, "m_outBuff");
    m_resamplesBuff.alloc_array(RESAMPLES_BUFF_SIZE, "m_resamplesBuff");
#if AUDIO_FEATURE_VU || AUDIO_FEATURE_SPECTRUM
    m_vuItems.delay_l.alloc_array(m_i2sChanCfg.dma_desc_num * m_i2sChanCfg.dma_frame_num, "delay_l");
    m_vuItems.delay_r.alloc_array(m_i2sChanCfg.dma_desc_num * m_i2sChanCfg.dma_frame_num, "delay_r");
#endif
#if AUDIO_FEATURE_SPECTRUM
    m_fftItems.buffer.alloc_array(m_fftItems.SIZE, "buffer");
    m_fftItems.window.alloc_array(m_fftItems.SIZE, "window");
    m_fftItems.work.alloc_array(m_fftItems.SIZE * 2, "work");
#endif
    m_metadataBuff.alloc(4096 + 1, "m_metadataBuff");   // höchstens 4096 Bytes plus Nullterminierung
    m_httpRespHdrBuff.alloc(4096, "m_httpRespHdrBuff"); // genug Platz für den HTTP-Antwort-Header

    if (!m_outBuff.valid() || !m_resamplesBuff.valid()) {
        result = false;
        goto exit;
    }
#if AUDIO_FEATURE_VU || AUDIO_FEATURE_SPECTRUM
    if (!m_vuItems.delay_l.valid() || !m_vuItems.delay_r.valid()) {
        result = false;
        goto exit;
    }
#endif
#if AUDIO_FEATURE_SPECTRUM
    if (!m_fftItems.buffer.valid() || !m_fftItems.window.valid() || !m_fftItems.work.valid()) {
        result = false;
        goto exit;
    }

    // FFT-Initialisierung
    for (int i = 0; i < m_fftItems.SIZE; i++) { // Hann-Fenster
        m_fftItems.window[i] = 0.5f * (1.0f - cosf(2.0f * M_PI * i / (m_fftItems.SIZE - 1)));
    }

    if (dsps_fft2r_init_fc32(nullptr, m_fftItems.SIZE) == ESP_OK) {
        m_fftItems.initialized = true;
    } else {
        AUDIO_LOG_ERROR("FFT init failed (size={})", m_fftItems.SIZE);
        result = false;
        goto exit;
    }
#endif

    i2sStop();
    if (i2s_channel_reconfig_std_gpio(m_i2sTxHandle, &gpio_cfg) != ESP_OK) {
        result = false;
        goto exit;
    }
    i2sStart();

exit:

    calculateVolumeLimits(); // einmalige Anlage, vol = 21, vol_steps = 21
    startAudioTask();

    m_fI2sInit = result;
    return m_fI2sInit;
}

/**
 * @brief Meldet, ob gerade eine Quelle geöffnet ist
 */
bool Audio::isRunning() {
    return m_fRunning;
}

/**
 * @brief Größe der laufenden Datei in Bytes, 0 bei einem Stream
 */
uint32_t Audio::getFileSize() {
    if (!m_audiofile) {
        if (m_audioFileSize > 0) {
            return m_audioFileSize;
        }
        return 0;
    }
    return m_audioFileSize;
}

/**
 * @brief Gesamtdauer der laufenden Datei in Sekunden
 */
uint32_t Audio::getAudioFileDuration() {
    if (!getBitRate())
        return 0;
    if (m_playlistFormat == FORMAT_M3U8)
        return 0;
    if (!m_audioDataSize)
        return 0;
    return m_audioFileDuration;
}

/**
 * @brief Bereits abgespielte Zeit in Sekunden
 */
uint32_t Audio::getAudioCurrentTime() {
    return m_audioCurrentTime;
}

/**
 * @brief Aktuelle Leseposition in der laufenden Datei
 */
uint32_t Audio::getAudioFilePosition() {
    if (!m_fStream)
        return 0;
    if ((m_dataMode != AUDIO_LOCALFILE) && (m_streamType != ST_WEBFILE)) {
        AUDIO_LOG_WARN("audio is not a file");
        return 0;
    }
    return m_audioFilePosition - inBufferFilled();
}

/**
 * @brief Setzt die Leseposition in der laufenden Datei
 */
bool Audio::setAudioFilePosition(uint32_t pos) {
    if (!m_fStream)
        return false;
    if ((m_dataMode != AUDIO_LOCALFILE) && (m_streamType != ST_WEBFILE)) {
        AUDIO_LOG_WARN("audio is not a file");
        return false;
    }
    if ((m_streamType == ST_WEBFILE) && (!m_fAcceptRanges)) {
        AUDIO_LOG_WARN("server does not accept ranges");
        return false;
    }
    if (pos > m_audioDataStart + m_audioDataSize) {
        AUDIO_LOG_WARN("given position is too large");
        return false;
    }
    if (pos < m_audioDataStart) {
        AUDIO_LOG_WARN("set audiodatastart at {}", m_audioDataStart);
        m_resumeFilePos = m_audioDataStart;
    }
    m_resumeFilePos = pos;
    syncSampleCountToFilePos();
    return true;
}

/**
 * @brief Rechnet eine Abspielzeit über die Xing-Sprungtabelle in eine Dateiposition um
 */
uint32_t Audio::xingTimeToFilePos(uint32_t sec) {
    if (!m_xing.hasToc || !m_audioFileDuration)
        return m_audioDataStart;
    float percent = 100.0f * sec / m_audioFileDuration;
    if (percent > 100.0f)
        percent = 100.0f;
    int idx = (int)percent;
    if (idx > 99)
        idx = 99;
    float a = m_xing.toc[idx];
    float b = (idx < 99) ? m_xing.toc[idx + 1] : 256.0f;
    float rel = (a + (b - a) * (percent - idx)) / 256.0f;
    return m_xing.tocBase + (uint32_t)(rel * m_xing.tocBytes);
}

/**
 * @brief Rechnet eine Dateiposition über die Xing-Sprungtabelle in die Abspielzeit um
 */
uint32_t Audio::xingFilePosToTime(uint32_t pos) {
    if (!m_xing.hasToc || !m_audioFileDuration || !m_xing.tocBytes)
        return 0;
    if (pos <= m_xing.tocBase)
        return 0;
    float rel = 256.0f * (pos - m_xing.tocBase) / m_xing.tocBytes;
    if (rel >= 256.0f)
        return m_audioFileDuration;
    int idx = 0;
    while (idx < 99 && m_xing.toc[idx + 1] <= rel)
        idx++;
    float a = m_xing.toc[idx];
    float b = (idx < 99) ? m_xing.toc[idx + 1] : 256.0f;
    float percent = idx;
    if (b > a && rel > a)
        percent += (rel - a) / (b - a);
    return (uint32_t)(percent / 100.0f * m_audioFileDuration);
}

/**
 * @brief Setzt den Sample-Zähler nach einem Positionswechsel auf die neue Abspielzeit
 *
 * Ohne Sprungtabelle gilt die Verhältnisgleichung sum_samples / tota_samples = (Position - Datenanfang) / Datenlänge, bei variabler Bitrate liefert die Sprungtabelle den genauen Wert.
 */
void Audio::syncSampleCountToFilePos() {
    if (m_xing.hasToc && m_i2sItems.sampleRate) {
        m_cat.sum_samples = (uint64_t)xingFilePosToTime(m_resumeFilePos) * m_i2sItems.sampleRate;
        return;
    }
    m_cat.sum_samples = (float)m_cat.tota_samples * ((float)(m_resumeFilePos - m_audioDataStart) / m_audioDataSize);
}

/**
 * @brief Springt an die angegebene Abspielzeit
 */
bool Audio::setAudioPlayTime(uint16_t sec) {
    // setAudioPlayTime(300) springt zum Beispiel an die Position 5 Minuten
    if ((m_dataMode != AUDIO_LOCALFILE) && (m_streamType != ST_WEBFILE))
        return false;
    if (!getBitRate())
        return false;
    if (!m_fRunning)
        return false;

    if (sec > getAudioFileDuration()) {
        AUDIO_LOG_WARN("setAudioPlayTime: {}s >= audioFileDuration: {}s -> stopSong", sec, getAudioFileDuration());
        stopSong();
        return false;
    }
    uint32_t filepos;
    if (m_xing.hasToc)
        filepos = xingTimeToFilePos(sec);
    else
        filepos = m_audioDataStart + (getBitRate() * sec / 8);
    m_resumeFilePos = filepos;
    syncSampleCountToFilePos();
    return true;
}

/**
 * @brief Spult um die angegebenen Sekunden vor oder zurück
 */
bool Audio::setTimeOffset(int sec) {
    if ((m_dataMode != AUDIO_LOCALFILE) && (m_streamType != ST_WEBFILE)) {
        AUDIO_LOG_WARN("{}", "not a file");
        return false;
    }
    if (!getBitRate())
        return false;
    if (!m_fRunning)
        return false;

    int32_t newTime = getAudioCurrentTime() + sec;
    if (newTime < 0)
        newTime = 0;
    if (newTime > getAudioFileDuration()) {
        stopSong();
        return true;
    }

    if (m_xing.hasToc) {
        m_resumeFilePos = xingTimeToFilePos(newTime);
    } else {
        uint32_t oneSec = getBitRate() / 8; // dekodierte Bytes je Sekunde
        int32_t offset = oneSec * sec;      // Bytes zum Vor- oder Zurückspulen
        int32_t pos = m_audioFilePosition - inBufferFilled();
        pos += offset;
        m_resumeFilePos = pos;
    }
    syncSampleCountToFilePos();
    return true;
}

/**
 * @brief Legt die Anzahl der Lautstärkestufen fest und skaliert die aktuelle Lautstärke um
 */
void Audio::setVolumeSteps(uint8_t steps) {
    // neue Stufenzahl begrenzen
    uint8_t new_steps = max(steps, (uint8_t)21);
    uint8_t old_steps = m_audioItems.volume_steps;

    if (new_steps == old_steps)
        return;

    // Verhältnis alte zu neuen Stufen
    float corr = (float)new_steps / (float)old_steps;

    // Ziel-Lautstärke umskalieren
    float new_volume = (float)m_audioItems.volume * corr;

    // die aktuelle Lautstärke muss mitskaliert werden, sonst springt der Pegel
    m_audioItems.cur_volume *= corr;

    // übernehmen und begrenzen
    m_audioItems.volume_steps = new_steps;

    m_audioItems.volume = (uint8_t)lroundf(new_volume);
    if (m_audioItems.volume > new_steps)
        m_audioItems.volume = new_steps;

    if (m_audioItems.cur_volume > (float)new_steps)
        m_audioItems.cur_volume = (float)new_steps;
}

/**
 * @brief Anzahl der Lautstärkestufen
 */
uint8_t Audio::getVolumeSteps() {
    return m_audioItems.volume_steps;
}

/**
 * @brief Schaltet stumm oder wieder laut
 */
void Audio::setMute(bool mute) {
    m_audioItems.mute = mute;
}

/**
 * @brief Meldet den Stummzustand
 */
bool Audio::getMute() {
    return m_audioItems.mute;
}

/**
 * @brief Liest ein einzelnes Byte aus der Datei
 */
int32_t Audio::audioFileRead() {
    int res = -1;
    if (m_dataMode == AUDIO_LOCALFILE) {
        res = fgetc(m_audiofile);
        if (res >= 0)
            m_audioFilePosition++;
    } else {
        res = m_client->read();
        if (res >= 0)
            m_audioFilePosition++;
    }
    return res;
}

/**
 * @brief Liest ein einzelnes Byte aus der Datei, mit Zeitgrenze
 */
int32_t Audio::audioFileRead(uint16_t timeout_ms) {
    int32_t res = -1;
    uint32_t timeout = audio_millis() + timeout_ms;

    while (true) {
        res = audioFileRead();
        if (res >= 0)
            break;
        if (timeout < audio_millis()) {
            AUDIO_LOG_ERROR("timeout");
            return -1;
        }
        vTaskDelay(10);
    }
    return res;
}

/**
 * @brief Liest einen Block aus der Datei
 */
int32_t Audio::audioFileRead(uint8_t* buff, size_t len) {
    if (buff && len == 0)
        return 0; // nichts zu tun
    int32_t readed_bytes = 0;
    uint32_t offset = 0;
    int res = -1;

    
    if (m_dataMode == AUDIO_LOCALFILE) {
        readed_bytes = static_cast<int32_t>(fread(buff + offset, 1, len, m_audiofile));
        if (readed_bytes >= 0) {
            m_audioFilePosition += readed_bytes;
            len -= readed_bytes;
            offset += readed_bytes;
            res = offset;
        }
    } else {
        readed_bytes = m_client->read(buff + offset, len);
        if (readed_bytes > 0) {
            m_audioFilePosition += readed_bytes;
            len -= readed_bytes;
            offset += readed_bytes;
            res = offset;
        }
    }
    return res;
}

/**
 * @brief Liest einen Block aus der Datei, mit Zeitgrenze
 */
int32_t Audio::audioFileRead(uint8_t* buff, size_t len, uint16_t timeout_ms) {

    uint32_t timeout = audio_millis() + timeout_ms;
    int32_t bytes_has_read = 0;
    uint8_t cnt = 0;
    while (bytes_has_read < len) {
        int32_t res = audioFileRead(buff + bytes_has_read, len - bytes_has_read);
        if (res <= 0) {
            vTaskDelay(10);
        }
        if (res > 0) {
            bytes_has_read += res;
        }
        if (timeout < audio_millis()) {
            AUDIO_LOG_ERROR("timeout, len: {} != bytes_has_read: {}", len, bytes_has_read);
            return -1;
        }
        AUDIO_LOG_DEBUG("buffFillValue {}, res {}, bytes_has_read {}", len, res, bytes_has_read);
    }
    AUDIO_LOG_DEBUG("len: {} != bytes_has_read: {}", len, bytes_has_read);
    return bytes_has_read;
}

/**
 * @brief Setzt die Dateiposition, bei Web-Dateien über eine Range-Anfrage
 */
int32_t Audio::audioFileSeek(uint32_t position, size_t len) {
    int32_t res = -1;

    if (m_dataMode == AUDIO_LOCALFILE) {
        uint32_t actualPos = static_cast<uint32_t>(ftell(m_audiofile));
        if (actualPos != m_audioFilePosition) {
            AUDIO_LOG_DEBUG("actualPos != m_audioFilePosition {} != {}", actualPos, m_audioFilePosition);
            m_audioFilePosition = actualPos;
        }
        if (!m_audiofile)
            return -1;
        if (position > m_audioFileSize) {
            AUDIO_LOG_WARN("position larger than size {} > {}", position, m_audioFileSize);
            position = m_audioFileSize;
        }
        bool r = (fseek(m_audiofile, position, SEEK_SET) == 0);
        m_audioFilePosition = static_cast<uint32_t>(ftell(m_audiofile));
        if (r == false) {
            AUDIO_LOG_ERROR("something went wrong");
            return -1;
        } else {
            return position;
        }
    } else {
        if (m_fAcceptRanges) {
            bool r;
            if (len == 0)
                len = UINT32_MAX;
            r = httpRange(position, len);
            if (res == false) {
                AUDIO_LOG_ERROR("http range request was not successful");
                return 0;
            }
            r = parseHttpRangeHeader();
            if (r == false) {
                AUDIO_LOG_ERROR("http range response was not successful");
                return 0;
            }
            m_audioFilePosition = position;
            return position;
        }
    }
    return res;
}

/**
 * @brief Setzt die Leseposition der lokalen Datei
 */
bool Audio::fsRange(uint32_t range) {

    if ((m_dataMode == AUDIO_LOCALFILE) && !m_audiofile) {
        AUDIO_LOG_WARN("{}", "local file not accessibble");
        return false;
    }
    uint32_t startAB = m_audioDataStart;                 // Anfang des Audiodaten-Blocks
    uint32_t endAB = m_audioDataStart + m_audioDataSize; // Ende des Audiodaten-Blocks
    if (range < (int32_t)startAB) {
        range = startAB;
    }
    if (range >= (int32_t)endAB) {
        range = endAB;
    }

    m_validSamples = 0;
    m_resumeFilePos = range; // wird in processLocalFile() ausgewertet
    return true;
}

/**
 * @brief Übernimmt die Abtastrate der Quelle und konfiguriert I2S neu
 */
bool Audio::setSampleRate(uint32_t sampRate) {

    if (!sampRate)
        return false;
    if (sampRate < 8000) {
        AUDIO_LOG_WARN("Sample rate must not be smaller than 8kHz, found: {}", sampRate);
        return false;
    }
    m_i2sItems.sampleRate = sampRate;
    reconfigI2S();
    iirCalculateCoefficients();
    return true;
}

/**
 * @brief Abtastrate der Quelle
 */
uint32_t Audio::getSampleRate() {
    return m_i2sItems.sampleRate;
}

/**
 * @brief Übernimmt die Auflösung der Quelle
 */
bool Audio::setBitsPerSample(int bits) {
    if ((bits != 32) && (bits != 24) && (bits != 16) && (bits != 8))
        return false;
    m_bitsPerSample = bits;
    return true;
}

/**
 * @brief Auflösung der Quelle in Bits je Sample
 */
uint8_t Audio::getBitsPerSample() {
    return m_bitsPerSample;
}

/**
 * @brief Übernimmt die Kanalzahl der Quelle
 */
bool Audio::setChannels(int ch) {
    m_channels = ch;
    return true;
}

/**
 * @brief Anzahl der Kanäle
 */
uint8_t Audio::getChannels() {
    if (m_channels == 0) { // darf nicht vorkommen
        m_channels = 2;
    }
    return m_channels;
}

/**
 * @brief Bitrate der Quelle, bei variabler Bitrate der Mittelwert
 */
uint32_t Audio::getBitRate() {
    if (m_nominalBitrate)
        return m_nominalBitrate;
    return m_avrBitrate;
}

/**
 * @brief Liest die letzte Granule-Position einer OGG-Datei für die Dauerberechnung
 */
uint64_t Audio::getLastGranulePosition(uint8_t codec) {
    if (codec != CODEC_OPUS && codec != CODEC_VORBIS)
        return 0; // nur für OPUS und VORBIS
    if (m_audioFileSize == 0 || (m_dataMode != AUDIO_LOCALFILE && !m_fAcceptRanges))
        return 0; // nur für Dateien, ohne Range-Unterstützung würde das Endlesen Audiodaten verbrauchen

    constexpr int32_t OGG_HEADER_BYTES = 27; ///< Fester OGG-Seitenkopf einschließlich Segmentanzahl
    const uint32_t tailBytes = min(m_audioFileSize, static_cast<uint32_t>(UINT16_MAX));
    const uint32_t rangeStart = m_audioFileSize - tailBytes;
    const uint32_t afp = m_audioFilePosition;
    uint64_t granulePos = 0;
    ps_ptr<char> buff;
    if (!buff.alloc(tailBytes, "buff"))
        return 0;
    if (audioFileSeek(rangeStart, tailBytes) != static_cast<int32_t>(rangeStart))
        return 0;

    const int32_t bytesRead = audioFileRead(reinterpret_cast<uint8_t*>(buff.get()), tailBytes, 3000);
    if (bytesRead >= OGG_HEADER_BYTES) {
        const int32_t pos = buff.last_special_index_of("OggS", bytesRead);
        if (pos >= 0 && pos <= bytesRead - OGG_HEADER_BYTES && (buff[pos + 5] & 0x04)) { // letzte OGG-Seite
            for (int j = 0; j < 8; j++) {
                granulePos |= static_cast<uint64_t>(static_cast<uint8_t>(buff[pos + 6 + j])) << (j * 8);
            }
        }
    }
    AUDIO_LOG_DEBUG("granulePos {}", granulePos);
    audioFileSeek(afp); // wiederherstellen
    return granulePos;
}

/**
 * @brief Wählt zwischen Philips- und LSB-Format auf dem I2S-Bus
 */
void Audio::setI2sCommFmtLsb(bool commFMT) {
    // false: Standardformat I2S_COMM_FORMAT_I2S_MSB (AC101, PCM5102A)
    // true: I2S_COMM_FORMAT_I2S_LSB für manche DACs (PT8211),
    //       auch LSBJ-Format genannt (Least Significant Bit Justified)

    if (commFMT) {
        info(*this, evt_info, "commFMT = LSBJ (Least Significant Bit Justified)");
    } else {
        info(*this, evt_info, "commFMT = Philips");
    }
    m_i2sItems.commFMT = commFMT;
    reconfigI2S();
    return;
}

/**
 * @brief Konfiguriert den I2S-Takt nach einem Formatwechsel neu
 */
void Audio::reconfigI2S() {

    i2s_channel_disable(m_i2sTxHandle);

    if (m_i2sItems.commFMT) {
        m_i2sStdCfg.slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO);
    } else {
        m_i2sStdCfg.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO);
    }
    i2s_channel_reconfig_std_slot(m_i2sTxHandle, &m_i2sStdCfg.slot_cfg);

    if (m_outputSr) {
        m_resampler.phase = 0; // Resampler vorbereiten
        m_resampler.phaseStep = ((uint64_t)m_i2sItems.sampleRate << 32) / m_outputSr;
        m_resampler.g_lpCoeffs = makeButterworthLPF_Q31(m_i2sItems.sampleRate);
        m_resampler.lpLeft = {};
        m_resampler.lpRight = {};
        m_i2sStdCfg.clk_cfg.sample_rate_hz = m_outputSr;
        AUDIO_LOG_DEBUG("output samplerate is {}", m_i2sStdCfg.clk_cfg.sample_rate_hz);
    } else {
        m_i2sStdCfg.clk_cfg.sample_rate_hz = m_i2sItems.sampleRate;
    }
    i2s_channel_reconfig_std_clock(m_i2sTxHandle, &m_i2sStdCfg.clk_cfg);
    i2s_channel_enable(m_i2sTxHandle);
}

/**
 * @brief Berechnet die VU-Pegel als Hüllkurvenfolger
 */
void Audio::calculateVUlevel(int32_t* sample) {

    uint16_t DELAY_BUFFER_SIZE = m_i2sChanCfg.dma_desc_num * m_i2sChanCfg.dma_frame_num; // Runtime in I2S-DMA
    // Verzögerungsleitung
    m_vuItems.delay_l[m_vuItems.delay_line_index] = sample[LEFTCHANNEL];
    m_vuItems.delay_r[m_vuItems.delay_line_index] = sample[RIGHTCHANNEL];
    m_vuItems.delay_line_index++;
    if (m_vuItems.delay_line_index == DELAY_BUFFER_SIZE)
        m_vuItems.delay_line_index = 0;

    int16_t pos = m_vuItems.delay_line_index - 1;
    if (pos == -1)
        pos = DELAY_BUFFER_SIZE - 1;

#if AUDIO_FEATURE_SPECTRUM
    // FFT buffer
    float mono = 0.5f * (float)((m_vuItems.delay_l[pos] >> 20) + (m_vuItems.delay_r[pos] >> 20));

    // automatische Verstärkung des FFT-Analysators
    constexpr float TARGET = 0.1f; // gewünschte RMS-Amplitude
    constexpr float ATTACK = 0.05f;
    constexpr float RELEASE_FFT = 0.005f;

    float level = fabsf(mono);

    if (level > 1e-6f) {
        float desired = TARGET / level;
        if (desired < m_fftItems.gain)
            m_fftItems.gain += ATTACK * (desired - m_fftItems.gain);
        else
            m_fftItems.gain += RELEASE_FFT * (desired - m_fftItems.gain);
    }
    if (fabsf(mono) < 1e-4f)
        mono = 0.0f;
    mono *= m_fftItems.gain;

    m_fftItems.buffer[m_fftItems.buffer_index] = mono;
    m_fftItems.buffer_index++;
    if (m_fftItems.buffer_index == m_fftItems.SIZE)
        m_fftItems.buffer_index = 0;
#endif

    uint8_t l = 0, r = 0;
    l = abs(m_vuItems.delay_l[pos] >> 23);
    r = abs(m_vuItems.delay_r[pos] >> 23);

    // Anstieg sofort übernehmen
    constexpr float RELEASE = 1.0f; // je größer, desto träger
    if (l > m_vuItems.left) {
        m_vuItems.left = l;
    } else if (m_vuItems.left > RELEASE) {
        m_vuItems.left -= RELEASE;
    }
    if (r > m_vuItems.right) {
        m_vuItems.right = r;
    } else if (m_vuItems.right > RELEASE) {
        m_vuItems.right -= RELEASE;
    }

    // links
    if (m_vuItems.left > m_vuItems.left_peak) {
        m_vuItems.left_peak = m_vuItems.left;
        m_vuItems.left_hold = settings.PEAK_HOLD_SAMPLES;
    } else {
        if (m_vuItems.left_hold > 0) {
            m_vuItems.left_hold--;
        } else if (m_vuItems.left_peak > settings.PEAK_RELEASE) {
            m_vuItems.left_peak -= settings.PEAK_RELEASE;
        }
    }

    // rechts
    if (m_vuItems.right > m_vuItems.right_peak) {
        m_vuItems.right_peak = m_vuItems.right;
        m_vuItems.right_hold = settings.PEAK_HOLD_SAMPLES;
    } else {
        if (m_vuItems.right_hold > 0) {
            m_vuItems.right_hold--;
        } else if (m_vuItems.right_peak > settings.PEAK_RELEASE) {
            m_vuItems.right_peak -= settings.PEAK_RELEASE;
        }
    }
}

/**
 * @brief VU-Pegel beider Kanäle, je ein Byte
 */
uint16_t Audio::getVUlevel() {
    if (!m_fRunning)
        return 0;
    AUDIO_LOG_DEBUG("{}", m_vuItems.left);
    // Mittelwert 0 bis 255                                                                                  MSB          LSB
    return ((uint8_t)m_vuItems.right_peak << 8) + (uint8_t)m_vuItems.left_peak; // liefert rrrrrrrrllllllll
}

/**
 * @brief Setzt die drei Verstärkungen der Klangregelung
 */
void Audio::setTone(float gainLowPass, float gainBandPass, float gainHighPass) {

    // gainLowPass   set between -12 ... +12 dB
    // gainBandPass  set between -12 ... +12 dB
    // gainHighPass  set between -12 ... +12 dB

    m_audioItems.gain_ls_db = fminf(fmaxf(gainLowPass, -12.0f), 12.0f);
    m_audioItems.gain_peq_db = fminf(fmaxf(gainBandPass, -12.0f), 12.0f);
    m_audioItems.gain_hs_db = fminf(fmaxf(gainHighPass, -12.0f), 12.0f);

    const bool enableFilter = m_audioItems.gain_ls_db != 0.0f || m_audioItems.gain_peq_db != 0.0f || m_audioItems.gain_hs_db != 0.0f;
    if (!enableFilter) {
        if (settings.IIR_FILTER)
            memset(m_audioItems.state_biquad, 0, sizeof(m_audioItems.state_biquad));
        settings.IIR_FILTER = false;
        m_audioItems.pre_gain = 1.0f;
        return;
    }

    settings.IIR_FILTER = true;
    iirCalculateCoefficients();
}

/**
 * @brief Erzwingt die Zusammenmischung auf Mono
 */
void Audio::forceMono(bool m) {
    m_fForceMono = m;          // false Stereo, true Mono
}

/**
 * @brief Legt die Abtastrate am Ausgang fest, SR_ORIGIN gibt die Quellrate durch
 */
void Audio::setOutputSampleRate(OutputSR_t sr) {
    if (sr == SR_44100) {
        m_outputSr = SR_44100; // Ausgang fest auf 44.1 kHz
    } else if (sr == SR_48000) {
        m_outputSr = SR_48000; // Ausgang fest auf 48 kHz
    } else {
        m_outputSr = SR_ORIGIN; // Ausgang folgt der Quellrate
    }
    reconfigI2S();
}

/**
 * @brief Verschiebt die Balance zwischen -16.0 dB links und +16.0 dB rechts
 */
void Audio::setBalance(float balance) {
    m_audioItems.balance = fminf(fmaxf(balance, -16.0f), 16.0f);
    calculateVolumeLimits();
}

/**
 * @brief Setzt die Basisbreite des Stereobilds in Prozent
 */
void Audio::setStereoWidth(uint8_t width) {
    m_stereoWidthPct = min(width, static_cast<uint8_t>(200));
}

/**
 * @brief Setzt die Ziel-Lautstärke
 */
void Audio::setVolume(uint8_t volume, uint8_t curve) {
    m_audioItems.volume = min(volume, m_audioItems.volume_steps);

    // der Parameter curve ist ohne Funktion
    calculateVolumeLimits();
}

/**
 * @brief Hinterlegt eine eigene Lautstärkekennlinie
 */
void Audio::setVolumeCurve(VolumeCurveFn curve) {

    // Eigene Kennlinie, im Anwendungscode zu setzen, zum Beispiel im setup().
    // t ist volume / steps im Bereich 0.0f bis 1.0f,
    // der Rückgabewert liegt zwischen -60.0 dB und 0.0 dB.
    // Beispiel:
    //   audio.setVolumeCurve([](float t) {
    //       return -60.0f + 60.0f * t * t;
    //   });

    m_volumeCurve = curve;
    calculateVolumeLimits();
}

/**
 * @brief Aktuelle Ziel-Lautstärke
 */
uint8_t Audio::getVolume() {
    return m_audioItems.volume;
}

/**
 * @brief Nummer des I2S-Ports
 */
int32_t Audio::getI2sPort() {
    return m_i2sItems.i2s_num;
}

/**
 * @brief Führt die aktuelle Lautstärke in kleinen Schritten an den Zielwert heran
 */
void Audio::gainRamp() {
    settings.VOL_FADING_SPEED = std::clamp(settings.VOL_FADING_SPEED, 1.0f, 100.0f); // Division durch null vermeiden
    // Zielwert bestimmen
    float target = m_audioItems.mute ? 0.0f : (float)m_audioItems.volume;

    // größte Änderung je Tick
    float step = (float)m_audioItems.volume_steps / settings.VOL_FADING_SPEED;

    float diff = target - m_audioItems.cur_volume;

    if (fabsf(diff) <= step) {
        // Ziel erreicht, kein Überschwingen
        m_audioItems.cur_volume = target;
    } else {
        // Rampe
        m_audioItems.cur_volume += (diff > 0.0f ? step : -step);
    }

    // Sicherheitsbegrenzung
    if (m_audioItems.cur_volume < 0.0f)
        m_audioItems.cur_volume = 0.0f;
    else if (m_audioItems.cur_volume > m_audioItems.volume_steps)
        m_audioItems.cur_volume = m_audioItems.volume_steps;

    calculateVolumeLimits();
}

/**
 * @brief Berechnet die Kanal-Limiter aus Lautstärke, Balance und Kennlinie neu
 */
void Audio::calculateVolumeLimits() {

    constexpr float BALANCE_DB = -16.0f;
    constexpr float MIN_DB = -60.0f; // leise
    constexpr float MAX_DB = 0.0f;   // voller Pegel

    auto volumeToLinear = [&](float volume, uint8_t steps) {
        if (volume <= 0.0f) {
            return 0.0f; // echte Stille
        }

        float t = volume / (float)steps; // 0.0f bis 1.0f
        t = fminf(fmaxf(t, 0.0f), 1.0f);

        //    float dB = MIN_DB + t * (MAX_DB - MIN_DB);
        float dB = m_volumeCurve ? m_volumeCurve(t) : (-112.0f * t * t * t + 172.0f * t * t + MIN_DB);
        dB = fminf(fmaxf(dB, MIN_DB), MAX_DB);

        return powf(10.0f, dB / 20.0f);
    };

    float vol = volumeToLinear(m_audioItems.cur_volume, m_audioItems.volume_steps);

    float l_db = 0.0f;
    float r_db = 0.0f;

    if (m_audioItems.balance > 0.0f) { // Gewicht rechts, links wird leiser
        l_db = BALANCE_DB * ((float)m_audioItems.balance / 16.0f);
    } else if (m_audioItems.balance < 0.0f) { // Gewicht links, rechts wird leiser
        r_db = BALANCE_DB * ((float)-m_audioItems.balance / 16.0f);
    }

    m_audioItems.limiter[LEFTCHANNEL] = vol * powf(10.0f, l_db / 20.0f);
    m_audioItems.limiter[RIGHTCHANNEL] = vol * powf(10.0f, r_db / 20.0f);
    updateEqHeadroom();
    AUDIO_LOG_DEBUG("m_limiter[LEFTCHANNEL] {}, m_limiter[RIGHTCHANNEL] {}", m_audioItems.limiter[LEFTCHANNEL], m_audioItems.limiter[RIGHTCHANNEL]);
}

/**
 * @brief Berechnet das Spektrum über eine FFT
 */
void Audio::processSpectrum() {

    // Aktualisierung mit 10 Hz
    uint32_t now = audio_millis();
    if (now - m_fftItems.last_ms < 100)
        return;
    m_fftItems.last_ms = now;

    // Fensterung, reelle Werte in komplexes Format bringen
    for (int i = 0; i < m_fftItems.SIZE; i++) {
        m_fftItems.work[2 * i] = m_fftItems.buffer[i] * m_fftItems.window[i];
        m_fftItems.work[2 * i + 1] = 0.0f;
    }

    // FFT ausführen
    dsps_fft2r_fc32(m_fftItems.work.get(), m_fftItems.SIZE);
    dsps_bit_rev_fc32(m_fftItems.work.get(), m_fftItems.SIZE);
    dsps_cplx2reC_fc32(m_fftItems.work.get(), m_fftItems.SIZE);

    const float bin_hz = (float)m_i2sItems.sampleRate / m_fftItems.SIZE;
    const float norm = 2.0f / m_fftItems.SIZE;

    // fünf interne Bänder
    float band[m_fftItems.BANDS] = {0};
    int bins[m_fftItems.BANDS] = {0};

    for (int i = 1; i < m_fftItems.SIZE / 2; i++) {

        float re = m_fftItems.work[2 * i];
        float im = m_fftItems.work[2 * i + 1];
        float mag = sqrtf(re * re + im * im) * norm;
        float f = i * bin_hz;

        int b = -1;
        if (f < 250)
            b = 0;
        else if (f < 400)
            b = -1;
        else if (f < 700)
            b = 1;
        else if (f < 1000)
            b = -1;
        else if (f < 1550)
            b = 2;
        else if (f < 2200)
            b = -1;
        else if (f < 4250)
            b = 3;
        else if (f < 6300)
            b = -1;
        else if (f < 11150)
            b = 4;
        else if (f < 16000)
            b = 5;
        else
            b = -1;

        if (b >= 0) {
            band[b] += mag * mag;
            bins[b]++;
        }
    }

    // Effektivwert und Gewichtung
    for (int i = 0; i < m_fftItems.BANDS; i++) {
        if (bins[i])
            band[i] = sqrtf(band[i] / bins[i]);
        else
            band[i] = 0.0f;
    }

    // Bandgewichtung, psychoakustisch motiviert
    band[0] *= 0.5f;
    band[1] *= 0.8f;
    band[2] *= 3.0f;
    band[3] *= 2.8f;
    band[4] *= 1.5f;
    band[5] *= 1.5f;

    // logarithmische Skala
    for (int i = 0; i < m_fftItems.BANDS; i++) {
        band[i] = log10f(band[i] + 1e-6f);
    }

    // zeitliche Glättung der angezeigten Bänder
    auto smooth = [](float old, float in) {
        constexpr float ATTACK = 0.6f;
        constexpr float RELEASE = 0.6f;
        return (in > old) ? old + ATTACK * (in - old) : old + RELEASE * (in - old);
    };

    for (int i = 0; i < m_fftItems.BANDS; i++) {
        m_fftItems.spec_smooth[i] = smooth(m_fftItems.spec_smooth[i], band[i]);
    }

    // über ein dB-Fenster auf 0 bis 255 abbilden
    constexpr float DB_MIN = -50.0f;
    constexpr float DB_MAX = 0.0f;

    for (int i = 0; i < m_fftItems.BANDS; i++) {

        float db = m_fftItems.spec_smooth[i] * 20.0f;

        if (db < DB_MIN)
            db = DB_MIN;
        if (db > DB_MAX)
            db = DB_MAX;

        float norm = (db - DB_MIN) / (DB_MAX - DB_MIN);

        m_fftItems.spectrum[i] = (uint8_t)(norm * 255.0f);
    }
}

/**
 * @brief Wendet Lautstärke, Balance und Stereobreite auf ein Sample-Paar an
 */
void Audio::applyVolume(int32_t* sample) {
    /* wichtig: diese Multiplikationen müssen vorzeichenbehaftet laufen, sonst ist das Ergebnis falsch */
    int32_t* s32 = (int32_t*)sample;
    s32[LEFTCHANNEL] *= m_audioItems.limiter[LEFTCHANNEL];
    s32[RIGHTCHANNEL] *= m_audioItems.limiter[RIGHTCHANNEL];
}

/**
 * @brief Belegter Platz im Eingangspuffer
 */
uint32_t Audio::inBufferFilled() {
    // belegte Bytes im Eingangspuffer
    return m_inBuff.bufferFilled();
}

/**
 * @brief Freier Platz im Eingangspuffer
 */
uint32_t Audio::inBufferFree() {
    // freie Bytes im Eingangspuffer
    return m_inBuff.freeSpace();
}

/**
 * @brief Größe des Eingangspuffers
 */
uint32_t Audio::getInBufferSize() {
    // Größe des Eingangspuffers in Bytes
    return m_inBuff.getBufsize();
}

/**
 * @brief Mischt beide Kanäle zu Mono zusammen
 */
void Audio::stereo2mono(int32_t* buff, uint16_t validSamples) {

    for (uint16_t i = 0; i < validSamples * 2; i += 2) {
        int64_t l = buff[i];
        int64_t r = buff[i + 1];
        int32_t m = (int32_t)((l + r) >> 1); // Mittelwert ohne Überlauf
        buff[i] = m;                         // links
        buff[i + 1] = m;                     // rechts
    }
}

/**
 * @brief Wendet die Stereobreite über eine Mitte-Seite-Umrechnung an
 */
void Audio::applyStereoWidth(int32_t* buff, uint16_t validSamples) {

    auto clampInt32 = [](int64_t value) -> int32_t {
        if (value > INT32_MAX)
            return INT32_MAX;
        if (value < INT32_MIN)
            return INT32_MIN;
        return static_cast<int32_t>(value);
    };

    const int64_t width = m_stereoWidthPct;
    for (uint16_t i = 0; i < validSamples * 2; i += 2) {
        const int64_t left = buff[i];
        const int64_t right = buff[i + 1];
        const int64_t mid = (left + right) / 2;
        const int64_t side = (left - right) / 2;
        const int64_t scaledSide = (side * width) / 100;
        buff[i] = clampInt32(mid + scaledSide);
        buff[i + 1] = clampInt32(mid - scaledSide);
    }
}
//            ***     D i g i t a l   b i q u a d r a t i c     f i l t e r     ***

/**
 * @brief Berechnet die IIR-Koeffizienten der Klangregelung
 */
void Audio::iirCalculateCoefficients() {

    AUDIO_LOG_DEBUG("gain gain_ls_db {}, gain gain_peq_db {}, gain gain_hs_db {}", m_audioItems.gain_ls_db, m_audioItems.gain_peq_db, m_audioItems.gain_hs_db);

    constexpr uint32_t MAX_EQ_FREQUENCY_PERCENT = 45;
    const uint32_t maxEqFrequencyHz = m_i2sItems.sampleRate * MAX_EQ_FREQUENCY_PERCENT / 100;
    const uint16_t FcLS = static_cast<uint16_t>(std::min<uint32_t>(settings.FREQ_LS_HZ, maxEqFrequencyHz));
    const uint16_t FcPKEQ = static_cast<uint16_t>(std::min<uint32_t>(settings.FREQ_PEAK_HZ, maxEqFrequencyHz));
    const uint16_t FcHS = static_cast<uint16_t>(std::min<uint32_t>(settings.FREQ_HS_HZ, maxEqFrequencyHz));
    const float QS = settings.QUALITY_SLOPE; // Güte der Shelf-Filter

    const float normFreqLS = static_cast<float>(FcLS) / static_cast<float>(m_i2sItems.sampleRate);    // Eckfrequenz des Filters
    const float normFreqPEQ = static_cast<float>(FcPKEQ) / static_cast<float>(m_i2sItems.sampleRate); // Mittenfrequenz des Filters
    const float normFreqHS = static_cast<float>(FcHS) / static_cast<float>(m_i2sItems.sampleRate);    // Eckfrequenz des Filters

    const bool sampleRateChanged = m_audioItems.eq_sample_rate != m_i2sItems.sampleRate;
    const bool qualityChanged = m_audioItems.eq_quality != QS;
    const bool lowChanged =
        sampleRateChanged || qualityChanged || m_audioItems.eq_frequency_hz[LOWSHELF] != FcLS || m_audioItems.eq_gain_db[LOWSHELF] != m_audioItems.gain_ls_db;
    const bool midChanged =
        sampleRateChanged || qualityChanged || m_audioItems.eq_frequency_hz[PEAKINGEQ] != FcPKEQ || m_audioItems.eq_gain_db[PEAKINGEQ] != m_audioItems.gain_peq_db;
    const bool highChanged =
        sampleRateChanged || qualityChanged || m_audioItems.eq_frequency_hz[HIFGSHELF] != FcHS || m_audioItems.eq_gain_db[HIFGSHELF] != m_audioItems.gain_hs_db;

    if (!lowChanged && !midChanged && !highChanged) {
        updateEqHeadroom();
        return;
    }

    auto dsps_biquad_gen_peakingEQ_f32 = [&](float* c, float f, float g, const float Q) -> void {
        float A = powf(10.0f, g / 40.0f);
        float w0 = 2.0f * M_PI * f;
        float cw = cosf(w0);
        float sw = sinf(w0);
        float alpha = sw / (2.0f * Q);
        float b0 = 1.0f + alpha * A;
        float b1 = -2.0f * cw;
        float b2 = 1.0f - alpha * A;
        float a0 = 1.0f + alpha / A;
        float a1 = -2.0f * cw;
        float a2 = 1.0f - alpha / A;
        c[0] = b0 / a0;
        c[1] = b1 / a0;
        c[2] = b2 / a0;
        c[3] = a1 / a0;
        c[4] = a2 / a0;
    };

    if (sampleRateChanged || qualityChanged) {
        memset(m_audioItems.state_biquad, 0, sizeof(m_audioItems.state_biquad));
    } else {
        if (lowChanged && (m_audioItems.eq_gain_db[LOWSHELF] == 0.0f || m_audioItems.gain_ls_db == 0.0f))
            memset(m_audioItems.state_biquad[LOWSHELF], 0, sizeof(m_audioItems.state_biquad[LOWSHELF]));
        if (midChanged && (m_audioItems.eq_gain_db[PEAKINGEQ] == 0.0f || m_audioItems.gain_peq_db == 0.0f))
            memset(m_audioItems.state_biquad[PEAKINGEQ], 0, sizeof(m_audioItems.state_biquad[PEAKINGEQ]));
        if (highChanged && (m_audioItems.eq_gain_db[HIFGSHELF] == 0.0f || m_audioItems.gain_hs_db == 0.0f))
            memset(m_audioItems.state_biquad[HIFGSHELF], 0, sizeof(m_audioItems.state_biquad[HIFGSHELF]));
    }

    if (lowChanged)
        dsps_biquad_gen_lowShelf_f32(m_audioItems.coeffs[LOWSHELF], normFreqLS, m_audioItems.gain_ls_db, QS);
    if (midChanged)
        dsps_biquad_gen_peakingEQ_f32(m_audioItems.coeffs[PEAKINGEQ], normFreqPEQ, m_audioItems.gain_peq_db, QS);
    if (highChanged)
        dsps_biquad_gen_highShelf_f32(m_audioItems.coeffs[HIFGSHELF], normFreqHS, m_audioItems.gain_hs_db, QS);

    m_audioItems.eq_sample_rate = m_i2sItems.sampleRate;
    m_audioItems.eq_frequency_hz[LOWSHELF] = FcLS;
    m_audioItems.eq_frequency_hz[PEAKINGEQ] = FcPKEQ;
    m_audioItems.eq_frequency_hz[HIFGSHELF] = FcHS;
    m_audioItems.eq_gain_db[LOWSHELF] = m_audioItems.gain_ls_db;
    m_audioItems.eq_gain_db[PEAKINGEQ] = m_audioItems.gain_peq_db;
    m_audioItems.eq_gain_db[HIFGSHELF] = m_audioItems.gain_hs_db;
    m_audioItems.eq_quality = QS;

    const float maxBoostDb = fmaxf(fmaxf(fmaxf(0.0f, m_audioItems.gain_ls_db), m_audioItems.gain_peq_db), m_audioItems.gain_hs_db);
    m_audioItems.eq_max_gain = powf(10.0f, maxBoostDb / 20.0f);
    updateEqHeadroom();

    AUDIO_LOG_DEBUG("\n([{}, {}, {}], [1.0, {}, {}]), # LOWSHELF\n([{},  {},  {} ], [1.0, {},  {} ]), # PEAKINGEQ\n([{}, {}, {}], [1.0, {}, {}]), # HIGHSHELF\n",
                    m_audioItems.coeffs[0][0], m_audioItems.coeffs[0][1], m_audioItems.coeffs[0][2], m_audioItems.coeffs[0][3], m_audioItems.coeffs[0][4],
                    m_audioItems.coeffs[1][0], m_audioItems.coeffs[1][1], m_audioItems.coeffs[1][2], m_audioItems.coeffs[1][3], m_audioItems.coeffs[1][4],
                    m_audioItems.coeffs[2][0], m_audioItems.coeffs[2][1], m_audioItems.coeffs[2][2], m_audioItems.coeffs[2][3], m_audioItems.coeffs[2][4]);
    AUDIO_LOG_DEBUG("m_audioItems.pre_gain {}", m_audioItems.pre_gain);
}
// ===================================================================
// EQ-Pegelreserve
// ===================================================================
/**
 * @brief Berechnet die nötige Pegelreserve für die aktuelle Lautstärke und Klangregelung.
 */

/**
 * @brief Passt die Pegelreserve der Klangregelung an die größte Anhebung an
 */
void Audio::updateEqHeadroom() {
    if (!settings.IIR_FILTER) {
        m_audioItems.pre_gain = 1.0f;
        return;
    }

#if AUDIO_EQ_HEADROOM_MODE == AUDIO_EQ_HEADROOM_NONE
    m_audioItems.pre_gain = 1.0f;
#elif AUDIO_EQ_HEADROOM_MODE == AUDIO_EQ_HEADROOM_ALWAYS
    m_audioItems.pre_gain = 1.0f / m_audioItems.eq_max_gain;
#else
    float volumeGain = 1.0f;
    if (settings.VOLUME_CONTROL)
        volumeGain = fmaxf(m_audioItems.limiter[LEFTCHANNEL], m_audioItems.limiter[RIGHTCHANNEL]);

    const float combinedGain = m_audioItems.eq_max_gain * volumeGain;
    m_audioItems.pre_gain = combinedGain > 1.0f ? 1.0f / combinedGain : 1.0f;
#endif
}

/**
 * @brief Wendet die drei Klangregelungs-Filter auf ein Sample-Paar an
 */
void Audio::iirFilter(int32_t* sample) {

    const auto saturateToInt32 = [](float value) -> int32_t {
        if (value >= static_cast<float>(INT32_MAX))
            return INT32_MAX;
        if (value <= static_cast<float>(INT32_MIN))
            return INT32_MIN;
        return static_cast<int32_t>(value);
    };

    int32_t* s32 = sample;
    float s[2];
    s[LEFTCHANNEL] = (float)(s32[LEFTCHANNEL] * m_audioItems.pre_gain);
    s[RIGHTCHANNEL] = (float)(s32[RIGHTCHANNEL] * m_audioItems.pre_gain);
    if (m_audioItems.gain_ls_db != 0.0f)
        dsps_biquad_sf32(s, s, 1, m_audioItems.coeffs[LOWSHELF], m_audioItems.state_biquad[LOWSHELF]);
    if (m_audioItems.gain_peq_db != 0.0f)
        dsps_biquad_sf32(s, s, 1, m_audioItems.coeffs[PEAKINGEQ], m_audioItems.state_biquad[PEAKINGEQ]);
    if (m_audioItems.gain_hs_db != 0.0f)
        dsps_biquad_sf32(s, s, 1, m_audioItems.coeffs[HIFGSHELF], m_audioItems.state_biquad[HIFGSHELF]);
    s32[LEFTCHANNEL] = saturateToInt32(s[LEFTCHANNEL]);
    s32[RIGHTCHANNEL] = saturateToInt32(s[RIGHTCHANNEL]);
    return;
}

//    AAC - T R A N S P O R T S T R E A M

/**
 * @brief Entpackt ein TS-Paket und liefert die Lage der Nutzdaten
 */
bool Audio::tsParsePacket(uint8_t* packet, uint8_t* packetStart, uint8_t* packetLength) {

    bool log = false;

    const uint8_t PAYLOAD_SIZE = 184;
    const uint8_t PID_ARRAY_LEN = 4;

    (void)PAYLOAD_SIZE; // unterdrückt -Wunused-variable

    if (packet == NULL) {
        if (log)
            AUDIO_LOG_WARN("parseTS reset");
        m_tspp.reset();
        return true;
    }

    // Aufbau eines TS-Pakets:
    //
    // +---------------+----+----+----+----+----+----+----+----+
    // | 0. Byte       |  0 |  1 |  0 |  0 |  0 |  1 |  1 |  1 |  Synchronbyte, immer 0x47
    // +---------------+----+----+----+----+----+----+----+----+
    // | 1. Byte       |PUSI| TP |    |PID |PID |PID |PID |PID |
    // +---------------+----+----+----+----+----+----+----+----+
    // | 2. Byte       |PID |PID |PID |PID |PID |PID |PID |PID |
    // +---------------+----+----+----+----+----+----+----+----+
    // | 3. Byte       |TSC |TSC |AFC |AFC | CC | CC | CC | CC |
    // +---------------+----+----+----+----+----+----+----+----+
    // | 4.-187. Byte  |    Nutzdaten, wenn AFC 01 oder 11     |
    // +---------------+---------------------------------------+
    //
    // PUSI: Payload Unit Start Indicator, gesetzt, wenn dieses Paket das erste Byte einer neuen Nutzdaten-Einheit enthält. Das erste Nutzdaten-Byte nennt dann deren Anfang.
    // TP: Transport Priority, gesetzt bei höherer Priorität gegenüber Paketen mit derselben PID.
    // PID: Packet Identifier, beschreibt die Nutzdaten.
    // TSC: Transport Scrambling Control, 00 bedeutet unverschlüsselt.
    // AFC: Adaptation Field Control, 01 nur Nutzdaten, 10 nur Adaptionsfeld, 11 Adaptionsfeld gefolgt von Nutzdaten, 00 reserviert.
    // CC: Continuity Counter, laufende Nummer der Nutzdaten-Pakete (0x00 bis 0x0F) je Strom, außer bei PID 8191.

    if (packet[0] != 0x47) {
        AUDIO_LOG_ERROR("ts SyncByte not found, first bytes are 0x{:02X} 0x{:02X} 0x{:02X} 0x{:02X}", packet[0], packet[1], packet[2], packet[3]);
        stopSong();
        return false;
    }
    int PID = (packet[1] & 0x1F) << 8 | (packet[2] & 0xFF);
    if (log)
        AUDIO_LOG_DEBUG("PID: 0x{:04X} ({})", PID, PID);
    int PUSI = (packet[1] & 0x40) >> 6;
    if (log)
        AUDIO_LOG_DEBUG("Payload Unit Start Indicator: {}", PUSI);
    int AFC = (packet[3] & 0x30) >> 4;
    if (log)
        AUDIO_LOG_DEBUG("Adaption Field Control: {}", AFC);

    int AFL = -1;
    if ((AFC & 0b10) == 0b10) { // AFC '11' Adaptation Field followed
        AFL = packet[4] & 0xFF; // Länge des Adaptionsfelds
        if (log)
            AUDIO_LOG_DEBUG("Adaptation Field Length: {}", AFL);
    }
    int PLS = PUSI ? 5 : 4; // Anfang der Nutzdaten, Payload Unit Start Indicator
    if (AFL > 0)
        PLS += AFL + 1; // Adaptionsfeld überspringen

    if (AFC == 2) { // das TS-Paket enthält nur ein Adaptionsfeld und keine Nutzdaten
        *packetStart = AFL + 1;
        *packetLength = 0;
        return true;
    }

    if (PID == 0) {
        // Program Association Table (PAT)
        if (log)
            AUDIO_LOG_DEBUG("PAT");
        m_tspp.pidNumber = 0;
        m_tspp.pidOfAAC = 0;

        int startOfProgramNums = 8;
        int lengthOfPATValue = 4;
        int sectionLength = ((packet[PLS + 1] & 0x0F) << 8) | (packet[PLS + 2] & 0xFF);
        if (log)
            AUDIO_LOG_DEBUG("Section Length: {}", sectionLength);
        int program_number, program_map_PID;
        int indexOfPids = 0;
        (void)program_number; // unterdrückt -Wunused-but-set-variable
        for (int i = startOfProgramNums; i <= sectionLength; i += lengthOfPATValue) {
            program_number = ((packet[PLS + i] & 0xFF) << 8) | (packet[PLS + i + 1] & 0xFF);
            program_map_PID = ((packet[PLS + i + 2] & 0x1F) << 8) | (packet[PLS + i + 3] & 0xFF);
            if (log)
                AUDIO_LOG_DEBUG("Program Num: 0x{:04X}({}) PMT PID: 0x{:04X}({})", program_number, program_number, program_map_PID, program_map_PID);
            m_tspp.pids[indexOfPids++] = program_map_PID;
        }
        m_tspp.pidNumber = indexOfPids;
        *packetStart = 0;
        *packetLength = 0;
        return true;
    } else if (PID == m_tspp.pidOfAAC) {
        if (log)
            AUDIO_LOG_DEBUG("AAC");
        uint8_t posOfPacketStart = 4;
        if (AFL >= 0) {
            posOfPacketStart = 5 + AFL;
            if (log)
                AUDIO_LOG_DEBUG("posOfPacketStart: {}", posOfPacketStart);
        }
        // Packetized Elementary Stream (PES) - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
        if (log)
            AUDIO_LOG_DEBUG("PES_DataLength {}", m_tspp.PES_DataLength);
        if (m_tspp.PES_DataLength > 0) {
            *packetStart = posOfPacketStart + m_tspp.fillData;
            *packetLength = TS_PACKET_SIZE - posOfPacketStart - m_tspp.fillData;
            if (log)
                AUDIO_LOG_DEBUG("packetlength {}", *packetLength);
            m_tspp.fillData = 0;
            m_tspp.PES_DataLength -= (*packetLength);
            return true;
        } else {
            int firstByte = packet[posOfPacketStart] & 0xFF;
            int secondByte = packet[posOfPacketStart + 1] & 0xFF;
            int thirdByte = packet[posOfPacketStart + 2] & 0xFF;
            if (log)
                AUDIO_LOG_DEBUG("First 3 bytes: 0x{:02X}, 0x{:02X}, 0x{:02X}", firstByte, secondByte, thirdByte);
            if (firstByte == 0x00 && secondByte == 0x00 && thirdByte == 0x01) { // Paket-Startcode-Präfix
                // ===================================================================
                // posOfPacketStart + 0...2     0x00, 0x00, 0x01                                          PES-Startcode
                // ===================================================================
                // posOfPacketStart + 3         0xE0 (Video) od 0xC0 (Audio)                              StreamID
                // ===================================================================
                // posOfPacketStart + 4...5     0xLL, 0xLL                                                PES Packet length
                // ===================================================================
                // posOfPacketStart + 6...7                                                               PTS/DTS-Flags
                // ===================================================================
                // posOfPacketStart + 8         0xXX                                                      header length
                // ===================================================================

                uint8_t StreamID = packet[posOfPacketStart + 3] & 0xFF;
                if (StreamID >= 0xC0 && StreamID <= 0xDF) {
                    ;
                } // okay ist audio stream
                if (StreamID >= 0xE0 && StreamID <= 0xEF) {
                    AUDIO_LOG_ERROR("video stream!");
                    return false;
                }
                int PES_PacketLength = ((packet[posOfPacketStart + 4] & 0xFF) << 8) + (packet[posOfPacketStart + 5] & 0xFF);
                if (log)
                    AUDIO_LOG_DEBUG("PES PacketLength: {}", PES_PacketLength);
                bool PTS_flag = false;
                bool DTS_flag = false;
                int flag_byte1 = packet[posOfPacketStart + 6] & 0xFF;
                int flag_byte2 = packet[posOfPacketStart + 7] & 0xFF;
                (void)flag_byte2; // noch ungenutzt
                if (flag_byte1 & 0b10000000)
                    PTS_flag = true;
                if (flag_byte1 & 0b00000100)
                    DTS_flag = true;
                if (log && PTS_flag)
                    AUDIO_LOG_DEBUG("PTS_flag is set");
                if (log && DTS_flag)
                    AUDIO_LOG_DEBUG("DTS_flag is set");
                uint8_t PES_HeaderDataLength = packet[posOfPacketStart + 8] & 0xFF;
                if (log)
                    AUDIO_LOG_DEBUG("PES_headerDataLength {}", PES_HeaderDataLength);

                m_tspp.PES_DataLength = PES_PacketLength;
                int startOfData = PES_HeaderDataLength + 9;
                if (posOfPacketStart + startOfData >= 188) { // Paket enthält nur Füllbytes
                    if (log)
                        AUDIO_LOG_DEBUG("posOfPacketStart + startOfData {}", posOfPacketStart + startOfData);
                    *packetStart = 0;
                    *packetLength = 0;
                    m_tspp.PES_DataLength -= (PES_HeaderDataLength + 3);
                    m_tspp.fillData = (posOfPacketStart + startOfData) - 188;
                    if (log)
                        AUDIO_LOG_DEBUG("fillData {}", m_tspp.fillData);
                    return true;
                }
                if (log)
                    AUDIO_LOG_DEBUG("First AAC data byte: {:02X}", packet[posOfPacketStart + startOfData]);
                if (log)
                    AUDIO_LOG_DEBUG("Second AAC data byte: {:02X}", packet[posOfPacketStart + startOfData + 1]);
                *packetStart = posOfPacketStart + startOfData;
                *packetLength = TS_PACKET_SIZE - posOfPacketStart - startOfData;
                m_tspp.PES_DataLength -= (*packetLength);
                m_tspp.PES_DataLength -= (PES_HeaderDataLength + 3);
                return true;
            }
            if (firstByte == 0 && secondByte == 0 && thirdByte == 0) {
                // PES-Startcode-Präfix ist 0x000000,
                // solche Pakete werden übersprungen
                return true;
            }
        }
        *packetStart = 0;
        *packetLength = 0;
        AUDIO_LOG_ERROR("PES not found");
        return false;
    } else if (m_tspp.pidNumber) {
        // Program Map Table (PMT)
        for (int i = 0; i < m_tspp.pidNumber; i++) {
            if (PID == m_tspp.pids[i]) {
                if (log)
                    AUDIO_LOG_DEBUG("PMT");
                int staticLengthOfPMT = 12;
                int sectionLength = ((packet[PLS + 1] & 0x0F) << 8) | (packet[PLS + 2] & 0xFF);
                if (log)
                    AUDIO_LOG_DEBUG("Section Length: {}", sectionLength);
                int programInfoLength = ((packet[PLS + 10] & 0x0F) << 8) | (packet[PLS + 11] & 0xFF);
                if (log)
                    AUDIO_LOG_DEBUG("Program Info Length: {}", programInfoLength);
                int cursor = staticLengthOfPMT + programInfoLength;
                while (cursor < sectionLength - 1) {
                    int streamType = packet[PLS + cursor] & 0xFF;
                    int elementaryPID = ((packet[PLS + cursor + 1] & 0x1F) << 8) | (packet[PLS + cursor + 2] & 0xFF);
                    if (log)
                        AUDIO_LOG_DEBUG("Stream Type: 0x{:02X} Elementary PID: 0x{:04X}", streamType, elementaryPID);

                    if (streamType == 0x0F || streamType == 0x11 || streamType == 0x04) {
                        if (log)
                            AUDIO_LOG_DEBUG("AAC PID discover");
                        m_tspp.pidOfAAC = elementaryPID;
                    }
                    int esInfoLength = ((packet[PLS + cursor + 3] & 0x0F) << 8) | (packet[PLS + cursor + 4] & 0xFF);
                    if (log)
                        AUDIO_LOG_DEBUG("ES Info Length: 0x{:04X}", esInfoLength);
                    cursor += 5 + esInfoLength;
                }
            }
        }
        *packetStart = 0;
        *packetLength = 0;
        return true;
    }
    // PES received before PAT and PMT seen
    *packetStart = 0;
    *packetLength = 0;
    if (PID > 0) {
        return true;
    }
    return false;
}
//    W E B S T R E A M  -  H E L P   F U N C T I O N S

/**
 * @brief Liest die ICY-Metadaten aus dem Datenstrom
 */
bool Audio::readMetadata(uint32_t maxBytes, uint16_t* readedBytes, bool first) {
    *readedBytes = 0;
    if (first) {
        m_rmet.pos_ml = 0; // aktuelle Position in der Metazeile
        m_rmet.metaDataSize = 0;
        m_rmet.res = 0;
        m_metadataBuff.clear();
        return true;
    }
    if (!maxBytes)
        return false;
    if (!m_rmet.metaDataSize) {
        int b = audioFileRead(); // erstes Byte der Metadaten?
        if (b < 0) {
            AUDIO_LOG_WARN("client->read() failed ({})", b);
            return false;
        }
        m_rmet.metaDataSize = b * 16; // Länge der Metadaten, höchstens 4096
        m_rmet.pos_ml = 0;
        m_metadataBuff[m_rmet.pos_ml] = 0; // für die neue Zeile vorbereiten
        *readedBytes = 1;
        maxBytes -= 1;
    }
    if (!m_rmet.metaDataSize) {
        return *readedBytes;
    } // Metadaten-Länge ist 0

    int32_t a = audioFileRead((uint8_t*)&m_metadataBuff[m_rmet.pos_ml], min(m_rmet.metaDataSize - m_rmet.pos_ml, maxBytes));
    if (a > 0) {
        m_rmet.res += a;
        *readedBytes += a;
        m_rmet.pos_ml += a;
    }
    if (m_rmet.pos_ml == m_rmet.metaDataSize) {
        m_metadataBuff[m_rmet.pos_ml] = '\0';
        if (m_metadataBuff.strlen() > 0) { // liegt überhaupt etwas an?
            // Die Metazeile enthält Interpret und Titel, zum Beispiel:
            // "StreamTitle='Don McLean - American Pie';StreamUrl='';"
            // Manchmal steht dort anderes, etwa:
            // "StreamTitle='60s 03 05 Magic60s';StreamUrl='';"
            // Der StreamTitle wird isoliert, umschließende Anführungszeichen werden entfernt.
            latinToUTF8(m_metadataBuff);                             // bei Bedarf nach UTF-8 wandeln
            int pos = m_metadataBuff.index_of_icase("song_spot", 0); // belanglose Angaben entfernen
            if (pos > 3) {                                           // zum Beispiel song_spot="T" MediaBaseId="0" itunesTrackId="0"
                m_metadataBuff[pos] = 0;
            }
            showstreamtitle(m_metadataBuff.get()); // Interpret und Titel aus den Metadaten melden, falls vorhanden
        }
        m_metacount = m_metaint;
        m_rmet.metaDataSize = 0;
        m_rmet.pos_ml = 0;
        m_metadataBuff[m_rmet.pos_ml] = 0; // für die neue Zeile vorbereiten
    } else {
        return false; // nicht genug Daten, nächste Runde
    }
    m_metadataBuff.clear();
    return true;
}

/**
 * @brief Liest die Blockgröße des Chunked-Transfers
 */
int32_t Audio::getChunkSize(uint16_t* readedBytes, bool first) {

    // <Blockgröße in Hex>\r\n
    // <Blockdaten>\r\n
    // <Blockgröße in Hex>\r\n
    // <Blockdaten>\r\n
    // ...
    // 0\r\n
    // \r\n

    constexpr uint16_t CHUNK_LINE_LENGTH = 255;

    ps_ptr<char> hex_str;
    int16_t idx1, idx2, idx3;
    int32_t chunkSize = -1;

    if (first) {
        m_gchs.reset();
        return 0;
    }
    if (!m_gchs.chunkLine.valid())
        m_gchs.chunkLine.calloc(CHUNK_LINE_LENGTH, "m_gchs1.chunkLine");

    auto hex_to_int = [&](ps_ptr<char> hexstr) -> int32_t {
        if (!hexstr.valid())
            return -1;
        for (int i = 0; i < hexstr.strlen(); i++) {
            if (!isxdigit(hexstr[i]))
                return -1;
        }
        return hexstr.to_int32(16);
    };

    *readedBytes = 0;

    while (true) {
        int16_t b = audioFileRead();
        if (b == -1) { // mehr Daten nötig
            chunkSize = -1;
            break;
        }

        m_gchs.chunkLine[m_gchs.position] = b;
        *readedBytes += 1;
        m_gchs.position += 1;

        if (m_gchs.chunkLine == "\r\n") { // CRLF überspringen
            m_gchs.chunkLine.clear();
            m_gchs.position = 0;
            AUDIO_LOG_DEBUG("skip CRLF");
        }

        if (m_gchs.position == CHUNK_LINE_LENGTH) {
            AUDIO_LOG_ERROR("ChunkLine is too long");
            m_gchs.chunkLine.hex_dump(CHUNK_LINE_LENGTH);
            goto error;
        }
        if (m_gchs.chunkLine.ends_with("\r\n") && m_gchs.chunkSize == -1) {
            idx1 = m_gchs.chunkLine.index_of(";");
            if (idx1 > 0) {                                                 // eine Erweiterung folgt, etwa "AF4;test=123\r\n"
                hex_str = m_gchs.chunkLine.substr(0, idx1);                 // "AF4;test=123\r\n" -> "AF4"
                ps_ptr<char> extension = m_gchs.chunkLine.substr(idx1 + 1); // "AF4;test=123\r\n" -> "test=123\r\n"
                extension = extension.substr(0, extension.strlen() - 2);    // "test=123\r\n" -> "test=123"
                if (extension != m_gchs.extension) {
                    m_gchs.extension = extension;
                    AUDIO_LOG_INFO("extension {}", m_gchs.extension);
                }
            } else {
                hex_str = m_gchs.chunkLine.substr(0, m_gchs.chunkLine.strlen() - 2); // "AF4\r\n" -> "AF4"
            }
            m_gchs.chunkSize = hex_to_int(hex_str); // "AF4" -> 2804
            if (m_gchs.chunkSize == -1) {
                AUDIO_LOG_ERROR("Invalid char in hex_str");
                hex_str.hex_dump(20);
                goto error;
            }
        }
        if (m_gchs.chunkSize > 0) {
            chunkSize = m_gchs.chunkSize;
            break;
        } else { // die Blockgröße ist 0
            if (m_gchs.chunkLine.ends_with("\r\n\r\n")) {
                idx1 = m_gchs.chunkLine.index_of("\r\n");
                idx2 = m_gchs.chunkLine.index_of("\r\n\r");
                if (idx1 == idx2) {
                    // zum Beispiel "000\r\n\r\n", "0\r\n\r\n" oder "0;end=true\r\n\r\n"
                } else { // kann einen Trailer haben, etwa "0\r\nContent-MD5: abcdef\r\nServer: xyz\r\n\r\n"
                    m_gchs.trailer = m_gchs.chunkLine.substr(idx1 + 1, idx1 - idx2);
                    AUDIO_LOG_INFO("trailer {}", m_gchs.trailer);
                }
                chunkSize = 0;
                break;
            }
        }
    }

    AUDIO_LOG_DEBUG("chunkSize {}", chunkSize);
    if (chunkSize == -1) {
        if (m_gchs.timeStamp + 3000 < audio_millis()) {
            AUDIO_LOG_WARN("timeout while get next chunkSize");
            m_gchs.timeStamp = audio_millis();
        }
    }
    if (chunkSize >= 0) {
        m_gchs.chunkSize = -1;
        m_gchs.position = 0;
        m_gchs.chunkLine.clear();
        m_gchs.timeStamp = audio_millis();
    }
    return chunkSize;

error:
    m_gchs.reset();
    return -100;
}

/**
 * @brief Liest einen ID3v1-Tag am Dateiende und meldet seine Felder
 */
bool Audio::readID3V1Tag() {
    if (m_codec != CODEC_MP3)
        return false;
    ps_ptr<char> chBuff;
    chBuff.alloc(256, "chBuff");

    const uint8_t* p = m_inBuff.getReadPtr();

    
    auto readID3Field = [&](ps_ptr<char>& field, const uint8_t* src, size_t len, const char* label = nullptr) {
        field.alloc(len + 1, "field");
        memcpy(field.get(), src, len);
        field[len] = '\0';
        latinToUTF8(field);
        if (field.strlen() > 0 && label) {
            field.insert(label, 0);
            info(*this, evt_id3data, "{}", field.get());
        }
    };

    if (m_inBuff.bufferFilled() == 128 && startsWith((const char*)p, "TAG")) {
        ps_ptr<char> title, artist, album, year, comment;

        readID3Field(title, p + 3, 30, "Title: ");
        readID3Field(artist, p + 33, 30, "Artist: ");
        readID3Field(album, p + 63, 30, "Album: ");
        readID3Field(year, p + 93, 4, "Year: ");
        readID3Field(comment, p + 97, 30, "Comment: ");

        uint8_t zeroByte = p[125];
        uint8_t track = p[126];
        uint8_t genre8 = p[127];

        info(*this, evt_info, zeroByte ? "ID3 version: 1" : "ID3 Version 1.1");

        if (zeroByte == 0) {
            sprintf(chBuff.get(), "Track Number: %d", track);
            info(*this, evt_id3data, "{}", chBuff.get());
        }

        if (genre8 < 192) {
            sprintf(chBuff.get(), "Genre: %d", genre8);
            info(*this, evt_id3data, "{}", chBuff.get());
        }

        return true;
    }

    if (m_inBuff.bufferFilled() == 227 && startsWith((const char*)p, "TAG+")) { // "TAG+" kann als Erweiterung zusätzlich existieren und ersetzt "TAG" nicht
        info(*this, evt_info, "ID3 version: 1 - Enhanced TAG");

        ps_ptr<char> title, artist, album, genre;

        readID3Field(title, p + 4, 60, "Title: ");
        readID3Field(artist, p + 64, 60, "Artist: ");
        readID3Field(album, p + 124, 60, "Album: ");
        readID3Field(genre, p + 185, 30, "Genre: ");

        // mögliche Erweiterung: Geschwindigkeit, Start- und Endzeit
        return true;
    }

    return false;
    // [1] https://en.wikipedia.org/wiki/List_of_ID3v1_Genres
    // [2] https://en.wikipedia.org/wiki/ID3#ID3v1_and_ID3v1.1[5]
}

/**
 * @brief Baut den Eingangspuffer nach einem Positionssprung neu auf
 */
int32_t Audio::newInBuffStart(int32_t resumeFilePos) {

    if ((m_controlCounter != 100) || (m_resumeFilePos >= (int32_t)m_audioDataStart + m_audioDataSize) || ((m_codec == CODEC_M4A) && !m_stszPosition)) {
        AUDIO_LOG_WARN("timeOffset not possible (1)");
        return -1;
    }

    // resumeFilePos innerhalb der Audiodaten halten
    if (resumeFilePos < (int32_t)m_audioDataStart)
        resumeFilePos = m_audioDataStart;

    uint32_t buffFillValue = std::min<uint32_t>(m_audioDataStart + m_audioDataSize - resumeFilePos, UINT16_MAX);

    AUDIO_LOG_DEBUG("new m_inBuff start at m_resumeFilePos {}, m_audioDataStart {}, buffFillValue {}", m_resumeFilePos, m_audioDataStart, buffFillValue);

    if (buffFillValue < m_inBuff.getMaxBlockSize()) {
        AUDIO_LOG_WARN("timeOffset not possible (2)");
        return -1;
    }

    // kritischen Bereich betreten
    xSemaphoreTake(m_mutexAudioTaskIsDecoding, 1 * configTICK_RATE_HZ);
    m_fLockInBuffer = true;

    // Eingangspuffer vorbereiten
    m_fAllDataReceived = false;
    audioFileSeek(resumeFilePos);
    m_inBuff.reset();
    int32_t bw = audioFileRead(m_inBuff.getWritePtr(), buffFillValue, 3000);
    if (bw == buffFillValue)
        m_inBuff.bytesWritten(bw);
    else
        return -1;

    int32_t offset = 0;
    int32_t newFilePos = resumeFilePos;

    // ===================================================================
    // codecspezifische Behandlung
    // ===================================================================

    auto fail = [&]() {
        AUDIO_LOG_ERROR("timeOffset not possible (3)");
        m_inBuff.bytesWasRead(0);
        m_fLockInBuffer = false;
        xSemaphoreGive(m_mutexAudioTaskIsDecoding);
        stopSong();
        return -1;
    };

    switch (m_codec) {
#if AUDIO_CODEC_M4A && AUDIO_CODEC_AAC
    case CODEC_M4A:
        offset = m4aCorrectResumeFilePos();
        if (offset < 0)
            return fail();
        break;
#endif

#if AUDIO_CODEC_WAV
    case CODEC_WAV:
        // WAV arbeitet mit 4-Byte-Grenzen
        offset = wavCorrectResumeFilePos();
        if (offset < 0)
            return fail();
        break;
#endif

#if AUDIO_CODEC_MP3
    case CODEC_MP3:
        offset = mp3CorrectResumeFilePos();
        if (offset < 0)
            return fail();
        break;
#endif

#if AUDIO_CODEC_FLAC
    case CODEC_FLAC:
        offset = flacCorrectResumeFilePos();
        if (offset < 0)
            return fail();
        break;
#endif

#if AUDIO_CODEC_VORBIS
    case CODEC_VORBIS:
        offset = oggCorrectResumeFilePos(); // nächste OggS-Marke
        if (offset < 0)
            return fail();
        break;
#endif

#if AUDIO_CODEC_OPUS
    case CODEC_OPUS:
        offset = oggCorrectResumeFilePos(); // nächste OggS-Marke
        if (offset < 0)
            return fail();
        break;
#endif

    default:
        return fail();
    }

    AUDIO_LOG_DEBUG("offset {}, readSpace {}", offset, m_inBuff.readSpace());
    newFilePos += offset;
    m_decoder->clear();

    // kritischen Bereich verlassen
    m_inBuff.bytesWasRead(offset);
    m_audioDataReadPtr = (resumeFilePos + offset) - m_audioDataStart;
    m_fLockInBuffer = false;
    xSemaphoreGive(m_mutexAudioTaskIsDecoding);

    return newFilePos;
}

/**
 * @brief Überwacht den Datenfluss und erkennt langsame oder abgerissene Streams
 */
bool Audio::streamDetection(uint32_t bytesAvail) {

    if (m_inBuff.bufferFilled() < m_inBuff.getMaxBlockSize()) {
        if (m_sdet.cnt_slow == 0)
            m_sdet.tmr_slow = audio_millis();
        m_sdet.cnt_slow++;
    } else {
        m_sdet.cnt_slow = 0;
        m_sdet.cnt_lost = 0;
    }

    // Fällt der Eingangspuffer binnen einer Sekunde hundertmal unter die Rahmengröße, ist der Stream zu langsam.
    // Meldung ausgeben
    if (m_sdet.cnt_slow && m_sdet.tmr_slow + 2000 < audio_millis()) {
        m_sdet.tmr_slow = audio_millis();
        info(*this, evt_info, "slow stream");
        m_sdet.cnt_slow = 0;
        m_sdet.cnt_lost++;
    }

    if (bytesAvail) {
        m_sdet.cnt_lost = 0;
    }
    if (m_inBuff.bufferFilled() > m_inBuff.getMaxBlockSize() * 2)
        return true; // genug Daten für die Wiedergabe

    // Kommen zehn Sekunden lang keine Daten, wird die Verbindung neu aufgebaut.
    if (m_sdet.cnt_lost == 5) {
        info(*this, evt_info, "Stream lost");
        connecttohost(m_lastHost.get());
        m_sdet.cnt_slow = 0;
        m_sdet.cnt_lost = 0;
        return false;
    }
    return false;
}

/**
 * @brief Rückt die Wiederaufnahme-Position auf die nächste M4A-Sample-Grenze
 */
uint32_t Audio::m4aCorrectResumeFilePos() {
    // Für einen Sprung in einer M4A-Datei muss der exakte Anfang eines AAC-Blocks gefunden werden. M4A hat kein Synchronwort, ein ungenauer Sprung kann zum Absturz führen.

    if (!m_stszPosition)
        return m_audioDataStart;

    typedef union {
        uint8_t u8[4];
        uint32_t u32;
    } tu;
    tu uu = {};

    uint32_t i = 0, pos = m_audioDataStart;
    uint32_t filePtr = m_audioFilePosition;
    bool found = false;
    audioFileSeek(m_stszPosition, m_stszNumEntries * 4);

    auto read_next = [&]() -> int {
        int r = 0;
        int cnt = 0;
        while (true) {
            r = audioFileRead();
            if (r >= 0)
                break;
            vTaskDelay(10);
            cnt++;
            if (cnt > 300) {
                AUDIO_LOG_ERROR("timeout");
                break;
            }
        }
        return r;
    };

    while (i < m_stszNumEntries) {
        i++;
        uu.u8[3] = read_next();
        uu.u8[2] = read_next();
        uu.u8[1] = read_next();
        uu.u8[0] = read_next();

        pos += uu.u32;
        if (pos >= m_resumeFilePos) {
            found = true;
            break;
        }
    }
    if (!found)
        return -1; // nicht gefunden

    audioFileSeek(filePtr);       // Dateiposition wiederherstellen
    return pos - m_resumeFilePos; // Sprungweite in Bytes
}

/**
 * @brief Rückt die Wiederaufnahme-Position auf die nächste OGG-Seite
 */
int32_t Audio::oggCorrectResumeFilePos() {
    // Startpunkt ist die nächste OggS-Marke
    if (m_inBuff.bufferFilled() < UINT16_MAX)
        return -1;
    vTaskDelay(1);
    // // AUDIO_LOG_INFO("in_resumeFilePos {}", resumeFilePos);

    auto find_sync_word = [&](uint8_t* pos, uint32_t av) -> int {
        int steps = 0;
        while (av--) {
            if (pos[steps] == 'O') {
                if (pos[steps + 1] == 'g') {
                    if (pos[steps + 2] == 'g') {
                        if (pos[steps + 3] == 'S') { // zweiten Teil der Marke prüfen
                            return steps;            // Marke gefunden, Abstand melden
                        }
                    }
                }
            }
            steps++;
        }
        return -1; // keine OggS-Marke gefunden
    };

    uint8_t* readPtr = m_inBuff.getReadPtr();
    size_t av = m_inBuff.readSpace();
    int32_t steps = 0;

    if (av < m_inBuff.getMaxBlockSize())
        return -1;

    steps = find_sync_word(readPtr, av);
    if (steps == -1)
        return -1;
    return steps; // Abstand zum Synchronwort
}

/**
 * @brief Rückt die Wiederaufnahme-Position auf den nächsten FLAC-Rahmen
 */
int32_t Audio::flacCorrectResumeFilePos() {
    uint8_t* p = m_inBuff.getReadPtr();
    size_t av = m_inBuff.readSpace();

    if (av < 32)
        return -1; // sichergehen

    auto utf8_length = [&](uint8_t first) -> int {
        if ((first & 0x80) == 0x00)
            return 1; // 0xxxxxxx
        if ((first & 0xE0) == 0xC0)
            return 2; // 110xxxxx
        if ((first & 0xF0) == 0xE0)
            return 3; // 1110xxxx
        if ((first & 0xF8) == 0xF0)
            return 4; // 11110xxx
        if ((first & 0xFC) == 0xF8)
            return 5; // 111110xx
        if ((first & 0xFE) == 0xFC)
            return 6; // 1111110x
        if (first == 0xFE)
            return 7; // 11111110
        return -1;    // ungültiges UTF-8
    };

    // FLAC CRC-8
    auto crc8_update = [](uint8_t crc, uint8_t data) -> uint8_t {
        crc ^= data;
        for (int i = 0; i < 8; ++i) {
            if (crc & 0x80)
                crc = (crc << 1) ^ 0x07;
            else
                crc <<= 1;
        }
        return crc;
    };

    for (size_t i = 0; i + 14 < av; ++i) {
        // 14-Bit-Synchronwort: 0xFFF8 oder 0xFFF9, die letzten zwei Bits sind variabel
        uint8_t b0 = p[i + 0]; // 11111111  sync
        uint8_t b1 = p[i + 1]; // 111111rb  sync         r-reserved    b-blocking strategy
        if (b0 != 0xFF)
            continue;
        if ((b1 & 0xFE) != 0xF8)
            continue;

        uint8_t b2 = p[i + 2]; // bbbbssss: b Blockgröße, s Abtastrate
        uint8_t blocksize_code = (b2 & 0xF0) >> 4;
        uint8_t samplerate_code = (b2 & 0x0F);

        uint8_t b3 = p[i + 3]; // ccccsssr: c Kanalzuordnung, s Sample-Größe, r reserviert
        uint8_t channel_assign = (b3 & 0xF0) >> 4;
        uint8_t bps_code = (b3 & 0x0E) >> 1; // Bits je Sample
        uint8_t reserved_bit = b3 & 0x01;    // muss 0 sein

        if (reserved_bit != 0)
            continue;
        if (channel_assign >= 11)
            continue; // 11-15 reserved
        if (bps_code == 0x07)
            continue; // reserviert

        size_t pos = i + 4; // erstes Byte des UTF-8-Felds
        int len = utf8_length(p[pos]);
        if (len < 1 || pos + len >= av)
            continue; // ungültiger oder unvollständiger Rahmenkopf

        size_t crc_end = pos + len; // hier liegt das CRC-Byte

        AUDIO_LOG_DEBUG("samplerate_code {}, bps_code {} channel_assign {}, reserved_bit {}", samplerate_code, bps_code, channel_assign, reserved_bit);
        AUDIO_LOG_DEBUG("b0 {}, b1 {}, b2 {}, b3 {}", b0, b1, b2, b3);

        uint8_t crc = 0;
        for (size_t k = i; k < crc_end; ++k)
            crc = crc8_update(crc, p[k]);

        if (crc != p[crc_end])
            continue;

        // an dieser Stelle ist es mit sehr hoher Sicherheit ein echter Rahmen
        AUDIO_LOG_DEBUG(">>> REAL FLAC-FRAME found at offset {}", i);
        return (int32_t)i;
    }

    return -1; // kein Rahmen in den ersten 64 KB
}

/**
 * @brief Rückt die Wiederaufnahme-Position auf das nächste MP3-Synchronwort
 */
int32_t Audio::mp3CorrectResumeFilePos() {

    int32_t steps = 0;
    int32_t sumSteps = 0;
    uint8_t* pos = m_inBuff.getReadPtr();
    size_t av = m_inBuff.readSpace();

    if (av < m_inBuff.getMaxBlockSize())
        return -1;

    while (true) {
        steps = m_decoder->findSyncWord(pos, av);
        if (steps == 0)
            break;
        if (steps == -1)
            return -1;
        pos += steps;
        sumSteps += steps;
    }
    return sumSteps; // Position des ersten Rahmen-Bytes
}

/**
 * @brief Rückt die Wiederaufnahme-Position auf eine WAV-Blockgrenze
 */
int32_t Audio::wavCorrectResumeFilePos() {
    int32_t pos = m_resumeFilePos;
    uint8_t offset = 0;
    ;
    // WAV muss auf einer 4-Byte-Grenze liegen
    while (pos % 4 != 0) {
        offset++;
        pos++;
    }
    if (pos >= m_audioDataStart + m_audioDataSize)
        return 0;
    return offset;
}

/**
 * @brief Bestimmt den Codec aus den ersten Bytes des Datenstroms
 */
uint8_t Audio::determineCodec(uint8_t presumed_codec) {
    if (presumed_codec == CODEC_OGG) {
        // Bei Content-Type application/ogg kann der Codec OPUS, FLAC oder VORBIS sein,
        // die ersten Datenbytes entscheiden.
        uint8_t res = CODEC_NONE;
        int idx = -1;
        idx = specialIndexOf(m_inBuff.getReadPtr(), "OggS", 127);
        AUDIO_LOG_DEBUG("idx {}", idx);
        if (idx == 0) {
            idx = specialIndexOf(m_inBuff.getReadPtr(), "OpusHead", 127);
            if (idx >= 28) {
                res = CODEC_OPUS;
            }
            idx = specialIndexOf(m_inBuff.getReadPtr(), "fLaC", 127);
            if (idx >= 28) {
                res = CODEC_FLAC;
            }
            idx = specialIndexOf(m_inBuff.getReadPtr(), "vorbis", 127);
            if (idx >= 28) {
                res = CODEC_VORBIS;
            }

            if (m_streamType == ST_WEBFILE || m_dataMode == AUDIO_LOCALFILE) { // Datei?
                m_lastGranulePosition = getLastGranulePosition(res);           // nur VORBIS und OPUS
            }
            AUDIO_LOG_DEBUG("lastGranulePosition {}", m_lastGranulePosition);
        }
        return res;
    }

    if (m_playlistFormat == FORMAT_M3U8)
        return presumed_codec; // HLS oder TS, nichts zu tun
    if (m_streamType != ST_WEBSTREAM)
        return presumed_codec; // Web-Datei, nichts zu tun

    if (presumed_codec == CODEC_AAC || presumed_codec == CODEC_MP3) { // nur beim Web-Stream: AAC oder MP3?
        uint8_t* data = m_inBuff.getReadPtr();
        uint8_t b0 = data[0];
        uint8_t b1 = data[1];
        int8_t mimeType = CODEC_NONE;
        if (b0 == 0xFF && (b1 & 0xF6) == 0xF0) { // AAC ADTS
            mimeType = CODEC_AAC;
        }

        else if (b0 == 0x56 && (b1 & 0xE0) == 0xE0) { // AAC LATM
            mimeType = CODEC_AAC;
        }

        else if (b0 == 0xFF && (b1 & 0xE0) == 0xE0) { // MP3
            uint8_t layer = (b1 >> 1) & 0x03;
            if (layer != 0) { // Layer darf nicht "reserviert" sein
                mimeType = CODEC_MP3;
            }
        }

        if (presumed_codec == mimeType)
            return presumed_codec;
        if (mimeType == CODEC_NONE)
            return presumed_codec; // weder AAC noch MP3
        info(*this, evt_info, "contentType is {}, but {} found", m_codecName[presumed_codec], m_codecName[mimeType]);
        return mimeType;
    }

    return presumed_codec; // alles Übrige, etwa natives FLAC oder WAV
}

/**
 * @brief Versionskennung der Library
 */
const char* Audio::getVersion() {
    trim(audioI2SVers);
    return audioI2SVers + 8;
}

/**
 * @brief Stellt die gesammelten Ereignisse aus der Warteschlange dem Host zu
 */
bool Audio::getInfo() {
    if (m_infoQueue.e.size() == 0)
        return false;
    msg_t i = {0};
    while (m_infoQueue.e.size()) {
        ps_ptr<char> msg = m_infoQueue.msg.back();
        i.msg = msg.c_get();
        i.e = (event_t)m_infoQueue.e.back();
        ps_ptr<char> evtstr = m_infoQueue.s.back();
        i.s = evtstr.c_get();
        i.arg1 = m_infoQueue.arg1.back();
        i.arg2 = m_infoQueue.arg2.back();
        i.i2s_num = m_i2sItems.i2s_num;
        i.vec = m_infoQueue.vec.back();

        m_infoQueue.msg.pop_back();
        m_infoQueue.e.pop_back();
        m_infoQueue.s.pop_back();
        m_infoQueue.arg1.pop_back();
        m_infoQueue.arg2.pop_back();
        m_infoQueue.vec.pop_back();
        audio_info_callback(i);
    }
    return true;
}

/**
 * @brief Entfernt Leerraum an Anfang und Ende am Platz
 */
void Audio::trim(char* str) {
    char* start = str; // Originalzeiger behalten
    char* end;
    while (isspace((unsigned char)*start))
        start++; // erstes Nicht-Leerzeichen suchen

    if (*start == 0) { // nur Leerzeichen enthalten
        str[0] = '\0'; // leere Zeichenkette liefern
        return;
    }

    end = start + strlen(start) - 1; // Ende der Zeichenkette suchen

    while (end > start && isspace((unsigned char)*end))
        end--;
    end[1] = '\0'; // hinter dem letzten Nicht-Leerzeichen terminieren

    // den gekürzten Text an den Anfang schieben
    memmove(str, start, strlen(start) + 1); // +1 für die Nullterminierung
}

/**
 * @brief Beginnt base mit str?
 */
bool Audio::startsWith(const char* base, const char* str) {
    // fb
    char c;
    while ((c = *str++) != '\0')
        if (c != *base++)
            return false;
    return true;
}

/**
 * @brief Sucht eine Teilzeichenkette ab startIndex
 */
int Audio::indexOf(const char* base, const char* str, int startIndex) {
   
    const char* p = base;
    for (; startIndex > 0; startIndex--)
        if (*p++ == '\0')
            return -1;
    char* pos = strstr(p, str);
    if (pos == nullptr)
        return -1;
    return pos - base;
}

/**
 * @brief Sucht ein Zeichen ab startIndex
 */
int Audio::indexOf(const char* base, char ch, int startIndex) {
    // fb
    const char* p = base;
    for (; startIndex > 0; startIndex--)
        if (*p++ == '\0')
            return -1;
    char* pos = strchr(p, ch);
    if (pos == nullptr)
        return -1;
    return pos - base;
}

/**
 * @brief Sucht eine Zeichenfolge in Binärdaten, mit exact nur am Anfang
 */
int Audio::specialIndexOf(uint8_t* base, const char* str, int baselen, bool exact) {
    int result = 0;
    if (strlen(str) > baselen)
        return -1; // mit exact muss der Treffer am Anfang liegen
    for (int i = 0; i < baselen - strlen(str); i++) {
        result = i;
        for (int j = 0; j < strlen(str) + exact; j++) {
            if (*(base + i + j) != *(str + j)) {
                result = -1;
                break;
            }
        }
        if (result >= 0)
            break;
    }
    return result;
}

/**
 * @brief Kleinster von drei Werten
 */
int32_t Audio::min3(int32_t a, int32_t b, int32_t c) {
    uint32_t min_val = a;
    if (b < min_val)
        min_val = b;
    if (c < min_val)
        min_val = c;
    return min_val;
}

/**
 * @brief Liest numBytes Bytes in Big-Endian-Reihenfolge als Zahl
 */
uint64_t Audio::bigEndian(uint8_t* base, uint8_t numBytes, uint8_t shiftLeft) {
    uint64_t result = 0;
    if (numBytes < 1 || numBytes > 8)
        return 0;
    for (int i = 0; i < numBytes; i++) {
        result |= (uint64_t)(*(base + i)) << ((numBytes - i - 1) * shiftLeft);
    }
    if (result > SIZE_MAX) {
        AUDIO_LOG_ERROR("range overflow");
        return 0;
    }
    return result;
}

/**
 * @brief Kodiert die Eingabe nach Base64
 */
bool Audio::b64encode(const char* source, uint16_t sourceLength, char* dest) {
    // Eigene Umsetzung. Im Original kam sie aus Arduinos libb64.
    // Der Aufrufer stellt base64_encode_expected_len(len) + 1 Bytes bereit.
    static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    if (!source || !dest)
        return false;

    size_t out = 0;
    size_t i = 0;
    while (i + 2 < sourceLength) {
        const uint32_t v = (static_cast<uint8_t>(source[i]) << 16) | (static_cast<uint8_t>(source[i + 1]) << 8) | static_cast<uint8_t>(source[i + 2]);
        dest[out++] = tbl[(v >> 18) & 0x3F];
        dest[out++] = tbl[(v >> 12) & 0x3F];
        dest[out++] = tbl[(v >> 6) & 0x3F];
        dest[out++] = tbl[v & 0x3F];
        i += 3;
    }

    const size_t rest = sourceLength - i;
    if (rest == 1) {
        const uint32_t v = static_cast<uint32_t>(static_cast<uint8_t>(source[i])) << 16;
        dest[out++] = tbl[(v >> 18) & 0x3F];
        dest[out++] = tbl[(v >> 12) & 0x3F];
        dest[out++] = '=';
        dest[out++] = '=';
    }
    else if (rest == 2) {
        const uint32_t v = (static_cast<uint32_t>(static_cast<uint8_t>(source[i])) << 16) | (static_cast<uint32_t>(static_cast<uint8_t>(source[i + 1])) << 8);
        dest[out++] = tbl[(v >> 18) & 0x3F];
        dest[out++] = tbl[(v >> 12) & 0x3F];
        dest[out++] = tbl[(v >> 6) & 0x3F];
        dest[out++] = '=';
    }

    dest[out] = '\0';
    return true;
}

/**
 * @brief Leert den Vektor und gibt seinen Speicher zurück
 */
void Audio::vectorClearAndShrink(std::vector<ps_ptr<char>>& vec) {
    for (int i = 0; i < vec.size(); i++)
        vec[i].reset();
    vec.clear();         // die Freigabe übernimmt der unique_ptr
    vec.shrink_to_fit(); // Speicher zurückgeben
}

/**
 * @brief Leert die Deque und gibt ihren Speicher zurück
 */
void Audio::dequeClearAndShrink(std::deque<ps_ptr<char>>& deq) {
    for (int i = 0; i < deq.size(); i++)
        deq[i].reset();
    deq.clear();         // die Freigabe übernimmt der unique_ptr
    deq.shrink_to_fit(); // Speicher zurückgeben
}

/**
 * @brief Einfacher Streuwert über eine Zeichenkette
 */
uint32_t Audio::simpleHash(const char* str) {
    if (str == NULL)
        return 0;
    uint32_t hash = 0;
    for (int i = 0; i < strlen(str); i++) {
        if (str[i] < 32)
            continue; // Steuerzeichen ignorieren
        hash += (str[i] - 31) * i * 32;
    }
    return hash;
}

/**
 * @brief Kodiert eine URL, mit spacesOnly nur die Leerzeichen
 */
ps_ptr<char> Audio::urlencode(const char* str, bool spacesOnly) {
    if (!str) {
        return {};
    } // Eingabe ist leer

    // Platz für das Ergebnis reservieren, im schlimmsten Fall dreifache Länge
    size_t inputLength = strlen(str);
    size_t bufferSize = inputLength * 3 + 1; // schlimmster Fall
    ps_ptr<char> encoded;
    encoded.alloc(bufferSize, "encoded");
    if (!encoded.valid()) {
        return {};
    } // Allokation fehlgeschlagen

    const char* p_input = str;
    char* p_encoded = encoded.get();    // Zeiger in den Ausgabepuffer
    size_t remainingSpace = bufferSize; // freier Platz im Ausgabepuffer

    while (*p_input) {
        if (isalnum((unsigned char)*p_input)) {
            // alphanumerische Zeichen unverändert übernehmen
            if (remainingSpace > 1) {
                *p_encoded++ = *p_input;
                remainingSpace--;
            } else {
                return {}; // Platzprüfung fehlgeschlagen
            }
        } else if (spacesOnly && *p_input != 0x20) {
            // Nur Leerzeichen nicht kodieren
            if (remainingSpace > 1) {
                *p_encoded++ = *p_input;
                remainingSpace--;
            } else {
                return {}; // Platzprüfung fehlgeschlagen
            }
        } else {
            // unsichere Zeichen als '%XX' kodieren
            if (remainingSpace > 3) {
                int written = snprintf(p_encoded, remainingSpace, "%%%02X", (unsigned char)*p_input);
                if (written < 0 || written >= (int)remainingSpace) {
                    return {}; // Fehler beim Schreiben in den Puffer
                }
                p_encoded += written;
                remainingSpace -= written;
            } else {
                return {}; // Platzprüfung fehlgeschlagen
            }
        }
        p_input++;
    }

    // nullterminieren
    if (remainingSpace > 0) {
        *p_encoded = '\0';
    } else {
        return {}; // Platzprüfung fehlgeschlagen
    }
    encoded.shrink_to_fit();
    return encoded;
}
// Eigener Task für Dekodierung und Ausgabe: playAudioData() läuft periodisch und holt die Daten aus dem Eingangspuffer. So bleibt der I2S-DMA auch dann gefüllt, wenn die loop() des Hosts hängt.


/**
 * @brief Legt den Kern des Audio-Tasks fest, sinnvollerweise nicht der Kern der Anwendung
 */
void Audio::setAudioTaskCore(uint8_t coreID) {
    if (coreID > 1)
        return;
    stopAudioTask();
    xSemaphoreTake(m_mutexAudioTask, 0.3 * configTICK_RATE_HZ);
    m_audioTaskCoreId = coreID;
    xSemaphoreGive(m_mutexAudioTask);
    startAudioTask();
}

/**
 * @brief Startet den Audio-Task für Dekodierung und Ausgabe
 */
void Audio::startAudioTask() {

    if (m_fAudioTaskIsRunning) {
        AUDIO_LOG_DEBUG("audio task is already running.");
        return;
    }
    AUDIO_LOG_INFO("start audio task.");
    m_fAudioTaskIsRunning = true;

    m_audioTaskHandle = xTaskCreateStaticPinnedToCore(&Audio::audioTaskWrapper, // Task-Funktion
                                                      "PeriodicTask",           // Task-Name
                                                      AUDIO_STACK_SIZE,         // Stackgröße in Worten
                                                      this,                     // Task-Parameter
                                                      2,                        // Priorität
                                                      xAudioStack,              // Task-Stack
                                                      &xAudioTaskBuffer,        // Speicher des Task-Kontrollblocks
                                                      m_audioTaskCoreId         // Kern, auf dem der Task läuft
    );
}

/**
 * @brief Beendet den Audio-Task
 */
void Audio::stopAudioTask() {
    if (!m_fAudioTaskIsRunning) {
        AUDIO_LOG_DEBUG("audio task is not running.");
        return;
    }
    AUDIO_LOG_INFO("stop audio task.");
    xSemaphoreTake(m_mutexAudioTask, 0.3 * configTICK_RATE_HZ);
    m_fAudioTaskIsRunning = false;
    if (m_audioTaskHandle != nullptr) {
        vTaskDelete(m_audioTaskHandle);
        m_audioTaskHandle = nullptr;
    }
    xSemaphoreGive(m_mutexAudioTask);
}

/**
 * @brief Statischer Einsprungpunkt des Audio-Tasks
 */
void Audio::audioTaskWrapper(void* param) {
    Audio* audioRunner = static_cast<Audio*>(param);
    audioRunner->audioTask();
}

/**
 * @brief Task-Schleife, wartet auf Arbeit und ruft performAudioTask()
 */
void Audio::audioTask() {
    while (m_fAudioTaskIsRunning) {
        vTaskDelay(1 / portTICK_PERIOD_MS); // periodically every x ms
        if (m_fI2sInit)
            performAudioTask();
    }
    vTaskDelete(nullptr); // diesen Task beenden
}

/**
 * @brief Ein Arbeitsschritt des Audio-Tasks: dekodieren und ausgeben
 */
void Audio::performAudioTask() {
    if (m_decoder) {
        xSemaphoreTake(m_mutexAudioTask, 0.3 * configTICK_RATE_HZ);
        playAudioData();
        xSemaphoreGive(m_mutexAudioTask);
        gainRamp();
        return;
    } else {
        if (m_validSamples)
            playChunk(); // empty I2S DMA
#if AUDIO_FEATURE_VU || AUDIO_FEATURE_SPECTRUM
        int32_t c[2] = {0};
        calculateVUlevel(c);
#endif
        gainRamp();
        vTaskDelay(20);
        return;
    }
}

/**
 * @brief Freier Stack des Audio-Tasks in Worten
 */
uint32_t Audio::getHighWatermark() {
    UBaseType_t highWaterMark = uxTaskGetStackHighWaterMark(m_audioTaskHandle);
    return highWaterMark; // Doppelworte
}
