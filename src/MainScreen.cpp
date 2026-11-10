/*
 * Diese Klasse geht auf lgfx::LGFX_Button aus LovyanGFX zurück und ist stark erweitert worden.
 * Die Herkunft zeigt sich unter anderem an _init_button, drawButton, contains, press, isPressed,
 * justPressed, justReleased, dem Zeichenrückruf und den Feldern für Textversatz und Ausrichtung.
 * LovyanGFX selbst folgt darin TFT_eSPI von Bodmer und dieses wiederum Adafruit_GFX.
 *
 * Copyright (c) 2020 lovyan03 (https://github.com/lovyan03)
 * Copyright (c) 2020 Bodmer (https://github.com/Bodmer)
 * Copyright (c) 2012 Adafruit Industries
 *
 * Weitergabe im Quelltext und in binärer Form, mit oder ohne Veränderung, ist unter der
 * FreeBSD-Lizenz mit zwei Bedingungen erlaubt, sofern dieser Urheberrechtsvermerk, die
 * Bedingungen und der Haftungsausschluss erhalten bleiben. Der vollständige Wortlaut steht in
 * referenz/RetroRadio-Button-LICENSE.txt.
 */

#include "MainScreen.h"
#include "MainScreenTab5Layout.h"
#include "Tab5Render.h"
#include "IconShapes.h"
#include "StringUtils.h"
#include "Thema.h"
#include "fonts/ComfortaaBold15.h"
#include "fonts/ComfortaaBold21.h"
#include "fonts/ComfortaaBold45.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <utility>
#if defined(ARDUINO_ARCH_ESP32)
#include <ctime>
#include <esp_heap_caps.h>
#include <sys/time.h>
#endif

// Zeichenpfade und Geometrie selektiv aus DIYLAB-DE/RetroRadio, ea4229a34fb39faaff23619092cf09cafb8c51cf, MainScreen.cpp und Button.cpp. GPL-3.0 sowie obiger Button-Hinweis, siehe referenz/RetroRadio-LICENSE. Die nativen Maße stehen mit Originalwert in MainScreenTab5Layout.h.
namespace
{
    namespace Battery = MainScreenTab5Layout::Battery;
    namespace Buttons = MainScreenTab5Layout::Buttons;
    namespace Clock = MainScreenTab5Layout::Clock;
    namespace Indicators = MainScreenTab5Layout::Indicators;
    constexpr int Width = MainScreenTab5Layout::WIDTH, Height = MainScreenTab5Layout::HEIGHT;
    constexpr IconId Icons[] = {IconId::Radio, IconId::Player, IconId::Config};
    constexpr Rolle IconTints[] = {Rolle::MainSymbolRadio, Rolle::MainSymbolSd, Rolle::MainSymbolKonfig}; ///< Tönung je Symbol in der Reihenfolge von Icons
    static_assert(Clock::DIAMETER == 600 && Clock::SPRITE_W == 645 && Clock::SPRITE_H == 669 && Buttons::START_X == 761);
    static_assert(Buttons::ICON_INSET * 2 + Buttons::ICON_SIZE == Buttons::SIZE);
    // clearBackground setzt voraus, dass die Symbolzeile rechts neben dem Uhrsprite und innerhalb seiner Höhe liegt.
    static_assert(Clock::SPRITE_Y < Buttons::START_Y && Buttons::START_Y + Buttons::SIZE < Clock::SPRITE_Y + Clock::SPRITE_H);
    static_assert(Clock::SPRITE_X + Clock::SPRITE_W < Buttons::START_X && Buttons::START_X + Buttons::GRID_W < Width);

    constexpr float HourHandRatio = 0.55f;      ///< Länge des Stundenzeigers relativ zum Radius, Wert des Originals
    constexpr float MinuteHandRatio = 0.75f;    ///< Länge des Minutenzeigers relativ zum Radius, Wert des Originals
    constexpr float SecondHandRatio = 0.95f;    ///< Länge des Sekundenzeigers bis zur Spitze relativ zum Radius, Wert des Originals
    constexpr float CounterWeightRatio = 0.2f;  ///< Rückstück des Sekundenzeigers relativ zum Radius, Wert des Originals
    constexpr float HandWidthRatio = 0.025f;    ///< Halbbreite von Stunden- und Minutenzeiger relativ zum Radius, Wert des Originals
    constexpr float SecondWidthRatio = 0.015f;  ///< Halbbreite des Sekundenzeigers an der Achse relativ zum Radius, Wert des Originals
    constexpr float CenterCircleRatio = 0.08f;  ///< Radius der Achse relativ zum Radius, Wert des Originals
    constexpr int SignalBarsWidth = Indicators::SIGNAL_BAR_COUNT * Indicators::SIGNAL_BAR_W + (Indicators::SIGNAL_BAR_COUNT - 1) * Indicators::SIGNAL_BAR_GAP; ///< Breite aller WLAN-Balken
    constexpr int SignalBarsHeight = Indicators::SIGNAL_BAR_H[Indicators::SIGNAL_BAR_COUNT - 1];                                                              ///< Höhe des höchsten WLAN-Balkens
#if defined(ARDUINO_ARCH_ESP32)
    constexpr int ViewPixels = 270 * 270; ///< Ausschnittpuffer der Zeiger. Der schräge Sekundenzeiger belegt samt Saum höchstens rund 260 x 260 Pixel
#endif

    /** @brief Liefert den Punkt im Abstand length vom Mittelpunkt (cx, cy) unter angleDeg Grad im Uhrzeigersinn ab 12 Uhr. */
    std::pair<float, float> polarPoint(float cx, float cy, float angleDeg, float length)
    {
        const float rad = (angleDeg - 90.0f) * 0.0174532925f; // DEG2RAD
        return {cx + cosf(rad) * length, cy + sinf(rad) * length};
    }

