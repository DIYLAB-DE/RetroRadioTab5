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

#ifndef UI_BUTTON_H
#define UI_BUTTON_H

#include <stdint.h>

#include "UiCanvas.h"

/**
 * @brief Schaltfläche mit Text, optionalem Symbol und externem Canvas-Ziel
 */
class UiButton
{
public:
    /**
     * @brief Position des Icons innerhalb des Buttons
     */
    enum class IconPosition
    {
        Left,   ///< Icon links vom Text
        Center, ///< Icon zentriert (ohne Text)
        Right   ///< Icon rechts vom Text
    };

    /**
     * @brief Initialisiert den UiButton mit allen Parametern
     *
     * @param gfx Vorbereitete Zeichenfläche, bleibt Eigentum des Aufrufers und muss während der Verwendung gültig bleiben
     * @param x X-Position des UiButton-Zentrums
     * @param y Y-Position des UiButton-Zentrums
     * @param w Breite des Buttons
     * @param h Höhe des Buttons
     * @param r Radius für abgerundete Ecken
     * @param outline Farbe des Rahmens
     * @param fill Füllfarbe des Buttons
     * @param textcolor Textfarbe
     * @param label Beschriftung, die in den internen 31-Byte-Puffer kopiert und gegebenenfalls gekürzt wird
     * @param icon Geliehene RGB565-Pixel, müssen bis zum Austausch oder Ende der Verwendung gültig bleiben
     * @param textsize_x Horizontale Textgröße
     * @param textsize_y Vertikale Textgröße (0 = wie textsize_x)
     */
    template <typename T>
    void initButton(UiCanvas *gfx, int16_t x, int16_t y, uint16_t w, uint16_t h,
                    uint16_t r, const T &outline, const T &fill, const T &textcolor,
                    const char *label, const uint16_t *icon,
                    float textsize_x = 1.0f, float textsize_y = 0.0f)
    {
        m_outlinecolor = outline;
        m_fillcolor = fill;
        m_textcolor = textcolor;
        _init_button(gfx, x, y, w, h, r, label, icon, textsize_x, textsize_y);
    }

    /**
     * @brief Setzt den UiButton-Text
     * @param label Neuer Text für den UiButton
     */
    void setLabelText(const char *label);

    /**
     * @brief Gibt den aktuellen UiButton-Text zurück
     * @return Pointer auf den Text
     */
    char *getLabelText();

    /**
     * @brief Setzt das Icon für den UiButton
     * @param icon Geliehene RGB565-Pixel, müssen bis zum Austausch oder Ende der Verwendung gültig bleiben
     */
    void setIcon(const uint16_t *icon);

    /**
     * @brief Setzt die Position des Icons
     * @param pos Icon-Position (Left, Center, Right)
     */
    void setIconPosition(IconPosition pos);

    /**
     * @brief Setzt die Größe des Icons
     * @param width Icon-Breite in Pixeln
     * @param height Icon-Höhe in Pixeln
     */
    void setIconSize(int16_t width, int16_t height);

    /**
     * @brief Setzt die Schriftart (Legacy-Format)
     * @param font Pointer auf Font-Daten (uint8_t Array)
     */
    void setFont(const uint8_t *font);

    /**
     * @brief Setzt eine geliehene Schrift im lgfx::GFXfont-Format
     * @param font Schriftobjekt, das während der Verwendung gültig bleiben muss
     */
    void setFont(const lgfx::v1::GFXfont *font);

    /**
     * @brief Aktiviert einen Zahlenkreis anstelle des Icons
     * @param number Anzuzeigende Zahl (0-99)
     */
    void enableNumberCircle(uint8_t number);

    /**
     * @brief Deaktiviert den Zahlenkreis
     */
    void disableNumberCircle();

    /**
     * @brief Setzt die Text-Position relativ zum UiButton-Zentrum
     * @param x_delta X-Verschiebung vom Zentrum
     * @param y_delta Y-Verschiebung vom Zentrum
     * @param datum Text-Ausrichtung (Standard: middle_center)
     */
    void setLabelDatum(int16_t x_delta, int16_t y_delta, uint8_t datum = MC_DATUM);

