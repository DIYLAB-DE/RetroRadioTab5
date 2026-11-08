#include "AppServices.h"
#include "SdPlayerTab5Layout.h"
#include "Logger.h"
#include "Thema.h"
#include <SD_MMC.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
    constexpr int CoverW = SdPlayerTab5Layout::Cover::W, CoverH = SdPlayerTab5Layout::Cover::H; ///< Natives Coverfeld, Original 230x220
    constexpr size_t MaxCoverBytes = 1000000;                                                   ///< Größte Bilddatei bzw. eingebettete Bildlänge, Größe von m_coverBytes. In Bedienungstexten 1 MB
    constexpr char FolderCoverName[] = "folder.jpg";                                            ///< Ordnerbild im Titelordner. FATFS vergleicht Namen ohne Beachtung der Groß- und Kleinschreibung, Folder.JPG passt ebenso

    /** @brief Liest length Bytes ab der aktuellen Dateiposition in Blöcken von 4 KiB und gibt anderen Tasks dazwischen Zeit. @return Alle Bytes gelesen. */
    bool readBytes(File &file, uint8_t *target, size_t length)
    {
        size_t read = 0;
        while (read < length)
        {
            const size_t n = file.read(target + read, std::min<size_t>(4096, length - read));
            if (!n)
                return false;
            read += n;
            vTaskDelay(1);
        }
        return true;
    }
}

bool AppServices::requestCover(const CoverRequest &request)
{
    if (!m_coverRequests || request.mediaGeneration != mediaStatus().mediaGeneration || !FileExplorer::supportedFile(request.path))
        return false;
    return xQueueOverwrite(m_coverRequests, &request) == pdPASS;
}

bool AppServices::takeCover(SdCoverFrame &frame)
{
    if (!m_coverReady.load(std::memory_order_acquire))
        return false;
    frame = m_coverFrame;
    return true;
}

void AppServices::decodeCover()
{
    m_coverFrame = {};
    m_coverFrame.revision = ++m_coverRevision;
    m_coverFrame.mediaGeneration = m_coverWork.mediaGeneration;
    m_coverFrame.playbackId = m_coverWork.playbackId;
    m_coverFrame.trackId = m_coverWork.trackId;
    auto finish = [this](SdCoverFrame::State state, bool folder)
    {
        m_coverFrame.state = state;
        if (state == SdCoverFrame::State::Ready)
        {
            m_coverFrame.pixels = m_coverPixels;
            m_coverFrame.width = CoverW;
            m_coverFrame.height = CoverH;
            m_coverFrame.pixelCount = CoverW * CoverH;
            m_coverFrame.folder = folder;
        }
        m_coverReady.store(true, std::memory_order_release);
    };

    // Zuerst das Ordnerbild. Das Ergebnis desselben Ordners auf derselben Karte gilt ohne erneutes Lesen. Ein fehlendes Bild wird bei jedem Titel neu gesucht, damit ein nachträglich hochgeladenes sofort zählt.
    char folder[sizeof(m_folderCover.path)]{};
    const char *slash = strrchr(m_coverWork.path, '/');
    const size_t folderLength = slash ? static_cast<size_t>(slash - m_coverWork.path) + 1 : 0;
    if (folderLength && folderLength + sizeof(FolderCoverName) <= sizeof(folder))
    {
        memcpy(folder, m_coverWork.path, folderLength);
        memcpy(folder + folderLength, FolderCoverName, sizeof(FolderCoverName));
    }
    const bool known = folder[0] && m_folderCover.mediaGeneration == m_coverWork.mediaGeneration && !strcmp(m_folderCover.path, folder);
    if (known && m_folderCover.usable && m_folderCover.inPixels)
    {
        finish(SdCoverFrame::State::Ready, true);
        return;
    }
    if (folder[0] && !(known && !m_folderCover.usable) && SD_MMC.exists(folder))
    {
        File file = SD_MMC.open(folder, FILE_READ);
        const size_t size = file && !file.isDirectory() ? file.size() : 0;
        const bool ok = size >= 100 && size <= MaxCoverBytes && readBytes(file, m_coverBytes, size) && decodeImage(m_coverBytes, size, false);
        file.close();
        memcpy(m_folderCover.path, folder, sizeof(folder));
        m_folderCover.mediaGeneration = m_coverWork.mediaGeneration;
        m_folderCover.usable = m_folderCover.inPixels = ok;
        LOG_INFO_F("SD Cover: Ordnerbild %s, %lu Bytes", ok ? "bereit" : "unbrauchbar", static_cast<unsigned long>(size));
        if (ok)
        {
            finish(SdCoverFrame::State::Ready, true);
            return;
        }
    }

    // Danach das eingebettete Bild, sofern audio-lite eines gemeldet hat.
    const auto &e = m_coverWork.event;
    if (!e.count)
    {
        finish(SdCoverFrame::State::Missing, false);
        return;
    }
    if (e.count > 16 || e.count % 2)
    {
        finish(SdCoverFrame::State::Error, false);
        return;
    }
    File file = SD_MMC.open(m_coverWork.path, FILE_READ);
    if (!file || file.isDirectory())
    {
        finish(SdCoverFrame::State::Error, false);
        return;
    }
    // Nur das erste Offset-/Längenpaar wird gelesen. Innerhalb dieses Blocks beginnt die Dekodierung an der ersten JPEG- oder PNG-Signatur.
    const uint32_t offset = e.offsets[0], length = e.offsets[1];
    if (length < 100 || length > MaxCoverBytes || offset > file.size() || length > file.size() - offset || !file.seek(offset) || !readBytes(file, m_coverBytes, length))
    {
        finish(SdCoverFrame::State::Error, false);
        return;
    }
    file.close();
    const bool ok = decodeImage(m_coverBytes, length, true);
    if (ok)
        m_folderCover.inPixels = false;
    finish(ok ? SdCoverFrame::State::Ready : SdCoverFrame::State::Error, false);
}

