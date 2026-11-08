#include "IconShapes.h"
#include "IconShapeData.h"

#include <Arduino.h>
#include <cmath>

// Abstandszeichnung, Even-Odd-Flächen und RGB565-Mischung selektiv aus DIYLAB-DE/RetroRadio, ea4229a34fb39faaff23619092cf09cafb8c51cf, src/IconShapes.cpp. GPL-3.0, siehe referenz/RetroRadio-LICENSE.
namespace
{
    constexpr float FarAway = 1.0e9f; ///< Abstand für „keine Form in Reichweite“
    constexpr int Samples = 2;        ///< Teilproben je Achse und Pixel für die Kantenglättung

    struct Bounds
    {
        float left, top, right, bottom;
    };

    /** @brief Begrenzt eine Deckung auf das Intervall 0 bis 1. */
    float coverage(float value)
    {
        return fminf(1.0f, fmaxf(0.0f, value));
    }

    /** @brief Liefert die Formgrenzen einschließlich weicher Außenkante. */
    Bounds bounds(const IconShape &shape, float stroke)
    {
        Bounds result{};
        if (shape.kind == IconShapeKind::Segment)
        {
            result = {fminf(shape.values[0], shape.values[2]), fminf(shape.values[1], shape.values[3]),
                      fmaxf(shape.values[0], shape.values[2]), fmaxf(shape.values[1], shape.values[3])};
        }
        else if (shape.kind == IconShapeKind::Ring || shape.kind == IconShapeKind::Arc)
        {
            result = {shape.values[0] - shape.values[2], shape.values[1] - shape.values[2],
                      shape.values[0] + shape.values[2], shape.values[1] + shape.values[2]};
        }
        else
        {
            const unsigned first = shape.firstPoint * 2;
            result = {ICON_POINTS[first], ICON_POINTS[first + 1], ICON_POINTS[first], ICON_POINTS[first + 1]};
            for (unsigned i = 1; i < shape.pointCount; ++i)
            {
                const unsigned p = first + i * 2;
                result.left = fminf(result.left, ICON_POINTS[p]);
                result.right = fmaxf(result.right, ICON_POINTS[p]);
                result.top = fminf(result.top, ICON_POINTS[p + 1]);
                result.bottom = fmaxf(result.bottom, ICON_POINTS[p + 1]);
            }
        }
        const float margin = (shape.kind == IconShapeKind::FilledPolygon ? shape.values[0] : stroke * .5f) + 1.0f;
        result.left -= margin;
        result.right += margin;
        result.top -= margin;
        result.bottom += margin;
        return result;
    }

    /** @brief Berechnet den kürzesten Abstand zu einer Strecke mit runden Enden. */
    float segmentDistance(float px, float py, float x0, float y0, float x1, float y1)
    {
        const float vx = x1 - x0, vy = y1 - y0, wx = px - x0, wy = py - y0;
        const float length = vx * vx + vy * vy;
        const float t = length > 0 ? coverage((wx * vx + wy * vy) / length) : 0;
        return hypotf(wx - t * vx, wy - t * vy);
    }

    /** @brief Überspringt weit entfernte Strecken, bevor eine Wurzel nötig wird. */
    bool farFromSegment(float px, float py, float ax, float ay, float bx, float by, float reach)
    {
        return (px < ax - reach && px < bx - reach) || (px > ax + reach && px > bx + reach) || (py < ay - reach && py < by - reach) || (py > ay + reach && py > by + reach);
    }

    /** @brief Liefert Kantendistanz und gemeinsame Even-Odd-Strahlenzählung einer Kontur. */
    float contourDistance(const IconShape &shape, float px, float py, float reach, bool &inside)
    {
        float nearest = FarAway;
        const unsigned edges = shape.closed ? shape.pointCount : shape.pointCount - 1;
        for (unsigned i = 0; i < edges; ++i)
        {
            const unsigned a = (shape.firstPoint + i) * 2;
            const unsigned b = (shape.firstPoint + (i + 1) % shape.pointCount) * 2;
            const float ax = ICON_POINTS[a], ay = ICON_POINTS[a + 1], bx = ICON_POINTS[b], by = ICON_POINTS[b + 1];
            if (!farFromSegment(px, py, ax, ay, bx, by, reach))
                nearest = fminf(nearest, segmentDistance(px, py, ax, ay, bx, by));
            if (shape.kind == IconShapeKind::FilledPolygon && (ay > py) != (by > py))
            {
                if (px < ax && px < bx)
                    inside = !inside;
                else if ((px < ax || px < bx) && px < ax + (py - ay) * (bx - ax) / (by - ay))
                    inside = !inside;
            }
        }
        return nearest;
    }

