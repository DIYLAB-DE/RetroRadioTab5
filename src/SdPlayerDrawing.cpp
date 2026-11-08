// Originalzeichenfunktionen aus DIYLAB-DE/RetroRadio ea4229a34fb39faaff23619092cf09cafb8c51cf, GPL-3.0 gemäß LICENSE.
#include "SDPlayer.h"
#include "Tab5Render.h"
#include "StringUtils.h"
#include "Thema.h"
#include "IconShapes.h"
#include "images/SdPlayerAssets.h"
#include "fonts/ComfortaaBold15.h"
#include "fonts/ComfortaaBold21.h"
#include "fonts/ComfortaaBold24.h"
#include "fonts/ComfortaaBold27.h"
#include "fonts/LeagueGothicCond51.h"
#include "fonts/OrbitronLight48.h"
#include <esp_heap_caps.h>
#include <algorithm>
#include <cmath>
#include <ctime>
#include <lgfx/utility/lgfx_miniz.h>
constexpr float PI_F = 3.1415927f;
namespace SL = SdPlayerTab5Layout;
namespace NL = RadioTab5Layout;
static_assert(SdAssetCassetteWidth == SL::Cassette::W && SdAssetCassetteHeight == SL::Cassette::H && SdAssetCoverWidth == SL::Cover::W && SdAssetCoverHeight == SL::Cover::H, "SdPlayerAssets.h passt nicht zur Layoutdatei");
extern const __FlashStringHelper *i18n_getRetroRadio(RetroRadioKey, LangIndex);

/** @brief Ersetzt den eingebrannten Grund eines Palettenbilds, soweit er vom Bildrand aus waagerecht oder senkrecht ohne Unterbrechung reicht. Gleichfarbige Stellen im Bild bleiben. @param hi Obere Bytes der Palettenindizes je Pixel. @param lo Untere Bytes. */
static void ersetzeRandgrund(uint16_t *bild, const uint8_t *hi, const uint8_t *lo, int w, int h, unsigned grundIndex, uint16_t ersatz)
{
    const auto grund = [&](int pixel)
    {
        return static_cast<unsigned>(hi[pixel]) * 256 + lo[pixel] == grundIndex;
    };
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w && grund(y * w + x); ++x)
            bild[y * w + x] = ersatz;
        for (int x = w - 1; x >= 0 && grund(y * w + x); --x)
            bild[y * w + x] = ersatz;
    }
    for (int x = 0; x < w; ++x)
    {
        for (int y = 0; y < h && grund(y * w + x); ++y)
            bild[y * w + x] = ersatz;
        for (int y = h - 1; y >= 0 && grund(y * w + x); --y)
            bild[y * w + x] = ersatz;
    }
}

void SdPlayer::truncateWithEllipsis(UiCanvas *spr, const char *in, char *out, int outSize, int maxWidth)
{
    if (!spr || !in || !in[0] || !out || outSize < 4)
    {
        if (out && outSize > 0)
            out[0] = '\0';

        return;
    }

    stringutils::safeCopy(out, outSize, in);

    const int ellW = spr->textWidth("...");
    if (spr->textWidth(out) <= maxWidth)
        return;

    if (ellW > maxWidth)
    {
        // Extremfall: selbst "..." passt nicht -> so viel wie möglich zeigen
        stringutils::safeCopy(out, outSize, ".");
        if (spr->textWidth(".") > maxWidth)
            out[0] = '\0'; // nichts passt -> leer

        return;
    }

    // Schnellabschätzung: wie viele Zeichen passen ungefähr?
    int len = (int)strlen(out);
    int w = spr->textWidth(out);

    if (w > 0)
    {
        int est = (len * (maxWidth - ellW)) / w;
        est = std::max(0, std::min(est, len - 1)); // mindestens ein Zeichen kürzen, notfalls bleibt keines stehen

        // Nie mitten in einer UTF-8-Sequenz terminieren, sonst bleibt ein halbes Zeichen vor dem "..." stehen
        while (est > 0 && ((unsigned char)out[est] & 0xC0) == 0x80)
            est--;

        out[est] = '\0';
        len = est;
    }

    // Feinjustierung: Zeichen für Zeichen kürzen, bis es passt. Fortsetzungsbytes zählen dabei zum Zeichen davor
    while (len > 0 && spr->textWidth(out) > maxWidth - ellW)
    {
        len--;
        while (len > 0 && ((unsigned char)out[len] & 0xC0) == 0x80)
            len--;

        out[len] = '\0';
    }

    // "..." anhängen - safeAppend sorgt selbst für Null-Terminierung
    stringutils::safeAppend(out, outSize, "...");
}

void SdPlayer::cleanupFilename(const char *filename, char *output, size_t maxLen)
{
    if (!filename || !output || maxLen == 0)
    {
        // Nur in einen vorhandenen Puffer terminieren, der Schutzzweig darf nicht selbst über den Null-Zeiger schreiben
        if (output && maxLen > 0)
            output[0] = '\0';

        return;
    }

    const char *ptr = filename;

    // Optionale Präfixe wie "Track", "CD", "Disc", "Disk" überspringen
    const char *prefixes[] = {"Track ", "track ", "CD ", "cd ", "Disc ", "disc ", "Disk ", "disk "};
    for (int i = 0; i < 8; i++)
    {
        size_t prefixLen = strlen(prefixes[i]);
        if (strncmp(ptr, prefixes[i], prefixLen) == 0)
        {
            ptr += prefixLen;
            break;
        }
    }

    const char *start = ptr;

    // Erste Zahlengruppe finden (1-3 Ziffern)
    int digitCount = 0;
    while (*ptr >= '0' && *ptr <= '9' && digitCount < 3)
    {
        ptr++;
        digitCount++;
    }

    // Ohne führende Ziffern den Ausgangstext ohne Dateiendung übernehmen
    if (digitCount == 0)
    {
        stringutils::safeCopy(output, maxLen, filename);

        char *dot = strrchr(output, '.');
        if (dot)
            *dot = '\0';

        return;
    }

    // Trennzeichen zwischen Disc und Track (z.B. "-" in "2-01")
    if (*ptr == '-' || *ptr == '_' || *ptr == '.')
    {
        ptr++;

        // Zweite Zahlengruppe (Track-Nummer nach Disc-Nummer)
        digitCount = 0;
        while (*ptr >= '0' && *ptr <= '9' && digitCount < 3)
        {
            ptr++;
            digitCount++;
        }
    }

    bool foundSeparator = false;

    // Übliche Trennzeichen-Kombinationen:
    // " - ", " ", ".", "_", "-", ". ", "._", etc.
    while (*ptr != '\0')
    {
        if (*ptr == ' ' || *ptr == '-' || *ptr == '.' || *ptr == '_')
        {
            ptr++;
            foundSeparator = true;
        }
        else
        {
            break;
        }
    }

    // Ohne gültiges Trennzeichen den Ausgangstext ohne Dateiendung übernehmen
    if (!foundSeparator || *ptr == '\0')
    {
        stringutils::safeCopy(output, maxLen, filename);

        char *dot = strrchr(output, '.');
        if (dot)
            *dot = '\0';

        return;
    }

    stringutils::safeCopy(output, maxLen, ptr);

    char *dot = strrchr(output, '.');
    if (dot)
        *dot = '\0';

    char *trimmed = output;
    while (*trimmed == ' ')
        trimmed++;

    if (trimmed != output)
        memmove(output, trimmed, strlen(trimmed) + 1);
}