bool AppServices::decodeImage(const uint8_t *data, size_t length, bool allowPng)
{
    size_t start = 0;
    bool png = false, jpg = false;
    const uint8_t sig[] = {137, 80, 78, 71, 13, 10, 26, 10};
    for (; start + 8 <= length; ++start)
    {
        png = allowPng && !memcmp(data + start, sig, 8);
        jpg = data[start] == 255 && data[start + 1] == 216 && data[start + 2] == 255;
        if (png || jpg)
            break;
    }
    if (!png && !jpg)
        return false;
    const auto *image = data + start;
    const size_t size = length - start;
    uint32_t width = 0, height = 0;
    if (png)
    {
        auto be = [](const uint8_t *p)
        {
            return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
        };
        if (size < 33 || be(image + 8) != 13 || memcmp(image + 12, "IHDR", 4))
            return false;
        width = be(image + 16);
        height = be(image + 20);
        if (width > 500 || height > 500)
            return false;
    }
    else
    {
        size_t pos = 2;
        while (pos + 4 <= size)
        {
            if (image[pos++] != 255)
                break;
            while (pos < size && image[pos] == 255)
                ++pos;
            if (pos >= size)
                break;
            uint8_t marker = image[pos++];
            if (marker == 217 || marker == 218)
                break;
            if (marker == 1 || (marker >= 208 && marker <= 215))
                continue;
            if (pos + 2 > size)
                break;
            uint16_t len = uint16_t(image[pos]) << 8 | image[pos + 1];
            if (len < 2 || len > size - pos)
                break;
            if ((marker >= 192 && marker <= 195) && len >= 8)
            {
                height = uint16_t(image[pos + 3]) << 8 | image[pos + 4];
                width = uint16_t(image[pos + 5]) << 8 | image[pos + 6];
                break;
            }
            pos += len;
        }
        if (width > 8192 || height > 8192)
            return false;
    }
    if (!width || !height)
        return false;
    const float scale = std::max(static_cast<float>(CoverW) / width, static_cast<float>(CoverH) / height);
    const int x = (CoverW - static_cast<int>(lroundf(width * scale))) / 2, y = (CoverH - static_cast<int>(lroundf(height * scale))) / 2;
    // Grund unter dem Bild wie die Spielerfläche. Der Dienst-Task liest die Rolle, das Thema steht seit dem Start fest
    m_coverCanvas.clear(farbe(Rolle::GemFlaeche));
    const bool ok = png ? m_coverCanvas.drawPng(image, size, x, y, 0, 0, 0, 0, scale, scale) : m_coverCanvas.drawJpg(image, size, x, y, 0, 0, 0, 0, scale, scale);
    // Themen mit Graustufen zeigen das Cover beim Kopieren als Abbildung zwischen Tinte und Papier, der Rand in Gem.Flaeche bleibt.
    const bool grau = themaAktiv().merkmale & MerkmalGraustufen;
    const uint16_t flaeche = farbe(Rolle::GemFlaeche), tinte = farbe(Rolle::GemTextHell), papier = farbe(Rolle::GemGrund);
    if (ok)
        for (int py = 0; py < CoverH; ++py)
            for (int px = 0; px < CoverW; ++px)
            {
                const uint16_t pixel = m_coverCanvas.readPixel(px, py);
                m_coverPixels[py * CoverW + px] = grau && pixel != flaeche ? bildGrau(pixel, tinte, papier) : pixel;
            }
    LOG_INFO_F("SD Cover: %s, %lux%lu", ok ? "bereit" : "Fehler", static_cast<unsigned long>(width), static_cast<unsigned long>(height));
    return ok;
}
