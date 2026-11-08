#include "AudioBuffer.h"

#include <esp_log.h>
#include <freertos/task.h>

#include <algorithm>
#include <cstring>

// Der Puffer liegt im PSRAM: ein Hauptbereich, dahinter ein Reservebereich, der Blöcke über das Ende des Hauptbereichs hinaus am Stück lesbar macht.
//
// Zustand 1: Der Lesezeiger liegt vor dem Schreibzeiger.
//
// m_startPtr        m_readPtr                m_writePtr                     m_endPtr        m_buffEnd
// v                 v                        v                              v               v
// +-------------------------------------------------------------------------+---------------+
// |                 |<----- readSpace ------>|<---------------- writeSpace ---------------->|
// |<---- frei ----->|<------- belegt ------->|<------------------- frei ------------------->|
// |<--------------------------- m_mainBuffSize ---------------------------->|<m_resBuffSize>|
//
// Zustand 2: Der Schreibzeiger ist umgebrochen und liegt vor dem Lesezeiger.
//
// m_startPtr                    m_writePtr               m_readPtr          m_endPtr        m_buffEnd
// v                             v                        v                  v               v
// +-------------------------------------------------------------------------+---------------+
// |                             |<----- writeSpace ----->|<---------- readSpace ----------->|
// |<---------- belegt --------->|<-------- frei -------->|<------------ belegt ------------>|
//
// Ist der Reservebereich vollgeschrieben, kopiert writeSpace() seinen Inhalt an den Anfang des Hauptbereichs und setzt den Schreibzeiger auf m_startPtr + m_resBuffSize, sobald der Lesezeiger nicht im Weg steht.
// Steht der Lesezeiger im Reservebereich und der Schreibzeiger weit genug dahinter im Hauptbereich, zieht readSpace() den Lesezeiger auf m_startPtr + (m_readPtr - m_endPtr) nach.

namespace {
    const char *TAG = "audio-lite";

    constexpr TickType_t OVERRUN_THROTTLE_TICKS = 100; ///< Wartezeit nach einer Invarianzverletzung, bremst Dauerfehler aus
}

// ===================================================================
// Konstruktion und Initialisierung
// ===================================================================

/**
 * @brief Konstruktor, allokiert noch nicht
 */
AudioBuffer::AudioBuffer()
{
}

/**
 * @brief Destruktor, gibt das Semaphor frei
 */
AudioBuffer::~AudioBuffer()
{
    if (m_mutex)
    {
        vSemaphoreDelete(m_mutex);
        m_mutex = nullptr;
    }
}

/**
 * @brief Allokiert den Puffer im PSRAM und setzt die Zeiger
 */
size_t AudioBuffer::init()
{
    if (m_init)
        return m_mainBuffSize;

    if (!m_buffer.alloc(m_mainBuffSize + m_resBuffSize, "AudioBuffer"))
    {
        ESP_LOGE(TAG, "AudioBuffer: %zu Bytes im PSRAM nicht verfügbar", m_mainBuffSize + m_resBuffSize);
        return 0;
    }

    m_mutex = xSemaphoreCreateBinary();
    if (!m_mutex)
    {
        ESP_LOGE(TAG, "AudioBuffer: Semaphor konnte nicht angelegt werden");
        m_buffer.reset();
        return 0;
    }
    xSemaphoreGive(m_mutex);

    m_startPtr = m_buffer.get();
    m_endPtr = m_startPtr + m_mainBuffSize;
    m_buffEnd = m_endPtr + m_resBuffSize;

    m_init = true;
    reset();

    return m_mainBuffSize;
}

/**
 * @brief Größe des Hauptbereichs
 */
size_t AudioBuffer::getBufsize()
{
    return m_mainBuffSize;
}

/**
 * @brief Setzt die größte Blockgröße
 */
void AudioBuffer::setMaxBlocksize(uint32_t mbs)
{
    m_maxBlockSize = mbs;
}

/**
 * @brief Größte Blockgröße, die der Decoder am Stück anfordert
 */
size_t AudioBuffer::getMaxBlockSize()
{
    return m_maxBlockSize;
}

// ===================================================================
// Platzabfragen
// ===================================================================

/**
 * @brief Gesamter freier Platz
 */
