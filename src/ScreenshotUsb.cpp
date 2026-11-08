#include "ScreenshotUsb.h"
#include <Arduino.h>
#include <esp_rom_crc.h>

bool ScreenshotUsb::begin(uint16_t *pixels)
{
    m_pixels = pixels;
    return m_pixels != nullptr;
}

void ScreenshotUsb::capture(lgfx::LGFX_Device &display)
{
    if (m_active)
    {
        Serial.println("\nSS1 ERROR BUSY");
        return;
    }
    if (!m_pixels || display.width() != Width || display.height() != Height)
    {
        Serial.println("\nSS1 ERROR BUFFER");
        return;
    }
    // Expliziter Pixeltyp: RGB565-Werte ohne die Bytevertauschung des uint16_t-Overloads.
    display.readRect(0, 0, Width, Height, reinterpret_cast<lgfx::rgb565_t *>(m_pixels));
    ++m_id;
    m_position = 0;
    m_started = millis();
    m_header = m_active = true;
}

void ScreenshotUsb::poll()
{
    if (!m_active)
        return;
    if (millis() - m_started > 120000)
    {
        m_active = false;
        return;
    }
    const uint32_t started = micros();
    for (unsigned packet = 0; packet < 4 && micros() - started < 2000; ++packet)
    {
        // Ein kompletter Datensatz passt in den beim Start reservierten USB-Puffer.
        if (Serial.availableForWrite() < 1200)
            return;
        char line[1200];
        int length = 0;
        if (m_header)
        {
            length = snprintf(line, sizeof(line), "\nSS1 BEGIN %lu %lu %lu RGB565LE_RLE\n", static_cast<unsigned long>(m_id), static_cast<unsigned long>(Width), static_cast<unsigned long>(Height));
            m_header = false;
        }
        else if (m_position == Pixels)
        {
            length = snprintf(line, sizeof(line), "\nSS1 END %lu %lu\n", static_cast<unsigned long>(m_id), static_cast<unsigned long>(Pixels));
            m_active = false;
        }
        else
        {
            uint8_t runs[512];
            size_t used = 0;
            const uint32_t first = m_position;
            while (used < sizeof(runs) && m_position < Pixels)
            {
                const uint16_t color = m_pixels[m_position];
                uint16_t count = 1;
                while (count < UINT16_MAX && m_position + count < Pixels && m_pixels[m_position + count] == color)
                    ++count;
                runs[used++] = count & 0xff;
                runs[used++] = count >> 8;
                runs[used++] = color & 0xff;
                runs[used++] = color >> 8;
                m_position += count;
            }
            const auto crc = esp_rom_crc32_le(0, runs, used);
            length = snprintf(line, sizeof(line), "\nSS1 DATA %lu %lu %08lx ", static_cast<unsigned long>(m_id), static_cast<unsigned long>(first), static_cast<unsigned long>(crc));
            constexpr char Hex[] = "0123456789abcdef";
            for (size_t i = 0; i < used; ++i)
            {
                line[length++] = Hex[runs[i] >> 4];
                line[length++] = Hex[runs[i] & 15];
            }
            line[length++] = '\n';
        }
        if (Serial.write(reinterpret_cast<const uint8_t *>(line), length) != static_cast<size_t>(length))
            m_active = false;
        if (!m_active)
            return;
    }
}
