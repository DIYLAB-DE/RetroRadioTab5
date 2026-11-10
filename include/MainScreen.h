#ifndef MAINSCREEN_H
#define MAINSCREEN_H

#include <M5GFX.h>
#include "Strings_mainscreen.h"
#include "BatteryIcon.h"

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
        BatteryIcon::State battery; ///< Akkuanzeige rechts auf der Datumszeile, ohne erkannten Akku bleibt die Fläche leer.
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
    /** @brief Wählt die Gangart des Sekundenzeigers und zeichnet die Uhr beim nächsten update neu. @param sweep true schleichend im Takt des Displays, false in Sekundenschritten. Ohne Bildsignal des Displays oder ohne Ausschnittpuffer bleibt es bei Sekundenschritten. */
    void setSecondSweep(bool sweep);
    /** @brief Zeichnet nur aktive, geänderte Bereiche mit vorbereiteten Puffern. Keine Netz- oder Dateiabfragen. Schleicht der Sekundenzeiger, liest es je Displaybild die Systemzeit. @return true bei einer Bildaktualisierung für die zentrale Zeitmessung. */
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
    bool m_sweep = true; ///< Sekundenzeiger schleicht, sonst Sekundenschritte
    uint8_t m_buttonDirty = 7; ///< Bitmaske der neu zu zeichnenden Symbole, Bit 0 = Radio
    lgfx::LGFX_Sprite m_battery; ///< Akkusymbol mit Prozentzahl, erst mit dem ersten erkannten Akku angelegt
    bool m_batteryDirty = true, m_batteryShown = false, m_batteryFailed = false; ///< Akkuanzeige neu zeichnen, Display zeigt ein Symbol, Sprite ließ sich nicht anlegen
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
    /** @brief Zeichnet das Zifferblatt ohne Zeiger: Markierungen, NTP/DST, WLAN-Balken, SSID und IP. */
    void drawDial(lgfx::LGFX_Sprite *buffer, int centerX, int centerY, float radius);
    /** @brief Zeichnet die WLAN-Balken. @return Unterkante der Balken als Bezug für SSID und IP. */
    int drawSignalBars(lgfx::LGFX_Sprite *buffer, int centerX, int centerY, float radius);
    /** @brief Liefert die Zahl der leuchtenden WLAN-Balken. */
    static int signalBars(const Status &status);
#if defined(ARDUINO_ARCH_ESP32)
    /** @brief Zeigerstellung in Grad im Uhrzeigersinn ab 12 Uhr. */
    struct Hands
    {
        float hour = 0, minute = 0, second = 0;
    };

    /** @brief Rechteck im Uhrsprite in Pixeln. */
    struct Box
    {
        int x = 0, y = 0, w = 0, h = 0;
    };

    /** @brief Sprite mit Zugang zur geglätteten Keillinie von M5GFX in Kommakoordinaten. Die öffentlichen Zeichenfunktionen nehmen ganze Pixel, ein schleichender Zeiger braucht Teilpixel. */
    class FineSprite : public lgfx::LGFX_Sprite
    {
    public:
        /** @brief Zeichnet eine geglättete Keillinie von (ax, ay) mit Halbbreite ar nach (bx, by) mit Halbbreite br in RGB565-Farbe color. */
        void wedge(float ax, float ay, float bx, float by, float ar, float br, uint16_t color)
        {
            draw_wedgeline(ax, ay, bx, by, ar, br, lgfx::convert_to_rgb888(color));
        }
    };

    lgfx::LGFX_Sprite m_dial;         ///< Zifferblatt ohne Zeiger als Hintergrund der Zeiger, SPRITE_W x DATE_Y
    FineSprite m_view;                ///< Zeichenfläche der Zeiger auf fremdem Speicher (m_viewPixels oder m_clock), Größe je Aufruf über setBuffer
    uint16_t *m_viewPixels = nullptr; ///< Ausschnittpuffer der Zeiger im PSRAM für höchstens ViewPixels Pixel
    Hands m_drawn{};                  ///< Zuletzt übertragene Zeigerstellung
    uint32_t m_lastFrame = 0;         ///< Bildzähler des Displays beim letzten Zeigerschritt
    bool m_composed = false;          ///< Display zeigt das aktuelle Zifferblatt mit Zeigern, danach genügen Ausschnitte
    bool m_dialDirty = true;          ///< Zifferblatt ohne Zeiger muss neu gezeichnet werden
    bool m_barsDirty = false;         ///< Nur die Zahl der WLAN-Balken hat sich geändert

    /** @brief Zeichnet die Uhr im Takt des Displays: Zifferblatt und Datum nur bei Änderung, die Zeiger je Displaybild nur im Rechteck um alte und neue Stellung. Dazu die Symbole. @return true, wenn etwas übertragen wurde. */
    bool updateSweep();
    /** @brief Liest die Systemzeit und liefert die Zeigerstellung mit schleichendem Sekundenzeiger. Minuten- und Stundenzeiger rücken wie im Original je Sekunde bzw. Minute. */
    static Hands currentHands();
    /** @brief Setzt Zifferblatt, Zeiger und bei Bedarf das Datum im ganzen Uhrsprite zusammen und überträgt es. */
    void composeClock(const Hands &hands);
    /** @brief Setzt ein Rechteck aus Zifferblatt und Zeigern zusammen und überträgt nur dieses. @return false, wenn es nicht in den Ausschnittpuffer passt. */
    bool pushBox(const Box &box, const Hands &hands);
    /** @brief Zeichnet die drei Zeiger samt Achse in m_view, dessen Ursprung im Uhrsprite bei (originX, originY) liegt. */
    void drawHands(int originX, int originY, const Hands &hands);
    /** @brief Liefert das Rechteck im Zifferblatt, das ein Zeiger bei angle mit Rückstück back, Länge length und Halbbreite width samt Glättungssaum belegt. */
    static Box handBox(float angle, float back, float length, float width);
#endif
    /** @brief Zeichnet die gedrehten Statusbeschriftungen einmalig vor. */
    void renderLabelCaches();
    /** @brief Zeichnet die Datumszeile des Uhrsprites bei Tages-, Sprach- oder Zeitgültigkeitswechsel neu. */
    void renderDate();
    /** @brief Zeichnet Akkusymbol und Prozentzahl rechts auf die Höhe der Datumszeile. Ohne Akku nur, um ein zuvor gezeigtes Symbol zu entfernen. @return true, wenn etwas übertragen wurde. */
    bool drawBattery();
    /** @brief Füllt beim Neuaufbau nur die Flächen außerhalb von Uhrsprite und Symbolen mit Gem.Grund, damit kein Zwischenbild entsteht. */
    void clearBackground();
    /** @brief Zeichnet ein Symbol mit Rahmen. Gedrückt mit invertiertem Rahmen, das Radio-Symbol ohne WLAN-Verbindung grau. */
    void drawButton(unsigned index);
};

#endif