    /** @brief Berechnet die gewichtete Helligkeit eines RGB565-Pixels für deaktivierte Symbole. */
    float getIntensityFrom565(uint16_t color)
    {
        const uint8_t r8 = (((color >> 11) & 0x1F) * 527 + 23) >> 6;
        const uint8_t g8 = (((color >> 5) & 0x3F) * 259 + 33) >> 6;
        const uint8_t b8 = ((color & 0x1F) * 527 + 23) >> 6;
        return (0.2126f * r8 + 0.7152f * g8 + 0.0722f * b8) / 255.0f;
    }

    /** @brief Mischt Vorder- und Hintergrund in RGB565 für deaktivierte Symbole. */
    uint16_t blendIcon(uint16_t fg, uint16_t bg, float alpha)
    {
        if (alpha >= 1.0f)
            return fg;
        if (alpha <= 0.0f)
            return bg;
        const uint32_t a = uint32_t(alpha * 256.0f), b = 256 - a;
        return uint16_t((((((fg >> 11) & 31) * a + ((bg >> 11) & 31) * b) >> 8) << 11) | (((((fg >> 5) & 63) * a + ((bg >> 5) & 63) * b) >> 8) << 5) | (((fg & 31) * a + (bg & 31) * b) >> 8));
    }
}

MainScreen::~MainScreen()
{
    release();
}

bool MainScreen::prepare(lgfx::LGFX_Sprite &sprite, int width, int height)
{
    sprite.setPsram(true);
    sprite.setColorDepth(16);
    sprite.setSwapBytes(true);
    sprite.setTextSize(1);
    sprite.setTextWrap(false);
    return sprite.createSprite(width, height) != nullptr;
}