    /**
     * @brief Setzt einen zusätzlichen X/Y-Offset für das Label.
     * @param x_offset Horizontaler Offset in Pixeln.
     * @param y_offset Vertikaler Offset in Pixeln.
     */
    void setLabelOffset(int16_t x_offset, int16_t y_offset);

    /**
     * @brief Setzt die Rahmenfarbe
     * @param clr Neue Rahmenfarbe
     */
    template <typename T>
    void setOutlineColor(const T &clr)
    {
        m_outlinecolor = clr;
    }

    /**
     * @brief Setzt die Füllfarbe
     * @param clr Neue Füllfarbe
     */
    template <typename T>
    void setFillColor(const T &clr)
    {
        m_fillcolor = clr;
    }

    /**
     * @brief Setzt die Textfarbe
     * @param clr Neue Textfarbe
     */
    template <typename T>
    void setTextColor(const T &clr)
    {
        m_textcolor = clr;
    }

    /**
     * @brief Lenkt Zeichenoperationen in eine andere Zeichenfläche um, der Aufrufer übernimmt den Displaytransfer
     * @param target Sprite-Zeichenziel, nullptr wirkt wie clearDrawTarget()
     * @param spriteOriginX X-Position, an der das Sprite später auf dem Display gepusht wird
     * @param spriteOriginY Y-Position, an der das Sprite später auf dem Display gepusht wird
     *
     * Der Button zeichnet nur in den geliehenen Canvas-Puffer. Beide Zeichenflächen müssen während der Verwendung gültig bleiben, die Klasse reserviert und überträgt keinen Displaypuffer.
     *
     * contains verwendet weiterhin die Koordinaten aus initButton, isPressed liefert ausschließlich den zuletzt über press gesetzten Zustand.
     *
     * Die Origin-Koordinaten geben an, wo das Sprite später auf dem Display landet. Der UiButton rechnet intern um: Sprite-Koordinate = Display-Koordinate - Origin.
     *
     * Beispiel: UiButton bei Display-Center (630, 230), Größe 260x56, Sprite 260x56 mit Push-Ziel (500, 202): setDrawTarget(sprite, 500, 202). Der UiButton zeichnet dann intern bei Sprite-Center (130, 28).
     *
     * Bei einem Sprite in Displaygröße oder einem UiButton an der Sprite-Position (0, 0) können beide Ursprungsparameter entfallen.
     *
     * Auf beiden Zielen zeichnet der Button Kreise, Ellipsen und Instrumente mit den geglätteten Zeichnern von UiCanvas.
     */
    void setDrawTarget(UiCanvas *target, int16_t spriteOriginX = 0, int16_t spriteOriginY = 0)
    {
        m_drawTarget = target;
        m_targetOriginX = spriteOriginX;
        m_targetOriginY = spriteOriginY;
    }

    /**
     * @brief Schaltet das alternative Zeichenziel ab, folgende Zeichenoperationen gehen wieder auf die in initButton übergebene Zeichenfläche.
     */
    void clearDrawTarget()
    {
        m_drawTarget = nullptr;
        m_targetOriginX = 0;
        m_targetOriginY = 0;
    }

    /**
     * @brief Zeichnet den UiButton
     * @param inverted True für invertierten Zustand (gedrückt)
     * @param long_name Alternativer Text (nullptr = normaler Text)
     */
    void drawButton(bool inverted = false, const char *long_name = nullptr);

    /**
     * @brief Prüft ob Koordinaten innerhalb des Buttons liegen
     * @param x X-Koordinate
     * @param y Y-Koordinate
     * @return True wenn Punkt im UiButton liegt
     */
    bool contains(int16_t x, int16_t y) const
    {
        return abs(x - m_x) <= (m_w / 2) && abs(y - m_y) <= (m_h / 2);
    }

    /**
     * @brief Setzt den Gedrückt-Status
     * @param p True = gedrückt, False = nicht gedrückt
     */
    void press(bool p)
    {
        m_last_press = m_pressed;
        m_pressed = p;
    }

