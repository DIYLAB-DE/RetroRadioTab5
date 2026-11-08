#ifndef RADIO_DIALOG_CANVAS_H
#define RADIO_DIALOG_CANVAS_H
#include "UiCanvas.h"

/** @brief Displaygroße Zeichenfläche für das Radio und seine Dialoge. Touch kommt ausschließlich zentral per setTouch(), nie von der Hardware. */
class RadioDialogCanvas : public UiCanvas
{
public:
    /** @brief Übernimmt einen Kontakt ohne Hardwarezugriff. @param x X-Koordinate in Displaypixeln. @param y Y-Koordinate in Displaypixeln. @param down true bei gedrücktem Kontakt. */
    void setTouch(int x, int y, bool down)
    {
        m_point.x = x;
        m_point.y = y;
        m_down = down;
    }

    /** @brief Kopiert den letzten zugeteilten Kontakt auch nach dem Loslassen. @param point Gültiger Ausgabezeiger, nicht nullptr. @return Zuletzt gesetzter Druckzustand. */
    bool getTouch(lgfx::touch_point_t *point)
    {
        *point = m_point;
        return m_down;
    }

private:
    lgfx::touch_point_t m_point{}; ///< Zuletzt zugeteilte Position
    bool m_down = false;           ///< Kontakt gedrückt
};
#endif
