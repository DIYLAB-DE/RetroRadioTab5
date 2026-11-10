#include "RetroRadio.h"
#include "Tab5Render.h"
#include "StringUtils.h"
#include "Logger.h"
#include "IconShapes.h"
#include "fonts/ComfortaaBold15.h"
#include "fonts/ComfortaaBold21.h"
#include "fonts/ComfortaaBold24.h"
#include "fonts/ComfortaaBold27.h"
#include "fonts/LeagueGothicCond51.h"
#include "images/logo_135.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>
#include <utility>
extern const __FlashStringHelper *i18n_getRetroRadio(RetroRadioKey, LangIndex);
#define STR(key) i18n_getRetroRadio(key, m_state.language)
constexpr float PI_F = 3.1415927f;
namespace NL = RadioTab5Layout;
static const uint8_t *const ScaleFont = ComfortaaBold21; ///< Schrift der Senderskala, das Original nutzt ComfortaaBold14
static constexpr uint32_t FrameWaitMs = 40;              ///< Höchstwartezeit auf die Bildfertigmeldung des Displays, gut zwei Bilder bei 64 Bildern/s
static constexpr float MaxStepSeconds = .04f;            ///< Längster Zeitschritt der Nadelfahrt, gut zwei Bilder: Nach einem Aussetzer fährt die Nadel weiter, statt den Weg aufzuholen
static constexpr int EyeRowsPerStep = 7;                  ///< Spritezeilen je Zeichenschritt des Auges, 15 Schritte je Bild. Während der Nadelfahrt passen so mehrere Schritte in die Restzeit eines Displaybildes
static constexpr uint32_t FrameReserveUs = 6000;         ///< Reserve je Displaybild für die übrige Hauptschleife (Touch, Sensoren, Dienste, rund 4 ms) und den nächsten Nadelschritt
static constexpr uint32_t StereoStableMs = 500;          ///< Mindestdauer eines gleichen Mono-/Stereo-Ergebnisses vor dem Wechsel des Symbols
// Selektive Originalzeichenpfade: RetroRadio ea4229a34fb39faaff23619092cf09cafb8c51cf. Skala, Infozeile und Auge nativ nach RadioTab5Layout.

void RetroRadio::drawScale()
{
    if (!(m_sprRetroScale && m_sprRetroScale->width() == NL::WIDTH && m_sprRetroScale->height() == NL::Scale::HEIGHT) || !(m_sprRetroDiv && m_sprRetroDiv->width() == NL::WIDTH && m_sprRetroDiv->height() == NL::Scale::DIVIDER_HEIGHT))
    {
        LOG_ERROR("Konnte Skala-Sprites nicht erstellen");
        return;
    }

    // Die Skala trägt das Logo mit eingebranntem Grund Gem.Flaeche (Original) bzw. als graue Kopie auf der Fläche des Themas
    const uint16_t panel = farbe(Rolle::GemFlaeche), favorite = farbe(Rolle::RadFavorit), station = farbe(Rolle::RadSenderMarke), empty = farbe(Rolle::GemRahmen);
    m_sprRetroScale->clear(panel);
    m_sprRetroScale->loadFont(ScaleFont);
    // Ohne Hintergrundfarbe: Der Schriftkasten ist so hoch wie der Zeilenabstand, ein gefüllter Kasten des nächsten Namens würde die Unterstreichung überdecken
    m_sprRetroScale->setTextColor(farbe(Rolle::GemTextSilber));
    m_sprRetroScale->setTextWrap(false);

    m_sprRetroDiv->clear(farbe(Rolle::GemTeiler));

    uint16_t scalaPosX = NL::Scale::STATION_X0;
    uint16_t scalaPosY = NL::Scale::STATION_Y0;

    for (uint16_t i = 0; i < m_stationCounter; i++)
    {
        // Auch leere Slots erhalten eine Position, weil die Touchzuordnung sie braucht
        m_stationList[i].x = scalaPosX;
        m_stationList[i].y = scalaPosY;

        if (m_stationList[i].url[0] != '\0')
        {
            m_sprRetroScale->fillRect(scalaPosX + NL::Scale::MARKER_X, scalaPosY + NL::Scale::MARKER_Y, NL::Scale::MARKER_W, NL::Scale::MARKER_H, (m_stationList[i].preset) ? favorite : station);
            m_sprRetroScale->drawString(m_stationList[i].shortName, scalaPosX + NL::Scale::NAME_X, scalaPosY + NL::Scale::NAME_Y);

            if (m_stationList[i].preset)
            {
                int16_t textWidth = m_sprRetroScale->textWidth(m_stationList[i].shortName);
                m_sprRetroScale->fillRect(scalaPosX + NL::Scale::NAME_X, scalaPosY + NL::Scale::NAME_Y + m_sprRetroScale->fontHeight(), textWidth, NL::Scale::UNDERLINE_H, favorite);
            }
        }
        else
        {
            // Leerer Slot: dezenter Marker als Platzhalter
            m_sprRetroScale->fillRect(scalaPosX + NL::Scale::MARKER_X, scalaPosY + NL::Scale::MARKER_Y, NL::Scale::MARKER_W, NL::Scale::MARKER_H, empty);
        }

        scalaPosX += NL::Scale::STATION_STEP_X;
        scalaPosY += NL::Scale::STATION_STEP_Y;

        if (scalaPosY > NL::Scale::ROW_WRAP_Y)
            scalaPosY = NL::Scale::ROW_RESTART_Y;
    }

    present(*m_sprRetroDiv, 0, NL::Scale::TOP_DIVIDER_Y);
    m_sprRetroScale->pushImage(NL::Scale::LOGO_X, NL::Scale::LOGO_Y, NL::Scale::LOGO_W, NL::Scale::LOGO_H, m_logoGrau ? m_logoGrau : logo_135);
    present(*m_sprRetroScale, 0, NL::Scale::Y);
    present(*m_sprRetroDiv, 0, NL::Scale::BOTTOM_DIVIDER_Y);
    // Die Szene trägt jetzt die Skala ohne Nadel. Die Panelkopie legt jede Querformatspalte als zusammenhängende Zeile in Panelrichtung ab, damit die Nadel ihren Hintergrund ohne Spaltenzugriffe ins PSRAM holt. Neu aufgebaut wird sie nur nach einer neuen Senderliste oder Drehlage, weil der Aufbau rund 70 ms kostet und jedes Schließen eines Dialogs die Skala neu zeichnet.
    m_sceneNeedle = m_sceneNeedleSub = -1;
    const int rotation = m_display->getRotation();
    if (!m_panelScale || (rotation != 3 && rotation != 1))
        m_panelRotation = -1;
    else if (m_panelStale || m_panelRotation != rotation)
    {
        m_scale.columnsToRows(m_panelScale, 0, NL::WIDTH, rotation == 1);
        m_panelRotation = rotation;
        m_panelStale = false;
    }
}

void RetroRadio::updateStereoIndicator(bool isMono)
{
    if (!m_sprRetroStereo)
    {
        LOG_ERROR("m_sprRetroStereo nicht initialisiert");
        return;
    }

    // Mono gedimmt, Stereo hell
    const uint16_t panel = farbe(Rolle::GemFlaeche), ring = farbe(isMono ? Rolle::RadStereoAus : Rolle::GemTextSilber);
    m_sprRetroStereo->clear(panel);
    m_sprRetroStereo->drawSmoothCircle(NL::Footer::STEREO_LEFT_X, NL::Footer::STEREO_CY, NL::Footer::STEREO_R, NL::Footer::STEREO_THICKNESS, ring, panel);
    m_sprRetroStereo->drawSmoothCircle(NL::Footer::STEREO_RIGHT_X, NL::Footer::STEREO_CY, NL::Footer::STEREO_R, NL::Footer::STEREO_THICKNESS, ring, panel);
    present(*m_sprRetroStereo, NL::Footer::STEREO_X, NL::Footer::STEREO_Y);
}

void RetroRadio::updateCodecIndicator(AudioManager::Codec codec)
{
    m_codec.clear(farbe(Rolle::GemFlaeche));
    if (codec != AudioManager::Codec::None)
    {
        // Farben der Formatmarken im SD-Player. WAV hat dort keine eigene Farbe und nimmt wie alle übrigen Formate die von AAC.
        using C = AudioManager::Codec;
        const char *label = codec == C::Mp3 ? "MP3" : codec == C::Aac  ? "AAC"
                                                  : codec == C::Flac   ? "FLAC"
                                                  : codec == C::Vorbis ? "Vorbis"
                                                                       : "WAV";
        const Rolle rolle = codec == C::Mp3 ? Rolle::SDBadgeMp3 : codec == C::Flac ? Rolle::SDBadgeFlac
                                                              : codec == C::Vorbis ? Rolle::SDBadgeOgg
                                                                                   : Rolle::SDBadgeAac;
        m_codec.loadFont(ComfortaaBold15);
        m_codec.setTextDatum(middle_center);
        m_codec.setTextColor(farbe(Rolle::SDBadgeText));
        const int w = std::min<int>(m_codec.textWidth(label) + NL::Footer::CODEC_BADGE_PAD, NL::Footer::CODEC_W);
        const int x = (NL::Footer::CODEC_W - w) / 2, y = (NL::Footer::CODEC_H - NL::Footer::CODEC_BADGE_H) / 2;
        m_codec.fillRect(x, y, w, NL::Footer::CODEC_BADGE_H, farbe(rolle));
        m_codec.drawString(label, x + w / 2, y + NL::Footer::CODEC_BADGE_H / 2);
        m_codec.unloadFont();
    }
    present(m_codec, NL::Footer::CODEC_X, NL::Footer::CODEC_Y);
}