    /**
     * @brief Gibt zurück ob UiButton gedrückt ist
     * @return True wenn gedrückt
     */
    bool isPressed(void) const
    {
        return m_pressed;
    }

    /**
     * @brief Gibt zurück ob UiButton gerade gedrückt wurde
     * @return True bei Zustandswechsel zu gedrückt
     */
    bool justPressed(void) const
    {
        return (m_pressed && !m_last_press);
    }

    /**
     * @brief Gibt zurück ob UiButton gerade losgelassen wurde
     * @return True bei Zustandswechsel zu nicht gedrückt
     */
    bool justReleased(void) const
    {
        return (!m_pressed && m_last_press);
    }

    /**
     * @brief Callback-Funktion für benutzerdefiniertes Zeichnen
     */
    typedef void (*drawCb)(UiCanvas *m_tft, int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, bool invert, const char *long_name);

    /**
     * @brief Setzt eine benutzerdefinierte Zeichenfunktion
     * @param cb Callback-Funktion
     */
    void setDrawCb(drawCb cb)
    {
        m_drawCb = cb;
    };

    /**
     * @brief Setzt die Rahmenbreiten, Standard 1 und 2 Pixel
     * @param normal Rahmenbreite im Ruhezustand in Pixeln, mindestens 1
     * @param pressed Rahmenbreite bei gedrückter Taste in Pixeln, mindestens 1
     */
    void setBorderWidths(uint8_t normal, uint8_t pressed)
    {
        m_borderNormal = normal ? normal : 1;
        m_borderPressed = pressed ? pressed : 1;
    }

    /**
     * @brief Setzt die Innenabstände für Symbol, Zahlenkreis und seitlich ausgerichtete Beschriftung. Standard 5 und 10 Pixel
     * @param inset Abstand von der linken bzw. rechten Kante in Pixeln
     * @param iconGap Abstand zwischen Symbol oder Zahlenkreis und Beschriftung in Pixeln
     */
    void setInsets(uint8_t inset, uint8_t iconGap)
    {
        m_inset = inset;
        m_iconGap = iconGap;
    }

    /**
     * @brief Setzt den Zustand des Buttons
     * @param enabled true aktiviert den UiButton
     * @param redraw false setzt nur den Zustand, ohne zu zeichnen
     *
     * Ein unveränderter Zustand löst nie ein Neuzeichnen aus. Ohne Neuzeichnen ist für Aufrufer gedacht, die unmittelbar danach ohnehin die ganze Fläche neu aufbauen. Sonst zeichnet der UiButton sich dort zweimal.
     */
    void setEnabled(bool enabled, bool redraw = true)
    {
        if (m_enabled == enabled)
            return;

        m_enabled = enabled;

        if (redraw)
            drawButton();
    }

    /**
     * @brief Aktiviert den UiButton
     */
    void enable()
    {
        setEnabled(true);
    }

    /**
     * @brief Deaktiviert den UiButton
     */
    void disable()
    {
        setEnabled(false);
    }

    /**
     * @brief Prüft, ob der UiButton aktiviert ist
     * @return true, wenn aktiviert
     */
    bool isEnabled() const
    {
        return m_enabled;
    }

private:
    /**
     * @brief Liefert die alternative oder die bei initButton übergebene Zeichenfläche
     * @return Geliehener Zeiger auf die Zielzeichenfläche
     */
    UiCanvas *target()
    {
        return m_drawTarget ? m_drawTarget : m_tft;
    }

    /**
     * @brief Zeichnet einen geglätteten Kreis auf alternative oder primäre Zeichenfläche
     *
     * Verwendet das alternative Zeichenziel, falls gesetzt, sonst die bei initButton übergebene Zeichenfläche.
     */
    void drawSmoothCircleOnTarget(int xc, int yc, float r, float thickness, uint16_t color, uint16_t bg)
    {
        if (m_drawTarget)
            m_drawTarget->drawSmoothCircle(xc, yc, r, thickness, color, bg);
        else
            m_tft->drawSmoothCircle(xc, yc, r, thickness, color, bg);
    }

