#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>

#if defined(ARDUINO_ARCH_ESP32)
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#endif
#include <atomic>

#ifndef LOGGER_ENABLED
#define LOGGER_ENABLED 1
#endif

#if LOGGER_ENABLED
/**
 * @brief Farbiger Singleton-Logger mit einmalig reserviertem Ausgabepuffer für ESP32/Arduino-Projekte.
 *
 * Diese Klasse implementiert das Singleton-Pattern und ermöglicht performantes, farbiges Logging im Terminal von überall im Projekt, ohne dynamische String-Allokationen zur Laufzeit. Die Ausgabe erfolgt über die serielle Schnittstelle mit ANSI-Farbcodes.
 *
 * @note Es werden ANSI-Farbcodes verwendet. Die Darstellung im Terminal hängt vom verwendeten Programm ab.
 *
 * @note Auf dem ESP32 baut der Logger jede Zeile unter einem FreeRTOS-Mutex auf und übergibt sie an eine Warteschlange, auf Serial schreibt nur ein Schreibtask (siehe init()).
 *
 * @example
 * // Einmalige Initialisierung in main.cpp
 * Logger::init(true, true, 1024);
 *
 * // Verwendung mit getInstance()
 * Logger::getInstance().println(Logger::INFO, "Systemstart erfolgreich!");
 * Logger::getInstance().printf(Logger::SUCCESS, "Wert: %d", 1234);
 *
 * // Oder mit den praktischen Makros für kürzere Syntax
 * LOG_INFO("Systemstart erfolgreich!");
 * LOG_SUCCESS_F("Wert: %d", 1234);
 * LOG_ERROR("Fehler aufgetreten!");
 */
class Logger
{
public:
    // Öffentliche Typen und Enums

    /**
     * @enum Level
     * @brief Vordefinierte Logging-Level zur strukturierten Log-Ausgabe.
     *
     * Jeder Level hat eine eigene Farbe und ein eigenes Tag-Label.
     */
    enum Level
    {
        INFO,     ///< Allgemeine Information (blau)
        WARNING,  ///< Warnung (gelb)
        ERROR,    ///< Fehler (rot)
        SUCCESS,  ///< Erfolgsmeldung (grün)
        CUSTOM,   ///< Benutzerdefiniertes Tag (cyan)
        BITRATE,  ///< Bitraten-Informationen (magenta)
        TITEL,    ///< Titel-Informationen (cyan)
        SENDER,   ///< Sender-Informationen (hell-magenta)
        AUDIO,    ///< Audio-bezogene Meldungen (hell-blau)
        RADIO,    ///< Radio-bezogene Meldungen (hell-grün)
        SETTINGS  ///< Einstellungs-bezogene Meldungen (weiß)
    };

    /**
     * @struct Tag
     * @brief Definition für ein einzelnes Log-Tag mit Name und Farbe.
     *
     * Wird verwendet, um die Zuordnung von Level zu Farbe und Label zu speichern.
     */
    struct Tag
    {
        const char *name;  ///< Anzeigename des Tags (z.B. "info", "error")
        const char *color; ///< ANSI-Farbe als Escape-String
    };

    // ANSI-Farbcodes (16-Farben-Palette)

    // Normale Farben
    static constexpr const char *BLACK = "\033[30m";   ///< Schwarz
    static constexpr const char *RED = "\033[31m";     ///< Rot
    static constexpr const char *GREEN = "\033[32m";   ///< Grün
    static constexpr const char *YELLOW = "\033[33m";  ///< Gelb
    static constexpr const char *BLUE = "\033[34m";    ///< Blau
    static constexpr const char *MAGENTA = "\033[35m"; ///< Magenta
    static constexpr const char *CYAN = "\033[36m";    ///< Cyan
    static constexpr const char *WHITE = "\033[37m";   ///< Weiß

    // Helle Farben
    static constexpr const char *BRIGHT_BLACK = "\033[90m";   ///< Hell-Schwarz / Grau
    static constexpr const char *BRIGHT_RED = "\033[91m";     ///< Hellrot
    static constexpr const char *BRIGHT_GREEN = "\033[92m";   ///< Hellgrün
    static constexpr const char *BRIGHT_YELLOW = "\033[93m";  ///< Hellgelb
    static constexpr const char *BRIGHT_BLUE = "\033[94m";    ///< Hellblau
    static constexpr const char *BRIGHT_MAGENTA = "\033[95m"; ///< Hellmagenta
    static constexpr const char *BRIGHT_CYAN = "\033[96m";    ///< Hellcyan
    static constexpr const char *BRIGHT_WHITE = "\033[97m";   ///< Hellweiß