void RetroRadio::drawFooterText(bool init, const char *sender, const char *title, const char *bitrate, uint16_t senderColor, uint16_t titleColor, uint16_t bitrateColor, uint16_t separatorColor, const char *separator, int reservedPixels, uint16_t backgroundColor)
{
    // Schriftstufen von groß nach klein. Die Originalschriften sind ComfortaaBold18, 16 und 14
    static const uint8_t *fonts[] = {ComfortaaBold27, ComfortaaBold24, ComfortaaBold21};
    static const int fontCount = sizeof(fonts) / sizeof(fonts[0]);
    static const char *ellipsis = "...";

    // init zeichnet nur den leeren Rahmen der Infozeile
    if (init)
    {
        if (!(m_sprRetroFooter && m_sprRetroFooter->width() == NL::WIDTH && m_sprRetroFooter->height() == NL::Footer::HEIGHT))
        {
            LOG_ERROR("Konnte m_sprRetroFooter Sprite nicht erstellen!");
            return;
        }

        m_sprRetroFooter->setTextWrap(false);
        m_sprRetroFooter->setTextDatum(ML_DATUM);

        const uint16_t line = farbe(Rolle::GemRahmen);
        m_sprRetroFooter->clear(backgroundColor);
        m_sprRetroFooter->fillRect(0, 0, NL::WIDTH, NL::Footer::LINE, line);
        m_sprRetroFooter->fillRect(0, NL::Footer::HEIGHT - NL::Footer::LINE, NL::WIDTH, NL::Footer::LINE, line);
        present(*m_sprRetroFooter, 0, NL::Footer::Y);

        return;
    }

    if (!sender || !title || !bitrate || !separator)
        return;

    const bool hasSender = sender[0] != '\0';
    const bool hasTitle = title[0] != '\0';
    const bool hasBitrate = bitrate[0] != '\0';

    if (!hasSender && !hasTitle && !hasBitrate)
        return;

    m_sprRetroFooter->fillRect(0, NL::Footer::LINE, NL::WIDTH, NL::Footer::HEIGHT - 2 * NL::Footer::LINE, backgroundColor);

    const int availableWidth = NL::WIDTH - reservedPixels;
    const int centerY = NL::Footer::HEIGHT / 2;

    auto calcFooterWidth = [&](const char *snd, const char *ttl, const char *brt) -> int
    {
        int width = 0;
        bool hasPrevious = false;

        const int separatorWidth = m_sprRetroFooter->textWidth(separator);

        if (snd[0] != '\0')
        {
            width += m_sprRetroFooter->textWidth(snd);
            hasPrevious = true;
        }

        if (ttl[0] != '\0')
        {
            if (hasPrevious)
                width += separatorWidth;

            width += m_sprRetroFooter->textWidth(ttl);
            hasPrevious = true;
        }

        if (brt[0] != '\0')
        {
            if (hasPrevious)
                width += separatorWidth;

            width += m_sprRetroFooter->textWidth(brt);
        }

        return width;
    };

    auto drawFooter = [&](const char *snd, const char *ttl, const char *brt, int startX)
    {
        int cursorX = startX;
        bool hasPrevious = false;

        if (snd[0] != '\0')
        {
            m_sprRetroFooter->setTextColor(senderColor, backgroundColor);
            m_sprRetroFooter->drawString(snd, cursorX, centerY);
            cursorX += m_sprRetroFooter->textWidth(snd);
            hasPrevious = true;
        }

        if (ttl[0] != '\0')
        {
            if (hasPrevious)
            {
                m_sprRetroFooter->setTextColor(separatorColor, backgroundColor);
                m_sprRetroFooter->drawString(separator, cursorX, centerY);
                cursorX += m_sprRetroFooter->textWidth(separator);
            }

            m_sprRetroFooter->setTextColor(titleColor, backgroundColor);
            m_sprRetroFooter->drawString(ttl, cursorX, centerY);
            cursorX += m_sprRetroFooter->textWidth(ttl);
            hasPrevious = true;
        }

        if (brt[0] != '\0')
        {
            if (hasPrevious)
            {
                m_sprRetroFooter->setTextColor(separatorColor, backgroundColor);
                m_sprRetroFooter->drawString(separator, cursorX, centerY);
                cursorX += m_sprRetroFooter->textWidth(separator);
            }

            m_sprRetroFooter->setTextColor(bitrateColor, backgroundColor);
            m_sprRetroFooter->drawString(brt, cursorX, centerY);
        }
    };

    for (int fontIndex = 0; fontIndex < fontCount; fontIndex++)
    {
        m_sprRetroFooter->unloadFont();
        m_sprRetroFooter->loadFont(fonts[fontIndex]);

        int totalWidth = calcFooterWidth(sender, title, bitrate);

        if (totalWidth <= availableWidth)
        {
            int startX = (NL::WIDTH - totalWidth) / 2;
            drawFooter(sender, title, bitrate, startX);
            m_sprRetroFooter->unloadFont();
            present(*m_sprRetroFooter, 0, NL::Footer::Y);
            return;
        }
    }

    m_sprRetroFooter->unloadFont();
    m_sprRetroFooter->loadFont(fonts[fontCount - 1]);

    m_truncatedTitle[0] = '\0';

    if (hasTitle && availableWidth > 0)
    {
        int fixedWidth = 0;
        const int separatorWidth = m_sprRetroFooter->textWidth(separator);

        if (hasSender)
        {
            fixedWidth += m_sprRetroFooter->textWidth(sender);
            if (hasTitle)
                fixedWidth += separatorWidth;
        }

        if (hasBitrate)
        {
            fixedWidth += m_sprRetroFooter->textWidth(bitrate);
            if (hasTitle)
                fixedWidth += separatorWidth;
        }

        const int ellipsisWidth = m_sprRetroFooter->textWidth(ellipsis);
        const int availableForTitle = availableWidth - fixedWidth - ellipsisWidth;

        if (availableForTitle > 0)
        {
            stringutils::safeCopy(m_truncatedTitle, title);

            int titleWidth = m_sprRetroFooter->textWidth(m_truncatedTitle);
            if (titleWidth > availableForTitle)
            {
                // Grober Vorschnitt nach Breitenverhältnis. Landet er auf einem UTF-8-Fortsetzungsbyte, wandert er auf den Anfang des angeschnittenen Zeichens zurück
                size_t estimatedLen = (strlen(m_truncatedTitle) * availableForTitle) / titleWidth;
                if (estimatedLen < strlen(m_truncatedTitle))
                {
                    while (estimatedLen > 0 && (m_truncatedTitle[estimatedLen] & 0xC0) == 0x80)
                        --estimatedLen;
                    m_truncatedTitle[estimatedLen] = '\0';
                }

                // Zeichenweise statt byteweise kürzen: Fortsetzungsbytes fallen mit ihrem Startbyte zusammen weg
                while (m_sprRetroFooter->textWidth(m_truncatedTitle) > availableForTitle && m_truncatedTitle[0] != '\0')
                {
                    size_t len = strlen(m_truncatedTitle) - 1;
                    while (len > 0 && (m_truncatedTitle[len] & 0xC0) == 0x80)
                        --len;
                    m_truncatedTitle[len] = '\0';
                }

                if (m_truncatedTitle[0] != '\0' && strlen(m_truncatedTitle) < strlen(title))
                    strncat(m_truncatedTitle, ellipsis, sizeof(m_truncatedTitle) - strlen(m_truncatedTitle) - 1);
            }
        }
    }

    const char *finalTitle = (m_truncatedTitle[0] != '\0') ? m_truncatedTitle : "";
    int finalWidth = calcFooterWidth(sender, finalTitle, bitrate);
    int startX = (NL::WIDTH - finalWidth) / 2;

    drawFooter(sender, finalTitle, bitrate, startX);
    m_sprRetroFooter->unloadFont();
    present(*m_sprRetroFooter, 0, NL::Footer::Y);
}

void RetroRadio::calcMarkerPos(float angleDeg, int center, int radiusOffset, int &outX, int &outY)
{
    float rad = (angleDeg - 90.0f) * PI_F / 180.0f;
    outX = center + cosf(rad) * (center - radiusOffset);
    outY = center + sinf(rad) * (center - radiusOffset);
}

int32_t RetroRadio::calculateMagicEyeValue()
{
    constexpr int32_t VALUE_MIN = 0;                  ///< Minimaler Ausschlag des magischen Auges
    constexpr int32_t VALUE_MAX = 100;                ///< Maximaler Ausschlag des magischen Auges
    constexpr int32_t WIFI_PERCENT_OPEN = 30;         ///< WLAN-Prozentwert bei -85 dBm, Auge vollständig offen
    constexpr int32_t WIFI_PERCENT_CLOSED = 90;       ///< WLAN-Prozentwert bei -55 dBm, Auge vollständig geschlossen
    // Bereiche in Skalenpixeln. Mit dem 1,6-fachen Senderabstand gewachsen, damit der Übergang dieselbe Senderstrecke umfasst (Original 30 und 6)
    constexpr int32_t TRANSITION_RANGE_APPROACH = 48; ///< Übergangsbereich beim Ankommen, der Ausschlag wächst allmählich
    constexpr int32_t TRANSITION_RANGE_LEAVE = 10;    ///< Übergangsbereich beim Wegfahren, der Ausschlag fällt fast sofort

    if (static_cast<int>(lroundf(m_needleX)) < 0)
        return VALUE_MIN;

    const uint8_t target = m_target;
    if (target >= m_stationCounter)
        return VALUE_MIN;

    const int32_t targetPos = m_stationList[target].x;
    const int32_t currentPos = static_cast<int>(lroundf(m_needleX));
    const int32_t mappedValue = map(m_state.wifiSignalPercent, WIFI_PERCENT_OPEN, WIFI_PERCENT_CLOSED, VALUE_MIN, VALUE_MAX);
    const int32_t eyeValue = m_state.wifiConnected ? constrain(mappedValue, VALUE_MIN, VALUE_MAX) : VALUE_MIN;

    const int32_t distanceToTarget = abs(currentPos - targetPos);

    // Ohne vorherige Senderposition gilt jede Entfernung als Annäherung
    int32_t distanceToOldPos = INT32_MAX;
    if (m_lastGoodPosition >= 0)
        distanceToOldPos = abs(currentPos - m_lastGoodPosition);

    const bool approaching = (distanceToTarget <= distanceToOldPos);

    int32_t distance;
    int32_t transitionRange;

    if (approaching)
    {
        distance = distanceToTarget;
        transitionRange = TRANSITION_RANGE_APPROACH;
    }
    else
    {
        distance = distanceToOldPos;
        transitionRange = TRANSITION_RANGE_LEAVE;
    }

    // An der alten oder neuen Senderposition entspricht der Ausschlag der auf -85 bis -55 dBm skalierten WLAN-Stärke
    if (distance == 0)
        return eyeValue;

    if (distance >= transitionRange)
        return VALUE_MIN;

    return (eyeValue * (transitionRange - distance)) / transitionRange;
}

void RetroRadio::drawMagicEye(bool init, int32_t x, int32_t y, int32_t value, int rowFrom, int rowTo)
{
    if (!m_ready)
        return;

    constexpr int SPRITE_WIDTH = NL::MagicEye::SIZE;
    constexpr int SPRITE_HEIGHT = NL::MagicEye::SIZE;
    constexpr float RING_THICKNESS = NL::MagicEye::RING_THICKNESS;
    constexpr float EYE_VALUE_MAX = 100.0f; ///< Voller Ausschlag aus calculateMagicEyeValue()
    const float roundCenter = SPRITE_WIDTH / 2.0f;
    const float roundRadius = roundCenter - 1.0f;

    if (init)
    {
        if (!(m_sprMagicEye && m_sprMagicEye->width() == SPRITE_WIDTH && m_sprMagicEye->height() == SPRITE_HEIGHT))
        {
            LOG_ERROR("Konnte m_sprMagicEye Sprite nicht erstellen!");
            return;
        }

        return;
    }

    const uint16_t panel = farbe(Rolle::GemFlaeche), ring = farbe(Rolle::GemAugenRing);
    if (rowFrom <= 0)
        m_sprMagicEye->clear(panel);

    float level = static_cast<float>(value) / EYE_VALUE_MAX;
    level = constrain(level, 0.0f, 1.0f);

    const uint8_t style = m_eyeStyle;

    if (style == 2)
    {
        // Der Rundbogen steht mittig im Sprite und bringt seinen Rahmen selbst mit.
        m_sprMagicEye->drawEm80MagicEye(SPRITE_WIDTH / 2, SPRITE_HEIGHT / 2,
                                        NL::MagicEye::EM80_HALF_W, NL::MagicEye::EM80_HALF_H,
                                        level, ring, panel, rowFrom, rowTo);
    }
    else if (style == 3)
    {
        // Das Leuchtband steht hochkant und mittig im Sprite und bringt seinen Rahmen ebenfalls selbst mit.
        m_sprMagicEye->drawEm84MagicEye(SPRITE_WIDTH / 2, SPRITE_HEIGHT / 2,
                                        NL::MagicEye::EM84_HALF_W, NL::MagicEye::EM84_HALF_H,
                                        level, ring, panel, rowFrom, rowTo);
    }
    else
    {
        // Der Ring liegt unter dem Schirm und entsteht deshalb ganz im ersten Schritt
        if (rowFrom <= 0)
            m_sprMagicEye->drawSmoothCircle(roundCenter, roundCenter, roundRadius, RING_THICKNESS, ring, panel);

        const float screenRadius = roundRadius - RING_THICKNESS;
        if (style == 1)
            m_sprMagicEye->drawEm34MagicEye(roundCenter, roundCenter, screenRadius, level, panel, rowFrom, rowTo);
        else
            m_sprMagicEye->drawEm11MagicEye(roundCenter, roundCenter, screenRadius, level, panel, rowFrom, rowTo);
    }

    if (rowTo >= SPRITE_HEIGHT)
        present(*m_sprMagicEye, x, y);
}

