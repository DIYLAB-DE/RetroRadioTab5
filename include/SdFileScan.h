#ifndef SD_FILE_SCAN_H
#define SD_FILE_SCAN_H
#include "FileExplorer.h"
#include <cstddef>
#include <cstdint>

/** @brief Ordnerliste und Sammeln der Auswahl ohne Rekursion. Gehört allein dem Dienstworker. Einträge nach SystemEntries::hidden (Punktnamen, Systemnamen, FAT-Attribut versteckt oder System) erscheinen weder in der Liste noch in der Sammlung und zählen nicht als übersprungen. Einträge, deren Pfad nicht in 255 Byte passt, deren Name ungültig ist oder die tiefer als MaxDepth liegen, werden übersprungen und gezählt, statt den Auftrag abzubrechen. Zu lange Namen werden für die Anzeige gekürzt. */
class SdFileScan
{
public:
    static constexpr unsigned MaxDepth = 20;                       ///< Tiefste gelesene Ordnerebene unter einem gewählten Ordner (dieser hat Ebene 0)
    static constexpr unsigned MaxIterations = 2000;                ///< Höchstzahl geöffneter Ordner und gelesener Einträge je Auftrag, auch nicht unterstützter. Begrenzt die Lesezeit, nicht den Speicher. Darüber meldet der Auftrag Limit
    static constexpr size_t MaxPending = FileExplorer::MaxEntries; ///< Plätze des Pfadstapels

    /** @brief Ein noch zu lesender Ordner auf dem Pfadstapel. */
    struct Pending
    {
        char path[256]; ///< Absoluter Ordnerpfad
        uint8_t depth;  ///< Ebene unter dem gewählten Ordner
    };

    /** @brief Lesezugriff auf Kartenordner. Der Dienstworker bindet die Karte an, Hosttests ein Modell. Es ist höchstens ein Ordner gleichzeitig offen. */
    class Directory
    {
    public:
        /** @brief Ein gelesener Eintrag, name bleibt bis zum nächsten next() oder close() gültig. */
        struct Item
        {
            const char *name = nullptr; ///< Name ohne Pfad, UTF-8
            bool directory = false;     ///< Ordner statt Datei
            uint32_t size = 0;          ///< Dateigröße in Bytes
            bool hidden = false;        ///< FAT-Attribut versteckt oder System gesetzt
        };

        virtual ~Directory() = default;
        /** @brief Öffnet einen Ordner. @param path Absoluter Pfad. @return false, wenn er fehlt oder nicht lesbar ist. */
        virtual bool open(const char *path) = 0;
        /** @brief Liest den nächsten Eintrag des geöffneten Ordners und setzt alle Felder von item. @param item Ziel. @return false am Ende oder bei einem Lesefehler. */
        virtual bool next(Item &item) = 0;
        /** @brief Schließt den geöffneten Ordner und den zuletzt gelesenen Eintrag. */
        virtual void close() = 0;
        /** @brief Meldet einen angeforderten Abbruch des Auftrags. */
        virtual bool cancelled() = 0;
    };

    /** @brief Arbeitet in vorab angelegten Puffern ohne eigene Allokation. @param entries Ergebnisliste mit FileExplorer::MaxEntries Plätzen. @param pending Pfadstapel mit MaxPending Plätzen. */
    SdFileScan(SdFileEntry *entries, Pending *pending) : m_entries(entries), m_pending(pending)
    {
    }

    /** @brief Listet Unterordner und unterstützte Dateien eines Ordners, Ordner zuerst, jeweils nach Name. @param directory Lesezugriff. @param path Absoluter Ordnerpfad. @return Data, Error (Ordner nicht lesbar), Limit (mehr als MaxEntries unterstützte oder mehr als MaxIterations gelesene Einträge) oder Cancelled. */
    SdFileResult::State list(Directory &directory, const char *path);
    /** @brief Löst die Auswahl samt Unterordnern bis MaxDepth zu unterstützten Dateien in Baumreihenfolge auf (siehe treeOrder). @param directory Lesezugriff. @param selection Gewählte Einträge. @param count Anzahl, höchstens SdFileRequest::MaxSelection. @return Data, Error, Limit (Liste oder Pfadstapel voll, mehr als MaxIterations gelesen) oder Cancelled. */
    SdFileResult::State collect(Directory &directory, const SdFileEntry *selection, size_t count);
    /** @brief Ordnet Pfade wie einen Ordnerbaum: Bestandteile einzeln ohne Groß-/Kleinschreibung verglichen, Dateien eines Ordners vor seinen Unterordnern, jeder Unterordner zusammenhängend. @return true, wenn a vor b steht. */
    static bool treeOrder(const char *a, const char *b);

    /** @brief Liefert die Zahl gültiger Einträge des letzten Auftrags. */
    size_t count() const
    {
        return m_count;
    }

    /** @brief Liefert die Zahl übersprungener Einträge des letzten Auftrags. */
    unsigned skipped() const
    {
        return m_skipped;
    }

private:
    /** @brief Ergebnis von add(). */
    enum class Add : uint8_t
    {
        Added,   ///< Eingetragen oder schon vorhanden
        Skipped, ///< Pfad zu lang oder Name ungültig, gezählt
        Full     ///< Ergebnisliste voll
    };

    /** @brief Beginnt einen Auftrag mit leerer Liste. */
    void reset();
    /** @brief Trägt einen gelesenen Eintrag ein. Ein Pfad über 255 Byte zählt als übersprungen, ein zu langer Name wird für die Anzeige UTF-8-sicher gekürzt. @param parent Ordnerpfad. @param item Eintrag. @return Ergebnis. */
    Add add(const char *parent, const Directory::Item &item);
    /** @brief Übernimmt den vorbereiteten Platz m_entries[m_count]. Ein ungültiger Pfad oder Name zählt als übersprungen, ein vorhandener Pfad als eingetragen. */
    Add keep();
    /** @brief Liest alle Ordner des Pfadstapels und trägt deren unterstützte Dateien ein. @return Data oder der Grund des Abbruchs. */
    SdFileResult::State drain(Directory &directory);
    /** @brief Setzt parent und name zu einem Pfad zusammen. @param target Ziel mit 256 Byte. @return false, wenn der Pfad nicht in 255 Byte passt. */
    static bool join(char (&target)[256], const char *parent, const char *name);

    SdFileEntry *m_entries;   ///< Ergebnisliste
    Pending *m_pending;       ///< Pfadstapel
    size_t m_count = 0;       ///< Einträge in m_entries
    size_t m_pendingCount = 0; ///< Belegte Plätze des Pfadstapels
    unsigned m_skipped = 0;   ///< Übersprungene Einträge
    unsigned m_iterations = 0; ///< Geöffnete Ordner und gelesene Einträge
};
#endif
