#include "UiCanvas.h"
#include "Gfx.h"
#include "Thema.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "fonts/ComfortaaBold10.h"
#include "fonts/ComfortaaBold14.h"
#include "fonts/ComfortaaBold15.h"
#include "fonts/ComfortaaBold16.h"
#include "fonts/ComfortaaBold18.h"
#include "fonts/ComfortaaBold21.h"
#include "fonts/ComfortaaBold24.h"
#include "fonts/ComfortaaBold27.h"
#include "fonts/ComfortaaBold30.h"
#include "fonts/ComfortaaBold45.h"
#include "fonts/LeagueGothicCond34.h"
#include "fonts/LeagueGothicCond51.h"
#include "fonts/OrbitronLight48.h"

// Originale Zeichenkerne aus DIYLAB-DE/RetroRadio ea4229a34fb39faaff23619092cf09cafb8c51cf, Gfx.cpp. GPL-3.0, siehe referenz/RetroRadio-LICENSE.
/**
 * @brief Zeichnet einen geglätteten Kreisring über den Pixelzeichner drawPixel(x, y, Farbe). r und thickness in Pixeln, thickness wird auf r begrenzt
 */
template <typename DrawPixelFunc>
void drawSmoothCircleCore(int xc, int yc, float r, float thickness, uint16_t color, uint16_t bg, DrawPixelFunc drawPixel)
{
    if (r <= 0.0f || thickness <= 0.0f)
        return;

    if (thickness > r)
        thickness = r;

    const float rOuter = r + 0.5f;
    const float rInner = r - thickness;
    const float rOuter2 = rOuter * rOuter;
    const float rInner2 = rInner * rInner;

    // Pixel-Plot mit Alpha-Schnellpfad: Vollalpha umgeht blendColor
    auto plotPixel = [&](int x, int y, float dy2)
    {
        const float dx = static_cast<float>(x) - xc;
        const float dist2 = dx * dx + dy2;

        if (dist2 > rOuter2 || dist2 < rInner2)
            return;

        const float dist = sqrtf(dist2);
        const float alphaOuter = fminf(1.0f, rOuter - dist);
        const float alphaInner = fminf(1.0f, dist - rInner);
        const float alpha = fmaxf(0.0f, fminf(alphaOuter, alphaInner));

        if (alpha >= 0.999f)
            drawPixel(x, y, color);
        else if (alpha > 0.0f)
            drawPixel(x, y, blendColor(color, bg, alpha));
    };

    const int yTop = static_cast<int>(yc - r - 1);
    const int yBottom = static_cast<int>(yc + r + 1);

    for (int y = yTop; y <= yBottom; ++y)
    {
        const float dy = static_cast<float>(y) - yc;
        const float dy2 = dy * dy;

        if (dy2 > rOuter2)
            continue;

        const float xOuter = sqrtf(rOuter2 - dy2);

        if (dy2 < rInner2)
        {
            // Zeile schneidet das innere Loch: linker und rechter Bogen separat
            const float xInner = sqrtf(rInner2 - dy2);

            const int xL0 = static_cast<int>(xc - xOuter - 1);
            const int xL1 = static_cast<int>(xc - xInner + 1);
            for (int x = xL0; x <= xL1; ++x)
                plotPixel(x, y, dy2);

            const int xR0 = static_cast<int>(xc + xInner - 1);
            const int xR1 = static_cast<int>(xc + xOuter + 1);
            for (int x = xR0; x <= xR1; ++x)
                plotPixel(x, y, dy2);
        }
        else
        {
            // Polkappen-Zeile: ein zusammenhängender Bogen über die volle Breite
            const int xL = static_cast<int>(xc - xOuter - 1);
            const int xR = static_cast<int>(xc + xOuter + 1);
            for (int x = xL; x <= xR; ++x)
                plotPixel(x, y, dy2);
        }
    }
}

/**
 * @brief Näherung des Abstands eines Punkts (dx, dy) zur Ellipsenkontur in Pixeln
 */
static inline float ellipseDistance(float dx, float dy, float rx, float ry)
{
    const float nx = dx / rx;
    const float ny = dy / ry;
    const float n2 = nx * nx + ny * ny;

    // Punkt im Mittelpunkt -> Distanz ist die kürzere Halbachse
    if (n2 < 0.000001f)
        return fminf(rx, ry);

    const float n = sqrtf(n2);
    return fabsf(n - 1.0f) / n * sqrtf(dx * dx + dy * dy);
}

/**
 * @brief Zeichnet eine geglättete Ellipsenkontur über den Pixelzeichner drawPixel(x, y, Farbe)
 */
template <typename DrawPixelFunc>
void drawSmoothEllipseCore(int xc, int yc, float rx, float ry, float thickness, uint16_t color, uint16_t bg, DrawPixelFunc drawPixel)
{
    if (rx <= 0.0f || ry <= 0.0f || thickness <= 0.0f)
        return;

    if (thickness > rx)
        thickness = rx;

    if (thickness > ry)
        thickness = ry;

    const float minR = fminf(rx, ry);
    const bool hasInnerEllipse = (thickness < minR);
    const float rxInner = rx - thickness;
    const float ryInner = ry - thickness;

    // Konservative Span-Grenzen: AA-Band ist immer +/- 1 Pixel breit, mit Padding 2 sicher abgedeckt
    const float rxAaOuter = rx + 2.0f;
    const float ryAaOuter = ry + 2.0f;
    const float ryAaOuter2 = ryAaOuter * ryAaOuter;

    const float rxAaHole = rx - 2.0f;
    const float ryAaHole = ry - 2.0f;
    const float ryAaHole2 = ryAaHole * ryAaHole;
    const bool hasAaHole = (rxAaHole > 0.0f && ryAaHole > 0.0f);

    // Pixel-Plot mit echter Distanzprüfung und Vollalpha-Schnellpfad
    auto plotPixel = [&](int x, int y, float dx, float dy)
    {
        const float outerDist = ellipseDistance(dx, dy, rx, ry);
        if (outerDist > 1.0f)
            return;

        const float alphaOuter = fminf(1.0f, fmaxf(0.0f, 1.0f - outerDist));
        float alphaInner = 1.0f;
        if (hasInnerEllipse)
        {
            const float innerDist = ellipseDistance(dx, dy, rxInner, ryInner);
            if (innerDist < 1.0f)
                alphaInner = fminf(1.0f, fmaxf(0.0f, innerDist));
        }

        const float alpha = alphaOuter * alphaInner;
        if (alpha >= 0.999f)
            drawPixel(x, y, color);
        else if (alpha > 0.0f)
            drawPixel(x, y, blendColor(color, bg, alpha));
    };

    const int yTop = static_cast<int>(yc - ry - 2);
    const int yBottom = static_cast<int>(yc + ry + 2);

    for (int y = yTop; y <= yBottom; ++y)
    {
        const float dy = static_cast<float>(y) - yc;
        const float dy2 = dy * dy;

        if (dy2 > ryAaOuter2)
            continue;

        const float xOuter = rxAaOuter * sqrtf(fmaxf(0.0f, 1.0f - dy2 / ryAaOuter2));

        if (hasAaHole && dy2 < ryAaHole2)
        {
            // Zeile schneidet das innere Loch: nur die zwei AA-Bögen iterieren
            const float xHole = rxAaHole * sqrtf(1.0f - dy2 / ryAaHole2);

            const int xL0 = static_cast<int>(xc - xOuter - 1);
            const int xL1 = static_cast<int>(xc - xHole + 1);
            for (int x = xL0; x <= xL1; ++x)
                plotPixel(x, y, static_cast<float>(x) - xc, dy);

            const int xR0 = static_cast<int>(xc + xHole - 1);
            const int xR1 = static_cast<int>(xc + xOuter + 1);
            for (int x = xR0; x <= xR1; ++x)
                plotPixel(x, y, static_cast<float>(x) - xc, dy);
        }
        else
        {
            // Polkappen-Zeile oder zu kleine Ellipse: durchgehender Span
            const int xL = static_cast<int>(xc - xOuter - 1);
            const int xR = static_cast<int>(xc + xOuter + 1);
            for (int x = xL; x <= xR; ++x)
                plotPixel(x, y, static_cast<float>(x) - xc, dy);
        }
    }
}

struct GaugeArcData
{
    float startX;
    float startY;
    float endX;
    float endY;
    float span;
    bool visible;
    bool fullCircle;
};

/**
 * @brief Normiert einen Winkel auf den Bereich 0 bis 2 PI
 */
static inline float normalizeGaugeAngle(float angle)
{
    constexpr float GAUGE_FULL_TURN = 6.283185307f;

    while (angle < 0.0f)
        angle += GAUGE_FULL_TURN;

    while (angle >= GAUGE_FULL_TURN)
        angle -= GAUGE_FULL_TURN;

    return angle;
}

/**
 * @brief Berechnet die im Uhrzeigersinn laufende Winkelspanne
 */
static inline float gaugeClockwiseSpan(float startRad, float endRad)
{
    constexpr float GAUGE_FULL_TURN = 6.283185307f;

    if (fabsf(endRad - startRad) >= GAUGE_FULL_TURN)
        return GAUGE_FULL_TURN;

    if (startRad <= endRad)
        return endRad - startRad;

    return GAUGE_FULL_TURN - normalizeGaugeAngle(startRad) + normalizeGaugeAngle(endRad);
}

/**
 * @brief Bereitet die Begrenzungsvektoren eines Gauge-Bogens vor
 */