void RetroRadio::drawKnob(uint8_t vol)
{
    int markerX = 0, markerY = 0;
    vol = constrain(vol, 0, 100);

    constexpr int x = NL::Knob::X;
    constexpr int y = NL::Knob::Y;
    constexpr int wh = NL::Knob::SIZE;
    constexpr int mid = NL::Knob::MID;

    constexpr int markerRadiusOffset = NL::Knob::MARKER_OFFSET;
    constexpr float markerCircleRadius = NL::Knob::MARKER_R;
    constexpr float markerCircleThickness = NL::Knob::MARKER_THICKNESS;

    const uint16_t colorActive = farbe(Rolle::GemReglerAktiv);
    const uint16_t colorTrack = farbe(Rolle::GemRahmen);
    const uint16_t colorMarker = farbe(Rolle::GemAkzent);
    const uint16_t colorText = farbe(Rolle::GemText);
    const uint16_t colorTextBg = farbe(Rolle::GemGrund);
    const uint16_t colorTextVal = farbe(Rolle::GemTextGedaempft);

    {
        // Während des Drehens führt der Touchwinkel die Markerposition. Sonst folgt sie der Lautstärke, eine Lage zwischen zwei Stufen derselben Lautstärke bleibt nach dem Loslassen stehen.
        if (m_pressed != 7 && static_cast<int>(m_totalDegrees / 36) != vol)
            m_totalDegrees = vol * 36.0f;

        // Das Label liegt in der Szene und wird bei jedem Neuzeichnen erneuert, damit ein Sprachwechsel greift
        m_tft->fillRect(x, y - NL::Knob::LABEL_H, wh, NL::Knob::LABEL_H, colorTextBg);
        m_tft->loadFont(ComfortaaBold15);
        m_tft->setTextColor(colorText, colorTextBg);
        m_tft->setTextDatum(TC_DATUM);
        m_tft->drawString(STR(RetroRadioKey::RETRO_KNOB_LABEL), x + wh / 2, y - NL::Knob::LABEL_H);
        m_tft->unloadFont();
        m_tft->setTextDatum(TL_DATUM);

        m_sprRetroKnob->loadFont(LeagueGothicCond51);
        m_sprRetroKnob->setTextColor(colorTextVal);
        m_sprRetroKnob->clear(colorTextBg);

        m_sprRetroKnob->drawSmoothGauge(mid, mid, NL::Knob::RING_R, NL::Knob::RING_THICKNESS, 0, 360, 0, 100, vol, colorActive, colorTrack, true, colorTextBg);

        calcMarkerPos(m_totalDegrees, mid, markerRadiusOffset, markerX, markerY);
        m_sprRetroKnob->drawSmoothCircle(markerX, markerY, markerCircleRadius, markerCircleThickness, colorMarker, colorTextBg);

        if (vol > 0)
        {
            char volStr[5];
            stringutils::safeFormat(volStr, "%d", vol);

            // Der Versatz hängt nur an der Stellenzahl und der Breite der ersten Ziffer, nicht an der Breite der ganzen Zahl. Dadurch steht sie still, statt bei jedem Wechsel um einen halben Bildpunkt zu springen, wie es eine echte Zentrierung täte.
            char firstDigit[2] = {volStr[0], '\0'};
            int32_t volTextW = m_sprRetroKnob->textWidth(firstDigit);
            int xOffset = (vol < 10) ? (volTextW / 2) : (vol < 100) ? volTextW
                                                                    : (2 * volTextW);
            m_sprRetroKnob->drawString(volStr, mid - xOffset, mid - NL::Knob::VALUE_Y);
        }
        else
        {
            m_sprRetroKnob->pushImage(mid - NL::Knob::MUTE_ICON / 2, mid - NL::Knob::MUTE_ICON / 2, NL::Knob::MUTE_ICON, NL::Knob::MUTE_ICON,
                                      m_muteIcon);
        }

        present(*m_sprRetroKnob, x, y);
        presentRect(x, y - NL::Knob::LABEL_H, wh, NL::Knob::LABEL_H);

        return;
    }
}

void RetroRadio::drawTracks(int8_t balance, int8_t gainLowPass, int8_t gainBandPass, int8_t gainHighPass)
{
    constexpr int sliderPosX = NL::Tracks::X;
    constexpr int sliderPosY = NL::Tracks::Y;

    constexpr int SLIDER_X[] = {NL::Tracks::SLIDER_X0, NL::Tracks::SLIDER_X0 + NL::Tracks::SLIDER_STEP, NL::Tracks::SLIDER_X0 + 2 * NL::Tracks::SLIDER_STEP, NL::Tracks::SLIDER_X0 + 3 * NL::Tracks::SLIDER_STEP};
    constexpr int TOUCH_WIDTH = NL::Tracks::TOUCH_W;
    constexpr int PIXEL_MIN = NL::Tracks::PIXEL_MIN;
    constexpr int PIXEL_ZERO = NL::Tracks::PIXEL_ZERO;
    constexpr int PIXEL_MAX = NL::Tracks::PIXEL_MAX;

    // Rückgabe int, weil die Pixelwerte den int8_t-Bereich überschreiten
    auto mapDbToPixel = [](int8_t db) -> int
    {
        return (db <= 0)
                   ? map(db, -12, 0, PIXEL_MIN, PIXEL_ZERO)
                   : map(db, 0, 12, PIXEL_ZERO, PIXEL_MAX);
    };

    const uint16_t ground = farbe(Rolle::GemGrund), panel = farbe(Rolle::GemFlaeche), border = farbe(Rolle::GemRahmen), zero = farbe(Rolle::GemReglerNull), accent = farbe(Rolle::GemAkzent);
    auto drawSlider = [&](int xPos, const __FlashStringHelper *label, int pixelValue, bool withLabel = true)
    {
        constexpr int cx = NL::Tracks::CENTER_X;
        m_sprRetroTracks->fillGradientRect(xPos + cx - NL::Tracks::GROOVE_W / 2, NL::Tracks::GROOVE_Y, NL::Tracks::GROOVE_W, NL::Tracks::GROOVE_H, panel, border, lgfx::v1::gradient_fill_styles::VLINEAR);

        if (withLabel && label)
            m_sprRetroTracks->drawString(label, (xPos == NL::Tracks::SLIDER_X0) ? 0 : xPos, 0);

        m_sprRetroTracks->drawFastHLine(xPos, PIXEL_ZERO, TOUCH_WIDTH, zero);
        m_sprRetroTracks->drawSmoothCircle(xPos + cx, pixelValue, NL::Tracks::KNOB_R, NL::Tracks::KNOB_THICKNESS, accent, ground);
        m_sprRetroTracks->fillSmoothCircle(xPos + cx, pixelValue, NL::Tracks::KNOB_FILL_R, ground);
    };

    balance = constrain(balance, -16, 16);
    gainLowPass = constrain(gainLowPass, -12, 12);
    gainBandPass = constrain(gainBandPass, -12, 12);
    gainHighPass = constrain(gainHighPass, -12, 12);

    int balancePixel = map(balance, -16, 16, PIXEL_MIN, PIXEL_MAX);
    int lowPixel = mapDbToPixel(gainLowPass);
    int midPixel = mapDbToPixel(gainBandPass);
    int highPixel = mapDbToPixel(gainHighPass);

    {
        m_sprRetroTracks->clear(ground);
        m_sprRetroTracks->loadFont(ComfortaaBold15);
        m_sprRetroTracks->setTextColor(farbe(Rolle::GemText), ground);

        drawSlider(SLIDER_X[0], STR(RetroRadioKey::RETRO_TRACK_BALANCE), balancePixel);
        drawSlider(SLIDER_X[1], STR(RetroRadioKey::RETRO_TRACK_LOW), lowPixel);
        drawSlider(SLIDER_X[2], STR(RetroRadioKey::RETRO_TRACK_MID), midPixel);
        drawSlider(SLIDER_X[3], STR(RetroRadioKey::RETRO_TRACK_HIGH), highPixel);

        present(*m_sprRetroTracks, sliderPosX, sliderPosY);
        m_sprRetroTracks->unloadFont();
    }
}

