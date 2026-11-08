#ifndef NUMPAD_H
#define NUMPAD_H

#include <stdint.h>

#include "UiCanvas.h"
#include "SettingsTab5Layout.h"

/**
 * @class Numpad
 * @brief Ziffernwidget mit 14 Tasten in einem 4x4-Raster für die Konfiguration.
 *
 * Kapselt Layout, Zeichnung und Treffertest. Die aufrufende Ansicht verarbeitet die Aktionszeichen. Das Widget speichert keine Eingabe und führt keine Rechnung aus.
 *
 * Layout des 4×4-Grids (content-relativ ab startY):
 *   Linke 3 Spalten × 4 Zeilen (normale Höhe):  7-9 / 4-6 / 1-3 / , 0 +/-
 *   Rechte Spalte (doppelte Höhe):               BS (Zeilen 0+1), C (Zeilen 2+3)
 *
 * Tastenhöhe (TARGET_BTN_H):
 *   Die normale Tastenhöhe beträgt 41 Pixel (Original 27), vier Zeilen einschließlich dreier Lücken benötigen REQUIRED_H Pixel.
 *
 * Koordinatensystem:
 *   computeLayout() erhält die Breite und einen Y-Start relativ zum Inhalt. draw() addiert originX/originY, hitTest() erwartet Koordinaten relativ zu diesem Ursprung.
 *
 * Aktionszeichen für den Aufrufer:
 *   '0'-'9' = Ziffer, '.' = Dezimalpunkt (Beschriftung „,“), 'S' = Vorzeichen umschalten,
 *   'B' = Backspace, 'C' = Clear
 */
class Numpad
{
public:
    /** @brief Anzahl der Nummernpad-Tasten (12 normale + BS + C). */
    static constexpr int NUM_KEYS = 14;

    /** @brief Anzahl Tastenzeilen im Grid. */
    static constexpr int PAD_ROWS = 4;

    /** @brief Anzahl Tastenspalten im Grid. */
    static constexpr int PAD_COLS = 4;

    /** @brief Abstand zwischen zwei Tasten in Pixeln (Original in SettingsTab5Layout::Security). */
    static constexpr int BTN_GAP = SettingsTab5Layout::Security::PAD_GAP;

    /** @brief Höhe einer normalen Taste in Pixeln. */
    static constexpr int TARGET_BTN_H = SettingsTab5Layout::Security::PAD_KEY_H;

    /**
     * @brief Reservierter Höhenbedarf des Nummernpads in Pixeln.
     *
     * Berechnet sich aus vier Tastenzeilen zu je TARGET_BTN_H und drei Lücken zu je BTN_GAP. Der Aufrufer bestimmt die Position innerhalb seines Inhaltsbereichs über startY.
     */
    static constexpr int REQUIRED_H = PAD_ROWS * TARGET_BTN_H + (PAD_ROWS - 1) * BTN_GAP;

    /** @brief Setzt alle Layout-Rects und internen Größen auf null. */
    void clearLayout();

    /**
     * @brief Berechnet Tasten-Rechtecke für die gegebene Breite und Y-Position.
     * @param w      Verfügbare Breite in Pixeln, Tasten werden mindestens einen Pixel breit
     * @param startY Y-Start des Nummernpads relativ zum Inhalt
     *
     * Alle berechneten Rechtecke sind relativ zum Inhaltsursprung. Vor dem ersten Zeichnen oder Treffertest und nach Größenänderungen computeLayout aufrufen.
     */
    void computeLayout(int w, int startY);

    /**
     * @brief Zeichnet Hintergrund und alle Tasten des Nummernpads.
     * @param spr        Vorbereitete, geliehene Zeichenfläche. Darf nicht nullptr sein
     * @param originX    Linke Kante des Inhaltsbereichs im Sprite
     * @param originY    Obere Kante des Inhaltsbereichs im Sprite
     * @param pressedKey Index der aktuell gedrückten Taste, -1 bedeutet keine Taste
     *
     * Lädt intern ComfortaaBold24 für die Tastenbeschriftung. Der Hintergrund wird mit dem Grund des Arbeitsbereichs (Rolle Gem.Flaeche) gefüllt, bevor die Tasten gezeichnet werden.
     */
    void draw(UiCanvas *spr, int originX, int originY, int pressedKey) const;

