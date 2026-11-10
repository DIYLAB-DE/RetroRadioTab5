#ifndef BATTERY_ICON_H
#define BATTERY_ICON_H

#include <cstdint>
#include "Thema.h"

/** @brief Akkusymbol für alle Ansichten: Anzeigezustand, drei Bauformen und der gemeinsame Zeichner. Reine Zeichenlogik ohne Hardwarezugriff, die Messung liefert Tab5Battery. */
namespace BatteryIcon
{
    /** @brief Anzeigezustand, den der zentrale Besitzer einmal je Sekunde an die Ansichten kopiert. */
    struct State
    {
        bool present = false;  ///< Akku erkannt. Ohne Akku zeichnet keine Ansicht ein Symbol
        bool external = false; ///< Strom von außen, das Symbol trägt einen Blitz
        bool blinkOff = false; ///< Dunkle Phase des Blinkens bei fast leerem Akku: Rahmen bleibt, die leuchtenden Segmente sind aus
        uint8_t percent = 0;   ///< Ladestand 0..100

        /** @brief Vergleicht alle Felder, die Ansichten zeichnen nur bei einem Unterschied neu. */
        bool operator==(const State &other) const
        {
            return present == other.present && external == other.external && blinkOff == other.blinkOff && percent == other.percent;
        }

        /** @brief Gegenstück zu operator==. */
        bool operator!=(const State &other) const
        {
            return !(*this == other);
        }
    };

    constexpr int SegmentCount = 5;     ///< Jedes Segment steht für 20 %
    constexpr uint8_t BlinkBelow = 10;  ///< Unter diesem Ladestand blinkt das rote Segment ohne Strom von außen

    /** @brief Liefert die Zahl der leuchtenden Segmente 1..5. Das letzte Segment bleibt bis 0 % an, damit ein erkannter Akku nie wie ein leerer Rahmen aussieht. */
    constexpr int segments(uint8_t percent)
    {
        return percent <= 20 ? 1 : percent > 80 ? 5
                                                : (percent + 19) / 20;
    }

    /** @brief Maße einer Bauform in Pixeln. Die Segmente füllen den Körper entlang seiner langen Achse: Länge - 2 x (frame + pad) = 5 x segment + 4 x gap. */
    struct Shape
    {
        bool upright;       ///< Pluspol oben und Füllung von unten, sonst Pluspol rechts und Füllung von links
        uint8_t width;      ///< Außenbreite samt Pluspol
        uint8_t height;     ///< Außenhöhe samt Pluspol
        uint8_t frame;      ///< Rahmenstärke
        uint8_t radius;     ///< Eckenradius des Körpers
        uint8_t pad;        ///< Abstand Rahmen -> Segmente
        uint8_t capLength;  ///< Länge des Pluspols entlang der langen Achse
        uint8_t capBreadth; ///< Breite des Pluspols quer dazu
        uint8_t segment;    ///< Länge eines Segments entlang der langen Achse
        uint8_t gap;        ///< Abstand zwischen zwei Segmenten
        float bolt;         ///< Maßstab des Blitzes, 1 = 16 x 30
    };

    constexpr Shape Upright{true, 27, 67, 3, 5, 2, 5, 11, 8, 3, 1.0f}; ///< Radio und SD-Player: so hoch wie ein Button, schmal genug für den Platz zwischen Digitaluhr oder Goniometer-Pegel und erstem Button
    constexpr Shape Large{false, 82, 42, 4, 7, 3, 6, 18, 10, 3, 0.9f}; ///< MainScreen und Sperrbildschirm neben der Prozentzahl in der Datumsschrift
    constexpr Shape Small{false, 50, 24, 2, 4, 2, 4, 10, 6, 2, 0.5f};  ///< Einstellungen unter der Prozentzahl, zusammen so hoch wie die Ziffern der Uhr

    /** @brief Füllfarbe der leuchtenden Segmente: ab drei Segmenten gut, bei zwei mittel, bei einem schwach. */
    inline uint16_t levelColor(int lit)
    {
        return farbe(lit >= 3 ? Rolle::VisPegelGruen : lit == 2 ? Rolle::EinSignalMittel
                                                                : Rolle::EinSignalSchwach);
    }

