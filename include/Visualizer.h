#ifndef VISUALIZER_H
#define VISUALIZER_H

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>

// ESP32 FreeRTOS
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

#include "psram_unique_ptr.hpp"

// Projekt-Header
#include "Logger.h"
#include "Gfx.h"
#include "RadioPreferences.h"
#include "I18n.h"
#include "kiss_fftr.h"

template <typename T>
using PsramUniquePtr = std::unique_ptr<T[], PsramDeleter>;

/**
 * @brief Enumeration für die Visualisierungsmodi
 */
enum class VisMode
{
    VIS_ANALOG = 0,   ///< Analoge VU-Meter Anzeige
    VIS_DIGITAL = 1,  ///< Digitale VU-Meter Anzeige
    VIS_SPECTRUM = 2, ///< Spektrum-Analysator Anzeige
    VIS_WAVE = 3,     ///< Wellenform Anzeige
    VIS_GONIO = 4,    ///< Goniometer Anzeige
    VIS_SPECTRUM_CENTERED = 5, ///< Spektrum mit 36 aus der Mitte wachsenden Balken
    VIS_WATERFALL = 6, ///< Wasserfall-Spektrum mit zeitlichem Verlauf
    VIS_AURORA = 7 ///< Ruhige Nordlicht-Farbschleier
};

/// Anzahl der Visualisierungsmodi
static constexpr int VIS_MODE_COUNT = 8;

/**
 * @brief Enumeration für die Stile der digitalen VU-Anzeige
 */
enum class DigitalVUStyle
{
    CLASSIC, ///< Klassischer Stil
    RAINBOW  ///< Regenbogen-Stil
};

/**
 * @brief Enumeration für die Stile der Spektrum-Anzeige
 */
enum class SpectrumStyle
{
    CLASSIC, ///< Klassischer Stil
    RAINBOW  ///< Regenbogen-Stil
};

/**
 * @brief Farbwelten des Nordlichts
 */
enum class AuroraStyle : uint8_t
{
    COOL, ///< Petrol, Türkis und Violett
    WARM  ///< Bernstein, Kupfer und warmes Rot
};

/**
 * @brief Enumeration für die Wellenform Anzeige
 */
enum class WaveformStyle
{
    OVERLAY, ///< Überlagerter Stil
    STACKED  ///< Gestapelter Stil
};

/**
 * @brief Stile des Goniometer-VU-Balkens, die Goniometerfigur bleibt in beiden einfarbig
 */
enum class GonioStyle
{
    CLASSIC, ///< Klassischer Stil
    RAINBOW  ///< Regenbogen-Stil
};

/**
 * @brief Farbfassung des analogen VU-Meters
 *
 * Beide Fassungen zeigen dasselbe Blatt in derselben Geometrie. Nur die Farben unterscheiden sich, einschließlich des Zeigers. Umgeschaltet wird mit langem Druck auf die Anzeige, wie bei den anderen Visualisierungen auch.
 */
enum class AnalogVUStyle : uint8_t
{
    CLASSIC, ///< Blau, Weiß und Rot wie auf einem Studiogerät
    GREEN,   ///< Grünes Blatt mit bernsteinfarbenem Zeiger, wie ein hinterleuchtetes Instrument
    COUNT    ///< Anzahl der Fassungen
};

/**
 * @brief Trägheit des Zeigers im analogen VU-Meter
 *
 * Die drei Stufen verwenden im Feder-Dämpfer-Modell 0,15, 0,30 und 0,60 Sekunden als Zeitparameter. Derselbe Parameter gilt für Anstieg und Abfall. Die tatsächliche Bewegung wird zeitdiskret berechnet.
 */
enum class VuBallistics : uint8_t
{
    FAST,     ///< Zeitparameter 150 ms
    STANDARD, ///< Zeitparameter 300 ms
    SLOW,     ///< Zeitparameter 600 ms
    COUNT     ///< Anzahl der Einstellungen
};

/**
 * @brief Konfigurationsstruktur für das Goniometer
 *
 * Definiert Position und Größe des Goniometers für verschiedene Module. Das Goniometer ist immer quadratisch.
 */
struct GonioConfig
{
    int16_t posX;       ///< X-Position des Goniometers
    int16_t posY;       ///< Y-Position des Goniometers
    uint8_t size;       ///< Größe des Goniometers (immer quadratisch)
    uint8_t center;     ///< Mittelpunkt
    uint8_t scale;      ///< Skalierungsfaktor für Koordinatenberechnungen
    uint8_t corrBarY;   ///< Y-Position des Korrelationsbalkens (relativ zum Sprite)
    uint8_t corrBarW;   ///< Breite des Korrelationsbalkens
    uint8_t corrCenter; ///< Mittelpunkt des Korrelationsbalkens
    uint8_t corrRange;  ///< Halbe Breite des Korrelationsbereichs
};

/**
 * @brief Abbildung der Originalgeometrie (800x480) auf die Zeichenfläche einer Visualizer-Instanz
 *
 * Alle Instrumente rechnen weiter in Originalkoordinaten. Punkte werden um den Ursprung (341,354) des Originalbereichs mit scale gestreckt und auf origin verschoben. Rechtecke über ihre beiden Endpunkte, damit sich Rundungen nicht aufsummieren. scale 1 mit origin (341,354) ist die Identität. Neu gezeichnet wird in der Zielgröße, es wird kein Bild skaliert.
 */
struct VisualizerGeometry
{
    float scale = 1.0f; ///< 1,0 für die 800x480-Fläche, 1,5 für die native Tab5-Radioansicht
    int originX = 341;  ///< Lage des Originalpunkts x=341 auf der Zeichenfläche
    int originY = 354;  ///< Lage des Originalpunkts y=354 auf der Zeichenfläche
};

/**
 * @brief Struktur für Spektrumbänder
 */
struct SpectrumBand
{
    float smoothedMagnitude; ///< Geglättete Magnitude des Spektrumbands
    float peakPosFloat;      ///< Gleitkomma-Position des Peaks
    uint16_t currentPos;     ///< Aktuelle Position der Anzeige
    uint16_t peakPos;        ///< Position des Peak-Werts
    uint32_t peakTime;       ///< Zeitstempel des letzten Peaks
};

/**
 * @brief Struktur für Kanal-Daten (RMS/PPM)
 */
struct ChannelData
{
    float smoothedDB;    ///< Geglätteter dB-Wert
    uint16_t currentPos; ///< Aktuelle Position der Anzeige
    uint16_t peakPos;    ///< Position des Peak-Werts
    uint32_t peakTime;   ///< Zeitstempel des letzten Peaks
};

