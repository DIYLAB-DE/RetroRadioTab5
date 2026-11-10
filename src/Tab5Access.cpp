#include "Tab5Access.h"

#include "MainScreenTab5Layout.h"
#include "Tab5Render.h"
#include "Thema.h"
#include "fonts/ComfortaaBold45.h"
#include "images/logo_450.h"
#include "images/made_in_germany.h"
#include <algorithm>
#include <cstdio>

// Selektiv aus DIYLAB-DE/RetroRadio, ea4229a34fb39faaff23619092cf09cafb8c51cf, LockScreen.h/.cpp und MainScreen.cpp. GPL-3.0, siehe referenz/RetroRadio-LICENSE. Gerätezugriff, AppState und zentrale PIN-Prüfung sind durch die unten dokumentierten Schnittstellen ersetzt. Die nativen Maße stehen mit Originalwert in MainScreenTab5Layout.h.
namespace
{
    using namespace MainScreenTab5Layout::Lock;
    namespace Battery = MainScreenTab5Layout::Battery;
    namespace Corner = MainScreenTab5Layout::Corner;
    constexpr int DISPLAY_W = MainScreenTab5Layout::WIDTH, DISPLAY_H = MainScreenTab5Layout::HEIGHT;
    constexpr int KEY_COUNT = KEY_COLS * KEY_ROWS;
    static_assert(LOGO_X + LOGO_W < KEYPAD_X && KEYPAD_Y + KEYPAD_H < DISPLAY_H && PULSE_FRAME <= KEY_RADIUS);
    constexpr uint32_t BUTTON_PULSE_ON_MS = 85, BUTTON_PULSE_OFF_MS = 70;
    constexpr uint8_t BUTTON_PULSE_PHASE_COUNT = 4;
    constexpr int KEY_CLEAR = -1, KEY_OK = -2;
    constexpr int KEY_DIGIT_MAP[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 0, KEY_CLEAR, KEY_OK};
    constexpr const char *KEY_LABELS[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "c", "ok"};

    /** @brief Prüft, ob physische Koordinaten auf dem Display liegen. */
    bool onDisplay(int32_t x, int32_t y)
    {
        return x >= 0 && y >= 0 && x < DISPLAY_W && y < DISPLAY_H;
    }

    /** @brief Legt eine graue, in der Helligkeit umgekehrte Kopie eines Bilds mit schwarzem Grund an: Grund wird Gem.Grund, Weiß Gem.TextHell. @return Puffer für free(), nullptr bei Allokationsfehler. */
    uint16_t *grauKopie(const uint16_t *bild, size_t anzahl)
    {
        auto *kopie = static_cast<uint16_t *>(malloc(anzahl * sizeof(uint16_t)));
        if (!kopie)
            return nullptr;
        const uint16_t tinte = farbe(Rolle::GemTextHell), papier = farbe(Rolle::GemGrund), grund = ThemaRetro.farbe[static_cast<size_t>(Rolle::GemGrund)];
        for (size_t i = 0; i < anzahl; ++i)
            kopie[i] = bildGrau(bild[i], tinte, papier, true, grund);
        return kopie;
    }
}

Tab5Access::~Tab5Access()
{
    release();
}

bool Tab5Access::begin(lgfx::LGFX_Device &display)
{
    if (m_ready)
        return m_display == &display;
    if (display.width() != 1280 || display.height() != 720)
        return false;
    m_display = &display;
    m_fontData.set(ComfortaaBold45);
    m_keypad.setPsram(true);
    m_keypad.setColorDepth(16);
    m_keypad.setSwapBytes(true);
    m_keypad.setTextSize(1);
    m_keypad.setTextWrap(false);
    if (!m_font.loadFont(&m_fontData) || !m_keypad.createSprite(KEYPAD_W, KEYPAD_H))
    {
        release();
        return false;
    }
    // Beide Bilder tragen Schwarz als Grund eingebrannt, Themen mit Graustufen zeigen sie einmal umgerechnet auf ihrem Grund.
    if (themaAktiv().merkmale & MerkmalGraustufen)
    {
        m_logoGrau = grauKopie(logo_450, LOGO_W * LOGO_H);
        m_madeInGrau = grauKopie(made_in_germany, MADEIN_W * MADEIN_H);
        if (!m_logoGrau || !m_madeInGrau)
        {
            release();
            return false;
        }
    }
    m_keypad.setFont(&m_font);
    m_ready = true;
    return true;
}

