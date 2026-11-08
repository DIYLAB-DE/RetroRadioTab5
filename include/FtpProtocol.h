#ifndef FTP_PROTOCOL_H
#define FTP_PROTOCOL_H

#include "WebValidation.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>

/** @brief Grenzen, Datentypen und Textbausteine des FTP-Dienstes ohne Netz-, Karten- oder Hardwarezugriff. Per Hosttest geprüft. */
namespace FtpProtocol
{
    constexpr uint16_t ControlPort = 21;               ///< Port des Steuerkanals
    constexpr uint16_t PassiveFirst = 50000;           ///< Erster Port des Datenkanals für PASV und EPSV
    constexpr uint16_t PassiveCount = 4;               ///< Anzahl der Datenports ab PassiveFirst, sie werden der Reihe nach vergeben
    constexpr const char *User = "tab5";               ///< Fester Benutzername
    constexpr size_t PasswordLength = 12;              ///< Länge eines erzeugten Passworts
    constexpr size_t PasswordCapacity = 16;            ///< Puffer für ein Passwort bis 15 Zeichen samt Nullbyte
    constexpr size_t MaxLine = 512;                    ///< Längste angenommene Befehlszeile ohne CRLF in Byte
    constexpr size_t MaxPath = 255;                    ///< Längster Kartenpfad in Byte wie im Webportal (WebValidation::path)
    constexpr uint32_t LoginTimeoutMs = 30000;         ///< Frist von der Verbindung bis zur Anmeldung
    constexpr uint32_t IdleTimeoutMs = 300000;         ///< Trennung nach so langer Zeit ohne Befehl
    constexpr uint32_t DataConnectTimeoutMs = 15000;   ///< Wartezeit auf die Datenverbindung nach PASV/EPSV und dem Übertragungsbefehl
    constexpr uint32_t DataStallTimeoutMs = 30000;     ///< Abbruch einer Übertragung ohne Fortschritt
    constexpr uint32_t LeaseWaitMs = 1500;             ///< So lange wiederholt eine Sitzung die Anforderung der Kartensperre, solange das Webportal sie hält
    constexpr uint32_t LeaseRetryMs = 50;              ///< Abstand dieser Wiederholungen
    constexpr uint32_t UploadSettleMs = 100;           ///< Nach dem Ende des Datenstroms so lange auf ABOR oder die Trennung des Steuerkanals achten, bevor ein Upload als vollständig gilt
    constexpr unsigned MaxLoginFailures = 3;           ///< Fehlversuche je Verbindung bis zur Trennung
    constexpr size_t UploadBlock = 4096;               ///< Größter Schreibauftrag eines Uploads. Wie die Blöcke des Webuploads. SD-Karte (Slot 0) und C6 (Slot 1) teilen den SDMMC-Controller, ein langer Schreibvorgang staut die WLAN-Pakete beim C6 zu großen Stößen
    constexpr size_t ListBatch = 16;                   ///< Höchstens so viele Einträge liefert ein Listenauftrag des Dienstworkers
    constexpr size_t ListReads = 64;                   ///< Höchstens so viele Ordnereinträge liest ein Listenauftrag, auch wenn er viele davon überspringt
    constexpr size_t TreeStepEntries = 16;             ///< Höchstens so viele Einträge zählt oder löscht ein Schritt der Baumarbeit, damit der Dienstworker zwischen den Schritten andere Aufträge bedient
    constexpr size_t TreeDepth = MaxPath / 2 + 1;      ///< Ebenen der Baumarbeit. Jede Ebene verlängert den Pfad um mindestens zwei Byte, tiefer passt kein Pfad in MaxPath
    constexpr uint64_t MaxFileSize = 0xFFFFFFFFull;    ///< Größte Datei auf FAT32

    /** @brief Ergebnis eines FTP-Kartenauftrags im Dienstworker. */
    enum class Status : uint8_t
    {
        Ok,           ///< Erfolgreich
        End,          ///< Liste oder Datei vollständig gelesen
        NotFound,     ///< Pfad oder übergeordneter Ordner fehlt
        Exists,       ///< Ziel vorhanden
        Denied,       ///< Ordner nicht leer oder Eintrag schreibgeschützt
        IsDirectory,  ///< Ordner statt Datei
        NotDirectory, ///< Datei statt Ordner
        Invalid,      ///< Ungültiger Pfad, Versatz oder Ablauf
        NoSpace,      ///< Karte voll
        Io,           ///< Lese- oder Schreibfehler der Karte
        Closed,       ///< Keine offene Datei oder Liste für diesen Auftrag
        Busy,         ///< Kartensperre fehlt oder eine Audiodatei ist offen
        NoCard,       ///< Keine bereite Karte
        More          ///< Baumarbeit nicht fertig, mit TreeStep fortsetzen
    };