/**
 * @brief Klasse für Audio-Visualisierungen
 *
 * Bietet die acht Modi von VisMode für Audio-Signale: analoge und digitale VU-Meter, klassisches und mittiges Spektrum (FFT), Wellenform, Goniometer, Wasserfall und Nordlicht. Zeichnen, Touch und Zustandswechsel laufen serialisiert im Oberflächentask. Nur die Audiowerte sind per Spinlock gesichert.
 */
class Visualizer
{
public:
    /**
     * @brief Legt den Visualizer ohne Ressourcen an, init() bereitet ihn vor
     */
    Visualizer();

    /**
     * @brief Gibt alle Ressourcen frei
     */
    ~Visualizer();

    /**
     * @brief Räumt alle allokierten Ressourcen auf
     */
    void cleanup();

    /**
     * @brief Initialisiert den Visualizer
     * @param tft Vorbereiteter logischer Zeichenpuffer, geliehen und bis cleanup gültig
     * @param geometry Abbildung der Originalgeometrie. Unterstützt werden Faktor 1,0 und 1,5, für die Skalenblätter und Schriften vorliegen
     * @return true wenn erfolgreich initialisiert, false bei Fehler oder zu kleinem Zeichenpuffer
     */
    bool init(Gfx &tft, const VisualizerGeometry &geometry = VisualizerGeometry{});
    /** @brief Verwirft alte Signaldaten beim Quellenwechsel ohne Allokation. */
    void resetSignal();
    /** @brief Übernimmt die zentrale Sprache für native Instrumentenbeschriftungen. @param lang Sprache. */
    void setLanguage(LangIndex lang);
    /** @brief Übernimmt geprüfte VU-Zeigerträgheit und Referenzpegel. @param ballistics 0..2. @param sensitivity 0..4. */
    void setVuSettings(uint8_t ballistics, uint8_t sensitivity);

    /**
     * @brief Setzt die Audio-Werte für die Visualisierung
     * @param rmsLeft RMS-Wert für linken Kanal
     * @param rmsRight RMS-Wert für rechten Kanal
     * @param peakLeft Peak-Wert für linken Kanal
     * @param peakRight Peak-Wert für rechten Kanal
     */
    void setAudioValues(float rmsLeft, float rmsRight, float peakLeft, float peakRight);

    /**
     * @brief Kopiert die neuesten Samples in den Wechselpuffer, Aufrufe mit Zeichnen und Zustandswechseln im UI-Task serialisieren
     * @param samplesL Zeiger auf Sample-Daten für linken Kanal
     * @param samplesR Zeiger auf Sample-Daten für rechten Kanal
     * @param count Anzahl der Samples
     * @param sampleRate Abtastrate dieses Sample-Blocks in Hz, 0 bei unbekannter Rate oder Stille
     *
     * Kopiert höchstens MAX_SAMPLES pro Kanal ab dem Ende des übergebenen Blocks. Nullzeiger und count == 0 lassen die bisherigen Samples unverändert. Die Quellpuffer werden nicht behalten, die atomaren Index-/Lesemarkierungen ersetzen keine gegenseitige Verriegelung nebenläufiger Aufrufe.
     */
    void setSampleData(const int16_t *samplesL, const int16_t *samplesR, uint16_t count, uint32_t sampleRate);

    /** @brief Bricht den zugeteilten Kontakt ohne Wisch- oder Halteaktion ab. */
    void cancelTouch()
    {
        m_visTouchSeen = false;
        m_visTouchState = VisTouchState::IDLE;
    }

    /**
     * @brief Behandelt Touch-Eingaben im Visualisierungsbereich: Wischen wechselt den Modus, langer Druck den Stil
     * @param touchX X-Koordinate relativ zur linken oberen Ecke des Bereichs
     * @param touchY Y-Koordinate relativ zur linken oberen Ecke des Bereichs
     * @param touched true wenn Bildschirm berührt wird
     */
    void handleTouch(int touchX, int touchY, bool touched);

    /**
     * @brief Zeichnet die aktuelle Visualisierung in die geliehene Zeichenfläche, ohne init() geschieht nichts
     * @param init true bereitet den Modus neu vor, false zeichnet ein Update
     */
    void draw(bool init);

    /**
     * @brief Gibt den aktuellen Visualisierungs-Modus zurück
     * @return Aktueller VisMode
     */
    VisMode getMode() const
    {
        return m_visMode;
    }

    /**
     * @brief Setzt den Visualisierungs-Modus
     * @param mode Der zu setzende Modus
     */
    void setMode(VisMode mode);

    /**
     * @brief Gibt den aktuellen Spektrum-Style zurück
     * @return Aktueller SpectrumStyle
     */
    SpectrumStyle getSpectrumStyle() const
    {
        return m_spectrumStyle;
    }

    /**
     * @brief Gibt den aktuellen DigitalVU-Style zurück
     * @return Aktueller DigitalVUStyle
     */
    DigitalVUStyle getDigitalVUStyle() const
    {
        return m_digitalVUStyle;
    }

    /**
     * @brief Gibt den aktuellen Waveform-Style zurück
     * @return Aktueller WaveformStyle
     */
    WaveformStyle getWaveformStyle() const
    {
        return m_waveformStyle;
    }

    /**
     * @brief Gibt den aktuellen Goniometer-Style zurück
     * @return Aktueller GonioStyle
     */
    GonioStyle getGonioStyle() const
    {
        return m_gonioStyle;
    }

    /** @brief Setzt den Wiedergabestatus für alle Visualisierungen. @param playing true bei frischem PCM. */
    void setPlaying(bool playing)
    {
        m_playing = playing;
    }

    /** @brief Wechselt auf zentrale Anforderung den Modus. @param direction Vorwärts (1) oder rückwärts (-1). */
    void nextMode(int direction)
    {
        switchVisMode(direction);
    }

    /** @brief Wechselt auf zentrale Anforderung den Stil des aktiven Modus. */
    void nextStyle()
    {
        toggleVisSubMode();
    }

    /** @brief Bildet eine Original-x-Koordinate (800x480) auf die Zeichenfläche dieser Instanz ab. */
    int mapX(int x) const
    {
        return gx(x);
    }

    /** @brief Bildet eine Original-y-Koordinate (800x480) auf die Zeichenfläche dieser Instanz ab. */
    int mapY(int y) const
    {
        return gy(y);
    }