static inline GaugeArcData makeGaugeArc(float startRad, float spanRad)
{
    constexpr float GAUGE_FULL_TURN = 6.283185307f;
    constexpr float FULL_CIRCLE_EPSILON = 0.0001f;

    GaugeArcData arc;
    arc.visible = (spanRad > 0.000001f);
    arc.fullCircle = (spanRad >= GAUGE_FULL_TURN - FULL_CIRCLE_EPSILON);

    if (spanRad > GAUGE_FULL_TURN)
        spanRad = GAUGE_FULL_TURN;

    arc.span = spanRad;

    const float normalizedStart = normalizeGaugeAngle(startRad);
    const float normalizedEnd = normalizeGaugeAngle(startRad + spanRad);

    arc.startX = sinf(normalizedStart);
    arc.startY = -cosf(normalizedStart);
    arc.endX = sinf(normalizedEnd);
    arc.endY = -cosf(normalizedEnd);

    return arc;
}

/**
 * @brief Prüft einen Punkt per Kreuzprodukt gegen einen vorberechneten Gauge-Bogen
 */
static inline bool isGaugeVectorInsideArc(float dx, float dy, const GaugeArcData &arc)
{
    if (!arc.visible)
        return false;

    if (arc.fullCircle)
        return true;

    constexpr float PI_VALUE = 3.141592654f;

    const float crossStartPoint = arc.startX * dy - arc.startY * dx;
    const float crossPointEnd = dx * arc.endY - dy * arc.endX;

    if (arc.span <= PI_VALUE)
        return crossStartPoint >= 0.0f && crossPointEnd >= 0.0f;

    const float crossEndPoint = arc.endX * dy - arc.endY * dx;
    const float crossPointStart = dx * arc.startY - dy * arc.startX;
    return !(crossEndPoint >= 0.0f && crossPointStart >= 0.0f);
}

/**
 * @brief Zeichnet einen geglätteten Instrumentenbogen mit optionaler Spur über den Pixelzeichner drawPixel(x, y, Farbe)
 */
template <typename DrawPixelFunc>
void drawSmoothGaugeCore(int xc, int yc, float r, float thickness, float angleStartDeg, float angleEndDeg, float valueMin, float valueMax, float value, uint16_t color, uint16_t colorTrack, bool trackVisible, uint16_t bg, DrawPixelFunc drawPixel)
{
    if (r <= 0.0f || thickness <= 0.0f || thickness > r || valueMin >= valueMax)
        return;

    const float t = fmaxf(0.0f, fminf(1.0f, (value - valueMin) / (valueMax - valueMin)));

    constexpr float DEG_TO_RAD_FACTOR = 0.017453292f;

    const float angleStartRad = angleStartDeg * DEG_TO_RAD_FACTOR;
    const float angleEndRad = angleEndDeg * DEG_TO_RAD_FACTOR;
    const float angleActualRad = angleStartRad + (angleEndRad - angleStartRad) * t;

    const GaugeArcData valueArc = makeGaugeArc(angleStartRad, gaugeClockwiseSpan(angleStartRad, angleActualRad));
    const GaugeArcData trackArc = makeGaugeArc(angleStartRad, gaugeClockwiseSpan(angleStartRad, angleEndRad));

    const float rOuter = r + 0.5f;
    const float rInner = r - thickness;
    const float rOuter2 = rOuter * rOuter;
    const float rInner2 = rInner * rInner;

    auto plotPixel = [&](int x, int y, float dx, float dy, float dy2)
    {
        const float dist2 = dx * dx + dy2;
        if (dist2 > rOuter2 || dist2 < rInner2)
            return;

        const bool inValueRange = isGaugeVectorInsideArc(dx, dy, valueArc);
        bool inTrack = false;

        if (!inValueRange && trackVisible)
            inTrack = isGaugeVectorInsideArc(dx, dy, trackArc);

        if (!inValueRange && !inTrack)
            return;

        const float dist = sqrtf(dist2);
        const float alphaOuter = fminf(1.0f, rOuter - dist);
        const float alphaInner = fminf(1.0f, dist - rInner);
        const float alpha = fmaxf(0.0f, fminf(alphaOuter, alphaInner));

        const uint16_t pixelColor = inValueRange ? color : colorTrack;
        if (alpha >= 0.999f)
            drawPixel(x, y, pixelColor);
        else if (alpha > 0.0f)
            drawPixel(x, y, blendColor(pixelColor, bg, alpha));
    };

    const int yTop = static_cast<int>(yc - r - 1);
    const int yBottom = static_cast<int>(yc + r + 1);

    for (int y = yTop; y <= yBottom; ++y)
    {
        const float dy = static_cast<float>(y) - yc;
        const float dy2 = dy * dy;

        if (dy2 > rOuter2)
            continue;

        const float xOuter = sqrtf(rOuter2 - dy2);

        if (dy2 < rInner2)
        {
            const float xInner = sqrtf(rInner2 - dy2);

            const int xL0 = static_cast<int>(xc - xOuter - 1);
            const int xL1 = static_cast<int>(xc - xInner + 1);
            for (int x = xL0; x <= xL1; ++x)
                plotPixel(x, y, static_cast<float>(x) - xc, dy, dy2);

            const int xR0 = static_cast<int>(xc + xInner - 1);
            const int xR1 = static_cast<int>(xc + xOuter + 1);
            for (int x = xR0; x <= xR1; ++x)
                plotPixel(x, y, static_cast<float>(x) - xc, dy, dy2);
        }
        else
        {
            const int xL = static_cast<int>(xc - xOuter - 1);
            const int xR = static_cast<int>(xc + xOuter + 1);
            for (int x = xL; x <= xR; ++x)
                plotPixel(x, y, static_cast<float>(x) - xc, dy, dy2);
        }
    }
}

// Vorbild der EM34-Darstellung: EM34-Emulator von N. Waldt, veröffentlicht unter der MIT-Lizenz.

/**
 * @brief Namensraum für die Kennwerte der Abstimmröhren
 *
 * Die beiden runden Bauformen EM11 und EM34 teilen sich Leuchtstoff, Randbogen, Kantenlicht und Metallkappe und unterscheiden sich nur in der Geometrie ihrer Leuchtflächen. Ihre Kennwerte tragen den Namensteil EYE oder den Namen der Röhre. Die EM80 ist ein stehender Rundbogen mit drei Leuchtfächern und bringt mit den Kennwerten EM80 ihre eigene Geometrie und ihren Rahmen mit. Leuchtstofffarben, Restleuchten und Randbogen teilt sie sich mit den runden Bauformen.
 *
 * Farben und Helligkeiten von Leuchtstoff, Randbogen, Kantenlicht und Metallkappe kommen aus der Tabellenrolle Aug.Phosphor des aktiven Themas (AugenKoeffizienten in Thema.h). Gerechnet wird Licht auf dunklem Grund. Mit MerkmalHellerGrund zieht drawMagicEyeCore dieses Licht je Kanal vom Grund ab, der Schirm zeigt dann Tinte auf Papier.
 *
 * Der Namensteil RATIO bezeichnet einen Anteil: bei den runden Bauformen am Schirmradius, bei der EM80 gibt es keinen, ihre Winkel stehen in Grad und ihre Längen in Pixeln. Alle übrigen Längen stehen in Pixeln. Sämtliche weichen Kanten werden über Pixelabstände gebildet und behalten dadurch bei jeder Sprite-Größe dieselbe Breite.
 */
namespace
{
    constexpr int EYE_SUPERSAMPLE = 1;    ///< Abtastungen je Achse und Bildpunkt
    constexpr float EYE_EDGE_AA = 0.8f;   ///< Halbe Breite jeder weichen Kante in Pixeln
    constexpr float EYE_DEG_TO_RAD = 0.01745329f; ///< Umrechnung von Grad in Radiant

    constexpr float EYE_EDGE_BAND_RATIO = 0.07f;  ///< Weicher Auslauf des Leuchtschirms nach außen
    constexpr float EYE_EDGE_BAND_MIN = 1.2f;     ///< Kleinster Auslauf des Leuchtschirms in Pixeln
    constexpr float EYE_RADIAL_BASE = 0.42f;      ///< Grundhelligkeit des Leuchtschirms in der Mitte
    constexpr float EYE_RADIAL_SPAN = 0.58f;      ///< Helligkeitszuwachs bis zum Schirmrand
    constexpr float EYE_RIM_INNER_RATIO = 0.051f; ///< Tiefe der Innenkante des hellen Randbogens unter dem Schirmrand
    constexpr float EYE_RIM_INNER_MIN = 1.6f;     ///< Kleinste Tiefe der Innenkante in Pixeln
    constexpr float EYE_RIM_OUTER_RATIO = 0.008f; ///< Tiefe der Außenkante des hellen Randbogens unter dem Schirmrand
    constexpr float EYE_RIM_OUTER_MIN = 0.4f;     ///< Kleinste Tiefe der Außenkante in Pixeln

    constexpr float EYE_EDGE_PEAK = 0.7f;        ///< Pixelabstand zur Kante mit dem hellsten Kantenlicht
    constexpr float EYE_EDGE_END = 2.0f;         ///< Pixelabstand, an dem das Kantenlicht erlischt
    constexpr float EYE_SECOND_EDGE_PEAK = 0.7f; ///< Pixelabstand mit dem hellsten Kantenlicht an den Nebenkeilen
    constexpr float EYE_SECOND_EDGE_END = 1.8f;  ///< Pixelabstand, an dem das Kantenlicht der Nebenkeile erlischt

    constexpr float EYE_CORNER_RADIUS_RATIO = 0.95f; ///< Abstand der Eckpunkte vom Mittelpunkt als Anteil des Radius
    constexpr float EYE_CORNER_CORE_RATIO = 0.05f;   ///< Radius des voll leuchtenden Eckkerns als Anteil des Radius
    constexpr float EYE_CORNER_CORE_MIN = 0.6f;      ///< Kleinster Radius des Eckkerns in Pixeln
    constexpr float EYE_CORNER_ROUND_RATIO = 0.14f;  ///< Radius, an dem das Eckleuchten erlischt, als Anteil des Radius
    constexpr float EYE_CORNER_ROUND_MIN = 2.0f;     ///< Kleinster Erlöschradius des Eckleuchtens in Pixeln

