#include "MessageBox.h"
#include <cstring>

// Modalverhalten und tatsächlicher AA-Hintergrund selektiv nach RetroRadio/MessageBox.cpp,
// ea4229a34fb39faaff23619092cf09cafb8c51cf, native Geometrie und zentrale Kontaktzufuhr neu.
// GPL-3.0, siehe referenz/RetroRadio-LICENSE.
int SettingsUi::Contact::process(TouchPhase phase, int target)
{
    if (phase == TouchPhase::Press)
    {
        m_target = target;
        return -1;
    }
    if (phase == TouchPhase::Move)
    {
        if (m_target != target)
            m_target = -1;
        return -1;
    }
    const int accepted = phase == TouchPhase::Release && m_target >= 0 && target == m_target ? target : -1;
    m_target = -1;
    return accepted;
}

bool SettingsUi::validText(const char *text, size_t capacity)
{
    if (!text || !capacity)
        return false;
    const size_t length = strnlen(text, capacity);
    if (length == capacity)
        return false;
    for (size_t i = 0; i < length;)
    {
        const unsigned char first = static_cast<unsigned char>(text[i++]);
        if (first < 0x20 || first == 0x7f)
            return false;
        if (first < 0x80)
            continue;
        const int extra = first >= 0xc2 && first <= 0xdf ? 1 : first >= 0xe0 && first <= 0xef ? 2
                                                           : first >= 0xf0 && first <= 0xf4   ? 3
                                                                                              : -1;
        if (extra < 0 || i + extra > length)
            return false;
        const unsigned char second = static_cast<unsigned char>(text[i]);
        if ((first == 0xe0 && second < 0xa0) || (first == 0xed && second >= 0xa0) || (first == 0xf0 && second < 0x90) || (first == 0xf4 && second >= 0x90))
            return false;
        for (int j = 0; j < extra; ++j)
            if ((static_cast<unsigned char>(text[i++]) & 0xc0) != 0x80)
                return false;
    }
    return true;
}

void SettingsUi::eraseLast(char *text)
{
    size_t length = strlen(text);
    if (!length)
        return;
    do
    {
        --length;
    } while (length && (static_cast<unsigned char>(text[length]) & 0xc0) == 0x80);
    text[length] = 0;
}