    static constexpr const char *RESET = "\033[0m"; ///< Setzt die Textfarbe zurück

    /**
     * @brief Vordefinierte Tags für die Logger-Ausgabe.
     *
     * Array mit allen definierten Log-Levels und ihren zugehörigen Namen und Farben. Die Reihenfolge entspricht dem Level-Enum.
     */
    static const Tag TAGS[11];

    // Singleton-Verwaltung

    /**
     * @brief Initialisiert den Singleton-Logger (nur einmal in main.cpp aufrufen).
     * @param enabled      Logging global aktivieren/deaktivieren
     * @param withTime     Zeitangabe bei jeder Log-Ausgabe einblenden
     * @param bufferSize   Größe des internen Puffers für Ausgaben (empfohlen: ≥512)
     * @return Referenz auf die Logger-Instanz
     *
     * @note Diese Funktion sollte nur einmal beim Programmstart aufgerufen werden. Weitere Aufrufe haben keine Wirkung.
     *
     * @warning Ein zu kleiner Puffer kann Ausgaben abschneiden. Die empfohlene Mindestgröße beträgt 512 Byte.
     *
     * Legt auf dem ESP32 einmalig eine Zeilenwarteschlange von 8 KB im internen RAM und einen Schreibtask an und leitet die ROM-Ausgabe (`ets_printf`, Arduinos `log_x`) sowie stdout und stderr (`printf`, ESP-IDF-Logs) dorthin um. Nur der Schreibtask schreibt danach Text auf Serial, jede Zeile mit einem Aufruf. Er läuft auf dem Kern des aufrufenden Tasks. init() muss deshalb im Task laufen, der Serial.begin() aufgerufen hat. Arduinos USB-Ausgabe (HWCDC) verliert sonst Bytes: Ein Schreibaufruf von einem anderen Kern schließt das Paket ab, das der USB-Interrupt gerade füllt, und der Interrupt verwirft den Rest seines Blocks von bis zu 64 Byte. Logaufrufe warten nie. Ist die Warteschlange voll, wird die Ausgabe verworfen und später als Anzahl gemeldet. Scheitert die Anlage, schreibt der Logger wie zuvor direkt auf Serial.
     */
    static Logger &init(bool enabled = true, bool withTime = false, size_t bufferSize = 512);

    /**
     * @brief Gibt die Singleton-Instanz zurück.
     * @return Referenz auf die Logger-Instanz
     *
     * @note Löst eine Assertion aus, wenn init() noch nicht aufgerufen wurde. Im Release-Build kann dies zu undefiniertem Verhalten führen.
     */
    static Logger &getInstance();

    /**
     * @brief Prüft, ob der Logger bereits initialisiert wurde.
     * @return true wenn Logger initialisiert ist, sonst false
     */
    static bool isInitialized();

    /** @brief Meldet eine Ausgabe, die außerhalb des Schreibtasks direkt auf Serial ging (Bildpakete). Der Schreibtask sendet nach dem Abfließen ein Nullpaket, damit der Host auch ein zuletzt volles 64-Byte-Paket sofort weitergibt. Wartet nie, aus jedem Task aufrufbar. */
    static void usbOutputWritten();

    // Kopierkonstruktor und Zuweisungsoperator verhindern (Singleton)
    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;

    /**
     * @brief Destruktor: Gibt den Puffer und den Mutex wieder frei.
     */
    ~Logger();

    // Konfiguration

    /**
     * @brief Logging ein- oder ausschalten.
     * @param enabled true = Logging aktiv, false = alle Ausgaben unterdrückt
     *
     * Wenn Logging deaktiviert ist, werden alle Log-Aufrufe ignoriert und erzeugen keine Ausgabe auf Serial. Ausgenommen sind Ausgaben innerhalb eines AnswerScope. Panik-, ROM- und stdout-Ausgaben gehen ohnehin am Schalter vorbei. Aus jedem Task aufrufbar.
     */
    void setEnabled(bool enabled);