    constexpr float EYE_CAP_RADIUS_RATIO = 0.45f; ///< Außenradius der Metallkappe als Anteil des Schirmradius
    constexpr float EYE_CAP_EDGE_START = 0.78f;   ///< Relative Position, ab der die Kappe zum Rand hin abdunkelt
    constexpr float EYE_CAP_EDGE_DARKEN = 22.0f;  ///< Stärke der Randabdunklung der Kappe

    constexpr float EM34_AXIS_ANGLE = -0.7853982f;   ///< Drehung der Keilachse in Radiant, ein negativer Wert neigt die Anzeige nach rechts oben
    constexpr float EM34_HALF_ANGLE_OPEN = 1.18f;    ///< Halber Schattenwinkel bei Pegel 0 in Radiant
    constexpr float EM34_HALF_ANGLE_CLOSED = 0.015f; ///< Halber Schattenwinkel bei vollem Pegel in Radiant
    constexpr float EM34_SENSITIVITY_LEFT = 1.18f;   ///< Empfindlichkeit des linken Hauptkeils, er schließt schon vor Vollausschlag
    constexpr float EM34_SENSITIVITY_RIGHT = 0.78f;  ///< Empfindlichkeit des rechten Hauptkeils, er bleibt bis zuletzt einen Spalt offen
    constexpr float EM34_SMALL_LENGTH_RATIO = 0.90f;       ///< Länge der Nebenkeile als Anteil des Radius
    constexpr float EM34_SMALL_HALF_WIDTH_RATIO = 0.1294f; ///< Halbe Breite der Nebenkeile am Schirmmittelpunkt
    constexpr float EM34_SMALL_TIP_RATIO = 0.02f;          ///< Halbe Breite der Nebenkeile an ihrer Spitze
    constexpr float EM34_SMALL_FADE_START = 0.90f;         ///< Relative Keillänge, ab der die Spitze ausläuft

    constexpr float EM11_AXIS_ANGLE = 0.0f;            ///< Drehung der Sektorachsen in Radiant, 0 stellt die Sektoren aufrecht
    constexpr float EM11_UPRIGHT_MIN_DEG = 12.0f;      ///< Öffnungswinkel der senkrechten Leuchtsektoren bei Pegel 0
    constexpr float EM11_UPRIGHT_MAX_DEG = 88.0f;      ///< Öffnungswinkel der senkrechten Leuchtsektoren bei Vollausschlag
    constexpr float EM11_CROSSWISE_MIN_DEG = 6.0f;     ///< Öffnungswinkel der waagerechten Leuchtsektoren bei Pegel 0
    constexpr float EM11_CROSSWISE_MAX_DEG = 70.0f;    ///< Öffnungswinkel der waagerechten Leuchtsektoren bei Vollausschlag
    constexpr float EM11_CROSSWISE_DELAY = 0.15f;      ///< Pegel, ab dem die waagerechten Sektoren merklich öffnen

    // Die EM80 ist keine Scheibe, sondern ein stehender Rundbogen mit senkrechten Seiten, halbkreisförmiger Oberkante und gerader Unterkante. Zwei schmale Schattenstrahlen laufen an der Bodenmitte zusammen und teilen den Schirm in DREI Leuchtfächer: einen mittleren, der mit dem Pegel aufklappt, und zwei äußere in den unteren Ecken, die dabei mitwachsen. Die Winkel sind an der Schattenbild-Tafel gemessen und lassen sich frei nachstellen.
    constexpr float EM80_RING_THICKNESS = 4.0f;       ///< Breite des dunklen Rahmens um den Leuchtschirm in Pixeln
    constexpr float EM80_INNER_MIN_DEG = 5.0f;        ///< Innenkante der Schattenstrahlen bei Pegel 0, zugleich der halbe Winkel des mittleren Fächers
    constexpr float EM80_INNER_MAX_DEG = 38.0f;       ///< Innenkante der Schattenstrahlen bei Vollausschlag
    constexpr float EM80_OUTER_OPEN_DEG = 80.0f;      ///< Außenkante der Schattenstrahlen bei Pegel 0, dahinter beginnen die äußeren Fächer
    constexpr float EM80_OUTER_CLOSED_DEG = 46.0f;    ///< Außenkante der Schattenstrahlen bei Vollausschlag
    constexpr float EM80_WEDGE_AA = 1.2f;             ///< Halbe Breite der Strahlenkanten in Pixeln, in der Vorlage weicher als der Schirmrand
    constexpr float EM80_BRIGHTNESS_BASE = 172.0f;    ///< Helligkeit in den Leuchtfächern
    constexpr float EM80_RADIAL_BASE = 0.70f;         ///< Helligkeit der Leuchtfächer am Zusammenlauf
    constexpr float EM80_RADIAL_SPAN = 0.30f;         ///< Helligkeitszuwachs der Leuchtfächer bis zur Schirmoberkante
    constexpr float EM80_EDGE_BAND = 1.4f;            ///< Weicher Auslauf des Leuchtschirms nach außen in Pixeln

    // Die EM84 ist ein Leuchtband. Zwei Balken wachsen von beiden Enden zur Mitte. Bei Vollausschlag treffen sie sich und lassen nur einen dünnen dunklen Steg stehen. Das Verhältnis des Leuchtschirms stammt aus der Schattenbild-Tafel, die das Band liegend zeigt. Hier steht es hochkant, weil das besser in die RetroSkala passt.
    constexpr float EM84_RING_THICKNESS = 4.0f;    ///< Breite des dunklen Rahmens um den Leuchtschirm in Pixeln
    constexpr float EM84_CORNER = 3.0f;            ///< Eckenradius des Leuchtschirms in Pixeln
    constexpr float EM84_BAR_MIN = 0.08f;          ///< Leuchtlänge je Balken bei Pegel 0 als Anteil der halben Schirmbreite
    constexpr float EM84_BAR_MAX = 0.98f;          ///< Leuchtlänge je Balken bei Vollausschlag, der Rest bleibt als schmale dunkle Naht
    constexpr float EM84_BAR_AA = 0.8f;            ///< Halbe Breite der Balkenkante in Pixeln, schmal genug damit die Naht bei Vollausschlag stehen bleibt
    constexpr float EM84_RADIAL_BASE = 0.70f;      ///< Helligkeit der Balken an ihrem inneren Ende
    constexpr float EM84_RADIAL_SPAN = 0.30f;      ///< Helligkeitszuwachs der Balken bis zum Schirmrand
    constexpr float EM84_EDGE_BAND = 1.4f;         ///< Weicher Auslauf des Leuchtschirms nach außen in Pixeln

    constexpr float EYE_RGB565_5BIT = 255.0f / 31.0f; ///< Streckung eines fünfstelligen Farbanteils auf den vollen Bereich
    constexpr float EYE_RGB565_6BIT = 255.0f / 63.0f; ///< Streckung eines sechsstelligen Farbanteils auf den vollen Bereich

    /**
     * @brief Bauform der nachgebildeten Abstimmröhre
     */
    enum class EyeGeometry
    {
        Em11, ///< Vier Leuchtsektoren, senkrecht und waagerecht, mit unterschiedlicher Öffnungscharakteristik
        Em34, ///< Zwei große Schattenkeile mit unterschiedlicher Empfindlichkeit und zwei feste Nebenkeile
        Em80, ///< Drei Leuchtfächer auf einem stehenden Rundbogen, geteilt von zwei Schattenstrahlen
        Em84  ///< Hochkant stehendes Leuchtband mit zwei Balken, die von den Enden zur Mitte wachsen
    };

    /**
     * @brief Umriss eines stehenden Rundbogens
     *
     * Senkrechte Seiten, halbkreisförmige Oberkante, gerade Unterkante. Alle Maße sind auf den Mittelpunkt des umschließenden Rechtecks bezogen.
     */
    struct EyeArch
    {
        float halfWidth;    ///< Halbe Breite des geraden Teils
        float arcCenterY;   ///< Höhe des Bogenmittelpunkts, darüber ist der Umriss kreisförmig
        float radiusSq;     ///< Quadrierter Bogenradius, er entspricht der halben Breite
        float invTwoRadius; ///< Kehrwert des doppelten Bogenradius, linearisiert den Randabstand
        float bottom;       ///< Höhe der Unterkante
    };

    /**
     * @brief Umriss eines Rechtecks mit abgerundeten Ecken
     *
     * Alle Maße sind auf den Mittelpunkt bezogen. Die Eckradien liegen als vorgezogene Halbmaße vor, damit der Pixelpfad ohne Subtraktion des Radius auskommt.
     */
    struct EyeRoundRect
    {
        float halfWidth;       ///< Halbe Breite
        float halfHeight;      ///< Halbe Höhe
        float innerHalfWidth;  ///< Halbe Breite abzüglich des Eckenradius
        float innerHalfHeight; ///< Halbe Höhe abzüglich des Eckenradius
        float radiusSq;        ///< Quadrierter Eckenradius
        float invTwoRadius;    ///< Kehrwert des doppelten Eckenradius, linearisiert den Randabstand
    };