void SdPlayer::drawCassetteLabel(bool force)
{
    if (!m_sprTapeLabel)
        return;

    const char *artist = m_currentArtist;
    if (!artist)
        artist = "";

    const CassetteDesign &design = m_cassetteDesigns[m_cassetteDesignIdx];

    // Vorlagen ohne freien Beschriftungsstreifen tragen keinen Interpreten auf der Kassette
    if (design.labelW <= 0)
        return;

    // Label-Hintergrund: der Ausschnitt des Beschriftungsstreifens aus der Farbtabelle, zeilenweise wegen des Zeilenversatzes der Quelle
    for (int y = 0; y < design.labelH; y++)
        m_sprTapeLabel->pushImage(0, y, design.labelW, 1, design.bitmap + (design.labelY + y) * CASSETTE_W + design.labelX);

    char finalText[96];
    finalText[0] = '\0';

    m_sprTapeLabel->unloadFont();
    m_sprTapeLabel->loadFont(ComfortaaBold21);
    truncateWithEllipsis(m_sprTapeLabel, artist, finalText, sizeof(finalText), design.labelW - SL::Cassette::LABEL_MARGIN);

    if (!force && strcmp(m_lastArtistDrawn, finalText) == 0)
        return;

    // zentriert zeichnen, die Schrift glättet sich gegen den Streifengrund
    m_sprTapeLabel->setTextColor(design.labelText);
    m_sprTapeLabel->setTextDatum(MC_DATUM);
    m_sprTapeLabel->drawString(finalText, design.labelW / 2, design.labelH / 2);

    // Den exakt streifenbreiten Sprite über present in Szene und Display kopieren.
    present(*m_sprTapeLabel, CASSETTE_X + design.labelX, CASSETTE_Y + design.labelY);

    stringutils::safeCopy(m_lastArtistDrawn, finalText);
}

void SdPlayer::drawCassetteBody()
{
    const CassetteDesign &d = m_cassetteDesigns[m_cassetteDesignIdx];
    m_sprCassette->pushImage(0, 0, CASSETTE_W, CASSETTE_H, d.bitmap);
}

void SdPlayer::buildWrapSprite()
{
    constexpr int MID = WRAP_SPR_SIZE / 2;
    const uint16_t hubDark = farbe(Rolle::SDNabeDunkel), rim = farbe(Rolle::SDNabeZahnkranz);

    const CassetteDesign &d = m_cassetteDesigns[m_cassetteDesignIdx];
    // Originalmaße in Pixeln der 285er Vorlage, mit dem Faktor der jeweiligen Vorlage gezeichnet
    const float f = d.scale;

    // SD.Transparenz ist zugleich Grund der Kantenglättung: Die Kreise mischen ihren Rand gegen den Schlüssel, der Saum bleibt nur unsichtbar, solange er so dunkel ist wie die Kassette um die Nabe.
    m_sprWrap->clear(farbe(Rolle::SDTransparenz));
    m_sprWrap->fillSmoothCircle(MID, MID, d.hubCoverR, farbe(Rolle::SDNabeRing));
    m_sprWrap->fillSmoothCircle(MID, MID, 16.0f * f, hubDark);
    m_sprWrap->drawSmoothCircle(MID, MID, 14.5f * f, 2.6f * f, rim, hubDark);

    // 6 Zähne ragen vom Ring nach innen (bei 0°, 60°, 120°, 180°, 240°, 300°)
    for (int i = 0; i < 6; ++i)
    {
        const float a = i * 60.0f * 3.1415927f / 180.0f;
        const float sx = sinf(a);
        const float cy = cosf(a);
        m_sprWrap->drawWideLine((int)roundf(MID + 13.0f * f * sx), (int)roundf(MID - 13.0f * f * cy),
                                (int)roundf(MID + 9.0f * f * sx), (int)roundf(MID - 9.0f * f * cy), 1.8f * f, rim);
    }

    m_sprWrap->fillSmoothCircle(MID, MID, 2.5f * f, farbe(Rolle::SDNabeAchse));
    m_sprWrap->setPivot(MID, MID);
}

void SdPlayer::pushDorn(int hubX, int hubY, float angle)
{
    if (!m_sprDorn || !m_sprWrap || !m_sprCassette)
        return;

    constexpr int S = WRAP_SPR_SIZE;
    constexpr int HALF = S / 2;
    const CassetteDesign &d = m_cassetteDesigns[m_cassetteDesignIdx];
    const int x0 = hubX - HALF;
    const int y0 = hubY - HALF;

    uint16_t *dst = (uint16_t *)m_sprDorn->getBuffer();
    const uint16_t *body = (const uint16_t *)m_sprCassette->getBuffer();
    const uint16_t *win = m_sprWin ? (const uint16_t *)m_sprWin->getBuffer() : nullptr;

    for (int y = 0; y < S; y++)
    {
        const int ty = y0 + y;

        // Grundlage ist der Kassettenkörper, darüber der Fensteranteil (Grund samt Wickeln)
        memcpy(dst + y * S, body + ty * CASSETTE_W + x0, S * sizeof(uint16_t));

        if (win && ty >= d.winY && ty < d.winY + d.winH)
        {
            const int sx = (x0 > d.winX) ? x0 : d.winX;
            const int ex = (x0 + S < d.winX + d.winW) ? (x0 + S) : (d.winX + d.winW);
            if (ex > sx)
                memcpy(dst + y * S + (sx - x0),
                       win + (ty - d.winY) * d.winW + (sx - d.winX),
                       (ex - sx) * sizeof(uint16_t));
        }
    }

    m_sprWrap->pushRotateZoomWithAA(m_sprDorn, (float)HALF, (float)HALF, angle, 1.0f, 1.0f, farbe(Rolle::SDTransparenz));
    present(*m_sprDorn, CASSETTE_X + x0, CASSETTE_Y + y0);
}

