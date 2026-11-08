#include "Visualizer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <esp_heap_caps.h>

#include "StringUtils.h"
#include "Logger.h"
#include "Thema.h"

#include "I18n.h"
extern const __FlashStringHelper *i18n_getRetroRadio(RetroRadioKey, LangIndex);
#include "Strings_retroradio.h"
#define STR(key) i18n_getRetroRadio(key, m_language)

#include "fonts/ComfortaaBold10.h"
#include "fonts/ComfortaaBold15.h"

#include "images/retro_vu_meter.h"
#include "images/retro_vu_meter_green.h"
#include "images/retro_vu_meter_327.h"
#include "images/retro_vu_meter_green_327.h"
#include "images/retro_vu_meter_hell_327.h"
#include "images/retro_vu_meter_green_hell_327.h"

namespace
{
    constexpr float VU_RAD_TO_DEG = 57.29578f; ///< Umrechnung von Radiant in Grad
    constexpr uint8_t VU_RAINBOW_GREEN = 64; ///< Grüner Startpunkt des VU-Farbverlaufs bis Rot bei Index 0

    // Lage und Länge der Nadel, wie das Sprite und sein Drehpunkt sie vorgeben. Daraus folgt, welchen Bereich sie
    // in einer Bildzeile bedeckt. Originalwerte für das 218x70-Blatt, VuGeometry leitet die Werte der jeweiligen Zeichenfläche ab.
    constexpr int VU_PLATE_W = 218;               ///< Breite des Skalenblatts
    constexpr int VU_PLATE_H = 70;                ///< Höhe des Skalenblatts
    constexpr int VU_PLATE_H_MAX = 105;           ///< Höhe des nativen 1,5-fachen Blatts, bemisst die Spannenpuffer
    constexpr float VU_PIVOT_X = 109.0f;          ///< Drehpunkt der Nadel im Blatt
    constexpr float VU_PIVOT_Y = 120.0f;          ///< Der Drehpunkt liegt unter dem Blatt
    constexpr float VU_NEEDLE_INNER_R = 10.0f;    ///< Innerer Radius der Nadel
    constexpr float VU_NEEDLE_OUTER_R = 130.0f;   ///< Äußerer Radius der Nadel
    constexpr float VU_NEEDLE_HALF_W = 3.5f;      ///< Halbe Breite samt weicher Kante, großzügig

    constexpr float VU_NEEDLE_R_MARGIN = 5.0f; ///< Zuschlag auf die Nadelradien: Der Radius wird an der Mittellinie gemessen, die Ecken des Balkens reichen darüber hinaus. Ohne ihn blieben an Spitze und Fuß ein bis zwei Zeilen ungelöscht

    // Die Spannen liegen fest im Speicher statt auf dem Stapel: Sie werden nur nacheinander gebraucht,
    // und der Zeichenweg läuft ausschließlich im Oberflächentask. Alt und Neu: Nadelstellung im vorigen und im neuen Bild.
    int16_t g_vuSpanAltVon[VU_PLATE_H_MAX], g_vuSpanAltBis[VU_PLATE_H_MAX];
    int16_t g_vuSpanNeuVon[VU_PLATE_H_MAX], g_vuSpanNeuBis[VU_PLATE_H_MAX];

    /** @brief Maße von Skalenblatt und Nadel auf der Zeichenfläche in Pixeln. */
    struct VuGeometry
    {
        int plateW, plateH;              ///< Blattgröße
        float pivotX, pivotY;            ///< Drehpunkt der Nadel im Blatt
        float innerR, outerR, halfW, margin; ///< Nadelradien, halbe Breite und Zuschlag
    };

    /**
     * @brief Ermittelt je Bildzeile, welchen Bereich die Nadel bei diesem Winkel bedeckt.
     *
     * Liegt in einer Zeile keine Nadel, weil sie dort ihren Radius verlassen hat, wird die Spanne leer gemeldet: bis liegt dann vor von.
     * @param g Maße des Blatts und der Nadel
     * @param angleDeg Zeigerwinkel in Grad, 0 zeigt senkrecht nach oben
     * @param von Ausgabe: erste Spalte je Zeile, g.plateH Einträge
     * @param bis Ausgabe: letzte Spalte je Zeile, g.plateH Einträge
     */
    void vuNeedleSpans(const VuGeometry &g, float angleDeg, int16_t *von, int16_t *bis)
    {
        const float rad = angleDeg / VU_RAD_TO_DEG;
        const float sinus = sinf(rad);
        const float kosinus = cosf(rad);

        for (int y = 0; y < g.plateH; ++y)
        {
            const float dy = g.pivotY - static_cast<float>(y);
            const float radius = dy / kosinus;

            if (radius < g.innerR - g.margin ||
                radius > g.outerR + g.margin)
            {
                von[y] = 1;
                bis[y] = 0;
                continue;
            }

            const float mitte = g.pivotX + dy * (sinus / kosinus);
            const float halb = g.halfW / kosinus + 1.0f;

            int a = static_cast<int>(floorf(mitte - halb));
            int b = static_cast<int>(ceilf(mitte + halb));

            if (a < 0)
                a = 0;

            if (b > g.plateW - 1)
                b = g.plateW - 1;

            von[y] = static_cast<int16_t>(a);
            bis[y] = static_cast<int16_t>(b);
        }
    }

    // Fenster der Spektrumanzeige: Bei Hann-Fenster und 1024 Punkten liegt die Summe aller Bänder rund 53 dB über
    // dem Effektivwert in dBFS, das stärkste Einzelband einige Dezibel darunter. Die Oberkante liegt deshalb 50 dB
    // über dem Bezugspegel des VU-Meters, damit passen Spektrum und Zeiger zum selben Programm.
    constexpr float SPECTRUM_TOP_OVER_REFERENCE = 50.0f; ///< Oberkante des Spektrumfensters über dem VU-Bezugspegel in dB
    constexpr float SPECTRUM_SPAN_DB = 36.0f;            ///< Höhe des Spektrumfensters in dB
    constexpr float SPECTRUM_MIN_HZ = 86.0f; ///< Untere Frequenzgrenze aller Spektren
    constexpr float SPECTRUM_MAX_HZ = 12987.0f; ///< Obere Frequenzgrenze aller Spektren
    constexpr float SPECTRUM_SCALE_FACTOR = 32.66f / 32767.0f; ///< Pegelskalierung der FFT-Eingangswerte

    // Bezugspegel in dBFS für 0 VU je Empfindlichkeitsstufe 0..4. Kleinere Werte lassen den Zeiger bei gleichem Programmpegel weiter ausschlagen, dieselben Werte stehen auf der Einstellseite.
    constexpr float VU_ALIGNMENT_DB[] = {-2.0f, -6.0f, -10.0f, -14.0f, -18.0f};

    // Zeitparameter des Feder-Dämpfer-Modells in Sekunden, in der Reihenfolge von VuBallistics
    constexpr float VU_RESPONSE_TIME[] = {0.15f, 0.30f, 0.60f};
    static_assert(sizeof(VU_RESPONSE_TIME) / sizeof(VU_RESPONSE_TIME[0]) == static_cast<size_t>(VuBallistics::COUNT),
                  "Zu jeder Trägheit gehört genau eine Einschwingzeit");

    // Feder-Dämpfer-Modell für Winkel und Winkelgeschwindigkeit. Der Dämpfungsgrad 0,81 erlaubt leichtes Überschwingen. Die Kreisfrequenz ergibt sich aus 4,03 geteilt durch den gewählten Zeitparameter.
    constexpr float VU_DAMPING = 0.81f;           ///< Dämpfungsgrad
    constexpr float VU_OMEGA_TIMES_TIME = 4.03f;  ///< Kreisfrequenz mal Zeitparameter

    // Die Bewegungsgleichung wird schrittweise integriert. Zu große Schritte machen die Rechnung
    // unruhig, deshalb wird ein langes Bildintervall in mehrere kleine zerlegt.
    constexpr float VU_MAX_STEP = 0.005f; ///< Größter Integrationsschritt in Sekunden
    constexpr int VU_MAX_STEPS = 32;      ///< Höchstzahl der Schritte je Bild
}

/**
 * @brief Byte-Swap für 16-Bit Farben (RGB565 Big-Endian <-> Little-Endian)
 */
inline uint16_t bswap16(uint16_t color)
{
    return (color >> 8) | (color << 8);
}

/**
 * @brief RAII-Guard für die Lesemarkierung des Samplepuffers
 *
 * Setzt m_readerActive auf true bei Konstruktion und automatisch auf false bei Destruktion (auch bei frühem return).
 *
 * Die atomare Markierung ist keine gegenseitige Verriegelung, Sampleübergabe und Zeichnen werden im Tab5-UI-Task serialisiert.
 */
class ReaderGuard
{
public:
    /**
     * @brief Markiert den zugehörigen Lesepfad für die Lebensdauer des Guards als aktiv.
     */
    explicit ReaderGuard(std::atomic<bool> &flag) : m_flag(flag)
    {
        m_flag.store(true, std::memory_order_release);
    }

    /**
     * @brief Hebt die Markierung des Lesepfads wieder auf.
     */
    ~ReaderGuard()
    {
        m_flag.store(false, std::memory_order_release);
    }

    ReaderGuard(const ReaderGuard &) = delete;
    ReaderGuard &operator=(const ReaderGuard &) = delete;

private:
    std::atomic<bool> &m_flag;
};

Visualizer::Visualizer() = default;

Visualizer::~Visualizer()
{
    cleanupCurrentVisSprites();

    if (m_fft_cfg)
    {
        kiss_fftr_free(m_fft_cfg);
        m_fft_cfg = nullptr;
    }
}

void Visualizer::cleanup()
{
    LOG_INFO_F("Visualizer::cleanup() - Modus war: %d", static_cast<int>(m_visMode));
    cleanupCurrentVisSprites();

    if (m_fft_cfg)
    {
        kiss_fftr_free(m_fft_cfg);
        m_fft_cfg = nullptr;
    }

    m_fftWindowInitialized = false;
    m_spectrumSampleRate = 0;
    m_waterfall.reset();
    m_aurora.reset();
    for (uint8_t i = 0; i < SAMPLE_BUFFER_COUNT; ++i)
    {
        m_samplesL[i].reset();
        m_samplesR[i].reset();
        m_sampleCount[i] = 0;
        m_sampleRate[i] = 0;
    }
    m_spectrum_bands.reset();
    m_font.unloadFont();
    m_tft = nullptr;
}

