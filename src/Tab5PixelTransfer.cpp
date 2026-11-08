#include "Tab5PixelTransfer.h"
#include <algorithm>

void Tab5PixelTransfer::rotate3(uint16_t *target, int panelWidth, int panelHeight, const uint16_t *source, int stride, int x, int y, int width, int height)
{
    constexpr int Tile = 32;
    uint16_t tile[Tile * Tile];
    for (int sx = 0; sx < width; sx += Tile)
    {
        const int columns = std::min(Tile, width - sx);
        for (int sy = 0; sy < height; sy += Tile)
        {
            const int rows = std::min(Tile, height - sy);
            // Zusammenhängend aus PSRAM lesen, innerhalb von SRAM transponieren, wieder zusammenhängend schreiben.
            for (int row = 0; row < rows; ++row)
            {
                const uint16_t *input = source + (sy + row) * stride + sx;
                for (int column = 0; column < columns; ++column)
                {
                    const uint16_t value = input[column];
                    tile[row * Tile + column] = static_cast<uint16_t>((value >> 8) | (value << 8));
                }
            }
            for (int column = 0; column < columns; ++column)
            {
                uint16_t *output = target + (panelHeight - 1 - x - sx - column) * panelWidth + y + sy;
                for (int row = 0; row < rows; ++row)
                    output[row] = tile[row * Tile + column];
            }
        }
    }
}

void Tab5PixelTransfer::rotate1(uint16_t *target, int panelWidth, int panelHeight, const uint16_t *source, int stride, int x, int y, int width, int height)
{
    (void)panelHeight;
    constexpr int Tile = 32;
    uint16_t tile[Tile * Tile];
    for (int sx = 0; sx < width; sx += Tile)
    {
        const int columns = std::min(Tile, width - sx);
        for (int sy = 0; sy < height; sy += Tile)
        {
            const int rows = std::min(Tile, height - sy);
            for (int row = 0; row < rows; ++row)
            {
                const uint16_t *input = source + (sy + row) * stride + sx;
                for (int column = 0; column < columns; ++column)
                {
                    const uint16_t value = input[column];
                    tile[row * Tile + column] = static_cast<uint16_t>((value >> 8) | (value << 8));
                }
            }
            // Gegenüber Rotation 3 um 180 Grad: logisches x wird Panelzeile, logisches y läuft rückwärts über die Panelbreite.
            for (int column = 0; column < columns; ++column)
            {
                uint16_t *output = target + (x + sx + column) * panelWidth + panelWidth - 1 - y - sy;
                for (int row = 0; row < rows; ++row)
                    output[-row] = tile[row * Tile + column];
            }
        }
    }
}
