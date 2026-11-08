#ifndef UI_CANVAS_H
#define UI_CANVAS_H
#include <M5GFX.h>
#include "Gfx.h"

/** @brief M5GFX-Sprite mit geglätteten Instrumentenzeichnern, ohne eigenen Hardwarezugriff. prepare und Zeichnen erfolgen serialisiert im UI-Task. Der Sprite besitzt seine Pixel, die Schriftindizes werden gemeinsam für die Laufzeit gehalten. */
class UiCanvas : public lgfx::LGFX_Sprite
{
public:
    /** @brief Reserviert den RGB565-Sprite im PSRAM und bereitet gemeinsame Schriftindizes beim ersten Aufruf vor. @param width Breite in Pixeln. @param height Höhe in Pixeln. @return false bei Schrift- oder Pufferfehler, true bei angelegtem Sprite. */
    bool prepare(int width, int height);
    /** @brief Wählt einen der bei prepare vorbereiteten VLW-Fonts ohne Allokation. @param font Zeiger auf eine unterstützte Schriftdatei, unbekannte Zeiger setzen die Schrift auf nullptr. */
    void loadFont(const uint8_t *font);

    /** @brief Löst die aktuell gewählte Schrift ohne den gemeinsamen Index freizugeben. */
    void unloadFont()
    {
        setFont(nullptr);
    }

    /** @brief Schreibt ein Pixel innerhalb der Spritegrenzen. */
    void drawPixelClipped(int x, int y, uint16_t color)
    {
        if (x >= 0 && y >= 0 && x < width() && y < height())
            drawPixel(x, y, color);
    }

    /** @brief Mischt eine Farbe spaltenweise über Pixel eines anderen Sprites und schreibt das Ergebnis in diesen Sprite, direkt im Speicher. Beide Sprites müssen RGB565-Sprites aus prepare sein. Die Koordinaten müssen innerhalb beider Sprites liegen. @param source Sprite mit den Hintergrundpixeln. @param sx Spalte in source. @param sy Erste Zeile in source. @param dx Spalte in diesem Sprite. @param dy Erste Zeile in diesem Sprite. @param h Zeilenzahl. @param color RGB565-Farbe. @param alpha Deckung der Farbe 0..1. */
    void blendColumnFrom(UiCanvas &source, int sx, int sy, int dx, int dy, int h, uint16_t color, float alpha);
    /** @brief Schreibt die Spalten [x0, x0 + count) zeilenweise nach target: Zeile i enthält Spalte x0 + i mit height() Pixeln in RGB565 ohne Bytetausch (Byteordnung des DSI-Framebuffers), bei reverse von unten nach oben. Gekachelt, damit Lesen und Schreiben im PSRAM zusammenhängend bleiben. @param target Mindestens count * height() Pixel. */
    void columnsToRows(uint16_t *target, int x0, int count, bool reverse);
    /** @brief Zeichnet einen geglätteten Kreisring. Mittelpunkt, Radius und Dicke in Pixeln, Farben in RGB565. Nichtpositive Radien oder Dicken zeichnen nichts. */
    void drawSmoothCircle(int xc, int yc, float r, float thickness, uint16_t color, uint16_t bg);
    /** @brief Zeichnet eine geglättete Ellipsenkontur. Mittelpunkt, Halbachsen und Dicke in Pixeln, Farben in RGB565. Nichtpositive Radien oder Dicken zeichnen nichts. */
    void drawSmoothEllipse(int xc, int yc, float rx, float ry, float thickness, uint16_t color, uint16_t bg);
    /** @brief Zeichnet einen geglätteten Instrumentenbogen mit optionaler Spur. Mittelpunkt, Radius und Dicke in Pixeln, Winkel in Grad, Farben in RGB565. value wird auf valueMin..valueMax begrenzt. */
    void drawSmoothGauge(int xc, int yc, float r, float thickness, float angleStart, float angleEnd, float valueMin, float valueMax, float value, uint16_t color, uint16_t colorTrack, bool trackVisible, uint16_t bg);
    /** @brief Zeichnet eine EM11-Anzeige um den Mittelpunkt in Pixeln. level wird auf 0..1 begrenzt, bg ist RGB565. @param rowFrom,rowTo Nur die Zielzeilen rowFrom bis rowTo - 1 zeichnen, rowTo -1 bis zum unteren Rand. So entsteht ein Bild in mehreren Schritten, die Pixel gleichen dem Bild in einem Zug. */
    void drawEm11MagicEye(int xc, int yc, float r, float level, uint16_t bg, int rowFrom = 0, int rowTo = -1);
    /** @brief Zeichnet eine EM34-Anzeige um den Mittelpunkt in Pixeln. level wird auf 0..1 begrenzt, bg ist RGB565. @param rowFrom,rowTo Zeilenbereich wie bei drawEm11MagicEye. */
    void drawEm34MagicEye(int xc, int yc, float r, float level, uint16_t bg, int rowFrom = 0, int rowTo = -1);
    /** @brief Zeichnet eine EM80-Anzeige mit Halbausmaßen in Pixeln. level wird auf 0..1 begrenzt, ring und bg sind RGB565. @param rowFrom,rowTo Zeilenbereich wie bei drawEm11MagicEye. */
    void drawEm80MagicEye(int xc, int yc, float halfWidth, float halfHeight, float level, uint16_t ring, uint16_t bg, int rowFrom = 0, int rowTo = -1);
    /** @brief Zeichnet eine EM84-Anzeige mit Halbausmaßen in Pixeln. level wird auf 0..1 begrenzt, ring und bg sind RGB565. @param rowFrom,rowTo Zeilenbereich wie bei drawEm11MagicEye. */
    void drawEm84MagicEye(int xc, int yc, float halfWidth, float halfHeight, float level, uint16_t ring, uint16_t bg, int rowFrom = 0, int rowTo = -1);
};

/** @brief Berechnet die gewichtete Helligkeit eines RGB565-Pixels. @param color RGB565-Farbe. @return Helligkeit 0..1 für deaktivierte Symbole. */
float getIntensityFrom565(uint16_t color);
#endif