void RetroRadio::drawAnalogClock(UiCanvas *sprite, int &lastDrawnSecond, int32_t x, int32_t y, int remainingMinutes, int remainingSeconds)
{
    if (!sprite)
        return;

    // Nativ 1,5-fach (Original 30, 6, 2, 1,5 und 4 Pixel)
    constexpr float RADIUS = 45.0f;
    constexpr float HOUR_HAND_RATIO = 0.5f;
    constexpr float MINUTE_HAND_RATIO = 0.7f;
    constexpr float SECOND_HAND_RATIO = 0.9f;
    constexpr float HOUR_MARK_LENGTH = 9.0f;
    constexpr float HAND_WIDTH = 3.0f;
    constexpr float SECOND_HAND_WIDTH = 2.25f;
    constexpr float CENTER_CIRCLE_RADIUS = 6.0f;
    constexpr float MARK_WIDTH = 1.5f;      ///< Original 1
    constexpr float TIMER_RADIUS = 51.0f;   ///< Original 34
    constexpr float TIMER_THICKNESS = 5.25f; ///< Original 3,5
    constexpr float SECOND_TIP_WIDTH = 1.05f; ///< Original 0,7
    constexpr int LABEL_TOP = 8, LABEL_SIDE = 5; ///< Abstand der Ziffern vom Rand, Original 5 und 3
    constexpr int MAX_TIMER_MINUTES = 60;
    constexpr int DEGREES_PER_HOUR = 30;
    constexpr int DEGREES_PER_MINUTE = 6;
    constexpr int BUFFER_SIZE = 4;
    constexpr float DEG2RAD = 0.0174532925f;

    const uint16_t COLOR_BACKGROUND = farbe(Rolle::GemFlaeche);
    const uint16_t COLOR_TIMER = farbe(Rolle::UhrTimerBogen);
    const uint16_t COLOR_TRACK = farbe(Rolle::GemRahmen);
    const uint16_t COLOR_TEXT = farbe(Rolle::UhrTimerZahl);
    const uint16_t COLOR_MARKS = farbe(Rolle::UhrAnalogMarken);
    const uint16_t COLOR_HANDS = farbe(Rolle::UhrAnalogZeiger);
    const uint16_t COLOR_SECOND_HAND = farbe(Rolle::UhrSekunde);

    constexpr int SPRITE_WIDTH = NL::Clock::ANALOG_SIZE;
    constexpr int SPRITE_HEIGHT = NL::Clock::ANALOG_SIZE;

    const float cx = SPRITE_WIDTH / 2.0f;
    const float cy = SPRITE_HEIGHT / 2.0f;

    constexpr uint8_t HOUR_MARKS[] = {1, 2, 4, 5, 7, 8, 10, 11};

    // -1 bedeutet: kein Sleep-Timer aktiv
    if (remainingMinutes < -1 || remainingMinutes > MAX_TIMER_MINUTES)
        remainingMinutes = -1;

    if (remainingSeconds < 0 || remainingSeconds >= 60)
        remainingSeconds = 0;

    const bool timeValid = m_state.timeValid;
    struct tm localTime{};
    localTime.tm_hour = m_state.hour;
    localTime.tm_min = m_state.minute;
    localTime.tm_sec = m_state.second;

    // Nur bei geänderter Sekunde neu zeichnen
    if (lastDrawnSecond == localTime.tm_sec)
        return;

    lastDrawnSecond = localTime.tm_sec;

    const float hLen = RADIUS * HOUR_HAND_RATIO;
    const float mLen = RADIUS * MINUTE_HAND_RATIO;
    const float sLen = RADIUS * SECOND_HAND_RATIO;

    sprite->clear(COLOR_BACKGROUND);

    auto getCoord = [](float cx, float cy, float angleDeg, float length) -> std::pair<float, float>
    {
        const float rad = (angleDeg - 90.0f) * DEG2RAD;
        return {cx + cosf(rad) * length, cy + sinf(rad) * length};
    };

    if (remainingMinutes != -1)
    {
        sprite->drawSmoothGauge(cx, cy,
                                TIMER_RADIUS, TIMER_THICKNESS,
                                0, 360,
                                0, MAX_TIMER_MINUTES, remainingMinutes,
                                COLOR_TIMER,
                                COLOR_TRACK,
                                false,
                                COLOR_BACKGROUND);

        sprite->setTextColor(COLOR_TEXT);

        char remainingMinutesBuffer[BUFFER_SIZE];
        stringutils::safeFormat(remainingMinutesBuffer, BUFFER_SIZE, "%02d", remainingMinutes);
        sprite->setTextDatum(top_right);
        sprite->drawString(remainingMinutesBuffer, SPRITE_WIDTH, 0);

        char remainingSecondsBuffer[BUFFER_SIZE];
        stringutils::safeFormat(remainingSecondsBuffer, BUFFER_SIZE, "%02d", remainingSeconds);
        sprite->setTextDatum(bottom_right);
        sprite->drawString(remainingSecondsBuffer, SPRITE_WIDTH, SPRITE_HEIGHT);
    }

    for (uint8_t hourMark : HOUR_MARKS)
    {
        const float angleDeg = hourMark * DEGREES_PER_HOUR;
        const float angleRad = angleDeg * DEG2RAD;
        const float sinAngle = sinf(angleRad);
        const float cosAngle = cosf(angleRad);

        const float x1 = cx + sinAngle * RADIUS;
        const float y1 = cy - cosAngle * RADIUS;
        const float x2 = cx + sinAngle * (RADIUS - HOUR_MARK_LENGTH);
        const float y2 = cy - cosAngle * (RADIUS - HOUR_MARK_LENGTH);

        sprite->drawWideLine(roundf(x1), roundf(y1), roundf(x2), roundf(y2), MARK_WIDTH, COLOR_MARKS);
    }

    sprite->setTextColor(COLOR_HANDS);
    sprite->setTextDatum(middle_center);
    sprite->drawString("12", cx, cy - RADIUS + LABEL_TOP);
    sprite->drawString("3", cx + RADIUS - LABEL_SIDE, cy);
    sprite->drawString("6", cx, cy + RADIUS - LABEL_SIDE);
    sprite->drawString("9", cx - RADIUS + LABEL_SIDE, cy);

    // Ohne plausible Systemzeit bleibt das Zifferblatt ohne Zeiger, damit sofort sichtbar ist, dass keine Uhrzeit vorliegt. Der Timerring bleibt davon unberührt
    if (timeValid)
    {
        const float hourAngle = (localTime.tm_hour % 12 + localTime.tm_min / 60.0f) * DEGREES_PER_HOUR;
        const float minuteAngle = (localTime.tm_min + localTime.tm_sec / 60.0f) * DEGREES_PER_MINUTE;
        const float secondAngle = localTime.tm_sec * DEGREES_PER_MINUTE;

        auto hourPos = getCoord(cx, cy, hourAngle, hLen);
        sprite->drawWideLine(cx, cy, hourPos.first, hourPos.second, HAND_WIDTH, COLOR_HANDS);

        auto minPos = getCoord(cx, cy, minuteAngle, mLen);
        sprite->drawWideLine(cx, cy, minPos.first, minPos.second, HAND_WIDTH, COLOR_HANDS);

        auto secPos = getCoord(cx, cy, secondAngle, sLen);
        sprite->drawWedgeLine(cx, cy, secPos.first, secPos.second, SECOND_HAND_WIDTH, SECOND_TIP_WIDTH, COLOR_SECOND_HAND);

        sprite->fillSmoothCircle(cx, cy, CENTER_CIRCLE_RADIUS, COLOR_SECOND_HAND);
    }

    present(*sprite, x, y);
}

void RetroRadio::drawDigitalClock(UiCanvas *sprite, int &lastDrawnSecond, int32_t x, int32_t y, int remainingMinutes, int remainingSeconds)
{
    if (!sprite)
        return;

    constexpr int MAX_TIMER_MINUTES = 60;

    const uint16_t COLOR_BACKGROUND = farbe(Rolle::GemGrund);
    const uint16_t COLOR_TIMER = farbe(Rolle::UhrTimerBogen);
    const uint16_t COLOR_TRACK = farbe(Rolle::UhrDigitalSpur);
    const uint16_t COLOR_TEXT = farbe(Rolle::UhrDigitalZiffern);

    // Mit Akku steht dessen Symbol gleich rechts neben dem Sprite, Ring und Restzeit rücken deshalb im Sprite nach links.
    const int SPRITE_WIDTH = sprite->width() - (m_state.battery.present ? NL::Battery::CLOCK_INSET : 0);
    const int SPRITE_HEIGHT = sprite->height();
    // 1,5-fach des Originals: Ring 26 vom rechten Rand, Radius cy-6, Stärke 4,5
    const float cx = SPRITE_WIDTH - 39;
    const float cy = SPRITE_HEIGHT / 2.0f;

    // -1 bedeutet: kein Sleep-Timer aktiv
    if (remainingMinutes < -1 || remainingMinutes > MAX_TIMER_MINUTES)
        remainingMinutes = -1;

    if (remainingSeconds < 0 || remainingSeconds >= 60)
        remainingSeconds = 0;

    const bool timeValid = m_state.timeValid;
    struct tm localTime{};
    localTime.tm_hour = m_state.hour;
    localTime.tm_min = m_state.minute;
    localTime.tm_sec = m_state.second;

    // Nur bei geänderter Sekunde neu zeichnen
    if (lastDrawnSecond == localTime.tm_sec)
        return;

    lastDrawnSecond = localTime.tm_sec;

    // Font7 in Textgröße 1 (das Original zeichnet Textgröße 0,75)
    sprite->setFont(&fonts::Font7);
    sprite->setTextSize(1.0f);
    sprite->setTextDatum(middle_left);

    // "88:88" in COLOR_TRACK zeigt die inaktiven Segmente, auch bei ungültiger Zeit, damit die Uhrform sichtbar bleibt
    sprite->setTextColor(COLOR_TRACK);
    sprite->drawString("88:88", 0, cy);

    if (timeValid)
    {
        // Das Leerzeichen hält die Stelle des Doppelpunkts frei, der darunter blinkend gezeichnet wird
        char timeBuffer[6];
        stringutils::safeFormat(timeBuffer, sizeof(timeBuffer), "%02d %02d", localTime.tm_hour, localTime.tm_min);
        sprite->setTextColor(COLOR_TEXT);
        sprite->drawString(timeBuffer, 0, cy);

        const uint16_t colonColor = (localTime.tm_sec % 2 == 0) ? COLOR_TEXT : COLOR_TRACK;
        sprite->setTextColor(colonColor);
        const int16_t colonX = sprite->textWidth("88");
        sprite->drawString(":", colonX, cy);
    }

    if (remainingMinutes != -1)
    {
        sprite->drawSmoothGauge(cx, cy,
                                cy - 9.0f, 6.75f,
                                0, 360,
                                0, MAX_TIMER_MINUTES, remainingMinutes,
                                COLOR_TIMER,
                                COLOR_TRACK,
                                true,
                                COLOR_BACKGROUND);

        // Schriftmaße einmal berechnen, DejaVu12 ändert sich zur Laufzeit nicht
        static int timer_text_max_width = 0;
        static int timer_font_height = 0;
        sprite->setFont(&fonts::DejaVu12);
        sprite->setTextSize(1.0f);

        if (timer_text_max_width == 0)
        {
            timer_text_max_width = sprite->textWidth("88");
            timer_font_height = sprite->fontHeight();
        }

        // Der Bereich wird vor dem Zeichnen gelöscht, weil die Zahlen ohne Hintergrundfarbe stehen
        sprite->fillRect(SPRITE_WIDTH - timer_text_max_width, 0, timer_text_max_width, timer_font_height, COLOR_BACKGROUND);
        char remainingMinutesBuffer[4];
        stringutils::safeFormat(remainingMinutesBuffer, 4, "%02d", remainingMinutes);
        sprite->setTextDatum(top_right);
        sprite->setTextColor(farbe(Rolle::UhrTimerZahl));
        sprite->drawString(remainingMinutesBuffer, SPRITE_WIDTH, 0);

        sprite->fillRect(SPRITE_WIDTH - timer_text_max_width, SPRITE_HEIGHT - timer_font_height, timer_text_max_width, timer_font_height, COLOR_BACKGROUND);
        char remainingSecondsBuffer[4];
        stringutils::safeFormat(remainingSecondsBuffer, 4, "%02d", remainingSeconds);
        sprite->setTextDatum(bottom_right);
        sprite->drawString(remainingSecondsBuffer, SPRITE_WIDTH, SPRITE_HEIGHT);
    }

    present(*sprite, x, y);
}

const char *RetroRadio::text(RetroRadioKey key) const
{
    return reinterpret_cast<const char *>(STR(key));
}

