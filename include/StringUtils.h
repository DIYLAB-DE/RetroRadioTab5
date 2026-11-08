#ifndef STRING_UTILS_H
#define STRING_UTILS_H

#include <cctype>   // isalnum
#include <stdarg.h>
#include <stddef.h> // size_t
#include <stdio.h>  // snprintf
#include <string.h> // strncpy, strlen

namespace stringutils
{

    /**
     * @brief Kopiert höchstens N-1 Bytes und terminiert das Ziel, nullptr als Quelle erzeugt einen Leerstring. Bytekürzung kann UTF-8 teilen.
     */
    template <size_t N>
    inline void safeCopy(char (&dst)[N], const char *src)
    {
        if (N == 0)
            return;

        if (!src)
        {
            dst[0] = '\0';
            return;
        }

        strncpy(dst, src, N - 1);
        dst[N - 1] = '\0';
    }

    /**
     * @brief Kopiert höchstens dstSize-1 Bytes und terminiert das Ziel. Ungültiges Ziel wird ignoriert, nullptr als Quelle erzeugt einen Leerstring. Bytekürzung kann UTF-8 teilen.
     */
    inline void safeCopy(char *dst, size_t dstSize, const char *src)
    {
        if (!dst || dstSize == 0)
            return;

        if (!src)
        {
            dst[0] = '\0';
            return;
        }

        strncpy(dst, src, dstSize - 1);
        dst[dstSize - 1] = '\0';
    }

    /**
     * @brief Kopiert wie safeCopy, kürzt aber an einer UTF-8-Zeichengrenze: Trifft die Kürzung ein Mehrbytezeichen, entfällt es ganz. Eine passende Quelle wird unverändert übernommen.
     * @param dst Ziel, wird immer terminiert. nullptr wird ignoriert.
     * @param dstSize Größe des Ziels in Bytes einschließlich Nullterminator, 0 wird ignoriert.
     * @param src Quelltext in UTF-8, nullptr erzeugt einen Leerstring.
     */
    inline void safeCopyUtf8(char *dst, size_t dstSize, const char *src)
    {
        safeCopy(dst, dstSize, src);
        if (!dst || dstSize == 0 || !src || strnlen(dst, dstSize) < dstSize - 1)
            return;

        // Die Quelle hat mindestens dstSize-1 Bytes, src[dstSize-1] ist also lesbar. Ist es ein Folgebyte, endet das Ziel mitten im Zeichen, dessen Startbyte höchstens drei Bytes davor liegt.
        const size_t end = dstSize - 1;
        size_t start = end;
        while (start > 0 && end - start < 3 && (static_cast<unsigned char>(src[start]) & 0xC0) == 0x80)
            --start;
        if (start < end && static_cast<unsigned char>(src[start]) >= 0xC0)
            dst[start] = '\0';
    }

    /**
     * @brief Feldvariante von safeCopyUtf8 für N Bytes einschließlich Nullterminator.
     */
    template <size_t N>
    inline void safeCopyUtf8(char (&dst)[N], const char *src)
    {
        safeCopyUtf8(dst, N, src);
    }

    /**
     * @brief Hängt Bytes an einen bereits terminierten Zieltext an, nullptr oder fehlender Platz ändern nichts. Bytekürzung kann UTF-8 teilen.
     */
    template <size_t N>
    inline void safeAppend(char (&dst)[N], const char *src)
    {
        if (N == 0)
            return;

        if (!src)
            return;

        size_t len = strnlen(dst, N);
        if (len >= N - 1)
            return;

        size_t space = (N - 1) - len;
        strncpy(dst + len, src, space);
        dst[N - 1] = '\0';
    }

    /**
     * @brief Hängt Bytes an einen innerhalb dstSize terminierten Zieltext an, ungültige Zeiger oder fehlender Platz ändern nichts. Bytekürzung kann UTF-8 teilen.
     */
    inline void safeAppend(char *dst, size_t dstSize, const char *src)
    {
        if (!dst || dstSize == 0 || !src)
            return;

        size_t len = strnlen(dst, dstSize);
        if (len >= dstSize - 1)
            return;

        size_t space = (dstSize - 1) - len;
        strncpy(dst + len, src, space);
        dst[dstSize - 1] = '\0';
    }

    /**
     * @brief Formatiert mit vsnprintf in N Bytes einschließlich Nullterminator, fmt und Argumente müssen gültig sein. Kürzung wird nicht gemeldet.
     */
    template <size_t N>
    inline void safeFormat(char (&dst)[N], const char *fmt, ...)
    {
        if (N == 0)
            return;

        va_list ap;
        va_start(ap, fmt);
        vsnprintf(dst, N, fmt, ap);
        va_end(ap);
    }

    /**
     * @brief Formatiert mit vsnprintf in dstSize Bytes einschließlich Nullterminator. Ungültiges Ziel wird ignoriert, fmt und Argumente müssen gültig sein.
     */
    inline void safeFormat(char *dst, size_t dstSize, const char *fmt, ...)
    {
        if (!dst || dstSize == 0)
            return;

        va_list ap;
        va_start(ap, fmt);
        vsnprintf(dst, dstSize, fmt, ap);
        va_end(ap);
    }