    /** @brief Meldet, ob seit dem letzten Aufruf etwas in die Zeichenfläche geschrieben wurde, und setzt die Marke zurück. Läuft im Oberflächentask. @return false, wenn VisualizerView den Zeilenvergleich auslassen darf. */
    bool takeCanvasWritten()
    {
        const bool written = m_canvasWritten;
        m_canvasWritten = false;
        return written;
    }

    /** @brief Kopiert den Modus und die acht gespeicherten Stilbits. */
    void preferences(RadioPreferences &value) const;
    /** @brief Übernimmt geprüften Modus und Stilbits ohne neue Puffer. */
    void applyPreferences(const RadioPreferences &value);

private:
    // Originalmaße der 800x480-Fläche, die Zeichenfläche einer Instanz folgt daraus über m_geometry.
    static constexpr GonioConfig CONFIG_RETRORADIO = {341, 354, 126, 63, 90, 116, 116, 63, 55};
    // Größter Hauptbereich aller Geometrien (nativ 1,5-fach), bestimmt die festen Puffer von Wasserfall und Nordlicht.
    static constexpr int MaxWidth = 668, MaxHeight = 107;
    LangIndex m_language = LangIndex::DE;

    // Grenzen für die Visualisierungen
    static constexpr float VU_DB_LOW = -60.0f; ///< Untere dB-Grenze der VU-Meter (Analog, Digital und Gonio)
    static constexpr float VU_DB_HIGH = 0.0f;  ///< Obere dB-Grenze der VU-Meter: Vollaussteuerung, denn höher geht ein Digitalsignal nicht
    static constexpr float VU_CALIBRATION_DB = 0.0f; ///< Aufschlag auf den gemessenen Pegel. null heißt, dass amplitudeToDb() echte dBFS liefert

    // Kennlinie des analogen VU-Meters, abgestimmt auf das Skalenblatt in images/retro_vu_meter.h
    static constexpr float VU_ZERO_SHARE = 0.71f;   ///< Anteil des Skalenweges, auf dem die Kennlinie 0 VU abbildet
    static constexpr float VU_TAN_MAX = 0.831169f;  ///< Steigung des äußersten Strahls des Skalenblatts
    static constexpr float VU_MAX_ANGLE = 39.7323f; ///< Halber Schwenk des Zeigers in Grad, folgt aus VU_TAN_MAX

    // Bereich für Visualisierungen in Originalkoordinaten
    static constexpr int VIS_AREA_X = 355; ///< X-Koordinate des Visualisierungsbereichs
    static constexpr int VIS_AREA_Y = 354; ///< Y-Koordinate des Visualisierungsbereichs
    static constexpr int VIS_AREA_W = 445; ///< Breite des Visualisierungsbereichs
    static constexpr int VIS_AREA_H = 71;  ///< Höhe des Visualisierungsbereichs

    VisualizerGeometry m_geometry; ///< Abbildung der Originalgeometrie auf diese Zeichenfläche
    int m_areaX = VIS_AREA_X, m_areaY = VIS_AREA_Y, m_areaW = VIS_AREA_W, m_areaH = VIS_AREA_H; ///< Hauptbereich auf der Zeichenfläche
    int m_labelH = 12; ///< Höhe der Frequenzzeile auf der Zeichenfläche
    GonioConfig m_gonio = CONFIG_RETRORADIO; ///< Goniometer auf der Zeichenfläche
    uint32_t m_waterfallFrameMs = 50; ///< Zeilentakt des Wasserfalls. Nativ kürzer, damit derselbe Zeitraum sichtbar bleibt
    const lgfx::IFont *m_smallFont = nullptr; ///< Kleine Beschriftung: DejaVu9, nativ DejaVu12

    /** @brief Bildet eine Original-x-Koordinate auf die Zeichenfläche ab. */
    int gx(int x) const
    {
        return m_geometry.originX + static_cast<int>(lroundf((x - 341) * m_geometry.scale));
    }

    /** @brief Bildet eine Original-y-Koordinate auf die Zeichenfläche ab. */
    int gy(int y) const
    {
        return m_geometry.originY + static_cast<int>(lroundf((y - 354) * m_geometry.scale));
    }

    /** @brief Rechnet eine Originallänge um, gerundet. Für Strichstärken, Schriftabstände und Innenmaße ohne eigene Lage. */
    int gl(int length) const
    {
        return static_cast<int>(lroundf(length * m_geometry.scale));
    }

    /** @brief Rechnet einen Originalwert in Pixeln ungerundet um, etwa Radien oder Geschwindigkeiten in Pixeln je Sekunde. */
    float gf(float value) const
    {
        return value * m_geometry.scale;
    }

    /** @brief true für die native 1,5-fache Geometrie. */
    bool native() const
    {
        return m_geometry.scale != 1.0f;
    }

    static constexpr int SPECTRUM_LABEL_H = 12; ///< Höhe der Frequenzzeile über Balken oder Wasserfall, Original
    static constexpr uint32_t WATERFALL_FRAME_MS = 50; ///< Eine Wasserfallzeile je 50 ms, Original
    static constexpr uint32_t BAR_FRAME_MS = 33; ///< Mindestabstand der Bilder von digitalen Balken, klassischem und mittigem Spektrum und Wellenform, höchstens rund 30 Bilder/s

    static constexpr int AURORA_LAYERS = 3; ///< Anzahl der überlagerten Nordlicht-Schleier
    static constexpr int AURORA_GLOW_STEPS = 1024; ///< Auflösung des weichen Leuchtprofils

    // FFT-Parameter
    static constexpr int CENTERED_BAR_GAP = 4; ///< Abstand zwischen den mittigen Spektrumbalken
    static constexpr int CENTERED_BAND_COUNT = 36; ///< Anzahl der mittig wachsenden Spektrumbalken
    static constexpr int FFT_N = 1024;       ///< FFT-Größe (Anzahl Samples)
    static constexpr int BIN_GAP = 2;        ///< Lücke zwischen den Balken des klassischen Spektrums, Original. Die Balkenbreite folgt aus der Verteilung über die ganze Fläche (Original 5 px, nativ 7 und 8 px)
    static constexpr int MAX_SAMPLES = 2048; ///< Maximale Anzahl von Audio-Samples

    lgfx::PointerWrapper m_fontData;
    lgfx::VLWfont m_font; ///< Einmal beim Start geladene Instrumentenbeschriftung.
    Gfx *m_tft = nullptr;       ///< Geliehener logischer Zeichenpuffer