bool RetroRadio::begin(lgfx::LGFX_Device &display, StationManager &stations, VisualizerView &visualizer)
{
    if (m_ready)
        return false;
    m_display = &display;
    m_stations = &stations;
    m_visualizer = &visualizer;
    if (!visualizer.ready() || !m_scene.prepare(NL::WIDTH, NL::HEIGHT) || !m_scale.prepare(NL::WIDTH, NL::Scale::HEIGHT) || !m_strip.prepare(NL::Scale::NEEDLE_STAGING_W, NL::Scale::HEIGHT) || !m_div.prepare(NL::WIDTH, NL::Scale::DIVIDER_HEIGHT) ||
        !m_footer.prepare(NL::WIDTH, NL::Footer::HEIGHT) || !m_stereo.prepare(NL::Footer::STEREO_W, NL::Footer::STEREO_H) || !m_codec.prepare(NL::Footer::CODEC_W, NL::Footer::CODEC_H) || !m_knob.prepare(NL::Knob::SIZE, NL::Knob::SIZE) || !m_tracks.prepare(NL::Tracks::WIDTH, NL::Tracks::HEIGHT) ||
        !m_eye.prepare(NL::MagicEye::SIZE, NL::MagicEye::SIZE) || !m_analogClock.prepare(NL::Clock::ANALOG_SIZE, NL::Clock::ANALOG_SIZE) || !m_digitalClock.prepare(NL::Clock::DIGITAL_W, NL::Clock::DIGITAL_H) || !m_battery.prepare(NL::Battery::W, NL::Battery::H))
        return false;
    // Ziffern der Analoguhr in DejaVu12, das Original nutzt DejaVu9
    m_analogClock.setFont(&fonts::DejaVu12);
    // Das Stummsymbol trägt den Grund des Drehknopfs eingebrannt
    m_muteIcon = IconShapes::draw(IconId::Mute, farbe(Rolle::GemStummSymbol), farbe(Rolle::GemGrund), NL::Knob::MUTE_ICON_SCALE);
    if (!m_muteIcon)
        return false;
    // Das Skalenlogo trägt die Retro-Fläche als Grund eingebrannt, Themen mit Graustufen zeigen es einmal umgerechnet auf ihrer Fläche.
    if (themaAktiv().merkmale & MerkmalGraustufen)
    {
        constexpr size_t pixels = NL::Scale::LOGO_W * NL::Scale::LOGO_H;
        m_logoGrau = static_cast<uint16_t *>(malloc(pixels * sizeof(uint16_t)));
        if (!m_logoGrau)
            return false;
        const uint16_t tinte = farbe(Rolle::GemTextHell), papier = farbe(Rolle::GemFlaeche), grund = ThemaRetro.farbe[static_cast<size_t>(Rolle::GemFlaeche)];
        for (size_t i = 0; i < pixels; ++i)
            m_logoGrau[i] = bildGrau(logo_135[i], tinte, papier, true, grund);
    }
    // Panelkopie der Skala für die Nadel (1280 x 450 Pixel, rund 1,1 MB im PSRAM). Ohne Speicher zeichnet die Nadel über die Szene.
    if (!m_panelScale)
        m_panelScale = static_cast<uint16_t *>(malloc(static_cast<size_t>(NL::WIDTH) * NL::Scale::HEIGHT * sizeof(uint16_t)));
    if (!m_panelScale)
        LOG_ERROR("Panelkopie der Skala nicht verfügbar, die Nadel zeichnet über die Szene");
    const int positions[] = {NL::Buttons::X0, NL::Buttons::X1, NL::Buttons::X2, NL::Buttons::X3, NL::Buttons::X4};
    // Tastensymbole vektoriell in 48 Pixel (Original 32). Ihre Farben stehen als Hexzahl im Etikett, initButton kopiert die Etiketten
    char star[24], back[24], forward[24], reset[24], home[24];
    snprintf(star, sizeof(star), "^STAR:48:%04X^", farbe(Rolle::RadStern));
    snprintf(back, sizeof(back), "^BCKW:48:%04X^", farbe(Rolle::GemAkzent));
    snprintf(forward, sizeof(forward), "^FORW:48:%04X^", farbe(Rolle::GemAkzent));
    snprintf(reset, sizeof(reset), "^RESET:48:%04X^", farbe(Rolle::GemAkzent));
    snprintf(home, sizeof(home), "^HOME:48:%04X^", farbe(Rolle::GemHausSymbol));
    const char *labels[] = {star, back, forward, reset, home};
    // Bei Symboltasten färbt die Textfarbe nur den Rahmen der gedrückten Taste
    const uint16_t outline = farbe(Rolle::GemRahmen), fill = farbe(Rolle::GemTastenFlaeche), text = farbe(Rolle::GemTextGedaempft);
    for (int i = 0; i < 5; ++i)
    {
        m_buttons[i].initButton(&m_scene, positions[i] + NL::Buttons::W / 2, NL::Buttons::Y + NL::Buttons::H / 2, NL::Buttons::W, NL::Buttons::H, NL::Buttons::RADIUS, outline, fill, text, labels[i], nullptr);
        m_buttons[i].setFont(LeagueGothicCond51);
        m_buttons[i].setBorderWidths(NL::Buttons::BORDER, NL::Buttons::BORDER_PRESSED);
    }
    loadStations();
    m_ready = true;
    return true;
}

void RetroRadio::loadStations()
{
    m_stations->withStations([this](const Station *items, int count)
    {
        m_stationCounter = std::min(count, MAX_STATIONS);
        for (int i = 0; i < m_stationCounter; ++i)
            m_stationList[i] = items[i];
    });
    int y = NL::Scale::STATION_Y0;
    for (int i = 0; i < m_stationCounter; ++i)
    {
        m_stationList[i].x = NL::Scale::STATION_X0 + NL::Scale::STATION_STEP_X * i;
        m_stationList[i].y = y;
        y += NL::Scale::STATION_STEP_Y;
        if (y > NL::Scale::ROW_WRAP_Y)
            y = NL::Scale::ROW_RESTART_Y;
    }
    m_scaleDirty = true;
    m_panelStale = true;
}

void RetroRadio::presentRect(int x, int y, int w, int h)
{
    if (m_bulk)
        return;
    const auto *pixels = static_cast<const uint16_t *>(m_scene.getBuffer());
    Tab5Render::pushRegion(*m_display, pixels + y * NL::WIDTH + x, NL::WIDTH, x, y, w, h);
}

void RetroRadio::present(UiCanvas &sprite, int x, int y)
{
    sprite.pushSprite(&m_scene, x, y);
    presentRect(x, y, sprite.width(), sprite.height());
}

void RetroRadio::setState(const State &state)
{
    State next = state;
    next.title[sizeof(next.title) - 1] = 0;
    next.stationName[sizeof(next.stationName) - 1] = 0;
    if (static_cast<unsigned>(next.language) >= static_cast<unsigned>(LangIndex::COUNT))
        next.language = LangIndex::DE;
    if (m_tuning || m_allowStart || next.streamRequestId != m_expectedStreamId || next.streamStation != m_target)
    {
        next.title[0] = 0;
        next.bitRate = 0;
        next.codec = AudioManager::Codec::None;
    }
    if (m_haveState && next.stationsRevision != m_state.stationsRevision)
    {
        cancelPendingStart();
        m_presets.cancel();
        loadStations();
        invalidate();
    }
    if (next.language != m_state.language)
    {
        m_visualizer->setLanguage(next.language);
        invalidate();
    }
    m_metadataDirty = m_metadataDirty || strcmp(next.title, m_state.title) || strcmp(next.stationName, m_state.stationName) || next.bitRate != m_state.bitRate || next.codec != m_state.codec || next.streamState != m_state.streamState || next.requestFailed != m_state.requestFailed;
    m_knobDirty = m_knobDirty || next.volume != m_state.volume || next.muted != m_state.muted;
    m_tracksDirty = m_tracksDirty || next.balance != m_state.balance || memcmp(next.tone, m_state.tone, sizeof(next.tone));
    m_state = next;
    if (m_state.acknowledgedRequestId == m_requestId && m_state.requestFailed)
    {
        m_allowStart = false;
        m_arrived = false;
    }
    if (m_state.selectedStation >= 0 && m_state.selectedStation < m_stationCounter && (!m_haveState || (!m_allowStart && !m_tuning && m_target != m_state.selectedStation)))
    {
        m_target = m_state.selectedStation;
        m_needleTarget = m_stationList[m_target].x;
        if (!m_haveState)
            m_needleX = m_needleTarget;
        else
        {
            m_tuning = true;
            m_lastMotion = 0;
        }
    }
    m_haveState = true;
}

bool RetroRadio::show()
{
    if (!m_ready || !m_visualizer->setBounds(NL::Visual::X, NL::Visual::Y, NL::Visual::W, NL::Visual::H))
        return false;
    m_touchActive = false;
    m_pressed = -1;
    m_scene.setTouch(0, 0, false);
    m_visualizer->handleTouch(0, 0, false);
    m_startOnShow = true;
    invalidate();
    return true;
}

void RetroRadio::invalidate()
{
    m_dirty = m_scaleDirty = m_metadataDirty = m_toolbarDirty = m_knobDirty = m_tracksDirty = true;
    m_oldNeedle = -1;
    m_eyeLevel = -1;
    m_lastDrawnSecond = -1;
    if (m_visualizer)
        m_visualizer->invalidate();
}

void RetroRadio::drawButtons()
{
    // AA-Ränder benötigen bei jedem Zustand denselben schwarzen Untergrund.
    m_scene.fillRect(NL::Buttons::X0, NL::Buttons::Y, NL::WIDTH - NL::Buttons::X0, NL::Buttons::H, farbe(Rolle::GemGrund));
    for (int i = 0; i < 5; ++i)
        m_buttons[i].drawButton(m_pressed == i);
    presentRect(NL::Buttons::X0, NL::Buttons::Y, NL::WIDTH - NL::Buttons::X0, NL::Buttons::H);
    m_toolbarDirty = false;
}

void RetroRadio::drawControls()
{
    if (m_knobDirty)
        drawKnob(m_state.muted ? 0 : m_state.volume);
    if (m_tracksDirty)
        drawTracks(m_state.balance, m_state.tone[0], m_state.tone[1], m_state.tone[2]);
    m_knobDirty = m_tracksDirty = false;
}

void RetroRadio::drawScene()
{
    m_bulk = true;
    m_scene.fillScreen(farbe(Rolle::GemGrund));
    drawScale();
    m_knobDirty = m_tracksDirty = true;
    drawControls();
    drawButtons();
    m_oldNeedle = -1;
    m_eyeLevel = -1;
    m_lastDrawnSecond = -1;
    drawNeedle();
    drawEye();
    drawClock();
    drawFooterText(true);
    m_metadataDirty = true;
    m_footerShown = false;
    m_stereoShown = -1;
    m_codecShown = -1;
    m_bulk = false;
    m_dirty = false;
    m_scaleDirty = false;
    presentRect(0, 0, NL::WIDTH, NL::HEIGHT);
    m_visualizer->invalidate();
}

void RetroRadio::drawNeedle()
{
    // Lage in Teilschritten von 1/NEEDLE_SUBSTEPS px. Zwischen zwei Pixeln mischen die beiden Randspalten Rot und Skalenhintergrund nach dem Bruchteil, sodass der Schwerpunkt der Nadel in feinen Stufen wandert. Auf ganze Pixel gerundet sprangen die Schritte in den Rampen zwischen 1 und 3 px je Bild. Im Stand liegt die Nadel auf einem ganzen Pixel und ist der volle Balken des Originals.
    const int sub = constrain(static_cast<int>(lroundf(m_needleX * NL::Scale::NEEDLE_SUBSTEPS)), 0, NL::Scale::NEEDLE_MAX_X * NL::Scale::NEEDLE_SUBSTEPS);
    if (m_oldNeedle >= 0 && sub == m_oldNeedleSub)
    {
        if (!m_tuning)
            syncSceneNeedle();
        return;
    }
    const int x = sub / NL::Scale::NEEDLE_SUBSTEPS, frac = sub % NL::Scale::NEEDLE_SUBSTEPS;
    const int left = m_oldNeedle >= 0 ? std::min(x, m_oldNeedle) : x;
    const int right = m_oldNeedle >= 0 ? std::max(x, m_oldNeedle) : x;
    const int width = right - left + NL::Scale::NEEDLE_STRIP;
    uint16_t *framebuffer = !m_bulk && width <= NL::Scale::NEEDLE_STAGING_W && m_panelRotation >= 0 && m_panelRotation == m_display->getRotation() ? Tab5Render::framebuffer(*m_display) : nullptr;
    if (framebuffer)
    {
        // Panelweg: Jede Querformatspalte des Streifens ist im Framebuffer eine zusammenhängende Zeile, und jede Zeile wird genau einmal mit ihrem Endwert geschrieben (Hintergrund aus der Panelkopie, Rot oder gemischter Rand). Jeder Speicherzugriff bleibt sequenziell, und das Panel kann keine Zeile ohne Nadel lesen. Über die Querformatszene lag jede der 450 Zeilen einer Spalte in einer eigenen Cachezeile im PSRAM (6 bis 8 ms je Schritt). Die Szene bleibt während der Fahrt unberührt.
        const bool flipped = m_panelRotation == 1;
        const int firstColumn = flipped ? Tab5Render::PanelWidth - NL::Scale::Y - NL::Scale::HEIGHT : NL::Scale::Y;
        const uint16_t red = farbe(Rolle::RadNadel);
        const int redX = x + NL::Scale::NEEDLE_RED_X;
        const int redFirst = frac ? redX + 1 : redX, redLast = redX + NL::Scale::NEEDLE_RED_W - 1;
        const float coverRight = static_cast<float>(frac) / NL::Scale::NEEDLE_SUBSTEPS;
        for (int column = left; column < left + width; ++column)
        {
            const int row = flipped ? column : Tab5Render::PanelHeight - 1 - column;
            uint16_t *out = framebuffer + row * Tab5Render::PanelWidth + firstColumn;
            const uint16_t *background = m_panelScale + column * NL::Scale::HEIGHT;
            if (column >= redFirst && column <= redLast)
                std::fill_n(out, NL::Scale::HEIGHT, red);
            else if (frac && (column == redX || column == redLast + 1))
            {
                const float cover = column == redX ? 1 - coverRight : coverRight;
                for (int i = 0; i < NL::Scale::HEIGHT; ++i)
                    out[i] = blendColor(red, background[i], cover);
            }
            else
                memcpy(out, background, NL::Scale::HEIGHT * sizeof(uint16_t));
        }
        Tab5Render::flushFramebuffer(*m_display, left, NL::Scale::Y, width, NL::Scale::HEIGHT);
        m_oldNeedle = x;
        m_oldNeedleSub = sub;
        if (!m_tuning)
            syncSceneNeedle();
        return;
    }
    // Szenenweg für den Sammelblock, breite Sprünge und ohne Panelkopie: Alte Stelle, neue Stelle und roter Zeiger werden zuerst vollständig in der Szene zusammengesetzt und dann in einer Übertragung ans Display gegeben. Der DSI-Framebuffer wird laufend ausgelesen. Jede Zwischenübertragung ohne Zeiger wäre als Flackern sichtbar.
    if (m_oldNeedle >= 0)
        restoreSceneStrip(m_oldNeedle);
    putSceneNeedle(x, frac);
    if (width <= NL::Scale::NEEDLE_STAGING_W && !m_bulk)
    {
        // Aus dem schmalen Zwischenpuffer: Der Cacheabgleich der Quelle umfasst so den Streifen statt 450 voller Szenenzeilen (rund 1 ms je Schritt).
        const auto *scene = static_cast<const uint16_t *>(m_scene.getBuffer());
        auto *strip = static_cast<uint16_t *>(m_strip.getBuffer());
        for (int y = 0; y < NL::Scale::HEIGHT; ++y)
            memcpy(strip + y * NL::Scale::NEEDLE_STAGING_W, scene + (y + NL::Scale::Y) * NL::WIDTH + left, width * sizeof(uint16_t));
        Tab5Render::pushRegion(*m_display, strip, NL::Scale::NEEDLE_STAGING_W, left, NL::Scale::Y, width, NL::Scale::HEIGHT);
    }
    else
        presentRect(left, NL::Scale::Y, width, NL::Scale::HEIGHT);
    m_oldNeedle = x;
    m_oldNeedleSub = sub;
}

