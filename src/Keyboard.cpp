#include "Keyboard.h"
#include "StringUtils.h"
#include <cstring>

// Eingabepuffer, Abbruch, Sichtbarkeit und Shift-Verhalten selektiv nach RetroRadio/Keyboard.cpp,
// ea4229a34fb39faaff23619092cf09cafb8c51cf, keine modale Schleife oder Display-/Touchabfrage.
// UTF-8-Löschung, feste Puffer und native Geometrie neu. GPL-3.0, referenz/RetroRadio-LICENSE.
namespace
{
    // Belegung je Ebene: Ziffernreihe und drei Buchstabenreihen (QWERTZ), Großbuchstaben bzw. Sonderzeichen mit Umlauten
    const char *const Lower[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "q", "w", "e", "r", "t", "z", "u", "i", "o", "p", "a", "s", "d", "f", "g", "h", "j", "k", "l", "-", "y", "x", "c", "v", "b", "n", "m", ".", "@", "_"};
    const char *const Upper[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "Q", "W", "E", "R", "T", "Z", "U", "I", "O", "P", "A", "S", "D", "F", "G", "H", "J", "K", "L", "-", "Y", "X", "C", "V", "B", "N", "M", ".", "@", "_"};
    const char *const Symbols[] = {"!", "\"", "#", "$", "%", "&", "'", "(", ")", "*", "+", ",", "-", ".", "/", ":", ";", "<", "=", ">", "?", "@", "[", "\\", "]", "^", "_", "`", "{", "|", "}", "~", "ä", "ö", "ü", "ß", "Ä", "Ö", "Ü", "€"};
    constexpr SettingsUi::Rect Cancel{24, 542, 260, 64}, Accept{948, 542, 308, 64}, Visibility{1090, 78, 166, 64}; ///< Abbrechen, Übernehmen und Sichtbarkeit des Passworts
}

bool Keyboard::open(const char *title, const char *initial, size_t maxBytes, Mode mode, LangIndex language)
{
    close();
    if (!maxBytes || maxBytes > 64 || !SettingsUi::validText(initial, maxBytes + 1))
        return false;
    stringutils::safeCopy(m_title, title);
    m_limit = maxBytes;
    m_mode = mode;
    m_language = language;
    m_hint = SettingsKey::InputHint;
    m_active = true;
    if (append(initial))
        return true;
    close();
    return false;
}

void Keyboard::close()
{
    memset(m_value, 0, sizeof(m_value));
    m_active = m_visible = m_upper = m_symbols = false;
    m_contact.cancel();
}

bool Keyboard::append(const char *value)
{
    if (!m_active || !SettingsUi::validText(value, 65))
        return false;
    const size_t length = strlen(value);
    if (length + strlen(m_value) > m_limit)
    {
        m_hint = SettingsKey::InputFull;
        return false;
    }
    for (size_t i = 0; i < length; ++i)
    {
        const unsigned char c = static_cast<unsigned char>(value[i]);
        if (m_mode == Mode::Password && (c < 0x20 || c > 0x7e))
        {
            m_hint = SettingsKey::InvalidPassword;
            return false;
        }
    }
    stringutils::safeAppend(m_value, value);
    m_hint = SettingsKey::InputHint;
    return true;
}

void Keyboard::backspace()
{
    SettingsUi::eraseLast(m_value);
    m_hint = SettingsKey::InputHint;
}

const char *Keyboard::key(size_t index) const
{
    if (index >= 40)
        return "";
    return m_symbols ? Symbols[index] : m_upper ? Upper[index]
                                                : Lower[index];
}

SettingsUi::Rect Keyboard::keyRect(size_t index) const
{
    return {24 + static_cast<int>(index % 10) * 124, 184 + static_cast<int>(index / 10) * 64, 116, 58};
}

Keyboard::Result Keyboard::touch(SettingsUi::TouchPhase phase, int x, int y)
{
    if (!m_active)
        return Result::None;
    int target = -1;
    for (size_t i = 0; i < 40; ++i)
        if (key(i)[0] && keyRect(i).contains(x, y))
            target = static_cast<int>(i);
    const bool letters = m_mode == Mode::Text || m_mode == Mode::Password;
    const SettingsUi::Rect special[] = {{24, 452, 180, 64}, {216, 452, 160, 64}, {388, 452, 620, 64}, {1020, 452, 236, 64}};
    for (int i = 0; i < 4; ++i)
        if ((letters || i == 3) && special[i].contains(x, y))
            target = 40 + i;
    if (Cancel.contains(x, y))
        target = 100;
    if (Accept.contains(x, y))
        target = 101;
    if (Visibility.contains(x, y) && m_mode == Mode::Password)
        target = 102;
    target = m_contact.process(phase, target);
    if (target >= 0 && target < 40)
        append(key(static_cast<size_t>(target)));
    else if (target == 40)
    {
        m_upper = !m_upper;
        m_symbols = false;
    }
    else if (target == 41)
        m_symbols = !m_symbols;
    else if (target == 42)
        append(" ");
    else if (target == 43)
        backspace();
    else if (target == 100)
        return Result::Cancelled;
    else if (target == 101)
        return Result::Accepted;
    else if (target == 102)
        m_visible = !m_visible;
    return Result::None;
}
