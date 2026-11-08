#include "Strings_settings.h"

// Texte und Zeitzonen selektiv aus DIYLAB-DE/RetroRadio,
// ea4229a34fb39faaff23619092cf09cafb8c51cf, Settings.cpp/Strings_settings.cpp.
// Die Status- und Fehlertexte für den zentralen Tab5-Dienstvertrag stammen nicht aus RetroRadio.
// Projektlizenz: referenz/RetroRadio-LICENSE (GPL-3.0).
namespace
{
    /** @brief Texte in der Reihenfolge von SettingsKey. Spalte 0 Deutsch, Spalte 1 Englisch. */
    const char *const Texts[][2] = {
        {"Sprache", "Language"},
        {"Startseite", "Home"},
        {"Abbrechen", "Cancel"},
        {"Übernehmen", "Apply"},
        {"Löschen", "Delete"},
        {"OK", "OK"},
        {"Weiter", "Next"},
        {"Zurück", "Previous"},
        {"Nicht verfügbar", "Unavailable"},
        {"Änderungen vorgemerkt", "Changes pending"},
        {"Keine Änderungen", "No changes"},
        {"Gespeichert", "Saved"},
        {"Wird ausgeführt ...", "Working ..."},
        {"Anforderung konnte nicht angenommen werden.", "Request could not be accepted."},
        {"Ungültige Eingabe", "Invalid input"},
        {"Einstellungen wurden andernorts geändert.", "Settings changed elsewhere."},
        {"Entwurf bleibt erhalten. Verwerfen lädt den neuen Stand.", "Draft retained. Discard loads the latest settings."},
        {"Speichern fehlgeschlagen. Entwurf bleibt erhalten.", "Save failed. Draft retained."},
        {"Anmeldung fehlgeschlagen", "Authentication failed"},
        {"Netz nicht erreichbar", "Network unavailable"},
        {"Keine gültige IP-Adresse erhalten", "No valid IP address received"},
        {"Zeitüberschreitung", "Timed out"},
        {"Dienst meldet einen Fehler", "Service reported an error"},
        {"Abgebrochen", "Cancelled"},
        {"Dienst ist noch nicht verfügbar.", "Service is not available yet."},
        {"Passwort", "Password"},
        {"Netzwerkname", "Hostname"},
        {"WLAN wählen", "Select Wi-Fi"},
        {"Netze werden gesucht ...", "Searching networks ..."},
        {"Keine Netze gefunden", "No networks found"},
        {"Weitere Netze vorhanden, Suche eingrenzen.", "More networks exist. Narrow the search."},
        {"verschlüsselt", "encrypted"},
        {"offen", "open"},
        {"Manuell", "Manual"},
        {"Neu suchen", "Rescan"},
        {"Dieses Profil zum Löschen vormerken?", "Mark this profile for deletion?"},
        {"Verbindung und IP-Adresse bestätigt.", "Connection and IP address confirmed."},
        {"Leer für offenes Netz, sonst 8-63 ASCII-Zeichen oder 64 Hex-Zeichen.", "Empty for open network, otherwise 8-63 ASCII or 64 hex characters."},
        {"1-32 Buchstaben/Ziffern/Bindestriche, kein Bindestrich am Rand.", "1-32 letters/digits/hyphens, no leading or trailing hyphen."},
        {"SSID: 1-32 Bytes, gültiges UTF-8.", "SSID: 1-32 bytes, valid UTF-8."},
        {"Servername oder IPv4-Adresse mit höchstens 63 Zeichen.", "Server name or IPv4 address, at most 63 characters."},
        {"Anzeigen", "Show"},
        {"Verbergen", "Hide"},
        {"Leerzeichen", "Space"},
        {"Eingabegrenze erreicht", "Input limit reached"},
        {"Änderungen erst mit Übernehmen bestätigen.", "Confirm edits with Apply."},
        {"Zeitserver", "Time server"},
        {"Zeit setzen", "Set time"},
        {"Gültiges Datum und Uhrzeit eingeben (2000-2099).", "Enter a valid date and time (2000-2099)."},
        {"Zeit wurde bestätigt.", "Time setting confirmed."},
        {"Alle aktiv", "All active"},
        {"PIN-Änderung bestätigt.", "PIN change confirmed."},
        {"Neu starten", "Restart"},
        {"Neuer gespeicherter Stand vorhanden", "New saved revision available"},
        {"Stumm angefordert", "Mute requested"},
        {"Gespeichert. Neustart erforderlich.", "Saved. Restart required."},
        {"Stumm bestätigt", "Mute confirmed"},
        {"WLAN-Name (SSID)", "Wi-Fi name (SSID)"},
    };
    static_assert(sizeof(Texts) / sizeof(Texts[0]) == static_cast<size_t>(SettingsKey::Count), "Texttabelle unvollständig");
    const SettingsTimezone Zones[] = {

        {"Europe/Berlin", "CET-1CEST,M3.5.0,M10.5.0/3"},
        {"Europe/Vienna", "CET-1CEST,M3.5.0,M10.5.0/3"},
        {"Europe/Zurich", "CET-1CEST,M3.5.0,M10.5.0/3"},
        {"Europe/Amsterdam", "CET-1CEST,M3.5.0,M10.5.0/3"},
        {"Europe/Brussels", "CET-1CEST,M3.5.0,M10.5.0/3"},
        {"Europe/Paris", "CET-1CEST,M3.5.0,M10.5.0/3"},
        {"Europe/Rome", "CET-1CEST,M3.5.0,M10.5.0/3"},
        {"Europe/Madrid", "CET-1CEST,M3.5.0,M10.5.0/3"},
        {"Europe/London", "GMT0BST,M3.5.0/1,M10.5.0"},
        {"Europe/Dublin", "IST-1GMT0,M10.5.0,M3.5.0/1"},
        {"Europe/Lisbon", "WET0WEST,M3.5.0/1,M10.5.0"},
        {"Europe/Athens", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
        {"Europe/Helsinki", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
        {"Europe/Stockholm", "CET-1CEST,M3.5.0,M10.5.0/3"},
        {"Europe/Oslo", "CET-1CEST,M3.5.0,M10.5.0/3"},
        {"Europe/Copenhagen", "CET-1CEST,M3.5.0,M10.5.0/3"},
        {"Europe/Warsaw", "CET-1CEST,M3.5.0,M10.5.0/3"},
        {"Europe/Prague", "CET-1CEST,M3.5.0,M10.5.0/3"},
        {"Europe/Budapest", "CET-1CEST,M3.5.0,M10.5.0/3"},
        {"Europe/Moscow", "MSK-3"},
        {"Europe/Kiev", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
        {"Europe/Istanbul", "TRT-3"},
        {"America/New_York", "EST5EDT,M3.2.0,M11.1.0"},
        {"America/Chicago", "CST6CDT,M3.2.0,M11.1.0"},
        {"America/Denver", "MST7MDT,M3.2.0,M11.1.0"},
        {"America/Los_Angeles", "PST8PDT,M3.2.0,M11.1.0"},
        {"America/Anchorage", "AKST9AKDT,M3.2.0,M11.1.0"},
        {"America/Phoenix", "MST7"},
        {"America/Toronto", "EST5EDT,M3.2.0,M11.1.0"},
        {"America/Vancouver", "PST8PDT,M3.2.0,M11.1.0"},
        {"America/Mexico_City", "CST6"},
        {"America/Sao_Paulo", "<-03>3"},
        {"America/Buenos_Aires", "<-03>3"},
        {"Asia/Tokyo", "JST-9"},
        {"Asia/Seoul", "KST-9"},
        {"Asia/Shanghai", "CST-8"},
        {"Asia/Hong_Kong", "HKT-8"},
        {"Asia/Singapore", "SGT-8"},
        {"Asia/Bangkok", "ICT-7"},
        {"Asia/Dubai", "GST-4"},
        {"Asia/Kolkata", "IST-5:30"},
        {"Asia/Jakarta", "WIB-7"},
        {"Australia/Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
        {"Australia/Melbourne", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
        {"Australia/Perth", "AWST-8"},
        {"Pacific/Auckland", "NZST-12NZDT,M9.5.0,M4.1.0/3"},
        {"Pacific/Honolulu", "HST10"},
        {"Africa/Cairo", "EET-2EEST,M4.5.5/0,M10.5.4/24"},
        {"Africa/Johannesburg", "SAST-2"},
        {"UTC", "UTC0"},
    };
}

const char *settingsText(SettingsKey key, LangIndex language)
{
    const size_t index = static_cast<size_t>(key);
    return index < static_cast<size_t>(SettingsKey::Count) ? Texts[index][language == LangIndex::EN ? 1 : 0] : "";
}

size_t settingsTimezoneCount()
{
    return sizeof(Zones) / sizeof(Zones[0]);
}

const SettingsTimezone *settingsTimezone(size_t index)
{
    return index < settingsTimezoneCount() ? &Zones[index] : nullptr;
}