bool MainScreen::begin(lgfx::LGFX_Device &display)
{
    if (m_ready)
        return m_display == &display;
    if (display.width() != 1280 || display.height() != 720)
        return false;
    m_display = &display;
    m_largeData.set(ComfortaaBold45);
    m_smallData.set(ComfortaaBold21);
    m_tinyData.set(ComfortaaBold15);
    if (!m_largeFont.loadFont(&m_largeData) || !m_smallFont.loadFont(&m_smallData) || !m_tinyFont.loadFont(&m_tinyData) || !prepare(m_clock, Clock::SPRITE_W, Clock::SPRITE_H) || !prepare(m_labelNtp, Indicators::LABEL_W, Indicators::LABEL_H) || !prepare(m_labelDst, Indicators::LABEL_W, Indicators::LABEL_H))
    {
        release();
        return false;
    }
    // Das Thema steht vor begin() fest, der Grund der Symbole ist die Füllung der Symboltasten und wird hier einmal eingebrannt.
    const uint16_t ground = farbe(Rolle::GemGrund), iconGround = farbe(Rolle::MainTasteFlaeche);
    for (unsigned i = 0; i < 3; ++i)
    {
        if (!prepare(m_buttons[i], Buttons::SIZE, Buttons::SIZE))
        {
            release();
            return false;
        }
        m_buttons[i].clear(ground);
        m_icons[i] = IconShapes::draw(Icons[i], farbe(IconTints[i]), iconGround, Buttons::ICON_SCALE);
        if (!m_icons[i])
        {
            release();
            return false;
        }
    }
#if defined(ARDUINO_ARCH_ESP32)
    // Ohne diese Puffer springt der Sekundenzeiger im Sekundentakt, die Startseite bleibt nutzbar.
    m_viewPixels = static_cast<uint16_t *>(heap_caps_aligned_alloc(64, ViewPixels * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!m_viewPixels || !prepare(m_dial, Clock::SPRITE_W, Clock::DATE_Y))
    {
        m_dial.deleteSprite();
        heap_caps_free(m_viewPixels);
        m_viewPixels = nullptr;
    }
#endif
    renderLabelCaches();
    m_ready = true;
    return true;
}

void MainScreen::release()
{
    if (m_display)
        m_display->waitDMA();
    for (auto *sprite : {&m_clock, &m_labelNtp, &m_labelDst, &m_buttons[0], &m_buttons[1], &m_buttons[2], &m_battery})
    {
        sprite->setFont(nullptr);
        sprite->deleteSprite();
    }
    for (auto &icon : m_icons)
    {
        free(icon);
        icon = nullptr;
    }
#if defined(ARDUINO_ARCH_ESP32)
    m_view.deleteSprite();
    m_dial.setFont(nullptr);
    m_dial.deleteSprite();
    heap_caps_free(m_viewPixels);
    m_viewPixels = nullptr;
    m_composed = false;
    m_dialDirty = true;
#endif
    m_largeFont.unloadFont();
    m_smallFont.unloadFont();
    m_tinyFont.unloadFont();
    m_ready = m_visible = m_contact = m_touchProcessed = false;
    m_batteryShown = m_batteryFailed = false;
    m_pressed = -1;
    m_display = nullptr;
}

void MainScreen::show()
{
    if (!m_ready)
        return;
    m_visible = true;
    invalidate();
}

void MainScreen::hide()
{
    m_visible = false;
    cancelTouch();
}

void MainScreen::invalidate()
{
    cancelTouch();
    m_fullDirty = m_clockDirty = m_dateDirty = m_batteryDirty = true;
    m_buttonDirty = 7;
}

void MainScreen::setSecondSweep(bool sweep)
{
    if (m_sweep == sweep)
        return;
    m_sweep = sweep;
    // Beide Gangarten zeichnen auf eigenem Weg, die neue baut die Uhr einmal ganz auf.
    m_clockDirty = true;
#if defined(ARDUINO_ARCH_ESP32)
    m_dialDirty = true;
#endif
}

bool MainScreen::validTime(const Status &status)
{
    if (!status.timeValid || status.year < 2024 || status.year > 9999 || status.month < 1 || status.month > 12 || status.weekday > 6 || status.hour > 23 || status.minute > 59 || status.second > 59)
        return false;
    static constexpr uint8_t Days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    unsigned limit = Days[status.month - 1];
    if (status.month == 2 && status.year % 4 == 0 && (status.year % 100 != 0 || status.year % 400 == 0))
        ++limit;
    return status.day >= 1 && status.day <= limit;
}

void MainScreen::copySsid(char (&out)[33], const char (&input)[33])
{
    size_t i = 0, written = 0;
    while (i < 32 && input[i])
    {
        const auto first = static_cast<unsigned char>(input[i]);
        size_t count = 1;
        uint32_t code = first;
        bool valid = first >= 32 && first < 127;
        if (first >= 0xC2 && first <= 0xF4)
        {
            count = first < 0xE0 ? 2 : (first < 0xF0 ? 3 : 4);
            code = first & (count == 2 ? 0x1F : count == 3 ? 0x0F
                                                           : 0x07);
            valid = i + count <= 32;
            for (size_t j = 1; valid && j < count; ++j)
            {
                const auto next = static_cast<unsigned char>(input[i + j]);
                valid = (next & 0xC0) == 0x80;
                code = (code << 6) | (next & 0x3F);
            }
            const uint32_t minimum = count == 2 ? 0x80 : count == 3 ? 0x800
                                                                    : 0x10000;
            valid = valid && code >= minimum && code <= 0x10FFFF && !(code >= 0xD800 && code <= 0xDFFF);
        }
        if (!valid)
        {
            out[written++] = '?';
            ++i;
            continue;
        }
        if (written + count > 32)
            break;
        memcpy(out + written, input + i, count);
        written += count;
        i += count;
    }
    out[written] = '\0';
}

bool MainScreen::setStatus(const Status &status)
{
    if (m_hasStatus && (status.revision == m_status.revision || static_cast<uint32_t>(status.revision - m_status.revision) >= 0x80000000u))
        return false;
    Status next = status;
    copySsid(next.ssid, status.ssid);
    next.language = status.language == LangIndex::EN ? LangIndex::EN : LangIndex::DE;
    next.timeValid = validTime(next);
    if (!next.timeValid)
    {
        next.year = next.month = next.day = next.weekday = next.hour = next.minute = next.second = 0;
        next.dstActive = false;
    }
    if (static_cast<unsigned>(next.wifi) > static_cast<unsigned>(WifiState::Connected))
        next.wifi = WifiState::Unknown;
    if (next.signalPercent > 100)
        next.signalPercent = 100;
    if (next.wifi != WifiState::Connected)
    {
        next.ssid[0] = '\0';
        next.ipv4Valid = false;
        next.signalValid = false;
    }
    if (!(next.ipv4[0] | next.ipv4[1] | next.ipv4[2] | next.ipv4[3]))
        next.ipv4Valid = false;
    if (!next.ipv4Valid)
        memset(next.ipv4, 0, sizeof(next.ipv4));
    if (!next.signalValid)
        next.signalPercent = 0;
    if (next.battery.percent > 100)
        next.battery.percent = 100;
    m_batteryDirty |= next.battery != m_status.battery;
    const bool language = !m_hasStatus || next.language != m_status.language;
    const bool calendar = language || next.timeValid != m_status.timeValid || next.year != m_status.year || next.month != m_status.month || next.day != m_status.day || next.weekday != m_status.weekday || next.timeZoneRevision != m_status.timeZoneRevision;
    m_dateDirty |= calendar;
    m_clockDirty |= calendar || next.hour != m_status.hour || next.minute != m_status.minute || next.second != m_status.second || next.ntpSynced != m_status.ntpSynced || next.dstActive != m_status.dstActive || (!next.timeValid && next.wifi != m_status.wifi);
    m_clockDirty |= language || next.wifi != m_status.wifi || next.signalValid != m_status.signalValid || next.signalPercent != m_status.signalPercent || next.ipv4Valid != m_status.ipv4Valid || memcmp(next.ipv4, m_status.ipv4, sizeof(next.ipv4)) || strcmp(next.ssid, m_status.ssid);
    if (!m_hasStatus || next.wifi != m_status.wifi)
        m_buttonDirty |= 1;
#if defined(ARDUINO_ARCH_ESP32)
    // Das Zifferblatt ohne Zeiger hängt nicht an der Uhrzeit. Die Signalstärke schwankt laufend, darum zählt nur die Zahl der Balken.
    m_dialDirty |= next.timeValid != m_status.timeValid || next.ntpSynced != m_status.ntpSynced || next.dstActive != m_status.dstActive || next.wifi != m_status.wifi || next.ipv4Valid != m_status.ipv4Valid || memcmp(next.ipv4, m_status.ipv4, sizeof(next.ipv4)) || strcmp(next.ssid, m_status.ssid);
    m_barsDirty |= signalBars(next) != signalBars(m_status);
#endif
    m_status = next;
    m_hasStatus = true;
    return true;
}

const char *MainScreen::text(MainScreenKey key) const
{
    return I18n::mainscreen(key, m_status.language);
}

void MainScreen::fitText(lgfx::LGFX_Sprite &sprite, char *value, size_t capacity, int maxWidth)
{
    if (!value || capacity < 4)
        return;
    value[capacity - 1] = '\0';
    if (sprite.textWidth(value) <= maxWidth)
        return;
    size_t length = strlen(value);
    do
    {
        if (length)
        {
            --length;
            while (length && (static_cast<unsigned char>(value[length]) & 0xC0) == 0x80)
                --length;
        }
        if (length + 4 > capacity)
            continue;
        memcpy(value + length, "\xE2\x80\xA6", 4);
        if (sprite.textWidth(value) <= maxWidth)
            return;
    } while (length);
    value[0] = '\0';
}

int8_t MainScreen::buttonAt(int32_t x, int32_t y)
{
    // Die Trefferfläche schließt die Grenzen um den Symbolmittelpunkt ein.
    if (y < Buttons::START_Y || y > Buttons::START_Y + Buttons::SIZE)
        return -1;
    for (int8_t i = 0; i < 3; ++i)
    {
        const int left = Buttons::START_X + i * Buttons::STEP_X;
        if (x >= left && x <= left + Buttons::SIZE)
            return i;
    }
    return -1;
}

void MainScreen::cancelTouch()
{
    if (m_pressed >= 0)
        m_buttonDirty |= 1u << m_pressed;
    m_pressed = -1;
    m_contact = m_touchProcessed = false;
}

MainScreen::Action MainScreen::touch(TouchPhase phase, int32_t x, int32_t y)
{
    if (!m_visible || !m_ready)
        return Action::None;
    if (phase == TouchPhase::Cancel || phase == TouchPhase::Release)
    {
        cancelTouch();
        return Action::None;
    }
    if (phase == TouchPhase::Press)
        m_contact = true;
    if (!m_contact || m_touchProcessed)
        return Action::None;
    const int8_t target = buttonAt(x, y);
    if (target < 0 || (target == 0 && m_status.wifi != WifiState::Connected))
        return Action::None;
    m_touchProcessed = true;
    m_pressed = target;
    m_buttonDirty |= 1u << target;
    // Die Druckrückmeldung wird vor der Rückgabe der Navigationsaktion gezeichnet.
    drawButton(target);
    m_buttonDirty &= ~(1u << target);
    return static_cast<Action>(target + 1);
}

void MainScreen::drawAnalogClock(lgfx::LGFX_Sprite *buffer, int centerX, int centerY, float radius)
{
    if (!buffer)
        return;
    drawDial(buffer, centerX, centerY, radius);

    // Zeiger nur zeichnen wenn die Systemzeit plausibel ist. Ohne NTP-Sync und ohne gepufferte RTC bleibt das Zifferblatt leer - so sieht der Anwender sofort, dass keine aktuelle Uhrzeit verfügbar ist.
    if (m_status.timeValid)
    {
        const int DEGREES_PER_HOUR = 30;
        const int DEGREES_PER_MINUTE = 6;
        const float hourLength = radius * HourHandRatio;
        const float minuteLength = radius * MinuteHandRatio;
        const float secondLength = radius * SecondHandRatio;
        const float handWidth = radius * HandWidthRatio;
        const float centerCircleRadius = radius * CenterCircleRatio;

        const float hourAngle = (m_status.hour % 12 + m_status.minute / 60.0f) * DEGREES_PER_HOUR;
        const float minuteAngle = (m_status.minute + m_status.second / 60.0f) * DEGREES_PER_MINUTE;
        const float secondAngle = m_status.second * DEGREES_PER_MINUTE;

        const uint16_t handColor = farbe(Rolle::MainUhrZeiger);
        auto hourPos = polarPoint(centerX, centerY, hourAngle, hourLength);
        buffer->drawWideLine(centerX, centerY, hourPos.first, hourPos.second, handWidth, handColor);

        auto minPos = polarPoint(centerX, centerY, minuteAngle, minuteLength);
        buffer->drawWideLine(centerX, centerY, minPos.first, minPos.second, handWidth, handColor);

        buffer->fillSmoothCircle(centerX, centerY, centerCircleRadius, farbe(Rolle::MainUhrAchse));

        const float counterWeightLength = radius * CounterWeightRatio;
        const float secHandWidth = radius * SecondWidthRatio;

        auto secTip = polarPoint(centerX, centerY, secondAngle, secondLength);
        auto counterWeightTip = polarPoint(centerX, centerY, secondAngle + 180, counterWeightLength);

        buffer->drawWedgeLine(roundf(counterWeightTip.first), roundf(counterWeightTip.second), roundf(secTip.first), roundf(secTip.second), secHandWidth, Clock::SECOND_TIP, farbe(Rolle::MainUhrSekunde));

        buffer->fillSmoothCircle(centerX, centerY, centerCircleRadius * 0.8f, farbe(Rolle::MainUhrAchseAkzent));
    }
}

void MainScreen::drawDial(lgfx::LGFX_Sprite *buffer, int centerX, int centerY, float radius)
{
    const uint16_t majorMarkColor = farbe(Rolle::MainUhrMarken), minorMarkColor = farbe(Rolle::MainUhrMarkenKlein), statusOffColor = farbe(Rolle::MainStatusAus);
    const uint16_t ntpColor = farbe(Rolle::MainStatusNtp), dstColor = farbe(Rolle::MainStatusDst);
    // Transparenzschlüssel der Beschriftungscaches. Er bildet Gem.Grund ab, mit dem renderLabelCaches sie löscht.
    const uint16_t labelKey = farbe(Rolle::GemGrund);

    const float HOUR_MARK_LENGTH = radius * 0.15f;
    const float MINOR_MARK_LENGTH = radius * 0.08f;
    const int DEGREES_PER_HOUR = 30;
    const int DEGREES_PER_MINUTE = 6;

    bool ntpSynced = m_status.ntpSynced;
    bool dstActive = m_status.dstActive;
    bool wifiConnected = m_status.wifi == WifiState::Connected;

    buffer->setTextDatum(TL_DATUM);

    for (int minute = 0; minute < 60; minute++)
    {
        if (minute % 5 != 0)
        {
            const float angleDeg = minute * DEGREES_PER_MINUTE;
            auto outer = polarPoint(centerX, centerY, angleDeg, radius - Clock::MARK_INSET);
            auto inner = polarPoint(centerX, centerY, angleDeg, radius - Clock::MARK_INSET - MINOR_MARK_LENGTH);
            buffer->drawWideLine(roundf(inner.first), roundf(inner.second), roundf(outer.first), roundf(outer.second), Clock::MINOR_MARK_W, minorMarkColor);
        }
    }

    // Stunden-Markierungen (mit Status-Farben für 11 Uhr und 1 Uhr)
    for (int hour = 0; hour < 12; hour++)
    {
        const float angleDeg = hour * DEGREES_PER_HOUR;
        auto outer = polarPoint(centerX, centerY, angleDeg, radius - Clock::MARK_INSET);
        auto inner = polarPoint(centerX, centerY, angleDeg, radius - Clock::MARK_INSET - HOUR_MARK_LENGTH);

        float lineWidth = (hour % 3 == 0) ? Clock::MAJOR_MARK_W : Clock::HOUR_MARK_W;
        uint16_t markColor = (hour % 3 == 0) ? majorMarkColor : minorMarkColor;

        // Status-Marker für 11 Uhr (NTP) und 1 Uhr (DST)
        if (hour == 11)
        {
            markColor = ntpSynced ? ntpColor : statusOffColor;
            lineWidth = Clock::MAJOR_MARK_W;
        }
        else if (hour == 1)
        {
            markColor = dstActive ? dstColor : statusOffColor;
            lineWidth = Clock::MAJOR_MARK_W;
        }

        buffer->drawWideLine(roundf(inner.first), roundf(inner.second), roundf(outer.first), roundf(outer.second), lineWidth, markColor);
    }

    // Rotierte NTP/DST-Labels

    {
        // NTP-Label bei 11 Uhr (330°), radial innerhalb der Markierung
        constexpr float NTP_ANGLE = 330.0f;
        auto ntpPos = polarPoint(centerX, centerY, NTP_ANGLE, radius * Indicators::LABEL_RADIAL_POS);
        m_labelNtp.pushRotateZoomWithAA(buffer, roundf(ntpPos.first), roundf(ntpPos.second), NTP_ANGLE + 90.0f, 1.0f, 1.0f, labelKey);
    }

    {
        // DST-Label bei 1 Uhr (30°), radial innerhalb der Markierung
        constexpr float DST_ANGLE = 30.0f;
        auto dstPos = polarPoint(centerX, centerY, DST_ANGLE, radius * Indicators::LABEL_RADIAL_POS);
        m_labelDst.pushRotateZoomWithAA(buffer, roundf(dstPos.first), roundf(dstPos.second), DST_ANGLE + 90.0f, 1.0f, 1.0f, labelKey);
    }

    // WiFi-Signalbalken
    const int yBottom = drawSignalBars(buffer, centerX, centerY, radius);

    // SSID + IP unter den Balken anzeigen
    const char *ssid = m_status.ssid;
    if (wifiConnected && ssid[0] != '\0')
    {
        buffer->setFont(&m_tinyFont);
        buffer->setTextDatum(TC_DATUM);
        buffer->setTextColor(farbe(Rolle::MainSsid));

        // Verfügbare Breite: Uhrenradius begrenzt den Platz
        constexpr int maxTextW = Indicators::SSID_MAX_W;
        int ssidY = yBottom + Indicators::SSID_OFFSET_Y;

        char truncated[36];
        stringutils::safeCopy(truncated, ssid);
        fitText(*buffer, truncated, sizeof(truncated), maxTextW);
        buffer->drawString(truncated, centerX, ssidY);

        // IP-Adresse direkt unter SSID (gleiche Font, gleiches Datum, eigene Farbe)
        const auto &ip = m_status.ipv4;
        if (m_status.ipv4Valid)
        {
            char ipBuf[16]; // "255.255.255.255\0" = 16 Bytes
            stringutils::safeFormat(ipBuf, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
            int ipY = ssidY + Indicators::IP_OFFSET_Y;
            buffer->setFont(&m_smallFont);
            buffer->setTextColor(farbe(Rolle::MainIp));
            buffer->drawString(ipBuf, centerX, ipY);
        }

        buffer->setFont(nullptr);
    }
}

int MainScreen::drawSignalBars(lgfx::LGFX_Sprite *buffer, int centerX, int centerY, float radius)
{
    constexpr int barCount = Indicators::SIGNAL_BAR_COUNT;
    constexpr int barW = Indicators::SIGNAL_BAR_W;
    constexpr int gap = Indicators::SIGNAL_BAR_GAP;

    // Position: zwischen Zentrum und 6-Uhr-Marker
    int barCenterY = centerY + int(radius * Indicators::SIGNAL_BAR_RADIAL_POS);
    int x0 = centerX - SignalBarsWidth / 2;
    int yBottom = barCenterY + SignalBarsHeight / 2;

    const bool wifiConnected = m_status.wifi == WifiState::Connected;
    const int activeBars = signalBars(m_status);
    const uint16_t statusOffColor = farbe(Rolle::MainStatusAus), wifiColor = farbe(Rolle::MainStatusWlan);

    for (int i = 0; i < barCount; ++i)
    {
        int barH = Indicators::SIGNAL_BAR_H[i];
        int bx = x0 + i * (barW + gap);
        int by = yBottom - barH;

        uint16_t color;
        if (!wifiConnected)
            color = statusOffColor;
        else
            color = (i < activeBars) ? wifiColor : statusOffColor;

        buffer->fillRect(bx, by, barW, barH, color);
    }
    return yBottom;
}

int MainScreen::signalBars(const Status &status)
{
    if (status.wifi != WifiState::Connected || status.signalPercent == 0)
        return 0;
    return (status.signalPercent - 1) * Indicators::SIGNAL_BAR_COUNT / 100 + 1;
}

#if defined(ARDUINO_ARCH_ESP32)
bool MainScreen::updateSweep()
{
    const uint16_t ground = farbe(Rolle::GemGrund);
    bool changed = m_buttonDirty != 0;
    if (m_fullDirty)
    {
        m_display->waitDMA();
        clearBackground();
        for (auto &button : m_buttons)
            button.clear(ground);
        m_fullDirty = false;
        m_dialDirty = true;
    }
    if (m_dialDirty)
    {
        m_dial.fillRect(0, 0, Clock::SPRITE_W, Clock::DATE_Y, ground);
        drawDial(&m_dial, Clock::CENTER_X, Clock::CENTER_Y, Clock::RADIUS);
        m_dialDirty = m_barsDirty = m_clockDirty = false;
        m_composed = false;
    }
    const uint32_t frame = Tab5Render::frames();
    if (!m_composed)
    {
        composeClock(m_status.timeValid ? currentHands() : Hands{});
        m_lastFrame = frame;
        changed = true;
    }
    else
    {
        if (m_dateDirty)
        {
            // Die Datumszeile liegt unter dem Zifferblatt, die Zeiger erreichen sie nicht.
            renderDate();
            const auto *pixels = static_cast<const uint16_t *>(m_clock.getBuffer());
            Tab5Render::pushRegion(*m_display, pixels + Clock::DATE_Y * Clock::SPRITE_W, Clock::SPRITE_W, Clock::SPRITE_X, Clock::SPRITE_Y + Clock::DATE_Y, Clock::SPRITE_W, Clock::DATE_H);
            changed = true;
        }
        // Ein Zeigerschritt je Displaybild. Die Spitze rückt dabei um rund ein halbes Pixel, ein Versatz zum Lesestrahl des Panels bleibt so unsichtbar.
        if (frame != m_lastFrame)
        {
            m_lastFrame = frame;
            Box boxes[4];
            unsigned count = 0;
            if (m_barsDirty)
            {
                const int bottom = drawSignalBars(&m_dial, Clock::CENTER_X, Clock::CENTER_Y, Clock::RADIUS);
                boxes[count++] = {Clock::CENTER_X - SignalBarsWidth / 2, bottom - SignalBarsHeight, SignalBarsWidth, SignalBarsHeight};
                m_barsDirty = false;
            }
            Hands hands = m_drawn;
            if (m_status.timeValid)
            {
                hands = currentHands();
                const float radius = Clock::RADIUS;
                // Je bewegtem Zeiger ein Rechteck um alte und neue Stellung, jedes enthält alle Zeiger.
                auto add = [&](float before, float after, float back, float length, float width)
                {
                    if (before == after)
                        return;
                    const Box a = handBox(before, back, length, width), b = handBox(after, back, length, width);
                    const int left = std::min(a.x, b.x), top = std::min(a.y, b.y);
                    boxes[count++] = {left, top, std::max(a.x + a.w, b.x + b.w) - left, std::max(a.y + a.h, b.y + b.h) - top};
                };
                add(m_drawn.second, hands.second, radius * CounterWeightRatio, radius * SecondHandRatio, radius * SecondWidthRatio);
                add(m_drawn.minute, hands.minute, 0.0f, radius * MinuteHandRatio, radius * HandWidthRatio);
                add(m_drawn.hour, hands.hour, 0.0f, radius * HourHandRatio, radius * HandWidthRatio);
            }
            for (unsigned i = 0; i < count; ++i)
                if (!pushBox(boxes[i], hands))
                {
                    // Nach einem Zeitsprung (NTP-Abgleich, Sommerzeit) passt das Rechteck nicht in den Ausschnittpuffer, dann geht das ganze Uhrsprite.
                    composeClock(hands);
                    break;
                }
            m_drawn = hands;
            changed |= count != 0;
        }
    }
    for (unsigned i = 0; i < 3; ++i)
        if (m_buttonDirty & (1u << i))
            drawButton(i);
    m_buttonDirty = 0;
    if (m_batteryDirty)
        changed |= drawBattery();
    return changed;
}

MainScreen::Hands MainScreen::currentHands()
{
    timeval now{};
    gettimeofday(&now, nullptr);
    const time_t seconds = now.tv_sec;
    tm local{};
    localtime_r(&seconds, &local);
    Hands hands;
    hands.second = (local.tm_sec + now.tv_usec / 1000000.0f) * 6.0f;
    hands.minute = (local.tm_min + local.tm_sec / 60.0f) * 6.0f;
    hands.hour = (local.tm_hour % 12 + local.tm_min / 60.0f) * 30.0f;
    return hands;
}

void MainScreen::composeClock(const Hands &hands)
{
    // Beide Sprites haben dieselbe Breite, das Zifferblatt belegt die ersten DATE_Y Zeilen.
    memcpy(m_clock.getBuffer(), m_dial.getBuffer(), static_cast<size_t>(Clock::SPRITE_W) * Clock::DATE_Y * sizeof(uint16_t));
    if (m_status.timeValid)
    {
        m_view.setBuffer(m_clock.getBuffer(), Clock::SPRITE_W, Clock::DATE_Y, 16);
        drawHands(0, 0, hands);
    }
    if (m_dateDirty)
        renderDate();
    Tab5Render::pushSprite(m_display, m_clock, Clock::SPRITE_X, Clock::SPRITE_Y);
    m_display->waitDMA();
    m_drawn = hands;
    m_composed = true;
}

bool MainScreen::pushBox(const Box &box, const Hands &hands)
{
    if (box.w <= 0 || box.h <= 0)
        return true;
    if (box.w * box.h > ViewPixels)
        return false;
    const auto *dial = static_cast<const uint16_t *>(m_dial.getBuffer());
    for (int row = 0; row < box.h; ++row)
        memcpy(m_viewPixels + row * box.w, dial + (box.y + row) * Clock::SPRITE_W + box.x, box.w * sizeof(uint16_t));
    if (m_status.timeValid)
    {
        m_view.setBuffer(m_viewPixels, box.w, box.h, 16);
        drawHands(box.x, box.y, hands);
    }
    Tab5Render::pushRegion(*m_display, m_viewPixels, box.w, Clock::SPRITE_X + box.x, Clock::SPRITE_Y + box.y, box.w, box.h);
    return true;
}

void MainScreen::drawHands(int originX, int originY, const Hands &hands)
{
    const float radius = Clock::RADIUS;
    const int centerX = Clock::CENTER_X - originX, centerY = Clock::CENTER_Y - originY;
    const float handWidth = radius * HandWidthRatio;
    const uint16_t handColor = farbe(Rolle::MainUhrZeiger);
    const auto hour = polarPoint(centerX, centerY, hands.hour, radius * HourHandRatio);
    m_view.wedge(centerX, centerY, hour.first, hour.second, handWidth, handWidth, handColor);
    const auto minute = polarPoint(centerX, centerY, hands.minute, radius * MinuteHandRatio);
    m_view.wedge(centerX, centerY, minute.first, minute.second, handWidth, handWidth, handColor);
    m_view.fillSmoothCircle(centerX, centerY, static_cast<int>(radius * CenterCircleRatio), farbe(Rolle::MainUhrAchse));
    const auto tip = polarPoint(centerX, centerY, hands.second, radius * SecondHandRatio);
    const auto back = polarPoint(centerX, centerY, hands.second + 180.0f, radius * CounterWeightRatio);
    m_view.wedge(back.first, back.second, tip.first, tip.second, radius * SecondWidthRatio, Clock::SECOND_TIP, farbe(Rolle::MainUhrSekunde));
    m_view.fillSmoothCircle(centerX, centerY, static_cast<int>(radius * CenterCircleRatio * 0.8f), farbe(Rolle::MainUhrAchseAkzent));
}

MainScreen::Box MainScreen::handBox(float angle, float back, float length, float width)
{
    const auto a = polarPoint(Clock::CENTER_X, Clock::CENTER_Y, angle + 180.0f, back);
    const auto b = polarPoint(Clock::CENTER_X, Clock::CENTER_Y, angle, length);
    // Saum für die Glättung von M5GFX (ein halbes Pixel über die Halbbreite) und das Runden auf ganze Pixel.
    const float margin = width + 2.0f;
    const int left = std::max(0, static_cast<int>(floorf(std::min(a.first, b.first) - margin)));
    const int top = std::max(0, static_cast<int>(floorf(std::min(a.second, b.second) - margin)));
    const int right = std::min(Clock::SPRITE_W, static_cast<int>(ceilf(std::max(a.first, b.first) + margin)) + 1);
    const int bottom = std::min(Clock::DATE_Y, static_cast<int>(ceilf(std::max(a.second, b.second) + margin)) + 1);
    return {left, top, right - left, bottom - top};
}
#endif

void MainScreen::renderLabelCaches()
{
    // Der Grund der Beschriftungen ist Gem.Grund, drawAnalogClock nimmt ihn als Transparenzschlüssel.
    const uint16_t ground = farbe(Rolle::GemGrund), textColor = farbe(Rolle::MainStatusText);
    for (auto *label : {&m_labelNtp, &m_labelDst})
    {
        label->clear(ground);
        label->setFont(&m_smallFont);
        label->setTextDatum(MC_DATUM);
        label->setTextColor(textColor, ground);
        label->setPivot(Indicators::LABEL_W / 2, Indicators::LABEL_H / 2);
        label->drawString(label == &m_labelNtp ? "NTP" : "DST", Indicators::LABEL_W / 2, Indicators::LABEL_H / 2);
    }
}

void MainScreen::renderDate()
{
    char date[80];
    if (m_status.timeValid)
    {
        const auto weekday = static_cast<MainScreenKey>(static_cast<unsigned>(MainScreenKey::Sunday) + m_status.weekday);
        const auto month = static_cast<MainScreenKey>(static_cast<unsigned>(MainScreenKey::January) + m_status.month - 1);
        stringutils::safeFormat(date, text(MainScreenKey::DateFormat), text(weekday), unsigned(m_status.day), text(month), unsigned(m_status.year));
    }
    else
        stringutils::safeCopy(date, text(MainScreenKey::DateUnknown));
    // Die Datumszeile liegt unterhalb des Zifferblatts im Uhrsprite und bleibt beim Sekundentakt stehen.
    const uint16_t ground = farbe(Rolle::GemGrund);
    m_clock.fillRect(0, Clock::DATE_Y, Clock::SPRITE_W, Clock::DATE_H, ground);
    m_clock.setFont(&m_largeFont);
    m_clock.setTextDatum(BC_DATUM);
    m_clock.setTextColor(farbe(Rolle::MainDatum), ground);
    m_clock.drawString(date, Clock::CENTER_X, Clock::SPRITE_H);
    m_clock.setFont(nullptr);
    m_dateDirty = false;
}

void MainScreen::drawButton(unsigned index)
{
    auto &button = m_buttons[index];
    const bool enabled = index != 0 || m_status.wifi == WifiState::Connected;
    const bool inverted = m_pressed == static_cast<int8_t>(index);
    const uint16_t fill = farbe(enabled ? Rolle::MainTasteFlaeche : Rolle::GemTasteAus);
    const uint16_t outline = farbe(enabled ? (inverted ? Rolle::MainTasteText : Rolle::MainTasteRahmen) : Rolle::GemTasteAus);
    const int borderWidth = inverted ? Buttons::BORDER_PRESSED : Buttons::BORDER;
    constexpr int inset = Buttons::ICON_INSET, size = Buttons::ICON_SIZE;
    // Beim Statuswechsel bleibt der vorhandene Hintergrund für die Kantenglättung erhalten.
    button.fillSmoothRoundRect(0, 0, Buttons::SIZE, Buttons::SIZE, Buttons::RADIUS, outline);
    button.fillSmoothRoundRect(borderWidth, borderWidth, Buttons::SIZE - 2 * borderWidth, Buttons::SIZE - 2 * borderWidth, Buttons::RADIUS, fill);
    if (enabled)
        button.pushImage(inset, inset, size, size, m_icons[index]);
    else
    {
        // Die Helligkeit des eingebrannten Symbols dient als Deckung, auf hellem Grund ist es ihr Abstand zur Helligkeit der Symbolfläche.
        const uint16_t symbolColor = farbe(Rolle::GemSymbolAus);
        const bool hellerGrund = themaAktiv().merkmale & MerkmalHellerGrund;
        const float groundIntensity = hellerGrund ? std::max(getIntensityFrom565(farbe(Rolle::MainTasteFlaeche)), 0.05f) : 0.0f;
        for (int j = 0; j < size; ++j)
            for (int i = 0; i < size; ++i)
            {
                const float pixelIntensity = getIntensityFrom565(m_icons[index][j * size + i]);
                const float intensity = hellerGrund ? (groundIntensity - pixelIntensity) / groundIntensity : pixelIntensity;
                if (intensity >= 0.05f)
                    button.drawPixel(inset + i, inset + j, blendIcon(symbolColor, fill, intensity));
            }
    }
    Tab5Render::pushSprite(m_display, button, Buttons::START_X + index * Buttons::STEP_X, Buttons::START_Y);
    m_display->waitDMA();
}

bool MainScreen::drawBattery()
{
    m_batteryDirty = false;
    const auto &battery = m_status.battery;
    // Ohne Akku bleibt die Fläche Grund und der Sprite unangelegt. Geleert wird nur, wenn vorher ein Symbol dort stand.
    if ((!battery.present && !m_batteryShown) || m_batteryFailed)
        return false;
    if (!m_battery.width() && !prepare(m_battery, Battery::W, Battery::H))
    {
        m_batteryFailed = true;
        return false;
    }
    const uint16_t ground = farbe(Rolle::GemGrund), tint = farbe(Rolle::MainDatum);
    m_battery.clear(ground);
    if (battery.present)
    {
        char value[8];
        stringutils::safeFormat(value, "%u %%", static_cast<unsigned>(battery.percent));
        m_battery.setFont(&m_largeFont);
        m_battery.setTextDatum(BC_DATUM);
        m_battery.setTextColor(tint, ground);
        // Die Zahl steht rechtsbündig auf der Grundlinie des Datums, das Symbol links daneben.
        const int width = m_battery.textWidth(value);
        m_battery.drawString(value, Battery::W - (width + 1) / 2, Battery::H);
        m_battery.setFont(nullptr);
        BatteryIcon::draw(m_battery, std::max(0, Battery::W - width - Battery::GAP - Battery::ICON_W), Battery::ICON_Y, BatteryIcon::Large, battery, tint, ground);
    }
    m_batteryShown = battery.present;
    Tab5Render::pushSprite(m_display, m_battery, Battery::X, Battery::Y);
    m_display->waitDMA();
    return true;
}

void MainScreen::clearBackground()
{
    auto &display = *m_display;
    const uint16_t ground = farbe(Rolle::GemGrund);
    const int clockRight = Clock::SPRITE_X + Clock::SPRITE_W, clockBottom = Clock::SPRITE_Y + Clock::SPRITE_H;
    const int rowBottom = Buttons::START_Y + Buttons::SIZE;
    display.fillRect(0, 0, Width, Clock::SPRITE_Y, ground);
    display.fillRect(0, clockBottom, Width, Height - clockBottom, ground);
    display.fillRect(0, Clock::SPRITE_Y, Clock::SPRITE_X, Clock::SPRITE_H, ground);
    display.fillRect(clockRight, Clock::SPRITE_Y, Buttons::START_X - clockRight, Clock::SPRITE_H, ground);
    display.fillRect(Buttons::START_X, Clock::SPRITE_Y, Width - Buttons::START_X, Buttons::START_Y - Clock::SPRITE_Y, ground);
    display.fillRect(Buttons::START_X, rowBottom, Width - Buttons::START_X, clockBottom - rowBottom, ground);
    // Lücken zwischen den Symbolen und rechts neben dem letzten.
    for (int i = 0; i < 3; ++i)
    {
        const int gapX = Buttons::START_X + i * Buttons::STEP_X + Buttons::SIZE;
        display.fillRect(gapX, Buttons::START_Y, i < 2 ? Buttons::SPACING : Width - gapX, Buttons::SIZE, ground);
    }
}

bool MainScreen::update()
{
    if (!m_visible || !m_ready)
        return false;
#if defined(ARDUINO_ARCH_ESP32)
    // Ohne Bildsignal des Displays oder ohne Ausschnittpuffer bleibt es bei Sekundenschritten.
    if (m_sweep && m_viewPixels && Tab5Render::frames())
        return updateSweep();
#endif
    const bool changed = m_fullDirty || m_clockDirty || m_dateDirty || m_buttonDirty;
    const uint16_t ground = farbe(Rolle::GemGrund);
    if (m_fullDirty)
    {
        m_display->waitDMA();
        clearBackground();
        for (auto &button : m_buttons)
            button.clear(ground);
        m_fullDirty = false;
    }
    if (m_clockDirty || m_dateDirty)
    {
        if (m_clockDirty)
        {
            // Nur das Zifferblatt oberhalb der Datumszeile wird neu gezeichnet.
            m_clock.fillRect(0, 0, Clock::SPRITE_W, Clock::DATE_Y, ground);
            drawAnalogClock(&m_clock, Clock::CENTER_X, Clock::CENTER_Y, Clock::RADIUS);
        }
        if (m_dateDirty)
            renderDate();
        Tab5Render::pushSprite(m_display, m_clock, Clock::SPRITE_X, Clock::SPRITE_Y);
        m_display->waitDMA();
        m_clockDirty = false;
    }
    for (unsigned i = 0; i < 3; ++i)
        if (m_buttonDirty & (1u << i))
            drawButton(i);
    m_buttonDirty = 0;
    const bool battery = m_batteryDirty && drawBattery();
    return changed || battery;
}