    /**
     * @brief Zeichnet eine geglättete Ellipse auf alternative oder primäre Zeichenfläche
     */
    void drawSmoothEllipseOnTarget(int xc, int yc, float rx, float ry, float thickness, uint16_t color, uint16_t bg)
    {
        if (m_drawTarget)
            m_drawTarget->drawSmoothEllipse(xc, yc, rx, ry, thickness, color, bg);
        else
            m_tft->drawSmoothEllipse(xc, yc, rx, ry, thickness, color, bg);
    }

    /**
     * @brief Zeichnet ein geglättetes Rundinstrument auf alternative oder primäre Zeichenfläche
     */
    void drawSmoothGaugeOnTarget(int xc, int yc, float r, float thickness, float angleStart, float angleEnd,
                                 float valueMin, float valueMax, float value,
                                 uint16_t color, uint16_t colorTrack, bool trackVisible, uint16_t bg)
    {
        if (m_drawTarget)
            m_drawTarget->drawSmoothGauge(xc, yc, r, thickness, angleStart, angleEnd, valueMin, valueMax, value, color, colorTrack, trackVisible, bg);
        else
            m_tft->drawSmoothGauge(xc, yc, r, thickness, angleStart, angleEnd, valueMin, valueMax, value, color, colorTrack, trackVisible, bg);
    }

    /**
     * @brief Interne Initialisierung
     */
    void _init_button(UiCanvas *gfx, int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t r, const char *label, const uint16_t *icon, float textsize_x, float textsize_y);

    /**
     * @brief Prüft ob Label ein Symbol-Code ist (^SYMBOL^)
     */
    bool isSymbolLabel(const char *label) const;

    /**
     * @brief Zeichnet Symbol basierend auf Label-Code
     */
    void drawSymbolFromLabel(const char *label, int16_t x, int16_t y, uint16_t color);

    /**
     * @brief Liest Größe und Farbe aus einem Symbol-Label (^SYMBOL:Größe:Farbe^)
     * @param label Symbol-Label, z. B. "^POWER:24:F800^" oder "^STAR::07E0^"
     * @param defaultSize Größe, falls das Label keine nennt
     * @param color Wird nur überschrieben, wenn das Label eine RGB565-Farbe als vier Hexziffern nennt
     * @return Größe des Symbols
     */
    int16_t parseSymbolAttributes(const char *label, int16_t defaultSize, uint16_t &color) const;

