#include "SettingsDialogs.h"
#include "UiButton.h"
#include "SettingsTexts.h"
#include "StringUtils.h"
#include "fonts/ComfortaaBold21.h"
#include "fonts/ComfortaaBold27.h"
#include "fonts/LeagueGothicCond51.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>

// Layouts und Zeichenpfade aus DIYLAB-DE/RetroRadio, ea4229a34fb39faaff23619092cf09cafb8c51cf, Keyboard/WifiList/MessageBox.cpp. GPL-3.0, siehe referenz/RetroRadio-LICENSE. Zentrale Kontaktphasen ersetzen die blockierenden Touchschleifen. Nativ in 1280x720, Maße mit Originalwerten in SettingsTab5Layout.h.
namespace
{
    using Rect = SettingsUi::Rect;
    using Phase = SettingsUi::TouchPhase;
    namespace K = SettingsTab5Layout::Keyboard;
    namespace N = SettingsTab5Layout::Networks;
    namespace G = SettingsTab5Layout::Progress;
    constexpr const char *ACTION_BACKSPACE = "BACKSPACE", *ACTION_SHIFT = "SHIFT", *ACTION_CAPSLOCK = "CAPSLOCK", *ACTION_SYMBOLS = "SYMBOLS", *ACTION_LETTERS = "LETTERS", *ACTION_SPACE = "SPACE", *ACTION_OK = "OK", *ACTION_CANCEL = "CANCEL", *ACTION_LEFT = "LEFT", *ACTION_RIGHT = "RIGHT";

    /** @brief Zeichnet eine UiButton-Schaltfläche ohne eigenen Display- oder Kontaktbesitz. Standard sind die nativen Werte Radius 6 (4), Rahmen 2 (1) und gedrückt 3 (2). */
    void dialogButton(UiCanvas &c, Rect r, const char *label, uint16_t border, uint16_t fill, uint16_t text, bool pressed = false, const uint8_t *font = LeagueGothicCond51, uint8_t datum = MC_DATUM, int radius = K::R, uint8_t borderNormal = SettingsTab5Layout::Style::BORDER, uint8_t borderPressed = SettingsTab5Layout::Style::BORDER_PRESSED)
    {
        UiButton b;
        b.initButton(&c, r.x + r.w / 2, r.y + r.h / 2, r.w, r.h, radius, border, fill, text, label, nullptr, 1, 1);
        b.setFont(font);
        b.setBorderWidths(borderNormal, borderPressed);
        if (datum != MC_DATUM)
            b.setLabelDatum(datum == ML_DATUM ? K::LABEL_OFFSET : -K::LABEL_OFFSET, 0, datum);
        b.drawButton(pressed);
    }

    /** @brief Erkennt Backspace und die beiden Cursoraktionen als wiederholbare Tasten. */
    bool repeatable(const char *a)
    {
        return a && (!strcmp(a, ACTION_BACKSPACE) || !strcmp(a, ACTION_LEFT) || !strcmp(a, ACTION_RIGHT));
    }

    /** @brief Zeichnet Text mit Wortumbruch anhand der Schriftbreite und mit festen Puffern, leading ist der Zusatzabstand zur Schrifthöhe. */
    void multiline(UiCanvas &c, int x, int y, int width, const char *text, int leading = 2)
    {
        char line[256]{}, word[64]{};
        size_t used = 0;
        const int height = c.fontHeight() + leading;
        while (text && *text)
        {
            if (*text == '\n')
            {
                if (used)
                    c.drawString(line, x, y);
                used = 0;
                line[0] = 0;
                y += height;
                ++text;
                continue;
            }
            size_t n = 0;
            while (*text && *text != ' ' && *text != '\n' && n < sizeof(word) - 1)
                word[n++] = *text++;
            word[n] = 0;
            while (*text == ' ')
                ++text;
            if (!n)
                continue;
            char candidate[256];
            snprintf(candidate, sizeof(candidate), "%s%s%s", line, used ? " " : "", word);
            if (used && (used + n + 1 >= sizeof(line) || c.textWidth(candidate) > width))
            {
                c.drawString(line, x, y);
                y += height;
                used = 0;
                line[0] = 0;
            }
            if (used)
                line[used++] = ' ';
            memcpy(line + used, word, n + 1);
            used += n;
        }
        if (used)
            c.drawString(line, x, y);
    }
}

#define K(l, a, w) {l, a, w, nullptr, Rolle::GemTextHell, MC_DATUM}           // Standard (zentriert)
#define K_C(l, a, w, c) {l, a, w, nullptr, c, MC_DATUM}                      // Mit eigener Farbrolle
#define K_L(l, a, w) {l, a, w, nullptr, Rolle::GemTextHell, ML_DATUM}         // Links ausgerichtet
#define K_R(l, a, w) {l, a, w, nullptr, Rolle::GemTextHell, MR_DATUM}         // Rechts ausgerichtet
#define K_ICON(l, a, w, i) {l, a, w, i, Rolle::GemTextHell, MC_DATUM}         // Mit Icon
#define K_ICON_C(l, a, w, i, c) {l, a, w, i, c, MC_DATUM}                    // Mit Icon und Farbrolle
#define K_END {nullptr, nullptr, 0.0f, nullptr, Rolle::GemTextHell, MC_DATUM} // Ende der Zeile

const SettingsKeyDef SettingsKeyboard::LAYOUT_LOWERCASE[SettingsKeyboard::Layout::KEYBOARD_ROWS][SettingsKeyboard::Layout::MAX_COLS] = {
    // 1. Zeile
    {
        K("^", "^", 1.0f),
        K("1", "1", 1.0f),
        K("2", "2", 1.0f),
        K("3", "3", 1.0f),
        K("4", "4", 1.0f),
        K("5", "5", 1.0f),
        K("6", "6", 1.0f),
        K("7", "7", 1.0f),
        K("8", "8", 1.0f),
        K("9", "9", 1.0f),
        K("0", "0", 1.0f),
        K("ß", "ß", 1.0f),
        K("´", "´", 1.0f),
        K("^BACKSPACE:30^", "BACKSPACE", 1.0f),
        K_END},
    // 2. Zeile
    {
        K("", "", 1.5f),
        K("q", "q", 1.0f),
        K("w", "w", 1.0f),
        K("e", "e", 1.0f),
        K("r", "r", 1.0f),
        K("t", "t", 1.0f),
        K("z", "z", 1.0f),
        K("u", "u", 1.0f),
        K("i", "i", 1.0f),
        K("o", "o", 1.0f),
        K("p", "p", 1.0f),
        K("ü", "ü", 1.0f),
        K("+", "+", 1.0f),
        K_END},
    // 3. Zeile
    {
        K_L("^CAPSLOCK:36^", "CAPSLOCK", 1.75f),
        K("a", "a", 1.0f),
        K("s", "s", 1.0f),
        K("d", "d", 1.0f),
        K("f", "f", 1.0f),
        K("g", "g", 1.0f),
        K("h", "h", 1.0f),
        K("j", "j", 1.0f),
        K("k", "k", 1.0f),
        K("l", "l", 1.0f),
        K("ö", "ö", 1.0f),
        K("ä", "ä", 1.0f),
        K("#", "#", 1.0f),
        K_END},
    // 4. Zeile
    {
        K_L("^SHIFT:36^", "SHIFT", 1.5f),
        K("<", "<", 1.0f),
        K("y", "y", 1.0f),
        K("x", "x", 1.0f),
        K("c", "c", 1.0f),
        K("v", "v", 1.0f),
        K("b", "b", 1.0f),
        K("n", "n", 1.0f),
        K("m", "m", 1.0f),
        K(",", ",", 1.0f),
        K(".", ".", 1.0f),
        K("-", "-", 1.0f),
        K_R("^SHIFT:36^", "SHIFT", 1.5f),
        K_END},
    // 5. Zeile
    {
        K("!€1", "SYMBOLS", 1.5f),
        K("@", "@", 1.0f),
        K("", "SPACE", 6.0f),
        K(".de", ".de", 1.5f),
        K("^CLEFT:36^", "LEFT", 1.0f),
        K("^CRIGHT:36^", "RIGHT", 1.0f),
        K("OK", "OK", 2.0f),
        K_END}};