void SdPlayer::drawReels(int radiusLeftPx, int radiusRightPx)
{
    if (!m_sprWin || !m_sprWinGround)
        return;

    const CassetteDesign &d = m_cassetteDesigns[m_cassetteDesignIdx];

    // Grund übernehmen: gleiche Größe und Farbtiefe, ein zusammenhängendes memcpy genügt
    memcpy(m_sprWin->getBuffer(), m_sprWinGround->getBuffer(), (size_t)d.winW * d.winH * sizeof(uint16_t));

    // Beide Bandwickel in einheitlicher Grundfarbe mit dunkleren Lagenringen, alle Flächen geglättet
    const int cy = d.hubY - d.winY;
    auto reel = [&](int cx, int radiusPx)
    {
        m_sprWin->fillSmoothCircle(cx, cy, radiusPx, d.tapeEdge);
        m_sprWin->drawSmoothCircle(cx, cy, radiusPx * 0.82f, d.scale, d.tape, d.tapeEdge);
        m_sprWin->drawSmoothCircle(cx, cy, radiusPx * 0.64f, d.scale, d.tape, d.tapeEdge);
    };
    // Naben außerhalb des Fensters (Bullaugen-Designs): die Wickel sitzen einen Originalbildpunkt weiter außen, sonst bleibt am Glasrand eine Lückenlinie. Sichtbare Naben: exakt zentriert, sonst steht der kleine Wickel sichtbar schief um den Dorn.
    const int out = (d.hubXLeft < d.winX) ? static_cast<int>(lroundf(d.scale)) : 0;
    reel(d.hubXLeft - d.winX - out, radiusLeftPx);
    reel(d.hubXRight - d.winX + out, radiusRightPx);

    // Die Dorne vor der Ausgabe in den Fensterpuffer rotieren, damit Wickel und Dorne gemeinsam übertragen werden.
    if (m_sprWrap)
    {
        const uint16_t transparent = farbe(Rolle::SDTransparenz);
        m_sprWrap->pushRotateZoomWithAA(m_sprWin, (float)(d.hubXLeft - d.winX), (float)(d.hubY - d.winY), m_wrapAngleL, 1.0f, 1.0f, transparent);
        m_sprWrap->pushRotateZoomWithAA(m_sprWin, (float)(d.hubXRight - d.winX), (float)(d.hubY - d.winY), m_wrapAngleR, 1.0f, 1.0f, transparent);
    }

    present(*m_sprWin, CASSETTE_X + d.winX, CASSETTE_Y + d.winY);
}

void SdPlayer::drawEmptyPlaylist()
{
    const uint16_t background = farbe(Rolle::GemFlaeche);
    constexpr int ICON_SCALE = SL::Playlist::EMPTY_ICON_SCALE;
    constexpr int ICON_Y = SL::Playlist::EMPTY_ICON_Y;
    constexpr int TITLE_Y = SL::Playlist::EMPTY_TITLE_Y;
    constexpr int HINT1_Y = SL::Playlist::EMPTY_HINT1_Y;
    constexpr int HINT2_Y = SL::Playlist::EMPTY_HINT2_Y;
    constexpr int HINT_MARGIN = SL::Playlist::EMPTY_HINT_MARGIN;

    // Symbol einmal vergrößert zeichnen und für die Laufzeit des Moduls behalten
    if (!m_emptyFolderIcon)
        m_emptyFolderIcon = IconShapes::draw(IconId::Folder, farbe(Rolle::SDOrdnerLeer), background, ICON_SCALE);

    if (m_emptyFolderIcon)
    {
        const int iconW = IconShapes::width(IconId::Folder) * ICON_SCALE;
        const int iconH = IconShapes::height(IconId::Folder) * ICON_SCALE;
        m_sprPlaylist->pushImage((PLAYLIST_W - iconW) / 2, ICON_Y, iconW, iconH, m_emptyFolderIcon);
    }

    m_sprPlaylist->setTextDatum(MC_DATUM);
    m_sprPlaylist->setTextColor(farbe(Rolle::GemText), background);
    m_sprPlaylist->drawString(I18n::sdPlayer(SdPlayerKey::NoTitles, m_language), PLAYLIST_W / 2, TITLE_Y);

    m_sprPlaylist->loadFont(ComfortaaBold21);
    m_sprPlaylist->setTextColor(farbe(Rolle::GemTextSchwach), background);
    char hint[96];
    truncateWithEllipsis(m_sprPlaylist, I18n::sdPlayer(SdPlayerKey::PlaylistHint1, m_language), hint, sizeof(hint), PLAYLIST_W - 2 * HINT_MARGIN);
    m_sprPlaylist->drawString(hint, PLAYLIST_W / 2, HINT1_Y);
    truncateWithEllipsis(m_sprPlaylist, I18n::sdPlayer(SdPlayerKey::PlaylistHint2, m_language), hint, sizeof(hint), PLAYLIST_W - 2 * HINT_MARGIN);
    m_sprPlaylist->drawString(hint, PLAYLIST_W / 2, HINT2_Y);

    // Schrift und Ausrichtung der Listenzeilen wiederherstellen
    m_sprPlaylist->loadFont(ComfortaaBold24);
    m_sprPlaylist->setTextDatum(ML_DATUM);
}