    /**
     * @brief Lässt die Ausgaben des erzeugenden Tasks auch bei ausgeschaltetem Logging durch, solange das Objekt lebt.
     *
     * Für Antworten auf USB-Befehle in der Hauptschleife, damit Diagnose und Prüfskripte unabhängig vom Schalter arbeiten. Ausgaben anderer Tasks bleiben unterdrückt. Nicht verschachteln.
     */
    class AnswerScope
    {
    public:
        AnswerScope();
        ~AnswerScope();
        AnswerScope(const AnswerScope &) = delete;
        AnswerScope &operator=(const AnswerScope &) = delete;
    };

    /**
     * @brief Zeitstempel-Ausgabe aktivieren/deaktivieren.
     * @param enabled true = Zeitangabe anzeigen, false = ohne Zeitangabe
     *
     * Die Zeit wird relativ zum Start bzw. zum letzten resetTime() berechnet und im Format MM:SS.mmm angezeigt.
     */
    void setTimeOutput(bool enabled);

    /**
     * @brief Setzt den Startzeitpunkt zurück (z.B. für neue Messperioden).
     *
     * Nach dem Aufruf beginnt die Zeitmessung von neuem bei 00:00.000.
     */
    void resetTime();

    // Standard-Logging mit vordefinierten Tags

    /**
     * @brief Loggt eine Nachricht ohne Zeilenumbruch.
     * @param level Log-Level (INFO, ERROR, SUCCESS, ...)
     * @param msg   Nachricht (const char*)
     *
     * Gibt die Nachricht mit dem entsprechenden Tag aus, ohne automatischen Zeilenumbruch am Ende.
     */
    void print(Level level, const char *msg);

    /**
     * @brief Loggt eine Nachricht mit Zeilenumbruch.
     * @param level Log-Level (INFO, ERROR, SUCCESS, ...)
     * @param msg   Nachricht (const char*)
     *
     * Gibt die Nachricht mit dem entsprechenden Tag aus und fügt automatisch einen Zeilenumbruch am Ende hinzu.
     */
    void println(Level level, const char *msg);

    /**
     * @brief Formatiertes Logging im printf-Stil.
     * @param level  Log-Level
     * @param format Format-String (wie printf)
     * @param ...    Variablen-Argumente
     *
     * Ermöglicht formatierte Ausgaben mit Variablen, z.B.:
     * logger.printf(Logger::INFO, "Wert: %d, Name: %s", 42, "Test");
     *
     * @note Bei Formatierungsfehler, leerem Formatergebnis oder unzureichender Puffergröße wird nur die Fehlermeldung „Buffer overflow“ ausgegeben.
     */
    void printf(Level level, const char *format, ...);

    /**
     * @brief Formatierte Zeile ohne Tag und Farbe, etwa für Statuszeilen, die Hostskripte auswerten.
     * @param format Format-String (wie printf), das Zeilenende \r\n hängt der Logger an.
     * @param ...    Variablen-Argumente
     *
     * @note Ein leeres oder zu langes Ergebnis wird wie bei printf() durch die Fehlermeldung „Buffer overflow“ ersetzt.
     */
    void printfPlain(const char *format, ...);

    // Custom-Tag-Logging

    /**
     * @brief Loggt eine Nachricht mit benutzerdefiniertem Tag (mit Zeilenumbruch).
     * @param tag   Name des Custom-Tags
     * @param msg   Nachricht
     * @param color ANSI-Farbe für das Tag (optional, Standard: Cyan)
     *
     * Ermöglicht die Ausgabe mit beliebigen Tag-Namen, die nicht in den vordefinierten Levels enthalten sind.
     */
    void printlnCustom(const char *tag, const char *msg, const char *color = CYAN);

    /**
     * @brief Loggt eine Nachricht mit benutzerdefiniertem Tag (ohne Zeilenumbruch).
     * @param tag   Name des Custom-Tags
     * @param msg   Nachricht
     * @param color ANSI-Farbe für das Tag (optional, Standard: Cyan)
     *
     * Wie printlnCustom(), jedoch ohne automatischen Zeilenumbruch.
     */
    void printCustom(const char *tag, const char *msg, const char *color = CYAN);

private:
    // Private Konstruktoren und Member

    /**
     * @brief Privater Konstruktor für Singleton-Pattern.
     * @param enabled      Logging global aktivieren/deaktivieren
     * @param withTime     Zeitangabe bei jeder Log-Ausgabe einblenden
     * @param bufferSize   Größe des internen Puffers für Ausgaben
     */
    Logger(bool enabled, bool withTime, size_t bufferSize);