    /**
     * @brief Zeichnet eine einzelne Taste (für Tap-Feedback).
     * @param spr        Ziel-Sprite
     * @param originX    Linke Kante des Inhaltsbereichs im Sprite
     * @param originY    Obere Kante des Inhaltsbereichs im Sprite
     * @param keyIdx     Tastenindex 0 bis NUM_KEYS-1
     * @param pressed    true für Tap-Feedback (Rolle Ein.NumGedrueckt)
     *
     * Setzt voraus, dass ComfortaaBold24 bereits geladen ist.
     */
    void drawKey(UiCanvas *spr, int originX, int originY,
                 int keyIdx, bool pressed) const;

    /**
     * @brief Treffertest auf das Nummernpad (content-relativ).
     * @param cx X-Koordinate relativ zum Inhaltsursprung (originX/originY)
     * @param cy Y-Koordinate relativ zum Inhaltsursprung
     * @return Tastenindex 0 bis NUM_KEYS-1 oder -1 bei keinem Treffer
     */
    int hitTest(int cx, int cy) const;

    /**
     * @brief Gibt das Aktionszeichen einer Taste zurück.
     * @param keyIdx Tastenindex 0 bis NUM_KEYS-1
     * @return Aktionszeichen ('0'-'9', '.', 'S', 'B', 'C') oder '\0' bei ungültigem Index
     *
     * Der Aufrufer entscheidet, welche Aktionszeichen im jeweiligen Eingabemodus erlaubt sind.
     */
    static char keyAction(int keyIdx);

private:
    /** @brief Achsenparalleles Rechteck (content-relativ). */
    struct Rect
    {
        int x; ///< Linke Kante
        int y; ///< Obere Kante
        int w; ///< Breite
        int h; ///< Höhe
    };

    /** @brief Tastentyp für Farbzuordnung. */
    enum class KeyType : uint8_t
    {
        DIGIT,     ///< Ziffern 0-9 und Dezimalzeichen
        ACTION,    ///< Vorzeichen (+/-)
        BACKSPACE, ///< Backspace-Taste (Vektorsymbol)
        CLEAR      ///< Clear-Taste
    };

    /** @brief Statische Beschreibung einer Nummernpad-Taste. */
    struct KeyEntry
    {
        char label[5]; ///< Anzeigetext (ASCII, max. 4 Zeichen + NUL)
        char action;   ///< Aktionszeichen
        KeyType type;  ///< Tastentyp für Farbzuordnung
    };

    static constexpr int BTN_RADIUS = SettingsTab5Layout::Security::PAD_R;       ///< Eckenradius der Tasten in Pixeln
    static constexpr int BTN_TEXT_OY = SettingsTab5Layout::Security::PAD_TEXT_OY; ///< Vertikaler Versatz der Beschriftung in Pixeln, gleicht die Schriftmetrik aus

    /**
     * @brief Tastendefinitionen des 4×4-Grids.
     *
     * Indizes 0-11: linke 3 Spalten × 4 Zeilen mit normaler Höhe. Index 12: BS - rechte Spalte, Zeilen 0+1 (doppelt hoch). Index 13: C - rechte Spalte, Zeilen 2+3 (doppelt hoch).
     */
    static const KeyEntry KEYS[NUM_KEYS];

    Rect m_keyRects[NUM_KEYS] = {}; ///< Tasten-Rechtecke (content-relativ, von computeLayout() befüllt)
    int m_btnW = 0;                 ///< Berechnete Tastenbreite in Pixeln
    int m_btnH = 0;                 ///< Berechnete Tastenhöhe in Pixeln (= TARGET_BTN_H nach computeLayout)
    int m_startY = 0;               ///< Y-Start des Nummernpads relativ zum Inhalt
    int m_w = 0;                    ///< Inhaltsbereich-Breite in Pixeln (für Hintergrund-fillRect)

    /**
     * @brief Zeichnet das Backspace-Vektorsymbol (Pfeil-Raute mit Kreuz).
     * @param spr   Ziel-Sprite
     * @param cx    Mittelpunkt X (sprite-relativ)
     * @param cy    Mittelpunkt Y (sprite-relativ)
     * @param size  Gesamtgröße des Symbols in Pixeln
     * @param color Linienfarbe (RGB565)
     *
     * Geometrie: Rahmen oben, unten und rechts, schräge Pfeilkanten links sowie ein Kreuz in der Mitte.
     */
    void drawBackspaceSymbol(UiCanvas *spr, int cx, int cy,
                             int size, uint16_t color) const;
};

#endif // NUMPAD_H