void SdPlayer::updateCassette(bool force)
{
    if (!m_sprWrap || !m_sprCassette || !m_sprWinGround)
        return;

    // Die Naben drehen nur bei laufender, von der Zentrale bestätigter Datei mit bekannter Länge
    const bool playing = (m_playback.state == SdPlaybackStatus::State::Playing) && m_playback.fileConfirmed && (m_playback.durationSeconds > 0);
    const uint32_t nowMs = m_now;

    // Die Wickeldorne werden nur gezeichnet, solange die Wiedergabe läuft oder wenn sich Zustand oder Bandlage geändert haben. Ein unveränderter Stillstand komponiert sie nicht erneut
    const bool stateChanged = (playing != m_lastCassettePlaying);
    m_lastCassettePlaying = playing;

    // Jeder Titel belegt denselben Bandanteil, darin folgt das Band der Spielzeit des Titels. Auch in der Pause aus dem Status gerechnet, sonst springen die Wickel beim Pausieren auf die Anfangsstellung. Am Ende der Playlist ist das Band ganz rechts aufgewickelt
    float p = 0.0f;
    if (m_count)
    {
        float track = 0.0f;
        if (m_playback.durationSeconds > 0)
            track = std::clamp((float)m_playback.positionSeconds / (float)m_playback.durationSeconds, 0.0f, 1.0f);
        const bool finished = m_playback.state == SdPlaybackStatus::State::Ended && m_selected + 1 == m_count;
        p = finished ? 1.0f : std::clamp(((float)m_selected + track) / (float)m_count, 0.0f, 1.0f);
    }

    // Titelwechsel und Änderungen der Playlist stellen die Wickel auch im Stopp und in der Pause nach
    if (!force && !playing && !stateChanged && p == m_lastCassetteProgress)
    {
        // Die Zeitbasis läuft weiter, damit beim Fortsetzen kein Sprung entsteht
        m_lastWrapTime = nowMs;
        return;
    }
    m_lastCassetteProgress = p;

    if (m_lastWrapTime == 0)
        m_lastWrapTime = nowMs;

    float dt = (nowMs - m_lastWrapTime) / 1000.0f; // Sekunden seit letztem Frame
    m_lastWrapTime = nowMs;

    if (dt < 0.f)
        dt = 0.f;

    if (dt > 0.05f)
        dt = 0.05f; // Höchstens 50 ms, damit ein blockierter Task keinen Sprung der Naben verursacht

    const CassetteDesign &design = m_cassetteDesigns[m_cassetteDesignIdx];
    const float rmin2 = (float)design.reelRMin * design.reelRMin;
    const float rmax2 = (float)design.reelRMax * design.reelRMax;
    const float d2 = rmax2 - rmin2;

    float rL = sqrtf(rmin2 + (1.0f - p) * d2); // linker Wickel
    float rR = sqrtf(rmin2 + (p)*d2);          // rechter Wickel

    // Drehgeschwindigkeit ω = ω_ref * (r_ref / r) -> kleinerer Radius = schneller
    const float rRef = 0.5f * (design.reelRMin + design.reelRMax);
    float omegaL = WRAP_DPS_AT_REF * (rRef / (rL > 0.f ? rL : rRef));
    float omegaR = WRAP_DPS_AT_REF * (rRef / (rR > 0.f ? rR : rRef));

    if (playing)
    {
        m_wrapAngleL -= omegaL * dt; // deg/s * s = deg
        m_wrapAngleR -= omegaR * dt;

        // Winkel normalisieren auf -360..360 Grad
        if (m_wrapAngleL < -360.0f)
            m_wrapAngleL += 360.0f;

        if (m_wrapAngleR < -360.0f)
            m_wrapAngleR += 360.0f;
    }

    // Bandwickel nur rendern, wenn sich ein Radius sichtbar geändert hat
    // Die Radien wandern über die Titeldauer nur wenige Bildpunkte, die Neuzeichnung ist deshalb selten
    const int rLpx = (int)lroundf(rL);
    const int rRpx = (int)lroundf(rR);

    if (force || rLpx != m_lastReelRadiusL || rRpx != m_lastReelRadiusR)
    {
        drawReels(rLpx, rRpx);
        m_lastReelRadiusL = rLpx;
        m_lastReelRadiusR = rRpx;
    }

    // Naben rotieren: der gezeichnete Einheitsdorn wird gedreht in den Hintergrund komponiert, Zeit-Drossel deckelt die Last
    const float angleDeltaL = fabsf(m_wrapAngleL - m_lastWrapAngleL);
    const float angleDeltaR = fabsf(m_wrapAngleR - m_lastWrapAngleR);
    const bool wrapDue = (nowMs - m_lastWrapPushMs) >= WRAP_UPDATE_MS;
    const bool needsWrapRedraw = force || stateChanged || (wrapDue && (angleDeltaL >= 2.0f || angleDeltaR >= 2.0f));

    if (needsWrapRedraw)
    {
        m_lastWrapPushMs = nowMs;

        pushDorn(design.hubXLeft, design.hubY, m_wrapAngleL);
        pushDorn(design.hubXRight, design.hubY, m_wrapAngleR);

        m_lastWrapAngleL = m_wrapAngleL;
        m_lastWrapAngleR = m_wrapAngleR;
    }
}

#define STR(key) i18n_getRetroRadio(key, m_language)

void SdPlayer::drawKnob(uint8_t vol)
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
        // Eine Lage zwischen zwei Stufen derselben Lautstärke bleibt nach dem Loslassen stehen
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

void SdPlayer::drawTracks(int8_t balance, int8_t gainLowPass, int8_t gainBandPass, int8_t gainHighPass)
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

void SdPlayer::calcMarkerPos(float angleDeg, int center, int radiusOffset, int &outX, int &outY)
{
    float rad = (angleDeg - 90.0f) * PI_F / 180.0f;
    outX = center + cosf(rad) * (center - radiusOffset);
    outY = center + sinf(rad) * (center - radiusOffset);
}

void SdPlayer::drawAnalogClock(UiCanvas *sprite, int &lastDrawnSecond, int32_t x, int32_t y, int remainingMinutes, int remainingSeconds)
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

    const uint16_t colorBackground = farbe(Rolle::GemFlaeche);
    const uint16_t colorMarks = farbe(Rolle::UhrAnalogMarken);
    const uint16_t colorHands = farbe(Rolle::UhrAnalogZeiger);
    const uint16_t colorSecondHand = farbe(Rolle::UhrSekunde);

    constexpr int SPRITE_WIDTH = NL::Clock::ANALOG_SIZE;
    constexpr int SPRITE_HEIGHT = NL::Clock::ANALOG_SIZE;

    const float cx = SPRITE_WIDTH / 2.0f;
    const float cy = SPRITE_HEIGHT / 2.0f;

    constexpr uint8_t HOUR_MARKS[] = {1, 2, 4, 5, 7, 8, 10, 11};

    if (remainingMinutes < -1 || remainingMinutes > MAX_TIMER_MINUTES)
        remainingMinutes = -1;

    if (remainingSeconds < 0 || remainingSeconds >= 60)
        remainingSeconds = 0;

    const bool timeValid = m_state.timeValid;
    struct tm localTime{};
    localTime.tm_hour = m_state.hour;
    localTime.tm_min = m_state.minute;
    localTime.tm_sec = m_state.second;

    if (lastDrawnSecond == localTime.tm_sec)
        return;

    lastDrawnSecond = localTime.tm_sec;

    const float hLen = RADIUS * HOUR_HAND_RATIO;
    const float mLen = RADIUS * MINUTE_HAND_RATIO;
    const float sLen = RADIUS * SECOND_HAND_RATIO;

    sprite->clear(colorBackground);

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
                                farbe(Rolle::UhrTimerBogen),
                                farbe(Rolle::GemRahmen),
                                false,
                                colorBackground);

        sprite->setTextColor(farbe(Rolle::UhrTimerZahl));

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

        sprite->drawWideLine(roundf(x1), roundf(y1), roundf(x2), roundf(y2), MARK_WIDTH, colorMarks);
    }

    sprite->setTextColor(colorHands);
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
        sprite->drawWideLine(cx, cy, hourPos.first, hourPos.second, HAND_WIDTH, colorHands);

        auto minPos = getCoord(cx, cy, minuteAngle, mLen);
        sprite->drawWideLine(cx, cy, minPos.first, minPos.second, HAND_WIDTH, colorHands);

        auto secPos = getCoord(cx, cy, secondAngle, sLen);
        sprite->drawWedgeLine(cx, cy, secPos.first, secPos.second, SECOND_HAND_WIDTH, SECOND_TIP_WIDTH, colorSecondHand);

        sprite->fillSmoothCircle(cx, cy, CENTER_CIRCLE_RADIUS, colorSecondHand);
    }

    present(*sprite, x, y);
}