    /** @brief Zeichnet das Umschaltsymbol (Pfeil mit Stiel). Nur die Höhe wird skaliert mit Mittelpunkt (x, y). size ist die Höhe in Pixeln. */
    void drawShiftSymbol(int16_t x, int16_t y, int16_t size, uint16_t color);
    /** @brief Zeichnet das Feststellsymbol (Pfeil mit Rechteck unten). Nur die Höhe wird skaliert mit Mittelpunkt (x, y). size ist die Höhe in Pixeln. */
    void drawCapsLockSymbol(int16_t x, int16_t y, int16_t size, uint16_t color);
    /** @brief Zeichnet das Rücktastensymbol mit glatten Linien mit Mittelpunkt (x, y). size ist die Höhe in Pixeln. */
    void drawBackSpaceSymbol(int16_t x, int16_t y, int16_t size, uint16_t color);
    /** @brief Zeichnet das Augensymbol mit Kantenglättung mit Mittelpunkt (x, y). size ist die Höhe in Pixeln. */
    void drawEyeOnSymbol(int16_t x, int16_t y, int16_t size, uint16_t color);
    /** @brief Zeichnet das durchgestrichenes Augensymbol mit Kantenglättung mit Mittelpunkt (x, y). size ist die Höhe in Pixeln. */
    void drawEyeOffSymbol(int16_t x, int16_t y, int16_t size, uint16_t color);
    /** @brief Zeichnet das geschlossenes Schloss mit glatten Linien mit Mittelpunkt (x, y). size ist die Höhe in Pixeln. */
    void drawLockSymbol(int16_t x, int16_t y, int16_t size, uint16_t color);
    /** @brief Zeichnet das Aktualisierensymbol (Kreispfeil) mit glatten Linien mit Mittelpunkt (x, y). size ist die Höhe in Pixeln. */
    void drawRefreshSymbol(int16_t x, int16_t y, int16_t size, uint16_t color);
    /** @brief Zeichnet das Löschsymbol als Kreuz mit Mittelpunkt (x, y). size ist die Höhe in Pixeln. */
    void drawDeleteSymbol(int16_t x, int16_t y, int16_t size, uint16_t color);
    /** @brief Zeichnet das Haussymbol als Kontur nach House Bold von Phosphor Icons (MIT, Lizenztext in IconShapeData.h) mit Mittelpunkt (x, y). size ist die Bezugsgröße in Pixeln (32er-Raster wie im Original). */
    void drawHomeSymbol(int16_t x, int16_t y, int16_t size, uint16_t color);
    /** @brief Zeichnet das Zahnradsymbol mit Mittelpunkt (x, y). size ist die Höhe in Pixeln. */
    void drawSettingsSymbol(int16_t x, int16_t y, int16_t size, uint16_t color);
    /** @brief Zeichnet das Pfeil nach links mit Mittelpunkt (x, y). size ist die Höhe in Pixeln. */
    void drawBackSymbol(int16_t x, int16_t y, int16_t size, uint16_t color);
    /** @brief Zeichnet das Wiedergabesymbol (Dreieck) mit Mittelpunkt (x, y). size ist die Höhe in Pixeln. */
    void drawPlaySymbol(int16_t x, int16_t y, int16_t size, uint16_t color);
    /** @brief Zeichnet das Pausesymbol (zwei Balken) mit Mittelpunkt (x, y). size ist die Höhe in Pixeln. */
    void drawPauseSymbol(int16_t x, int16_t y, int16_t size, uint16_t color);
    /** @brief Zeichnet das Stoppsymbol (Quadrat) mit Mittelpunkt (x, y). size ist die Höhe in Pixeln. */
    void drawStopSymbol(int16_t x, int16_t y, int16_t size, uint16_t color);
    /** @brief Zeichnet das Pluszeichen mit Mittelpunkt (x, y). size ist die Höhe in Pixeln. */
    void drawPlusSymbol(int16_t x, int16_t y, int16_t size, uint16_t color);
    /** @brief Zeichnet das Minuszeichen mit Mittelpunkt (x, y). size ist die Höhe in Pixeln. */
    void drawMinusSymbol(int16_t x, int16_t y, int16_t size, uint16_t color);
    /** @brief Zeichnet das Vorlaufsymbol (doppelte spitze Klammer nach rechts) mit Mittelpunkt (x, y). size ist die Höhe in Pixeln. */
    void drawForwardSymbol(int16_t x, int16_t y, int16_t size, uint16_t color);
    /** @brief Zeichnet das Rücklaufsymbol (doppelte spitze Klammer nach links) mit Mittelpunkt (x, y). size ist die Höhe in Pixeln. */
    void drawBackwardSymbol(int16_t x, int16_t y, int16_t size, uint16_t color);
    /** @brief Zeichnet das Titelsprung vorwärts (Doppeldreieck mit Haltebalken rechts) mit Mittelpunkt (x, y). size ist die Höhe in Pixeln. */
    void drawSkipForwardSymbol(int16_t x, int16_t y, int16_t size, uint16_t color);
    /** @brief Zeichnet das Titelsprung rückwärts (Haltebalken links, Doppeldreieck nach links) mit Mittelpunkt (x, y). size ist die Höhe in Pixeln. */
    void drawSkipBackwardSymbol(int16_t x, int16_t y, int16_t size, uint16_t color);
    /** @brief Zeichnet das fünfzackiger Stern mit Mittelpunkt (x, y). size ist die Höhe in Pixeln. */
    void drawStarSymbol(int16_t x, int16_t y, int16_t size, uint16_t color);
    /** @brief Zeichnet das Ein/Aus-Symbol mit Mittelpunkt (x, y). size ist die Höhe in Pixeln. */
    void drawPowerSymbol(int16_t x, int16_t y, int16_t size, uint16_t color);
    /** @brief Zeichnet das Ordnersymbol mit Mittelpunkt (x, y). size ist die Höhe in Pixeln. */
    void drawFolderSymbol(int16_t x, int16_t y, int16_t size, uint16_t color);
    /** @brief Zeichnet das Cursorpfeil nach links mit Mittelpunkt (x, y). size ist die Höhe in Pixeln. */
    void drawCLeftSymbol(int16_t x, int16_t y, int16_t size, uint16_t color);
    /** @brief Zeichnet das Cursorpfeil nach rechts mit Mittelpunkt (x, y). size ist die Höhe in Pixeln. */
    void drawCRightSymbol(int16_t x, int16_t y, int16_t size, uint16_t color);

