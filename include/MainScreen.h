#ifndef MAINSCREEN_H
#define MAINSCREEN_H

#include <M5GFX.h>
#include "Strings_mainscreen.h"

/** @brief Startseite nativ in 1280x720 (Maße in MainScreenTab5Layout.h) als reiner Verbraucher zentraler Zeit-, WLAN- und Spracheingaben. Alle Aufrufe erfolgen serialisiert aus dem UI-Task. */
class MainScreen
{
public:
    /** @brief Navigationswunsch aus touch(). */
    enum class Action : uint8_t
    {
        None,
        Radio,
        SdPlayer,
        Settings
    };
    /** @brief Phase eines Kontakts. */
    enum class TouchPhase : uint8_t
    {
        Press,
        Move,
        Release,
        Cancel
    };
    /** @brief Verbindungszustand der WLAN-Station. */
    enum class WifiState : uint8_t
    {
        Unknown,
        Disconnected,
        Connecting,
        Connected
    };

    /** @brief Vollständig kopiertes Statusabbild. Revision pro Snapshot erhöhen, Überlauf ist bei Abständen kleiner als 2^31 unterstützt. */
    struct Status
    {
        uint32_t revision = 0;         ///< Zählt jeden Snapshot hoch
        uint32_t timeZoneRevision = 0; ///< Ändert sich bei neuer Zeitzone oder neuen NTP-Einstellungen
        LangIndex language = LangIndex::DE; ///< Sprache der Texte
        bool timeValid = false;        ///< Datum und Uhrzeit sind gültig
        uint16_t year = 0; ///< Gregorianisches Jahr 2024..9999, gültig nur mit timeValid.
        uint8_t month = 0, day = 0, weekday = 0; ///< Monat 1..12, Tag 1..31, Wochentag 0=Sonntag..6=Samstag.
        uint8_t hour = 0, minute = 0, second = 0; ///< Lokale Uhrzeit
        bool dstActive = false;                   ///< Sommerzeit aktiv
        bool ntpSynced = false; ///< Zentral bestätigter Abgleich in dieser Laufzeit, unabhängig von WLAN-Verbindung.
        WifiState wifi = WifiState::Unknown; ///< Verbindungszustand
        bool signalValid = false;            ///< Signalstärke ist gültig
        uint8_t signalPercent = 0; ///< Bereits zentral normierter Wert 0..100.
        bool ipv4Valid = false; ///< IPv4-Adresse ist gültig
        uint8_t ipv4[4]{};      ///< Eigene IPv4-Adresse
        char ssid[33]{}; ///< Höchstens 32 SSID-Bytes, ungültiges UTF-8 wird für die Anzeige ersetzt.
    };

    /** @brief Erstellt die noch inaktive Ansicht ohne Hardware-/Dienstzugriff. */
    MainScreen() = default;
    /** @brief Gibt nur eigene Grafikressourcen frei, die Displayreferenz muss bis dahin leben. */
    ~MainScreen();
    MainScreen(const MainScreen &) = delete;
    MainScreen &operator=(const MainScreen &) = delete;

    /** @brief Bereitet Puffer, drei Symbole und Schriften einmalig vor. Das Thema muss vorher gesetzt sein, weil Symbole und Beschriftungen seine Farben einbrennen. @param display Initialisiertes Display mit logischen 1280x720 Pixeln. @return false bei falscher Größe oder Ressourcenfehler, ohne Teildarstellung. Wiederholung mit derselben Referenz ist nach Erfolg ohne Allokation möglich. */
    bool begin(lgfx::LGFX_Device &display);
    /** @brief Aktiviert die Ansicht und fordert eine vollständige Zeichnung beim nächsten update an. Kein Audio-/Versorgungszugriff. */
    void show();
    /** @brief Deaktiviert Anzeige und Eingabe, verwirft einen laufenden Kontakt und behält die vorbereiteten Puffer. */
    void hide();
    /** @brief Invalidiert nach überdeckendem Modal die ganze Ansicht und verwirft einen begonnenen Kontakt. */
    void invalidate();
    /** @brief Kopiert ein neues Statusabbild und markiert betroffene Bereiche. @param status Zentraler Snapshot. Keine Zeiger bleiben erhalten. @return true bei Übernahme, false bei gleicher/älterer Revision. Unplausible Zeitdaten werden sichtbar ungültig. */
    bool setStatus(const Status &status);
    /** @brief Zeichnet nur aktive, geänderte Bereiche mit vorbereiteten Puffern. Keine Uhr-/Netz-/Dateiabfragen. @return true bei einer Bildaktualisierung für die zentrale Zeitmessung. */
    bool update();
    /** @brief Verarbeitet genau einen zentral zugeordneten Kontakt. @param phase Press/Move/Release/Cancel. @param x Physische X-Koordinate. @param y Physische Y-Koordinate. @return Navigation beim ersten Press im aktivierten Symbol, bis Release keine weitere Aktion. Außerhalb der Symbole keine Navigation, das Radio-Symbol reagiert nur bei bestehender WLAN-Verbindung. */
    Action touch(TouchPhase phase, int32_t x, int32_t y);