void SdPlayer::drawDigitalClock(UiCanvas *sprite, int &lastDrawnSecond, int32_t x, int32_t y, int remainingMinutes, int remainingSeconds)
{
    if (!sprite)
        return;

    constexpr int MAX_TIMER_MINUTES = 60;

    const uint16_t colorBackground = farbe(Rolle::GemGrund);
    const uint16_t colorTrack = farbe(Rolle::UhrDigitalSpur);
    const uint16_t colorText = farbe(Rolle::UhrDigitalZiffern);

    const int SPRITE_WIDTH = sprite->width();
    const int SPRITE_HEIGHT = sprite->height();
    // 1,5-fach des Originals: Ring 26 vom rechten Rand, Radius cy-6, Stärke 4,5
    const float cx = SPRITE_WIDTH - 39;
    const float cy = SPRITE_HEIGHT / 2.0f;

    if (remainingMinutes < -1 || remainingMinutes > MAX_TIMER_MINUTES)
        remainingMinutes = -1;

    if (remainingSeconds < 0 || remainingSeconds >= 60)
        remainingSeconds = 0;

    const bool timeValid = m_state.timeValid;
    struct tm localTime{};
    localTime.tm_hour = m_state.hour;
    localTime.tm_min = m_state.minute;
    localTime.tm_sec = m_state.second;

    if (lastDrawnSecond == localTime.tm_sec)
        return;

    lastDrawnSecond = localTime.tm_sec;

    // Digitaluhr mit blinkendem Doppelpunkt. Font7 unskaliert, das Original skaliert mit Textgröße 0,75
    sprite->setFont(&fonts::Font7);
    sprite->setTextSize(1.0f);
    sprite->setTextDatum(middle_left);

    // Hintergrund "88:88" in Uhr.DigitalSpur - zeigt die inaktiven 7-Segmente. Wird auch bei ungültiger Zeit dargestellt, damit die Uhrform sichtbar bleibt.
    sprite->setTextColor(colorTrack);
    sprite->drawString("88:88", 0, cy);

    // Uhrzeit und Doppelpunkt nur bei plausibler Systemzeit
    if (timeValid)
    {
        char timeBuffer[6];
        stringutils::safeFormat(timeBuffer, sizeof(timeBuffer), "%02d %02d", localTime.tm_hour, localTime.tm_min);
        sprite->setTextColor(colorText);
        sprite->drawString(timeBuffer, 0, cy);

        const uint16_t colonColor = (localTime.tm_sec % 2 == 0) ? colorText : colorTrack;
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
                                farbe(Rolle::UhrTimerBogen),
                                colorTrack,
                                true,
                                colorBackground);

        // Statische Variablen für Font-Maße (werden nur einmal berechnet)
        static int timer_text_max_width = 0;
        static int timer_font_height = 0;
        sprite->setFont(&fonts::DejaVu12);
        sprite->setTextSize(1.0f);

        if (timer_text_max_width == 0)
        {
            timer_text_max_width = sprite->textWidth("88");
            timer_font_height = sprite->fontHeight();
        }

        sprite->fillRect(SPRITE_WIDTH - timer_text_max_width, 0, timer_text_max_width, timer_font_height, colorBackground);
        char remainingMinutesBuffer[4];
        stringutils::safeFormat(remainingMinutesBuffer, 4, "%02d", remainingMinutes);
        sprite->setTextDatum(top_right);
        sprite->setTextColor(farbe(Rolle::UhrTimerZahl));
        sprite->drawString(remainingMinutesBuffer, SPRITE_WIDTH, 0);

        sprite->fillRect(SPRITE_WIDTH - timer_text_max_width, SPRITE_HEIGHT - timer_font_height, timer_text_max_width, timer_font_height, colorBackground);
        char remainingSecondsBuffer[4];
        stringutils::safeFormat(remainingSecondsBuffer, 4, "%02d", remainingSeconds);
        sprite->setTextDatum(bottom_right);
        sprite->drawString(remainingSecondsBuffer, SPRITE_WIDTH, SPRITE_HEIGHT);
    }

    present(*sprite, x, y);
}

void SdPlayer::drawClock()
{
    if (m_lastDrawnSecond < 0)
        m_digitalClock.clear(farbe(Rolle::GemGrund));
    int minutes = -1, seconds = 0;
    if (m_sleep.isTimerActive())
        m_sleep.getRemainingTime(minutes, seconds);
    if (m_visualizer->mode() == VisMode::VIS_GONIO)
        drawAnalogClock(&m_analogClock, m_lastDrawnSecond, NL::Clock::ANALOG_X, NL::Clock::ANALOG_Y, minutes, seconds);
    else
        drawDigitalClock(&m_digitalClock, m_lastDrawnSecond, NL::Clock::DIGITAL_X, NL::Clock::DIGITAL_Y, minutes, seconds);
}

