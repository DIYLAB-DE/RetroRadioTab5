#ifndef AUDIO_LITE_AUDIOBUFFER_H
#define AUDIO_LITE_AUDIOBUFFER_H

/**
 * @file AudioBuffer.h
 * @brief Ringpuffer zwischen Byte-Quelle und Decoder
 */

#include "psram_unique_ptr.hpp"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cstddef>
#include <cstdint>

// ===================================================================
// AudioBuffer
// ===================================================================

/**
 * @brief Ringpuffer, der gelesene Bytes aufnimmt und dem Decoder blockweise anbietet
 *
 * Der Puffer liegt im PSRAM und besteht aus zwei Bereichen. Der Hauptbereich nimmt die Daten auf, der anschließende Reservebereich erlaubt es, einen Block auch dann zusammenhängend zu lesen, wenn er über das Ende des Hauptbereichs hinausreicht. Deshalb gibt `readSpace()` immer einen am Stück lesbaren Block zurück, ohne dass der Aufrufer den Umbruch behandeln muss.
 *
 * Schreibender und lesender Zugriff laufen aus verschiedenen Kontexten. Die Aktualisierung der Zeiger ist deshalb über ein Semaphor abgesichert. Das gilt für die Zeiger, nicht für den Pufferinhalt: wer über `getWritePtr()` oder `getReadPtr()` an die Daten geht, muss die zuvor erfragte Blockgröße einhalten.
 *
 * Übliche Reihenfolge beim Schreiben ist `writeSpace()`, dann in `getWritePtr()` schreiben, dann `bytesWritten()`. Beim Lesen entsprechend `readSpace()`, aus `getReadPtr()` lesen, dann `bytesWasRead()`.
 */
class AudioBuffer
{
public:
    /**
     * @brief Konstruktor, legt die Größen fest, allokiert aber noch nicht
     */
    AudioBuffer();

    /**
     * @brief Destruktor, gibt Puffer und Semaphor frei
     */
    ~AudioBuffer();

    AudioBuffer(const AudioBuffer &) = delete;
    AudioBuffer &operator=(const AudioBuffer &) = delete;
    AudioBuffer(AudioBuffer &&) = delete;
    AudioBuffer &operator=(AudioBuffer &&) = delete;

    /**
     * @brief Allokiert den Puffer im PSRAM und setzt die Zeiger
     * @return Größe des Hauptbereichs in Bytes, 0 wenn die Allokation fehlschlug
     *
     * Darf mehrfach aufgerufen werden, ein bereits allokierter Puffer wird nicht erneut angelegt.
     */
    size_t init();

    /**
     * @brief Meldet, ob der Puffer allokiert ist
     * @return true nach erfolgreichem `init()`
     */
    bool isInitialized() { return m_init; }

    /**
     * @brief Größe des Hauptbereichs
     * @return Größe in Bytes
     */
    size_t getBufsize();

    /**
     * @brief Größte Blockgröße, die der Decoder am Stück anfordert
     * @return Blockgröße in Bytes
     */
    size_t getMaxBlockSize();

    /**
     * @brief Setzt die größte Blockgröße
     * @param mbs Blockgröße in Bytes
     *
     * Wird vom Decoder nach dem Erkennen des Formats gesetzt und begrenzt, wie weit `readSpace()` in den Reservebereich hineinreichen darf.
     */
    void setMaxBlocksize(uint32_t mbs);

    /**
     * @brief Gesamter freier Platz
     * @return Anzahl überschreibbarer Bytes
     */
    size_t freeSpace();

    /**
     * @brief Am Stück beschreibbarer Platz ab dem Schreibzeiger
     * @return Anzahl Bytes, die ohne Umbruch geschrieben werden können
     */
    size_t writeSpace();

    /**
     * @brief Belegter Platz
     * @return Anzahl belegter Bytes
     */
    size_t bufferFilled();

    /**
     * @brief Am Stück lesbarer Block ab dem Lesezeiger
     * @return Anzahl Bytes, die ohne Umbruch gelesen werden können
     */
    size_t readSpace();

    /**
     * @brief Rückt den Schreibzeiger vor
     * @param bw Anzahl geschriebener Bytes
     *
     * `bw` darf den zuvor mit `writeSpace()` erfragten Wert nicht überschreiten.
     */
    void bytesWritten(size_t bw);

    /**
     * @brief Rückt den Lesezeiger vor
     * @param br Anzahl gelesener Bytes
     *
     * `br` darf den zuvor mit `readSpace()` erfragten Wert nicht überschreiten.
     */
    void bytesWasRead(size_t br);

    /**
     * @brief Aktueller Schreibzeiger
     * @return Zeiger auf die nächste freie Stelle
     *
     * @warning Gültig für höchstens `writeSpace()` Bytes und nur bis zum nächsten `bytesWritten()` oder `reset()`.
     */
    uint8_t *getWritePtr();

    /**
     * @brief Aktueller Lesezeiger
     * @return Zeiger auf das nächste ungelesene Byte
     *
     * @warning Gültig für höchstens `readSpace()` Bytes und nur bis zum nächsten `bytesWasRead()` oder `reset()`.
     */
    uint8_t *getReadPtr();

    /**
     * @brief Setzt den Puffer auf leer zurück
     *
     * Der Speicher bleibt allokiert, nur die Zeiger und Zähler werden zurückgesetzt.
     */
    void reset();

private:
    static constexpr size_t MAIN_BUFFER_SIZE = UINT16_MAX * 10; ///< Größe des Hauptbereichs in Bytes
    static constexpr size_t RESERVE_BUFFER_SIZE = UINT16_MAX;   ///< Größe des Reservebereichs für Blöcke über den Umbruch hinaus
    static constexpr size_t MAX_RETURN = UINT16_MAX;            ///< Obergrenze, die die Platzabfragen zurückmelden

    size_t m_mainBuffSize = MAIN_BUFFER_SIZE;   ///< Größe des Hauptbereichs
    size_t m_resBuffSize = RESERVE_BUFFER_SIZE; ///< Größe des Reservebereichs
    size_t m_maxBlockSize = 0;                  ///< Größte vom Decoder angeforderte Blockgröße
    size_t m_writeSpace = 0;                    ///< Zuletzt berechneter beschreibbarer Block
    size_t m_readSpace = 0;                     ///< Zuletzt berechneter lesbarer Block

    ps_ptr<uint8_t> m_buffer;        ///< Der eigentliche Speicher im PSRAM
    uint8_t *m_startPtr = nullptr;   ///< Anfang des Hauptbereichs
    uint8_t *m_endPtr = nullptr;     ///< Ende des Hauptbereichs, Anfang des Reservebereichs
    uint8_t *m_buffEnd = nullptr;    ///< Ende des Reservebereichs
    uint8_t *m_writePtr = nullptr;   ///< Aktueller Schreibzeiger
    uint8_t *m_readPtr = nullptr;    ///< Aktueller Lesezeiger

    bool m_init = false;    ///< Puffer ist allokiert
    bool m_isEmpty = true;  ///< Puffer enthält keine ungelesenen Daten
    bool m_isFull = false;  ///< Puffer ist vollständig belegt

    SemaphoreHandle_t m_mutex = nullptr; ///< Schützt die Zeiger-Aktualisierungen
};

#endif // AUDIO_LITE_AUDIOBUFFER_H