    static Logger *_instance; ///< Singleton-Instanz

    std::atomic<bool> _enabled;       ///< Gibt an, ob Logging aktiv ist. Schreibt die Einstellungsübernahme, lesen alle Tasks
    std::atomic<TaskHandle_t> _answerTask{nullptr}; ///< Task, dessen Ausgaben ein AnswerScope gerade durchlässt
    bool _withTime;                   ///< Gibt an, ob Zeitangaben ausgegeben werden
    uint32_t _start;                  ///< Startzeit für Zeitberechnungen (Millisekunden)
    char *_buffer;                    ///< Interner Puffer für Log-Ausgaben
    size_t _bufferSize;               ///< Größe des Puffers in Bytes
    SemaphoreHandle_t _mtx = nullptr; ///< FreeRTOS-Mutex für atomare Ausgaben

    // Private Hilfsfunktionen

    /** @brief Ausgabe erlaubt: Logging an oder der aufrufende Task steht in einem AnswerScope. */
    bool allowed() const;

    /**
     * @brief Baut das Präfix ([Tag][Zeit]) für eine Logzeile im Puffer auf.
     * @param level  Log-Level
     * @return Anzahl der belegten Bytes im Puffer nach dem Präfix
     */
    size_t buildPrefix(Level level);

    /**
     * @brief Baut das Präfix für benutzerdefinierte Tags im Puffer auf.
     * @param tag   Name des Tags
     * @param color ANSI-Farbe
     * @return Anzahl der belegten Bytes nach dem Präfix
     */
    size_t buildCustomPrefix(const char *tag, const char *color);

    /**
     * @brief Hauptfunktion für das Logging ohne String-Allokationen (Standard-Tags).
     * @param level   Log-Level
     * @param msg     Nachricht
     * @param newline true = mit Zeilenumbruch, false = ohne
     */
    void logDirect(Level level, const char *msg, bool newline);

    /**
     * @brief Logging mit benutzerdefinierten Tags (ohne String-Allokationen).
     * @param tag     Name des Custom-Tags
     * @param msg     Nachricht
     * @param color   ANSI-Farbe
     * @param newline true = mit Zeilenumbruch, false = ohne
     */
    void logCustomDirect(const char *tag, const char *msg, const char *color, bool newline);

    /**
     * @brief Formatiert Millisekunden direkt in ein char-Array.
     * @param ms      Millisekunden seit Start
     * @param buf     Zielpuffer für die Zeit
     * @param bufSize Größe des Puffers
     *
     * Format: MM:SS.mmm (z.B. "02:34.567")
     */
    void formatMillisDirect(uint32_t ms, char *buf, size_t bufSize) const;

    /**
     * @brief Sperrt den Mutex für Thread-Safe-Zugriff.
     */
    void lock();

    /**
     * @brief Gibt den Mutex wieder frei.
     */
    void unlock();
};

// MAKROS FÜR KÜRZERE SYNTAX

/**
 * @brief Makros für vereinfachten Logger-Zugriff ohne getInstance() schreiben zu müssen.
 * @note Diese Makros sind nur verfügbar, wenn der Logger bereits initialisiert wurde.
 */

/// @brief Audio-Zeile im printf-Stil, gleiche Ausgabe wie LOG_AUDIO_F
#define LOG_DIRECT_F(format, ...) Logger::getInstance().printf(Logger::AUDIO, format, ##__VA_ARGS__)

/// @brief Loggt eine Info-Nachricht mit Zeilenumbruch
#define LOG_INFO(msg) Logger::getInstance().println(Logger::INFO, msg)

/// @brief Loggt eine Warn-Nachricht mit Zeilenumbruch
#define LOG_WARNING(msg) Logger::getInstance().println(Logger::WARNING, msg)

/// @brief Loggt eine Fehler-Nachricht mit Zeilenumbruch
#define LOG_ERROR(msg) Logger::getInstance().println(Logger::ERROR, msg)

/// @brief Loggt eine Erfolgs-Nachricht mit Zeilenumbruch
#define LOG_SUCCESS(msg) Logger::getInstance().println(Logger::SUCCESS, msg)