    // Touch-Status für Visualisierungs-Bereich
    enum class VisTouchState
    {
        IDLE,           ///< Kein Touch aktiv
        TOUCH_STARTED,  ///< Touch begonnen, warte auf Geste
        LONG_PRESS_DONE ///< Langer Druck wurde ausgeführt
    };

    // Touch-Handling
    VisTouchState m_visTouchState = VisTouchState::IDLE; ///< Touch-Status
    bool m_visTouchSeen = false;                         ///< Berührungszustand des Vorframes, erkennt frische Berührungen
    int m_visTouchStartX = 0;                            ///< Start-X-Position der Visualisierungsbereich-Berührung
    int m_visTouchLastX = 0;                             ///< Letzte gültige X-Position (für Release-Erkennung)
    unsigned long m_visTouchStartTime = 0;               ///< Startzeit der Visualisierungsbereich-Berührung

    // Visualisierungs-Modi und Styles
    VisMode m_visMode = VisMode::VIS_ANALOG;                   ///< Aktueller Visualisierungsmodus
    SpectrumStyle m_spectrumStyle = SpectrumStyle::CLASSIC;    ///< Aktueller Style für die Spektrum-Anzeige
    SpectrumStyle m_centeredSpectrumStyle = SpectrumStyle::CLASSIC; ///< Unabhängige Farbwahl des mittigen Spektrums
    SpectrumStyle m_waterfallStyle = SpectrumStyle::RAINBOW; ///< Unabhängige Farbwahl des Wasserfalls
    AuroraStyle m_auroraStyle = AuroraStyle::COOL; ///< Gespeicherte Ziel-Farbwelt des Nordlichts
    DigitalVUStyle m_digitalVUStyle = DigitalVUStyle::CLASSIC; ///< Aktueller Style für die digitale VU-Anzeige
    WaveformStyle m_waveformStyle = WaveformStyle::OVERLAY;    ///< Aktueller Style für die Wellenform Anzeige
    GonioStyle m_gonioStyle = GonioStyle::CLASSIC;             ///< Stil des Goniometer-VU-Balkens, die Figur bleibt einfarbig
    bool m_playing = false; ///< Frisches PCM der aktiven Radio- oder SD-Quelle.
    bool m_canvasWritten = true; ///< Seit dem letzten takeCanvasWritten() in die Zeichenfläche geschrieben

    /** @brief Überträgt einen Modussprite an seine Stelle in der Zeichenfläche und merkt die Änderung für VisualizerView vor. */
    void pushToCanvas(Gfx_Sprite *sprite, int x, int y)
    {
        sprite->pushSprite(x, y);
        m_canvasWritten = true;
    }

    // Analoge VU-Meter
    bool m_needleInit = false;                                ///< Flag für Zeiger-Initialisierung
    float m_pastValL = -VU_MAX_ANGLE, m_pastValR = -VU_MAX_ANGLE;           ///< Aktueller Zeigerwinkel je Kanal
    float m_needleVelL = 0.0f, m_needleVelR = 0.0f;                         ///< Winkelgeschwindigkeit des Zeigers in Grad je Sekunde
    float m_lastRenderedL = -VU_MAX_ANGLE, m_lastRenderedR = -VU_MAX_ANGLE; ///< Zuletzt gerenderte Werte
    VuBallistics m_vuBallistics = VuBallistics::STANDARD;                   ///< Trägheit des Zeigers
    AnalogVUStyle m_analogVUStyle = AnalogVUStyle::CLASSIC;                 ///< Farbfassung des Skalenblatts
    uint8_t m_vuSensitivity = 2;                                            ///< Stufe der Empfindlichkeit, 0 bis 4
    unsigned long m_lastUpdate_analog = 0;                    ///< Zeitstempel des letzten analogen Updates

    // Digitale VU-Meter
    ChannelData m_rmsChannels[2] = {{VU_DB_LOW, 0, 0, 0}, {VU_DB_LOW, 0, 0, 0}}; ///< RMS-Kanaldaten
    ChannelData m_ppmChannels[2] = {{VU_DB_LOW, 0, 0, 0}, {VU_DB_LOW, 0, 0, 0}}; ///< PPM-Kanaldaten
    unsigned long m_lastUpdate_digital = 0;                              ///< Zeitstempel des letzten digitalen Updates

    // Sample-Daten in zwei Wechselpuffern, Nutzung im UI-Task serialisiert
    // Writer schreibt in Puffer [m_writeBufferIdx], dann atomarer Swap. Reader liest immer aus Puffer [m_writeBufferIdx ^ 1].
    static constexpr uint8_t SAMPLE_BUFFER_COUNT = 2; ///< Anzahl der Double-Buffer

    PsramUniquePtr<int16_t> m_samplesL[SAMPLE_BUFFER_COUNT]; ///< Double-Buffer für linke Samples [0] und [1]
    PsramUniquePtr<int16_t> m_samplesR[SAMPLE_BUFFER_COUNT]; ///< Double-Buffer für rechte Samples [0] und [1]
    uint16_t m_sampleCount[SAMPLE_BUFFER_COUNT] = {0, 0};    ///< Sample-Count pro Puffer (für jeden Puffer separat)
    uint32_t m_sampleRate[SAMPLE_BUFFER_COUNT] = {0, 0};    ///< Abtastrate in Hz, gemeinsam mit dem jeweiligen Sample-Puffer veröffentlicht

    std::atomic<uint8_t> m_writeBufferIdx{0};  ///< Aktueller Schreib-Puffer-Index (0 oder 1). Nach dem Schreiben wird geswappt.
    std::atomic<bool> m_readerActive{false};   ///< Lesemarkierung. setSampleData überspringt den Swap, wenn es die gesetzte Markierung sieht

    // Zählt veröffentlichte Samplepuffer. Das Goniometer zeichnet seine Figur nur bei einer neuen Marke oder einer erzwungenen Initialisierung.
    std::atomic<uint32_t> m_sampleSeq{0};      ///< Fortlaufende Marke der Abtastwertübergaben
    uint32_t m_gonioDrawnSeq = 0;              ///< Marke, die das Goniometer zuletzt gezeichnet hat
    bool m_gonioDrawnPlaying = false;          ///< Wiedergabezustand beim letzten Goniometerbild, ein Wechsel zeichnet auch ohne neue Marke neu