void RetroRadio::restoreSceneStrip(int at)
{
    // Zeilenweise Blockkopie aus der Skalenkopie, beide Puffer sind gleich breit und gleich kodiert. Einzelne pushImage-Aufrufe je Zeile kosteten rund 5 ms je Schritt.
    const auto *source = static_cast<const uint16_t *>(m_scale.getBuffer());
    auto *target = static_cast<uint16_t *>(m_scene.getBuffer());
    for (int y = 0; y < NL::Scale::HEIGHT; ++y)
        memcpy(target + (y + NL::Scale::Y) * NL::WIDTH + at, source + y * NL::WIDTH + at, NL::Scale::NEEDLE_STRIP * sizeof(uint16_t));
}

void RetroRadio::putSceneNeedle(int x, int frac)
{
    if (m_sceneNeedle >= 0)
        restoreSceneStrip(m_sceneNeedle);
    restoreSceneStrip(x);
    const uint16_t red = farbe(Rolle::RadNadel);
    const int redX = x + NL::Scale::NEEDLE_RED_X;
    if (frac == 0)
        m_scene.fillRect(redX, NL::Scale::Y, NL::Scale::NEEDLE_RED_W, NL::Scale::HEIGHT, red);
    else
    {
        // Randspalten aus der Skalenkopie gemischt, direkt im Speicher.
        m_scene.fillRect(redX + 1, NL::Scale::Y, NL::Scale::NEEDLE_RED_W - 1, NL::Scale::HEIGHT, red);
        const float coverRight = static_cast<float>(frac) / NL::Scale::NEEDLE_SUBSTEPS;
        const int rightX = redX + NL::Scale::NEEDLE_RED_W;
        m_scene.blendColumnFrom(m_scale, redX, 0, redX, NL::Scale::Y, NL::Scale::HEIGHT, red, 1 - coverRight);
        m_scene.blendColumnFrom(m_scale, rightX, 0, rightX, NL::Scale::Y, NL::Scale::HEIGHT, red, coverRight);
    }
    m_sceneNeedle = x;
    m_sceneNeedleSub = x * NL::Scale::NEEDLE_SUBSTEPS + frac;
}

void RetroRadio::syncSceneNeedle()
{
    if (m_oldNeedle >= 0 && m_sceneNeedleSub != m_oldNeedleSub)
        putSceneNeedle(m_oldNeedle, m_oldNeedleSub % NL::Scale::NEEDLE_SUBSTEPS);
}

void RetroRadio::drawEye()
{
    // m_eyeLevel -1 verlangt sofort ein ganzes Bild, etwa nach einem Neuaufbau der Szene oder Bauformwechsel. Ein angefangenes Bild ist dann verworfen.
    const bool immediate = m_eyeLevel < 0;
    if (immediate)
        m_eyeBuilding = -1;
    if (m_eyeBuilding < 0)
    {
        const int value = calculateMagicEyeValue();
        if (value == m_eyeLevel)
            return;
        m_eyeBuilding = value;
        m_eyeRow = 0;
    }
    // Ein Bild entsteht in Schritten von EyeRowsPerStep Zeilen und erscheint erst nach dem letzten. Während der Nadelfahrt bekommt es nur die Restzeit des Displaybildes, gemessen am bisher längsten Schritt, weil ein ganzes Augenbild sonst ein Bild der Nadel verschlucken würde. Ein angefangenes Bild wird mit seinem Wert fertig, erst danach zählt ein neuer Wert.
    while (m_eyeRow < NL::MagicEye::SIZE)
    {
        if (!immediate && !frameTimeLeft(m_eyeStepUs))
        {
            ++m_motion.eyeSkipped;
            return;
        }
        const int rowTo = std::min(m_eyeRow + EyeRowsPerStep, NL::MagicEye::SIZE);
        const uint32_t before = micros();
        drawMagicEye(false, NL::MagicEye::X, NL::MagicEye::Y, m_eyeBuilding, m_eyeRow, rowTo);
        // Im Sammelblock fehlt die Übertragung des letzten Schritts, die Dauer wäre zu kurz
        if (!m_bulk)
            m_eyeStepUs = std::max<uint32_t>(m_eyeStepUs, micros() - before);
        m_eyeRow = rowTo;
    }
    m_eyeLevel = m_eyeBuilding;
    m_eyeBuilding = -1;
    if (m_tuning)
        ++m_motion.eyeDrawn;
}

void RetroRadio::drawClock()
{
    const bool fresh = m_lastDrawnSecond < 0;
    if (fresh)
        m_digitalClock.clear(farbe(Rolle::GemGrund));
    int minutes = -1, seconds = 0;
    if (m_sleep.isTimerActive())
        m_sleep.getRemainingTime(minutes, seconds);
    if (m_visualizer->mode() == VisMode::VIS_GONIO)
        drawAnalogClock(&m_analogClock, m_lastDrawnSecond, NL::Clock::ANALOG_X, NL::Clock::ANALOG_Y, minutes, seconds);
    else
        drawDigitalClock(&m_digitalClock, m_lastDrawnSecond, NL::Clock::DIGITAL_X, NL::Clock::DIGITAL_Y, minutes, seconds);
    drawBattery(fresh);
}

void RetroRadio::drawBattery(bool fresh)
{
    const auto &battery = m_state.battery;
    if (!fresh && battery == m_batteryDrawn)
        return;
    m_batteryDrawn = battery;
    // Ohne Akku bleibt die Zeile unberührt. Kommt oder geht der Akku, baut update() die Fläche über den Weg des Moduswechsels neu auf.
    if (!battery.present)
        return;
    const uint16_t ground = farbe(Rolle::GemGrund);
    m_battery.clear(ground);
    BatteryIcon::draw(m_battery, 0, 0, BatteryIcon::Upright, battery, farbe(Rolle::UhrDigitalZiffern), ground);
    present(m_battery, m_visualizer->mode() == VisMode::VIS_GONIO ? NL::Battery::GONIO_X : NL::Battery::X, NL::Battery::Y);
}

RetroRadio::Input RetroRadio::command(Action action, int station)
{
    return {action, station, m_requestId, m_state.stationsRevision};
}

void RetroRadio::cancelPendingStart()
{
    if (++m_requestId == 0)
        ++m_requestId;
    m_allowStart = false;
    m_arrived = false;
}

RetroRadio::Input RetroRadio::selectStation(int index, uint32_t now)
{
    if (!m_ready || index < 0 || index >= m_stationCounter || !m_stationList[index].url[0])
        return {};
    m_startOnShow = false;
    cancelPendingStart();
    m_expectedStreamId = m_requestId;
    if (!m_tuning)
    {
        m_lastGoodPosition = static_cast<int>(lroundf(m_needleX));
        m_visSilencePending = true;
    }
    m_target = index;
    m_needleTarget = m_stationList[index].x;
    m_lastMotion = now;
    m_motion = {};
    m_tuning = true;
    m_allowStart = true;
    m_arrived = false;
    m_state.title[0] = 0;
    m_state.bitRate = 0;
    m_state.codec = AudioManager::Codec::None;
    stringutils::safeCopy(m_state.stationName, m_stationList[index].name);
    m_metadataDirty = true;
    m_visualizer->resetSignal();
    return command(Action::Select, index);
}

RetroRadio::Input RetroRadio::takeRequest()
{
    if (m_pending.action != Action::None)
    {
        const auto result = m_pending;
        m_pending = {};
        return result;
    }
    if (m_allowStart && m_arrived && m_state.acknowledgedRequestId == m_requestId && !m_state.requestFailed)
    {
        m_allowStart = false;
        m_arrived = false;
        return command(Action::Start, m_target);
    }
    return {};
}

void RetroRadio::advance(uint32_t now)
{
    const bool before = m_motionDrawing;
    m_motionDrawing = false;
    advanceNeedle(now);
    m_motionDrawing = before;
}

