#ifndef WEB_VALIDATION_H
#define WEB_VALIDATION_H

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <climits>
#include "SystemEntries.h"

/** @brief Begrenzte Eingabeprüfung des Tab5-Portals ohne Hardware- oder Speicherzugriff. */
namespace WebValidation
{
    constexpr size_t MaxBody = 32768;                  ///< Größter angenommener Anfragetext in Bytes
    constexpr uint32_t MaxUpload = 32 * 1024 * 1024;   ///< Größte angenommene Uploaddatei in Bytes
    constexpr unsigned MaxJsonDepth = 8;               ///< Größte angenommene Verschachtelung von Objekten und Feldern im JSON

    /** @brief Prüft ohne Rekursion, dass Objekte und Felder höchstens maximum Ebenen tief verschachtelt sind. Klammern in Strings zählen nicht. Läuft vor cJSON, dessen rekursiver Parser bei einigen Hundert Ebenen den Stack des HTTP-Tasks überläuft. @param text Terminierter JSON-Text. @param maximum Größte zulässige Tiefe. @return true, wenn die Tiefe nirgends überschritten wird. Über die Gültigkeit des JSON entscheidet erst der Parser. */
    inline bool jsonDepth(const char *text, unsigned maximum = MaxJsonDepth)
    {
        if (!text)
            return false;
        unsigned depth = 0;
        bool string = false;
        for (const char *p = text; *p; ++p)
        {
            if (string)
            {
                if (*p == '\\' && p[1])
                    ++p;
                else if (*p == '"')
                    string = false;
            }
            else if (*p == '"')
                string = true;
            else if (*p == '[' || *p == '{')
            {
                if (++depth > maximum)
                    return false;
            }
            else if ((*p == ']' || *p == '}') && depth)
                --depth;
        }
        return true;
    }

    constexpr const char *UploadPrefix = ".rr-web-"; ///< Namensanfang der Staging- und Sicherungsdateien des Datei-Managers

    /** @brief Erkennt ausschließlich die eigenen Reste des Datei-Managers, UploadPrefix + 8 Hexziffern + ".tmp" (Stagingdatei) oder ".old" (Sicherung beim Ersetzen), für die Bereinigung beim Mounten. @param name Dateiname ohne Pfad. @return true bei genau diesem Muster. */
    inline bool temporaryUpload(const char *name)
    {
        if (!name || strlen(name) != 20 || strncmp(name, UploadPrefix, 8) || (strcmp(name + 16, ".tmp") && strcmp(name + 16, ".old")))
            return false;
        for (size_t i = 8; i < 16; ++i)
            if (!((name[i] >= '0' && name[i] <= '9') || (name[i] >= 'a' && name[i] <= 'f')))
                return false;
        return true;
    }