    // Spektrum-Analysator
    PsramUniquePtr<SpectrumBand> m_spectrum_bands;          ///< Array der Spektrum-Bänder
    kiss_fftr_cfg m_fft_cfg = nullptr;                      ///< FFT-Konfiguration
    std::array<kiss_fft_scalar, FFT_N> m_fft_input{};       ///< FFT-Eingangsdaten (statisch, kein Heap)
    std::array<kiss_fft_cpx, FFT_N / 2 + 1> m_fft_output{}; ///< FFT-Ausgangsdaten (statisch, kein Heap)
    std::array<float, FFT_N> m_fft_window_lut{};            ///< Look-Up-Table für Hann-Fenster
    std::array<int, 64> m_band_bin_indices_lut{};           ///< Look-Up-Table für Band-Bin-Indizes
    std::array<float, CENTERED_BAND_COUNT + 1> m_centeredBandEdges{}; ///< Bandgrenzen innerhalb der FFT-Bins für anteilige Leistungszuordnung
    bool m_fftWindowInitialized = false;                   ///< Hann-Fenster wurde berechnet
    uint32_t m_spectrumSampleRate = 0;                     ///< Abtastrate der aktuellen Bandzuordnung in Hz
    uint32_t m_lastSpectrumUpdate = 0;                      ///< Zeitstempel des letzten Spektrum-Updates
    std::array<float, 64> m_spectrumTargets{};              ///< Normierte Zielhöhen der zuletzt analysierten Frequenzbänder
    std::array<uint16_t, CENTERED_BAND_COUNT> m_centeredSpectrumColors{}; ///< Vorberechnete Regenbogenfarben
    uint32_t m_spectrumAnalyzedSeq = 0;                     ///< Zuletzt analysierte Sample-Marke
    bool m_spectrumHadSamples = false;                     ///< Die letzte Analyse enthielt freigegebene Audiodaten
    bool m_centeredSpectrumDirty = true;                   ///< Erzwingt das Neuzeichnen aller Balken beider Spektren nach Initialisierung, Raten- oder Stilwechsel

    /**
     * @brief Wiederverwendete Frequenzzuordnung, Farbskala und aktuelle Zeile des Wasserfalls
     */
    struct WaterfallData
    {
        std::array<float, MaxWidth + 1> binEdges; ///< Logarithmische Pixelgrenzen innerhalb der FFT-Bins
        std::array<uint16_t, 256> colors; ///< Farbskala in der Byte-Reihenfolge des Zeichenpuffers
        std::array<uint8_t, MaxWidth> targetLevels; ///< Pegelstufen des zuletzt ausgewerteten Spektrums
        std::array<uint8_t, MaxWidth> displayLevels; ///< Zeitlich geglättete Pegelstufen der neuesten Zeile
        uint32_t sampleRate; ///< Abtastrate der Pixelzuordnung
        uint16_t activeRows; ///< Verbleibende Zeilen bis zum vollständig leeren Verlauf
    };

    std::unique_ptr<WaterfallData, PsramDeleter> m_waterfall; ///< Beim ersten Aufruf angelegte Wasserfalldaten im PSRAM

    /**
     * @brief Vorberechnete Leuchtprofile und langsame Bewegungszustände des Nordlichts
     */
    struct AuroraData
    {
        std::array<uint16_t, AURORA_GLOW_STEPS + 1> glow; ///< Gaußförmiges Leuchtprofil mit 15 Nachkommabits
        std::array<uint16_t, MaxHeight> edgeFade; ///< Vertikaler Randverlauf von 0 bis 256
        float phase[AURORA_LAYERS][2]; ///< Unabhängige Phasen der beiden Wellen je Schleier
        float audioLevel; ///< Über mehrere Sekunden geglätteter Musikpegel von 0 bis 1
        float warmMix; ///< Überblendung von der kühlen zur warmen Farbwelt
        float fadeIn; ///< Sanftes Einblenden nach einem Moduswechsel
        uint32_t lastFrame; ///< Zeitstempel des zuletzt begonnenen Bildes
        float waveSin[AURORA_LAYERS][2]; ///< Sinuswerte an der nächsten zu berechnenden Spalte
        float waveCos[AURORA_LAYERS][2]; ///< Kosinuswerte an der nächsten zu berechnenden Spalte
        float stepSin[AURORA_LAYERS][2]; ///< Sinuswerte der räumlichen Phasenschritte
        float stepCos[AURORA_LAYERS][2]; ///< Kosinuswerte der räumlichen Phasenschritte
        float colors[AURORA_LAYERS][3]; ///< RGB-Koeffizienten des begonnenen Bildes
        int profileScale[AURORA_LAYERS]; ///< Skalierung der Leuchtprofile des begonnenen Bildes
        uint16_t nextColumn; ///< Nächste Spalte, m_areaW bedeutet ein vollständig berechnetes Bild
        bool rendering; ///< Ein begonnenes Bild wird berechnet oder wartet auf seine Ausgabe
    };

    std::unique_ptr<AuroraData, PsramDeleter> m_aurora; ///< Einmalig angelegte Nordlicht-Daten im PSRAM

    // Wellenform-Anzeige
    static constexpr uint32_t WAVE_COLUMN_MS = 12;            ///< Zeit je Spalte, Original. Ein Durchlauf über die Fläche dauert rund 5,3 s
    static constexpr uint32_t WAVE_MAX_SPAN_MS = 96;          ///< Höchstens so viel neue Spur nach einer Zeichenpause, Original 8 Spalten zu 12 ms
    static constexpr int WAVE_MAX_COLUMNS = 16;               ///< Obergrenze der Spalten je Bild, nativ sind es höchstens 12
    static constexpr int WAVE_CURSOR_GAP = 2;                 ///< Freie Spalten vor der Schreibposition, Original
    static constexpr uint8_t WAVE_PEAK_HISTORY_SIZE = 90;     ///< Anzahl der Pegelspitzen für die Auto-Verstärkung
    uint32_t m_waveColumnMs = WAVE_COLUMN_MS;                 ///< Spaltentakt. Nativ kürzer, damit ein Durchlauf gleich lang dauert
    uint32_t m_lastWaveUpdateMs = 0;                          ///< Zeitpunkt des letzten gezeichneten Blocks oder Leerbilds
    uint32_t m_waveDrawnSeq = 0;                              ///< Marke des zuletzt dargestellten Sampleblocks
    uint32_t m_waveColumnRestMs = 0;                          ///< Bereits verstrichener Bruchteil einer Spalte in Millisekunden
    int m_waveNextX = -1;                                     ///< Nächste Schreibspalte oder -1 für einen neuen Spuranfang
    bool m_waveHasSamples = false;                            ///< Die Zeitspur enthält Daten der laufenden Wiedergabe
    float m_waveAmp = 1.0f;                                   ///< Auto-Verstärkung für neu hinzukommende Ausschläge
    float m_wavePeakHistory[WAVE_PEAK_HISTORY_SIZE] = {};     ///< Pegelspitzen der zuletzt dargestellten Blöcke
    uint8_t m_wavePeakHistoryIdx = 0;                         ///< Schreibposition der nächsten Pegelspitze

