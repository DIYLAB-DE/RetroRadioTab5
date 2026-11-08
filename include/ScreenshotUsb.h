#ifndef SCREENSHOT_USB_H
#define SCREENSHOT_USB_H

#include <M5GFX.h>

/** @brief Überträgt eine Displayaufnahme aus dem gemeinsamen Aufnahmeplatz als geprüfte RGB565-Lauflängen über USB. */
class ScreenshotUsb
{
public:
    /** @brief Bindet den gemeinsamen Aufnahmeplatz an, den auch der Web-Screenshot nutzt. @param pixels 1280x720 RGB565-Pixel, gehört dem Aufrufer und lebt bis zum Ende. @return true bei vorhandenem Platz. */
    bool begin(uint16_t *pixels);
    /** @brief Liest das Display im UI-Task in den Aufnahmeplatz. @param display Aktuelles 1280x720-Display. */
    void capture(lgfx::LGFX_Device &display);
    /** @brief Überträgt höchstens vier Pakete oder 2 ms lang, solange im USB-Ausgabepuffer Platz für ein ganzes Paket ist. Bricht eine Übertragung nach 120 s oder bei einem Schreibfehler ab. Aus dem UI-Task aufrufen. */
    void poll();

    /** @brief Bricht ausschließlich die laufende Bildübertragung ab. */
    void cancel()
    {
        m_active = false;
    }

    /** @brief Meldet eine laufende Übertragung, während der der Aufnahmeplatz nicht überschrieben werden darf. @return true bis zum Ende oder Abbruch. */
    bool active() const
    {
        return m_active;
    }

private:
    static constexpr uint32_t Width = 1280, Height = 720, Pixels = Width * Height;
    uint16_t *m_pixels = nullptr;                      ///< Aufnahmeplatz, gehört dem Aufrufer
    uint32_t m_id = 0, m_position = 0, m_started = 0;  ///< Aufnahmenummer, nächstes Pixel der Übertragung, Startzeit in ms
    bool m_active = false, m_header = false;           ///< Übertragung läuft, Kopfzeile fehlt noch
};

#endif