    /** @brief Ordnereintrag oder Ergebnis einer Abfrage. */
    struct Entry
    {
        char name[MaxPath + 1]{}; ///< Name ohne Pfad als UTF-8, bei einer Abfrage leer
        uint64_t size = 0;        ///< Größe in Byte, bei Ordnern 0
        int64_t modified = 0;     ///< Änderungszeit als Unix-Zeit (UTC), 0 unbekannt
        bool directory = false;   ///< Ordner
    };

    /** @brief Art eines FTP-Kartenauftrags. */
    enum class Kind : uint8_t
    {
        Stat,      ///< path abfragen. Ergebnis in entry
        ListOpen,  ///< Ordner path zum Auflisten öffnen
        ListNext,  ///< Nächste Einträge des offenen Ordners nach entries lesen
        ListClose, ///< Offenen Ordner schließen
        ReadOpen,  ///< Datei path ab offset zum Lesen öffnen. Größe in entry
        Read,      ///< Nächsten Block der offenen Datei nach data lesen
        WriteOpen, ///< Stagingdatei für den Zielpfad path anlegen
        Write,     ///< count Byte aus data an die Stagingdatei anhängen
        Commit,    ///< Stagingdatei schließen und path damit anlegen oder ersetzen
        Close,     ///< Offene Datei schließen und eine Stagingdatei verwerfen. Gilt auch ohne Kartensperre
        Mkdir,     ///< Ordner path anlegen
        Rmdir,     ///< Leeren Ordner path löschen
        RmdirHidden, ///< Ordner path ohne sichtbare Einträge löschen. Versteckte Einträge nach SystemEntries::hidden werden samt Inhalt mit entfernt, dann More bis zum Ende der Baumarbeit
        Delete,    ///< Datei path löschen
        Rename,    ///< path nach target umbenennen oder verschieben
        TreeCount, ///< Zählen aller Dateien und Ordner unter dem Ordner path beginnen, versteckte eingeschlossen
        TreeRemove, ///< Löschen des Ordners path samt Inhalt beginnen, versteckte Einträge eingeschlossen
        TreeStep,  ///< Nächsten Schritt der Baumarbeit ausführen: More, End nach dem letzten Schritt oder Fehler mit dem betroffenen Pfad in target
        TreeClose  ///< Baumarbeit ohne weiteren Kartenzugriff beenden, gilt auch ohne Kartensperre
    };

    /** @brief Kartenauftrag des FTP-Dienstes. Gehört der Sitzung, der Dienstworker bearbeitet ihn synchron. Puffer und Liste liegen im PSRAM. */
    struct Job
    {
        Kind kind = Kind::Stat;                       ///< Art des Auftrags
        char path[MaxPath + 1]{}, target[MaxPath + 1]{}; ///< Kanonischer Kartenpfad und bei Rename das neue Ziel, nach einem Fehler der Baumarbeit der betroffene Pfad in target
        uint64_t offset = 0;                          ///< Startversatz bei ReadOpen
        uint32_t count = 0;                           ///< Bytes in data: bei Write vom Aufrufer, bei Read vom Worker
        uint8_t *data = nullptr;                      ///< Datenpuffer
        uint32_t capacity = 0;                        ///< Größe von data
        Entry *entries = nullptr;                     ///< Ziel von ListNext
        uint16_t entryCapacity = 0, entryCount = 0;   ///< Plätze und gelieferte Einträge in entries
        uint32_t skipped = 0;                         ///< Seit ListOpen übersprungene Einträge: Pfad über 255 Byte oder ungültiger Name
        uint32_t files = 0, folders = 0;              ///< Baumarbeit: bisher gezählte bzw. gelöschte Dateien und Ordner unter dem Startordner, ohne ihn selbst
        Entry entry;                                  ///< Ergebnis von Stat und ReadOpen
        Status status = Status::Io;                   ///< Ergebnis
    };

    /** @brief Setzt den Steuerkanal byteweise zu Befehlszeilen zusammen. Telnet-Steuerfolgen (IAC, etwa vor ABOR) fallen weg, eine Zeile endet mit LF, ein CR davor wird entfernt. Zu lange Zeilen und Zeilen mit Nullbyte werden bis zum Zeilenende verworfen und einmal gemeldet. */
    class LineReader
    {
    public:
        /** @brief Ergebnis eines Bytes. */
        enum class Event : uint8_t
        {
            None,   ///< Zeile noch nicht vollständig
            Line,   ///< Eine Zeile liegt in line()
            Invalid ///< Zeile zu lang oder mit Nullbyte, verworfen
        };

