#include "Strings_sdplayer.h"

// Texte der Titelliste, des Dateidialogs und der Dialoge selektiv aus DIYLAB-DE/RetroRadio, ea4229a34fb39faaff23619092cf09cafb8c51cf, SdUiStrings.h/SdUiStrings.cpp. GPL-3.0, siehe referenz/RetroRadio-LICENSE.
// Die Status- und Fehlertexte für den zentralen Tab5-Dienstvertrag stammen nicht aus RetroRadio.
namespace
{
    /** @brief Texte in der Reihenfolge von SdPlayerKey. Spalte 0 Deutsch, Spalte 1 Englisch. */
    constexpr const char *Texts[][2] = {
        {"Bereit", "Ready"},
        {"SD-Karte nicht verfügbar", "SD card unavailable"},
        {"SD-Karte wird geprüft", "Checking SD card"},
        {"SD-Karte nicht lesbar", "SD card unreadable"},
        {"Keine Titel", "No tracks"},
        {"Kein Cover", "No cover"},
        {"Cover wird geladen", "Loading cover"},
        {"Cover nicht lesbar", "Cover unreadable"},
        {"Titel wird gestartet", "Starting track"},
        {"Datei nicht lesbar", "File unreadable"},
        {"Aktion fehlgeschlagen", "Action failed"},
        {"Anforderung nicht angenommen", "Request not accepted"},
        {"Auswahlgrenze erreicht", "Selection limit reached"},
        {"Dateipfad ungültig oder zu lang", "Invalid or overlong file path"},
        {"Verzeichnis wird gelesen", "Reading directory"},
        {"Auswahl wird gesammelt", "Collecting selection"},
        {"Keine unterstützten Dateien", "No supported files"},
        {"Verzeichnis nicht lesbar", "Directory unreadable"},
        {"Markieren und Hinzufügen wählen", "Select items, then choose Add"},
        {"Funktion noch nicht verfügbar", "Function not available yet"},
        {"Titel hinzugefügt", "Tracks added"},
        {"Titel bereits in der Liste", "Tracks already listed"},
        {"Übersprungen: %u (Pfad zu lang oder zu tief)", "Skipped: %u (path too long or too deep)"},
        {"Tippe auf das Ordnersymbol und", "Tap the folder icon and pick"},
        {"wähle Ordner oder Titel", "folders or tracks"},
        {"keine Wiedergabe", "no playback"},
        {"Playlist beendet", "Playlist ended"},
        {"Album unbekannt", "Unknown album"},
        {"Track: %d/%d", "Track: %d/%d"},
        {"SD: %d,%d GB frei von %d,%d GB", "SD: %d.%d GB free of %d.%d GB"},
        {"Keine SD-Karte", "No SD card"},
        {"%d Titel | %s", "%d tracks | %s"},
        {"1 Titel | %s", "1 track | %s"},
        {"Titel über das Ordnersymbol", "Add tracks via folder icon"},
        {"LISTE LEEREN?", "CLEAR LIST?"},
        {"Die gesamte Abspielliste wird gelöscht.", "The whole playlist will be removed."},
        {"ZURÜCKSETZEN?", "RESET?"},
        {"Die Lautstärke, der Ton und die Visualisierungen werden zurückgesetzt.", "Volume, audio and visualisations will be reset."},
        {"SLEEP-TIMER", "SLEEP TIMER"},
        {"Player verlassen nach:", "Quit player after:"},
        {"Musikdatei(en) auswählen:", "Select music file(s):"},
        {"Dateiname", "File name"},
        {"Typ", "Type"},
        {"Größe", "Size"},
        {"Verzeichnis ist leer", "Directory is empty"},
        {"%d Dateien, %d Ordner ges.", "%d files, %d folders total"},
        {"%d Ordner ausgewählt", "%d folders selected"},
        {"%s in %d Dateien ausgew.", "%s in %d files selected"},
        {"- nichts ausgewählt -", "- nothing selected -"},
    };
    static_assert(sizeof(Texts) / sizeof(Texts[0]) == static_cast<unsigned>(SdPlayerKey::Count), "SD-Sprachtabelle unvollständig");
}

const char *I18n::sdPlayer(SdPlayerKey key, LangIndex language)
{
    const auto index = static_cast<unsigned>(key);
    return index < static_cast<unsigned>(SdPlayerKey::Count) ? Texts[index][language == LangIndex::EN ? 1 : 0] : "";
}