    /**
     * @brief Formatiert in N Bytes. @return Benötigte Bytezahl ohne Nullterminator, auch bei gekürzter Ausgabe, 0 bei Formatierungsfehler.
     */
    template <size_t N>
    inline size_t safeFormatReturn(char (&dst)[N], const char *fmt, ...)
    {
        if (N == 0)
            return 0;

        va_list ap;
        va_start(ap, fmt);
        int ret = vsnprintf(dst, N, fmt, ap);
        va_end(ap);
        return (ret < 0) ? 0 : static_cast<size_t>(ret);
    }

    /**
     * @brief Formatiert in dstSize Bytes. @return Benötigte Bytezahl ohne Nullterminator, auch bei gekürzter Ausgabe, 0 bei ungültigem Ziel oder Formatierungsfehler.
     */
    inline size_t safeFormatReturn(char *dst, size_t dstSize, const char *fmt, ...)
    {
        if (!dst || dstSize == 0)
            return 0;

        va_list ap;
        va_start(ap, fmt);
        int ret = vsnprintf(dst, dstSize, fmt, ap);
        va_end(ap);
        return (ret < 0) ? 0 : static_cast<size_t>(ret);
    }

    /**
     * @brief URL-Kodierung für HTTP-Parameter
     * @param in Eingabetext
     * @param out Ausgabe-Buffer
     * @param outSize Größe des Ausgabepuffers
     * @details Codiert Sonderzeichen gemäß RFC 3986
     */
    inline void urlEncode(const char *in, char *out, size_t outSize)
    {
        if (!in || !out || outSize == 0)
        {
            if (out && outSize > 0)
                out[0] = '\0';
            return;
        }

        constexpr const char *hex = "0123456789ABCDEF";
        size_t pos = 0;

        while (*in && pos + 1 < outSize)
        {
            const unsigned char c = static_cast<unsigned char>(*in++);

            // Unreservierte Zeichen (RFC 3986)
            if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            {
                out[pos++] = static_cast<char>(c);
            }
            else if (c == ' ')
            {
                // Platz für "%20" prüfen
                if (pos + 3 < outSize)
                {
                    out[pos++] = '%';
                    out[pos++] = '2';
                    out[pos++] = '0';
                }
                else
                {
                    break; // Zielpuffer voll
                }
            }
            else
            {
                // Platz für "%XX" prüfen
                if (pos + 3 < outSize)
                {
                    out[pos++] = '%';
                    out[pos++] = hex[c >> 4];
                    out[pos++] = hex[c & 0x0F];
                }
                else
                {
                    break; // Zielpuffer voll
                }
            }
        }

        out[pos] = '\0';
    }

    /**
     * @brief Ersetzt UTF-8 Umlaute durch ASCII-Äquivalente
     * @param in Eingabetext (UTF-8)
     * @param out Ausgabepuffer (ASCII)
     * @param outsz Größe des Ausgabepuffers
     */
    inline void replaceUmlautsUtf8ToAscii(const char *in, char *out, size_t outsz)
    {
        if (!in || !out || outsz == 0)
        {
            if (out && outsz > 0)
                out[0] = '\0';

            return;
        }

        size_t pos = 0;
        const unsigned char *p = reinterpret_cast<const unsigned char *>(in);

        while (*p && pos + 1 < outsz)
        {
            // UTF-8 Zwei-Byte-Sequenzen für deutsche Umlaute
            if (p[0] == 0xC3 && p[1])
            {
                switch (p[1])
                {
                case 0xA4: // ä
                    if (pos + 2 < outsz)
                    {
                        out[pos++] = 'a';
                        out[pos++] = 'e';
                    }
                    p += 2;
                    continue;
                case 0x84: // Ä
                    if (pos + 2 < outsz)
                    {
                        out[pos++] = 'A';
                        out[pos++] = 'e';
                    }
                    p += 2;
                    continue;
                case 0xB6: // ö
                    if (pos + 2 < outsz)
                    {
                        out[pos++] = 'o';
                        out[pos++] = 'e';
                    }
                    p += 2;
                    continue;
                case 0x96: // Ö
                    if (pos + 2 < outsz)
                    {
                        out[pos++] = 'O';
                        out[pos++] = 'e';
                    }
                    p += 2;
                    continue;
                case 0xBC: // ü
                    if (pos + 2 < outsz)
                    {
                        out[pos++] = 'u';
                        out[pos++] = 'e';
                    }
                    p += 2;
                    continue;
                case 0x9C: // Ü
                    if (pos + 2 < outsz)
                    {
                        out[pos++] = 'U';
                        out[pos++] = 'e';
                    }
                    p += 2;
                    continue;
                case 0x9F: // ß
                    if (pos + 2 < outsz)
                    {
                        out[pos++] = 's';
                        out[pos++] = 's';
                    }
                    p += 2;
                    continue;
                default:
                    break;
                }
            }

            // ASCII-Zeichen unverändert übernehmen
            if (*p < 128)
            {
                out[pos++] = static_cast<char>(*p);
                ++p;
            }
            else
            {
                // Übrige Nicht-ASCII-Bytes werden durch '?' ersetzt
                out[pos++] = '?';
                ++p;
            }
        }

        out[pos] = '\0';
    }
}

#endif // STRING_UTILS_H