    // Goniometer-Anzeige
    float m_gonioAmp = 1.0f;                                    ///< Auto-Gain Verstärkungsfaktor
    float m_gonioCorrPrev = 0.0f;                               ///< Vorheriger Korrelationswert (für Ballistik)
    static constexpr uint8_t GONIO_PEAK_HISTORY_SIZE = 90;      ///< Größe des Peak-History-Buffers (~3 Sekunden bei 30fps)
    float m_gonioPeakHistory[GONIO_PEAK_HISTORY_SIZE] = {0.0f}; ///< Ringpuffer für Peak-Werte
    uint8_t m_gonioPeakHistoryIdx = 0;                          ///< Aktueller Index im Ringpuffer
    float m_gonioSlidingMax = 0.0f;                             ///< Gleitendes Maximum über Peak-History
    uint32_t m_gonioLastPeakUpdate = 0;                         ///< Zeitstempel des letzten Peak-Updates
    uint32_t m_gonioLastDynamicsUpdate = 0;                     ///< Zeitstempel der letzten Dynamik-Aktualisierung

    // Goniometer VU-Meter (zeitbasierte Ballistik)
    static constexpr float GONIO_VU_FALL_RATE = 50.0f;    ///< VU-Abfallgeschwindigkeit in Pixel/Sekunde
    static constexpr float GONIO_VU_RISE_RATE = 2000.0f;  ///< VU-Anstiegsgeschwindigkeit in Pixel/Sekunde
    static constexpr uint32_t GONIO_PEAK_HOLD_TIME = 800; ///< Peak-Haltezeit in Millisekunden
    static constexpr float GONIO_PEAK_FALL_RATE = 80.0f;  ///< Peak-Abfallgeschwindigkeit in Pixel/Sekunde

    uint32_t m_lastGonioVUUpdate = 0; ///< Zeitstempel des letzten GonioVU-Updates
    float m_gonioVUValL = 0.0f;       ///< Aktueller VU-Wert links (float für smooth interpolation)
    float m_gonioVUValR = 0.0f;       ///< Aktueller VU-Wert rechts (float für smooth interpolation)
    float m_gonioVUPeakL = 0.0f;      ///< Aktueller Peak-Wert links
    float m_gonioVUPeakR = 0.0f;      ///< Aktueller Peak-Wert rechts
    uint32_t m_gonioVUPeakTimeL = 0;  ///< Zeitstempel des letzten Peak-Updates links
    uint32_t m_gonioVUPeakTimeR = 0;  ///< Zeitstempel des letzten Peak-Updates rechts
    uint8_t m_lastDrawnValL = 0;      ///< Zuletzt gezeichneter VU-Wert links (Dirty-Flag)
    uint8_t m_lastDrawnValR = 0;      ///< Zuletzt gezeichneter VU-Wert rechts (Dirty-Flag)
    uint8_t m_lastDrawnPeakL = 0;     ///< Zuletzt gezeichneter Peak-Wert links (Dirty-Flag)
    uint8_t m_lastDrawnPeakR = 0;     ///< Zuletzt gezeichneter Peak-Wert rechts (Dirty-Flag)

    // Sprites für die Visualisierungen
    Gfx_Sprite *m_sprVU_Analog_Needle = nullptr; ///< Zeichenpuffer der VU-Nadel, 3 Pixel breit (bei Faktor 1,5 vier)
    // Pro Instrument ein Sprite, pushSprite kopiert synchron in den logischen Gfx-Canvas. Den Displaytransfer übernimmt anschließend VisualizerView.
    Gfx_Sprite *m_sprVU_Analog_L = nullptr; ///< Sprite für das analoge VU links
    Gfx_Sprite *m_sprVU_Analog_R = nullptr; ///< Sprite für das analoge VU rechts
    Gfx_Sprite *m_sprVU_Digital_RMS_L = nullptr;          ///< Sprite für digitale RMS links
    Gfx_Sprite *m_sprVU_Digital_RMS_R = nullptr;          ///< Sprite für digitale RMS rechts
    Gfx_Sprite *m_sprVU_Digital_PPM_L = nullptr;          ///< Sprite für digitale PPM links
    Gfx_Sprite *m_sprVU_Digital_PPM_R = nullptr;          ///< Sprite für digitale PPM rechts
    Gfx_Sprite *m_sprVU_Spectrum = nullptr; ///< Sprite für die Spektrum-Anzeige
    Gfx_Sprite *m_sprAurora = nullptr; ///< Wiederverwendeter Zeichenpuffer für das Nordlicht
    Gfx_Sprite *m_sprVU_Wave = nullptr;     ///< Sprite für die Wellenform-Anzeige
    Gfx_Sprite *m_sprGonio = nullptr;       ///< Sprite für die Goniometer-Anzeige

    // Vorbereiteter Untergrund für Kreuz, Teilbalken und Beschriftungen. Vor jeder neuen Figur per memcpy in den Zeichenpuffer kopiert.
    Gfx_Sprite *m_sprGonioBg = nullptr;     ///< Fertiger Untergrund des Goniometers
    Gfx_Sprite *m_sprGonioVULeft = nullptr;               ///< Sprite für Goniometer VU links
    Gfx_Sprite *m_sprGonioVUMiddle = nullptr;             ///< Sprite für Goniometer VU Mitte
    Gfx_Sprite *m_sprGonioVURight = nullptr;              ///< Sprite für Goniometer VU rechts
    Gfx_Sprite *m_sprGonioInfo = nullptr;                 ///< Sprite für Goniometer Info-Anzeige

    // Lineare Spitzenwertschwellen des gemessenen Programmpegels für die zwei Lampen je Kanal, unabhängig von der Darstellungsverstärkung des Goniometers.
    static constexpr float GONIO_INFO_THR1_LINEAR = 0.70794578f; ///< Schwelle 1, entsprechend -3 dBFS
    static constexpr float GONIO_INFO_THR2_LINEAR = 0.94406088f; ///< Schwelle 2, entsprechend -0,5 dBFS

