#include "AudioManager.h"

#include "Logger.h"      // für das Loggen
#include "StringUtils.h" // für sichere String-Operationen

namespace
{
    // Audio-Sample-Verarbeitung
    constexpr uint16_t MAX_SAMPLE_BUFFER_SIZE = 2048;
    constexpr int16_t MONO_TOLERANCE_THRESHOLD = 4;
    constexpr int32_t INT16_MIN_AS_INT32 = -32768;
    constexpr int32_t INT16_MAX_AS_INT32 = 32767;
    // 16 KiB I2S-DMA statt 64 KiB: Der SDIO-WLAN-Treiber braucht denselben internen DMA-Speicher.
    constexpr uint16_t I2sDmaDescriptors = 8;
    constexpr uint16_t I2sDmaFrames = 256;

    // String-Validierung
    constexpr size_t MIN_STRING_LENGTH_SUSPICIOUS = 2;
    constexpr unsigned char ASCII_MAX = 0x7F;
    constexpr unsigned char PRINTABLE_MIN = 0x20;
    constexpr char UPPERCASE_TO_LOWERCASE_OFFSET = 32; // 'A' -> 'a'

    // UTF-16 BOM Erkennung
    constexpr unsigned char UTF16_BOM_BYTE1 = 0xFF;
    constexpr unsigned char UTF16_BOM_BYTE2 = 0xFE;

    // UTF-8 kodierte UTF-16 BOM
    constexpr unsigned char UTF8_ENCODED_BOM_BYTE1 = 0xC3;
    constexpr unsigned char UTF8_ENCODED_BOM_BYTE2_1 = 0xBF;
    constexpr unsigned char UTF8_ENCODED_BOM_BYTE2_2 = 0xBE;

    /**
     * @brief Konvertiert ein Q31-Sample mit Sättigung in einen 16-Bit-Wert.
     */
    int16_t q31ToInt16(int32_t sample)
    {
        int32_t value = sample >> 16;
        if (value < INT16_MIN_AS_INT32)
            value = INT16_MIN_AS_INT32;
        if (value > INT16_MAX_AS_INT32)
            value = INT16_MAX_AS_INT32;
        return static_cast<int16_t>(value);
    }

    /**
     * @brief Ordnet den Decodernamen aus audio-lite (whoIsIt) einem Format zu.
     * @param name Anfang der Meldung, der Name endet nach length Zeichen.
     */
    AudioManager::Codec codecFromName(const char *name, size_t length)
    {
        constexpr struct
        {
            const char *name;
            AudioManager::Codec codec;
        } Names[] = {{"MP3", AudioManager::Codec::Mp3}, {"AAC", AudioManager::Codec::Aac}, {"FLAC", AudioManager::Codec::Flac}, {"VORBIS", AudioManager::Codec::Vorbis}, {"WAV", AudioManager::Codec::Wav}};

        for (const auto &entry : Names)
            if (strlen(entry.name) == length && !strncmp(name, entry.name, length))
                return entry.codec;
        return AudioManager::Codec::None;
    }
}

AudioManager g_audioManager;

AudioManager::AudioManager()
    : m_rmsLeft(0.0f),
      m_rmsRight(0.0f),
      m_peakLeft(0.0f),
      m_peakRight(0.0f),
      m_monoSignal(false),
      m_numValidSamples(0)
{
    memset(m_sampleBufferLeft, 0, sizeof(m_sampleBufferLeft));
    memset(m_sampleBufferRight, 0, sizeof(m_sampleBufferRight));
}

void AudioManager::init(Audio &audio)
{
    m_audio = &audio;

    Audio::audio_info_callback = [this](Audio::msg_t msg)
    {
        this->handleAudioCallback(msg);
    };
}

inline void AudioManager::enqueueEvent_(AudioManagerEventType type, const char *text)
{
    if (!text)
        return;

    portENTER_CRITICAL(&m_evtMux);

    const uint8_t head = m_qHead.load(std::memory_order_relaxed);

    if (!qIsFull_())
    {
        m_evtQ[head].requestId = m_requestId.load();
        m_evtQ[head].type = type;
        stringutils::safeCopyUtf8(m_evtQ[head].text, text);
        m_qHead.store((head + 1) % EVENT_Q_CAP, std::memory_order_relaxed);
    }
    else
    {
        // Queue voll - ältestes Event überschreiben
        m_evtQ[head].requestId = m_requestId.load();
        m_evtQ[head].type = type;
        stringutils::safeCopyUtf8(m_evtQ[head].text, text);
        m_qHead.store((head + 1) % EVENT_Q_CAP, std::memory_order_relaxed);
        m_qTail.store((m_qTail.load(std::memory_order_relaxed) + 1) % EVENT_Q_CAP, std::memory_order_relaxed);
    }

    portEXIT_CRITICAL(&m_evtMux);
}

