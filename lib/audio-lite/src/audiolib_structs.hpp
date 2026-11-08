#ifndef AUDIO_LITE_AUDIOLIB_STRUCTS_H
#define AUDIO_LITE_AUDIOLIB_STRUCTS_H
#include "psram_unique_ptr.hpp"
#include <cstdint>
#include <deque>
#include <stddef.h>

/**
 * @file audiolib_structs.hpp
 * @brief Zustandsblöcke und Datenstrukturen der Audio-Fassade
 *
 * Die meisten Structs bündeln den Arbeitszustand je einer Funktion in Audio.cpp und liegen dort als Member der Klasse Audio. Der Kommentar am Struct nennt diese Funktion.
 */

namespace audiolib {

/**
 * @brief SYLT-Rahmen (synchronisierter Liedtext) aus dem ID3-Tag
 */
struct sylt_t {
    size_t size;
    uint32_t pos;
    char lang[5];
    uint8_t text_encoding;
    uint8_t time_stamp_format;
    uint8_t content_type;
};

/**
 * @brief Zustandsblock von read_ID3_Header()
 */
struct ID3Hdr_t {
    size_t retvalue = {};
    size_t headerSize = {};
    size_t tagSize = {};
    size_t cnt = {};
    size_t id3Size = {};
    size_t totalId3Size = {}; ///< Summe bei mehreren ID3-Headern (id3_1_size + id3_2_size + ...)
    size_t remainingHeaderBytes = {};
    size_t v22_tag_length = {};
    uint8_t ID3version = {};
    uint8_t ID3revision = {};
    uint8_t flags = {};
    bool unsync = {};
    bool extended_header = {};
    bool experimental_indicator = {};
    bool footer_present = {};
    size_t offset = {};
    size_t currentPosition = {};
    int ehsz = {};
    char tag[5] = {};
    char frameid[5] = {};
    char lang[5] = {};
    size_t framesize = {};
    bool compressed = {};
    std::vector<uint32_t> APIC_vec = {};
    sylt_t SYLT = {};
    uint8_t numID3Header = {};
    uint16_t iBuffSize = {};
    uint8_t contentDescriptorTerminator_0 = {};
    uint8_t contentDescriptorTerminator_1 = {};
    uint8_t textStringTerminator_0 = {};
    uint8_t textStringTerminator_1 = {};
    bool byteOrderMark = {};
    ps_ptr<char> iBuff;

