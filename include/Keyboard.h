#ifndef KEYBOARD_H
#define KEYBOARD_H
#include "MessageBox.h"

/** @brief Nicht blockierende Bildschirmtastatur mit eigenem rücknehmbarem Eingabepuffer. */
class Keyboard
{
public:
    /** @brief Eingabeart. */
    enum class Mode : uint8_t
    {
        Text,     ///< Beliebiger UTF-8-Text, offen angezeigt, mit Groß- und Sonderzeichenebene
        Password  ///< Druckbares ASCII, standardmäßig verdeckt, mit Sichtbarkeitstaste
    };
    /** @brief Ergebnis einer Berührung. */
    enum class Result : uint8_t
    {
        None,      ///< Eingabe läuft weiter
        Accepted,  ///< Übernehmen gedrückt, Wert über value() abholen
        Cancelled  ///< Abbrechen gedrückt
    };
    /** @brief Öffnet die Tastatur mit einer kopierten Anfangseingabe. @param title Titel. @param initial Anfangswert, gültiges UTF-8 und für den Modus zulässig. @param maxBytes Maximale Länge in UTF-8-Bytes, 1 bis 64. @param mode Eingabeart. @param language Sprache. @return false bei ungültigen Grenzen oder Texten. Die Tastatur bleibt dann geschlossen. */
    bool open(const char *title, const char *initial, size_t maxBytes, Mode mode, LangIndex language);
    /** @brief Verarbeitet eine Kontaktphase. Zeichen werden bei Release der Taste angehängt. @param phase Phase. @param x,y Position in Displaypixeln. @return Abschluss oder None. Der Puffer bleibt bis close() erhalten. */
    Result touch(SettingsUi::TouchPhase phase, int x, int y);
    /** @brief Hängt gültigen UTF-8-Text an die Eingabe an, soweit der Modus ihn zulässt. @param value Text, höchstens 64 Bytes. @return true, wenn der ganze Text übernommen wurde. Sonst bleibt die Eingabe unverändert und der Hinweis nennt den Grund (nur bei Überlänge und ungültigem Passwortzeichen). */
    bool append(const char *value);
    /** @brief Löscht das letzte vollständige UTF-8-Zeichen. */
    void backspace();

    /** @brief Ersetzt den Hinweistext unter dem Eingabefeld, etwa durch einen Fehler des Aufrufers. @param key Text. Die nächste Eingabe stellt den Standardhinweis wieder her. */
    void setError(SettingsKey key)
    {
        m_hint = key;
    }

    /** @brief Liefert die aktuelle Eingabe. @return Zeiger auf den internen Puffer, gültig bis close() oder zur nächsten Eingabe. */
    const char *value() const
    {
        return m_value;
    }

    /** @brief Verwirft die Eingabe und überschreibt den Puffer. */
    void close();

    /** @brief Bricht den Kontakt ab. */
    void cancelTouch()
    {
        m_contact.cancel();
    }

    /** @brief Liefert das gedrückte Ziel für gezielte Neuzeichnung. @return Ziel (0..39 Zeichentaste, 40 Umschalten, 41 Sonderzeichen, 42 Leertaste, 43 Löschen, 100 Abbrechen, 101 Übernehmen, 102 Sichtbarkeit) oder -1. */
    int pressed() const
    {
        return m_contact.pressed();
    }

    /** @brief Meldet den Modalzustand. @return true bei geöffneter Tastatur. */
    bool active() const
    {
        return m_active;
    }

private:
    char m_title[96] = {}, m_value[65] = {};        ///< Titel, Eingabepuffer für höchstens 64 Bytes samt Terminator
    size_t m_limit = 0;                              ///< Maximale Eingabelänge in Bytes
    Mode m_mode = Mode::Text;                        ///< Eingabeart
    LangIndex m_language = LangIndex::DE;            ///< Sprache der Beschriftungen
    SettingsKey m_hint = SettingsKey::InputHint;     ///< Hinweistext unter dem Eingabefeld
    bool m_active = false, m_upper = false, m_symbols = false, m_visible = false; ///< Geöffnet, Großbuchstaben, Sonderzeichenebene, Passwort sichtbar
    SettingsUi::Contact m_contact;                   ///< Kontaktverfolgung der Tasten
    /** @brief Liefert die Beschriftung und das Zeichen der Taste 0..39 im Raster 10x4 für die aktuelle Ebene. Leer, wo keine Taste liegt. */
    const char *key(size_t index) const;
    /** @brief Liefert das Rechteck der Zeichentaste 0..39 im Raster 10x4. */
    SettingsUi::Rect keyRect(size_t index) const;
};
#endif