bool AudioManager::dequeueEvent(AudioManagerEventType &type, char *out, size_t len, uint32_t *requestId)
{
    portENTER_CRITICAL(&m_evtMux);

    const bool empty = qIsEmpty_();
    if (!empty)
    {
        const uint8_t tail = m_qTail.load(std::memory_order_relaxed);
        const auto &ev = m_evtQ[tail];
        type = ev.type;
        if (requestId)
            *requestId = ev.requestId;

        if (out && len)
            stringutils::safeCopy(out, len, ev.text);

        m_qTail.store((tail + 1) % EVENT_Q_CAP, std::memory_order_relaxed);
    }

    portEXIT_CRITICAL(&m_evtMux);
    return !empty;
}

bool AudioManager::startsWithIgnoreCase(const char *str, const char *prefix) const
{
    if (!str || !prefix)
        return false;

    while (*prefix)
    {
        char c1 = *str;
        char c2 = *prefix;

        if (c1 >= 'A' && c1 <= 'Z')
            c1 += UPPERCASE_TO_LOWERCASE_OFFSET;

        if (c2 >= 'A' && c2 <= 'Z')
            c2 += UPPERCASE_TO_LOWERCASE_OFFSET;

        if (c1 != c2)
            return false;

        str++;
        prefix++;
    }

    return true;
}

const char *AudioManager::findTagValue(const char *str, const char **patterns, int patternCount) const
{
    if (!str)
        return nullptr;

    for (int i = 0; i < patternCount; i++)
        if (startsWithIgnoreCase(str, patterns[i]))
            return str + strlen(patterns[i]);

    return nullptr;
}

bool AudioManager::isValidMetadataString(const char *str) const
{
    if (!str || str[0] == '\0')
        return false;

    size_t len = strlen(str);

    // Zu kurz -> ist verdächtig
    if (len <= MIN_STRING_LENGTH_SUSPICIOUS)
    {
        // Prüfen ob alle Zeichen > 0x7F (High-Bytes = wahrscheinlich verstümmelt)
        bool allHighBytes = true;
        for (size_t i = 0; i < len; i++)
        {
            if ((unsigned char)str[i] <= ASCII_MAX)
            {
                allHighBytes = false;
                break;
            }
        }

        if (allHighBytes)
            return false; // Verstümmelt
    }

    // Prüfen auf bekannte UTF-16-BOM-Zeichen (als UTF-8 kodiert oder roh)
    if (len >= 2)
    {
        unsigned char c0 = (unsigned char)str[0];
        unsigned char c1 = (unsigned char)str[1];

        // Rohe UTF-16 LE BOM (0xFF 0xFE)
        if (c0 == UTF16_BOM_BYTE1 && c1 == UTF16_BOM_BYTE2)
            return false;

        // UTF-8-kodierte BOM (0xC3 0xBF oder 0xC3 0xBE)
        if (c0 == UTF8_ENCODED_BOM_BYTE1 && (c1 == UTF8_ENCODED_BOM_BYTE2_1 || c1 == UTF8_ENCODED_BOM_BYTE2_2))
        {
            // Wenn direkt danach noch 0xC3 0xBx kommt, ist es UTF-16-Müll
            if (len >= 4)
            {
                unsigned char c2 = (unsigned char)str[2];
                unsigned char c3 = (unsigned char)str[3];
                if (c2 == UTF8_ENCODED_BOM_BYTE1 && (c3 == UTF8_ENCODED_BOM_BYTE2_1 || c3 == UTF8_ENCODED_BOM_BYTE2_2))
                    return false;
            }
        }
    }

    return true;
}