const SettingsKeyDef SettingsKeyboard::LAYOUT_UPPERCASE[SettingsKeyboard::Layout::KEYBOARD_ROWS][SettingsKeyboard::Layout::MAX_COLS] = {
    // 1. Zeile
    {
        K("°", "°", 1.0f),
        K("!", "!", 1.0f),
        K("\\", "\\", 1.0f),
        K("§", "§", 1.0f),
        K("$", "$", 1.0f),
        K("%", "%", 1.0f),
        K("&", "&", 1.0f),
        K("/", "/", 1.0f),
        K("(", "(", 1.0f),
        K(")", ")", 1.0f),
        K("=", "=", 1.0f),
        K("?", "?", 1.0f),
        K("`", "`", 1.0f),
        K("^BACKSPACE:30^", "BACKSPACE", 1.0f),
        K_END},
    // 2. Zeile
    {
        K("", "", 1.5f),
        K("Q", "Q", 1.0f),
        K("W", "W", 1.0f),
        K("E", "E", 1.0f),
        K("R", "R", 1.0f),
        K("T", "T", 1.0f),
        K("Z", "Z", 1.0f),
        K("U", "U", 1.0f),
        K("I", "I", 1.0f),
        K("O", "O", 1.0f),
        K("P", "P", 1.0f),
        K("Ü", "Ü", 1.0f),
        K("*", "*", 1.0f),
        K_END},
    // 3. Zeile
    {
        K_L("^CAPSLOCK:36^", "CAPSLOCK", 1.75f),
        K("A", "A", 1.0f),
        K("S", "S", 1.0f),
        K("D", "D", 1.0f),
        K("F", "F", 1.0f),
        K("G", "G", 1.0f),
        K("H", "H", 1.0f),
        K("J", "J", 1.0f),
        K("K", "K", 1.0f),
        K("L", "L", 1.0f),
        K("Ö", "Ö", 1.0f),
        K("Ä", "Ä", 1.0f),
        K("'", "'", 1.0f),
        K_END},
    // 4. Zeile
    {
        K_L("^SHIFT:36^", "SHIFT", 1.5f),
        K(">", ">", 1.0f),
        K("Y", "Y", 1.0f),
        K("X", "X", 1.0f),
        K("C", "C", 1.0f),
        K("V", "V", 1.0f),
        K("B", "B", 1.0f),
        K("N", "N", 1.0f),
        K("M", "M", 1.0f),
        K(";", ";", 1.0f),
        K(":", ":", 1.0f),
        K("_", "_", 1.0f),
        K_R("^SHIFT:36^", "SHIFT", 1.5f),
        K_END},
    // 5. Zeile
    {
        K("!€1", "SYMBOLS", 1.5f),
        K("@", "@", 1.0f),
        K("", "SPACE", 6.0f),
        K(".de", ".de", 1.5f),
        K("^CLEFT:36^", "LEFT", 1.0f),
        K("^CRIGHT:36^", "RIGHT", 1.0f),
        K("OK", "OK", 2.0f),
        K_END}};

const SettingsKeyDef SettingsKeyboard::LAYOUT_SYMBOLS_LOWERCASE[SettingsKeyboard::Layout::KEYBOARD_ROWS][SettingsKeyboard::Layout::MAX_COLS] = {
    // 1. Zeile
    {
        K("^", "^", 1.0f),
        K("1", "1", 1.0f),
        K_C("²", "²", 1.0f, Rolle::DlgTasteZweitzeichen),
        K_C("³", "³", 1.0f, Rolle::DlgTasteZweitzeichen),
        K("4", "4", 1.0f),
        K("5", "5", 1.0f),
        K("6", "6", 1.0f),
        K_C("{", "{", 1.0f, Rolle::DlgTasteZweitzeichen),
        K_C("[", "[", 1.0f, Rolle::DlgTasteZweitzeichen),
        K_C("]", "]", 1.0f, Rolle::DlgTasteZweitzeichen),
        K_C("}", "}", 1.0f, Rolle::DlgTasteZweitzeichen),
        K_C("\\", "\\", 1.0f, Rolle::DlgTasteZweitzeichen),
        K("´", "´", 1.0f),
        K("^BACKSPACE:30^", "BACKSPACE", 1.0f),
        K_END},
    // 2. Zeile
    {
        K("", "", 1.5f),
        K_C("@", "@", 1.0f, Rolle::DlgTasteZweitzeichen),
        K("w", "w", 1.0f),
        K_C("€", "€", 1.0f, Rolle::DlgTasteZweitzeichen),
        K("r", "r", 1.0f),
        K("t", "t", 1.0f),
        K("z", "z", 1.0f),
        K("u", "u", 1.0f),
        K("i", "i", 1.0f),
        K("o", "o", 1.0f),
        K("p", "p", 1.0f),
        K("ü", "ü", 1.0f),
        K_C("~", "~", 1.0f, Rolle::DlgTasteZweitzeichen),
        K_END},
    // 3. Zeile
    {
        K_L("^CAPSLOCK:36^", "CAPSLOCK", 1.75f),
        K("a", "a", 1.0f),
        K("s", "s", 1.0f),
        K("d", "d", 1.0f),
        K("f", "f", 1.0f),
        K("g", "g", 1.0f),
        K("h", "h", 1.0f),
        K("j", "j", 1.0f),
        K("k", "k", 1.0f),
        K("l", "l", 1.0f),
        K("ö", "ö", 1.0f),
        K("ä", "ä", 1.0f),
        K("#", "#", 1.0f),
        K_END},
    // 4. Zeile
    {
        K_L("^SHIFT:36^", "SHIFT", 1.5f),
        K_C("|", "|", 1.0f, Rolle::DlgTasteZweitzeichen),
        K("y", "y", 1.0f),
        K("x", "x", 1.0f),
        K("c", "c", 1.0f),
        K("v", "v", 1.0f),
        K("b", "b", 1.0f),
        K("n", "n", 1.0f),
        K("m", "m", 1.0f),
        K(",", ",", 1.0f),
        K(".", ".", 1.0f),
        K("-", "-", 1.0f),
        K_R("^SHIFT:36^", "SHIFT", 1.5f),
        K_END},
    // 5. Zeile
    {
        K("ABC", "LETTERS", 1.5f),
        K("@", "@", 1.0f),
        K("", "SPACE", 6.0f),
        K(".de", ".de", 1.5f),
        K("^CLEFT:36^", "LEFT", 1.0f),
        K("^CRIGHT:36^", "RIGHT", 1.0f),
        K("OK", "OK", 2.0f),
        K_END}};

const SettingsKeyDef SettingsKeyboard::LAYOUT_SYMBOLS_UPPERCASE[SettingsKeyboard::Layout::KEYBOARD_ROWS][SettingsKeyboard::Layout::MAX_COLS] = {
    // 1. Zeile
    {
        K("°", "°", 1.0f),
        K("!", "!", 1.0f),
        K_C("²", "²", 1.0f, Rolle::DlgTasteZweitzeichen),
        K_C("³", "³", 1.0f, Rolle::DlgTasteZweitzeichen),
        K("$", "$", 1.0f),
        K("%", "%", 1.0f),
        K("&", "&", 1.0f),
        K_C("{", "{", 1.0f, Rolle::DlgTasteZweitzeichen),
        K_C("[", "[", 1.0f, Rolle::DlgTasteZweitzeichen),
        K_C("]", "]", 1.0f, Rolle::DlgTasteZweitzeichen),
        K_C("}", "}", 1.0f, Rolle::DlgTasteZweitzeichen),
        K_C("\\", "\\", 1.0f, Rolle::DlgTasteZweitzeichen),
        K("`", "`", 1.0f),
        K("^BACKSPACE:30^", "BACKSPACE", 1.0f),
        K_END},
    // 2. Zeile
    {
        K("", "", 1.5f),
        K_C("@", "@", 1.0f, Rolle::DlgTasteZweitzeichen),
        K("W", "W", 1.0f),
        K_C("€", "€", 1.0f, Rolle::DlgTasteZweitzeichen),
        K("R", "R", 1.0f),
        K("T", "T", 1.0f),
        K("Z", "Z", 1.0f),
        K("U", "U", 1.0f),
        K("I", "I", 1.0f),
        K("O", "O", 1.0f),
        K("P", "P", 1.0f),
        K("Ü", "Ü", 1.0f),
        K_C("~", "~", 1.0f, Rolle::DlgTasteZweitzeichen),
        K_END},
    // 3. Zeile
    {
        K_L("^CAPSLOCK:36^", "CAPSLOCK", 1.75f),
        K("A", "A", 1.0f),
        K("S", "S", 1.0f),
        K("D", "D", 1.0f),
        K("F", "F", 1.0f),
        K("G", "G", 1.0f),
        K("H", "H", 1.0f),
        K("J", "J", 1.0f),
        K("K", "K", 1.0f),
        K("L", "L", 1.0f),
        K("Ö", "Ö", 1.0f),
        K("Ä", "Ä", 1.0f),
        K("'", "'", 1.0f),
        K_END},
    // 4. Zeile
    {
        K_L("^SHIFT:36^", "SHIFT", 1.5f),
        K_C("|", "|", 1.0f, Rolle::DlgTasteZweitzeichen),
        K("Y", "Y", 1.0f),
        K("X", "X", 1.0f),
        K("C", "C", 1.0f),
        K("V", "V", 1.0f),
        K("B", "B", 1.0f),
        K("N", "N", 1.0f),
        K("M", "M", 1.0f),
        K(";", ";", 1.0f),
        K(":", ":", 1.0f),
        K("_", "_", 1.0f),
        K_R("^SHIFT:36^", "SHIFT", 1.5f),
        K_END},
    // 5. Zeile
    {
        K("ABC", "LETTERS", 1.5f),
        K("@", "@", 1.0f),
        K("", "SPACE", 6.0f),
        K(".de", ".de", 1.5f),
        K("^CLEFT:36^", "LEFT", 1.0f),
        K("^CRIGHT:36^", "RIGHT", 1.0f),
        K("OK", "OK", 2.0f),
        K_END}};