void SdPlayer::drawMetaText(bool init, const char *first, const char *second, uint16_t firstColor, uint16_t secondColor, uint16_t separatorColor, const char *separator, int reservedPixels, uint16_t backgroundColor)
{
    // Wie die Infozeile des Radios: Schriften 27, 24 und 21 (Original 18, 16 und 14), Linien 2 Pixel
    static const uint8_t *fonts[] = {ComfortaaBold27, ComfortaaBold24, ComfortaaBold21};
    static const int fontCount = sizeof(fonts) / sizeof(fonts[0]);
    static const char *ellipsis = "...";
    static const int spriteWidth = SL::WIDTH;
    static const int spriteHeight = SL::Footer::HEIGHT;
    static const int spriteY = SL::Footer::Y;
    static const int line = SL::Footer::LINE;

    if (init)
    {
        m_sprMeta->setTextWrap(false);
        m_sprMeta->setTextDatum(ML_DATUM);
        const uint16_t lineColor = farbe(Rolle::GemRahmen);
        m_sprMeta->clear(backgroundColor);
        m_sprMeta->fillRect(0, 0, spriteWidth, line, lineColor);
        m_sprMeta->fillRect(0, spriteHeight - line, spriteWidth, line, lineColor);
        present(*m_sprMeta, 0, spriteY);

        return;
    }

    if (!first || !second || !separator)
        return;

    const bool hasSender = first[0] != '\0';
    const bool hasTitle = second[0] != '\0';

    if (!hasSender && !hasTitle)
        return;

    m_sprMeta->fillRect(0, line, spriteWidth, spriteHeight - 2 * line, backgroundColor);

    const int availableWidth = spriteWidth - reservedPixels;
    const int centerY = spriteHeight / 2;

    auto calcFooterWidth = [&](const char *snd, const char *ttl) -> int
    {
        int width = 0;
        bool hasPrevious = false;
        const int separatorWidth = m_sprMeta->textWidth(separator);

        if (snd[0] != '\0')
        {
            width += m_sprMeta->textWidth(snd);
            hasPrevious = true;
        }

        if (ttl[0] != '\0')
        {
            if (hasPrevious)
                width += separatorWidth;

            width += m_sprMeta->textWidth(ttl);
        }

        return width;
    };

    auto drawFooter = [&](const char *snd, const char *ttl, int startX)
    {
        int cursorX = startX;
        bool hasPrevious = false;

        if (snd[0] != '\0')
        {
            m_sprMeta->setTextColor(firstColor, backgroundColor);
            m_sprMeta->drawString(snd, cursorX, centerY);
            cursorX += m_sprMeta->textWidth(snd);
            hasPrevious = true;
        }

        if (ttl[0] != '\0')
        {
            if (hasPrevious)
            {
                m_sprMeta->setTextColor(separatorColor, backgroundColor);
                m_sprMeta->drawString(separator, cursorX, centerY);
                cursorX += m_sprMeta->textWidth(separator);
            }

            m_sprMeta->setTextColor(secondColor, backgroundColor);
            m_sprMeta->drawString(ttl, cursorX, centerY);
        }
    };

    for (int fontIndex = 0; fontIndex < fontCount; fontIndex++)
    {
        m_sprMeta->unloadFont();
        m_sprMeta->loadFont(fonts[fontIndex]);

        int totalWidth = calcFooterWidth(first, second);

        if (totalWidth <= availableWidth)
        {
            int startX = (spriteWidth - totalWidth) / 2;
            drawFooter(first, second, startX);
            m_sprMeta->unloadFont();
            present(*m_sprMeta, 0, spriteY);

            return;
        }
    }

    m_sprMeta->unloadFont();
    m_sprMeta->loadFont(fonts[fontCount - 1]);

    const int separatorWidth = m_sprMeta->textWidth(separator);
    const int ellipsisWidth = m_sprMeta->textWidth(ellipsis);

    int availableForText = availableWidth;
    if (hasSender && hasTitle)
        availableForText -= separatorWidth;

    char truncatedArtist[256];
    char truncatedTitle[256];
    truncatedArtist[0] = '\0';
    truncatedTitle[0] = '\0';

    if (hasSender && hasTitle)
    {
        int artistWidth = m_sprMeta->textWidth(first);
        int titleWidth = m_sprMeta->textWidth(second);
        int totalTextWidth = artistWidth + titleWidth;

        if (totalTextWidth > availableForText)
        {
            int minWidth = ellipsisWidth + m_sprMeta->textWidth("AAA"); // Minimum pro Feld

            if (artistWidth + minWidth <= availableForText)
            {
                stringutils::safeCopy(truncatedArtist, 256, first);
                int remainingWidth = availableForText - artistWidth;
                truncateWithEllipsis(m_sprMeta, second, truncatedTitle, 256, remainingWidth);
            }
            else if (titleWidth + minWidth <= availableForText)
            {
                stringutils::safeCopy(truncatedTitle, 256, second);
                int remainingWidth = availableForText - titleWidth;
                truncateWithEllipsis(m_sprMeta, first, truncatedArtist, 256, remainingWidth);
            }
            else
            {
                // Feste Verteilung: 40% Artist, 60% Title
                int maxArtistWidth = (availableForText * 40) / 100;
                int maxTitleWidth = availableForText - maxArtistWidth;

                truncateWithEllipsis(m_sprMeta, first, truncatedArtist, 256, maxArtistWidth);
                truncateWithEllipsis(m_sprMeta, second, truncatedTitle, 256, maxTitleWidth);
            }
        }
        else
        {
            stringutils::safeCopy(truncatedArtist, 256, first);
            stringutils::safeCopy(truncatedTitle, 256, second);
        }
    }
    else if (hasSender)
    {
        truncateWithEllipsis(m_sprMeta, first, truncatedArtist, 256, availableForText);
    }
    else if (hasTitle)
    {
        truncateWithEllipsis(m_sprMeta, second, truncatedTitle, 256, availableForText);
    }

    const char *finalArtist = (hasSender && truncatedArtist[0] != '\0') ? truncatedArtist : "";
    const char *finalTitle = (hasTitle && truncatedTitle[0] != '\0') ? truncatedTitle : "";

    int finalWidth = calcFooterWidth(finalArtist, finalTitle);
    int startX = (spriteWidth - finalWidth) / 2;

    drawFooter(finalArtist, finalTitle, startX);
    m_sprMeta->unloadFont();
    present(*m_sprMeta, 0, spriteY);
}