bool Visualizer::init(Gfx &tft, const VisualizerGeometry &geometry)
{
    if (m_tft)
        return false;
    // Skalenblätter und Schriften liegen nur für diese beiden Faktoren vor
    if (geometry.scale != 1.0f && geometry.scale != 1.5f)
        return false;
    m_geometry = geometry;
    // Die Zeichenfläche muss den gesamten Originalbereich (341..800, 354..480) aufnehmen
    if (gx(341) < 0 || gy(354) < 0 || tft.width() < gx(800) || tft.height() < gy(480))
        return false;
    m_areaX = gx(VIS_AREA_X);
    m_areaY = gy(VIS_AREA_Y);
    m_areaW = gx(VIS_AREA_X + VIS_AREA_W) - m_areaX;
    m_areaH = gy(VIS_AREA_Y + VIS_AREA_H) - m_areaY;
    if (m_areaW > MaxWidth || m_areaH > MaxHeight)
        return false;
    m_labelH = gl(SPECTRUM_LABEL_H);
    // Gleiche sichtbare Zeitspanne: Die Zeilen wachsen mit dem Faktor, also wird ihr Takt entsprechend kürzer
    m_waterfallFrameMs = static_cast<uint32_t>(lroundf(WATERFALL_FRAME_MS / m_geometry.scale));
    // Ebenso bei der Wellenform: mehr Spalten, kürzerer Spaltentakt, ein Durchlauf bleibt bei rund 5,3 s
    m_waveColumnMs = static_cast<uint32_t>(lroundf(WAVE_COLUMN_MS / m_geometry.scale));
    {
        const auto &o = CONFIG_RETRORADIO;
        const int size = gx(o.posX + o.size) - gx(o.posX);
        const int barX = size * 5 / 126;
        const int barW = gl(o.corrBarW);
        m_gonio = {static_cast<int16_t>(gx(o.posX)), static_cast<int16_t>(gy(o.posY)), static_cast<uint8_t>(size), static_cast<uint8_t>(size / 2),
                   static_cast<uint8_t>(gl(o.scale)), static_cast<uint8_t>(gl(o.corrBarY)), static_cast<uint8_t>(barW),
                   static_cast<uint8_t>(barX + barW / 2), static_cast<uint8_t>(gl(o.corrRange))};
    }
    m_smallFont = native() ? static_cast<const lgfx::IFont *>(&fonts::DejaVu12) : static_cast<const lgfx::IFont *>(&fonts::DejaVu9);
    m_tft = &tft;
    // PSRAM ist erst nach dem Boardstart verlässlich verfügbar, deshalb hier und nicht im Konstruktor
    for (uint8_t i = 0; i < SAMPLE_BUFFER_COUNT; ++i)
    {
        m_samplesL[i].reset(static_cast<int16_t *>(heap_caps_malloc(MAX_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
        m_samplesR[i].reset(static_cast<int16_t *>(heap_caps_malloc(MAX_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));

        if (m_samplesL[i] && m_samplesR[i])
        {
            memset(m_samplesL[i].get(), 0, MAX_SAMPLES * sizeof(int16_t));
            memset(m_samplesR[i].get(), 0, MAX_SAMPLES * sizeof(int16_t));
        }

        m_sampleCount[i] = 0;
    }

    m_spectrum_bands.reset(static_cast<SpectrumBand *>(heap_caps_malloc(64 * sizeof(SpectrumBand), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));

    if (m_spectrum_bands)
        for (int i = 0; i < 64; i++)
            m_spectrum_bands[i] = {0.0f, 0.0f, 0, 0, 0};

    for (uint8_t i = 0; i < SAMPLE_BUFFER_COUNT; ++i)
        if (!m_samplesL[i] || !m_samplesR[i])
            return false;
    if (!m_spectrum_bands)
        return false;
    m_fontData.set(native() ? ComfortaaBold15 : ComfortaaBold10);
    if (!m_font.loadFont(&m_fontData))
        return false;

    m_visTouchState = VisTouchState::IDLE;
    m_visTouchStartX = 0;
    m_visTouchLastX = 0;
    m_visTouchStartTime = 0;

    // Gespeicherte Modi und Stile übernimmt anschließend der zentrale Dienst über applyPreferences()
    m_currentRMSLeft = 0.0f;
    m_currentRMSRight = 0.0f;
    m_currentPeakLeft = 0.0f;
    m_currentPeakRight = 0.0f;

    m_needleInit = false;
    m_pastValL = -VU_MAX_ANGLE;
    m_pastValR = -VU_MAX_ANGLE;
    m_needleVelL = 0.0f;
    m_needleVelR = 0.0f;
    m_lastRenderedL = -VU_MAX_ANGLE;
    m_lastRenderedR = -VU_MAX_ANGLE;
    m_lastUpdate_analog = 0;

    for (int i = 0; i < 2; ++i)
    {
        m_rmsChannels[i] = {VU_DB_LOW, 0, 0, 0};
        m_ppmChannels[i] = {VU_DB_LOW, 0, 0, 0};
    }

    m_lastUpdate_digital = 0;

    m_writeBufferIdx.store(0);
    m_readerActive.store(false);
    for (uint8_t i = 0; i < SAMPLE_BUFFER_COUNT; ++i)
    {
        m_sampleCount[i] = 0;
        m_sampleRate[i] = 0;
    }
    m_lastSpectrumUpdate = 0;

    m_waveNextX = -1;

    for (uint8_t i = 0; i < GONIO_PEAK_HISTORY_SIZE; i++)
        m_gonioPeakHistory[i] = 0.0f;

    m_gonioPeakHistoryIdx = 0;
    m_gonioSlidingMax = 0.0f;
    m_gonioLastPeakUpdate = 0;
    m_gonioLastDynamicsUpdate = 0;
    m_gonioAmp = 1.0f;
    m_gonioCorrPrev = 0.0f;

    // Alle Modi beim Start vorbereiten, damit während Touch und Zeichnen keine erste Heap-Allokation anfällt
    for (int mode = 0; mode < VIS_MODE_COUNT; ++mode)
    {
        m_visMode = static_cast<VisMode>(mode);
        initVisMode(m_visMode);
    }
    if (!m_sprVU_Analog_Needle || !m_sprVU_Analog_L || !m_sprVU_Analog_R ||
        !m_sprVU_Digital_RMS_L || !m_sprVU_Digital_RMS_R || !m_sprVU_Digital_PPM_L || !m_sprVU_Digital_PPM_R ||
        !m_sprVU_Spectrum || !m_fft_cfg || !m_waterfall || !m_aurora || !m_sprAurora || !m_sprVU_Wave ||
        !m_sprGonio || !m_sprGonioBg || !m_sprGonioInfo || !m_sprGonioVULeft || !m_sprGonioVUMiddle || !m_sprGonioVURight)
        return false;
    m_visMode = VisMode::VIS_ANALOG;
    initVisMode(m_visMode);
    LOG_INFO_F("Visualizer: acht originale Modi vorbereitet, %dx%d.", m_areaW, m_areaH);
    return true;
}

void Visualizer::setAudioValues(float rmsLeft, float rmsRight, float peakLeft, float peakRight)
{
    portENTER_CRITICAL(&m_audioMux);
    m_currentRMSLeft = rmsLeft;
    m_currentRMSRight = rmsRight;
    m_currentPeakLeft = peakLeft;
    m_currentPeakRight = peakRight;
    portEXIT_CRITICAL(&m_audioMux);
}

void Visualizer::setSampleData(const int16_t *samplesL, const int16_t *samplesR, uint16_t count, uint32_t sampleRate)
{
    if (!samplesL || !samplesR || count == 0)
        return;

    // Der Writer schreibt in den inaktiven Puffer und tauscht ihn danach atomar gegen den Lesepuffer
    const uint8_t writeIdx = m_writeBufferIdx.load(std::memory_order_relaxed);

    if (!m_samplesL[writeIdx] || !m_samplesR[writeIdx])
        return;

    const uint32_t srcCount = count;

    const uint16_t samplesToCopy = (srcCount > MAX_SAMPLES) ? MAX_SAMPLES : static_cast<uint16_t>(srcCount);

    // Bei mehr als MAX_SAMPLES Samples zählen die neuesten
    const uint32_t start = (srcCount > MAX_SAMPLES) ? (srcCount - MAX_SAMPLES) : 0;

    memcpy(m_samplesL[writeIdx].get(), samplesL + start, samplesToCopy * sizeof(int16_t));
    memcpy(m_samplesR[writeIdx].get(), samplesR + start, samplesToCopy * sizeof(int16_t));
    m_sampleCount[writeIdx] = samplesToCopy;
    m_sampleRate[writeIdx] = sampleRate;

    // Bei beobachteter Lesemarkierung den kopierten Puffer noch nicht veröffentlichen, ein späterer Aufruf kann ihn überschreiben.
    if (m_readerActive.load(std::memory_order_acquire))
        return;

    // memory_order_release stellt sicher, dass alle vorherigen Schreibzugriffe abgeschlossen sind, bevor der Tausch sichtbar wird
    m_writeBufferIdx.store(writeIdx ^ 1, std::memory_order_release);

    // Die Frischemarke kennzeichnet den veröffentlichten Samplepuffer für die nachfolgende Zeichnung
    m_sampleSeq.fetch_add(1, std::memory_order_release);
}

void Visualizer::handleTouch(int touchX, int touchY, bool touched)
{
    static constexpr int SWIPE_THRESHOLD = 50;
    static constexpr unsigned long SWIPE_MAX_TIME = 500;
    static constexpr unsigned long LONG_PRESS_TIME = 800;

    // Nur ein frisches Aufsetzen darf eine Geste beginnen. Eine laufende Berührung, die aus Playlist-Drag oder Scrubbing hereinwandert, nicht
    const bool freshTouch = touched && !m_visTouchSeen;
    m_visTouchSeen = touched;

    // Der Bereich schließt die obere Grenze aus: [X, X+W) und [Y, Y+H)
    const bool inVisArea = touched &&
                           touchX >= m_areaX &&
                           touchX < m_areaX + m_areaW &&
                           touchY >= m_areaY &&
                           touchY < m_areaY + m_areaH;

    switch (m_visTouchState)
    {
    case VisTouchState::IDLE:
        if (inVisArea && freshTouch)
        {
            m_visTouchState = VisTouchState::TOUCH_STARTED;
            m_visTouchStartX = touchX;
            m_visTouchLastX = touchX;
            m_visTouchStartTime = millis();
        }
        break;

    case VisTouchState::TOUCH_STARTED:
        if (!touched)
        {
            const unsigned long touchDuration = millis() - m_visTouchStartTime;
            const int swipeDistance = m_visTouchLastX - m_visTouchStartX;

            // Die Wischstrecke wächst mit der Geometrie, damit sie auf dem Display gleich lang bleibt
            if (touchDuration < SWIPE_MAX_TIME && abs(swipeDistance) > gl(SWIPE_THRESHOLD))
                switchVisMode(swipeDistance > 0 ? 1 : -1);

            m_visTouchState = VisTouchState::IDLE;
        }
        else if (inVisArea && millis() - m_visTouchStartTime > LONG_PRESS_TIME)
        {
            toggleVisSubMode();
            m_visTouchState = VisTouchState::LONG_PRESS_DONE;
        }
        else if (inVisArea)
        {
            m_visTouchLastX = touchX;
        }
        // Außerhalb des Bereichs bleibt die letzte Position stehen, damit das Loslassen noch als Wischen zählt
        break;

    case VisTouchState::LONG_PRESS_DONE:
        if (!touched)
            m_visTouchState = VisTouchState::IDLE;

        break;
    }
}

void Visualizer::draw(bool init)
{
    // Vor erfolgreichem init() ist kein Zeichenpuffer zugeordnet
    if (!m_tft)
        return;

    // Die vier Audiopegel werden unter dem Spinlock kopiert. Zeichenpuffer, Modus und Samples gehören dem serialisierten Oberflächenpfad
    float rmsLeft, rmsRight, peakLeft, peakRight;
    portENTER_CRITICAL(&m_audioMux);
    rmsLeft = m_currentRMSLeft;
    rmsRight = m_currentRMSRight;
    peakLeft = m_currentPeakLeft;
    peakRight = m_currentPeakRight;
    portEXIT_CRITICAL(&m_audioMux);

    drawVis(init, rmsLeft, rmsRight, peakLeft, peakRight);
}

void Visualizer::setMode(VisMode mode)
{
    if (static_cast<unsigned>(mode) >= VIS_MODE_COUNT || m_visMode == mode)
        return;

    m_visMode = mode;

    initVisMode(m_visMode);
}

void Visualizer::switchVisMode(int direction)
{
    int newMode = (static_cast<int>(m_visMode) + direction) % VIS_MODE_COUNT;

    if (newMode < 0)
        newMode += VIS_MODE_COUNT;

    setMode(static_cast<VisMode>(newMode));

    LOG_INFO_F("VU-Modus gewechselt zu: %d", static_cast<int>(m_visMode));
}

void Visualizer::toggleVisSubMode()
{
    switch (m_visMode)
    {
    case VisMode::VIS_GONIO:
        m_gonioStyle = (m_gonioStyle == GonioStyle::CLASSIC)
                           ? GonioStyle::RAINBOW
                           : GonioStyle::CLASSIC;

        LOG_INFO_F("Goniometer-Stil gewechselt zu: %d", static_cast<int>(m_gonioStyle));
        break;

    case VisMode::VIS_WAVE:
        m_waveformStyle = (m_waveformStyle == WaveformStyle::OVERLAY)
                              ? WaveformStyle::STACKED
                              : WaveformStyle::OVERLAY;

        m_waveNextX = -1;
        LOG_INFO_F("Wellenform-Stil gewechselt zu: %d", static_cast<int>(m_waveformStyle));
        break;

    case VisMode::VIS_SPECTRUM:
        m_spectrumStyle = (m_spectrumStyle == SpectrumStyle::CLASSIC)
                              ? SpectrumStyle::RAINBOW
                              : SpectrumStyle::CLASSIC;
        m_centeredSpectrumDirty = true;

        LOG_INFO_F("Spektrum-Stil gewechselt zu: %d", static_cast<int>(m_spectrumStyle));
        break;

    case VisMode::VIS_SPECTRUM_CENTERED:
        m_centeredSpectrumStyle = (m_centeredSpectrumStyle == SpectrumStyle::CLASSIC)
                                      ? SpectrumStyle::RAINBOW
                                      : SpectrumStyle::CLASSIC;
        m_centeredSpectrumDirty = true;
        break;

    case VisMode::VIS_WATERFALL:
        m_waterfallStyle = (m_waterfallStyle == SpectrumStyle::CLASSIC)
                               ? SpectrumStyle::RAINBOW
                               : SpectrumStyle::CLASSIC;
        // Die Farbskala gilt für den gesamten Verlauf, deshalb werden alte Zeilen beim Wechsel gelöscht
        m_spectrumHadSamples = false;
        m_lastSpectrumUpdate = millis();
        drawWaterfall(true, false, false, 0);
        break;

    case VisMode::VIS_AURORA:
        m_auroraStyle = m_auroraStyle == AuroraStyle::COOL ? AuroraStyle::WARM : AuroraStyle::COOL;
        break;

    case VisMode::VIS_DIGITAL:
        m_digitalVUStyle = (m_digitalVUStyle == DigitalVUStyle::RAINBOW)
                               ? DigitalVUStyle::CLASSIC
                               : DigitalVUStyle::RAINBOW;

        LOG_INFO_F("Digital-VU-Stil gewechselt zu: %d", static_cast<int>(m_digitalVUStyle));
        break;

    case VisMode::VIS_ANALOG:
        m_analogVUStyle = (m_analogVUStyle == AnalogVUStyle::CLASSIC) ? AnalogVUStyle::GREEN
                                                                      : AnalogVUStyle::CLASSIC;

        LOG_INFO_F("Analog-VU-Fassung gewechselt zu: %d", static_cast<int>(m_analogVUStyle));

        // Anders als bei den übrigen Stilen hängt hier auch die Farbe des Zeigersprites am Stil,
        // er wird nur beim Init gefüllt, deshalb den Modus neu aufsetzen
        initVisMode(m_visMode);
        break;
    }
}

void Visualizer::initVisMode(VisMode mode)
{
    if (m_tft)
    {
        m_tft->fillScreen(farbe(Rolle::GemGrund));
        m_canvasWritten = true;
    }
    switch (mode)
    {
    case VisMode::VIS_ANALOG:
        drawAnalogVU(true, 0.0f, 0.0f);
        break;
    case VisMode::VIS_DIGITAL:
        drawDigitalVU(true, 0.0f, 0.0f, 0.0f, 0.0f);
        break;
    case VisMode::VIS_SPECTRUM:
    case VisMode::VIS_SPECTRUM_CENTERED:
    case VisMode::VIS_WATERFALL:
        drawSpectrum(true);
        break;
    case VisMode::VIS_AURORA:
        drawAurora(true, 0.0f, 0.0f);
        break;
    case VisMode::VIS_WAVE:
        drawWave(true);
        break;
    case VisMode::VIS_GONIO:
        drawGonio(true, 0.0f, 0.0f);
        drawGonioVU(true, 0.0f, 0.0f);
        break;
    }
}

void Visualizer::clearVisualizationArea()
{
    if (!m_tft)
        return;

    const uint16_t ground = farbe(Rolle::GemGrund);
    m_canvasWritten = true;
    m_tft->fillRect(gx(VIS_AREA_X - 10), m_areaY, m_areaX + m_areaW - gx(VIS_AREA_X - 10), m_areaH, ground);

    // Der Goniometerbereich ist der größte Bereich, den Goniometer samt VU-Meter belegen kann
    const auto &o = CONFIG_RETRORADIO;
    m_tft->fillRect(m_gonio.posX, m_gonio.posY,
                    gx(o.posX + o.size + 38) - m_gonio.posX, m_gonio.size, ground);
}

void Visualizer::drawVis(bool init, float rmsLeft, float rmsRight, float peakLeft, float peakRight)
{
    switch (m_visMode)
    {
    case VisMode::VIS_ANALOG:
        drawAnalogVU(init, rmsLeft, rmsRight);
        break;
    case VisMode::VIS_DIGITAL:
        drawDigitalVU(init, rmsLeft, rmsRight, peakLeft, peakRight);
        break;
    case VisMode::VIS_SPECTRUM:
    case VisMode::VIS_SPECTRUM_CENTERED:
    case VisMode::VIS_WATERFALL:
        drawSpectrum(init);
        break;
    case VisMode::VIS_AURORA:
        drawAurora(init, rmsLeft, rmsRight);
        break;
    case VisMode::VIS_WAVE:
        drawWave(init);
        break;
    case VisMode::VIS_GONIO:
        drawGonio(init, peakLeft, peakRight);
        // Der Balken zeigt den Effektivwert, wie das analoge und das digitale Instrument auch.
        // Die Spitzenwerte bleiben den beiden Lampen unter dem Verstärkungsfaktor vorbehalten.
        drawGonioVU(init, rmsLeft, rmsRight);
        break;
    }
}

void Visualizer::cleanupCurrentVisSprites()
{
    if (!m_tft)
        return;

    m_tft->safeDeleteSprite(m_sprVU_Analog_Needle);
    m_tft->safeDeleteSprite(m_sprVU_Analog_L);
    m_tft->safeDeleteSprite(m_sprVU_Analog_R);

    m_tft->safeDeleteSprite(m_sprVU_Digital_RMS_L);
    m_tft->safeDeleteSprite(m_sprVU_Digital_RMS_R);
    m_tft->safeDeleteSprite(m_sprVU_Digital_PPM_L);
    m_tft->safeDeleteSprite(m_sprVU_Digital_PPM_R);

    m_tft->safeDeleteSprite(m_sprVU_Spectrum);
    m_tft->safeDeleteSprite(m_sprAurora);
    m_tft->safeDeleteSprite(m_sprVU_Wave);
    m_tft->safeDeleteSprite(m_sprGonio);
    m_tft->safeDeleteSprite(m_sprGonioBg);
    m_tft->safeDeleteSprite(m_sprGonioVULeft);
    m_tft->safeDeleteSprite(m_sprGonioVUMiddle);
    m_tft->safeDeleteSprite(m_sprGonioVURight);

    // Die Schrift gehört dem Visualizer und darf nicht mit dem Sprite freigegeben werden
    if (m_sprGonioInfo)
        m_sprGonioInfo->setFont(nullptr);

    m_tft->safeDeleteSprite(m_sprGonioInfo);
}

float Visualizer::amplitudeToDb(float amplitude) const
{
    if (amplitude <= 1e-5f)
        return VU_DB_LOW;

    float db = 20.0f * log10f(amplitude) + VU_CALIBRATION_DB;

    if (db < VU_DB_LOW)
        db = VU_DB_LOW;

    if (db > VU_DB_HIGH)
        db = VU_DB_HIGH;

    return db;
}

float Visualizer::vuAlignmentDb() const
{
    constexpr size_t anzahl = sizeof(VU_ALIGNMENT_DB) / sizeof(VU_ALIGNMENT_DB[0]);
    return VU_ALIGNMENT_DB[(m_vuSensitivity < anzahl) ? m_vuSensitivity : anzahl / 2];
}

const uint16_t *Visualizer::analogVUPlate() const
{
    // Themen mit hellem Grund zeigen die hellen Papierblätter, die 218x70-Blätter gibt es nur dunkel.
    if (native() && (themaAktiv().merkmale & MerkmalHellerGrund))
        return (m_analogVUStyle == AnalogVUStyle::GREEN) ? retro_vu_meter_green_hell_327 : retro_vu_meter_hell_327;
    if (native())
        return (m_analogVUStyle == AnalogVUStyle::GREEN) ? retro_vu_meter_green_327 : retro_vu_meter_327;
    return (m_analogVUStyle == AnalogVUStyle::GREEN) ? retro_vu_meter_green : retro_vu_meter;
}

float Visualizer::dbToNeedleAngle(float db) const
{
    float share = VU_ZERO_SHARE * powf(10.0f, (db - vuAlignmentDb()) / 20.0f);

    if (share > 1.0f)
        share = 1.0f;

    if (share < 0.0f)
        share = 0.0f;

    return atanf((2.0f * share - 1.0f) * VU_TAN_MAX) * VU_RAD_TO_DEG;
}

void Visualizer::drawAnalogVU(bool init, float rmsLeft, float rmsRight)
{
    if (!m_tft)
        return;

    if (!m_playing)
        rmsLeft = rmsRight = 0.0f;

    const int vuX1 = gx(355), vuY1 = gy(355), vuX2 = gx(582), vuY2 = gy(355);
    constexpr float redrawThreshold = 0.5f;
    // Blattgröße über die Endpunkte, Nadelmaße mit dem Faktor gestreckt. Bei 1,5 entspricht das dem 327x105-Blatt in include/images/retro_vu_meter_327.h
    const VuGeometry vg = {gx(355 + VU_PLATE_W) - vuX1, gy(355 + VU_PLATE_H) - vuY1, gf(VU_PIVOT_X), gf(VU_PIVOT_Y),
                           gf(VU_NEEDLE_INNER_R), gf(VU_NEEDLE_OUTER_R), gf(VU_NEEDLE_HALF_W), gf(VU_NEEDLE_R_MARGIN)};
    // Die Nadel ist 3 Pixel breit mit Drehpunkt in der rechten Spalte, bei 1,5 vier Pixel, damit Breite und Versatz zur Achse mitwachsen
    const int needleW = native() ? 4 : 3;

    unsigned long currentTime = millis();
    float deltaTime = (currentTime - m_lastUpdate_analog) / 1000.0f;

    if (init)
    {
        m_needleInit = true;
        m_pastValL = m_pastValR = m_lastRenderedL = m_lastRenderedR = -VU_MAX_ANGLE;
        m_needleVelL = m_needleVelR = 0.0f;
        m_lastUpdate_analog = currentTime;

        if (!m_sprVU_Analog_Needle)
        {
            if (!m_tft->safeCreateSprite(m_sprVU_Analog_Needle, needleW, gl(120), false))
            {
                LOG_ERROR("Konnte m_sprVU_Analog_Needle Sprite nicht erstellen!");
                return;
            }
        }

        // Auf dem grünen Blatt steht der Zeiger in Bernstein: Ein Zeiger im Farbton seiner Skala tritt nicht hervor, sondern konkurriert mit ihr.
        m_sprVU_Analog_Needle->clear(farbe((m_analogVUStyle == AnalogVUStyle::GREEN) ? Rolle::VisNadelGruen
                                                                                     : Rolle::VisNadelKlassisch));
        m_sprVU_Analog_Needle->setPivot(needleW - 1, gf(130));

        if (!m_sprVU_Analog_L)
        {
            if (!m_tft->safeCreateSprite(m_sprVU_Analog_L, vg.plateW, vg.plateH))
            {
                LOG_ERROR("Konnte m_sprVU_Analog_L Sprite nicht erstellen!");
                return;
            }
        }
        m_sprVU_Analog_L->setPivot(vg.pivotX, vg.pivotY);

        if (!m_sprVU_Analog_R)
        {
            if (!m_tft->safeCreateSprite(m_sprVU_Analog_R, vg.plateW, vg.plateH))
            {
                LOG_ERROR("Konnte m_sprVU_Analog_R Sprite nicht erstellen!");
                return;
            }
        }
        m_sprVU_Analog_R->setPivot(vg.pivotX, vg.pivotY);

        clearVisualizationArea();

        m_sprVU_Analog_L->pushImage(0, 0, vg.plateW, vg.plateH, analogVUPlate());
        m_sprVU_Analog_Needle->pushRotatedWithAA(m_sprVU_Analog_L, -VU_MAX_ANGLE);
        pushToCanvas(m_sprVU_Analog_L, vuX1, vuY1);

        m_sprVU_Analog_R->pushImage(0, 0, vg.plateW, vg.plateH, analogVUPlate());
        m_sprVU_Analog_Needle->pushRotatedWithAA(m_sprVU_Analog_R, -VU_MAX_ANGLE);
        pushToCanvas(m_sprVU_Analog_R, vuX2, vuY2);

        return;
    }

    if (deltaTime <= 0)
        return;

    m_lastUpdate_analog = currentTime;

    // Die Bewegungsgleichung wird in festen kleinen Schritten gerechnet, deren Zahl begrenzt ist. Ein langes
    // Bildintervall würde die Schritte deshalb strecken und die Rechnung aufschaukeln lassen. Beim allerersten
    // Aufruf steht der Zeitstempel noch bei null, das Intervall betrüge dann die gesamte Laufzeit seit dem Start.
    if (deltaTime > VU_MAX_STEPS * VU_MAX_STEP)
        deltaTime = VU_MAX_STEPS * VU_MAX_STEP;

    const uint16_t *plate = analogVUPlate();

    const size_t ballistics = static_cast<size_t>(m_vuBallistics);
    const float omega = VU_OMEGA_TIMES_TIME / VU_RESPONSE_TIME[ballistics];
    const float federKraft = omega * omega;
    const float daempfung = 2.0f * VU_DAMPING * omega;

    int schritte = static_cast<int>(deltaTime / VU_MAX_STEP) + 1;
    if (schritte > VU_MAX_STEPS)
        schritte = VU_MAX_STEPS;

    const float schrittZeit = deltaTime / static_cast<float>(schritte);

    // Nur der Streifen wird aufgefrischt, den die Nadel verlassen oder neu eingenommen hat. Ein volles Blatt
    // hat im Original gut 15 000 Bildpunkte, der Streifen wenige hundert.
    auto processChannel = [&](float rms, float &pastVal, float &velocity, float &lastRendered, Gfx_Sprite *spr, int x, int y)
    {
        if (!spr)
            return;

        float dBValue = amplitudeToDb(rms);
        float targetVal = dbToNeedleAngle(dBValue);

        // Die Feder zieht zum Zielwert, die Dämpfung bremst. Erst die Geschwindigkeit, dann der Weg
        for (int i = 0; i < schritte; ++i)
        {
            velocity += (federKraft * (targetVal - pastVal) - daempfung * velocity) * schrittZeit;
            pastVal += velocity * schrittZeit;
        }

        if (fabs(pastVal - lastRendered) < redrawThreshold)
            return;

        vuNeedleSpans(vg, lastRendered, g_vuSpanAltVon, g_vuSpanAltBis);
        vuNeedleSpans(vg, pastVal, g_vuSpanNeuVon, g_vuSpanNeuBis);

        // Das Blatt nur dort wiederherstellen, wo die alte Nadel stand
        for (int row = 0; row < vg.plateH; ++row)
        {
            if (g_vuSpanAltBis[row] < g_vuSpanAltVon[row])
                continue;

            const int breite = g_vuSpanAltBis[row] - g_vuSpanAltVon[row] + 1;
            spr->pushImage(g_vuSpanAltVon[row], row, breite, 1,
                           plate + row * vg.plateW + g_vuSpanAltVon[row]);
        }

        m_sprVU_Analog_Needle->pushRotatedWithAA(spr, pastVal);

        // Zum Display geht das umschließende Rechteck beider Nadelstellungen
        int links = vg.plateW, rechts = -1, oben = vg.plateH, unten = -1;
        for (int row = 0; row < vg.plateH; ++row)
        {
            const bool alt = g_vuSpanAltBis[row] >= g_vuSpanAltVon[row];
            const bool neu = g_vuSpanNeuBis[row] >= g_vuSpanNeuVon[row];

            if (!alt && !neu)
                continue;

            if (alt)
            {
                links = min(links, static_cast<int>(g_vuSpanAltVon[row]));
                rechts = max(rechts, static_cast<int>(g_vuSpanAltBis[row]));
            }

            if (neu)
            {
                links = min(links, static_cast<int>(g_vuSpanNeuVon[row]));
                rechts = max(rechts, static_cast<int>(g_vuSpanNeuBis[row]));
            }

            oben = min(oben, row);
            unten = max(unten, row);
        }

        if (rechts >= links && unten >= oben)
        {
            m_tft->setClipRect(x + links, y + oben, rechts - links + 1, unten - oben + 1);
            pushToCanvas(spr, x, y);
            m_tft->clearClipRect();
        }

        lastRendered = pastVal;
    };

    processChannel(rmsLeft, m_pastValL, m_needleVelL, m_lastRenderedL, m_sprVU_Analog_L, vuX1, vuY1);
    processChannel(rmsRight, m_pastValR, m_needleVelR, m_lastRenderedR, m_sprVU_Analog_R, vuX2, vuY2);
}

void Visualizer::drawDigitalVU(bool init, float rmsLeft, float rmsRight, float peakLeft, float peakRight)
{
    if (!m_tft)
        return;

    if (!m_playing)
        rmsLeft = rmsRight = peakLeft = peakRight = 0.0f;

    // Zeilenlagen in Originalkoordinaten werden über ihre Endpunkte auf die Zeichenfläche abgebildet
    const int VU_W = gl(440);
    const int RMS_HEIGHT = gy(VIS_AREA_Y + 18) - m_areaY;
    const int RMS_PEAK_WIDTH = gl(1);
    const int PPM_PEAK_WIDTH = gl(8);
    const int PPM_HEIGHT = gy(VIS_AREA_Y + 23) - gy(VIS_AREA_Y + 22);
    const int PPM_L_Y = gy(VIS_AREA_Y + 22), PPM_R_Y = gy(VIS_AREA_Y + VIS_AREA_H - 23);
    static constexpr uint32_t PEAK_HOLD_MS = 1000;

    // Einmal je Bild gelesen, die Zeilenschleifen unten verwenden nur diese Werte
    const uint16_t RMS_COLOR_GREEN = farbe(Rolle::VisPegelGruen);
    const uint16_t RMS_COLOR_ORANGE = farbe(Rolle::VisPegelOrange);
    const uint16_t RMS_COLOR_RED = farbe(Rolle::VisPegelRot);
    const uint16_t RMS_COLOR_BG = farbe(Rolle::VisPegelGrund);
    const uint16_t RMS_COLOR_PEAK = farbe(Rolle::VisSpitze);
    const uint16_t SEGMENT_GAP = farbe(Rolle::VisSegmentLuecke);

    const uint16_t PPM_COLOR_GREEN = farbe(Rolle::VisPpmGruen);
    const uint16_t PPM_COLOR_YELLOW = farbe(Rolle::VisPpmGelb);
    const uint16_t PPM_COLOR_RED = farbe(Rolle::VisPegelRot);
    const uint16_t PPM_COLOR_BG = farbe(Rolle::VisPegelGrund);
    const uint16_t PPM_COLOR_PEAK = farbe(Rolle::VisSpitze);

    const uint32_t now = millis();

    if (init)
    {
        m_rmsChannels[0] = m_rmsChannels[1] = {VU_DB_LOW, 0, 0, 0};
        m_ppmChannels[0] = m_ppmChannels[1] = {VU_DB_LOW, 0, 0, 0};
        m_lastUpdate_digital = now;

        if (!m_sprVU_Digital_RMS_L)
        {
            if (!m_tft->safeCreateSprite(m_sprVU_Digital_RMS_L, VU_W, RMS_HEIGHT))
            {
                LOG_ERROR("Konnte Digital-VU RMS_L Sprite nicht erstellen!");
                return;
            }
        }
        if (!m_sprVU_Digital_RMS_R)
        {
            if (!m_tft->safeCreateSprite(m_sprVU_Digital_RMS_R, VU_W, RMS_HEIGHT))
            {
                LOG_ERROR("Konnte Digital-VU RMS_R Sprite nicht erstellen!");
                return;
            }
        }
        if (!m_sprVU_Digital_PPM_L)
        {
            if (!m_tft->safeCreateSprite(m_sprVU_Digital_PPM_L, VU_W, PPM_HEIGHT))
            {
                LOG_ERROR("Konnte Digital-VU PPM_L Sprite nicht erstellen!");
                return;
            }
        }
        if (!m_sprVU_Digital_PPM_R)
        {
            if (!m_tft->safeCreateSprite(m_sprVU_Digital_PPM_R, VU_W, PPM_HEIGHT))
            {
                LOG_ERROR("Konnte Digital-VU PPM_R Sprite nicht erstellen!");
                return;
            }
        }

        clearVisualizationArea();

        const int MARK_Y = gy(VIS_AREA_Y + 25);
        const int MARK_HEIGHT_MAJOR = gy(VIS_AREA_Y + 45) - MARK_Y;
        const int MARK_HEIGHT_MINOR = gy(VIS_AREA_Y + 40) - gy(VIS_AREA_Y + 30);
        const int MARK_HEIGHT_ZERO = MARK_HEIGHT_MAJOR;
        const int MARK_MINOR_Y = gy(VIS_AREA_Y + 30);
        const int MARK_W = gl(1);
        const uint16_t MARK_COLOR_MAJOR = farbe(Rolle::VisSkalaGross);
        const uint16_t MARK_COLOR_MINOR = farbe(Rolle::VisSkalaKlein);
        const uint16_t MARK_COLOR_ZERO = farbe(Rolle::VisPegelRot);
        const uint16_t TEXT_COLOR = farbe(Rolle::GemTextHell);
        const uint16_t TEXT_GROUND = farbe(Rolle::GemGrund);
        const int TEXT_OFFSET_Y = gl(4);

        m_tft->setFont(&m_font);
        m_canvasWritten = true;

        auto drawMark = [&](float dB, bool isMajor, bool showText)
        {
            if (dB < VU_DB_LOW || dB > VU_DB_HIGH)
                return;

            uint16_t xPos = m_areaX + (uint16_t)((dB - VU_DB_LOW) / (VU_DB_HIGH - VU_DB_LOW) * VU_W);
            bool isZero = (fabs(dB) < 0.001f);

            uint16_t markColor = isZero ? MARK_COLOR_ZERO : (isMajor ? MARK_COLOR_MAJOR : MARK_COLOR_MINOR);
            int markHeight = isZero ? MARK_HEIGHT_ZERO : (isMajor ? MARK_HEIGHT_MAJOR : MARK_HEIGHT_MINOR);
            const int markY = isMajor ? MARK_Y : MARK_MINOR_Y;

            if (MARK_W == 1)
                m_tft->drawFastVLine(xPos, markY, markHeight, markColor);
            else
                m_tft->fillRect(xPos, markY, MARK_W, markHeight, markColor);

            if (showText && (isMajor || isZero))
            {
                char dbText[8];
                if (isZero)
                {
                    stringutils::safeCopy(dbText, sizeof(dbText), "0");
                    m_tft->setTextColor(MARK_COLOR_ZERO, TEXT_GROUND);
                }
                else
                {
                    stringutils::safeFormat(dbText, "%.0f", dB);
                    m_tft->setTextColor(TEXT_COLOR, TEXT_GROUND);
                }

                int textXPos = xPos - (m_tft->textWidth(dbText) / 2);

                if (dB >= 0.0f)
                    textXPos += gl(1);

                m_tft->drawString(dbText, textXPos, MARK_Y + TEXT_OFFSET_Y);
            }
        };

        const float majorMarks[] = {-100, -90, -80, -70, -60, -50, -40, -30, -20, -12, -6, -3, 0, 3, 6, 12};
        for (float dB : majorMarks)
            drawMark(dB, true, true);

        bool dBHighInMajors = false;

        for (float mark : majorMarks)
        {
            if (fabs(mark - VU_DB_HIGH) < 0.001f)
            {
                dBHighInMajors = true;
                break;
            }
        }

        if (!dBHighInMajors && VU_DB_HIGH > 3.0f)
            drawMark(VU_DB_HIGH, true, true);

        float currentDB = ceilf(VU_DB_LOW);
        while (currentDB < VU_DB_HIGH)
        {
            bool isMajorMark = false;
            for (float majorDB : majorMarks)
            {
                if (fabs(currentDB - majorDB) < 0.001f)
                {
                    isMajorMark = true;
                    break;
                }
            }

            if (!isMajorMark)
                drawMark(currentDB, false, false);

            if (currentDB < -20.0f)
                currentDB += 2.0f;
            else
                currentDB += 1.0f;
        }

        m_tft->setFont(nullptr);

        return;
    }

    const uint32_t elapsed = now - m_lastUpdate_digital;
    if (elapsed < BAR_FRAME_MS)
        return;

    const float dt = elapsed * 0.001f;
    m_lastUpdate_digital = now;

    const uint16_t threshOrange = (uint16_t)((-12.0f - VU_DB_LOW) / (VU_DB_HIGH - VU_DB_LOW) * VU_W);
    const uint16_t threshRed = (uint16_t)((-3.0f - VU_DB_LOW) / (VU_DB_HIGH - VU_DB_LOW) * VU_W);
    const uint16_t ppmThreshYellow = (uint16_t)((-18.0f - VU_DB_LOW) / (VU_DB_HIGH - VU_DB_LOW) * VU_W);
    const uint16_t ppmThreshRed = (uint16_t)((-6.0f - VU_DB_LOW) / (VU_DB_HIGH - VU_DB_LOW) * VU_W);

    auto updateRMSChannel = [&](float rms, ChannelData &ch)
    {
        float targetDB = amplitudeToDb(rms);

        float alpha = (targetDB > ch.smoothedDB) ? (1.0f - expf(-dt / 0.05f)) : (1.0f - expf(-dt / 0.3f));

        ch.smoothedDB += (targetDB - ch.smoothedDB) * alpha;
        ch.currentPos = (ch.smoothedDB <= VU_DB_LOW) ? 0 : (ch.smoothedDB >= VU_DB_HIGH) ? VU_W
                                                                                         : (uint16_t)((ch.smoothedDB - VU_DB_LOW) / (VU_DB_HIGH - VU_DB_LOW) * VU_W);

        if (ch.currentPos > ch.peakPos)
        {
            ch.peakPos = ch.currentPos;
            ch.peakTime = now;
        }
        else if (now - ch.peakTime > PEAK_HOLD_MS && ch.peakPos > 0)
        {
            ch.peakPos--;
        }
    };

    auto updatePPMChannel = [&](float peak, ChannelData &ch)
    {
        float targetDB = amplitudeToDb(peak);

        float alpha = (targetDB > ch.smoothedDB) ? (1.0f - expf(-dt / 0.003f)) : (1.0f - expf(-dt / 0.15f));

        ch.smoothedDB += (targetDB - ch.smoothedDB) * alpha;
        ch.currentPos = (ch.smoothedDB <= VU_DB_LOW) ? 0 : (ch.smoothedDB >= VU_DB_HIGH) ? VU_W
                                                                                         : (uint16_t)((ch.smoothedDB - VU_DB_LOW) / (VU_DB_HIGH - VU_DB_LOW) * VU_W);

        if (ch.currentPos > ch.peakPos)
        {
            ch.peakPos = ch.currentPos;
            ch.peakTime = now;
        }
        else if (now - ch.peakTime > (PEAK_HOLD_MS / 2) && ch.peakPos > 0)
        {
            ch.peakPos--;
        }
    };

    auto renderRMSChannel = [&](Gfx_Sprite *spr, const ChannelData &ch)
    {
        // Die erste Zeile wird direkt in den Puffer geschrieben, das ist schneller als drawPixel
        uint16_t *buffer = (uint16_t *)spr->getBuffer();
        if (!buffer)
            return;

        for (uint16_t x = 0; x < VU_W; x++)
        {
            uint16_t color;
            const uint16_t bgStripe = (x % 2 == 0) ? SEGMENT_GAP : RMS_COLOR_BG;

            if (x >= ch.peakPos && x < ch.peakPos + RMS_PEAK_WIDTH)
            {
                color = RMS_COLOR_PEAK;
            }
            else if (x < ch.currentPos)
            {
                if (x % 2 == 0)
                {
                    if (m_digitalVUStyle == DigitalVUStyle::RAINBOW)
                    {
                        // Fließender Verlauf von Grün bis Rot über die Balkenlänge
                        color = rainbowColors(map(x, 0, VU_W, VU_RAINBOW_GREEN, 0));
                    }
                    else
                    {
                        if (x < threshOrange)
                            color = RMS_COLOR_GREEN;
                        else if (x < threshRed)
                            color = RMS_COLOR_ORANGE;
                        else
                            color = RMS_COLOR_RED;
                    }
                }
                else
                {
                    color = SEGMENT_GAP;
                }
            }
            else
            {
                color = bgStripe;
            }

            buffer[x] = bswap16(color);
        }

        for (int y = 1; y < RMS_HEIGHT; y++)
            memcpy(&buffer[y * VU_W], &buffer[0], VU_W * sizeof(uint16_t));
    };

    auto renderPPMChannel = [&](Gfx_Sprite *spr, const ChannelData &ch)
    {
        uint16_t *buffer = (uint16_t *)spr->getBuffer();
        if (!buffer)
            return;

        for (uint16_t x = 0; x < VU_W; x++)
        {
            uint16_t color;

            if (ch.peakPos > 0 && x >= ch.peakPos && x < ch.peakPos + PPM_PEAK_WIDTH)
            {
                color = PPM_COLOR_PEAK;
            }
            else if (x < ch.currentPos)
            {
                if (x < ppmThreshYellow)
                    color = PPM_COLOR_GREEN;
                else if (x < ppmThreshRed)
                    color = PPM_COLOR_YELLOW;
                else
                    color = PPM_COLOR_RED;
            }
            else
            {
                color = PPM_COLOR_BG;
            }

            buffer[x] = bswap16(color);
        }

        // Der Balken ist im Original eine Zeile hoch, bei Faktor 1,5 zwei
        for (int y = 1; y < spr->height(); y++)
            memcpy(&buffer[y * VU_W], &buffer[0], VU_W * sizeof(uint16_t));
    };

    updateRMSChannel(rmsLeft, m_rmsChannels[0]);
    updateRMSChannel(rmsRight, m_rmsChannels[1]);
    updatePPMChannel(peakLeft, m_ppmChannels[0]);
    updatePPMChannel(peakRight, m_ppmChannels[1]);

    renderRMSChannel(m_sprVU_Digital_RMS_L, m_rmsChannels[0]);
    renderRMSChannel(m_sprVU_Digital_RMS_R, m_rmsChannels[1]);
    renderPPMChannel(m_sprVU_Digital_PPM_L, m_ppmChannels[0]);
    renderPPMChannel(m_sprVU_Digital_PPM_R, m_ppmChannels[1]);

    pushToCanvas(m_sprVU_Digital_RMS_L, m_areaX, m_areaY);
    pushToCanvas(m_sprVU_Digital_RMS_R, m_areaX, m_areaY + m_areaH - RMS_HEIGHT);

    pushToCanvas(m_sprVU_Digital_PPM_L, m_areaX, PPM_L_Y);
    pushToCanvas(m_sprVU_Digital_PPM_R, m_areaX, PPM_R_Y);
}

void Visualizer::drawSpectrum(bool init)
{
    if (!m_tft || !m_spectrum_bands)
        return;

    constexpr int BAND_COUNT = 63; ///< Anzahl der klassischen Frequenzbänder
    constexpr int FFT_BIN_COUNT = FFT_N / 2 + 1; ///< FFT-Werte einschließlich der Nyquist-Frequenz
    const int CENTERED_GRAPH_H = m_areaH - m_labelH; ///< Gesamthöhe der mittig wachsenden Balken
    const int CENTERED_PEAK_H = gl(2); ///< Höhe der Spitzenmarkierung
    const int CENTERED_PEAK_GAP = gl(1); ///< Abstand zwischen Balken und Spitzenmarkierung
    const int CENTERED_GAP = gl(CENTERED_BAR_GAP); ///< Abstand zwischen den mittigen Balken
    const int CLASSIC_GAP = gl(BIN_GAP); ///< Abstand zwischen den klassischen Balken
    const int CLASSIC_PEAK_H = gl(1); ///< Höhe der klassischen Spitzenmarkierung
    constexpr uint32_t PEAK_HOLD_MS = 800; ///< Haltezeit der Spitzen beider Spektren
    const float CLASSIC_PEAK_FALL_RATE = gf(60.0f); ///< Rücklauf der klassischen Spitzen in Pixeln pro Sekunde, mit der Höhe gestreckt
    const float CENTERED_PEAK_FALL_RATE = gf(30.0f); ///< Rücklauf der Gesamthöhe, die obere Spitzenmarkierung fällt mit der Hälfte davon
    const uint16_t BAR_GREEN = farbe(Rolle::VisPegelGruen); ///< Grüne Balkenfarbe beider Spektren
    const uint16_t PEAK_SILVER = farbe(Rolle::VisSpitzeSilber); ///< Spitzenmarke beider Spektren im klassischen Stil
    const uint16_t BAR_GROUND = farbe(Rolle::VisPegelGrund); ///< Grundzeilen und Segmentlücken im Balken des klassischen Spektrums
    const uint16_t SEGMENT_GAP = farbe(Rolle::VisSegmentLuecke); ///< Lückenzeilen über dem Balken des klassischen Spektrums

    const bool centered = m_visMode == VisMode::VIS_SPECTRUM_CENTERED;
    const bool waterfall = m_visMode == VisMode::VIS_WATERFALL;
    const int bandCount = centered ? CENTERED_BAND_COUNT : BAND_COUNT;
    const int graphHeight = centered ? CENTERED_GRAPH_H : m_areaH;
    const uint32_t now = millis();
    const uint32_t elapsed = now - m_lastSpectrumUpdate;
    const uint32_t frameMs = waterfall ? m_waterfallFrameMs : BAR_FRAME_MS;
    if (!init && elapsed < frameMs)
        return;

    // Bandgrenzen des klassischen Spektrums in Hz, BAND_COUNT + 1 Werte
    static constexpr float BAND_CUTOFF_FREQS[BAND_COUNT + 1] = {
        SPECTRUM_MIN_HZ, 129.00f, 172.00f, 215.00f, 258.00f, 301.00f, 344.00f, 387.00f,
        430.00f, 473.00f, 516.00f, 559.00f, 602.00f, 645.00f, 688.00f, 731.00f,
        774.00f, 817.00f, 860.00f, 946.00f, 1032.00f, 1118.00f, 1204.00f, 1290.00f,
        1376.00f, 1505.00f, 1634.00f, 1763.00f, 1892.00f, 2021.00f, 2193.00f, 2365.00f,
        2537.00f, 2709.00f, 2924.00f, 3139.00f, 3354.00f, 3612.00f, 3870.00f, 4128.00f,
        4429.00f, 4730.00f, 5074.00f, 5418.00f, 5805.00f, 6192.00f, 6622.00f, 7052.00f,
        7525.00f, 7998.00f, 8514.00f, 9030.00f, 9589.00f, 10148.00f, 10750.00f, 11352.00f,
        11568.00f, 11783.00f, 11997.00f, 12210.00f, 12423.00f, 12636.00f, 12850.00f, SPECTRUM_MAX_HZ};

    // Logarithmische Bandgrenzen des mittigen Spektrums von 86 Hz bis 12987 Hz
    static constexpr float CENTERED_CUTOFF_FREQS[] = {
        SPECTRUM_MIN_HZ, 98.8613f, 113.6461f, 130.6419f, 150.1795f,
        172.6389f, 198.4572f, 228.1366f, 262.2545f, 301.4748f,
        346.5605f, 398.3889f, 457.9681f, 526.4575f, 605.1895f,
        695.6959f, 799.7376f, 919.3388f, 1056.8264f, 1214.8753f,
        1396.5606f, 1605.4170f, 1845.5079f, 2121.5046f, 2438.7767f,
        2803.4971f, 3222.7616f, 3704.7273f, 4258.7712f, 4895.6727f,
        5627.8232f, 6469.4671f, 7436.9793f, 8549.1835f, 9827.7185f,
        11297.4590f, SPECTRUM_MAX_HZ};
    static_assert(sizeof(CENTERED_CUTOFF_FREQS) / sizeof(CENTERED_CUTOFF_FREQS[0]) == CENTERED_BAND_COUNT + 1,
                  "Die Anzahl der Bandgrenzen muss zur Anzahl der mittigen Balken passen");
    const float *cutoffFreqs = centered ? CENTERED_CUTOFF_FREQS : BAND_CUTOFF_FREQS;

    if (init)
    {
        if (!m_sprVU_Spectrum)
        {
            if (!m_tft->safeCreateSprite(m_sprVU_Spectrum, m_areaW, m_areaH))
            {
                LOG_ERROR("Konnte Spektrum-Sprite nicht erstellen!");
                return;
            }
            m_sprVU_Spectrum->setFont(m_smallFont);
            m_sprVU_Spectrum->setTextColor(farbe(Rolle::GemTextSilber));
        }

        clearVisualizationArea();

        if (!m_fft_cfg)
            m_fft_cfg = kiss_fftr_alloc(FFT_N, 0, nullptr, nullptr);

        if (!m_fftWindowInitialized)
        {
            for (int i = 0; i < FFT_N; i++)
                m_fft_window_lut[i] = 0.5f * (1.0f - cosf(6.28318530718f * i / (FFT_N - 1)));

            m_fftWindowInitialized = true;
        }

        if (!m_fft_cfg)
        {
            LOG_ERROR("FFT-Konfiguration konnte nicht erstellt werden!");
            return;
        }

        // Ein Moduswechsel ändert Bandzuordnung und Höhenmaßstab auch bei gleicher Abtastrate.
        m_spectrumSampleRate = 0;
        m_spectrumHadSamples = false;
        m_spectrumTargets.fill(0.0f);
        for (int band = 0; band < bandCount; ++band)
            m_spectrum_bands[band] = {0.0f, 0.0f, 0, 0, 0};
        for (int band = 0; band < CENTERED_BAND_COUNT; ++band)
            m_centeredSpectrumColors[band] = rainbowColors(band * (BAND_COUNT - 1) * 2 / (CENTERED_BAND_COUNT - 1));
        m_centeredSpectrumDirty = true;
        m_lastSpectrumUpdate = now;

        m_sprVU_Spectrum->clear(farbe(Rolle::GemGrund));
        if (waterfall)
        {
            drawSpectrumLabels(cutoffFreqs, bandCount);
            drawWaterfall(true, false, false, 0);
            return;
        }
        pushToCanvas(m_sprVU_Spectrum, m_areaX, m_areaY);
        return;
    }

    if (!m_fft_cfg || !m_sprVU_Spectrum)
        return;

    const float dt = elapsed * 0.001f;
    m_lastSpectrumUpdate = waterfall ? now - elapsed % m_waterfallFrameMs : now;
    uint32_t sampleRate;
    bool hasSamples;
    bool analyze;
    {
        // Der Writer wird nur während der Sample-Übernahme am Puffertausch gehindert.
        ReaderGuard guard(m_readerActive);
        const uint8_t readIdx = m_writeBufferIdx.load(std::memory_order_acquire) ^ 1;
        const int16_t *samplesL = m_samplesL[readIdx].get();
        const int16_t *samplesR = m_samplesR[readIdx].get();
        const uint32_t sampleSeq = m_sampleSeq.load(std::memory_order_acquire);
        sampleRate = m_sampleRate[readIdx];
        const uint16_t sampleCount = m_sampleCount[readIdx];
        hasSamples = samplesL && samplesR && sampleCount > 0 && sampleRate > 0 && m_playing;
        analyze = sampleSeq != m_spectrumAnalyzedSeq || sampleRate != m_spectrumSampleRate || hasSamples != m_spectrumHadSamples;

        if (analyze && hasSamples)
        {
            const uint16_t numSamples = (sampleCount > FFT_N) ? FFT_N : sampleCount;
            const uint16_t start = sampleCount - numSamples;
            for (int i = 0; i < numSamples; ++i)
            {
                const int idx = start + i;
                const float mono = 0.5f * (samplesL[idx] + samplesR[idx]);
                m_fft_input[i] = mono * m_fft_window_lut[i];
            }
            if (numSamples < FFT_N)
                memset(m_fft_input.data() + numSamples, 0, (FFT_N - numSamples) * sizeof(kiss_fft_scalar));
        }
        m_spectrumAnalyzedSeq = sampleSeq;
    }
    m_spectrumHadSamples = hasSamples;

    if (sampleRate != m_spectrumSampleRate)
    {
        const float freqToBin = sampleRate ? static_cast<float>(FFT_N) / sampleRate : 0.0f;
        const float nyquist = sampleRate * 0.5f;
        for (int band = 0; !waterfall && band <= bandCount; ++band)
        {
            if (centered)
            {
                // Jede FFT-Stützstelle vertritt eine Binbreite um ihre Mittenfrequenz.
                const float binEdge = fminf(cutoffFreqs[band], nyquist) * freqToBin + 0.5f;
                m_centeredBandEdges[band] = binEdge;
                m_band_bin_indices_lut[band] = static_cast<int>(binEdge);
            }
            else
            {
                m_band_bin_indices_lut[band] = (cutoffFreqs[band] < nyquist)
                                                   ? static_cast<int>(ceilf(cutoffFreqs[band] * freqToBin))
                                                   : FFT_BIN_COUNT;
            }
        }
        for (int band = 0; band < bandCount; ++band)
            m_spectrum_bands[band] = {0.0f, 0.0f, 0, 0, 0};
        m_spectrumSampleRate = sampleRate;
        m_centeredSpectrumDirty = true;
    }

    if (analyze && hasSamples)
        kiss_fftr(m_fft_cfg, m_fft_input.data(), m_fft_output.data());

    if (waterfall)
    {
        drawWaterfall(false, analyze, hasSamples, elapsed);
        return;
    }

    Gfx_Sprite *current_sprite = m_sprVU_Spectrum;
    const float ALPHA_UP = 1.0f - expf(-dt / 0.01f);
    const float ALPHA_DOWN = 1.0f - expf(-dt / 0.25f);
    const float PEAK_FALL_SPEED = (centered ? CENTERED_PEAK_FALL_RATE : CLASSIC_PEAK_FALL_RATE) * dt;

    // Der Bezugspegel verschiebt das dB-Fenster, der Lautstärkeregler verändert die Analyse nicht.
    const float DBHIGH = vuAlignmentDb() + SPECTRUM_TOP_OVER_REFERENCE;
    const float DBLOW = DBHIGH - SPECTRUM_SPAN_DB;
    const float DB_RANGE_INV = 1.0f / (DBHIGH - DBLOW);
    const float OFFSET = 10.0f * log10f(SPECTRUM_SCALE_FACTOR * SPECTRUM_SCALE_FACTOR / FFT_N);

    uint16_t *buffer = static_cast<uint16_t *>(current_sprite->getBuffer());
    if (!buffer)
        return;
    const int sprite_width = current_sprite->width();
    bool changed = m_centeredSpectrumDirty;

    for (int band = 0; band < bandCount; ++band)
    {
        if (analyze)
        {
            float bandPower = 0.0f;
            if (hasSamples)
            {
                const int firstBin = m_band_bin_indices_lut[band];
                const int endBin = m_band_bin_indices_lut[band + 1] + (centered ? 1 : 0);
                for (int bin = firstBin; bin < endBin; ++bin)
                {
                    float weight = 1.0f;
                    if (centered)
                    {
                        // Rand-Bins liefern nur den vom Band überdeckten Anteil ihrer Leistung.
                        if (bin == firstBin)
                            weight -= m_centeredBandEdges[band] - bin;
                        if (bin == endBin - 1)
                            weight -= bin + 1 - m_centeredBandEdges[band + 1];
                    }
                    const float re = m_fft_output[bin].r;
                    const float im = m_fft_output[bin].i;
                    bandPower += (re * re + im * im) * weight;
                }
                // Ein klassisches Band, das schmaler ist als der Abstand der FFT-Stützstellen, enthält bei manchen Abtastraten keine
                // (bei 48 kHz 516 bis 559 Hz, ab 88,2 kHz mehrere Bassbänder) und bliebe immer unten. Seine Leistung wird an der
                // Bandmitte zwischen den beiden benachbarten Stützstellen interpoliert, Bänder über der Nyquistfrequenz bleiben leer.
                if (!centered && endBin <= firstBin && firstBin > 0 && firstBin < FFT_BIN_COUNT)
                {
                    const float center = 0.5f * (cutoffFreqs[band] + cutoffFreqs[band + 1]) * FFT_N / sampleRate;
                    const float upperWeight = fminf(fmaxf(center - (firstBin - 1), 0.0f), 1.0f);
                    const auto &lower = m_fft_output[firstBin - 1];
                    const auto &upper = m_fft_output[firstBin];
                    bandPower = (lower.r * lower.r + lower.i * lower.i) * (1.0f - upperWeight) + (upper.r * upper.r + upper.i * upper.i) * upperWeight;
                }
            }

            float normalized = 0.0f;
            if (bandPower > 1e-10f)
            {
                const float magnitudeDb = 10.0f * log10f(bandPower) + OFFSET;
                normalized = (magnitudeDb - DBLOW) * DB_RANGE_INV;
                if (normalized < 0.0f)
                    normalized = 0.0f;
                else if (normalized > 1.0f)
                    normalized = 1.0f;
            }
            m_spectrumTargets[band] = normalized;
        }

        auto &b = m_spectrum_bands[band];
        const uint16_t previousPos = b.currentPos;
        const uint16_t previousPeak = b.peakPos;
        const float normalized = m_spectrumTargets[band];
        b.smoothedMagnitude += (normalized - b.smoothedMagnitude) * ((normalized > b.smoothedMagnitude) ? ALPHA_UP : ALPHA_DOWN);
        b.currentPos = static_cast<uint16_t>(b.smoothedMagnitude * graphHeight);

        const float cur = b.currentPos;
        if (cur >= b.peakPosFloat)
        {
            b.peakPosFloat = cur;
            b.peakTime = now;
        }
        else if (now - b.peakTime > PEAK_HOLD_MS)
        {
            b.peakPosFloat = fmaxf(cur, b.peakPosFloat - PEAK_FALL_SPEED);
        }
        b.peakPos = static_cast<uint16_t>(fmaxf(1.0f, b.peakPosFloat));
        const bool bandChanged = m_centeredSpectrumDirty || previousPos != b.currentPos || previousPeak != b.peakPos;
        changed = changed || bandChanged;

        // Das klassische Spektrum schreibt nur Balken neu, deren Höhe oder Spitze sich bewegt hat. Die übrigen stehen unverändert im Sprite.
        if (centered || !bandChanged)
            continue;

        // Gleichmäßig über die ganze Fläche verteilt wie das mittige Spektrum, nativ ergibt das 7 und 8 px breite Balken bei 3 px Lücke.
        const int x = band * (m_areaW + CLASSIC_GAP) / BAND_COUNT;
        const int width = (band + 1) * (m_areaW + CLASSIC_GAP) / BAND_COUNT - x - CLASSIC_GAP;
        const int bar_top = m_areaH - b.currentPos;
        const int peak_y = m_areaH - b.peakPos;
        uint16_t barColor = BAR_GREEN;
        uint16_t peakColor = PEAK_SILVER;
        if (m_spectrumStyle == SpectrumStyle::RAINBOW)
            barColor = peakColor = rainbowColors(band * 2);

        for (int y = 0; y < m_areaH; ++y)
        {
            uint16_t color;
            if (y >= peak_y && y < peak_y + CLASSIC_PEAK_H)
                color = peakColor;
            else if (y < bar_top)
                color = (y % 2 == 0) ? BAR_GROUND : SEGMENT_GAP;
            else
                color = (y % 2 == 0) ? barColor : BAR_GROUND;

            uint16_t *line = buffer + y * sprite_width + x;
            for (int i = 0; i < width; ++i)
                line[i] = bswap16(color);
        }
    }

    if (!changed)
        return;

    if (centered)
    {
        current_sprite->clear(farbe(Rolle::GemGrund));
        const int CENTER_Y = m_labelH + CENTERED_GRAPH_H / 2; ///< Mittellinie des Balkenfelds
        for (int band = 0; band < bandCount; ++band)
        {
            const auto &b = m_spectrum_bands[band];
            const int x = band * (m_areaW + CENTERED_GAP) / bandCount;
            const int nextX = (band + 1) * (m_areaW + CENTERED_GAP) / bandCount;
            const int width = nextX - x - CENTERED_GAP;
            const int halfHeight = b.currentPos > 1 ? b.currentPos / 2 : 1;
            const uint16_t barColor = m_centeredSpectrumStyle == SpectrumStyle::RAINBOW ? m_centeredSpectrumColors[band] : BAR_GREEN;
            const uint16_t peakColor = m_centeredSpectrumStyle == SpectrumStyle::RAINBOW ? barColor : PEAK_SILVER;
            current_sprite->fillRect(x, CENTER_Y - halfHeight, width, halfHeight * 2 + 1, barColor);
            const int peakHalfHeight = b.peakPos > 1 ? b.peakPos / 2 : 1;
            int peakY = CENTER_Y - peakHalfHeight - CENTERED_PEAK_H - CENTERED_PEAK_GAP;
            if (peakY < m_labelH)
                peakY = m_labelH;
            current_sprite->fillRect(x, peakY, width, CENTERED_PEAK_H, peakColor);
        }
    }
    m_centeredSpectrumDirty = false;

    drawSpectrumLabels(cutoffFreqs, bandCount);
    pushToCanvas(current_sprite, m_areaX, m_areaY);
}

void Visualizer::drawSpectrumLabels(const float *cutoffFreqs, int bandCount)
{
    constexpr uint16_t LABEL_FREQUENCIES[] = {125, 500, 1000, 2000, 4000, 8000, 12000}; ///< Frequenzmarken innerhalb der Bandgrenzen in Hz
    const bool centered = m_visMode == VisMode::VIS_SPECTRUM_CENTERED;
    const bool waterfall = m_visMode == VisMode::VIS_WATERFALL;
    Gfx_Sprite *current_sprite = m_sprVU_Spectrum;
    const int labelY = (centered || waterfall) ? 0 : gl(6);
    for (const uint16_t frequency : LABEL_FREQUENCIES)
    {
        int band = 0;
        while (band < bandCount - 1 && frequency >= cutoffFreqs[band + 1])
            ++band;
        const float fraction = (frequency - cutoffFreqs[band]) / (cutoffFreqs[band + 1] - cutoffFreqs[band]);
        const float bandWidth = static_cast<float>(m_areaW + gl(centered ? CENTERED_BAR_GAP : BIN_GAP)) / bandCount;
        const int centerX = waterfall
                                ? static_cast<int>(m_areaW * logf(frequency / cutoffFreqs[0]) / logf(cutoffFreqs[bandCount] / cutoffFreqs[0]))
                                : static_cast<int>((band + fraction) * bandWidth);
        char label[8];
        if (frequency < 1000)
            stringutils::safeFormat(label, "%u", static_cast<unsigned>(frequency));
        else
            stringutils::safeFormat(label, "%uk", static_cast<unsigned>(frequency / 1000));
        const int width = current_sprite->textWidth(label);
        int x = centerX - width / 2;
        if (x < 0)
            x = 0;
        else if (x > m_areaW - width)
            x = m_areaW - width;
        current_sprite->drawString(label, x, labelY);
    }
}

/** @brief Füllt Wasserfallzeilen mit der Stille, der byte-getauschten ersten Farbstufe. Die Stille Schwarz bleibt ein memset. */
static void fillWaterfallRows(uint16_t *rows, size_t pixels, uint16_t silence)
{
    if (silence == 0)
        memset(rows, 0, pixels * sizeof(uint16_t));
    else
        std::fill_n(rows, pixels, silence);
}

void Visualizer::drawWaterfall(bool init, bool analyze, bool hasSamples, uint32_t elapsed)
{
    const int GRAPH_H = m_areaH - m_labelH; ///< Anzahl der sichtbaren Zeitschritte
    constexpr int SMOOTHING_BITS = 8; ///< Nach acht Halbierungen ist jede Differenz der 8-Bit-Pegelstufen abgebaut
    constexpr float INTERPOLATION_MAX_WIDTH = 0.25f; ///< Interpolation nur bei mehr als vier Pixeln je FFT-Bin
    constexpr float INTERPOLATION_FULL_WIDTH = 0.125f; ///< Vollständige Interpolation ab acht Pixeln je FFT-Bin
    constexpr float SPAN_DB = 48.0f; ///< Dynamikumfang der Wasserfall-Farbskala
    const uint16_t *const COLOR_STOPS = farbtabellen().wasserfall; ///< Pegelfarben von Stille bis zum höchsten Farbpegel (Vis.WasserfallSkala)
    constexpr int COLOR_SEGMENTS = sizeof(Farbtabellen::wasserfall) / sizeof(Farbtabellen::wasserfall[0]) - 1; ///< Anzahl der Farbübergänge

    if (!m_sprVU_Spectrum)
        return;

    if (init && !m_waterfall)
    {
        void *mem = heap_caps_malloc(sizeof(WaterfallData), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!mem)
            mem = malloc(sizeof(WaterfallData));
        if (!mem)
        {
            LOG_ERROR("Konnte Wasserfalldaten nicht allokieren!");
            return;
        }
        memset(mem, 0, sizeof(WaterfallData));
        m_waterfall.reset(static_cast<WaterfallData *>(mem));
    }
    if (!m_waterfall)
        return;

    auto &data = *m_waterfall;
    auto *buffer = static_cast<uint16_t *>(m_sprVU_Spectrum->getBuffer());
    if (!buffer)
        return;
    const int stride = m_sprVU_Spectrum->width();
    uint16_t *graph = buffer + m_labelH * stride;

    if (init)
    {
        data.targetLevels.fill(0);
        data.displayLevels.fill(0);
        data.activeRows = 0;
        data.sampleRate = 0;
        const uint16_t green = farbe(Rolle::VisPegelGruen); ///< Obere Stufe der grünen Farbskala
        for (size_t i = 0; i < data.colors.size(); ++i)
        {
            const float level = static_cast<float>(i) / (data.colors.size() - 1);
            uint16_t color;
            if (m_waterfallStyle == SpectrumStyle::CLASSIC)
            {
                color = blendColor(green, COLOR_STOPS[0], level);
            }
            else
            {
                const float position = level * COLOR_SEGMENTS;
                const int segment = static_cast<int>(position);
                color = segment < COLOR_SEGMENTS
                            ? blendColor(COLOR_STOPS[segment + 1], COLOR_STOPS[segment], position - segment)
                            : COLOR_STOPS[COLOR_SEGMENTS];
            }
            data.colors[i] = bswap16(color);
        }
        fillWaterfallRows(graph, GRAPH_H * stride, data.colors[0]);
        pushToCanvas(m_sprVU_Spectrum, m_areaX, m_areaY);
        return;
    }

    if (data.sampleRate != m_spectrumSampleRate)
    {
        const float freqToBin = m_spectrumSampleRate ? static_cast<float>(FFT_N) / m_spectrumSampleRate : 0.0f;
        const float nyquist = m_spectrumSampleRate * 0.5f;
        const float logRange = logf(SPECTRUM_MAX_HZ / SPECTRUM_MIN_HZ);
        for (int x = 0; x <= m_areaW; ++x)
        {
            const float frequency = SPECTRUM_MIN_HZ * expf(logRange * x / m_areaW);
            data.binEdges[x] = fminf(frequency, nyquist) * freqToBin + 0.5f;
        }
        data.sampleRate = m_spectrumSampleRate;
    }

    if (analyze)
    {
        const float dbHigh = vuAlignmentDb() + SPECTRUM_TOP_OVER_REFERENCE;
        const float dbLow = dbHigh - SPAN_DB;
        const float offset = 10.0f * log10f(SPECTRUM_SCALE_FACTOR * SPECTRUM_SCALE_FACTOR / FFT_N);
        for (int x = 0; x < m_areaW; ++x)
        {
            float power = 0.0f;
            if (hasSamples)
            {
                const float left = data.binEdges[x];
                const float right = data.binEdges[x + 1];
                const float width = right - left;
                if (width > 0.0f)
                {
                    const int firstBin = static_cast<int>(left);
                    const int lastBin = static_cast<int>(right);
                    for (int bin = firstBin; bin <= lastBin; ++bin)
                    {
                        // Auch ein schmaler Pixel erhält den überdeckten Anteil seiner FFT-Stützstelle.
                        const float weight = fminf(right, bin + 1.0f) - fmaxf(left, static_cast<float>(bin));
                        const float re = m_fft_output[bin].r;
                        const float im = m_fft_output[bin].i;
                        power += (re * re + im * im) * weight;
                    }
                    // Die mittlere Leistung pro Bin hält den Farbpegel unabhängig von der Pixelbandbreite.
                    power /= width;
                    if (width < INTERPOLATION_MAX_WIDTH)
                    {
                        // Stark vergrößerte FFT-Stützstellen erhalten einen Verlauf zwischen ihren Mitten.
                        const float position = (left + right) * 0.5f - 0.5f;
                        const int lowerBin = static_cast<int>(position);
                        const int upperBin = lowerBin < FFT_N / 2 ? lowerBin + 1 : lowerBin;
                        const float fraction = position - lowerBin;
                        const auto &lower = m_fft_output[lowerBin];
                        const auto &upper = m_fft_output[upperBin];
                        const float lowerPower = lower.r * lower.r + lower.i * lower.i;
                        const float upperPower = upper.r * upper.r + upper.i * upper.i;
                        const float interpolatedPower = lowerPower * (1.0f - fraction) + upperPower * fraction;
                        // Zwischen acht und vier Pixeln je FFT-Bin geht die Interpolation stufenlos in die Bandmittelung über.
                        const float interpolation = fminf(1.0f, (INTERPOLATION_MAX_WIDTH - width) / (INTERPOLATION_MAX_WIDTH - INTERPOLATION_FULL_WIDTH));
                        power = power * (1.0f - interpolation) + interpolatedPower * interpolation;
                    }
                }
            }
            float level = 0.0f;
            if (power > 1e-10f)
                level = fminf(1.0f, fmaxf(0.0f, (10.0f * log10f(power) + offset - dbLow) / SPAN_DB));
            data.targetLevels[x] = static_cast<uint8_t>(level * (data.colors.size() - 1));
        }
    }

    if (elapsed < m_waterfallFrameMs)
        return;

    const uint32_t steps = elapsed / m_waterfallFrameMs;
    const int rows = steps < GRAPH_H ? static_cast<int>(steps) : GRAPH_H;
    const int smoothingSteps = rows < SMOOTHING_BITS ? rows : SMOOTHING_BITS;
    bool rowVisible = false;
    for (int x = 0; x < m_areaW; ++x)
    {
        // Pro Zeilentakt halbiert sich die Pegeldifferenz, die Rundung zum Ziel beendet auch den Ausklang bei Stille.
        const int target = data.targetLevels[x];
        const int previous = data.displayLevels[x];
        const int distance = previous > target ? previous - target : target - previous;
        const int remainder = distance >> smoothingSteps;
        data.displayLevels[x] = target + (previous > target ? remainder : -remainder);
        rowVisible = rowVisible || data.colors[data.displayLevels[x]] != data.colors[0];
    }
    if (!rowVisible && data.activeRows == 0)
        return;

    memmove(graph + rows * stride, graph, (GRAPH_H - rows) * stride * sizeof(uint16_t));
    // Ausgelassene Zeitschritte bleiben leer, die Höhe des Verlaufs bleibt an die reale Zeit gebunden.
    fillWaterfallRows(graph, rows * stride, data.colors[0]);
    for (int x = 0; x < m_areaW; ++x)
        graph[x] = data.colors[data.displayLevels[x]];
    data.activeRows = rowVisible ? GRAPH_H : (data.activeRows > rows ? data.activeRows - rows : 0);
    pushToCanvas(m_sprVU_Spectrum, m_areaX, m_areaY);
}

void Visualizer::drawAurora(bool init, float rmsLeft, float rmsRight)
{
    constexpr uint32_t FRAME_MS = 33; ///< Höchstens etwa 30 Bilder pro Sekunde
    constexpr uint32_t SLICE_US = 25000; ///< Obergrenze je Aufruf, nach jeder Spalte geprüft. Greift nur, wenn der Oberflächentask verdrängt wird
    const int COLUMNS_PER_SLICE = (m_areaW + 3) / 4; ///< Ein Bild in vier Rechenaufrufen zu je rund 8,5 ms. Jeder Durchlauf der Oberflächenschleife kostet zusätzlich rund 3 ms, mehr Abschnitte dehnen die Bildzeit Richtung MAX_FRAME_MS und verlangsamen dann die Bewegung
    constexpr uint32_t MAX_FRAME_MS = 100; ///< Begrenzter Bewegungsschritt nach einer Zeichenpause
    constexpr float TAU = 6.28318530718f; ///< Vollkreis für die Wellenphasen
    constexpr float AUDIO_TIME_S = 4.0f; ///< Zeitkonstante der Audioreaktion
    constexpr float COLOR_TIME_S = 1.5f; ///< Zeitkonstante des Farbwechsels
    constexpr float FADE_TIME_S = 0.8f; ///< Zeitkonstante der Einblendung
    constexpr float AUDIO_LOW_DB = -50.0f; ///< Untere Grenze der sanften Musiksteuerung
    constexpr float AUDIO_HIGH_DB = -8.0f; ///< Obere Grenze der sanften Musiksteuerung
    constexpr float BASE_BRIGHTNESS = 0.40f; ///< Grundhelligkeit bei Stille
    constexpr float AUDIO_BRIGHTNESS = 0.25f; ///< Maximaler Helligkeitsbeitrag der Musik
    constexpr float BREATH_DEPTH = 0.15f; ///< Langsame Helligkeitsschwankung jedes Schleiers
    constexpr float AUDIO_WIDTH = 0.18f; ///< Maximale relative Verbreiterung durch den Musikpegel
    constexpr float CURVE_HEIGHT = 0.16f; ///< Höhe der langsamen Hauptwelle relativ zur Zeichenfläche
    constexpr float FOLD_HEIGHT = 0.055f; ///< Höhe der zweiten, flacheren Welle
    const float EDGE_WIDTH = gf(24.0f); ///< Weiche Ausblendung an den seitlichen Rändern in Pixeln, mit der Geometrie gestreckt
    constexpr float PROFILE_SIGMAS = 4.0f; ///< Reichweite des vorberechneten Leuchtprofils
    constexpr int POSITION_SCALE = 256; ///< Acht Nachkommabits für die Lage und Breite der Schleier
    constexpr int COLOR_SCALE = 16; ///< Vier Nachkommabits für die RGB-Koeffizienten
    constexpr int GLOW_MAX = 32767; ///< 15 Nachkommabits für die Leuchtstärke
    static constexpr uint8_t DITHER[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}}; ///< Ortsfestes Raster für Zwischenstufen im RGB565-Format

    /**
     * @brief Form und Bewegung eines Schleiers
     */
    struct Layer
    {
        float center; ///< Mittlere Höhenlage als Anteil der Zeichenfläche
        float width; ///< Breite des Leuchtprofils in Pixeln
        float cycles[2]; ///< Räumliche Wellenanzahl über die gesamte Breite
        float periods[2]; ///< Bewegungsperioden in Sekunden
        float start[2]; ///< Phasen beim Einblenden in Radiant
    };

    static constexpr Layer LAYERS[AURORA_LAYERS] = {
        {0.32f, 9.0f, {0.85f, 1.8f}, {12.0f, 15.0f}, {0.4f, 2.1f}},
        {0.50f, 11.0f, {1.1f, 2.2f}, {15.0f, 11.0f}, {2.5f, 4.3f}},
        {0.66f, 8.0f, {0.7f, 1.6f}, {9.0f, 14.0f}, {4.7f, 0.8f}}}; ///< Drei unabhängig bewegte Schleier, ihre kühle und warme RGB-Farbe stehen in Vis.AuroraKalt und Vis.AuroraWarm
    static_assert(sizeof(Farbtabellen::auroraKalt) / sizeof(Farbtabellen::auroraKalt[0]) == AURORA_LAYERS, "Je Schleier eine Farbe in Vis.AuroraKalt und Vis.AuroraWarm");

    if (!m_tft)
        return;

    if (init)
    {
        if (!m_sprAurora)
        {
            if (!m_tft->safeCreateSprite(m_sprAurora, m_areaW, m_areaH))
            {
                LOG_ERROR("Konnte Nordlicht-Sprite nicht erstellen!");
                return;
            }
        }
        if (!m_aurora)
        {
            void *mem = heap_caps_malloc(sizeof(AuroraData), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!mem)
                mem = malloc(sizeof(AuroraData));
            if (!mem)
            {
                LOG_ERROR("Konnte Nordlicht-Daten nicht allokieren!");
                return;
            }
            memset(mem, 0, sizeof(AuroraData));
            m_aurora.reset(static_cast<AuroraData *>(mem));
            for (int i = 0; i <= AURORA_GLOW_STEPS; ++i)
            {
                const float distance = PROFILE_SIGMAS * i / AURORA_GLOW_STEPS;
                m_aurora->glow[i] = static_cast<uint16_t>(GLOW_MAX * expf(-0.5f * distance * distance));
            }
            m_aurora->glow[AURORA_GLOW_STEPS] = 0;
            for (int y = 0; y < m_areaH; ++y)
            {
                const float envelope = sinf(0.5f * TAU * y / (m_areaH - 1));
                m_aurora->edgeFade[y] = static_cast<uint16_t>(POSITION_SCALE * envelope * envelope);
            }
        }
        for (int layer = 0; layer < AURORA_LAYERS; ++layer)
            for (int wave = 0; wave < 2; ++wave)
                m_aurora->phase[layer][wave] = LAYERS[layer].start[wave];
        m_aurora->audioLevel = 0.0f;
        m_aurora->warmMix = m_auroraStyle == AuroraStyle::WARM ? 1.0f : 0.0f;
        m_aurora->fadeIn = 0.0f;
        m_aurora->lastFrame = millis();
        m_aurora->rendering = false;
        m_aurora->nextColumn = 0;
        clearVisualizationArea();
    }
    if (!m_sprAurora || !m_aurora)
        return;

    auto &data = *m_aurora;
    const uint32_t now = millis();
    if (!init && !m_playing)
    {
        // Ohne frisches PCM, etwa während der Zeigerfahrt, bleibt das Bild stehen. Die verstrichene Zeit wird beim Fortsetzen nicht nachgeholt
        data.lastFrame = now;
        data.rendering = false;
        data.nextColumn = 0;
        return;
    }
    const uint32_t sliceStart = micros();
    if (!data.rendering)
    {
        const uint32_t elapsed = now - data.lastFrame;
        if (!init && elapsed < FRAME_MS)
            return;
        data.lastFrame = now;
        const float dt = (elapsed < MAX_FRAME_MS ? elapsed : MAX_FRAME_MS) * 0.001f;
        float targetLevel = 0.0f;
        if (m_playing)
        {
            const float db = amplitudeToDb(fmaxf(rmsLeft, rmsRight));
            targetLevel = fminf(1.0f, fmaxf(0.0f, (db - AUDIO_LOW_DB) / (AUDIO_HIGH_DB - AUDIO_LOW_DB)));
        }
        data.audioLevel += (targetLevel - data.audioLevel) * (1.0f - expf(-dt / AUDIO_TIME_S));
        const float targetWarmth = m_auroraStyle == AuroraStyle::WARM ? 1.0f : 0.0f;
        data.warmMix += (targetWarmth - data.warmMix) * (1.0f - expf(-dt / COLOR_TIME_S));
        data.fadeIn += (1.0f - data.fadeIn) * (1.0f - expf(-dt / FADE_TIME_S));

        const Farbtabellen &tabellen = farbtabellen();
        for (int layer = 0; layer < AURORA_LAYERS; ++layer)
        {
            for (int wave = 0; wave < 2; ++wave)
            {
                data.phase[layer][wave] += TAU * dt / LAYERS[layer].periods[wave];
                if (data.phase[layer][wave] >= TAU)
                    data.phase[layer][wave] -= TAU;
                data.waveSin[layer][wave] = sinf(data.phase[layer][wave]);
                data.waveCos[layer][wave] = cosf(data.phase[layer][wave]);
                const float step = TAU * LAYERS[layer].cycles[wave] / (m_areaW - 1);
                data.stepSin[layer][wave] = sinf(step);
                data.stepCos[layer][wave] = cosf(step);
            }
            const float brightness = data.fadeIn * (BASE_BRIGHTNESS + AUDIO_BRIGHTNESS * data.audioLevel) * (1.0f - BREATH_DEPTH + BREATH_DEPTH * data.waveSin[layer][1]);
            const float width = gf(LAYERS[layer].width) * (1.0f + AUDIO_WIDTH * data.audioLevel);
            data.profileScale[layer] = static_cast<int>(POSITION_SCALE * AURORA_GLOW_STEPS / (PROFILE_SIGMAS * width));
            const uint8_t *cool = tabellen.auroraKalt[layer];
            const uint8_t *warm = tabellen.auroraWarm[layer];
            for (int channel = 0; channel < 3; ++channel)
                data.colors[layer][channel] = COLOR_SCALE * brightness * (cool[channel] + data.warmMix * (warm[channel] - cool[channel]));
        }
        data.nextColumn = 0;
        data.rendering = true;
    }

    if (data.nextColumn == m_areaW)
    {
        // Die Bildübertragung belegt einen eigenen Aufruf ohne weitere Pixelberechnung, damit ein Aufruf das Zeitbudget nicht sprengt
        pushToCanvas(m_sprAurora, m_areaX, m_areaY);
        data.rendering = false;
        return;
    }

    auto *buffer = static_cast<uint16_t *>(m_sprAurora->getBuffer());
    if (!buffer)
        return;
    const int stride = m_sprAurora->width();
    // Auf hellem Grund sind die Schleier Tinte: Das berechnete Licht wird je Kanal vom Grund abgezogen statt auf Schwarz addiert.
    const bool ink = themaAktiv().merkmale & MerkmalHellerGrund;
    const uint16_t paper = farbe(Rolle::GemGrund);
    const uint32_t paperRed = paper >> 11, paperGreen = (paper >> 5) & 63, paperBlue = paper & 31;
    int columns = 0;
    while (data.nextColumn < m_areaW)
    {
        const int x = data.nextColumn;
        const int edgeDistance = x < m_areaW - 1 - x ? x : m_areaW - 1 - x;
        const float edge = fminf(1.0f, edgeDistance / EDGE_WIDTH);
        const float edgeFade = edge * edge * (3.0f - 2.0f * edge);
        int center[AURORA_LAYERS];
        uint16_t columnColors[AURORA_LAYERS][3];
        for (int layer = 0; layer < AURORA_LAYERS; ++layer)
        {
            const float height = LAYERS[layer].center + CURVE_HEIGHT * data.waveSin[layer][0] + FOLD_HEIGHT * data.waveSin[layer][1];
            center[layer] = static_cast<int>((m_areaH - 1) * POSITION_SCALE * height);
            for (int channel = 0; channel < 3; ++channel)
                columnColors[layer][channel] = static_cast<uint16_t>(data.colors[layer][channel] * edgeFade);
            // Die Additionstheoreme verschieben beide Wellen ohne trigonometrische Aufrufe im Pixelpfad.
            for (int wave = 0; wave < 2; ++wave)
            {
                const float sine = data.waveSin[layer][wave];
                const float cosine = data.waveCos[layer][wave];
                data.waveSin[layer][wave] = sine * data.stepCos[layer][wave] + cosine * data.stepSin[layer][wave];
                data.waveCos[layer][wave] = cosine * data.stepCos[layer][wave] - sine * data.stepSin[layer][wave];
            }
        }
        for (int y = 0; y < m_areaH; ++y)
        {
            uint32_t red = 0, green = 0, blue = 0;
            for (int layer = 0; layer < AURORA_LAYERS; ++layer)
            {
                int distance = y * POSITION_SCALE - center[layer];
                if (distance < 0)
                    distance = -distance;
                const int index = (distance * data.profileScale[layer]) >> 16;
                if (index >= AURORA_GLOW_STEPS)
                    continue;
                const uint32_t glow = (static_cast<uint32_t>(data.glow[index]) * data.edgeFade[y]) >> 8;
                red += glow * columnColors[layer][0];
                green += glow * columnColors[layer][1];
                blue += glow * columnColors[layer][2];
            }
            // RGB-Koeffizienten mit vier und Leuchtwerte mit 15 Nachkommabits ergeben 19 Nachkommabits vor der RGB565-Quantisierung.
            const uint32_t dither = 2 * DITHER[y & 3][x & 3] + 1;
            red = (red + (dither << 17)) >> 22;
            green = (green + (dither << 16)) >> 21;
            blue = (blue + (dither << 17)) >> 22;
            if (red > 31)
                red = 31;
            if (green > 63)
                green = 63;
            if (blue > 31)
                blue = 31;
            if (ink)
            {
                red = paperRed > red ? paperRed - red : 0;
                green = paperGreen > green ? paperGreen - green : 0;
                blue = paperBlue > blue ? paperBlue - blue : 0;
            }
            buffer[y * stride + x] = bswap16(static_cast<uint16_t>((red << 11) | (green << 5) | blue));
        }
        ++data.nextColumn;
        ++columns;
        if (columns >= COLUMNS_PER_SLICE || static_cast<uint32_t>(micros() - sliceStart) >= SLICE_US)
            return;
    }
}

void Visualizer::resetWaveTrace()
{
    m_waveNextX = 0;
    m_waveColumnRestMs = 0;
    m_waveHasSamples = false;
    m_waveAmp = 1.0f;
    m_wavePeakHistoryIdx = 0;
    memset(m_wavePeakHistory, 0, sizeof(m_wavePeakHistory));

    const bool stacked = m_waveformStyle == WaveformStyle::STACKED;
    const uint16_t grid = farbe(Rolle::VisRaster);
    m_sprVU_Wave->clear(farbe(Rolle::GemGrund));
    lineH(m_sprVU_Wave, 0, stacked ? m_areaH / 4 : m_areaH / 2, m_areaW, grid);
    if (stacked)
        lineH(m_sprVU_Wave, 0, 3 * m_areaH / 4, m_areaW, grid);
    pushToCanvas(m_sprVU_Wave, m_areaX, m_areaY);
}

void Visualizer::drawWave(bool init)
{
    if (!m_tft)
        return;

    const uint32_t now = millis();
    const uint32_t elapsed = now - m_lastWaveUpdateMs;
    if (!init && m_waveNextX >= 0 && elapsed < BAR_FRAME_MS)
        return;

    if (init)
    {
        // Ein Sprite wird synchron in die logische Zeichenfläche kopiert, der Displaytransfer folgt in VisualizerView
        if (!m_sprVU_Wave && !m_tft->safeCreateSprite(m_sprVU_Wave, m_areaW, m_areaH))
        {
            LOG_ERROR("Konnte Wellenform-Sprite nicht erstellen!");
            return;
        }
        clearVisualizationArea();
    }

    if (!m_sprVU_Wave)
        return;
    if (init || m_waveNextX < 0)
    {
        m_lastWaveUpdateMs = now;
        resetWaveTrace();
        return;
    }

    // Ohne neuen Sampleblock bleibt die Spur stehen, die verstrichene Zeit wird beim nächsten Block nachgeholt
    const uint32_t sampleSeq = m_sampleSeq.load(std::memory_order_acquire);
    if (m_playing && m_waveHasSamples && sampleSeq == m_waveDrawnSeq)
        return;

    const uint32_t span = m_waveHasSamples ? std::min(elapsed, WAVE_MAX_SPAN_MS) : BAR_FRAME_MS;
    const uint32_t projectedMs = span + m_waveColumnRestMs;
    const int columns = std::min(static_cast<int>(projectedMs / m_waveColumnMs), WAVE_MAX_COLUMNS);

    struct Column
    {
        int16_t minL, maxL, minR, maxR;
    };

    Column peaks[WAVE_MAX_COLUMNS] = {};
    bool hasSamples = false;
    if (m_playing)
    {
        // Der Writer wird nur während der Übernahme der Ausschläge am Puffertausch gehindert
        ReaderGuard guard(m_readerActive);
        const uint8_t readIdx = m_writeBufferIdx.load(std::memory_order_acquire) ^ 1;
        const int16_t *samplesL = m_samplesL[readIdx].get();
        const int16_t *samplesR = m_samplesR[readIdx].get();
        const int count = m_sampleCount[readIdx];
        hasSamples = samplesL && samplesR && count >= 2 && m_sampleRate[readIdx] > 0;
        if (hasSamples)
        {
            // Der Block verteilt sich gleichmäßig auf die neuen Spalten, je Spalte zählen kleinster und größter Wert
            for (int x = 0; x < columns; ++x)
            {
                const int first = x * count / columns;
                const int end = std::max(first + 1, (x + 1) * count / columns);
                Column &peak = peaks[x];
                for (int i = first; i < end; ++i)
                {
                    const int16_t l = samplesL[i];
                    const int16_t r = samplesR[i];
                    peak.minL = std::min(peak.minL, l);
                    peak.maxL = std::max(peak.maxL, l);
                    peak.minR = std::min(peak.minR, r);
                    peak.maxR = std::max(peak.maxR, r);
                }
            }
        }
    }

    m_lastWaveUpdateMs = now;
    if (!hasSamples)
    {
        if (m_waveHasSamples)
            resetWaveTrace();
        return;
    }

    m_waveHasSamples = true;
    m_waveDrawnSeq = sampleSeq;
    m_waveColumnRestMs = projectedMs % m_waveColumnMs;

    // Die Auto-Verstärkung zielt auf 90 % Aussteuerung der Spitzen der letzten 90 Blöcke
    int peak = 0;
    for (int x = 0; x < columns; ++x)
    {
        const Column &p = peaks[x];
        const int negativePeak = -static_cast<int>(std::min(p.minL, p.minR));
        const int positivePeak = std::max(p.maxL, p.maxR);
        peak = std::max(peak, std::max(negativePeak, positivePeak));
    }
    m_wavePeakHistory[m_wavePeakHistoryIdx] = peak / 32768.0f;
    m_wavePeakHistoryIdx = (m_wavePeakHistoryIdx + 1) % WAVE_PEAK_HISTORY_SIZE;
    float windowPeak = 0.0f;
    for (float value : m_wavePeakHistory)
        windowPeak = std::max(windowPeak, value);
    if (windowPeak > 0.001f)
    {
        // Bei Übersteuerung schnell, bei leisem Signal langsam und sonst mittelschnell nachregeln
        const float target = 0.9f / windowPeak;
        if (windowPeak * m_waveAmp > 1.05f)
            m_waveAmp += (target * 0.95f - m_waveAmp) * 0.3f;
        else if (target > m_waveAmp)
            m_waveAmp += (target - m_waveAmp) * 0.08f;
        else
            m_waveAmp += (target - m_waveAmp) * 0.12f;
        m_waveAmp = std::clamp(m_waveAmp, 0.1f, 100.0f);
    }

    const bool stacked = m_waveformStyle == WaveformStyle::STACKED;
    const int midL = stacked ? m_areaH / 4 : m_areaH / 2;
    const int midR = stacked ? 3 * m_areaH / 4 : midL;
    const int stroke = gl(1); // Strichstärke des Originals: 1 Pixel, so stark wie die Mittellinie
    const int height = midL - stroke;
    const float scale = m_waveAmp * height / 32768.0f;
    auto offset = [scale, height](int16_t value)
    {
        return std::clamp(static_cast<int>(lroundf(value * scale)), -height, height);
    };

    // Jede neue Spalte wird gelöscht und mit den Ausschlägen gefüllt, links über rechts. Die Lücke davor markiert die Schreibposition
    const uint16_t ground = farbe(Rolle::GemGrund), colorLeft = farbe(Rolle::VisWelleLinks), colorRight = farbe(Rolle::VisWelleRechts);
    const int dirty = columns + gl(WAVE_CURSOR_GAP);
    for (int i = 0; i < dirty; ++i)
    {
        const int x = (m_waveNextX + i) % m_areaW;
        m_sprVU_Wave->drawFastVLine(x, 0, m_areaH, ground);
        if (i >= columns)
            continue;
        const Column &p = peaks[i];
        const int topL = midL - offset(p.maxL);
        const int topR = midR - offset(p.maxR);
        m_sprVU_Wave->drawFastVLine(x, topR, midR - offset(p.minR) - topR + stroke, colorRight);
        m_sprVU_Wave->drawFastVLine(x, topL, midL - offset(p.minL) - topL + stroke, colorLeft);
    }

    // In die Zeichenfläche gehen nur die neuen Spalten samt Lücke, am rechten Rand in zwei getrennten Rechtecken
    auto pushColumns = [this](int x, int width)
    {
        m_tft->setClipRect(m_areaX + x, m_areaY, width, m_areaH);
        pushToCanvas(m_sprVU_Wave, m_areaX, m_areaY);
    };
    const int firstWidth = std::min(dirty, m_areaW - m_waveNextX);
    pushColumns(m_waveNextX, firstWidth);
    if (firstWidth < dirty)
        pushColumns(0, dirty - firstWidth);
    m_tft->clearClipRect();
    m_waveNextX = (m_waveNextX + columns) % m_areaW;
}

void Visualizer::drawGonioStaticElements(Gfx_Sprite *spr, const GonioConfig &cfg)
{
    spr->setFont(m_smallFont);
    const int stroke = gl(1); // Strichstärke des Originals: 1 Pixel
    const uint16_t grid = farbe(Rolle::VisRaster), ground = farbe(Rolle::GemGrund);

    const int16_t crossPad = cfg.size * 19 / 126;
    const int16_t crossEnd = cfg.size - crossPad;
    for (int o = 0; o < stroke; ++o)
    {
        spr->drawLine(crossPad + o, crossPad, crossEnd + o, crossEnd, grid);
        spr->drawLine(crossEnd + o, crossPad, crossPad + o, crossEnd, grid);
    }

    // Die unterbrochenen Balken lassen die Mitte frei
    const int16_t midStart = cfg.size * 57 / 126;
    const int16_t midEnd = cfg.size * 69 / 126;
    const int16_t barPos = cfg.size * 61 / 126;
    const int16_t barLen = cfg.size * 3 / 126;

    for (int16_t i = 0; i < cfg.size; i += gl(5))
    {
        if (i < midStart || i > midEnd)
        {
            lineH(spr, barPos, i, barLen, grid);
            lineV(spr, i, barPos, barLen, grid);
        }
    }

    spr->setTextColor(farbe(Rolle::VisGonioInfoText), ground);
    const int16_t labelTop = cfg.size * 12 / 126;
    const int16_t labelMidY = cfg.size * 5 / 126;
    spr->drawString("L", cfg.size * 14 / 126, labelTop);
    spr->drawString("M", cfg.center - gl(5), labelMidY);
    spr->drawString("R", cfg.size * 107 / 126, labelTop);

    spr->setTextColor(farbe(Rolle::VisGonioInfoTitel), ground);
    const int16_t labelBottom = (cfg.size * 107 / 126) - gl(2);
    spr->drawString("-1", cfg.size * 5 / 126, labelBottom);
    spr->drawString("0", cfg.corrCenter - gl(3), labelBottom);
    spr->drawString("+1", (cfg.size * 107 / 126) - gl(1), labelBottom);

    const int16_t barX = cfg.size * 5 / 126;
    const int16_t barH = cfg.size * 5 / 126;
    spr->fillRect(barX, cfg.corrBarY, cfg.corrBarW, barH, grid);
    lineV(spr, cfg.corrCenter, cfg.corrBarY, barH, farbe(Rolle::VisKorrelationMitte));

    for (int o = 0; o < stroke; ++o)
        spr->drawRect(o, o, cfg.size - 2 * o, cfg.size - 2 * o, grid);
}

void Visualizer::lineH(Gfx_Sprite *spr, int x, int y, int w, uint16_t color) const
{
    if (gl(1) == 1)
        spr->drawFastHLine(x, y, w, color);
    else
        spr->fillRect(x, y, w, gl(1), color);
}

void Visualizer::lineV(Gfx_Sprite *spr, int x, int y, int h, uint16_t color) const
{
    if (gl(1) == 1)
        spr->drawFastVLine(x, y, h, color);
    else
        spr->fillRect(x, y, gl(1), h, color);
}

void Visualizer::gonioSquareRect(int index, int &x, int &y, int &w, int &h) const
{
    const int base = (index < 2 ? baseXL : baseXR) + (index % 2) * (SQW + GAP);
    x = infoX(base);
    w = infoX(base + SQW) - x;
    y = infoY(baseY);
    h = infoY(baseY + SQH) - y;
}

void Visualizer::drawGonio(bool init, float peakLeft, float peakRight)
{
    if (!m_tft)
        return;

    // Der Guard signalisiert dem Writer, dass gerade gelesen wird
    ReaderGuard guard(m_readerActive);

    const uint8_t readIdx = m_writeBufferIdx.load(std::memory_order_acquire) ^ 1;
    const int16_t *samplesL = m_samplesL[readIdx].get();
    const int16_t *samplesR = m_samplesR[readIdx].get();
    uint16_t sampleCount = m_sampleCount[readIdx];

    if (!samplesL || !samplesR)
        return;

    if (!m_playing)
        sampleCount = 0;

    // Ohne neue Samplemarke bleibt die Figur unverändert. Nur eine erzwungene Initialisierung oder ein Wechsel des Wiedergabezustands (Pause, Titelende, Aussetzer) zeichnet erneut
    const uint32_t sampleSeq = m_sampleSeq.load(std::memory_order_acquire);
    if (!init && sampleSeq == m_gonioDrawnSeq && m_playing == m_gonioDrawnPlaying)
        return;

    m_gonioDrawnSeq = sampleSeq;
    m_gonioDrawnPlaying = m_playing;

    const GonioConfig &cfg = m_gonio;

    if (init)
    {
        // Ein Sprite wird synchron in die logische Zeichenfläche kopiert, der Displaytransfer folgt in VisualizerView
        if (!m_sprGonio)
        {
            if (!m_tft->safeCreateSprite(m_sprGonio, cfg.size, cfg.size))
            {
                LOG_ERROR("Konnte m_sprGonio Sprite nicht erstellen!");
                return;
            }
        }

        if (!m_sprGonioBg)
        {
            if (!m_tft->safeCreateSprite(m_sprGonioBg, cfg.size, cfg.size))
            {
                LOG_ERROR("Konnte m_sprGonioBg Sprite nicht erstellen!");
                return;
            }
        }

        if (!m_sprGonioInfo)
        {
            if (!m_tft->safeCreateSprite(m_sprGonioInfo, infoX(GONIO_INFO_W), infoY(VIS_AREA_H)))
            {
                LOG_ERROR("Konnte m_sprGonioInfo Sprite nicht erstellen!");
                return;
            }
        }

        clearVisualizationArea();

        const uint16_t panel = farbe(Rolle::GemFlaeche), border = farbe(Rolle::GemRahmen), marker = farbe(Rolle::VisGonioInfoMarke), standby = farbe(Rolle::VisPegelGrund);
        m_sprGonioInfo->clear(panel);
        m_sprGonioInfo->setFont(&m_font);

        m_sprGonioInfo->setTextColor(farbe(Rolle::VisGonioInfoPeak));
        m_sprGonioInfo->drawString("PEAK", infoX(88), infoY(52));

        // Das Label des Verstärkungsfaktors steht fest. Die Wertposition folgt aus seiner Breite, die je nach Sprache wechselt
        m_sprGonioInfo->setTextColor(farbe(Rolle::VisGonioInfoText));
        m_sprGonioInfo->setTextDatum(textdatum_t::top_left);
        lineV(m_sprGonioInfo, 0, 0, infoY(VIS_AREA_H), border);

        const __FlashStringHelper *gainLabel = i18n_getRetroRadio(RetroRadioKey::RETRO_VIS_GAIN_LABEL, m_language);
        m_sprGonioInfo->drawString(gainLabel, infoX(11), infoY(5));
        const int16_t labelW = m_sprGonioInfo->textWidth(gainLabel);
        m_gainValueX = infoX(11) + labelW + gl(5);
        m_sprGonioInfo->drawString("x1.0", m_gainValueX, infoY(5));

        int sx, sy, sw, sh;
        m_sprGonioInfo->drawString(STR(RetroRadioKey::RETRO_VIS_GONIO_LEFT), infoX(34), infoY(32));
        lineH(m_sprGonioInfo, infoX(11), infoY(46), infoX(11 + 2 * SQW + GAP) - infoX(11), marker);
        gonioSquareRect(0, sx, sy, sw, sh);
        drawGonioSquare(sx, sy, sw, sh, standby);
        gonioSquareRect(1, sx, sy, sw, sh);
        drawGonioSquare(sx, sy, sw, sh, standby);

        m_sprGonioInfo->drawString(STR(RetroRadioKey::RETRO_VIS_GONIO_RIGHT), infoX(138), infoY(32));
        lineH(m_sprGonioInfo, infoX(121), infoY(46), infoX(121 + 2 * SQW + GAP) - infoX(121), marker);
        gonioSquareRect(2, sx, sy, sw, sh);
        drawGonioSquare(sx, sy, sw, sh, standby);
        gonioSquareRect(3, sx, sy, sw, sh);
        drawGonioSquare(sx, sy, sw, sh, standby);

        pushToCanvas(m_sprGonioInfo, gx(GONIO_INFO_X), gy(VIS_AREA_Y));

        // Der Streifen rechts vom Info-Sprite bis zum Rand wird in dessen Hintergrund aufgefüllt, mit Randlinie
        m_tft->fillRect(gx(713), gy(VIS_AREA_Y), gx(800) - gx(713), infoY(VIS_AREA_H), panel);

        if (gl(1) == 1)
            m_tft->drawFastVLine(gx(799), gy(VIS_AREA_Y), infoY(VIS_AREA_H), border);
        else
            m_tft->fillRect(gx(800) - gl(1), gy(VIS_AREA_Y), gl(1), infoY(VIS_AREA_H), border);

        m_gonioAmp = 1.0f;
        m_gonioCorrPrev = 0.0f;
        m_gonioLastDynamicsUpdate = millis();

        for (uint8_t i = 0; i < GONIO_PEAK_HISTORY_SIZE; i++)
            m_gonioPeakHistory[i] = 0.0f;

        m_gonioPeakHistoryIdx = 0;
        m_gonioSlidingMax = 0.0f;

        m_lastSquareStateL1 = false;
        m_lastSquareStateL2 = false;
        m_lastSquareStateR1 = false;
        m_lastSquareStateR2 = false;

        // Ein ungültiger Wert erzwingt das erste Zeichnen
        m_lastDrawnGain = -999.0f;

        // Der unveränderliche Untergrund wird einmal aufgebaut und danach in jedes Bild kopiert, statt Kreuz, Balken und Beschriftungen jedes Mal neu zu zeichnen
        m_sprGonioBg->clear(farbe(Rolle::GemGrund));
        drawGonioStaticElements(m_sprGonioBg, cfg);

        Gfx_Sprite *spr = m_sprGonio;
        m_sprGonioBg->pushSprite(spr, 0, 0);

        pushToCanvas(spr, cfg.posX, cfg.posY);

        return;
    }

    Gfx_Sprite *current_sprite = m_sprGonio;

    // Der Untergrund enthält bereits Kreuz, Teilbalken und Beschriftungen. Er wird roh kopiert, weil beide Sprites
    // gleich groß und gleich tief sind, pushSprite ginge durch die Farbumsetzung von LovyanGFX und wäre dafür unnötig teuer.
    memcpy(current_sprite->getBuffer(), m_sprGonioBg->getBuffer(),
           static_cast<size_t>(cfg.size) * cfg.size * sizeof(uint16_t));

    // Es werden höchstens die 1024 jüngsten Stereopaare verarbeitet
    constexpr uint16_t GONIO_N = 1024;
    const uint16_t take = (sampleCount >= GONIO_N) ? GONIO_N : sampleCount;
    const uint16_t start = (sampleCount > take) ? (sampleCount - take) : 0;

    if (take == 0)
    {
        // Ohne Samples zeigt das Bild nur den Untergrund. Die Infofläche geht auf Ruhewerte (kein Signal, x1.0)
        pushToCanvas(current_sprite, cfg.posX, cfg.posY);
        drawGonioInfo(m_gonioAmp, 0.0f, 0.0f);
        return;
    }

    float sum_lr = 0.0f, sum_l2 = 0.0f, sum_r2 = 0.0f, corr = 0.0f;

    // Spitzenwerte der unverstärkten Samples für die Auto-Verstärkung
    int32_t maxSampleL = 0;
    int32_t maxSampleR = 0;

    // Fester optischer Füllgrad: Die Figur belegt höchstens etwa 90 % des verfügbaren Radius
    constexpr float plotFill = 0.90f;
    const float screenScale = fminf((float)cfg.center, (float)cfg.scale) * plotFill;
    constexpr float SAMPLE_TO_NORMALIZED = 1.0f / 32767.0f;
    const float plotSampleScale = m_gonioAmp * SAMPLE_TO_NORMALIZED;

    uint16_t *buffer = (uint16_t *)current_sprite->getBuffer();
    if (!buffer)
        return;
    const uint16_t pointColor = farbe(Rolle::VisGonioPunkt);

    for (uint16_t i = 0; i < take; i++)
    {
        const uint16_t idx = start + i;

        const int32_t sampleL = samplesL[idx];
        const int32_t sampleR = samplesR[idx];
        const float sampleValueL = static_cast<float>(sampleL);
        const float sampleValueR = static_cast<float>(sampleR);

        const float normL = sampleValueL * plotSampleScale;
        const float normR = sampleValueR * plotSampleScale;

        const int32_t absSampleL = (sampleL < 0) ? -sampleL : sampleL;
        const int32_t absSampleR = (sampleR < 0) ? -sampleR : sampleR;

        if (absSampleL > maxSampleL)
            maxSampleL = absSampleL;

        if (absSampleR > maxSampleR)
            maxSampleR = absSampleR;

        // Gemeinsame Normalisierung und Darstellungsverstärkung kürzen sich aus der Korrelation heraus
        sum_lr += sampleValueL * sampleValueR;
        sum_l2 += sampleValueL * sampleValueL;
        sum_r2 += sampleValueR * sampleValueR;

        // Mid/Side-Geometrie: x trägt die Seitenanteile, y die Mitte
        const float xNorm = (normL - normR) * 0.5f;
        const float yNorm = (normL + normR) * 0.5f;

        const int16_t x = cfg.center - (int16_t)(xNorm * screenScale);
        const int16_t y = cfg.center - (int16_t)(yNorm * screenScale);

        if (x < 0 || x >= cfg.size || y < 0 || y >= cfg.size)
            continue;

        // Der Stil färbt nur den VU-Balken daneben, die Figur bleibt in jeder Fassung einfarbig.
        buffer[y * cfg.size + x] = bswap16(pointColor);
    }

    // Die Auto-Verstärkung wirkt erst im nächsten Bild
    const int32_t maxSample = (maxSampleL > maxSampleR) ? maxSampleL : maxSampleR;
    const float maxPeak = static_cast<float>(maxSample) * SAMPLE_TO_NORMALIZED;

    const uint32_t now = millis();
    float dynamicsDeltaTime = (now - m_gonioLastDynamicsUpdate) * 0.001f;
    m_gonioLastDynamicsUpdate = now;
    if (dynamicsDeltaTime > 0.1f)
        dynamicsDeltaTime = 0.1f;

    if (now - m_gonioLastPeakUpdate >= 33)
    {
        m_gonioLastPeakUpdate = now;

        m_gonioPeakHistory[m_gonioPeakHistoryIdx] = maxPeak;
        m_gonioPeakHistoryIdx = (m_gonioPeakHistoryIdx + 1) % GONIO_PEAK_HISTORY_SIZE;

        m_gonioSlidingMax = 0.0f;
        for (uint8_t i = 0; i < GONIO_PEAK_HISTORY_SIZE; i++)
            if (m_gonioPeakHistory[i] > m_gonioSlidingMax)
                m_gonioSlidingMax = m_gonioPeakHistory[i];
    }

    const float effectivePeak = (m_gonioSlidingMax > 0.001f) ? m_gonioSlidingMax : maxPeak;

    if (effectivePeak > 0.001f)
    {
        const float target_amp = 1.0f / effectivePeak;
        constexpr float GAIN_OVERLOAD_RATE = 9.0f;
        constexpr float GAIN_RISE_RATE = 2.4f;
        constexpr float GAIN_FALL_RATE = 3.6f;
        float gainTarget;
        float gainRate;

        if (effectivePeak * m_gonioAmp > 1.05f)
        {
            gainTarget = target_amp * 0.95f;
            gainRate = GAIN_OVERLOAD_RATE;
        }
        else if (target_amp > m_gonioAmp)
        {
            gainTarget = target_amp;
            gainRate = GAIN_RISE_RATE;
        }
        else
        {
            gainTarget = target_amp;
            gainRate = GAIN_FALL_RATE;
        }

        const float gainStep = fminf(gainRate * dynamicsDeltaTime, 1.0f);
        m_gonioAmp += (gainTarget - m_gonioAmp) * gainStep;

        if (m_gonioAmp > 500.0f)
            m_gonioAmp = 500.0f;

        if (m_gonioAmp < 0.1f)
            m_gonioAmp = 0.1f;
    }

    // Die Korrelation folgt dem Ziel mit begrenzter Geschwindigkeit
    if (take > 0 && (sum_l2 + sum_r2 > 0.0f))
        corr = sum_lr / (sum_l2 + sum_r2);
    else
        corr = 0.0f;

    float correlationTarget = corr * 2.0f;
    if (correlationTarget > 1.0f)
        correlationTarget = 1.0f;
    else if (correlationTarget < -1.0f)
        correlationTarget = -1.0f;

    constexpr float CORRELATION_RATE = 0.3f;
    const float maxCorrelationStep = CORRELATION_RATE * dynamicsDeltaTime;
    const float correlationDelta = correlationTarget - m_gonioCorrPrev;
    if (correlationDelta > maxCorrelationStep)
        m_gonioCorrPrev += maxCorrelationStep;
    else if (correlationDelta < -maxCorrelationStep)
        m_gonioCorrPrev -= maxCorrelationStep;
    else
        m_gonioCorrPrev = correlationTarget;

    const int16_t corrPos = cfg.corrCenter + (int16_t)(cfg.corrRange * m_gonioCorrPrev);
    Rolle corBox = Rolle::GemHinweis;

    if (corrPos > cfg.corrCenter + (cfg.corrRange / 8))
        corBox = Rolle::VisGonioKorrelation;
    else if (corrPos < cfg.corrCenter - (cfg.corrRange / 8))
        corBox = Rolle::VisPegelRot;

    const int16_t boxW = cfg.size * 4 / 126;
    const int16_t barH = cfg.size * 5 / 126;
    current_sprite->fillRect(corrPos - (boxW / 2), cfg.corrBarY, boxW, barH, farbe(corBox));

    pushToCanvas(current_sprite, cfg.posX, cfg.posY);

    drawGonioInfo(m_gonioAmp, peakLeft, peakRight);
}

void Visualizer::drawGonioVU(bool init, float rmsLeft, float rmsRight)
{
    if (!m_tft)
        return;

    if (!m_playing)
    {
        rmsLeft = rmsRight = 0.0f;
        m_gonioVUValL = m_gonioVUValR = 0.0f;
        m_gonioVUPeakL = m_gonioVUPeakR = 0.0f;
    }

    const GonioConfig &cfg = m_gonio;
    // Lage der drei Streifen hinter dem Goniometer über ihre Originalkanten 475, 483, 497 und 505
    const int leftX = gx(475), middleX = gx(483), rightX = gx(497);
    const int barW = middleX - leftX, middleW = rightX - middleX;

    if (init)
    {
        if (!m_sprGonioVULeft)
        {
            if (!m_tft->safeCreateSprite(m_sprGonioVULeft, barW, cfg.size))
            {
                LOG_ERROR("Konnte Gonio VU-Left Sprite nicht erstellen!");
                return;
            }
        }
        if (!m_sprGonioVUMiddle)
        {
            if (!m_tft->safeCreateSprite(m_sprGonioVUMiddle, middleW, cfg.size))
            {
                LOG_ERROR("Konnte Gonio VU-Middle Sprite nicht erstellen!");
                return;
            }
        }
        if (!m_sprGonioVURight)
        {
            if (!m_tft->safeCreateSprite(m_sprGonioVURight, gx(505) - rightX, cfg.size))
            {
                LOG_ERROR("Konnte Gonio VU-Right Sprite nicht erstellen!");
                return;
            }
        }

        const uint16_t ground = farbe(Rolle::GemGrund), label = farbe(Rolle::GemTextSilber), zero = farbe(Rolle::VisPegelRot), tick = farbe(Rolle::VisSkalaLinie);
        m_sprGonioVUMiddle->clear(ground);
        m_sprGonioVUMiddle->setFont(m_smallFont);
        m_sprGonioVUMiddle->setTextColor(label, ground);

        const float dbRange = VU_DB_HIGH - VU_DB_LOW;

        // Ein Wert, der auf der Skala nicht vorkommt: Stünde hier 0, fiele ausgerechnet die oberste Zeile durch,
        // denn da die Skala bei Vollaussteuerung endet, ist das die 0-dB-Zeile.
        int lastTick = static_cast<int>(VU_DB_HIGH) + 1;
        for (uint8_t y = 0; y < cfg.size; ++y)
        {
            int dB = roundf(VU_DB_HIGH - (y * dbRange / cfg.size));
            if (dB != lastTick)
            {
                const int below = y + gl(2), above = y - gl(10);
                if (dB == 0)
                {
                    lineH(m_sprGonioVUMiddle, 0, y, middleW, zero);
                    m_sprGonioVUMiddle->setTextColor(zero, ground);
                    m_sprGonioVUMiddle->drawString("0", gl(4), below);
                    m_sprGonioVUMiddle->setTextColor(label, ground);
                }

                if (dB == -5)
                {
                    lineH(m_sprGonioVUMiddle, 0, y, middleW, tick);
                    m_sprGonioVUMiddle->drawString("5", gl(4), below);
                }

                if (dB == -10)
                {
                    lineH(m_sprGonioVUMiddle, 0, y, middleW, tick);
                    m_sprGonioVUMiddle->drawString("10", 0, (VU_DB_LOW == -10) ? above : below);
                }

                if (dB == -20)
                {
                    lineH(m_sprGonioVUMiddle, 0, y, middleW, tick);
                    m_sprGonioVUMiddle->drawString("20", 0, (VU_DB_LOW == -20) ? above : below);
                }

                if (dB == -30)
                {
                    lineH(m_sprGonioVUMiddle, 0, y, middleW, tick);
                    m_sprGonioVUMiddle->drawString("30", 0, (VU_DB_LOW == -30) ? above : below);
                }

                if (dB == -40)
                {
                    lineH(m_sprGonioVUMiddle, 0, y, middleW, tick);
                    m_sprGonioVUMiddle->drawString("40", 0, (VU_DB_LOW == -40) ? above : below);
                }

                if (dB == -50)
                {
                    lineH(m_sprGonioVUMiddle, 0, y, middleW, tick);
                    m_sprGonioVUMiddle->drawString("50", 0, (VU_DB_LOW == -50) ? above : below);
                }

                lastTick = dB;
            }
        }

        pushToCanvas(m_sprGonioVUMiddle, middleX, cfg.posY);

        const uint16_t gap = farbe(Rolle::VisSegmentLuecke), barGround = farbe(Rolle::VisPegelGrund);
        for (uint8_t y = 0; y < cfg.size; ++y)
        {
            const uint16_t bgStripe = (y % 2 == 0) ? gap : barGround;
            m_sprGonioVULeft->drawFastHLine(0, y, barW, bgStripe);
            m_sprGonioVURight->drawFastHLine(0, y, barW, bgStripe);
        }

        pushToCanvas(m_sprGonioVULeft, leftX, cfg.posY);
        pushToCanvas(m_sprGonioVURight, rightX, cfg.posY);

        m_lastGonioVUUpdate = millis();
        m_gonioVUValL = m_gonioVUValR = 0.0f;
        m_gonioVUPeakL = m_gonioVUPeakR = 0.0f;
        m_gonioVUPeakTimeL = m_gonioVUPeakTimeR = 0;
        m_lastDrawnValL = m_lastDrawnValR = 0;
        m_lastDrawnPeakL = m_lastDrawnPeakR = 0;

        return;
    }

    uint32_t now = millis();
    float dt = (now - m_lastGonioVUUpdate) / 1000.0f;
    m_lastGonioVUUpdate = now;

    auto amplitudeToBarHeight = [&](float amplitude) -> float
    {
        float dB = amplitudeToDb(amplitude);
        return (dB - VU_DB_LOW) * cfg.size / (VU_DB_HIGH - VU_DB_LOW);
    };

    float targetValL = amplitudeToBarHeight(rmsLeft);
    float targetValR = amplitudeToBarHeight(rmsRight);

    auto stepToward = [&](float current, float target) -> float
    {
        // Die Geschwindigkeiten in Pixeln je Sekunde wachsen mit dem Geometriefaktor
        if (target > current)
        {
            current += gf(GONIO_VU_RISE_RATE) * dt;
            if (current > target)
                current = target;
        }
        else
        {
            current -= gf(GONIO_VU_FALL_RATE) * dt;
            if (current < target)
                current = target;
        }

        if (current < 0.0f)
            current = 0.0f;

        if (current > cfg.size)
            current = static_cast<float>(cfg.size);

        return current;
    };

    m_gonioVUValL = stepToward(m_gonioVUValL, targetValL);
    m_gonioVUValR = stepToward(m_gonioVUValR, targetValR);

    if (m_gonioVUValL > m_gonioVUPeakL)
    {
        m_gonioVUPeakL = m_gonioVUValL;
        m_gonioVUPeakTimeL = now;
    }
    else if (now - m_gonioVUPeakTimeL > GONIO_PEAK_HOLD_TIME)
    {
        m_gonioVUPeakL -= gf(GONIO_PEAK_FALL_RATE) * dt;

        if (m_gonioVUPeakL < m_gonioVUValL)
            m_gonioVUPeakL = m_gonioVUValL;

        if (m_gonioVUPeakL < 0.0f)
            m_gonioVUPeakL = 0.0f;
    }

    if (m_gonioVUValR > m_gonioVUPeakR)
    {
        m_gonioVUPeakR = m_gonioVUValR;
        m_gonioVUPeakTimeR = now;
    }
    else if (now - m_gonioVUPeakTimeR > GONIO_PEAK_HOLD_TIME)
    {
        m_gonioVUPeakR -= gf(GONIO_PEAK_FALL_RATE) * dt;

        if (m_gonioVUPeakR < m_gonioVUValR)
            m_gonioVUPeakR = m_gonioVUValR;

        if (m_gonioVUPeakR < 0.0f)
            m_gonioVUPeakR = 0.0f;
    }

    const uint8_t currentValL = static_cast<uint8_t>(m_gonioVUValL);
    const uint8_t currentValR = static_cast<uint8_t>(m_gonioVUValR);
    const uint8_t currentPeakL = static_cast<uint8_t>(m_gonioVUPeakL);
    const uint8_t currentPeakR = static_cast<uint8_t>(m_gonioVUPeakR);

    // Nur bei geänderten Pixelwerten neu zeichnen
    if (currentValL == m_lastDrawnValL &&
        currentValR == m_lastDrawnValR &&
        currentPeakL == m_lastDrawnPeakL &&
        currentPeakR == m_lastDrawnPeakR)
        return;

    m_lastDrawnValL = currentValL;
    m_lastDrawnValR = currentValR;
    m_lastDrawnPeakL = currentPeakL;
    m_lastDrawnPeakR = currentPeakR;

    auto dBtoY = [&](float dBthr) -> uint8_t
    {
        float y = (dBthr - VU_DB_LOW) * (float)(cfg.size - 1) / (float)(VU_DB_HIGH - VU_DB_LOW);

        if (y < 0.f)
            y = 0.f;

        if (y > (float)(cfg.size - 1))
            y = (float)(cfg.size - 1);

        return static_cast<uint8_t>(lroundf(y));
    };

    // Dieselben Schwellen wie beim breiten Balken der Digitalanzeige, denn beide zeigen denselben Effektivwert.
    // Bei -5 und 0 dB wäre der rote Bereich nur eine einzige Bildzeile hoch, weil die Skala bei Vollaussteuerung endet.
    const uint8_t yCutOrange = dBtoY(-12.0f); // ab hier Orange
    const uint8_t yCutRed = dBtoY(-3.0f);     // ab hier Rot

    uint16_t *bufferL = (uint16_t *)m_sprGonioVULeft->getBuffer();
    uint16_t *bufferR = (uint16_t *)m_sprGonioVURight->getBuffer();

    if (!bufferL || !bufferR)
        return;

    const uint16_t red = farbe(Rolle::VisPegelRot), orange = farbe(Rolle::VisPegelOrange), green = farbe(Rolle::VisPegelGruen);
    const uint16_t gap = farbe(Rolle::VisSegmentLuecke), barGround = farbe(Rolle::VisPegelGrund), peak = farbe(Rolle::VisSpitze);
    for (uint8_t y = 0; y < cfg.size; ++y)
    {
        // y zählt von unten nach oben, gezeichnet wird invertiert
        uint16_t segColor;
        if (m_gonioStyle == GonioStyle::RAINBOW)
            segColor = rainbowColors(VU_RAINBOW_GREEN - (y * VU_RAINBOW_GREEN / (cfg.size - 1)));
        else
            segColor = (y >= yCutRed) ? red : (y >= yCutOrange) ? orange
                                                                : green;

        const uint16_t bgStripe = (y % 2 == 0) ? gap : barGround;
        const bool isPeakL = (y >= currentPeakL && y < currentPeakL + gl(1));
        const bool isPeakR = (y >= currentPeakR && y < currentPeakR + gl(1));
        const int drawY = cfg.size - 1 - y;

        uint16_t colorL, colorR;

        if (y < currentValL)
            colorL = (y % 2 == 0) ? gap : segColor;
        else
            colorL = isPeakL ? peak : bgStripe;

        if (y < currentValR)
            colorR = (y % 2 == 0) ? gap : segColor;
        else
            colorR = isPeakR ? peak : bgStripe;

        colorL = bswap16(colorL);
        colorR = bswap16(colorR);

        // Die Balken sind im Original 8 Pixel breit, bei 1,5 zwölf
        uint16_t *lineL = &bufferL[drawY * barW];
        uint16_t *lineR = &bufferR[drawY * barW];

        for (int x = 0; x < barW; x++)
        {
            lineL[x] = colorL;
            lineR[x] = colorR;
        }
    }

    pushToCanvas(m_sprGonioVULeft, leftX, cfg.posY);
    pushToCanvas(m_sprGonioVURight, rightX, cfg.posY);
}

void Visualizer::drawGonioInfo(float gainFactor, float peakL, float peakR)
{
    if (!m_tft || !m_sprGonioInfo)
        return;

    // Der Guard signalisiert dem Writer, dass gerade gelesen wird
    ReaderGuard guard(m_readerActive);

    const uint8_t readIdx = m_writeBufferIdx.load(std::memory_order_acquire) ^ 1;
    const uint16_t sampleCount = m_sampleCount[readIdx];

    constexpr float MIN_SIGNAL_THRESH = 1e-4f; // Schwelle für "kein hörbares Signal"
    const uint32_t now = millis();
    bool needsPush = false; // Das Info-Sprite hat sich geändert und muss übertragen werden
    const bool hasSignal = m_playing &&
                           sampleCount >= 2 &&
                           (peakL > MIN_SIGNAL_THRESH || peakR > MIN_SIGNAL_THRESH);

    // Der Gain-Text wird nur formatiert und gezeichnet, wenn sich der sichtbare Wert ändert
    const bool showDecimal = !hasSignal || gainFactor < 10.0f;
    float displayGain = 1.0f;
    if (hasSignal)
        displayGain = showDecimal ? roundf(gainFactor * 10.0f) * 0.1f : roundf(gainFactor);

    // Das Vorzeichen trennt "x10.0" von "x10", ohne den Text vorab zu formatieren
    const float gainCacheKey = showDecimal ? displayGain : -displayGain;
    if (gainCacheKey != m_lastDrawnGain)
    {
        char gainStr[8];
        if (!hasSignal)
            stringutils::safeCopy(gainStr, sizeof(gainStr), "x1.0");
        else if (showDecimal)
            stringutils::safeFormat(gainStr, "x%.1f", displayGain);
        else
            stringutils::safeFormat(gainStr, "x%.0f", displayGain);

        const int16_t clipX = m_gainValueX - gl(5);
        const int16_t clipW = infoX(GONIO_INFO_W) - clipX - gl(5);
        const int16_t clipY = infoY(5), clipH = infoY(17) - infoY(5);
        m_sprGonioInfo->setClipRect(clipX, clipY, clipW, clipH);
        m_sprGonioInfo->fillRect(clipX, clipY, clipW, clipH, farbe(Rolle::GemFlaeche)); // überdeckt den alten Text
        m_sprGonioInfo->drawString(gainStr, m_gainValueX, clipY);
        m_sprGonioInfo->clearClipRect();

        m_lastDrawnGain = gainCacheKey;
        needsPush = true;
    }

    // Ohne Signal oder bei gestoppter Wiedergabe gehen alle Quadrate aus
    if (!hasSignal)
    {
        if (m_lastSquareStateL1 || m_lastSquareStateL2 ||
            m_lastSquareStateR1 || m_lastSquareStateR2)
        {
            m_lastSquareStateL1 = false;
            m_lastSquareStateL2 = false;
            m_lastSquareStateR1 = false;
            m_lastSquareStateR2 = false;

            m_sprGonioInfo->setClipRect(infoX(baseXL), infoY(baseY), infoX(baseXL + 162) - infoX(baseXL), infoY(baseY + SQH) - infoY(baseY)); // alle vier Quadrate
            for (int square = 0; square < 4; ++square)
            {
                int sx, sy, sw, sh;
                gonioSquareRect(square, sx, sy, sw, sh);
                drawGonioSquare(sx, sy, sw, sh, farbe(Rolle::VisPegelGrund));
            }
            m_sprGonioInfo->clearClipRect();
            needsPush = true;
        }

        if (needsPush)
            pushToCanvas(m_sprGonioInfo, gx(GONIO_INFO_X), gy(VIS_AREA_Y));

        return;
    }

    // Jede überschrittene Schwelle startet die Haltezeit ihres Quadrats neu
    if (peakL >= GONIO_INFO_THR1_LINEAR)
        m_infoPeakHoldL1 = now;
    if (peakL >= GONIO_INFO_THR2_LINEAR)
        m_infoPeakHoldL2 = now;
    if (peakR >= GONIO_INFO_THR1_LINEAR)
        m_infoPeakHoldR1 = now;
    if (peakR >= GONIO_INFO_THR2_LINEAR)
        m_infoPeakHoldR2 = now;

    auto isLit = [&](uint32_t tLast) -> bool
    {
        return (tLast != 0) && (now - tLast < GONIO_INFO_HOLD_MS);
    };

    bool currentStateL1 = isLit(m_infoPeakHoldL1);
    bool currentStateL2 = isLit(m_infoPeakHoldL2);
    bool currentStateR1 = isLit(m_infoPeakHoldR1);
    bool currentStateR2 = isLit(m_infoPeakHoldR2);

    // Ein Quadrat wird nur bei geändertem Zustand neu gezeichnet, Reihenfolge L1, L2, R1, R2
    const bool current[4] = {currentStateL1, currentStateL2, currentStateR1, currentStateR2};
    bool *last[4] = {&m_lastSquareStateL1, &m_lastSquareStateL2, &m_lastSquareStateR1, &m_lastSquareStateR2};
    const uint16_t threshold1 = farbe(Rolle::VisPegelOrange), threshold2 = farbe(Rolle::VisPegelRot), standby = farbe(Rolle::VisPegelGrund);
    const uint16_t onColor[4] = {threshold1, threshold2, threshold1, threshold2};
    for (int square = 0; square < 4; ++square)
    {
        if (current[square] == *last[square])
            continue;
        int sx, sy, sw, sh;
        gonioSquareRect(square, sx, sy, sw, sh);
        m_sprGonioInfo->setClipRect(sx, sy, sw, sh);
        drawGonioSquare(sx, sy, sw, sh, current[square] ? onColor[square] : standby);
        m_sprGonioInfo->clearClipRect();
        *last[square] = current[square];
        needsPush = true;
    }

    if (needsPush)
        pushToCanvas(m_sprGonioInfo, gx(GONIO_INFO_X), gy(VIS_AREA_Y));
}

void Visualizer::drawGonioSquare(int x, int y, int width, int height, uint16_t colorFill)
{
    if (!m_sprGonioInfo)
        return;

    m_sprGonioInfo->fillRect(x, y, width, height, colorFill);
    const uint16_t border = farbe(Rolle::GemRahmen);
    for (int o = 0; o < gl(1); ++o)
        m_sprGonioInfo->drawRect(x + o, y + o, width - 2 * o, height - 2 * o, border);
}

void Visualizer::setLanguage(LangIndex lang)
{
    if (m_language == lang)
        return;
    m_language = lang;
    if (m_tft)
        initVisMode(m_visMode);
}

void Visualizer::resetSignal()
{
    setPlaying(false);
    setAudioValues(0, 0, 0, 0);
    for (int i = 0; i < SAMPLE_BUFFER_COUNT; ++i)
    {
        m_sampleCount[i] = 0;
        m_sampleRate[i] = 0;
    }
    m_sampleSeq.fetch_add(1, std::memory_order_release);
    if (m_tft)
        initVisMode(m_visMode);
}

void Visualizer::setVuSettings(uint8_t ballistics, uint8_t sensitivity)
{
    if (ballistics >= static_cast<uint8_t>(VuBallistics::COUNT) || sensitivity > 4)
        return;
    m_vuBallistics = static_cast<VuBallistics>(ballistics);
    m_vuSensitivity = sensitivity;
}

void Visualizer::preferences(RadioPreferences &value) const
{
    value.mode = static_cast<uint8_t>(m_visMode);
    value.styles[0] = static_cast<uint8_t>(m_analogVUStyle);
    value.styles[1] = static_cast<uint8_t>(m_digitalVUStyle);
    value.styles[2] = static_cast<uint8_t>(m_spectrumStyle);
    value.styles[3] = static_cast<uint8_t>(m_waveformStyle);
    value.styles[4] = static_cast<uint8_t>(m_gonioStyle);
    value.styles[5] = static_cast<uint8_t>(m_centeredSpectrumStyle);
    value.styles[6] = static_cast<uint8_t>(m_waterfallStyle);
    value.styles[7] = static_cast<uint8_t>(m_auroraStyle);
}

void Visualizer::applyPreferences(const RadioPreferences &value)
{
    if (value.mode >= VIS_MODE_COUNT)
        return;
    for (auto style : value.styles)
        if (style > 1)
            return;
    m_analogVUStyle = static_cast<AnalogVUStyle>(value.styles[0]);
    m_digitalVUStyle = static_cast<DigitalVUStyle>(value.styles[1]);
    m_spectrumStyle = static_cast<SpectrumStyle>(value.styles[2]);
    m_waveformStyle = static_cast<WaveformStyle>(value.styles[3]);
    m_gonioStyle = static_cast<GonioStyle>(value.styles[4]);
    m_centeredSpectrumStyle = static_cast<SpectrumStyle>(value.styles[5]);
    m_waterfallStyle = static_cast<SpectrumStyle>(value.styles[6]);
    m_auroraStyle = static_cast<AuroraStyle>(value.styles[7]);
    m_visMode = static_cast<VisMode>(value.mode);
    if (m_tft)
        initVisMode(m_visMode);
}