const SettingsKeyDef SettingsKeyboard::LAYOUT_EN_LOWERCASE[SettingsKeyboard::Layout::KEYBOARD_ROWS][SettingsKeyboard::Layout::MAX_COLS] = {
    // 1. Zeile
    {
        K("`", "`", 1.0f),
        K("1", "1", 1.0f),
        K("2", "2", 1.0f),
        K("3", "3", 1.0f),
        K("4", "4", 1.0f),
        K("5", "5", 1.0f),
        K("6", "6", 1.0f),
        K("7", "7", 1.0f),
        K("8", "8", 1.0f),
        K("9", "9", 1.0f),
        K("0", "0", 1.0f),
        K("-", "-", 1.0f),
        K("=", "=", 1.0f),
        K("^BACKSPACE:30^", "BACKSPACE", 1.0f),
        K_END},
    // 2. Zeile
    {
        K("", "", 1.5f),
        K("q", "q", 1.0f),
        K("w", "w", 1.0f),
        K("e", "e", 1.0f),
        K("r", "r", 1.0f),
        K("t", "t", 1.0f),
        K("y", "y", 1.0f),
        K("u", "u", 1.0f),
        K("i", "i", 1.0f),
        K("o", "o", 1.0f),
        K("p", "p", 1.0f),
        K("ü", "ü", 1.0f),
        K("]", "]", 1.0f),
        K_END},
    // 3. Zeile
    {
        K_L("^CAPSLOCK:36^", "CAPSLOCK", 1.75f),
        K("a", "a", 1.0f),
        K("s", "s", 1.0f),
        K("d", "d", 1.0f),
        K("f", "f", 1.0f),
        K("g", "g", 1.0f),
        K("h", "h", 1.0f),
        K("j", "j", 1.0f),
        K("k", "k", 1.0f),
        K("l", "l", 1.0f),
        K("ö", "ö", 1.0f),
        K("ä", "ä", 1.0f),
        K("\\", "\\", 1.0f),
        K_END},
    // 4. Zeile
    {
        K_L("^SHIFT:36^", "SHIFT", 2.5f),
        K("z", "z", 1.0f),
        K("x", "x", 1.0f),
        K("c", "c", 1.0f),
        K("v", "v", 1.0f),
        K("b", "b", 1.0f),
        K("n", "n", 1.0f),
        K("m", "m", 1.0f),
        K(",", ",", 1.0f),
        K(".", ".", 1.0f),
        K("-", "-", 1.0f),
        K_R("^SHIFT:36^", "SHIFT", 1.5f),
        K_END},
    // 5. Zeile
    {
        K("!€1", "SYMBOLS", 1.5f),
        K("@", "@", 1.0f),
        K("", "SPACE", 6.0f),
        K(".com", ".com", 1.5f),
        K("^CLEFT:36^", "LEFT", 1.0f),
        K("^CRIGHT:36^", "RIGHT", 1.0f),
        K("OK", "OK", 2.0f),
        K_END}};

const SettingsKeyDef SettingsKeyboard::LAYOUT_EN_UPPERCASE[SettingsKeyboard::Layout::KEYBOARD_ROWS][SettingsKeyboard::Layout::MAX_COLS] = {
    // 1. Zeile
    {
        K("~", "~", 1.0f),
        K("!", "!", 1.0f),
        K("\"", "\"", 1.0f),
        K("£", "£", 1.0f),
        K("$", "$", 1.0f),
        K("%", "%", 1.0f),
        K("^", "^", 1.0f),
        K("&", "&", 1.0f),
        K("*", "*", 1.0f),
        K("(", "(", 1.0f),
        K(")", ")", 1.0f),
        K("_", "_", 1.0f),
        K("+", "+", 1.0f),
        K("^BACKSPACE:30^", "BACKSPACE", 1.0f),
        K_END},
    // 2. Zeile
    {
        K("", "", 1.5f),
        K("Q", "Q", 1.0f),
        K("W", "W", 1.0f),
        K("E", "E", 1.0f),
        K("R", "R", 1.0f),
        K("T", "T", 1.0f),
        K("Y", "Y", 1.0f),
        K("U", "U", 1.0f),
        K("I", "I", 1.0f),
        K("O", "O", 1.0f),
        K("P", "P", 1.0f),
        K("Ü", "Ü", 1.0f),
        K("}", "}", 1.0f),
        K_END},
    // 3. Zeile
    {
        K_L("^CAPSLOCK:36^", "CAPSLOCK", 1.75f),
        K("A", "A", 1.0f),
        K("S", "S", 1.0f),
        K("D", "D", 1.0f),
        K("F", "F", 1.0f),
        K("G", "G", 1.0f),
        K("H", "H", 1.0f),
        K("J", "J", 1.0f),
        K("K", "K", 1.0f),
        K("L", "L", 1.0f),
        K("Ö", "Ö", 1.0f),
        K("Ä", "Ä", 1.0f),
        K("'", "'", 1.0f),
        K_END},
    // 4. Zeile
    {
        K_L("^SHIFT:36^", "SHIFT", 2.5f),
        K("Z", "Z", 1.0f),
        K("X", "X", 1.0f),
        K("C", "C", 1.0f),
        K("V", "V", 1.0f),
        K("B", "B", 1.0f),
        K("N", "N", 1.0f),
        K("M", "M", 1.0f),
        K(";", ";", 1.0f),
        K(":", ":", 1.0f),
        K("_", "_", 1.0f),
        K_R("^SHIFT:36^", "SHIFT", 1.5f),
        K_END},
    // 5. Zeile
    {
        K("!€1", "SYMBOLS", 1.5f),
        K("@", "@", 1.0f),
        K("", "SPACE", 6.0f),
        K(".com", ".com", 1.5f),
        K("^CLEFT:36^", "LEFT", 1.0f),
        K("^CRIGHT:36^", "RIGHT", 1.0f),
        K("OK", "OK", 2.0f),
        K_END}};

const SettingsKeyDef SettingsKeyboard::LAYOUT_EN_SYMBOLS_LOWERCASE[SettingsKeyboard::Layout::KEYBOARD_ROWS][SettingsKeyboard::Layout::MAX_COLS] = {
    // 1. Zeile
    {
        K("`", "`", 1.0f),
        K("1", "1", 1.0f),
        K_C("²", "²", 1.0f, Rolle::DlgTasteZweitzeichen),
        K_C("³", "³", 1.0f, Rolle::DlgTasteZweitzeichen),
        K("4", "4", 1.0f),
        K("5", "5", 1.0f),
        K("6", "6", 1.0f),
        K_C("{", "{", 1.0f, Rolle::DlgTasteZweitzeichen),
        K_C("[", "[", 1.0f, Rolle::DlgTasteZweitzeichen),
        K_C("]", "]", 1.0f, Rolle::DlgTasteZweitzeichen),
        K_C("}", "}", 1.0f, Rolle::DlgTasteZweitzeichen),
        K_C("\\", "\\", 1.0f, Rolle::DlgTasteZweitzeichen),
        K("=", "=", 1.0f),
        K("^BACKSPACE:30^", "BACKSPACE", 1.0f),
        K_END},
    // 2. Zeile
    {
        K("", "", 1.5f),
        K_C("@", "@", 1.0f, Rolle::DlgTasteZweitzeichen),
        K("w", "w", 1.0f),
        K_C("€", "€", 1.0f, Rolle::DlgTasteZweitzeichen),
        K("r", "r", 1.0f),
        K("t", "t", 1.0f),
        K("y", "y", 1.0f),
        K("u", "u", 1.0f),
        K("i", "i", 1.0f),
        K("o", "o", 1.0f),
        K("p", "p", 1.0f),
        K("ü", "ü", 1.0f),
        K_C("~", "~", 1.0f, Rolle::DlgTasteZweitzeichen),
        K_END},
    // 3. Zeile
    {
        K_L("^CAPSLOCK:36^", "CAPSLOCK", 1.75f),
        K("a", "a", 1.0f),
        K("s", "s", 1.0f),
        K("d", "d", 1.0f),
        K("f", "f", 1.0f),
        K("g", "g", 1.0f),
        K("h", "h", 1.0f),
        K("j", "j", 1.0f),
        K("k", "k", 1.0f),
        K("l", "l", 1.0f),
        K("ö", "ö", 1.0f),
        K("ä", "ä", 1.0f),
        K("\\", "\\", 1.0f),
        K_END},
    // 4. Zeile
    {
        K_L("^SHIFT:36^", "SHIFT", 2.5f),
        K("z", "z", 1.0f),
        K("x", "x", 1.0f),
        K("c", "c", 1.0f),
        K("v", "v", 1.0f),
        K("b", "b", 1.0f),
        K("n", "n", 1.0f),
        K("m", "m", 1.0f),
        K(",", ",", 1.0f),
        K(".", ".", 1.0f),
        K("-", "-", 1.0f),
        K_R("^SHIFT:36^", "SHIFT", 1.5f),
        K_END},
    // 5. Zeile
    {
        K("ABC", "LETTERS", 1.5f),
        K("@", "@", 1.0f),
        K("", "SPACE", 6.0f),
        K(".com", ".com", 1.5f),
        K("^CLEFT:36^", "LEFT", 1.0f),
        K("^CRIGHT:36^", "RIGHT", 1.0f),
        K("OK", "OK", 2.0f),
        K_END}};