    void reset() {
        *this = ID3Hdr_t{};
    }
};

/**
 * @brief Zustandsblock von processWebStreamHLS()
 */
struct pwsHLS_t {
    uint16_t maxFrameSize;
    uint16_t ID3BuffSize;
    uint32_t availableBytes;
    bool firstBytes;
    bool f_chunkFinished;
    uint32_t byteCounter;
    int32_t chunkSize;
    uint16_t ID3WritePtr;
    uint16_t ID3ReadPtr;
    ps_ptr<uint8_t> ID3Buff;
};

/**
 * @brief Zustandsblock von parsePlaylist_M3U8()
 */
struct pplM3u8_t {
    uint64_t xMedSeq;
    bool f_mediaSeq_found;
    bool firstCall;
};

/**
 * @brief Zustandsblock von read_M4A_Header()
 */
struct m4aHdr_t {
    size_t headerSize;
    size_t retvalue;
    size_t atomsize;
    size_t sizeof_ftyp;
    size_t sizeof_moov;
    size_t sizeof_free;
    size_t sizeof_mdat;
    size_t sizeof_trak;
    size_t sizeof_ilst;
    size_t sizeof_esds;
    size_t sizeof_mdia;
    size_t sizeof_minf;
    size_t sizeof_mdhd;
    size_t sizeof_stbl;
    size_t sizeof_stsd;
    size_t sizeof_stsz;
    size_t sizeof_mp4a;
    size_t sizeof_udta;
    size_t sizeof_meta;
    size_t sizeof_chpl;
    size_t audioDataPos;
    size_t cnt;
    size_t offset;
    uint32_t mdat_startPos;
    uint32_t picPos;
    uint32_t picLen;
    uint32_t ilst_pos;
    uint8_t channel_count;
    uint8_t sample_size;         ///< Bits je Sample
    uint8_t objectTypeIndicator; ///< aus der esds-Box
    uint8_t streamType;          ///< aus der esds-Box
    uint32_t bufferSizeDB;       ///< aus der esds-Box
    uint32_t maxBitrate;         ///< aus der esds-Box
    uint32_t nomBitrate;         ///< aus der esds-Box
    uint32_t timescale;          ///< aus der mdhd-Box
    uint32_t duration;           ///< aus der mdhd-Box
    uint16_t sample_rate;
    uint8_t aac_profile;
    uint32_t stsz_num_entries;
    uint32_t stsz_table_pos;
    uint32_t ilst_already_consumed;
    bool progressive; ///< moov liegt vor mdat
    bool version_flags;
    bool mdat_seen;
};

/**
 * @brief Zustandsblock von playChunk()
 */
struct plCh_t {
    uint32_t count = 0;
    size_t i2s_bytesConsumed;
    uint16_t samples;
    int16_t* sample[2];
    esp_err_t err;
};

/**
 * @brief Zustandsblock von loop()
 */
struct lVar_t {
    uint8_t no_host_cnt;
    uint32_t no_host_timer;
    uint8_t count;
};

/**
 * @brief Ergebnis von dismantle_host()
 */
struct hwoe_t {
    bool ssl;
    ps_ptr<char> hwoe;     ///< Host ohne Pfadanteil
    ps_ptr<char> rqh_host; ///< Host, wie er im Request-Header steht
    uint16_t port;
    ps_ptr<char> extension;
    ps_ptr<char> query_string;
};

/**
 * @brief Zustandsblock von processLocalFile()
 */
struct prlf_t {
    uint32_t ctime;
    int32_t newFilePos;
    bool audioHeaderFound;
    uint32_t timeout;
    uint32_t maxFrameSize;
    uint32_t availableBytes;
    int32_t bytesAddedToBuffer;
};

/**
 * @brief Zustandsblock von calculateAudioTime()
 */
typedef struct _cat {
    uint64_t sumBytesIn{};
    uint64_t sum_samples{};
    uint32_t counter{};
    uint32_t timeStamp{};
    uint32_t deltaBytesIn{};
    uint32_t nominalBitRate{};
    uint32_t tota_samples{};
    uint32_t avrBitRate{};
    uint16_t syltIdx{};
    uint32_t avrBitrateStable{};
    uint32_t oldAvrBitrate{};
    uint32_t brCounter{};
    bool firstCall{};

    void reset() {
        *this = _cat{};
    }
} cat_t;

/**
 * @brief Kopfdaten des Xing- oder Info-Rahmens einer MP3-Datei
 */
typedef struct _xing {
    bool hasToc{};       ///< Sprungtabelle vorhanden und gültig
    uint8_t toc[100]{};  ///< Sprungtabelle: Byteanteil (0..255) je Prozent der Spielzeit
    uint32_t tocBase{};  ///< Dateiposition des Kennungsrahmens, Bezugspunkt der Tabelle
    uint32_t tocBytes{}; ///< Länge des Audiostreams in Bytes laut Kennungsrahmen

    void reset() {
        *this = _xing{};
    }
} xing_t;

/**
 * @brief Zustandsblock der IIR-Filterketten (IIR_filterChain0/1/2)
 */
struct ifCh_t {
    // 16-Bit-Pfad
    float inSample0_16[2];
    float outSample0_16[2];
    int16_t iir_out0_16[2];
    float inSample1_16[2];
    float outSample1_16[2];
    int16_t iir_out1_16[2];
    float inSample2_16[2];
    float outSample2_16[2];
    int16_t iir_out2_16[2];
    // 32-Bit-Pfad
    float inSample0_32[2];
    float outSample0_32[2];
    int32_t iir_out0_32[2];
    float inSample1_32[2];
    float outSample1_32[2];
    int32_t iir_out1_32[2];
    float inSample2_32[2];
    float outSample2_32[2];
    int32_t iir_out2_32[2];
};

/**
 * @brief Zustandsblock von ts_parsePacket()
 */
typedef struct _tspp {
    int pidNumber{};
    int pids[4]{}; ///< Länge muss PID_ARRAY_LEN in Audio.h entsprechen
    int PES_DataLength{};
    int pidOfAAC{};
    uint8_t fillData{};

    void reset() {
        *this = _tspp{};
    }
} tspp_t;

/**
 * @brief Zustandsblock von processWebStream()
 */
struct pwst_t {
    uint16_t maxFrameSize;
    uint32_t chunkSize = 0;
    bool f_skipCRLF = false;
    uint32_t availableBytes;
    bool f_clientIsConnected;
    uint32_t writeSpace = 0;
    uint16_t readedBytes;
};

/**
 * @brief Zustandsblock von getChunkSize()
 */
struct gchs_t {
    int32_t chunkSize = -1;
    uint32_t timeStamp = {};
    ps_ptr<char> chunkLine = {};
    ps_ptr<char> extension = {};
    ps_ptr<char> trailer = {};
    uint16_t position = 0;