    UiCanvas *m_tft = nullptr;               ///< Geliehene Zeichenfläche aus initButton
    drawCb m_drawCb = nullptr;               ///< Benutzerdefinierte Zeichenfunktion
    int16_t m_x = 0;                         ///< X-Position (Zentrum)
    int16_t m_y = 0;                         ///< Y-Position (Zentrum)
    int16_t m_xd = 0;                        ///< X-Verschiebung für Text
    int16_t m_yd = 0;                        ///< Y-Verschiebung für Text
    uint16_t m_w = 0;                        ///< Breite
    uint16_t m_h = 0;                        ///< Höhe
    uint16_t m_r = 0;                        ///< Eckenradius
    uint16_t m_outlinecolor = 0;             ///< Rahmenfarbe, gesetzt von initButton
    uint16_t m_fillcolor = 0;                ///< Füllfarbe, gesetzt von initButton
    uint16_t m_textcolor = 0;                ///< Textfarbe, gesetzt von initButton
    uint8_t m_textdatum = MC_DATUM;          ///< Text-Ausrichtung
    static constexpr size_t label_length = 30; ///< Maximale Länge der Beschriftung in Bytes
    char m_label[label_length + 1];                ///< UiButton-Text
    const uint16_t *m_icon = nullptr;              ///< Icon-Daten
    float m_textsize_x, _textsize_y;               ///< Textgröße
    bool m_pressed = false;                        ///< Aktueller Status
    bool m_last_press = false;                     ///< Vorheriger Status
    IconPosition m_iconPos = IconPosition::Center; ///< Icon-Position
    int16_t m_iconWidth = 32;                      ///< Icon-Breite
    int16_t m_iconHeight = 32;                     ///< Icon-Höhe
    const uint8_t *m_font_legacy = nullptr;        ///< Zeiger für Legacy-Schriftarten
    const lgfx::v1::GFXfont *m_font_gfx = nullptr; ///< Geliehene lgfx::GFXfont-Schrift
    bool m_useNumberCircle = false;                ///< Zahlenkreis aktiviert
    uint8_t m_circleNumber = 0;                    ///< Anzuzeigende Zahl
    bool m_enabled = true;                         ///< UiButton ist aktiviert/deaktiviert
    int16_t m_labelOffsetX = 0;                    ///< Zusätzlicher X-Offset für das Label
    int16_t m_labelOffsetY = 0;                    ///< Zusätzlicher Y-Offset für das Label
    uint8_t m_borderNormal = 1;                    ///< Rahmenbreite im Ruhezustand in Pixeln
    uint8_t m_borderPressed = 2;                   ///< Rahmenbreite bei gedrückter Taste in Pixeln
    uint8_t m_inset = 5;                           ///< Abstand von Symbol, Zahlenkreis oder seitlicher Beschriftung zur Kante
    uint8_t m_iconGap = 10;                        ///< Abstand zwischen Symbol oder Zahlenkreis und Beschriftung
    UiCanvas *m_drawTarget = nullptr;            ///< Geliehenes alternatives Zeichenziel, nullptr verwendet m_tft
    int16_t m_targetOriginX = 0;                   ///< X-Position, an der das Sprite-Ziel später auf dem Display liegt
    int16_t m_targetOriginY = 0;                   ///< Y-Position, an der das Sprite-Ziel später auf dem Display liegt
};

#endif // UI_BUTTON_H