        /** @brief Nimmt ein Byte auf. @return Line, sobald eine Zeile vollständig ist. Sie bleibt bis zum nächsten push() gültig. */
        Event push(uint8_t c)
        {
            if (m_complete)
                clear();
            if (m_telnet == 1)
            {
                // IAC IAC steht für ein Datenbyte 0xFF. WILL, WONT, DO und DONT tragen ein Optionsbyte.
                m_telnet = c >= 0xfb && c <= 0xfe ? 2 : 0;
                if (c != 0xff)
                    return Event::None;
            }
            else if (m_telnet == 2)
            {
                m_telnet = 0;
                return Event::None;
            }
            else if (c == 0xff)
            {
                m_telnet = 1;
                return Event::None;
            }
            if (c == '\n')
            {
                if (!m_invalid && m_length && m_line[m_length - 1] == '\r')
                    --m_length;
                const bool invalid = m_invalid || m_length > MaxLine;
                m_line[invalid ? 0 : m_length] = 0;
                m_complete = true;
                return invalid ? Event::Invalid : Event::Line;
            }
            if (m_invalid)
                return Event::None;
            // Ein Platz mehr für das CR vor dem LF.
            if (!c || m_length >= MaxLine + 1)
                m_invalid = true;
            else
                m_line[m_length++] = static_cast<char>(c);
            return Event::None;
        }

        /** @brief Liefert die zuletzt vollständige Zeile ohne Zeilenende. */
        char *line()
        {
            return m_line;
        }

        /** @brief Verwirft den Inhalt samt angefangener Zeile und überschreibt den Puffer, weil er ein Passwort enthalten kann. */
        void clear()
        {
            memset(m_line, 0, sizeof(m_line));
            m_length = 0;
            m_invalid = m_complete = false;
            m_telnet = 0;
        }

    private:
        char m_line[MaxLine + 2]{}; ///< Zeile samt möglichem CR und Nullbyte
        size_t m_length = 0;        ///< Belegte Bytes
        bool m_invalid = false;     ///< Aktuelle Zeile wird verworfen
        bool m_complete = false;    ///< line() ist vollständig, das nächste Byte beginnt eine neue Zeile
        uint8_t m_telnet = 0;       ///< 1 nach IAC, 2 vor einem Optionsbyte
    };

