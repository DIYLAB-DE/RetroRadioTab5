#ifndef TAB5_RENDER_H
#define TAB5_RENDER_H
#include <M5GFX.h>

namespace Tab5Render
{
    constexpr int PanelWidth = 720;   ///< Pixel je Zeile des DSI-Framebuffers, das Panel steht im Hochformat
    constexpr int PanelHeight = 1280; ///< Zeilen des DSI-Framebuffers
#if defined(ARDUINO_ARCH_ESP32)

    /** @brief Bereitschaft und Zähler der PPA seit dem Start. */
    struct PpaCounters
    {
        uint32_t timeouts; ///< Übertragungen, deren PPA-Fertigmeldung nicht innerhalb der Zeitgrenze kam
        uint32_t late;     ///< Fertigmeldungen, die nach der Zeitgrenze noch kamen
        bool ready;        ///< Die PPA überträgt. false ohne PPA-Client oder nach einem Fehler der PPA, dann bleibt der gekachelte CPU-Weg
        uint32_t jobs;     ///< An die PPA übergebene Aufträge
    };

    /** @brief Bindet ausschließlich den bestätigten 720x1280-DSI-Framebuffer im Querformat an (Rotation 3, um 180 Grad gedreht Rotation 1) und richtet die PPA samt Fertigmeldung ein. Ohne PPA bleibt der gekachelte CPU-Weg. @param display Initialisiertes Display. @return false bei anderem Panel, anderer Größe oder anderer Rotation. */
    bool begin(lgfx::LGFX_Device &display);
    /** @brief Überträgt einen opaken 16-Bit-Sprite unter Beachtung des Display-Clippings, andere Sprites gehen über M5GFX. */
    void pushSprite(lgfx::LGFX_Device *display, lgfx::LGFX_Sprite &sprite, int x, int y);
    /** @brief Überträgt einen RGB565-Ausschnitt (M5GFX-Byteordnung) an die Position x/y und schreibt ihn für DSI zurück. Nur aus dem UI-Task aufrufen. Wartet höchstens 500 ms auf die Fertigmeldung der PPA. Bleibt sie aus, überträgt die CPU diese Fläche und alle weiteren, bis die verspätete Meldung kommt. @param stride Zeilenbreite der Quelle in Pixeln. */
    void pushRegion(lgfx::LGFX_Device &display, const uint16_t *source, int stride, int x, int y, int width, int height);
    /** @brief Liefert Bereitschaft und Zähler der PPA und nimmt dabei eine inzwischen eingetroffene verspätete Meldung an. Nur aus dem UI-Task aufrufen. */
    PpaCounters ppaCounters();
    /** @brief Wartet auf die Fertigmeldung des nächsten Displaybildes (on_refresh_done des DPI-Treibers). Eine schon vorliegende Meldung zählt nicht, damit der Aufrufer am Takt des Panels hängt. Nur aus dem UI-Task aufrufen. @param timeoutMs Höchstwartezeit in ms. @return false ohne Bildsignal oder nach der Zeitgrenze. Der Aufrufer zeichnet dann frei laufend weiter. */
    bool waitFrame(uint32_t timeoutMs);
    /** @brief Zeitpunkt der letzten Bildfertigmeldung auf der Basis von millis(), 0 ohne Signal. */
    uint32_t lastFrameMs();
    /** @brief Bildfertigmeldungen seit dem Start, 0 heißt, der DPI-Treiber meldet keine Bilder. */
    uint32_t frames();
    /** @brief Zeitpunkt der letzten Bildfertigmeldung in µs (untere 32 Bit von esp_timer, wie micros()), 0 ohne Signal. */
    uint32_t lastFrameUs();
    /** @brief Abstand der letzten beiden Bildfertigmeldungen in µs, also die Bildperiode des Panels, 0 vor der zweiten Meldung. */
    uint32_t framePeriodUs();
    /** @brief Liefert den gebundenen DSI-Framebuffer (PanelWidth x PanelHeight, RGB565 ohne Bytetausch) zum direkten Schreiben, wenn display gebunden ist und im Querformat steht. Sonst nullptr. Rotation 3 legt Querformat (x, y) in Panelzeile PanelHeight - 1 - x, Spalte y, Rotation 1 in Zeile x, Spalte PanelWidth - 1 - y. Nur UI-Task. Nach dem Schreiben flushFramebuffer aufrufen. */
    uint16_t *framebuffer(lgfx::LGFX_Device &display);
    /** @brief Macht direkt in den Framebuffer geschriebene Pixel eines Querformat-Rechtecks sichtbar, die Panelimplementierung schreibt die betroffenen Cachezeilen für DSI zurück. Nur UI-Task. */
    void flushFramebuffer(lgfx::LGFX_Device &display, int x, int y, int width, int height);
    /** @brief Nennt das von M5GFX erkannte Panel: Touch-Adresse 0x14 ist der GT911 des ILI9881C, 0x55 der ST-Touch. Nur der ST7121 fährt den DSI-Bus mit 900 Mbit/s. @return "ILI9881C+GT911", "ST7121", "ST7123" oder "unbekannt". */
    const char *panelName(lgfx::LGFX_Device &display);
#else
    // Hostbau ohne DSI-Framebuffer: zeilenweise über M5GFX.
    inline void pushRegion(lgfx::LGFX_Device &display, const uint16_t *source, int stride, int x, int y, int width, int height)
    {
        for (int row = 0; row < height; ++row)
            display.pushImage(x, y + row, width, 1, source + row * stride);
    }

    inline void pushSprite(lgfx::LGFX_Device *display, lgfx::LGFX_Sprite &sprite, int x, int y)
    {
        sprite.pushSprite(display, x, y);
    }

    // Ohne DSI-Framebuffer gibt es kein Bildsignal, die Nadel läuft frei.
    inline bool waitFrame(uint32_t)
    {
        return false;
    }

    inline uint32_t lastFrameMs()
    {
        return 0;
    }

    inline uint32_t frames()
    {
        return 0;
    }

    inline uint32_t lastFrameUs()
    {
        return 0;
    }

    inline uint32_t framePeriodUs()
    {
        return 0;
    }

    inline uint16_t *framebuffer(lgfx::LGFX_Device &)
    {
        return nullptr;
    }

    inline void flushFramebuffer(lgfx::LGFX_Device &, int, int, int, int)
    {
    }
#endif
}
#endif
