#include "Numpad.h"

#include <cstring>

#include "Thema.h"
#include "fonts/ComfortaaBold24.h"

const Numpad::KeyEntry Numpad::KEYS[Numpad::NUM_KEYS] =
    {
        {"7", '7', KeyType::DIGIT},
        {"8", '8', KeyType::DIGIT},
        {"9", '9', KeyType::DIGIT},
        {"4", '4', KeyType::DIGIT},
        {"5", '5', KeyType::DIGIT},
        {"6", '6', KeyType::DIGIT},
        {"1", '1', KeyType::DIGIT},
        {"2", '2', KeyType::DIGIT},
        {"3", '3', KeyType::DIGIT},
        {",", '.', KeyType::DIGIT},
        {"0", '0', KeyType::DIGIT},
        {"+/-", 'S', KeyType::ACTION},
        {"BS", 'B', KeyType::BACKSPACE},
        {"C", 'C', KeyType::CLEAR},
};

void Numpad::clearLayout()
{
    memset(m_keyRects, 0, sizeof(m_keyRects));
    m_btnW = 0;
    m_btnH = 0;
    m_startY = 0;
    m_w = 0;
}

void Numpad::computeLayout(int w, int startY)
{
    m_w = w;
    m_startY = startY;

    m_btnH = TARGET_BTN_H;
    m_btnW = (w - (PAD_COLS - 1) * BTN_GAP) / PAD_COLS;
    if (m_btnW < 1)
        m_btnW = 1;

    const int tallH = 2 * m_btnH + BTN_GAP;

    const int colX[4] = {
        0,
        m_btnW + BTN_GAP,
        2 * (m_btnW + BTN_GAP),
        3 * (m_btnW + BTN_GAP)};

    const int rowY[4] = {
        startY,
        startY + m_btnH + BTN_GAP,
        startY + 2 * (m_btnH + BTN_GAP),
        startY + 3 * (m_btnH + BTN_GAP)};

    for (int k = 0; k < 12; ++k)
    {
        const int row = k / 3;
        const int col = k % 3;
        m_keyRects[k] = {colX[col], rowY[row], m_btnW, m_btnH};
    }

    m_keyRects[12] = {colX[3], rowY[0], m_btnW, tallH};

    m_keyRects[13] = {colX[3], rowY[2], m_btnW, tallH};
}

char Numpad::keyAction(int keyIdx)
{
    if (keyIdx < 0 || keyIdx >= NUM_KEYS)
        return '\0';
    return KEYS[keyIdx].action;
}

void Numpad::draw(UiCanvas *spr, int originX, int originY, int pressedKey) const
{
    spr->fillRect(originX, originY + m_startY, m_w, REQUIRED_H, farbe(Rolle::GemFlaeche));

    spr->loadFont(ComfortaaBold24);
    for (int k = 0; k < NUM_KEYS; ++k)
        drawKey(spr, originX, originY, k, pressedKey == k);

    spr->unloadFont();
}

void Numpad::drawKey(UiCanvas *spr, int originX, int originY, int keyIdx, bool pressed) const
{
    if (keyIdx < 0 || keyIdx >= NUM_KEYS)
        return;

    const Rect &r = m_keyRects[keyIdx];
    const KeyEntry &e = KEYS[keyIdx];
    const int ax = originX + r.x;
    const int ay = originY + r.y;

    if (r.w <= 0 || r.h <= 0)
        return;

    uint16_t fill, textColor;
    if (pressed)
    {
        fill = farbe(Rolle::EinNumGedrueckt);
        textColor = farbe(Rolle::GemTextHell);
    }
    else
    {
        switch (e.type)
        {
        case KeyType::DIGIT:
            fill = farbe(Rolle::EinNumTaste);
            textColor = farbe(Rolle::EinText);
            break;
        case KeyType::ACTION:
            fill = farbe(Rolle::GemAuswahl);
            textColor = farbe(Rolle::GemTextHell);
            break;
        case KeyType::BACKSPACE:
            fill = farbe(Rolle::EinNumZurueck);
            textColor = farbe(Rolle::GemTextHell);
            break;
        case KeyType::CLEAR:
        default:
            fill = farbe(Rolle::EinNumLoeschen);
            textColor = farbe(Rolle::GemTextHell);
            break;
        }
    }

    spr->fillSmoothRoundRect(ax, ay, r.w, r.h, BTN_RADIUS, fill);

    const int cx = ax + r.w / 2;
    const int cy = ay + r.h / 2 + BTN_TEXT_OY;

    if (e.action == 'B')
    {
        drawBackspaceSymbol(spr, cx, cy, m_btnH, textColor);
    }
    else
    {
        spr->setTextDatum(MC_DATUM);
        spr->setTextColor(textColor, fill);
        spr->drawString(e.label, cx, cy);
    }
}

void Numpad::drawBackspaceSymbol(UiCanvas *spr, int cx, int cy,
                                 int size, uint16_t color) const
{
    // Das Symbol ist auf einem 24x24-Raster entworfen
    const float sc = size / 24.0f;
    const int16_t half = static_cast<int16_t>(size / 2);
    const int16_t lx = static_cast<int16_t>(cx - half);
    const int16_t ty = static_cast<int16_t>(cy - half);

    spr->drawSmoothLine(lx + static_cast<int16_t>(7 * sc), ty + static_cast<int16_t>(3 * sc), lx + static_cast<int16_t>(22 * sc), ty + static_cast<int16_t>(3 * sc), color);
    spr->drawSmoothLine(lx + static_cast<int16_t>(7 * sc), ty + static_cast<int16_t>(21 * sc), lx + static_cast<int16_t>(22 * sc), ty + static_cast<int16_t>(21 * sc), color);
    spr->drawSmoothLine(lx + static_cast<int16_t>(22 * sc), ty + static_cast<int16_t>(3 * sc), lx + static_cast<int16_t>(22 * sc), ty + static_cast<int16_t>(21 * sc), color);
    spr->drawSmoothLine(lx + static_cast<int16_t>(0 * sc), ty + static_cast<int16_t>(12 * sc), lx + static_cast<int16_t>(7 * sc), ty + static_cast<int16_t>(3 * sc), color);
    spr->drawSmoothLine(lx + static_cast<int16_t>(0 * sc), ty + static_cast<int16_t>(12 * sc), lx + static_cast<int16_t>(7 * sc), ty + static_cast<int16_t>(21 * sc), color);
    spr->drawSmoothLine(lx + static_cast<int16_t>(9 * sc), ty + static_cast<int16_t>(8 * sc), lx + static_cast<int16_t>(18 * sc), ty + static_cast<int16_t>(17 * sc), color);
    spr->drawSmoothLine(lx + static_cast<int16_t>(9 * sc), ty + static_cast<int16_t>(17 * sc), lx + static_cast<int16_t>(18 * sc), ty + static_cast<int16_t>(8 * sc), color);
}

int Numpad::hitTest(int cx, int cy) const
{
    for (int k = 0; k < NUM_KEYS; ++k)
    {
        const Rect &r = m_keyRects[k];
        if (r.w <= 0 || r.h <= 0)
            continue;

        if (cx >= r.x && cx < r.x + r.w && cy >= r.y && cy < r.y + r.h)
            return k;
    }
    return -1;
}
