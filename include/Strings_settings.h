#ifndef STRINGS_SETTINGS_H
#define STRINGS_SETTINGS_H
#include "I18n.h"
#include <stddef.h>

/** @brief Selektive Konfigurationstexte in Deutsch und Englisch. Die Reihenfolge entspricht der Tabelle in Strings_settings.cpp, Count steht am Ende. */
enum class SettingsKey : uint16_t
{
    Language,
    Home,
    Cancel,
    Apply,
    Delete,
    Ok,
    Next,
    Previous,
    Unavailable,
    NotSaved,
    Unchanged,
    Saved,
    Working,
    QueueBusy,
    Invalid,
    Conflict,
    ConflictHelp,
    StorageError,
    AuthError,
    NoNetwork,
    NoDhcp,
    Timeout,
    ServiceError,
    Cancelled,
    Missing,
    Password,
    Hostname,
    ChooseWifi,
    Scanning,
    NoResults,
    MoreResults,
    Encrypted,
    Open,
    Manual,
    Refresh,
    DeleteProfile,
    TestSuccess,
    InvalidPassword,
    InvalidHostname,
    InvalidSsid,
    InvalidNtp,
    Show,
    Hide,
    Space,
    InputFull,
    InputHint,
    Ntp,
    SetTime,
    BadDate,
    TimeSet,
    AllActive,
    PinDone,
    Restart,
    RevisionChanged,
    MutedRequested,
    RestartRequired,
    MuteConfirmed,
    SsidName,
    Count
};
/** @brief Liefert einen statischen Text ohne globale Sprachumschaltung. @param key Textschlüssel. @param language Übergebene Sprache. @return Nicht-null Text. */
const char *settingsText(SettingsKey key, LangIndex language);

/** @brief Region/POSIX-Paar ohne RTC-Abhängigkeit. */
struct SettingsTimezone
{
    const char *region; ///< Name der Region, z. B. "Europe/Berlin"
    const char *posix;  ///< POSIX-Zeitzonenregel für setenv("TZ")
};

/** @brief Liefert die Zahl auswählbarer Zeitzonen. @return Anzahl. */
size_t settingsTimezoneCount();
/** @brief Liest ein statisches Zeitzonenpaar. @param index Index. @return Paar oder nullptr. */
const SettingsTimezone *settingsTimezone(size_t index);
#endif