void Tab5Access::release()
{
    if (m_display)
        m_display->waitDMA();
    m_keypad.setFont(nullptr);
    m_keypad.deleteSprite();
    m_battery.setFont(nullptr);
    m_battery.deleteSprite();
    m_batteryShown = m_batteryFailed = false;
    m_font.unloadFont();
    free(m_logoGrau);
    free(m_madeInGrau);
    m_logoGrau = m_madeInGrau = nullptr;
    clearInput();
    m_ready = m_active = m_touchActive = m_keypadVisible = false;
    m_pressedKey = -1;
    cancelButtonPulse();
    m_display = nullptr;
}

void Tab5Access::clearInput()
{
    volatile char *input = m_input;
    for (size_t i = 0; i < sizeof(m_input); ++i)
        input[i] = 0;
    m_inputLength = 0;
}

void Tab5Access::activate(uint32_t now, bool contactHeld)
{
    if (!m_ready)
        return;
    clearInput();
    m_active = true;
    m_touchActive = contactHeld;
    m_pressedKey = -1;
    m_keypadVisible = false;
    m_lastActivityMs = now;
    cancelButtonPulse();
    drawScreen();
}

void Tab5Access::deactivate()
{
    // Zuerst inaktiv, damit cancelTouch kein gedrücktes Tastenfeld mehr zeichnet.
    m_active = false;
    cancelTouch();
    cancelButtonPulse();
    m_keypadVisible = false;
    clearInput();
}

void Tab5Access::drawScreen()
{
    m_display->waitDMA();
    // Nur die Flächen um Logo und Schriftzug mit Gem.Grund füllen: kein Zwischenbild beim Neuaufbau, ein sichtbares Tastenfeld verschwindet. Beide Bilder bringen ihren Grund selbst mit, als Original schwarz, als graue Kopie in Gem.Grund.
    auto &display = *m_display;
    const uint16_t ground = farbe(Rolle::GemGrund);
    const int logoBottom = LOGO_Y + LOGO_H, madeInBottom = MADEIN_Y + MADEIN_H;
    display.fillRect(0, 0, DISPLAY_W, LOGO_Y, ground);
    display.fillRect(0, LOGO_Y, LOGO_X, LOGO_H, ground);
    display.fillRect(LOGO_X + LOGO_W, LOGO_Y, DISPLAY_W - LOGO_X - LOGO_W, LOGO_H, ground);
    display.fillRect(0, logoBottom, DISPLAY_W, MADEIN_Y - logoBottom, ground);
    display.fillRect(0, MADEIN_Y, MADEIN_X, MADEIN_H, ground);
    display.fillRect(MADEIN_X + MADEIN_W, MADEIN_Y, DISPLAY_W - MADEIN_X - MADEIN_W, MADEIN_H, ground);
    display.fillRect(0, madeInBottom, DISPLAY_W, DISPLAY_H - madeInBottom, ground);
    const bool swap = m_display->getSwapBytes();
    m_display->setSwapBytes(true);
    m_display->pushImage(LOGO_X, LOGO_Y, LOGO_W, LOGO_H, m_logoGrau ? m_logoGrau : logo_450);
    m_display->pushImage(MADEIN_X, MADEIN_Y, MADEIN_W, MADEIN_H, m_madeInGrau ? m_madeInGrau : made_in_germany);
    m_display->waitDMA();
    m_display->setSwapBytes(swap);
    // Die Fläche ist gerade geleert, ein erkannter Akku wird neu gezeichnet.
    m_batteryShown = false;
    drawBattery();
    if (m_keypadVisible)
        renderKeypad();
}

void Tab5Access::setBattery(const BatteryIcon::State &state)
{
    if (state == m_batteryState)
        return;
    m_batteryState = state;
    if (m_ready && m_active)
        drawBattery();
}