const SettingsKeyDef SettingsKeyboard::LAYOUT_EN_SYMBOLS_UPPERCASE[SettingsKeyboard::Layout::KEYBOARD_ROWS][SettingsKeyboard::Layout::MAX_COLS] = {
    // 1. Zeile
    {
        K("~", "~", 1.0f),
        K("!", "!", 1.0f),
        K_C("²", "²", 1.0f, Rolle::DlgTasteZweitzeichen),
        K_C("³", "³", 1.0f, Rolle::DlgTasteZweitzeichen),
        K("$", "$", 1.0f),
        K("%", "%", 1.0f),
        K("^", "^", 1.0f),
        K_C("{", "{", 1.0f, Rolle::DlgTasteZweitzeichen),
        K_C("[", "[", 1.0f, Rolle::DlgTasteZweitzeichen),
        K_C("]", "]", 1.0f, Rolle::DlgTasteZweitzeichen),
        K_C("}", "}", 1.0f, Rolle::DlgTasteZweitzeichen),
        K_C("\\", "\\", 1.0f, Rolle::DlgTasteZweitzeichen),
        K("+", "+", 1.0f),
        K("^BACKSPACE:30^", "BACKSPACE", 1.0f),
        K_END},
    // 2. Zeile
    {
        K("", "", 1.5f),
        K_C("@", "@", 1.0f, Rolle::DlgTasteZweitzeichen),
        K("W", "W", 1.0f),
        K_C("€", "€", 1.0f, Rolle::DlgTasteZweitzeichen),
        K("R", "R", 1.0f),
        K("T", "T", 1.0f),
        K("Y", "Y", 1.0f),
        K("U", "U", 1.0f),
        K("I", "I", 1.0f),
        K("O", "O", 1.0f),
        K("P", "P", 1.0f),
        K("Ü", "Ü", 1.0f),
        K_C("~", "~", 1.0f, Rolle::DlgTasteZweitzeichen),
        K_END},
    // 3. Zeile
    {
        K_L("^CAPSLOCK:36^", "CAPSLOCK", 1.75f),
        K("A", "A", 1.0f),
        K("S", "S", 1.0f),
        K("D", "D", 1.0f),
        K("F", "F", 1.0f),
        K("G", "G", 1.0f),
        K("H", "H", 1.0f),
        K("J", "J", 1.0f),
        K("K", "K", 1.0f),
        K("L", "L", 1.0f),
        K("Ö", "Ö", 1.0f),
        K("Ä", "Ä", 1.0f),
        K("'", "'", 1.0f),
        K_END},
    // 4. Zeile
    {
        K_L("^SHIFT:36^", "SHIFT", 2.5f),
        K("Z", "Z", 1.0f),
        K("X", "X", 1.0f),
        K("C", "C", 1.0f),
        K("V", "V", 1.0f),
        K("B", "B", 1.0f),
        K("N", "N", 1.0f),
        K("M", "M", 1.0f),
        K(";", ";", 1.0f),
        K(":", ":", 1.0f),
        K("_", "_", 1.0f),
        K_R("^SHIFT:36^", "SHIFT", 1.5f),
        K_END},
    // 5. Zeile
    {
        K("ABC", "LETTERS", 1.5f),
        K("@", "@", 1.0f),
        K("", "SPACE", 6.0f),
        K(".com", ".com", 1.5f),
        K("^CLEFT:36^", "LEFT", 1.0f),
        K("^CRIGHT:36^", "RIGHT", 1.0f),
        K("OK", "OK", 2.0f),
        K_END}};

#undef K
#undef K_C
#undef K_L
#undef K_R
#undef K_ICON
#undef K_ICON_C
#undef K_END

// Eingabelebenzyklus

bool SettingsKeyboard::open(const char *title, const char *initial, size_t maxBytes, Mode mode, LangIndex language)
{
    close();
    if (!title || !initial || !maxBytes || maxBytes > 64 || !SettingsUi::validText(title, 96) || !SettingsUi::validText(initial, maxBytes + 1))
        return false;
    stringutils::safeCopy(m_title, title);
    stringutils::safeCopy(m_value, initial);
    m_limit = maxBytes;
    m_cursor = strlen(m_value);
    m_mode = mode;
    m_language = language;
    m_active = m_changed = m_startPending = true;
    m_state = KeyboardState::LOWERCASE;
    m_capsLock = m_passwordVisible = m_hasReleased = false;
    m_blink = false;
    m_error = SettingsKey::InputHint;
    m_inputDirty = m_titleDirty = true;
    m_drawnPressed = -1;
    buildLayout();
    return true;
}

void SettingsKeyboard::close()
{
    volatile char *p = m_value;
    for (size_t i = 0; i < sizeof(m_value); ++i)
        p[i] = 0;
    m_active = false;
    m_canvas = nullptr;
    m_cursor = 0;
    m_keyCount = 0;
    cancelTouch();
}

void SettingsKeyboard::cancelTouch()
{
    m_contact = false;
    m_pressed = -1;
    m_repeatAction = nullptr;
}

void SettingsKeyboard::setError(SettingsKey key)
{
    m_error = key;
    m_changed = m_titleDirty = true;
}

bool SettingsKeyboard::symbolState() const
{
    return m_state == KeyboardState::SYMBOLS_LOWERCASE || m_state == KeyboardState::SYMBOLS_UPPERCASE;
}

void SettingsKeyboard::buildLayout()
{
    const bool en = m_language == LangIndex::EN;
    const SettingsKeyDef(*layout)[Layout::MAX_COLS] = en ? LAYOUT_EN_LOWERCASE : LAYOUT_LOWERCASE;
    if (m_state == KeyboardState::UPPERCASE)
        layout = en ? LAYOUT_EN_UPPERCASE : LAYOUT_UPPERCASE;
    else if (m_state == KeyboardState::SYMBOLS_LOWERCASE)
        layout = en ? LAYOUT_EN_SYMBOLS_LOWERCASE : LAYOUT_SYMBOLS_LOWERCASE;
    else if (m_state == KeyboardState::SYMBOLS_UPPERCASE)
        layout = en ? LAYOUT_EN_SYMBOLS_UPPERCASE : LAYOUT_SYMBOLS_UPPERCASE;
    m_keyCount = 0;
    for (int row = 0; row < Layout::KEYBOARD_ROWS; ++row)
    {
        int x = Layout::DEFAULT_X + Layout::PADDING;
        for (int col = 0; col < Layout::MAX_COLS && layout[row][col].label; ++col)
        {
            const auto &def = layout[row][col];
            if (def.width_units <= 0)
                continue;
            const int width = static_cast<int>(std::round(def.width_units * Layout::UNIT_WIDTH + (def.width_units - 1.0f) * Layout::KEY_GAP));
            if (def.action && def.action[0])
                m_keys[m_keyCount++] = {&def, {x, Layout::DEFAULT_Y + Layout::KEYBOARD_Y_REL + row * (Layout::KEY_HEIGHT + Layout::KEY_GAP), width, Layout::KEY_HEIGHT}};
            x += width + Layout::KEY_GAP;
        }
    }
    m_changed = m_keysDirty = true;
}

SettingsUi::Rect SettingsKeyboard::inputRect() const
{
    const bool password = m_mode == Mode::Password;
    return {Layout::DEFAULT_X + Layout::PADDING + (password ? Layout::INPUT_HEIGHT + Layout::KEY_GAP : 0), Layout::DEFAULT_Y + Layout::INPUT_Y_REL, Layout::KEYBOARD_WIDTH - 2 * Layout::PADDING - (password ? Layout::INPUT_HEIGHT + Layout::KEY_GAP : 0), Layout::INPUT_HEIGHT};
}

