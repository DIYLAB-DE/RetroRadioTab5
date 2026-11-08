#include "VisualizerView.h"
#include "Thema.h"

bool VisualizerView::begin(lgfx::LGFX_Device &display)
{
    if (m_ready)
        return m_display == &display;
    m_display = &display;
    // 1,5-fache Geometrie. Die Zeichenfläche deckt genau den Bereich ab
    VisualizerGeometry radio;
    radio.scale = 1.5f;
    radio.originX = 0;
    radio.originY = 0;
    if (!m_radio.canvas.begin(RadioWidth, RadioHeight) || !m_radio.visualizer.init(m_radio.canvas, radio))
    {
        m_radio.visualizer.cleanup();
        m_radio.canvas.release();
        m_display = nullptr;
        m_active = nullptr;
        return false;
    }
    m_active = &m_radio;
    m_ready = true;
    m_force = true;
    return true;
}

bool VisualizerView::setBounds(int x, int y, int width, int height)
{
    Bank *bank = (width == RadioWidth && height == RadioHeight) ? &m_radio : nullptr;
    if (!m_ready || !bank || x < 0 || y < 0 || x > 1280 - width || y > 720 - height)
        return false;
    if (bank != m_active || x != m_x || y != m_y)
    {
        m_active = bank;
        m_active->visualizer.resetSignal();
        m_x = x;
        m_y = y;
        m_tailWasGonio = false;
        invalidate();
    }
    return true;
}

void VisualizerView::invalidate()
{
    m_force = true;
}

void VisualizerView::update(uint32_t now, AudioManager &audio, bool silent)
{
    if (!m_ready || !m_active)
        return;
    auto &visualizer = m_active->visualizer;
    const auto status = audio.getStatus();
    const bool playing = !silent && status.state == AudioManager::State::Streaming && status.pcmAgeMs < 1000;
    visualizer.setPlaying(playing);
    if (audio.isAudioDataReady())
    {
        if (playing)
        {
            visualizer.setAudioValues(audio.getRMSLeft(), audio.getRMSRight(), audio.getPeakLeft(), audio.getPeakRight());
            visualizer.setSampleData(audio.getSampleBufferLeft(), audio.getSampleBufferRight(), audio.getValidSamplesCount(), audio.getSampleRate());
        }
        audio.consumeAudioData();
    }
    visualizer.draw(false);
    // Ohne Schreibzugriff seit dem letzten Durchlauf entfällt der Zeilenvergleich, der für den ganzen Streifen rund 5,5 ms kostet.
    // Der Zeilenvergleich sendet nur geänderte Zeilen. Geht eine Übertragung unterwegs verloren, bliebe der alte Inhalt stehen,
    // bis sich die Zeile wieder ändert, am Fuß des Spektrums kann das bei gleichmäßiger Musik lange dauern. Deshalb gehen die
    // eigenen Flächen zusätzlich im Abstand RefreshMs vollständig ans Display, die Uhr rechts neben dem Goniometer bleibt unberührt.
    const bool refresh = static_cast<int32_t>(now - m_lastRefresh) >= static_cast<int32_t>(RefreshMs);
    if (refresh)
        m_lastRefresh = now;
    const bool written = visualizer.takeCanvasWritten() || m_force || refresh;
    const bool full = m_force || refresh;
    // Teilflächen in Originalkoordinaten (800x480), über die Geometrie der Instanz auf ihre Zeichenfläche abgebildet:
    // oberer Streifen 341..800 x 354..425. Beim Goniometer nur bis 713, dazu der Fuß 341..505 x 425..480. Der Fuß endet mit dem
    // Goniometer-VU-Balken bei 505, die Tasten darunter beginnen in der Tab5-Geometrie bereits bei 847 und damit vor mapX(513) = 849.
    // Die Zwischenzeile 341..800 x 425..435 bis zur Tastenreihe zeichnet im Betrieb niemand, sie wird nur bei der Auffrischung gesendet.
    auto &canvas = m_active->canvas;
    const bool gonio = mode() == VisMode::VIS_GONIO;
    const int left = visualizer.mapX(341), top = visualizer.mapY(354), stripBottom = visualizer.mapY(425), bottom = visualizer.mapY(480);
    const int gonioRight = visualizer.mapX(713), right = visualizer.mapX(800), tailRight = visualizer.mapX(505), gapBottom = visualizer.mapY(435);
    if (written)
        canvas.presentRegion(*m_display, left, top, (gonio ? gonioRight : right) - left, stripBottom - top, m_x, m_y, full);
    // Eine fehlerhafte Übertragung, die in der Zwischenzeile landet, bliebe sonst bis zum nächsten Ansichtswechsel stehen. Die
    // Zeichenfläche ist dort Grund, im Goniometermodus links der obere Teil des Fußes. Die Tastenreihe ab 435 bleibt unberührt.
    if (refresh)
        canvas.presentRegion(*m_display, left, stripBottom, right - left, gapBottom - stripBottom, m_x, m_y + stripBottom - top, true);
    if (gonio && m_force)
        canvas.presentRegion(*m_display, gonioRight, top, right - gonioRight, stripBottom - top, m_x + gonioRight - left, m_y, true);
    if (gonio && written)
        canvas.presentRegion(*m_display, left, stripBottom, tailRight - left, bottom - stripBottom, m_x, m_y + stripBottom - top, full);
    if (m_tailWasGonio && !gonio)
    {
        canvas.fillRect(left, stripBottom, tailRight - left, bottom - stripBottom, farbe(Rolle::GemGrund));
        canvas.presentRegion(*m_display, left, stripBottom, tailRight - left, bottom - stripBottom, m_x, m_y + stripBottom - top, true);
    }
    m_tailWasGonio = gonio;
    m_force = false;
}

void VisualizerView::nextMode(int direction)
{
    if (!m_ready)
        return;
    const int next = (static_cast<int>(mode()) + direction % VIS_MODE_COUNT + VIS_MODE_COUNT) % VIS_MODE_COUNT;
    setMode(static_cast<VisMode>(next));
}

void VisualizerView::setMode(VisMode mode)
{
    if (!m_ready || static_cast<unsigned>(mode) >= VIS_MODE_COUNT)
        return;
    m_radio.visualizer.setMode(mode);
    invalidate();
}

void VisualizerView::nextStyle()
{
    if (!m_ready)
        return;
    m_radio.visualizer.nextStyle();
    invalidate();
}

void VisualizerView::setLanguage(LangIndex lang)
{
    m_radio.visualizer.setLanguage(lang);
    invalidate();
}

void VisualizerView::resetSignal()
{
    if (!m_ready)
        return;
    m_radio.visualizer.resetSignal();
    invalidate();
}

void VisualizerView::setVuSettings(uint8_t ballistics, uint8_t sensitivity)
{
    m_radio.visualizer.setVuSettings(ballistics, sensitivity);
    invalidate();
}

void VisualizerView::handleTouch(int x, int y, bool touched)
{
    if (!m_ready || !m_active)
        return;
    RadioPreferences before, after;
    m_active->visualizer.preferences(before);
    m_active->visualizer.handleTouch(x, y, touched);
    m_active->visualizer.preferences(after);
    if (before.mode != after.mode || memcmp(before.styles, after.styles, sizeof(before.styles)))
        invalidate();
}