bool AudioManager::parseId3Metadata(const char *id3String)
{
    if (!id3String || id3String[0] == '\0')
        return false;

    // Artist-Muster (von spezifisch zu allgemein)
    const char *artistPatterns[] = {
        "Lead artist(s)/Lead performer(s)/Soloist(s)/Performing group: ",
        "ARTIST=",
        "Artist=",
        "Artist: ",
        "Artist:",
        "BAND=",
        "Band=",
        "Band: ",
        "Band:"};

    const char *titlePatterns[] = {
        "Title/Songname/Content description: ",
        "TITLE=",
        "Title=",
        "Title: ",
        "Title:"};

    const char *albumPatterns[] = {
        "Album/Movie/Show title: ",
        "ALBUM=",
        "Album=",
        "Album: ",
        "Album:"};

    bool foundAny = false;
    char cleanedValue[MAX_EVENT_STRING_LENGTH];

    const char *value = findTagValue(id3String, artistPatterns, sizeof(artistPatterns) / sizeof(artistPatterns[0]));
    if (value)
    {
        if (cleanId3String(value, cleanedValue, sizeof(cleanedValue)) && isValidMetadataString(cleanedValue))
        {
            stringutils::safeCopyUtf8(m_metadata.artist, cleanedValue);
            m_metadata.hasArtist = true;
            foundAny = true;

            enqueueEvent_(AudioManagerEventType::ID3_ARTIST, m_metadata.artist);
        }
    }

    value = findTagValue(id3String, titlePatterns, sizeof(titlePatterns) / sizeof(titlePatterns[0]));
    if (value)
    {
        if (cleanId3String(value, cleanedValue, sizeof(cleanedValue)) && isValidMetadataString(cleanedValue))
        {
            stringutils::safeCopyUtf8(m_metadata.title, cleanedValue);
            m_metadata.hasTitle = true;
            foundAny = true;

            enqueueEvent_(AudioManagerEventType::ID3_TITLE, m_metadata.title);
        }
    }

    value = findTagValue(id3String, albumPatterns, sizeof(albumPatterns) / sizeof(albumPatterns[0]));
    if (value)
    {
        if (cleanId3String(value, cleanedValue, sizeof(cleanedValue)) && isValidMetadataString(cleanedValue))
        {
            stringutils::safeCopyUtf8(m_metadata.album, cleanedValue);
            m_metadata.hasAlbum = true;
            foundAny = true;

            enqueueEvent_(AudioManagerEventType::ID3_ALBUM, m_metadata.album);
        }
    }

    return foundAny;
}

bool AudioManager::cleanId3String(const char *input, char *output, size_t maxLen)
{
    if (!input || !output || maxLen == 0)
        return false;

    size_t len = strlen(input);

    // UTF-16 BOM erkennen und ignorieren (Library kann es nicht dekodieren)
    if (len >= 2 && ((unsigned char)input[0] == UTF16_BOM_BYTE1 && (unsigned char)input[1] == UTF16_BOM_BYTE2))
    {
        // UTF-16 erkannt - Library hat es verstümmelt, ignorieren
        output[0] = '\0';
        return false;
    }

    // Steuerzeichen (außer Zeilenende) und die Bytes 0xFF/0xFE gelten als Datenmüll, UTF-8-Folgebytes bleiben erlaubt.
    bool hasGarbage = false;

    for (size_t i = 0; i < len; i++)
    {
        unsigned char c = input[i];
        if ((c < PRINTABLE_MIN && c != 0x00 && c != '\n' && c != '\r') || (c == 0xFF || c == 0xFE))
        {
            hasGarbage = true;
            break;
        }
    }

    if (hasGarbage)
    {
        output[0] = '\0';
        return false;
    }

    stringutils::safeCopy(output, maxLen, input);

    size_t i = 0;
    while (output[i] != '\0')
    {
        if (output[i] != ' ' && output[i] != '\t' && output[i] != '\n' && output[i] != '\r')
            return true;

        i++;
    }

    return false;
}

void AudioManager::handleAudioCallback(const Audio::msg_t &msg)
{
    switch (msg.e)
    {
    case Audio::evt_info:
        handleInfo(msg);
        break;

    case Audio::evt_id3data:
        handleId3Data(msg);
        break;

    case Audio::evt_eof:
        handleEof(msg);
        break;

    case Audio::evt_name:
        handleStationName(msg);
        break;

    case Audio::evt_icydescription:
        handleIcyDescription(msg);
        break;

    case Audio::evt_streamtitle:
        handleStreamTitle(msg);
        break;

    case Audio::evt_bitrate:
        handleBitrate(msg);
        break;

    case Audio::evt_icyurl:
        handleIcyUrl(msg);
        break;

    case Audio::evt_icylogo:
        handleIcyLogo(msg);
        break;

    case Audio::evt_lasthost:
        handleLastHost(msg);
        break;

    case Audio::evt_image:
        if (m_covers && m_file.load() && m_activeGeneration.load() == m_generation.load())
        {
            CoverEvent event;
            event.requestId = m_requestId.load();
            if (msg.vec.size() <= 16 && msg.vec.size() % 2 == 0)
            {
                event.count = msg.vec.size();
                std::copy(msg.vec.begin(), msg.vec.end(), event.offsets);
            }
            xQueueOverwrite(m_covers, &event);
        }
        break;

    case Audio::evt_lyrics:
        handleLyrics(msg);
        break;

    case Audio::evt_log:
        if (msg.s && strcmp(msg.s, "LOGE") == 0)
        {
            m_decoderError.store(true);
            if (m_file.load())
                LOG_WARNING_F("SD Decoderfehler: %s", msg.msg ? msg.msg : "ohne Meldung");
        }
        break;

    default:
        break;
    }
}