uint16_t SettingsKeyboard::keyColor(const char *a) const
{
    if (!a)
        return farbe(Rolle::DlgTaste);
    if (!strcmp(a, ACTION_SHIFT) && (m_state == KeyboardState::UPPERCASE || m_state == KeyboardState::SYMBOLS_UPPERCASE))
        return farbe(Rolle::DlgTasteAktiv);
    if (!strcmp(a, ACTION_CAPSLOCK) && m_capsLock)
        return farbe(Rolle::DlgTasteAktiv);
    const char *special[] = {ACTION_BACKSPACE, ACTION_SHIFT, ACTION_CAPSLOCK, ACTION_SYMBOLS, ACTION_LETTERS, ACTION_SPACE, ACTION_OK, ACTION_CANCEL, ACTION_LEFT, ACTION_RIGHT};
    for (auto k : special)
        if (!strcmp(a, k))
            return farbe(Rolle::DlgTasteSonder);
    return farbe(Rolle::DlgTaste);
}

SettingsUi::Rect SettingsKeyboard::closeRect()
{
    return {Layout::DEFAULT_X + Layout::KEYBOARD_WIDTH - K::CLOSE_RIGHT, Layout::DEFAULT_Y + K::CLOSE_Y, K::CLOSE, K::CLOSE};
}

SettingsUi::Rect SettingsKeyboard::eyeRect() const
{
    return {Layout::DEFAULT_X + Layout::PADDING, inputRect().y, Layout::INPUT_HEIGHT, Layout::INPUT_HEIGHT};
}

SettingsUi::Rect SettingsKeyboard::drawTitle(UiCanvas &c) const
{
    // Titelzeile bis vor die Schließen-Taste, überlange Titel endeten auch im Original unter der Taste.
    const Rect strip{Layout::DEFAULT_X + Layout::PADDING, Layout::DEFAULT_Y + Layout::PADDING, closeRect().x - Layout::DEFAULT_X - 2 * Layout::PADDING, Layout::LABEL_HEIGHT};
    const uint16_t grund = farbe(Rolle::GemGrund);
    c.fillRect(strip.x, strip.y, strip.w, strip.h, grund);
    c.setClipRect(strip.x, strip.y, strip.w, strip.h);
    c.loadFont(ComfortaaBold27);
    c.setTextDatum(ML_DATUM);
    c.setTextColor(farbe(Rolle::GemTitel), grund);
    c.drawString(m_error == SettingsKey::InputHint ? m_title : settingsText(m_error, m_language), Layout::DEFAULT_X + Layout::PADDING, Layout::DEFAULT_Y + K::TITLE_Y);
    c.clearClipRect();
    c.unloadFont();
    return strip;
}

SettingsUi::Rect SettingsKeyboard::drawInput(UiCanvas &c) const
{
    const Rect field = inputRect(), line{Layout::DEFAULT_X + Layout::PADDING, field.y, Layout::KEYBOARD_WIDTH - 2 * Layout::PADDING, Layout::INPUT_HEIGHT};
    const bool password = m_mode == Mode::Password;
    c.fillRect(line.x, line.y, line.w, line.h, farbe(Rolle::GemGrund));
    if (password)
        drawKey(c, 101);
    c.fillSmoothRoundRect(field.x, field.y, field.w, field.h, Layout::KEY_RADIUS, farbe(Rolle::DlgEingabeFeld));
    c.loadFont(ComfortaaBold27);
    c.setTextDatum(ML_DATUM);
    c.setTextColor(farbe(Rolle::GemTextHell));
    c.setClipRect(field.x, field.y, field.w, field.h);
    char shown[65], prefix[65];
    const size_t length = strlen(m_value);
    if (password && !m_passwordVisible)
    {
        memset(shown, '*', length);
        shown[length] = 0;
        memset(prefix, '*', m_cursor);
        prefix[m_cursor] = 0;
    }
    else
    {
        memcpy(shown, m_value, length + 1);
        memcpy(prefix, m_value, m_cursor);
        prefix[m_cursor] = 0;
    }
    c.drawString(shown, field.x + K::TEXT_X, field.y + field.h / 2 + (password && !m_passwordVisible ? K::HIDDEN_DY : 0));
    if (m_blink)
        c.fillRect(field.x + K::TEXT_X + c.textWidth(prefix), field.y + K::CURSOR_INSET, K::CURSOR_W, field.h - 2 * K::CURSOR_INSET, farbe(Rolle::DlgCursor));
    memset(shown, 0, sizeof(shown));
    memset(prefix, 0, sizeof(prefix));
    c.clearClipRect();
    c.unloadFont();
    return line;
}

SettingsUi::Rect SettingsKeyboard::drawKey(UiCanvas &c, int index) const
{
    // Jede Taste liegt auf dem einfarbigen Tastaturgrund. Erst den Grund herstellen, damit die weichen Kanten nicht bei jedem Druck nachdunkeln.
    Rect r{0, 0, 0, 0};
    const uint16_t grund = farbe(Rolle::GemGrund);
    if (index == 100)
    {
        r = closeRect();
        c.fillRect(r.x, r.y, r.w, r.h, grund);
        dialogButton(c, r, "^DEL:30^", farbe(Rolle::DlgSchliessen), farbe(Rolle::DlgSchliessen), farbe(Rolle::GemTextHell), m_pressed == 100);
    }
    else if (index == 101)
    {
        if (m_mode != Mode::Password)
            return r;
        r = eyeRect();
        c.fillRect(r.x, r.y, r.w, r.h, grund);
        dialogButton(c, r, m_passwordVisible ? "^EYEON:30^" : "^EYEOFF:30^", farbe(Rolle::DlgTasteSonder), farbe(Rolle::DlgTasteSonder), farbe(Rolle::GemText), m_pressed == 101);
    }
    else if (index >= 0 && static_cast<size_t>(index) < m_keyCount)
    {
        const auto &key = m_keys[index];
        const auto &d = *key.definition;
        r = key.rect;
        c.fillRect(r.x, r.y, r.w, r.h, grund);
        dialogButton(c, r, d.label, grund, keyColor(d.action), farbe(d.labelRolle), m_pressed == index, ComfortaaBold27, d.labelDatum);
    }
    c.unloadFont();
    return r;
}

void SettingsKeyboard::draw(UiCanvas &c)
{
    if (!m_active)
        return;
    m_canvas = &c;
    c.fillSmoothRoundRect(Layout::DEFAULT_X, Layout::DEFAULT_Y, Layout::KEYBOARD_WIDTH, Layout::TOTAL_HEIGHT, Layout::BORDER_RADIUS, farbe(Rolle::GemDialogRahmen));
    c.fillSmoothRoundRect(Layout::DEFAULT_X + K::BORDER, Layout::DEFAULT_Y + K::BORDER, Layout::KEYBOARD_WIDTH - 2 * K::BORDER, Layout::TOTAL_HEIGHT - 2 * K::BORDER, Layout::BORDER_RADIUS, farbe(Rolle::GemGrund));
    drawTitle(c);
    drawKey(c, 100);
    drawInput(c);
    for (size_t i = 0; i < m_keyCount; ++i)
        drawKey(c, static_cast<int>(i));
    m_keysDirty = m_inputDirty = m_titleDirty = false;
    m_drawnPressed = m_pressed;
}

size_t SettingsKeyboard::drawChanges(UiCanvas &c, SettingsUi::Rect (&changed)[MaxChanges])
{
    if (!m_active)
        return 0;
    m_canvas = &c;
    size_t count = 0;
    const auto add = [&](const Rect &r)
    {
        if (r.w > 0 && count < MaxChanges)
            changed[count++] = r;
    };
    if (m_titleDirty)
        add(drawTitle(c));
    if (m_keysDirty)
    {
        const Rect keys{Layout::DEFAULT_X + Layout::PADDING, Layout::DEFAULT_Y + Layout::KEYBOARD_Y_REL, Layout::KEYBOARD_WIDTH - 2 * Layout::PADDING, Layout::KEYBOARD_PART_HEIGHT};
        c.fillRect(keys.x, keys.y, keys.w, keys.h, farbe(Rolle::GemGrund));
        for (size_t i = 0; i < m_keyCount; ++i)
            drawKey(c, static_cast<int>(i));
        add(keys);
        if (m_drawnPressed == 100 || m_pressed == 100)
            add(drawKey(c, 100));
    }
    else if (m_drawnPressed != m_pressed)
    {
        // Die Sichtbarkeitstaste liegt in der Eingabezeile und entsteht dort ohnehin neu.
        if (m_drawnPressed >= 0 && !(m_inputDirty && m_drawnPressed == 101))
            add(drawKey(c, m_drawnPressed));
        if (m_pressed >= 0 && !(m_inputDirty && m_pressed == 101))
            add(drawKey(c, m_pressed));
    }
    if (m_inputDirty)
        add(drawInput(c));
    m_keysDirty = m_inputDirty = m_titleDirty = false;
    m_drawnPressed = m_pressed;
    return count;
}