    /** @brief Berechnet den Abstand eines Punkts zu einem Kreisbogen. @param startDeg Startwinkel in Grad. @param endDeg Endwinkel in Grad, auch über 0 Grad hinweg. */
    static float iconDistanceToArc(float px, float py, float cx, float cy, float radius, float startDeg, float endDeg)
    {
        float angle = atan2f(py - cy, px - cx) * 57.2957795131f;
        if (angle < 0.0f)
            angle += 360.0f;

        float low = fmodf(startDeg, 360.0f);
        if (low < 0.0f)
            low += 360.0f;

        float high = fmodf(endDeg, 360.0f);
        if (high < 0.0f)
            high += 360.0f;

        const bool onArc = (low <= high) ? (angle >= low && angle <= high)
                                         : (angle >= low || angle <= high);

        if (onArc)
            return fabsf(hypotf(px - cx, py - cy) - radius);

        const float startX = cx + radius * cosf(startDeg / 57.2957795131f);
        const float startY = cy + radius * sinf(startDeg / 57.2957795131f);
        const float endX = cx + radius * cosf(endDeg / 57.2957795131f);
        const float endY = cy + radius * sinf(endDeg / 57.2957795131f);

        return fminf(hypotf(px - startX, py - startY), hypotf(px - endX, py - endY));
    }

    /** @brief Wertet ausschließlich die benötigten Strichformen aus. */
    float shapeCoverage(const IconShape &shape, float px, float py, float stroke, float scale)
    {
        float distance = FarAway;
        if (shape.kind == IconShapeKind::Segment)
            distance = segmentDistance(px, py, shape.values[0], shape.values[1], shape.values[2], shape.values[3]);
        else if (shape.kind == IconShapeKind::Ring)
            distance = fabsf(hypotf(px - shape.values[0], py - shape.values[1]) - shape.values[2]);
        else if (shape.kind == IconShapeKind::Arc)
            distance = iconDistanceToArc(px, py, shape.values[0], shape.values[1], shape.values[2], shape.values[3], shape.values[4]);
        else if (shape.kind == IconShapeKind::Polyline)
        {
            bool unused = false;
            distance = contourDistance(shape, px, py, stroke * .5f + 1.0f, unused);
        }
        return coverage(.5f - (distance - stroke * .5f) * scale);
    }

    /** @brief Mischt die drei RGB565-Kanäle ohne weitere Speicherreservierung. */
    uint16_t mix(uint16_t fore, uint16_t back, float alpha)
    {
        const auto channel = [alpha](int a, int b)
        {
            return static_cast<unsigned>(b + (a - b) * alpha + .5f);
        };
        return static_cast<uint16_t>((channel(fore >> 11, back >> 11) << 11) | (channel((fore >> 5) & 63, (back >> 5) & 63) << 5) | channel(fore & 31, back & 31));
    }
}

uint16_t *IconShapes::draw(IconId id, uint16_t color, uint16_t background, float scale)
{
    if (static_cast<unsigned>(id) >= static_cast<unsigned>(IconId::Count) || scale < 1 || scale > (id == IconId::Folder ? 6 : 2))
        return nullptr;
    const int size = static_cast<int>(lroundf(width(id) * scale)), rows = static_cast<int>(lroundf(height(id) * scale));
    auto *out = static_cast<uint16_t *>(ps_malloc(static_cast<size_t>(size) * rows * sizeof(uint16_t)));
    if (!out)
        return nullptr;
    const auto &definition = ICON_DEFINITIONS[static_cast<unsigned>(id)];
    Bounds boxes[ICON_MAX_SHAPE_COUNT];
    for (unsigned i = 0; i < definition.shapeCount; ++i)
        boxes[i] = bounds(definition.shapes[i], definition.stroke);
    for (int y = 0; y < rows; ++y)
    {
        for (int x = 0; x < size; ++x)
        {
            float sum = 0;
            for (int sy = 0; sy < Samples; ++sy)
            {
                for (int sx = 0; sx < Samples; ++sx)
                {
                    const float px = (x + (sx + .5f) / Samples) / scale;
                    const float py = (y + (sy + .5f) / Samples) / scale;
                    float added = 0, nearest = FarAway, grow = 0;
                    bool inside = false;
                    for (unsigned i = 0; i < definition.shapeCount; ++i)
                    {
                        const auto &box = boxes[i];
                        if (px < box.left || px > box.right || py < box.top || py > box.bottom)
                            continue;
                        const auto &shape = definition.shapes[i];
                        if (shape.kind == IconShapeKind::FilledPolygon)
                        {
                            grow = shape.values[0];
                            nearest = fminf(nearest, contourDistance(shape, px, py, grow + 1.0f, inside));
                        }
                        else
                            added = fmaxf(added, shapeCoverage(shape, px, py, definition.stroke, scale));
                    }
                    if (inside || nearest < FarAway)
                        added = fmaxf(added, coverage(.5f - ((inside ? -nearest : nearest) - grow) * scale));
                    sum += added;
                }
            }
            out[static_cast<size_t>(y) * size + x] = mix(color, background, sum / (Samples * Samples));
        }
    }
    return out;
}