    /** @brief Prüft kanonisches UTF-8 ohne Steuerzeichen, Überlängen oder unvollständige Zeichen. @param value Terminierter Text. @param capacity Größe des Puffers einschließlich Nullterminator. Ein Text ohne Terminator darin ist ungültig. @return true bei gültigem Text, auch leer. */
    inline bool text(const char *value, size_t capacity)
    {
        if (!value || !capacity)
            return false;
        const size_t length = strnlen(value, capacity);
        if (length >= capacity)
            return false;
        for (size_t i = 0; i < length;)
        {
            uint32_t c = static_cast<unsigned char>(value[i++]);
            if (c < 0x20 || c == 0x7f)
                return false;
            if (c < 0x80)
                continue;
            unsigned rest;
            uint32_t minimum;
            if (c >= 0xc2 && c <= 0xdf)
            {
                rest = 1;
                minimum = 0x80;
                c &= 0x1f;
            }
            else if (c >= 0xe0 && c <= 0xef)
            {
                rest = 2;
                minimum = 0x800;
                c &= 0x0f;
            }
            else if (c >= 0xf0 && c <= 0xf4)
            {
                rest = 3;
                minimum = 0x10000;
                c &= 7;
            }
            else
                return false;
            if (i + rest > length)
                return false;
            while (rest--)
            {
                const auto b = static_cast<unsigned char>(value[i++]);
                if ((b & 0xc0) != 0x80)
                    return false;
                c = (c << 6) | (b & 0x3f);
            }
            if (c < minimum || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff) || (c >= 0x80 && c <= 0x9f))
                return false;
        }
        return true;
    }

    /** @brief Erlaubt nur absolute Kartenpfade bis 255 Byte. Ein Bestandteil darf den ganzen Rest belegen, weil FAT lange Namen bis 255 Zeichen kennt. Kein Bestandteil darf nach SystemEntries::systemName ausgeblendet sein (führender Punkt, auch „.“ und „..“, oder Systemname), damit Portal und FTP nichts ansprechen oder anlegen, was ihre Listen nicht zeigen. @param value Pfad. @param root true, wenn die Wurzel „/“ selbst zulässig ist. @return true bei gültigem Pfad. */
    inline bool path(const char *value, bool root = true)
    {
        if (!text(value, 256) || value[0] != '/' || strchr(value, '\\') || strchr(value, ':'))
            return false;
        if (!value[1])
            return root;
        const char *part = value + 1;
        for (const char *p = part;; ++p)
            if (*p == '/' || !*p)
            {
                const size_t n = p - part;
                if (!n || SystemEntries::systemName(part, n) || part[n - 1] == ' ' || part[n - 1] == '.')
                    return false;
                for (size_t i = 0; i < n; ++i)
                    if (strchr("*?<>|\"", part[i]))
                        return false;
                if (!*p)
                    break;
                part = p + 1;
            }
        return true;
    }

    /** @brief Liest eine vollständige Dezimalzahl ohne stilles Abschneiden, Leerzeichen oder Überlauf. @param value Text. @param minimum Kleinster zulässiger Wert. @param maximum Größter zulässiger Wert. @param out Ergebnis, bei false unverändert. @return true bei gültiger Zahl im Bereich. */
    inline bool integer(const char *value, int minimum, int maximum, int &out)
    {
        if (!value || !*value)
            return false;
        bool negative = *value == '-';
        if (negative && !*++value)
            return false;
        int64_t number = 0;
        for (; *value; ++value)
        {
            if (*value < '0' || *value > '9')
                return false;
            number = number * 10 + (*value - '0');
            if (number > INT_MAX)
                return false;
        }
        if (negative)
            number = -number;
        if (number < minimum || number > maximum)
            return false;
        out = static_cast<int>(number);
        return true;
    }

    /** @brief Dekodiert genau ein Formularfeld, doppelte Schlüssel und eingebettete Nullbytes sind Fehler. @param body URL-kodierter Anfragetext. @param key Gesuchter Schlüssel. @param out Zielpuffer für den dekodierten Wert. @param capacity Größe des Zielpuffers. @return true bei genau einem gültigen Vorkommen. */
    inline bool form(const char *body, const char *key, char *out, size_t capacity)
    {
        if (!body || !key || !out || !capacity)
            return false;
        out[0] = 0;
        bool found = false;
        for (const char *p = body; *p;)
        {
            const char *end = strchr(p, '&');
            if (!end)
                end = p + strlen(p);
            const char *equal = static_cast<const char *>(memchr(p, '=', end - p));
            if (!equal)
                return false;
            if (static_cast<size_t>(equal - p) == strlen(key) && !memcmp(p, key, equal - p))
            {
                if (found)
                    return false;
                found = true;
                size_t used = 0;
                for (const char *v = equal + 1; v < end; ++v)
                {
                    unsigned char c = *v;
                    if (c == '+')
                        c = ' ';
                    else if (c == '%')
                    {
                        if (end - v < 3)
                            return false;
                        auto hex = [](char h) -> int
                        {
                            return h >= '0' && h <= '9' ? h - '0' : h >= 'a' && h <= 'f' ? h - 'a' + 10
                                                                : h >= 'A' && h <= 'F'   ? h - 'A' + 10
                                                                                         : -1;
                        };
                        int a = hex(v[1]), b = hex(v[2]);
                        if (a < 0 || b < 0)
                            return false;
                        c = (a << 4) | b;
                        v += 2;
                    }
                    if (!c || used + 1 >= capacity)
                        return false;
                    out[used++] = c;
                }
                out[used] = 0;
            }
            p = *end ? end + 1 : end;
        }
        return found && text(out, capacity);
    }

    /** @brief Vergleicht genau length Bytes ohne Rücksicht auf ASCII-Groß- und Kleinschreibung. */
    inline bool equalIgnoreCase(const char *a, const char *b, size_t length)
    {
        for (size_t i = 0; i < length; ++i)
        {
            const char x = a[i] >= 'A' && a[i] <= 'Z' ? a[i] + 32 : a[i], y = b[i] >= 'A' && b[i] <= 'Z' ? b[i] + 32 : b[i];
            if (x != y || !x)
                return false;
        }
        return true;
    }

    /** @brief Prüft den Host-Kopf gegen die eigenen Namen des Geräts, damit eine fremde Seite per DNS-Rebinding keine Anfragen stellen kann. @param host Wert des Host-Kopfs. @param ip Eigene IPv4-Adresse, 0.0.0.0 gilt nicht. @param hostname Eigener Hostname. @return true für die IPv4 in Punktschreibweise, den Hostnamen oder „<hostname>.local“ ohne Rücksicht auf Groß- und Kleinschreibung, jeweils ohne oder mit Port 1..65535. */
    inline bool ownHost(const char *host, const uint8_t ip[4], const char *hostname)
    {
        if (!host || !ip || !hostname || !hostname[0])
            return false;
        const char *colon = strchr(host, ':');
        const size_t length = colon ? static_cast<size_t>(colon - host) : strlen(host);
        int port = 0;
        if (colon && (colon[1] == '0' || !integer(colon + 1, 1, 65535, port)))
            return false;
        auto equal = [&](const char *name, const char *suffix)
        {
            const size_t a = strlen(name), b = strlen(suffix);
            return length == a + b && equalIgnoreCase(host, name, a) && equalIgnoreCase(host + a, suffix, b);
        };
        char address[16];
        snprintf(address, sizeof(address), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
        return ((ip[0] | ip[1] | ip[2] | ip[3]) && equal(address, "")) || equal(hostname, "") || equal(hostname, ".local");
    }

    /** @brief Prüft den Origin-Kopf einer schreibenden Anfrage auf gleiche Herkunft. @param origin Wert des Origin-Kopfs. @param host Host-Kopf derselben Anfrage, vorher mit ownHost() geprüft. @return true bei genau „http://“ und diesem Host ohne Rücksicht auf Groß- und Kleinschreibung. */
    inline bool sameOrigin(const char *origin, const char *host)
    {
        if (!origin || !host || strncmp(origin, "http://", 7))
            return false;
        const size_t length = strlen(host);
        return strlen(origin + 7) == length && equalIgnoreCase(origin + 7, host, length);
    }

    /** @brief Wachsende Sperre der Web-Anmeldung nach Fehlversuchen. Gilt für alle Clients gemeinsam und nur bis zum Neustart. Zeiten in ms auf einer 64-Bit-Uhr, die in der Laufzeit nicht überläuft. */
    class LoginLock
    {
    public:
        static constexpr uint32_t SpacingMs = 1000;       ///< Mindestabstand zwischen zwei Anmeldeversuchen
        static constexpr unsigned FreeFailures = 4;       ///< Fehlversuche ohne Sperre
        static constexpr uint32_t FirstLockMs = 60000;    ///< Sperre beim ersten Fehlversuch über FreeFailures, verdoppelt sich mit jedem weiteren
        static constexpr uint32_t MaxLockMs = 3600000;    ///< Obergrenze der Sperre

        /** @brief @return Wartezeit in ms bis zum nächsten zulässigen Versuch, 0 wenn er jetzt erlaubt ist. */
        uint64_t remaining(uint64_t now) const
        {
            return now < m_until ? m_until - now : 0;
        }

        /** @brief Verbucht einen Fehlversuch und setzt danach den Mindestabstand oder die Sperre. @return Dauer der neuen Sperre in ms, 0 solange FreeFailures nicht überschritten ist. */
        uint32_t failed(uint64_t now)
        {
            if (m_failures < UINT16_MAX)
                ++m_failures;
            uint32_t lock = 0;
            if (m_failures > FreeFailures)
            {
                lock = FirstLockMs;
                for (unsigned i = FreeFailures + 1; i < m_failures && lock < MaxLockMs; ++i)
                    lock *= 2;
                if (lock > MaxLockMs)
                    lock = MaxLockMs;
            }
            m_until = now + (lock > SpacingMs ? lock : SpacingMs);
            return lock;
        }

        /** @brief Setzt nach erfolgreicher Anmeldung den Fehlerzähler zurück, der Mindestabstand gilt weiter. */
        void succeeded(uint64_t now)
        {
            m_failures = 0;
            m_until = now + SpacingMs;
        }

        /** @brief @return Fehlversuche seit der letzten erfolgreichen Anmeldung. */
        unsigned failures() const
        {
            return m_failures;
        }

    private:
        unsigned m_failures = 0; ///< Fehlversuche seit Start oder letzter Anmeldung
        uint64_t m_until = 0;    ///< Frühester Zeitpunkt des nächsten Versuchs
    };
}
#endif