    void reset() {
        *this = gchs_t{};
    }
};

/**
 * @brief Zustandsblock von processWebFile()
 */
struct pwf_t {
    uint32_t maxFrameSize;
    int32_t newFilePos;
    bool audioHeaderFound;
    uint32_t chunkSize;
    size_t audioDataCount;
    uint32_t byteCounter;
    uint32_t nextChunkCount;
    bool f_waitingForPayload = false;
    bool f_clientIsConnected;
    uint32_t ctime;
    uint32_t timeout;
    uint32_t availableBytes;
    int32_t bytesAddedToBuffer;
};

/**
 * @brief Zustandsblock von playAudioData()
 */
struct pad_t {
    uint8_t count = 0;
    size_t oldAudioDataSize = 0;
    bool lastFrames = false;
    int32_t bytesToDecode;
    int32_t bytesDecoded;
};

/**
 * @brief Zustandsblock von sendBytes()
 */
struct sbyt_t {
    int32_t bytesLeft;
    bool f_setDecodeParamsOnce = true;
    uint8_t channels = 0;
    int nextSync = 0;
    uint8_t isPS = 0;
    const char* opus_mode = nullptr;
};

/**
 * @brief Zustandsblock von readMetadata()
 */
struct rmet_t {
    uint32_t pos_ml = 0; ///< aktuelle Position in der Metazeile
    uint32_t metaDataSize = 0;
    uint16_t res = 0;
};

/**
 * @brief Zustandsblock von processWebStreamTS()
 */
struct pwsts_t {
    uint32_t availableBytes; ///< verfügbare Bytes im Stream
    bool f_firstPacket;
    bool f_chunkFinished;
    bool f_nextRound;
    uint32_t byteCounter; ///< Zähler der empfangenen Bytes
    uint8_t ts_packetStart = 0;
    uint8_t ts_packetLength = 0;
    uint8_t ts_packetPtr = 0;
    const uint8_t ts_packetsize = 188;
    ps_ptr<uint8_t> ts_packet;
    size_t chunkSize = 0;
};

/**
 * @brief Zustandsblock von read_WAV_Header()
 */
struct rwh_t {
    size_t headerSize;
    uint32_t cs = 0;
    uint8_t bts = 0;
};

/**
 * @brief Zustandsblock von read_FLAC_Header()
 */
typedef struct _rflh {
    std::vector<uint32_t> picVec{};
    size_t headerSize{};
    size_t retvalue{};
    bool f_lastMetaBlock{};
    uint32_t picPos{};
    uint32_t picLen{};
    uint32_t duration{};
    uint32_t nominalBitrate{};
    uint8_t numChannels{};
    uint8_t bitsPerSample{};
    uint32_t sampleRate{};
    uint32_t maxFrameSize{};
    uint32_t maxBlockSize{};
    uint32_t totalSamplesInStream{};

    void reset() {
        *this = _rflh{};
    }
} rflh_t;

/**
 * @brief Zustandsblock von parseHttpResponseHeader()
 */
typedef struct _phreh {
    uint32_t ctime{};
    uint32_t timeout{};
    uint32_t stime{};
    uint32_t bitrate{};
    bool f_time{};
    bool f_icy_data{};

    void reset() {
        *this = _phreh{};
    }
} phreh_t;

/**
 * @brief Zustandsblock von parseHttpRangeHeader()
 */
struct phrah_t {
    uint32_t ctime;
    uint32_t timeout;
    uint32_t stime;
    bool f_time = false;
};

/**
 * @brief Zustandsblock von streamDetection()
 */
struct sdet_t {
    uint32_t tmr_slow = 0;
    uint32_t tmr_lost = 0;
    uint32_t cnt_slow = 0;
    uint8_t cnt_lost = 0;
};

/**
 * @brief Zustandsblock von findNextSync()
 */
struct fnsy_t {
    int nextSync = 0;
    uint32_t swnf = 0;
};

/**
 * @brief Lautstärke- und Klangregelungszustand
 */
struct audioItems_t {
    float gain_ls_db = 0.0;            ///< Tiefen in dB
    float gain_peq_db = 0.0;           ///< Mitten in dB
    float gain_hs_db = 0.0;            ///< Höhen in dB
    float pre_gain = 1.0;              ///< Pegelreserve vor den Filtern
    float eq_max_gain = 1.0;           ///< Größte eingestellte Anhebung als Faktor
    uint32_t eq_sample_rate = 0;       ///< Abtastrate der aktuellen Koeffizienten
    uint16_t eq_frequency_hz[3] = {0}; ///< Frequenzen der aktuellen Koeffizienten
    float eq_gain_db[3] = {0};         ///< Verstärkungen der aktuellen Koeffizienten
    float eq_quality = 0.0;            ///< Güte der aktuellen Koeffizienten
    float coeffs[3][5] = {0};
    float state_biquad[3][4] = {0};
    uint8_t volume = 0;
    uint8_t volume_steps = 21;
    float cur_volume = 0.0f;
    float limiter[2] = {0};
    float balance = 0.0f; ///< -16.0 dB links ... 0 ... -16.0 dB rechts
    bool mute = false;
};

/**
 * @brief I2S-Konfiguration
 */
struct i2s_items_t {
    int32_t i2s_num = 0;
    uint32_t sampleRate = 48000;
    bool commFMT = false;
};

/**
 * @brief Zustand der VU-Pegelmessung
 */
struct vu_items_t {
    ps_ptr<int32_t> delay_l;
    ps_ptr<int32_t> delay_r;
    uint16_t delay_line_index = 0;
    float left = 0;  ///< Mittelwert der Samples, linker Kanal
    float right = 0; ///< Mittelwert der Samples, rechter Kanal
    uint8_t left_peak = 0;
    uint8_t right_peak = 0;
    uint16_t left_hold = 0;
    uint16_t right_hold = 0;
};

#define FFT_BANDS 6
#define FFT_SIZE 256

/**
 * @brief Zustand der Spektrumberechnung
 */
struct fft_items_t {
    const uint16_t SIZE = FFT_SIZE;
    const uint16_t BANDS = FFT_BANDS;
    ps_ptr<float> buffer;               ///< FFT-Eingang (reell)
    ps_ptr<float> window;               ///< FFT-Fensterfunktion
    uint16_t buffer_index = 0;
    uint16_t pos = 0;
    bool initialized = false;           ///< FFT ist initialisiert
    float spec_smooth[FFT_BANDS] = {0}; ///< Glättung der Bandpegel
    uint32_t last_ms = 0;               ///< Zeitsteuerung (10 Hz)
    float gain = 1.0f;                  ///< automatische Verstärkung in process()
    bool lr_switch = false;
    ps_ptr<float> work;                 ///< FFT-Arbeitspuffer (komplex verschränkt)
    uint8_t spectrum[FFT_BANDS] = {0};  ///< Ergebnis je Band
};

/**
 * @brief Verzögerungszustand eines Biquad-Filters
 */
struct Biquad {
    int64_t z1 = 0;
    int64_t z2 = 0;
};

/**
 * @brief Festkomma-Koeffizienten eines Biquad-Filters
 */
struct BiquadCoeffs {
    int64_t b0;
    int64_t b1;
    int64_t b2;
    int64_t a1;
    int64_t a2;
};

/**
 * @brief Zustand des Resamplers
 */
struct resampler_t {
    static constexpr size_t MAX_IN_FRAMES = 4608;
    static constexpr size_t MAX_OUT_FRAMES = 10500;
    uint64_t phase = 0;
    uint64_t phaseStep = 0;
    Biquad lpLeft;
    Biquad lpRight;
    uint32_t outFrames = 0;
    BiquadCoeffs g_lpCoeffs;
    // Übergang für stetige Interpolation zwischen zwei Blöcken
    int32_t lastL = 0;    ///< letztes linkes Sample des vorigen Blocks
    int32_t lastR = 0;    ///< letztes rechtes Sample des vorigen Blocks
    bool hasLast = false; ///< false beim ersten Block, dort gibt es noch kein voriges Sample
};

/**
 * @brief Warteschlange für Ereignismeldungen an den Host
 */
struct info_queue_t {
    std::deque<ps_ptr<char>> msg = {};
    std::deque<ps_ptr<char>> s = {};
    std::deque<uint8_t> e = {}; ///< Ereignistyp
    std::deque<int32_t> arg1 = {};
    std::deque<int32_t> arg2 = {};
    std::deque<std::vector<uint32_t>> vec = {}; ///< APIC-Bildpositionen [pos, len, pos, len, ...]

    void reset() {
        *this = info_queue_t{};
    }
};

/**
 * @brief ICY-Metadaten des laufenden Streams
 */
struct icy_items_t {
    ps_ptr<char> icy_genre = {};
    ps_ptr<char> icy_logo = {};
    ps_ptr<char> icy_name = {};
    ps_ptr<char> icy_description = {};
    ps_ptr<char> icy_url = {};
    ps_ptr<char> icy_metaint = {};
    ps_ptr<char> icy_br = {};

    void reset() {
        *this = icy_items_t{};
    }
};

} // namespace audiolib

#endif // AUDIO_LITE_AUDIOLIB_STRUCTS_H