void RetroRadio::update(uint32_t now, AudioManager &audio)
{
    if (!m_ready)
        return;
    if (m_tuning && m_motion.leaveUs)
        m_motion.maxOutsideUs = std::max(m_motion.maxOutsideUs, static_cast<uint32_t>(micros() - m_motion.leaveUs));
    const bool wasModal = modal();
    m_sleep.update();
    m_presets.update();
    if (m_message.active() && static_cast<int32_t>(now - m_dialogTime) >= 10000)
        m_message.close(); // vorzeichenbehaftet: ein späterer Kontaktzeitstempel darf nicht als abgelaufen gelten
    if (wasModal && !modal())
        invalidate();
    advanceNeedle(now);
    if (modal())
    {
        audio.consumeAudioData();
        return;
    }
    // Das Symbol folgt dem gemessenen Signal, nicht dem Quellformat. Ein Wechsel gilt erst nach StereoStableMs gleichem Ergebnis und wird wie die übrige Infozeile erst nach der Nadelfahrt gezeichnet.
    if (audio.isAudioDataReady() && audio.isMono() != m_monoCandidate)
    {
        m_monoCandidate = !m_monoCandidate;
        m_monoSince = now;
    }
    if (m_monoCandidate != (m_stereoStable == 1) && now - m_monoSince >= StereoStableMs)
    {
        m_stereoStable = m_monoCandidate ? 1 : 0;
        m_metadataDirty = true;
    }
    if (m_startOnShow && m_target >= 0)
    {
        m_startOnShow = false;
        m_pending = selectStation(m_target, now);
    }
    // Abgerissenen oder abgewiesenen Stream ohne Zutun wieder aufnehmen (audio-lite tat das im Original selbst): über
    // denselben Weg wie eine Senderwahl (stumm, Codec herunterfahren, Start, Freigabe nach frischem PCM). Abstand 2, 4, 8,
    // 16 und dann 30 s, ein laufender Stream mit Bitrate setzt ihn zurück.
    if (m_state.streamState == AudioManager::State::Streaming && m_state.bitRate)
        m_retryDelay = 0;
    const bool failed = m_haveState && m_target >= 0 && !m_tuning && !m_allowStart && m_pending.action == Action::None && (m_state.streamState == AudioManager::State::Error || (m_state.acknowledgedRequestId == m_requestId && m_state.requestFailed));
    if (!failed)
        m_retryAt = 0;
    else if (!m_retryAt)
    {
        m_retryDelay = m_retryDelay ? std::min<uint32_t>(m_retryDelay * 2, 30000) : 2000;
        m_retryAt = now + m_retryDelay;
        if (!m_retryAt)
            m_retryAt = 1;
    }
    else if (static_cast<int32_t>(now - m_retryAt) >= 0)
    {
        m_retryAt = 0;
        LOG_INFO_F("RADIO Wiederanlauf Sender %d nach %lu ms", m_target, static_cast<unsigned long>(m_retryDelay));
        m_pending = selectStation(m_target, now);
    }
    // Kommt oder geht der Akku, wechseln Ring und Restzeit der Digitaluhr ihren Platz und das Symbol erscheint oder verschwindet. Der Weg des Moduswechsels räumt die Fläche und baut alles neu auf.
    if (m_state.battery.present != m_batteryPresent)
    {
        m_batteryPresent = m_state.battery.present;
        m_drawnMode = static_cast<VisMode>(VIS_MODE_COUNT);
    }
    if (m_dirty)
        drawScene();
    if (m_drawnMode != m_visualizer->mode())
    {
        m_scene.fillRect(NL::Visual::X, NL::Visual::Y, NL::Visual::W, NL::Visual::H, farbe(Rolle::GemGrund));
        presentRect(NL::Visual::X, NL::Visual::Y, NL::Visual::W, NL::Visual::H);
        m_drawnMode = m_visualizer->mode();
        m_lastDrawnSecond = -1;
        m_visCostUs = 0;
        drawButtons();
        m_visualizer->invalidate();
    }
    if (m_scaleDirty)
    {
        drawScale();
        m_oldNeedle = -1;
        m_eyeLevel = -1;
        m_scaleDirty = false;
    }
    drawNeedle();
    if (m_knobDirty || m_tracksDirty)
        drawControls();
    if (m_toolbarDirty)
        drawButtons();
    // Die Infozeile wartet auf die Ankunft der Nadel: Ihr Neuzeichnen kostet rund 40 ms und würde die ersten Bilder der Fahrt verschlucken.
    if (m_metadataDirty && !m_tuning)
    {
        char rate[24]{};
        if (m_state.bitRate)
            snprintf(rate, sizeof(rate), "%lu kbit/s", static_cast<unsigned long>(m_state.bitRate / 1000));
        const char *title = m_state.title;
        if (m_state.requestFailed || m_state.streamState == AudioManager::State::Error)
            title = text(RetroRadioKey::RADIO_ERROR);
        // Nur bei geändertem Text zeichnen und erst das fertige Bild übertragen. Sonst ginge bei jeder Zustandsänderung,
        // etwa während der Zeigerfahrt, zuerst die leere Zeile und dann der Text ans Display, und die Zeile blinkte.
        if (!m_footerShown || strcmp(m_footerStation, m_state.stationName) || strcmp(m_footerTitle, title) || strcmp(m_footerRate, rate))
        {
            stringutils::safeCopy(m_footerStation, m_state.stationName);
            stringutils::safeCopy(m_footerTitle, title);
            stringutils::safeCopy(m_footerRate, rate);
            m_bulk = true;
            drawFooterText(true);
            drawFooterText(false, m_state.stationName, title, rate);
            updateCodecIndicator(m_state.codec);
            updateStereoIndicator(m_stereoStable);
            m_bulk = false;
            presentRect(0, NL::Footer::Y, NL::WIDTH, NL::Footer::HEIGHT);
            m_footerShown = true;
            m_stereoShown = m_stereoStable;
            m_codecShown = static_cast<int8_t>(m_state.codec);
        }
        else
        {
            if (m_stereoStable != m_stereoShown)
            {
                updateStereoIndicator(m_stereoStable);
                m_stereoShown = m_stereoStable;
            }
            if (static_cast<int8_t>(m_state.codec) != m_codecShown)
            {
                updateCodecIndicator(m_state.codec);
                m_codecShown = static_cast<int8_t>(m_state.codec);
            }
        }
        m_metadataDirty = false;
    }
    // Das Auge kommt nach Knopf, Reglern, Tasten und Infozeile, die ungeprüft zeichnen, damit es während der Fahrt nur die danach übrige Zeit des Displaybildes nutzt. Sonst verpasst die Nadel mit Augenschritten vor einem Knopfbild (rund 10 ms) ein Displaybild.
    drawEye();
    // Während der Fahrt zeigt der Visualizer Ruhe wie ein Radio zwischen zwei Sendern und wertet den Ton des alten Senders nicht mehr aus. Beginnt die Fahrt aus dem Stand, zeichnet das nächste Bild die Ruhestellung, solange die Nadel noch steht. Danach bekommen Visualizer und Uhr nur die Restzeit des Displaybildes, gemessen an ihrer bisher längsten Dauer, weil ein Visualizerbild von bis zu 19 ms sonst jedes zweite Bild der Nadel verschluckte.
    if (!m_tuning)
        m_visSilencePending = false;
    const bool silence = m_visSilencePending;
    const uint32_t beforeVis = micros();
    if (silence || frameTimeLeft(m_visCostUs))
    {
        m_visualizer->update(now, audio, m_tuning);
        m_visCostUs = std::max<uint32_t>(m_visCostUs, micros() - beforeVis);
        if (silence)
        {
            m_visSilencePending = false;
            // Die Uhr der Fahrt beginnt nach diesem langen Bild neu, damit die Nadel ohne Sprung anfährt.
            m_lastMotion = 0;
        }
    }
    else
        ++m_motion.visSkipped;
    const uint32_t beforeClock = micros();
    if (frameTimeLeft(m_clockCostUs))
    {
        drawClock();
        m_clockCostUs = std::max<uint32_t>(m_clockCostUs, micros() - beforeClock);
    }
    if (m_tuning)
    {
        m_motion.leaveUs = micros();
        m_motion.maxVisUs = std::max(m_motion.maxVisUs, beforeClock - beforeVis);
        m_motion.maxClockUs = std::max(m_motion.maxClockUs, m_motion.leaveUs - beforeClock);
    }
}

bool RetroRadio::frameTimeLeft(uint32_t neededUs) const
{
    const uint32_t period = Tab5Render::framePeriodUs();
    if (!m_tuning || !period)
        return true;
    return static_cast<uint32_t>(micros() - Tab5Render::lastFrameUs()) + neededUs + FrameReserveUs < period;
}

int RetroRadio::hitButton(int x, int y) const
{
    for (int i = 0; i < 5; ++i)
        if (m_buttons[i].contains(x, y))
            return i;
    return -1;
}

void RetroRadio::openDialog(int button, uint32_t now)
{
    // Dialoge übertragen aus der Szene, die Nadel muss dort an ihrer sichtbaren Stelle stehen.
    syncSceneNeedle();
    m_scene.setTouch(0, 0, false);
    m_dialogTime = now;
    if (button == 0)
    {
        m_presets.setBounds(NL::Dialog::PRESETS_X, NL::Dialog::PRESETS_Y, NL::Dialog::PRESETS_W, NL::Dialog::PRESETS_H);
        m_presets.setTitle(text(RetroRadioKey::RETRO_PRESET_DLG_TITLE));
        m_presets.setTimeout(10000);
        m_presets.setCallback([this](int index)
        {
            if (index >= 0)
                m_pending = selectStation(index, millis());
            invalidate();
        });
        m_presets.start(m_stationList, m_stationCounter);
        presentRect(NL::Dialog::PRESETS_X, NL::Dialog::PRESETS_Y, NL::Dialog::PRESETS_W, NL::Dialog::PRESETS_H);
    }
    else if (button == 3)
    {
        const char *buttons[] = {m_state.language == LangIndex::DE ? "weiter" : "continue", m_state.language == LangIndex::DE ? "abbruch" : "cancel"};
        m_message.setBounds(NL::Dialog::MESSAGE_X, NL::Dialog::MESSAGE_Y, NL::Dialog::MESSAGE_W, NL::Dialog::MESSAGE_H);
        m_message.open(text(RetroRadioKey::RETRO_RESET_DLG_TITLE), text(RetroRadioKey::RETRO_RESET_DLG_MESSAGE), "", buttons, 2);
        m_message.draw(m_scene);
        presentRect(NL::Dialog::MESSAGE_X, NL::Dialog::MESSAGE_Y, NL::Dialog::MESSAGE_W, NL::Dialog::MESSAGE_H);
    }
    else if (button == 4)
    {
        // Der Dialog behält nur die Zeiger, das Haus-Etikett für sofortiges Verlassen trägt die Farbe Gem.HausSymbol als Hexzahl und lebt deshalb statisch.
        static char home[24];
        snprintf(home, sizeof(home), "^HOME:48:%04X^", farbe(Rolle::GemHausSymbol));
        static const char *labels[] = {"5", "15", "25", "45", "60", home};
        static const int values[] = {5, 15, 25, 45, 60, 0};
        m_sleep.setBounds(NL::Dialog::SLEEP_X, NL::Dialog::SLEEP_Y, NL::Dialog::SLEEP_W, NL::Dialog::SLEEP_H);
        m_sleep.setTitle(text(RetroRadioKey::RETRO_SLEEP_DLG_TITLE));
        m_sleep.setMessage(text(RetroRadioKey::RETRO_SLEEP_DLG_MESSAGE));
        m_sleep.setCancelLabel(m_state.language == LangIndex::DE ? "abbruch" : "cancel");
        m_sleep.setStopLabel(m_state.language == LangIndex::DE ? "stopp" : "stop");
        m_sleep.setOptions(labels, values, 6);
        m_sleep.setTimeout(10000);
        m_sleep.setCallback([this](int minutes)
        {
            if (minutes == 0)
            {
                cancelPendingStart();
                m_pending = command(Action::Home);
            }
            else if (minutes > 0)
                m_sleep.startSleepCountdown(minutes, [this]()
                {
                    cancelPendingStart();
                    m_pending = command(Action::Sleep);
                });
            invalidate();
        });
        m_sleep.start();
        presentRect(NL::Dialog::SLEEP_X, NL::Dialog::SLEEP_Y, NL::Dialog::SLEEP_W, NL::Dialog::SLEEP_H);
    }
}