void AudioManager::handleInfo(const Audio::msg_t &msg)
{
    if (!msg.msg)
        return;

    // audio-lite meldet Decoderwechsel nur als Text "<Name>Decoder has been initialized" bzw. "... destroyed".
    constexpr char Marker[] = "Decoder has been ";
    if (const char *tail = strstr(msg.msg, Marker))
    {
        const char *what = tail + sizeof(Marker) - 1;
        if (!strcmp(what, "initialized"))
            m_codec.store(codecFromName(msg.msg, static_cast<size_t>(tail - msg.msg)));
        else if (!strcmp(what, "destroyed"))
            m_codec.store(Codec::None);
    }

    enqueueEvent_(AudioManagerEventType::INFO, msg.msg);
}

void AudioManager::handleId3Data(const Audio::msg_t &msg)
{
    if (!msg.msg)
        return;

    // Metadaten parsen (fügt selbst Events in Queue ein)
    parseId3Metadata(msg.msg);
}

void AudioManager::handleEof(const Audio::msg_t &msg)
{
    m_eof.store(true);
    const char *eofText = msg.msg ? msg.msg : "EOF";

    enqueueEvent_(AudioManagerEventType::EOF_AUDIO, eofText);
}

void AudioManager::handleStationName(const Audio::msg_t &msg)
{
    if (!msg.msg)
        return;

    enqueueEvent_(AudioManagerEventType::STATION_NAME, msg.msg);
}

void AudioManager::handleIcyDescription(const Audio::msg_t &msg)
{
    if (!msg.msg)
        return;

    enqueueEvent_(AudioManagerEventType::ICY_DESCRIPTION, msg.msg);
}

void AudioManager::handleStreamTitle(const Audio::msg_t &msg)
{
    if (!msg.msg)
        return;

    enqueueEvent_(AudioManagerEventType::STREAM_TITLE, msg.msg);
}

void AudioManager::handleBitrate(const Audio::msg_t &msg)
{
    if (!msg.msg)
        return;

    enqueueEvent_(AudioManagerEventType::BITRATE, msg.msg);
}

void AudioManager::handleIcyUrl(const Audio::msg_t &msg)
{
    if (!msg.msg)
        return;

    enqueueEvent_(AudioManagerEventType::ICY_URL, msg.msg);
}

void AudioManager::handleIcyLogo(const Audio::msg_t &msg)
{
    if (!msg.msg)
        return;

    enqueueEvent_(AudioManagerEventType::ICY_LOGO, msg.msg);
}

void AudioManager::handleLastHost(const Audio::msg_t &msg)
{
    if (!msg.msg)
        return;

    enqueueEvent_(AudioManagerEventType::LAST_HOST, msg.msg);
}

void AudioManager::handleLyrics(const Audio::msg_t &msg)
{
    if (!msg.msg)
        return;

    enqueueEvent_(AudioManagerEventType::LYRICS, msg.msg);
}