    /**
     * @brief Zeichnet das Symbol mit der linken oberen Ecke bei (x, y). Der Aufrufer hat die Fläche vorher mit ground gefüllt. Ohne Akku zeichnet die Funktion nichts, in der dunklen Blinkphase den Rahmen mit dunklen Segmenten.
     * @param canvas Sprite oder Display mit fillSmoothRoundRect, fillRect und fillTriangle.
     * @param shape Bauform.
     * @param state Anzeigezustand.
     * @param frame Farbe von Rahmen und Pluspol in RGB565.
     * @param ground Farbe der Fläche unter dem Symbol in RGB565.
     */
    template <class Canvas>
    void draw(Canvas &canvas, int x, int y, const Shape &shape, const State &state, uint16_t frame, uint16_t ground)
    {
        if (!state.present)
            return;
        const int bodyX = x, bodyY = shape.upright ? y + shape.capLength : y;
        const int bodyW = shape.upright ? shape.width : shape.width - shape.capLength, bodyH = shape.upright ? shape.height - shape.capLength : shape.height;
        // Der Pluspol ragt zwei Pixel in den Körper, der danach darüber liegt. So entsteht keine Fuge an der runden Ecke.
        constexpr int overlap = 2;
        if (shape.upright)
            canvas.fillSmoothRoundRect(x + (shape.width - shape.capBreadth) / 2, y, shape.capBreadth, shape.capLength + overlap, 2, frame);
        else
            canvas.fillSmoothRoundRect(x + bodyW - overlap, y + (shape.height - shape.capBreadth) / 2, shape.capLength + overlap, shape.capBreadth, 2, frame);
        canvas.fillSmoothRoundRect(bodyX, bodyY, bodyW, bodyH, shape.radius, frame);
        canvas.fillSmoothRoundRect(bodyX + shape.frame, bodyY + shape.frame, bodyW - 2 * shape.frame, bodyH - 2 * shape.frame, shape.radius > shape.frame ? shape.radius - shape.frame : 1, ground);
        const int lit = state.blinkOff ? 0 : segments(state.percent), inset = shape.frame + shape.pad, step = shape.segment + shape.gap;
        const uint16_t on = levelColor(lit), off = farbe(Rolle::UhrDigitalSpur);
        for (int i = 0; i < SegmentCount; ++i)
        {
            const uint16_t color = i < lit ? on : off;
            if (shape.upright)
                canvas.fillRect(bodyX + inset, bodyY + bodyH - inset - shape.segment - i * step, bodyW - 2 * inset, shape.segment, color);
            else
                canvas.fillRect(bodyX + inset + i * step, bodyY + inset, shape.segment, bodyH - 2 * inset, color);
        }
        if (!state.external)
            return;
        // Blitz als vier Dreiecke um die Körpermitte, vorher viermal versetzt in der Grundfarbe als Saum, damit er sich von leuchtenden Segmenten abhebt.
        static constexpr int8_t points[6][2] = {{4, -15}, {-8, 3}, {-1, 3}, {-4, 15}, {8, -3}, {1, -3}};
        static constexpr uint8_t triangles[4][3] = {{0, 1, 2}, {0, 2, 5}, {2, 3, 4}, {2, 4, 5}};
        static constexpr int8_t offsets[5][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}, {0, 0}};
        const float centerX = bodyX + bodyW / 2.0f, centerY = bodyY + bodyH / 2.0f;
        const int edge = shape.bolt >= 0.75f ? 2 : 1;
        const uint16_t bolt = farbe(Rolle::GemTextHell);
        for (const auto &offset : offsets)
        {
            const bool fill = !offset[0] && !offset[1];
            for (const auto &triangle : triangles)
            {
                int px[3], py[3];
                for (int corner = 0; corner < 3; ++corner)
                {
                    // Die Koordinaten liegen auf der Zeichenfläche und sind nie negativ, + 0,5 rundet deshalb kaufmännisch.
                    px[corner] = static_cast<int>(centerX + points[triangle[corner]][0] * shape.bolt + 0.5f) + offset[0] * edge;
                    py[corner] = static_cast<int>(centerY + points[triangle[corner]][1] * shape.bolt + 0.5f) + offset[1] * edge;
                }
                canvas.fillTriangle(px[0], py[0], px[1], py[1], px[2], py[2], fill ? bolt : ground);
            }
        }
    }
}

#endif