void Tab5Access::drawBattery()
{
    const auto &battery = m_batteryState;
    if ((!battery.present && !m_batteryShown) || m_batteryFailed)
        return;
    if (!m_battery.width())
    {
        m_battery.setPsram(true);
        m_battery.setColorDepth(16);
        m_battery.setSwapBytes(true);
        m_battery.setTextSize(1);
        m_battery.setTextWrap(false);
        if (!m_battery.createSprite(Battery::W, Battery::H))
        {
            m_batteryFailed = true;
            return;
        }
    }
    const uint16_t ground = farbe(Rolle::GemGrund), tint = farbe(Rolle::MainDatum);
    m_battery.fillSprite(ground);
    if (battery.present)
    {
        char value[8];
        snprintf(value, sizeof(value), "%u %%", static_cast<unsigned>(battery.percent));
        m_battery.setFont(&m_font);
        m_battery.setTextDatum(BC_DATUM);
        m_battery.setTextColor(tint, ground);
        // Symbol und Zahl stehen zusammen mittig.
        const int width = m_battery.textWidth(value), left = std::max(0, (Battery::W - Battery::ICON_W - Battery::GAP - width) / 2);
        m_battery.drawString(value, left + Battery::ICON_W + Battery::GAP + width / 2, Battery::H);
        m_battery.setFont(nullptr);
        BatteryIcon::draw(m_battery, left, Battery::ICON_Y, BatteryIcon::Large, battery, tint, ground);
    }
    m_batteryShown = battery.present;
    Tab5Render::pushSprite(m_display, m_battery, Battery::LOCK_X, Battery::Y);
    m_display->waitDMA();
}

void Tab5Access::renderKeypad(int8_t pressedKey, bool pulseFrames, uint16_t pulseColor)
{
    // Zwischen den Tasten zeigt das Tastenfeld den Grund der Ansicht.
    m_keypad.fillSprite(farbe(Rolle::GemGrund));
    m_keypad.setTextDatum(MC_DATUM);
    const uint16_t keyFill = farbe(Rolle::GemTastenFlaeche), pressedFill = farbe(Rolle::SperreTasteGedrueckt);
    const uint16_t borderColor = pulseFrames ? pulseColor : farbe(Rolle::SperreTasteRahmen);
    const uint16_t digitText = farbe(Rolle::SperreTasteText), clearText = farbe(Rolle::SperreTasteLoeschen), okText = farbe(Rolle::SperreTasteOk);
    for (int i = 0; i < KEY_COUNT; ++i)
    {
        const int col = i % KEY_COLS;
        const int row = i / KEY_COLS;
        const int x = col * (KEY_W + KEY_GAP);
        const int y = row * (KEY_H + KEY_GAP);
        const int cx = x + KEY_W / 2;
        const int cy = y + KEY_H / 2;
        const bool isPressed = (i == pressedKey);
        const uint16_t fillColor = isPressed ? pressedFill : keyFill;
        const uint16_t textColor = KEY_DIGIT_MAP[i] == KEY_CLEAR ? clearText : KEY_DIGIT_MAP[i] == KEY_OK ? okText
                                                                                                          : digitText;

        m_keypad.fillRoundRect(x, y, KEY_W, KEY_H, KEY_RADIUS, fillColor);
        // Rahmen und Pulsrahmen sind wie im Original eingerückte Rundrechtecke. Der Radius sinkt je Pixel um 1 (konzentrisch), weil gleiche Radien ab 2 Pixeln Lücken in den Ecken lassen.
        for (int frame = 0; frame < KEY_BORDER; ++frame)
            m_keypad.drawRoundRect(x + frame, y + frame, KEY_W - frame * 2, KEY_H - frame * 2, KEY_RADIUS - frame, borderColor);
        m_keypad.setTextColor(textColor, fillColor);
        m_keypad.drawString(KEY_LABELS[i], cx, cy);

        if (pulseFrames)
            for (int frame = KEY_BORDER; frame < PULSE_FRAME; ++frame)
                m_keypad.drawRoundRect(x + frame, y + frame, KEY_W - frame * 2, KEY_H - frame * 2, KEY_RADIUS - frame, pulseColor);
    }
    Tab5Render::pushSprite(m_display, m_keypad, KEYPAD_X, KEYPAD_Y);
    m_display->waitDMA();
}