void AudioManager::task_processAudioSamples32(int32_t *outBuff, int16_t validSamples, bool *continueI2S)
{
    if (continueI2S)
        *continueI2S = true;

    if (!outBuff || validSamples <= 0)
        return;

    m_pcmFrames.fetch_add(static_cast<uint32_t>(validSamples));
    m_lastPcm.store(millis());
    for (int i = 0; i < validSamples * 2; ++i)
    {
        if (outBuff[i] != 0)
        {
            m_nonzeroBlocks.fetch_add(1);
            break;
        }
    }

    if (m_dataReady.load(std::memory_order_acquire))
        return;

    const uint16_t samplesToProcess = (validSamples > static_cast<int16_t>(MAX_SAMPLE_BUFFER_SIZE))
                                          ? static_cast<uint16_t>(MAX_SAMPLE_BUFFER_SIZE)
                                          : static_cast<uint16_t>(validSamples);

    int32_t currentMaxL = 0;
    int32_t currentMaxR = 0;
    int64_t currentSumSquareL = 0;
    int64_t currentSumSquareR = 0;
    uint32_t monoSampleCount = 0;

    const int32_t *__restrict__ src = outBuff;
    int16_t *__restrict__ dstL = m_sampleBufferLeft;
    int16_t *__restrict__ dstR = m_sampleBufferRight;

    for (uint16_t i = 0; i < samplesToProcess; ++i)
    {
        const int32_t sampleL = q31ToInt16(src[0]);
        const int32_t sampleR = q31ToInt16(src[1]);
        src += 2;

        dstL[i] = static_cast<int16_t>(sampleL);
        dstR[i] = static_cast<int16_t>(sampleR);

        currentSumSquareL += sampleL * sampleL;
        currentSumSquareR += sampleR * sampleR;

        const int32_t absL = (sampleL < 0) ? -sampleL : sampleL;
        const int32_t absR = (sampleR < 0) ? -sampleR : sampleR;
        if (absL > currentMaxL)
            currentMaxL = absL;
        if (absR > currentMaxR)
            currentMaxR = absR;

        const int32_t diff = sampleL - sampleR;
        const int32_t absDiff = (diff < 0) ? -diff : diff;
        if (absDiff < MONO_TOLERANCE_THRESHOLD)
            ++monoSampleCount;
    }

    const float invSamples = 1.0f / static_cast<float>(samplesToProcess);

    // Gemessen wird vor dem Softwarelautstärkeregler, die Werte beschreiben den Programmpegel der aktiven Radio- oder SD-Quelle.
    m_rmsLeft = sqrtf(static_cast<float>(currentSumSquareL) * invSamples) * RMS_SCALE_FACTOR;
    m_rmsRight = sqrtf(static_cast<float>(currentSumSquareR) * invSamples) * RMS_SCALE_FACTOR;

    m_peakLeft = static_cast<float>(currentMaxL) * PEAK_SCALE_FACTOR;
    m_peakRight = static_cast<float>(currentMaxR) * PEAK_SCALE_FACTOR;

    // Der zentral gemeldete Hardware-Stummzustand setzt Messwerte und Samples für die Anzeige auf null, der PCM-Abgriff liegt vor der Stummschaltung.
    // Pegel 0 am Drehknopf gilt für die Anzeige ebenso als stumm, weil der Knopf dann das Stummsymbol zeigt. Der Softwarepegel liegt hinter der Messstelle.
    if (m_muted.load(std::memory_order_relaxed) || m_loudnessVolume.load(std::memory_order_relaxed) == 0)
    {
        memset(m_sampleBufferLeft, 0, static_cast<size_t>(samplesToProcess) * sizeof(int16_t));
        memset(m_sampleBufferRight, 0, static_cast<size_t>(samplesToProcess) * sizeof(int16_t));
        m_rmsLeft = m_rmsRight = 0.0f;
        m_peakLeft = m_peakRight = 0.0f;
    }

    const float monoRatio = static_cast<float>(monoSampleCount) * invSamples;
    m_monoSignal = (monoRatio > MONO_THRESHOLD_FACTOR);

    m_numValidSamples = samplesToProcess;
    m_sampleRate = m_audio ? m_audio->getSampleRate() : 0;
    m_dataReady.store(true, std::memory_order_release);
}

bool AudioManager::isAudioDataReady() const
{
    return m_dataReady.load(std::memory_order_acquire);
}

float AudioManager::getRMSLeft() const
{
    return m_rmsLeft;
}

float AudioManager::getRMSRight() const
{
    return m_rmsRight;
}

float AudioManager::getPeakLeft() const
{
    return m_peakLeft;
}

float AudioManager::getPeakRight() const
{
    return m_peakRight;
}

bool AudioManager::isMono() const
{
    return m_monoSignal;
}

uint16_t AudioManager::getValidSamplesCount() const
{
    return m_numValidSamples;
}

uint32_t AudioManager::getSampleRate() const
{
    return m_sampleRate;
}

const int16_t *AudioManager::getSampleBufferLeft() const
{
    return m_sampleBufferLeft;
}

const int16_t *AudioManager::getSampleBufferRight() const
{
    return m_sampleBufferRight;
}

void AudioManager::consumeAudioData()
{
    m_dataReady.store(false, std::memory_order_release);
}

void audio_visualizer_process_i2s(int32_t *outBuff, int16_t validSamples, bool *continueI2S)
{
    g_audioManager.task_processAudioSamples32(outBuff, validSamples, continueI2S);
}

#include "Tab5Board.h"
#include <WiFi.h>