void SdPlayer::drawCover()
{
    const char *preferredText = nullptr;
    if (m_coverReady)
    {
        present(m_cover, COVER_X, COVER_Y);
        return;
    }
    if (!m_sprCover)
        return;

    const char *m_currentAudioFile = m_playbackId && m_count ? m_tracks[m_selected].file.path : "";
    char baseName[128] = {0};
    if (preferredText && preferredText[0] != '\0')
    {
        stringutils::safeCopy(baseName, preferredText);
    }
    else if (m_currentAudioFile[0])
    {
        const char *p = strrchr(m_currentAudioFile, '/');
        p = p ? p + 1 : m_currentAudioFile;
        cleanupFilename(p, baseName, sizeof(baseName));
    }
    else
    {
        stringutils::safeCopy(baseName, I18n::sdPlayer(SdPlayerKey::NoCover, m_language));
    }

    const uint16_t border = farbe(Rolle::GemRahmen);
    m_sprCover->clear(farbe(Rolle::GemFlaeche));
    m_sprCover->pushImage(0, 0, COVER_W, COVER_H, m_bitmaps[8]);

    m_sprCover->fillRect(0, 0, SL::Cover::LINE, COVER_H, border);
    m_sprCover->fillRect(COVER_W - SL::Cover::LINE, 0, SL::Cover::LINE, COVER_H, border);

    const bool hasTrackText = (m_currentAudioFile[0] != '\0' || (preferredText && preferredText[0] != '\0'));
    if (hasTrackText)
    {
        char initials[4] = {0};
        {
            int n = 0;
            bool inWord = false;
            for (const char *p = baseName; *p && n < 3; ++p)
            {
                if (isalnum((unsigned char)*p) && !inWord)
                {
                    initials[n++] = (char)toupper((unsigned char)*p);
                    inWord = true;
                }
                else if (!isalnum((unsigned char)*p))
                {
                    inWord = false;
                }
            }
            if (n == 0)
            {
                initials[0] = '?';
                initials[1] = '\0';
            }
            else
            {
                initials[n] = '\0';
            }
        }

        // Orbitron Light 48, das Original nutzt die Bitmapschrift Orbitron_Light_32
        m_sprCover->loadFont(OrbitronLight48);
        m_sprCover->setTextDatum(TL_DATUM);
        m_sprCover->setTextColor(farbe(Rolle::SDCoverInitialen));
        m_sprCover->drawString(initials, SL::Cover::INITIALS_XY, SL::Cover::INITIALS_XY);
        m_sprCover->unloadFont();
    }

    // Textzeile unten: mit Titel der Name rechtsbündig, ohne Titel der Platzhalter zentriert mit hellem erstem Wort und Rest in Orange
    m_sprCover->loadFont(ComfortaaBold24);
    if (hasTrackText)
    {
        constexpr int TEXT_MARGIN = SL::Cover::TEXT_MARGIN;
        m_sprCover->setTextColor(farbe(Rolle::SDCoverText));
        m_sprCover->setTextDatum(BR_DATUM);
        char ell[100];
        ell[0] = '\0';
        truncateWithEllipsis(m_sprCover, baseName, ell, sizeof(ell), COVER_W - TEXT_MARGIN);
        m_sprCover->drawString(ell, COVER_W - TEXT_MARGIN, COVER_H - TEXT_MARGIN);
    }
    else
    {
        constexpr int TEXT_MARGIN = SL::Cover::TEXT_MARGIN;
        char firstWord[sizeof(baseName)];
        stringutils::safeCopy(firstWord, baseName);
        const char *rest = "";
        char *space = strchr(firstWord, ' ');
        if (space)
        {
            *space = '\0';
            rest = baseName + (space - firstWord);
        }
        const int firstW = m_sprCover->textWidth(firstWord);
        const int totalW = firstW + m_sprCover->textWidth(rest);
        const int textX = (COVER_W - totalW) / 2;
        m_sprCover->setTextDatum(BL_DATUM);
        m_sprCover->setTextColor(farbe(Rolle::GemText));
        m_sprCover->drawString(firstWord, textX, COVER_H - TEXT_MARGIN);
        m_sprCover->setTextColor(farbe(Rolle::GemTitel));
        m_sprCover->drawString(rest, textX + firstW, COVER_H - TEXT_MARGIN);
    }
    m_sprCover->setTextDatum(TL_DATUM);
    m_sprCover->unloadFont();

    present(*m_sprCover, COVER_X, COVER_Y);
}

void SdPlayer::presentRect(int x, int y, int w, int h)
{
    if (m_bulk)
        return;
    Tab5Render::pushRegion(*m_display, static_cast<const uint16_t *>(m_scene.getBuffer()) + y * SL::WIDTH + x, SL::WIDTH, x, y, w, h);
}

void SdPlayer::present(UiCanvas &sprite, int x, int y)
{
    // Zeilenweise Speicherkopie: Sprite und Szene haben dasselbe RGB565-Format. pushSprite kopiert pixelweise und braucht für die Playlist rund 19 ms je Bild.
    const int w = sprite.width(), h = sprite.height();
    if (x < 0 || y < 0 || x + w > SL::WIDTH || y + h > SL::HEIGHT)
    {
        sprite.pushSprite(&m_scene, x, y);
        presentRect(std::max(x, 0), std::max(y, 0), std::min(w, SL::WIDTH - std::max(x, 0)), std::min(h, SL::HEIGHT - std::max(y, 0)));
        return;
    }
    const auto *src = static_cast<const uint16_t *>(sprite.getBuffer());
    auto *dst = static_cast<uint16_t *>(m_scene.getBuffer());
    for (int row = 0; row < h; ++row)
        memcpy(dst + (y + row) * SL::WIDTH + x, src + row * w, w * sizeof(uint16_t));
    presentRect(x, y, w, h);
}

void SdPlayer::applyCassetteDesign()
{
    m_sprWin = &m_windows[m_cassetteDesignIdx];
    m_sprWinGround = &m_grounds[m_cassetteDesignIdx];
    m_sprTapeLabel = &m_labels[m_cassetteDesignIdx];
    // Körper, Etikett, Wickel und Dorne erst vollständig in der Szene zusammensetzen und dann einmal übertragen. Ein Zwischenbild ohne Wickel flackert.
    const bool bulk = m_bulk;
    m_bulk = true;
    drawCassetteBody();
    buildWrapSprite();
    present(m_cassette, CASSETTE_X, CASSETTE_Y);
    drawCassetteLabel(true);
    updateCassette(true);
    m_bulk = bulk;
    presentRect(CASSETTE_X, CASSETTE_Y, CASSETTE_W, CASSETTE_H);
}

