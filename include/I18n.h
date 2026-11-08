#ifndef I18N_H
#define I18N_H
#include <Arduino.h>
#include "Strings_retroradio.h"

/** @brief Sprachspalten der selektiv übernommenen RetroRadio-Tabelle. */
enum class LangIndex : uint8_t
{
    DE = 0,   ///< Deutsch
    EN = 1,   ///< Englisch
    COUNT     ///< Anzahl der Sprachen, keine gültige Sprache
};

namespace I18n
{
    /** @brief Liefert den Radio-Text immer in Deutsch. @param key Radio-Schlüssel. @return Gültiger Flash-Text, bei unbekanntem Schlüssel leer. */
    const __FlashStringHelper *str(RetroRadioKey key);
}
#endif