bool AudioManager::begin()
{
    if (m_task)
        return true;
    m_commands = xQueueCreate(1, sizeof(Command));
    m_controls = xQueueCreate(4, sizeof(Control));
    m_covers = xQueueCreate(1, sizeof(CoverEvent));
    if (!m_commands || !m_controls || !m_covers || xTaskCreatePinnedToCore(radioTask, "radio-audio", 12288, this, 2, &m_task, 0) != pdPASS)
    {
        if (m_commands)
            vQueueDelete(m_commands);
        if (m_controls)
            vQueueDelete(m_controls);
        if (m_covers)
            vQueueDelete(m_covers);
        m_commands = m_controls = m_covers = nullptr;
        return false;
    }
    return true;
}

bool AudioManager::requestStart(const char *url, int stationIndex, uint32_t requestId)
{
    if (!m_commands || m_state.load() == State::Unavailable || !url || !url[0] || strlen(url) >= sizeof(Command::url))
        return false;
    Command command = {};
    stringutils::safeCopy(command.url, url);
    command.stationIndex = stationIndex;
    command.requestId = requestId;
    command.start = true;
    command.generation = m_generation.fetch_add(1) + 1;
    return xQueueOverwrite(m_commands, &command) == pdPASS;
}

bool AudioManager::requestFile(const char *path, uint32_t requestId)
{
    if (!m_commands || m_state.load() == State::Unavailable || !path || strncmp(path, "/sdcard/", 8) || !path[8] || strlen(path) >= sizeof(Command::url))
        return false;
    Command command = {};
    stringutils::safeCopy(command.url, path);
    command.stationIndex = -1;
    command.requestId = requestId;
    command.start = command.file = true;
    command.generation = m_generation.fetch_add(1) + 1;
    return xQueueOverwrite(m_commands, &command) == pdPASS;
}

void AudioManager::requestStop()
{
    if (!m_commands || m_state.load() == State::Unavailable)
        return;
    Command command = {};
    command.generation = m_generation.fetch_add(1) + 1;
    xQueueOverwrite(m_commands, &command);
}

bool AudioManager::requestPause(bool paused, uint32_t requestId)
{
    if (!m_controls || !m_file.load())
        return false;
    Control command{m_generation.load(), requestId, 0, false, paused};
    return xQueueSend(m_controls, &command, 0) == pdPASS;
}

bool AudioManager::requestSeek(uint32_t seconds, uint32_t requestId)
{
    if (!m_controls || !m_file.load())
        return false;
    Control command{m_generation.load(), requestId, seconds, true, false};
    return xQueueSend(m_controls, &command, 0) == pdPASS;
}

bool AudioManager::canOutput(const Status &status, uint32_t requestId)
{
    return requestId && status.requestId == requestId && status.state == State::Streaming && status.pcmAgeMs <= 500 && status.nonzeroBlocks > 0;
}

bool AudioManager::setTuning(const Tuning &tuning)
{
    if (tuning.low < 100 || tuning.low > 800 || tuning.mid < 1500 || tuning.mid > 6000 || tuning.high < 4000 || tuning.high > 12000 || tuning.width > 200 || tuning.loudness > 100)
        return false;
    portENTER_CRITICAL(&m_tuningLock);
    m_tuning = tuning;
    portEXIT_CRITICAL(&m_tuningLock);
    return true;
}

void AudioManager::setVolume(uint8_t volume)
{
    m_volume.store(volume > MaxVolume ? MaxVolume : volume);
    m_loudnessVolume.store(m_volume.load());
}

void AudioManager::setLoudnessVolume(uint8_t volume)
{
    m_loudnessVolume.store(volume > MaxVolume ? MaxVolume : volume);
}

void AudioManager::setTone(int8_t low, int8_t mid, int8_t high)
{
    const int8_t values[] = {low, mid, high};
    for (int i = 0; i < 3; ++i)
        m_tone[i].store(values[i] < -12 ? -12 : values[i] > 12 ? 12
                                                               : values[i]);
}

AudioManager::Status AudioManager::getStatus() const
{
    const uint32_t generation = m_generation.load(), activeGeneration = m_activeGeneration.load();
    const State state = m_state.load();
    const uint32_t requestId = m_requestId.load(), lastPcm = m_lastPcm.load();
    Status status{state, m_pcmFrames.load(), lastPcm ? millis() - lastPcm : UINT32_MAX,
                  m_rate.load(), m_bitRate.load(), m_nonzeroBlocks.load(), m_stationIndex.load(), m_appliedVolume.load(),
                  requestId, m_controlId.load(), m_position.load(), m_duration.load(),
                  m_file.load(), m_fileConfirmed.load(), m_commandFailed.load(), m_codec.load()};
    if (generation != activeGeneration || generation != m_generation.load() || activeGeneration != m_activeGeneration.load() || state != m_state.load() || requestId != m_requestId.load())
        status.state = State::Starting;
    return status;
}