int8_t Tab5Access::keyAt(int32_t x, int32_t y)
{
    for (int8_t i = 0; i < KEY_COUNT; ++i)
    {
        const int cx = KEYPAD_X + (i % KEY_COLS) * (KEY_W + KEY_GAP) + KEY_W / 2;
        const int cy = KEYPAD_Y + (i / KEY_COLS) * (KEY_H + KEY_GAP) + KEY_H / 2;
        if (x >= cx - KEY_W / 2 && x <= cx + KEY_W / 2 && y >= cy - KEY_H / 2 && y <= cy + KEY_H / 2)
            return i;
    }
    return -1;
}

void Tab5Access::cancelTouch()
{
    const bool redraw = m_pressedKey >= 0;
    m_touchActive = false;
    m_pressedKey = -1;
    if (redraw && m_active && m_keypadVisible && !m_buttonPulseActive)
        renderKeypad();
}

bool Tab5Access::touch(TouchPhase phase, int32_t x, int32_t y, uint32_t now)
{
    if (!m_active)
        return false;
    if (phase == TouchPhase::Cancel)
    {
        cancelTouch();
        return false;
    }
    const bool inside = onDisplay(x, y);
    if (phase == TouchPhase::Release)
    {
        if (m_buttonPulseActive)
        {
            m_touchActive = false;
            m_pressedKey = -1;
            return false;
        }
        const int8_t key = m_touchActive && inside ? m_pressedKey : -1;
        cancelTouch();
        if (key >= 0 && handleKeyPress(key, now))
        {
            m_active = false;
            m_keypadVisible = false;
            clearInput();
            return true;
        }
        return false;
    }
    if (!inside)
    {
        const bool redraw = m_pressedKey >= 0;
        m_pressedKey = -1;
        // Ein Kontakt außerhalb des Displays beendet keine noch ausstehende Loslasssperre und darf beim Zurückziehen keine neue Taste beginnen.
        m_touchActive = true;
        if (redraw && m_keypadVisible && !m_buttonPulseActive)
            renderKeypad();
        return false;
    }
    if (m_buttonPulseActive)
        return false;
    m_lastActivityMs = now;
    if (m_touchActive)
        return false;
    m_touchActive = true;
    m_pressedKey = -1;
    if (!m_keypadVisible)
    {
        m_keypadVisible = true;
        renderKeypad();
        return false;
    }
    m_pressedKey = keyAt(x, y);
    if (m_pressedKey >= 0)
        renderKeypad(m_pressedKey);
    return false;
}

bool Tab5Access::handleKeyPress(int8_t keyIndex, uint32_t now)
{
    const int mapping = KEY_DIGIT_MAP[keyIndex];
    if (mapping == KEY_CLEAR)
    {
        clearInput();
        startButtonPulse(farbe(Rolle::SperrePulsLoeschen), now);
        return false;
    }
    if (mapping == KEY_OK)
    {
        if (m_validator && m_validator(m_input))
            return true;
        clearInput();
        startButtonPulse(farbe(Rolle::SperrePulsFehler), now);
        return false;
    }
    if (m_inputLength < MaxInputLength)
    {
        m_input[m_inputLength++] = static_cast<char>('0' + mapping);
        m_input[m_inputLength] = '\0';
    }
    return false;
}

void Tab5Access::startButtonPulse(uint16_t color, uint32_t now)
{
    m_buttonPulseColor = color;
    m_buttonPulseActive = true;
    m_buttonPulsePhase = 0;
    m_buttonPulsePhaseStart = now;
    renderKeypad(-1, true, color);
}

void Tab5Access::cancelButtonPulse()
{
    m_buttonPulseActive = false;
    m_buttonPulsePhase = 0;
    m_buttonPulsePhaseStart = 0;
    m_buttonPulseColor = 0;
}

void Tab5Access::update(uint32_t now)
{
    if (!m_active)
        return;
    if (m_buttonPulseActive)
    {
        const uint32_t phaseDuration = m_buttonPulsePhase % 2 == 0 ? BUTTON_PULSE_ON_MS : BUTTON_PULSE_OFF_MS;
        if (now - m_buttonPulsePhaseStart < phaseDuration)
            return;
        ++m_buttonPulsePhase;
        m_buttonPulsePhaseStart = now;
        if (m_buttonPulsePhase >= BUTTON_PULSE_PHASE_COUNT)
        {
            cancelButtonPulse();
            renderKeypad();
        }
        else if (m_buttonPulsePhase % 2 == 0)
            renderKeypad(-1, true, m_buttonPulseColor);
        else
            renderKeypad();
        if (m_buttonPulseActive)
            return;
    }
    if (m_keypadVisible && m_idleTimeoutMs && !m_touchActive && now - m_lastActivityMs >= m_idleTimeoutMs)
    {
        cancelButtonPulse();
        clearInput();
        m_keypadVisible = false;
        m_pressedKey = -1;
        drawScreen();
    }
}

