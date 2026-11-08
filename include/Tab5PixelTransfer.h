#ifndef TAB5_PIXEL_TRANSFER_H
#define TAB5_PIXEL_TRANSFER_H
#include <cstdint>

namespace Tab5PixelTransfer
{
    /** @brief Kopiert RGB565 aus M5GFX-Sprite-Bytefolge gekachelt in Rotation 3. Alle Rechtecke sind vorab auf Quelle/Ziel begrenzt. stride ist die Quellzeilenbreite, panelWidth/panelHeight die physische Zielgröße. */
    void rotate3(uint16_t *target, int panelWidth, int panelHeight, const uint16_t *source, int stride, int x, int y, int width, int height);
    /** @brief Wie rotate3, aber für Rotation 1 (Querformat um 180 Grad gedreht). */
    void rotate1(uint16_t *target, int panelWidth, int panelHeight, const uint16_t *source, int stride, int x, int y, int width, int height);
}
#endif