void AudioManager::radioTask(void *context)
{
    auto &manager = *static_cast<AudioManager *>(context);
    static Audio audio;
    manager.init(audio);
    audio.setVolumeSteps(100);
    audio.setVolume(0);
    bool mono = true;
    audio.forceMono(mono);
    audio.setConnectionTimeout(2000, 5000);
    audio.settings.DMA_DESC_NUM = I2sDmaDescriptors;
    audio.settings.DMA_FRAME_NUM = I2sDmaFrames;
    if (!audio.setPinout(Tab5Board::I2sBclk, Tab5Board::I2sLrck, Tab5Board::I2sData, Tab5Board::I2sMclk))
    {
        LOG_ERROR("Audio: I2S-Initialisierung fehlgeschlagen.");
        vTaskSuspend(nullptr);
    }
    manager.m_state.store(State::Idle);
    LOG_INFO("AudioManager bereit: Radio/Datei, 32-Bit-I2S, MCLK 256fs. Codec bleibt stumm.");
    bool active = false, paused = false;
    int8_t tone[3] = {127, 127, 127};
    int8_t balance = 127;
    Tuning appliedTuning{};
    bool tuningApplied = false;
    uint8_t toneVolume = 255;
    uint32_t started = 0;
    for (;;)
    {
        Command command;
        if (xQueueReceive(manager.m_commands, &command, active ? 0 : portMAX_DELAY) == pdPASS)
        {
            manager.m_state.store(State::Starting);
            audio.stopSong();
            active = paused = false;
            portENTER_CRITICAL(&manager.m_evtMux);
            manager.m_qHead.store(0);
            manager.m_qTail.store(0);
            portEXIT_CRITICAL(&manager.m_evtMux);
            manager.resetMetadata();
            manager.m_lastPcm.store(0);
            manager.m_nonzeroBlocks.store(0);
            manager.m_rate.store(0);
            manager.m_bitRate.store(0);
            manager.m_codec.store(Codec::None);
            manager.m_position.store(0);
            manager.m_duration.store(0);
            manager.m_controlId.store(0);
            manager.m_fileConfirmed.store(false);
            manager.m_commandFailed.store(false);
            manager.m_eof.store(false);
            manager.m_decoderError.store(false);
            manager.m_requestId.store(command.requestId);
            manager.m_file.store(command.file);
            manager.m_stationIndex.store(command.stationIndex);
            if (command.start && command.generation == manager.m_generation.load())
            {
                audio.setVolume(manager.m_volume.load());
                manager.m_appliedVolume.store(manager.m_volume.load());
                active = command.file ? audio.connecttoFS(command.url) : WiFi.status() == WL_CONNECTED && audio.connecttohost(command.url);
                started = millis();
            }
            if (command.generation != manager.m_generation.load())
            {
                audio.stopSong();
                active = false;
                continue;
            }
            manager.m_state.store(active ? State::Streaming : command.start ? State::Error
                                                                            : State::Idle);
            manager.m_activeGeneration.store(command.generation);
            LOG_INFO_F("Audio: Quelle=%s Kennung=%lu Zustand=%u", command.file ? "SD" : "Radio", static_cast<unsigned long>(command.requestId), static_cast<unsigned>(manager.m_state.load()));
        }
        Control control;
        if (xQueueReceive(manager.m_controls, &control, 0) == pdPASS && control.generation == manager.m_activeGeneration.load() && control.generation == manager.m_generation.load())
        {
            bool ok = active && manager.m_file.load();
            if (ok && control.seek)
            {
                const int64_t delta = static_cast<int64_t>(control.seconds) - audio.getAudioCurrentTime();
                ok = !paused && delta >= INT32_MIN && delta <= INT32_MAX && audio.setTimeOffset(static_cast<int>(delta));
            }
            else if (ok && paused != control.paused)
            {
                ok = audio.pauseResume();
                if (ok)
                {
                    paused = control.paused;
                    started = millis();
                    manager.m_lastPcm.store(0);
                }
            }
            manager.m_commandFailed.store(!ok);
            manager.m_controlId.store(control.requestId);
            if (ok)
                manager.m_state.store(paused ? State::Paused : State::Streaming);
        }
        if (active)
        {
            const uint8_t volume = manager.m_volume.load();
            if (volume != manager.m_appliedVolume.load())
            {
                audio.setVolume(volume);
                manager.m_appliedVolume.store(volume);
            }
            if (balance != manager.m_balance.load())
            {
                balance = manager.m_balance.load();
                audio.setBalance(balance / 16.0f);
            }
            Tuning tuning;
            portENTER_CRITICAL(&manager.m_tuningLock);
            tuning = manager.m_tuning;
            portEXIT_CRITICAL(&manager.m_tuningLock);
            const uint8_t loudnessVolume = manager.m_loudnessVolume.load();
            bool toneChanged = !tuningApplied || tuning.low != appliedTuning.low || tuning.mid != appliedTuning.mid || tuning.high != appliedTuning.high || tuning.loudness != appliedTuning.loudness || toneVolume != loudnessVolume;
            if (!tuningApplied || tuning.width != appliedTuning.width)
                audio.setStereoWidth(tuning.width);
            // Der einzelne eingebaute Lautsprecher bekommt beide Kanäle gemischt, die Buchse Stereo.
            if (mono == manager.m_stereoOutput.load())
            {
                mono = !mono;
                audio.forceMono(mono);
            }
            for (int i = 0; i < 3; ++i)
                if (tone[i] != manager.m_tone[i].load())
                {
                    tone[i] = manager.m_tone[i].load();
                    toneChanged = true;
                }
            if (toneChanged)
            {
                audio.settings.FREQ_LS_HZ = tuning.low;
                audio.settings.FREQ_PEAK_HZ = tuning.mid;
                audio.settings.FREQ_HS_HZ = tuning.high;
                // Wie applyAudioTone in RetroRadio (ea4229a): Loudness hebt Tiefen um bis zu 8 dB und Höhen um bis zu 3 dB an und wirkt oberhalb von Pegel 90 nicht mehr.
                const float factor = static_cast<float>(tuning.loudness) / 100.0f * (loudnessVolume < 90 ? static_cast<float>(90 - loudnessVolume) / 90.0f : 0.0f);
                auto clamp = [](float value)
                {
                    return value < -12.0f ? -12.0f : value > 12.0f ? 12.0f
                                                                   : value;
                };
                audio.setTone(clamp(tone[0] + 8.0f * factor), clamp(tone[1]), clamp(tone[2] + 3.0f * factor));
                toneVolume = loudnessVolume;
            }
            appliedTuning = tuning;
            tuningApplied = true;
            audio.loop();
            // audio-lite stoppt die Datei vor dem Einreihen von EOF. Erst der nächste loop liefert dieses Ereignis, vorher wäre ein reguläres Ende fälschlich ein Fehler.
            if (manager.m_file.load() && !paused && !audio.isRunning())
                audio.loop();
            // lastPcm vor now lesen: Der audio-lite-Task kann dazwischen einen neueren Zeitstempel schreiben.
            const uint32_t lastPcm = manager.m_lastPcm.load(), now = millis();
            if (manager.m_file.load() && lastPcm)
                manager.m_fileConfirmed.store(true);
            if (!paused)
            {
                const bool eof = manager.m_eof.load(), stopped = !audio.isRunning(), wifiLost = !manager.m_file.load() && WiFi.status() != WL_CONNECTED, timedOut = pcmTimedOut(now, lastPcm, started);
                if (eof || stopped || wifiLost || timedOut)
                {
                    const bool ended = manager.m_file.load() && eof && manager.m_fileConfirmed.load() && !manager.m_decoderError.load();
                    manager.m_state.store(ended ? State::Ended : State::Error);
                    audio.stopSong();
                    audio.setVolume(0);
                    active = false;
                    LOG_INFO_F("Audio abgeschlossen: regulär=%u EOF=%u Datei=%u bestätigt=%u Decoderfehler=%u gestoppt=%u WLAN-Ausfall=%u Zeitüberschreitung=%u PCMAlter=%ld ms%s", ended, eof, manager.m_file.load(), manager.m_fileConfirmed.load(), manager.m_decoderError.load(), stopped, wifiLost, timedOut, static_cast<long>(static_cast<int32_t>(now - (lastPcm ? lastPcm : started))), lastPcm ? "" : " seit Start");
                }
            }
            manager.m_rate.store(audio.getSampleRate());
            manager.m_bitRate.store(audio.getBitRate());
            if (manager.m_file.load() && active)
            {
                manager.m_position.store(audio.getAudioCurrentTime());
                manager.m_duration.store(audio.getAudioFileDuration());
            }
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

bool AudioManager::takeCover(CoverEvent &event)
{
    return m_covers && xQueueReceive(m_covers, &event, 0) == pdPASS;
}
