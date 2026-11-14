#include "AudioSliderTouch.h"
#include "RadioTab5Layout.h"

namespace
{
    namespace T = RadioTab5Layout::Tracks;

    /** @brief Ganzzahlige Umrechnung wie Arduinos map(), damit Treffer und Zeichnung der Regler dieselbe Knopfmitte ergeben. */
    int mapRange(int x, int inMin, int inMax, int outMin, int outMax)
    {
        return static_cast<long>(x - inMin) * (outMax - outMin) / (inMax - inMin) + outMin;
    }

    /** @brief Linke Kante des Reglers index in der Reglerfläche. */
    constexpr int left(int index)
    {
        return T::SLIDER_X0 + T::SLIDER_STEP * index;
    }
}

int AudioSliderTouch::position(int index, int value)
{
    if (index == 0)
        return mapRange(value < -16 ? -16 : value > 16 ? 16 : value, -16, 16, T::PIXEL_MIN, T::PIXEL_MAX);
    const int db = value < -12 ? -12 : value > 12 ? 12 : value;
    return db <= 0 ? mapRange(db, -12, 0, T::PIXEL_MIN, T::PIXEL_ZERO) : mapRange(db, 0, 12, T::PIXEL_ZERO, T::PIXEL_MAX);
}

int AudioSliderTouch::value(int index, int y)
{
    const int pixel = y < T::PIXEL_MAX ? T::PIXEL_MAX : y > T::PIXEL_MIN ? T::PIXEL_MIN : y;
    if (index == 0)
        return mapRange(pixel, T::PIXEL_MIN, T::PIXEL_MAX, -16, 16);
    return pixel > T::PIXEL_ZERO ? mapRange(pixel, T::PIXEL_MIN, T::PIXEL_ZERO, -12, 0) : mapRange(pixel, T::PIXEL_ZERO, T::PIXEL_MAX, 0, 12);
}

AudioSliderTouch::Target AudioSliderTouch::update(bool touched, int x, int y, const int (&positions)[COUNT], uint32_t now)
{
    if (!touched)
    {
        // Ein kurzer Tipp auf den Knopf stellt den Regler erst beim Loslassen auf die Stelle des Aufsetzens.
        const bool tap = m_phase == Phase::Holding && m_startY >= T::PIXEL_MAX && m_startY <= T::PIXEL_MIN;
        m_phase = Phase::Idle;
        return tap ? Target{m_slider, m_startY} : Target{};
    }

    if (m_phase == Phase::Consumed)
        return {};

    int index = -1;
    for (int i = 0; i < COUNT; ++i)
        if (x >= left(i) && x <= left(i) + T::TOUCH_W)
        {
            index = i;
            break;
        }

    if (m_phase == Phase::Idle)
    {
        // Nur ein Druck auf den Knopf selbst kann zur Haltegeste werden. Daneben springt der Regler sofort an die Fingerposition.
        m_phase = Phase::Sliding;
        if (index >= 0)
        {
            const int dx = x - (left(index) + T::CENTER_X), dy = y - positions[index];
            if (dx * dx + dy * dy <= T::KNOB_R * T::KNOB_R)
            {
                m_slider = index;
                m_startX = x;
                m_startY = y;
                m_startTime = now;
                m_phase = Phase::Holding;
            }
        }
    }

    if (m_phase == Phase::Holding)
    {
        const int dx = x - m_startX, dy = y - m_startY;
        if (dx * dx + dy * dy <= T::HOLD_TOLERANCE * T::HOLD_TOLERANCE)
        {
            if (now - m_startTime < HOLD_MS)
                return {};
            m_phase = Phase::Consumed;
            return {m_slider, T::PIXEL_ZERO, true};
        }
        m_phase = Phase::Sliding;
    }

    if (index < 0 || y < T::PIXEL_MAX || y > T::PIXEL_MIN)
        return {};
    return {index, y};
}
