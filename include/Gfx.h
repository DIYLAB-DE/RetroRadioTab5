#ifndef GFX_H
#define GFX_H

#include <M5GFX.h>
#include <array>

using Gfx_Sprite = lgfx::LGFX_Sprite;

/** @brief Lokaler M5GFX-Zeichenpuffer für die Audioinstrumente. */
class Gfx : public lgfx::LGFX_Sprite
{
public:
    static constexpr int Width = 800, Height = 480; ///< Größte zulässige Puffergröße in Pixeln
    /** @brief Reserviert Zeichen- und Vergleichspuffer (je RGB565 im PSRAM) einmal nach dem Boardstart und löscht beide. @param width Logische Breite, höchstens Width. @param height Logische Höhe, höchstens Height. @return false bei ungültiger Größe oder fehlendem Speicher. */
    bool begin(int width = Width, int height = Height);
    /** @brief Gibt Zeichen- und Vergleichspuffer nach fehlgeschlagener Vorbereitung frei. */
    void release();
    /** @brief Überträgt nur geänderte Zeilen in nativer Pixelgröße. @param display Display. @param x Linke Kante. @param y Obere Kante. @param force Vollständige Ausgabe nach Dialogen. @return true bei übertragenen Pixeln. */
    bool present(lgfx::LGFX_Device &display, int x, int y, bool force = false);
    /** @brief Überträgt nur geänderte Zeilen eines unverzerrten Quellrechtecks. @param sx Linke Kante der Quelle. @param sy Obere Kante der Quelle. @param w Breite. @param h Höhe. @param x Linke Zielkante. @param y Obere Zielkante. @param force Alle Zeilen übertragen. @return true bei übertragenen Pixeln. */
    bool presentRegion(lgfx::LGFX_Device &display, int sx, int sy, int w, int h, int x, int y, bool force = false);
    /** @brief Gibt einen Sprite frei und setzt seinen Zeiger zurück. @param sprite Besitzender Zeiger. */
    void safeDeleteSprite(Gfx_Sprite *&sprite);
    /** @brief Erstellt oder verwendet einen Sprite wieder. @param sprite Besitzender Zeiger. @param width Breite. @param height Höhe. @param usePsram PSRAM bevorzugen. @return Erfolg. */
    bool safeCreateSprite(Gfx_Sprite *&sprite, int width, int height, bool usePsram = true);

private:
    lgfx::LGFX_Sprite m_previous; ///< Zuletzt übertragener Inhalt für den Zeilenvergleich
};

/** @brief Liefert eine Farbe der Regenbogenskala Vis.Regenbogen des aktiven Themas. @param value Farbposition, Werte ab 192 laufen zyklisch weiter. @return RGB565-Farbe. */
uint16_t rainbowColors(uint8_t value);
/** @brief Mischt zwei RGB565-Farben. @param fg Vordergrund. @param bg Hintergrund. @param alpha Deckkraft 0..1. @return Mischfarbe. */
uint16_t blendColor(uint16_t fg, uint16_t bg, float alpha);
#endif
