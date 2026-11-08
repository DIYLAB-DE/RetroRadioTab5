#ifndef STRINGS_RETRORADIO_H
#define STRINGS_RETRORADIO_H

#include <stdint.h>

/**
 * @brief Schlüssel für anwender-sichtbare Texte im RetroRadio-Modul
 *
 * Enthält Dialog-Titel, Dialog-Nachrichten und Control-Labels des Radio-Hauptscreens sowie die Labels der Visualizer-Sub-Komponente (Goniometer-Info). Beim Hinzufügen eines Keys auch die Pointer-Tabelle in `Strings_retroradio.cpp` an derselben Stelle erweitern. Der `static_assert` dort prüft nur die Anzahl, nicht die Reihenfolge.
 *
 * Sub-Komponenten-Präfixe:
 *   RETRO_      Hauptscreen (Dialoge, Knob, Equalizer)
 *   RETRO_VIS_  Visualizer (Goniometer-Info-Sprite)
 *   RADIO_      Infozeile des Radios
 */
enum class RetroRadioKey : uint16_t
{
    // Dialog-Texte (Hauptscreen)
    RETRO_PRESET_DLG_TITLE,    ///< "FAVORITEN" / "FAVOURITES"
    RETRO_RESET_DLG_TITLE,     ///< "ZURÜCKSETZEN?" / "RESET?"
    RETRO_RESET_DLG_MESSAGE,   ///< Reset-Bestätigungs-Text (Lautstärke, Ton, Visualisierungen)
    RETRO_SLEEP_DLG_TITLE,     ///< "SLEEP-TIMER" / "SLEEP TIMER"
    RETRO_SLEEP_DLG_MESSAGE,   ///< "Radio verlassen nach:" / "Quit radio after:"

    // Control-Labels (Hauptscreen)
    RETRO_KNOB_LABEL,          ///< "Lautstärke" / "Volume"
    RETRO_TRACK_BALANCE,       ///< "Balance" / "Balance"
    RETRO_TRACK_LOW,           ///< "Tiefen" / "Bass"
    RETRO_TRACK_MID,           ///< "Mitten" / "Mid"
    RETRO_TRACK_HIGH,          ///< "Höhen" / "Treble"

    // Visualizer (Goniometer-Info-Sprite)
    RETRO_VIS_GAIN_LABEL,      ///< "Verstärkungsfaktor" / "Gain factor"
    RETRO_VIS_GONIO_LEFT,      ///< "links" / "left"
    RETRO_VIS_GONIO_RIGHT,     ///< "rechts" / "right"

    // Infozeile des Radios
    RADIO_ERROR,         ///< "Stream unterbrochen" / "Connection interrupted. Start again."

    RETRORADIO_KEY_COUNT       ///< Anzahl der Texte, kein gültiger Schlüssel
};

enum class LangIndex : uint8_t;
class __FlashStringHelper;
/** @brief Liefert einen Radiotext in der übergebenen Sprache. @param key Textschlüssel. @param lang Sprache. Eine ungültige fällt auf Deutsch zurück. @return Flash-Text, bei ungültigem Schlüssel leer. */
const __FlashStringHelper *i18n_getRetroRadio(RetroRadioKey key, LangIndex lang);

#endif // STRINGS_RETRORADIO_H
