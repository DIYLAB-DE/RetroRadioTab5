#ifndef RADIO_PREFERENCES_H
#define RADIO_PREFERENCES_H
#include <stdint.h>

/** @brief Kleiner Radiodatensatz ohne Zugangsdaten und Hardwareparameter. Tab5Config sichert alles außer station im NVS, Tab5Rtc sichert station, Lautstärke, Stummschaltung, Modus und Stile im RTC-RAM. */
struct RadioPreferences
{
    static constexpr uint8_t DefaultVolume = 30; ///< Pegel nach dem Reset der Radioansicht und auf einem Gerät ohne gespeicherte Radiowerte (Original RetroRadio), beim Start begrenzt Tab5Rtc::loadRadio ihn ohne gültigen RTC-Wert auf AudioManager::DefaultVolume
    uint8_t mode = 0, styles[8]{0, 1, 1, 0, 1, 1, 1, 0}, volume = DefaultVolume; ///< Visualizermodus 0..7 (Vorgabe Analog-VU), Stil 0/1 je Modus (Analog-VU, Digital-VU, Spektrum, Wellenform, Goniometer, zentriertes Spektrum, Wasserfall, Aurora): Regenbogen überall, wo es ihn gibt (beim Goniometer nur im VU-Balken, die Figur bleibt einfarbig). Analog-VU klassisch, Wellenform überlagert, Nordlicht kühl. Der Visualizer übernimmt sie beim Start über applyPreferences, seine Membervorgaben gelten nur davor. Lautstärke 0..100
    int8_t tone[3]{10, 1, 10}, balance = 0;    ///< Klangbänder tief, mittel, hoch in dB (-12..12) mit der Reset-Vorgabe des Originals, Balance -16..16
    bool muted = false;                         ///< Stummgeschaltet
    uint8_t beforeMute = DefaultVolume;         ///< Lautstärke 1..100, auf die das Entstummen zurückkehrt
    uint8_t station = 0;                        ///< Senderindex 0..69, Speicherung ausschließlich im RTC-RAM

    /** @brief Rückkehrpegel nach einer Pegeländerung aus einem beliebigen Bedienweg. So kehrt das Entstummen aus 0 auf den zuletzt gehörten Pegel zurück. @param level Neuer Pegel 0..100. @param previous Bisheriger Rückkehrpegel. @return level, wenn er über 0 liegt, sonst previous. */
    static constexpr uint8_t returnVolume(uint8_t level, uint8_t previous)
    {
        return level ? level : previous;
    }

    /** @brief Pegel beim Entstummen aus Pegel 0. @param returnLevel Rückkehrpegel. @param quietVolume Leiser Ersatz 1..100 für einen ungültigen Rückkehrpegel. @return returnLevel im Bereich 1..100, sonst quietVolume. */
    static constexpr uint8_t unmuteVolume(uint8_t returnLevel, uint8_t quietVolume)
    {
        return returnLevel >= 1 && returnLevel <= 100 ? returnLevel : quietVolume;
    }
};
#endif