    // Layout
    // Originalmaße im Info-Sprite, drawGonioInfo bildet sie über die Geometrie ab.
    static constexpr int SQW = 30;    ///< Breite eines Peak-Quadrats in Pixeln
    static constexpr int SQH = 15;    ///< Höhe eines Peak-Quadrats in Pixeln
    static constexpr int GAP = 10;    ///< Abstand zwischen den Quadraten in Pixeln
    static constexpr int baseY = 50;  ///< Y-Startposition der Quadrate (relativ zum Sprite)
    static constexpr int baseXL = 11; ///< X-Startposition der linken Quadratgruppe (relativ zum Sprite)
    static constexpr int baseXR = 121; ///< X-Startposition der rechten Quadratgruppe (relativ zum Sprite)
    static constexpr int GONIO_INFO_X = 513, GONIO_INFO_W = 200; ///< Lage und Breite des Info-Sprites in Originalkoordinaten

    /** @brief Bildet eine waagerechte Originallage im Info-Sprite auf das Sprite ab. */
    int infoX(int x) const
    {
        return gx(GONIO_INFO_X + x) - gx(GONIO_INFO_X);
    }

    /** @brief Bildet eine senkrechte Originallage im Info-Sprite auf das Sprite ab. */
    int infoY(int y) const
    {
        return gy(VIS_AREA_Y + y) - gy(VIS_AREA_Y);
    }

    static constexpr uint32_t GONIO_INFO_HOLD_MS = 1200; ///< Haltezeit eines getriggerten Quadrats in Millisekunden

    // Trigger-Haltezeiten je Quadrat (L1/L2/R1/R2)
    uint32_t m_infoPeakHoldL1 = 0; ///< Zeitstempel des letzten L1-Triggers (millis)
    uint32_t m_infoPeakHoldL2 = 0; ///< Zeitstempel des letzten L2-Triggers (millis)
    uint32_t m_infoPeakHoldR1 = 0; ///< Zeitstempel des letzten R1-Triggers (millis)
    uint32_t m_infoPeakHoldR2 = 0; ///< Zeitstempel des letzten R2-Triggers (millis)

    // Goniometer Info Cache
    float m_lastDrawnGain = -999.0f; ///< Zuletzt gezeichneter Gain-Faktor

    // Cache für Quadrat-Zustände
    bool m_lastSquareStateL1 = false; ///< Letzter Zustand des L1-Quadrats
    bool m_lastSquareStateL2 = false; ///< Letzter Zustand des L2-Quadrats
    bool m_lastSquareStateR1 = false; ///< Letzter Zustand des R1-Quadrats
    bool m_lastSquareStateR2 = false; ///< Letzter Zustand des R2-Quadrats
    int16_t m_gainValueX = 120;       ///< X-Position des Gain-Werts im Info-Sprite, nach drawGonio()-Init dynamisch an Label-Breite angepasst

    // Audio-Werte
    float m_currentRMSLeft = 0.0f;                                  ///< Aktueller RMS-Wert links
    float m_currentRMSRight = 0.0f;                                 ///< Aktueller RMS-Wert rechts
    float m_currentPeakLeft = 0.0f;                                 ///< Aktueller Peak-Wert links
    float m_currentPeakRight = 0.0f;                                ///< Aktueller Peak-Wert rechts
    mutable portMUX_TYPE m_audioMux = portMUX_INITIALIZER_UNLOCKED; ///< Spinlock für die Audio-Werte, beide Kerne

    /**
     * @brief Wechselt den Visualisierungs-Modus
     * @param direction Richtung des Wechsels (1 = vorwärts, -1 = rückwärts)
     */
    void switchVisMode(int direction);

    /**
     * @brief Schaltet den Style der aktuellen Visualisierung um
     */
    void toggleVisSubMode();

    /**
     * @brief Initialisiert den gewählten Visualisierungs-Modus
     * @param mode Der zu initialisierende Visualisierungsmodus
     */
    void initVisMode(VisMode mode);

    /**
     * @brief Löscht den gesamten Visualisierungsbereich
     *
     * Löscht den größtmöglichen Bereich, den ein Visualisierungsmodul belegen kann. Dies umfasst:
     * - Den Hauptvisualisierungsbereich (VIS_AREA)
     * - Den Goniometer-Bereich inkl. VU-Meter
     *
     * Diese Methode wird beim Wechsel zwischen Visualisierungsmodi aufgerufen, um Reste des vorherigen Modus zu entfernen.
     */
    void clearVisualizationArea();

    /**
     * @brief Zeichnet die Visualisierung basierend auf aktuellem Modus
     * @param init true für Initialisierung, false für Update
     * @param rmsLeft RMS-Wert für linken Kanal
     * @param rmsRight RMS-Wert für rechten Kanal
     * @param peakLeft Peak-Wert für linken Kanal
     * @param peakRight Peak-Wert für rechten Kanal
     */
    void drawVis(bool init, float rmsLeft, float rmsRight, float peakLeft, float peakRight);

    /**
     * @brief Gibt alle vorbereiteten Instrumentensprites frei
     */
    void cleanupCurrentVisSprites();

    /**
     * @brief Rechnet einen linearen Amplitudenwert in einen kalibrierten VU-Pegel um
     * @param amplitude Linearer Pegel, 1,0 entspricht Vollausschlag (0 dBFS)
     * @return dB-Wert, begrenzt auf [VU_DB_LOW, VU_DB_HIGH]
     *
     * Zentrale Umrechnung für Analog-VU, Digital-VU und Gonio-VU. Begrenzt das Ergebnis auf den sichtbaren VU-Bereich. Bei Stille (amplitude nahe 0) wird VU_DB_LOW geliefert.
     */
    float amplitudeToDb(float amplitude) const;

    /**
     * @brief Liefert die eingestellte Trägheit des analogen VU-Zeigers
     * @return Aktuelle VuBallistics
     */
    VuBallistics getVuBallistics() const
    {
        return m_vuBallistics;
    }

    /**
     * @brief Rechnet einen VU-Pegel in den Zeigerwinkel des analogen Instruments um
     * @param db Pegel in dB, 0 dB entspricht 0 VU
     * @return Winkel in Grad, 0 zeigt senkrecht nach oben
     *
     * Ein VU-Instrument schlägt proportional zur Spannung aus, nicht zum Pegel in Dezibel. Der Weg von links ist deshalb VU_ZERO_SHARE mal zehn hoch dB durch zwanzig. Daraus folgt der Winkel über den Arkustangens. Der berechnete Skalenanteil wird auf 0..1 begrenzt.
     *
     * Welcher Pegel 0 VU entspricht, hängt von der eingestellten Empfindlichkeit ab. Die Empfindlichkeitsstufe verschiebt den Bezug zwischen gemessenem Programmpegel und Skalenbeschriftung.
     */
    float dbToNeedleAngle(float db) const;