/// @brief Loggt eine Audio-Nachricht mit Zeilenumbruch
#define LOG_AUDIO(msg) Logger::getInstance().println(Logger::AUDIO, msg)

/// @brief Loggt eine Radio-Nachricht mit Zeilenumbruch
#define LOG_RADIO(msg) Logger::getInstance().println(Logger::RADIO, msg)

/// @brief Loggt eine Einstellungs-Nachricht mit Zeilenumbruch
#define LOG_SETTINGS(msg) Logger::getInstance().println(Logger::SETTINGS, msg)

/// @brief Loggt eine Info-Nachricht ohne Zeilenumbruch
#define LOG_INFO_PRINT(msg) Logger::getInstance().print(Logger::INFO, msg)

/// @brief Loggt eine Warn-Nachricht ohne Zeilenumbruch
#define LOG_WARNING_PRINT(msg) Logger::getInstance().print(Logger::WARNING, msg)

/// @brief Loggt eine Fehler-Nachricht ohne Zeilenumbruch
#define LOG_ERROR_PRINT(msg) Logger::getInstance().print(Logger::ERROR, msg)

/// @brief Loggt eine Erfolgs-Nachricht ohne Zeilenumbruch
#define LOG_SUCCESS_PRINT(msg) Logger::getInstance().print(Logger::SUCCESS, msg)

/// @brief Printf-Style Logging mit beliebigem Level
#define LOG_PRINTF(level, format, ...) Logger::getInstance().printf(level, format, ##__VA_ARGS__)

/// @brief Printf-Style Info-Logging
#define LOG_INFO_F(format, ...) Logger::getInstance().printf(Logger::INFO, format, ##__VA_ARGS__)

/// @brief Zeile ohne Tag im printf-Stil, Zeilenende hängt der Logger an
#define LOG_PLAIN_F(format, ...) Logger::getInstance().printfPlain(format, ##__VA_ARGS__)

/// @brief Printf-Style Warn-Logging
#define LOG_WARNING_F(format, ...) Logger::getInstance().printf(Logger::WARNING, format, ##__VA_ARGS__)

/// @brief Printf-Style Fehler-Logging
#define LOG_ERROR_F(format, ...) Logger::getInstance().printf(Logger::ERROR, format, ##__VA_ARGS__)

/// @brief Printf-Style Erfolgs-Logging
#define LOG_SUCCESS_F(format, ...) Logger::getInstance().printf(Logger::SUCCESS, format, ##__VA_ARGS__)

/// @brief Printf-Style Audio-Logging
#define LOG_AUDIO_F(format, ...) Logger::getInstance().printf(Logger::AUDIO, format, ##__VA_ARGS__)

/// @brief Printf-Style Radio-Logging
#define LOG_RADIO_F(format, ...) Logger::getInstance().printf(Logger::RADIO, format, ##__VA_ARGS__)

/// @brief Printf-Style Einstellungs-Logging
#define LOG_SETTINGS_F(format, ...) Logger::getInstance().printf(Logger::SETTINGS, format, ##__VA_ARGS__)

/// @brief Custom-Tag mit Zeilenumbruch (Standardfarbe: Cyan)
#define LOG_CUSTOM(tag, msg) Logger::getInstance().printlnCustom(tag, msg)

/// @brief Custom-Tag mit Zeilenumbruch und eigener Farbe
#define LOG_CUSTOM_COLOR(tag, msg, color) Logger::getInstance().printlnCustom(tag, msg, color)

#else

/**
 * @brief Stub für deaktiviertes Logging (LOGGER_ENABLED = 0).
 *
 * Diese Variante hält die öffentliche API des Loggers verfügbar, erzeugt aber keine Ausgabe und benötigt keine Logger-Implementierung im aktiven Codepfad.
 */
class Logger
{
public:
    /**
     * @enum Level
     * @brief Vordefinierte Logging-Level zur strukturierten Log-Ausgabe.
     */
    enum Level
    {
        INFO,
        WARNING,
        ERROR,
        SUCCESS,
        CUSTOM,
        BITRATE,
        TITEL,
        SENDER,
        AUDIO,
        RADIO,
        SETTINGS
    };

    /**
     * @struct Tag
     * @brief Definition für ein einzelnes Log-Tag mit Name und Farbe.
     */
    struct Tag
    {
        const char *name;  ///< Anzeigename des Tags
        const char *color; ///< ANSI-Farbe als Escape-String
    };