void Tab5CornerGesture::reset()
{
    m_tracking = m_upper = m_startedInCorner = false;
    m_startX = m_startY = 0;
}

bool Tab5CornerGesture::touch(Tab5Access::TouchPhase phase, int32_t x, int32_t y)
{
    if (phase == Tab5Access::TouchPhase::Release || phase == Tab5Access::TouchPhase::Cancel || !onDisplay(x, y))
    {
        reset();
        return false;
    }
    constexpr int zone = Corner::START_ZONE, corner = Corner::CORNER, swipe = Corner::SWIPE;
    if (!m_tracking)
    {
        const bool inUpperStartZone = x < zone && y < zone;
        const bool inLowerStartZone = x < zone && y > DISPLAY_H - zone;
        const bool inUpperCorner = x < corner && y < corner;
        const bool inLowerCorner = x < corner && y > DISPLAY_H - corner;
        if (inUpperStartZone || inLowerStartZone)
        {
            m_tracking = true;
            m_startX = x;
            m_startY = y;
            m_upper = inUpperStartZone;
            m_startedInCorner = inUpperStartZone ? inUpperCorner : inLowerCorner;
        }
        return false;
    }
    const int32_t dx = x - m_startX, dy = y - m_startY;
    bool triggered;
    if (m_upper)
    {
        const bool reachedCorner = x < corner && y < corner;
        const bool inwardSwipe = reachedCorner && dx < -swipe && dy < -swipe;
        const bool outwardSwipe = m_startedInCorner && dx > swipe && dy > swipe;
        triggered = inwardSwipe || outwardSwipe;
    }
    else
    {
        const bool reachedCorner = x < corner && y > DISPLAY_H - corner;
        const bool inwardSwipe = reachedCorner && dx < -swipe && dy > swipe;
        const bool outwardSwipe = m_startedInCorner && dx > swipe && dy < -swipe;
        triggered = inwardSwipe || outwardSwipe;
    }
    if (triggered)
        reset();
    return triggered;
}

Tab5TimeWindow::Event Tab5TimeWindow::evaluate(bool timeValid, const tm &local, const uint32_t (&masks)[7])
{
    if (!timeValid || local.tm_wday < 0 || local.tm_wday > 6 || local.tm_hour < 0 || local.tm_hour > 23)
    {
        m_day = m_hour = m_manualDay = m_manualHour = InvalidSlot;
        m_enabled = true;
    }
    else
    {
        const uint8_t day = local.tm_wday == 0 ? 6 : local.tm_wday - 1;
        const uint8_t hour = local.tm_hour;
        if ((day != m_day || hour != m_hour) && (day != m_manualDay || hour != m_manualHour))
            m_manualDay = m_manualHour = InvalidSlot;
        m_day = day;
        m_hour = hour;
        m_enabled = (masks[day] & (uint32_t(1) << hour)) != 0;
        if (m_enabled)
            m_manualDay = m_manualHour = InvalidSlot;
    }
    if (m_timerSleep)
        return Event::None;
    if (m_enabled)
    {
        if (!m_sleeping)
            return Event::None;
        m_sleeping = m_requireRelease = false;
        return Event::Wake;
    }
    if (m_sleeping || (m_day == m_manualDay && m_hour == m_manualHour))
        return Event::None;
    m_sleeping = m_requireRelease = true;
    return Event::Sleep;
}

Tab5TimeWindow::Event Tab5TimeWindow::observeTouch(bool touched)
{
    if (!m_sleeping)
        return Event::None;
    if (m_requireRelease)
    {
        if (!touched)
            m_requireRelease = false;
        return Event::None;
    }
    if (!touched)
        return Event::None;
    if (!m_enabled && m_day != InvalidSlot && m_hour != InvalidSlot)
    {
        m_manualDay = m_day;
        m_manualHour = m_hour;
    }
    m_sleeping = m_timerSleep = false;
    return Event::Wake;
}