    /**
     * @brief Alle je Aufruf einmalig berechneten Größen einer Abstimmröhre
     *
     * Zu jeder weichen Kante gehört der Kehrwert ihrer Breite, damit im Pixelinneren keine Division nötig ist.
     */
    struct EyeSetup
    {
        EyeGeometry geometry;    ///< Bauform, die den Zuschnitt der Leuchtflächen bestimmt
        int spanX;               ///< Halbe Breite des Durchlaufbereichs in Bildpunkten
        int spanTop;             ///< Oberste Zeile des Durchlaufbereichs relativ zum Anker
        int spanBottom;          ///< Unterste Zeile des Durchlaufbereichs relativ zum Anker
        float rowLimitSq;        ///< Quadrierter Außenradius für die zeilenweise Begrenzung der runden Bauformen
        float cosAxis;           ///< Kosinus der Achsdrehung
        float sinAxis;           ///< Sinus der Achsdrehung
        float radiusSq;          ///< Quadrierter Radius des Leuchtschirms
        float invTwoRadius;      ///< Kehrwert des doppelten Radius, wandelt Quadratabstände in Randtiefen
        float invRadiusSq;       ///< Kehrwert des quadrierten Radius
        float invAaSpan;         ///< Kehrwert der vollen Kantenbreite
        float invEdgeBand;       ///< Kehrwert der Schirmrandbreite
        float rimInnerDepth;     ///< Tiefe der Innenkante des Randbogens unter dem Schirmrand
        float rimOuterDepth;     ///< Tiefe der Außenkante des Randbogens unter dem Schirmrand
        float invEdgeRise;       ///< Kehrwert der Anstiegsbreite des Kantenlichts
        float invEdgeFall;       ///< Kehrwert der Abfallbreite des Kantenlichts
        float invSecondRise;     ///< Kehrwert der Anstiegsbreite des Kantenlichts an den Nebenkeilen
        float invSecondFall;     ///< Kehrwert der Abfallbreite des Kantenlichts an den Nebenkeilen
        float capRadius;         ///< Außenradius der Metallkappe
        float capRadiusSq;       ///< Quadrierter Außenradius der Metallkappe
        float invTwoCapRadius;   ///< Kehrwert des doppelten Kappenradius
        float invCapRadius;      ///< Kehrwert des Kappenradius
        float cornerCoreSq;      ///< Quadrierter Radius des Eckkerns
        float invCornerSpan;     ///< Kehrwert der quadrierten Spanne des Eckleuchtens
        float cornerAreaSq;      ///< Quadrierter Abstand, unterhalb dessen kein Eckleuchten geprüft wird
        float cosHalfLeft;       ///< Kosinus des halben Öffnungswinkels links, nur EM34
        float sinHalfLeft;       ///< Sinus des halben Öffnungswinkels links, nur EM34
        float cosHalfRight;      ///< Kosinus des halben Öffnungswinkels rechts, nur EM34
        float sinHalfRight;      ///< Sinus des halben Öffnungswinkels rechts, nur EM34
        float invSmallLength;    ///< Kehrwert der Nebenkeillänge, nur EM34
        float smallSpan;         ///< Differenz zwischen mittlerer und Spitzenbreite der Nebenkeile, nur EM34
        float smallTip;          ///< Halbe Breite der Nebenkeile an der Spitze, nur EM34
        float invFadeSpan;       ///< Kehrwert des Auslaufbereichs an der Keilspitze, nur EM34
        float cornerAlongLeft;   ///< Achsabstand der linken Eckpunkte, nur EM34
        float cornerAcrossLeft;  ///< Querabstand der linken Eckpunkte, nur EM34
        float cornerAlongRight;  ///< Achsabstand der rechten Eckpunkte, nur EM34
        float cornerAcrossRight; ///< Querabstand der rechten Eckpunkte, nur EM34
        float cosUpright;        ///< Kosinus des halben Sektorwinkels senkrecht, nur EM11
        float sinUpright;        ///< Sinus des halben Sektorwinkels senkrecht, nur EM11
        float cosCrosswise;      ///< Kosinus des halben Sektorwinkels waagerecht, nur EM11
        float sinCrosswise;      ///< Sinus des halben Sektorwinkels waagerecht, nur EM11
        float cornerUprightX;    ///< Querabstand der Eckpunkte an den senkrechten Sektoren, nur EM11
        float cornerUprightY;    ///< Achsabstand der Eckpunkte an den senkrechten Sektoren, nur EM11
        float cornerCrosswiseX;  ///< Achsabstand der Eckpunkte an den waagerechten Sektoren, nur EM11
        float cornerCrosswiseY;  ///< Querabstand der Eckpunkte an den waagerechten Sektoren, nur EM11
        EyeArch ringArch;        ///< Außenkante des dunklen Rahmens, nur EM80
        EyeArch screenArch;      ///< Kante des Leuchtschirms innerhalb des Rahmens, nur EM80
        float apexY;             ///< Höhe des Zusammenlaufs der Fächer an der Bodenmitte, nur EM80
        float cosInner;          ///< Kosinus der Innenkante der Schattenstrahlen, nur EM80
        float sinInner;          ///< Sinus der Innenkante der Schattenstrahlen, nur EM80
        float cosOuter;          ///< Kosinus der Außenkante der Schattenstrahlen, nur EM80
        float sinOuter;          ///< Sinus der Außenkante der Schattenstrahlen, nur EM80
        float invBeamSpanSq;     ///< Kehrwert des quadrierten Abstands vom Zusammenlauf zur Schirmoberkante, nur EM80
        float invWedgeAaSpan;    ///< Kehrwert der vollen Breite der Strahlenkanten, nur EM80
        EyeRoundRect ringRect;   ///< Außenkante des Rahmens, nur EM84
        EyeRoundRect screenRect; ///< Kante des Leuchtschirms innerhalb des Rahmens, nur EM84
        float gapHalfLength;     ///< Halbe Länge des dunklen Stegs zwischen den beiden Balken, nur EM84
        float invBarAaSpan;      ///< Kehrwert der vollen Breite der Balkenkante, nur EM84
        float invBandHalfLength; ///< Kehrwert der halben Bandlänge, nur EM84
        float ringRed;           ///< Rotanteil der Rahmenfarbe, nur EM80 und EM84
        float ringGreen;         ///< Grünanteil der Rahmenfarbe, nur EM80 und EM84
        float ringBlue;          ///< Blauanteil der Rahmenfarbe, nur EM80 und EM84
        const AugenKoeffizienten *licht; ///< Farb- und Helligkeitskoeffizienten Aug.Phosphor des aktiven Themas
    };

    /**
     * @brief Farbe und Deckung einer einzelnen Abtastung
     */
    struct EyeSample
    {
        float red;      ///< Rotanteil von 0 bis 255
        float green;    ///< Grünanteil von 0 bis 255
        float blue;     ///< Blauanteil von 0 bis 255
        float coverage; ///< Anteil, zu dem die Abtastung auf dem Leuchtschirm liegt
    };
}

/**
 * @brief Weicher Hermite-Übergang mit vorberechnetem Kehrwert der Kantenbreite
 */