bool SdPlayer::prepareGraphics()
{
    if (!m_scene.prepare(SL::WIDTH, SL::HEIGHT) || !m_cover.prepare(COVER_W, COVER_H) || !m_cassette.prepare(CASSETTE_W, CASSETTE_H) || !m_wrap.prepare(WRAP_SPR_SIZE, WRAP_SPR_SIZE) || !m_dorn.prepare(WRAP_SPR_SIZE, WRAP_SPR_SIZE) ||
        !m_knob.prepare(NL::Knob::SIZE, NL::Knob::SIZE) || !m_sliders.prepare(NL::Tracks::WIDTH, NL::Tracks::HEIGHT) || !m_playlist.prepare(PLAYLIST_W, SL::Playlist::H) || !m_meta.prepare(SL::WIDTH, SL::Footer::HEIGHT) ||
        !m_details.prepare(COVER_W, SL::Cover::DETAILS_H) || !m_progress.prepare(SL::Progress::W, SL::Progress::H) || !m_digitalClock.prepare(NL::Clock::DIGITAL_W, NL::Clock::DIGITAL_H) || !m_analogClock.prepare(NL::Clock::ANALOG_SIZE, NL::Clock::ANALOG_SIZE))
        return false;
    // Ziffern der Analoguhr wie beim Radio: nächstgrößere unskalierte Originalschrift statt DejaVu9
    m_analogClock.setFont(&fonts::DejaVu12);
    // Themen mit Graustufen rechnen die Palette jedes Bilds einmal um: Kassetten als Abbildung zwischen Tinte und Papier, ihre Randecken in der Retro-Fläche werden Gem.Flaeche. Das Platzhalter-Cover ist eine helle Zeichnung auf dem Retro-Grund der Detailzeile und wird umgekehrt.
    const bool grau = themaAktiv().merkmale & MerkmalGraustufen;
    const uint16_t tinte = farbe(Rolle::GemTextHell), papier = farbe(Rolle::GemGrund), flaeche = farbe(Rolle::GemFlaeche), coverGrund = farbe(Rolle::SDDetailGrund);
    const uint16_t retroFlaeche = ThemaRetro.farbe[static_cast<size_t>(Rolle::GemFlaeche)], retroCoverGrund = ThemaRetro.farbe[static_cast<size_t>(Rolle::SDDetailGrund)];
    for (int i = 0; i < 9; ++i)
    {
        const int w = i == 8 ? COVER_W : CASSETTE_W, h = i == 8 ? COVER_H : CASSETTE_H;
        const auto &asset = SdPlayerAssets[i];
        heap_caps_free(m_bitmaps[i]);
        m_bitmaps[i] = nullptr;
        m_bitmaps[i] = static_cast<uint16_t *>(heap_caps_malloc(w * h * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        auto *decoded = static_cast<uint8_t *>(heap_caps_malloc(asset.unpacked, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!m_bitmaps[i] || !decoded)
        {
            heap_caps_free(decoded);
            return false;
        }
        auto *decoder = static_cast<lgfx_tinfl_decompressor *>(heap_caps_malloc(sizeof(lgfx_tinfl_decompressor), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!decoder)
        {
            heap_caps_free(decoded);
            return false;
        }
        lgfx_tinfl_init(decoder);
        size_t input = asset.size, output = asset.unpacked;
        const bool ok = lgfx_tinfl_decompress(decoder, asset.data, &input, decoded, decoded, &output, TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF) == TINFL_STATUS_DONE && output == asset.unpacked;
        heap_caps_free(decoder);
        if (!ok)
        {
            heap_caps_free(decoded);
            return false;
        }
        const auto *hi = decoded + asset.palette * 2, *lo = hi + w * h;
        unsigned grundIndex = asset.palette;
        for (unsigned index = 0; grau && index < asset.palette; ++index)
        {
            const uint16_t wert = decoded[index * 2] | (static_cast<uint16_t>(decoded[index * 2 + 1]) << 8);
            if (i < 8 && wert == retroFlaeche)
                grundIndex = index;
            const uint16_t neu = i < 8 ? bildGrau(wert, tinte, papier) : bildGrau(wert, tinte, coverGrund, true, retroCoverGrund);
            decoded[index * 2] = neu & 0xFF;
            decoded[index * 2 + 1] = neu >> 8;
        }
        for (int pixel = 0; pixel < w * h; ++pixel)
        {
            const unsigned index = static_cast<unsigned>(hi[pixel]) * 256 + lo[pixel];
            if (index >= asset.palette)
            {
                heap_caps_free(decoded);
                return false;
            }
            m_bitmaps[i][pixel] = decoded[index * 2] | (static_cast<uint16_t>(decoded[index * 2 + 1]) << 8);
        }
        if (grundIndex < asset.palette)
            ersetzeRandgrund(m_bitmaps[i], hi, lo, w, h, grundIndex, flaeche);
        heap_caps_free(decoded);
    }
    // Farben aus den Kassettenreihen des aktiven Themas (Etikettschrift, Lagenringe, Wickel). Geometrie und Originalwerte in SdPlayerAssets.h
    static_assert(sizeof(SdCassetteGeometries) / sizeof(SdCassetteGeometries[0]) == CASSETTE_DESIGN_COUNT, "Kassettengeometrie unvollständig");
    static_assert(sizeof(Farbtabellen::kassetteBand) / sizeof(Farbtabellen::kassetteBand[0]) == CASSETTE_DESIGN_COUNT, "Kassettenreihen des Themas unvollständig");
    const Farbtabellen &tabellen = farbtabellen();
    for (int i = 0; i < CASSETTE_DESIGN_COUNT; ++i)
    {
        const auto &g = SdCassetteGeometries[i];
        m_cassetteDesigns[i] = {m_bitmaps[i], tabellen.kassetteEtikettText[i], g.labelX, g.labelY, g.labelW, g.labelH, tabellen.kassetteBand[i], tabellen.kassetteBandKante[i], g.winX, g.winY, g.winW, g.winH, g.hubY, g.hubXLeft, g.hubXRight, g.hubCoverR, g.reelRMin, g.reelRMax, g.scale};
    }
    const auto &designs = m_cassetteDesigns;
    for (int i = 0; i < 8; ++i)
    {
        const auto &d = designs[i];
        if (!m_windows[i].prepare(d.winW, d.winH) || !m_grounds[i].prepare(d.winW, d.winH) || !m_labels[i].prepare(std::max<int>(1, d.labelW), std::max<int>(1, d.labelH)))
            return false;
        for (int y = 0; y < d.winH; ++y)
            m_grounds[i].pushImage(0, y, d.winW, 1, d.bitmap + (d.winY + y) * CASSETTE_W + d.winX);
    }
    free(m_emptyFolderIcon);
    free(m_muteIcon);
    // Beide Symbole tragen ihren Grund eingebrannt: die Playlist-Fläche bzw. den Grund des Drehknopfs.
    m_emptyFolderIcon = IconShapes::draw(IconId::Folder, farbe(Rolle::SDOrdnerLeer), farbe(Rolle::GemFlaeche), SL::Playlist::EMPTY_ICON_SCALE);
    m_muteIcon = IconShapes::draw(IconId::Mute, farbe(Rolle::GemStummSymbol), farbe(Rolle::GemGrund), NL::Knob::MUTE_ICON_SCALE);
    if (!m_emptyFolderIcon || !m_muteIcon)
        return false;
    const Hit hits[] = {Hit::Previous, Hit::Play, Hit::Pause, Hit::Stop, Hit::Next, Hit::Browse, Hit::Clear, Hit::Reset, Hit::Power};
    // Die Symbole werden vektoriell in 48 Pixeln gezeichnet, das Original nutzt 32
    // Stopp trägt die Farbe Gem.Warnung, das Haus Gem.HausSymbol, je als Hexzahl im Etikett. initButton kopiert die Etiketten.
    char stop[24], home[24];
    snprintf(stop, sizeof(stop), "^STOP:48:%04X^", farbe(Rolle::GemWarnung));
    snprintf(home, sizeof(home), "^HOME:48:%04X^", farbe(Rolle::GemHausSymbol));
    const char *labels[] = {"^SKIPB:48^", "^PLAY:48^", "^PAUSE:48^", stop, "^SKIPF:48^", "durchs.", "leeren", "reset", home};
    const uint16_t outline = farbe(Rolle::GemRahmen), fill = farbe(Rolle::GemTastenFlaeche), symbol = farbe(Rolle::GemAkzent), text = farbe(Rolle::GemTextGedaempft);
    for (int i = 0; i < 9; ++i)
    {
        const auto b = bounds(hits[i]);
        m_buttons[i].initButton(&m_scene, b.x + b.w / 2, b.y + b.h / 2, b.w, b.h, SL::Transport::RADIUS, outline, fill, i < 5 ? symbol : text, labels[i], nullptr);
        m_buttons[i].setFont(LeagueGothicCond51);
        m_buttons[i].setBorderWidths(SL::Transport::BORDER, SL::Transport::BORDER_PRESSED);
    }
    return true;
}