    static constexpr const char *BLACK = "";           ///< Schwarz
    static constexpr const char *RED = "";             ///< Rot
    static constexpr const char *GREEN = "";           ///< Grün
    static constexpr const char *YELLOW = "";          ///< Gelb
    static constexpr const char *BLUE = "";            ///< Blau
    static constexpr const char *MAGENTA = "";         ///< Magenta
    static constexpr const char *CYAN = "";            ///< Cyan
    static constexpr const char *WHITE = "";           ///< Weiß
    static constexpr const char *BRIGHT_BLACK = "";    ///< Hell-Schwarz / Grau
    static constexpr const char *BRIGHT_RED = "";      ///< Hellrot
    static constexpr const char *BRIGHT_GREEN = "";    ///< Hellgrün
    static constexpr const char *BRIGHT_YELLOW = "";   ///< Hellgelb
    static constexpr const char *BRIGHT_BLUE = "";     ///< Hellblau
    static constexpr const char *BRIGHT_MAGENTA = "";  ///< Hellmagenta
    static constexpr const char *BRIGHT_CYAN = "";     ///< Hellcyan
    static constexpr const char *BRIGHT_WHITE = "";    ///< Hellweiß
    static constexpr const char *RESET = "";           ///< Setzt die Textfarbe zurück

    static const Tag TAGS[11];

    // Die Methoden dieses Stubs haben dieselbe Signatur wie oben und tun nichts, init() setzt nur den Initialisierungszustand.
    static Logger &init(bool enabled = true, bool withTime = false, size_t bufferSize = 512)
    {
        (void)enabled;
        (void)withTime;
        (void)bufferSize;
        initializedFlag() = true;
        return instance();
    }

    static Logger &getInstance()
    {
        return instance();
    }

    static bool isInitialized()
    {
        return initializedFlag();
    }

    static void usbOutputWritten()
    {
    }

    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;

    ~Logger() = default;

    void setEnabled(bool enabled)
    {
        (void)enabled;
    }

    class AnswerScope
    {
    };

    void setTimeOutput(bool enabled)
    {
        (void)enabled;
    }

    void resetTime()
    {
    }

    void print(Level level, const char *msg)
    {
        (void)level;
        (void)msg;
    }

    void println(Level level, const char *msg)
    {
        (void)level;
        (void)msg;
    }

    void printf(Level level, const char *format, ...)
    {
        (void)level;
        (void)format;
    }

    void printfPlain(const char *format, ...)
    {
        (void)format;
    }

    void printlnCustom(const char *tag, const char *msg, const char *color = CYAN)
    {
        (void)tag;
        (void)msg;
        (void)color;
    }

    void printCustom(const char *tag, const char *msg, const char *color = CYAN)
    {
        (void)tag;
        (void)msg;
        (void)color;
    }

private:
    Logger() = default;

    static Logger &instance()
    {
        static Logger logger;
        return logger;
    }

    static bool &initializedFlag()
    {
        static bool initialized = false;
        return initialized;
    }
};

#define LOG_DIRECT_F(...) ((void)0)
#define LOG_INFO(...) ((void)0)
#define LOG_WARNING(...) ((void)0)
#define LOG_ERROR(...) ((void)0)
#define LOG_SUCCESS(...) ((void)0)
#define LOG_AUDIO(...) ((void)0)
#define LOG_RADIO(...) ((void)0)
#define LOG_SETTINGS(...) ((void)0)
#define LOG_INFO_PRINT(...) ((void)0)
#define LOG_WARNING_PRINT(...) ((void)0)
#define LOG_ERROR_PRINT(...) ((void)0)
#define LOG_SUCCESS_PRINT(...) ((void)0)
#define LOG_PRINTF(...) ((void)0)
#define LOG_INFO_F(...) ((void)0)
#define LOG_PLAIN_F(...) ((void)0)
#define LOG_WARNING_F(...) ((void)0)
#define LOG_ERROR_F(...) ((void)0)
#define LOG_SUCCESS_F(...) ((void)0)
#define LOG_AUDIO_F(...) ((void)0)
#define LOG_RADIO_F(...) ((void)0)
#define LOG_SETTINGS_F(...) ((void)0)
#define LOG_CUSTOM(...) ((void)0)
#define LOG_CUSTOM_COLOR(...) ((void)0)

#endif

#endif