RetroRadio::Input RetroRadio::touch(TouchPhase phase, int x, int y, uint32_t now)
{
    if (!m_ready)
        return {};
    // Szene und Display decken sich. Der Visualizer erhält Koordinaten relativ zu seiner Fläche.
    const int vx = x - NL::Visual::X, vy = y - NL::Visual::Y;
    const auto inEye = [](int px, int py)
    {
        return px >= NL::MagicEye::X && px < NL::MagicEye::X + NL::MagicEye::SIZE && py >= NL::MagicEye::Y && py < NL::MagicEye::Y + NL::MagicEye::SIZE;
    };
    const auto inKnob = [](int px, int py)
    {
        return px > NL::Knob::X - NL::Knob::TOUCH_MARGIN && px < NL::Knob::X + NL::Knob::SIZE + NL::Knob::TOUCH_MARGIN && py > NL::Knob::Y - NL::Knob::TOUCH_MARGIN && py < NL::Knob::Y + NL::Knob::SIZE + NL::Knob::TOUCH_MARGIN;
    };
    constexpr int knobX = NL::Knob::X + NL::Knob::MID, knobY = NL::Knob::Y + NL::Knob::MID;
    const bool down = phase == TouchPhase::Press || phase == TouchPhase::Move;
    if (phase == TouchPhase::Cancel)
    {
        m_scene.setTouch(-1, -1, true);
        m_sleep.update();
        m_presets.update();
        m_scene.setTouch(-1, -1, false);
        m_message.cancelTouch();
        m_touchActive = false;
        m_pressed = -1;
        m_visualizer->cancelTouch();
        m_toolbarDirty = true;
        return {};
    }
    if (modal())
    {
        m_dialogTime = now;
        m_scene.setTouch(x, y, down);
        if (m_message.active())
        {
            const int result = m_message.touch(static_cast<SettingsUi::TouchPhase>(phase), x, y);
            if (result >= 0)
            {
                m_message.close();
                invalidate();
                if (result == 0)
                    return command(Action::Reset);
            }
            else
            {
                m_message.draw(m_scene);
                presentRect(NL::Dialog::MESSAGE_X, NL::Dialog::MESSAGE_Y, NL::Dialog::MESSAGE_W, NL::Dialog::MESSAGE_H);
            }
        }
        else
        {
            const bool presets = m_presets.isActive();
            m_sleep.update();
            m_presets.update();
            if (!modal())
                invalidate();
            else if (presets)
                presentRect(NL::Dialog::PRESETS_X, NL::Dialog::PRESETS_Y, NL::Dialog::PRESETS_W, NL::Dialog::PRESETS_H);
            else
                presentRect(NL::Dialog::SLEEP_X, NL::Dialog::SLEEP_Y, NL::Dialog::SLEEP_W, NL::Dialog::SLEEP_H);
        }
        return {};
    }
    RadioPreferences before, after;
    m_visualizer->preferences(before);
    m_visualizer->handleTouch(vx, vy, down);
    m_visualizer->preferences(after);
    if (before.mode != after.mode || memcmp(before.styles, after.styles, 8))
        m_pending = command(Action::VisualizerChanged);
    if (phase == TouchPhase::Press)
    {
        m_touchActive = true;
        m_pressed = hitButton(x, y);
        m_pressTime = now;
        m_eyeDone = false;
        m_toolbarDirty = m_pressed >= 0;
        if (inEye(x, y))
        {
            m_pressed = 6;
            return {};
        }
        if (y > NL::Scale::Y && y < NL::Scale::Y + NL::Scale::HEIGHT && x >= 0 && x < NL::Scale::TOUCH_RIGHT)
            return selectStation(x / NL::Scale::STATION_STEP_X, now);
        if (inKnob(x, y))
        {
            const int dx = x - knobX, dy = y - knobY, d = dx * dx + dy * dy;
            if (d <= NL::Knob::TOUCH_MUTE_R * NL::Knob::TOUCH_MUTE_R)
            {
                m_pressed = 8;
                return command(Action::Mute);
            }
            if (d >= NL::Knob::TOUCH_RING_R * NL::Knob::TOUCH_RING_R)
            {
                m_pressed = 7;
                if (static_cast<int>(m_totalDegrees / 36) != m_state.volume)
                    m_totalDegrees = m_state.volume * 36.0f;
                m_lastAngle = atan2f(dy, dx) * 57.29578f;
            }
        }
    }
    if (down && m_touchActive)
    {
        if (m_pressed == 6)
        {
            if (!inEye(x, y))
                m_pressed = -1;
            else if (!m_eyeDone && now - m_pressTime >= 800)
            {
                m_eyeDone = true;
                setEyeStyle((m_eyeStyle + 1) % 4);
                auto out = command(Action::EyeChanged);
                out.value = m_eyeStyle;
                return out;
            }
        }
        else if (m_pressed == 7 && !m_state.muted && inKnob(x, y))
        {
            const float angle = atan2f(y - knobY, x - knobX) * 57.29578f, delta = fmodf(angle - m_lastAngle + 540, 360) - 180;
            if (fabsf(delta) >= 2)
            {
                m_lastAngle = angle;
                m_totalDegrees = constrain(m_totalDegrees + delta, 0.0f, 36.0f * AudioManager::MaxVolume);
                // Die Markierung folgt dem Finger stufenlos, die Lautstärke ändert sich weiter alle 36 Grad
                m_knobDirty = true;
                const int volume = m_totalDegrees / 36;
                if (volume != m_state.volume)
                {
                    auto out = command(Action::Volume);
                    out.value = volume;
                    return out;
                }
            }
        }
        else if (m_pressed >= 0 && m_pressed < 5 && hitButton(x, y) != m_pressed)
        {
            m_pressed = -1;
            m_toolbarDirty = true;
        }
        const int pixel = y - NL::Tracks::Y;
        if (pixel > NL::Tracks::PIXEL_MAX - 1 && pixel < NL::Tracks::PIXEL_MIN + 1)
        {
            for (int i = 0; i < 4; ++i)
            {
                const int left = NL::Tracks::X + NL::Tracks::SLIDER_X0 + NL::Tracks::SLIDER_STEP * i;
                if (x < left || x > left + NL::Tracks::TOUCH_W)
                    continue;
                auto out = command(i == 0 ? Action::Balance : Action::Tone);
                out.control = i - 1;
                out.value = i == 0 ? map(pixel, NL::Tracks::PIXEL_MIN, NL::Tracks::PIXEL_MAX, -16, 16) : pixel > NL::Tracks::PIXEL_ZERO ? map(pixel, NL::Tracks::PIXEL_MIN, NL::Tracks::PIXEL_ZERO, -12, 0)
                                                                                                                                        : map(pixel, NL::Tracks::PIXEL_ZERO, NL::Tracks::PIXEL_MAX, 0, 12);
                return out;
            }
        }
    }
    if (phase == TouchPhase::Release)
    {
        const int target = m_pressed;
        m_pressed = -1;
        m_touchActive = false;
        m_toolbarDirty = true;
        if (target >= 0 && target < 5 && hitButton(x, y) == target && now - m_pressTime < 2000)
        {
            if (target == 1 || target == 2)
            {
                const int direction = target == 2 ? 1 : -1;
                for (int step = 1; step <= m_stationCounter; ++step)
                {
                    const int index = (m_target + direction * step + m_stationCounter) % m_stationCounter;
                    if (m_stationList[index].url[0])
                        return selectStation(index, now);
                }
            }
            else
                openDialog(target, now);
        }
    }
    return {};
}

void RetroRadio::nextVisualizer(int direction)
{
    if (m_visualizer)
        m_visualizer->nextMode(direction);
}

void RetroRadio::advanceNeedle(uint32_t now)
{
    if (!m_tuning)
        return;
    // Steht ein komplettes Neuzeichnen an, etwa nach einer Favoritenwahl im Dialog, zeichnet drawScene die Nadel mit. Ein Schritt davor ginge direkt ins Display und läge über dem noch sichtbaren Dialog.
    const bool drawing = m_motionDrawing && !modal() && !m_dirty;
    if (drawing && m_motion.endUs)
        m_motion.maxRestUs = std::max(m_motion.maxRestUs, static_cast<uint32_t>(micros() - m_motion.endUs));
    // Sichtbare Fahrt im Takt des Displays: Nach der Fertigmeldung eines Bildes liegt der Schritt im Framebuffer, bevor das Panel die Nadelzeilen des nächsten Bildes liest, und die Uhr der Fahrt ist der Zeitpunkt dieser Meldung. So trägt jedes Bild genau einen gleich langen Schritt. Frei laufend schwankte der Weg je Bild mit der Lage der Übertragung zum Lesestrahl.
    if (drawing && Tab5Render::waitFrame(FrameWaitMs))
    {
        now = Tab5Render::lastFrameMs();
        const uint32_t frame = Tab5Render::frames();
        if (m_motion.lastFrame && frame > m_motion.lastFrame + 1)
            m_motion.missed += frame - m_motion.lastFrame - 1;
        m_motion.lastFrame = frame;
        // Die Uhr der Fahrt beginnt mit dem ersten gezeichneten Bild. Was zwischen Senderwahl und diesem Bild lag (Stummschalten, Stopp), erzeugt so keinen Sprung am Start.
        if (m_motion.steps == 0)
            m_lastMotion = now;
    }
    const float dt = m_lastMotion ? std::min(MaxStepSeconds, (now - m_lastMotion) * .001f) : 0;
    m_lastMotion = now;
    m_motion.maxDtMs = std::max(m_motion.maxDtMs, static_cast<uint32_t>(dt * 1000 + .5f));
    // Fahrt mit begrenzter Höchstgeschwindigkeit, weich angefahren und so gebremst, dass die Nadel genau am Ziel
    // steht. Position und Geschwindigkeit bleiben bei einem neuen Ziel stetig.
    const int steps = std::max(1, static_cast<int>(ceilf(dt / .004f)));
    const float step = dt / steps;
    bool reached = false;
    for (int i = 0; i < steps && !reached; ++i)
    {
        const float distance = m_needleTarget - m_needleX;
        const float wanted = copysignf(std::min(NL::Scale::NEEDLE_SPEED, sqrtf(2 * NL::Scale::NEEDLE_ACCEL * fabsf(distance))), distance);
        const float change = NL::Scale::NEEDLE_ACCEL * step;
        m_needleVelocity += constrain(wanted - m_needleVelocity, -change, change);
        const float next = m_needleX + m_needleVelocity * step;
        reached = (distance > 0) != (m_needleTarget - next > 0) || fabsf(m_needleTarget - next) < .35f;
        m_needleX = reached ? m_needleTarget : constrain(next, 0.0f, static_cast<float>(NL::Scale::NEEDLE_MAX_X));
    }
    if (reached || (fabsf(m_needleTarget - m_needleX) < .35f && fabsf(m_needleVelocity) < 3))
    {
        m_needleX = m_needleTarget;
        m_needleVelocity = 0;
        m_tuning = false;
        m_arrived = true;
        m_metadataDirty = true;
        LOG_PLAIN_F("NADEL Vis=%d Strecke=%d Schritte=%lu Verpasst=%lu MaxDtMs=%lu NadelUs=%lu RestUs=%lu VisUs=%lu VisAus=%lu UhrUs=%lu AussenUs=%lu Auge=%lu AugeAus=%lu AugeUs=%lu", static_cast<int>(m_visualizer->mode()), static_cast<int>(lroundf(m_needleTarget)) - m_lastGoodPosition, static_cast<unsigned long>(m_motion.steps), static_cast<unsigned long>(m_motion.missed), static_cast<unsigned long>(m_motion.maxDtMs), static_cast<unsigned long>(m_motion.maxNeedleUs), static_cast<unsigned long>(m_motion.maxRestUs), static_cast<unsigned long>(m_motion.maxVisUs), static_cast<unsigned long>(m_motion.visSkipped), static_cast<unsigned long>(m_motion.maxClockUs), static_cast<unsigned long>(m_motion.maxOutsideUs), static_cast<unsigned long>(m_motion.eyeDrawn), static_cast<unsigned long>(m_motion.eyeSkipped), static_cast<unsigned long>(m_eyeStepUs));
    }
    if (drawing)
    {
        const uint32_t start = micros();
        drawNeedle();
        m_motion.endUs = micros();
        m_motion.maxNeedleUs = std::max(m_motion.maxNeedleUs, m_motion.endUs - start);
        ++m_motion.steps;
    }
}

void RetroRadio::hide()
{
    cancelPendingStart();
    m_presets.cancel();
    m_sleep.cancel();
    m_message.close();
    m_startOnShow = false;
    m_pending = {};
    m_retryAt = m_retryDelay = 0;
    m_touchActive = false;
    m_pressed = -1;
    m_scene.setTouch(-1, -1, false);
    if (m_visualizer)
        m_visualizer->cancelTouch();
}