    /** @brief Zerlegt eine Befehlszeile. Der Befehl besteht aus drei oder vier ASCII-Buchstaben und wird in Großbuchstaben kopiert. Der Parameter ist der Rest nach genau einem Leerzeichen und darf weitere Leerzeichen enthalten, etwa in Dateinamen. @param verb Ziel des Befehls. @param argument Zeiger auf den Parameter in line, leer ohne Parameter. @return false bei ungültigem Befehl. */
    inline bool splitCommand(const char *line, char (&verb)[5], const char *&argument)
    {
        size_t n = 0;
        for (; line[n] && line[n] != ' '; ++n)
        {
            const char c = line[n];
            if (n >= 4 || !((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')))
                return false;
            verb[n] = c >= 'a' ? static_cast<char>(c - 32) : c;
        }
        if (n < 3)
            return false;
        verb[n] = 0;
        argument = line[n] ? line + n + 1 : line + n;
        return true;
    }

    /** @brief Bildet aus Arbeitsordner und FTP-Pfad einen kanonischen Kartenpfad. Absolute Pfade beginnen mit „/“, sonst gilt der Arbeitsordner. „.“ und leere Bestandteile entfallen, „..“ geht einen Ordner zurück und bleibt an der Wurzel stehen. Das Ergebnis muss WebValidation::path genügen: höchstens 255 Byte, gültiges UTF-8, keine nach SystemEntries ausgeblendeten Namen. @param cwd Kanonischer Arbeitsordner. @param argument Pfad aus dem Befehl, leer für den Arbeitsordner. @param out Ziel, bei false leer. @return false bei ungültigem oder zu langem Pfad. */
    inline bool resolvePath(const char *cwd, const char *argument, char (&out)[MaxPath + 1])
    {
        size_t length = 0;
        out[0] = 0;
        auto apply = [&](const char *p)
        {
            while (*p)
            {
                while (*p == '/')
                    ++p;
                const char *part = p;
                while (*p && *p != '/')
                    ++p;
                const size_t n = static_cast<size_t>(p - part);
                if (!n || (n == 1 && part[0] == '.'))
                    continue;
                if (n == 2 && part[0] == '.' && part[1] == '.')
                {
                    while (length && out[length - 1] != '/')
                        --length;
                    if (length)
                        --length;
                    out[length] = 0;
                    continue;
                }
                if (length + 1 + n > MaxPath)
                    return false;
                out[length++] = '/';
                memcpy(out + length, part, n);
                length += n;
                out[length] = 0;
            }
            return true;
        };
        if ((argument[0] != '/' && !apply(cwd)) || !apply(argument))
        {
            out[0] = 0;
            return false;
        }
        if (!length)
        {
            out[0] = '/';
            out[1] = 0;
        }
        if (!WebValidation::path(out, true))
        {
            out[0] = 0;
            return false;
        }
        return true;
    }

    /** @brief Liefert den letzten Bestandteil eines kanonischen Pfads, für „/“ den Pfad selbst. */
    inline const char *baseName(const char *path)
    {
        const char *slash = strrchr(path, '/');
        return slash && slash[1] ? slash + 1 : path;
    }

    /** @brief Liest den Versatz von REST: nur Dezimalziffern, höchstens MaxFileSize. @return false bei ungültigem Wert. */
    inline bool parseOffset(const char *text, uint64_t &out)
    {
        if (!text || !*text)
            return false;
        uint64_t value = 0;
        for (const char *p = text; *p; ++p)
        {
            if (*p < '0' || *p > '9')
                return false;
            value = value * 10 + static_cast<uint64_t>(*p - '0');
            if (value > MaxFileSize)
                return false;
        }
        out = value;
        return true;
    }

    /** @brief Prüft, ob die Gegenstelle im eigenen IPv4-Netz liegt. @param mask Netzmaske. Ohne Maske oder ohne eigene Adresse gilt niemand. */
    inline bool sameSubnet(const uint8_t (&peer)[4], const uint8_t (&own)[4], const uint8_t (&mask)[4])
    {
        if (!(mask[0] | mask[1] | mask[2] | mask[3]) || !(own[0] | own[1] | own[2] | own[3]))
            return false;
        for (int i = 0; i < 4; ++i)
            if ((peer[i] & mask[i]) != (own[i] & mask[i]))
                return false;
        return true;
    }

    /** @brief Schreibt die Antwort auf PASV. @return false, wenn der Puffer nicht reicht. */
    inline bool passiveReply(const uint8_t (&ip)[4], uint16_t port, char *out, size_t capacity)
    {
        const int n = snprintf(out, capacity, "227 Entering Passive Mode (%u,%u,%u,%u,%u,%u).\r\n", ip[0], ip[1], ip[2], ip[3], port >> 8, port & 0xffu);
        return n > 0 && static_cast<size_t>(n) < capacity;
    }

    /** @brief Schreibt die Antwort auf EPSV. @return false, wenn der Puffer nicht reicht. */
    inline bool extendedPassiveReply(uint16_t port, char *out, size_t capacity)
    {
        const int n = snprintf(out, capacity, "229 Entering Extended Passive Mode (|||%u|).\r\n", port);
        return n > 0 && static_cast<size_t>(n) < capacity;
    }

    /** @brief Schreibt eine Unix-Zeit als UTC im Format YYYYMMDDHHMMSS für MDTM und die Angabe modify. @return false ohne bekannte Zeit. */
    inline bool timestamp(int64_t time, char (&out)[15])
    {
        if (time <= 0)
            return false;
        const time_t value = static_cast<time_t>(time);
        tm utc{};
        if (!gmtime_r(&value, &utc))
            return false;
        snprintf(out, sizeof(out), "%04u%02u%02u%02u%02u%02u", static_cast<unsigned>(utc.tm_year + 1900) % 10000u, static_cast<unsigned>(utc.tm_mon + 1) % 100u, static_cast<unsigned>(utc.tm_mday) % 100u, static_cast<unsigned>(utc.tm_hour) % 100u, static_cast<unsigned>(utc.tm_min) % 100u, static_cast<unsigned>(utc.tm_sec) % 100u);
        return true;
    }

    /** @brief Schreibt eine Zeile für MLSD oder MLST nach RFC 3659 mit den Angaben type, size (nur Dateien) und modify (UTC). @param name Bei MLSD der Name, bei MLST der Pfad. @param mlst true stellt wie bei MLST ein Leerzeichen voran. @return Länge, 0 wenn der Puffer nicht reicht. */
    inline size_t factsLine(const Entry &entry, const char *name, bool mlst, char *out, size_t capacity)
    {
        char stamp[15], size[32] = "", modify[32] = "";
        if (!entry.directory)
            snprintf(size, sizeof(size), "size=%llu;", static_cast<unsigned long long>(entry.size));
        if (timestamp(entry.modified, stamp))
            snprintf(modify, sizeof(modify), "modify=%s;", stamp);
        const int n = snprintf(out, capacity, "%stype=%s;%s%s %s\r\n", mlst ? " " : "", entry.directory ? "dir" : "file", size, modify, name);
        return n > 0 && static_cast<size_t>(n) < capacity ? static_cast<size_t>(n) : 0;
    }

    /** @brief Schreibt eine Zeile für LIST im verbreiteten Unix-Format mit Ortszeit. Zeiten, die mehr als rund sechs Monate von now abweichen, zeigen das Jahr statt der Uhrzeit. @param now Aktuelle Unix-Zeit. @return Länge, 0 wenn der Puffer nicht reicht. */
    inline size_t listLine(const Entry &entry, const char *name, int64_t now, char *out, size_t capacity)
    {
        static constexpr const char *Months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
        constexpr int64_t HalfYear = 182LL * 24 * 3600;
        const time_t value = static_cast<time_t>(entry.modified > 0 ? entry.modified : 0);
        tm local{};
        if (!localtime_r(&value, &local))
            local = tm{};
        char when[16];
        const int64_t distance = now > entry.modified ? now - entry.modified : entry.modified - now;
        if (distance < HalfYear)
            snprintf(when, sizeof(when), "%s %2d %02d:%02d", Months[local.tm_mon % 12], local.tm_mday, local.tm_hour, local.tm_min);
        else
            snprintf(when, sizeof(when), "%s %2d  %04d", Months[local.tm_mon % 12], local.tm_mday, (local.tm_year + 1900) % 10000);
        const int n = snprintf(out, capacity, "%s 1 tab5 tab5 %llu %s %s\r\n", entry.directory ? "drwxr-xr-x" : "-rw-r--r--", static_cast<unsigned long long>(entry.size), when, name);
        return n > 0 && static_cast<size_t>(n) < capacity ? static_cast<size_t>(n) : 0;
    }

    /** @brief Erzeugt ein Passwort aus PasswordLength Zeichen ohne leicht verwechselbare Zeichen (0, O, 1, I, l). Jedes Zeichen ist gleich wahrscheinlich. @param random Zufallsquelle mit 32 Bit je Aufruf. */
    inline void generatePassword(char (&out)[PasswordCapacity], uint32_t (*random)())
    {
        static constexpr char Alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";
        constexpr unsigned Size = sizeof(Alphabet) - 1;
        constexpr unsigned Limit = 256 / Size * Size; // Bytes ab Limit verworfen, damit kein Zeichen häufiger vorkommt
        size_t length = 0;
        while (length < PasswordLength)
            for (uint32_t value = random(), i = 0; i < 4 && length < PasswordLength; ++i, value >>= 8)
                if ((value & 0xffu) < Limit)
                    out[length++] = Alphabet[(value & 0xffu) % Size];
        out[length] = 0;
    }

    /** @brief Prüft ein gespeichertes Passwort: 8 bis 15 sichtbare ASCII-Zeichen ohne Leerzeichen. */
    inline bool validPassword(const char *password)
    {
        if (!password)
            return false;
        const size_t length = strnlen(password, PasswordCapacity);
        if (length < 8 || length >= PasswordCapacity)
            return false;
        for (size_t i = 0; i < length; ++i)
            if (password[i] <= ' ' || password[i] > '~')
                return false;
        return true;
    }

    /** @brief Vergleicht eine Eingabe mit dem Passwort in gleichbleibender Zeit über die ersten PasswordCapacity Zeichen. @return true nur bei gleicher Länge und gleichem Inhalt. */
    inline bool equalSecret(const char *input, const char *secret)
    {
        const size_t a = strnlen(input, MaxLine + 1), b = strnlen(secret, PasswordCapacity);
        unsigned difference = static_cast<unsigned>(a ^ b) | (b == 0);
        for (size_t i = 0; i < PasswordCapacity; ++i)
            difference |= static_cast<unsigned char>(i < a ? input[i] : 0) ^ static_cast<unsigned char>(i < b ? secret[i] : 0);
        return difference == 0;
    }

    /** @brief Überschreibt einen Puffer so, dass der Compiler das Schreiben nicht weglässt. */
    inline void wipe(void *data, size_t length)
    {
        volatile unsigned char *p = static_cast<volatile unsigned char *>(data);
        while (length--)
            *p++ = 0;
    }
}
#endif