bool SettingsKeyboard::append(const char *value)
{
    if (!m_active || !value || !SettingsUi::validText(value, 65))
        return false;
    const size_t n = strlen(value), length = strlen(m_value);
    if (n + length > m_limit)
        return false;
    memmove(m_value + m_cursor + n, m_value + m_cursor, length - m_cursor + 1);
    memcpy(m_value + m_cursor, value, n);
    m_cursor += n;
    m_changed = m_inputDirty = true;
    return true;
}

void SettingsKeyboard::backspace()
{
    if (!m_cursor)
        return;
    size_t previous = m_cursor - 1;
    while (previous && (static_cast<unsigned char>(m_value[previous]) & 0xc0) == 0x80)
        --previous;
    memmove(m_value + previous, m_value + m_cursor, strlen(m_value + m_cursor) + 1);
    m_cursor = previous;
    m_changed = m_inputDirty = true;
}

void SettingsKeyboard::moveCursor(int direction)
{
    if (direction < 0 && m_cursor)
    {
        --m_cursor;
        while (m_cursor && (static_cast<unsigned char>(m_value[m_cursor]) & 0xc0) == 0x80)
            --m_cursor;
    }
    else if (direction > 0 && m_cursor < strlen(m_value))
    {
        ++m_cursor;
        while ((static_cast<unsigned char>(m_value[m_cursor]) & 0xc0) == 0x80)
            ++m_cursor;
    }
    m_changed = m_inputDirty = true;
}

size_t SettingsKeyboard::cursorFromTouch(int x) const
{
    if (!m_canvas)
        return m_cursor;
    m_canvas->loadFont(ComfortaaBold27);
    const int wanted = x - inputRect().x - K::TEXT_X;
    int before = 0;
    size_t previous = 0;
    char prefix[65]{};
    const bool hidden = m_mode == Mode::Password && !m_passwordVisible;
    for (size_t n = 1; n <= strlen(m_value); ++n)
    {
        if ((static_cast<unsigned char>(m_value[n]) & 0xc0) == 0x80)
            continue;
        if (hidden)
            memset(prefix, '*', n);
        else
            memcpy(prefix, m_value, n);
        prefix[n] = 0;
        const int after = m_canvas->textWidth(prefix);
        if (wanted < (before + after) / 2)
        {
            m_canvas->unloadFont();
            return previous;
        }
        previous = n;
        before = after;
    }
    m_canvas->unloadFont();
    return previous;
}

SettingsKeyboard::Result SettingsKeyboard::processKey(const char *a)
{
    if (!strcmp(a, ACTION_OK))
        return Result::Accepted;
    if (!strcmp(a, ACTION_CANCEL))
        return Result::Cancelled;
    if (!strcmp(a, ACTION_BACKSPACE))
        backspace();
    else if (!strcmp(a, ACTION_LEFT))
        moveCursor(-1);
    else if (!strcmp(a, ACTION_RIGHT))
        moveCursor(1);
    else if (!strcmp(a, ACTION_SHIFT))
    {
        if (m_capsLock)
        {
            m_capsLock = false;
            m_state = symbolState() ? KeyboardState::SYMBOLS_LOWERCASE : KeyboardState::LOWERCASE;
        }
        else if (m_state == KeyboardState::LOWERCASE)
            m_state = KeyboardState::UPPERCASE;
        else if (m_state == KeyboardState::UPPERCASE)
            m_state = KeyboardState::LOWERCASE;
        else
            m_state = m_state == KeyboardState::SYMBOLS_LOWERCASE ? KeyboardState::SYMBOLS_UPPERCASE : KeyboardState::SYMBOLS_LOWERCASE;
        buildLayout();
    }
    else if (!strcmp(a, ACTION_CAPSLOCK))
    {
        m_capsLock = !m_capsLock;
        m_state = symbolState() ? (m_capsLock ? KeyboardState::SYMBOLS_UPPERCASE : KeyboardState::SYMBOLS_LOWERCASE) : (m_capsLock ? KeyboardState::UPPERCASE : KeyboardState::LOWERCASE);
        buildLayout();
    }
    else if (!strcmp(a, ACTION_SYMBOLS))
    {
        m_state = m_capsLock ? KeyboardState::SYMBOLS_UPPERCASE : KeyboardState::SYMBOLS_LOWERCASE;
        buildLayout();
    }
    else if (!strcmp(a, ACTION_LETTERS))
    {
        m_state = m_capsLock ? KeyboardState::UPPERCASE : KeyboardState::LOWERCASE;
        buildLayout();
    }
    else
    {
        append(!strcmp(a, ACTION_SPACE) ? " " : a);
        if (!m_capsLock && (m_state == KeyboardState::UPPERCASE || m_state == KeyboardState::SYMBOLS_UPPERCASE))
        {
            m_state = symbolState() ? KeyboardState::SYMBOLS_LOWERCASE : KeyboardState::LOWERCASE;
            buildLayout();
        }
    }
    return Result::None;
}

bool SettingsKeyboard::update(uint32_t now)
{
    m_now = now;
    if (!m_active)
        return false;
    if (m_startPending)
    {
        m_startedAt = now;
        m_startPending = false;
    }
    const bool blink = (now / 500) % 2 == 0;
    if (blink != m_blink)
    {
        m_blink = blink;
        m_changed = m_inputDirty = true;
    }
    if (m_contact && repeatable(m_repeatAction) && now - m_pressedAt > 400 && now - m_lastRepeat > 80)
    {
        processKey(m_repeatAction);
        m_lastRepeat = now;
    }
    const bool changed = m_changed;
    m_changed = false;
    return changed;
}

SettingsKeyboard::Result SettingsKeyboard::touch(Phase phase, int x, int y)
{
    if (!m_active)
        return Result::None;
    if (phase == Phase::Cancel)
    {
        cancelTouch();
        m_changed = true;
        return Result::None;
    }
    if (phase == Phase::Release)
    {
        cancelTouch();
        m_lastRelease = m_now;
        m_hasReleased = true;
        m_changed = true;
        return Result::None;
    }
    if (phase != Phase::Press || m_contact || m_startPending || m_now - m_startedAt < 150 || (m_hasReleased && m_now - m_lastRelease < 30))
        return Result::None;
    m_contact = true;
    m_pressedAt = m_lastRepeat = m_now;
    m_changed = true;
    if (closeRect().contains(x, y))
    {
        m_pressed = 100;
        return Result::Cancelled;
    }
    if (inputRect().contains(x, y))
    {
        m_cursor = cursorFromTouch(x);
        m_inputDirty = true;
        return Result::None;
    }
    if (m_mode == Mode::Password && eyeRect().contains(x, y))
    {
        m_pressed = 101;
        m_passwordVisible = !m_passwordVisible;
        m_inputDirty = true;
        return Result::None;
    }
    for (size_t i = 0; i < m_keyCount; ++i)
        if (m_keys[i].rect.contains(x, y))
        {
            m_pressed = i;
            m_repeatAction = m_keys[i].definition->action;
            return processKey(m_repeatAction);
        }
    return Result::None;
}

void SettingsWifiList::open(LangIndex language, Kind kind)
{
    m_language = language;
    m_kind = kind;
    m_state = State::Scanning;
    m_count = 0;
    m_selected = -1;
    m_scroll = 0;
    m_startPending = true;
    m_lastScan = m_now;
    cancelTouch();
}

void SettingsWifiList::update(uint32_t now)
{
    m_now = now;
    if (m_startPending)
    {
        m_startedAt = now;
        m_startPending = false;
    }
}

void SettingsWifiList::cancelTouch()
{
    m_contact = m_listContact = m_scrolling = false;
    m_pressed = -1;
}

int SettingsWifiList::pressed() const
{
    return m_pressed + (m_scroll + 1) * 100 + (m_selected + 1) * 10000;
}

void SettingsWifiList::fail(SettingsKey key)
{
    m_state = State::Failed;
    m_error = key;
    m_count = 0;
    cancelTouch();
}

bool SettingsWifiList::setNetworks(const Network *networks, size_t count, bool more)
{
    if (count > MaxNetworks || (count && !networks))
        return false;
    for (size_t i = 0; i < count; ++i)
        if (!SettingsUi::validText(networks[i].ssid, 33) || !networks[i].ssid[0])
            return false;
    // Sortieren durch Einfügen ist stabil und braucht keinen Hilfsspeicher. origin() führt jede Zeile auf den Eintrag des Aufrufers zurück, so findet die Bluetooth-Seite die Adresse.
    uint8_t order[MaxNetworks];
    for (size_t i = 0; i < count; ++i)
    {
        size_t j = i;
        for (; j > 0 && networks[order[j - 1]].rssi < networks[i].rssi; --j)
            order[j] = order[j - 1];
        order[j] = static_cast<uint8_t>(i);
    }
    for (size_t i = 0; i < count; ++i)
    {
        m_networks[i] = networks[order[i]];
        m_origin[i] = order[i];
    }
    m_count = count;
    m_more = more;
    m_state = State::Ready;
    m_selected = count ? 0 : -1;
    m_scroll = 0;
    cancelTouch();
    return true;
}

