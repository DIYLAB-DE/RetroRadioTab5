#include "Gfx.h"
#include "Tab5Render.h"
#include "Thema.h"
#include <new>

// Sprite-Verwaltung und Farbhelfer aus RetroRadio, ea4229a34fb39faaff23619092cf09cafb8c51cf.
namespace
{
    constexpr uint8_t RAINBOW_COLOR_COUNT = sizeof(Farbtabellen::regenbogen) / sizeof(Farbtabellen::regenbogen[0]); ///< Anzahl der Farben im zyklischen RGB565-Verlauf
}

void Gfx::safeDeleteSprite(Gfx_Sprite *&sprite)
{
    if (sprite)
    {
        // SmoothFont-Speicher freigeben
        sprite->unloadFont();

        sprite->deleteSprite();
        delete sprite;
        sprite = nullptr;
    }
}

bool Gfx::safeCreateSprite(Gfx_Sprite *&sprite, int width, int height, bool usePsram)
{
    if (sprite)
    {
        if (sprite->width() == width && sprite->height() == height)
        {
            sprite->setPsram(usePsram);
            sprite->setSwapBytes(true);
            sprite->setColorDepth(16);
            return true;
        }

        safeDeleteSprite(sprite);
    }

    sprite = new (std::nothrow) Gfx_Sprite(this);
    if (!sprite)
        return false;

    sprite->setPsram(usePsram);
    sprite->setSwapBytes(true);
    sprite->setColorDepth(16);

    if (!sprite->createSprite(width, height))
    {
        safeDeleteSprite(sprite);
        return false;
    }

    return true;
}

uint16_t blendColor(uint16_t fg, uint16_t bg, float alpha)
{
    if (alpha >= 1.0f)
        return fg;

    if (alpha <= 0.0f)
        return bg;

    uint32_t alpha256 = (uint32_t)(alpha * 256.0f);
    uint32_t inv_alpha = 256 - alpha256;

    uint32_t r1 = (fg >> 11) & 0x1F;
    uint32_t g1 = (fg >> 5) & 0x3F;
    uint32_t b1 = fg & 0x1F;

    uint32_t r2 = (bg >> 11) & 0x1F;
    uint32_t g2 = (bg >> 5) & 0x3F;
    uint32_t b2 = bg & 0x1F;

    // Integer-basierte Interpolation (schneller als Float)
    uint32_t r = (r1 * alpha256 + r2 * inv_alpha) >> 8;
    uint32_t g = (g1 * alpha256 + g2 * inv_alpha) >> 8;
    uint32_t b = (b1 * alpha256 + b2 * inv_alpha) >> 8;

    return static_cast<uint16_t>((r << 11) | (g << 5) | b);
}

uint16_t rainbowColors(uint8_t value)
{
    if (value >= RAINBOW_COLOR_COUNT)
        value -= RAINBOW_COLOR_COUNT;

    return farbtabellen().regenbogen[value];
}

bool Gfx::begin(int width, int height)
{
    if (width <= 0 || height <= 0 || width > Width || height > Height)
        return false;
    setPsram(true);
    setColorDepth(16);
    setSwapBytes(true);
    m_previous.setPsram(true);
    m_previous.setColorDepth(16);
    if (!createSprite(width, height) || !m_previous.createSprite(width, height))
    {
        deleteSprite();
        m_previous.deleteSprite();
        return false;
    }
    const uint16_t grund = farbe(Rolle::GemGrund);
    fillScreen(grund);
    m_previous.fillScreen(grund);
    return true;
}

void Gfx::release()
{
    deleteSprite();
    m_previous.deleteSprite();
}

bool Gfx::present(lgfx::LGFX_Device &display, int x, int y, bool force)
{
    return presentRegion(display, 0, 0, width(), height(), x, y, force);
}

bool Gfx::presentRegion(lgfx::LGFX_Device &display, int sx, int sy, int w, int h, int x, int y, bool force)
{
    auto *pixels = static_cast<const uint16_t *>(getBuffer());
    auto *previous = static_cast<uint16_t *>(m_previous.getBuffer());
    if (!pixels || !previous || sx < 0 || sy < 0 || w <= 0 || h <= 0 || sx + w > width() || sy + h > height())
        return false;
    const int stride = width();
    int first = h, last = -1;
    for (int row = 0; row < h; ++row)
    {
        const auto *source = pixels + (sy + row) * stride + sx;
        auto *saved = previous + (sy + row) * stride + sx;
        if (!force && memcmp(source, saved, w * sizeof(uint16_t)) == 0)
            continue;
        first = std::min(first, row);
        last = row;
        memcpy(saved, source, w * sizeof(uint16_t));
    }
    if (last < first)
        return false;
#if defined(ARDUINO_ARCH_ESP32)
    Tab5Render::pushRegion(display, pixels + (sy + first) * stride + sx, stride, x, y + first, w, last - first + 1);
#else
    for (int row = first; row <= last; ++row)
        display.pushImage(x, y + row, w, 1, reinterpret_cast<const lgfx::swap565_t *>(pixels + (sy + row) * stride + sx));
#endif
    return true;
}