    /**
     * @brief Liefert den eingestellten Bezugspegel in dBFS
     * @return Pegel, bei dem der Zeiger 0 VU erreicht
     *
     * Denselben Wert benutzt die Spektrumanzeige, damit ihr Fenster zum selben Programm passt.
     */
    float vuAlignmentDb() const;

    /**
     * @brief Zeichnet das analoge VU-Meter
     * @param init true für Initialisierung, false für Update
     * @param rmsLeft RMS-Wert für linken Kanal
     * @param rmsRight RMS-Wert für rechten Kanal
     */
    void drawAnalogVU(bool init, float rmsLeft, float rmsRight);
    /** @brief Liefert das vorbereitete VU-Skalenblatt, 218x70 oder nativ 327x105 Pixel. */
    const uint16_t *analogVUPlate() const;

    /**
     * @brief Zeichnet das digitale VU-Meter
     * @param init true für Initialisierung, false für Update
     * @param rmsLeft RMS-Wert für linken Kanal
     * @param rmsRight RMS-Wert für rechten Kanal
     * @param peakLeft Peak-Wert für linken Kanal
     * @param peakRight Peak-Wert für rechten Kanal
     */
    void drawDigitalVU(bool init, float rmsLeft, float rmsRight, float peakLeft, float peakRight);

    /**
     * @brief Zeichnet je nach Modus das klassische Spektrum, mittige Balken oder den Wasserfall
     * @param init true für Initialisierung, false für Update
     */
    void drawSpectrum(bool init);

    /**
     * @brief Beschriftet die Frequenzachse des aktiven Spektrums
     * @param cutoffFreqs Bandgrenzen in Hz, mit bandCount + 1 Einträgen
     * @param bandCount Anzahl der Bänder
     */
    void drawSpectrumLabels(const float *cutoffFreqs, int bandCount);

    /**
     * @brief Aktualisiert den Wasserfall im gemeinsamen Spektrum-Zeichenpuffer
     * @param init true zum Zurücksetzen des Verlaufs und Vorbereiten der Farbskala
     * @param analyze true wenn neue FFT-Werte oder ein geänderter Wiedergabezustand vorliegen
     * @param hasSamples true wenn die FFT gültige Audiodaten enthält
     * @param elapsed Vergangene Zeit seit der letzten Wasserfallzeile in Millisekunden
     */
    void drawWaterfall(bool init, bool analyze, bool hasSamples, uint32_t elapsed);

    /**
     * @brief Zeichnet langsam bewegte Nordlicht-Schleier mit sanfter Audioreaktion
     * @param init true zum Vorbereiten des Zeichenpuffers und Neustarten der Einblendung
     * @param rmsLeft Linearer Effektivwert des linken Audiokanals
     * @param rmsRight Linearer Effektivwert des rechten Audiokanals
     */
    void drawAurora(bool init, float rmsLeft, float rmsRight);

    /**
     * @brief Löscht die Zeitspur und zeichnet die Mittellinien des aktuellen Kanalstils
     */
    void resetWaveTrace();

    /**
     * @brief Ergänzt die Zeitspur von links nach rechts um die positiven und negativen Ausschläge des neuesten Sampleblocks
     * @param init true für Initialisierung, false für Update
     */
    void drawWave(bool init);

    /**
     * @brief Zeichnet die Goniometer Anzeige
     * @param init true für Initialisierung, false für Update
     * @param peakLeft Peak-Wert für linken Kanal (0..1)
     * @param peakRight Peak-Wert für rechten Kanal (0..1)
     */
    void drawGonio(bool init, float peakLeft, float peakRight);

    /**
     * @brief Zeichnet die Goniometer VU-Anzeige
     * @param init true für Initialisierung, false für Update
     * @param rmsLeft RMS-Wert für linken Kanal (0..1)
     * @param rmsRight RMS-Wert für rechten Kanal (0..1)
     */
    void drawGonioVU(bool init, float rmsLeft, float rmsRight);

    /**
     * @brief Zeichnet die Goniometer Info-Anzeige
     *  - vier Peak-Quadrate (L: [Thr1][Thr2], R: [Thr1][Thr2]) mit Hold
     *  - aktuellen Auto-Gain-Faktor
     *
     * @param gainFactor  aktueller Auto-Gain-Faktor
     * @param peakL       Peak linear (0..1) links
     * @param peakR       Peak linear (0..1) rechts
     */
    void drawGonioInfo(float gainFactor, float peakL, float peakR);

    /**
     * @brief Zeichnet ein Quadrat für die Goniometer Info-Anzeige
     * @param x X-Position des Quadrats
     * @param y Y-Position des Quadrats
     * @param width Breite des Quadrats
     * @param height Höhe des Quadrats
     * @param colorFill Füllfarbe des Quadrats
     */
    void drawGonioSquare(int x, int y, int width, int height, uint16_t colorFill);

    /**
     * @brief Liefert das Rechteck eines Peak-Quadrats im Info-Sprite
     * @param index 0 und 1 links (Schwelle 1 und 2), 2 und 3 rechts
     * @param x, y, w, h Rechteck auf dem Info-Sprite
     */
    void gonioSquareRect(int index, int &x, int &y, int &w, int &h) const;

    /** @brief Waagerechte Linie in Originalstärke 1 px, nativ gl(1) Zeilen stark. Bei Faktor 1 unverändert drawFastHLine. */
    void lineH(Gfx_Sprite *spr, int x, int y, int w, uint16_t color) const;
    /** @brief Senkrechte Linie in Originalstärke 1 px, nativ gl(1) Spalten stark. Bei Faktor 1 unverändert drawFastVLine. */
    void lineV(Gfx_Sprite *spr, int x, int y, int h, uint16_t color) const;

    /**
     * @brief Zeichnet die statischen Elemente des Goniometers
     * @param spr Zeiger auf das Sprite, in das gezeichnet werden soll
     * @param cfg Konfiguration des Goniometers (Position, Größe, etc.)
     *
     * Zeichnet alle statischen Elemente des Goniometers wie Kreuz, Balken, Beschriftungen und Korrelationsanzeige. Wird beim Vorbereiten des Hintergrundpuffers aufgerufen. Updates kopieren diesen Puffer.
     */
    void drawGonioStaticElements(Gfx_Sprite *spr, const GonioConfig &cfg);
};

#endif // VISUALIZER_H