    /** @brief Meldet erfolgreiche Ressourcenvorbereitung. @return true nach erfolgreichem begin. */
    bool isReady() const
    {
        return m_ready;
    }

private:
    lgfx::LGFX_Device *m_display = nullptr;
    lgfx::LGFX_Sprite m_clock, m_labelNtp, m_labelDst; ///< Uhrsprite einschließlich Datumszeile, vorgezeichnete NTP- und DST-Beschriftung
    lgfx::LGFX_Sprite m_buttons[3];                    ///< Je ein Sprite für Radio, SD-Player und Konfiguration
    lgfx::PointerWrapper m_largeData, m_smallData, m_tinyData;
    lgfx::VLWfont m_largeFont, m_smallFont, m_tinyFont;
    uint16_t *m_icons[3]{}; ///< Gezeichnete Symbole auf Main.TasteFlaeche, mit free() freizugeben
    Status m_status{};
    bool m_hasStatus = false, m_ready = false, m_visible = false;
    bool m_fullDirty = true, m_clockDirty = true, m_dateDirty = true;
    uint8_t m_buttonDirty = 7; ///< Bitmaske der neu zu zeichnenden Symbole, Bit 0 = Radio
    bool m_contact = false, m_touchProcessed = false;
    int8_t m_pressed = -1; ///< Index des gedrückten Symbols, sonst -1

    /** @brief Gibt eigene Sprites, Schriftindizes und Symbole frei. */
    void release();
    /** @brief Reserviert einen festen RGB565-Sprite im PSRAM. @return false bei Speichermangel. */
    bool prepare(lgfx::LGFX_Sprite &sprite, int width, int height);
    /** @brief Prüft Kalenderfelder ohne eigene Systemzeitverwaltung. */
    static bool validTime(const Status &status);
    /** @brief Bereinigt begrenzte SSID-Daten ohne unvollständige UTF-8-Zeichen. */
    static void copySsid(char (&out)[33], const char (&input)[33]);
    /** @brief Kürzt SSID an UTF-8-Grenzen mit dem Auslassungszeichen auf die vorhandene Breite. */
    static void fitText(lgfx::LGFX_Sprite &sprite, char *text, size_t capacity, int maxWidth);
    /** @brief Liefert den Text aus der zentral übernommenen Sprachtabelle. */
    const char *text(MainScreenKey key) const;
    /** @brief Liefert den Index (0 Radio, 1 SD-Player, 2 Konfiguration) des Symbols an der physischen Position, sonst -1. Die Ränder zählen zur Trefferfläche. */
    static int8_t buttonAt(int32_t x, int32_t y);
    /** @brief Beendet den Kontakt einschließlich seiner optischen Rückmeldung. */
    void cancelTouch();
    /** @brief Zeichnet Zifferblatt, Zeiger und Status mit zentralem Zeit-/WLAN-Snapshot oberhalb der Datumszeile. */
    void drawAnalogClock(lgfx::LGFX_Sprite *buffer, int centerX, int centerY, float radius);
    /** @brief Zeichnet die gedrehten Statusbeschriftungen einmalig vor. */
    void renderLabelCaches();
    /** @brief Zeichnet die Datumszeile des Uhrsprites bei Tages-, Sprach- oder Zeitgültigkeitswechsel neu. */
    void renderDate();
    /** @brief Füllt beim Neuaufbau nur die Flächen außerhalb von Uhrsprite und Symbolen mit Gem.Grund, damit kein Zwischenbild entsteht. */
    void clearBackground();
    /** @brief Zeichnet ein Symbol mit Rahmen. Gedrückt mit invertiertem Rahmen, das Radio-Symbol ohne WLAN-Verbindung grau. */
    void drawButton(unsigned index);
};

#endif
