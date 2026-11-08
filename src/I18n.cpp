#include "I18n.h"

// Radio-Lookup aus RetroRadio/I18n.cpp, Zusatzmodule und globale Modulverwaltung entfallen.
extern const __FlashStringHelper *i18n_getRetroRadio(RetroRadioKey key, LangIndex lang);

const __FlashStringHelper *I18n::str(RetroRadioKey key)
{
    return i18n_getRetroRadio(key, LangIndex::DE);
}
