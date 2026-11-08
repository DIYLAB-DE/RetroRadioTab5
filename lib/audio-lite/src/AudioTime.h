#ifndef AUDIO_LITE_AUDIOTIME_H
#define AUDIO_LITE_AUDIOTIME_H

/**
 * @file AudioTime.h
 * @brief Zeitbasis der Library
 */

#include <esp_timer.h>

#include <cstdint>

/**
 * @brief Millisekunden seit dem Start des Systems
 * @return Zeit in Millisekunden
 *
 * Tritt an die Stelle von Arduinos `millis()` und verhält sich genauso, einschließlich des Überlaufs nach rund 49,7 Tagen. Alle Zeitvergleiche der Library sind auf diesen Überlauf ausgelegt, weil sie als Differenz gerechnet werden.
 */
inline uint32_t audio_millis()
{
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

#endif // AUDIO_LITE_AUDIOTIME_H