static inline float eyeRamp(float x, float edge0, float invSpan)
{
    float t = (x - edge0) * invSpan;
    t = constrain(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

/**
 * @brief Füllt die Größen, die beide Bauformen gemeinsam haben
 */
static void eyePrepareShared(EyeSetup &s, float r, float axisAngle)
{
    s.licht = &farbtabellen().augePhosphor;
    s.cosAxis = cosf(axisAngle);
    s.sinAxis = sinf(axisAngle);

    // Runde Bauformen laufen zeilenweise nur über den Kreisausschnitt.
    const float outerRadius = r + 1.0f;
    s.rowLimitSq = outerRadius * outerRadius;
    s.spanX = static_cast<int>(ceilf(r)) + 1;
    s.spanTop = -s.spanX;
    s.spanBottom = s.spanX;

    s.radiusSq = r * r;
    s.invTwoRadius = 1.0f / (2.0f * r);
    s.invRadiusSq = 1.0f / s.radiusSq;
    s.invAaSpan = 1.0f / (2.0f * EYE_EDGE_AA);
    s.invEdgeBand = 1.0f / fmaxf(EYE_EDGE_BAND_MIN, r * EYE_EDGE_BAND_RATIO);
    s.rimInnerDepth = fmaxf(EYE_RIM_INNER_MIN, r * EYE_RIM_INNER_RATIO);
    s.rimOuterDepth = fmaxf(EYE_RIM_OUTER_MIN, r * EYE_RIM_OUTER_RATIO);

    s.invEdgeRise = 1.0f / EYE_EDGE_PEAK;
    s.invEdgeFall = 1.0f / (EYE_EDGE_END - EYE_EDGE_PEAK);
    s.invSecondRise = 1.0f / EYE_SECOND_EDGE_PEAK;
    s.invSecondFall = 1.0f / (EYE_SECOND_EDGE_END - EYE_SECOND_EDGE_PEAK);

    s.capRadius = r * EYE_CAP_RADIUS_RATIO;
    s.capRadiusSq = s.capRadius * s.capRadius;
    s.invTwoCapRadius = 1.0f / (2.0f * s.capRadius);
    s.invCapRadius = 1.0f / s.capRadius;

    const float core = fmaxf(EYE_CORNER_CORE_MIN, r * EYE_CORNER_CORE_RATIO);
    const float round = fmaxf(EYE_CORNER_ROUND_MIN, r * EYE_CORNER_ROUND_RATIO);
    s.cornerCoreSq = core * core;
    s.invCornerSpan = 1.0f / (round * round - s.cornerCoreSq);

    // Das Eckleuchten sitzt am Schirmrand, weiter innen wird es gar nicht erst geprüft.
    const float cornerArea = r * EYE_CORNER_RADIUS_RATIO - round;
    s.cornerAreaSq = cornerArea * cornerArea;
}

/**
 * @brief Bereitet eine EM34-Darstellung vor
 */
static EyeSetup eyePrepareEm34(float r, float level)
{
    EyeSetup s;
    s.geometry = EyeGeometry::Em34;
    eyePrepareShared(s, r, EM34_AXIS_ANGLE);

    const float levelLeft = fminf(level * EM34_SENSITIVITY_LEFT, 1.0f);
    const float levelRight = fminf(level * EM34_SENSITIVITY_RIGHT, 1.0f);
    const float halfLeft = EM34_HALF_ANGLE_OPEN + (EM34_HALF_ANGLE_CLOSED - EM34_HALF_ANGLE_OPEN) * levelLeft;
    const float halfRight = EM34_HALF_ANGLE_OPEN + (EM34_HALF_ANGLE_CLOSED - EM34_HALF_ANGLE_OPEN) * levelRight;

    s.cosHalfLeft = cosf(halfLeft);
    s.sinHalfLeft = sinf(halfLeft);
    s.cosHalfRight = cosf(halfRight);
    s.sinHalfRight = sinf(halfRight);

    s.invSmallLength = 1.0f / (r * EM34_SMALL_LENGTH_RATIO);
    s.smallTip = r * EM34_SMALL_TIP_RATIO;
    s.smallSpan = r * EM34_SMALL_HALF_WIDTH_RATIO - s.smallTip;
    s.invFadeSpan = 1.0f / (1.0f - EM34_SMALL_FADE_START);

    const float cornerRadius = r * EYE_CORNER_RADIUS_RATIO;
    s.cornerAlongLeft = cornerRadius * s.cosHalfLeft;
    s.cornerAcrossLeft = cornerRadius * s.sinHalfLeft;
    s.cornerAlongRight = cornerRadius * s.cosHalfRight;
    s.cornerAcrossRight = cornerRadius * s.sinHalfRight;

    return s;
}

/**
 * @brief Bereitet eine EM11-Darstellung vor
 *
 * Die senkrechten Sektoren öffnen früh und weit, die waagerechten später und schmaler.
 */
static EyeSetup eyePrepareEm11(float r, float level)
{
    EyeSetup s;
    s.geometry = EyeGeometry::Em11;
    eyePrepareShared(s, r, EM11_AXIS_ANGLE);

    const float uprightDeg = EM11_UPRIGHT_MIN_DEG + (EM11_UPRIGHT_MAX_DEG - EM11_UPRIGHT_MIN_DEG) * level * level;

    float delayed = (level - EM11_CROSSWISE_DELAY) / (1.0f - EM11_CROSSWISE_DELAY);
    delayed = constrain(delayed, 0.0f, 1.0f);
    const float eased = delayed * delayed * (3.0f - 2.0f * delayed);
    const float crosswiseDeg = EM11_CROSSWISE_MIN_DEG + (EM11_CROSSWISE_MAX_DEG - EM11_CROSSWISE_MIN_DEG) * eased;

    const float halfUpright = uprightDeg * 0.5f * EYE_DEG_TO_RAD;
    const float halfCrosswise = crosswiseDeg * 0.5f * EYE_DEG_TO_RAD;

    s.cosUpright = cosf(halfUpright);
    s.sinUpright = sinf(halfUpright);
    s.cosCrosswise = cosf(halfCrosswise);
    s.sinCrosswise = sinf(halfCrosswise);

    const float cornerRadius = r * EYE_CORNER_RADIUS_RATIO;
    s.cornerUprightX = cornerRadius * s.sinUpright;
    s.cornerUprightY = cornerRadius * s.cosUpright;
    s.cornerCrosswiseX = cornerRadius * s.cosCrosswise;
    s.cornerCrosswiseY = cornerRadius * s.sinCrosswise;

    return s;
}

/**
 * @brief Füllt einen Rundbogen aus halber Breite, Oberkante und Unterkante
 */
static void eyeFillArch(EyeArch &a, float halfWidth, float top, float bottom)
{
    a.halfWidth = halfWidth;
    a.arcCenterY = top + halfWidth;
    a.radiusSq = halfWidth * halfWidth;
    a.invTwoRadius = 1.0f / (2.0f * halfWidth);
    a.bottom = bottom;
}

/**
 * @brief Bereitet eine EM80-Darstellung vor
 *
 * Übergeben werden die halben Maße des umschließenden Rechtecks, darin liegt außen der dunkle Rahmen und innen der Leuchtschirm.
 */
static EyeSetup eyePrepareEm80(float halfWidth, float halfHeight, float level, uint16_t ring)
{
    EyeSetup s;
    s.geometry = EyeGeometry::Em80;
    s.licht = &farbtabellen().augePhosphor;

    // Der Rundbogen wird zeilenweise in voller Breite durchlaufen, die Deckung verwirft die Ecken.
    s.spanX = static_cast<int>(ceilf(halfWidth)) + 1;
    s.spanBottom = static_cast<int>(ceilf(halfHeight)) + 1;
    s.spanTop = -s.spanBottom;
    s.rowLimitSq = 0.0f;

    eyeFillArch(s.ringArch, halfWidth, -halfHeight, halfHeight);

    const float screenHalfWidth = halfWidth - EM80_RING_THICKNESS;
    const float screenTop = -halfHeight + EM80_RING_THICKNESS;
    const float screenBottom = halfHeight - EM80_RING_THICKNESS;
    const float screenHeight = screenBottom - screenTop;
    eyeFillArch(s.screenArch, screenHalfWidth, screenTop, screenBottom);

    // Die Innenkante der Schattenstrahlen wandert gleichmäßig nach außen, die Außenkante folgt erst spät und lässt dabei die äußeren Fächer wachsen.
    const float innerAngle = (EM80_INNER_MIN_DEG + (EM80_INNER_MAX_DEG - EM80_INNER_MIN_DEG) * level) * EYE_DEG_TO_RAD;
    const float outerAngle = (EM80_OUTER_OPEN_DEG - (EM80_OUTER_OPEN_DEG - EM80_OUTER_CLOSED_DEG) * level * level) * EYE_DEG_TO_RAD;
    s.cosInner = cosf(innerAngle);
    s.sinInner = sinf(innerAngle);
    s.cosOuter = cosf(outerAngle);
    s.sinOuter = sinf(outerAngle);

    // Alle Fächer laufen an der Bodenmitte des Leuchtschirms zusammen.
    s.apexY = screenBottom;
    const float beamSpan = fmaxf(1.0f, screenHeight);
    s.invBeamSpanSq = 1.0f / (beamSpan * beamSpan);
    s.invWedgeAaSpan = 1.0f / (2.0f * EM80_WEDGE_AA);

    // Weiche Kanten und Randbogen des Leuchtschirms
    s.invAaSpan = 1.0f / (2.0f * EYE_EDGE_AA);
    s.invEdgeBand = 1.0f / EM80_EDGE_BAND;
    s.rimInnerDepth = EYE_RIM_INNER_MIN;
    s.rimOuterDepth = EYE_RIM_OUTER_MIN;

    s.ringRed = static_cast<float>((ring >> 11) & 0x1F) * EYE_RGB565_5BIT;
    s.ringGreen = static_cast<float>((ring >> 5) & 0x3F) * EYE_RGB565_6BIT;
    s.ringBlue = static_cast<float>(ring & 0x1F) * EYE_RGB565_5BIT;

    return s;
}

/**
 * @brief Füllt ein Rechteck mit abgerundeten Ecken aus halben Maßen und Eckenradius
 */
static void eyeFillRoundRect(EyeRoundRect &r, float halfWidth, float halfHeight, float corner)
{
    r.halfWidth = halfWidth;
    r.halfHeight = halfHeight;
    r.innerHalfWidth = halfWidth - corner;
    r.innerHalfHeight = halfHeight - corner;
    r.radiusSq = corner * corner;
    r.invTwoRadius = 1.0f / (2.0f * corner);
}

/**
 * @brief Bereitet eine EM84-Darstellung vor
 *
 * Übergeben werden die halben Maße des umschließenden Rechtecks, darin liegt außen der dunkle Rahmen und innen das Leuchtband.
 */
static EyeSetup eyePrepareEm84(float halfWidth, float halfHeight, float level, uint16_t ring)
{
    EyeSetup s;
    s.geometry = EyeGeometry::Em84;
    s.licht = &farbtabellen().augePhosphor;

    // Das Leuchtband ist klein genug, um es zeilenweise in voller Breite zu durchlaufen.
    s.spanX = static_cast<int>(ceilf(halfWidth)) + 1;
    s.spanBottom = static_cast<int>(ceilf(halfHeight)) + 1;
    s.spanTop = -s.spanBottom;
    s.rowLimitSq = 0.0f;

    eyeFillRoundRect(s.ringRect, halfWidth, halfHeight, EM84_CORNER + EM84_RING_THICKNESS);

    const float screenHalfWidth = halfWidth - EM84_RING_THICKNESS;
    const float screenHalfHeight = halfHeight - EM84_RING_THICKNESS;
    eyeFillRoundRect(s.screenRect, screenHalfWidth, screenHalfHeight, EM84_CORNER);

    // Beide Balken wachsen gleich weit von den Enden nach innen. Dazwischen bleibt der dunkle Steg. Das Band steht hochkant, seine Längsachse ist also die Höhe.
    const float litLength = EM84_BAR_MIN + (EM84_BAR_MAX - EM84_BAR_MIN) * level;
    s.gapHalfLength = screenHalfHeight * (1.0f - litLength);
    s.invBarAaSpan = 1.0f / (2.0f * EM84_BAR_AA);
    s.invBandHalfLength = 1.0f / fmaxf(1.0f, screenHalfHeight);

    // Weiche Kanten und Randbogen wie bei den runden Bauformen
    s.invAaSpan = 1.0f / (2.0f * EYE_EDGE_AA);
    s.invEdgeBand = 1.0f / EM84_EDGE_BAND;
    s.rimInnerDepth = EYE_RIM_INNER_MIN;
    s.rimOuterDepth = EYE_RIM_OUTER_MIN;

    s.ringRed = static_cast<float>((ring >> 11) & 0x1F) * EYE_RGB565_5BIT;
    s.ringGreen = static_cast<float>((ring >> 5) & 0x3F) * EYE_RGB565_6BIT;
    s.ringBlue = static_cast<float>(ring & 0x1F) * EYE_RGB565_5BIT;

    return s;
}

/**
 * @brief Linearisierter Abstand zum Rand eines Rechtecks mit abgerundeten Ecken, innerhalb der Fläche positiv
 *
 * Nur im Eckbereich wird der Kreisabstand aus dem Quadratabstand linearisiert, damit der Pixelpfad ohne Wurzel auskommt.
 */
static inline float eyeRoundRectDepth(const EyeRoundRect &r, float dx, float dy)
{
    const float across = fabsf(dx);
    const float along = fabsf(dy);
    const float overX = across - r.innerHalfWidth;
    const float overY = along - r.innerHalfHeight;

    if (overX <= 0.0f || overY <= 0.0f)
        return fminf(r.halfWidth - across, r.halfHeight - along);

    return (r.radiusSq - (overX * overX + overY * overY)) * r.invTwoRadius;
}

/**
 * @brief Linearisierter Abstand zum Rand eines Rundbogens, innerhalb der Fläche positiv
 *
 * Oberhalb des Bogenmittelpunkts wird der Kreisabstand aus dem Quadratabstand linearisiert, damit der Pixelpfad ohne Wurzel auskommt.
 */
static inline float eyeArchDepth(const EyeArch &a, float dx, float dy)
{
    const float floorDepth = a.bottom - dy;

    if (dy >= a.arcCenterY)
        return fminf(a.halfWidth - fabsf(dx), floorDepth);

    const float arcY = dy - a.arcCenterY;
    const float arcDepth = (a.radiusSq - (dx * dx + arcY * arcY)) * a.invTwoRadius;

    return fminf(arcDepth, floorDepth);
}

/**
 * @brief Helligkeit einer metallischen Kappe an einer relativen Position
 */
static inline float eyeCapGray(float position, float light, float dark, float max)
{
    position = constrain(position, 0.0f, 1.0f);

    float gray = light - (light - dark) * position;
    gray -= eyeRamp(position, EYE_CAP_EDGE_START, 1.0f / (1.0f - EYE_CAP_EDGE_START)) * EYE_CAP_EDGE_DARKEN;

    return constrain(gray, 0.0f, max);
}

/**
 * @brief Berechnet Farbe und Deckung der runden Bauformen an einem Punkt relativ zum Schirmmittelpunkt
 *
 * Abstände zum Schirmrand werden aus dem Quadratabstand linearisiert, weil sie nur in Randnähe ausgewertet werden. Dadurch bleibt der Pixelpfad frei von Wurzeln. Nur innerhalb der Metallkappe wird eine gezogen.
 */
static EyeSample eyeSampleRound(const EyeSetup &s, float dx, float dy)
{
    EyeSample out;
    out.red = 0.0f;
    out.green = 0.0f;
    out.blue = 0.0f;

    const float distSq = dx * dx + dy * dy;
    const float rimDepth = (s.radiusSq - distSq) * s.invTwoRadius;
    out.coverage = constrain(rimDepth + 0.5f, 0.0f, 1.0f);
    if (out.coverage <= 0.0f)
        return out;

    const float signedAlong = dx * s.cosAxis + dy * s.sinAxis;
    const float signedAcross = -dx * s.sinAxis + dy * s.cosAxis;
    const float along = fabsf(signedAlong);
    const float across = fabsf(signedAcross);

    const float edgeFade = eyeRamp(rimDepth, 0.0f, s.invEdgeBand);

    float visible = 0.0f;
    float edgeGlow = 0.0f;
    float secondEdgeGlow = 0.0f;
    float cornerGlow = 0.0f;

    if (s.geometry == EyeGeometry::Em34)
    {
        const bool leftHalf = (signedAlong < 0.0f);
        const float cosHalf = leftHalf ? s.cosHalfLeft : s.cosHalfRight;
        const float sinHalf = leftHalf ? s.sinHalfLeft : s.sinHalfRight;

        // Vorzeichenbehafteter Pixelabstand zur Keilkante, positive Werte liegen auf der leuchtenden Seite.
        const float wedgeDistance = across * cosHalf - along * sinHalf;
        const float mainVisible = eyeRamp(wedgeDistance, -EYE_EDGE_AA, s.invAaSpan);

        // Nebenkeile stehen senkrecht zu den Hauptkeilen und sind pegelunabhängig.
        const float wedgePos = fminf(across * s.invSmallLength, 1.0f);
        const float narrowing = 1.0f - wedgePos;
        const float halfWidth = s.smallTip + s.smallSpan * narrowing * narrowing;
        const float smallDistance = along - halfWidth;
        const float smallVisible = eyeRamp(smallDistance, -EYE_EDGE_AA, s.invAaSpan);
        const float wedgeFade = 1.0f - eyeRamp(wedgePos, EM34_SMALL_FADE_START, s.invFadeSpan);
        const float smallFactor = 1.0f - (1.0f - smallVisible) * wedgeFade;

        visible = mainVisible * smallFactor;

        if (wedgeDistance > 0.0f && wedgeDistance < EYE_EDGE_END)
            edgeGlow = eyeRamp(wedgeDistance, 0.0f, s.invEdgeRise) *
                       (1.0f - eyeRamp(wedgeDistance, EYE_EDGE_PEAK, s.invEdgeFall)) * smallFactor * edgeFade;

        if (smallDistance > 0.0f && smallDistance < EYE_SECOND_EDGE_END)
            secondEdgeGlow = eyeRamp(smallDistance, 0.0f, s.invSecondRise) *
                             (1.0f - eyeRamp(smallDistance, EYE_SECOND_EDGE_PEAK, s.invSecondFall)) *
                             mainVisible * wedgeFade * edgeFade;

        if (distSq > s.cornerAreaSq)
        {
            const float cornerAlong = leftHalf ? s.cornerAlongLeft : s.cornerAlongRight;
            const float cornerAcross = leftHalf ? s.cornerAcrossLeft : s.cornerAcrossRight;
            const float deltaAlong = along - cornerAlong;
            const float deltaAcross = across - cornerAcross;
            const float nearestSq = deltaAlong * deltaAlong + deltaAcross * deltaAcross;
            cornerGlow = (1.0f - eyeRamp(nearestSq, s.cornerCoreSq, s.invCornerSpan)) * visible * edgeFade;
        }
    }
    else
    {
        // Vier Leuchtsektoren: der Punkt leuchtet, wenn er in einem der beiden Sektorpaare liegt.
        const float uprightDistance = across * s.sinUpright - along * s.cosUpright;
        const float crosswiseDistance = along * s.sinCrosswise - across * s.cosCrosswise;
        const float litDistance = fmaxf(uprightDistance, crosswiseDistance);

        visible = eyeRamp(litDistance, -EYE_EDGE_AA, s.invAaSpan);

        if (litDistance > 0.0f && litDistance < EYE_EDGE_END)
            edgeGlow = eyeRamp(litDistance, 0.0f, s.invEdgeRise) *
                       (1.0f - eyeRamp(litDistance, EYE_EDGE_PEAK, s.invEdgeFall)) * edgeFade;

        if (distSq > s.cornerAreaSq)
        {
            const float deltaUprightX = along - s.cornerUprightX;
            const float deltaUprightY = across - s.cornerUprightY;
            const float deltaCrossX = along - s.cornerCrosswiseX;
            const float deltaCrossY = across - s.cornerCrosswiseY;
            const float nearestSq = fminf(deltaUprightX * deltaUprightX + deltaUprightY * deltaUprightY,
                                          deltaCrossX * deltaCrossX + deltaCrossY * deltaCrossY);
            cornerGlow = (1.0f - eyeRamp(nearestSq, s.cornerCoreSq, s.invCornerSpan)) * visible * edgeFade;
        }
    }

    const AugenKoeffizienten &k = *s.licht;
    const float radial = EYE_RADIAL_BASE + EYE_RADIAL_SPAN * distSq * s.invRadiusSq;

    float brightness = edgeFade * (k.helligkeitLeuchten * visible * radial + k.helligkeitSchatten * (1.0f - visible));
    float yellowTint = k.tonKante * edgeGlow + k.tonNebenkante * secondEdgeGlow + k.tonEcke * cornerGlow;

    brightness += k.helligkeitKante * edgeGlow;
    brightness += k.helligkeitNebenkante * secondEdgeGlow;
    brightness += k.helligkeitEcke * cornerGlow;

    // Heller Randbogen, nur in Randnähe
    if (rimDepth < s.rimInnerDepth + EYE_EDGE_AA)
    {
        const float rimBand = eyeRamp(rimDepth, s.rimOuterDepth - EYE_EDGE_AA, s.invAaSpan) *
                              (1.0f - eyeRamp(rimDepth, s.rimInnerDepth - EYE_EDGE_AA, s.invAaSpan));
        const float rimGlow = rimBand * visible * edgeFade;
        brightness += k.helligkeitRand * rimGlow;
        yellowTint += rimGlow;
    }

    brightness = fminf(brightness, k.helligkeitMax);
    yellowTint = constrain(yellowTint, 0.0f, 1.0f);

    out.red = brightness * (k.phosphorRot + (k.randRot - k.phosphorRot) * yellowTint);
    out.green = brightness * (k.phosphorGruen + (k.randGruen - k.phosphorGruen) * yellowTint);
    out.blue = brightness * (k.phosphorBlau + (k.randBlau - k.phosphorBlau) * yellowTint);

    if (distSq < s.capRadiusSq + s.capRadius)
    {
        const float capDepth = (s.capRadiusSq - distSq) * s.invTwoCapRadius;
        const float capCoverage = constrain(capDepth + 0.5f, 0.0f, 1.0f);
        if (capCoverage > 0.0f)
        {
            const float gray = eyeCapGray(sqrtf(distSq) * s.invCapRadius, k.kappeHell, k.kappeDunkel, k.helligkeitMax);
            out.red = gray * capCoverage + out.red * (1.0f - capCoverage);
            out.green = gray * k.kappeGruen * capCoverage + out.green * (1.0f - capCoverage);
            out.blue = gray * k.kappeBlau * capCoverage + out.blue * (1.0f - capCoverage);
        }
    }

    return out;
}

/**
 * @brief Verrechnet Leuchtstoff, Randbogen und Rahmen für die Bauformen mit eigenem Rahmen
 * @param out Abtastung, deren Farbanteile gefüllt werden
 * @param s Vorbereitete Größen der Abstimmröhre
 * @param screenDepth Abstand zum Schirmrand, innerhalb der Fläche positiv
 * @param screenCoverage Anteil, zu dem die Abtastung auf dem Leuchtschirm liegt
 * @param visible Anteil, zu dem die Abtastung in einer Leuchtfläche liegt
 * @param radial Helligkeitsfaktor der Leuchtfläche an dieser Stelle
 * @param baseBrightness Helligkeit der voll ausgeleuchteten Fläche
 *
 * EM80 und EM84 unterscheiden sich nur im Zuschnitt ihrer Leuchtflächen. Restleuchten, Schirmauslauf, Randbogen und der Übergang zum Rahmen sind bei beiden gleich.
 */
static inline void eyeShadeFramedScreen(EyeSample &out, const EyeSetup &s, float screenDepth, float screenCoverage,
                                        float visible, float radial, float baseBrightness)
{
    const AugenKoeffizienten &k = *s.licht;
    const float edgeFade = eyeRamp(screenDepth, 0.0f, s.invEdgeBand);

    float brightness = edgeFade * (baseBrightness * visible * radial + k.helligkeitSchatten * (1.0f - visible));
    float yellowTint = 0.0f;

    // Heller Randbogen, nur in Randnähe. Helligkeit, Breite und Färbung entsprechen den runden Bauformen.
    if (screenDepth < s.rimInnerDepth + EYE_EDGE_AA)
    {
        const float rimBand = eyeRamp(screenDepth, s.rimOuterDepth - EYE_EDGE_AA, s.invAaSpan) *
                              (1.0f - eyeRamp(screenDepth, s.rimInnerDepth - EYE_EDGE_AA, s.invAaSpan));
        const float rimGlow = rimBand * visible * edgeFade;
        brightness += k.helligkeitRand * rimGlow;
        yellowTint = constrain(rimGlow, 0.0f, 1.0f);
    }

    brightness = fminf(brightness, k.helligkeitMax);

    const float red = brightness * (k.phosphorRot + (k.randRot - k.phosphorRot) * yellowTint);
    const float green = brightness * (k.phosphorGruen + (k.randGruen - k.phosphorGruen) * yellowTint);
    const float blue = brightness * (k.phosphorBlau + (k.randBlau - k.phosphorBlau) * yellowTint);

    // Am Übergang zum Rahmen wird der Leuchtschirm gegen die Rahmenfarbe verrechnet.
    const float outside = 1.0f - screenCoverage;
    out.red = red * screenCoverage + s.ringRed * outside;
    out.green = green * screenCoverage + s.ringGreen * outside;
    out.blue = blue * screenCoverage + s.ringBlue * outside;
}

/**
 * @brief Berechnet Farbe und Deckung der EM80 an einem Punkt relativ zur Mitte des umschließenden Rechtecks
 *
 * Zwei Schattenstrahlen laufen an der Bodenmitte zusammen und teilen den Schirm in drei Leuchtfächer: der mittlere liegt innerhalb ihrer Innenkante, die beiden äußeren jenseits ihrer Außenkante. In den Strahlen glimmt der Schirm weiter, wie es die Vorlage zeigt. Alle Randabstände laufen über eyeArchDepth und bleiben damit wurzelfrei.
 */
static EyeSample eyeSampleEm80(const EyeSetup &s, float dx, float dy)
{
    EyeSample out;

    const float ringDepth = eyeArchDepth(s.ringArch, dx, dy);
    out.coverage = constrain(ringDepth + 0.5f, 0.0f, 1.0f);
    out.red = s.ringRed;
    out.green = s.ringGreen;
    out.blue = s.ringBlue;
    if (out.coverage <= 0.0f)
        return out;

    const float screenDepth = eyeArchDepth(s.screenArch, dx, dy);
    const float screenCoverage = constrain(screenDepth + 0.5f, 0.0f, 1.0f);
    if (screenCoverage <= 0.0f)
        return out;

    // Vorzeichenbehaftete Pixelabstände zu den beiden Kanten eines Schattenstrahls, gemessen vom Zusammenlauf an der Bodenmitte.
    const float alongBeam = s.apexY - dy;
    const float acrossBeam = fabsf(dx);
    const float innerDistance = alongBeam * s.sinInner - acrossBeam * s.cosInner;
    const float outerDistance = alongBeam * s.sinOuter - acrossBeam * s.cosOuter;

    // Es leuchtet, wer innerhalb der Innenkante liegt (mittlerer Fächer) oder jenseits der Außenkante (die beiden äußeren Fächer).
    const float visible = fmaxf(eyeRamp(innerDistance, -EM80_WEDGE_AA, s.invWedgeAaSpan),
                                eyeRamp(-outerDistance, -EM80_WEDGE_AA, s.invWedgeAaSpan));

    // Die Fächer werden zur Schirmoberkante hin heller, der Quadratabstand zum Zusammenlauf genügt als Maß.
    const float beamSq = fminf((dx * dx + alongBeam * alongBeam) * s.invBeamSpanSq, 1.0f);
    const float radial = EM80_RADIAL_BASE + EM80_RADIAL_SPAN * beamSq;

    eyeShadeFramedScreen(out, s, screenDepth, screenCoverage, visible, radial, EM80_BRIGHTNESS_BASE);

    return out;
}

/**
 * @brief Berechnet Farbe und Deckung der EM84 an einem Punkt relativ zur Mitte des umschließenden Rechtecks
 *
 * Das Band steht hochkant. Zwei Balken wachsen von oben und unten zur Mitte, dazwischen bleibt ein dunkler Steg, der bei Vollausschlag nur noch als schmale Naht stehenbleibt. Alle Randabstände laufen über eyeRoundRectDepth und bleiben damit wurzelfrei.
 */
static EyeSample eyeSampleEm84(const EyeSetup &s, float dx, float dy)
{
    EyeSample out;

    const float ringDepth = eyeRoundRectDepth(s.ringRect, dx, dy);
    out.coverage = constrain(ringDepth + 0.5f, 0.0f, 1.0f);
    out.red = s.ringRed;
    out.green = s.ringGreen;
    out.blue = s.ringBlue;
    if (out.coverage <= 0.0f)
        return out;

    const float screenDepth = eyeRoundRectDepth(s.screenRect, dx, dy);
    const float screenCoverage = constrain(screenDepth + 0.5f, 0.0f, 1.0f);
    if (screenCoverage <= 0.0f)
        return out;

    // Vorzeichenbehafteter Pixelabstand zur Balkenkante, positive Werte liegen im Leuchtenden.
    const float along = fabsf(dy);
    const float visible = eyeRamp(along - s.gapHalfLength, -EM84_BAR_AA, s.invBarAaSpan);

    // Die Balken werden zu den Enden hin heller, wie die runden Bauformen zum Schirmrand.
    const float reach = fminf(along * s.invBandHalfLength, 1.0f);
    const float radial = EM84_RADIAL_BASE + EM84_RADIAL_SPAN * reach * reach;

    eyeShadeFramedScreen(out, s, screenDepth, screenCoverage, visible, radial, s.licht->helligkeitLeuchten);

    return out;
}

/**
 * @brief Rechnet die Rahmenfarbe von EM80 und EM84 für hellen Grund in Tinte um, damit drawMagicEyeCore nach dem Abziehen vom Grund wieder die Rahmenfarbe zeigt. Auf dunklem Grund unverändert.
 */
static EyeSetup eyeRingOnGround(EyeSetup s, uint16_t bg)
{
    if (!(themaAktiv().merkmale & MerkmalHellerGrund))
        return s;
    s.ringRed = fmaxf(0.0f, static_cast<float>((bg >> 11) & 0x1F) * EYE_RGB565_5BIT - s.ringRed);
    s.ringGreen = fmaxf(0.0f, static_cast<float>((bg >> 5) & 0x3F) * EYE_RGB565_6BIT - s.ringGreen);
    s.ringBlue = fmaxf(0.0f, static_cast<float>(bg & 0x1F) * EYE_RGB565_5BIT - s.ringBlue);
    return s;
}

/**
 * @brief Zeichnet eine Abstimmröhre aus den vorbereiteten Größen
 *
 * Der Durchlaufbereich steht in der Vorbereitung, die runden Bauformen beschränken jede Bildzeile zusätzlich auf den Kreisausschnitt. rowFrom und rowTo begrenzen ihn auf die Zielzeilen rowFrom bis rowTo - 1, rowTo -1 reicht bis zum unteren Rand. Zeilen außerhalb des Ziels fielen ohnehin dem Zuschnitt zum Opfer.
 */
template <typename TDisplay>
static void drawMagicEyeCore(TDisplay *display, int xc, int yc, const EyeSetup &setup, uint16_t bg, int rowFrom, int rowTo)
{
    const float step = 1.0f / static_cast<float>(EYE_SUPERSAMPLE);
    const float firstOffset = step * 0.5f - 0.5f;
    const float invSampleCount = 1.0f / static_cast<float>(EYE_SUPERSAMPLE * EYE_SUPERSAMPLE);
    // Auf hellem Grund ist das Licht Tinte: jeder Kanal wird vom Grund abgezogen.
    const bool ink = themaAktiv().merkmale & MerkmalHellerGrund;
    const uint32_t paperRed = static_cast<uint32_t>(((bg >> 11) & 0x1F) * EYE_RGB565_5BIT), paperGreen = static_cast<uint32_t>(((bg >> 5) & 0x3F) * EYE_RGB565_6BIT), paperBlue = static_cast<uint32_t>((bg & 0x1F) * EYE_RGB565_5BIT);
    const bool isArch = (setup.geometry == EyeGeometry::Em80);
    const bool isBar = (setup.geometry == EyeGeometry::Em84);

    const int firstRow = std::max(setup.spanTop, rowFrom - yc);
    const int lastRow = std::min(setup.spanBottom, (rowTo < 0 ? static_cast<int>(display->height()) : rowTo) - 1 - yc);

    for (int offsetY = firstRow; offsetY <= lastRow; ++offsetY)
    {
        const float rowY = static_cast<float>(offsetY);

        int halfWidth = setup.spanX;
        if (!isArch && !isBar)
        {
            const float rest = setup.rowLimitSq - rowY * rowY;
            if (rest <= 0.0f)
                continue;

            halfWidth = static_cast<int>(sqrtf(rest));
        }
        else if (isArch && rowY < setup.ringArch.arcCenterY)
        {
            // Oberhalb des Bogenmittelpunkts spart der Kreisausschnitt die leeren Ecken aus. Nur die EM80 füllt ringArch, das Leuchtband läuft immer über die volle Breite.
            const float arcY = rowY - setup.ringArch.arcCenterY;
            const float rest = setup.ringArch.radiusSq - arcY * arcY;
            if (rest <= 0.0f)
                continue;

            halfWidth = static_cast<int>(sqrtf(rest)) + 1;
        }

        for (int offsetX = -halfWidth; offsetX <= halfWidth; ++offsetX)
        {
            float sumRed = 0.0f;
            float sumGreen = 0.0f;
            float sumBlue = 0.0f;
            float sumCoverage = 0.0f;

            for (int sy = 0; sy < EYE_SUPERSAMPLE; ++sy)
            {
                const float dy = rowY + firstOffset + step * static_cast<float>(sy);

                for (int sx = 0; sx < EYE_SUPERSAMPLE; ++sx)
                {
                    const float dx = static_cast<float>(offsetX) + firstOffset + step * static_cast<float>(sx);
                    const EyeSample sample = isArch  ? eyeSampleEm80(setup, dx, dy)
                                             : isBar ? eyeSampleEm84(setup, dx, dy)
                                                     : eyeSampleRound(setup, dx, dy);
                    if (sample.coverage <= 0.0f)
                        continue;

                    sumRed += sample.red * sample.coverage;
                    sumGreen += sample.green * sample.coverage;
                    sumBlue += sample.blue * sample.coverage;
                    sumCoverage += sample.coverage;
                }
            }

            if (sumCoverage <= 0.0f)
                continue;

            const float invSum = 1.0f / sumCoverage;
            uint32_t red = static_cast<uint32_t>(sumRed * invSum);
            uint32_t green = static_cast<uint32_t>(sumGreen * invSum);
            uint32_t blue = static_cast<uint32_t>(sumBlue * invSum);
            if (ink)
            {
                red = paperRed > red ? paperRed - red : 0;
                green = paperGreen > green ? paperGreen - green : 0;
                blue = paperBlue > blue ? paperBlue - blue : 0;
            }
            const uint16_t color = static_cast<uint16_t>(((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3));

            display->drawPixelClipped(xc + offsetX, yc + offsetY, blendColor(color, bg, sumCoverage * invSampleCount));
        }
    }
}

void UiCanvas::drawSmoothCircle(int xc, int yc, float r, float thickness, uint16_t color, uint16_t bg)
{
    drawSmoothCircleCore(xc, yc, r, thickness, color, bg, [this](int x, int y, uint16_t col)
    {
        this->drawPixelClipped(x, y, col);
    });
}

void UiCanvas::drawSmoothGauge(int xc, int yc, float r, float thickness, float angleStart, float angleEnd, float valueMin, float valueMax, float value, uint16_t color, uint16_t colorTrack, bool trackVisible, uint16_t bg)
{
    drawSmoothGaugeCore(xc, yc, r, thickness, angleStart, angleEnd, valueMin, valueMax, value, color, colorTrack, trackVisible, bg, [this](int x, int y, uint16_t col)
    {
        this->drawPixelClipped(x, y, col);
    });
}

void UiCanvas::drawEm11MagicEye(int xc, int yc, float r, float level, uint16_t bg, int rowFrom, int rowTo)
{
    if (r < 1.0f)
        return;

    level = constrain(level, 0.0f, 1.0f);
    drawMagicEyeCore(this, xc, yc, eyePrepareEm11(r, level), bg, rowFrom, rowTo);
}

void UiCanvas::drawEm34MagicEye(int xc, int yc, float r, float level, uint16_t bg, int rowFrom, int rowTo)
{
    if (r < 1.0f)
        return;

    level = constrain(level, 0.0f, 1.0f);
    drawMagicEyeCore(this, xc, yc, eyePrepareEm34(r, level), bg, rowFrom, rowTo);
}

void UiCanvas::drawEm80MagicEye(int xc, int yc, float halfWidth, float halfHeight, float level, uint16_t ring, uint16_t bg, int rowFrom, int rowTo)
{
    // Innerhalb des Rahmens muss noch ein Leuchtschirm übrig bleiben.
    if (halfWidth < EM80_RING_THICKNESS + 2.0f || halfHeight < EM80_RING_THICKNESS + 2.0f)
        return;

    level = constrain(level, 0.0f, 1.0f);
    drawMagicEyeCore(this, xc, yc, eyeRingOnGround(eyePrepareEm80(halfWidth, halfHeight, level, ring), bg), bg, rowFrom, rowTo);
}

void UiCanvas::drawEm84MagicEye(int xc, int yc, float halfWidth, float halfHeight, float level, uint16_t ring, uint16_t bg, int rowFrom, int rowTo)
{
    // Der Eckenradius des Rahmens darf nicht größer sein als seine halben Maße.
    const float minHalf = EM84_RING_THICKNESS + EM84_CORNER + 1.0f;
    if (halfWidth < minHalf || halfHeight < minHalf)
        return;

    level = constrain(level, 0.0f, 1.0f);
    drawMagicEyeCore(this, xc, yc, eyeRingOnGround(eyePrepareEm84(halfWidth, halfHeight, level, ring), bg), bg, rowFrom, rowTo);
}

namespace
{
    const uint8_t *const FontData[] = {ComfortaaBold10, ComfortaaBold14, ComfortaaBold15, ComfortaaBold16, ComfortaaBold18, ComfortaaBold21, ComfortaaBold24, ComfortaaBold27, ComfortaaBold30, ComfortaaBold45, LeagueGothicCond34, LeagueGothicCond51, OrbitronLight48};
    constexpr unsigned FontCount = sizeof(FontData) / sizeof(FontData[0]);
    lgfx::PointerWrapper FontSources[FontCount];
    lgfx::VLWfont Fonts[FontCount];
    bool FontsReady = false;
}

bool UiCanvas::prepare(int width, int height)
{
    if (!FontsReady)
    {
        for (unsigned i = 0; i < FontCount; ++i)
        {
            FontSources[i].set(FontData[i]);
            if (!Fonts[i].loadFont(&FontSources[i]))
            {
                for (auto &loaded : Fonts)
                    loaded.unloadFont();
                return false;
            }
        }
        FontsReady = true;
    }
    setPsram(true);
    setColorDepth(16);
    setSwapBytes(true);
    setTextWrap(false);
    setTextSize(1);
    return createSprite(width, height) != nullptr;
}

void UiCanvas::loadFont(const uint8_t *font)
{
    for (unsigned i = 0; i < FontCount; ++i)
        if (font == FontData[i])
        {
            setTextSize(1);
            setFont(&Fonts[i]);
            return;
        }
    setFont(nullptr);
}

float getIntensityFrom565(uint16_t color)
{
    const uint8_t r8 = (((color >> 11) & 31) * 527 + 23) >> 6, g8 = (((color >> 5) & 63) * 259 + 33) >> 6, b8 = ((color & 31) * 527 + 23) >> 6;
    return (0.2126f * r8 + 0.7152f * g8 + 0.0722f * b8) / 255.0f;
}

void UiCanvas::drawSmoothEllipse(int xc, int yc, float rx, float ry, float thickness, uint16_t color, uint16_t bg)
{
    drawSmoothEllipseCore(xc, yc, rx, ry, thickness, color, bg, [this](int x, int y, uint16_t col)
    {
        this->drawPixelClipped(x, y, col);
    });
}

void UiCanvas::blendColumnFrom(UiCanvas &source, int sx, int sy, int dx, int dy, int h, uint16_t color, float alpha)
{
    // RGB565-Sprites liegen als swap565 mit vertauschten Bytes im Speicher. Die Pixelschnittstelle von LovyanGFX (readPixel/drawPixel) brauchte für 900 Pixel rund 5 ms, der direkte Weg unter 0,2 ms.
    const auto *from = static_cast<const uint16_t *>(source.getBuffer()) + sy * source.width() + sx;
    auto *to = static_cast<uint16_t *>(getBuffer()) + dy * width() + dx;
    for (int i = 0; i < h; ++i, from += source.width(), to += width())
        *to = __builtin_bswap16(blendColor(color, __builtin_bswap16(*from), alpha));
}

void UiCanvas::columnsToRows(uint16_t *target, int x0, int count, bool reverse)
{
    constexpr int Tile = 32;
    uint16_t tile[Tile * Tile];
    const auto *pixels = static_cast<const uint16_t *>(getBuffer());
    const int w = width(), h = height();
    for (int sx = 0; sx < count; sx += Tile)
    {
        const int columns = std::min(Tile, count - sx);
        for (int sy = 0; sy < h; sy += Tile)
        {
            const int rows = std::min(Tile, h - sy);
            // Kachelweise zusammenhängend lesen, im SRAM transponieren und den Bytetausch der Spritedarstellung aufheben, dann zusammenhängend schreiben.
            for (int row = 0; row < rows; ++row)
            {
                const uint16_t *input = pixels + (sy + row) * w + x0 + sx;
                for (int column = 0; column < columns; ++column)
                    tile[column * Tile + row] = __builtin_bswap16(input[column]);
            }
            for (int column = 0; column < columns; ++column)
            {
                uint16_t *output = target + (sx + column) * h;
                for (int row = 0; row < rows; ++row)
                    output[reverse ? h - 1 - sy - row : sy + row] = tile[column * Tile + row];
            }
        }
    }
}
