#include "Strings_mainscreen.h"

// Wochentage und Monate selektiv aus DIYLAB-DE/RetroRadio, ea4229a34fb39faaff23619092cf09cafb8c51cf, Strings_common.cpp. GPL-3.0, siehe referenz/RetroRadio-LICENSE.
namespace
{
    constexpr const char *Texts[][2] = {
        {"Radio", "Radio"}, {"SD-Player", "SD player"}, {"Konfiguration", "Settings"}, {"Zeit noch nicht verfügbar", "Time not available yet"}, {"Zeit in Konfiguration prüfen", "Check time in Settings"}, {"WLAN in Konfiguration einrichten", "Set up Wi-Fi in Settings"}, {"WLAN-Status unbekannt", "Wi-Fi status unknown"}, {"WLAN verbindet", "Connecting to Wi-Fi"}, {"WLAN nicht verbunden", "Wi-Fi disconnected"}, {"WLAN verbunden", "Wi-Fi connected"}, {"IP nicht verfügbar", "IP unavailable"}, {"--. ---. ----", "--. ---. ----"}, {"%s., %u. %s %u", "%s., %u. %s %u"}, {"NTP", "NTP"}, {"DST", "DST"}, {"So", "Sun"}, {"Mo", "Mon"}, {"Di", "Tue"}, {"Mi", "Wed"}, {"Do", "Thu"}, {"Fr", "Fri"}, {"Sa", "Sat"}, {"Januar", "January"}, {"Februar", "February"}, {"März", "March"}, {"April", "April"}, {"Mai", "May"}, {"Juni", "June"}, {"Juli", "July"}, {"August", "August"}, {"September", "September"}, {"Oktober", "October"}, {"November", "November"}, {"Dezember", "December"}};
    static_assert(sizeof(Texts) / sizeof(Texts[0]) == static_cast<unsigned>(MainScreenKey::Count));
}

const char *I18n::mainscreen(MainScreenKey key, LangIndex language)
{
    const auto index = static_cast<unsigned>(key);
    if (index >= static_cast<unsigned>(MainScreenKey::Count))
        return "";
    return Texts[index][language == LangIndex::EN ? 1 : 0];
}