size_t AudioBuffer::freeSpace()
{
    if (!m_init)
        return 0;

    if (m_readPtr == m_writePtr)
    {
        if (m_isEmpty)
            return m_mainBuffSize;

        if (m_isFull)
            return 0;

        ESP_LOGE(TAG, "freeSpace: writePtr == readPtr, aber weder leer noch voll (w %zu, r %zu)", static_cast<size_t>(m_writePtr - m_startPtr), static_cast<size_t>(m_readPtr - m_startPtr));
    }

    if (m_readPtr < m_writePtr)
    {
        if (m_writePtr > m_endPtr)
            return static_cast<size_t>(m_readPtr - m_startPtr);

        return static_cast<size_t>(m_endPtr - m_writePtr) + static_cast<size_t>(m_readPtr - m_startPtr);
    }

    return static_cast<size_t>(m_readPtr - m_writePtr);
}

/**
 * @brief Belegter Platz
 */
size_t AudioBuffer::bufferFilled()
{
    if (!m_init)
        return 0;

    xSemaphoreTake(m_mutex, portMAX_DELAY);
    size_t filled = 0;

    if (m_readPtr == m_writePtr)
    {
        if (m_isEmpty)
        {
            filled = 0;
            goto end;
        }
        if (m_isFull)
        {
            filled = m_mainBuffSize;
            goto end;
        }
        ESP_LOGE(TAG, "bufferFilled: writePtr == readPtr, aber weder leer noch voll (w %zu, r %zu)", static_cast<size_t>(m_writePtr - m_startPtr), static_cast<size_t>(m_readPtr - m_startPtr));
    }

    if (m_readPtr < m_writePtr)
    {
        filled = static_cast<size_t>(m_writePtr - m_readPtr);
        goto end;
    }

    filled = static_cast<size_t>(m_endPtr - m_readPtr) + static_cast<size_t>(m_writePtr - m_startPtr);

end:
    xSemaphoreGive(m_mutex);
    return filled;
}

/**
 * @brief Am Stück beschreibbarer Platz ab dem Schreibzeiger
 */
size_t AudioBuffer::writeSpace()
{
    if (!m_init)
        return 0;

    xSemaphoreTake(m_mutex, portMAX_DELAY);
    m_writeSpace = 0;

    size_t spaceToEnd = static_cast<size_t>(m_buffEnd - m_writePtr);

    if (m_isFull)
    {
        m_writeSpace = 0;
        goto end;
    }

    if (m_isEmpty)
    {
        m_writeSpace = std::min(MAX_RETURN, spaceToEnd);
        goto end;
    }

    // Reservebereich ist voll: seinen Inhalt an den Anfang zurückholen, aber nur wenn der Lesezeiger nicht im Weg steht
    if (spaceToEnd == 0)
    {
        if (m_readPtr > m_startPtr + m_resBuffSize)
        {
            memcpy(m_startPtr, m_endPtr, m_resBuffSize);
            m_writePtr = m_startPtr + m_resBuffSize;
        }
    }

    if (m_writePtr < m_readPtr)
    {
        if (m_readPtr >= m_endPtr)
        {
            // Der Lesezeiger steht im Reservebereich, der Schreibzeiger darf ihm dorthin nicht folgen
            m_writeSpace = std::min(MAX_RETURN, static_cast<size_t>(m_endPtr - m_writePtr));
        }
        else
        {
            m_writeSpace = std::min(MAX_RETURN, static_cast<size_t>(m_readPtr - m_writePtr));
        }
        goto end;
    }

    if (m_writePtr > m_readPtr)
    {
        m_writeSpace = std::min(MAX_RETURN, spaceToEnd);
        goto end;
    }

    ESP_LOGE(TAG, "writeSpace: writePtr == readPtr, aber weder leer noch voll (w %zu, r %zu)", static_cast<size_t>(m_writePtr - m_startPtr), static_cast<size_t>(m_readPtr - m_startPtr));

end:
    xSemaphoreGive(m_mutex);
    return m_writeSpace;
}

/**
 * @brief Am Stück lesbarer Block ab dem Lesezeiger
 */
size_t AudioBuffer::readSpace()
{
    if (!m_init)
        return 0;

    xSemaphoreTake(m_mutex, portMAX_DELAY);

    // Lesezeiger steht im Reservebereich, der Schreibzeiger ist schon weit genug im Hauptbereich: Lesezeiger nachziehen
    if (m_readPtr >= m_endPtr && m_writePtr <= m_endPtr)
    {
        size_t len = static_cast<size_t>(m_readPtr - m_endPtr);
        if (m_writePtr > m_startPtr + len)
            m_readPtr = m_startPtr + len;
    }

    m_readSpace = 0;

    if (m_isEmpty)
        goto end;

    if (m_isFull)
    {
        m_readSpace = std::min(MAX_RETURN, static_cast<size_t>(m_buffEnd - m_readPtr));
        goto end;
    }

    if (m_readPtr < m_writePtr)
    {
        m_readSpace = std::min(MAX_RETURN, static_cast<size_t>(m_writePtr - m_readPtr));
        goto end;
    }

    if (m_readPtr > m_writePtr)
    {
        m_readSpace = std::min(MAX_RETURN, static_cast<size_t>(m_buffEnd - m_readPtr));
        goto end;
    }

    ESP_LOGE(TAG, "readSpace: writePtr == readPtr, aber weder leer noch voll (w %zu, r %zu)", static_cast<size_t>(m_writePtr - m_startPtr), static_cast<size_t>(m_readPtr - m_startPtr));

end:
    xSemaphoreGive(m_mutex);
    return m_readSpace;
}

