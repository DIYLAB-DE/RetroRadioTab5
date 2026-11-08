#ifndef MESSAGE_BOX_H
#define MESSAGE_BOX_H
#include <cstddef>
#include <cstdint>
#include "Strings_settings.h"

/** @brief Gemeinsame Bedien- und Textbausteine von Einstellungen, Tastatur und Dialogen. */
namespace SettingsUi
{
    /** @brief Zentral angelieferte Kontaktphase in logischen Displaykoordinaten. */
    enum class TouchPhase : uint8_t
    {
        Press,   ///< Erster Kontakt
        Move,    ///< Kontakt bewegt sich oder bleibt liegen
        Release, ///< Kontakt gelöst
        Cancel   ///< Kontakt entzogen
    };

    /** @brief Rechteck mit exklusiver rechter und unterer Grenze. */
    struct Rect
    {
        int x, y, w, h;

        /** @brief Prüft einen Punkt. @param px X. @param py Y. @return Punkt im Rechteck. */
        bool contains(int px, int py) const
        {
            return px >= x && px < x + w && py >= y && py < y + h;
        }
    };

    /** @brief Kontaktverfolgung ohne erneute Aktivierung nach Herausziehen. */
    class Contact
    {
    public:
        /** @brief Verarbeitet genau eine Phase. @param phase Phase. @param target Getroffenes Ziel oder -1. @return Bestätigtes Ziel nur bei Release, sonst -1. */
        int process(TouchPhase phase, int target);

        /** @brief Verwirft den laufenden Kontakt. */
        void cancel()
        {
            m_target = -1;
        }

        /** @brief Liefert das noch gedrückte Ziel. @return Ziel oder -1. */
        int pressed() const
        {
            return m_target;
        }

    private:
        int m_target = -1; ///< Beim Drücken getroffenes Ziel, -1 ohne
    };

    /** @brief Prüft begrenzten, terminierenden UTF-8-Text ohne Steuerzeichen. @param text Puffer. @param capacity Puffergröße. @return Gültigkeit. */
    bool validText(const char *text, size_t capacity);
    /** @brief Entfernt das letzte vollständige UTF-8-Zeichen. @param text Terminierender Puffer. */
    void eraseLast(char *text);
}

#endif