void SettingsWifiList::draw(UiCanvas &c) const
{
    c.fillSmoothRoundRect(N::X, N::Y, N::W, N::H, N::R, farbe(Rolle::GemDialogRahmen));
    c.fillSmoothRoundRect(N::X + N::BORDER, N::Y + N::BORDER, N::W - 2 * N::BORDER, N::H - 2 * N::BORDER, N::R, farbe(Rolle::GemGrund));
    drawContent(c);
}

void SettingsWifiList::drawContent(UiCanvas &c) const
{
    constexpr int X = N::X, Y = N::Y, W = N::W, H = N::H, LX = X + N::LIST_X, LY = Y + N::LIST_Y, LW = N::LIST_WIDTH, LH = N::LIST_HEIGHT, CORNER = N::BORDER + N::R;
    const uint16_t grund = farbe(Rolle::GemGrund), titel = farbe(Rolle::GemTitel);
    // Innenfläche ohne die abgerundeten Ecken erneuern, so bleiben deren weiche Kanten bei jedem Neuzeichnen unverändert.
    c.fillRect(X + CORNER, Y + N::BORDER, W - 2 * CORNER, H - 2 * N::BORDER, grund);
    c.fillRect(X + N::BORDER, Y + CORNER, W - 2 * N::BORDER, H - 2 * CORNER, grund);
    c.loadFont(ComfortaaBold27);
    c.setTextDatum(ML_DATUM);
    c.setTextColor(titel, grund);
    const bool devices = m_kind == Kind::Devices;
    c.drawString(settingsUiText(devices ? SettingsTextKey::BT_SCAN_TITLE : SettingsTextKey::WIFI_SCAN_TITLE, m_language), X + N::TITLE_X, Y + N::TITLE_Y);
    dialogButton(c, {X + W - N::CLOSE_RIGHT, Y + N::CLOSE_Y, N::CLOSE, N::CLOSE}, "^DEL:30^", farbe(Rolle::DlgSchliessen), farbe(Rolle::DlgSchliessen), farbe(Rolle::GemTextHell), m_pressed == 100);
    dialogButton(c, {X + W - N::OK_RIGHT, Y + H - N::BUTTON_DY, N::BUTTON_W, N::BUTTON_H}, "ok", farbe(Rolle::GemRahmen), farbe(Rolle::GemTastenFlaeche), farbe(Rolle::GemDialogText), m_pressed == 101);
    dialogButton(c, {X + W - N::SCAN_RIGHT, Y + H - N::BUTTON_DY, N::BUTTON_W, N::BUTTON_H}, "scan", farbe(Rolle::GemRahmen), farbe(Rolle::GemTastenFlaeche), farbe(Rolle::GemTextGedaempft), m_pressed == 102);
    c.loadFont(ComfortaaBold27);
    c.setClipRect(LX, LY, LW, LH);
    if (m_state != State::Ready || !m_count)
    {
        c.setTextDatum(MC_DATUM);
        c.setTextColor(titel);
        if (devices && m_state != State::Failed)
            c.drawString(settingsUiText(m_state == State::Scanning ? SettingsTextKey::BT_SCANNING : SettingsTextKey::BT_NONE_FOUND, m_language), LX + LW / 2, LY + LH / 2);
        else
            c.drawString(settingsText(m_state == State::Scanning ? SettingsKey::Scanning : m_state == State::Failed ? m_error
                                                                                                                    : SettingsKey::NoNetwork,
                                      m_language),
                         LX + LW / 2, LY + LH / 2);
    }
    const uint16_t auswahl = farbe(Rolle::GemAuswahl), textHell = farbe(Rolle::GemTextHell), text = farbe(Rolle::GemDialogText), trenner = farbe(Rolle::DlgListenTrenner);
    const uint16_t netzStark = farbe(Rolle::DlgNetzStark), netzMittel = farbe(Rolle::DlgNetzMittel), netzSchwach = farbe(Rolle::DlgNetzSchwach);
    for (int i = m_scroll; i < static_cast<int>(m_count) && i < m_scroll + static_cast<int>(PageSize); ++i)
    {
        const int y = LY + (i - m_scroll) * N::ROW_H;
        const bool selected = i == m_selected;
        const auto &net = m_networks[i];
        const uint16_t bg = selected ? auswahl : grund, col = selected ? textHell : text;
        if (selected)
            c.fillRect(LX, y, LW, N::ROW_H, bg);
        c.setTextColor(col);
        c.setTextDatum(ML_DATUM);
        c.drawString(net.ssid, LX + N::ROW_TEXT_X, y + N::ROW_H / 2);
        if (net.encrypted)
        {
            // Schloss nativ. Original Bügel Radius 5, Stärke 2 bei cy-3, Körper 17x12 bei (cx-8,cy-2) mit 1 px Rand, Loch Radius 2 bei cy+3
            const int cx = LX + LW - N::LOCK_RIGHT, cy = y + N::LOCK_Y;
            c.drawSmoothCircle(cx, cy - 5, 7.5f, 3, col, bg);
            c.fillSmoothRoundRect(cx - 12, cy - 3, 26, 18, 3, col);
            c.fillSmoothRoundRect(cx - 10, cy - 1, 22, 14, 3, bg);
            c.fillSmoothCircle(cx, cy + 5, 3, col);
        }
        char signal[16];
        snprintf(signal, sizeof(signal), "%d%%", std::clamp(2 * (net.rssi + 100), 0, 100));
        c.setTextColor(net.rssi >= -50 ? netzStark : net.rssi >= -70 ? netzMittel
                                                                     : netzSchwach);
        c.setTextDatum(MR_DATUM);
        c.drawString(signal, LX + LW - N::SIGNAL_RIGHT, y + N::ROW_H / 2);
        // Trennlinie beidseitig 8 eingerückt (Original 5 und Breite LW-10)
        if (!selected && i < static_cast<int>(m_count) - 1)
            c.fillRect(LX + N::SEP_X, y + N::ROW_H - SettingsTab5Layout::Style::LINE, LW - 2 * N::SEP_X, SettingsTab5Layout::Style::LINE, trenner);
    }
    c.clearClipRect();
    if (m_count > PageSize)
    {
        const int h = std::max(N::SCROLL_MIN_H, LH * static_cast<int>(PageSize) / static_cast<int>(m_count));
        c.fillRect(X + W - N::SCROLL_RIGHT, LY, N::SCROLL_W, LH, grund);
        c.fillSmoothRoundRect(X + W - N::SCROLL_RIGHT, LY + (LH - h) * m_scroll / (static_cast<int>(m_count) - static_cast<int>(PageSize)), N::SCROLL_W, h, N::SCROLL_R, farbe(Rolle::DlgListeScrollDaumen));
    }
    c.unloadFont();
}

SettingsWifiList::Result SettingsWifiList::touch(Phase phase, int x, int y)
{
    constexpr Rect list{N::X + N::LIST_X, N::Y + N::LIST_Y, N::LIST_WIDTH, N::LIST_HEIGHT};
    if (phase == Phase::Cancel)
    {
        cancelTouch();
        return {};
    }
    if (phase == Phase::Press)
    {
        if (m_startPending || m_now - m_startedAt < 150)
            return {};
        m_contact = true;
        m_startY = y;
        m_startScroll = m_scroll;
        m_listContact = list.contains(x, y) && m_state == State::Ready;
        m_scrolling = false;
        if (Rect{N::X + N::W - N::CLOSE_RIGHT, N::Y + N::CLOSE_Y, N::CLOSE, N::CLOSE}.contains(x, y))
        {
            m_pressed = 100;
            return {Action::Cancel, -1};
        }
        if (Rect{N::X + N::W - N::OK_RIGHT, N::Y + N::H - N::BUTTON_DY, N::BUTTON_W, N::BUTTON_H}.contains(x, y) && m_selected >= 0)
        {
            m_pressed = 101;
            return {Action::Selected, m_selected};
        }
        if (Rect{N::X + N::W - N::SCAN_RIGHT, N::Y + N::H - N::BUTTON_DY, N::BUTTON_W, N::BUTTON_H}.contains(x, y) && m_now - m_lastScan > 2000)
        {
            m_pressed = 102;
            return {Action::Rescan, -1};
        }
        m_pressed = -1;
        if (m_listContact)
        {
            const int index = m_scroll + (y - list.y) / N::ROW_H;
            if (index < static_cast<int>(m_count))
                m_selected = index;
        }
    }
    else if (phase == Phase::Move && m_contact && m_listContact)
    {
        const int delta = m_startY - y;
        if (std::abs(delta) > N::DRAG)
            m_scrolling = true;
        if (m_scrolling)
            m_scroll = std::clamp(m_startScroll + delta / N::ROW_H, 0, std::max(0, static_cast<int>(m_count) - static_cast<int>(PageSize)));
    }
    else if (phase == Phase::Release && m_contact)
    {
        cancelTouch();
    }
    return {};
}

