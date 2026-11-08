#ifndef STRINGS_MAINSCREEN_H
#define STRINGS_MAINSCREEN_H

#include "I18n.h"

/** @brief Texte des MainScreen, ohne eigene globale Sprachauswahl. */
enum class MainScreenKey : uint8_t
{
    Radio,           ///< Beschriftung des Radio-Symbols
    SdPlayer,        ///< Beschriftung des SD-Player-Symbols
    Settings,        ///< Beschriftung des Konfigurationssymbols
    TimeUnavailable,
    CheckTime,
    ConfigureWifi,
    WifiUnavailable,
    WifiConnecting,
    WifiDisconnected,
    WifiConnected,
    IpUnavailable,
    DateUnknown,
    DateFormat,      ///< printf-Format: Wochentag, Tag, Monat, Jahr
    Ntp,
    Dst,
    Sunday,
    Monday,
    Tuesday,
    Wednesday,
    Thursday,
    Friday,
    Saturday,
    January,
    February,
    March,
    April,
    May,
    June,
    July,
    August,
    September,
    October,
    November,
    December,
    Count            ///< Anzahl der Texte, kein gültiger Schlüssel
};

namespace I18n
{
    /** @brief Liefert einen MainScreen-Text ohne Allokation. @param key Textschlüssel. @param language Gewünschte Sprache, ungültig fällt auf Deutsch zurück. @return Dauerhaft gültiger Text, bei unbekanntem Schlüssel leer. */
    const char *mainscreen(MainScreenKey key, LangIndex language);
}

#endif