// ===================================================================
// Zeiger fortschalten
// ===================================================================

/**
 * @brief Rückt den Schreibzeiger vor
 */
void AudioBuffer::bytesWritten(size_t bw)
{
    if (!m_init)
        return;

    xSemaphoreTake(m_mutex, portMAX_DELAY);

    if (!bw)
        goto end;

    if (bw > m_writeSpace)
        ESP_LOGE(TAG, "bytesWritten: bw %zu größer als der erfragte writeSpace %zu", bw, m_writeSpace);

    if (m_writePtr < m_readPtr && m_writePtr + bw > m_readPtr)
    {
        ESP_LOGE(TAG, "bytesWritten: writePtr überholt readPtr (w %zu, r %zu, bw %zu)", static_cast<size_t>(m_writePtr - m_startPtr), static_cast<size_t>(m_readPtr - m_startPtr), bw);
        m_writePtr = m_readPtr;
        goto end;
    }

    if (m_writePtr + bw > m_buffEnd)
    {
        ESP_LOGE(TAG, "bytesWritten: writePtr überläuft buffEnd (w %zu, end %zu, bw %zu)", static_cast<size_t>(m_writePtr - m_startPtr), static_cast<size_t>(m_buffEnd - m_startPtr), bw);
        m_writePtr = m_buffEnd;
        goto end;
    }

    m_writePtr += bw;
    if (m_writePtr == m_readPtr)
        m_isFull = true;

    m_isEmpty = false;

end:
    xSemaphoreGive(m_mutex);
}

/**
 * @brief Rückt den Lesezeiger vor
 */
void AudioBuffer::bytesWasRead(size_t br)
{
    if (!m_init)
        return;

    xSemaphoreTake(m_mutex, portMAX_DELAY);

    if (!br)
        goto end;

    if (m_readSpace < br)
    {
        ESP_LOGE(TAG, "bytesWasRead: br %zu größer als der erfragte readSpace %zu", br, m_readSpace);
        vTaskDelay(OVERRUN_THROTTLE_TICKS);
        goto end;
    }

    if (m_readPtr < m_writePtr && m_readPtr + br > m_writePtr)
    {
        ESP_LOGE(TAG, "bytesWasRead: readPtr überholt writePtr (r %zu, w %zu, br %zu)", static_cast<size_t>(m_readPtr - m_startPtr), static_cast<size_t>(m_writePtr - m_startPtr), br);
        m_readPtr = m_writePtr;
        vTaskDelay(OVERRUN_THROTTLE_TICKS);
        goto end;
    }

    if (m_readPtr + br > m_buffEnd)
    {
        ESP_LOGE(TAG, "bytesWasRead: readPtr überläuft buffEnd (r %zu, end %zu, br %zu)", static_cast<size_t>(m_readPtr - m_startPtr), static_cast<size_t>(m_buffEnd - m_startPtr), br);
        m_readPtr = m_buffEnd;
        vTaskDelay(OVERRUN_THROTTLE_TICKS);
        goto end;
    }

    m_readPtr += br;
    if (m_readPtr == m_writePtr)
        m_isEmpty = true;

    m_isFull = false;

end:
    xSemaphoreGive(m_mutex);
}

// ===================================================================
// Zeigerzugriff und Rücksetzen
// ===================================================================

/**
 * @brief Aktueller Schreibzeiger
 */
uint8_t *AudioBuffer::getWritePtr()
{
    return m_writePtr;
}

/**
 * @brief Aktueller Lesezeiger
 */
uint8_t *AudioBuffer::getReadPtr()
{
    return m_readPtr;
}

/**
 * @brief Setzt den Puffer auf leer zurück
 */
void AudioBuffer::reset()
{
    if (!m_init)
        return;

    m_writePtr = m_startPtr;
    m_readPtr = m_startPtr;
    m_isEmpty = true;
    m_isFull = false;
    m_readSpace = 0;
    m_writeSpace = std::min(MAX_RETURN, static_cast<size_t>(m_buffEnd - m_writePtr));
}