void SettingsMessageBox::open(const char *title, const char *line1, const char *line2, const char *const *buttons, size_t count)
{
    close();
    if (!title || !line1 || !line2 || !buttons || !count || count > 3)
        return;
    stringutils::safeCopy(m_title, title);
    snprintf(m_message, sizeof(m_message), "%s%s%s", line1, line2[0] ? "\n" : "", line2);
    m_count = count;
    for (size_t i = 0; i < count; ++i)
        stringutils::safeCopy(m_buttons[i], buttons[i]);
    m_active = true;
}

void SettingsMessageBox::close()
{
    m_active = m_pending = m_layoutValid = false;
    m_loading[0] = 0;
    cancelTouch();
}

void SettingsMessageBox::setBounds(int x, int y, int w, int h)
{
    if (x >= 0 && y >= 0 && w >= 180 && h >= 180 && x + w <= 1280 && y + h <= 720)
    {
        m_bounds = {x, y, w, h};
        m_layoutValid = false;
    }
}

void SettingsMessageBox::setPending(const char *text)
{
    stringutils::safeCopy(m_loading, text);
    m_pending = true;
    cancelTouch();
}

void SettingsMessageBox::draw(UiCanvas &c) const
{
    // Nativ für 1280x720, Originalwerte 800x480 in Klammern: Rand 30 (20), Rahmen 2 (1), Radius 6 (4), Titel +27 (+18), Trennlinie +72 (+48) mit 3 (2), Text +98 (+65), Tasten 68 (45) hoch mit 15 (10) Abstand
    if (!m_active)
        return;
    const auto &r = m_bounds;
    const uint16_t grund = farbe(Rolle::GemGrund);
    c.fillSmoothRoundRect(r.x, r.y, r.w, r.h, 6, farbe(Rolle::GemDialogRahmen));
    c.fillSmoothRoundRect(r.x + 2, r.y + 2, r.w - 4, r.h - 4, 6, grund);
    c.loadFont(ComfortaaBold27);
    c.setTextDatum(TL_DATUM);
    c.setTextColor(farbe(Rolle::GemTitel), grund);
    c.drawString(m_title, r.x + 30, r.y + 27);
    c.fillRect(r.x + 30, r.y + 72, r.w - 60, 3, farbe(Rolle::GemDialogTrenner));
    c.loadFont(ComfortaaBold21);
    c.setTextColor(farbe(Rolle::GemDialogText), grund);
    // Zeilenraster 33 (22): ComfortaaBold21 ist 30 hoch, daher 3 statt 2 Pixel Zusatzabstand
    multiline(c, r.x + 30, r.y + 98, r.w - 60, m_message, 3);
    if (m_pending)
    {
        c.setTextDatum(MC_DATUM);
        c.setTextColor(farbe(Rolle::GemTitel), grund);
        c.drawString(m_loading, r.x + r.w / 2, r.y + r.h - 63);
        c.unloadFont();
        return;
    }
    int widths[3]{}, total = 0;
    c.loadFont(LeagueGothicCond51);
    for (size_t i = 0; i < m_count; ++i)
    {
        widths[i] = std::min(std::max(120, static_cast<int>(c.textWidth(m_buttons[i])) + 54), (r.w - 60 - static_cast<int>(m_count - 1) * 15) / static_cast<int>(m_count));
        total += widths[i];
    }
    total += static_cast<int>(m_count - 1) * 15;
    int x = m_count == 1 ? r.x + (r.w - total) / 2 : r.x + r.w - 30 - total;
    for (size_t i = 0; i < m_count; ++i)
    {
        m_buttonRects[i] = {x, r.y + r.h - 98, widths[i], 68};
        dialogButton(c, m_buttonRects[i], m_buttons[i], farbe(Rolle::GemRahmen), farbe(Rolle::GemTastenFlaeche), farbe(i ? Rolle::GemTextGedaempft : Rolle::GemDialogText), m_pressed == static_cast<int>(i), LeagueGothicCond51, MC_DATUM, 6, 2, 3);
        x += widths[i] + 15;
    }
    m_layoutValid = true;
    c.unloadFont();
}

int SettingsMessageBox::touch(Phase phase, int x, int y)
{
    if (!m_active || m_pending || !m_layoutValid)
        return -1;
    if (phase == Phase::Cancel)
    {
        cancelTouch();
        return -1;
    }
    int target = -1;
    for (size_t i = 0; i < m_count; ++i)
        if (m_buttonRects[i].contains(x, y))
            target = i;
    if (phase == Phase::Press)
    {
        m_contact = true;
        m_pressed = target;
    }
    else if (phase == Phase::Move && m_contact)
        m_pressed = target;
    else if (phase == Phase::Release)
    {
        const int result = m_contact ? m_pressed : -1;
        cancelTouch();
        return result;
    }
    return -1;
}

void SettingsProgressBox::setTitle(const char *text)
{
    stringutils::safeCopy(m_title, text);
}

void SettingsProgressBox::setMessage(const char *text)
{
    stringutils::safeCopy(m_message, text);
}

void SettingsProgressBox::setBounds(int x, int y, int w, int h)
{
    if (x >= 0 && y >= 0 && w >= 180 && h >= 240 && x + w <= SettingsTab5Layout::WIDTH && y + h <= SettingsTab5Layout::HEIGHT)
    {
        m_bounds = {x, y, w, h};
        m_autoCenter = false;
    }
}

void SettingsProgressBox::setAutoCenter(bool center)
{
    m_autoCenter = center;
}

void SettingsProgressBox::setBarStyle(int segment, int gap)
{
    m_segmentWidth = std::max(1, segment);
    m_gapWidth = std::max(0, gap);
}

void SettingsProgressBox::start(const char *message, int progress)
{
    if (message)
        setMessage(message);
    m_progress = std::clamp(progress, -1, 100);
    m_animation = 0;
    m_animationTime = 0;
    m_active = true;
    if (m_autoCenter)
    {
        m_bounds.x = (SettingsTab5Layout::WIDTH - m_bounds.w) / 2;
        m_bounds.y = (SettingsTab5Layout::HEIGHT - m_bounds.h) / 2;
    }
}

void SettingsProgressBox::updateProgress(int value)
{
    m_progress = std::clamp(value, -1, 100);
}

bool SettingsProgressBox::updateAnimation(uint32_t now)
{
    if (!m_active || m_progress >= 0 || now - m_animationTime < 50)
        return false;
    m_animationTime = now;
    m_animation = (m_animation + 5) % 101;
    return true;
}

SettingsUi::Rect SettingsProgressBox::barRect() const
{
    const auto &r = m_bounds;
    return {r.x + G::MARGIN, r.y + r.h - G::BAR_DY, r.w - 2 * G::MARGIN, G::BAR_H};
}

void SettingsProgressBox::draw(UiCanvas &c) const
{
    if (!m_active)
        return;
    const auto &r = m_bounds;
    const uint16_t grund = farbe(Rolle::GemGrund);
    c.fillSmoothRoundRect(r.x, r.y, r.w, r.h, G::R, farbe(Rolle::GemDialogRahmen));
    c.fillSmoothRoundRect(r.x + G::BORDER, r.y + G::BORDER, r.w - 2 * G::BORDER, r.h - 2 * G::BORDER, G::R, grund);
    c.loadFont(ComfortaaBold27);
    c.setTextDatum(TL_DATUM);
    c.setTextColor(farbe(Rolle::GemTitel), grund);
    c.drawString(m_title, r.x + G::MARGIN, r.y + G::TITLE_Y);
    c.fillRect(r.x + G::MARGIN, r.y + G::LINE_Y, r.w - 2 * G::MARGIN, G::LINE_H, farbe(Rolle::GemDialogTrenner));
    c.loadFont(ComfortaaBold21);
    c.setTextColor(farbe(Rolle::GemDialogText), grund);
    multiline(c, r.x + G::MARGIN, r.y + G::TEXT_Y, r.w - 2 * G::MARGIN, m_message, G::TEXT_LEADING);
    c.unloadFont();
    drawBar(c);
}

void SettingsProgressBox::drawBar(UiCanvas &c) const
{
    if (!m_active)
        return;
    const Rect bar = barRect();
    const int period = m_segmentWidth + m_gapWidth, filled = bar.w * m_progress / 100, winW = bar.w * 30 / 100, start = (bar.w - winW) * m_animation / 100;
    const uint16_t balken = farbe(Rolle::DlgFortschrittBalken), kappe = farbe(Rolle::DlgFortschrittKappe);
    c.fillRect(bar.x, bar.y, bar.w, bar.h, farbe(Rolle::DlgFortschrittGrund));
    for (int x = 0; x < bar.w; ++x)
        if (x % period < m_segmentWidth)
            c.drawFastVLine(bar.x + x, bar.y, bar.h, (m_progress >= 0 ? x < filled : x >= start && x < start + winW) ? balken : kappe);
}
